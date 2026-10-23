# Ghidra + ghidra-mcp

How to install, start and use the shared Ghidra project that engineers read the PS2 executable in. Paths below are
`coney.local.toml` keys or workspace-relative; see [Local workspace](workspace.md). `coney-tools workspace init
--fetch ghidra` downloads Ghidra and the Emotion Engine extension ([Bootstrap](workspace.md#bootstrap)); the steps
below are what is left to do by hand. On a machine of your own, `coney-tools ghidra setup` does all of it up to a
project of `SLUS_212.15` with our function names ([Starting your own project](#own-project)).

## Pieces

| Piece | Path |
| --- | --- |
| Ghidra 12.1.4 | `ghidra_install` (default `../../tools/ghidra_12.1.4_PUBLIC`) |
| EE extension | `<ghidra_install>\Ghidra\Extensions\ghidra-emotionengine-reloaded` |
| JDK 21 | `jdk_home` (no default; a system `java` may be older, so always set `JAVA_HOME` to this JDK) |
| ghidra-mcp v7.0.0 | `ghidra_mcp_repo` (no default; jar `target\GhidraMCPHeadless.jar` and the Python bridge). Use it as it is: do not rebuild, pull or edit it. Building it on a machine of your own needs Maven 3.9+ ([ghidra-mcp on your machine](#own-ghidra-mcp)) |
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

The coney-tools commands do not use the bridge: `coney-tools original study` and `original verify` talk HTTP to the
server on port 8090 directly (reading endpoints only, the same ones `orig index` uses), so they work from a script, a hook
or a person's terminal. With no server answering they exit with 3 and write nothing
([Study](stamps.md#study)).

## Loading a program {#loading-a-program}

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

## Starting your own project {#own-project}

The pieces above are the maintainers' set-up. On a machine of your own, one command builds the project the server
opens, from your own disc, with our function names in it:

```sh
uv run --project python coney-tools ghidra setup [--disc DISC] [--dry-run]
```

It needs a **JDK 21** (for example Eclipse Temurin 21; `jdk_home` in `coney.local.toml`, else `JAVA_HOME`, else the
`java` on your `PATH`), and it does, skipping each step that is already done:

1. **Fetch Ghidra 12.1.4** with the Emotion Engine extension into `ghidra_install`, as `workspace init --fetch ghidra`
   does ([Fetching the research tools](workspace.md#fetch)), when it is not there yet.
2. **Compile `r5900.sla`** with Ghidra's `sleigh`, when the extension has none (as under
   [Install the extension](#install-the-extension)).
3. **Import and analyse `SLUS_212.15`** from the disc (`--disc`, else `game_dir`: a mounted disc, a folder or an
   `.iso`) into the project `warriors` in `ghidra_projects`, with `analyzeHeadless`, the ELF loader and the language
   `r5900:LE:32:default`. The executable is copied into a temporary folder in the scratch folder for the import and
   deleted after it. Its SHA-1 is checked first: a disc whose `SLUS_212.15` is not the NTSC-U release
   (`e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`) is refused, because every address in the research and in the names
   refers to that one file; `--any-release` imports it anyway.
4. **Apply the names** with [`ghidra import-names`](#import-names) (`--no-names` stops before it).

It prints counts, the executable's size and SHA-1, and each step; the logs (`ghidra-import.log`,
`ghidra-sleigh.log`, `ghidra-import-names.log`) go to `ghidra/` in the scratch folder. Measured on 2026-10-21 on
a fresh workspace (Windows, a busy 16-core machine): fetching Ghidra 35 s, compiling `r5900.sla` 11 s, the headless
import and analysis 152 s, which finds 13,789 functions. `--project-dir` and `--project-name` set up another project
than `<ghidra_projects>/warriors`. A project that a running ghidra-mcp server holds open is refused: stop the server
first ([Gotchas](#gotchas)).

### Our function names {#import-names}

```sh
uv run --project python coney-tools ghidra import-names [--program SLUS_212.15] [--overwrite] [--dry-run]
```

The maintainers' project holds the names they gave 11,530 of its 13,971 functions, and the plate comments and types
that go with them. Only the names, with each function's address and size, are published, in
`docs/progress/ghidra-functions.tsv` ([The Understood measure](research-workflow.md#understood)), and the research
pages cite the same addresses. `import-names` applies them to your project by address: it runs Coney's Ghidra script
`ImportNames.java` ([`python/src/coney_tools/ghidra_scripts/`](repo:python/src/coney_tools/ghidra_scripts/ImportNames.java))
through `analyzeHeadless` on the program, without analysing it again, and prints how many names were:

* **matched**: Ghidra has a function at the address. It is *renamed*, is already the *same*, or is *kept* because you
  named it yourself (a `USER_DEFINED` name; `--overwrite` replaces those too). A matched function whose size differs
  from the listing is renamed anyway and counted on a line of its own.
* **created**: analysis found no function there, so one is created, and kept with the name only when its size is the
  listed size.
* **skipped**: Ghidra's function there is a *thunk* (renaming it would rename the function it jumps to), the address
  lies *inside* another function, holds *data*, cannot be disassembled (*nocode*), the function could not be created
  (*failed*) or came out another *size* (the function is removed again, though the instructions disassembled for it
  stay), or Ghidra refused the name (*error*).

Names it applies get the source `IMPORTED`, so running it again after the listing changes replaces them, while your
own names stay. `--dry-run` opens the program read-only: the script does everything, Ghidra saves none of it, and
the counts are the ones a real run would give. Measured on 2026-10-21 on a project `ghidra setup` had just made
(Windows, a busy 16-core machine; import, analysis and names 2 min 42 s in all): of the 11,527 names (the listing's
11,530 less three that are Ghidra-style `FUN_` names), 11,345 were matched (11,290 renamed, 55 already the same) and
182 created, none skipped; a second run found all 11,527 the same, in 15 s. Plate comments, structures and signatures
are not shared at all until the
research database exists ([The research database](research-data.md#the-research-database)).

### ghidra-mcp on your machine {#own-ghidra-mcp}

The project works in Ghidra's own window as it is. For the agent tools you also need ghidra-mcp's headless server,
`target/GhidraMCPHeadless.jar` in the clone `workspace init --fetch ghidra-mcp` makes, which
`tools/start-ghidra-mcp.ps1` runs by its main class with Ghidra's jars beside it. Building it needs **Apache Maven
3.9+**, which neither `workspace init` nor `ghidra setup` installs. ghidra-mcp's README and `pom.xml` name Ghidra
12.1.3 while Coney pins 12.1.4; leave the pom as it is. The steps the maintainers' jar was built with, in the clone,
with `JAVA_HOME` set to the JDK 21:

1. `python -m tools.setup ensure-prereqs --ghidra-path <ghidra_install>` (from its README) copies the jars of your
   Ghidra 12.1.4 into the local Maven repository under the pom's version; its setup treats 12.1.4 as compatible
   (same minor series).
2. `mvn clean package -Pheadless -DskipTests`, then copy `target/GhidraMCP-7.0.0.jar` to
   `target/GhidraMCPHeadless.jar`.

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
