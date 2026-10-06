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
track that the player advances with the real clock. Afterwards the screen is blacked again and the sound driver
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

## Coney's implementation

None yet: Coney skips every movie as if it had ended at once ([Front end](frontend.md#coneys-implementation)). A
faithful player needs a Bink 1 (revision `i`) video decoder and the Bink audio DCT decoder; the rest is this page.
