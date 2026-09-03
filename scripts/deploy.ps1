#!/usr/bin/env pwsh
#Requires -Version 5.1
# Dev loop: copy the built .asi and the vendored ASI loader into the game folder.

[CmdletBinding()]
param(
    # Positional so `deploy.ps1 "D:\Games\Wreckfest"` works, matching the
    # positional game path install.cmd takes. Named -Config stays available.
    [Parameter(Position = 0)][string]$GamePath,
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot

Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force

if (-not $GamePath) {
    $GamePath = Find-GamePath -GameId 'wreckfest'
}
if (-not $GamePath -or -not (Test-Path $GamePath)) {
    throw "Wreckfest not found. Pass -GamePath explicitly."
}

$asi = Join-Path $projectDir "build/$Config/WreckfestHeadTracking.asi"
if (-not (Test-Path $asi)) { throw "Build output not found: $asi. Run 'pixi run build' first." }

$loader = Join-Path $projectDir 'vendor/ultimate-asi-loader/dinput8.dll'
if (-not (Test-Path $loader)) { throw "Vendored ASI loader missing. Run 'pixi run update-deps'." }

Copy-Item $asi (Join-Path $GamePath 'WreckfestHeadTracking.asi') -Force
Write-Host "  deployed WreckfestHeadTracking.asi" -ForegroundColor DarkGray

# Wreckfest_x64.exe does not import DINPUT8.dll, so the loader has to take a
# name the game actually resolves out of its own directory. VERSION.dll is a
# static import and is not on the KnownDLLs list, so the game-local copy wins.
$loaderTarget = Join-Path $GamePath 'version.dll'
if (-not (Test-Path $loaderTarget)) {
    Copy-Item $loader $loaderTarget -Force
    Write-Host "  deployed version.dll (Ultimate ASI Loader)" -ForegroundColor DarkGray
} else {
    Write-Host "  version.dll already present, left alone" -ForegroundColor DarkGray
}

Write-Host "Deployed to $GamePath" -ForegroundColor Green
