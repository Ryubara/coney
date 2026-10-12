# Building and testing

This page takes you from a fresh clone to a built, tested Coney on Windows, Linux or macOS, and covers the Python
tools, the documentation and the checks CI runs on every pull request. Running the result, with every option of the
`coney` executable, is [Running Coney](running.md). The rules the code itself follows are in
[Conventions](conventions.md); where to put your game files and tools is in [Local workspace](workspace.md).

Nothing here needs the game. The build and every test run on synthetic data, so you can build and test Coney
before you have a disc image at all.

## What you need

| | Windows | Linux | macOS |
| --- | --- | --- | --- |
| Compiler | MSVC 19.38+ (Visual Studio 2022 17.8 or Build Tools 2022) | GCC 13+ or Clang 19+ | Xcode 16+ |
| CMake 3.28+ and Ninja | bundled with Visual Studio | distribution packages | Homebrew |
| Git | yes | yes | yes |
| [uv](https://docs.astral.sh/uv/) | for the Python tools and pre-commit | same | same |

`coney-tools workspace init` (or the `bootstrap.sh` and `bootstrap.ps1` launchers) checks this table on your machine
and prints what is missing ([Bootstrap](workspace.md#bootstrap)). When uv itself is missing, the launchers install a
pinned version of it with Astral's installer, which adds uv to your `PATH` (shell profiles on Linux and macOS, user
`PATH` on Windows).

The C++ dependencies (SDL3, librw, Dear ImGui, Catch2, FFmpeg) are not installed by hand: CMake downloads them on the
first configure,
at the exact commits pinned in `cmake/deps.cmake`. That first configure needs network access and takes a few
minutes; later builds reuse the copies under `build/<preset>/_deps/`.

librw is built for its OpenGL 3 platform with SDL3 creating the window and the context, so the build also needs the
OpenGL headers and libraries: part of the system on Windows and macOS, `libgl1-mesa-dev` on Linux (in the package
list below). CMake hands librw the SDL3 it fetched rather than one installed on the machine; how, and why librw's own
asserts are off, is explained in `cmake/deps.cmake`.

Clang 17 and 18 are supported by the language rules but not on Linux with libstdc++: libstdc++ only declares
`std::expected` when the compiler reports full C++20 concepts support, which Clang first does in version 19. Use
Clang 19 or newer there.

### FFmpeg {#ffmpeg}

The game's movies are Bink 1 files ([Movies](../research/movies.md)); Coney decodes them with FFmpeg's Bink demuxer,
Bink video decoder and Bink audio (DCT) decoder. `cmake/deps.cmake` fetches the FFmpeg 9.0.2 release tarball and
checks its SHA-256, and `cmake/ffmpeg/CMakeLists.txt` compiles only what those three components need (about 90 of
FFmpeg's C files) into one static library, `ffmpeg_bink`, linked by `src/platform/` alone. The build is
LGPL-2.1-or-later:
no GPL, version-3 or nonfree part, no network, no programs, no assembly ([Licences](repo:LEGAL.md)).

**Why not FFmpeg's own build.** FFmpeg builds with a `configure` script and `make`, which need a POSIX shell (MSYS2 on
Windows, with MSVC as the compiler) and refuse a source or build folder whose path has a space in it, as many Windows
checkouts do. Prebuilt LGPL archives are full builds of everything, and the usual ones are not kept at fixed versions.
So CMake builds the trimmed library itself, the same way on Windows (MSVC), Linux (GCC, Clang) and macOS (Apple
Clang), with nothing to install. It writes the files `configure` would generate (`config.h` and the component lists),
setting every `CONFIG_`, `HAVE_` and `ARCH_` name the sources use to 0 except the few the file lists; FFmpeg's plain
C code runs (no assembly), which decodes a 640 × 448 movie many times faster than it plays. FFmpeg is always built
optimised, whatever the preset, because its code relies on the compiler removing disabled branches. It adds about a
minute to a clean build; its sources are cached with the other dependencies under `build/<preset>/_deps/`.

**To update FFmpeg**, change the version, URL and SHA-256 in `cmake/deps.cmake`. If the new release links with
missing symbols, the source list in `cmake/ffmpeg/CMakeLists.txt` needs the files that define them: on Linux, configure
the release with the options in that file's header, build its three libraries, link a small program that opens and
decodes a Bink file with `-Wl,-Map`, and take the objects the map lists.

**To use a different or modified FFmpeg** (the LGPL's relinking right), point CMake at its sources and rebuild:
`cmake --preset dev -DFETCHCONTENT_SOURCE_DIR_FFMPEG=/path/to/ffmpeg`. Coney's own code sees FFmpeg only through
`src/platform/`, so any FFmpeg with the Bink components works.

### Windows

Install Visual Studio 2022 or the Build Tools for Visual Studio 2022 with the **Desktop development with C++**
workload. It brings MSVC, CMake and Ninja; you do not need to install them separately.

The compiler and those tools are on `PATH` only inside a shell with the MSVC environment loaded. Either open the
**x64 Native Tools Command Prompt for VS 2022** from the Start menu, or run `vcvars64.bat` in an existing
`cmd` shell:

```bat
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
```

For a Visual Studio install, replace `BuildTools` with your edition (`Community`, `Professional` or `Enterprise`)
and `Program Files (x86)` with `Program Files`. Every `cmake` and `ctest` command below runs from that shell.

In Git Bash, MSYS converts arguments that look like paths: a `C:\...` argument to `sed` or `git show
origin/main:<file>` can arrive mangled. Set `MSYS_NO_PATHCONV=1` for such a command, or write the script to a file
and run it, rather than quoting backslashes through a heredoc.

### Linux

On Ubuntu 24.04 (other distributions have the same packages under similar names):

```sh
sudo apt-get install cmake ninja-build g++-13 \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
  libwayland-dev libxkbcommon-dev libegl1-mesa-dev libgl1-mesa-dev libdbus-1-dev libudev-dev \
  libasound2-dev libpulse-dev libpipewire-0.3-dev
```

The second and third lines are what SDL3 needs to build its X11 and Wayland video drivers, the fourth its ALSA,
PulseAudio and PipeWire sound output; `libgl1-mesa-dev` is
also what librw's OpenGL renderer links against. For Clang, install
`clang-19 clang-tools-19` in place of `g++-13`; `clang-tools-19` brings `clang-scan-deps`, which CMake runs on C++23
sources when the compiler is Clang. Pick the compiler with `CC` and `CXX` before the first configure:

```sh
export CC=clang-19 CXX=clang++-19
```

A build directory remembers its compiler, so to switch compilers later, delete `build/<preset>/` first.

### macOS

Install Xcode 16 or newer (or its command line tools with `xcode-select --install`), then:

```sh
brew install cmake ninja
```

## Build and test

Everything goes through the presets in `CMakePresets.json`. The everyday loop is:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The first line only needs running again when a `CMakeLists.txt` or `cmake/deps.cmake` changes in a way the build
does not pick up by itself. Each preset builds into its own `build/<preset>/` folder, so presets never disturb each
other.

| Preset | Build type | What it is for |
| --- | --- | --- |
| `dev` | Debug | day-to-day work |
| `release` | RelWithDebInfo | an optimised build that can still be debugged |
| `ci` | Debug, warnings as errors | what CI builds; run it before a pull request |
| `asan` | Debug, warnings as errors, AddressSanitizer and UBSan | memory and undefined-behaviour bugs; Linux and macOS only |

`ctest` runs the Catch2 unit tests (`coney_tests`) and smoke tests of the `coney` executable itself: it starts and
stops headless, prints its help, refuses a bad command line, plays a synthetic input script and opens
[the debug menu](debug-menu.md) with the pad chord. `ctest -N` lists them all; each is declared in the
`CMakeLists.txt` of the folder it tests. The unit tests build their disc images, archives, RenderWare texture dictionaries,
streamed worlds, PS2 geometry and collision meshes byte by byte; none needs the game or a GPU (the librw tests run
librw on its NULL device).

Tests that check your own disc run only when the environment variable `CONEY_DISC` names the disc, are reported as
skipped otherwise, and print counts only. They carry the Catch2 tag `[disc]` and a tag for their subject (`[world]`,
`[story]`, `[rumble]`, ...; `coney_tests --list-tags` lists them), so you can run the ones for the area you changed.
With `CONEY_DISC` set when CMake configures, more smoke tests run `coney --disc` as well (the start-up movies, the main
menu, the character viewer, a level played under a scripted pad):

```sh
CONEY_DISC=/path/to/warriors.iso build/dev/tests/coney_tests "[disc]"
```

The level99 intro's sound check (`[disc][story][audio]`) also writes everything it mixed as a 48 kHz stereo WAV file
when `CONEY_AUDIO_WAV` names one, to listen to; keep it out of the repository.

## Sanitizers

The `asan` preset builds with AddressSanitizer and UndefinedBehaviorSanitizer and runs the tests under them. Any
report fails the test that caused it.

```sh
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

LeakSanitizer reads its suppressions from `tests/lsan.supp`, which hides one known leak inside librw and nothing in
Coney's own code. Suppressions match function names, so the sanitizer must be able to name the frames in a report.
GCC's and Apple Clang's runtimes do that on their own. Clang on Linux needs `llvm-symbolizer`: install the `llvm-19`
package and point the runtime at it, or librw's leak goes unrecognised and the tests fail.

```sh
export ASAN_SYMBOLIZER_PATH=/usr/lib/llvm-19/bin/llvm-symbolizer
```

The preset is not offered on Windows: MSVC's AddressSanitizer has no UBSan and no LeakSanitizer, so it would check
much less than the Linux and macOS runs that CI does anyway.

## clang-tidy

CI runs clang-tidy 19 over Coney's own sources (`src/` and `tests/`, never the fetched dependencies), using the
`compile_commands.json` that every preset writes. To run the same check locally on Linux, after a `ci` build with
Clang 19:

```sh
run-clang-tidy-19 -p build/ci -quiet '^(?!.*_deps).*/(src|tests)/'
```

A full pass takes over an hour, so a pull request checks only the files it changes: each changed `.cpp`, plus every
`.cpp` that includes a changed header, directly or through other headers (`.github/scripts/tidy-files.sh <base>`
prints the list for a change, or `ALL`). A change to the build setup (`CMakeLists.txt`, `cmake/`, `CMakePresets.json`,
`.clang-tidy`, the build workflow) and every push to `main`, nightly run and manual run check everything, so a header
included some unusual way is still caught within a day.

`coney-tools check changed` runs the pull request's pass for you: the same file selection, clang-tidy from
the Visual Studio LLVM folder (macOS: Homebrew's `llvm@19`) over the `build/dev` compile database, and the GCC and
Clang warnings that MSVC does not report ([the local gate](testing.md#local-gate), [its options](coney-tools.md#check)).
On Linux, where `dev` builds with GCC, configure a Clang database for it once:
`CC=clang-19 CXX=clang++-19 cmake --preset dev -B build/tidy`, then pass `--build-dir build/tidy`.

## Python tools

`python/` is the `coney-tools` package (see [Conventions](conventions.md#python)). [uv](https://docs.astral.sh/uv/)
installs the right Python and the locked dependencies into `python/.venv` the first time you run something:

```sh
uv run --project python coney-tools --help
uv run --project python pytest python/tests
uv run --project python ruff check python
uv run --project python mypy python/src python/tests
```

`coney-tools config show` prints the paths your `coney.local.toml` sets (start from `coney.local.example.toml`;
[Local workspace](workspace.md) explains each one). `coney-tools repo check-title` checks a commit or pull request
title against the commit rules in [CONTRIBUTING.md](repo:CONTRIBUTING.md#commits) (their values are in
`.github/conventions.yml`: [Repository conventions](repo-conventions.md)), `coney-tools repo check-docs`
that a change to the code also changes the docs ([Research workflow](research-workflow.md#writing-up-a-finding)), and
`coney-tools repo check-body` that a pull request description follows the template, including its Testing and
Screenshots sections and no unticked item in the Checklist
([CONTRIBUTING.md](repo:CONTRIBUTING.md#setting-up),
[What a pull request shows](testing.md#pr-evidence)).
`coney-tools wad` reads the archive on your own disc (`info`, `list`, `extract`, `names`), `coney-tools pcsx2` and
`coney-tools trace` record the original's per-update traces in PCSX2 and compare them with Coney's; [The coney-tools
command line](coney-tools.md) shows how to run each command.

## Formatting and pre-commit

clang-format decides the layout of C and C++ code, ruff that of the Python package, and a few hooks catch trailing
whitespace, broken YAML or TOML, merge-conflict markers and files over 512 KB (usually game data added by mistake).
All of them run through [pre-commit](https://pre-commit.com/), configured in `.pre-commit-config.yaml`:

```sh
uvx pre-commit run --all-files
```

Run that before every commit, or install the hooks once with `uvx pre-commit install` so that `git commit` runs them
for you. The hooks fix what they can; stage their changes and commit again. Install them once in each clone; the
worktrees of a clone share them. No hook checks a commit title: a pull request is squash-merged, so only its title
reaches `main`, and CI checks that against the commit title rules (`area: Verb the rest`, at most 72 characters; the
rules are `commits.title` in [.github/conventions.yml](repo:.github/conventions.yml) and
[CONTRIBUTING.md](repo:CONTRIBUTING.md#commits) lists them).

## Documentation

This site is built with MkDocs from `docs/`. The setup, the build and preview commands (`.venv/Scripts/python -m mkdocs
build --strict` is the one CI runs) and how to write the pages are in [Writing these docs](writing-docs.md).

## What CI runs

Every pull request, and every push to `main` or a `release/` branch, runs these GitHub Actions workflows. The `build`
workflow also runs every night on `main` and on demand: open the repository's Actions tab, choose `build`, press
"Run workflow" and pick a branch that is already on GitHub (or `gh workflow run build.yml --ref <branch>`). A manual
run checks every file with clang-tidy. On Linux and macOS the jobs compile through ccache and keep its cache between
runs, so a job recompiles only what changed; each job prints its hit rate at the end.

| Workflow | Jobs |
| --- | --- |
| `build` | `ci` preset build and tests on Windows (MSVC), Linux (GCC 13 and Clang 19) and macOS (Apple Clang); `asan` preset on Linux Clang 19 and macOS; clang-tidy (a pull request: changed files only); pre-commit over every file; the `build-ok` gate |
| `python` | ruff, mypy, pytest and `coney-tools repo check` on Windows, Linux and macOS; the `python-ok` gate |
| `docs` | `mkdocs build --strict`, markdownlint over every Markdown file, actionlint over the workflows |
| `pr` | the pull request title against the commit title rules, that code changes come with docs changes, and the description against the template (pull requests only; editing the title or description reruns it) |

One more workflow runs on issues, not pull requests: `issue-check` runs `coney-tools issue check` on every issue
opened or edited and keeps one comment listing the problems ([Filing an issue](issues.md#filing)); it is not a required
check.

On Windows, do not run WSL builds or clang-tidy locally to stand in for the Linux and macOS jobs: GitHub CI is that
gate, and a failure there is fixed forward on the branch.

### What a pull request runs {#what-a-pull-request-runs}

Runners are shared (about 20 jobs at once, 5 of them on macOS), so a pull request runs only what its change can break.
A `changes` job at the start of `build` and `python` decides, with `.github/scripts/ci-scope.sh`:

| Change | `build`, `sanitizers`, `clang-tidy` | `python` | `format`, `docs`, `pr` workflows |
| --- | --- | --- | --- |
| Draft pull request (any change) | skipped | skipped | run |
| Only documentation: every changed file is on the list below the table | skipped | Linux only | run |
| Anything else (`src/`, `tests/`, `python/`, `cmake/`, `CMakeLists.txt`, `CMakePresets.json`, `.clang-*`, the workflows, `assets/`, a file not on the list) | run | all three OSes | run |
| Push to `main` or `release/`, nightly run, manual run | run | all three OSes | run |

The documentation-only list is `paths.docs_only` in [.github/conventions.yml](repo:.github/conventions.yml) (a
trailing `/` is a folder and `*` matches within one folder level). `ci-scope.sh` reads it from the file, so there is
no second copy, and `coney-tools repo audit --files` fails a pull request when the script stops being able to. The
block below is generated from the list (`coney-tools repo render`), and `research/` includes `research/traces/`:

<!-- conventions:begin docs-only-paths -->
- `docs/`: the folder and everything under it
- `research/`: the folder and everything under it
- `*.md`: matching files at the top level only
- `.github/*.md`: matching files directly in `.github/`
- `.github/conventions.yml`: that file
- `mkdocs.yml`: that file
- `mkdocs_hooks.py`: that file
- `requirements-docs.txt`: that file
- `.markdownlint*`: matching files at the top level only
<!-- conventions:end -->

`python` still runs on Linux for documentation because its repository checks compare `docs/` and `research/` with
each other (missions, bindings, references, progress), and its tests read `research/traces/`. Marking a draft "Ready
for review" starts the full run, so open work as a draft while it is unfinished and mark it ready when it should be
checked. There is no merge queue.

A skipped matrix job reports its name unexpanded (`build (${{ matrix.name }})`), so the per-platform jobs cannot be
required checks. Each workflow ends with a gate job instead, `build-ok` and `python-ok`, which runs every time and
fails when a job it covers failed or was cancelled; the ruleset on `main` requires the gates, not the jobs behind them.
What `main` requires is the `merge` section of [.github/conventions.yml](repo:.github/conventions.yml), a copy of the
GitHub ruleset and settings, and this block is generated from it (`coney-tools repo render`):

<!-- conventions:begin required-checks -->
- Merge method: squash only. The commit on `main` has the pull request's title; its message is the pull request's
  description.
- Required checks: `title`, `docs`, `body`, `build-ok`, `python-ok`, `format`, `mkdocs`, `markdownlint`, `actionlint`.
- Auto-merge is on, the branch is deleted after the merge, history is linear and a branch need not be up to date with
  `main`.
- Who merges: maintainer and coordinator, never the agent that wrote the work.
<!-- conventions:end -->

If a job fails, the commands above reproduce it locally on the platform you are on. The workflow files under
`.github/workflows/` are short and list every step.
