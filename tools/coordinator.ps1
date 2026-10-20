# SPDX-License-Identifier: GPL-3.0-or-later

<#
.SYNOPSIS
    Starts (or continues) the coordinator's Claude Code session in the main checkout, with the coney mod loaded.

.DESCRIPTION
    Runs `claude --continue --plugin-dir <checkout>\tools\claude-mod\coney` in the main checkout (found from this
    script's folder through git's common dir, so it works from a worktree too), with a first prompt that points the
    session at its role (docs/guides/how-we-work.md#coordinator) and at HANDOFF.md, the local state file, as
    background. --continue picks up the folder's most recent conversation; -New starts a fresh one.
    The mod (docs/guides/coordinator-mod.md) reads `coney-tools coord status` every 15 seconds and wakes the session
    when new work waits. -IdleWakeMinutes N also wakes it after N idle minutes with no subagent at work (it sets
    CONEY_IDLE_WAKE_MINUTES for the session; 0, the default, leaves it off). -Model picks the session's model;
    -ClaudeArgs passes more arguments to claude from a PowerShell prompt (`-ClaudeArgs '--effort','high'`). -ModDir
    loads the mod from another folder, such as a worktree that changes it. -DryRun prints the folder and the command
    line and starts nothing.

.EXAMPLE
    pwsh tools/coordinator.ps1
    pwsh tools/coordinator.ps1 -IdleWakeMinutes 45
#>

[CmdletBinding()]
param(
    [int]$IdleWakeMinutes = 0,
    [switch]$New,
    [switch]$DryRun,
    [string]$ModDir = '',
    [string]$Model = '',
    [string[]]$ClaudeArgs = @()
)

$ErrorActionPreference = 'Stop'

# The main checkout: the parent of git's common dir (a worktree's .git points back to it).
$here = Split-Path -Parent $PSScriptRoot
$common = git -C $here rev-parse --path-format=absolute --git-common-dir
if ($LASTEXITCODE -ne 0 -or -not $common) { throw "coordinator.ps1: $here is not in a git checkout" }
$checkout = Split-Path -Parent ($common.Trim() -replace '/', '\')
$mod = if ($ModDir) { (Resolve-Path $ModDir).Path } else { Join-Path $checkout 'tools\claude-mod\coney' }
if (-not (Test-Path (Join-Path $mod '.claude-plugin\plugin.json'))) {
    throw "coordinator.ps1: no coney mod at $mod (is the main checkout older than the mod?)"
}

$prompt = 'You are the coordinator. Follow docs/guides/how-we-work.md#coordinator (and the guides it links); ' +
    'HANDOFF.md holds the current state, as background. The coney mod wakes you when new work waits and names ' +
    'its ids (docs/guides/coordinator-mod.md). Carry on from the state.'

$argv = @()
if (-not $New) { $argv += '--continue' }
$argv += @('--plugin-dir', $mod)
if ($Model) { $argv += @('--model', $Model) }
$argv += $ClaudeArgs
$argv += $prompt

if ($DryRun) {
    Write-Output "folder: $checkout"
    if ($IdleWakeMinutes -gt 0) { Write-Output "CONEY_IDLE_WAKE_MINUTES=$IdleWakeMinutes" }
    Write-Output ("claude " + (($argv | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join ' '))
    exit 0
}

if ($IdleWakeMinutes -gt 0) { $env:CONEY_IDLE_WAKE_MINUTES = "$IdleWakeMinutes" }
else { Remove-Item Env:CONEY_IDLE_WAKE_MINUTES -ErrorAction SilentlyContinue }
Push-Location $checkout
try {
    & claude @argv
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
