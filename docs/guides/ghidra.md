# Ghidra + ghidra-mcp

How to install, start and use the shared Ghidra project that analysts read the PS2 executable in. Paths below are
`coney.local.toml` keys or workspace-relative; see [Local workspace](workspace.md). `coney-tools workspace init
--fetch ghidra` downloads Ghidra and the Emotion Engine extension ([Bootstrap](workspace.md#bootstrap)); the steps
below are what is left to do by hand.

## Pieces

| Piece | Path |
| --- | --- |
| Ghidra 12.1.4 | `ghidra_install` (default `../../tools/ghidra_12.1.4_PUBLIC`) |
| EE extension | `<ghidra_install>\Ghidra\Extensions\ghidra-emotionengine-reloaded` |
| JDK 21 | `jdk_home` (no default; a system `java` may be older, so always set `JAVA_HOME` to this JDK) |
| ghidra-mcp v7.0.0 | `ghidra_mcp_repo` (no default; jar `target\GhidraMCPHeadless.jar` and the Python bridge). Use it as it is: do not rebuild, pull or edit it |
| Server start script | `tools\start-ghidra-mcp.ps1` |
| The Ghidra project the server keeps open | project name `warriors`: the files `warriors.gpr` and `warriors.rep` inside the folder `ghidra_projects` (default `../../ghidra`, that is `<workspace>\ghidra`) |

Language id: **`r5900:LE:32:default`** (compiler spec `default`; processor `MIPS-R5900`, from `emotionengine.ldefs`).

Port: **8090** is this project's server. Another server on the machine may use a different port; never stop a server
you did not start.

## Install the extension

1. Unzip the extension release into `<ghidra_install>\Ghidra\Extensions\` (an extension in the install folder is
   loaded as a module by both `analyzeHeadless` and the MCP server). Do not install it into
   `%APPDATA%\ghidra\ghidra_12.1.4_PUBLIC\Extensions`, which any other Ghidra 12.1.4 copy would share.
2. If the release was built for another Ghidra version (12.1.3 for 12.1.4), set `version=` in its
   `extension.properties` to the installed version; no API breakage was seen.
3. The release holds no `.sla`; compile it once (it prints warnings, such as "8 NOP constructors found", and no
   errors):

   ```bat
   cd <ghidra_install>\Ghidra\Extensions\ghidra-emotionengine-reloaded\data\languages
   set JAVA_HOME=<jdk_home>
   ..\..\..\..\..\support\sleigh.bat r5900.slaspec r5900.sla
   ```

   Do it again when the extension is reinstalled or upgraded, or when Ghidra reports a stale or missing `r5900.sla`.

## Start and stop the server

```powershell
powershell -ExecutionPolicy Bypass -File tools\start-ghidra-mcp.ps1        # 127.0.0.1:8090, project warriors in <ghidra_projects>
```

It works from the main checkout and from any worktree: `coney.local.toml` is untracked, so a worktree has none, and
the script then reads the main checkout's (found through git's common dir) and resolves relative paths from the
main checkout. A worktree that holds its own copy uses that one. Run it as is; do not copy the config into a worktree
or pass paths to work around a "Ghidra install not found" error (fix the config instead).

If a server already answers on the port (`/check_connection`), the script says so and exits 0 instead of starting a
second server on the same project (two servers on one project fight over its lock), so starting it again is safe.
`-DryRun` prints the checkout, config file and paths it would use, and whether a server is up, without starting
anything.

Options: `-Port`, `-ProjectDir`, `-ProjectName`, `-BindAddress` (leave loopback), `-McpRepo` (overrides
`ghidra_mcp_repo`), `-DryRun`. `-ProjectDir` is the **folder** that holds the project files (default:
`ghidra_projects`, `<workspace>\ghidra`) and `-ProjectName` is the project's **name** (`warriors`): the project is
`<ProjectDir>\<ProjectName>.gpr` with its `.rep` folder beside it, so `-ProjectDir ...\ghidra\warriors` is wrong. You
rarely need either. The script reads `ghidra_install`, `ghidra_projects`, `jdk_home` and `ghidra_mcp_repo` from
`coney.local.toml` (double- or single-quoted values) and stops with an error naming the key, the resolved path, the
checkout it ran from and the config file it used when one is missing. It starts Ghidra with this project's
GhidraHome and the extension jars on the classpath, creates the project with `/create_project` on the first start and
writes the java PID to `<ghidra_projects>\ghidra-mcp-8090.pid`.

Stop only this server: `Stop-Process -Id (Get-Content <ghidra_projects>\ghidra-mcp-8090.pid)` (or Ctrl+C in its
console). A forced stop leaves `warriors.lock`; the next start opens the project anyway.

Check it: `Invoke-RestMethod http://127.0.0.1:8090/check_connection` answers
`Connection OK - GhidraMCP Headless Server v7.0.0-headless`.

## MCP registration

The repository's `.mcp.json` registers the bridge for Claude Code as `ghidra-mcp`: it runs
`coney-tools mcp serve ghidra-mcp`, which starts `bridge-mcp-ghidra --transport stdio` from `ghidra_mcp_repo` with
`GHIDRA_MCP_URL=http://127.0.0.1:8090` (unless you set another) and `GHIDRA_MCP_LAZY=0`
([Setting up an agent tool](agents.md#setup)). `GHIDRA_MCP_LAZY=0` loads all tool groups at connect (Claude Code does
not pick up groups loaded later). Start the server **before** the Claude Code session. Tools:
`ToolSearch select:mcp__ghidra-mcp__<name>`.

## Loading a program

Raw blobs (memory dumps, overlays, IOP modules as raw): HTTP POST JSON (forward-slash paths) or MCP tools:
`load_program {file, language: "r5900:LE:32:default"}`, then `run_analysis`, then (a raw blob has no entry point:
`disassemble_bytes {start_address, end_address}` and `create_function {address}`), then `list_functions` and
`decompile_function?address=0x...`, and `save_program`.

PS2 ELFs (`SLUS_xxx.xx`, `.elf`, `.irx`): `/load_program` with `language` only does a raw import (no ELF loader), so
import with `analyzeHeadless` into a separate project, then bring it into the server's reach:

```bat
set JAVA_HOME=<jdk_home>
<ghidra_install>\support\analyzeHeadless.bat <dir>\elfproj warriors_elf ^
    -import <path>\SLUS_xxx.xx -loader ElfLoader -processor r5900:LE:32:default -cspec default
```

(The extension's `r5900.opinion` should also select R5900 for EE ELFs without `-processor`; pin it anyway.)
When `analyzeHeadless` has finished, `open_project {path: "<dir>/elfproj/warriors_elf.gpr"}` (closes `warriors`) or
copy the program into a copy of the project folder, then `load_program_from_project {path: "/SLUS_xxx.xx"}`. Reopen
`<ghidra_projects>/warriors.gpr` with `open_project` when done.

## Gotchas

* PowerShell: a function parameter named `$b` shadows `$B` (case-insensitive), so name your base-URL variable
  differently.
* `/import_file` is GUI-only; use `/load_program`.
* A project open in the server is locked: never point `analyzeHeadless` and the server at the same project at once.
* The server's user settings folder is `%APPDATA%\ghidramcp\ghidramcp_12.1.4_PUBLIC` and `analyzeHeadless`'s is
  `%APPDATA%\ghidra\ghidra_12.1.4_PUBLIC`; both are shared with any other Ghidra 12.1.4 on the machine (preferences
  and logs only, no extensions).
* The bridge's `create_function` warns about PascalCase naming; harmless.
* `/delete_file` on a program just closed may answer "is in use"; stop the server or leave it.
* Long calls (loading and analysing a full ELF) take minutes: use long HTTP timeouts and poll `/analysis_status`.
