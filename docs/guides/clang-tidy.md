# clang-tidy

How Coney runs clang-tidy on its own sources: [here, before the pull request](#local), in `task check`; [on the pull
request](#on-a-pull-request), in CI; [the checks](#checks) it runs; and [the whole-codebase pass](#whole-codebase) on
`main`, nightly, by hand and for a build-setup change. CI [builds only what clang-tidy reads](#build) and keeps [a
result cache](#cache). The other CI jobs and where each check runs are in [What CI runs](building.md#what-ci-runs).

## Where it runs {#where}

Three places, with one list of checks (the repository's `.clang-tidy`) and one major version of clang-tidy
(`ci.tidy.llvm_major` of [.github/conventions.yml](repo:.github/conventions.yml), 19):

| Where | What it checks | Who sees a finding |
| --- | --- | --- |
| `task check`'s `clang-tidy` check, on the engineer's machine (Windows too) | the lines the change touches in its C++ files | the engineer, before review: the check fails |
| CI's required `clang-tidy` check on a pull request | the same lines, read through Linux's headers | everyone: annotations on the pull request |
| CI on a push to `main` or a `release/` branch, nightly, by hand, and on a pull request that changes the build setup | every file of `src/` and `tests/` | the log and the summary; a failed run on `main` opens a `P-critical` issue ([Failures on main](building.md#ci-failure)) |

The local check came after six pull requests failed CI's clang-tidy in one day after their reviews had passed, each
then costing a fix unit, a new check, a new review and another CI round ([#1496](issue:1496)). It replaced the rule
that no clang-tidy ran on Windows.

## Here, before the pull request {#local}

`task check` runs the `clang-tidy` check after its build, when a changed path is under `src/` or `tests/`
(`proof.only_when`; [task check](coney-tools-repo.md#task-check)); `coney-tools ci tidy-local` runs the same pass alone
([`ci tidy-local`](coney-tools-ci.md#tidy-local)). It is CI's pull-request pass, from the same code
(`python/src/coney_tools/local_tidy.py` over `ci_tidy.py`):

1. the branch's diff against its merge base with the base branch (`git diff -U0 --no-renames`, as the workflow);
2. the sources [the plan](#on-a-pull-request) picks: a changed source itself, a changed header through one source that
   includes it;
3. clang-tidy with `.clang-tidy` on each, against the compile database the build of the `ci` preset just wrote, a
   check slot's share of the cores at a time, below normal priority;
4. only the findings on the changed lines count. Each prints as `path:line:column [check] message`, and any fails the
   check, as does a source that does not compile or that the compile database does not name (build again).

**Setting it up.** On Windows, `coney-tools workspace init --fetch clang-tidy` downloads LLVM's official Windows release
at the version CI's runner installs (19.1.1) and keeps only `clang-tidy.exe` and clang's own headers, in
`tools/clang-tidy-19.1.1` ([the downloads](workspace.md#fetch)). On Linux install `clang-tidy-19` with apt, on macOS
Homebrew's `llvm@19`. `clang_tidy_program` of coney.local.toml names another copy. A clang-tidy of another major
version, or none, makes the check "cannot run" (no stamp, nothing wrong with the change): a newer one reports what CI
would not, an older one misses what it would.

**Windows paths.** clang-tidy sees a header's path with backslashes on Windows (`C:\...\src\core/pad.h`), so the check
hands it `.clang-tidy`'s `HeaderFilterRegex` with each `/` widened to either slash, and applies the line filter itself
after clang-tidy instead of through `--line-filter` (whose names clang-tidy compares with those paths). Both give the
findings CI's settings give; neither changes `.clang-tidy`.

### Where it differs from CI {#differences}

On Windows the compile database is MSVC's, so clang-tidy reads each source as clang-cl does, against Microsoft's
standard library and with the `_WIN32` code paths; CI reads it as Clang 19 on Linux against libstdc++. The checks that
look into the library's own code can then disagree. Measured on the pull requests of [#1496](issue:1496), at the commit
CI failed, with the local pass against an MSVC `ci` compile database of the same sources:

| Pull request | CI's findings | Found here | Not found here, and why |
| --- | --- | --- | --- |
| [#1487](issue:1487) | `tests/warriors/xbox_save_test.cpp:309` `bugprone-unchecked-optional-access` | MEASURE-1487 | MEASURE-1487-WHY |
| [#1488](issue:1488) | `src/gui/mod_ui/ui_model.cpp:45` `bugprone-unused-return-value`; `src/gui/mod_ui/ui_screen.cpp:447` `bugprone-optional-value-conversion` | the first | the second: the check matches the shape of `std::optional`'s converting constructor, which libstdc++ and Microsoft's library write differently, so `event(kind, *chosen)` into a `std::optional` parameter is not matched here |
| [#1492](issue:1492) | `tests/net/event_channel_test.cpp:345` and `:346` `bugprone-implicit-widening-of-multiplication-result` | MEASURE-1492 | MEASURE-1492-WHY |
| [#1493](issue:1493) | `src/debug/lua_console.cpp:76` `bugprone-exception-escape` | none | `bugprone-exception-escape` follows `throw` expressions through the function bodies it can see: libstdc++ throws `std::bad_alloc` and `std::length_error` from inline code in its headers, Microsoft's library from functions compiled into its DLL (`_Xlength_error`, `_Xbad_alloc`), so a destructor that may allocate looks safe here |

So the local check catches most of what CI reports, and the rest is of two kinds: a finding in the standard library's
shape (`bugprone-exception-escape` through an allocation, `bugprone-optional-value-conversion`), and a finding in
code only Linux compiles. Those still arrive as CI annotations and are fixed on the branch.

## On a pull request {#on-a-pull-request}

The `clang-tidy` job of the `build` workflow does the same as the other engine jobs: it is skipped (and so counts as
passed) for a draft and for a change that touches no engine file ([what a pull request
runs](building.md#what-a-pull-request-runs)). Otherwise:

1. `coney-tools ci tidy-plan` reads the pull request's diff (`git diff -U0 --no-renames`, so a pure rename shows as a
   deletion and an addition) and chooses what to check. A changed source (`.cpp`) is checked itself; a changed header
   is checked through **one** source that includes it, directly or through other headers (the source with the header's
   own name if there is one, `brain.h` through `brain.cpp`, else the nearest includer): one is enough to analyse the
   header's lines, and a header reaches 100 to 280 of the sources. The plan also holds the changed lines of every file.
   **A change to the build setup** (the top `CMakeLists.txt`, anything under `cmake/`, `CMakePresets.json`,
   `.clang-tidy`: `ci.tidy.whole_when`) reaches files it does not touch, a compiler flag or a check setting being read
   by every one, so the plan then says `everything` and the job runs [the whole-codebase pass](#whole-codebase) on that
   pull request. The settings (the folders, the extensions, the build-setup files) are `ci.tidy` of
   [.github/conventions.yml](repo:.github/conventions.yml).
2. The job configures the `ci` preset with Clang 19, which writes the compile database, and [builds only the headers
   clang-tidy reads](#build).
3. `coney-tools ci tidy-run` runs `clang-tidy-19` on the planned sources, several at once, through [the result
   cache](#cache), and keeps only the findings on the changed lines: a diagnostic on a line the pull request did not
   touch is never reported, whichever source it was found through. The checks are the repository's `.clang-tidy`, and
   its `WarningsAsErrors` makes every finding an error. The job fails on any finding, and also when clang-tidy itself
   fails (a missing compile database, a crash) or a planned source does not compile (a compiler error on any line,
   changed or not: clang-tidy could not analyse it).
4. The findings reach the pull request as annotations on the changed lines: the job writes a SARIF file and uploads it to
   code scanning (category `clang-tidy`). A pull request from a fork has a read-only token that cannot upload, so the
   job prints the findings as workflow commands (`::error file=...`), which annotate the diff as well. Either way the
   job's summary lists the first findings and the log holds all of them.

To check again after a fix, push (the job reruns) or use "Re-run jobs". A finding that is a false positive is
silenced at its line with `// NOLINT(check-name)` and a reason, rarely. A finding your change causes in a line you did
not touch (you removed a null check that code elsewhere relied on) is not on the pull request: the pass over every file
on `main` reports it.

## Building only what it reads {#build}

clang-tidy needs the compile database and the headers the build generates, not the object files. The job therefore
builds, after configuring, only the targets `coney-tools ci tidy-deps` prints for the plan's sources: for each, the
order-only target CMake's Ninja generator writes for the target that compiles it
(`cmake_object_order_depends_target_<target>`: that target's generated headers and its dependencies', nothing
compiled), or the source's object file when there is no such target. Coney generates one file this way, the embedded
shader data of `src/platform/shaders/`; SDL3's and Catch2's configuration headers are written by the configure step.
The build job is the one that reports compile errors, so a failure here does not stop the job.

## The result cache {#cache}

`ci tidy-run --cache DIR` keeps each source's outcome, one JSON file per entry (`python/src/coney_tools/tidy_cache.py`).
An entry is named by clang-tidy's version, the text of `.clang-tidy` and the source's compile command, and holds
clang-tidy's exit code, every finding it printed (all lines: the change's lines are chosen after the cache, so one entry
serves any pull request) and the SHA-256 of every file the source read, system headers included, from the dependency
file clang-tidy writes when asked. A later run takes the entry instead of running clang-tidy while every one of those
files is unchanged; a changed or missing file is a miss. A failure of clang-tidy itself is never kept.

In CI the folder is an `actions/cache` entry, `clang-tidy-results-<hash of .clang-tidy>-<run id>`: every run restores
the newest one `main` saved for the same `.clang-tidy`, and only `main`'s runs save (a pull request's cache is scoped
to its own ref, so nobody else could restore it), also when a newer push cancels the run, since each entry is written
whole as its source finishes. The `cache-prune` job keeps one such entry per branch, like the compiler caches
([CI caches](building.md#ci-caches)). A change to `.clang-tidy` starts from an empty cache.

## The checks {#checks}

The checks are the list in `.clang-tidy`: every check is named in full, none by wildcard, so a newer clang-tidy
never adds a check nobody chose (a pull request would go red after a runner image update with no change of ours). The
first entry, `-*`, turns off clang-tidy's own defaults, and the rest is sorted, one check per line. **A new check is
added by name, never by wildcard**: put its full name in its place in the list (`clang-tidy-19 --checks='-*,bugprone-*'
--list-checks` prints a family's names). A check that is left out on purpose has its reason in a comment at the end of
the file. The file is a build-setup file (`ci.tidy.whole_when`), so the pull request that changes it runs the
whole-codebase pass and must pass it. A test (`python/tests/test_clang_tidy_config.py`) refuses a wildcard, a removal or
an unsorted list, and another (`test_workspace_fetch.py`) keeps the major version of the workflow's `clang-tidy-19`,
the Windows download and `ci.tidy.llvm_major` equal.

## The whole-codebase pass {#whole-codebase}

A push to `main` or a `release/` branch, the nightly run and a manual run of the `build` workflow
([What CI runs](building.md#what-ci-runs)) check every file of Coney's own sources (`src/` and `tests/`, never the
fetched dependencies under `_deps/`), and so does a pull request that changes the build setup: `ci tidy-plan --all` (or
the build-setup plan), then `ci tidy-run` over every source the compile database names, keeping every finding. Its
findings are in the log and the summary, not annotations, and it **fails on any finding anywhere** in `src/` and
`tests/`: while `main` is red, a build-setup pull request is red for findings it did not cause, so fix those (the open
`P-critical` issue lists them) first or in the same pull request.

Without the cache the pass takes 80 to 140 minutes on a runner (the clang-tidy step of the last two nightly runs
before the cache took 83 and 139 minutes); with it, a run checks again only the sources whose inputs changed since
`main`'s last saved run. On Linux, after configuring the `ci` preset with Clang 19 and building `ci tidy-deps`'
targets, `ci tidy-plan --all --out plan.json` then `ci tidy-run --plan plan.json --build-dir build/ci --clang-tidy
clang-tidy-19` reproduce the pass, and `tidy-plan --diff` of `git diff -U0 --no-renames origin/main...HEAD` in place of
`--all` a pull request's.

### Measured {#measured}

The pull-request job before this design, on the four failed runs of [#1496](issue:1496): 5 to 34 minutes, most of it
the full build that only generated the headers.

| Pull request | Job | Package install | Full build | clang-tidy |
| --- | --- | --- | --- | --- |
| [#1487](issue:1487) | 10.3 min | 39 s | 431 s | 95 s |
| [#1488](issue:1488) | 22.4 min | 41 s | 1175 s | 71 s |
| [#1492](issue:1492) | 34.0 min | 1159 s (a slow mirror) | 593 s | 232 s |
| [#1493](issue:1493) | 5.0 min | 42 s | 160 s | 44 s |

AFTER-MEASURE
