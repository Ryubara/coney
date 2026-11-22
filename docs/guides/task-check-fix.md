# Fix checks

How `coney-tools task check` ([the command](coney-tools-repo.md#task-check)) checks a fix to a change whose Check
already passed: [when it applies](#fix), [when the full Check runs instead](#fallback), [what the stamp holds and `pr
verify` accepts](#stamp), [what it leaves out](#blind) and [what it saves](#measured).

## A fix runs the tests of what changed {#fix}

A fix unit changes a line or two of a pull request whose full Check already passed, and checking the whole change again
repeats the earlier run: 1421 (Xbox save import) touched `src/CMakeLists.txt`, so each of its fixes ran the whole suite
again, 4099 to 4159 tests with the disc and 16 to 25 minutes of them, for fixes that one to three test files reach. So
`task check` checks a change as a **fix** when the task's log (the parent's, for a fix task) holds an earlier passing
Check it can stand on, the *anchor*: the newest `checked` stamp that passed, is of another change, is not a fix itself,
and ran a profile at least as big as the one the change needs now. A fix runs the same checks as the profile the change
needs, the build included, but the Python and C++ tests are scoped to **the files changed since the anchor**, not to the
whole branch: the paths whose own part of the change hash differs between the two changes (`pr_diff.file_hashes`), so a
rebase that leaves the branch's own lines as they were changes no file, and a resolved conflict does. Which tests that
is follows the usual rules ([Which Python tests run](coney-tools-repo.md#task-check-pytest), [Which C++ tests
run](coney-tools-repo.md#task-check-ctest)) applied to those files, so an engine file the tests cannot be traced from (a
scene list, a reference picture, a shader, the console's command list) runs the whole suite, as it would in any Check.
Only a fix whose every changed file is of a class no C++ test reads (`proof.ctest.skip`: documentation and tools) runs
the C++ tests of the branch's own test files, so the build is still run against the tests it wrote.

The Python tests of a fix are scoped the same way with one addition: they also include the tests that read **a scoped
class the whole branch touched** (`proof.pytest.scoped`: `docs_only`, `engine`), not only those of the files changed
since the anchor (`proof_profiles.fix_pytest_scope`). A test marked `reads("engine")` scans a whole tree (the guard that
no C++ test saves a capture to a fixed name reads every file under `tests/`), so a fix that edited only a page, after a
branch that edited C++ test files, still runs it; with the files since alone it ran only the `docs_only` tests and the
guard was skipped (task 1588, review round 8: two captures it would have caught). These are the cheap tests (about 540,
no check slot); a branch path under `whole_when` or of an unscoped class adds nothing, since the anchor ran the whole
suite for it, and `pr verify` holds the stamp to the same scope.

## When the full Check runs instead {#fallback}

The rule is `proof.fix` in [.github/conventions.yml](repo:.github/conventions.yml), read from the base like the rest of
`proof`; a base without the key has no fix profile. **The full Check runs instead** when, since the anchor:

* more files changed than `max_files` (10);
* a file matching `full_when` changed: the CMake files, the presets, `cmake/`, any `.cmake` file, `tests/support/` and
  the Python tests' fixtures;
* a header changed that more than `max_header_includers` (400) C++ files include, directly or through other headers.
  The engine's core includes itself in one large cycle, so half of the 750 headers have more than 94 includers; 400
  picks out the 60 most shared (`core/error.h` with 1534, `core/pad.h`, `graphics/render_device.h`, ...) at the time
  of writing;
* the C++ tests of what changed come out as the whole suite anyway: a deleted or moved C++ file, or an engine file
  that is no C++ file the tests can be traced from (`tests/pictures/scenes.toml`, a reference picture,
  `src/console/commands.toml`, a shader, `tests/lsan.supp`, an asset);
* or the anchor's commit cannot be read, or no longer gives the change its stamp covers.

The output says which on its `fix:` line (`fix: on Check #35 (change 1369909b79df): 4 files changed since
(src/fileio/fatx_volume.cpp, ...); the tests are theirs`, or `fix: not used: src/CMakeLists.txt changed
(proof.fix.full_when: */CMakeLists.txt); the full check runs ...`). `--no-fix` and `--profile` always run the full
Check. A resume ([Resuming a run](task-check-resume.md#resume)) never mixes the two: the fix's profile in the resume key
names its anchor.

## The stamp and pr verify {#stamp}

The stamp is an ordinary `checked` stamp of the profile the change needs, with `detail.fix`: the anchor's sequence
number (`on`), the hash of its stamp (`on_stamp`) and of its change (`on_change`), and the count and hash of the files
changed since (`files`, `sha256`). A fix after a fix stands on the same anchor, with every file changed since it. `pr
verify` accepts the fix's stamp on top of the anchor: it finds the anchor in the log by `on` and `on_stamp`, requires
that it could be stood on and that its build record still holds, recomputes the files changed since and the fallback
rules from the base's `proof.fix`, refuses a fix those rules no longer allow or whose files differ, and holds the
stamp's `pytest_scope` and `ctest_scope` to the tests of those files ([pr verify](pr.md#verify)).

## What a fix does not run again {#blind}

By the maintainer's decision of 2026-11-20: the tests of the rest of the branch, which the anchor ran, and the tests
that main's commits since could affect after a rebase, which CI runs on the pull request and `ci disc-suite` after the
merge. The build itself is always whole, so a rebase that breaks the compile is caught.

## What it saves {#measured}

Measured on the fix units of 2026-11-19 by working out their plans again from their stamp logs and commits. The fix
profile's C++ tests are estimated from the recorded time of each of their test cases (the build keeps them in
`test-costs.json`), and its other checks and build from the full run's log, since a fix runs those as they are:

| Check | Full (from its log) | Fix profile |
| --- | --- | --- |
| 1421-fix (Check #34): 4 files changed since Check #30, two sources and their tests | 42 min: 4 min of checks, a 20-minute rebuild after a rebase, the whole suite (4099 tests, 16 min) because the branch touched `src/CMakeLists.txt` | about 24 min: the same checks and rebuild, then 3 test files, 19 cases, 6 s |
| 1421 (Check #35): 1 test file changed since Check #34, no rebase | 30 min: 4 min of checks, a 14 s build, the whole suite (4159 tests, 25.5 min) | about 5 min: 1 test file, 5 cases, 2 s |
| 1204-fix (Check #11): rebased; `imgui_overlay_test.cpp` and a page changed since Check #8 | 65 min: 3 min of checks, a 38-minute rebuild after a rebase, the whole suite (4194 tests, 24 min) because the branch touched `cmake/deps.cmake` | about 41 min: 1 test file, 3 cases, 10 s |

The tests were most of the time saved; a fix that follows a rebase still waits for its rebuild.
