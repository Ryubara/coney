@echo off
rem SPDX-License-Identifier: GPL-3.0-or-later
rem The session launcher for cmd.exe: starts Claude Code as one of Coney's roles. Put this folder on PATH.
rem Ctrl+C here asks "Terminate batch job (Y/N)?": answer N. coney-agent.ps1 (PowerShell) does not ask.
rem Parameters: docs/guides/coney-tools.md#coney-agent
uv run --project "%~dp0..\python" coney-agent %*
