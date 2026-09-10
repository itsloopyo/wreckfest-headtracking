#!/usr/bin/env pwsh
#Requires -Version 5.1
# Dev loop: copy the built .asi and the vendored ASI loader into the game folder.

[CmdletBinding()]
param(
    # Positional so `deploy.ps1 "D:\Games\Wreckfest"` works, matching the
    # positional game path install.cmd takes. Named -Config stays available.
    # Left empty, every install on this machine is deployed to: the Steam and
    # Game Pass copies are different builds with their own build profiles, and
    # a dev loop that silently picked one of them leaves the other running
    # whatever .asi was last copied there.
    [Parameter(Position = 0)][string]$GamePath,
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot

Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force

if ($GamePath) {
    if (-not (Test-Path $GamePath)) { throw "Game path does not exist: $GamePath" }
    $targets = @($GamePath)
} else {
    $targets = @(Find-AllGamePaths -GameId 'wreckfest')
    if ($targets.Count -eq 0) {
        throw "Wreckfest not found. Pass -GamePath explicitly."
    }
}

$asi = Join-Path $projectDir "build/$Config/WreckfestHeadTracking.asi"
if (-not (Test-Path $asi)) { throw "Build output not found: $asi. Run 'pixi run build' first." }

$loader = Join-Path $projectDir 'vendor/ultimate-asi-loader/dinput8.dll'
if (-not (Test-Path $loader)) { throw "Vendored ASI loader missing. Run 'pixi run update-deps'." }

foreach ($target in $targets) {
    Write-Host $target -ForegroundColor Cyan
    Copy-Item $asi (Join-Path $target 'WreckfestHeadTracking.asi') -Force
    Write-Host "  deployed WreckfestHeadTracking.asi" -ForegroundColor DarkGray

    # Wreckfest_x64.exe does not import DINPUT8.dll, so the loader has to take a
    # name the game actually resolves out of its own directory. VERSION.dll is a
    # static import and is not on the KnownDLLs list, so the game-local copy wins.
    $loaderTarget = Join-Path $target 'version.dll'
    if (-not (Test-Path $loaderTarget)) {
        Copy-Item $loader $loaderTarget -Force
        Write-Host "  deployed version.dll (Ultimate ASI Loader)" -ForegroundColor DarkGray
    } else {
        Write-Host "  version.dll already present, left alone" -ForegroundColor DarkGray
    }
}

Write-Host ("Deployed to {0} install(s)." -f $targets.Count) -ForegroundColor Green
