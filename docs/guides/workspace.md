# Local workspace

Coney's tools expect a small set of folders next to the repository: your game files, a PCSX2 install, Ghidra, a
scratch area and any extra worktrees. This page describes that layout, how to create it and how to tell the tools
where things are. None of it is published or checked in; you can arrange your machine differently as long as
`coney.local.toml` points at the right places.

## The layout

```text
Coney/                          the workspace (a plain folder, not a repository)
  repos/coney/                  the main checkout; agent sessions start here
  worktrees/coney/<branch>/     every worktree, and nowhere else
  scratch/                      captures, dumps, tool output, leftovers; never committed
  cache/ccache/                 the compiler cache every checkout's builds share (building.md#compiler-cache)
  game/                         your disc image and files extracted from it
  emulators/pcsx2, pcsx2-b, ... portable PCSX2 copies of the PCSX2-MCP fork build (plus BIOS), one per agent
  tools/                        local installs: Ghidra (with the EE extension), RenderDoc, a JDK if needed
  ghidra/                       local Ghidra projects (rebuildable caches)
```

The top folder's name does not matter. What matters is the shape: the checkout sits two levels below the workspace
root, so `../../game` from the repository root is the `game/` folder.

## Why siblings

Everything that is not source code lives outside the repository, next to it:

* **Safety.** The disc image, extracted files and Ghidra databases (which contain the game's code) must never be
  committed ([LEGAL.md](repo:LEGAL.md#no-game-data)). With them outside the
  working tree, a stray `git add -A` cannot reach them.
* **Simplicity.** The repository's `.gitignore` does not have to list every local folder, and a fresh clone has no
  leftovers to trip over.
* **Reuse.** One Ghidra install, one emulator and one scratch area serve the main checkout and every worktree.

## Bootstrap {#bootstrap}

`coney-tools workspace init` sets the workspace up for you. Two small launchers install
[uv](https://docs.astral.sh/uv/) when it is missing and run it: `bootstrap.sh` on Linux and macOS, `bootstrap.ps1` on
Windows. From a fresh clone at `<workspace>/repos/coney`:

```sh
./bootstrap.sh                                               # Linux, macOS
powershell -ExecutionPolicy Bypass -File bootstrap.ps1       # Windows
```

When uv is missing, a launcher runs Astral's installer for the uv version pinned at its top
(`astral.sh/uv/<version>/install.sh`, or `install.ps1` on Windows). That installer puts uv in `~/.local/bin`
(`%USERPROFILE%\.local\bin` on Windows) and adds it to your `PATH`: in your shell profiles (`.profile`, `.bashrc`,
`.zshrc`, ...) on Linux and macOS, in your user `PATH` on Windows. Set `UV_NO_MODIFY_PATH=1` first to keep your
profiles as they are, or install uv yourself.

Or with no clone yet, from the folder that should become the workspace, with the launcher downloaded on its own and
the repository's address from the README: `sh bootstrap.sh --repo-url URL`. Inside a checkout the command is also
`uv run --project python coney-tools workspace init`. It:

1. creates the folders of [the layout](#the-layout) that are missing;
2. clones the repository into `repos/coney` when run outside a checkout (`--repo-url`);
3. downloads the research tools you ask for with `--fetch` (below);
4. writes `repos/coney/coney.local.toml` from the example when there is none, filling in `jdk_home` when it finds a
   JDK 21, and `ghidra_mcp_repo` and `pcsx2_mcp_dir` when those tools are in `tools/` (`pcsx2_mcp_dir` names the
   newest fork build, `tools/pcsx2-mcp/release/PCSX2-MCP-coney-v2.9.129.N-win64`, when one is there);
5. checks the prerequisites ([Building and testing](building.md#what-you-need)): Git, the C++ compiler, CMake 3.28+
   and Ninja, which the engine needs, and a JDK 21 and Node.js 18+, which only the research tools need, and ccache,
   which makes builds faster but is not needed. It prints what is missing and what to install; it installs none of it
   (ccache on Windows comes with `--fetch ccache`).

Every step that is already done is skipped, so running it again is safe; an existing `coney.local.toml` is never
changed. `--dry-run` prints the steps without doing any. `--workspace DIR` names the workspace instead of finding it
around the checkout (a checkout must sit in a workspace's `repos/` folder). The exit code is 1 when the engine's
prerequisites are missing, 2 for a usage error, 0 otherwise.

**Never your game or firmware.** The bootstrapper never downloads or asks for game files or a BIOS
([LEGAL.md](repo:LEGAL.md)). Put your own disc image in `game/`, and the BIOS dumped from your own console in each
PCSX2 copy's `bios/` folder.

### Fetching the research tools {#fetch}

Engineers studying the original need Ghidra and PCSX2. `--fetch TOOL` (repeatable, or `--fetch all`) downloads
each from its own project's GitHub releases (or clones its repository) at the version below, checks it against the
SHA-256 pinned in
`python/src/coney_tools/workspace_fetch.py` and unpacks it; downloads are kept in `scratch/downloads/`. Every file
of a tool is downloaded and checked before any is unpacked, and the tool's folder appears only once it is complete,
so a failed or interrupted fetch leaves nothing half-installed: run the command again to retry it. An archive entry
that would land outside the tool's folder is refused.

| `--fetch` | What | Into | Licence |
| --- | --- | --- | --- |
| `ghidra` | Ghidra 12.1.4, with the Emotion Engine extension v2.1.38 (built for 12.1.4) in its `Ghidra/Extensions/` | `tools/ghidra_12.1.4_PUBLIC` | Apache-2.0 both |
| `ghidra-mcp` | ghidra-mcp's repository at the commit Coney was set up with (7.0.0 pre-release) | `tools/ghidra-mcp` | Apache-2.0 |
| `ccache` | ccache 4.14.1, the [compiler cache](building.md#compiler-cache) `coney-tools build` uses; Windows only (the release CI's Windows job installs), on Linux and macOS install the `ccache` package with apt or Homebrew | `tools/ccache-4.14.1` | GPL-3.0 |
| `pcsx2-mcp` | the maintainer's PCSX2-MCP fork (`Ryubara/PCSX2-MCP`) at a pinned commit: its PCSX2 2.9.129 patch and the Node.js MCP server's source | `tools/pcsx2-mcp` | GPL-3.0 |

PCSX2 itself is not downloaded: every copy runs the fork's own build ([PCSX2 copies](#pcsx2-copies)).

What is left to do by hand, which the command reminds you of:

* **Ghidra:** compile the extension's `r5900.sla` once and import `SLUS_212.15`: `coney-tools ghidra setup` does both
  and applies our function names ([Starting your own project](ghidra.md#own-project)).
* **ghidra-mcp:** build its headless server (`target/GhidraMCPHeadless.jar`) with Maven 3.9+
  ([ghidra-mcp on your machine](ghidra.md#own-ghidra-mcp)); the agent bridge runs from the source as it is.
* **PCSX2-MCP:** build the fork as its README says (`pcsx2-plugin\prepare.ps1`, then CMake, then `npm ci` and
  `npm run build` in `pcsx2-mcp-server/`), into a release folder `release/PCSX2-MCP-coney-v2.9.129.N-win64` that
  holds PCSX2's program with `pcsx2-mcp-server/` beside it; point `pcsx2_mcp_dir` at it and run
  `coney-tools pcsx2 setup-copies` ([PCSX2 copies](#pcsx2-copies)). Each copy needs your BIOS.

To move to newer versions, change the pins (URL and SHA-256, or the commit) in `workspace_fetch.py`; GitHub shows
each release asset's SHA-256.

### PCSX2 copies {#pcsx2-copies}

Several agents run the original at once, each in its own portable PCSX2 copy under `pcsx2_root`
(`../../emulators`): `pcsx2`, `pcsx2-b`, `pcsx2-c`, ... (`pcsx2_copies`, 5 by default). Every copy runs the
PCSX2-MCP fork's build named by `pcsx2_mcp_dir` and listens on its own two ports, copy *i* (`pcsx2` is 0) on:

| Setting in `inis/PCSX2.ini` | Value | Port of `pcsx2` .. `pcsx2-e` |
| --- | --- | --- |
| `[EmuCore]` `EnablePINE`, `PINESlot` | `true`, `pcsx2_pine_base` + *i* | 28011 .. 28015 |
| `[EmuCore]` `EnableDebugServer`, `DebugServerPort` | `true`, `pcsx2_debug_base` + *i* | 21512 .. 21516 |
| `[InputSources]` `SDL` | `false`: the user's gamepad never drives an agent's game | |

`coney-tools pcsx2 setup-copies [--count N] [--dry-run]` makes the copies that are missing and updates the rest
([`pcsx2`](coney-tools.md#pcsx2)). It copies only the build's program files over a copy (and removes old program
files the build no longer has, such as a stock PCSX2's `updater.exe`), writes `portable.txt`, and sets the keys above
in the copy's ini, leaving every other setting as it was. A copy's own `inis/`, `bios/`, `memcards/`, `sstates/`,
`recordings/` and the other folders PCSX2 writes are never replaced. A new copy starts from the first copy's ini and
`bios/`. A copy whose PCSX2 runs is refused (exit code 1); close it and run the command again. Run it again after
every new fork build.

Save states made by another PCSX2 version (the copies ran a stock PCSX2 2.9.94 until 2026-10-20) stay in each copy's
`sstates/`, but PCSX2 2.9.129 may refuse to load them: make the states a task needs again on the new build.

## Creating the folders by hand

From the folder where you want the workspace (any name works):

```sh
mkdir Coney
cd Coney
mkdir repos worktrees scratch game emulators tools ghidra
git clone <the Coney repository URL> repos/coney
```

Then put your own files in place:

* `game/`: your disc image and the files you extract from it. Coney never ships these and you never commit them.
* `emulators/pcsx2/`, `pcsx2-b/`, ...: the PCSX2 copies, made by `coney-tools pcsx2 setup-copies`
  ([PCSX2 copies](#pcsx2-copies)), each with your BIOS.
* `tools/`: Ghidra 12.1.4 with the Emotion Engine extension (setup in [Ghidra + ghidra-mcp](ghidra.md)) and a
  JDK 21 if you do not have one elsewhere.

`ghidra/` stays empty until the Ghidra tooling creates a project in it.

## coney.local.toml {#local-config}

Tools find the sibling folders through `coney.local.toml` in the repository root. The file is untracked. Copy the
shipped example and adjust it:

```sh
cp coney.local.example.toml coney.local.toml
```

Paths are relative to the repository root or absolute. The keys and their defaults:

| Key | Default | What it points at |
| --- | --- | --- |
| `game_dir` | `../../game` | your disc image and files extracted from it |
| `ghidra_install` | `../../tools/ghidra_12.1.4_PUBLIC` | Ghidra 12.1.4 with the Emotion Engine extension |
| `ghidra_projects` | `../../ghidra` | local Ghidra projects |
| `pcsx2_dir` | `../../emulators/pcsx2` | portable PCSX2 with PINE enabled |
| `pcsx2_root` | `../../emulators` | folder holding the `pcsx2`, `pcsx2-b`, ... copies that `coney-tools pcsx2 claim` hands out |
| `pcsx2_copies` | `5` | a number: how many copies `pcsx2 setup-copies` makes |
| `pcsx2_pine_base` | `28011` | a number: the PINE port of copy `pcsx2`; copy *i* uses this + *i* |
| `pcsx2_debug_base` | `21512` | a number: the debug server port of copy `pcsx2`; copy *i* uses this + *i* |
| `pcsx2_claims_dir` | `pcsx2-claims` in the main checkout's `../../scratch` | where those claims live; one place every worktree sees, whatever its `scratch_dir` |
| `build_claims_dir` | `build-claims` in the main checkout's `../../scratch` | where `coney-tools build` keeps its slots and queue; one place every worktree sees, whatever its `scratch_dir` |
| `build_slots` | `2` | a number: how many builds and test runs `coney-tools build` lets run at once |
| `build_fast_lane_steps` | `50` | a number: a build with fewer steps out of date by themselves (`ninja -n -d explain` causes, [`build`](coney-tools.md#build)) uses the fast lane; `0` turns the lane off |
| `build_fast_lane_tests` | `20` | a number: a `-R` or `-L` test run that selects at most this many tests uses the fast lane; `0` turns it off |
| `ccache_dir` | `cache/ccache` in the main checkout's `../..` | the [compiler cache](building.md#compiler-cache) every checkout's `coney-tools build` shares |
| `ccache_program` | the copy `workspace init --fetch ccache` put in `../../tools/ccache-4.14.1`, else `ccache` on `PATH` | the ccache executable |
| `ccache_size_gb` | `25` | a number: the compiler cache's size cap in GB (ccache drops the least recently used objects beyond it); `0` turns the cache off |
| `scratch_dir` | `../../scratch` | captures and tool output |
| `jdk_home` | none: you must set it | a JDK 21 home, needed to run Ghidra |
| `ghidra_mcp_repo` | none: you must set it | a built ghidra-mcp checkout, used read-only |
| `pcsx2_mcp_dir` | none | a PCSX2-MCP fork build (`release/PCSX2-MCP-coney-v2.9.129.N-win64`): the copies' PCSX2 (`pcsx2 setup-copies`) and the `pcsx2` agent server in its `pcsx2-mcp-server/`; a server-only folder (`dist/index.js`) serves the agent server alone |
| `ffmpeg_dir` | none (`PATH`) | the folder holding `ffmpeg` and `ffprobe`, for in-game clips ([`clips`](coney-tools.md#clips)); Coney itself reads `CONEY_FFMPEG` |

Each line is `key = "value"` (the number keys are written without quotes: `build_slots = 2`); a `#` starts a
comment. A tool that needs a folder that does not exist stops with an
error naming the key, the path it resolved and the config file it used, so a wrong entry is easy to find.

## Worktrees

Extra worktrees go under `worktrees/coney/<branch>/` in the workspace and nowhere else, so they stay out of the
main checkout and share its siblings. From the repository root:

```sh
git worktree add ../../worktrees/coney/<branch> -b <branch>
```

A worktree has no `coney.local.toml` of its own, and needs none: every tool run from a worktree (`coney-tools`
and `tools/start-ghidra-mcp.ps1`) finds the main checkout through git's common dir, reads its `coney.local.toml` and
resolves the relative paths from the main checkout, so `../../game` is the same `game/` folder from every worktree.
A worktree that does hold its own copy uses that one instead, with paths relative to the worktree. Remove a
worktree when its branch is merged: `coney-tools workspace clean` does it for you ([Cleanup](#cleanup)).

## Cleanup and free space {#cleanup}

A build of the engine is about 6 GB per preset (`build/dev` measured 6.4 GB on 2026-10-20, with `/Z7` objects) and each
track has a worktree, so unremoved worktrees fill a drive quickly: once about ninety of them left 3 GB free and broke
builds. Two commands keep that from happening; run them from the main checkout or any worktree.

Deleting a `build/` folder is cheap since the shared [compiler cache](building.md#compiler-cache): a fresh `dev`
folder of the same sources builds in about 2 minutes from the cache (98.7 % hits) instead of about 11, so the rules
below still delete idle folders and agents still delete their own when they finish a unit. The cache itself, in
`cache/ccache`, is never deleted by `clean`: it keeps itself under `ccache_size_gb` (25 GB), which the drive's
30 GB floor allows for.

```sh
uv run --project python coney-tools workspace clean [--apply] [--build-hours N] [--worktree-hours N] [--fetch]
    [--min-gb N] [--margin-gb N] [--shell-hours N] [--log-idle-hours N]
uv run --project python coney-tools workspace disk [--min-gb N]
```

**`workspace clean`** is a dry run until `--apply` is given. It lists every worktree under `worktrees/coney/` and
what it would do, then the bytes it would reclaim, then the stale processes; with `--apply` it acts and prints the
free space before and after.

* **Removes a worktree** (`git worktree remove`, then its local branch) when all of these hold: no uncommitted
  changes (untracked files count), no `.coney-busy` file, not locked by git, not the folder you run it from, untouched
  for `--worktree-hours` (default 1, so a branch just created from `main` is not taken for a merged one), and every
  commit is merged. A branch is merged when `origin/main` contains it, or when GitHub's merged pull requests
  (read once for all worktrees: `gh pr list --state merged --limit 1000 --json headRefName,headRefOid`) include one
  from the branch whose head is the branch's tip or a later commit; squash merges leave the branch's
  commits out of `main`, so `git branch --merged` alone would keep them all. Without `gh` (or when it fails), only
  what `origin/main` contains counts, so run `git fetch` (or pass `--fetch`) first; the rest is kept, never guessed.
* **Deletes `build/`** (generated, ignored by git) from every worktree that is not busy and has not been touched for
  `--build-hours` (default `workspace.build_idle_hours`, 4; it is only a rebuild): the newest modification time of
  its tracked and untracked files, of git's `HEAD` for it and of the build folder and the entries one and two levels
  in (a running build writes there). Sources are never touched this way, so it also applies to a worktree with
  uncommitted or unmerged work.
* **Disk pressure** (`--min-gb`, default `workspace.disk_min_free_gb`, 30; `--margin-gb`, default
  `workspace.disk_margin_gb`, 10). After the two steps above, if the drive still has less than the floor free, `clean`
  deletes the `build/` folders of the remaining worktrees, **the one last used longest ago first**, until the free
  space reaches the floor plus the margin (40 GB by default), re-reading the free space after each. This is what keeps
  the drive above the floor when twenty-five open branches each hold a 6 GB build used within the last hours, which
  the age rule alone never touches. Only `build/` folders go in this mode, never a worktree, a source file or a branch,
  so uncommitted and unmerged work is safe. Skipped: a worktree with the busy file, the folder you run it from, a
  `build/` that git does not ignore, and a worktree that holds a build slot or waits for one (a slot claim or ticket
  in `scratch/build-claims/` naming it; without that folder, nothing is skipped on that account). Each deletion is
  printed with the free space before and after, and a line says so when the drive is still under the floor with no
  build folder left to delete. A dry run lists the deletions it would make, adding up the sizes.
* **Never touches** the main checkout, a worktree outside `worktrees/`, or folders git does not know as worktrees (it
  only names them; the loose files and logs in `worktrees/coney/` are for you to delete). A worktree whose folder is
  gone has its record pruned.
* Read-only files (git's packs, some tools' output) are cleared before deleting, and long Windows paths work.
* **Stops stale processes** (the tree under each, by process id, never by name;
  [`stale_processes.py`](repo:python/src/coney_tools/stale_processes.py)):
    * *a stale wait*: a shell (bash, sh, zsh, pwsh, PowerShell) older than `--shell-hours` (default 3) whose command
      sleeps in an `until` or `while` loop, when every file the loop reads (named in it, or held in a variable it
      reads: `f=/c/x/build.log; while ...; grep "$f"`) has not changed for `--log-idle-hours` (default 1) or does not
      exist. The run it waits for has died, so it would wait for ever and keep its finished agent listed as running.
      A loop that names no file it can check is never judged;
    * *an orphaned test run*: a `ctest` or `coney_tests` whose parent process is gone (or whose parent's pid now
      belongs to a younger process). Nobody reads its result, and an orphaned `ctest` goes on starting tests.

  It never stops the process running `clean` or any of its ancestors, and lists only the topmost stale process of a
  tree. On 2026-10-12 twenty such shells, hours old, and an orphaned `ctest` were found and killed by hand; the
  [bounded-waits hook](agents.md#bounded-waits) keeps new ones from being started.

**The busy file.** An agent creates an empty `.coney-busy` in its worktree root as it starts and deletes it when it is
done (`touch .coney-busy`; it is in `.gitignore`). While it exists, `clean` leaves that worktree and its `build/`
entirely alone, however old its files look: use it for any stretch where files may not change for hours (a long
build, a test sweep, a wait on the emulator). A file left behind by a dead agent protects its worktree for good;
delete it once you know the work is over. The file's name is `workspace.busy_marker` of the conventions file.

**`workspace disk`** prints the free space of the workspace's drive and exits 1 below `--min-gb` (default: 30, the
`workspace.disk_min_free_gb` key of [.github/conventions.yml](repo:.github/conventions.yml)), for hooks and scripts
(`coney-tools workspace disk || coney-tools workspace clean --apply`); a hook that runs `clean --apply` every half
hour keeps the drive above the floor on its own. The coordinator runs
`clean --apply` after every merge round (`coney-tools repo audit --local` lists what it would remove and the scratch
folders of finished work that still hold files taken from the original), and agents delete their own `build/` when they
finish a unit ([Briefing](agents.md#briefs)). `scratch/` is not cleaned automatically: prune built docs sites, logs and
captures there yourself (below).

**How long the audit takes.** `coney-tools repo audit --local` is meant to be run as a habit, so it is quick: GitHub is
asked once for the merged pull requests of every branch (not once per worktree), the worktrees are judged and the
scratch folders walked several at a time, and the last activity of a worktree is read only when the audit may report
it. Measured on 2026-10-12 on a workspace of 80 worktrees and 180 scratch folders holding 900,000 files: 11 seconds,
against about 3 minutes before the change (9.7 minutes on 2026-10-11, with more worktrees). The old and the new code
report the same 194 findings.

## Scratch

`scratch/` holds anything you generate while working: emulator captures, memory dumps, tool output, half-finished
experiments. It is never committed and nothing depends on it surviving. Prune it when the work that produced a file
is done, and keep out of it anything you would be sorry to lose; findings belong in
[research pages](research-workflow.md), not in scratch.

Name a scratch folder for its branch (`scratch/<branch>/`). `coney-tools repo audit --local` warns (`ws.scratch`) about
a folder whose branch is merged or gone and that still holds files taken from the original: the types in
`workspace.game_capture_suffixes` of [.github/conventions.yml](repo:.github/conventions.yml) (PCSX2 save states
and memory cards, traces, memory dumps and WAD entries: `.p2s`, `.ps2`, `.trace`, `.dump`, `.raw`, `.bin`), in any
folder. `.bin` is generic on purpose, so some build and terminal files match too: the walk never enters a build or
tool tree (`workspace.scratch_skip_folders`: `CMakeFiles`, `_deps`, `.gradle`, `build`, `venv`, `.venv`,
`site-packages`, `.git`, `node_modules`, in any case), and the finding lists the first five paths (relative to the
folder) and counts the rest: look at them before deleting. `.iso` is not listed: the scratch ISOs are hard links to
the user's disc and cost no space. They are of no use once the work is
finished, and they are the files that must never leave the machine. Screenshots are not looked at there: upload what
an issue or a pull request needs, then delete them. The folders in `workspace.shared_scratch` (the emulator's files,
the saved states, the PCSX2 claims, the downloads, the preview builds) belong to the workspace and are never reported.
