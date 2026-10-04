# Roadmap

How Coney gets from an empty window to the whole game, as a sequence of milestones. Each milestone ends in
something you can run or check, and each builds on the ones before it. Research for a milestone usually starts
before the previous one is finished, because the implementer can only begin once the research page exists.

This page is kept current: a milestone's status changes in the same commit as the work that changes it.

| Milestone | Status |
| --- | --- |
| [Foundations](#foundations) | done |
| [Read the disc](#read-the-disc) | in progress |
| [Boot the engine](#boot-the-engine) | not started |
| [First pixels](#first-pixels) | not started |
| [Scripts](#scripts) | not started |
| [Characters](#characters) | not started |
| [Gameplay](#gameplay) | not started |
| [Sound and video](#sound-and-video) | not started |
| [The whole game](#the-whole-game) | not started |

## Foundations

The build on Windows, Linux and macOS, CI, the `coney` executable (a window and a headless test mode), the
`coney-tools` command line, the legal rules and these docs.

## Read the disc

Everything Coney loads comes out of `WARRIORS.WAD`, so the first job is to read it and know what is in it.

- `coney-tools wad`: list, extract and name the archive's entries from the player's own disc
  ([WARRIORS.DIR / .WAD](research/formats/wad-dir.md)).
- Recover as many entry names as possible (412 of 10,701 so far).
- A survey of every entry type: what each is, how many there are, which ones RenderWare or Lua already explain.
- A map of the executable: which functions belong to which of the original's source files, from the file paths its
  assertion messages carry. It tells every later analyst where to look.

**Done when** the tool extracts a disc, every entry type has a research page or a stub, and the source-file map is
published.

## Boot the engine

The path from the executable's entry point to its main loop, and the core it stands on: memory, the chunk system
that loads data, file I/O and the frame loop. Coney reads its data straight from the player's disc image.

**Done when** `coney` opens the player's disc and loads and parses any WAD entry through the reimplemented chunk
system, under a fixed timestep.

## First pixels

librw's OpenGL 3 renderer through SDL3, then the game's textures, models and level geometry, with a free camera.

**Done when** a level is on screen, textured, and can be flown through.

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

## The whole game

Every mission and mode, checked against the original running in PCSX2, then a first release.
