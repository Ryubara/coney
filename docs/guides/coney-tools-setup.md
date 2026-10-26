# The coney-tools coordination and setup commands

The commands that coordinate agents and set up a machine: [`coord`](#coord), what waits for the coordinator;
[`conclave`](#conclave), the read model of the Claude Code mod; [`doctor`](#doctor), a check of a contributor's machine
and disc; [`ghidra`](#ghidra), your own Ghidra project with our function names; [`mcp`](#mcp), the debugging servers for
agent tools; [`workspace`](#workspace), the local workspace; and [`coney-agent`](#coney-agent), the session launcher.
The other groups are listed in [the command line overview](coney-tools.md#groups).

## coord {#coord}

`coord status` answers "what waits for the coordinator?" in one read. Anyone can run it at a prompt, and the
coordinator's [Tether](#coord-wakes) runs it to wake the session when new work arrives:

```sh
uv run --project python coney-tools coord status            # one line per section, then the ids
uv run --project python coney-tools coord status --json --github
uv run --project python coney-tools coord status --ids --github --kind unit   # one id per line, for a poller
```

Without `--github` it reads only the machine, in a few seconds, and never touches the network. It reads:

* the PCSX2 copies (the rows of `pcsx2 status --json`);
* the build slots and queues (`build status --json`);
* each worktree's last detached build (`build result`);
* the release queue: each `PR.md` or `PR-<n>.md` in the scratch folder of a branch that has a worktree, and whether
  `origin` has the branch, as the local clone knows it;
* the tracks: each scratch folder's `STATUS.md`, kept while its worktree exists or for three days after its last
  change.

`--github` adds, through `gh`:

* the open issues: number, status, priority and area labels, the other labels and age, but no titles;
* the open pull requests: branch, draft, mergeability, auto-merge, the required checks in one word, and the causes
  the [`repo pr-health`](coney-tools-repo.md#repo-pr-health) judge finds, whose report is included whole;
* the CI queue's counts (`gh run list --status queued` and `--status in_progress`);
* for each unit file, the pull request that released it: an open one for its branch, or one merged or closed after
  the file last changed. A unit with neither is queued.

`--json` prints one object: `time`, `checkout`, `github`, the sections `worktrees`, `pcsx2`, `builds`, `results`,
`tracks`, `release`, `issues`, `prs` and `ci`, and `action`. The last three sections are `null` without `--github`.
Each section has `error`, `null` when it was read. A section that could not be read holds only `error`, and the
others stand, so a failure is never taken for "nothing waits". The exit code is 0, and 2 when the command cannot run
at all (outside a checkout). `action` lists the ids of what waits, sorted:

| Id | When |
| --- | --- |
| `issue:<n>:S-needs-triage` | an open issue carries `S-needs-triage` |
| `pr:<n>:<cause>` | `repo pr-health` calls the pull request stuck for that cause |
| `unit:<branch>:<file>` | a unit file is queued |
| `build:done:<worktree>` | the worktree's detached build ended within the last hour |

An id stays the same while the thing waits, and leaves the list once it has been dealt with, so a reader can tell new
work from old by comparing lists. The `issue`, `pr` and `unit` ids need GitHub and appear only in a `--github` read.

`--ids` prints only the ids, one per line, for a program that watches them, and cannot be combined with `--json`.
`--kind issue|pr|unit|build` (repeatable) reads only the sections those kinds come from, and prints only their ids,
so a poller pays for one read. A GitHub kind needs `--github` (exit 2 without it). When a section the ids come from
could not be read (for `unit` and `build` ids that includes the worktree list), `--ids` prints nothing, names the
section and its error on stderr and exits 1, so a watcher reports a failed read instead of an empty list.

### Waking the coordinator {#coord-wakes}

Between turns the coordinator's session sees nothing, so new work would wait until someone typed. Two plugins that the
maintainer runs in every session watch for it and wake the session when it arrives. [Conclave](conclave.md#watchers),
Coney's own mod, tells it Coney's own events: a failed check or another pull request problem, a merge, a unit ready for
`pr open` and a question, and holds its turn end for pull request problems nobody holds ([`pr hold`](pr.md#hold)). The
[Tether](https://github.com/Ryubara/tether) plugin tells it the rest: issues to triage and detached builds that ended,
and it keeps the session's health (context, cache, subagents). Tether needs 1.0.0-alpha.31 or later. Tether reads
`.claude/tether.json` in the session's folder. The repository commits no such file, because Tether would then apply it
to every agent's worktree too, and the maintainer's own file holds machine settings. The coordinator's local file
(listed in `.git/info/exclude`) carries the `watch` settings below. This block is the source those settings are copied
from: change it here first, then in the local file. `python/tests/test_coord_cli.py` checks that every `coney-tools`
command in it parses.

```json
{
  "watch": {
    "storeName": "coney",
    "wakePreamble": "Coordinator: new work. Handle it or say why it can wait:",
    "idleWakeMinutes": 0,
    "pollers": [
      {
        "id": "triage",
        "kind": "issues",
        "title": "Issues to triage",
        "labels": ["S-needs-triage"],
        "everySeconds": 120,
        "remindMinutes": 30,
        "remindMaxMinutes": 240
      },
      {
        "id": "builds",
        "title": "Detached builds",
        "command": ["uv", "run", "-q", "--project", "python", "coney-tools", "coord", "status", "--ids", "--kind", "build"],
        "everySeconds": 15,
        "timeoutMs": 90000
      }
    ]
  }
}
```

The block holds two pollers. A coordinator's file from before Conclave's watchers also holds `release` and `prs`: delete
them, or every unit and every stuck pull request is told twice, once by each plugin (the local file may carry more
pollers of Tether's own, such as `agents`, which stay). What each poller wakes for:

* **`triage`** wakes for each issue waiting for [triage](issues.md#triage): an open issue carrying `S-needs-triage`,
  read every two minutes. It wakes once per issue, keyed by its number, so the coordinator's own comment does not wake
  it again.
* **`builds`** wakes for each `build:done:` id, a detached build that ended within the hour, read every 15 seconds.

What used to be two more pollers is Conclave's, from the same `coord status` ids and `repo pr-health` causes (the
[snapshot](conclave.md#snapshot) reads the same sources): the **release queue** (a `unit:` id, a [release
queue](how-we-work.md#release-queue) file with no pull request yet) and the **pull requests** (a `pr:` id, with the
owner and waiver records now `pr hold` and the stop guard now Conclave's). `coord status --ids` still prints those ids
for any program that wants them. Tether's `prs` poller also treated a pull request whose worktree held a `.coney-busy`
marker as owned (its `busyMarker` and `assignHours` settings); that goes with the poller, and only [`pr hold`](pr.md#hold)
names an owner now: an agent working on a failing pull request is recorded with `pr hold <n> --who <agent>`, whatever
marker its worktree holds.

Tether starts a turn only for ids it has not told before, and never while a turn runs or you type. Ids still waiting
are told again 30 minutes later, then after 60, 120 and at most every four hours (`remindMinutes`). The `builds`
poller has no reminders: a build result is news once.

A wake the engine refuses (after you interrupt a turn) shows `wake refused`. It is tried again two minutes later, or
as soon as your next turn starts. A wake that started no turn is sent again.

`idleWakeMinutes` above 0 also wakes the session after that many idle minutes with nothing new, but never while a
subagent is at work. A poller whose read fails shows as failing in the status line and in `/tether-watch`. Its ids
are never taken for "nothing waits". `/tether-watch` reports each poller's last read.

<!-- command-reference:begin coord -->
## Parameters of `coord` {#coord-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `coord status` {#coord-status-usage}

Issues, pull requests, CI, PCSX2 copies, build slots, release queue and tracks, with action ids.

```text
coney-tools coord status [--json] [--github] [--ids] [--kind KIND]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--json` | flag | off | print the report as JSON |
| `--github` | flag | off | also read GitHub through gh (slower) |
| `--ids` | flag | off | print only the action ids, one per line; exit 1 when a section they come from could not be read |
| `--kind KIND` | choice | none | read only what these action kinds need, and print only their ids (repeatable). One of `build`, `issue`, `pr`, `unit`. |
<!-- command-reference:end -->

## conclave {#conclave}

`conclave snapshot` prints everything the [Conclave mod](conclave.md) draws, as one JSON object. The mod is written in
TypeScript and cannot read the task file, the stamp log, the claim cards or the hook settings, so this command reads
them and the mod only draws the result. It is read-only: it writes nothing, to GitHub or to disk.

```sh
uv run --project python coney-tools conclave snapshot            # one line per section
uv run --project python coney-tools conclave snapshot --json     # the object the mod reads
uv run --project python coney-tools conclave snapshot --json --github --unit fix-frame-812
```

Without `--github` it reads only the machine, in about a second. `--github` adds the claim cards, the open and recently
merged pull requests (with the [`repo pr-health`](coney-tools-repo.md#repo-pr-health) causes), the open issues the
claims, the units and `P-critical` name and the [events](conclave.md#watchers) (a failed check, a merge, a unit ready
for `pr open`, a question, each with an id and a state), through `gh`, in about 6.6 seconds. `--unit TASK` keeps one
unit's row (exit 2 when there is none). The sections, each with an `error` that is `null` when it was read, and the rest
of the shape are in [Conclave](conclave.md#snapshot); the exit code is 0 whatever a section says, and 2 when the command
cannot run at all. The limits and intervals it sends are the `conclave` section of
[.github/conventions.yml](repo:.github/conventions.yml).

<!-- command-reference:begin conclave -->
## Parameters of `conclave` {#conclave-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `conclave snapshot` {#conclave-snapshot-usage}

Units, stamps, builds, claims, pull requests and the machine, as one JSON object.

```text
coney-tools conclave snapshot [--json] [--github] [--unit TASK]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--json` | flag | off | print the snapshot as JSON (the mod's input) |
| `--github` | flag | off | also read GitHub through gh (slower) |
| `--unit TASK` | text | none | keep only this unit's row |
<!-- command-reference:end -->

## doctor {#doctor}

```sh
uv run --project python coney-tools doctor [--disc DISC]
```

Checks this machine and the disc for building and contributing, section by section (build tools, configuration,
disc, research tools, disk), and prints a fix for each problem. Exit code 1 when something the engine's build or the
disc needs is missing, 0 otherwise (notes, such as another release of the game or a missing research tool, do not
change it). Read only; no `--agent`, works without `coney.local.toml`. What it checks:
[Check your setup](building.md#doctor).

<!-- command-reference:begin doctor -->
## Parameters of `doctor` {#doctor-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

Check this machine and your disc for building and contributing, with a fix for each problem.

```text
coney-tools doctor [--disc DISC]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--disc DISC` | text | none | a mounted disc, folder or .iso to check (default: game_dir) |
<!-- command-reference:end -->

## ghidra {#ghidra}

```sh
uv run --project python coney-tools ghidra setup [--disc DISC] [--no-names] [--any-release] [--project-dir DIR]
    [--project-name NAME] [--overwrite] [--dry-run]
uv run --project python coney-tools ghidra import-names [--program NAME] [--listing FILE] [--project-dir DIR]
    [--project-name NAME] [--overwrite] [--dry-run]
```

`setup` makes a contributor's own Ghidra project of `SLUS_212.15`: it fetches the pinned Ghidra and the Emotion
Engine extension when `ghidra_install` has none, compiles `r5900.sla`, imports and analyses the executable from the
disc (`--disc`, else `game_dir`) into `<ghidra_projects>/warriors` after checking its SHA-1 against the NTSC-U
release, and then runs `import-names`. `import-names` applies the names of `docs/progress/ghidra-functions.tsv` to the
program by address with a Ghidra script run by `analyzeHeadless`, and prints how many were matched, created and
skipped. Both need a JDK 21, skip what is done, print counts and hashes only, and keep their logs in `ghidra/` in the
scratch folder; `--dry-run` changes nothing (for `import-names` it opens the program read-only and prints the counts a
real run would give). Exit codes: 0 done, 2 a missing piece, a refused disc or a failed Ghidra step (the message names
its log). What each step does and what the counts mean: [Starting your own project](ghidra.md#own-project).

<!-- command-reference:begin ghidra -->
## Parameters of `ghidra` {#ghidra-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `ghidra setup` {#ghidra-setup-usage}

Fetch Ghidra, compile the EE language, import SLUS_212.15 from your disc, apply our names.

```text
coney-tools ghidra setup [--disc DISC] [--no-names] [--any-release] [--project-dir PROJECT_DIR]
    [--project-name PROJECT_NAME] [--overwrite] [--dry-run]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--disc DISC` | text | none | a mounted disc, folder or .iso (default: game_dir) |
| `--no-names` | flag | off | stop after the import; do not apply the names |
| `--any-release` | flag | off | import an executable that is not the NTSC-U release anyway |
| `--project-dir PROJECT_DIR` | path | none | the folder holding the project (default: ghidra_projects) |
| `--project-name PROJECT_NAME` | text | `warriors` | the project's name (default warriors) |
| `--overwrite` | flag | off | also replace names you gave by hand |
| `--dry-run` | flag | off | print what would happen; save nothing |

### `ghidra import-names` {#ghidra-import-names-usage}

Apply docs/progress/ghidra-functions.tsv's names to your project by address.

```text
coney-tools ghidra import-names [--program PROGRAM] [--listing LISTING] [--project-dir PROJECT_DIR]
    [--project-name PROJECT_NAME] [--overwrite] [--dry-run]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--program PROGRAM` | text | `SLUS_212.15` | the program in the project (default SLUS_212.15) |
| `--listing LISTING` | path | none | the names listing (default docs/progress/ghidra-functions.tsv) |
| `--project-dir PROJECT_DIR` | path | none | the folder holding the project (default: ghidra_projects) |
| `--project-name PROJECT_NAME` | text | `warriors` | the project's name (default warriors) |
| `--overwrite` | flag | off | also replace names you gave by hand |
| `--dry-run` | flag | off | print what would happen; save nothing |
<!-- command-reference:end -->

## mcp {#mcp}

```sh
uv run --project python coney-tools mcp serve ghidra-mcp|pcsx2 [--copy NAME]
```

Runs one of the debugging servers that the repository's `.mcp.json` registers for agent tools, with stdin and stdout
passed through to it: `ghidra-mcp` is ghidra-mcp's Python bridge from `ghidra_mcp_repo` (run by uv, talking to the
headless server on port 8090 unless `GHIDRA_MCP_URL` says otherwise), `pcsx2` is the PCSX2-MCP fork's Node.js server
from `pcsx2_mcp_dir` (its `pcsx2-mcp-server/`). The server connects to nothing until `pcsx2_connect`; with
`--copy NAME` it starts with that copy's ports in `PCSX2_DEBUG_PORT` and `PCSX2_PINE_PORT`, which a fork server from
the `env-default-ports` change on takes as `pcsx2_connect`'s defaults (an older build ignores them, so pass the ports
to `pcsx2_connect` anyway). The pcsx2 server always starts with `PCSX2_REQUIRE_AGENT=1`, which a fork build
from v2.9.129.9 on reads as "every `pcsx2_*` call must carry an `agent` argument, and not `default`" (an older
build ignores it); that build also has `pcsx2_disconnect` and releases a connection idle for `PCSX2_IDLE_MINUTES`
(default 30, read from the environment the agent tool gives) when another caller wants its port ([The PCSX2 MCP
guard](agents.md#pcsx2-guard)). The checkout is `CLAUDE_PROJECT_DIR` when the agent tool
sets it, else the current folder; a worktree reads the main checkout's `coney.local.toml`. A missing key, folder or
program (`uv`, `node`) prints one line on stderr and exits with code 2, so a server that is not installed fails
without affecting the others. Why and how it is set up: [Setting up an agent tool](agents.md#setup).

<!-- command-reference:begin mcp -->
## Parameters of `mcp` {#mcp-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `mcp serve` {#mcp-serve-usage}

Run a server found through coney.local.toml, over stdin and stdout.

```text
coney-tools mcp serve NAME [--copy COPY]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `NAME` | choice | required | which server to run. One of `ghidra-mcp`, `pcsx2`. |
| `--copy COPY` | text | none | pcsx2 only: the PCSX2 copy (pcsx2, pcsx2-b, ...) whose ports pcsx2_connect defaults to |
<!-- command-reference:end -->

## workspace {#workspace}

```sh
uv run --project python coney-tools workspace init [--workspace DIR] [--repo-url URL] [--fetch TOOL ...] [--dry-run]
```

Creates the missing folders of the [local workspace](workspace.md), clones the repository when run outside one
(`--repo-url`), writes `coney.local.toml` from the example when there is none, checks the prerequisites and, with
`--fetch ghidra|ghidra-mcp|pcsx2-mcp-release|pcsx2-mcp|ccache|lua|all`, downloads the research tools and, on
Windows, the [compiler cache](building.md#compiler-cache) at pinned versions, checked by SHA-256 (`pcsx2-mcp-release`
is the PCSX2-MCP fork's Windows release, the PCSX2 the copies run, `pcsx2 setup-copies`; the two MCP projects are
cloned at a pinned commit; on Linux and macOS ccache comes from the package manager; `lua` builds Lua 4.0.1's `luac`
from its source for [`lua decompile --roundtrip`](coney-tools-fidelity.md#lua)).
Every step already done is skipped; `--dry-run` only prints them. It never downloads game files or a BIOS. Exit
codes: 1 when a prerequisite of the engine is missing, 2 for a usage error. The launchers `bootstrap.sh` and
`bootstrap.ps1` install uv (a pinned version, with Astral's installer, which adds it to `PATH`) and run it, adding
`--fetch ccache` when no `--fetch` is given. What each
step does and the pinned versions: [Bootstrap](workspace.md#bootstrap).

```sh
uv run --project python coney-tools workspace clean [--apply] [--build-hours N] [--worktree-hours N] [--fetch]
    [--min-gb N] [--margin-gb N] [--shell-hours N] [--log-idle-hours N]
uv run --project python coney-tools workspace disk [--min-gb N]
```

`clean` removes the worktrees whose pull request merged (with their local branches) when they have no uncommitted
changes, and deletes the `build/` folders nobody has touched for `--build-hours` (default
`workspace.build_idle_hours`, 24), skipping any worktree that holds or waits for a build slot and any worktree
holding the busy file (`workspace.busy_marker`, `.coney-busy`; an empty one older than `--build-hours` is ignored, as
its agent is gone, and one with an id in it always counts). The slots are read from the build queue's own claim and
ticket files each time and again just before each deletion, with the queue's rules for a dead claim, never from an
earlier listing. It never touches the main checkout. When the drive is still under `--min-gb` (default
`workspace.disk_min_free_gb`, 30) after that, it deletes more `build/` folders, oldest last use first and never a
worktree's sources, until `--margin-gb` (default `workspace.disk_margin_gb`, 10) above the floor is free, skipping
worktrees that are busy or hold or wait for a build slot (disk pressure). It is a dry run without `--apply`, and
reports each deletion, the bytes reclaimed and the free space before and after. It also lists, and with `--apply`
stops, the **stale processes**: shells older than `--shell-hours` (default 3) waiting in a sleep loop on files
unchanged for `--log-idle-hours` (default 1), and orphaned `ctest` or `coney_tests` runs whose parent is gone.
`disk` prints the workspace drive's free space and exits 1 below `--min-gb` (default: `workspace.disk_min_free_gb` of
[.github/conventions.yml](repo:.github/conventions.yml), 30). Rules and the busy file:
[Cleanup](workspace.md#cleanup).

<!-- command-reference:begin workspace -->
## Parameters of `workspace` {#workspace-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `workspace init` {#workspace-init-usage}

Create the workspace folders, clone, write coney.local.toml and check the prerequisites.

```text
coney-tools workspace init [--workspace WORKSPACE] [--repo-url REPO_URL] [--fetch FETCH] [--dry-run]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--workspace WORKSPACE` | path | none | the workspace folder (default: found from the checkout) |
| `--repo-url REPO_URL` | text | none | the repository to clone when not run inside a checkout |
| `--fetch FETCH` | choice | none | also download a research tool, or ccache on Windows, at its pinned version (repeatable; never a BIOS or game files). One of `ghidra`, `ghidra-mcp`, `pcsx2-mcp`, `pcsx2-mcp-release`, `ccache`, `lua`, `all`. |
| `--dry-run` | flag | off | print the steps without doing any |

### `workspace clean` {#workspace-clean-usage}

Remove merged worktrees and old build folders (all idle ones when the drive is low), stop stale waits and orphaned test
runs (a dry run unless --apply).

```text
coney-tools workspace clean [--workspace WORKSPACE] [--apply] [--build-hours BUILD_HOURS] [--min-gb MIN_GB]
    [--margin-gb MARGIN_GB] [--worktree-hours WORKTREE_HOURS] [--fetch] [--shell-hours SHELL_HOURS]
    [--log-idle-hours LOG_IDLE_HOURS]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--workspace WORKSPACE` | path | none | the workspace folder (default: found from the checkout) |
| `--apply` | flag | off | act; without it, only report what would be done |
| `--build-hours BUILD_HOURS` | number | none | delete build/ folders of worktrees untouched this many hours (default: workspace.build_idle_hours) |
| `--min-gb MIN_GB` | number | none | disk pressure: under this much free space, delete idle build/ folders (default: workspace.disk_min_free_gb) |
| `--margin-gb MARGIN_GB` | number | none | ... until this much more than --min-gb is free (default: workspace.disk_margin_gb) |
| `--worktree-hours WORKTREE_HOURS` | number | `1.0` | remove merged worktrees only when untouched this many hours (default: 1) |
| `--fetch` | flag | off | git fetch origin main first, so merges are current |
| `--shell-hours SHELL_HOURS` | number | `3.0` | a shell waiting in a loop is stale only when older than this many hours (default: 3) |
| `--log-idle-hours LOG_IDLE_HOURS` | number | `1.0` | ... and every file its loop reads is unchanged this many hours (default: 1) |

### `workspace disk` {#workspace-disk-usage}

Print the free space of the workspace drive; exit 1 below --min-gb.

```text
coney-tools workspace disk [--workspace WORKSPACE] [--min-gb MIN_GB]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--workspace WORKSPACE` | path | none | the workspace folder (default: found from the checkout) |
| `--min-gb MIN_GB` | number | none | the free space in GB below which the exit code is 1 (default: workspace.disk_min_free_gb) |
<!-- command-reference:end -->

## config {#config}

```sh
uv run --project python coney-tools config show
```

Prints the paths your `coney.local.toml` sets, so you can see which checkout, game folder and tools a command will
use (start from `coney.local.example.toml`; [Local workspace](workspace.md) explains each key).

<!-- command-reference:begin config -->
## Parameters of `config` {#config-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `config show` {#config-show-usage}

Show the configured paths.

```text
coney-tools config show
```

Takes no parameters.
<!-- command-reference:end -->

## coney-agent {#coney-agent}

```sh
uv run --project python coney-agent [ROLE] [--mod-dir FOLDER] [--dry-run] [-- CLAUDE-OPTION ...]
tools/coney-agent [ROLE] ...        # Linux, macOS, Git Bash: put tools/ on PATH (a link to it works)
tools/coney-agent.ps1 [ROLE] ...    # Windows, PowerShell
tools/coney-agent.cmd [ROLE] ...    # Windows, cmd.exe
```

The session launcher: starts Claude Code as one of Coney's roles, so the hooks know who is acting ([Which role to
start](agents.md#roles)); a session started without one may only read (`require_role`). It is a separate console script
of the same package, not a `coney-tools` group, and not the player-facing launcher of the game. It finds the Coney
checkout that holds the current folder (the nearest folder above it with `AGENTS.md` and `.claude/agents/`; outside any
checkout, the one it is installed from), checks the role against the files in `.claude/agents/`, and runs `claude
--agent ROLE` in that checkout, adding `--plugin-dir <mod folder>` when the folder exists (the mod then loads in every
mode, trusted folder or not). When it does not, it says so on stderr and starts without the mod, unless `--mod-dir`
named the folder (then it is an error).

* `ROLE`: the role to start, a file name in `.claude/agents/` without `.md`. Default `coordinator`. An unknown role is
  refused with the list of roles.
* `--mod-dir FOLDER`: the Coney mod folder to load with `--plugin-dir`. Default `tools/claude-mod` in the checkout, which
  may be missing. A relative folder is taken from where you run the command, not from the checkout, and a folder you
  name that is not there is refused (exit 2).
* `--dry-run`: print the command, quoted so that a shell can run it, and the folder it would run in; start nothing.
* `-- CLAUDE-OPTION ...`: everything after a lone `--` goes to `claude` unchanged (`coney-agent engineer -- --model
  opus`).

On Linux and macOS the session launcher is replaced by Claude Code, so Ctrl+C reaches Claude Code alone; on Windows it
waits for Claude Code with Ctrl+C ignored in itself. That assumes the native Claude Code (`claude.exe`): when `claude`
on `PATH` is a batch file (the npm shim, `claude.cmd`), Windows starts it through `cmd.exe`, which asks "Terminate
batch job (Y/N)?" on Ctrl+C whatever the wrapper, and the session launcher says so on stderr (answer N). In `cmd.exe`
the `.cmd` wrapper asks the same; the `.ps1` wrapper does not, but Windows PowerShell 5.1 refuses scripts by default
(its execution policy is Restricted), so there run `uv run --project python coney-agent` or allow scripts once with
`Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`; PowerShell 7 runs it as it is. Exit codes: Claude Code's own
exit code when it runs; 2 for an unknown role or when `claude` is not on `PATH` or `--mod-dir` names a folder that is
not there.

<!-- command-reference:begin coney-agent -->
## Parameters of `coney-agent` {#coney-agent-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

Start Claude Code as one of Coney's roles, from the checkout that holds the current folder.

```text
coney-agent [ROLE] [--mod-dir MOD_DIR] [--dry-run]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[ROLE]` | text | `coordinator` | the role to start (default coordinator) |
| `--mod-dir MOD_DIR` | path | none | the Coney mod folder to load with --plugin-dir (default tools/claude-mod of the checkout) |
| `--dry-run` | flag | off | print the command and the folder; start nothing |
<!-- command-reference:end -->
