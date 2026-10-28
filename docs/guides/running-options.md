# Window, sound, network, recording and debug options

The options that shape the window and the run, apart from what it loads: [recording a clip](#record) with `--record`,
[which display](#window-display) the window opens on, the [frame rate](#frame-rate) with `--bench`, [sound](#sound),
[network play](#network-play) and [the debug menus' options](#the-debug-menus). The other pages are listed in [the
overview](running.md#pages).

## Recording a clip {#record}

`--record FILE` writes a short video clip of the run for an issue or a pull request: H.264 in an MP4, 30 frames a
second (one per fixed step), 720 lines high at the picture's shape (960 x 720 for the usual 4:3), CRF 26 with the
`slow` preset, and the game's own sound as AAC at 96 kbit/s. `--record-seconds N` sets its length, 1 to 10 seconds;
10, the default, is also the most. Coney's own debug tooling, with no counterpart in the original.

```sh
CLIPS=$(uv run --project python coney-tools clips folder my-task)   # the absolute path of the clips folder
build/dev/src/platform/coney --disc /path/to/warriors.iso --record "$CLIPS/front-end.mp4" --record-seconds 5
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level level99 --input-script tests/support/play_walk.txt --record "$CLIPS/walk.mp4"
```

- **What it records.** The frames Coney draws, read back from its own frame buffer after the game's 2D layers (HUD,
  menus, fades, line blend) and before the debug menus and the developer overlay are drawn, so neither appears; the
  debug menus' lines in the 3D scene are switched off while it records. It never reads the screen or a window. The
  sound is the mixer's own output, step by step.
- **When.** From the first frame, or with `--play-level` from the level's first frame of play (as `--frames` counts).
  Without `--frames` the run ends when the clip is done. `--record` puts Coney in test mode (one step and one render a
  frame, no clock), so the same command and `--input-script` give the same frames: the line it prints ends with a hash
  of every frame recorded.
- **Where.** `FILE` must be a plain name (letters, digits, `.`, `_`, `-`) ending in `.mp4`, in an existing folder
  named `clips` outside every git checkout: the task's scratch folder, `<workspace>/scratch/<task>/clips/`
  (`coney-tools clips folder TASK` makes and prints it). Pass it absolute: a relative `../../scratch/...` is right
  only from the main checkout, not from a worktree (three levels deep). Anything else is refused before the run
  starts.
- **What the file holds.** Only the picture and the sound: no title, comment, encoder version or path (FFmpeg runs
  with `-map_metadata -1`, bit-exact headers and x264's settings note removed). `coney-tools clips check` confirms it.
  A separate `.record.csv` companion holds the first event-log step and the number of recorded frames, both numeric.
  Record with `--event-log FILE` as well to list events with their time in the clip using
  [`coney-tools clips events`](coney-tools-pcsx2.md#clips). That report omits details and redacts text-bearing names.
- **Needs** a window (not `--headless`, `--load` or `--render-references`) and FFmpeg: `CONEY_FFMPEG` names the
  program, else `ffmpeg` is looked up on `PATH`. Frames go to FFmpeg through its standard input (its only input), the
  sound is muxed in a second pass; the part files beside the clip are deleted. A clip over 10 MB prints a warning.

Clips are uploaded only with `coney-tools clips publish`, which deletes them afterwards
([Clips](testing.md#clips)).

## Which display the window opens on {#window-display}

`--window-display DISPLAY`, or the environment variable `CONEY_WINDOW_DISPLAY` when the option is not given, opens the
window centred on one display. It is debug tooling, there so that agents' runs stay off the display a person is
working on ([Running Coney](research-tools.md#running-coney)). The value is one of:

| Value | Display |
| --- | --- |
| `1`, `2`, ... | That display in SDL's list, counted from 1. On Windows the primary display is 1 and the others follow in Windows' own order, which need not match the numbers in Windows' display settings |
| `portrait` (any case) | The first display taller than wide |
| any other text | The first display whose name holds it, case ignored: SDL's name for it, on Windows the monitor's model (`DELL U2724D`) |

The window keeps its size when it fits the display's usable area (the display less its taskbar or menu bar, and less
the window's own borders) and is otherwise shrunk to the largest size of the same shape that fits, so a 1920 × 1080
window on a 1080 × 1920 display opens 1080 wide; frames then have the window's new size. An empty value, or neither
the option nor the variable, leaves the window where SDL puts it (on the primary display). A value that matches no
display prints one warning on stderr, listing the displays there are with their numbers, names and sizes, and the
window opens where SDL puts it.

Whenever a value is set (even one that matches no display), the window also opens without taking the keyboard focus,
as with `--no-activate`, and SDL keeps it from taking the focus when it is shown or raised later: a run placed on a
display of its own is one an agent or a tool started, and the person typing elsewhere keeps their keystrokes.

librw makes and shows the window itself inside its start-up, so Coney cannot create it in the right place; it moves
the window the moment SDL reports it shown, before its OpenGL context exists or a frame is drawn
(`RenderEngine::start`, `repo:src/platform/render_engine.cpp`; the choosing and fitting are
`repo:src/core/window_display.h`). A hidden window (`--render-references`, the tests') is hidden as SDL shows it, then
moved: librw makes it shown and has no way to make it hidden. Moving a window to a display with another scale makes
Windows resize it on the way (640 × 448 made on a 150 % display came out 646 × 465 on a 100 % one, letterboxing every
frame and capture), so Coney sets the size again once the window has arrived; the `coney.frame_copies_upright` check
opens its window on every display in turn and fails when a frame is not the size it asked for. It starts and stops the
renderer once per display, which is why it also checks, after each stop, that librw's "shader in use"
(`rw::gl3::currentShader`) is null, and, once at the start, that starting the renderer on SDL's dummy video driver (a
display, no OpenGL) comes back as an error. It checks nothing else of the renderer's restart; the `coney_tests` case
`a renderer started again after one stopped meets none of its state` checks the rest over four starts (the texture
units, the framebuffer cache, fog and its colour, the 2D shader override and the rule that every raster is destroyed
before the stop). What librw leaves behind, and what Coney does about each piece, is on [Restarting the
renderer](../research/graphics-restart.md). The same variable places the
windows of PCSX2 copies that `coney-tools pcsx2 launch` and `pcsx2 record` start
([Driving PCSX2](research-tools.md#driving-pcsx2)).

## Frame rate {#frame-rate}

The game always advances in the original's fixed steps of 1/30 s, 30 a second, whatever the display does; drawing is
apart from the step and runs as fast as the display or the cap allows, each frame blended between the last two steps,
so motion is smooth above 30 frames a second and the game's speed never changes
([Update and render](conventions.md#update-and-render)).

| Option | Does |
| --- | --- |
| `--fps-cap N` | at most N frames a second; 0, the default, is no cap. 30 is the original's rhythm: one step and one frame, nothing blended |
| `--vsync on\|off` | wait for the display's vertical blank when presenting (on, the default) or not (frames may tear) |
| `--line-blend on\|off` | an optional PS2 look: soften the picture as the PS2's video output does, each shown line the mean of two neighbouring lines of the original's 448 (on; [Rendering](../research/rendering.md#output)), or show the frame as drawn (off, the default); reference renders are never blended |
| `--show-fps` | print the frame and step rates once a second, and the totals when Coney stops |

The default, no cap with vsync on, draws one frame per refresh of the display. With vsync off and no cap Coney draws
as fast as it can, which keeps a processor core busy; give a cap to save power. Below 7.5 frames a second (or after a
stall such as dragging the window) the game slows down rather than catching up in a rush, as the original slows down
when a frame runs long. A blended frame shows the world up to one step (33 ms) behind the newest simulated state, and
the pads are read once per step, as in the original.

Test mode is lockstep: with `--frames`, `--headless`, `--input-script`, `--screenshot` or `--record` every frame is
one step and one render, with no clock, so a run gives the same result every time on any machine. `--fps-cap`
and `--show-fps` are refused there.

A test-mode run's window opens without taking the keyboard focus, so a scripted run never interrupts someone typing;
`--no-activate` does the same for any other run, and `--render-references` never takes it. Agents start Coney only
this way ([Running Coney](research-tools.md#running-coney)).

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --show-fps
build/dev/src/platform/coney --disc /path/to/warriors.iso --view-character --fps-cap 144 --vsync off --show-fps
```

`--bench FILE` measures a `--play-level ... --frames N` run with Coney's profiler and writes the frame times, the
zones' times and the counters (draw calls, uploads, allocations) as JSON; `--bench-fighters N` spawns N AI fighters
in front of the player first, and the other `--bench-*` options set up the crowd and how the run is measured. How to
benchmark and what the numbers say: [Performance](performance.md#bench).
`--cpu-skinning` skins the humans on the CPU instead of the GPU, to compare the two
([GPU skinning](performance.md#gpu-skinning)).

## Sound {#sound}

Coney plays through the system's default playback device at 48 kHz stereo and prints the device's name at start-up;
when no device opens, it says why and runs silent. When the default output changes, or a headset is plugged in or
out, the sound moves to the new default device and carries on; if SDL gives the device up altogether, Coney opens the
default device again, trying once a second until one opens. `--no-audio` opens no device. In test mode no device opens either:
the sound is mixed offline, 1,600 frames (1/30 s) per frame, so a run mixes the same samples every time.
`--audio-test` plays a synthesised tone sweep (220 to 880 Hz and back, looping) and prints at the end what was mixed;
offline that is the frame count, the peak and a hash. The debug menus' Audio page sets the volumes and plays the same
tone ([The debug menus](debug-menu.md#pages)).

```sh
build/dev/src/platform/coney --audio-test
build/dev/src/platform/coney --headless --frames 60 --audio-test
```

## Network play {#network-play}

Coney's [network play](multiplayer.md): a game can host and one other can join it, with the ping and loss on the
debug menus' Network page. The guest's pad plays the host's pad port 2, as a second pad plugged into the
host's machine would ([Player two](multiplayer.md#player-two)); the guest's own screen does not show the host's game.
`--host` hosts from the start, on UDP
port 27615 of every interface unless told otherwise: `--host 30000` picks the port, `--host 127.0.0.1:0` listens on
this machine only on any free port (the tests do), and the run prints the port it got. `--join ADDR[:PORT]` joins the
game at a host name or IPv4 address. `--net-name NAME` is the name the other players see (1 to 32 printable ASCII
characters). A port that cannot be opened, a bad name, or `--host` with `--join`, ends the run with exit code 1. In
test mode each step passes the network a fixed 1/30 s, so a run is the same every time; with `--host` or `--join` a
run prints one `net:` line at the end with the state and the bytes sent and received.

```sh
build/dev/src/platform/coney --host --net-name Cleon            # the host
build/dev/src/platform/coney --join 192.168.1.20 --net-name Swan # another machine on the network
build/dev/src/platform/coney --headless --frames 3 --host 127.0.0.1:0
```

## The debug menus {#the-debug-menus}

Every run has Coney's debug menu and, in a window, the developer overlay; [The debug menus](debug-menu.md) has the
chords, the pages and how to add to them. Three options matter here: `--tunables FILE` names the file the tunable
overrides are loaded from and saved to (a windowed run without it uses `coney-tunables.ini` in your config folder, a
headless run none), `--dev-overlay N` shows the overlay for the first N frames, a test aid
([the overlay](debug-menu.md#the-developer-overlay)), and `--mesh-view SPEC` switches the Debug draw page's
[mesh views](debug-menu.md#mesh-views) on from the first frame of play, for a screenshot of a view or the check that
the views leave the frame as they found it.

`SPEC` is a list of items separated by commas:

| Item | Does |
| --- | --- |
| `wire=selection\|humans\|all` | the model wireframe on the inspector's selection, every human or everything drawn |
| `view=wire\|material\|draw\|lod` | the mesh view: wireframe only, or flat by material, by draw call or by LOD |
| `scope=selection\|humans\|all` | the models the mesh view applies to (default `all`) |
| `normals`, `bones` | the selection's normals, its bone axes |
| `select=nearest`, `select=player` | select the human nearest the player (the player when there is no other), or the player, in the Entity inspector |
| `frames=N` | after N frames, switch every view off again and drop that selection |

```sh
build/dev/src/platform/coney --tunables ../../scratch/tunables.ini
build/dev/src/platform/coney --headless --frames 30 --input-script tests/support/debug_menu.txt
build/dev/src/platform/coney --frames 60 --dev-overlay 30 --screenshot ../../scratch/overlay-hidden.png
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level level99 --skip-movies --frames 90 \
    --mesh-view wire=selection,select=player --screenshot ../../scratch/wire.png
```
