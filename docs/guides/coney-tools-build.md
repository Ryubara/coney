# The coney-tools docs, build and benchmark commands

The commands that check pages, build the engine and measure it: [`docs`](#docs) checks pages against the documentation
standards, [`build`](#build) configures, builds and tests the engine under the machine's build slots, and
[`perf`](#perf) benchmarks it with `coney --bench`. The other groups are listed in [the command line
overview](coney-tools.md#groups).

## docs {#docs}

```sh
uv run --project python coney-tools docs check [PATHS ...] [--base origin/main] [--summary] [--quiet]
uv run --project python coney-tools docs render [--check]
uv run --project python coney-tools docs legacy [--init | --prune | --check] [--base origin/main]
```

Checks Markdown pages against the standard of [Writing these docs](writing-docs.md#checking): the front matter, the
sections of its kind, the "Verified against" line, the evidence levels, the addresses, the copied paragraphs, the dates
and the length of a research page, the dates, issue numbers, addresses and landing page of an engine page
([`docs/engine/`](writing-docs.md#engine-pages)), and the Style rules every page keeps. Writing-docs owns the rules and
the heuristics, and `research` in [.github/conventions.yml](repo:.github/conventions.yml) the kinds, sections and limits;
this entry is the command. Without `PATHS` it checks the pages changed since
`--base` (committed or not, untracked included), as [`ci run`](coney-tools-ci.md#ci-run) does in its `docs` step and the
`docs` workflow does on a pull request. With `PATHS` it checks those files, or every page under a folder, whether or not
they changed. It prints `path:line: rule: message` for each finding (`--quiet` prints only the counts), a warning (a
long page) never fails, and `--summary` adds how many pages of each kind pass and how many pages break each rule: run it
on `docs` for the standing of the whole tree, with the number of legacy pages. Exit 0 when no page has an error, 1 when
one has, 2 when the command is not run inside a checkout, the conventions cannot be read, `--base` is unknown, or a
named path is missing, outside the checkout or not a page the check covers (`README.md`, a generated page).

`docs render` writes the parameter reference of every command into the guides: the generated **Parameters of** block
at the end of each group's section, read from the command line's own definitions ([the parameter
reference](coney-tools.md#parameter-reference)). The guides are the `guides` folders inside the folders `paths.docs` of
[.github/conventions.yml](repo:.github/conventions.yml) names. `--check` changes nothing and exits 1 with one line for
each stale block, naming its group and its page; either way it exits 1, naming the group, for a group with no block or
two, a marker that names no group, and a command or parameter that has no `help=` text, which no rendering fixes. It is
the audit check `files.command-reference` too, so `ci run` and the `python` workflow run it on every change: a
command or a parameter that is missing from the guides, or a block that is stale, does not merge. Exit 2 when the
command is not run inside a checkout.

A research page is read against the executable's data: `docs/progress/ghidra-functions.tsv` (every function, its size
and name), `research/elf-sections.toml` (the sections of `SLUS_212.15`) and, once the folder exists, the run records of
`research/runs/`. The other research pages are read too, for the owner of an address and for copied paragraphs.

`docs legacy` keeps `research/legacy.toml`, the research pages that do not meet the standard yet
([Legacy pages](writing-docs.md#legacy-pages)). Without an option it scans every research page and prints how many fail
each rule. `--init` writes the file from a scan and refuses when it exists; `--prune` takes out the pages and rules that
now pass and lowers the counts, and adds nothing (but, against `--base`, the entry of a page git sees as moved follows
it to its new path, keeping the grading rule `evidence` only); `--check` exits 1 when the file lists a page that is gone
or a rule that now passes, or (when `--base` has the file) adds a page or a rule since the branch left `--base`.
`docs check` runs the same check on the file whenever the change touches a research page, a data file the checks read
(the function listing, the sections, the run records, the function database or the file itself), the conventions or the
checks, so it fails a legacy rule that passes and a new page that is listed. On a listed page the change touches, it
fails every mechanical rule page-wide and the grading of the sections the change touches (an address with no level, an
old-form runtime claim); an entry may list any research rule or `stale-wording`. A page moved with git keeps its entry's
grading exemption and nothing else, and a line that only rewrites link targets to follow the change is no touch: to a
page it moved, to a section it moved between two pages, or to an id new on the same page while the old one stays (a
split section; all under [Legacy pages](writing-docs.md#legacy-pages)).

<!-- command-reference:begin docs -->
## Parameters of `docs` {#docs-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `docs check` {#docs-check-usage}

Check pages against their kind's rules; without PATHS, the pages changed against the base.

```text
coney-tools docs check [PATHS ...] [--base BASE] [--summary] [--quiet]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[PATHS ...]` | text | none | pages or folders to check (default: the pages changed since --base) |
| `--base BASE` | text | `origin/main` | the branch the change is measured from |
| `--summary` | flag | off | also count the pages that pass, per kind, and per rule |
| `--quiet` | flag | off | print only the counts, not each finding |

### `docs render` {#docs-render-usage}

Write the parameter reference of every command into its guide page (generated blocks), from the parsers.

```text
coney-tools docs render [--check]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--check` | flag | off | change nothing; exit 1 when a block is stale or missing |

### `docs legacy` {#docs-legacy-usage}

The research pages that do not meet the standard yet (research/legacy.toml): scan, write, shrink, check.

```text
coney-tools docs legacy [--init | --prune | --check] [--base BASE]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--init` | flag | off | write the legacy file from a scan (refuses if it exists). Not with `--prune`, `--check`. |
| `--prune` | flag | off | take out what is fixed and lower the counts; never adds. Not with `--init`, `--check`. |
| `--check` | flag | off | exit 1 when the file lists a page or rule that now passes. Not with `--init`, `--prune`. |
| `--base BASE` | text | `origin/main` | with --check: the branch the file may not grow since |
<!-- command-reference:end -->

## build {#build}

```sh
uv run --project python coney-tools build [--agent ID] [--preset dev|playtest|release|ci|ci-release|asan] [--test [--full]] [-R REGEX] [-L LABEL]
    [--target NAME ...] [--configure | --configure-only] [--build-dir FOLDER] [--env NAME=VALUE ...] [--priority]
    [--lane auto|main] [--max-wait MIN] [--budget MIN] [--detach] [-j N] [--dir CHECKOUT]
uv run --project python coney-tools build status [--json] [--dir CHECKOUT]
uv run --project python coney-tools build result [--json] [--dir CHECKOUT]
uv run --project python coney-tools build unclaim --agent ID [--force] [--slot N] [--lane main|fast|check]
    [--dir CHECKOUT]
```

`--dir` names the checkout the command is about and works before or after the subcommand (`build --dir X result` and
`build result --dir X`).

The one way an agent configures, builds and tests the engine. A run that configured, built or tested leaves a **build
record** in the task's scratch folder (preset, tip, exit code, test counts with the skipped disc tests apart, the log's
hash); `task check` reads it instead of printed text (`pr verify` will)
([The build record](stamps.md#build-record)). Several agents share one machine and a full build uses
every core, so a build takes a **slot** like a PCSX2 copy takes a claim, and at most `build_slots` (default 2) run at
once. The command replaces `cmake --preset`, `cmake --build --preset` and `ctest --preset`; a hook in
`.claude/settings.json` refuses those, `ctest` and `ninja` typed directly in a shell
([`.claude/hooks/build_guard.py`](repo:.claude/hooks/build_guard.py)). The tool's own subprocesses are not shell
commands, so the hook never sees them. `--agent` names whose run a slot, a queue place and a log are; a person typing
the command in a terminal may leave it out and gets their user name (`me` when it has no usable characters). It
stays required whenever the output is not a terminal, as under every agent tool, and inside Claude Code
(`CLAUDECODE` set), so every agent's build says whose it is.

**What a run does.** From the checkout (the current one, or `--dir`):

1. *Precheck*, before it waits for anything: `CMakePresets.json` names the preset, a new build folder is within the
   cap (see "The cap on build folders" below), and `cmake`, `ctest` and `ninja` are found. On Windows it runs
   `vcvars64.bat` once and captures the environment (found with `vswhere` in the installer folder under
   `Program Files (x86)`, which is not on `PATH`; inside a Developer shell it does nothing), so no special shell is
   needed.
2. *The build folder's lock*, held to the very end (see "One run per build folder" below).
3. *Configure and build*, one claim: `cmake --preset P` when the build folder has no `CMakeCache.txt` or with
   `--configure`, then `cmake --build --preset P -j N`, through the [compiler cache](building.md#compiler-cache).
   `--configure-only` stops after the configure (the compile database `ci run` reads). `--target NAME`
   (repeatable) builds only those targets and what they need: `--target coney` is a play build, the executable
   without the roughly thousand test files of `coney_tests`. It is refused with `--test` (the tests would run an
   old `coney_tests` or `coney`) and with `--configure-only`.
4. *Test*, with `--test` and only after a successful build, a claim of its own (it queues again between the two, so a
   long test run does not hold a slot against other agents' builds longer than it must): `ctest --preset P -j N` (`N` is
   `-j`, else `test_jobs`, else half the cores: see "Parallelism" below), with the filters `-R REGEX` and `-L LABEL`
   when given (see "Choosing tests" below).

**The disc for the tests.** The disc tests read only the environment variable `CONEY_DISC`
(`repo:tests/support/disc_play_fixtures.h`) and skip without it, and ctest counts a skipped test as passed. So the
command sets `CONEY_DISC` for the whole run (CMake reads it too when it configures, to register the `coney --disc`
smoke tests): a value already in the environment, or given with `--env CONEY_DISC=...`, is kept (an empty one counts
as not set); otherwise it is the `game_dir` of `coney.local.toml` (the one `.iso` in that folder, else the folder
itself, as `trace` hands it to Coney), when that exists. A folder configured before the variable was set keeps the
smoke tests it registered then; `--configure` registers them. With `--test` the summary gets a line before the test
line: `disc tests: ran with the disc (CONEY_DISC from the environment | game_dir in coney.local.toml)`, or
`disc tests: SKIPPED (<why>)` with the two ways to run them, and `S skipped` ends the test counts when ctest
reported skips (`N passed, M failed of T, S skipped`: ctest's own summary counts a skipped test as passed, but the
line does not, so `N + M + S = T`; a disc configured and tests still skipping is named, and the log says
which). A suite that passes
with `disc tests: SKIPPED` ran no disc test.

**The log.** The full output goes to a file of its own per run, `build-<preset>-<agent>-<date>-<time>.log` in the
worktree's folder of the scratch directory (`scratch/<worktree>/`; the process id is added if that name exists), and
its path is the first line printed. No two runs share a log, so one never garbles another's; the newest 20 logs of a
worktree are kept. What is printed after is short: a line per step (outcome, time, lane, how many steps were out of
date, time spent waiting), a line on the compiler cache after the build (`compiler cache: 980 hits, 12 misses
(98.8 %)`: this run's calls alone, from a stats log of its own, though other builds share the cache; or `off` and
why, or `not used` for a folder configured before the cache was on), the first compile, link and CMake errors (MSVC,
GCC and Clang forms) and the failed steps,
the failed tests by name (the first eight, then a count; the counts and the names are ctest's own, read from its "The
following tests FAILED:" list up to its blank line or "Errors while running CTest" line, labels after a state
included, so a test's output that looks like a list entry is never taken for a failure; without that list, or when it
holds fewer names than ctest counted, the `***Failed` and `***Timeout` lines supply the rest) with `N passed, M failed
of T`, and a last `result:` line that names the failing step or the first failing tests. A command that exits non-zero
with no error, no failed step and no failed test in its output says so plainly, `stopped from outside (no failing step
found)`: it was killed, timed out or interrupted by something else. Exit code `0` on success, `1` on a failed step or
a filter that selects no tests, `2` on a precheck or usage problem, `75` when it gave up waiting for a slot or for the
build folder, `130` on an interrupt or when its caller is gone. The children run at below-normal priority, on Windows
with no console window of their own (`CREATE_NO_WINDOW`, so a build started from a background task opens no terminal
that takes the focus), and never outlive the command (below).

**Process lifetimes.** An orphaned `ctest` goes on starting tests after its caller is gone, and killing the tests one
by one only lets it start the next, so nothing the command starts may outlive it
([`process_lifetime.py`](repo:python/src/coney_tools/process_lifetime.py)):

* *The command is killed.* On Windows it puts itself in a job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` before
  it starts anything; `cmake`, `ninja`, the compilers, `ctest` and every `coney_tests` inherit the job, and when the
  command ends, however it ends (Task Manager included), Windows ends them all. On Linux and macOS each child runs in
  a session of its own, so its whole tree is one process group that the command stops on an interrupt, `SIGTERM` or
  `SIGHUP`; on Linux the child also gets `SIGKILL` when the command dies by `SIGKILL` (`PR_SET_PDEATHSIG`).
* *Its caller is gone.* A watch thread looks every two seconds at the command's ancestors, up to the shell that ran it
  (through `uv` and Python's launchers; under Git Bash, as far as the native chain goes). When one has ended (the
  agent tool restarted, the background shell was killed), the command stops its child, starts nothing more and
  leaves as on an interrupt, releasing its claims (by force 15 seconds later if a wait does not notice); exit `130`.
* *An older run died without these guards.* When a claim or a folder lock whose process is gone is taken over or
  released with `build unclaim`, the build programs that run started (`cmake`, `ctest`, `ninja`, `coney_tests`: its
  direct children by parent pid, started after the claim was made, as Windows reuses pids) are stopped with everything
  under them, and the run that took over says `stopped N process trees <agent>'s run left running`. A claim that is
  stale only by age (its pid lives) is taken over without stopping anything, as the pid may be somebody else's by now.

**Parallelism.** The compile and the tests each have their own default, and two settings change them:

| Setting | Default | What it does |
| --- | --- | --- |
| `-j N` (option of `build`) | none: the two defaults below | the parallel jobs of the compile **and** of `ctest`, beating `test_jobs`; `0` counts as not given |
| the compile's default | the cores divided by `build_slots`, at least 1 | `cmake --build -j`: with five slots on 16 cores each build starts 3 jobs, so builds sharing the machine do not each start a job per core |
| the tests' default | half the cores, or the compile's share when that is more, at least 1 | `ctest -j`: a test run does not overlap its own compile, so it can use more cores than a compile that shares them; 16 cores give 8 tests with 2 to 5 slots, and all 16 with one (the compile's share is then the whole machine) |
| `test_jobs` (key of [`coney.local.toml`](workspace.md#local-config)) | `0` | a whole number: the parallel tests of `build --test`, in place of the tests' default; `0` keeps that default. Raise it on a machine with cores and memory to spare (each disc test holds a level in memory), lower it when others share the machine |

Use the tests' share with care on a busy machine: every test run starts that many tests, a filtered `-R` or `-L` run in
the fast lane included, so `build_slots` runs in the main lane and one in the fast lane at once start up to
(`build_slots` + 1) × N processes. Lower `test_jobs` when others share the machine.

**The slots.** Each run holds a claim: a folder `slot-N.claim/` with `owner.json` (agent, worktree, preset, the step it
is at, pid, start time) in the claims directory, built whole in a private folder and renamed into place, so exactly one
caller wins and none sees a half-made claim. The directory is `build-claims/` in the shared scratch folder, the main
checkout's `../../scratch`, whatever a worktree's `scratch_dir` says (keys `build_claims_dir` and `build_slots` in
[`coney.local.toml`](workspace.md#local-config)). Nothing is cached: `status` is rebuilt from the folders and the live
processes every time. A claim is released on success, on failure and on an interrupt, and is **stale** (taken over by
the next claimer, who says so and stops what the dead run left running; see "Process lifetimes" above) when its process
is gone, or when it is older than four hours with a process that lives (a hung build, or a reused pid). Takeovers go
through a lock folder, as the PCSX2 claims do.

**The queue.** With no slot free, the build waits in a first-come queue of ticket files (`queue/`). Only the head of
the queue may claim, so waiters cannot overtake one another; `--priority` tickets go ahead of normal ones (for the
coordinator, or a build somebody is waiting on). A ticket whose process is gone is dropped. While it waits, the
command prints its place, a rough wait (from the holders' recorded step and how long they have run it) and the
holders, every minute. **It does not wait for ever:** after `--max-wait` minutes (default and most 40, per claim;
also spelled `--max-wait`), so that an agent never outlives its one-hour prompt cache, it leaves the queue and
exits `75` with `still queued, position N of M, try again later` and the way on: do other work and run the same
command again, or start it with `--detach`, write `STATUS.md` and stop ([Build waits](agents.md#build-waits)).

**The time budget.** `--max-wait` caps one wait; `--budget` (minutes, default and most 45) caps the whole call,
waits and steps together, so an agent's wait plus its build stays inside the hour its prompt cache lasts. A larger
`--max-wait` or `--budget`, or `--budget 0` (no budget), is refused with exit `2` unless the run is detached. Each
finished step's duration is kept in `durations.json` in the claims directory, the last ten per preset, step and size
(`ci/configure/main`, `ci/build/fast` or `ci/build/main` by lane, `ci/test/filtered` with `-R` or `-L`, `ci/test/full`
without); a build of some targets only has them in its key (`dev/build/main:coney`) and a build through the compiler
cache `+ccache` (`dev/build/main+ccache`), because both take a fraction of a full build without the cache and would
otherwise pull its median down or up; a failed step is not recorded. While a run waits for a slot, it leaves the
queue with exit `75` as soon as
the time the call has taken plus the median of what it waits for (a phase's steps added up; with no history, the
typical 5 minutes for a configure, 15 for a build, 10 for a full test run, 3 for anything in the fast lane) would pass
the budget, and says so with the numbers: `time budget: 25 min of the call so far; the build would take about 30 min
(median of 4 runs) and the budget is 45 min`. It also leaves at its first look when the queue's own estimate of the
wait still ahead (from the holders' steps), added to those, would pass the budget: `the queue ahead is about 50 min`.
The test phase counts the build before it, and the wait for the build folder counts too. The budget only decides
whether to keep waiting: a step that has started always runs to its end, and a free slot is always taken.

**Detached runs.** `--detach` checks the run as usual (precheck, refusals), then starts the same run in a process of its
own, apart from the calling shell (on Windows `CREATE_NO_WINDOW` in a new process group, never `DETACHED_PROCESS`, which
makes Windows Terminal show an empty window for the venv launcher's child; out of the caller's job when the job allows
it; elsewhere a session of its own), and returns at once printing the log path and the child's pid. A detached run may
wait and run as long as it needs: `--max-wait` defaults to 240 and `--budget` to `0`, and neither is capped. Its
children still end with it, but nothing watches the shell that started it. It records itself in `build-result.json` in
the worktree's scratch folder (state, agent, preset, pid, when that process started, log, start time; at the end the exit
code, the printed summary and the end time; its console output goes to `build-detached.out` beside it), however it ends,
a refused precheck included. A second `--detach` in a worktree whose detached run still runs is refused. `build result`
prints that record: the run's own exit code and summary once it has ended, `75` and `still running` while it runs, and
`1` with `ended without a result` when its process is gone without writing one (killed); `--json` prints the record
itself. "Still running" means the recorded pid lives **and** that process started when the record says: the system
hands a dead process's pid to a later one, and a stale record once kept a worktree "busy" for good because another
program had been given the pid. A pid whose process began at another time is gone (an older record without the start
time is judged by the record's own time: a process that began long after it is a stranger), and a start time that
cannot be read counts as running.

**The fast lane.** A small job should not queue behind a full build. Before claiming, the command measures the job.
For a build: the manifest is brought up to date for real with `ninja build.ninja` (it re-checks the `CONFIGURE_DEPENDS`
globs, which is cheap, and re-runs CMake only when a CMake file or a glob changed, which the build would do first
anyway; under the folder's lock, outside a slot), then `ninja -n -d explain` (of the `--target` targets only, when
given) runs on a *copy* of `build.ninja`, and the
**causes** it names are counted: each "output x older than most recent input y", "output x doesn't exist" or "command
line changed for x". A plain `ninja -n` is no use here: the globs make ninja want to re-run CMake first, and a dry run
stops there having planned nothing; and a dry run on a copy plans too much, because it cannot ask a `restat` step
whether its output really changed, and everything behind such a step is planned too (CMake's module dependency collation
was one, before [module scanning](building.md#module-scanning) was turned off: 410 planned steps for a touched source
whose real build was 10). The causes are what a real build must do whatever the `restat` steps find: one per touched
source, and one for each source that includes a touched header. For a test run, `ctest -N` with the same `-R` and `-L`
counts the tests selected. A build with fewer than `build_fast_lane_steps` causes (default 50) or a filtered test run of
at most `build_fast_lane_tests` tests (default 20) takes the **fast lane**: one extra slot of its own (`fast-1.claim/`,
queue `queue-fast/`), so at most one such job runs at once, with a short queue of its own. A first build, a
`--configure`, a build that cannot be measured (the manifest refresh failed, ninja printed no cause) and any test run
without a filter take a main slot. A threshold of `0` turns the lane off. The summary line says what decided it ("fast
lane, 2 steps out of date").

A filtered test run is also measured by **time**: one disc playthrough of a single test can hold a slot for half an
hour, which the fast lane exists to prevent. When any test the filter selects has a recorded time, and those times add
up to more than `build_fast_lane_test_seconds` (default 300; `0` turns the rule off), the run takes a main slot
whatever its count, and says so ("1 test expected to take 25.0 min by its recorded time, ... so the main lane"). A test
with no recorded time adds nothing, so a run of only such tests goes by its count. The times are ctest's own averages
(`Testing/Temporary/CTestCostData.txt` in the build folder) and, because a build folder is often new, a copy kept per
preset in `claims/test-costs.json` that every test run updates from its folder's file; the folder's own times win.
`--lane main` sends any run, build and tests, to a main slot without measuring, for a job you know is long; the
default, `--lane auto`, chooses as above.

**The cap on build folders.** A build folder is about 7 GB and the drive must keep 30 GB free, so only so many
worktrees can hold one. `build` refuses to configure a **new** build folder (one that is not on disk) while the
worktrees that already hold a build folder (`build/`, or theirs in the [build root](workspace.md#build-root)), in
any preset, are at `build_worktree_cap` (default 7, in
[`coney.local.toml`](workspace.md#local-config); `0` turns the cap off). It exits `2` before it waits for anything and
prints the worktrees holding builds, longest idle first, each with the size of its builds, the hours since it was
last touched and its state (busy with the marker or a build slot; its branch merged, by `origin/main` or by a merged
pull request; or unmerged work), saying which of them
[`workspace clean --apply`](coney-tools-setup.md#workspace) would free now. Only the worktrees under the workspace's
`worktrees/` folder are counted: the main checkout's `build/release`, the coordinator's play build, never counts and a
run in the main checkout is never refused. A folder that exists keeps building whatever the count is, and a second
folder in a worktree that already holds one (a second configuration, a `release` beside a `dev`) adds no worktree,
so it is allowed. Two runs that configure at the very same moment may both pass: the cap is a brake, not a lock. Why it
is needed and what to do on a refusal: [The cap on build folders](agents.md#build-cap)
([`build_cap.py`](repo:python/src/coney_tools/build_cap.py)).

**One run per build folder.** Two runs in one build folder collide: a second build's link fails
with `LNK1168` while the first's tests hold `coney_tests.exe`, and the logs and the dependency log are written by both.
So each build folder (the checkout, the preset or `--build-dir`) has a **lock**, taken before anything else and held from
the configure to the last test, however the run ends. A second run in the same folder **waits** for it, in a queue
like the slots' (`claims/folders/<worktree>-<folder>-<digest>/`, the same stale rules: the holder's process is gone, or
four hours), printing who holds it and the step it is at every minute, and gives up after `--max-wait` minutes with
exit `75` as for a slot. Runs in other folders, other worktrees or other presets are not held up by it. The lock
covers the run from its measuring on, so the manifest refresh and the dry run never overlap a build either, and
a run that waited looks at the folder again once the lock is its own (the run before it may have configured it).

**When the whole suite runs.** `--test` with no `-R` or `-L` is the whole suite: 15 to 40 minutes, most of it the disc
tests, and a main slot and a check slot held for that long. An agent runs it **once, at the handover tip**, and
`task check` does it (it calls the same code and says so), so while iterating an agent runs the tests it touched with
`-R` or `-L`. The command keeps that rule: **a run that names its `--agent` and has `--test` with neither filter is
refused (exit 2) unless it carries `--full`**, the flag that says "this is the run of the whole suite for the tip".
Not refused: a filtered run; a build with no `--test`; a person at a terminal who leaves `--agent` out (the id
then comes from the user name); CI, which builds through the presets and not this command; `task check`, and
`ci run`'s `engine` step, which pass `--full` themselves; and `orig fails-first`, which always names its tests.
`--full` is refused with no `--test` and beside `-R` or `-L`, where it would claim something untrue, and it is kept in
the build record's command line. The flag is a statement, not a check that the tip is the handover one: use it for
that run only, since every other full suite holds the slots the other agents' builds wait for
([Check slots](#check-slots)).

**Choosing tests.** `-R REGEX` runs the tests whose names match and `-L LABEL` those with a label that
matches (ctest's two filters; both together select the tests that satisfy both). The command lists the selection
first (`ctest -N`), takes the fast lane for 20 tests or fewer and fails, naming the filter, when nothing matches. Every
test that needs the player's disc has the label **`disc`**: the Catch2 tag `[disc]` of a test case becomes a ctest label
(`catch_discover_tests(... ADD_TAGS_AS_LABELS)`), and the `coney --disc` checks that exist only when `CONEY_DISC` is set
at configure time carry it too. So `build --test -L disc` runs exactly the disc tests (they skip without the disc), and
every other tag is a label as well (`-L routes`). `-L` matches a part of a label name: `-L '^disc$'` is the exact one.
`-R` is a **CMake regular expression**, and the tool passes it to ctest exactly as it was typed (a test checks that a
regex with `^ ( | ) $ . + \` and spaces reaches ctest unchanged), so a test that matches nothing is usually the
expression. Test names hold spaces, `.`, `+`, parentheses, `:` and quotes, and `. + ( ) [ ] * ? ^ $ |` mean something to
the expression: escape the ones that are part of a name with a backslash (`'^(L1 \+ R1 with a full rage meter starts
rage|coney\.help)$'`), and quote the argument against your shell with single quotes (PowerShell and bash both leave
those alone; a double-quoted `$` or backtick is the shell's). Without anchors a name matches as a substring, which is
often enough: `-R 'rage meter'`. Preview a filter without a run: `ctest --preset P -N -R ... -L ...` lists what it
selects, and the hook allows it.

**Another build folder.** `--build-dir FOLDER` (relative to the checkout) configures, builds and tests in that folder
instead of `build/<preset>`, with the preset's settings (`cmake --preset P -B FOLDER`, `cmake --build FOLDER`, `ctest
--test-dir FOLDER` with the test preset's environment passed on), for a second configuration beside the first, such as
a Clang build beside the default compiler's.

**The build root.** A machine can keep every checkout's builds on another drive: with `build_root` set in its
untracked [`coney.local.toml`](workspace.md#build-root), the build tree of a checkout is `<build_root>/<id>/` instead of
its `build/`, so `build/<preset>` becomes `<build_root>/<id>/<preset>` and a `--build-dir build/clang` becomes
`<build_root>/<id>/clang` (any other `--build-dir` stays as given). Each step then names that folder as `--build-dir`
does. Unset, nothing changes and every command is the one above. `task check`, `orig fails-first` and `ci run` build
through this command, and the cap, `workspace clean`, `perf` and `trace` find the folder through the same resolver
([`build_root.py`](repo:python/src/coney_tools/build_root.py)).
`--env NAME=VALUE` (repeatable) sets an environment variable for the run, such as `CC=clang-19`; CMake reads `CC` and
`CXX` at a folder's first configure and keeps them in the cache.

`build status` prints every lane (main, fast and check): each slot's holder, worktree, preset, step and minutes, the
queue in order (place, agent, priority, minutes waiting) and the stale entries; `--json` prints the same as one object.
`build unclaim --agent ID [--lane main|fast|check]` is manual cleanup: it frees the slots a crashed or stuck run of that
agent left, stops what a crashed run left running, and drops its tickets. A slot whose process still runs, or another
agent's, needs `--force`, which frees the slot but does not stop the process. How agents use it, and what exit `75`
asks of them: [Builds and tests](agents.md#builds).

### Check slots and live progress {#check-slots}

A build slot limits compiles, not suites. A full suite (the whole pytest run, the engine's whole ctest run) takes 30
to 40 minutes alone and several times that with its neighbours: a night with 8 to 16 of them at once on an 8-core
machine turned half-hour suites into two- and three-hour ones, and agents gave up on them and started them again. So
every command that runs a **full suite** holds a **check slot** for the run, from the same machinery as the build slots
(the claims directory, the queue, the stale-claim rules, a lane of its own: `check-N.claim/` and `queue-check/`), and
at most `check_slots` of them run at once, across all worktrees:

| Holder | What holds the slot |
| --- | --- |
| `task check` | the `pytest` check (the whole suite), for the run of pytest |
| `ci run`, `ci pr` | the `python` step's pytest, for the run of pytest |
| `build --test --full` (so also `task check`'s build) | the test phase when it has **no `-R` or `-L` filter**: the whole ctest run; an agent may start it only with `--full` ([When the whole suite runs](#build)). The wait for the check slot comes before the wait for a build slot, so a run in the check queue holds no build slot |

A **targeted** run takes none: `build --test -R ...` (so `orig fails-first`, which names its tests), or `pytest` on a
few test files typed by hand. Run those freely; run the full suite once, in `task check`. A run that has to wait says
so, and again every minute: `waiting for a check slot (pytest): place 2 of 3, about 40 min; held by alice (dev, 12
min), bob (ctest, 3 min); 2 full suites may run at once (check_slots in coney.local.toml)`. It gives up after
`--max-wait` minutes with exit 75 (nothing is written: run it again), like a build waiting for a slot. A holder that
crashed leaves a stale claim: the next suite takes it over, says so and stops what the dead run left running. `build
status` lists the check lane with the others; `build unclaim --lane check --agent ID` frees an agent's check slots by
hand.

`check_slots` is in [`coney.local.toml`](workspace.md#local-config); `0` (the default) means one slot for every four
cores and at least one: 2 on an 8-core machine, 4 on 16. Set it lower when the machine does more than tests.

A slow run must be told from a hung one, so `task check`, `ci run` and `orig fails-first` print a line when each stage
starts and ends, with the time of day, and flush every line at once (a run redirected to a file shows it grow). A check
that runs a long program (pytest, mypy, pre-commit) streams its output into `check-logs/<program>.log` in the task's
scratch folder, line by line (a new run starts the file again), and prints `pytest: still running, 12 min, 45% done` at
least every minute. `orig fails-first` shows the build's own lines as they come. The engine's build and test run print
the same kind of line from inside `build`: `build: still running, 9 min, ninja step 410 of 1204` and `test: still
running, 22 min, 1310 tests done, 2 failed`.

<!-- command-reference:begin build -->
## Parameters of `build` {#build-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `build` {#build-usage}

Configure, build and test under the build slots (at most two at once); status, result, unclaim.

```text
coney-tools build [--agent AGENT] [--preset PRESET] [--dir DIR] [--configure] [--configure-only] [--test] [--full]
    [--target NAME] [--regex REGEX] [--label LABEL] [--build-dir BUILD_DIR] [--env NAME=VALUE] [--priority]
    [--lane RUN_LANE] [--max-wait MIN] [--budget MIN] [--detach] [--jobs JOBS]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--agent AGENT` | text | none | your id: letters, digits, '.', '_' and '-' (default for a person: your user name; agents must pass it) |
| `--preset PRESET` | choice | `dev` | the CMake preset (default dev). One of `dev`, `playtest`, `release`, `ci`, `ci-release`, `asan`. |
| `--dir DIR` | path | none | the checkout to build (default: the current one) |
| `--configure` | flag | off | configure even when the build folder has a cache |
| `--configure-only` | flag | off | configure and stop (no build): the compile database for check |
| `--test` | flag | off | run ctest after a successful build |
| `--full` | flag | off | with --test: say that this run of the whole suite, without -R or -L, is the handover tip's; an agent's unfiltered --test run is refused without it (`task check` runs the whole suite itself) |
| `--target NAME` | text | none | build only this target and what it needs (repeatable), e.g. `coney` for a play build without the tests; not with --test |
| `-R, --regex REGEX` | text | none | run only the tests whose names match (ctest -R, a CMake regular expression: escape . + ( ) with a backslash); a few tests are quick |
| `-L, --label LABEL` | text | none | run only the tests with a label that matches (ctest -L); `disc` is every test that needs the game's disc |
| `--build-dir BUILD_DIR` | path | none | build in this folder instead of build/\<preset\> (relative to the checkout; with a build root, build/... moves into it), with the preset's settings; for a second configuration, such as a Clang one next to the default compiler's |
| `--env NAME=VALUE` | text | none | set an environment variable for the run, e.g. CC=clang-19 (repeatable); CMake reads CC and CXX at the first configure of a folder |
| `--priority` | flag | off | go ahead of the normal queue (a build someone awaits) |
| `--lane RUN_LANE` | choice | `auto` | auto (default): a small build or test run takes the fast lane; main: always a main slot, for a run you know is long. One of `auto`, `main`. |
| `--max-wait MIN` | number | none | minutes to wait for a slot, per phase, before exiting with code 75 (default and most 40; with --detach default 240, no limit) |
| `--budget MIN` | number | none | minutes the whole call may take, waits and steps together: stop waiting (exit 75) once the wait plus what the step usually takes would pass it (default and most 45, inside an hour's prompt cache; with --detach default 0: no budget) |
| `--detach` | flag | off | run in the background, apart from this shell, and return at once: the result goes to build-result.json in the worktree's scratch folder (`build result` shows it); for a wait or a run that would outlast your prompt cache |
| `-j, --jobs JOBS` | integer | none | parallel jobs (default: the cores divided by the slots) |

### `build status` {#build-status-usage}

Who holds the slots (the build lanes), who waits, and stale entries.

```text
coney-tools build status [--dir DIR] [--json]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--dir DIR` | path | none | the checkout the command is about (default: the current one) |
| `--json` | flag | off | print JSON instead of text |

### `build result` {#build-result-usage}

The outcome of the worktree's last --detach run.

```text
coney-tools build result [--json] [--dir DIR]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--json` | flag | off | print the record as JSON |
| `--dir DIR` | path | none | the checkout the command is about (default: the current one) |

### `build unclaim` {#build-unclaim-usage}

Manual cleanup: free an agent's slots and drop its queue places.

```text
coney-tools build unclaim --agent RELEASE_AGENT [--force] [--slot SLOT] [--lane LANE] [--dir DIR]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--agent RELEASE_AGENT` | text | required | the agent whose entries to remove |
| `--force` | flag | off | also another agent's, or a build that still runs |
| `--slot SLOT` | integer | none | one slot by number (with --lane for the fast lane) |
| `--lane LANE` | choice | none | one lane only (default: all). One of `main`, `fast`, `check`. |
| `--dir DIR` | path | none | the checkout the command is about (default: the current one) |
<!-- command-reference:end -->

## perf {#perf}

```sh
uv run --project python coney-tools perf bench --out FOLDER [--fighters 0,10,25,50,100] [--level level95]
    [--frames 600] [--warmup 60] [--headless] [--cpu-skinning] [--preset playtest] [--coney EXE] [--disc DISC]
uv run --project python coney-tools perf table REPORT.json ...
```

`perf bench` plays the level (the hub, `level95`, by default) once per fighter count with `coney --bench`
([Performance](performance.md#bench)): each run spawns that many AI fighters on the first frame of play, leaves
`--warmup` frames unmeasured and measures `--frames` more, windowed with `--vsync off` (or `--headless`: the simulation
alone), with `--skip-movies`; `--cpu-skinning` skins the humans on the CPU instead of the GPU ([GPU
skinning](performance.md#gpu-skinning)), and its reports' names end in `-cpu`. Each report and its log go to `--out` (a
scratch folder), named for the level, the count and headless; the command prints one Markdown table of the frame times
(mean, 95th and 99th percentile, frames a second), the main zones and the counters, and a line per build and renderer
the rows came from. Coney is `build/PRESET/src/platform/coney`, in the build root when one is set (`playtest` by
default: an optimised build with the debug tools, which `--bench` needs), the disc `game_dir`. Exit 1 when a run fails.
`perf table` prints the same table from reports already written.

<!-- command-reference:begin perf -->
## Parameters of `perf` {#perf-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `perf bench` {#perf-bench-usage}

Play a level once per fighter count with `coney --bench` and print a table of the reports.

```text
coney-tools perf bench [--coney CONEY] [--preset PRESET] [--disc DISC] [--level LEVEL] [--fighters FIGHTERS]
    [--frames FRAMES] [--warmup WARMUP] [--headless] [--cpu-skinning] --out OUT
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--coney CONEY` | path | none | Coney's executable; default: build/PRESET/src/platform/coney |
| `--preset PRESET` | text | `playtest` | the build folder to take Coney from (default playtest: optimised, with the debug tools --bench needs) |
| `--disc DISC` | text | none | the disc; default: game_dir from coney.local.toml |
| `--level LEVEL` | text | `level95` | the level to play (default level95) |
| `--fighters FIGHTERS` | text | `0, 10, 25, 50, 100` | the fighter counts, comma-separated (default 0,10,25,50,100) |
| `--frames FRAMES` | integer | `600` | frames measured per run (default 600) |
| `--warmup WARMUP` | integer | `60` | frames before measuring (default 60) |
| `--headless` | flag | off | no window: the simulation's cost alone |
| `--cpu-skinning` | flag | off | skin the humans on the CPU (coney --cpu-skinning), to compare with the GPU's skinning |
| `--out OUT` | path | required | the folder for the reports and logs (scratch) |

### `perf table` {#perf-table-usage}

Print the table of bench reports already written.

```text
coney-tools perf table REPORTS [REPORTS ...]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `REPORTS [REPORTS ...]` | path | required | the JSON reports, in row order |
<!-- command-reference:end -->
