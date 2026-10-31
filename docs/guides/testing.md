# Testing

Coney's engineers never stop to wait for someone to play their work: finished work merges once the automated checks
pass, and play-testing happens at milestones. That only works if the tests are strong enough to stand in for a
player, and if every task proves its work the same way. This page is that standard: what "done" means for each kind
of work, the rules every test follows, and where the checks run. How to build and run them is in
[Building and testing](building.md).

## What "done" means {#done}

A piece of work is done when the evidence in this table exists and passes. Report it with the work: in the pull
request's Testing and Screenshots sections ([What a pull request shows](#pr-evidence)).

Gameplay work is checked against **the original running**, not only against our reading of its code (maintainer
decision, 2026-10-20): code read in Ghidra can be misread, and a test written from that reading passes on the
misreading. So the rows for work a player sees ask for a [runtime comparison](#runtime-comparison), and the pull
request says so in a `Runtime:` line that CI checks ([below](#runtime-line)).

| Work | Evidence |
| --- | --- |
| A format, parser or other pure code | Unit tests on synthetic fixtures; a disc test that prints aggregates when the disc is involved |
| Engine behaviour that a player sees | Unit tests where it can be isolated, and a disc test that starts through [the player's path](#test-through-the-players-path); a [runtime comparison](#runtime-comparison) with the original; before and after captures of Coney and the original from the same spot for anything visible ([What a pull request shows](#pr-evidence)) |
| A mission checkpoint | Its [playthrough test](#playthrough-tests) passes; `research/missions.yaml` updated; an [event diff](#diff-against-the-original) against the original where a recording exists; a [runtime comparison](#runtime-comparison) of the checkpoint's moments with the original; before and after captures of Coney and the original from the same spot for anything visible |
| A bug fix | A test that fails before the fix and passes after it (a test that guards a refactor of behaviour that already matched is [stamped `keeps`](#keeps)); in a player-facing area (`gameplay`, `graphics`, `audio`), also a [runtime comparison](#runtime-comparison) with the original, and before and after captures of Coney and the original from the same spot for anything visible |
| A research finding | An address and an [evidence level](research-evidence.md#evidence-levels) on every claim; runtime claims checked in the emulator and citing their [run record](research-evidence.md#run-records); `research claims` clean on the pages changed; a [trace diff](research-traces.md#comparing-with-coney) where a scenario exists |
| Python tools and generators | pytest, ruff and mypy; every generator's `--check` |
| Docs | `mkdocs build --strict`, markdownlint and [`docs check`](writing-docs.md#checking) on the pages touched |

### A test that cannot fail first {#keeps}

A test that guards a refactor, a performance change, or behaviour Coney already has and the original matches (a disc
test that pins strike timing, a lookup that must find the same brain as the scan it replaces) passes before the change as
well as after it, so it can never fail first: it is stamped `keeps` instead, with `orig fails-first --keeps`
([Keeps](stamps.md#keeps) says how, and what the reviewer confirms). It is no way round a test that should fail: a test
for a bug fix or new behaviour fails first, or the reviewer refuses the unit.

### The runtime comparison {#runtime-comparison}

Before work that a player sees is done, its behaviour is compared with the original **running**, in one of two ways:

- **A trace diff** ([Comparing with Coney](research-traces.md#comparing-with-coney)): the same scripted scenario
  recorded on the original and on Coney, compared per update with `coney-tools trace diff`. Use it for numbers that
  change every update: positions, headings, health, timers, the camera. The result is the columns checked, the
  updates compared and the first step outside tolerance (or none).
- **The same moment from a PCSX2 save state and from Coney**, captured and compared: load the saved slot in a PCSX2
  copy you hold a claim on, read the values or take the picture
  ([Driving PCSX2](research-tools.md#driving-pcsx2); the ready-made recipes for it are `coney-tools pcsx2 recipes`,
  which is being added and gets its own guide page), put Coney in the same place, and compare the named values or the
  two pictures. Use it for one-off facts: a constant, a flag, a state after an event, how a scene looks.

An [event diff](#diff-against-the-original) counts for what it covers (hints, objectives, spawns, scenes, sounds). The
evidence names what was compared and gives numbers: "trace diff walk60, 600 updates, columns x y heading, 0 outside
tolerance", "PCSX2 slot 3 reads 0x00271f40 = 4, Coney's field = 4". A disagreement is not hidden: it becomes a fix
in the same pull request, or an open question on the research page and a marked [stand-in](#stand-ins) in the code.
The captures and logs stay in the scratch folder while the pull request is open, because the reviewer opens them
([Task review](review/task.md)).

A change the original cannot say anything about has no comparison, and says so with a reason that is not
convenience: "refactor with no change to behaviour (the existing trace diff still shows 0 differences)", "the
original has no such case (Coney's debug menu)". "Not checked" or "no time" is not a reason, and neither is "the copy
was busy" when a PCSX2 copy could have been claimed.

Before implementing from a research page, the engineer **re-reads the page's cited addresses in Ghidra** and compares
them with what the page says. Where they disagree, the engineer records the disagreement on the page (the address, what
the page said, what the code does, the evidence level) before writing the code, so the next reader is not misled
either ([Studying the original](research-workflow.md#studying-the-original)).

## Kinds of tests

- **Unit tests** (Catch2 under `tests/`, mirroring `src/`) cover everything that can be tested without the game.
  They are written first.
- **Smoke tests** run the `coney` executable itself under `ctest`: start-up, argument errors, scripted input.
- **Disc tests** need the player's own disc, which they read from `CONEY_DISC`. `coney-tools build --test` sets it from
  `game_dir` in `coney.local.toml` when the environment does not, and its summary says whether the disc tests ran or
  were skipped ([the disc for the tests](coney-tools-build.md#build)); without a disc they skip, and ctest counts a skip
  as a pass, so a suite is not whole while that line says `SKIPPED`. Every one carries the ctest label `disc` (the Catch2
  tag `[disc]`), so `coney-tools build --test -L disc` runs them and nothing else ([`build`](coney-tools-build.md#build)).
- **Playthrough tests** are disc tests that play a mission checkpoint from start to end through the pad.
- **Event diffs** replay a recorded run of the original into Coney and compare the ordered events.
- **Trace diffs** record the same scripted scenario on the original and on Coney, per update, and compare the
  values ([Comparing with Coney](research-traces.md#comparing-with-coney)).

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

### Python tests do not depend on the shell {#python-environment}

A Python test gives the same answer in a task's shell (`CONEY_TASK` and `VIRTUAL_ENV` set), inside an agent tool
(`CLAUDECODE`, `CONEY_AGENT_TOKEN`), in CI (`GITHUB_REPOSITORY`) and on a machine with a disc (`CONEY_DISC`).
`python/tests/conftest.py` makes it so: an autouse fixture removes the variables the tools read before each test, and
`python/tests/ambient_env.py` is the one list of them, each with what it changes. A test that needs one sets it with
`monkeypatch.setenv`; it never has to `delenv` one "in case" (a helper that a test calls several times may clear a
variable again between its calls). Two things stay: the machine's own variables every program needs (`PATH`), and the
switches a developer sets on purpose (`CI`, `CONEY_UPDATE_FIXTURES`, `CONEY_ENGINE_STUB`, `CONEY_TYPESCRIPT`), which
reach their tests.

`test_test_environment.py` keeps the list from falling behind. It scans `python/src`, `.claude` and `mkdocs_hooks.py`
and fails when the code reads a variable that is in none of the module's tables: a name read through `getenv`,
`env_get`, `env.get`, `environ[...]`, `"X" in env` or `"X" not in env`, or spelled with one of our prefixes (`CONEY_`,
`PCSX2_`, `GHIDRA_`, `CLAUDE_`, `GITHUB_`, `CMAKE_`). It cannot see a name held in a variable and read from there, so a
tool that does that adds the name to `ambient_env.py` by hand. It also runs one test of each kind that failed under
`CONEY_TASK` in a shell with `CONEY_TASK`, `VIRTUAL_ENV` and `CLAUDECODE` set. A tool that starts reading a new variable
adds it to `ambient_env.py` in the same commit.

A test that runs a shell script gets its `bash` from `python/tests/shell_support.py`, never from
`shutil.which("bash")`. On a Windows machine with the Windows Subsystem for Linux that call can find System32's
`bash.exe`, a Linux that sees the checkout as `/mnt/c/...` and has no `uv`, so a script fails or, behind a hook
command's trailing `exit 0`, prints nothing and the test reads empty output. `git_bash()` returns the bash beside
`git` (Git for Windows') on Windows, the one on `PATH` elsewhere, and None when there is none, and `needs_bash` skips a
test with `no_bash_reason()` for that case. `test_shell_support.py` covers the choice with a made-up `PATH`.

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
emulator: a PCSX2 [input recording](research-tools.md#input-recordings) for a fixed run, or the
[`pad`](pcsx2-recipes/input.md#pad) recipe for a driver that reacts to the game. The other things a runtime check sets up
in the original (the player found, a human teleported or spawned, a checkpoint loaded) are
[PCSX2 recipes](pcsx2-recipes.md) too. Use realistic partial deflections
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

The reference order comes from a recording of the original, written on the research page. A mission
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

A driver plays as fast as a player who wants to win would, not merely fast enough for today's AI. The level3 chase's
driver ran and walked to its climbs, and passed only because Coney let the crew turn back and fight the pursuers off.
Once the chapter's own lock on the Warrior commands took effect, the crew ran on ahead and the pursuers beat Snow
down. The dispatcher's refusal of a disabled command is confirmed (code); that the chapter's Setup turns the commands
off, and the pursuers' speed, are inferred from the research ([level3](../research/scripting.md#level3),
[Automatic commands](../research/ai-gangs.md#warrior-auto-commands),
[the devil run](../research/ai-scripting.md#devil-run)). It still passed for only half of the AI's seeds: it slowed to a
walk before each climb, and the pursuers, who speed up as they fall behind, caught it there; the same route driven
in the original lost Snow the same way. Now it sprints into the climbs (a running player takes a face 4.5 m off,
[Climbing](../research/characters.md#climb)), charges again when a charge leaves the barrier standing, and the test
runs every `--ai-seed` from 0 to 7, so a pass is no longer one lucky sequence. A jump leg also ends when he lands
after running off the edge past its take-off: a tap on the last update before the edge comes too late to jump, the
lower roof beyond still catches him, and a driver that kept waiting for the jump stood still until the pursuers came.
A climb counts only once it has taken him up or over the face: when the sliding doors began to work, the mimes came
out of their lift as the original's do, a climb cut short in the scramble left the driver standing at a wall it took
for climbed, and it now climbs again.

A driver that fights holds the stick toward the foe it means to hit while it presses the attack, as a player does: a
player's attack searches for its target along the stick's heading, and along his facing only when the stick is at
rest ([Targets](../research/combat-targets.md#targeting)). A driver that rests the stick once in reach strikes whoever
the player happens to face; the level5 boss fight's driver did, and lost once the AI's enemy scan changed who the
crew fought.

Why not a frame-locked script: it replays inputs by step number, so any change to the camera or the AI moves the
characters a little, the recorded presses then land in the wrong place, and the test fails (or passes by luck) for
reasons that have nothing to do with the mission. A driver that reads the state and steers toward the marker
survives those changes, and a failure means the mission really cannot be finished. Like every disc test, it skips
without a disc and prints only aggregates.

A fight is chaotic: a change of 0.1 mm in one update can send it down another course, so the commit a bisect of a
failing fight names may only have nudged it. Before blaming that commit, print the positions, health and goal states
every update on both sides of it and find the first update that differs: if the two runs agree until a difference in
the last digit, the fault is in the course the fight took after it. The level5 boss fight bisected to the physics
sweep (#434), which moved a boss 0.0001 m; the real fault was a capped boss whose hits were never counted.

### One test per item {#one-test-per-item}

A suite is only as fast as its slowest ctest test, and ctest runs a test as one process: a loop over the level's
checkpoints, a `GENERATE` over the AI's seeds or a `for` over the missions is one test however many items it holds, and
nothing else of it runs in parallel. The disc tests that play each checkpoint, mission or seed in turn were the long
ones (the level3 chase over eight seeds alone was one test of 16 minutes, a suite of 21).
So a test that plays a list of items registers **one test case per item** with `coney::test::registerEach`
(`repo:tests/support/each_case.h`, tested in `repo:tests/support/each_case_test.cpp`):

```cpp
constexpr std::array<std::uint32_t, 8> kSeeds{0, 1, 2, 3, 4, 5, 6, 7};
void playChase(std::uint32_t seed);  // opens the disc (SKIP without it), plays, checks

[[maybe_unused]] const bool kChaseRegistered = coney::test::registerEach(
    kSeeds, "[disc][story][playthrough]",
    [](std::uint32_t seed) { return std::format("the level3 chase for AI seed {}", seed); },
    [](std::uint32_t seed) { playChase(seed); });
```

Every item keeps every assertion, a name of its own (the seed, the level and checkpoint) and the tags, so ctest lists,
selects (`-R`) and times it, and a failure names the item. The tags must say `[disc]` when the body skips without the
disc (a Python test checks every `registerEach` and `registerCase` call as it checks `TEST_CASE`s, and fails on a call
whose tags it cannot read). Split a test when its items are independent and take long enough to matter (a case under a
few seconds stays a loop); a scenario whose stages build on one another, as the level34 checkpoint 3 riot (stereos, then
loot, then the meter), stays one test. `build --test` then runs the cases `-j` at a time, longest first once a run has
recorded their costs ([How long the tests take](building.md#test-timings)).

### Cite the expected values {#citations}

Every test case says where its expected numbers come from, so a reviewer can check the claim and not only the test. A
line in the case (or in the comment block right above it) names the research section that gives the values:

```cpp
TEST_CASE("a front grab on a standing target plays the grab intro") {
    // Expected: combat/grabs.md#grab, confirmed (code)
    ...
}
```

The form is `// Expected: <page>.md#<anchor>` with the page below `docs/research` (a leading `research/` is read too) and
the claim's evidence level after a comma. A value no research claim backs yet is written `// Expected: unverified, #<issue>`
with an issue that verifies it, never left bare. `coney-tools research claims` lists the claims a branch changed with the
tests that cite their sections, and `coney-tools standards` finds the cases that cite nothing: the test files that do not
yet are listed, and each test case a change touches, and every case of a test file it adds, is held to the rule
([the standards](coney-tools-standards.md#standards)). The marker, the macros and the form are `standards.tests` in
[.github/conventions.yml](repo:.github/conventions.yml), and both commands read the form from there, so a citation
with or without the leading `research/` counts for both.

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
description that leaves either empty (`coney-tools pr check-body`). All of what CI checks in a description is the
`pull_requests` section of [.github/conventions.yml](repo:.github/conventions.yml), shown here as a generated block
(`coney-tools repo render`):

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
  `legacy check-pr-review` give it).
- AI use: after `Commits written by:` one of `a person`, `an AI`, `an AI, edited by a person`, `a person, with AI help`;
  unless it is `a person`, `Model:` names the model (`n/a` is not enough).
- `Model:` also names every model that a `Co-Authored-By` trailer on the branch's commits names (any case, anywhere on
  the line), whoever wrote them. It is checked where the branch is at hand: `legacy check-pr-review`, and
  `pr check-body` given `--worktree`; CI's `body` job sees only the description.
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
- **Runtime:** for a title in an area a player sees, one line in Testing that reports the
  [runtime comparison](#runtime-comparison) ([The Runtime line](#runtime-line)).
- **Screenshots** shows anything a player can see, before and after: Coney before the change, Coney after it and
  the original in PCSX2 from the same place, so a reader can compare them (a view the original cannot show is
  said so, with the reason, in the caption). When
  nothing visible changes, the section is one line, `Screenshots: none (<reason>)`, for example `Screenshots: none
  (no visible change)` or `Screenshots: none (research page only)`.

### The Runtime line {#runtime-line}

For a title in an area a player sees (`gameplay`, `graphics`, `audio`; the key `pull_requests.runtime` of
[.github/conventions.yml](repo:.github/conventions.yml)), the Testing section holds one line starting `Runtime:`. It
gives the evidence of the [runtime comparison](#runtime-comparison) with numbers (a trace diff, an event diff, or named
PCSX2 captures or reads compared with Coney's), or `Runtime: none (<reason>)`:

```text
- Runtime: trace diff walk60, 600 updates, columns x y heading: 0 outside tolerance
- Runtime: PCSX2 slot 3 reads 0x00271f40 = 4 after the hit; Coney's field = 4 (same scenario, 12 updates later)
- Runtime: none (refactor with no change in behaviour; the existing trace diff still shows 0 differences)
```

`coney-tools pr check-body --title "<title>"` refuses the description without it. CI's `body` job runs that command
with the pull request's title, and `legacy check-pr-review` runs it with line 1 of `PR.md`, so the message is the same
locally and in CI. The check refuses a missing, empty or numberless line (and `none` without a reason); the reviewer
refuses one with nothing behind it in the scratch folder ([Task review](review/task.md)). Any
other title area does not need the line: leave it out, or keep it when the original running says something about the
change (`pr text` takes it from any area).

### Capturing and attaching {#pr-media}

!!! note "For the project's agents"

    The parts about `scratch/<branch>/shots/`, the coordinator attaching pictures and PCSX2 claims are for the project's
    agents. A person opening their own pull request drags the PNG into its description box.

- **Attach, never commit.** Gameplay screenshots and short videos of Coney or of the original are uploaded to the
  pull request or issue as GitHub attachments, with `gh --attach` (below) or, for a person, by dragging the file into
  the description box. They are never committed:
  game imagery stays out of git ([LEGAL.md](repo:LEGAL.md#no-game-data)); the only images in the repository are the
  small reference screenshots that page allows. Never attach assets, saves, dumps, executables, BIOS images or game
  text, only pictures of the running game.
- **Coney.** `coney --screenshot PATH` saves the last frame of a test-mode run as a PNG. To line a frame up with the
  original's, pin the view with `--camera`, `--freeze-world` and `--render-size 640x448`
  ([Matched views](running-play-level.md#matched-views)). `--screenshot` needs a window, so the run is not
  `--headless`. Take the "before" shot from a build of `main` (or the branch point) and the "after" shot from the
  branch, with the same options.
- **The original.** `coney-tools pcsx2 screenshot --copy NAME --out PNG` writes a PCSX2 copy's picture (Windows only;
  [Driving PCSX2](research-tools.md#driving-pcsx2)), from a copy you hold a claim on.
- **Look at each picture before using it.** Open every capture and check that it shows what its caption says:
  gameplay, not a letterboxed scene, a loading or black screen or a menu; the right level and place; the frame and
  options the text states. A before and after pair must differ: two identical shots mean a capture went wrong, so take
  it again. The `reviewer` agent checks the same before release, and `legacy check-pr-review` refuses byte-identical
  images ([Task review](review/task.md)).
- **Video.** A short clip (10 seconds at most) of Coney comes from `coney --record`, one of the original from
  `coney-tools clips record`, and both go up only with `coney-tools clips publish`: the rules and how each is kept are
  in [Clips](#clips) below. Never a screen recorder.
- **Agents** keep every capture in `scratch/<branch>/shots/` and write real image references in the pull request
  text (`PR.md`), paths relative to the scratch folder: `![Coney after](./shots/after.png)`. `coney-tools pr open`
  ([pr open](pr.md#open)) does the attaching when the coordinator releases the unit: it runs from that folder and passes
  one `--attach` per file (on an update only the pictures that are new), then reads the description back to check that
  each reference became an upload address and no local path is left. Underneath it is this, which is what a person
  runs by hand from their own terminal or fork (in a Claude Code session in this repository the PR guard refuses it:
  [After the switch](pr.md#after-the-switch)):

  ```sh
  cd scratch/<branch>
  gh pr create --title "<title>" --body-file PR.md --attach ./shots/before.png --attach ./shots/after.png
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

### Clips {#clips}

!!! note "For the project's agents"

    `coney-tools clips publish` and the rules below are how the project's agents publish clips. A person drags a short
    clip into the pull request or issue on GitHub; the no-game-data rule still applies.

A short video shows a bug or a fix better than screenshots: a before and after of Coney, and the original from the
same place. Agents record and publish them themselves (the user's request, 2026-10-12), under five rules. Each is
kept by the code, not by memory or by this page:

| Rule | How the code keeps it |
| --- | --- |
| **Only the game's own picture.** Never a screen, desktop or window capture. | Coney's frames are read back from its own frame buffer (`RenderEngine::setFrameTap()`) and piped to FFmpeg's standard input, its only input (`clips::videoEncodeArguments()`, tested). The original's come from PCSX2's own capture of its GS output (`clips record`), re-encoded from that file. Every FFmpeg command `coney-tools` runs goes through `clips.run_ffmpeg`, which refuses any capture device (`gdigrab`, `ddagrab`, `x11grab`, `avfoundation`, `dshow`, `lavfi`, ...) and any input that is not a file; a test checks that `clips record` never calls `pcsx2 screenshot`'s window read. |
| **Nothing private in the frames or the file.** No debug overlay, log, FPS counter, path, user name or window chrome; no metadata. | Coney reads the frame before the debug menus and the developer overlay are drawn and switches the debug lines off while recording (`--record-debug-draw` keeps the 3D lines the run itself switched on, such as a collision wireframe, never text); there is no window chrome in a frame buffer. `clips record` turns every PCSX2 on-screen display element off for the run. Every encode strips metadata and chapters (`-map_metadata -1`, `-map_chapters -1`), writes bit-exact headers and drops x264's settings note; a clip's name is plain letters, digits, `.`, `_`, `-`. `clips check` and `clips publish` refuse a clip whose ffprobe tags go beyond the fixed ones every MP4 carries. |
| **Deleted once uploaded.** | `coney-tools clips publish` is the only upload: it posts the comment (or description) with `gh --attach` and deletes each clip only when gh reports success, keeping them and saying so otherwise. `coney-tools clips clean` lists the clips older than 24 hours in every task's clips folder, and with `--apply` deletes them. |
| **Only in the scratch folder.** `<workspace>/scratch/<task>/clips/`, never in a worktree. | `coney --record`, `clips record`, `clips check` and `clips publish` refuse a path inside any git checkout or outside a `clips` folder (`clips::checkClipPath()`, `platform::checkClipLocation()`, `clips.check_clip_path`). |
| **Short.** 10 seconds by default and at most. | `--record-seconds` and `clips record --seconds` refuse more; every encode cuts at the length (`-t`); `clips publish` refuses a longer file. The cap is `workspace.clips.max_seconds` in [.github/conventions.yml](repo:.github/conventions.yml), and a test checks that Coney's `kMaxClipSeconds` equals it. |

The steps, for an agent:

```sh
CLIPS=$(uv run --project python coney-tools clips folder my-task)   # makes and prints the absolute <workspace>/scratch/my-task/clips
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level level99 --input-script walk.txt --record "$CLIPS/after.mp4"
uv run --project python coney-tools clips check "$CLIPS/after.mp4"
uv run --project python coney-tools clips publish --pr 123 --body "Coney after the fix" "$CLIPS/after.mp4"
# the original: --iso and --scratch are needed when coney.local.toml has no game_dir or scratch_dir; --out is absolute, from any checkout
uv run --project python coney-tools clips record STATE --out "$CLIPS/original.mp4" --agent ID --iso /path/to/warriors.iso --scratch /path/to/scratch
```

An agent that cannot publish (agents do not post to GitHub: the coordinator does) names its clips in its report and
leaves them in the clips folder; the coordinator publishes them after it opens the pull request, and
`clips clean --apply` removes any left behind. Never attach a clip with `gh` directly, never commit one, and never
reference one in `PR.md` (`pr check-body` refuses the local path): the clip goes in as a comment of its own. The
commands: [coney-tools clips](coney-tools-pcsx2.md#clips), [Recording a clip](running-options.md#record).

## Where the checks run {#where}

### The local gate {#local-gate}

The gate is `coney-tools task check`, the Check stage of [the pipeline](how-we-work.md#the-pipeline): on the committed
tip of a task it runs the whole list below for the change's kind and writes the `checked` stamp that Review and
`pr open` require ([profiles and checks](coney-tools-repo.md#task-check)). It leaves to CI the warnings only GCC emits,
macOS, the sanitizers and clang-tidy.

```sh
uv run --project python coney-tools task check <task>
```

While working, one command checks what CI would fail on for the files you touched; it is an aid, not the gate, and
the quickest check for a person contributing without a task:

```sh
uv run --project python coney-tools ci run
```

It runs pre-commit on the changed files, the page standards on the changed pages and `mkdocs build --strict` when docs
changed, ruff, pytest and mypy when `python/` or `.claude/` changed, `orig check-tags` when `src/` changed (a new
gameplay function carries `@orig` or `@coney-only`), and `repo check-conventions`. [The command's
page](coney-tools-ci.md#ci-run) has the options.

**In other agent tools.** An agent tool that is not Claude Code (Codex, Gemini CLI, Antigravity, Cursor, Copilot, Grok
Build) runs the same hooks once the person trusts them ([Agent tools](enforcement.md#harnesses)), but its agent opens no
pull request, so before the person opens one they run `coney-tools ci pr --title "<title>" --body <file>`: the same
steps, preceded by the title, the description and the docs rule and followed by the engine's build and tests ([`ci pr`](coney-tools-ci.md#ci-pr),
[Agent tools other than Claude Code](agents-other-tools.md)). A tool that cannot reach the disc, Ghidra or PCSX2 names
that in the Testing section instead of claiming a check it could not run.

**clang-tidy is CI's alone.** It is not run locally (on Windows there are no local WSL builds or clang-tidy runs):
every pull request that changes engine files gets a required `clang-tidy` check on the lines it changes, and the whole
codebase is tidied nightly ([clang-tidy](clang-tidy.md)). It catches what MSVC lets through, such as an unchecked
`std::optional` access (`bugprone-unchecked-optional-access`) or a use after a move. A finding is an annotation on the
pull request; it is fixed as a fix task on the branch ([Tracks](how-we-work.md#slots)). A designated initializer that
skips a field (`-Wmissing-designated-field-initializers`) is caught by the GCC and Clang builds instead, whose `-Wall -Wextra`
warnings are errors in the `ci`, `ci-release` and `asan` presets (`CONEY_WARNINGS_AS_ERRORS`); clang-tidy's `-*` turns
off its compiler diagnostics.

`ci run` runs on Windows, Linux and macOS. Nobody checks it by hand on all three, so CI does: the `python` workflow
runs its `repo` and `pre-commit` steps on every operating system it tests (`--base HEAD^1`, the branch a pull request's
merge commit joins), and a step that works on one system and not another fails there. A person on Linux or macOS who
finds a difference fixes it forward.

The whole gate, which `task check` runs:

- **Engine changes:** the `ci` preset build and its tests with the disc configured, in one command:
  `uv run --project python coney-tools build --agent <id> --preset ci --test --full` (it waits for a free
  [build slot](coney-tools-build.md#build), loads the MSVC environment itself and passes the configured disc to the tests;
  agents never run `cmake --build` or `ctest` directly). Read its `disc tests:` line: `SKIPPED` means no disc test
  ran, and the run is made again with a disc.
- **Python:** `ruff check` and `ruff format --check` on `python/` and `.claude/hooks/`, `mypy` and `pytest` on
  `python/`; a Python test is not stamped `fails-first` (that stamp is for the `TEST_CASE`s of an `@orig` change): a
  tooling task's tests-first rule rests on the reviewer and on pytest in its `checked` stamp ([pr
  verify](pr.md#verify)); `coney-tools repo check-conventions`; `coney-tools progress check` (the progress inputs; its
  tables are built into the docs, not committed); and every generator's `--check` (`bindings render`, `bindings
  sync-coney`, `bindings render-cpp`, `refs render`, `missions render`, `research render`, `pcsx2 recipes render`).
  Rerun a generator without `--check` when you change what it reads.
- **Everything:** `uvx pre-commit run --all-files` leaves the tree unchanged, markdownlint passes,
  `mkdocs build --strict` passes, and `coney-tools docs check` passes on the pages you touched.

Commands and presets: [Building and testing](building.md).

The whole gate runs once per tip, in `task check`, before Review. While working,
check with the `dev` preset and only the tests the change touches
(`coney-tools build --agent <id> --preset dev --test -R <pattern>`, or `-L disc` when the disc tests are the ones
affected). Builds share a few slots on one machine, and a full `ci` build with the whole suite holds one for 15 to 30
minutes, so running it for every step makes everyone else wait. The tool keeps the rule: an agent's `build --test`
with no `-R` or `-L` is refused unless it carries `--full` ([When the whole suite runs](coney-tools-build.md#build)).

**A full suite takes a check slot.** The whole pytest run and the whole ctest run each take 30 to 40 minutes on a
machine to themselves, so `task check`, `ci run` (its pytest) and `build --test` with no `-R` or `-L` hold a **check
slot** while they run: `check_slots` in `coney.local.toml` of them at once across all worktrees (default one per four
cores), the others wait in a queue that says its place every minute
([Check slots](coney-tools-build.md#check-slots)). A few test files typed by hand (`pytest python/tests/test_x.py`,
`build --test -R <pattern>`) need no slot: run those while working, and the full suite once, in `task check`. The
gate shows its progress as it runs: each check logs its start and end with the time, and a long one says it is still
running every minute, so a quiet log is a hung run.

### Continuous integration {#ci}

CI builds and tests on Windows, Linux (GCC and Clang) and macOS, with sanitizers and clang-tidy on the changed lines,
and runs the Python and docs checks. It is the gate for the platforms you are not on: do not run virtual-machine or
cross-platform builds locally to stand in for it, and fix a red CI forward rather than leaving `main` red. Code that
MSVC accepts but the others reject is the usual cause: see [Portability traps](conventions.md#portability-traps).

## Play-testing {#play-testing}

People play a play-test build (the `playtest` preset: optimised, with the debug menus) at milestones, from one list
of what changed and how to reach it (commands that start
through the player's path). Nobody waits for them: work merges on the automated gate. A finding comes back as a fix
task, starting with a test that reproduces it. A mission stays **Pending approval** until a maintainer
has played it ([Missions](research-data.md#missions)).
