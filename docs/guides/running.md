# Running Coney

Every option of the `coney` executable and what each mode does. This page is the overview: how to start Coney, the
command line and which page documents which options, the window, the viewers, playing a level or the controls. It
assumes you have built Coney ([Building and testing](building.md)); the viewers and the game need your own copy of
the game (`--disc`), while `--headless`, `--sandbox` and `--audio-test` do not
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
coney [--disc PATH] [--load ENTRY]... [--view-txd ENTRY] [--view-sheet SHEET] [--frames N] [--screenshot PATH]
      [--headless] [--help] [--input-script FILE] [--view-text FONT TEXT] [--language CODE]
      [--view-world NAME] [--view-character [NAME]] [--anim CLIP]
      [--play-level NAME [--spawn NAME | --checkpoint N] [--start X,Y,Z,H[,D,YAW]] [--trace FILE]
                   [--script-trace FILE] [--scene NAME] [--camera X,Y,Z,QX,QY,QZ,QW[,FOV]] [--freeze-world]]
      [--trace-humans FILE]
      [--render-size WxH]
      [--sandbox [NAME]] [--assets DIR] [--render-references DIR [--kind KIND] [--only NAME]... [--names FILE]]
      [--fps-cap N] [--vsync on|off] [--line-blend on|off] [--show-fps] [--tunables FILE]
      [--no-audio | --audio-test]
      [--skip-movies] [--rumble TYPE [--arena N] [--gang-size N]]
      [--profiles DIR] [--dev-overlay N] [--no-activate] [--window-display DISPLAY] [--event-log FILE] [--pad-pipe]
      [--record FILE [--record-seconds N]] [--bench FILE [--bench-fighters N] [--bench-warmup N]]
      [--log-file FILE] [--log-verbose CATEGORIES]
      [--host [[ADDR:]PORT] | --join ADDR[:PORT]] [--net-name NAME]
```

That is the command line of a build with the debug tools (`dev`, `playtest`, `ci`). The release build a player gets
(the `release` preset, [The debug tools and the game](building.md#debug-tools)) takes a player's options alone, and
refuses any other with a message that names the debug tools; `coney --help` lists what a build takes:

```text
coney [--disc PATH] [--language CODE] [--profiles DIR]
      [--fps-cap N] [--vsync on|off] [--line-blend on|off] [--show-fps]
      [--no-activate] [--window-display DISPLAY] [--no-audio] [--skip-movies]
      [--host [[ADDR:]PORT] | --join ADDR[:PORT]] [--net-name NAME]
      [--headless] [--frames N] [--input-script FILE] [--help]
```

It plays the story from boot (or shows the idle screen without a disc), and hosts or joins network play
([Network play](running-options.md#network-play)) as any build does. `--headless`, `--frames` and
`--input-script` are there for its own smoke tests; everything else on this page (the viewers, `--play-level`, the
sandbox, the traces, the event log, the pad pipe, screenshots, clips, the debug menus) needs the debug tools.

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

The STORY profiles are saved as one file each (`profile-1.sav` to `profile-6.sav`) in `profiles` in your data folder
(`%APPDATA%\Coney\Coney\` on Windows, `~/.local/share/Coney/Coney/` on Linux, `~/Library/Application
Support/Coney/Coney/` on macOS); `--profiles DIR` names another folder. A test-mode run without `--profiles` keeps
its profiles in memory, so tests never touch yours ([Profiles and saving](../research/save.md#coney)).

## The pages {#pages}

### [The window, sound and network options](running-options.md)

* [`--record`](running-options.md#record): a short video clip of the run.
* [`--window-display`](running-options.md#window-display): which display the window opens on.
* [`--fps-cap`, `--vsync`, `--line-blend`, `--show-fps`](running-options.md#frame-rate): the frame rate.
  {#frame-rate}
* [`--no-audio`, `--audio-test`](running-options.md#sound): the sound.
  {#sound}
* [`--host`, `--join`](running-options.md#network-play): network play.
* [`--tunables`, `--dev-overlay`](running-options.md#the-debug-menus): the debug menus' options.

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
  {#event-log}

### [Controls and input scripts](running-input.md)

* [The controls](running-input.md#controls): keyboard and gamepad.
  {#controls}
* [`--input-script`](running-input.md#input-scripts): scripted input for tests.
  {#input-scripts}

<!-- The ids on the list items above keep the links that name running.md#<section> working in the legacy research pages
that cite them. Touching a research page holds it to the whole research standard, so they go, with these ids, once those
pages are touched and cite the page that holds the section. -->

## Shutdown order {#shutdown-order}

`main` destroys things in reverse order of declaration, so whatever points at the sound output must be declared
after it or reset before it. The game session (`session`) is reset explicitly right after the run summaries, before the
sound output goes: its scripts' binding context and play mode hold the game sound, and tearing them down afterwards
read freed state and segfaulted `--play-level level95` at exit. Any new
object that keeps a pointer into the sound output, the scene or the renderer follows the same rule. A ctest with
`PASS_REGULAR_EXPRESSION` ignores the exit code, so `coney.exits_cleanly_level95` and `_level99` (with `CONEY_DISC`
set) check the exit code alone.
