#!/usr/bin/env pwsh
#Requires -Version 5.1
# Dev loop: run uninstall.cmd against every Wreckfest install on this machine.
#
# uninstall.cmd itself takes one game path and is the end-user and launcher
# entry point; it is not changed here. What this adds is the loop, because a
# machine with both the Steam and the Game Pass copy has two deployments and
# uninstall.cmd with no path resolves only the first one detection returns.

[CmdletBinding()]
param(
    # Passed straight through to uninstall.cmd, after the game path.
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force

$paths = @(Find-AllGamePaths -GameId 'wreckfest')
if ($paths.Count -eq 0) { throw "Wreckfest not found." }

$uninstall = Join-Path $PSScriptRoot 'uninstall.cmd'
$failed = @()
foreach ($path in $paths) {
    Write-Host $path -ForegroundColor Cyan
    & $uninstall $path @Arguments
    if ($LASTEXITCODE -ne 0) { $failed += "$path (exit $LASTEXITCODE)" }
}

if ($failed.Count -gt 0) {
    throw ("uninstall failed for: {0}" -f ($failed -join ', '))
}
Write-Host ("Uninstalled from {0} install(s)." -f $paths.Count) -ForegroundColor Green
