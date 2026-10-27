# Contributing to Coney

Thank you for helping. People and AI agents follow the same rules here, and no AI tooling is required to
contribute. If you only read one other file first, read [LEGAL.md](LEGAL.md).

## Before you start

- Read [LEGAL.md](LEGAL.md). The short version: no game data anywhere in the project.
- Contributors study the original game (in Ghidra and running in PCSX2), write `docs/research/` pages where they
  help and implement the behaviour in Coney's own code. See [the research workflow](docs/guides/research-workflow.md).
- Research lives in `docs/research/`. If the page you need does not exist or is wrong, fixing the research is a
  contribution in its own right. Each claim carries an evidence level and, where it applies, an address.

## Your first hour

From nothing to a first pull request, as a person on Windows, Linux or macOS. It needs no AI tooling, no Ghidra, no
emulator and no BIOS, and no disc until step 4. Each step links to the page that holds the detail.

**What you need.** A C++23 toolchain (Windows: Visual Studio 2022 17.8 or the Build Tools with *Desktop development
with C++*; Linux: GCC 13+ or Clang 19+ and the SDL packages; macOS: Xcode 16+), CMake 3.28+ and Ninja, Git, and
[uv](https://docs.astral.sh/uv/) for the `coney-tools` command line. The exact versions and the Linux package list
are in the [README](README.md#requirements) and [Building and testing](docs/guides/building.md#what-you-need).
Node.js 18+ makes the formatting hooks faster (otherwise pre-commit downloads its own). clang-tidy needs no
install: CI runs it on the lines of every pull request. For step 4, your own copy of
the North American (NTSC-U) game: the research is checked against its executable `SLUS_212.15`; other releases are
untested. Once uv is there, `uv run --project python coney-tools doctor` in the clone checks all of this, your disc
included, and prints how to fix each problem ([Check your setup](docs/guides/building.md#doctor)).

1. **Clone.** The command is in the [README](README.md#clone); without write access, fork first and follow
   [Working from a fork](#working-from-a-fork). A clone anywhere works: the tools then keep their logs, scratch
   files and compiler cache in a folder beside it, `<clone>-workspace` (`~/code/coney` uses `~/code/coney-workspace`),
   never inside the repository. The tidy layout for more than a first change is to clone into
   `<workspace>/repos/coney` and run `./bootstrap.sh` there (`powershell -ExecutionPolicy Bypass -File bootstrap.ps1`
   on Windows): it makes the sibling folders (`scratch/`, `tools/`, ...) the tools then use, writes
   `coney.local.toml`, installs uv when it is missing, fetches the compiler cache on Windows and prints what else you
   lack ([Bootstrap](docs/guides/workspace.md#bootstrap)).
2. **Build and run the tests.** No game files are needed:

    ```sh
    uv run --project python coney-tools build --preset dev --test
    ```

    Your user name labels the build's log and slot (`--agent NAME` picks another; the project's agents must pass
    it). It downloads and builds the dependencies, then Coney, then runs the tests (2,688 of them, none needing the
    game).
    On Windows it loads the Visual Studio environment itself, so any shell works. The first time is long: about 11
    minutes to build on an idle 16-core machine, 57 on a busy one without a compiler cache, then up to half an hour of
    tests. To get going sooner, build only the program with `--target coney` (no tests), and later run only the tests
    you need with `-R <regex>`. Install [ccache](docs/guides/building.md#compiler-cache) and the next fresh build takes
    minutes; later builds recompile only what changed. Without uv, `cmake --preset dev`, `cmake --build --preset dev`
    and `ctest --preset dev` do the same, in a shell with the compiler on its path
    ([README](README.md#build-and-test)).
3. **Run it.** `build/dev/src/platform/coney` (`coney.exe` on Windows). With no arguments it opens a window; `--headless
   --frames 3` runs with no window or GPU. Escape closes it.
4. **Run it with your disc.** Give `--disc` a mounted disc, a folder holding `WARRIORS.DIR`, `WARRIORS.WAD` and
   `SLUS_212.15`, or an ISO image you made of your own disc with any disc-imaging tool:

    ```sh
    build/dev/src/platform/coney --disc /path/to/warriors.iso
    uv run --project python coney-tools doctor --disc /path/to/warriors.iso   # checks it is the NTSC-U release
    ```

    The tests that need the disc skip without it. To run them, set `CONEY_DISC` to the same path and run the build
    command with `-L disc` ([Building and testing](docs/guides/building.md#build-and-test)). Never copy anything from
    the disc into the repository, an issue or a pull request ([LEGAL.md](LEGAL.md)). What every option does:
    [Running Coney](docs/guides/running.md).
5. **Pick a first change.** The safest first pull requests change no gameplay: a fix to a docs or research page
   (every page ends with what it was checked against; a wrong claim is worth fixing), a test, a tool or build
   problem, or an issue labelled `good first issue` or `help wanted` in the issue tracker. A change to what a player sees
   (`gameplay`, `graphics`, `audio`) must also be compared with the original running, which needs the research tools
   below; talk to a maintainer in the issue first. Take the issue before you start: comment `/claim` on it, and a
   GitHub Action assigns it to you, so nobody else works on it ([Claiming an issue](docs/guides/issues.md#claiming)).
   Write the test first ([Testing](docs/guides/testing.md)) and
   keep the code in the style of [Conventions](docs/guides/conventions.md).
6. **Check, commit, open the pull request.** Branch from `main`, make the change, then run the one command that does
   what CI would fail on for the files you touched (for the docs check it needs the setup in [Writing these
   docs](docs/guides/writing-docs.md#setup-once); CI adds clang-tidy on your changed lines, the other compilers and the
   sanitizers):

    ```sh
    uv run --project python coney-tools check changed
    ```

    Commit with a title of the form `area: Verb the rest` ([Commits](#commits)), push the branch to your fork and
    open the pull request. Start its description from the [template](.github/pull_request_template.md), answer every
    section (a docs-only change says `Screenshots: none (no visible change)`), and check it with `coney-tools repo
    check-body` ([Setting up](#setting-up)). A maintainer approves CI for a first pull request and reviews it; you
    do not merge it yourself.

**Beyond the first hour: studying the original.** Reading the game's code and watching it run is optional and
heavier. It needs a JDK 21, Ghidra 12.1.4 with the Emotion Engine extension and Maven to build ghidra-mcp
([Ghidra + ghidra-mcp](docs/guides/ghidra.md)), and a PCSX2 build with the debug server patch plus a BIOS dumped from
your own console ([PCSX2 copies](docs/guides/workspace.md#pcsx2-copies)). On Windows, `coney-tools workspace init
--fetch pcsx2-mcp-release` downloads the fork's release (PCSX2 with the debug server and the MCP server, checked by
SHA-256) and `coney-tools pcsx2 setup-copies` makes the copies from it; building the fork from source is only for
changing it. The fork is built for Windows only, so on Linux and macOS only the Ghidra half is available today.
With a JDK 21 installed, `coney-tools ghidra setup` downloads Ghidra and the extension, imports `SLUS_212.15` from
your disc into your own project and gives its functions the names the maintainers published
([Starting your own project](docs/guides/ghidra.md#own-project)); the rest you set up by hand.
Two things are not shared yet, so expect them to be missing on your machine: the plate comments, types and structures
of the maintainers' Ghidra project (only the function names are published, in `docs/progress/ghidra-functions.tsv`,
and the research pages), and the PCSX2 save states and traces. Start from the research page for the subsystem you
want ([Research workflow](docs/guides/research-workflow.md)).

**What is for the project's agents, not for you.** The coordinator's build queue and claims (`pcsx2 claim`,
`build` slots, `STATUS.md`), the task files, stamps and `pr open` that turn their work into pull requests
([How we work](docs/guides/how-we-work.md#the-pipeline)), the Tether and Claude Code configuration
(`.claude/`, `.mcp.json`), `PR.md` and the `reviewer`, and most of [How we work](docs/guides/how-we-work.md) and
[Running agents](docs/guides/agents.md). You can ignore all of it; a pull request you open yourself follows the
same rules in the template. One thing reaches you if you use Claude Code in this checkout: start it with
`uv run --project python coney-agent engineer`, because a plain `claude` session here may only read files
([Which role to start](docs/guides/agents.md#roles)). The engineer role brings the project's agent instructions
(worktrees, scratch folders, the local checks, reporting to a coordinator) and refuses `git push` and `gh pr create`
inside that session: push your branch and open your pull request from your own terminal.

**Stamps are local agent bookkeeping.** The stamp log (`docs/guides/stamps.md`) is what the project's agents keep on
their own machine to work in order; it is never checked in CI. Working by hand you need no stamps, no `coney-tools`
and no Claude Code: the local checks are optional, and CI runs them on your pull request like anyone's, which a
maintainer reviews.

## Setting up

The checks below are what CI runs on every pull request; `coney-tools check changed` runs them for you.

CI checks every pull request's title against the rules under [Commits](#commits), because the squash merge makes it the
title of the one commit on `main`; the titles of the commits on the branch are not checked. To check a title yourself,
with [uv](https://docs.astral.sh/uv/) installed:

```sh
echo "docs: Add a page" | uv run --project python coney-tools repo check-title -
```

CI also refuses a pull request that changes `src/` or `python/src/` without changing `docs/` or `research/`; put a
line `Docs: none` in the description when the change needs no documentation. To check it yourself:

```sh
git diff --name-only main... | uv run --project python coney-tools repo check-docs
```

CI also checks the pull request description against [the template](.github/pull_request_template.md), including one
created with `gh pr create --body-file`, and reruns when you edit it. These rules are the `pull_requests` keys of
[.github/conventions.yml](.github/conventions.yml), and the block is generated from them (`coney-tools repo render`):

<!-- conventions:begin pr-rules -->
- Sections, in this order: Summary, Issues affected, Checklist, Testing, Screenshots, AI use.
- Summary and Testing must hold text of their own: a section left as the template has it (its comments, `...`,
  `<reason>`) counts as empty.
- Docs: a change under `src/`, `python/src/` needs a change under `docs/`, `research/` or the line `Docs: none` in the
  description.
- Closing: a line `Fixes #N` in Summary for each issue the pull request finishes, one per line; merging closes it.
- Checklist: every item is ticked (`- [x]`): a pull request lists only the work it finishes, and a task it does not
  finish is left out and lives in its own issue.
- Issues affected: one line `- #N: how it is affected` for each open issue the pull request partly fixes, changes the
  behaviour of or makes out of date, or the single line `None (<reason>)` with any short reason, for example
  `None (one-line fix)`. `coney-tools issue impact` suggests the list; running it is optional.
- Screenshots: an uploaded image (a path on your machine is refused) or a line `Screenshots: none (<reason>)` that gives
  a reason.
- Testing: when the title's area is one a player sees (`gameplay`, `graphics`, `audio`), a line starting `Runtime:` with
  the evidence from the original running (a trace diff, an event diff, or named PCSX2 captures or reads compared with
  Coney's, with numbers) or `Runtime: none (<reason>)`. It is checked given the title (CI's `body` job and
  `repo check-review` give it).
- AI use: after `Commits written by:` one of `a person`, `an AI`, `an AI, edited by a person`, `a person, with AI help`;
  unless it is `a person`, `Model:` names the model (`n/a` is not enough).
- `Model:` also names every model that a `Co-Authored-By` trailer on the branch's commits names (any case, anywhere on
  the line), whoever wrote them. It is checked where the branch is at hand: `repo check-review`, and `repo check-body`
  given `--worktree`; CI's `body` job sees only the description.
- The text names no path on your machine (`scratch/`, `../../`, `C:\`) outside an image reference.
<!-- conventions:end -->

A pull request lists only the work it finishes; file an issue for anything else you find
([Issues](docs/guides/issues.md#follow-up-work)). To check a description yourself:

```sh
uv run --project python coney-tools repo check-body --body description.md --title "gameplay: Fix the thing"
```

The documentation must build with `.venv/Scripts/python -m mkdocs build --strict`, which rejects broken links and
pages missing from the navigation. The setup, the Linux and macOS form and how to write the pages are in
[Writing these docs](docs/guides/writing-docs.md).

Game files, emulators and tool installs live outside the repository, as siblings of the checkout. The layout is
described in [docs/guides/workspace.md](docs/guides/workspace.md).

## Commits

A commit title is:

```text
area: Verb the rest
```

The length, the tense and the areas and verbs are `commits.title` in
[.github/conventions.yml](.github/conventions.yml) ([Repository conventions](docs/guides/repo-conventions.md)); the rules
are written out in [AGENTS.md](AGENTS.md#commits-and-github), and this block, generated from them
(`coney-tools repo render`), shows the areas:

<!-- conventions:begin commit-areas -->
| Area | Covers |
| --- | --- |
| `build` | CMake, presets, dependency pins and toolchain setup |
| `ci` | GitHub Actions workflows and pre-commit configuration |
| `core` | engine level only: main loop and timing, memory, chunks, the task engine's plumbing, shared utilities (src/core) |
| `gameplay` | the game's rules a player meets: combat, AI and crew behaviour, movement and physics responses, missions and scripts, objects and pickups, HUD and hint logic, camera behaviour |
| `fileio` | disc and WAD access, file systems and streaming (src/fileio) |
| `graphics` | rendering through librw: models, textures, effects, drawing the HUD (src/graphics) |
| `audio` | sound, music and speech playback; the rules that trigger a sound stay in their gameplay area (src/audio) |
| `platform` | SDL3 and OS glue: windowing, input, timing (src/platform) |
| `tools` | C++ asset tools and the coney_tools Python package |
| `research` | the research pages under docs/research and the research database |
| `agents` | AGENTS.md, the CLAUDE.md and GEMINI.md pointers and other agent instructions |
| `tests` | test-only changes |
| `legal` | LICENSE, LEGAL.md and other licensing or provenance files |
| `docs` | project-wide documentation: README, guides and the docs site |

Verbs: Add, Fix, Update, Remove, Rebuild, Rework, Improve, Document.
<!-- conventions:end -->

Commits on a branch are dropped by the squash merge, so they need not be tidy or build one by one; what must hold is
the branch's final state, the pull request title and its description. Where you can, make one commit per logical
change, with a test in the same commit as the code that makes it pass: it is easier to review.

The body has an opening paragraph saying what changes and why, then `Changes:` with one bullet per part when there
are several, then `Verified:` with the commands you ran and what they printed. For example:

```text
docs: Add the contributor documents

README, contributing guide, the Contributor Covenant, the security
policy and the pull request template.

Verified: mkdocs build --strict passes
```

```text
legal: Update the provenance rule for commits written by agents

A commit an agent writes names the model in a Co-Authored-By line,
so the history shows which work an agent did.

Changes:
- LEGAL.md: describe the agent trailer
- AGENTS.md: point at the rule

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```

## Agents' commits

A commit an AI agent wrote names the agent in a trailer: the model's name and the vendor's noreply address, and no
other email address:

```text
Co-Authored-By: <model name> <its noreply address>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```

The person who submits or merges the work answers for it. The full rule is in [LEGAL.md](LEGAL.md#provenance).

## Pull requests

- Every pull request is squash-merged: its commits become one commit on `main`, and the pull request title becomes
  the commit title, so it follows the same `area: Verb the rest` format. A pull request holds one feature, so that
  the commit it becomes also reverts as one. The commits inside it are not checked; the title is.
- CI must pass.
- Fill in the pull request template: a summary, a checklist of what the pull request finishes and what is still to
  do, what you tested and the results, before and after screenshots of anything a player can see (attach them to the
  pull request; never commit them), and who wrote the work in the commits (a person, an AI, or both) and with which
  model. Who opened the pull request does not matter there. CI refuses a description that leaves out a section or an answer.
- Keep a pull request to one subject. Unrelated changes belong in separate pull requests.
- The project's agents never open their own pull requests: the coordinator releases each one with `coney-tools pr
  open` ([pr open](docs/guides/pr.md#open)). It checks the pull request, rebases and pushes the branch, opens the pull
  request and turns on auto-merge, except for a change to a path the maintainer merges by hand (`merge.manual_paths`
  in `.github/conventions.yml`, such as `AGENTS.md`). A pull request you open yourself needs none of those steps; the
  rules above still apply.

### Working from a fork

Without write access to the repository, work from a fork:

1. Fork the repository on GitHub, clone your fork, and add the project's repository as a second remote called
   `upstream`.
2. Create a branch from `main` for each change, never work on your fork's `main`: `git switch -c <branch>`.
3. Run the checks above, commit with titles in the `area: Verb the rest` form, and push the branch to your fork.
4. Open a pull request from your branch into the project's `main`; GitHub offers it as a banner on the project's page.
   A maintainer approves CI for a first-time contributor, then the pull request is reviewed and squash-merged.
5. Push further commits to the same branch to update the pull request. Run `git fetch upstream` and rebase on
   `upstream/main` if it conflicts. Leave "Allow edits by maintainers" on, so a maintainer can fix a small thing
   in place.

## Agents

AI agents read [AGENTS.md](AGENTS.md) first; it is the single instruction file for every agent in this repository.
The repository also ships the agent tool configuration (`.mcp.json` for the Ghidra and PCSX2 debugging servers,
`.claude/agents/` for the roles, `.claude/settings.json` for shared permissions); what each holds and how to set
it up: [Setting up an agent tool](docs/guides/agents.md#setup).

The project's own agents work through the [pipeline](docs/guides/how-we-work.md#the-pipeline): a claimed task, the
stamps of each stage, a review by the `reviewer` agent with `coney-tools task review` (it opens every screenshot and
checks the text against the diff and the test logs, [Task review](docs/guides/review/task.md)) and `pr open`. If you
work with an agent of your own, you need none of that: your pull request follows the rules above.
