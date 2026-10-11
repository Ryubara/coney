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
  game/                         your disc image and files extracted from it
  emulators/pcsx2/              portable PCSX2 (plus BIOS)
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
   JDK 21, and `ghidra_mcp_repo` and `pcsx2_mcp_dir` when those tools are in `tools/`;
5. checks the prerequisites ([Building and testing](building.md#what-you-need)): Git, the C++ compiler, CMake 3.28+
   and Ninja, which the engine needs, and a JDK 21 and Node.js 18+, which only the research tools need. It prints what
   is missing and what to install; it installs none of it.

Every step that is already done is skipped, so running it again is safe; an existing `coney.local.toml` is never
changed. `--dry-run` prints the steps without doing any. `--workspace DIR` names the workspace instead of finding it
around the checkout (a checkout must sit in a workspace's `repos/` folder). The exit code is 1 when the engine's
prerequisites are missing, 2 for a usage error, 0 otherwise.

**Never your game or firmware.** The bootstrapper never downloads or asks for game files or a BIOS
([LEGAL.md](repo:LEGAL.md)). Put your own disc image in `game/`, and the BIOS dumped from your own console in PCSX2's
`bios/` folder.

### Fetching the research tools {#fetch}

Analysts need Ghidra and PCSX2 (implementers need neither). `--fetch TOOL` (repeatable, or `--fetch all`) downloads
each from its own project's GitHub releases at the version below, checks it against the SHA-256 pinned in
`python/src/coney_tools/workspace_fetch.py` and unpacks it; downloads are kept in `scratch/downloads/`. Every file
of a tool is downloaded and checked before any is unpacked, and the tool's folder appears only once it is complete,
so a failed or interrupted fetch leaves nothing half-installed: run the command again to retry it. An archive entry
that would land outside the tool's folder is refused.

| `--fetch` | What | Into | Licence |
| --- | --- | --- | --- |
| `ghidra` | Ghidra 12.1.4, with the Emotion Engine extension v2.1.38 (built for 12.1.4) in its `Ghidra/Extensions/` | `tools/ghidra_12.1.4_PUBLIC` | Apache-2.0 both |
| `pcsx2` | PCSX2 2.9.94: the Windows build (made portable with `portable.txt`), the Linux AppImage or the macOS app | `emulators/pcsx2` | GPL-3.0 |
| `ghidra-mcp` | ghidra-mcp's repository at the commit Coney was set up with (7.0.0 pre-release) | `tools/ghidra-mcp` | Apache-2.0 |
| `pcsx2-mcp` | PCSX2-MCP 1.0.0's Node.js server only (plain JavaScript, so it runs on every platform) | `tools/pcsx2-mcp` | GPL-3.0 |

What is left to do by hand, which the command reminds you of:

* **Ghidra:** compile the extension's `r5900.sla` once, as [Ghidra + ghidra-mcp](ghidra.md) shows.
* **ghidra-mcp:** build its headless server (`target/GhidraMCPHeadless.jar`) as its README says; the agent bridge
  runs from the source as it is.
* **PCSX2:** your BIOS, and PINE enabled in its settings ([Driving PCSX2](research-workflow.md#driving-pcsx2)). The
  PCSX2-MCP release also holds a modified PCSX2 for Windows; Coney does not use it and the bootstrapper does not
  install it.

The Windows PCSX2 is a 7z archive, which Python cannot read; the command extracts it with the `tar` that Windows 11
ships. An older Windows 10 `tar` may not read 7z: then the fetch stops with tar's error, and you extract the archive
from `scratch/downloads/` into `emulators/pcsx2` by hand (with 7-Zip) and create an empty `portable.txt` beside
`pcsx2-qt.exe`.

To move to newer versions, change the pins (URL and SHA-256, or the commit) in `workspace_fetch.py`; GitHub shows
each release asset's SHA-256.

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
* `emulators/pcsx2/`: a portable PCSX2 with the PINE slot enabled.
* `tools/`: Ghidra 12.1.4 with the Emotion Engine extension (setup in [Ghidra + ghidra-mcp](ghidra.md)) and a
  JDK 21 if you do not have one elsewhere.

`ghidra/` stays empty until the Ghidra tooling creates a project in it.

## coney.local.toml

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
| `pcsx2_root` | the main checkout | folder holding the `pcsx2`, `pcsx2-b`, ... copies that `coney-tools pcsx2 claim` hands out |
| `pcsx2_claims_dir` | `<scratch_dir>/pcsx2-claims` | where those claims live; one place every worktree sees |
| `scratch_dir` | `../../scratch` | captures and tool output |
| `jdk_home` | none: you must set it | a JDK 21 home, needed to run Ghidra |
| `ghidra_mcp_repo` | none: you must set it | a built ghidra-mcp checkout, used read-only |
| `pcsx2_mcp_dir` | none | PCSX2-MCP's Node.js server (`dist/index.js`) or the release folder holding it; only for the `pcsx2` agent server |

Each line is `key = "value"`; a `#` starts a comment. A tool that needs a folder that does not exist stops with an
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
worktree when its branch is merged: `git worktree remove ../../worktrees/coney/<branch>`.

## Scratch

`scratch/` holds anything you generate while working: emulator captures, memory dumps, tool output, half-finished
experiments. It is never committed and nothing depends on it surviving. Prune it when the work that produced a file
is done, and keep out of it anything you would be sorry to lose; findings belong in
[research pages](research-workflow.md), not in scratch.
