# Coney

> [!IMPORTANT]
> Coney is in early development and not playable yet. It contains none of the original game's files or code: you
> play it with your own copy of the game. Coney was restarted in October 2026 with a stricter clean-room process; the
> [clean-room page](docs/clean-room.md) explains how it works.

## What Coney is

Coney is an independent, open-source reimplementation of the engine of *The Warriors* (PS2, 2005), written in C++23
on SDL3 for Windows, Linux and macOS. The goals:

- **Faithful gameplay.** Every gameplay rule matches the original until the game is complete.
- **Two renderer modes.** Faithful, which keeps the PS2 look, and enhanced, which adds shadows, reflections and
  highlights over the original lighting.
- **A fixed 60 Hz simulation**, deterministic, with networked play built in (lockstep with rollback).
- **Your own install.** A launcher reads your PS2 or Xbox disc image once and builds a local install from it. The
  original mission scripts run from that install on Coney's Lua 4.0 VM.
- **Modding.** A Lua SDK for new missions, and tools for custom content.

Coney is not affiliated with, endorsed by or sponsored by Rockstar Games or Take-Two Interactive.

## Progress

The clean-room tooling comes first: the page checks, the gates between the research and the engine, and the
isolated containers implementers work in. Engine work starts when that system is in place, one subsystem at a time.

Progress will be reported per subsystem as the share of the original's code covered by reimplemented functions that
passed their comparison, next to the share of research that passed the page gate. Both numbers are generated, never
typed in.

## Building and running

### Requirements

- Python 3.11 or later, for `coney-tools` (standard library only, nothing to install).
- The engine's requirements (a C++23 compiler, CMake) arrive with the engine. Implementers work in a container and
  need only Docker ([CONTRIBUTING.md](CONTRIBUTING.md)).

### Clone

```sh
git clone https://github.com/Ryubara/coney.git
cd coney
```

### Build and test

There is no engine to build yet. To run the tool's tests:

```sh
cd python
python -I -B -m unittest discover -s tests
```

To run every check CI runs, the same way CI runs it ([Checks and CI](docs/ci.md)):

```sh
python -I python/coney-tools.py ci
```

### Run

Nothing to run yet. The launcher and installer come first: they take your disc image and set up a local install.

## Documentation

- [`python/README.md`](python/README.md): `coney-tools`, including the research page check.
- [`AGENTS.md`](AGENTS.md): how agents work in this repository.
- [`docs/ci.md`](docs/ci.md): the checks CI runs, how to run them locally, and the pinned versions.
- The [documentation site](https://ryubara.github.io/coney/), with the clean-room page and the guides (online once
  the repository is public; `docs/` holds the same pages).

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) first: who may take which role, and what each role needs. Everyone follows
the [Code of Conduct](CODE_OF_CONDUCT.md). Report security problems as [SECURITY.md](SECURITY.md) describes.

## Licence

Coney's code is licensed under the GNU General Public License, version 3 or later ([LICENSE](LICENSE)).
[LEGAL.md](LEGAL.md) covers game data, the clean room, trademarks and the licences of documentation and
dependencies.
