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

### Check your setup {#doctor}

```sh
uv run --project python coney-tools doctor [--disc DISC]
```

One command checks everything above and the rest a contributor needs, and prints `ok`, `problem` or `note` per line,
with the fix after `->`: the build tools (Git, the compiler, CMake and Ninja, uv, ccache, clang-tidy 19, and on Linux
an old default `g++` while a newer one is installed, which needs `CC` and `CXX`), `coney.local.toml` and the scratch
folder, your disc (`--disc`, else `game_dir`: `SLUS_212.15` and `WARRIORS.DIR` by size and SHA-1, `WARRIORS.WAD` by
size, against the NTSC-U release the research refers to), the research tools (JDK 21, Node.js, Ghidra, the compiled
EE language, a project holding `SLUS_212.15`, whether port 8090 is free or held by ghidra-mcp, the PCSX2-MCP build
`pcsx2_mcp_dir` names, with the command that downloads the fork's release when there is none, and a BIOS in the first
copy) and the free disk
space (a `dev` build with its tests takes about 6.5 GB; under 10 GB free is a problem). It works in a plain clone,
needs no `--agent` and no configuration, changes nothing and prints sizes and hashes only. A problem is something the
engine's build or the disc needs, and makes the exit code 1; a note is optional (another release of the game, a
missing research tool). On an idle machine it takes a few seconds; hashing the disc's two small files is most of it.

The C++ dependencies (SDL3, librw, Dear ImGui, ENet, Catch2, FFmpeg) are not installed by hand: CMake downloads them
on the first configure, at the exact commits pinned in `cmake/deps.cmake`. That first configure needs network access
and takes a few minutes; later builds reuse the copies under `build/<preset>/_deps/`.

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

### ENet {#enet}

Network play ([Network multiplayer](multiplayer.md)) sends its packets over UDP with ENet 1.3.18 (MIT).
`cmake/deps.cmake` fetches the `v1.3.18` tag's commit and compiles ENet's nine C files into a static library,
`enet`, linked by `src/platform/` alone, as Dear ImGui is built: ENet's own `CMakeLists.txt` is not used (it would
add directory-wide flags), so the fetch names a `SOURCE_SUBDIR` it does not have. Its configure step only detects
socket functions every supported system has, so the definitions are set in `cmake/deps.cmake` for Linux, macOS
uses `unix.c`'s own defaults, and Windows links Winsock (`ws2_32`) and `winmm`, whose timer ENet times its resends
with. To update ENet, change the commit and the tag comment together.

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
and `Program Files (x86)` with `Program Files`. A person running `cmake` and `ctest` by hand runs them from that
shell; [`coney-tools build`](coney-tools.md#build) loads the environment itself, so an agent needs no such shell.

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

Everything goes through the presets in `CMakePresets.json`. **Agents build and test only through `coney-tools build`**,
which holds a [build slot](coney-tools.md#build) (at most two builds run on the machine at once, so six agents do not
starve one another), configures when the build folder has no cache, builds, and with `--test` runs the tests; it
prints a short summary and keeps the full output in a log. Nothing it starts outlives it: the compilers, `ctest` and
the test executables end when the command ends or the shell that ran it goes away, and a crashed run's leftovers are
stopped by the next build that takes its slot ([Process lifetimes](coney-tools.md#build)):

```sh
uv run --project python coney-tools build --agent <your id> --preset dev --test
```

A hook in `.claude/settings.json` refuses `cmake --build`, `cmake --preset`, `ctest` and `ninja` typed directly (it
only applies inside Claude Code). A person may use either way. Typed in a terminal, `--agent` may be left out: the build
is then labelled with your user name. An agent always passes it (it is required when the output is not a terminal,
and inside Claude Code). On Windows the
tool loads the Visual Studio environment itself, so no special prompt is needed. Underneath, and for a person
alone on a machine, the everyday loop is:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The first line only needs running again when a `CMakeLists.txt` or `cmake/deps.cmake` changes in a way the build
does not pick up by itself (`coney-tools build --configure` forces it). Each preset builds into its own
`build/<preset>/` folder, so presets never disturb each other.

!!! note "For the project's agents"

    The next two paragraphs are about several AI agents sharing one machine; a person building alone can skip them.
    Alone on a machine, a build never waits for a slot.

    A build or a test run that takes minutes runs in the background with its output in a log. Waiting for it is
    bounded: never `until grep -q ... log; do sleep ...; done`, which waits for ever when the build dies first (a hook
    refuses it; [Bounded waits](agents.md#bounded-waits)). A `ctest` whose caller is gone keeps starting tests;
    `coney-tools workspace clean --apply` stops it ([Cleanup](workspace.md#cleanup)).

    No agent waits on a build past its prompt cache (about an hour): a call waits at most 40 minutes for a slot and
    takes at most 45 in all, and leaves at once with exit 75 when the queue says it cannot fit. A longer wait runs
    detached (`--detach`), the agent writes `STATUS.md` and stops, and whoever continues reads `coney-tools build
    result` ([Build waits](agents.md#build-waits)).

| Preset | Build type | What it is for |
| --- | --- | --- |
| `dev` | Debug | day-to-day work |
| `release` | RelWithDebInfo | an optimised build that can still be debugged |
| `ci` | Debug, warnings as errors | what CI builds; run it before a pull request |
| `asan` | Debug, warnings as errors, AddressSanitizer and UBSan | memory and undefined-behaviour bugs; Linux and macOS only |

On MSVC every preset keeps debug information in each object (`/Z7`): [Debug information](#debug-info).

`ctest` runs the Catch2 unit tests (`coney_tests`) and smoke tests of the `coney` executable itself: it starts and
stops headless, prints its help, refuses a bad command line, plays a synthetic input script and opens
[the debug menu](debug-menu.md) with the pad chord. `ctest -N` lists them all; each is declared in the
`CMakeLists.txt` of the folder it tests. The unit tests build their disc images, archives, RenderWare texture dictionaries,
streamed worlds, PS2 geometry and collision meshes byte by byte; none needs the game or a GPU (the librw tests run
librw on its NULL device).

Tests that check your own disc run only when the environment variable `CONEY_DISC` names the disc, are reported as
skipped otherwise, and print counts only. `coney-tools build --test` sets it from `game_dir` in `coney.local.toml`
when it is not already set ([`build`](coney-tools.md#build)); a raw `coney_tests` run needs it set by hand. They carry the Catch2 tag `[disc]` and a tag for their subject (`[world]`,
`[story]`, `[rumble]`, ...; `coney_tests --list-tags` lists them), so you can run the ones for the area you changed.
With `CONEY_DISC` set when CMake configures, more smoke tests run `coney --disc` as well (the start-up movies, the main
menu, the character viewer, a level played under a scripted pad):

```sh
CONEY_DISC=/path/to/warriors.iso build/dev/tests/coney_tests "[disc]"
```

Under `ctest` each test case is a test, and its Catch2 tags are its labels, so `-L disc` selects the disc tests (the
hand-registered `coney.*` checks that exist only when `CONEY_DISC` is set at configure time carry the label too) and
`-LE disc` leaves them out: `coney-tools build --agent <id> --test -L disc` ([`build`](coney-tools.md#build)). A
test case that skips without the disc but lacks the tag fails a Python test.

The level99 intro's sound check (`[disc][story][audio]`) also writes everything it mixed as a 48 kHz stereo WAV file
when `CONEY_AUDIO_WAV` names one, to listen to; keep it out of the repository.

### The debug tools and the game {#debug-tools}

Coney is getting two kinds of build (a maintainer decision, 2026-10-21): builds with the **debug tools**, which
developers and agents use, and a **release** build a player will get, the game alone with none of the tools compiled
in (the launcher will install it). The split lands in steps: today the tools have targets of their own, and every
build still carries them. The debug tools are the [debug menus](debug-menu.md) and their pages, the developer
overlay and Dear ImGui, the debug lines, the viewers, the cheats and tunables, the [sandbox](sandbox.md) and the
developer command-line flags.

The split is made by target, not by `#ifdef`. The game is `coney_core`, `coney_audio`, `coney_movies`, `coney_net`
and `coney_platform`; the debug tools are two libraries of their own:

| Target | Folder | Holds |
| --- | --- | --- |
| `coney_debug` | `src/` | the debug menus' model and pages (`src/debug/`), the pad menu's renderer and bitmap font, the sandbox's worlds, the sheet and text viewers |
| `coney_devtools` | `src/platform/` | the menus' platform glue, the developer overlay (the only target that links Dear ImGui), the texture, character and sandbox viewers, the reference images |

`coney` and `coney_tests` link both. The game reaches the debug tools only through hooks it offers, never the other
way round: the play mode has Coney's own capabilities as its own API (teleport, freeze, noclip, spawn, change
character), and `PlayLevelControls` in `coney_devtools` turns them into the debug menus' `debug::PlayControls`, with
the Entity inspector, the Animation player and the Debug draw lines, which it hands the mode as a hook
(`PlayLevelMode::setSceneLines()`). Until the game session and the sound output are untied the same way,
`coney_platform` still links `coney_debug`; the release preset follows once they are.
`python/tests/test_debug_tools_split.py` reads the CMake files and the game's sources and fails when a game library
lists a debug source or includes a `debug/` header, or anything but `coney_devtools` links `imgui`.

### Debug information on MSVC {#debug-info}

Every preset keeps the debug information inside each object (`/Z7`,
`CMAKE_MSVC_DEBUG_INFORMATION_FORMAT` `Embedded`, policy CMP0141 on for the dependencies too) instead of MSVC's
default `/Zi`, which has every compile write one shared PDB through `mspdbsrv`. Several builds at once (a few agents
sharing a machine, or `-j` on a busy one) contend for that PDB and fail at random with `fatal error C1090: PDB API
call failed, error code 23`; a rerun clears it, but costs a rebuild. With `/Z7` a compile never touches a shared
file, so that contention cannot happen (the fix rests on this mechanism: how often the error came before was not
measured), and a compiler cache can store the object ([Compiler cache](#compiler-cache), [Caches](#ci-caches)).
Debugging works the same; the linker still writes the executable's PDB. The setting lives once, in the hidden
`embedded-debug-info` preset of `CMakePresets.json`, which the hidden `cacheable` preset inherits, and every preset
inherits `cacheable`, which also turns precompiled headers off, as a cache cannot use them.

### Compiler cache {#compiler-cache}

`coney-tools build` compiles through [ccache](https://ccache.dev), the cache CI uses, with one cache for the whole
machine. Every agent's worktree is a fresh checkout of nearly the same sources, so without it each one compiled the
whole engine, its thousand test files and the dependencies again; with it, the first build of a fresh worktree takes
the objects another worktree already compiled. Measured on 2026-10-20 (Windows, MSVC 19.44, 16 cores, `dev` at
`-j 3`, no other agent building; the first two rows ran at the same time, each the other's only load; each row a
fresh build folder, its logs kept with the track's scratch files):

| `dev` build of a fresh folder, same commit | Build step | Cache |
| --- | --- | --- |
| Cache disabled (`--env CCACHE_DISABLE=1`), worktree A | 10.9 min | none |
| Empty cache, worktree B, at the same time | 12.8 min | 0 hits, 1,511 misses |
| Worktree A again, after it | 2.1 min | 1,492 hits, 19 misses (98.7 %) |
| Worktree B, `--target coney` in `build/play` | 47 s | 978 hits, 0 misses |

The first build fills the cache at a small cost over none; every later fresh folder of nearly the same sources takes
it back. One `dev` build's objects are 0.6 GB in the cache, so the 25 GB cap keeps many presets and branches. What
was left of the cached build was mostly CMake's C++ module scanning (733 of its 935 step-seconds, summed from
`.ninja_log`), which ccache does not cache; it is off now ([Module scanning](#module-scanning)).

- **Install it.** Windows: the bootstrap launchers fetch it by default, and `coney-tools workspace init --fetch
  ccache` does the same: ccache 4.14.1 (the release CI's Windows job uses, checked by SHA-256) in
  `tools/ccache-4.14.1`. Linux: `apt-get install ccache`; macOS: `brew install
  ccache`. `workspace init` lists it as an optional prerequisite. Without it builds work as before, and the summary
  says `compiler cache: off (ccache not found ...)`.
- **Settings** ([coney.local.toml](workspace.md#local-config)): `ccache_dir`, the cache folder (default
  `cache/ccache` in the workspace, beside `scratch/`); `ccache_size_gb`, its cap (default 25: ccache drops the least
  recently used objects beyond it, so the drive keeps its 30 GB floor; `0` turns the cache off); `ccache_program`,
  the executable (default the fetched copy, else `ccache` on `PATH`). The cap is also written into the folder's
  `ccache.conf`, so a `ccache -s` or a build started by hand does not trim the cache to ccache's default 5 GB.
- **How it is switched on.** Only through the run's environment, never the presets: `CMAKE_C_COMPILER_LAUNCHER` and
  `CMAKE_CXX_COMPILER_LAUNCHER` name ccache, and CMake reads them only when it first configures a folder. A folder
  configured before keeps compiling without the cache (so a build running when the cache was turned on is never
  changed under it), and its summary says `compiler cache: not used (... delete the folder to build with it)`. A
  launcher you set yourself is kept.
- **Sharing between worktrees.** `CCACHE_BASEDIR` is the checkout being built, so ccache rewrites the checkout's
  absolute paths in a command line relative to the build folder, and the same source in two worktrees is one command
  line and one cache entry; `CCACHE_NOHASHDIR` keeps the build folder's own path out of the hash. ccache does not
  rewrite a path inside a compile definition, so **no target may get a definition holding a checkout path**: the
  test target's `CONEY_ASSETS_DIR` and `CONEY_TEST_SUPPORT_DIR` made every test object miss in a second worktree.
  They now reach the tests through one file, `tests/support/test_paths.cpp`
  (`coney::test::assetsDir()`, `testSupportDir()`), and a Python test refuses such a definition on a target.
- **Precompiled headers are off in every preset.** ccache cannot use them, so SDL3's made its sources uncacheable on
  every build (ccache counts them as `could_not_use_precompiled_header`). So `dev` and `release` inherit the hidden
  `cacheable` preset as `ci` and `asan` do.
- **What a hit carries.** A hit is the object another worktree compiled, with that worktree's absolute paths in it:
  in its debug information (a debugger may open the source file in the other worktree) and in what `__FILE__` expanded
  to (`CONEY_ASSERT` messages may name the other worktree). ccache's manual names `__FILE__` among the things such
  sharing gets wrong, so no code may use `__FILE__` to find files: the tests use `coney::test::testSupportDir()`.
- **ccache's path in a folder.** CMake writes the launcher's absolute path into the folder's `CMakeCache.txt` at the
  first configure. A ccache moved or replaced by another version in another folder breaks the folders configured
  with the old one (the compile cannot start the launcher): keep the old `tools/ccache-<version>` until they are
  deleted, or delete those folders.
- **Version.** ccache 4.10 or newer gives a run its own hits (`--print-log-stats`); with an older one the build still
  uses the cache and the summary says `hits unavailable`.
- **The report.** Each run gives ccache a stats log of its own (`CCACHE_STATSLOG`, beside the run's log), so the
  summary's `compiler cache: H hits, M misses (P %), U uncacheable` is that build's alone, although other builds use
  the cache at the same time.
- **By hand.** A person building without the tool gets the same with `cmake --preset dev
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache` on a new folder.

### Module scanning {#module-scanning}

The top `CMakeLists.txt` turns CMake's C++ module scanning off (`CMAKE_CXX_SCAN_FOR_MODULES OFF`), for Coney's
targets and the dependencies alike. CMake 3.28 scans every C++20-and-later source for `import` before compiling it: a
scan step per source and a collation step per target that every compile of the target waits for. Nothing here uses
modules, and the compiler cache cannot cache the scans. Measured on 2026-10-20 (Windows, `dev` at `-j 3`, a fresh
folder each, every compile from the cache, no other agent building):

| Fresh `dev` folder | Steps | Step-seconds (summed `.ninja_log`) | Build step |
| --- | --- | --- | --- |
| Scanning on | 3,444 | 935, of which the scans 733 | 2.1 min |
| Scanning off | 1,514 | 119 | 38 s |

A source that ever `import`s a module turns it back on for its target (`CXX_SCAN_FOR_MODULES ON`); a Python test keeps
the setting in place.

## Sanitizers

The `asan` preset builds with AddressSanitizer and UndefinedBehaviorSanitizer and runs the tests under them. Any
report fails the test that caused it.

```sh
uv run --project python coney-tools build --agent <your id> --preset asan --test
```

(Underneath: `cmake --preset asan`, `cmake --build --preset asan`, `ctest --preset asan`.)

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

Pull requests are tidied locally, before they are pushed: CI's clang-tidy runs only nightly on `main` and by hand,
and no pull request waits on it (a maintainer decision, 2026-10-21: a full pass took 50 to 100 minutes on a GitHub
runner, most of it spent walking the standard library's and the dependencies' headers). The nightly and manual runs
check Coney's own sources (`src/` and `tests/`, never the fetched dependencies) with clang-tidy 19, using the
`compile_commands.json` that every preset writes. To run the same full pass locally on Linux, after a `ci` build with
Clang 19:

```sh
run-clang-tidy-19 -p build/ci -quiet '^(?!.*_deps).*/(src|tests)/'
```

A pull request's pass checks only the files it changes: each changed `.cpp`, plus every `.cpp` that includes a
changed header, directly or through other headers (`.github/scripts/tidy-files.sh <base>` prints the list for a
change, or `ALL` for a change to the build setup: `CMakeLists.txt`, `cmake/`, `CMakePresets.json`, `.clang-tidy`, the
build workflow). The nightly run checks everything, so a header included some unusual way, or a build-setup change
that breaks a file it did not touch, is still caught within a day; whoever sees it red fixes it forward.

`coney-tools check changed` runs that pass for you, and it is the pull request's only clang-tidy gate: the same file
selection, clang-tidy from
the Visual Studio LLVM folder (macOS: Homebrew's `llvm@19`) over the `build/dev` compile database, and the GCC and
Clang warnings that MSVC does not report ([the local gate](testing.md#local-gate), [its options](coney-tools.md#check)).
On Linux, where `dev` builds with GCC, configure a Clang database for it once, with the build tool (a hook refuses a
raw `cmake`): `coney-tools build --agent <id> --configure-only --preset dev --build-dir build/tidy --env CC=clang-19
--env CXX=clang++-19`, then pass `--build-dir build/tidy` to `check changed`.

### The tidy slots and the result cache {#clang-tidy-cache}

clang-tidy costs about a minute and a gigabyte of memory per file, and a header change reaches 100 to 280 of the 948
`.cpp` files, so on a machine shared by many agents the gate is held to the same rules as a build:

- **Tidy slots.** `check changed` runs its clang-tidy processes only while it holds a slot of the machine's tidy lane,
  a lane of the [build slots](coney-tools.md#build) with its own queue (`build status` lists it). `tidy_slots` of
  `coney.local.toml` sets how many gates tidy at once (default 1) and `tidy_jobs` how many clang-tidy processes each
  runs (default 0: a build slot's share of the cores, `-j` of a build); together they bound clang-tidy's share of the
  machine. Before the lane, every gate started its own processes: on 2026-10-21 eight gates ran 22 of them (about
  20 GB) beside five full builds on 16 threads, and the builds queued for over an hour. A run of at most 6 files (a
  rerun after a fix, a change that reaches a few sources) takes the fast lane's slot instead, so it never waits behind a
  gate of 250 files. Once a gate has its slot it asks the cache again, as another gate may have tidied the same files
  while it waited. Ctrl+C or a kill lets the slot go at once: the files not started yet are dropped.
- **Result cache.** A file clang-tidy found clean is remembered in a cache every worktree shares (`tidy_cache_dir`,
  default `cache/clang-tidy` in the workspace, beside the [compiler cache](#compiler-cache)), and the next gate that
  meets the same file in the same state skips it. The key is everything that decides the result: clang-tidy's version,
  the checks and warning flags, `.clang-tidy`, `cmake/deps.cmake` (the pinned third-party versions), the file's compile
  command with the checkout's path taken out, the size and date of the compiler it names, the system include
  variables (`INCLUDE`, `CPATH` and the like, which name the Windows SDK and MSVC versions), and the contents of the
  file and of every Coney header it includes, transitively (every `#include` line counts, whatever `#if` it sits
  under). So a rerun after a fix tidies only the files the fix reaches, and a gate whose files are all cached takes no
  slot and finishes in seconds. Each clean file is stored as soon as it finishes, and only if its key is still the
  same: a file edited while the gate waited or ran is tidied again next time. A failing file is never cached. Entries
  unused for 30 days are removed; `--no-cache` ignores the cache for one run.

What the key leaves out, by design: the contents of headers outside the checkout (the standard library, the compiler's
own, which the compiler and include variables stand for) and of the fetched dependencies under `_deps` (pinned by
`deps.cmake`). It does not follow forced includes (`-include`, `/FI`), `-idirafter`, `#include MACRO` or the contents of
`@response` files; Coney's build uses none of them. Delete the cache folder if a toolchain changes in a way none of
these show.

## Python tools

`python/` is the `coney-tools` package (see [Conventions](conventions.md#python)). [uv](https://docs.astral.sh/uv/)
installs the right Python and the locked dependencies into `python/.venv` the first time you run something:

```sh
uv run --project python coney-tools --help
uv run --project python pytest python/tests
uv run --project python ruff check python .claude/hooks
uv run --project python ruff format --check python .claude/hooks
uv run --project python mypy --config-file python/pyproject.toml python/src python/tests
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

clang-format decides the layout of C and C++ code, ruff that of the Python package, markdownlint that of every Markdown
file (a line over 120 characters is refused), and a few hooks catch trailing whitespace, broken YAML or TOML,
merge-conflict markers and files over 512 KB (usually game data added by mistake). All of them run through
[pre-commit](https://pre-commit.com/), configured in `.pre-commit-config.yaml`:

```sh
uvx pre-commit run --all-files
```

Run that before every commit, or install the hooks once with `uvx pre-commit install` so that `git commit` runs them
for you. The hooks fix what they can; stage their changes and commit again. Install them once in each clone; the
worktrees of a clone share them. The markdownlint hook runs the same rules as CI's markdownlint job
(`.markdownlint.yaml`, with the files it skips in `.markdownlint-cli2.yaml`) through Node.js, so Node.js 18 or newer
must be on your `PATH` (pre-commit would otherwise download its own copy on the first run). `coney-tools check
changed` runs it on the files you changed, and `repo audit` (`files.markdown-hook`) fails when the hook is removed from
the configuration. No hook checks a commit title: a pull request is squash-merged, so only its title reaches `main`,
and CI checks that against the commit title rules (`commits.title` in
[.github/conventions.yml](repo:.github/conventions.yml); [CONTRIBUTING.md](repo:CONTRIBUTING.md#commits) lists them).

## Documentation

This site is built with MkDocs from `docs/`. The setup, the build and preview commands (`.venv/Scripts/python -m mkdocs
build --strict` is the one CI runs) and how to write the pages are in [Writing these docs](writing-docs.md).

## What CI runs

Every pull request, and every push to `main` or a `release/` branch, runs these GitHub Actions workflows. The `build`
workflow also runs every night on `main` and on demand: open the repository's Actions tab, choose `build`, press
"Run workflow" and pick a branch that is already on GitHub (or `gh workflow run build.yml --ref <branch>`). Only the
nightly and manual runs run clang-tidy, over every file. Every compiling job, Windows included, goes through ccache
and starts from `main`'s cache, so it recompiles only what changed; each job prints its hit rate at the end and in its summary
([Caches](#ci-caches)).

A newer push cancels the older run of the same workflow on the same ref, queued or running: on a pull request, and
on `main` or a `release/` branch too, so a burst of merges builds only the newest commit instead of queueing one full
build per merge on the shared macOS runners. A commit whose run was cancelled that way is covered by the next run and
by the nightly one; the nightly and hand-started `build` runs are never cancelled.

| Workflow | Jobs |
| --- | --- |
| `build` | `ci` preset build and tests on Windows (MSVC), Linux (GCC 13 and Clang 19) and macOS (Apple Clang); `asan` preset on Linux Clang 19 and macOS; clang-tidy (nightly and manual runs only); pre-commit over every file; the `build-ok` gate |
| `python` | ruff, mypy, pytest and the local gate (`check changed`: its `repo` and `pre-commit` steps) on Windows, Linux and macOS; the repository checks (`repo check`, `repo render --check`, `repo audit --files`, `progress check`, `orig check --base HEAD^1`, the generators' `--check`) on Linux; the `python-ok` gate |
| `docs` | `mkdocs build --strict`, the page standards on a pull request's changed pages ([`docs check`](writing-docs.md#checking)), markdownlint over every Markdown file, actionlint over the workflows |
| `pr` | the pull request title against the commit title rules, that code changes come with docs changes, and the description against the template (pull requests only; editing the title or description reruns it) |

One more workflow runs on issues, not pull requests: `issue-check` runs `coney-tools issue check` on every issue
opened or edited and keeps one comment listing the problems ([Filing an issue](issues.md#filing)); it is not a required
check.

On Windows, do not run WSL builds or clang-tidy locally to stand in for the Linux and macOS jobs: GitHub CI is that
gate, and a failure there is fixed forward on the branch.

### What a pull request runs {#what-a-pull-request-runs}

Runners are shared (about 20 jobs at once, 5 of them on macOS), so a pull request runs only what its change can break.
Two checks are given up on purpose: an engine-only pull request no longer runs the local gate's `pre-commit` step
or the repository checks on Windows or macOS, because they do not depend on the OS (below) and `format` still runs
pre-commit over every file on Linux. A `changes` job at the start of `build` and `python` decides, with
`.github/scripts/ci-scope.sh`. It sorts each changed file into the first of three classes of
[.github/conventions.yml](repo:.github/conventions.yml) whose list matches it (`paths.docs_only`, then
`paths.tools_only`, then `paths.engine`), and the pull request runs what any of its files needs. The lists are those of
the branch the pull request merges into, not its own: the file is itself `docs_only`, so a pull request that edited a
list could otherwise skip the jobs it changes. A change to a list applies to the pull requests after it:

| Change | `build`, `sanitizers` | `python` | `format`, `docs`, `pr` workflows |
| --- | --- | --- | --- |
| Draft pull request (any change) | skipped | skipped | run |
| A `docs_only` file: documentation, the conventions file, the docs site's setup, the `docs`, `pr`, `hygiene` and `issue-status` workflows | skipped | Linux only | run |
| A `tools_only` file: `python/`, `.claude/`, the pre-commit configuration, the issue forms, the `python` workflow and the other tool files | skipped | all three OSes | run |
| An `engine` file: `src/`, `tests/`, `assets/`, `cmake/`, the CMake files, `.clang-*`, the `build` workflow and its ccache action | run | Linux only | run |
| A file in no class (`.github/scripts/`, a new workflow, `.gitattributes`, `LICENSE`, ...), or no changed file | run | all three OSes | run |
| Push to `main` or `release/`, nightly run, manual run | run | all three OSes | run |

A pull request with files of several classes gets the union: documentation and `python/` runs python on every OS and
no engine jobs; `src/` and `python/` runs the engine jobs and python on every OS. Both sides of a rename count, so
moving a file out of `src/` is an engine change. Why each class runs what it runs:

- **The engine jobs read only the engine files.** CMake and the C++ tests read `src/`, `tests/`, `assets/` and
  `cmake/` from the checkout, nothing under `python/`, `.claude/`, `docs/` or `research/` (the paths those name in
  C++ are help text). The tools are read by the python package, its tests and the docs build (`mkdocs_hooks.py` imports
  `coney_tools` from `python/src/`); the docs workflow runs on every pull request, so that is still checked.
- **Python runs on Linux for every ready pull request,** because its repository checks compare `docs/` and
  `research/` with each other (missions, bindings, references, progress), read the conventions and the workflows, and
  read C++ files (the `@orig` tags, the binding table, the debug menus' signature table), and its tests read
  `research/traces/`, `tests/` and `cmake/`. Those reads do not depend on the OS: each normalises CRLF to LF (or reads
  text in universal-newline mode) and compares contents.
- **Python runs on every OS only when the tools changed** (or a file no class names): ruff, mypy, pytest and the local
  gate (`check changed`: its `repo` and `pre-commit` steps) are run on each OS because the tools behave differently
  there (paths, command-line length, programs, line endings). An engine-only change does not change those tools.
- **In the python job, the OS-independent repository checks run once, on Linux:** `repo check`, `repo render --check`,
  `repo audit --files`, `progress check` and the generators' `--check` (`natives`, `refs`, `missions`). `repo check`
  and `repo render --check` are part of `repo audit --files`, which the local gate's `repo` step still runs on every OS.
- **The `format` job and the local gate's `pre-commit` step both run pre-commit,** on purpose: `format` runs every
  hook on every file on Linux, the gate runs `coney-tools check changed` (its own grouping of the changed files and its
  program lookup) on each OS the python job tests. Dropping either would lose a check the other does not make.

The lists, generated from [.github/conventions.yml](repo:.github/conventions.yml) (`coney-tools repo render`; a
trailing `/` is a folder and `*` matches within one folder level). `ci-scope.sh` reads them from the file, so there is
no second copy, and `coney-tools repo audit --files` fails a pull request when the script stops being able to.

`docs_only` (`research/` includes `research/traces/`):

<!-- conventions:begin docs-only-paths -->
- `docs/`: the folder and everything under it
- `research/`: the folder and everything under it
- `overrides/`: the folder and everything under it
- `assets/art/`: the folder and everything under it
- `*.md`: matching files at the top level only
- `.github/*.md`: matching files directly in `.github/`
- `.github/conventions.yml`: that file
- `.github/workflows/docs.yml`: that file
- `.github/workflows/pr.yml`: that file
- `.github/workflows/hygiene.yml`: that file
- `.github/workflows/issue-status.yml`: that file
- `mkdocs.yml`: that file
- `mkdocs_hooks.py`: that file
- `requirements-docs.txt`: that file
- `.markdownlint*`: matching files at the top level only
<!-- conventions:end -->

`tools_only`:

<!-- conventions:begin tools-only-paths -->
- `python/`: the folder and everything under it
- `.claude/`: the folder and everything under it
- `tools/`: the folder and everything under it
- `bootstrap.ps1`: that file
- `bootstrap.sh`: that file
- `coney.local.example.toml`: that file
- `.mcp.json`: that file
- `.pre-commit-config.yaml`: that file
- `.github/ISSUE_TEMPLATE/`: the folder and everything under it
- `.github/workflows/python.yml`: that file
<!-- conventions:end -->

`engine`:

<!-- conventions:begin engine-paths -->
- `src/`: the folder and everything under it
- `tests/`: the folder and everything under it
- `assets/`: the folder and everything under it
- `cmake/`: the folder and everything under it
- `CMakeLists.txt`: that file
- `CMakePresets.json`: that file
- `.clang-format`: that file
- `.clang-tidy`: that file
- `.github/actions/`: the folder and everything under it
- `.github/workflows/build.yml`: that file
<!-- conventions:end -->

Marking a draft "Ready for review" starts the full run, so open work as a draft while it is unfinished and mark it
ready when it should be checked. There is no merge queue.

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

### Caches {#ci-caches}

The `build` workflow keeps two kinds of GitHub Actions cache, both through the actions in `.github/actions/`. The
repository has 10 GB of them; when that is full GitHub drops the least recently used entries.

- **The compiler cache** (`ccache`, then `ccache-save` at the end of the job): one entry per job, of the `ccache`
  folder. Pull requests only restore it; pushes to `main` and `release/` branches, the nightly run and manual runs
  save it. A pull request's entry would be scoped to its own `refs/pull/N/merge` ref, which no other pull request can
  read, so it could only push `main`'s useful entries out: on 2026-10-13 the cache held 9.6 of its 10 GB in 28
  entries, one per job and commit. A pull request restores the newest entry of its job from `main`.
- **The dependency sources** (`deps`, then `deps-save` after the configure step): SDL3, librw, Catch2, Dear ImGui, ENet
  and FFmpeg as fetched, one entry per OS and version of `cmake/deps.cmake`, shared by every job and preset on that OS.
  `CONEY_DEPS_SOURCES` makes `cmake/deps.cmake` fetch into `build/_deps-sources/<name>` and use a complete copy found
  there without downloading. The build trees under `build/<preset>/_deps/` are not kept: they belonged to one
  compiler and preset, a fresh job rebuilds them anyway (Ninja has no record of them), and they made eight entries of
  about 400 MB. The same rule applies: only runs outside pull requests save.
- **Rolling entries.** Each saved compiler-cache entry is named after the run, and the `cache-prune` job, which runs
  after the build jobs outside pull requests, deletes every older entry of the same job (and older dependency entries
  of the same OS) on that branch, so `main` keeps one per job. Before saving, a job drops what it did not use
  (`ccache --evict-older-than` the job's own duration), so the entry is the size of one build, not of the cap.
- **Sharing.** The nightly clang-tidy job builds with the same compiler, preset and flags as `build (linux-clang-19)`,
  so it restores that job's entry and leaves the saving to it. The `asan` jobs differ in flags and keep their own.
- **What a cache can store.** MSVC's default `/Zi` writes every object's debug information into one shared PDB,
  which ccache refuses; every preset uses `/Z7` instead ([Debug information on MSVC](#debug-info)), so that is
  settled for the cache too. SDL3's precompiled header made its 265 sources uncacheable everywhere, so every preset
  inherits the hidden `cacheable` preset, which turns `CMAKE_DISABLE_PRECOMPILE_HEADERS` on.
- **Sizes.** Each job's cap while it runs is 1 GB (`max-size` of the `ccache` action): the restored entry plus what
  the job compiles anew must fit, or ccache evicts restored objects before the build reaches them and those miss too.
  At the earlier 700 MB, about one build's objects, the Windows, GCC, macOS and macOS sanitizer jobs of a pull
  request's run on 2026-10-21 all ended at their cap, with 72.5, 79.8, 80.0 and 34.3 % hits, while the Clang jobs,
  under it, had 93.1 %. The saved entry is trimmed to what the job used (below), so it stays about one build's size,
  0.3 to 0.7 GB. Six jobs save (four `build`, two `sanitizers`): at most 6 × 1 GB = 6 GB even untrimmed, plus three
  dependency entries (one per OS, about 0.1 to 0.2 GB each) and the `setup-uv` caches (about 50 MB per OS), under
  7 GB of the 10 GB. While a `main` run is between its saves and `cache-prune`, the old and new entries of a job both
  exist for a few minutes. When a job's summary shows the cache at its cap and a rebuild of an unchanged commit hits
  little, raise that cap and redo this sum.
- **Hit rates.** Each job's summary shows `ccache --show-stats` for that job alone (the counters are zeroed after the
  restore): cacheable calls, hits, misses, uncacheable calls and the cache size.
