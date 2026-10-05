# Legal

Coney is an independent, open-source reimplementation of *The Warriors* (PS2, 2005). It is not affiliated with,
endorsed by or sponsored by Rockstar Games or Take-Two Interactive. These rules protect the project and its
contributors, and every review checks them.

## No game data

Nothing from the original game goes into this project. That covers executables, BIOS images, assets, extracted files
and memory dumps, and it applies everywhere:

- the repository, including history;
- issues, pull requests and comments;
- CI caches and logs;
- test fixtures;
- release artifacts.

Players supply their own disc. Tests that need game data skip when it isn't configured; they never fail for lack
of it and never ship a copy.

**Reference lists** are the one exception, decided 2026-10-04. Curated lists that identify things in
the game may be committed and published in `docs/` and `research/`: names and ids (characters, objects, weapons,
levels, animation clips, WAD entry names, script bindings), numeric values and short descriptions written by us. They
are facts that let players and modders interoperate with the game, in the way FiveM documents GTA V. They may be
illustrated with **reference screenshots**: small images Coney renders or captures of a model, object or screen
(thumbnails, transparent or plain background) to identify it on its reference page, as FiveM's references do. They
never include the assets themselves (model, texture, sound or movie files, or textures exported as images), the game's
text (subtitles, dialogue, menu strings beyond short labels) or script source, and they are never a file copied out of
the disc.

## Clean room

Ghidra pseudocode and disassembly are for reading, by analyst agents and by humans doing research. Analysts read the
original and write what they learn into `docs/research/`.

Implementers write engine code from `docs/research/` only. The result has the same observable behaviour but our own
structure, names and control flow. Rekit, the project's reverse-engineering agent kit (a sibling project), enforces
this where platforms allow: the implementer agent has no Ghidra or emulator tools.

Facts may be reproduced: data layouts, constants, file formats and interoperability algorithms.

## Research limits

Research documents describe; they don't reproduce. They hold addresses and short explanatory snippets only, never a
whole function.

## Licences

- Code is GPL-3.0-or-later (see `LICENSE`).
- Documentation in `docs/` is CC-BY-SA-4.0 (see `docs/LICENSE`).
- Dependencies keep their own licences. They are fetched at build time at the commits pinned in `cmake/deps.cmake`,
  never copied into the repository: SDL3 (zlib), librw (MIT), Dear ImGui (MIT, the debug menus' developer overlay)
  and Catch2 (Boost Software Licence 1.0, tests only). A build that is passed on carries their licence notices.
- Third-party assets in `assets/` keep theirs, with the licence file beside them. The sandbox's textures in
  `assets/sandbox/` are from Kenney's Prototype Textures (`www.kenney.nl`), released under CC0 1.0
  (`assets/sandbox/License.txt`); one is re-encoded, which CC0 allows. Only assets under CC0 or a licence compatible
  with GPL-3.0-or-later go in, never anything from the original game ([No game data](#no-game-data)).
- Each source file states its licence with `SPDX-License-Identifier: GPL-3.0-or-later`.

## Provenance

By contributing you agree that:

- your contribution is licensed under the project's licences: GPL-3.0-or-later for code, CC-BY-SA-4.0 for
  documentation in `docs/` (see [Licences](#licences));
- it contains no game data ([No game data](#no-game-data));
- it respects the clean room ([Clean room](#clean-room)).

A commit an AI agent writes names the agent in a `Co-Authored-By:` line: the model's name and the vendor's noreply
address (for Claude, `<noreply@anthropic.com>`), never a personal email address. The maintainer who merges the
work answers for it.

## Trademarks

The project and its binary are called Coney. "The Warriors", Rockstar Games and Take-Two Interactive are trademarks
of their respective owners. They are used here only to describe what Coney is compatible with.
