# Roadmap

How Coney gets from an empty window to the whole game, as a sequence of milestones. Each milestone ends in
something you can run or check, and each builds on the ones before it. Research for a milestone usually starts
before the previous one is finished, because the implementer can only begin once the research page exists.

This page is kept current: a milestone's status changes in the same commit as the work that changes it. The
[Progress](progress/index.md) page and the README read their milestone table from here.

| Milestone | Status |
| --- | --- |
| [Foundations](#foundations) | done |
| [Read the disc](#read-the-disc) | done |
| [Boot the engine](#boot-the-engine) | done |
| [First pixels](#first-pixels) | not started |
| [Scripts](#scripts) | not started |
| [Characters](#characters) | not started |
| [Gameplay](#gameplay) | not started |
| [Sound and video](#sound-and-video) | not started |
| [Enhancements](#enhancements) | not started |
| [Xbox assets (optional)](#xbox-assets-optional) | not started |
| [The whole game](#the-whole-game) | not started |

## Foundations

The build on Windows, Linux and macOS, CI, the `coney` executable (a window and a headless test mode), the
`coney-tools` command line, the legal rules and these docs.

## Read the disc

Everything Coney loads comes out of `WARRIORS.WAD`, so the first job is to read it and know what is in it.

- Done: `coney-tools wad` lists, extracts and names the archive's entries from the player's own disc or disc image
  ([guide](guides/coney-tools.md), [WARRIORS.DIR / .WAD](research/formats/wad-dir.md)).
- Recover as many entry names as possible (3,990 of 10,701 so far, see
  [WAD contents](research/formats/wad-contents.md#names)).
- Done: a survey of every entry type: what each is, how many there are, which ones RenderWare or Lua already
  explain ([WAD contents](research/formats/wad-contents.md)).
- Done: a map of the executable, placing the original's source files and the middleware in `.text` (15% of it tied
  to a named file, 41% to a directory, 20% middleware), and pointing at `main`, the chunk system and file I/O
  ([Source map](research/source-map.md)).

**Done when** the tool extracts a disc, every entry type has a research page or a stub, and the source-file map is
published.

## Boot the engine

The path from the executable's entry point to its main loop, and the core it stands on: memory, the chunk system
that loads data, file I/O and the frame loop. Coney reads its data straight from the player's disc image.

- Done: research pages for the boot path and frame loop ([Boot and the main loop](research/boot.md)), the chunk
  loader ([Chunk system](research/chunk-system.md)) and the file layers ([File I/O](research/file-io.md)).
- Done: disc access from a folder or an ISO image, the `WARRIORS.DIR` index and entry reads (`src/fileio/`).
- Done: the chunk system: flat and grouped containers, the handler table, per-load stacks with checked pops, and
  raw blocks for every type whose subsystem is not written yet (`src/core/`).
- Done: the game-mode stack and the game clock on a fixed 1/30 s step (`src/gamemodes/`, `src/core/game_timer.h`).
- Done: `coney --disc <disc> --load <entry>` loads any WAD entry through the chunk system and prints a summary
  ([Building and testing](guides/building.md#run-coney)). On the NTSC-U disc 7,066 of the 10,701 entries load as
  chunk containers; the rest are not containers (Lua bytecode, text, sound banks, RenderWare-only data).
- Left for later milestones: the memory system (no research page yet), the rest of the initialisation order, the
  asynchronous file queue and the chunk handlers that need their subsystems.

**Done when** `coney` opens the player's disc and loads and parses any WAD entry through the reimplemented chunk
system, under a fixed timestep.

## First pixels

librw's OpenGL 3 renderer through SDL3, then the game's textures, models and level geometry, with a free camera.

**Done when** a level is on screen, textured, and can be flown through.

- Done: the research page for the graphics device, the frame, textures and 2D drawing
  ([Graphics device and textures](research/graphics.md)).

## Scripts

Much of the game's logic is Lua 4.0 bytecode in the WAD. Coney runs it in a Lua 4.0 interpreter and reimplements the
functions the game exposes to its scripts.

**Done when** a level's scripts load and run without errors.

## Characters

People in the world: models and animation, physics and collision, pathfinding, and the player under control from a
gamepad or keyboard.

**Done when** the player can walk around a level and other characters move in it.

## Gameplay

Combat, AI, missions, game modes, the front end and menus, cameras and saving.

**Done when** the first mission can be played from the title screen to its end.

## Sound and video

Sound banks, streamed music, speech and the Bink movies.

**Done when** the first mission plays with its sound, music and cutscenes.

## Enhancements

What the PC can do beyond the PS2, without changing how the game plays: any resolution and aspect ratio (widescreen),
anti-aliasing, and rendering interpolated between simulation steps so that motion is smooth above the game's fixed
30 Hz simulation. PS2-specific effects (the VU microcode's particles, glows and screen filters) are rewritten for the
PC renderer from their research pages. Texture-replacement packs, made by players and keyed by texture name, load
from a mods folder; Coney ships only the loader, never a pack.

**Done when** the first mission plays at a widescreen resolution with anti-aliasing and interpolated rendering, and a
test pack replaces a named texture.

## Xbox assets (optional)

A player who also owns the Xbox version can point Coney at that disc too. Coney then loads its sharper textures and
720p movies in place of the PS2 ones wherever the two correspond, through a resolver keyed by name and resource
hash, and falls back to the PS2 disc everywhere else. The PS2 version stays the only reference for behaviour.
Feasibility, formats and the plan: [Xbox assets](research/xbox-assets.md). The survey is a repeatable check:
`coney-tools xbox` reads the Xbox disc's archive and reproduces its counts ([guide](guides/coney-tools.md#xbox)).

**Done when** a level renders with the Xbox disc's textures and a movie plays from its HD version, and the game
runs the same with or without the Xbox disc.

## The whole game

Every mission and mode, checked against the original running in PCSX2, then a first release.
