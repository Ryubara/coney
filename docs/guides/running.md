# Running Coney

Every option of the `coney` executable and what each mode does. This page says how to start Coney, shows the whole
command line, says how to [install the game from your disc](#install) and where [script patches](#script-patches)
are made, and lists the pages that document the options:
the window, sound and network, loading and viewing your disc, playing a level with its traces and logs, and the
controls. It assumes you have built Coney ([Building and testing](building.md)); the viewers and the game need your own
copy of the game (`--disc`, or an install with `--data`), while `--headless`, `--sandbox` and `--audio-test` do not
([Local workspace](workspace.md) says where to keep it). How tests drive the executable, and what a change must show
before it is done, is in [Testing](testing.md); the debug menus have [their own page](debug-menu.md).

## Starting Coney {#run-coney}

The executable is `build/<preset>/src/platform/coney` (`coney.exe` on Windows). Run with no arguments, it opens a
960 × 720 window and runs until you close it (or press Escape). Underneath, the game-mode stack runs on a fixed
1/30 s step with an idle mode at its bottom, which clears the screen to a dark slate and presents it every frame.
The game runs at the same speed at any frame rate ([Frame rate](running-options.md#frame-rate)).

Everything is drawn into the original's 640 × 448 screen, shown at a television's 4:3 shape as large as the window
allows and centred; a window of another shape gets black bars at the sides or at the top and bottom.

```text
coney --install DISC --data DIR [--verify]
coney --script-patch make|apply ...
coney [--disc PATH] [--load ENTRY]... [--view-txd ENTRY] [--view-sheet SHEET] [--frames N] [--screenshot PATH]
      [--draw-hash]
      [--headless] [--help] [--input-script FILE] [--view-text FONT TEXT] [--language CODE]
      [--view-world NAME] [--view-character [NAME]] [--anim CLIP]
      [--play-level NAME [--spawn NAME | --checkpoint N] [--start X,Y,Z,H[,D,YAW]] [--give ITEM,COUNT]... [--trace FILE]
                   [--script-trace FILE] [--scene NAME] [--camera X,Y,Z,QX,QY,QZ,QW[,FOV]] [--freeze-world]]
      [--trace-humans FILE]
      [--render-size WxH]
      [--sandbox [NAME]] [--assets DIR] [--render-references DIR [--kind KIND] [--only NAME]... [--names FILE]]
      [--fps-cap N] [--vsync on|off] [--line-blend on|off] [--show-fps] [--renderer gl3|gpu]
      [--preset ps2|xbox|enhanced|high] [--tunables FILE]
      [--no-audio | --audio-test]
      [--skip-movies] [--rumble TYPE [--arena N] [--gang-size N]]
      [--profiles DIR] [--dev-overlay N] [--mesh-view SPEC] [--no-activate] [--window-display DISPLAY] [--event-log FILE]
      [--pad-pipe] [--record FILE [--record-seconds N]]
      [--bench FILE [--bench-fighters N] [--bench-allies N] [--bench-god] [--bench-spawn-step N] [--bench-warmup N]
        [--bench-measure N] [--bench-frames FILE] [--bench-real-time] [--bench-place X,Y,Z,H]
        [--bench-neutrals N] [--bench-fps]]
      [--log-file FILE] [--log-verbose CATEGORIES]
      [--host [[ADDR:]PORT] | --join ADDR[:PORT]] [--net-name NAME] [--cpu-skinning]
```

That is the command line of a build with the debug tools (`dev`, `playtest`, `ci`). The release build a player gets
(the `release` preset, [The debug tools and the game](building.md#debug-tools)) takes a player's options alone, and
refuses any other with a message that names the debug tools; `coney --help` lists what a build takes:

```text
coney --install DISC --data DIR [--verify]
coney [--disc PATH | --data DIR] [--language CODE] [--profiles DIR]
      [--fps-cap N] [--vsync on|off] [--line-blend on|off] [--show-fps]
      [--renderer gl3|gpu] [--preset ps2|xbox|enhanced|high]
      [--no-activate] [--window-display DISPLAY] [--no-audio] [--skip-movies]
      [--host [[ADDR:]PORT] | --join ADDR[:PORT]] [--net-name NAME]
      [--headless] [--frames N] [--input-script FILE] [--help]
```

It plays the story from boot (or shows the idle screen without a disc), and hosts or joins network play
([Network play](running-options.md#network-play)) as any build does. `--headless`, `--frames` and
`--input-script` are there for its own smoke tests; everything else in the command line above (the viewers,
`--play-level`, the sandbox, the traces, the event log, the pad pipe, screenshots, clips, the debug menus) needs the
debug tools.

Coney draws with librw's OpenGL 3 renderer (an OpenGL 3.3 core context through SDL3; librw falls back to 2.1 or OpenGL
ES). Where the driver offers less than OpenGL 3.3 (Windows' own OpenGL 1.1 renderer on a machine with no graphics
driver, such as CI's), Coney stops at start-up with an error that names the version it was offered, since SDL would
otherwise hand librw the old context and librw would crash. `--frames N` stops after N frames (each one fixed step and
one render, see [Frame rate](running-options.md#frame-rate)), which is how tests and scripts run it. `--headless` runs
with no window and librw's NULL renderer, so it needs neither a display nor a GPU; this is how CI runs it:

```sh
build/dev/src/platform/coney --headless --frames 3
```

Without `--headless`, a machine with no display or no OpenGL fails at start-up with SDL's reason and exit code 1.

`--screenshot PATH` saves the last frame (the one `--frames N` stops at) as a PNG and prints how many of its pixels
differ from the background and a hash of the frame, so a script can check that something was drawn without keeping
the image. Keep screenshots of game data out of the repository (`../../scratch/` is the place); they go on a pull
request as attachments, uploaded with `gh --attach` ([Capturing and attaching](testing.md#pr-media)).

`--draw-hash` records what the last frame (the one `--frames N` stops at, counted from the first frame of play with
`--play-level`) asks librw and OpenGL to draw and prints its counts and a hash of it, rounded to a fixed grid, in a
line `draw stream: R records: ... hash 0x...`. Two builds that print the same hash for the same frame drew the same
thing, which is how the renderer's work proves that the *PS2* picture did not move
([How pictures are compared](../engine/renderer.md#comparing)). It runs with a window and headless; headless, only the
2D drawing that reaches the draw layer is in it. It prints counts and the hash only, never what was drawn.

The STORY profiles are saved as one file each (`profile-1.sav` to `profile-6.sav`) in `profiles` in your data folder
(`%APPDATA%\Coney\Coney\` on Windows, `~/.local/share/Coney/Coney/` on Linux, `~/Library/Application
Support/Coney/Coney/` on macOS); `--profiles DIR` names another folder. A test-mode run without `--profiles` keeps
its profiles in memory, so tests never touch yours ([Profiles and saving](../research/save.md#coney)).

## Installing from your disc {#install}

`--install DISC --data DIR` copies your PS2 disc into an install folder, so the game runs from it with no disc or
image afterwards, then exits. `DISC` is a mounted disc, a folder of its files or an ISO image, read directly on every
system (no mounting); `DIR` is the folder you pick, made if needed. The layout is the one on
[The install folder](asset-and-script-architecture.md#install-folder):

```sh
build/dev/src/platform/coney --install /path/to/warriors.iso --data ../../Coney
build/dev/src/platform/coney --install H:/ --data ../../Coney --verify
```

```text
<DIR>/
  coney(.exe)            this executable, copied (unless it already runs from DIR)
  game/ps2/              every file of the disc as it is: WARRIORS.DIR, WARRIORS.WAD, SLUS_212.15, IOP/, PSS/, ...
  overrides/             empty: packs/, textures/, audio/sounds/, audio/music/, movies/, files/
  index/install.json     what was installed: the disc, Coney's version, every file's size and SHA-1, digests
```

What it does, in order:

1. **Checks the disc** against the known releases ([The discs Coney installs from](../research/discs.md)): a disc that
   is not the game, another release, or a copy with a missing or resized file is refused with the reason before
   anything is written. Today only the NTSC-U disc (SLUS-21215) is known.
2. **Checks the space**: the drive must hold the files still to copy (4.27 GB for a new install) and 64 MB more.
3. **Copies each file as it is**, computing its SHA-1 while it copies. It is written as `<name>.part` and renamed only
   once its SHA-1 matches the known one, so a stopped install (closed, crashed, the drive full) never leaves a
   half-copied file under its real name. A file whose SHA-1 differs is not kept and the install stops, naming it.
4. **Copies Coney's own executable** into `DIR` and writes the empty override folders.
5. **Writes `index/install.json` last.** It is removed when an install starts and exists only once one has finished.
   It records the disc's id, title, path and digest, Coney's version, per type (`ps2/archive`, `ps2/sound`,
   `ps2/movies`, `ps2/modules`, `ps2/executable`, `ps2/system`) the count, bytes and digest, and each file's path,
   size and SHA-1 ([digests](../research/discs.md#digests)). The same disc gives the same file, byte for byte,
   whatever the folder.

Running the same command again continues where it stopped: a file already there with its size is kept (only a file
whose SHA-1 was checked ever gets its name), and the rest are copied. `--verify` reads every installed file again and
copies any that no longer matches. Coney prints one line per file and one each tenth of the way, then a summary of
counts, bytes and the disc's digest; never a file's contents. It exits 0 when the install finished and 1 when it was
refused or failed, with the reason. The game text is not extracted: the install copies the disc's files as they are
([Maintainer decisions](asset-and-script-architecture.md#maintainer-decisions)). The Xbox disc is not taken yet.

The install is a library (`src/install/`, the folder writes in `src/platform/install_folder_fs.cpp`) with a progress
callback that can cancel, so a launcher can run the same steps behind its own screen.

**Playing from an install.** `--data DIR` without `--install` plays the installed game: it stands in for `--disc` in
every mode (`coney --data ../../Coney --play-level level99`), reading `DIR/game/ps2/`, the disc's files as they are,
with the same code as a disc folder. It refuses a folder whose install has not finished (no `index/install.json`) and
says how to finish it, and cannot be given with `--disc`. With neither `--disc` nor `--data`, Coney looks in its own
folder: run from a finished install (the `coney` that `--install` copied there), it plays that install, so a player
just starts `coney`. A `--disc` on the command line always wins.

**Replacing a WAD file: `overrides/files/`.** A game played from an install reads each file in `DIR/overrides/files/`
in place of the WAD entry it is named after (`intro.scn`, or `0x` and the name hash), and logs `overrides/files: N WAD
entries replaced: a modded session`; `--disc` ignores the folder. The folder never holds a script: a file that would
replace one of the game's scripts, or that is a compiled Lua chunk, is refused with a warning, and the disc's script
runs. An edited game script is a patch ([Script patches](#script-patches)). The naming rules and warnings:
[Replacing a WAD entry](asset-and-script-architecture.md#file-overrides).

## Script patches {#script-patches}

`--script-patch make|apply` makes a script patch from a decompiled game script and your edit of it, or applies one,
on files, then exits; it needs no disc and opens no window. It must come first on the command line. The format and the
command are in [Script patches](script-patches.md#command-line).

## The pages {#pages}

### [Window, sound, network, recording and debug options](running-options.md)

* [`--record`](running-options.md#record): a short video clip of the run.
* [`--window-display`](running-options.md#window-display): which display the window opens on.
* [`--fps-cap`, `--vsync`, `--line-blend`, `--show-fps`, `--renderer`, `--preset`,
  `--bench`](running-options.md#frame-rate): the frame rate, the renderer, the display preset and the benchmark.
  {#frame-rate}
* [`--no-audio`, `--audio-test`](running-options.md#sound): the sound.
  {#sound}
* [`--host`, `--join`](running-options.md#network-play): network play.
* [`--tunables`, `--dev-overlay`, `--mesh-view`](running-options.md#the-debug-menus): the debug menus' options.

### [Loading and viewing your disc](running-viewers.md)

* [`--disc`, `--load`](running-viewers.md#loading-entries-from-your-disc): loading entries and the start-up.
  {#loading-entries-from-your-disc}
* [`--view-txd`, `--view-sheet`, `--view-text`](running-viewers.md#viewing-texture-dictionaries): the 2D viewers.
  {#viewing-text}
* [`--view-world`](running-viewers.md#the-world-viewer): the streamed world.
  {#the-world-viewer}
* [`--view-character`](running-viewers.md#the-character-viewer): a character and its clips.
  {#the-character-viewer}
* [`--sandbox`](running-viewers.md#the-sandbox): Coney's own test layouts.
* [`--render-references`](running-viewers.md#reference-images): the reference images of the docs.
  {#reference-images}

### [Playing a level, tracing and logs](running-play-level.md)

* [`--play-level`](running-play-level.md#playing-a-level): a level as player 1.
  {#playing-a-level}
* [`--trace`, `--trace-humans`, `--script-trace`](running-play-level.md#tracing): per-step traces.
  {#tracing}
* [`--event-log`](running-play-level.md#event-log), [`--log-file`](running-play-level.md#run-log),
  [`--pad-pipe`](running-play-level.md#pad-pipe) and [matched views](running-play-level.md#matched-views).

### [Controls and input scripts](running-input.md)

* [The controls](running-input.md#controls): keyboard and gamepad.
  {#controls}
* [`--input-script`](running-input.md#input-scripts): scripted input for tests.
  {#input-scripts}

<!-- The ids on the list items above keep the links that name running.md#<section> working in the legacy research pages
that cite them. Touching a research page holds it to the whole research standard, so they go, with these ids, once those
pages are touched and cite the page that holds the section. -->
