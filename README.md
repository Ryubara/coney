# Coney

An open-source reimplementation of *The Warriors* for Windows, Linux and macOS, built mainly on reverse engineering
the 2005 PS2 version, the reference for how the game plays, with the Xbox port as a second source for rendering and
assets.

Status: in development, not yet a complete game. With your disc Coney plays the start-up movies and the front end and
runs levels as far as the [missions](docs/missions/index.md) page records.

> [!IMPORTANT]
> Coney contains no game data. You need your own copy of the game to use it. See [LEGAL.md](LEGAL.md).

## What Coney is

Coney aims to run the whole of *The Warriors* natively on Windows, Linux and macOS, from a disc you own. It is a
functional reimplementation: new C++ code that behaves like the original.

### How Coney is made

Projects that bring old games to new platforms work in one of three ways:

- A **decompilation** rewrites the original's machine code as source that compiles back to the same binary. It is
  exact, but the result stays close to the original console and its design.
- A **static recompilation** translates the binary itself into code for another machine. It runs soonest, but the
  translated code is hard to read or change, and the PS2's graphics hardware makes it especially hard there.
- A **reimplementation**, which is what Coney is, writes new code that behaves the same.

Coney's contributors read the original in Ghidra and watch it run in PCSX2, so Coney follows the same rules, timings
and maths. The code is our own: written in Coney's own structure and names, on our own engine, with no part of the
original's code, data or binary inside it. It is not a clean-room reimplementation: the people writing the code also
read the original.

What this gives us:

- **A native game, not an emulated one**, on Windows, Linux and macOS.
- **Room to grow:** co-op over a network, higher frame rates, widescreen and modern lighting and shadows need code
  that was designed for them, which a translated or matched binary is not
  ([graphics and performance](docs/roadmap.md#graphics-and-performance)).
- **Room for other assets and for mods:** the game's assets come from your own copy: the PS2 disc today, and the
  Xbox version as an optional extra ([in progress](docs/roadmap.md#xbox-assets-optional)). Assets people make
  themselves can be loaded too ([custom content](docs/roadmap.md#custom-content)), and the engine has room for mod
  tools ([script mods](docs/roadmap.md#script-mods)) and more. Custom assets and mod tools are planned: the game comes first.
- **Code anyone can read and change**, under our own licence.

What it costs:

- **Faithfulness has to be checked, not assumed.** Each ported function cites the original's address, every change
  to one is compared side by side with the original, and tests check Coney against the running game.
- **It takes longer:** every system has to be understood, not just matched or translated.

### The PS2 and Xbox versions

Coney reimplements *The Warriors* as a whole, not one console's version of it
([research](docs/research/xbox-port.md)):

- **One codebase.** Rockstar Toronto built the PS2 and Xbox versions from one codebase. Their final builds were
  compiled on the same day, from changelist numbers five apart, and both were released in North America on
  17 October 2005.
- **Built around the PS2.** The engine and its data were made for the PS2: the Xbox version still reads the PS2's
  data formats and names its files after the PS2's processor and sound format. So the PS2 version is Coney's main
  source for how the game plays. It is also the one we can check at runtime, in PCSX2's debugger, which Coney's tools
  are built around; the Xbox executable serves as a reading aid beside it.
- **What the Xbox port adds:** textures twice the width and height, HD and widescreen movies, and its own Direct3D 8
  renderer, whose lighting and shadows are written up in
  [Xbox lighting and shadows](docs/research/xbox-lighting.md). **What it does worse:** its music is at a lower sample
  rate.

Coney takes the Xbox version's gains as faithful and builds on them: they, and what neither console could do (higher
frame rates, anti-aliasing, modern lighting), are [graphics and performance](docs/roadmap.md#graphics-and-performance)
options. Gameplay stays the PS2's, and the PS2 look is always there to choose.

The rules we work under are in [LEGAL.md](LEGAL.md).

## Progress

How much of the original game's own code Coney reimplements, how much of it research has placed, and where the
[roadmap](docs/roadmap.md) stands. Middleware (RenderWare, the PS2 SDK, Bink, Lua) is replaced, not reimplemented,
so it is not counted.

[![Reimplemented](https://img.shields.io/endpoint?url=https%3A%2F%2Fryubara.github.io%2Fconey%2Fprogress%2Fbadges%2Freimplemented.json)](https://ryubara.github.io/coney/progress/)
[![Researched](https://img.shields.io/endpoint?url=https%3A%2F%2Fryubara.github.io%2Fconey%2Fprogress%2Fbadges%2Fresearched.json)](https://ryubara.github.io/coney/progress/)
[![Understood](https://img.shields.io/endpoint?url=https%3A%2F%2Fryubara.github.io%2Fconey%2Fprogress%2Fbadges%2Funderstood.json)](https://ryubara.github.io/coney/progress/)
[![Milestones](https://img.shields.io/endpoint?url=https%3A%2F%2Fryubara.github.io%2Fconey%2Fprogress%2Fbadges%2Fmilestones.json)](https://ryubara.github.io/coney/progress/)

<p>
  <a href="https://ryubara.github.io/coney/">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="https://ryubara.github.io/coney/progress/readme-dark.png">
      <img
        alt="Coney's progress: a square per function, in three colours, and the count of each in a legend"
        src="https://ryubara.github.io/coney/progress/readme-light.png"
        width="880">
    </picture>
  </a>
</p>

Each square is one function of the original's own code, in address order: green is reimplemented, the softer grey
understood (named and cited), the stronger grey not yet started. They are the same squares, in the same order and
colours, as on the [docs home page](https://ryubara.github.io/coney/) (there in rows of 240, here 170), where pointing
at a square names its function.

The percentages differ on purpose: the badges weigh each function by the size of its code, while the picture counts
every function as one square, so its share is lower where the reimplemented functions are the larger ones; and
"Understood" in its legend counts only the functions understood but not yet reimplemented.

The figures and the picture are measured from `main` each time the [documentation](#documentation) is built, so they
are never out of date. Per subsystem, the research coverage, the middleware and how it is all measured:
[Progress](docs/progress/index.md).

## Building and running

Coney is built from source. These steps give you a development build, the one to use while the project is in
progress; they do not make a release package. The build needs no game files, so you can build and test Coney before
you own or have found a disc.

### Requirements

| | Windows | Linux | macOS |
| --- | --- | --- | --- |
| Compiler | MSVC 19.38+ (Visual Studio 2022 17.8 or Build Tools 2022, **Desktop development with C++**) | GCC 13+ or Clang 19+ | Xcode 16+ |
| CMake 3.28+ and Ninja | bundled with Visual Studio | distribution packages | `brew install cmake ninja` |
| Git | yes | yes | yes |

- Network access on the first build: CMake downloads the dependencies (SDL3, librw, Dear ImGui, Catch2 and FFmpeg) at
  the versions pinned in `cmake/deps.cmake`. You install none of them yourself.
- OpenGL 3.3 or newer, to open a window. A headless run needs neither a display nor a GPU.
- On Linux, the packages SDL3 and librw build against; on Ubuntu 24.04:

    ```sh
    sudo apt-get install cmake ninja-build g++-13 \
      libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
      libwayland-dev libxkbcommon-dev libegl1-mesa-dev libgl1-mesa-dev libdbus-1-dev libudev-dev \
      libasound2-dev libpulse-dev libpipewire-0.3-dev
    ```

    For Clang 19 install `clang-19 clang-tools-19` instead of `g++-13`, and run
    `export CC=clang-19 CXX=clang++-19` before the first build.

### Clone

```sh
git clone https://github.com/Ryubara/coney.git
cd coney
```

To set up the whole workspace around a clone (the sibling folders for your disc and tools, `coney.local.toml`, a
check of everything below and, if you ask, the research tools), clone into `<workspace>/repos/coney` and run
`./bootstrap.sh` there, or `powershell -ExecutionPolicy Bypass -File bootstrap.ps1` on Windows. With no clone yet,
download `bootstrap.sh` alone and run `sh bootstrap.sh --repo-url https://github.com/Ryubara/coney.git` in an empty
folder. See [Local workspace](docs/guides/workspace.md#bootstrap).

### Build and test

**On Windows**, the compiler, CMake and Ninja are only on the `PATH` in a shell that has the MSVC environment loaded.
Open a command prompt that has it in one of two ways:

- Start menu: search for **x64 Native Tools Command Prompt for VS 2022** and open it. If Windows answers "Missing
  Shortcut: Windows is searching for %comspec%", use the second way.
- From any command prompt, run the `vcvars64.bat` script that Visual Studio installed. It is in the `VC\Auxiliary\Build`
  folder of your Visual Studio folder. For the Build Tools it is:

    ```bat
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    ```

The environment lasts only for that window, so repeat this in every new prompt. Then, on every platform, from the
folder you cloned into:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The first line configures the build and downloads the dependencies, which takes a few minutes; the second compiles
Coney into `build/dev/`; the third runs the tests, none of which needs the game.

### Run

The executable is `build/dev/src/platform/coney` (`coney.exe` on Windows). Without arguments it opens a window and runs
until you close it or press Escape. To run with your own copy of the game, give it the disc: a mounted disc, a folder
holding `WARRIORS.DIR` and `WARRIORS.WAD`, or an ISO image.

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso
```

`coney --help` lists every option, and [Running Coney](docs/guides/running.md) says what they do.
[Building and testing](docs/guides/building.md) covers the other presets (`playtest`, `release`, `ci`, `asan`), switching
compilers and the Python tools.

## Documentation

The documentation is published at **<https://ryubara.github.io/coney/>**, rebuilt from `main` on every push. It
is a MkDocs site built from `docs/`. To build or preview it locally, see
[Writing these docs](docs/guides/writing-docs.md).

- [`docs/research/`](docs/research/) holds what is known about the game: formats, engine behaviour, addresses.
- [`docs/guides/`](docs/guides/) holds how-to guides for working on the project.

## Contributing

> **Heads-up:** a new contributor workflow is being built and will be ready soon. Please hold off on starting new work
> until it lands; the guides below are about to change.

Contributions are welcome, from people and from AI agents, under the same rules. Read
[CONTRIBUTING.md](CONTRIBUTING.md) before you start; its [first hour](CONTRIBUTING.md#your-first-hour) takes you from a
clone to a pull request, and it covers commit format and pull requests.
Everyone taking part follows the [Code of Conduct](CODE_OF_CONDUCT.md). [AGENTS.md](AGENTS.md) holds the
instructions for AI agents. Report security problems as described in [SECURITY.md](SECURITY.md).

## Licence

Code is licensed GPL-3.0-or-later (see [LICENSE](LICENSE)). Documentation in `docs/` is licensed CC-BY-SA-4.0 (see
[docs/LICENSE](docs/LICENSE)). "The Warriors", Rockstar Games and Take-Two Interactive are trademarks of their
respective owners; Coney is not affiliated with or endorsed by them.
