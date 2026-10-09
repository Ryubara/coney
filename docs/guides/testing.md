# Testing

Coney's tracks never stop to wait for someone to play their work: finished work merges once the automated checks
pass, and play-testing happens at milestones. That only works if the tests are strong enough to stand in for a
player, and if every track proves its work the same way. This page is that standard: what "done" means for each kind
of work, the rules every test follows, and where the checks run. How to build and run them is in
[Building and testing](building.md).

## What "done" means {#done}

A piece of work is done when the evidence in this table exists and passes. Report it with the work.

| Work | Evidence |
| --- | --- |
| A format, parser or other pure code | Unit tests on synthetic fixtures; a disc test that prints aggregates when the disc is involved |
| Engine behaviour that a player sees | Unit tests where it can be isolated, and a disc test that starts through [the player's path](#test-through-the-players-path) |
| A mission checkpoint | Its [playthrough test](#playthrough-tests) passes; `research/missions.yaml` updated; an [event diff](#diff-against-the-original) against the original where a recording exists |
| A bug fix | A test that fails before the fix and passes after it |
| A research finding | An address and an [evidence level](research-workflow.md#evidence-levels) on every claim; runtime claims checked in the emulator; a [trace diff](research-workflow.md#comparing-with-coney) where a scenario exists |
| Python tools and generators | pytest, ruff and mypy; every generator's `--check` |
| Docs | `mkdocs build --strict` and markdownlint |

## Kinds of tests

- **Unit tests** (Catch2 under `tests/`, mirroring `src/`) cover everything that can be tested without the game.
  They are written first.
- **Smoke tests** run the `coney` executable itself under `ctest`: start-up, argument errors, scripted input.
- **Disc tests** need the player's own disc, found through `coney.local.toml` or `CONEY_DISC`. They skip without it.
- **Playthrough tests** are disc tests that play a mission checkpoint from start to end through the pad.
- **Event diffs** replay a recorded run of the original into Coney and compare the ordered events.
- **Trace diffs** record the same scripted scenario on the original and on Coney, per update, and compare the
  values ([Comparing with Coney](research-workflow.md#comparing-with-coney)).

## Rules every test follows

### Synthetic fixtures, aggregates only {#no-game-data}

Format code is tested on small fixtures **we write** byte by byte, never on files taken from the disc. Disc tests
print only aggregates (counts, hashes matched), never content. See [LEGAL.md](repo:LEGAL.md#no-game-data).

### No run waits for a click {#no-dialogs}

Tests run with nobody at the keyboard, so nothing may open a dialog. On Windows, the debug C runtime opens a modal
"Debug Assertion Failed!" box for its own checks (`front()` on an empty vector, an index out of range), and the run
hangs on it until the timeout. `coney` and `coney_tests` call `platform::reportErrorsToConsole()` before anything
else, so these reports go to stderr and the process ends at once; the `coney.error_dialogs_off` test checks it in
MSVC debug builds. In tests, check a container's size (`REQUIRE(!starts.empty())`) before reading `front()`,
`back()` or an index, so a missing value fails one test cleanly instead of ending the whole run.

### Test through the player's path {#test-through-the-players-path}

The game is set up one way. The story from the main menu, `coney --play-level LEVEL --checkpoint N` and the debug
menus' jumps all run in one `platform::GameSession` (`src/platform/game_session.h`): the start-up flow, gameplay with
its loading screen, intro movie, pause and mission screens, the play mode, the game's sound and the debug menus'
services. A direct start differs from the story only in the level and checkpoint it starts at: it skips the movies,
the legal screen, the memory-card check and the menus, and nothing else. New set-up code goes into the session, never
into one of the paths.

Disc tests that play a level start it there too: `coney::test::DiscSession` (`repo:tests/support/disc_session.h`) is
the session as `coney --play-level` runs it in test mode, with the offline sound output and a pad script that starts
at the level's first frame of play. A test that builds its own gameplay or level scripts (the older `LevelScripts`
harnesses) checks one subsystem only, and says so; it is not evidence that the game works for the player.
`repo:tests/platform/disc_game_session_test.cpp` checks that a direct start and a jump from the story's front end
give `level99` checkpoint 2 the same set-up.

`--start` (which places the player somewhere) and `--scene` (which plays one scene) skip the level script's own
timing, so they are tools for looking at something, never for a verdict: keep them out of tests and out of the lines
handed to play-testers.

Why: play-testing found three bugs (no glass sound after a pause, a mission that stopped at checkpoint 2, cheats that
did nothing) that every test missed, because the tests and `--play-level` set the game up differently from the story.
When a play-tester sees a bug that a test does not, first ask which path each one took.

### Drive it like a gamepad {#gamepad}

All movement is driven as a gamepad with analog sticks, in Coney (`--input-script` `stick` lines) and in the
emulator (the pad patches in [Driving PCSX2](research-workflow.md#driving-pcsx2)). Use realistic partial deflections
and state the magnitude, not only full deflection: the game's walk, run and dead-zone thresholds only show between
the extremes, so keyboard-style input hides behaviour.

### Mission playthrough tests {#playthrough-tests}

A mission is tested by an **adaptive playthrough**, not by a frame-locked pad script. A driver written for the mission
plays it through the pad, with analog sticks at realistic deflections, and decides each input from what is on screen:
the objective marker, which callbacks are armed, where the NPCs stand. The test asserts that

- every hint and help message fires, in the order the original showed them;
- each objective is detected and completes;
- scenes play in order;
- the run reaches the next checkpoint.

The reference order comes from an analyst's recording of the original, written on the research page. A mission
checkpoint is not done until its playthrough test passes. Level 99's `CourseDriver`
(`repo:tests/platform/disc_level99_course_test.cpp`) is the example.

Why not a frame-locked script: it replays inputs by step number, so any change to the camera or the AI moves the
characters a little, the recorded presses then land in the wrong place, and the test fails (or passes by luck) for
reasons that have nothing to do with the mission. A driver that reads the state and steers toward the marker
survives those changes, and a failure means the mission really cannot be finished. Like every disc test, it skips
without a disc and prints only aggregates.

### Diff against the original {#diff-against-the-original}

A recorded run of the original, with the same pad input from the same start, is replayed into Coney, and the two
ordered event lists (hints, objectives, spawns, scenes, sounds) are compared. Whatever Coney lacks or orders
differently becomes a fix or a research question. The runs start through the player's path on both sides.

### Stand-ins are visible {#stand-ins}

Where a research page is silent, code uses a clearly marked stand-in (`stand-in` or `Coney's choice` in a comment) and
the page gets an open question. A test may pass on a stand-in, but the stand-in stays counted until a faithful
reimplementation replaces it.

## Where the checks run {#where}

### The local gate {#local-gate}

Before reporting work or merging it, in your own worktree:

- **Engine changes:** the `ci` preset build and `ctest --preset ci` with the disc configured (Windows: from a shell
  with the MSVC environment).
- **Python:** `ruff check`, `ruff format --check`, `mypy` and `pytest` on `python/`; `coney-tools repo check`; and
  every generator's `--check` (`progress update`, `natives render`, `natives coney`, `natives cpp`, `refs render`,
  `missions render`). Rerun a generator without `--check` when you change what it reads.
- **Everything:** `uvx pre-commit run --all-files` leaves the tree unchanged, markdownlint passes, and
  `mkdocs build --strict` passes.

Commands and presets: [Building and testing](building.md).

### Continuous integration {#ci}

CI builds and tests on Windows, Linux (GCC and Clang) and macOS, with sanitizers and clang-tidy, and runs the Python
and docs checks. It is the gate for the platforms you are not on: do not run virtual-machine or cross-platform builds
locally to stand in for it, and fix a red CI forward rather than leaving `main` red. Code that MSVC accepts but the
others reject is the usual cause: see [Portability traps](conventions.md#portability-traps).

## Play-testing {#play-testing}

People play a release build at milestones, from one list of what changed and how to reach it (commands that start
through the player's path). Nobody waits for them: work merges on the automated gate. A finding comes back as a fix
on the owning track, starting with a test that reproduces it. A mission stays **Pending approval** until a maintainer
has played it ([Missions](research-workflow.md#missions)).
