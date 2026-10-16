# Movies and captions

## Scope and sources

This page records the full-screen movies of both versions, the moments the PS2 version plays them, what the player
sees while one plays, and the captions (subtitles) shown over them: where their text and timing live, how many lines
each movie has and how they look. The disc layout of the movie files is on the PS2 disc layout page
(ps2-disc-layout), the start-up movies on the start-up page (start-up), the chunk container on the chunk container
page (chunk-container) and the level records on the level list page (ref-levels).

Sources: the PS2 disc image "Warriors, The (USA) (En,Fr,De,Es,It).iso" (the `PSS` folder, `SLUS_212.15` and the
`.lev`, `.scn` and `.lua` entries of `WARRIORS.WAD`, located through `WARRIORS.DIR`) and the Xbox disc image
"Warriors, The (USA).iso" (its `bik` folder).

## Movie files

The PS2 disc holds 16 movies in `PSS`, 410,338,076 bytes in all. Every one is a Bink file with the signature `BIKi`,
640 x 448 pixels, whose header size field plus 8 equals the file size. Every audio track is a single stereo track at
48,000 Hz.

| File | Size (bytes) | Frames | Frame rate | Length | Audio |
| --- | ---: | ---: | --- | ---: | --- |
| `L1_IN.BIK` | 58,558,968 | 5,137 | 30/1 | 171.2 s | stereo |
| `L9_IN.BIK` | 34,138,400 | 2,458 | 30/1 | 81.9 s | stereo |
| `L9_OUT.BIK` | 4,933,168 | 363 | 30/1 | 12.1 s | stereo |
| `L31_IN.BIK` | 16,767,680 | 1,172 | 30/1 | 39.1 s | stereo |
| `L31_OUT.BIK` | 19,479,396 | 1,376 | 30/1 | 45.9 s | stereo |
| `L34_OUT.BIK` | 22,788,160 | 1,633 | 30/1 | 54.4 s | stereo |
| `L51_IN.BIK` | 140,089,108 | 18,274 | 2997/100 | 609.7 s | stereo |
| `L52_IN.BIK` | 22,345,988 | 1,567 | 30/1 | 52.2 s | stereo |
| `L54_IN.BIK` | 15,270,360 | 1,066 | 30/1 | 35.5 s | stereo |
| `L81_OUT.BIK` | 11,607,724 | 810 | 30/1 | 27.0 s | stereo |
| `L84_OUT.BIK` | 139,516 | 180 | 30/1 | 6.0 s | none |
| `L87_OUT.BIK` | 12,819,700 | 929 | 30/1 | 31.0 s | stereo |
| `L99_IN.BIK` | 21,897,112 | 2,853 | 2997/100 | 95.2 s | stereo |
| `LOGO.BIK` | 303,228 | 115 | 2997/100 | 3.8 s | stereo |
| `PLOGO.BIK` | 3,560,952 | 360 | 30/1 | 12.0 s | none |
| `TRAILER.BIK` | 25,638,616 | 1,978 | 30/1 | 65.9 s | stereo |

## When each movie plays

Level movies are named after the level's number: `L<number>_IN` is the level's opening movie and `L<number>_OUT` its
closing movie. In the level list (`config_preload3.lua`, see ref-levels), the seven records whose seventh value is
`LT_PLAY` are exactly the seven levels with an `_IN` movie, and the six records whose eighth value is `LT_PLAY` are
exactly the six levels with an `_OUT` movie. Titles below are the English strings stored for each record.

| Movie | Plays | Level record | Story order | English title | Captions |
| --- | --- | --- | ---: | --- | --- |
| `LOGO` | At power-on, not skippable | none | | | none |
| `PLOGO` | At power-on, skippable | none | | | none |
| `L1_IN` | At power-on, skippable; also listed in `level100.lua` | `level1` (opening) | 0 | none | 24 lines |
| `L99_IN` | Start of the level | `level99` (opening) | 1 | New Blood | 6 lines |
| `L87_OUT` | End of the level | `level87` (closing) | 3 | Payback | 8 lines |
| `L34_OUT` | End of the level | `level34` (closing) | 4 | Blackout | 9 lines |
| `L81_OUT` | End of the level | `level81` (closing) | 8 | Encore | 7 lines |
| `L31_IN` | Start of the level | `level31` (opening) | 11 | Boys In Blue | 8 lines |
| `L31_OUT` | End of the level | `level31` (closing) | 11 | Boys In Blue | 4 lines |
| `L9_IN` | Start of the level | `level9` (opening) | 13 | All-City | 12 lines |
| `L9_OUT` | End of the level | `level9` (closing) | 13 | All-City | none |
| `L51_IN` | Start of the level | `level51` (opening) | 14 | Desperate Dudes | 63 lines |
| `L52_IN` | Start of the level | `level52` (opening) | 15 | No Permits, No Parley | 8 lines |
| `L54_IN` | Start of the level | `level54` (opening) | 16 | Home Run | 7 lines |
| `L84_OUT` | End of the level | `level84` (closing) | 18 | Come Out To Play | none |
| `TRAILER` | From the front end's movie list | none | | | none |

- `level100.lua` (archive entry 6,867) holds a list named `movies` with two names, `TRAILER` and `L1_IN`. The
  front-end strings files list a TRAILER item beside CREDITS in all five languages.
- No other movie name appears in any archive entry.
- Both the opening and the closing movie of a level are skippable.

## Playing a movie

| Outcome | Value |
| --- | --- |
| File played | `PSS\<NAME>.BIK`, the movie's name without the extension, for example `L99_IN` |
| Music and sound effects | All stop when the movie starts |
| Screen before and after | Cleared to black |
| Captions | Shown only when the level in memory has caption text and the archive holds a cue scene `<name>_sub.scn` |
| Skipping | A skippable movie ends as soon as any button is pressed on any of the 8 controller slots |
| Caption on skip | The caption on screen disappears at once |
| START during a skip | The press is dropped: ending a movie never opens the pause menu |
| Subtitles option | Dialogue captions show only with Subtitles on (profile offset `0x4E0`, see save-data) |

- With the console set to English a new profile has Subtitles off, so the movies play without captions until the
  player turns the option on.
- `L1_IN` has its captions in both `level1.lev` and `level100.lev`, so it is captioned at power-on and from the
  front end alike.

## Caption text

Caption text is chunk type 0x51 of each `.lev` entry (one per level, 64 in all). 26 levels hold text; in the other
38 the chunk is 16 bytes whose length field reads 2 and the rest zero. The payload starts with a u16 length, counted
from the start of the payload, at which the text ends; the chunk is zero-padded to a multiple of 16 bytes.

| Offset | Size | Type | Meaning |
| --- | ---: | --- | --- |
| `0x00` | 2 | u16 | Length of the text in bytes, including this field |
| `0x02` | varies | records | Records up to that length |

Each record is a u32 tag followed by a zero-ended Latin-1 string.

| Tag | Meaning | Count per language over the 26 levels |
| ---: | --- | ---: |
| 0 | Language name, opening that language's section | 1 per level |
| 1 | Group name, opening a group of lines (one scene or movie) | 210 groups, 209 distinct names |
| 2 | Title card line, such as a place and time; `<CR>` marks a line break | 23 |
| 3 | Dialogue line | 1,192 |

- Every text level has five sections, in the order `ENGLISH`, `GERMAN`, `SPANISH`, `FRENCH`, `ITALIAN`, with the
  same groups in the same order and the same number of lines per group.
- The section shown is the one whose language name matches the game's language.
- The largest chunk is in `level51.lev` (30,128 bytes) and the smallest text chunk in `level84.lev` (7,152 bytes).
- A movie's group is named after the movie in lower case with `_sub` added, such as `l99_in_sub`, and holds only
  dialogue lines. The other groups belong to scenes played in the game engine.

## Movie caption groups

| Movie | Text in (entry) | Group | Lines per language | Cue scene (entry, bytes) | Scene length |
| --- | --- | --- | ---: | --- | ---: |
| `L1_IN` | `level1.lev` (6,864), `level100.lev` (6,866) | `l1_in_sub` | 24 | 4,320, 1,760 | 172.33 s |
| `L9_IN` | `level9.lev` (9,227) | `l9_in_sub` | 12 | 6,839, 1,008 | 82.00 s |
| `L31_IN` | `level31.lev` (7,858) | `l31_in_sub` | 8 | 4,589, 800 | 39.67 s |
| `L31_OUT` | `level31.lev` (7,858) | `l31_out_sub` | 4 | 4,590, 656 | 46.00 s |
| `L34_OUT` | `level34.lev` (7,918) | `l34_out_sub` | 9 | 4,636, 1,520 | 54.33 s |
| `L51_IN` | `level51.lev` (8,029) | `l51_in_sub` | 63 | 5,085, 4,400 | 617.57 s |
| `L52_IN` | `level52.lev` (8,189) | `l52_in_sub` | 8 | 5,250, 1,264 | 52.33 s |
| `L54_IN` | `level54.lev` (8,260) | `l54_in_sub` | 7 | 5,361, 704 | 35.67 s |
| `L81_OUT` | `level81.lev` (8,778) | `l81_out_sub` | 7 | 5,934, 1,440 | 27.00 s |
| `L87_OUT` | `level87.lev` (9,169) | `l87_out_sub` | 8 | 6,432, 736 | 30.83 s |
| `L99_IN` | `level99.lev` (9,605) | `l99_in_sub` | 6 | 6,806, 944 | 95.17 s |

- `L9_OUT`, `L84_OUT`, `LOGO`, `PLOGO` and `TRAILER` have neither a caption group nor a cue scene.
- Two more cue scenes have no movie of their own: `l51_c11_sub` (entry 4,931, 2 show cues, 15.00 s) and
  `l98_c1_sub` (entry 6,766, 4 show cues at the same times as `l31_out_sub`, 46.00 s).

## Caption cues

Each movie's timing is a scene record in the archive entry `<name>_sub.scn`, named by the CRC-32 of
`./ee_files/<name>_sub.scn`. Only these fields matter for captions.

| Offset | Size | Type | Meaning | `l1_in_sub` |
| --- | ---: | --- | --- | --- |
| `0x08` | 24 | string | Scene name, the same as the caption group's name | `l1_in_sub` |
| `0x30` | 32 | string | Part name | `l1_in_xbox2` |
| `0x84` | 4 | u32 | Scene length in 1/30 s | 5,170 |
| `0xAC` | 4 | u32 | Offset of the end of the event list | `0x6DC` |
| `0x138` | 4 | f32 | Scene length in seconds | 172.333 |
| `0x148` | 2 | u16 | Event count | 36 |

The events are the last (count x 24) bytes before the end offset, in time order.

| Event offset | Size | Type | Meaning |
| --- | ---: | --- | --- |
| `+0x00` | 2 | u16 | Time in 1/30 s from the start of the movie |
| `+0x02` | 2 | u16 | Event type: 0x29 for a caption cue |
| `+0x04` | 4 | u32 | Cue kind: 0 shows the group's following line, 4 clears the caption |
| `+0x08` | 16 | | Not used by caption cues |

- In every movie the number of show cues equals the group's line count, so each line is shown exactly once, in
  the order stored.
- A line stays on screen until the following show cue, a clear cue or the end of the movie.
- Three scenes also hold events of type 0x1A, which are not caption cues: 3 in `l34_out_sub`, 5 in `l52_in_sub` and
  3 in `l81_out_sub`.

| Group | Show cues | Clear cues | First line at | Last clear at |
| --- | ---: | ---: | ---: | ---: |
| `l1_in_sub` | 24 | 12 | 29.90 s | 166.67 s |
| `l9_in_sub` | 12 | 3 | 0.83 s | 76.70 s |
| `l31_in_sub` | 8 | 4 | 0.20 s | 36.70 s |
| `l31_out_sub` | 4 | 1 | 16.47 s | 37.13 s |
| `l34_out_sub` | 9 | 0 | 9.50 s | none: the last line stays to the end |
| `l51_in_sub` | 63 | 30 | 0.30 s | 520.33 s |
| `l52_in_sub` | 8 | 4 | 2.10 s | 51.47 s |
| `l54_in_sub` | 7 | 1 | 4.27 s | 34.40 s |
| `l81_out_sub` | 7 | 2 | 0.50 s | 25.67 s |
| `l87_out_sub` | 8 | 2 | 0.47 s | 30.03 s |
| `l99_in_sub` | 6 | 5 | 19.27 s | 78.63 s |

### Example timeline

`L99_IN` (the opening of New Blood) has 11 caption cues. The English group begins with "Can you count, suckers?"
and ends with "Can you dig it?"; in German, Spanish, French and Italian the same six lines are translated.

| Time (1/30 s) | Seconds | Cue | Caption on screen afterwards |
| ---: | ---: | --- | --- |
| 578 | 19.27 | Show | Line 1 |
| 986 | 32.87 | Clear | none |
| 1,031 | 34.37 | Show | Line 2 |
| 1,143 | 38.10 | Clear | none |
| 1,185 | 39.50 | Show | Line 3 |
| 1,486 | 49.53 | Clear | none |
| 1,506 | 50.20 | Show | Line 4 |
| 1,734 | 57.80 | Clear | none |
| 1,812 | 60.40 | Show | Line 5 |
| 2,086 | 69.53 | Show | Line 6 |
| 2,359 | 78.63 | Clear | none |

## Caption look

| Property | Dialogue line (tag 3) | Title card line (tag 2) |
| --- | --- | --- |
| Horizontal place | Centred | Centred |
| Vertical place (centre of the text) | 75 % of the screen height with the default display setting; 76 %, 80 % or 67 % with others | 50 % |
| Text scale | 1.0 | 1.2 |
| Wrap width | 70 % of the screen width | 90 % of the screen width |
| Colour | Not confirmed | Red 0x86, green 0x1A, blue 0x1A (dark red), opaque |
| Time on screen | Set by the cues | 5 s: fades in over 1.5 s, holds, fades out over the last 1.5 s |
| Subtitles option off | Not shown | Still shown |

- Movie captions are dialogue lines only; no movie group holds a title card line.

## Xbox version

The Xbox disc holds 48 movies in `bik`, 1,657,295,376 bytes in all: three files for each PS2 movie, named in lower
case as plain, `_w` and `_hd`. All are `BIKi` files whose header size field plus 8 equals the file size.

| Property | PS2 | Xbox |
| --- | --- | --- |
| Picture size | 640 x 448 | Plain and `_w` 640 x 480; `_hd` 1280 x 720, except `trailer_hd.bik` at 640 x 480 |
| `l51_in` frames | 18,274 at 29.97 (609.7 s) | 18,528 at 30 (617.6 s) plain; 18,530 for `_w` and `_hd` |
| `l99_in` frames | 2,853 at 29.97 (95.2 s) | 2,856 at 30 (95.2 s) |
| `l1_in` frames | 5,137 | 5,137; 5,136 for `l1_in_hd.bik` |
| `plogo` | Silent, 30 frames/s | Stereo 48,000 Hz, 29.97 frames/s |
| `trailer` | 30 frames/s | 29.97 frames/s |
| `logo` | 29.97 frames/s | 29.97 frames/s; 30 frames/s for `logo_w.bik` |
| Level movie audio | Stereo 48,000 Hz | Stereo 44,100 Hz for every level movie except `l1_in` (48,000 Hz) |
| `l84_out` | Silent | Silent in all three files |
| `l51_in` size | 140,089,108 bytes | 259,795,556 bytes (plain) |
| `l99_in` size | 21,897,112 bytes | 40,873,152 bytes (plain) |

- Every other movie has the same frame count and frame rate on both discs.
- `plogo.bik` and `plogo_w.bik` are byte-identical (3,576,940 bytes each).
- The caption cue scene for `L51_IN` lasts 617.57 s, matching the Xbox movie's length rather than the PS2 movie's;
  its last cue (520.33 s) lies well within both.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x0042a938` | Play a full-screen movie with its captions | Playing a movie |
| `0x001cabc0` | Prepare the caption text for the game's language | Caption text |
| `0x001ca950` | Draw the caption on screen | Caption look |

## Acceptance checks

- The PS2 disc's `PSS` folder holds 16 `BIKi` movies of 640 x 448 totalling 410,338,076 bytes; `L84_OUT.BIK` and
  `PLOGO.BIK` have no audio.
- The seven level records with `LT_PLAY` as their seventh value are levels 1, 9, 31, 51, 52, 54 and 99, the same
  numbers as the seven `L<number>_IN` movies; the six with `LT_PLAY` as their eighth value are levels 9, 31, 34, 81,
  84 and 87, the same numbers as the six `L<number>_OUT` movies.
- Starting New Blood from the beginning plays `L99_IN.BIK` before play begins.
- Pressing any button during a level's opening or closing movie ends it at once, without opening the pause menu.
- The game's music and sound effects stop when a movie starts, and the screen is black before and after it.
- With Subtitles on and the language set to English, the first caption of the New Blood opening appears 19.27 s into
  the movie and reads "Can you count, suckers?  I say the future is ours... if you can count."
- With Subtitles off, no captions appear over any movie.
- 26 of the 64 `.lev` entries hold caption text in five languages; the other 38 hold a 16-byte 0x51 chunk.
- Each language section holds 210 groups with 1,192 dialogue lines and 23 title card lines.
- The archive holds 13 `_sub.scn` cue scenes; 11 belong to movies and the show-cue count of each equals its group's
  line count.
- `l51_in_sub` holds 63 lines, the most of any movie, and its cue scene is 4,400 bytes.
- The Xbox disc holds 48 movies in `bik`, with `_hd` files at 1280 x 720 except `trailer_hd.bik` at 640 x 480.
- The Xbox `plogo.bik` and `plogo_w.bik` are byte-identical.

## Open questions

- The colour of dialogue captions: it comes from a value set at run time that was not read.
- Which display settings place dialogue captions at 76 %, 80 % or 67 % of the screen height.
- Whether captions appear exactly at their cue times or up to about 1/6 s late; this was not measured in the game.
- What the type 0x1A events in three cue scenes do (they may set the camera).
- Whether a level's opening movie also plays when the level is restarted or resumed from a later section, and
  whether the closing movie plays only on success or also when the player quits.
- Whether `TRAILER` and the front end's `L1_IN` are skippable, and whether the front end offers `L1_IN` in its movie
  list as well as `TRAILER`.
- When the cue scenes `l51_c11_sub` and `l98_c1_sub`, which have no movie, are used.
- Where the Xbox version keeps its caption text and cues; the Xbox archive volumes were not searched.
- Which part of `L51_IN` is missing from the PS2 movie, which is 7.9 s shorter than the Xbox one.
