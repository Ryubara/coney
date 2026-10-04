# Building and testing

This page takes you from a fresh clone to a built, tested Coney on Windows, Linux or macOS, and covers the Python
tools, the documentation and the checks CI runs on every pull request. The rules the code itself follows are in
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

The C++ dependencies (SDL3, librw, Catch2) are not installed by hand: CMake downloads them on the first configure,
at the exact commits pinned in `cmake/deps.cmake`. That first configure needs network access and takes a few
minutes; later builds reuse the copies under `build/<preset>/_deps/`.

librw is built for its OpenGL 3 platform with SDL3 creating the window and the context, so the build also needs the
OpenGL headers and libraries: part of the system on Windows and macOS, `libgl1-mesa-dev` on Linux (in the package
list below). CMake hands librw the SDL3 it fetched rather than one installed on the machine; how, and why librw's own
asserts are off, is explained in `cmake/deps.cmake`.

Clang 17 and 18 are supported by the language rules but not on Linux with libstdc++: libstdc++ only declares
`std::expected` when the compiler reports full C++20 concepts support, which Clang first does in version 19. Use
Clang 19 or newer there.

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

### Linux

On Ubuntu 24.04 (other distributions have the same packages under similar names):

```sh
sudo apt-get install cmake ninja-build g++-13 \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
  libwayland-dev libxkbcommon-dev libegl1-mesa-dev libgl1-mesa-dev libdbus-1-dev libudev-dev
```

The second and third lines are what SDL3 needs to build its X11 and Wayland video drivers; `libgl1-mesa-dev` is
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

`ctest` runs the Catch2 unit tests (`coney_tests`) and six smoke tests of the `coney` executable itself: it starts
and stops headless, prints its help, refuses a bad argument, refuses `--load` or `--view-txd` without `--disc` and
refuses a disc that does not exist. The unit tests build their disc images, archives and RenderWare texture
dictionaries byte by byte; none needs the game or a GPU (the texture tests run librw on its NULL device). One test
checks every texture dictionary on your own disc; it runs only when the environment variable `CONEY_DISC` names the
disc, is reported as skipped otherwise, and prints counts only:

```sh
CONEY_DISC=/path/to/warriors.iso build/dev/tests/coney_tests "[disc]"
```

## Run Coney {#run-coney}

The executable is `build/<preset>/src/platform/coney` (`coney.exe` on Windows). Run with no arguments, it opens a
window and runs until you close it (or press Escape). Underneath, the game-mode stack runs on a fixed 1/30 s step
with an idle mode at its bottom, which clears the window to a dark slate and presents it every frame.

```text
coney [--disc PATH] [--load ENTRY]... [--view-txd ENTRY] [--frames N] [--screenshot PATH] [--headless] [--help]
```

Coney draws with librw's OpenGL 3 renderer (an OpenGL 3.3 core context through SDL3; librw falls back to 2.1 or
OpenGL ES). `--frames N` stops after N frames, which is how tests and scripts run it. `--headless` runs with no window
and librw's NULL renderer, so it needs neither a display nor a GPU; this is how CI runs it:

```sh
build/dev/src/platform/coney --headless --frames 3
```

Without `--headless`, a machine with no display or no OpenGL fails at start-up with SDL's reason and exit code 1.

`--screenshot PATH` saves the last frame (the one `--frames N` stops at) as a PNG and prints how many of its pixels
differ from the background and a hash of the frame, so a script can check that something was drawn without keeping
the image. Keep screenshots of game data out of the repository (`../../scratch/` is the place).

### Loading entries from your disc

`--disc PATH` names your own copy of the game: a mounted disc (`H:\` on Windows, `/mnt/disc` on Linux), a folder
holding `WARRIORS.DIR` and `WARRIORS.WAD`, or an ISO 9660 image of the disc (the same forms `coney-tools wad`
takes, see [The coney-tools command line](coney-tools.md#naming-the-disc)). Coney reads `WARRIORS.DIR`, checks it
against `WARRIORS.WAD`, and prints how many entries it lists.

`--load ENTRY` loads one WAD entry through the reimplemented chunk system and prints a summary of it. `ENTRY` is a
file name such as `level1.lev` (any letter case) or a name hash written `0x` and up to 8 hex digits, such as
`0x7e23a6f2`, for entries whose name is not known. Give `--load` as often as you like; with any `--load`, Coney
opens no window, loads one entry per frame of its fixed timestep and exits when all are done.

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --load level1.lev --load global.pak
```

```text
/path/to/warriors.iso: WARRIORS.DIR lists 10701 entries
level1.lev: entry 6864, hash 0x393f70e2, 46992 bytes
  flat container: 18 chunks, 46688 bytes of chunk data (header says 46688)
    type 0x03 Collision Mesh: 1 chunk, 160 bytes
    ...
  left on the stacks: 18 chunks, 0 objects; 0 trailing bytes
```

The summary gives the container's shape (flat, or grouped for a pack of resources), the number of chunks and their
bytes per [chunk type](../research/chunk-system.md#chunk-type-table), and what the chunk handlers left on the
loader's stacks. It prints counts and sizes only, never the data. An entry that is not a chunk container (Lua
bytecode, text, sound banks) is reported as such. Coney exits with 0 when every entry loaded, 1 when any could not
be found or parsed, and 2 for a bad command line. With `--load`, the two RenderWare texture dictionary chunk types
(`0x0B` and `0x2A`) are read by their stream handlers through librw, on its NULL renderer.

### Viewing texture dictionaries

`--view-txd ENTRY` opens the window and shows every texture of a WAD entry's texture dictionaries, laid out in a grid
that fills the window, each scaled to its cell without changing its shape and drawn with its transparency over a
grey background. `ENTRY` is named as for `--load`. The entry may be a chunk container holding texture dictionary
chunks (a standalone resource, a pack or a level), or a world sector atomics file, whose stream starts with a
(usually empty) dictionary. Coney prints how many dictionaries and textures it found, with each texture's size and
format, then runs until the window closes or `--frames N` is reached.

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --view-txd 863681355 --frames 3 --screenshot ../../scratch/legal.png
```

That entry is the legal screen, the first image the game shows ([Graphics](../research/graphics.md#first-screen)).
The textures are the PS2's palettised formats, converted by librw: palettes are expanded and the PS2's alpha range
(128 is opaque) is scaled to 0-255.

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
title against the commit rules in [CONTRIBUTING.md](repo:CONTRIBUTING.md#commits).
`coney-tools wad` reads the archive on your own disc (`info`, `list`, `extract`, `names`); [The coney-tools command
line](coney-tools.md) shows how to run each command.

## Formatting and pre-commit

clang-format decides the layout of C and C++ code, ruff that of the Python package, and a few hooks catch trailing
whitespace, broken YAML or TOML, merge-conflict markers and files over 512 KB (usually game data added by mistake).
All of them run through [pre-commit](https://pre-commit.com/), configured in `.pre-commit-config.yaml`:

```sh
uvx pre-commit run --all-files
```

Run that before every commit, or install the hooks once with `uvx pre-commit install` so that `git commit` runs them
for you. The hooks fix what they can; stage their changes and commit again.

## Documentation

This site is built with MkDocs from `docs/`. In a virtual environment at the repository root:

```sh
py -m venv .venv
.venv/Scripts/pip install -r requirements-docs.txt
.venv/Scripts/python -m mkdocs build --strict
```

On Linux and macOS use `python3 -m venv .venv` and `.venv/bin/` in place of `.venv/Scripts/`.
`mkdocs serve --dev-addr 127.0.0.1:8000` previews the site and reloads it as you edit. `--strict` turns broken links
and pages missing from the navigation into errors, as CI does. How to write the pages is in
[Writing these docs](writing-docs.md).

## What CI runs

Every pull request, and every push to `main` or a `release/` branch, runs these GitHub Actions workflows:

| Workflow | Jobs |
| --- | --- |
| `build` | `ci` preset build and tests on Windows (MSVC), Linux (GCC 13 and Clang 19) and macOS (Apple Clang); `asan` preset on Linux Clang 19 and macOS; clang-tidy; pre-commit over every file |
| `python` | ruff, mypy, pytest and `coney-tools repo check` on Windows, Linux and macOS |
| `docs` | `mkdocs build --strict`, markdownlint over every Markdown file, actionlint over the workflows |
| `pr` | the pull request title against the commit title rules (pull requests only) |

If a job fails, the commands above reproduce it locally. The workflow files under `.github/workflows/` are short and
list every step.
