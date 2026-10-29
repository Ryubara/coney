# Loading and viewing your disc

What `--disc` loads and shows: [loading entries](#loading-entries-from-your-disc) and the start-up from the command line,
the viewers for [texture dictionaries](#viewing-texture-dictionaries), [sprite sheets](#viewing-sprite-sheets),
[text](#viewing-text), [the world](#the-world-viewer) and [the characters](#the-character-viewer), [the
sandbox](#the-sandbox) and the [reference images](#reference-images). The other pages are listed in [the
overview](running.md#pages).

## Loading entries from your disc

`--disc PATH` names your own copy of the game: a mounted disc (`H:\` on Windows, `/mnt/disc` on Linux), a folder
holding `WARRIORS.DIR` and `WARRIORS.WAD`, or an ISO 9660 image of the disc (the same forms `coney-tools wad`
takes, see [Naming the disc](coney-tools-disc.md#naming-the-disc)). Coney reads `WARRIORS.DIR`, checks it
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

**STORY** starts a new game as the original does, through the profile screens: (with two pads, how many players), the
profile manager (CREATE NEW PROFILE when there is none), the name keyboard (cross types the key under the cursor, OK
accepts), the difficulty, the brightness (left and right) and the subtitles. The profile is saved like any other (where:
[Starting Coney](running.md#run-coney)), or kept in memory in a test-mode run without `--profiles`. The menus fade out
and the game's own scripts take it from there (`Menu.startGame`, `runNextMission(1)`, the mission-complete mode,
`UnlockAndLoad`), to `level99` at checkpoint 1. Its level script creates Rembrandt, and Coney loads the level with him
where the script put him, under your control ([Playing a level](running-play-level.md#playing-a-level)), after the
level's loading screen (its pictures, the timed bar and the fades, for about 3.2 s: [Level
loading](../research/level-loading.md#coneys-implementation)), and its intro movie `L99_IN` plays over it (skipped with
`--skip-movies`). At checkpoint 1 the level's script starts with its intro scene `l99_c1`
([Scenes](../research/scenes.md#coneys-implementation)), and the humans it creates are AI humans ([Playing a
level](running-play-level.md#playing-a-level)). The log shows the way: `profile manager: PM_Create` and the other
screens, `profile manager: profile "A" created in slot 0`, `script: Menu.startGame()`, `mission complete: kind 4`,
`level flow: starting level99`, `loading screen: level99 (3 pictures: ...)`,
`gameplay: level99 checkpoint 1: player 1 Rembrandt ...`, then `movie: L99_IN`. The scripted way
(`tests/support/story_new_profile_session.txt`: a one-letter name, the defaults, the cursor moved with the left stick at
70 %; the test harnesses without the memory-card screen play the same presses 220 frames earlier,
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

### Rumble from the command line {#rumble-from-the-command-line}

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

## Viewing texture dictionaries

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

## Viewing sprite sheets

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

## Viewing text

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

## The world viewer {#the-world-viewer}

`--view-world NAME` loads a level's streamed scenery and lets you fly through it
([The streamed world](../research/world.md#coneys-implementation)). `NAME` is a level, such as `level2`, which loads
both of its worlds (`level2s` and `level2d`), a single world such as `level2s`, or `objarena`. The worlds' layouts and
textures load first, then the parts around the camera before the first frame (the original's preload), and from then
on one part is read or freed per frame as you move. New scenery fades in over a second; the view's far end follows
the nearest scenery not yet loaded, up to the player camera's far clip of 115, with fog in the background colour,
white until a level script sets one. The camera looks through the player camera's lens (65°, near clip 0.1). For a
level, its level file (`<name>.lev`) loads too. Its night sky, turning clouds and skyline are drawn behind the scenery,
and its light glows (the halos round lamps) are drawn over it. Fog still fades the nearest scenery to white, and the
white shows below the horizon once you leave the streets. The viewer loads no objects and runs no script; `--play-level`
does both. Coney prints one line for each part read or freed and a summary when it stops (counts only).

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

### World edits {#world-edits}

`--world-edit FILE` takes a level's streamed world apart, with the world viewer or a level's `--play-level`: sectors
drawn apart on a grid, a world hidden, a building cut out of the sector atomics and moved, collision added to a roof.
It is Coney's own debug tool, an experiment for Coney Studio ([How the world splits into
buildings](../research/world-split.md)); nothing faithful changes without it, and the release build has no such option.
The file holds one statement a line, `#` to the end of a line a comment, positions in the game's axes (metres, z up):

| Statement | Does |
| --- | --- |
| `hide s` or `hide d` | that world (`<level>s`, the solid scenery, or `<level>d`, the detail) is not drawn |
| `spread [columns=N] [gap=G] [scale=S \| cell=C] [at=X,Y,Z]` | the world viewer reads every part before its first frame and draws every sector apart: each scaled by `S` (0.25) about its box's centre, or with `cell` each scaled on its own to fit a `C`-metre square (so a small sector is seen beside a large one), standing on a grid of `N` columns (10) from `at` along +x, the rows along -y; a cell is the largest sector's footprint times `S`, or `C`, plus `G` metres (2); the `s` world's sectors first, a row left empty, then the `d` world's; the level's sky, skyline and glows are left out |
| `camera at=X,Y,Z [heading=DEG] [pitch=DEG]` | the world viewer's camera starts there; heading 0 looks along +y and grows towards +x, pitch is positive up |
| `cut NAME box=X0,Y0,Z0,X1,Y1,Z1 [by=centroid\|corners\|pieces] [collision=yes\|no]` | once every sector under the box is loaded, the triangles the box takes are cut out of those sectors' atomics (both worlds) into pieces of their own: by centroid (the default), by all three corners, or whole connected pieces that lie in the box; with `collision=yes` (the default) the level's collision triangles wholly in the box are switched off and added back as extra triangles that move with the pieces |
| `move NAME step=N offset=DX,DY,DZ` | a key of the cut's movement: at fixed step `N` (from the first step of play or of the viewer) it stands `offset` from where the disc has it, straight from key to key |
| `roof box=X0,Y0,Z0,X1,Y1,Z1 [material=ID]` | the `s` world's up-facing faces whose centroid the box holds become collision triangles of that material (5, concrete), once the sectors under it are loaded |
| `debugdraw collision [radius=M]` | the [Debug draw](debug-menu.md#debug-draw) page's collision wireframe on every frame, within `M` metres of the player (8) |

```text
# level99, checkpoint 2: the shop across the street lifted out and moved.
cut shop box=50,11,-1,65,31,20
move shop step=60 offset=0,0,0
move shop step=150 offset=0,0,8
move shop step=240 offset=-12,0,8
```

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --view-world level99 --world-edit spread.txt
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level level99 --checkpoint 2 --skip-movies --freeze-world --camera X,Y,Z,QX,QY,QZ,QW --world-edit shop.txt
```

The cut pieces and the roofs' collision are the editor's, drawn after their world's sectors and lit as they are; the
added collision replaces the debug menus' Spawner's, so the two do not mix. A clip made with `--record` keeps the
collision wireframe only with `--record-debug-draw` ([Recording a clip](running-options.md#record)). Coney prints one
line per cut and roof made, and a summary when it stops (counts only). A part the streamer frees and reads again comes
back as the disc has it, so the editor places its sectors on the spread again and cuts them again by every cut already
made; the pieces it cut first stay, so a building never shows twice.

## The character viewer {#the-character-viewer}

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

## The sandbox {#the-sandbox}

`--sandbox [NAME]` flies a free camera round one of Coney's own test worlds of textured shapes (slopes, stairs,
ledges, fences, gaps), with no disc needed. It is Coney's feature, not the original's; the layouts, their format and
how to add one are in [The sandbox](sandbox.md).

```sh
build/dev/src/platform/coney --sandbox parkour
```

## Reference images {#reference-images}

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
`docs/references/images/`, run `coney-tools refs compress-images` and then `coney-tools refs refresh`
([coney-tools](coney-tools-references.md#refs)), which links every image that exists, in the hats and inventory lists too.

The images are deterministic: no clock or randomness, a fixed camera and 4x4 supersampling averaged with integer
arithmetic, and sprites scaled with integer arithmetic, so a re-render on the same machine and driver gives
byte-identical files (a different GPU driver may differ by a few model pixel values). Coney prints one line per image
(hashes, file names and sizes only). On the US disc it renders 541 character, 1,399 object, 6 car, 21 radar icon and
58 particle images in one to two minutes. The Character List has 543 records, and one name hash appears three times;
the Object List has 1,406, and one name hash appears twice; the first record's image is kept and the repeats are
reported. The six car models are not objects' models: the objects pass reports them as models of several atomics and
counts them, and the cars pass draws them. Any other record that fails is reported and counted, the batch goes on, and
the exit status is 1.
