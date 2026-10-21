# The launcher

The launcher is how a player installs, updates and starts Coney without a terminal: a small program of its own,
`coney-launcher`, beside `coney` in the [install folder](asset-and-script-architecture.md#install-folder). This page is
its design and the state of each part. The install it runs is the same library `coney --install` runs
([Running Coney](running.md#install)). Changes to the launcher or the installer need a maintainer's approval before
they merge (`repo.maintainer_decisions` in [.github/conventions.yml](repo:.github/conventions.yml)).

## Decided {#decided}

Maintainer decisions of 2026-10-21:

- A separate C++23 executable, SDL3 for the window and Dear ImGui over SDL's GPU renderer for the screens; it redraws
  only on input, progress or a timer, so an idle launcher costs nothing. Borderless window with its own title strip.
- Updates come from the repository's GitHub releases, described by a release manifest, `launcher.json`, that pins
  every file's size and SHA-256 ([below](#manifest)); downloads go over HTTPS through the operating system's own
  stack, behind one interface in `src/platform/`.
- An update is verified, staged, swapped in and rolled back if anything fails, then the launcher relaunches itself
  ([Updating](#updating)).
- Screens: a status panel, one big action tile (install, update, play), small tiles (settings, verify, folder) and the
  release notes from the manifest. The background is the maintainer's art; the launcher ships placeholder slots
  until the files are in ([Art](#art)).
- PS2 disc first: the install, `--data` and the launcher end to end with the PS2 disc; the Xbox disc follows as its
  own steps.

## The release manifest {#manifest}

Each release carries one `launcher.json` as a release asset. `src/launcher/release_manifest.cpp` reads it
(`launcher::parseRelease`) and refuses anything that could write outside the install folder.

```json
{
  "format": 1,
  "version": "0.2.0",
  "channel": "stable",
  "min_launcher": "0.1.0",
  "notes": ["One short line per change, for the news panel"],
  "changelog_url": "https://...",
  "platforms": {
    "windows-x64": [
      {"path": "coney.exe", "size": 12345678, "sha256": "<64 hex digits>", "asset": "coney-windows-x64.exe"}
    ]
  }
}
```

| Field | Meaning |
| --- | --- |
| `format` | The layout: 1. A launcher refuses another number and asks for a newer launcher. |
| `version`, `min_launcher` | `MAJOR.MINOR.PATCH[-PRERELEASE]`, ordered as Semantic Versioning 2.0 orders them; a release needs at least `min_launcher` |
| `channel` | `stable` or `preview` |
| `platforms` | Per platform (`windows-x64`, `linux-x64`, `macos-arm64`, `macos-x64`), every program file: its path in the install folder (relative, `/` between folders, no `..`), size, SHA-256 and the release asset it is downloaded from |

Only program files are listed: Coney's executables, licences and own assets. The game's files are never in a release
and never downloaded ([LEGAL.md](repo:LEGAL.md#no-game-data)).

## Updating {#updating}

`launcher::planUpdate` (`src/launcher/update_plan.cpp`) compares the release with the program files the install holds
and lists the files to download (missing, or another size or SHA-256) and the program files to remove. It refuses a
release with no build for the platform, one that needs a newer launcher, and one that is not newer than the installed
version, except for a repair (the verify tile), which restores the installed version's own files. Planned next, in
order: staging each download under `update/staging/<version>/` and checking its size and SHA-256 before it counts;
the swap (the old files to `update/rollback/<old version>/`, the new ones in, rolled back on any failure); the
launcher replacing itself (on Windows a running executable can be renamed but not overwritten, so it renames itself
aside, moves the new one in and relaunches).

## Art {#art}

The launcher draws two pieces of art, supplied by the maintainer, and a placeholder for each until the file is there:

| Slot | File, in `launcher/art/` beside the launcher | Size and format | Placeholder |
| --- | --- | --- | --- |
| Background | `background.webp` (or `background.jpg`) | 1920 × 1080, sRGB, cropped to fill; the launcher darkens the left 60 % and the bottom 45 % under its panels, so the subject belongs right of centre | a dark vertical gradient |
| Wordmark | `wordmark.png` | 600 × 160 transparent PNG, light on transparent, drawn at 300 × 80 at 100 % scale | "CONEY" in the interface font |
| Window icon | `icon.png` | 256 × 256 transparent PNG | the system's default |

## State {#state}

| Part | Where | State |
| --- | --- | --- |
| The install (copy, check, `index/install.json`) | `src/install/`, `coney --install` | built |
| Playing from an install | `coney --data`, the install beside the executable | built |
| JSON reader, SHA-256 | `src/core/json.cpp`, `src/core/sha256.cpp` | built |
| Release manifest, versions, update plan | `src/launcher/` | built |
| Staging, swap, rollback, self-replace | `src/launcher/` | planned |
| HTTPS downloads | `src/platform/`, one interface | planned |
| The window and screens | `coney-launcher` | planned, after the maintainer approves the screens |
| `launcher.json` written by the release job | CI | planned (changes the release process: a maintainer decision) |
