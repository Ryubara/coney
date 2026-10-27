# AGENTS.md -- Coney

Coney is an open-source, functional reimplementation of *The Warriors* (Rockstar Toronto, PS2, 2005) in C++23,
running natively on Windows, Linux and macOS from a disc the player owns. Anyone should be able to find, from our
docs and code comments, where each behaviour lives in the original and port it. Legal rules in full: `LEGAL.md`.

## Layout

`src/core/` platform-neutral engine code; `src/platform/` the only place for OS, SDL3 or librw code; `tests/`
Catch2; `python/` the `coney-tools` CLI (uv); `docs/research/` what we know about the original; `docs/guides/` how
we work; `assets/` Coney's own assets (sandbox layouts, CC0 textures; never game data); `cmake/deps.cmake` pinned
dependencies. Worktrees go in `../../worktrees/coney/<branch>/`, scratch output
in `../../scratch/`; game files, emulators and tool installs live beside the repo, never in it.

## How we work

Read `docs/guides/how-we-work.md` before starting: the pipeline from issue to merge, the slots (3 task engineers and 1
fix engineer), the coordinator, sharing one machine and merging. Whoever starts agents follows `docs/guides/agents.md`
(fork or fresh, which model, briefs, long commands and the prompt cache). Nobody waits for a person to play their work:
the automated tests are the gate, so every piece of work proves itself as `docs/guides/testing.md` defines ("done" per
kind of work, the player's path, gamepad input, playthrough tests, the checks to run before reporting). A person
contributing starts at CONTRIBUTING.md, section Your first hour; the rest of this file is for agents.

## Build and test

- Engine: `uv run --project python coney-tools build --agent <id> --preset dev --test` (presets `dev`, `release`,
  `ci`, `asan`); a hook refuses raw `cmake --build`, `ctest`, `ninja`: [`build`](docs/guides/coney-tools-build.md#build).
- Python: `uv run --project python pytest python/tests`; `uvx pre-commit run --all-files` before committing.
- Docs: `.venv/Scripts/python -m mkdocs build --strict`; `coney-tools docs check`. Details: `docs/guides/building.md`.
- On Windows, no local WSL builds or clang-tidy runs: GitHub CI is the Linux and macOS gate; fix failures forward.

## Rules (never broken)

- **No game data** anywhere in the repo, issues, fixtures or CI: no executables, BIOS, assets, extracted files or
  dumps. Tests use synthetic fixtures; disc-backed checks print counts and hashes only. Exception: curated
  reference lists (names, ids, values, our own short descriptions) and small reference screenshots of models;
  never asset files, game text or script source, per `LEGAL.md`. Issues may also hold gameplay screenshots and
  short videos of Coney or the original (a maintainer decision, 2026-10-11), never files, saves, dumps or text.
- **One role:** engineers study the original game (Ghidra through ghidra-mcp, the running game through the pcsx2 MCP
  server), write `docs/research/` pages where they help, citing addresses and short snippets, and implement the
  behaviour in Coney's own code (`docs/guides/research-workflow.md`).
- **Research claims** carry an evidence level: confirmed (code) at a cited address, confirmed (runtime), inferred,
  or speculative. Addresses are like `0x001490b8` in NTSC-U `SLUS_212.15`. How: `docs/guides/research-evidence.md`.
- **Reverse-engineering work** (Ghidra, disassembly, PCSX2, research claims) runs on Opus 5.5.
- **Faithful first:** gameplay matches the original exactly until the game is complete. Coney's own mechanics and
  enhancements wait (designs may be written now); debug tooling is not a mechanic and is fine.
- **Never exclude a file from the 512 KB large-file check** in `.pre-commit-config.yaml`: it catches game data
  committed by mistake. Split big generated data and pages instead.
<!-- conventions:begin maintainer-decisions -->
- **Maintainer decisions** are never an agent's or a coordinator's: it stops and reports instead of acting. They are:
  make the repository public; tag a release; force-push main or someone else's branch; legal or licence questions;
  change the installer or launcher; change these conventions.
<!-- conventions:end -->
- Whoever pushes a pull-request branch may `git push --force-with-lease` it.
- **Local first; no agent pushes or waits on CI.** An engineer takes one claimed task through the
  [pipeline](docs/guides/how-we-work.md#the-pipeline) to a passing `task review`; the coordinator opens the pull
  request with [`coney-tools pr open`](docs/guides/pr.md#open).

## Code

- C++23 within MSVC 19.38+, GCC 13+, Clang 17+ (19+ on Linux), Xcode 16+; clang-format (`.clang-format`) decides layout;
  no exceptions: recoverable errors return `std::expected<T, coney::Error>`, broken invariants `CONEY_ASSERT`.
- Reimplemented code cites the original (`@orig 0x<addr> <Name> (<File>.cpp)` and its research page); other gameplay
  code says `@coney-only <reason>` (`conventions.md#coney-only`); script-usable ones: `#coney-only-capabilities`.
- A doc comment on every public type and function, and a short comment above every other function (helpers,
  private members, long lambdas) and before each step of a long one; comments explain *why*, briefly.
- Keep the test mode possible (fixed timestep, seeded randomness, scripted input): never assume real time, a display
  or a human. Tests for everything testable without the game, written first: `docs/guides/testing.md`.
- Write for networked play: state owned by the game mode, player slots not one player, ids not pointers, gameplay
  randomness only from `GameRandom`, simulated effects as events: `docs/guides/network-architecture.md#checklist`.
- Port, never transcribe: keep the logic, write Coney's own code (`conventions.md#porting`); the rest: `conventions.md`.

## Documentation

Document as you work: a change that alters behaviour, a command or what we know updates its living doc in the same
commit, and a page you touch meets [the docs standard](docs/guides/writing-docs.md#touch-it) as a whole. One document
per subject, updated in place: findings in `docs/research/`, how-tos in `docs/guides/`, the why in code comments.
Mission work updates `research/missions.yaml` (status, checkpoints) in the same commit and reruns `coney-tools missions
render`. CI refuses a pull request that changes `src/` or `python/src/` without changing `docs/` or `research/`, unless
its description has a line `Docs: none`. `HANDOFF.md` holds only the current state of the work (local, never committed).

## Commits and GitHub

Every rule below that a program checks is a key of `.github/conventions.yml`, the one source: titles, description
sections, labels, merge settings, hygiene limits and path classes (`docs/guides/repo-conventions.md`). Change the
rule there, never a copy of it. The block below is generated from it (`coney-tools repo render`): edit the file, not
the block.

<!-- conventions:begin commit-rules -->
- Title `area: Verb the rest`, at most 72 characters, present tense, no trailing period. Verbs: Add, Fix, Update,
  Remove, Rebuild, Rework, Improve, Document. Areas: `build`, `ci`, `core`, `gameplay`, `fileio`, `graphics`, `audio`,
  `platform`, `tools`, `research`, `agents`, `tests`, `legal`, `docs`. A breaking change ends the title with
  `(BREAKING)`. What each area covers: `commits.title` in `.github/conventions.yml`, which CI checks PR titles against.
- A body says what changed for a user or developer and why, wrapped at 72 columns. An agent ends the message with
  `Co-Authored-By: <model name> <noreply address>` (the vendor's noreply address: `noreply@anthropic.com`,
  `noreply@google.com`, `noreply@openai.com`) and no other trailer. No other email address appears in a message.
<!-- conventions:end -->

- No file, code comment or commit message contains the repository's or the docs site's address; only the README
  does. Docs link files outside `docs/` as `repo:<path>`.
- Work happens on branches, one pull request per feature. `main` takes no direct pushes: it is protected, and a
  pull request must pass every required check. A pull request is **squash-merged**, so it becomes one commit on
  `main` with the pull request's title; commits on the branch need not be tidy.
- A pull request description starts from `.github/pull_request_template.md` (`gh pr create` does not fill it in
  when given a body). Agents write it to a file; the coordinator opens the pull request. An agent never merges a
  pull request: the maintainer does, or a coordinator the maintainer has allowed to, and only by squash once every
  required check passes and the pull request follows these rules. A pull request or issue you write carries no
  "Generated with" line; the template's AI use section is where the model is named.
- A task's status lives in its task file, unit record, stamp log and `STATUS.md` in its scratch folder, not in a draft
  pull request (`docs/guides/how-we-work.md`, "Unit status").
