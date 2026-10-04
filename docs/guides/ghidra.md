# Ghidra + ghidra-mcp setup (warriors-decomp, PS2)

Set up 2026-10-04. Paths below are `coney.local.toml` keys or workspace-relative; see
[Local workspace](workspace.md). Verified end to end with a hand-built R5900 blob (see "Verification").

## Pieces

| Piece | Path |
| --- | --- |
| Ghidra 12.1.4 (this project's copy) | `ghidra_install` (default `../../tools/ghidra_12.1.4_PUBLIC`) |
| EE extension (installed) | `<ghidra_install>\Ghidra\Extensions\ghidra-emotionengine-reloaded` |
| JDK 21 | `jdk_home` (no default; a system `java` may be older: always set `JAVA_HOME` to this JDK) |
| ghidra-mcp v7.0.0 (shared, read-only) | `ghidra_mcp_repo` (no default; jar `target\GhidraMCPHeadless.jar` + Python bridge). Do not rebuild, pull or edit it: other projects may depend on it. |
| Server start script (ours) | `tools\start-ghidra-mcp.ps1` |
| Ghidra project the server keeps open | `<ghidra_projects>\warriors.gpr` (default `../../ghidra`) |

Language id: **`r5900:LE:32:default`** (compiler spec `default`; processor `MIPS-R5900`, from `emotionengine.ldefs`).

Ports: **8090** = this project. Another project's ghidra-mcp server may use a different port; never stop a server
you did not start.

## Extension install (what was done)

1. Unzipped `ghidra_12.1.3_PUBLIC_20260825_ghidra-emotionengine-reloaded.zip` into
   `<ghidra_install>\Ghidra\Extensions\` (install-dir extensions are loaded as modules by both
   `analyzeHeadless` and the MCP server). NOT installed into `%APPDATA%\ghidra\ghidra_12.1.4_PUBLIC\Extensions` (shared
   with any other Ghidra 12.1.4 copy) -- that dir is still empty.
2. Version patch: `extension.properties` `version=12.1.3` -> `version=12.1.4` (built for 12.1.3; no API breakage seen).
3. The zip has no `.sla`; compiled it once (warnings only, no errors: "8 NOP constructors found"):

   ```bat
   cd <ghidra_install>\Ghidra\Extensions\ghidra-emotionengine-reloaded\data\languages
   set JAVA_HOME=<jdk_home>
   ..\..\..\..\..\support\sleigh.bat r5900.slaspec r5900.sla
   ```

   Redo this if the extension is reinstalled/upgraded (or if Ghidra reports a stale/missing `r5900.sla`).

## Start / stop the server

```powershell
powershell -ExecutionPolicy Bypass -File tools\start-ghidra-mcp.ps1        # 127.0.0.1:8090, project <ghidra_projects>\warriors
```

Options: `-Port`, `-ProjectDir`, `-ProjectName`, `-BindAddress` (leave loopback), `-McpRepo` (overrides
`ghidra_mcp_repo`). The script reads `ghidra_install`, `ghidra_projects`, `jdk_home` and `ghidra_mcp_repo` from
`coney.local.toml` and stops with an error naming the key and the resolved path when one is missing. It follows the
shared `start-headless.ps1` logic (which targets another project's Ghidra install) with: this project's GhidraHome,
extension jars added to the classpath, project created via `/create_project` on first start, and the java PID written to
`<ghidra_projects>\ghidra-mcp-8090.pid`.

Stop (only this server): `Stop-Process -Id (Get-Content <ghidra_projects>\ghidra-mcp-8090.pid)` (or Ctrl+C in its console).
A forced stop leaves `warriors.lock`; the next start opens the project fine anyway (tested).

Check: `Invoke-RestMethod http://127.0.0.1:8090/check_connection` -> `Connection OK - GhidraMCP Headless Server v7.0.0-headless`.

## MCP registration (Claude Code, local scope, this project only)

```sh
claude mcp add ghidra-mcp -s local -e GHIDRA_MCP_URL=http://127.0.0.1:8090 -e GHIDRA_MCP_LAZY=0 -- uv run --directory <ghidra_mcp_repo> bridge-mcp-ghidra --transport stdio
```

`GHIDRA_MCP_LAZY=0` loads all tool groups at connect (Claude Code does not pick up groups loaded later). Start the
server **before** the Claude Code session. Tools: `ToolSearch select:mcp__ghidra-mcp__<name>`.

## Call sequence

Raw blobs (memory dumps, overlays, IOP modules as raw): HTTP POST JSON (forward-slash paths) or MCP tools:
`load_program {file, language: "r5900:LE:32:default"}` -> `run_analysis` -> (a raw blob has no entry point:
`disassemble_bytes {start_address, end_address}` + `create_function {address}`) -> `list_functions` /
`decompile_function?address=0x...` -> `save_program`.

PS2 ELFs (`SLUS_xxx.xx`, `.elf`, `.irx`): `/load_program` with `language` only does a raw import (no ELF loader), so
import with `analyzeHeadless` into a separate project, then copy it into the server's reach:

```bat
set JAVA_HOME=<jdk_home>
<ghidra_install>\support\analyzeHeadless.bat <dir>\elfproj warriors_elf ^
    -import <path>\SLUS_xxx.xx -loader ElfLoader -processor r5900:LE:32:default -cspec default
```

(The extension's `r5900.opinion` should also auto-select R5900 for EE ELFs without `-processor`; pin it anyway.)
Then, with that `analyzeHeadless` finished: `open_project {path: "<dir>/elfproj/warriors_elf.gpr"}` (closes
`warriors`) or copy the program into a copy of the project folder, then `load_program_from_project {path: "/SLUS_xxx.xx"}`.
Reopen `<ghidra_projects>/warriors.gpr` with `open_project` when done.

## Verification (2026-10-04)

Blob `addiu sp,sp,-16; sq ra,0(sp); li v0,42; lq ra,0(sp); jr ra; addiu sp,sp,16` (EE-only `sq`/`lq`):

* `analyzeHeadless ... -loader BinaryLoader -processor r5900:LE:32:default -postScript DumpInsns.java -deleteProject`:
  `Using Language/Compiler: r5900:LE:32:default:default`, instructions `sq ra,0x0(sp)` / `lq ra,0x0(sp)`, decompile
  `undefined4 test_fn(void) { return 0x2a; }`.
* Server on 8090: `load_program` -> `{"success":true,"language":"r5900:LE:32:default"}`, `get_metadata` architecture
  `MIPS-R5900`, `disassemble_bytes` / `create_function` / `decompile_function` -> `return 0x2a;`.
  Test program removed (project recreated empty).

## Gotchas

* PowerShell: a function param named `$b` shadows `$B` (case-insensitive) -- name your base-URL variable differently.
* `/import_file` is GUI-only; use `/load_program`.
* A project open in the server is locked: never point `analyzeHeadless` and the server at the same project at once.
* The server's user settings dir is `%APPDATA%\ghidramcp\ghidramcp_12.1.4_PUBLIC` and analyzeHeadless's is
  `%APPDATA%\ghidra\ghidra_12.1.4_PUBLIC` -- both shared with any other Ghidra 12.1.4 setup on the machine
  (preferences/logs only; no extensions there).
* The bridge's `create_function` warns about PascalCase naming; harmless.
* `/delete_file` on a program just closed may answer "is in use"; stop the server or leave it.
* Long calls (load/analysis of a full ELF) take minutes: use long HTTP timeouts and poll `/analysis_status`.
