# SPDX-License-Identifier: GPL-3.0-or-later

<#
.SYNOPSIS
    Starts a GhidraMCP headless server for the Coney (The Warriors, PS2) Ghidra project on
    127.0.0.1:8090, using the local Ghidra 12.1.4 install (with the ghidra-emotionengine-reloaded
    extension) and a JDK 21.

.DESCRIPTION
    Every machine-specific path comes from coney.local.toml at the repository root (copy
    coney.local.example.toml; see docs/guides/workspace.md). Keys read here:
      * ghidra_install   Ghidra 12.1.4 with the Emotion Engine extension
      * ghidra_projects  folder that holds the Ghidra project
      * jdk_home         JDK 21 home (no default: it must be set)
      * ghidra_mcp_repo  a built ghidra-mcp checkout (no default); also settable with -McpRepo
    Relative paths are resolved against the repository root. A missing key falls back to the
    defaults in coney.local.example.toml, except jdk_home and ghidra_mcp_repo. A path that does not
    exist makes the script stop with an error naming the key and the resolved path.

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
    # stop: Stop-Process -Id (Get-Content <ghidra_projects>\ghidra-mcp-8090.pid)
#>
param(
    [int]$Port = 8090,
    [string]$BindAddress = "127.0.0.1",
    [string]$ProjectDir = "",
    [string]$ProjectName = "warriors",
    [string]$McpRepo = ""
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path $PSScriptRoot -Parent

# Read simple `key = "value"` lines from coney.local.toml; ignore comments and blank lines.
$Config = @{}
$ConfigFile = Join-Path $RepoRoot "coney.local.toml"
if (Test-Path $ConfigFile) {
    foreach ($line in Get-Content $ConfigFile) {
        if ($line -match '^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"([^"]*)"') { $Config[$Matches[1]] = $Matches[2] }
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
    if (-not $value) { throw "$Key is not set. Add it to coney.local.toml (see docs/guides/workspace.md)." }
    if (-not [System.IO.Path]::IsPathRooted($value)) { $value = Join-Path $RepoRoot $value }
    return [System.IO.Path]::GetFullPath($value)
}

function Assert-Exists([string]$Key, [string]$Path, [string]$What) {
    if (-not (Test-Path $Path)) {
        throw "$Key`: $What not found at $Path. Fix $Key in coney.local.toml (see docs/guides/workspace.md)."
    }
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

Write-Host "JAVA_HOME     : $JdkHome"
Write-Host "GHIDRA_HOME   : $GhidraHome"
Write-Host "Headless jar  : $HeadlessJar"
Write-Host "Classpath jars: $($jars.Count)"
Write-Host "Project dir   : $ProjectDir"
Write-Host "Bind          : ${BindAddress}:${Port}"

$Gpr = Join-Path $ProjectDir "$ProjectName.gpr"
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
