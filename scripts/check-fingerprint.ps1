#!/usr/bin/env pwsh
#Requires -Version 5.1
# Read the PE fingerprint (TimeDateStamp / SizeOfImage / CheckSum) of every
# Wreckfest install on this machine and compare each against the build profiles
# in src/builds/. First thing to run when a user reports the dormant
# "unknown build" log line, and the first step of a post-patch rederive.
#
# This answers "does this EXE have a profile", and nothing more. It does not
# check whether the pinned camera slot and transform offset still hold on a new
# build - that rederive is a separate job, and the template printed below
# carries the CURRENT profile's numbers, not measurements of this EXE.

[CmdletBinding()]
param([string]$ExePath)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force

# Both stores' registries. Each file is append-only and holds every profile for
# that store; the store a running EXE belongs to decides which one a new profile
# gets appended to.
$offsetsFiles = [ordered]@{
    Steam = @{
        Display = 'Steam'
        Path = Join-Path $projectDir 'src/builds/steam_offsets.cpp'
        ConstPrefix = 'kSteamProfile_'
        NamePrefix = 'steam-win64-'
    }
    Xbox = @{
        Display = 'Xbox / Game Pass'
        Path = Join-Path $projectDir 'src/builds/gdk_offsets.cpp'
        ConstPrefix = 'kGdkProfile_'
        NamePrefix = 'gdk-win64-'
    }
}

# A Game Pass package denies reads of its own executable, so whether the bytes
# are there to parse is asked before parsing rather than inferred from the shape
# of a failure. Everything Get-PeFingerprintFromFile throws after this is a real
# fault in the file, and is meant to reach the caller.
function Test-FileReadable {
    param([Parameter(Mandatory = $true)][string]$Path)

    try {
        $stream = [System.IO.File]::Open($Path, 'Open', 'Read', 'ReadWrite')
        $stream.Close()
        return $true
    } catch {
        return $false
    }
}

function Get-PeFingerprintFromFile {
    param([Parameter(Mandatory = $true)][string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    # Every offset below is taken from the file, so each one is range-checked before
    # it is used. Without this a truncated download or a text file passed by mistake
    # comes back as a raw ArgumentException naming an array index, which tells the
    # person running this nothing about what they actually handed it.
    if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        throw "Not a PE image (no MZ header): $Path"
    }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOffset -lt 0 -or $peOffset -gt ($bytes.Length - 4)) {
        throw ("Not a PE image (PE header offset 0x{0:X} is outside the file): {1}" -f $peOffset, $Path)
    }
    if ([BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x00004550) { throw "Not a PE image: $Path" }

    $coff = $peOffset + 4
    $optHdr = $coff + 20
    # SizeOfImage sits at optional-header +56 and CheckSum at +64, so the header has
    # to reach at least +68.
    if (($optHdr + 68) -gt $bytes.Length) {
        throw "PE optional header is truncated: $Path"
    }
    return [pscustomobject]@{
        TimeDateStamp = [BitConverter]::ToUInt32($bytes, $coff + 4)
        SizeOfImage   = [BitConverter]::ToUInt32($bytes, $optHdr + 56)
        CheckSum      = [BitConverter]::ToUInt32($bytes, $optHdr + 64)
        Source        = 'PE header on disk'
    }
}

# The mod logs the running EXE's fingerprint on every launch, so where the
# package will not be read at all the log is the route to the same three
# numbers. The denial is not an ACL a user can fix: the file's own SDDL grants
# BUILTIN\Users read, and the read fails anyway, elevated or not, with the game
# running or not.
function Get-PeFingerprintFromLog {
    param([Parameter(Mandatory = $true)][string]$LogPath)

    if (-not (Test-Path $LogPath)) { return $null }
    $line = Select-String -Path $LogPath -Pattern 'running EXE fingerprint: TimeDateStamp=0x([0-9A-Fa-f]{8}) SizeOfImage=0x([0-9A-Fa-f]{8}) CheckSum=0x([0-9A-Fa-f]{8})' |
        Select-Object -Last 1
    if (-not $line) { return $null }
    return [pscustomobject]@{
        TimeDateStamp = [Convert]::ToUInt32($line.Matches[0].Groups[1].Value, 16)
        SizeOfImage   = [Convert]::ToUInt32($line.Matches[0].Groups[2].Value, 16)
        CheckSum      = [Convert]::ToUInt32($line.Matches[0].Groups[3].Value, 16)
        Source        = "HeadTracking.log ($LogPath)"
    }
}

function Get-KnownProfiles {
    param([Parameter(Mandatory = $true)][hashtable]$Store)

    $found = @()
    foreach ($k in (Select-String -Path $Store.Path -Pattern '\{\s*0x([0-9A-Fa-f]{8}),\s*0x([0-9A-Fa-f]{8}),\s*0x([0-9A-Fa-f]{8})\s*\}')) {
        $found += [pscustomobject]@{
            TimeDateStamp = [Convert]::ToUInt32($k.Matches[0].Groups[1].Value, 16)
            SizeOfImage   = [Convert]::ToUInt32($k.Matches[0].Groups[2].Value, 16)
            CheckSum      = [Convert]::ToUInt32($k.Matches[0].Groups[3].Value, 16)
            File          = $Store.Path
            LineNumber    = $k.LineNumber
        }
    }
    return $found
}

function Write-ProfileTemplate {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Store,
        [Parameter(Mandatory = $true)]$Fingerprint,
        [Parameter(Mandatory = $true)][datetime]$Built
    )

    # The offsets block is lifted verbatim out of the newest profile in the
    # store's offsets file rather than written out here. A hand-kept copy is what
    # this script shipped before, and it had drifted a whole design behind the
    # struct: it printed camera_compute_slot / camera_out_transform /
    # camera_out_transform_floats, none of which are fields of OffsetTable any
    # more, so following it produced a profile that would not compile.
    $offsetsText = Get-Content -Raw $Store.Path
    # Name, then the fingerprint braces, then the offsets braces - the second
    # inner group is the one wanted, so the fingerprint's is matched explicitly
    # rather than skipped over.
    $blocks = [regex]::Matches($offsetsText,
        '(?s)extern\s+const\s+BuildProfile\s+\w+\s*=\s*\{[^{}]*\{[^{}]*\}\s*,\s*\{(.*?)\}\s*,\s*\}\s*;')
    if ($blocks.Count -eq 0) {
        throw "Could not read an existing profile's offsets block from $($Store.Path)."
    }
    $offsetLines = $blocks[0].Groups[1].Value -split "`n" |
        Where-Object { $_.Trim() } |
        ForEach-Object { '        ' + $_.Trim() }

    Write-Host "No profile matches this EXE. Append a new profile to $($Store.Path):" -ForegroundColor Yellow
    Write-Host ""
    Write-Host ("extern const BuildProfile {0}{1:yyyyMMdd} = {{" -f $Store.ConstPrefix, $Built)
    Write-Host ("    `"{0}{1:yyyyMMdd}`"," -f $Store.NamePrefix, $Built)
    Write-Host ("    {{ 0x{0:X8}, 0x{1:X8}, 0x{2:X8} }}," -f $Fingerprint.TimeDateStamp, $Fingerprint.SizeOfImage, $Fingerprint.CheckSum)
    Write-Host "    {"
    $offsetLines | ForEach-Object { Write-Host $_ }
    Write-Host "    },"
    Write-Host "};"
    Write-Host ""
    Write-Host "Then add it to the TOP of kKnownProfiles in src/builds/build_registry.cpp."
    Write-Host "Every number in that block is COPIED from the newest existing profile, not"
    Write-Host "measured from this EXE. Rederive and confirm them before shipping. Until then"
    Write-Host "set view_manager_update_rva, camera_view_matrix_rva, view_manager_ptr_rva,"
    Write-Host "garage_camera_vtable_rva and race_session_active_rva to 0: IsProfileComplete()"
    Write-Host "checks those five (and camera_world_transform_floats), so a zero in any of them"
    Write-Host "routes the build to a profile that stays dormant."
}

$config = Get-GameConfig -GameId 'wreckfest'

if ($ExePath) {
    if (-not (Test-Path $ExePath)) { throw "EXE not found: $ExePath" }
    $targets = @([pscustomobject]@{ Exe = $ExePath; Dir = (Split-Path -Parent $ExePath) })
} else {
    $paths = @(Find-AllGamePaths -GameId 'wreckfest')
    if ($paths.Count -eq 0) { throw "Wreckfest not found. Pass -ExePath explicitly." }
    $targets = foreach ($p in $paths) {
        [pscustomobject]@{ Exe = (Join-Path $p 'Wreckfest_x64.exe'); Dir = $p }
    }
}

foreach ($target in $targets) {
    $store = if (Test-IsXboxPath -Config $config -Path $target.Dir) { $offsetsFiles.Xbox } else { $offsetsFiles.Steam }

    Write-Host ""
    Write-Host "EXE: $($target.Exe)  [$($store.Display)]"

    if (Test-FileReadable -Path $target.Exe) {
        $fp = Get-PeFingerprintFromFile -Path $target.Exe
    } else {
        $fp = Get-PeFingerprintFromLog -LogPath (Join-Path $target.Dir 'HeadTracking.log')
        if (-not $fp) {
            Write-Host "  this executable cannot be read, and there is no HeadTracking.log beside it" -ForegroundColor Yellow
            Write-Host "  to read the fingerprint out of. Launch the game once with the mod installed" -ForegroundColor Yellow
            Write-Host "  and run this again." -ForegroundColor Yellow
            continue
        }
    }

    $built = [DateTimeOffset]::FromUnixTimeSeconds($fp.TimeDateStamp).UtcDateTime
    Write-Host ("  TimeDateStamp 0x{0:X8}  ({1:yyyy-MM-dd HH:mm:ss} UTC)" -f $fp.TimeDateStamp, $built)
    Write-Host ("  SizeOfImage   0x{0:X8}" -f $fp.SizeOfImage)
    Write-Host ("  CheckSum      0x{0:X8}" -f $fp.CheckSum)
    Write-Host ("  read from     {0}" -f $fp.Source)
    Write-Host ""

    $matched = $false
    foreach ($storeKey in $offsetsFiles.Keys) {
        foreach ($known in (Get-KnownProfiles -Store $offsetsFiles[$storeKey])) {
            if ($known.TimeDateStamp -ne $fp.TimeDateStamp) { continue }
            if ($known.SizeOfImage -ne $fp.SizeOfImage) { continue }
            if ($known.CheckSum -ne $fp.CheckSum) { continue }
            Write-Host "MATCH: this build already has a profile ($($known.File) line $($known.LineNumber))." -ForegroundColor Green
            $matched = $true
        }
    }

    if (-not $matched) {
        Write-ProfileTemplate -Store $store -Fingerprint $fp -Built $built
    }
}
