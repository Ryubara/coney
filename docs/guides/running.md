# Running Coney

Every option of the `coney` executable and what each mode does: the window and the frame rate, sound, loading and
viewing pieces of your disc, playing a level, tracing and logging, reference images, the controls and input scripts.
It assumes you have built Coney ([Building and testing](building.md)); the viewers and the game need your own copy of
the game (`--disc`), while `--headless`, `--sandbox` and `--audio-test` do not
([Local workspace](workspace.md) says where to keep it). How tests drive the executable, and what a change must show
before it is done, is in [Testing](testing.md); the debug menus have [their own page](debug-menu.md).

## Starting Coney {#run-coney}

The executable is `build/<preset>/src/platform/coney` (`coney.exe` on Windows). Run with no arguments, it opens a
960 × 720 window and runs until you close it (or press Escape). Underneath, the game-mode stack runs on a fixed
1/30 s step with an idle mode at its bottom, which clears the screen to a dark slate and presents it every frame.
The game runs at the same speed at any frame rate ([Frame rate](#frame-rate)).

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

Coney draws with librw's OpenGL 3 renderer (an OpenGL 3.3 core context through SDL3; librw falls back to 2.1 or OpenGL
ES). Where the driver offers less than OpenGL 3.3 (Windows' own OpenGL 1.1 renderer on a machine with no graphics
driver, such as CI's), Coney stops at start-up with an error that names the version it was offered, since SDL would
otherwise hand librw the old context and librw would crash. `--frames N` stops after N frames (each one fixed step and
one render, see [Frame rate](#frame-rate)), which is how tests and scripts run it. `--headless` runs with no window
and librw's NULL renderer, so it needs neither a display nor a GPU; this is how CI runs it:

```sh
build/dev/src/platform/coney --headless --frames 3
```

Without `--headless`, a machine with no display or no OpenGL fails at start-up with SDL's reason and exit code 1.

`--screenshot PATH` saves the last frame (the one `--frames N` stops at) as a PNG and prints how many of its pixels
differ from the background and a hash of the frame, so a script can check that something was drawn without keeping
the image. Keep screenshots of game data out of the repository (`../../scratch/` is the place); they go on a pull
request as attachments, uploaded with `gh --attach` ([Capturing and attaching](testing.md#pr-media)).

### Recording a clip {#record}

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
- **Needs** a window (not `--headless`, `--load` or `--render-references`) and FFmpeg: `CONEY_FFMPEG` names the
  program, else `ffmpeg` is looked up on `PATH`. Frames go to FFmpeg through its standard input (its only input), the
  sound is muxed in a second pass; the part files beside the clip are deleted. A clip over 10 MB prints a warning.

Clips are uploaded only with `coney-tools clips publish`, which deletes them afterwards
([Clips](testing.md#clips)).

The STORY profiles are saved as one file each (`profile-1.sav` to `profile-6.sav`) in `profiles` in your data folder
(`%APPDATA%\Coney\Coney\` on Windows, `~/.local/share/Coney/Coney/` on Linux, `~/Library/Application
Support/Coney/Coney/` on macOS); `--profiles DIR` names another folder. A test-mode run without `--profiles` keeps
its profiles in memory, so tests never touch yours ([Profiles and saving](../research/save.md#coney)).

### Which display the window opens on {#window-display}

`--window-display DISPLAY`, or the environment variable `CONEY_WINDOW_DISPLAY` when the option is not given, opens the
window centred on one display. It is debug tooling, there so that agents' runs stay off the display a person is
working on ([Running Coney](research-workflow.md#running-coney)). The value is one of:

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
`repo:src/core/window_display.h`). A hidden window (`--render-references`) is moved too. Moving a window to a display
with another scale makes Windows resize it on the way (640 × 448 made on a 150 % display came out 646 × 465 on a
100 % one, letterboxing every frame and capture), so Coney sets the size again once the window has arrived; the
`coney.frame_copies_upright` check opens its window on every display in turn and fails when a frame is not the size
it asked for. The same variable places the
windows of PCSX2 copies that `coney-tools pcsx2 launch` and `pcsx2 record` start
([Driving PCSX2](research-workflow.md#driving-pcsx2)).

### Frame rate {#frame-rate}

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
this way ([Running Coney](research-workflow.md#running-coney)).

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --show-fps
build/dev/src/platform/coney --disc /path/to/warriors.iso --view-character --fps-cap 144 --vsync off --show-fps
```

`--bench FILE` measures a `--play-level ... --frames N` run with Coney's profiler and writes the frame times, the
zones' times and the counters (draw calls, uploads, allocations) as JSON; `--bench-fighters N` spawns N AI fighters
in front of the player first. How to benchmark and what the numbers say: [Performance](performance.md#bench).

### Sound {#sound}

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

### Network play {#network-play}

Coney's [network play](multiplayer.md) is in progress: a game can host and one other can join it, with the ping and
loss on the debug menus' Network page. The guest's pad plays the host's pad port 2, as a second pad plugged into the
host's machine would ([Player two](multiplayer.md#player-two)); the guest's own screen does not show the host's game
yet. `--host` hosts from the start, on UDP
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

### Loading entries from your disc

`--disc PATH` names your own copy of the game: a mounted disc (`H:\` on Windows, `/mnt/disc` on Linux), a folder
holding `WARRIORS.DIR` and `WARRIORS.WAD`, or an ISO 9660 image of the disc (the same forms `coney-tools wad`
takes, see [The coney-tools command line](coney-tools.md#naming-the-disc)). Coney reads `WARRIORS.DIR`, checks it
against `WARRIORS.WAD`, and prints how many entries it lists.

With `--disc` and no viewer or `--load`, Coney runs the game's start-up as far as it goes. First the start-up movies
play with their sound (`LOGO`, `PLOGO`, then the intro `L1_IN`; [Movies](../research/movies.md#coneys-implementation)):
any pad button skips one, except `LOGO`, which always plays its 3.8 s. `--skip-movies` skips every movie at once (but
one chosen on the debug menu's [Movies page](debug-menu.md#movies)); the frame numbers below count from the legal
screen, as they do with it. Then the legal screen for five seconds (150
frames; no button skips it, as in the original), the memory-card check's "checking" message for three seconds (90
frames), then the menus' first screen over `level100`'s world, the game's logo and a blinking "press START" (from frame
242). START leads to the main menu (story, extras, quick rumble); the d-pad or the left stick moves, cross chooses and
triangle or circle goes back. Coney prints a line for each movie, music and sound it plays or skips, and one for each
screen it reaches
([Front end](../research/frontend.md#coneys-implementation)). `--language CODE` picks the strings and the legal
screen.

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --frames 3 --screenshot ../../scratch/legal.png
build/dev/src/platform/coney --disc /path/to/warriors.iso --headless --frames 530 --skip-movies --input-script tests/support/start_menu.txt
```

The second line runs to the main menu without a window: its last line is `profile manager: PM_Mode`. The front end
loads behind the memory-card loading screen (its two pictures and the red spinner, 7.2 s), so the menus are up from
frame 458.

**STORY** starts a new game as the original does, through the profile screens: (with two pads, how many players),
the profile manager (CREATE NEW PROFILE when there is none), the name keyboard (cross types the key under the cursor,
OK accepts), the difficulty, the brightness (left and right) and the subtitles. Profiles last for the run only (Coney
has no saves yet). The menus fade out and the game's own scripts take it from there (`Menu.startGame`,
`runNextMission(1)`, the mission-complete mode, `UnlockAndLoad`), to `level99` at checkpoint 1. Its level script
creates Rembrandt, and Coney loads the level with him where the script put him, under your control
([Playing a level](#playing-a-level)), after the level's loading screen (its pictures, the timed bar and the fades, for
about 3.2 s: [Level loading](../research/level-loading.md#coneys-implementation)), and its intro movie `L99_IN` plays
over it (skipped with `--skip-movies`). There is no intro scene, tutorial or other character yet. The log shows the
way: `profile manager: PM_Create` and the other screens, `profile manager: profile "A" created in slot 0`, `script:
Menu.startGame()`, `mission complete: kind 4`, `level flow: starting level99`, `loading screen: level99 (3 pictures:
...)`, `gameplay: level99 checkpoint 1: player 1 Rembrandt ...`, then `movie: L99_IN`. The scripted way
(`tests/support/story_new_profile_session.txt`: a one-letter name, the defaults, the cursor moved with the left stick
at 70 %; the test harnesses without the memory-card screen play the same presses 220 frames earlier,
`tests/support/story_new_profile.txt`):

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --headless --frames 790 --skip-movies --input-script tests/support/story_new_profile_session.txt
```

The loading screen shows from about frame 672 and level99 plays from frame 768.

**QUICK RUMBLE** follows the original's path too: the menu's scripts call `ShowRumbleModeInterface`, which opens the
Rumble menu (mode 0x11) and its four screens, as a fresh boot offers them: **Game Mode** (1 ON 1 or WAR PARTY),
**Game Type** (one player against the computer, or versus or co-op with a second pad), **Choose Gangs** (the BASEBALL
FURIES against the ORPHANS, the gangs a fresh profile has) and **Choose Area** (the Fight Pen, `level102`). The lists
come from the disc's own Rumble scripts, so the text is the game's. Up and down (d-pad or left stick) pick an entry,
cross confirms it and moves on, triangle or circle goes back a screen (out of the menu from the first). On Choose
Gangs each side picks in turn: up and down choose the gang, left and right rotate its roster to choose the warchief,
cross locks the side (twice in all), and back unlocks it; each change of screen is logged (`rumble menu: Game Type`).
The layout is Coney's: the screen's
name and its entries in white and grey on black. Confirming the arena calls the scripts' `Menu.startRumbleMode`, whose
level request loads the arena; the arena's script reads the set-up (`GetRumbleModeData`, `GetRumbleModeGangName`), and
its start callback creates player 1, the Furies' first fighter, and teleports him to his gang's first flag, where you
control him. A Brawl then plays to its end ([Rumble mode](../research/rumble.md#coney)): the intro names the two
gangs over the arena, and cross on its prompt starts the countdown; the Orphan confronts you and fights. Knock him
out and the win camera circles the winner, then the result screen offers **Replay** (the match again) and, after
**more**, **Rumble menu** (back to Game Mode) and **Quit** (the main menu). In a script, from boot (cross skips the
two logo movies, the stick up most of the way wraps the main menu round to QUICK RUMBLE, then cross on each screen,
twice on Choose Gangs, then on the intro's prompt):

```text
120 tap cross
135 tap cross
430 tap start
442 stick left 0 70
444 stick left 0 0
455 tap cross
510 tap cross
520 tap cross
530 tap cross
535 tap cross
540 tap cross
860 tap cross
900 tap cross
940 tap cross
980 tap cross
1020 tap cross
```

Left at that, the Orphan beats the Fury, cheers on the win camera, and the result screen names the Orphans (about frame
6100). With the stick down most of the way on Game Mode (`500 stick left 0 -70`, `502 stick left 0 0`) the match is a
WAR PARTY, five a side; when the player goes down with team-mates standing, the pad passes to one of them (`player: the
pad passes from human ... to human ...`). Add `tap square` every 12 frames from 1100 to 2000 and the Orphan goes down
instead; the result screen takes input from about frame 2400 (`2400 tap cross` replays). The run logs each step (`rumble
intro: done, calling FinishCountdown`, `rumble result: winner ...`, `rumble result: choice 0`).

#### Rumble from the command line {#rumble-from-the-command-line}

A fresh profile's Rumble menu offers only **1 ON 1** and **WAR PARTY** (Game Mode), the Furies and the Orphans and
the Fight Pen; King of the hill, Battle royal, Survival and Wheelchair, and the other arenas, are unlocked through the
story ([Rumble](../research/rumble.md)). For play-testing the others there is `--rumble TYPE [--arena N] [--gang-size
N]` (`--disc` needed): Coney boots as usual and goes through QUICK RUMBLE's menus, and when the arena is confirmed the
mode, the arena and the gang size are written over the menu's choice, so the arena's scripts run as the menu would have
set them. Go through the menu by hand (or with a script, as above); whatever you choose there, the match is the one you
named, and the log says `rumble menu: --rumble sets game type ...`.

| `TYPE` | Mode | Game type | Default arena | Default gang size |
| --- | --- | --- | --- | --- |
| `brawl` | Brawl (1 ON 1) | 12 | 102 | 1 |
| `warparty` | Brawl (WAR PARTY) | 14 | 102 | 5 |
| `kinghill` | King of the hill | 2 | 101 | 3 |
| `royal` | Battle royal | 3 | 131 | 3 |
| `survival` | Survival | 9 | 134 | 1 |
| `wchair` | Wheelchair | 24 | 104 | 1 |

`TYPE` may also be the game type's number (a listed number takes that mode's defaults; any other 1 to 99 plays in arena
102 with one a side, which its arena script may not support). `--arena` takes an arena's level number (101 to 137) and
`--gang-size` 1 to 9. A bad `TYPE` prints the valid names and exits with 2. Test mode works as for any run: add
`--headless --frames N --input-script FILE`.

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --skip-movies --rumble kinghill
build/dev/src/platform/coney --disc /path/to/warriors.iso --rumble royal --arena 131 --gang-size 3
```

`--load ENTRY` loads one WAD entry through the reimplemented chunk system and prints a summary of it. `ENTRY` is a
file name such as `level1.lev` (any letter case) or a name hash written `0x` and up to 8 hex digits, such as
`0x7e23a6f2`, for entries whose name is not known. Give `--load` as often as you like; with any `--load`, Coney
opens no window, loads one entry per frame of its fixed timestep and exits when all are done.

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --load level1.lev --load global.pak
```

```text
/path/to/warriors.iso: WARRIORS.DIR lists 10701 entries
level1.lev: entry 6864, hash 0x393f70e2, 46992 bytes
  flat container: 18 chunks, 46688 bytes of chunk data (header says 46688)
    type 0x03 Collision Mesh: 1 chunk, 160 bytes
    ...
  left on the stacks: 18 chunks, 0 objects; 0 trailing bytes
```

The summary gives the container's shape (flat, or grouped for a pack of resources), the number of chunks and their
bytes per [chunk type](../research/chunk-system.md#chunk-type-table), and what the chunk handlers left on the
loader's stacks. It prints counts and sizes only, never the data. An entry that is not a chunk container (Lua
bytecode, text, sound banks) is reported as such. Coney exits with 0 when every entry loaded, 1 when any could not
be found or parsed, and 2 for a bad command line. With `--load`, the two RenderWare texture dictionary chunk types
(`0x0B` and `0x2A`) are read by their stream handlers through librw, on its NULL renderer.

### Viewing texture dictionaries

`--view-txd ENTRY` opens the window and shows every texture of a WAD entry's texture dictionaries, laid out in a grid
that fills the window, each scaled to its cell without changing its shape and drawn with its transparency over a
grey background. `ENTRY` is named as for `--load`. The entry may be a chunk container holding texture dictionary
chunks (a standalone resource, a pack or a level), or a world sector atomics file, whose stream starts with a
(usually empty) dictionary. Coney prints how many dictionaries and textures it found, with each texture's size and
format, then runs until the window closes or `--frames N` is reached.

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --view-txd 863681355 --frames 3 --screenshot ../../scratch/legal.png
```

That entry is the legal screen, the first image the game shows ([Graphics](../research/graphics.md#first-screen)).
The textures are the PS2's palettised formats, converted by librw: palettes are expanded and the PS2's alpha range
(128 is opaque) is scaled to 0-255.

### Viewing sprite sheets

`--view-sheet SHEET` opens the window and shows every rectangle of a sprite sheet
([GUI](../research/gui.md#particle-page)) as a sprite, laid out in a grid over the logical screen, each at its own
shape, over a grey background. The sprites go through the same sprite batch and 2D pass as the game's
([GUI](../research/gui.md#coneys-implementation)), so this is also a check of that path. `SHEET` is a sheet's
resource name, such as `menu_system`, `big_font` or `legal_screen` (Coney looks for the WAD file named by the decimal
CRC-32 of the name), or failing that a WAD entry named as for `--load`. Coney prints the number of rectangles, the
sheet's first glyph (-1 for a sheet that is not a font) and the texture's size.

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --view-sheet menu_system --frames 2 --screenshot ../../scratch/menu.png
```

### Viewing text

`--view-text FONT TEXT` opens the window and lays out and draws one text, as a text widget would
([GUI](../research/gui.md#coneys-implementation)): markup tags such as `<COLOR AA2B2BFF>`, `<SIZE 1.5>`, `<CR>`,
`<CENTER>`, `<BIGFONT>` and the button glyphs `<X>`, `<T>`, `<DU>` all work. `FONT` is the sheet the text starts in:
`part_page0`, the font Coney's text starts in and the one with the button pictures, or `big_font`, which `<BIGFONT>`
always uses. `TEXT` is the text itself (quote it for the shell), or `@` and the id of one of the game's UI strings,
decimal or `0x` hex (`@0x1f` is the menus' usage line). For `@ID` Coney runs the game's string scripts for the
language `--language CODE` gives (`en`, `es`, `fr`, `it` or `de`; English by default) and prints the number of strings
and the length of the one shown, never its text. The text's game time runs with the frames, so `<PULSE>` and
`<DISPLAYTIME>` animate.

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --view-text part_page0 @0x1f --frames 3 --screenshot ../../scratch/usage.png
build/dev/src/platform/coney --disc /path/to/warriors.iso --view-text part_page0 "<BIGFONT>Title</BIGFONT><CR>Press <X>" --frames 3
```

### The world viewer {#the-world-viewer}

`--view-world NAME` loads a level's streamed scenery and lets you fly through it
([The streamed world](../research/world.md#coneys-implementation)). `NAME` is a level, such as `level2`, which loads
both of its worlds (`level2s` and `level2d`), a single world such as `level2s`, or `objarena`. The worlds' layouts and
textures load first, then the parts around the camera before the first frame (the original's preload), and from then
on one part is read or freed per frame as you move. New scenery fades in over a second; the view's far end follows
the nearest scenery not yet loaded, up to the player camera's far clip of 115, with fog in the background colour,
white until a level script sets one. The camera looks through the player camera's lens (65°, near clip 0.1). For a
level, its level file (`<name>.lev`) loads too. Its night sky, turning clouds and skyline are drawn behind the scenery,
and its light glows (the halos round lamps) are drawn over it. Fog still fades the nearest scenery to white, and the
white shows below the horizon once you leave the streets. There are no objects or script yet. Coney prints one line
for each part read or freed and a summary when it stops (counts only).

The camera starts above the middle of the first part and is driven by pad 1. These controls are Coney's own; the
original's cameras follow the player:

| Pad | Keyboard | Does |
| --- | --- | --- |
| left stick | W A S D | fly forward and back along the view, and sideways |
| right stick, d-pad | arrow keys | turn left and right, look up and down |
| R1 / L1 | E / Q | rise / sink straight up and down |
| cross (held) | K or Space | five times as fast |

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --view-world level2
build/dev/src/platform/coney --disc /path/to/warriors.iso --view-world level2 --frames 60 --input-script fly.txt --screenshot ../../scratch/level2.png
build/dev/src/platform/coney --disc /path/to/warriors.iso --view-world level51 --headless --frames 300 --input-script fly.txt
```

With `--headless` the streaming, visibility and draw distance run exactly as in a window and nothing is drawn, which
is how a test drives it. Game time runs on the fixed 1/30 s step, so a script gives the same parts in the same frames
every time.

### The character viewer {#the-character-viewer}

`--view-character [NAME]` loads a character by its model name, as the Character List names it, and plays one of its
clips on the skinned model in place, on the fixed 1/30 s step
([Characters](../research/characters.md#coneys-implementation)). Without a name it shows Rembrandt (`warr_re_cv`).
`--anim CLIP` picks the clip by anim id (a number, such as `408`, Rembrandt's walk) or by name (such as `gen_walk`);
without it the viewer plays id 408, or the character's first clip when 408 is not set. Clips loop. There is no level,
movement or follow camera; the character stands in a slate-blue void, and Coney prints a summary when it stops (counts
only).

The orbit camera looks at the character's middle and is driven by pad 1. These controls are Coney's own; analog
sticks move in proportion to how far they are pushed:

| Pad | Keyboard | Does |
| --- | --- | --- |
| right stick, d-pad | arrow keys | orbit left and right, look from higher or lower |
| left stick (up / down) | W / S | move in and out |
| R1 / L1 | E / Q | move in / out |
| circle | L | play the next clip |
| square | J | play the previous clip |

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --view-character
build/dev/src/platform/coney --disc /path/to/warriors.iso --view-character warr_ty_cv --anim gen_walk
build/dev/src/platform/coney --disc /path/to/warriors.iso --view-character --frames 90 --input-script tests/support/character_orbit.txt --screenshot ../../scratch/rembrandt.png
```

A script drives it like a player, with partial deflections: `stick right 60 0` orbits at 60 % of the full rate,
`stick left 0 35` moves in at 35 %.

### The sandbox {#the-sandbox}

`--sandbox [NAME]` flies a free camera round one of Coney's own test worlds of textured shapes (slopes, stairs,
ledges, fences, gaps), with no disc needed. It is Coney's feature, not the original's; the layouts, their format and
how to add one are in [The sandbox](sandbox.md).

```sh
build/dev/src/platform/coney --sandbox parkour
```

### Playing a level {#playing-a-level}

`--play-level NAME` puts you in a level (`level2`, `level99`: any of the 79 with a streamed world, listed on the debug
menu's [Levels page](debug-menu.md), which also switches between them) as player 1, driven with a gamepad's analog
sticks, with the follow camera behind him ([Characters](../research/characters.md#coneys-implementation),
[Camera](../research/camera.md#coneys-implementation)). The level's worlds and level file load and stream as in [the
world viewer](#the-world-viewer); he stands where the level's own script creates player 1, as when the story reaches the
level: Coney runs the scripts the original runs before it (the preloads, then a fresh Lua state with `SetCheckPoint`),
then `global.lua` and the level's script, and takes the position and heading of its `HuCreate` for player 1, and prints
them (`gameplay: level2 checkpoint 3: player 1 Cleon (type 1, model warr_cl) at ...`). `--checkpoint N` picks the
checkpoint (1 by
default), so `--play-level level2 --checkpoint 3` starts where level2's third checkpoint does. He is drawn as the
character the script's type names (Cleon at `level2 --checkpoint 3`, Snow at `level3 --checkpoint 4`), through the
type's `CfgChar` model ([Characters](../research/characters.md#type-to-model)); a type with no model falls back to
Rembrandt with a log line. The scripts' world flags are kept, so a script that teleports player 1 to a flag puts him
there with the flag's heading, without snapping him to the ground, as the original: the hub, `level95`, starts the
Warchief at one of its doors, chosen with the game's own random numbers (read from your disc's executable; door 5 while
the tutorial is locked, which it always is for now), and an arena run alone (`--play-level level102`) uses the Rumble
menu's default set-up ([Flags](../research/flags.md#coneys-implementation)). The scripts keep running in play, one
script frame per step: their scheduled calls, their volume-box triggers and their message handlers, and every other
human they create is an AI human with its own model, driven by the scripts' goals and gangs
([AI](../research/ai.md#coney)); bindings Coney lacks are skipped with a log line. A level whose script creates no
player 1 puts him above
the middle of its first world's part 1, Coney's stand-in. He walks, runs, sprints, turns and leans into turns, stops,
steps onto kerbs under 25 cm, slides along walls, jumps, climbs fences and walls, and falls off ledges on the level's
collision mesh, with the original's speeds, turn rates and clips: the walk or run start, a blend across walk, jog, run
and sprint, the idle, the run stop, the jump and its landing, and the climbs
([Characters](../research/characters.md#sprint)). He fights as the original's player does
([Combat](../research/combat.md#coneys-implementation)): the square and cross chains, the snaps, the run attack, the
charge and the dive, the block, rage, the grab with its strikes, spins, throws and the mugging, and the tackle, with the
original's commands, timing and clips; the sandbox's fight yard
(`--play-level sandbox:combat`) has passive targets that take the hits. With a target (the one he attacked, or the one
L1 picks) he locks onto it, as the original's default settings do: he faces it and the stick walks him at one speed in
any direction with the combat walk until it is more than 2.5 m away or down. There are no objects yet, and scenes end at
once; a fall out of the world puts him back at the start. Coney prints
a line whenever the clip changes and a summary when it stops (the player's position, speed, gait, clip, traversal state,
stamina, the camera's distance and counts only).

| Pad | Keyboard | Does |
| --- | --- | --- |
| left stick | W A S D (full deflection) | move, relative to the camera: a walk below 95 % of the stick's travel, a run above |
| L2 held | 1 | sprint, with the stick past 95 %, while stamina lasts (135, 6.75 s); let go to refill it |
| triangle | I | climb the fence or wall ahead (within reach, stick pushed); otherwise jump from a run or sprint; in a grab, mug |
| square | J | attack (`S1`; pressed again in the chain window `SS2`, then `SSS3`; with the stick pushed past 95 % to a side or back, a snap; at a run, the run attack); in a grab, strike |
| cross | K or Space | attack on the release (`X1`, then `XX2` or `XS2`); in a grab, strike |
| circle | L | tapped, grab the target in reach; held, tackle; in a grab, throw towards the stick (pushed past 25 %) |
| R1 held | E | block: the stick turns him in place and he does not move; in a grab, R1 pressed spins the hold |
| L1 held | Q | pick the target in front (within 2.5 m): he faces it and the stick walks him round it |
| L1 + R1 | Q + E | start rage, with a full rage meter |
| L2 held, then cross or square | 1, then K or J | the charge or the dive, at a run or sprint |
| right stick | none | turn the camera round him, look up and down |

The stick's direction is turned by the camera's heading, so up always moves away from the camera. The game's own
dead zone (12 %) applies; the walk speed does not depend on how far the stick is pushed, only whether it is pushed
past 95 %. Let go at a walk and he stops at once and settles into the idle; let go (or pull the stick back) at a run
or a sprint and he skids through the run stop. The camera swings round behind him while he walks, runs or sprints
(the original's auto-centre rule), except just after a wall hid him from it, so a stick held to the side runs him in a
circle; it pulls in to 3 m and lowers to 7° over half a second of a sprint and goes back a quarter of a second after
it, and it swings away from a wall beside it.
The jump needs a run (faster than 3.3 m/s) and is refused within 5.5 m of a climbable face, where triangle climbs
or does nothing. In a script: `press l2`, `stick left 0 100`, `tap triangle`. The fights: `tap square` every 6 frames
for the square chain, `tap circle` to grab and `press circle` with a `release circle` 7 or more frames later to
tackle, `press r1` to block (the fight scripts: `tests/support/combat_*.txt`).

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level level99
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level level99 --frames 200 --input-script tests/support/play_walk.txt --screenshot ../../scratch/walk.png
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level level99 --headless --frames 300 --input-script tests/support/play_wall.txt
```

The simulation runs on the fixed 1/30 s step and reads only the pads and game time, so a script gives the same path,
clips and camera every time, in a window or headless. Drawing blends the snapshots of the last two steps (Rembrandt's
feet, heading and pose, the camera's eye and target), so play runs at the same speed and moves smoothly at any frame
rate ([Frame rate](#frame-rate)). Scripts drive it with partial deflections, as a player would: `stick left 0 30` walks
forward at 30 %, `stick left 50 87` runs along an arc to the right, `stick right 60 0` turns the camera at 60 % of its
full rate.

`--play-level sandbox` (or `sandbox:NAME` for another layout) plays a [sandbox](sandbox.md) test world in place of a
level: the same player, follow camera and character, on the sandbox's collision mesh. Only the scenery differs.
Rembrandt starts at the layout's first spawn point, or at the one `--spawn NAME` names. Nothing streams; the far clip
is the layout's fog end. The disc is still needed, for the character.

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level sandbox:parkour --spawn lane
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level sandbox --spawn stairs --frames 120 --input-script tests/support/sandbox_walk_short.txt --screenshot ../../scratch/stairs.png
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level sandbox:combat --frames 30 --input-script tests/support/combat_combo.txt --screenshot ../../scratch/combo.png
```

A layout's `target` lines put passive humans to fight there ([Sandbox](sandbox.md#the-layout-format)); the summary
then adds the fight's counts (hits, damage, power, rage, and the targets' health, reactions, stuns and knockdowns).

#### Tracing {#tracing}

`--trace FILE` (with `--play-level`) writes the player's and the follow camera's state after every step to `FILE`,
one CSV line per step, so a feel or combat comparison with the original ([Feel comparison](../research/feel.md)) can
be repeated with Coney alone. Drive it with an input script in test mode, so the run is the same every time:

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level sandbox:parkour --spawn lane --headless --frames 300 --input-script ../../scratch/run_turn.txt --trace ../../scratch/run_turn.csv
```

The columns, positions in metres (game axes, z up), angles in degrees, speeds in m/s:

| Columns | What |
| --- | --- |
| `step` | the step, from 1 (frame N of an input script is step N + 1) |
| `x`, `y`, `z`, `heading` | the feet and the facing (0 faces +y, anticlockwise) |
| `speed`, `vz` | the speed across the ground and the vertical speed |
| `gait`, `clip`, `traversal` | the stored gait (0 standing to 5 sprint), the anim id playing, and `none`, `jumping`, `landing`, `run stop`, ... |
| `stamina`, `sprinting` | the stamina meter and whether a sprint is asked for |
| `cam_x` ... `wanted_z` | the camera's position, its look-at point and its wanted position |
| `cam_distance`, `cam_pitch`, `cam_yaw` | the camera's distance from its look-at point, its pitch above it and the heading its view faces |
| `band_near`, `target_pitch`, `auto_turn` | the leash band's near edge (the sprint zoom moves it), the target pitch, and the auto-centre rule's turn this step |
| `command`, `health`, `power` | the command matched this step (0 for none; [Combat](../research/combat.md#commands)), the health and the power meter |
| `aim_x`, `aim_y`, `aim_z`, `aim_lag` | the aim point the view faces ([Camera](../research/camera.md#aim-point)) and its lag |

`--trace-humans FILE` (with `--play-level` or `--rumble`) writes **every** human's state after every step to `FILE`, one
CSV row per human per step (`step`, then `human`, the human's number), in a Rumble match as well as in a level. It is
Coney's side of the gang-battle comparison: [`coney-tools trace fight-report`](coney-tools.md#trace) turns the rows
into behaviour statistics per human and per gang and compares them with the original's recording, because the two
games draw different random numbers and a fight cannot be compared update by update. `--trace` stays the player's
trace for `--play-level`, and in a Rumble match (where there is no one level to trace the player of) it means
`--trace-humans`. A Rumble match goes through the menu and the arena's level, so drive the menu with an input
script (the match starts when the arena is confirmed) and give `--frames` for the whole run; the file is opened once
and the rows of every level loaded go into it, numbered by step from the first row.

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --skip-movies --headless --frames 6000 --rumble warparty --arena 102 --gang-size 5 --input-script ../../scratch/rumble_menu.txt --trace-humans ../../scratch/fight.csv
```

Two options make Coney's run match the reference recordings of the original, where every fighter is AI-driven and each
recording draws different random numbers:

- `--ai-player` (with `--rumble`) hands player 1 to the AI when the countdown ends, once the arena script's
  `FinishCountdown` and the rules' `StartIt` have run: the brain his class gives takes him over, his goals and actions
  are cleared and the pad drives no one, as the original's `Human_ReleasePlayer` (`0x0022a2a8`) does to the human it
  takes the pad from ([Rumble: Coney](../research/rumble.md#coney)). The log says `player: player 1 (human N) handed
  to the AI`; his trace rows then have `player` -1 and his class's `btype`. The pad does nothing for the rest of the
  match, and the scripts' hand-over to a team-mate no longer happens, since nobody is a player; nor does the
  game-over check fail the match when he is beaten.
- `--ai-seed N` (with `--rumble` or `--play-level`, 0 to 2147483647) varies the AI's random draws: the game's random
  stream starts N draws in (as each recording of the original starts its index at `0x006eb880` somewhere else), and
  every brain's and gang's own generator is seeded from N. The same N plays the same run; 0, the default, is the
  usual run.

The gang-fight scenario, a 9-a-side Brawl in the Fight Pen (the Furies against the Orphans) played out by the
AI, once per seed (`rumble_menu.txt` drives QUICK RUMBLE and the intro's prompt, as `kQuickRumbleScript` in
`repo:tests/support/disc_play_fixtures.h` does):

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --skip-movies --headless --frames 12000 --rumble 1 --arena 102 --gang-size 9 --ai-player --ai-seed 3 --input-script ../../scratch/rumble_menu.txt --trace-humans ../../scratch/fight_seed3.csv
```

The columns, positions in metres (game axes), angles in degrees, speeds in m/s; those marked (O) are also written by
the original's all-humans recorder under the same name and unit (`research/traces/fields.toml`, `[[humans]]`):

| Columns | What |
| --- | --- |
| `step`, `human` (O) | the step, from 1, and the human's number: the player's brain is 0, the others are numbered as they first appear, and keep the number when another leaves |
| `player`, `btype`, `gang` (O) | the player number a pad drives (0 for player 1, -1 for none), the brain's type (0 player, 1 cop, 2 gang, 3 Warrior, ...) and the gang's id (-1 for none) |
| `x`, `y`, `z`, `heading`, `speed`, `vz`, `gait` (O) | as in the player's trace |
| `clip`, `clip_frame`, `command` (O) | the anim id playing, its frame (30 a second) and the command of the step (0 for none) |
| `health`, `health_max`, `power`, `stamina` (O) | the meters |
| `state` (O) | the record's state word as far as Coney keeps its states: grabbed `0x10` / `0x20` (front, rear), grabbing `0x40` / `0x80`, mounted `0x800`, blocking `0x8000`, wounded `0x10000`, arrested `0x20000`, knocked out `0x40000`, down `0x80000`, stunned `0x100000`; the stance, dying (`0x80000000`) and the other bits stay 0 ([Rumble: Coney](../research/rumble.md#coney)) |
| `phase` (O) | the attack phase, the held flags (`0x1` wind-up, `0x2` chain window, `0x4` end) |
| `down`, `blocking`, `stunned`, `grabbed`, `grabbing`, `attacking` (O) | 1 on the ground, 1 blocking, 1 stunned, 1 held or tackled, 1 holding someone, 1 in an attack phase |
| `lock`, `target` (O) | the number of the human its fighter has locked on, and of its brain's target (-1 for none) |
| `goals`, `goal`, `goal0` ... `goal5`, `react` (O) | the goal stack's depth, its top goal's type id, the stack from the bottom (-1 above the top) and the reaction goal's id ([goal types](../references/goal-types.md)) |
| `actions`, `next_attack`, `attackable`, `warned` (O) | the actions queued, the ms until it may attack and may be attacked again, and the attacks announced on it this step |
| `enemies`, `slots`, `slots_max`, `active` (O) | the enemy list's length, the attackers holding a slot on it, the slots it allows and the active-attacker places held on it |
| `space_up`, `space_down`, `threat` (O) | its spacing bytes (standing and down) and its threat response |
| `fight_updates`, `fight_active` (O) | with the Fight goal on top, the updates it has run and 1 when it holds an active place on its target; -1 with another goal on top |
| `g_alert` (O) | the gang's alert state: always -1, as Coney does not model it ([Gang fights: Coney](../research/gang-fights.md#coney)) |
| `g_tactic` (O) | the gang's tactic as the original's vtable: `0x00543740` TacticConfront, `0x00543320` TacticAttack, `0x00544160` the attack command's; 1 for another tactic (Coney's stand-in), 0 for none |
| `g_leader`, `g_chosen` (O) | the numbers of the gang's leader and chosen target (-1 for none) |
| `g_teamups`, `g_order`, `brain_off` (O) | 1 when the gang's team-ups are allowed (`GangEnableAttackStrategies`), the team-up order running (always -1: Coney builds none) and 1 while the brain is off (`BrDead`) |
| `target_dist`, `action_kind`, `attack_kind`, `slot_held` | Coney's own: metres to the target, the front action (`none`, `attack`, `move`, ...), the attack kind it presses (-1 when it is none) and 1 when it holds a slot on its target |
| `help_calls`, `helpers_sent`, `target_causes` | Coney's own: the help calls the human has made, the HelpRespond goals it has been sent and its target changes by cause (`pick=3;front=1`; empty for none: `pick`, `front`, `hit_back`, `help`, `engage`, `displaced`, `reaction`, `order`, `other`, [Rumble: Coney](../research/rumble.md#coney)) so far, which stand for the original's `help-call`, `send-helper` and `set-target` hook logs |

`--script-trace FILE` (with `--play-level` and a level) writes what the level's scripts do to `FILE`, one line per
call: every script binding they call, with its arguments and what it returned (`HUDSetObjective(0, "...", 0, nil)`,
`HuCreate(...) -> 396`), and every call the engine makes into them by name, marked `>` (`> P1.EnterRiot(74, 398)`, a
box's message, a scene's end). Short lists of numbers (a position) are shown whole, other tables as `{table}`. It is
how a mission's run shows where its scripts wait. The file holds the game's own text (objectives, speech names): keep
it out of the repository.

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level level34 --checkpoint 1 --headless --frames 1800 --script-trace ../../scratch/level34.trace
```

`--start X,Y,Z,HEADING[,DISTANCE,YAW]` (with `--play-level`) moves player 1, after the level's own start, to those
feet (dropped onto the ground below) and heading in degrees, and with the last two puts the follow camera DISTANCE
metres from him with its view facing YAW degrees. It is a test aid: a trace scenario starts Coney where the
original's save state stands, as `walk60` and `run_circle` do from slot 1's first trace row.

`--scene NAME` (with `--play-level`) plays the in-engine scene NAME at once, after any `--start`, as a level99
cinematic: player 1 takes the role named `warrrecv` and the other roles are played by stand-ins drawn from the role's
model (a role whose model Coney does not know is drawn as player 1's), letterboxed and skippable with cross or START
([Scenes](../research/scenes.md)). Any level's scenes play, not only level99's (`--play-level level2 --scene l2_c1`);
the stand-ins are lit like the other humans. It plays on the level's own
scene system, beside the scenes the level's scripts play themselves (level99's intro, `l99_c1`, at checkpoint 1). It
is a test aid:

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level level99 --scene l99_c5 --frames 560 --screenshot ../../scratch/l99_c5.png
```

The original's trace, recorded in PCSX2 by `coney-tools pcsx2 record`, uses the same names and units for the columns
both have, and `coney-tools trace coney` runs a scenario's script on Coney with `--trace`; `coney-tools trace diff`
compares the two, column by column ([Comparing with Coney](research-workflow.md#comparing-with-coney)).

**The story's own set-up.** A level played with `--play-level` is the story's level, started there directly: it runs in
the same game session as the story from the main menu and the debug menus' jumps (`src/platform/game_session.h`,
[Test through the player's path](testing.md#test-through-the-players-path)), and differs only in skipping the
start-up movies, the legal screen, the memory-card check and the menus. So it has the loading screen and the level's
intro movie (`--skip-movies` skips the movie), the pause (START) and the mission screens, the game's sound with the
level's own bank, and the debug menus' Lua console and Cheats page work on its scripts. An `--input-script` starts on
the first frame after the loading screen and the movie (with no movie, the level's first step runs in the screen's last
frame, with the pad at rest), and `--frames` and `--screenshot` count from there too, however long the loading screen
and the movie take (a level that never starts playing ends the run after 10 minutes of frames, with a message).

**Going on to the next level.** A level played with `--play-level` (or from the debug menus) goes on to the next one the
way the story does, because it is the story's: when a mission ends (`HUDLaunchMissionComplete`), the mission-complete
mode runs the scripts' `UnlockAndLoad` (the unlocks, then `runNextMission(1)`, [Story
order](../research/scripting.md#run-next-mission)), and the level flow loads the level and checkpoint it chose; a level
a script starts with `MenuLoadLevel` (the hub's missions) is played the same way. The unlocks, bank and saved script
numbers carry from one level to the next, so the story's order holds: 99, 80, 87, the hub (`level95`), 34 and on.
`--frames` still ends the run.

#### Event logs {#event-log}

`--event-log FILE` writes what a player would notice to `FILE`, one CSV line per event (`step,kind,name,detail`;
`step` is the update, from 1): every script binding called (`call`, with its arguments as `--script-trace` shows
them), every call into the scripts by name (`callback`), each hint the hint box shows (`hint`, its text), each sound
started (`sound`, its hash), and the level's humans other than player 1 appearing, running out of health and being
removed (`human_in`, `human_out`, `human_gone`). It works in any mode, from the front end on; with `--play-level`
it starts when the level's Lua state is made, before its scripts run, with the step counted from there, so the
loading screen's events stay out. The file is flushed every update, so a run that is killed keeps its events. It is
the Coney side of a [differential playthrough](research-workflow.md#differential-playthroughs). Like
`--script-trace` it holds the game's own text: keep it out of the repository.

#### The run log {#run-log}

Everything Coney prints about a run is a line of the [run log](run-log.md), stamped `[step N | mm:ss.s | level
cpK]` with a category: on the console, and in `coney.log` in your data folder (a test-mode run writes a file only
with `--log-file FILE`). `--log-verbose ai,combat` keeps the `detail` lines of those categories too (`all` for every
one). How to read it, attach it to a bug report and add lines: [The run log](run-log.md).

#### The pad pipe {#pad-pipe}

`--pad-pipe` lets a driver program play player 1 by what it sees, in lock step: before each update Coney
writes one line `@obs {json}` to standard output (the update before's observation: `play`, then while a level is in
play the player's feet and heading, the camera's eye and target, the other humans as `[x, y, alive, standing,
enemy]` (standing: not on the ground, so a held or mounted human stands, as the original's state bits say) and the
observed world objects as `[type, x, y, shown]`, with car stereos among them), then reads lines from
standard input until a pad: `observe PREFIX...` chooses the world object types observed from then on, and `pad
BUTTONS RX RY LX LY` (the buttons in hexadecimal, as an input script's masks, then the four stick bytes, 0x80
centred) is the pad of the update. With `--play-level` the first observation is of the level's first update of
play: until then the session loads the level as the story does and the pad rests. With `--event-log` each event is
also written as `@ev <csv line>`; other lines on standard output are Coney's own messages. At the end of standard
input the pad is released. It is test mode (one step per update) and cannot be combined with `--input-script`.
`coney-tools trace mission` drives it.

#### Matched views {#matched-views}

Three options make a frame that can be laid beside a screenshot of the original from the same place:

- `--camera X,Y,Z,QX,QY,QZ,QW[,FOV]` (with `--play-level`) pins player 1's view for the whole run, over the follow,
  scene and free cameras. X,Y,Z is the eye in the game's axes (metres, z up) and QX..QW the orientation quaternion,
  both exactly as the original's camera object holds them at `+0x10` and `+0x20`; FOV is the field of view used, in
  degrees (`+0x48`, 60 when not given). The quaternion turns the camera's +y (the view direction) and +z (up) into the
  world ([The base camera object](../research/camera.md#the-base-camera-object)). The near and far clips are the
  current camera's, as usual, and the scenery streams round the pinned eye.
- `--render-size WxH` draws frames of W × H pixels (`640x448` is the original's frame buffer). The 2D screen fills the
  whole frame and the 3D view keeps the 4:3 shape of a television across it, so a 640 × 448 screenshot lines up pixel
  for pixel with the original's frame buffer. It needs a window.
- `--freeze-world` (with `--play-level`) stops the world after the first step of play: the scripts, people, cars,
  particles and animation stand still, while the fixed step runs on and the scenery still streams in (and fades in)
  round the camera, so every later frame shows the same picture.

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level level99 --checkpoint 1 --skip-movies --freeze-world --camera 75.2,41.1,1.8,0,0,0.7071068,0.7071068,65 --render-size 640x448 --frames 300 --screenshot ../../scratch/l99_view.png
```

`--screenshot` needs a window, so these runs are not `--headless`.

### Shutdown order {#shutdown-order}

`main` destroys things in reverse order of declaration, so whatever points at the sound output must be declared
after it or reset before it. The game session (`session`) is reset explicitly right after the run summaries, before the
sound output goes: its scripts' binding context and play mode hold the game sound, and tearing them down afterwards
read freed state and segfaulted `--play-level level95` at exit. Any new
object that keeps a pointer into the sound output, the scene or the renderer follows the same rule. A ctest with
`PASS_REGULAR_EXPRESSION` ignores the exit code, so `coney.exits_cleanly_level95` and `_level99` (with `CONEY_DISC`
set) check the exit code alone.

### The debug menus {#the-debug-menus}

Every run has Coney's debug menu and, in a window, the developer overlay; [The debug menus](debug-menu.md) has the
chords, the pages and how to add to them. Two options matter here: `--tunables FILE` names the file the tunable
overrides are loaded from and saved to (a windowed run without it uses `coney-tunables.ini` in your config folder, a
headless run none), and `--dev-overlay N` shows the overlay for the first N frames, a test aid
([the overlay](debug-menu.md#the-developer-overlay)).

```sh
build/dev/src/platform/coney --tunables ../../scratch/tunables.ini
build/dev/src/platform/coney --headless --frames 30 --input-script tests/support/debug_menu.txt
build/dev/src/platform/coney --frames 60 --dev-overlay 30 --screenshot ../../scratch/overlay-hidden.png
```

### Reference images {#reference-images}

`--render-references DIR` writes the reference images [the references' images](../references/index.md#images) show
and exits, one folder per list below `DIR` (made if missing): the small reference screenshots of models and the 2D
icons of at most 64 x 64 that `LEGAL.md` allows for the docs.

- **Characters** (`characters/`, 256x256): every Character List record once, standing as the character viewer's first
  frame shows it (its default clip, id 408 or else its first clip, at time 0), textured
  ([Characters](../research/characters.md#coneys-implementation)).
- **Objects** (`objects/`, 256x256): every Object List record once, its model read as the level file's models are,
  stood up and turned to face the camera ([The Object List](../research/level-loading.md#the-object-list)).
- **Cars** (`cars/`, 256x256): the six car types, each its Object List record's 47-atomic model with only the first 26
  atomics drawn, the undamaged car ([Cars](../research/cars.md#model)).
- **Radar icons** (`radar/icon-<n>.png`): rectangle `n` of `part_page0` for every icon id the scripts and the code use,
  at its own size ([GUI: radar](../research/gui.md#radar-icons)).
- **Particle effects** (`particles/`): the first sprite of each of the 58 traced types, scaled to fit 64x64
  ([Particles](../research/particles.md#sprite-words)).

The models are drawn under the character viewer's fixed lights, from the same fixed three-quarter front camera framed
on the model, on a transparent background. The sprites are cut from their sheet's texture, every texel a rectangle's
corners fall in ([Sprite sheet](../research/gui.md#particle-page)), and scaled by area averaging, which repeats each
texel when a 16-texel sprite becomes 64 pixels. Models are drawn offscreen with OpenGL, so it needs a display and a
GPU, but the window stays hidden; `--headless` is refused.

- `--kind KIND` renders only one list: `characters`, `objects`, `cars`, `radar` or `particles`; `all`, the default,
  renders every one.
- `--only NAME` renders just that entry: a model, object, car or particle type name, `icon-<n>` for a radar icon, or a
  `0x` name hash; repeatable.
- `--names FILE` names the character and object images: a text file of model and object type names, one per line (`#`
  starts a comment). An image is named after its name when that name is in the file or given to `--only`, otherwise
  after its name hash (`cbe1bfc3.png`).

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --render-references ../../scratch/refs
build/dev/src/platform/coney --disc /path/to/warriors.iso --render-references ../../scratch/refs --only warr_re_cv --only dyn_bat
build/dev/src/platform/coney --disc /path/to/warriors.iso --render-references ../../scratch/refs --kind objects --names ../../scratch/names.txt
build/dev/src/platform/coney --disc /path/to/warriors.iso --render-references ../../scratch/refs --kind radar --only icon-28
```

To refresh the docs' thumbnails, render with a names file holding the `name` of every entry of
`research/references/character-models.yaml` and `objects.yaml`, copy the named images (and the characters' hash-named
ones, which the character models list uses) and the `cars`, `radar` and `particles` folders into
`docs/references/images/`, run `coney-tools refs compress-images` and then `coney-tools refs extract`
([coney-tools](coney-tools.md#refs)), which links every image that exists, in the hats and inventory lists too.

The images are deterministic: no clock or randomness, a fixed camera and 4x4 supersampling averaged with integer
arithmetic, and sprites scaled with integer arithmetic, so a re-render on the same machine and driver gives
byte-identical files (a different GPU driver may differ by a few model pixel values). Coney prints one line per image
(hashes, file names and sizes only). On the US disc it renders 541 character, 1,399 object, 6 car, 21 radar icon and
58 particle images in one to two minutes. The Character List has 543 records, and one name hash appears three times;
the Object List has 1,406, and one name hash appears twice; the first record's image is kept and the repeats are
reported. The six car models are not objects' models: the objects pass reports them as models of several atomics and
counts them, and the cars pass draws them. Any other record that fails is reported and counted, the batch goes on, and
the exit status is 1.

### Controls {#controls}

In a window, Coney reads the keyboard and any gamepad SDL3 recognises, and turns them into the PS2 pad the game
expects ([Front end](../research/frontend.md#coneys-implementation)). The legal screen ignores input, as in the
original; the menus, [the world viewer](#the-world-viewer), [the character viewer](#the-character-viewer) and
[a played level](#playing-a-level) and [the sandbox](sandbox.md) read port 1.

| PS2 pad | Gamepad (SDL3 names) | Keyboard (port 1) |
| --- | --- | --- |
| d-pad | d-pad | arrow keys |
| left stick | left stick | W A S D (full deflection) |
| right stick | right stick | none |
| cross | south (A on Xbox, cross on PlayStation) | K or Space |
| circle | east | L |
| square | west | J |
| triangle | north | I |
| START | start | Enter |
| SELECT | back | Backspace |
| L1, R1 | left and right shoulder | Q, E |
| L2, R2 | left and right trigger (held from a quarter of the travel) | 1, 3 |
| L3, R3 | stick presses | F, H |
| L3 and R3 together (the debug menu) | both stick presses | F4, or F and H together |
| none (the developer overlay) | none | F1 |

Gamepads can be connected and pulled out at any time. A gamepad that connects takes the first free port, port 1
first, and the log says so (`gamepad: NAME on port 1`); one pulled out frees its port and the other port keeps its
gamepad; a third waits for a free port. The keyboard always plays on port 1, alongside port 1's gamepad, and keeps
playing when it goes. Coney does not pause or show the original's controller-removed screen. On Windows, SDL's
DirectInput support is off: its device scan, which SDL repeats whenever any USB or Bluetooth device comes or goes,
can freeze the window for seconds. Xbox (XInput) gamepads and those SDL drives directly (PlayStation, Switch and
more) do not need it; for an older DirectInput-only gamepad, set the environment variable
`SDL_JOYSTICK_DIRECTINPUT=1` before starting Coney.

Escape quits, unless the developer overlay has the keyboard (a text box in it is being typed in); while it has the
keyboard, the keyboard does not play on port 1. A headless run reads no devices. The last two rows open the debug
menu and the overlay ([Opening it and the controls](debug-menu.md#controls)).

A gamepad's sticks are squared off like a DualShock 2's: a modern stick reports a circle, about 0.71 on each axis at a
full diagonal, which the game's per-axis dead zone turns into a walk; the DualShock 2 reaches both extremes there, so
Coney stretches each stick's circle onto the square and a full diagonal runs as it does on a PS2
([Pad record](../research/frontend.md#pad-record)). Straight pushes and W A S D are unchanged.

### Input scripts {#input-scripts}

`--input-script FILE` replaces the keyboard and gamepads with a script, so a test or a headless run gets the same
input every time without a human. Each line is `FRAME [p1|p2] ACTION ARGS`, where `FRAME` counts from 0 (the frame
the line takes effect on; a script puts Coney in test mode, where a frame is one fixed step); `#` starts
 a comment and blank lines are skipped. Frames must not go down from one line to
the next.

| Action | Meaning |
| --- | --- |
| `press BUTTON...` | hold the buttons from this frame on |
| `release BUTTON...` | let them go from this frame on |
| `tap BUTTON...` | hold them for this frame only: pressed on it, released on the next |
| `stick left X Y`, `stick right X Y` | move a stick; X and Y are whole numbers from -100 to 100, right and up positive |
| `steer [PERCENT] X,Y...` | push the left stick, at PERCENT (1 to 100, default 100) of its reach, toward each world point (metres) in turn as seen from player 1's feet and the camera's heading, recomputed every frame; a point counts as reached within 1 m, and the stick centres at the last one or at the next `stick left` line |
| `connect`, `disconnect` | plug the pad in or pull it out |

Buttons are `cross`, `circle`, `triangle`, `square`, `l1`, `r1`, `l2`, `r2`, `l3`, `r3`, `start`, `select`, `up`,
`down`, `left` and `right`. The port defaults to `p1`; port 1 starts connected and port 2 does not. A held button
reports full pressure. For example:

```text
# Press START on frame 150, then move down the menu and accept.
150 tap start
160 tap down
170 tap cross
200 p2 connect
```

```sh
build/dev/src/platform/coney --headless --frames 300 --input-script ../../scratch/menu.txt
```

A script that cannot be read or has a bad line stops Coney at start-up with exit code 1 and the line's number.
