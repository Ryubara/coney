# Testing

Coney's tracks never stop to wait for someone to play their work: finished work merges once the automated checks
pass, and play-testing happens at milestones. That only works if the tests are strong enough to stand in for a
player, and if every track proves its work the same way. This page is that standard: what "done" means for each kind
of work, the rules every test follows, and where the checks run. How to build and run them is in
[Building and testing](building.md).

## What "done" means {#done}

A piece of work is done when the evidence in this table exists and passes. Report it with the work: in the pull
request's Testing and Screenshots sections ([What a pull request shows](#pr-evidence)).

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
  Every one carries the ctest label `disc` (the Catch2 tag `[disc]`), so `coney-tools build --test -L disc` runs them
  and nothing else ([`build`](coney-tools.md#build)).
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
checkpoint is not done until its playthrough test passes. Two drivers are the examples. The level3 chase's `RouteDriver`
(`repo:tests/platform/disc_story_playthrough_test.cpp`): route legs (run to a point, climb, sprint jump, sprint charge)
followed by reading where the player stands, through the game session (`DiscSession` takes a driver). Level 99's
`CourseDriver` (`repo:tests/platform/disc_level99_course_test.cpp`) is the other. A driver plays through
`coney::test::DiscSession` given a `LivePad` (`repo:tests/support/live_pad.h`), which it steers one frame at a time:
the hub's `Gym` driver (`repo:tests/platform/disc_hub_workout_test.cpp`) walks the player along the level's path data
with the stick at 60 %, plans again when he stops moving, and plays each gym station from what the scripts and the
player show.

A driver writes stick bytes as an input script does (`stickByteFromPercent`): each axis has its own dead zone round
the centre, so a byte mapped linearly over 0-255 reads back smaller than meant. A full diagonal laid out that way
reads about 0.86, under the run's 0.95, and the player only walks (found when the level3 chase driver walked instead
of running).

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

## What a pull request shows {#pr-evidence}

A reviewer should see what was tested and what the result looks like without running anything. The pull request
description ([the template](repo:.github/pull_request_template.md)) has two sections for it, and CI refuses a
description that leaves either empty (`coney-tools repo check-body`). All of what CI checks in a description is the
`pull_requests` section of [.github/conventions.yml](repo:.github/conventions.yml), shown here as a generated block
(`coney-tools repo render`):

<!-- conventions:begin pr-rules -->
- Sections, in this order: Summary, Checklist, Testing, Screenshots, AI use.
- Summary and Testing must hold text of their own: a section left as the template has it (its comments, `...`,
  `<reason>`) counts as empty.
- Docs: a change under `src/`, `python/src/` needs a change under `docs/`, `research/` or the line `Docs: none` in the
  description.
- Closing: a line `Fixes #N` in Summary for each issue the pull request finishes, one per line; merging closes it.
- Checklist: each unchecked item under `Remaining:` cites an issue or pull request as `#N`.
- Screenshots: an uploaded image (a path on your machine is refused) or a line `Screenshots: none (<reason>)` that gives
  a reason.
- AI use: after `Commits written by:` one of `a person`, `an AI`, `an AI, edited by a person`, `a person, with AI help`;
  unless it is `a person`, `Model:` names the model (`n/a` is not enough).
- The text names no path on your machine (`scratch/`, `../../`, `C:\`) outside an image reference.
<!-- conventions:end -->

What the two sections hold:

- **Testing** says what was run and what came out: the commands (the build preset, `ctest`, `pytest`,
  `mkdocs build --strict`), the tests added or changed, any disc, [playthrough](#playthrough-tests) or
  [trace-diff](#diff-against-the-original) run, and the results as counts, pass or fail and numbers ("612 passed, 0
  failed", "reached checkpoint 4 in 1,830 updates", "14 of 14 positions match"). Disc runs print aggregates only
  ([above](#no-game-data)). For a docs-only or research change, list the checks that ran (pre-commit, the docs build)
  and the runtime evidence behind the claims (the PCSX2 trace or memory read, with its address and evidence level).
  What could not be tested yet is listed as such, with the reason and the exact steps for whoever can.
- **Screenshots** shows anything a player can see, before and after: Coney before the change, Coney after it and,
  where the original has the same view, the original in PCSX2 from the same place, so a reader can compare them. When
  nothing visible changes, the section is one line, `Screenshots: none (<reason>)`, for example `Screenshots: none
  (no visible change)` or `Screenshots: none (research page only)`.

### Capturing and attaching {#pr-media}

- **Attach, never commit.** Gameplay screenshots and short videos of Coney or of the original are uploaded to the
  pull request or issue as GitHub attachments, with `gh --attach` (below) or, for a person, by dragging the file into
  the description box. They are never committed:
  game imagery stays out of git ([LEGAL.md](repo:LEGAL.md#no-game-data)); the only images in the repository are the
  small reference screenshots that page allows. Never attach assets, saves, dumps, executables, BIOS images or game
  text, only pictures of the running game.
- **Coney.** `coney --screenshot PATH` saves the last frame of a test-mode run as a PNG. To line a frame up with the
  original's, pin the view with `--camera`, `--freeze-world` and `--render-size 640x448`
  ([Matched views](building.md#matched-views)). `--screenshot` needs a window, so the run is not `--headless`. Take
  the "before" shot from a build of `main` (or the branch point) and the "after" shot from the branch, with the same
  options.
- **The original.** `coney-tools pcsx2 screenshot --copy NAME --out PNG` writes a PCSX2 copy's picture (Windows only;
  [Driving PCSX2](research-workflow.md#driving-pcsx2)), from a copy you hold a claim on.
- **Video.** Nothing in Coney or `coney-tools` records video. A short clip is made by hand with a screen recorder; an
  agent uses a few screenshots of the moment instead.
- **Agents** keep every capture in `scratch/<branch>/shots/` and write real image references in the pull request
  text (`PR.md`), paths relative to the scratch folder: `![Coney after](./shots/after.png)`. The coordinator uploads
  them from that folder, one `--attach` per file, when it opens the pull request:

  ```sh
  cd scratch/<branch>
  gh pr create --body-file PR.md --attach ./shots/before.png --attach ./shots/after.png
  ```

  `gh` replaces each reference whose path matches an `--attach` with the uploaded
  `https://github.com/user-attachments/assets/...` link (a file the text does not mention is appended); `--attach
  <file>#<alt>` sets the alt text. `gh pr edit <n> --body-file PR.md --attach ...` does the same later, and
  `gh pr comment` and `gh issue create` or `comment` take `--attach` too. It needs gh 2.99 or newer (`gh --version`).
  `check-body` refuses a Markdown image or `<img>` that still points at a local path, which is the reminder to
  attach it.
- **No local paths in the text.** A published pull request or issue body never names a file or folder on your machine:
  no `scratch/...`, `../../...`, `C:\...` or worktree path, and no "files in <folder>". Say "the scratch folder" and
  describe the picture. The only local paths a draft may hold are the `![alt](./shots/<file>.png)` image references that
  `gh --attach` rewrites to uploads. `check-body` refuses a body with any other such path in its text; code spans and
  code blocks are not looked at. The files are game data: they stay in scratch and are deleted when the pull request is
  merged ([Sharing one machine](how-we-work.md#shared-machine)).

## Where the checks run {#where}

### The local gate {#local-gate}

Before reporting work or merging it, in your own worktree, run the one command that checks what CI would fail on for
the files you touched, and fix what it prints:

```sh
uv run --project python coney-tools check changed
```

It runs pre-commit on the changed files, clang-tidy with the GCC and Clang warnings on the files CI's pull-request job
would check (a changed header through the `.cpp` files that include it), `mkdocs build --strict` when docs changed,
pytest and mypy when `python/` changed, and `repo check`; it needs a `build/dev` that was configured once
(`coney-tools build --configure-only --agent <id>`: configure, no build). It catches what MSVC lets through: a
designated initializer that skips a field (`-Wmissing-designated-field-initializers`), an unchecked `std::optional` access
(`bugprone-unchecked-optional-access`). [The command's page](coney-tools.md#check) has the options. It does not
replace the list below, which is the whole gate, and it leaves to CI the warnings only GCC emits, macOS, the
sanitizers and the full clang-tidy pass; a change to the build setup is checked on its changed files only.

The whole gate:

- **Engine changes:** the `ci` preset build and its tests with the disc configured, in one command:
  `uv run --project python coney-tools build --agent <id> --preset ci --test` (it waits for a free
  [build slot](coney-tools.md#build) and loads the MSVC environment itself; agents never run `cmake --build` or `ctest`
  directly).
- **Python:** `ruff check`, `ruff format --check`, `mypy` and `pytest` on `python/`; `coney-tools repo check`;
  `coney-tools progress check` (the progress inputs; its tables are built into the docs, not committed); and every
  generator's `--check` (`natives render`, `natives coney`, `natives cpp`, `refs render`, `missions render`). Rerun a
  generator without `--check` when you change what it reads.
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
