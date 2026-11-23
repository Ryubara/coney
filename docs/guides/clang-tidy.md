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
would not, an older one misses what it would. `task check` looks for the program before it starts, so a machine
without one learns it at once, not after the build and the tests.

**The warnings MSVC lacks** {#local-warnings}. CI's GCC and Clang builds run `-Wall -Wextra` with `-Werror`; MSVC's
`/W4`, and so the Windows compile database, has no warning for an aggregate or designated initializer that leaves a
member out, and `-*` keeps clang-tidy's own compiler diagnostics off. A change could pass `task check` on Windows
and fail every Linux build (twice on 2026-11-22: [#1642](issue:1642)). The run therefore adds, for each warning in
`ci.tidy.local_warnings` of [.github/conventions.yml](repo:.github/conventions.yml) (`missing-field-initializers`,
`missing-designated-field-initializers`; GCC's `-Wmissing-field-initializers` covers both), the compiler's
`--extra-arg=-W<name>` and the `--checks=clang-diagnostic-<name>` that makes clang-tidy print it (a diagnostic shows
only when its check is on, and the `--checks` is added to `.clang-tidy`'s list, which stays CI's list; CI needs
neither, its database carries `-Wextra`). `warning_arguments` of `local_tidy.py` builds them, on every platform, and
the finding is one of any other: `path:line:column [clang-diagnostic-missing-field-initializers] missing field
'radar' initializer`, on the changed lines only. The warning is reported where clang puts it, at the initializer's
closing brace, so a multi-line initializer to which a member was added but whose closing line is untouched is not
caught. A member with a default member initializer may be left out, as in GCC and Clang, and so may all of them (`T{}`).
To add a warning of this kind, put its name in the list: no more is needed.
This is the clang-diagnostic case only: an unchecked optional `*` is a check's finding, not a warning, and is
found through [a copy of Microsoft's header](#differences).

**Measured** on 2026-11-22 with the pinned clang-tidy. A probe of an aggregate of three `int`s against a clang-cl
compile command with `/W4`: `HudFrame{1}`, `{1, 2}`, `{.radar = 1}` and `{.radar = 1, .gangLiving = 2}` each reported
once, and `{1, 2, 3}`, the designated form with all three and an empty `HudFrame{}` were quiet; the test is
`test_local_tidy.py`'s `test_an_initializer_that_leaves_a_member_out_is_refused` and its accepted twin (they run the
real clang-tidy, and skip where none of CI's major version is installed). For false positives, the whole of 60 sources
drawn at random from `src/` and `tests/` (seed 1642) was taken as changed, so every line counted, against the MSVC `ci`
compile database of a build: no finding. Planting `ProbeFrame{.radar = 1}` in `src/core/pads.cpp` then gave exactly
`src/core/pads.cpp:108:55 [clang-diagnostic-missing-designated-field-initializers] missing field 'gangLiving'
initializer`.

**Windows paths.** clang-tidy sees a header's path with backslashes on Windows (`<checkout>\src\core/pad.h`), so the
check hands it `.clang-tidy`'s `HeaderFilterRegex` with each `/` widened to either slash, and applies the line filter
itself after clang-tidy instead of through `--line-filter` (whose names clang-tidy compares with those paths). Both
give the findings CI's settings give; neither changes `.clang-tidy`.

### Where it differs from CI {#differences}

On Windows the compile database is MSVC's, so clang-tidy reads each source as clang-cl does, against Microsoft's
standard library and with the `_WIN32` code paths; CI reads it as Clang 19 on Linux against libstdc++. The checks that
look into the library's own code can then disagree. Measured on the pull requests of [#1496](issue:1496), with the
sources at the commit CI failed on and the MSVC `ci` compile database of the branch's own build (a source the build did
not have yet given its folder's flags):

| Pull request | CI's findings | Found here | Not found here, and why |
| --- | --- | --- | --- |
| [#1487](issue:1487) | `tests/warriors/xbox_save_test.cpp:309` `bugprone-unchecked-optional-access` | it | nothing; the local run also gave `src/fileio/qcow2_image.h:39` `bugprone-exception-escape` in the defaulted move constructor, which CI did not: Microsoft's `std::map` allocates a node when moved, libstdc++'s does not, so the constructor can throw only on Windows (advice here, below) |
| [#1488](issue:1488) | `src/gui/mod_ui/ui_model.cpp:45` `bugprone-unused-return-value`; `src/gui/mod_ui/ui_screen.cpp:447` `bugprone-optional-value-conversion` | the first | the second: the check matches the shape of `std::optional`'s converting constructor, which libstdc++ and Microsoft's library write differently, so `event(kind, *chosen)` into a `std::optional` parameter is not matched here |
| [#1492](issue:1492) | `tests/net/event_channel_test.cpp:345` and `:346` `bugprone-implicit-widening-of-multiplication-result` | both | nothing |
| [#1493](issue:1493) | `src/debug/lua_console.cpp:76` `bugprone-exception-escape` | none | `bugprone-exception-escape` follows `throw` expressions through the function bodies it can see: libstdc++ throws `std::bad_alloc` and `std::length_error` from inline code in its headers, Microsoft's library from functions compiled into its DLL (`_Xlength_error`, `_Xbad_alloc`), so a destructor that may allocate looks safe here |

Four of CI's six findings were found here. The local pass took 2 to 4 minutes for 3 to 12 sources and 15 minutes
for the 34 of [#1492](issue:1492), four at a time on a machine running other builds. So the local check catches most of
what CI reports, and the rest is of two kinds: a finding in the standard library's
shape (`bugprone-exception-escape` through an allocation, `bugprone-optional-value-conversion`), and a finding in
code only Linux compiles. Those still arrive as CI annotations and are fixed on the branch.

**An unchecked `*optional` is found here, through a copy of Microsoft's header.** `bugprone-unchecked-optional-access`
finds an access by the class that declares the operator, and Microsoft's `std::optional` declares its `operator*` in a
base class (`_Optional_construct_base`) and only brings it in with `using _Mybase::operator*;`, so the check saw an
unchecked `.value()` and `->` but never `*` (measured with the pinned clang-tidy on a planted probe against the MSVC `ci`
compile database: `return *value;` on a parameter, a default-constructed optional or a function's result, not reported;
libstdc++'s `operator*` is in `std::optional`, so CI reported all of them; [#1620](issue:1620) and
[#1627](issue:1627) failed CI on eight such lines after a clean local run). The local run therefore reads a copy of
Microsoft's `<optional>` in which that one `using` is the four `operator*` overloads declared in `std::optional`, each
calling the base class's (`python/src/coney_tools/tidy_msvc_optional.py`):

1. before the sources, one clang-tidy run on an empty file, with the compiler of the build's compile database, prints
   the include folders it uses (`-v`), and the first with an `optional` is Microsoft's;
2. the copy is written to `tidy-optional/optional` in the build folder (not again while it is current), and every
   clang-tidy command of the run gets `--extra-arg=-external:I<that folder>`, so each `#include <optional>`, the
   library's own included, finds it first. It has to be a system folder: with a plain `-I` the copy is a user header,
   and its sign-compare warnings became errors in five test files;
3. nothing but clang-tidy reads the copy, and the check does not look into the call, so it models the dereference as it
   does libstdc++'s. The cost is one 0.1-second run.

Where the header has no such `using` (a toolset that changed it) the run goes on as before and prints a note saying
that an unchecked `*` is not seen; the tests for the copy fail on such a machine (`test_tidy_msvc_optional.py`), so it
is brought up to date. On Linux and macOS nothing is copied: their libraries declare `operator*` in `std::optional`.
**A `REQUIRE(opt.has_value())` is not a guard to this check** (Catch2's macro reports to the runner and carries on, so
the analysis follows the path where the optional is empty), here as in CI: a test that dereferences after one writes the
`NOLINTNEXTLINE(bugprone-unchecked-optional-access)` with its reason, as the tests of `tests/camera/` do, or returns
early on `if (!opt)` instead.

**Advice.** `bugprone-exception-escape` differs both ways, so on Windows its findings are printed as advice
(`advice, not a failure (may differ from CI): ...`) and fail nothing; on Linux, where the library is CI's, they fail
the check as any other. The checks treated so are `ci.tidy.local_advisory` of
[.github/conventions.yml](repo:.github/conventions.yml); a check joins the list only when it is measured to differ
both ways, since a check that only misses findings here costs nothing to keep failing on.

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
4. The findings reach the pull request as annotations on the changed lines: the job writes a SARIF file and uploads it
   to code scanning (category `clang-tidy`). A pull request from a fork has a read-only token that cannot upload, so the
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
(`cmake_object_order_depends_target_<target>`: that target's generated headers and the targets it depends on), or the
source's object file when there is no such target. An order-only target compiles none of its own target's sources,
but it also builds the targets its target depends on, in full: `coney-studio`'s pulls in `coney` (the executable,
built and linked), and `coney`'s the copied assets, so a plan with a `studio_*.cpp` source, and every pass over the
whole codebase, still builds `coney`. Coney generates one file this way, the embedded shader data of
`src/platform/shaders/`; SDL3's and Catch2's configuration headers are written by the configure step.
The build job is the one that reports compile errors, so a failure here does not stop the job.

## The result cache {#cache}

`ci tidy-run --cache DIR` keeps each source's outcome, one JSON file per entry (`python/src/coney_tools/tidy_cache.py`).
An entry is named by clang-tidy's version, the text of `.clang-tidy` and the source's compile command, and holds
clang-tidy's exit code, every finding it printed (all lines: the change's lines are chosen after the cache, so one entry
serves any pull request) and the SHA-256 of every file the source read, system headers included, from the dependency
file clang-tidy writes when asked. A later run takes the entry instead of running clang-tidy while every one of those
files is unchanged; a changed or missing file is a miss. A failure of clang-tidy itself is never kept, nor the outcome
of a source that does not compile (a `clang-diagnostic-error` finding): a missing include is not in the dependency
file, so a kept compile error would stay after the header it lacked appears (a generated header `ci tidy-deps` failed
to build, say).

One change the hashes cannot see: a **newly added** header that shadows an include (an earlier include directory
gains a file of the same name) or that makes a `__has_include` true. Neither is in the dependency file, which names
only the files that were read, so such a change gets the kept outcome until another input of the source changes. It
is rare here (no Coney header shares a name with another on the include path); the pass over every file nightly runs
through the same cache, so a suspect entry is cleared by changing `.clang-tidy` or by deleting the cache entry in the
repository's Actions caches.

In CI the folder is an `actions/cache` entry, `clang-tidy-results-<hash of .clang-tidy>-<run id>`: every run restores
the newest one saved for the same `.clang-tidy` in its own ref's scope or `main`'s. Pull requests never save (a pull
request's cache is scoped to its own ref, so nobody else could restore it, and it would push `main`'s entries out);
every other run saves into its own ref's scope: `main`'s pushes and nightly runs, which every run reads, and manual
runs and `release/**` pushes, which only later runs of that same ref read. A run saves also when a newer push
cancels it, since each entry is written whole as its source finishes. The `cache-prune` job keeps one such entry per
branch, like the compiler caches ([CI caches](building.md#ci-caches)). A change to `.clang-tidy` starts from an
empty cache.

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

With this design the job builds the planned sources' order-only targets instead (generated headers only, nothing
compiled), so the full-build column, 53 to 88 percent of each job above, goes. The clang-tidy step itself is unchanged
on a source the cache has not seen; one it has seen with the same inputs costs a lookup (two sources here: 56 seconds
run, 0.1 seconds from the cache, each entry about 50 KB for the 280 to 360 files it read). The job's own times after the
change are those of its first runs in CI.
