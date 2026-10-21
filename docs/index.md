---
template: home.html
# The hero of the home page (overrides/home.html): see "The home page" in guides/writing-docs.md.
hero:
  heading: Coney
  tagline: >-
    An open-source reimplementation of The Warriors (Rockstar Toronto, PS2, 2005) that runs natively on Windows,
    Linux and macOS.
  image: assets/art/hero # without the extension: hero.webp, with hero.jpg as the fallback
  position: 50% 30% # which part of the art stays in view when the screen crops it
  credit: "Artwork: David Jarvis, 1979 film poster"
  actions:
    - { label: Get started, page: guides/building.md, primary: true }
    - { label: Roadmap, page: roadmap.md }
    - { label: GitHub, repo: true }
---

# Coney

## What Coney is

Coney aims to run the whole of *The Warriors* natively, from a disc you own. It is a functional reimplementation: new
C++ code that behaves like the original. Contributors study the original game,
write down what they learn on the [research pages](research/overview.md) and implement it in Coney's own code,
and the [progress page](progress/index.md) measures how far that has come.

## Bring your own disc

!!! warning "No game data here"
    This project never contains game assets, executables, or BIOS files. You need your own legally obtained copy of
    the game.

Coney reads the game's files from your own disc, and the research is checked against the North American (NTSC-U)
release. Building and testing Coney needs no game files, so you can start before you have found a disc; the
[building guide](guides/building.md) walks through it.

## Platforms

<div class="grid cards" markdown>

- :fontawesome-brands-windows: **Windows**

    Built with MSVC 19.38 or newer (Visual Studio 2022 17.8 or the Build Tools).

- :fontawesome-brands-linux: **Linux**

    Built with GCC 13 or newer, or Clang 19 or newer.

- :fontawesome-brands-apple: **macOS**

    Built with Xcode 16 or newer.

</div>

Each needs CMake 3.28 or newer and a graphics driver with OpenGL 3.3. The dependencies are fetched at the first
build.

## Explore the project

<div class="grid cards" markdown>

- :material-map-marker-path: **[Roadmap](roadmap.md)**

    The milestones from an empty window to the whole game, and where we are

- :material-history: **[Changelog](changelog.md)**

    Every change to Coney, newest first, written from the project's history

- :material-chart-line: **[Progress](progress/index.md)**

    How much of the game is reimplemented and researched, overall and per subsystem

- :material-flag-checkered: **[Missions](missions/index.md)**

    Which story missions, flashbacks and bonus stages Coney can play, checkpoint by checkpoint

- :material-microscope: **[Research](research/overview.md)**

    How the original game works: disc, executable, file formats, engine subsystems

- :material-book-open-variant: **[Game references](references/index.md)**

    Lists of the game's characters, gangs, objects, cars, levels, scenes and more

- :material-hammer-wrench: **[Guides](guides/building.md)**

    How to build and play Coney, use the tools and the research workflow

- :material-source-pull: **[Contribute](repo:CONTRIBUTING.md#your-first-hour)**

    From a clone to a first pull request in an hour: what to install, how to build, run and test, and the rules

</div>
