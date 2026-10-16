# PS2 start-up

## Scope and sources

This page records what the PS2 version shows and loads between power-on and the front end: the movies and screens,
their lengths, what is read from the disc at start-up, the game's clocks and the list of game mode values. How the
disc is read is on the file access page (file-access); the archive directory is on the archive directory page
(ps2-archive-directory).

Sources: the PS2 disc image "Warriors, The (USA) (En,Fr,De,Es,It).iso" (its `SYSTEM.CNF`, `SLUS_212.15`, the
`MODULES` and `PSS` folders and `WARRIORS.WAD`) and, for comparison, the Xbox image "Warriors, The (USA).iso".

## What the player sees

The rows are in the order the player sees them.

| Screen | Disc source | Length | Skippable |
| --- | --- | --- | --- |
| Black screen while the console starts the program and the IOP drivers load | `SLUS_212.15`, `MODULES` folder | not measured | No |
| Logo movie | `PSS\LOGO.BIK` | 3.84 s (115 frames at 29.97 frames/s) | No |
| Second logo movie, silent | `PSS\PLOGO.BIK` | 12.0 s (360 frames at 30 frames/s) | Yes, any button |
| Opening story movie | `PSS\L1_IN.BIK` | 171.2 s, about 2 min 51 s (5,137 frames at 30 frames/s) | Yes, any button |
| Legal screen | One of 12 textures in `WARRIORS.WAD` | 5 s | No |
| No-controller message, only when no controller is connected | Language file in `WARRIORS.WAD` | Until START is pressed on a connected controller | No |
| Memory card check and autosave messages | Language file in `WARRIORS.WAD` | Depends on the card and the player's answers | No |
| Front end (main menu) | `WARRIORS.WAD` | Until the player picks something | |

- Before each movie, music and sound effects stop and the screen is cleared to black.
- A skippable movie ends as soon as any button is pressed on any connected controller.
- The opening story movie is the scene where the Warriors talk about going to Cyrus's meeting. The archive holds its
  subtitles in English, German, Spanish, French and Italian (entries 6,864 and 6,866); the first English line reads
  "It's still on, and we're going.  Cyrus sent an emissary this afternoon to make sure."
- The legal screen stays up for 5,000 ms counted from the moment it appears; pressing buttons does not shorten it.
  The start-up scripts are read from the archive while it is up.
- The memory card messages come from the same language files as the disc messages: a notice that the memory card
  in MEMORY CARD slot 1 is being checked, and the autosave notice.

## Start-up movies

All 16 PS2 movies are Bink files (signature `BIKi`), 640 x 448, in the `PSS` folder. Three play at start-up:

| File | First sector | Size (bytes) | Frames | Frame rate | Length | Audio tracks |
| --- | ---: | ---: | ---: | --- | --- | ---: |
| `LOGO.BIK` | 2,072,955 | 303,228 | 115 | 2997/100 (29.97) | 3.84 s | 1 (48,000 Hz) |
| `PLOGO.BIK` | 2,073,104 | 3,560,952 | 360 | 30/1 | 12.00 s | 0 |
| `L1_IN.BIK` | 1,886,993 | 58,558,968 | 5,137 | 30/1 | 171.23 s | 1 (48,000 Hz) |

- `TRAILER.BIK` (1,978 frames, 65.9 s) is not part of start-up; the front end lists a TRAILER item under EXTRAS.
- The other 12 movies are story scenes played around levels (names of the form `L<level>_IN` and `L<level>_OUT`).

## Legal screen

The legal screen is a single 512 x 512, 8-bit texture drawn full screen. Each of the 12 versions is its own archive
entry of 263,680 bytes, a resource chunk holding a texture dictionary with one texture.

| Language value | Language | Normal texture (entry) | Texture used with the wide display setting (entry) |
| ---: | --- | --- | --- |
| 0 | English | `legal_screen` (3,480) | `legal_screen_w` (3,031) |
| 1 | Spanish | `legal_screen_sp` (278) | `legal_screen_w_sp` (2,839) |
| 2 | French | `legal_screen_fr` (2,537) | `legal_screen_w_fr` (3,314) |
| 3 | Italian | `legal_screen_it` (1,850) | `legal_screen_w_it` (184) |
| 4 | German | `legal_screen_ge` (165) | `legal_screen_w_ge` (1,868) |

- `legal_screen_euro` (entry 1,763) replaces the English screen when a further display setting is on.
- `legal_screen_euro_w` (entry 3,558) is on the disc but the PS2 game never shows it.
- Each entry is named by the CRC-32 of its texture name written as a decimal number (see the file access page).

## Loaded at start-up

| What | Disc source | Notes |
| --- | --- | --- |
| IOP replacement image | `MODULES\IOPRP300.IMG` (275,345 bytes) | The IOP is restarted with it |
| Serial and controller drivers | `MODULES\SIO2MAN.IRX` (6,641), `MODULES\PADMAN.IRX` (44,869) | |
| Sound library | `MODULES\LIBSD.IRX` (28,661) | |
| Game's IOP sound and streaming driver | `MODULES\IOP.IRX` (143,132) | Performs every archive read and all streamed sound |
| Memory card drivers | `MODULES\MCMAN.IRX` (95,909), `MODULES\MCSERV.IRX` (7,417) | |
| Archive directory | `WARRIORS.DIR` (128,428) | Kept in memory for the whole session |
| Archive | `WARRIORS.WAD` | Opened on stream 0 of the IOP driver |
| Streamed sound | `IOP\BFW.SND` | Opened on sound streams 1 to 12; the driver allows 13 streams |
| Level 1 resources | `WARRIORS.WAD` | Held only while the start-up movies play |
| Legal screen texture | `WARRIORS.WAD` | One of the 12 above, freed once drawn; the picture stays on screen |
| Start-up scripts | `WARRIORS.WAD` | Read while the legal screen is up |

- An IOP driver that fails to load is retried until it loads.
- `SYSTEM.CNF` (57 bytes) names `SLUS_212.15` on the disc as the program, with version 1.03 and video mode NTSC.

## Clocks and frame rate

| Clock | Unit or value |
| --- | --- |
| Base counter | The CPU's cycle counter, 294,912,000 ticks per second |
| Milliseconds | Ticks divided by 294,912 |
| Seconds | Whole seconds: milliseconds divided by 1,000 |
| Game clock step | 1/30 second (9,830,400 ticks) per frame when the game clock runs in fixed steps |
| Game clock speed | 1.0 by default |
| Longest real-time step | 40 ms (11,796,480 ticks); a longer gap between frames counts as 40 ms, except under one camera kind |
| Legal screen hold | 5,000 ms |
| Controller polling during a blocking read | At most once every 67 ms |

- The front end, the legal screen, the memory card screens and modes `0x0c`, `0x0d` and `0x0e` run the game clock
  in fixed 1/30-second steps; the clock returns to real time when the error screen ends.
- All 16 movies run at 30 or 29.97 frames per second.

## Game modes

Each screen the game can be in is a game mode with a fixed value. Modes stack: a mode shown over another (the pause
menu over play, the error screen over anything) returns to the one below when it ends.

| Value | Mode | What the player sees |
| ---: | --- | --- |
| `0x01` | Level play | A level being played |
| `0x05` | Legal screen | The legal screen at start-up |
| `0x06` | Memory card | The memory card check and autosave messages |
| `0x07` | Saving | A save of the current progress, with its message box |
| `0x08` | Front end | The main menu and front-end screens; choosing a level leads to level play |
| `0x0a` | Pause | The pause menu over a stopped game; game sound is paused |
| `0x0b` | Not confirmed | Game sound is paused |
| `0x0c` | Menu over play | A menu over the still-running world; one spoken line, chosen by level, plays once |
| `0x0d` | Menu over play | A menu over the still-running world |
| `0x0e` | Timed hold | The world keeps running for a set time, with no menu |
| `0x0f` | Error | The disc and controller messages (see the file access page) |
| `0x10` | Menu screen | A menu screen using the front-end sound bank |
| `0x11` | Rumble screens | A rumble-mode screen using the front-end sound bank |
| `0x12` | Menu screen | A menu screen using the front-end sound bank |
| `0x13` | Menu screen | A screen using the front-end sound bank |
| `0x14` | Not confirmed | The world keeps running with music stopped |

## Xbox version

- The Xbox movies are in a `bik` folder, each in three versions: plain and `_w` at 640 x 480, `_hd` at 1280 x 720.
- The Xbox `plogo.bik` has an audio track and runs at 29.97 frames per second (360 frames, 12.01 s); the PS2
  `PLOGO.BIK` is silent at 30 frames per second.
- The Xbox `logo.bik` (115 frames, 3.84 s) and `l1_in.bik` (5,137 frames, 171.23 s) match the PS2 lengths.
- The Xbox has no `MODULES` folder, no `SYSTEM.CNF` and no `.SND` files at the disc root; its program is
  `default.xbe` (4,636,672 bytes).

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x001446d0` | Start the game from power-on to the front end | What the player sees |
| `0x00160df8` | Set up the game's systems and open the archive at power-on | Loaded at start-up |
| `0x00148230` | Restart the IOP and load its drivers | Loaded at start-up |
| `0x0042a938` | Play a full-screen movie | Start-up movies |
| `0x00159c08` | Draw the legal screen for the current language | Legal screen |
| `0x00145a10` | Advance the game clock by one frame | Clocks and frame rate |

## Acceptance checks

- The PS2 disc's `PSS` folder holds 16 Bink movies, all 640 x 448.
- `LOGO.BIK` holds 115 frames at 29.97 frames per second (3.84 s); `PLOGO.BIK` holds 360 frames at 30 frames per
  second (12.0 s) and has no audio track; `L1_IN.BIK` holds 5,137 frames at 30 frames per second (171.2 s).
- From power-on the player sees the logo movie, the silent second logo movie, the opening story movie and the legal
  screen, in that order, before the memory card messages and the main menu.
- Pressing a button during the logo movie does not end it; pressing any button during the second logo movie or the
  opening story movie ends that movie at once.
- The legal screen stays up for 5 seconds whatever buttons are pressed.
- With the language set to English the legal screen is the texture `legal_screen` (archive entry 3,480), a 512 x 512
  8-bit image; with German it is `legal_screen_ge` (entry 165).
- The archive holds 12 legal screen textures, each in an entry of 263,680 bytes, and one of them,
  `legal_screen_euro_w`, never appears on the PS2.
- Starting with no controller connected shows the no-controller message after the legal screen; pressing START on a
  newly connected controller clears it.
- The archive holds subtitles for the opening story movie in five languages, the first English line beginning "It's
  still on, and we're going."
- The Xbox disc holds three versions of each movie (plain, `_w`, `_hd`), and its `plogo.bik` has an audio track.

## Open questions

- How long the black screen before the logo movie lasts on real hardware.
- What the two logo movies show (the image content of `LOGO.BIK` and `PLOGO.BIK`).
- Whether the opening story movie shows its subtitles at first start-up, before the player has chosen a subtitle
  setting.
- Which display settings select the `_w` and `euro` legal screens (the names suggest wide screen and European
  versions), and how the language value is chosen on a US console.
- What the player sees in modes `0x0b` and `0x10` to `0x14`, and which in-game events lead to modes `0x0c`, `0x0d`
  and `0x0e` (for example mission failure or game over).
- Whether values 2, 3, 4 and 9 belong to any mode.
- Which camera kind lifts the 40 ms limit on a real-time step.
- The frame rate the PS2 actually displays during play; the game clock steps 1/30 second, but no runtime measurement
  was made.
- Whether a front-end title screen ("press START") comes between the memory card check and the main menu.
