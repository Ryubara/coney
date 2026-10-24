# clang-tidy

How CI runs clang-tidy on Coney's own sources: [on a pull request](#on-a-pull-request), [the checks](#checks) it
runs, and [the whole-codebase pass](#whole-codebase) nightly, by hand and for a build-setup change. The other CI
jobs and where each check runs are in [What CI runs](building.md#what-ci-runs).

## Where it runs {#where}

CI tidies every pull request that changes engine files, **on the lines it changes**, in the required check `clang-tidy`,
and every push to `main` or a `release/` branch the same way (the pushed commit's change, `HEAD^1` to `HEAD`; findings
in the log). It tidies every file of Coney's own sources (`src/` and `tests/`, never the fetched dependencies) nightly
on `main` and when started by hand. There is no local clang-tidy, and no local WSL builds or clang-tidy runs on Windows:
a full pass takes 50 to 100 minutes on a CI runner (most of it spent in the standard library's and the dependencies'
headers), and a local gate competed with the builds for cores and memory. The maintainer dropped it on 2026-10-23
(#514), so a finding comes back as an annotation on the pull request and is fixed forward on the branch.

## On a pull request {#on-a-pull-request}

The `clang-tidy` job of the `build` workflow does the same as the other engine jobs: it is skipped (and so counts as
passed) for a draft and for a change that touches no engine file ([what a pull request
runs](building.md#what-a-pull-request-runs)). Otherwise:

1. `coney-tools ci tidy-plan` reads the pull request's diff (`git diff -U0`) and chooses what to check. A changed source
   (`.cpp`) is checked itself; a changed header is checked through **one** source that includes it, directly or through
   other headers (the source with the header's own name if there is one, `brain.h` through `brain.cpp`, else the nearest
   includer): one is enough to analyse the header's lines, and a header reaches 100 to 280 of the sources. The plan
   also holds the changed lines of every file. **A change to the build setup** (the top `CMakeLists.txt`, anything
   under `cmake/`, `CMakePresets.json`, `.clang-tidy`: `ci.tidy.whole_when`) reaches files it does not touch, a compiler
   flag or a check setting being read by every one, so the plan then says `everything` and the job runs the
   whole-codebase pass below on that pull request instead of the changed lines. Every other pull request is checked
   on its changed lines only. The settings (the folders, the extensions, the build-setup files) are `ci.tidy` of
   [.github/conventions.yml](repo:.github/conventions.yml).
2. The job configures and builds the `ci` preset with Clang 19 (through the [compiler cache](building.md#ci-caches),
   which starts from `main`'s), because the build generates the headers clang-tidy needs and writes the compile
   database.
3. `coney-tools ci tidy-run` runs `clang-tidy-19` on the planned sources, several at once, with a line filter of the
   changed lines: a diagnostic on a line the pull request did not touch is never reported, whichever source it was
   found through. The checks are the repository's `.clang-tidy`, and its `WarningsAsErrors` makes every finding an
   error. The job fails on any finding, and also when clang-tidy itself fails (a missing compile database, a crash) or
   a planned source does not compile (a compiler error on any line, changed or not: clang-tidy could not analyse it).
4. The findings reach the pull request as annotations on the changed lines: the job writes a SARIF file and uploads it to
   code scanning (category `clang-tidy`). A pull request from a fork has a read-only token that cannot upload, so the
   job prints the findings as workflow commands (`::error file=...`), which annotate the diff as well. Either way the
   job's summary lists the first findings and the log holds all of them.

To check again after a fix, push (the job reruns) or use "Re-run jobs". A finding that is a false positive is
silenced at its line with `// NOLINT(check-name)` and a reason, rarely. A finding your change causes in a line you did
not touch (you removed a null check that code elsewhere relied on) is not on the pull request: the nightly run reports
it, and a failed nightly run opens a `P-critical` issue ([Failures on main](building.md#ci-failure)).

## The checks {#checks}

The checks are the list in `.clang-tidy`: every check is named in full, none by wildcard, so a newer clang-tidy
never adds a check nobody chose (a pull request would go red after a runner image update with no change of ours). The
first entry, `-*`, turns off clang-tidy's own defaults, and the rest is sorted, one check per line. **A new check is
added by name, never by wildcard**: put its full name in its place in the list (`clang-tidy-19 --checks='-*,bugprone-*'
--list-checks` prints a family's names). A check that is left out on purpose has its reason in a comment at the end of
the file. The file is a build-setup file (`ci.tidy.whole_when`), so the pull request that changes it runs the
whole-codebase pass and must pass it. A test (`python/tests/test_clang_tidy_config.py`) refuses a wildcard, a removal or
an unsorted list.

## The whole-codebase pass {#whole-codebase}

The `build` workflow also runs every night on `main` and on demand ([What CI runs](building.md#what-ci-runs)), and a
change to the build setup runs the same pass; all of them check every file in the same job. The pass takes close to an
hour, its findings are in the log, not annotations, and it **fails on any finding anywhere** in `src/` and `tests/`:
while the nightly pass is red, a build-setup pull request is red for findings it did not cause, so fix those (the open
`P-critical` issue lists them) first or in the same pull request.

```sh
run-clang-tidy-19 -p build/ci -quiet '^(?!.*_deps).*/(src|tests)/'
```

On Linux, after a `ci` build with Clang 19, the same command checks the whole codebase and the two `ci` commands above
(`tidy-plan --diff <(git diff -U0 --no-renames origin/main...HEAD) --out plan.json`, then `tidy-run --plan plan.json
--build-dir build/ci --clang-tidy clang-tidy-19`) reproduce a pull request's pass; other machines need not.
