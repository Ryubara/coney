# Coney Director

Coney Director is a planned recorder and video editor built into Coney, modelled on GTA V's Rockstar Editor: the
player records play in the game, then opens Director to watch the recordings from any camera and make videos from them,
with markers, slow and fast motion, a free camera, target tracking with smooth blends, and smooth scrubbing. Videos
export as MP4. The parent issue is [#1184](issue:1184).

**Status: design.** Nothing in this folder is built. These pages are designs in the sense of [Page
kinds](../writing-docs.md#page-kinds); what gets built moves into a guide.

## What Director is {#principles}

- **Two parts:** the recorder in the game, and Director, which views and edits recordings. Director places no actors
  and builds no scenes: scenes are made with [Coney Studio](../studio/index.md) and its scripts and recorded in the
  game.
- **Recordings are replays, not video:** the engine plays the recorded game state again and draws it with Director's
  cameras.
- **Clips of 60 seconds**, each saved as it ends, the next one starting with no gap.
- **One fixed folder** in the install folder for recordings and videos
  ([The install folder](../asset-and-script-architecture.md#install-folder)).
- **Nothing in Director changes how the game plays.** Recording reads the game's state and never alters it, and the
  [test mode](../testing.md) holds: a recording made with a fixed step and seeded randomness is the same every run.

A maintainer decision: the name is **Coney Director**. GTA V's *Director Mode*, which stages scenes with actors, is a
different feature that Director does not have ([The words Director uses](rockstar-editor.md#terms)).

## The design pages {#pages}

One page per research unit, each a sub-issue of [#1184](issue:1184). A unit that merges its page adds its row here
and its line to the `nav:` of `mkdocs.yml`, under *Coney Director design*.

| Page | What it decides | Issue |
| --- | --- | --- |
| [The Rockstar Editor survey](rockstar-editor.md) | What the Rockstar Editor records and how, its words and features, what players praise and dislike, other games' replays; the words Director uses, the feature list of its first version, the open questions | [#1185](issue:1185) |
| [Recording](recording.md) | What a recording stores (the drawn state in key frames and deltas, measured on the hub and a crowd of 50) and holds, clips of 60 seconds with no gap, seeking and scrubbing, the file, versions and mods, the folder and a full disk, what it shares with networking, the tests, the build issues | [#1186](issue:1186) |
