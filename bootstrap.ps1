# SPDX-License-Identifier: GPL-3.0-or-later

<#
.SYNOPSIS
    Sets up a Coney workspace on Windows: installs uv if it is missing, then runs `coney-tools workspace init` with
    the arguments given (docs/guides/workspace.md#bootstrap).

.EXAMPLE
    # From a checkout:
    powershell -ExecutionPolicy Bypass -File bootstrap.ps1 --fetch all --dry-run
    # On its own (clones the repository):
    powershell -ExecutionPolicy Bypass -File bootstrap.ps1 --repo-url URL --workspace C:\Coney
#>

$ErrorActionPreference = "Stop"

if (-not (Get-Command uv -ErrorAction SilentlyContinue)) {
    Write-Host "bootstrap: installing uv (https://docs.astral.sh/uv/)"
    powershell -ExecutionPolicy ByPass -NoProfile -Command "irm https://astral.sh/uv/install.ps1 | iex"
    $env:PATH = "$env:USERPROFILE\.local\bin;$env:PATH"
}

$Here = $PSScriptRoot
if (Test-Path (Join-Path $Here "python\pyproject.toml")) {
    # Inside a checkout: run its own coney-tools from the checkout, so init finds the workspace around it.
    Push-Location $Here
    try { & uv run --isolated --frozen --project (Join-Path $Here "python") coney-tools workspace init @args }
    finally { Pop-Location }
    exit $LASTEXITCODE
}

# On its own: coney-tools comes from the repository named by --repo-url.
$Url = ""
for ($i = 0; $i -lt $args.Count; $i++) {
    if ($args[$i] -like "--repo-url=*") { $Url = $args[$i].Substring(11) }
    elseif ($args[$i] -eq "--repo-url" -and $i + 1 -lt $args.Count) { $Url = $args[$i + 1] }
}
if (-not $Url) {
    Write-Error "bootstrap: not inside a Coney checkout; pass --repo-url URL (the address in the README)"
    exit 2
}
& uvx --from "git+$Url#subdirectory=python" coney-tools workspace init @args
exit $LASTEXITCODE
