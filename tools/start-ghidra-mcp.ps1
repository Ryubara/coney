# SPDX-License-Identifier: GPL-3.0-or-later

<#
.SYNOPSIS
    Starts a GhidraMCP headless server for the Coney (The Warriors, PS2) Ghidra project on
    127.0.0.1:8090, using the local Ghidra 12.1.4 install (with the ghidra-emotionengine-reloaded
    extension) and a JDK 21.

.DESCRIPTION
    Works from any checkout or worktree. Every machine-specific path comes from coney.local.toml (copy
    coney.local.example.toml; see docs/guides/workspace.md). That file is untracked, so a worktree has none of its
    own: the script uses the one in the checkout it runs from if there is one, else the MAIN checkout's (found with
    git's common dir), and resolves relative paths against the folder that holds the file. Keys read here:
      * ghidra_install   Ghidra 12.1.4 with the Emotion Engine extension
      * ghidra_projects  folder that holds the Ghidra project
      * jdk_home         JDK 21 home (no default: it must be set)
      * ghidra_mcp_repo  a built ghidra-mcp checkout (no default); also settable with -McpRepo
    A missing key falls back to the defaults in coney.local.example.toml, except jdk_home and
    ghidra_mcp_repo. A path that does not exist makes the script stop with an error naming the key, the
    resolved path, the checkout it ran from and the config file it used.

    If a server already answers on the port, the script says so and exits 0 without starting a second one (two
    servers on one project fight over its lock). -DryRun resolves and checks everything, prints it and exits
    without starting anything.

    -ProjectDir is the FOLDER that holds the project files (default: ghidra_projects, e.g. Coney\ghidra),
    -ProjectName the project's name (default warriors): the files are <ProjectDir>\<ProjectName>.gpr and .rep.

    The launch logic follows the headless script shipped with ghidra-mcp, which cannot be reused as
    is because it points at another project's Ghidra install. Differences:
      * the classpath also includes Ghidra\Extensions\*\lib\*.jar (the EE extension's loader and
        analyzers); the R5900 language itself is found through Ghidra's module discovery of
        Ghidra\Extensions\<ext>\data\languages.
      * defaults: Port 8090, ProjectName warriors, the project folder is ghidra_projects.
      * writes the java PID to <ProjectDir>\ghidra-mcp-<Port>.pid.

    The jar (target\GhidraMCPHeadless.jar) is used read-only from the shared ghidra-mcp checkout.
    Never rebuild or pull that repository from this project.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\start-ghidra-mcp.ps1
    # check what it would use, starting nothing:
    powershell -ExecutionPolicy Bypass -File tools\start-ghidra-mcp.ps1 -DryRun
    # stop: Stop-Process -Id (Get-Content <ghidra_projects>\ghidra-mcp-8090.pid)
#>
param(
    [int]$Port = 8090,
    [string]$BindAddress = "127.0.0.1",
    [string]$ProjectDir = "",
    [string]$ProjectName = "warriors",
    [string]$McpRepo = "",
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path $PSScriptRoot -Parent

# The main checkout: the parent of git's common dir (a worktree's .git points back to it). The checkout itself when
# it is the main one or git cannot say.
function Get-MainCheckout([string]$Root) {
    try {
        $common = (& git -C $Root rev-parse --path-format=absolute --git-common-dir 2>$null | Select-Object -First 1)
        if ($LASTEXITCODE -eq 0 -and $common -and (Split-Path $common -Leaf) -eq ".git") {
            return [System.IO.Path]::GetFullPath((Join-Path $common ".."))
        }
    } catch { }
    return $Root
}

# coney.local.toml is untracked, so a worktree has none: use this checkout's own file if it has one, else the main
# checkout's. Relative paths resolve against the folder that holds the file (the main checkout when there is none).
$MainRoot = Get-MainCheckout $RepoRoot
$ConfigFile = $null
foreach ($candidate in @($RepoRoot, $MainRoot)) {
    if (Test-Path (Join-Path $candidate "coney.local.toml")) { $ConfigFile = Join-Path $candidate "coney.local.toml"; break }
}
$ConfigBase = if ($ConfigFile) { Split-Path $ConfigFile -Parent } else { $MainRoot }
$ConfigNote = if ($ConfigFile) { "config file $ConfigFile" } else { "no coney.local.toml in $RepoRoot or $MainRoot (defaults)" }

# Read simple `key = "value"` or `key = 'value'` lines from coney.local.toml (TOML's literal strings let Windows
# paths skip backslash escapes, and coney-tools accepts both); ignore comments and blank lines.
$Config = @{}
if ($ConfigFile) {
    foreach ($line in Get-Content $ConfigFile) {
        if ($line -match '^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?:"(?<v>[^"]*)"|''(?<v>[^'']*)'')') {
            $Config[$Matches[1]] = $Matches['v']
        }
    }
}

# Defaults from coney.local.example.toml. jdk_home and ghidra_mcp_repo have none.
$Defaults = @{
    ghidra_install  = "../../tools/ghidra_12.1.4_PUBLIC"
    ghidra_projects = "../../ghidra"
}

# Returns the absolute path for a key: the configured value, else its default. Throws when unset.
function Resolve-ConfigPath([string]$Key, [string]$Override = "") {
    $value = $Override
    if (-not $value) { $value = $Config[$Key] }
    if (-not $value) { $value = $Defaults[$Key] }
    if (-not $value) { throw "$Key is not set. Add it to coney.local.toml (see docs/guides/workspace.md). Ran from checkout $RepoRoot; $ConfigNote." }
    if (-not [System.IO.Path]::IsPathRooted($value)) { $value = Join-Path $ConfigBase $value }
    return [System.IO.Path]::GetFullPath($value)
}

function Assert-Exists([string]$Key, [string]$Path, [string]$What) {
    if (-not (Test-Path $Path)) {
        throw "$Key`: $What not found at $Path. Fix $Key in coney.local.toml (see docs/guides/workspace.md). Ran from checkout $RepoRoot; $ConfigNote."
    }
}

# True when a GhidraMCP server already answers on the port.
function Test-ServerUp([string]$Address, [int]$Number) {
    try { $null = Invoke-RestMethod -Uri "http://${Address}:${Number}/check_connection" -TimeoutSec 2; return $true } catch { return $false }
}

# A server on the port already: leave it alone (a second one on the same project would fight over its lock).
$ServerUp = Test-ServerUp $BindAddress $Port
if ($ServerUp -and -not $DryRun) {
    Write-Host "A GhidraMCP server already answers on ${BindAddress}:${Port} (http://${BindAddress}:${Port}/check_connection); not starting another."
    exit 0
}

$GhidraHome = Resolve-ConfigPath "ghidra_install"
Assert-Exists "ghidra_install" $GhidraHome "Ghidra install"
$ProjectDir = Resolve-ConfigPath "ghidra_projects" $ProjectDir
Assert-Exists "ghidra_projects" $ProjectDir "Ghidra projects folder"
$JdkHome = Resolve-ConfigPath "jdk_home"
Assert-Exists "jdk_home" "$JdkHome\bin\java.exe" "JDK 21 (bin\java.exe)"
$McpHome = Resolve-ConfigPath "ghidra_mcp_repo" $McpRepo
$HeadlessJar = Join-Path $McpHome "target\GhidraMCPHeadless.jar"
Assert-Exists "ghidra_mcp_repo" $HeadlessJar "headless jar (target\GhidraMCPHeadless.jar)"
if (-not (Test-Path "$GhidraHome\Ghidra\Extensions\ghidra-emotionengine-reloaded\data\languages\r5900.sla")) {
    Write-Warning "r5900.sla missing: compile it with support\sleigh.bat (see docs/guides/ghidra.md)."
}

$env:JAVA_HOME = $JdkHome
$env:PATH = "$JdkHome\bin;$env:PATH"

$jars = @($HeadlessJar)
foreach ($pat in @("Framework\*\lib\*.jar", "Features\*\lib\*.jar", "Processors\*\lib\*.jar", "Extensions\*\lib\*.jar")) {
    $jars += Get-ChildItem -Path "$GhidraHome\Ghidra\$pat" -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName }
}
$classpath = [string]::Join(";", $jars)

Write-Host "Checkout      : $RepoRoot"
Write-Host "Main checkout : $MainRoot"
Write-Host "Config        : $ConfigNote"
Write-Host "JAVA_HOME     : $JdkHome"
Write-Host "GHIDRA_HOME   : $GhidraHome"
Write-Host "Headless jar  : $HeadlessJar"
Write-Host "Classpath jars: $($jars.Count)"
Write-Host "Project dir   : $ProjectDir"
Write-Host "Bind          : ${BindAddress}:${Port}"

$Gpr = Join-Path $ProjectDir "$ProjectName.gpr"
if ($DryRun) {
    Write-Host "Project       : $Gpr ($(if (Test-Path $Gpr) { 'exists' } else { 'does not exist yet' }))"
    Write-Host "Server on ${BindAddress}:${Port}: $(if ($ServerUp) { 'already running' } else { 'not running' })"
    Write-Host "Dry run: nothing started."
    exit 0
}
$javaArgs = @(
    "-Dghidra.home=$GhidraHome",
    "-Dapplication.name=GhidraMCP",
    "-classpath", $classpath,
    "com.xebyte.headless.GhidraMCPHeadlessServer",
    "--bind", $BindAddress,
    "--port", $Port
)
if (Test-Path $Gpr) {
    $javaArgs += @("--project", $Gpr)
    Write-Host "Project       : opening existing $Gpr"
} else {
    Write-Host "Project       : $Gpr does not exist; creating it once the server is up"
    $null = Start-Job -ArgumentList $BindAddress, $Port, $ProjectDir, $ProjectName -ScriptBlock {
        param($addr, $port, $dir, $name)
        $base = "http://${addr}:${port}"
        for ($i = 0; $i -lt 120; $i++) {
            try { $null = Invoke-RestMethod -Uri "$base/check_connection" -TimeoutSec 2; break } catch { Start-Sleep -Milliseconds 500 }
        }
        $body = @{ parentDir = ($dir -replace '\\', '/'); name = $name } | ConvertTo-Json -Compress
        try { $null = Invoke-RestMethod -Method Post -Uri "$base/create_project" -ContentType "application/json" -Body $body } catch { }
    }
}

$PidFile = Join-Path $ProjectDir "ghidra-mcp-$Port.pid"
Write-Host "PID file      : $PidFile"
Write-Host "Starting GhidraMCP headless server (Ctrl+C to stop)..."

# Run java in the foreground (like start-headless.ps1) but record its PID so a
# caller can stop exactly this server.
$proc = Start-Process -FilePath "$JdkHome\bin\java.exe" -ArgumentList ($javaArgs | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -NoNewWindow -PassThru
Set-Content -Path $PidFile -Value $proc.Id
Write-Host "java PID      : $($proc.Id)"
try { $proc.WaitForExit() } finally {
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
    Remove-Item $PidFile -ErrorAction SilentlyContinue
}
