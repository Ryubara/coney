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

## Creating the folders

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

Each line is `key = "value"`; a `#` starts a comment. A tool that needs a folder that does not exist stops with an
error naming the key and the path it resolved, so a wrong entry is easy to find.

## Worktrees

Extra worktrees go under `worktrees/coney/<branch>/` in the workspace and nowhere else, so they stay out of the
main checkout and share its siblings. From the repository root:

```sh
git worktree add ../../worktrees/coney/<branch> -b <branch>
```

A worktree has no `coney.local.toml` of its own. Copy the main checkout's file into it; the default relative
paths then need one more level (`../../../`) unless you use absolute paths, so absolute paths are the easier choice
if you use worktrees. Remove a worktree when its branch is merged: `git worktree remove ../../worktrees/coney/<branch>`.

## Scratch

`scratch/` holds anything you generate while working: emulator captures, memory dumps, tool output, half-finished
experiments. It is never committed and nothing depends on it surviving. Prune it when the work that produced a file
is done, and keep out of it anything you would be sorry to lose; findings belong in
[research pages](research-workflow.md), not in scratch.

## Until the workspace move

Today the project still runs from a single folder that holds the game files, PCSX2 and the Ghidra databases inside
the checkout. `.gitignore` keeps a clearly marked `Transitional` block that ignores those folders, and the block is
deleted when the checkout moves into the layout above. Do not rely on it for new work.
