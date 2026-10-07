# Movies (Bink full-motion video)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). The disc check
`coney-tools movies list` ([coney-tools](../guides/coney-tools.md#movies)) read all 16 movies of the NTSC-U disc on
2026-10-06 and prints header values, sizes and a hash only.

## Purpose

What an implementer needs to play the game's movies faithfully: which files there are and what is in their headers,
who plays each one and when, what the player does around a movie (sound, screen, input), how frames reach the screen,
how a movie is skipped, and where the captions shown over a movie come from.

In one paragraph: a movie is a Bink 1 file `PSS/<NAME>.BIK` on the disc, outside the WAD. `PlayMovie(name, skippable)`
blocks its caller until the movie ends: it stops the music and sounds, hands the sound hardware to Bink, blacks the
screen, then decodes frame after frame, copying each one unscaled into the middle of the frame buffer, until the last
frame, a read error, or (when skippable) any button. Captions are not in the movie: the loaded level file's Subtitles
chunk holds their text, and a small scene record `<name>_sub.scn` holds their timing, as caption events on its camera
track that the player advances with the real clock. Afterwards the screen is left faded to black and the sound driver
restored.

## Original structure

`Movie/PlayMovie.cpp` (path string `0x0058bfa8`) and `Movie/BinkMovie.cpp` (`0x0058bcf0`) hold the movie code
(`0x00429a98`-`0x0042af64`); RAD's Bink library is linked in from about `0x004c1cd0` (inferred, from the calls). The
caption system lives at `0x001ca950`-`0x001cb400` ([Boot](boot.md#timers) has its freeze link). Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0042a938` | `Movie_Play(name, skippable)` | everything around one movie | confirmed (code) |
| `0x00429fe8` | `BinkMovie_Play(player, path, skippable, captions)` | the Bink session and the frame loop | confirmed (code) |
| `0x0042a820` | `Movie_CheckSkip` | any button on any pad ends a skippable movie | confirmed (code) |
| `0x0042a718` | `Movie_AdvanceCaptions` | moves the caption scene on by the real time passed | confirmed (code) |
| `0x0042a7f0` | `Movie_DrawCaptions` | draws the caption, then the 2D queue (`0x00185d20`) | confirmed (code) |
| `0x00429b18` | `Movie_BuildUploadPacket` | the GS image transfer of one frame | confirmed (code) |
| `0x00429e20` | (callback) | counts down the frames handed to the GIF (`0x0051545c`) | confirmed (code); its caller is not traced |
| `0x00429e58` | (debug) | Bink's statistics, printed every 32 frames | confirmed (code) |
| `0x00429a98` | (set-up) | Bink set-up at device start ([Graphics](graphics.md#start-up)) | confirmed (code) for the call |
| `0x0036c258` | `PlayMovie` binding | [Scenes and movies bindings](../references/bindings/scene.md) | confirmed (code) |
| `0x001cab90` | chunk `0x51` handler | keeps the Subtitles chunk in `0x0050ea74` | confirmed (code) |
| `0x001cabc0` | `Captions_Init` | resets the captions and picks the language | confirmed (code) |
| `0x001cacc0` | `Captions_SelectLanguage(name)` | finds a language's section | confirmed (code) |
| `0x001cad38` | `Captions_SelectScene(name)` | finds a scene's (or movie's) captions | confirmed (code) |
| `0x001cb190` | `Captions_Next` | makes the next record the current caption | confirmed (code) |
| `0x001cb010` | `Captions_SetKind(kind)` | style and placement for a kind | confirmed (code) |
| `0x001ca950` | `Captions_Draw` | draws the current caption | confirmed (code) |
| `0x0041da30` | `Cfg_SetSubtitles` | the subtitle option, `W_GameState + 0x438` | confirmed (code) |

## The movies {#the-movies}

**Disc check (NTSC-U):** every file is Bink revision **`i`** (signature `BIKi`), **640 × 448** (the logical screen,
[Graphics](graphics.md#video-mode)), video flags 0 (no alpha plane, no scaling flags). Every movie with sound has
**one audio track, id 0: 48,000 Hz, stereo, 16-bit, the DCT transform** (flags `0x7000`), largest decoded packet
145,920 bytes. `PLOGO` and `L84_OUT` have no audio track. Each file has one key frame, its first. Header and
frame-index hash (`movies list`): `3173c726…9dce`.

| Movie | Bytes | Frames | Rate | Seconds | Audio | Played by | Caption scene, records per language |
| --- | ---: | ---: | --- | ---: | --- | --- | --- |
| `LOGO` | 303,228 | 115 | 29.97 | 3.8 | yes | boot, not skippable | none |
| `PLOGO` | 3,560,952 | 360 | 30 | 12.0 | none | boot | none |
| `TRAILER` | 25,638,616 | 1,978 | 30 | 65.9 | yes | front end, EXTRAS | none |
| `L1_IN` | 58,558,968 | 5,137 | 30 | 171.2 | yes | boot; `level1` intro; front end attract | `l1_in_sub`, 24 (in `level1.lev` and `level100.lev`) |
| `L9_IN` | 34,138,400 | 2,458 | 30 | 81.9 | yes | `level9` intro | `l9_in_sub`, 12 |
| `L9_OUT` | 4,933,168 | 363 | 30 | 12.1 | yes | `level9` outro | none |
| `L31_IN` | 16,767,680 | 1,172 | 30 | 39.1 | yes | `level31` intro | `l31_in_sub`, 8 |
| `L31_OUT` | 19,479,396 | 1,376 | 30 | 45.9 | yes | `level31` outro | `l31_out_sub`, 4 |
| `L34_OUT` | 22,788,160 | 1,633 | 30 | 54.4 | yes | `level34` outro | `l34_out_sub`, 9 |
| `L51_IN` | 140,089,108 | 18,274 | 29.97 | 609.7 | yes | `level51` intro | `l51_in_sub`, 63 |
| `L52_IN` | 22,345,988 | 1,567 | 30 | 52.2 | yes | `level52` intro | `l52_in_sub`, 8 |
| `L54_IN` | 15,270,360 | 1,066 | 30 | 35.5 | yes | `level54` intro | `l54_in_sub`, 7 |
| `L81_OUT` | 11,607,724 | 810 | 30 | 27.0 | yes | `level81` outro | `l81_out_sub`, 7 |
| `L84_OUT` | 139,516 | 180 | 30 | 6.0 | none | `level84` outro | none |
| `L87_OUT` | 12,819,700 | 929 | 30 | 31.0 | yes | `level87` outro | `l87_out_sub`, 8 |
| `L99_IN` | 21,897,112 | 2,853 | 29.97 | 95.2 | yes | `level99` intro (the first mission) | `l99_in_sub`, 6 |

"Rate" is the header's `30/1` or `2997/100`. Which level plays which movie is the level table's flags
([Scenes and movies](../references/scenes.md#movie)). Caption records were counted in the English section of each
level's Subtitles chunk; the other four languages have the same counts (checked for `level1`). There is no credits
movie: the credits are drawn by the engine (`Credits.cpp`, [Source map](source-map.md)); inferred, as no other
`PlayMovie` caller exists.

### Who plays them {#callers}

`Movie_Play` has five callers, confirmed (code) at each call:

| Caller | Movie | Skippable | When |
| --- | --- | --- | --- |
| `main` `0x001447cc`, `0x001447dc`, `0x001447ec` | `LOGO`, `PLOGO`, `L1_IN` | 0, 1, 1 | start-up, in that order ([Boot](boot.md#main)) |
| `InitLevel` `0x00160600` | `L<n>_IN`, `n` = record `+0x04` | 1 | the record's flag `0x02`, first section only ([Level loading](level-loading.md#initlevel)) |
| mode 1 `Exit` `0x00158540` | `L<n>_OUT` (format `0x0054ed20`) | 1 | a level change is pending and the record's flag `0x04` is set; after `UnloadLevel`, before the level file is released, so its Subtitles chunk is still loaded ([Level loading](level-loading.md#unload)) |
| `PlayMovie` binding `0x0036c290` | any | the flag, default true | `level100.lua`'s `Menu.playMovie` ([Scripts](scripting.md#scenes-and-movies)) |

On the front end, `Menu.playMovie(2)` (`L1_IN`) is the attract movie after 70 s idle on `PM_Greet`
([Front end](frontend.md#profile-manager)), and **`Menu.playMovie(1)` (`TRAILER`) is the EXTRAS screen's item code 0**:
`PM_Extras` (`0x002074f8`) calls it on confirm with that item selected and no fade running, and plays front-end cue
9 (cue 15 on back). Confirmed (code); the item's label is not traced.

## Around a movie: `Movie_Play` {#movie-play}

`0x0042a938`, in this order, confirmed (code); what the sound commands do is inferred where stated.

1. **Stop the game's sound.** Service the file manager until idle; stop the music (`0x00110548` → `0x0010ddd8`: the
   audio manager's two music slots, `+0x154` and `+0x158`); stop every playing sound (`0x0010fba8` → `0x00112a90`);
   one update of the MultiStream device (slot `+0x10`, `0x0014c140`); send the sound command buffer and wait
   (`0x0014fae8(0)`).
2. **Caption scene**, only when a level's Subtitles chunk is loaded (`0x0050ea74` not null): if the file
   `<name>_sub.scn` (format `0x0058bf90`) exists, allocate its size in the `Level Dynamic & LUA Pool`
   ([Memory](memory.md)), read it at once through the file manager, fix it up as a scene header (`0x00352098`,
   [Scenes](scenes.md#data)), make a scene instance (`WarMoveInstance`, 0x90 bytes, `0x00354c80`), a type-4 scene
   camera (`0x0011e1b0`), bind the instance to the scene's camera track (`0x00356290`), and select the captions by
   the scene's name (`Captions_SelectScene`, [below](#caption-text)). The subtitle option is not read here.
3. **Hand the sound hardware over.** MultiStream's internal fast load off (`0x00151640(0)`, command `0x51`); send
   and wait; commands `0x3b` and `0x3a` (meaning not traced); **reverb off on both SPU2 cores**
   (`SOUND_EnableEffects`, `0x0014fcd8(core, 0, 0, 0, 0, 0)`, named by its error string `0x0054bed0`); audio manager
   `+0x1c4` = 0 (`0x001110e8`, which also calls device slot `+0xc0`); send and wait.
4. **The path** `PSS\<name>.BIK` (format `0x0058bfe0`; the PAL flag chooses between two identical formats).
5. **Black screen**: twice, clear the main camera to opaque black (device slot `+0x40`, `0xff000000`) and show it
   (camera slot `+0x78`), so both display buffers are black; flush the render queue (slot `+0x18`).
6. **Play**: a `BinkMoviePlayer` (0x4c bytes, kept in `0x00515474` meanwhile) runs
   [`BinkMovie_Play`](#player)`(player, path, skippable, captions ? instance : null)`, then is freed.
7. **Give the sound back**: commands `0x2a` (2), `0x2a` (4), `0x3c`; fast load modes 1, 4 and 7
   (`0x00151640`); `0x57`; `0x53` (0) and `0x53` (1); send and wait.
8. **Free the caption scene**: destroy the camera (`0x0011e440(camera, 1)`), the instance and the record.
9. **Black, and kept black**: flush; twice, clear to black and show, each time setting one of the two
   screen-effects managers (`0x005fdeb8`) to a **fade out to black over 0 s** (`0x0018cc60(0 s, manager, out, black,
   0)`), so the screen stays fully black until someone fades in ([Front end](frontend.md#movies)); flush.

`Movie_Play` restarts neither the music nor the reverb nor `+0x1c4`: whoever called it does (inferred; see
[Open questions](#open-questions)). Nothing else runs while it blocks: no game update, task, script or clock
(inferred, from the call graph: the only per-frame work in the loop is below).

## The player: `BinkMovie_Play` {#player}

`0x00429fe8`, confirmed (code); the Bink calls are named from their arguments and strings (inferred).

1. Copy the name into the player (`+0x00`, at most 63 characters), then turn it into **`cdrom0:\PSS\<NAME>.BIK;1`**
   (format `0x0058bd18`): Bink reads the file straight from the disc, not through the WAD
   ([File I/O](file-io.md)).
2. Register the callback `0x00429e20` in an 8-entry callback list (`0x00151ed0`, list `0x005e4448`).
3. **IOP memory**: `0x004cd0f0(1)` = 8,192 bytes, allocated on the IOP (`0x00442570`; printed as "We allocated %d
   bytes from IOP memory").
4. Make the `Sector Pool` current ([Level loading](level-loading.md#memory)) and give Bink its allocator
   (`0x00429f50`, 64-byte aligned, and `0x00429fa8`).
5. **Start Bink's IOP sound module** (`0x004cc370(1, -1, iopMemory, 0x11)`): an IRX image carried inside the
   executable (`0x00592180`, 7,096 bytes) is copied to the IOP by SIF DMA and started. On failure nothing plays.
6. **Volume**: `0x004cd2b8(core, 0x3fff, 0x3fff, 0x6665, 0x6665)` for cores 0 and 1 (IOP command 5). After the movie
   the same call with `0x3fff, 0x3fff, 0x7fff, 0x7fff`. Inferred: the last pair is the output volume, so **a movie
   plays at 80 % of full** (`0x6665` / `0x7fff`); which hardware register each value reaches is not traced.
7. **Sound output**: `0x004c4b10(0x004d0198, 0)`, Bink's set-sound-system call with the IOP output (inferred).
8. **Open** (`0x004c1cd0(path, 0)`): open flags 0, so Bink's defaults and its first audio track, id 0, the only one
   on the disc (inferred). On failure print "BinkOpen failed!" and Bink's error, shut the IOP module down and return:
   the movie is silently skipped.
9. `+0x40` = 1. Set up the cameras (device slot `+0x28`) for the full screen with field of view 60, near 0.05, far
   100,000, then present (slot `+0x30`).
10. **Frame buffer**: width × height × 4 bytes, 128-byte aligned, in the `Sector Pool` (tag `Bink`): 1,146,880 bytes
    for 640 × 448. `InitLevel` keeps 3,200,000 bytes of that pool free for an intro movie
    ([Level loading](level-loading.md#initlevel)). Note the real-time clock (`0x0050b8b8`, milliseconds) in `+0x44`
    and `+0x48`.
11. **The loop**, until it breaks:
    - If Bink says the next frame is due (`0x004c4028` returns 0): decode it (`0x004c3528`) and count it; **when the
      count reaches the frame count, stop** (that last frame is decoded but never shown). Wait for the GIF DMA channel
      to be idle, then copy the frame into the buffer (`0x004c4a88`: uncached address, pitch width × 4, the frame's
      height, at 0, 0, surface type 3, 32-bit). If the copy was done (returns 0; inferred: Bink returns non-zero for a
      frame it skips to catch up), draw it:
      set up the cameras, clear to black (slot `+0x40`), advance the captions ([below](#caption-timing)), flush the
      render queue (slot `+0x18`), send the [frame](#presenting) to the GS, draw the captions and the 2D queue,
      count a frame handed over (`0x0051545c`), and present (slot `+0x30`). Then tell Bink the frame is done
      (`0x004c3ba0`).
    - If Bink's read-error field (`+0x1c`) is set: print "I/O error reading the Bink file!" and stop.
    - Read the pads (`Pads_Update`, [Front end](frontend.md#input)); if the movie is skippable and
      [`Movie_CheckSkip`](#skipping) says so, stop.
12. **End**: Bink's summary (`0x004c43e8`, debug), the volumes back, close (`0x004c3e68`), stop the IOP module
    (`0x004cc738`), free the IOP memory and the frame buffer.

### Presenting a frame {#presenting}

`Movie_BuildUploadPacket` (`0x00429b18`) builds a GS **image transfer** (`BITBLTBUF`, `TRXPOS`, `TRXREG`, `TRXDIR`,
then the pixels in IMAGE packets of at most 32,767 quadwords) of the decoded frame in 32-bit colour (`PSMCT32`) into
the frame buffer, sent on DMA channel 2. Confirmed (code):

- **No scaling.** The frame is written pixel for pixel, at **((screen width − width) / 2, (screen height − height) /
  2)**, its height cut to the screen height. On the default 640 × 448 screen the 640 × 448 movies fill it exactly; in
  480p (640 × 480) they sit centred between two 16-line black bands (the screen was cleared to black). There is no
  letterbox of the game's own: any black bars are in the video.
- **Into the buffer not on show**: the destination base is 0 or width × height / 64 (in the GS's 2 KB pages), chosen
  from which buffer the driver reports (`0x004a4798`) (inferred: double buffering).
- **Colour**: Bink converts its YUV frames to 32-bit RGB on the EE (surface type 3); the GS takes them as they are.
  Interlacing is the screen's own: the movie is a full frame, shown like any game frame
  ([Graphics](graphics.md#video-mode)).

**Timing.** Bink decides when the next frame is due (its wait call) from the movie's rate, 30 or 29.97 a second, and
keeps the picture with the sound; the loop spins between frames. The driver shows a new frame at most every second
vertical blank, 29.97 a second on NTSC ([Graphics](graphics.md#frame-rate)), so a 30-frame movie is shown at the
display's rate with Bink skipping the odd frame to stay with the audio (inferred). The game's fixed 1/30 s step and
its clocks play no part.

**Sound.** The movie's sound never passes through the game's sound engine: Bink decodes it on the EE and its own IOP
module plays it (inferred from steps 5-7), with the game's music and sounds stopped and reverb off.

### Skipping {#skipping}

The second argument of `PlayMovie` is **skippable** (0 = no). `LOGO` is the only movie that cannot be skipped
([callers](#callers)). `Movie_CheckSkip` (`0x0042a820`), confirmed (code):

- Look at the 8 pad records (`0x005dd810`, [Front end](frontend.md#pad-record)) in order; the first whose **current
  button word is not zero** skips: **any button, the d-pad included; the sticks do not count**.
- Then clear the caption (kind 4), call `0x00145670` when `0x006fecc0` is set (not traced), and **remove START
  (`0x0800`) from the current sample** of that record and every later one, so the press that skipped is not seen
  again by the screen that follows.
- It runs on every pass of the loop, after the pads are read; the pads are sampled at most every 6 ms. So a button
  already held when the movie starts skips it at once (inferred, from the check).

## Captions {#captions}

### The text: the level's Subtitles chunk {#caption-text}

The caption text of every scene and movie of a level is chunk **`0x51` Subtitles** of its `.lev` file (number 18 in
[the level file](level-loading.md#the-level-file)); its handler `0x001cab90` keeps a pointer to it in `0x0050ea74`;
releasing the level object resets the captions too (`0x0040c7f0`). Layout, confirmed (code) at `0x001cabc0` and
`0x001cb190`:

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | u16 | `length`; records are read while they start before `length − 1` |
| `+0x02` | records | each a **u32 kind** then a **NUL-terminated string**, packed with no padding |

| Kind | Meaning |
| --- | --- |
| 0 | language marker: the string is `ENGLISH`, `GERMAN`, `SPANISH`, `FRENCH` or `ITALIAN` |
| 1 | scene marker: the string is a scene or caption-scene name (`l1_in_sub`, `l99_c1`) |
| 2 | an emphasised caption ([below](#caption-drawing)) |
| 3 | an ordinary caption |
| 4 | as the current kind, hides the caption (not a record kind in `level1` or `level99`) |
| above 6 | read as 3 |

**Disc check:** `level1.lev`'s chunk is 7,693 bytes: five language sections in that order, each holding
`l1_in_sub` and 24 kind-3 captions. `level99.lev`'s sections hold `l99_in_sub` and ten in-engine scenes; across the
five languages 240 kind-3 and 5 kind-2 captions.

**Selecting.** `Captions_Init` (`0x001cabc0`; at start-up after `level1` is loaded, [Boot](boot.md#main), and from
`InitLevel` through `0x001ad588`) clears the state, makes the text object, and when a chunk is loaded selects the
section of the game state's language (`W_GameState + 0x120`, named by `0x00418ad8`): `Captions_SelectLanguage` finds
the kind-0 record with that name and starts later searches after it. `Captions_SelectScene(name)` reads on from there
to the kind-1 record with the name; then the captions are active and the next show takes the record after it. If
the name is not found everything is left as it was and no captions show.

### The timing: `<movie>_sub.scn` {#caption-timing}

The timing is a scene record in the WAD, `<name>_sub.scn` in lower case (11 of them, the movies with captions above):
a header with **no roles, objects or lights and one camera track** whose events are type 41, caption control
([Scenes](scenes.md#events)): at its frame (30 a second from the movie's start), **`+4` = 0 shows the next record**
(`Captions_Next`), **`+4` = 4 hides the caption** (4 or 5 set that kind; 6 sets a flag, `0x001cb000`, then shows
the next). **Disc check:** the movies' scenes use only 0 and 4; in every one of the 11 the number of show events
equals the language section's caption count; three also carry lens events (type 26), which do nothing visible. Each
scene's length is the movie's to within 9 s (`l1_in_sub` 5,170 frames, `L1_IN` 5,137).

**Advancing** (`Movie_AdvanceCaptions`, `0x0042a718`, called for each frame drawn): when at least **0.166 s of real
time** has passed since the last advance, move the caption scene's camera track on by the real time passed
(`0x003560a8`, which fires the events up to the new frame). So captions follow the real clock in steps of 166 ms or
more, not the movie's frame count. Confirmed (code).

### Drawing {#caption-drawing}

`Captions_Draw` (`0x001ca950`), drawn after the frame each time one is shown, confirmed (code):

- Nothing when the captions are not active, no caption is current, or its kind is 4.
- **The subtitle option**: a kind other than 2 is drawn only when `W_GameState + 0x438` is set. That is the
  subtitles option: `Cfg_SetSubtitles` (binding `CfgSubtitles`) and the `PM_Subtitles` screen (`0x0020cf30`) write
  it, and the game state's reset (`0x00418860`) sets it to **on for every language but English**. So with the
  defaults an English game shows no movie captions.
- **Ordinary caption** (`Captions_SetKind`, `0x001cb010`): centred at x = 0.5 of the screen and **y = 0.75** in the
  default interlaced 4:3 mode; 0.76 with 16:9, 0.8 in 480p, 0.67 on PAL 4:3, PAL 16:9 keeps the last value (initially
  0.75, `0x0050ea7c`); with a player camera, `0x00122d48` adjusts these for its aspect (no player camera exists during
  the boot movies). Text size 1.0, colour **(178, 178, 178, 255)** (`0x005fd310`), wrapped at **0.7** of the screen
  width, the lines centred vertically on y, drawn through the text object of `0x001c1738` with `Font_Draw`
  ([GUI](gui.md#text)), draw flags 4 and 3 and shadow alpha 128.
- **Kind 2**: the screen's centre, size 1.2, colour (134, 26, 26, 255), wrapped at 0.9; it fades in over the first
  1.5 s and out over the last 1.5 s of 5 s, and freezes the game meanwhile ([Boot](boot.md#timers)). No movie uses it.

## Coney's implementation

`movies::MovieMode` (`src/movies/movie_mode.h`) is the player, a game mode that each `Movie_Play` request pushes over
its caller, which waits beneath it until the movie ends: the start-up movies, the attract movie, a level's intro.
FFmpeg's Bink demuxer and its Bink video and audio (DCT) decoders read `PSS/<NAME>.BIK` from the disc
(`src/platform/ffmpeg_movie_decoder.h`, the build: [Building](../guides/building.md#ffmpeg)). As this page says: the
frames unscaled and centred over black, the last frame never shown, every other sound stopped first and the movie's
at 80 % volume, any button but the sticks skipping every movie but `LOGO`, the captions from the level's Subtitles
chunk and `<movie>_sub.scn` in steps of 0.166 s, drawn only with the subtitle option for ordinary ones. Unit tests
over a fake decoder (`tests/movies/`) and the disc check `coney_tests "[disc][movies]"` (`LOGO`: 115 frames, 368,640
16-bit samples).

While a movie plays the game clock stands still, as it does while `Movie_Play` blocks: the movie mode steps 1/30 s
of its own time but advances no game time (`GameMode::stopsGameClock`), so the mode below resumes where it was, its
scripts' delayed calls and scenes included ([Front end: background](frontend.md#background)).

**Coney stand-ins:** the movie's clock is the game's fixed step, not Bink's real-time pacing, so test mode runs the
same every time (a 29.97 movie shows no new frame on about one step in a thousand); YUV to RGB uses the BT.601 studio
matrix; the captions are read from `level<n>.lev` for `L<n>_IN` and `L<n>_OUT` whatever level is loaded, and drawn in
`big_font`; the movie sound plays on the mixer's music bus; the screen-effects fade-out after a movie is not set
(the front end's own fades follow). `--skip-movies` skips every movie, as before Coney played them.

## Open questions {#open-questions}

- Who restarts the music, the reverb and audio manager `+0x1c4` after a movie (an intro's level music was started
  just before it).
- What MultiStream commands `0x2a`, `0x3a`, `0x3b`, `0x3c`, `0x53` and `0x57` do.
- Which hardware volumes the IOP command 5 values set.
- The caption font sheet, and what `Font_Draw`'s flags 4 and 3 select.
- Who calls the callback list `0x005e4448`, and what `0x00145670` does on a skip.
- Runtime check of the 80 % movie volume, the caption placement and the skip rule.
- Which YUV to RGB matrix Bink's EE conversion uses (Coney uses BT.601 studio range).
