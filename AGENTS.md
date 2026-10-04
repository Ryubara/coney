# AGENTS.md -- Coney

Coney is an open-source, functional reimplementation of *The Warriors* (Rockstar Toronto, PS2, 2005) in C++23,
running natively on Windows, Linux and macOS from a disc the player owns. Anyone should be able to hand-write a
subsystem from our docs and comments alone. Legal rules in full: `LEGAL.md`.

## Layout

`src/core/` platform-neutral engine code; `src/platform/` the only place for OS, SDL3 or librw code; `tests/`
Catch2; `python/` the `coney-tools` CLI (uv); `docs/research/` what we know about the original; `docs/guides/` how
we work; `cmake/deps.cmake` pinned dependencies. Worktrees go in `../../worktrees/coney/<branch>/`, scratch output
in `../../scratch/`; game files, emulators and tool installs live beside the repo, never in it.

## Build and test

- Engine: `cmake --preset dev && cmake --build --preset dev && ctest --preset dev` (Windows: from a shell with the
  MSVC environment, `vcvars64.bat`). Presets `dev`, `release`, `ci` (warnings as errors), `asan` (Linux/macOS).
- Python: `uv run --project python pytest python/tests`; `uvx pre-commit run --all-files` before committing.
- Docs: `.venv/Scripts/python -m mkdocs build --strict`. Build details: `docs/guides/building.md`.

## Rules (never broken)

- **No game data** anywhere in the repo, issues, fixtures or CI: no executables, BIOS, assets, extracted files or
  dumps. Tests use synthetic fixtures; disc-backed checks print counts and hashes only.
- **Clean room:** only analysts read decompiler output or disassembly, and they write `docs/research/` pages that
  cite addresses and short snippets. Engine code is written fresh from those pages, never transcribed.
- **Research claims** carry an evidence level: confirmed (code) at a cited address, confirmed (runtime), inferred,
  or speculative. Addresses are like `0x001490b8` in NTSC-U `SLUS_212.15`. How: `docs/guides/research-workflow.md`.
- **Reverse-engineering work** (Ghidra, disassembly, PCSX2, research claims) runs on Opus 5.5.
- Making the repo public, tagging releases, force-pushing and legal or licence questions are maintainer decisions.

## Code

- C++23 within MSVC 19.38+, GCC 13+, Clang 17+, Xcode 16+; clang-format (`.clang-format`) decides layout; no
  exceptions: recoverable errors return `std::expected<T, coney::Error>`, broken invariants `CONEY_ASSERT`.
- Reimplemented code cites the original: `@orig 0x<addr> <Name> (<File>.cpp)`, plus a link to its research page.
- A doc comment on every public type and function; comments explain *why*. Keep the engine's test mode possible
  (fixed timestep, seeded randomness, scripted input): never assume real time, a display or a human.
- Tests for everything testable without the game. Details: `docs/guides/conventions.md`.

## Documentation

Document as you work: a change that alters behaviour, a command or what we know updates its living doc in the same
commit. One document per subject, updated in place: findings in `docs/research/`, how-tos in `docs/guides/`, the
why in code comments. `HANDOFF.md` holds only the current state of the work (local, never committed).

## Commits and GitHub

- Title `area: Verb the rest`, at most 72 characters, present tense, no trailing period. Verbs: Add, Fix, Update,
  Remove, Rebuild, Rework, Improve, Document. Areas: `build`, `ci`, `core`, `fileio`, `graphics`, `audio`,
  `platform`, `tools`, `research`, `agents`, `tests`, `legal`, `docs` (list and meanings:
  `.github/commit-conventions.json`, which CI checks PR titles against).
- A body says what changed for a user or developer and why. An agent ends the message with
  `Co-Authored-By: <model name>`, with no email address anywhere in the message.
- Work happens on branches; before a push the branch is squashed into feature-sized commits. Only `main` and
  release branches are pushed.
