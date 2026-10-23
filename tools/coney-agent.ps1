# SPDX-License-Identifier: GPL-3.0-or-later
# The session launcher for PowerShell: starts Claude Code as one of Coney's roles. Put this folder on PATH.
# Prefer it to coney-agent.cmd: when cmd.exe runs a batch file, Ctrl+C asks "Terminate batch job (Y/N)?" and answering
# Y can leave Claude Code running; here Ctrl+C reaches Claude Code and the session launcher ends with it.
# Windows PowerShell 5.1 refuses scripts by default (Restricted): run `uv run --project python coney-agent` instead, or
# allow scripts once with `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`. PowerShell 7 runs this file as it is.
# Parameters: docs/guides/coney-tools.md#coney-agent
$project = Join-Path (Split-Path -Parent $PSScriptRoot) 'python'
& uv run --project $project coney-agent @args
exit $LASTEXITCODE
