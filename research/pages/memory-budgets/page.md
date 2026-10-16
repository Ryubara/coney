# Memory budgets and the resource cache

## Sources

- PS2 disc image (NTSC-U, `SLUS_212.15`): the archive directory `WARRIORS.DIR` (10,701 entries) and the archive
  `WARRIORS.WAD`, read for file sizes and for the world memory manifests.
- `SLUS_212.15` studied read-only for the sizes and percentages below.
- Ten PCSX2 savestates of the PS2 game, all taken in the tutorial level (`level99`, first mission), read for the
  measured sizes. The screenshots stored in the savestates show the player in the opening street and later following
  the dealer objective.
- Sizes are in bytes. "KiB" is 1,024 bytes.

## Console memory

| Item | Value |
| --- | --- |
| Main memory of the PS2 | 33,554,432 bytes (32 MiB) |
| End of the executable image, including its zero-filled data | `0x00715b5c` |
| Start of the system budget | `0x00715b80` |
| End of the system budget | `0x01fed804` |
| System budget size | 26,049,668 bytes |
| Main memory above the system budget, outside every budget | 75,772 bytes |
| Free space left in the system budget during play (all ten savestates) | 329,764 bytes |

The system budget is everything the executable leaves free, and every other budget on this page is carved out of
it. Its size was the same in all ten savestates.

## Budgets

The names below are this page's own. "Shared" budgets hand out and take back pieces of any size; "one-shot"
budgets are filled front to back and are only ever freed as a whole.

| Budget | Kind | Size | Lives in | Holds |
| --- | --- | ---: | --- | --- |
| System | shared | 26,049,668 (measured) | main memory | every other budget, plus buffers set up at boot |
| Filter | shared | 167,936 (`0x29000`), fixed | system | not identified; 61,056 to 61,392 bytes in use |
| Debug | shared | 1,024, fixed | system | nothing on a retail console (0 usable bytes) |
| Level and script | shared | 2,027,520 (`0x1ef000`), fixed | system | mission script state, the per-level world records, small records that track each loaded resource |
| Sector | shared | largest free block of the system budget at boot, less 131,072; measured 17,217,536 | system | all streamed and loaded game data (see below) |
| Spare sector | shared | largest free block left after the sector budget, less 131,072, at least 4,096; measured 4,096 | system | nothing (0 bytes in use in every savestate) |

A shared budget offers 1,024 bytes less than its size: the sector budget offers 17,216,512 of its 17,217,536 bytes,
the level and script budget 2,026,496 of 2,027,520.

The sector budget holds the one-shot budgets in the next section.

## Budget sizes from file sizes

Each piece of loaded data gets a one-shot budget inside the sector budget. Percentages are rounded down to whole
bytes.

| Data | Disc file | Budget size | Example |
| --- | --- | --- | --- |
| Global data | `warriors.glr` (635,488 bytes) | 101 % of the file size | 641,842 |
| Level file | `<level>.lev` | 103 % of the file size, at least 262,144 (256 KiB) | `level99.lev` 553,872 gives 570,488 |
| World stream | `<world>_sec.wld` | the stream budget in the world's manifest | `level99s_sec.wld` 811,848 gives 842,914 |
| World part | `<world>_ms<n>.sec` | part n's budget in the world's manifest | `level99s_ms1.sec` 248,154 gives 276,005 |
| World stream, no manifest | `<world>_sec.wld` | 135 % of the file size, at least 716,800 | only `courtyard_sec.wld` lacks a manifest on the PS2 disc |
| World part, no manifest | `<world>_ms<n>.sec` | 130 % of the file size, at least 262,144 | the parts of `courtyard`, if it has any |
| Shared character header | archive entry `2644698417` (2,126,704 bytes) | exactly the file size | 2,126,704 |
| Other resources (characters, objects, cars, textures, sprite sheets, character data, animations) | one file each | a size given with the request; on the tutorial, most are the file size plus 112 or plus 198 | 5,568-byte file gives 5,680 |
| Opening-movie reserve | none | 3,200,000, held while a level with an opening movie loads | 3,200,000 |

- Once a world part has been loaded, its budget for later loads becomes exactly the bytes it used.
- On the tutorial, 101 resources with a hashed archive name had a budget 112 bytes above their file size, 103 had
  198 bytes above, 9 had 479 above, 4 matched their file size exactly, 2 had 15,334 above and 1 had 134 above.
- The opening-movie reserve is taken from the sector budget only when the level has an opening movie (such as
  `PSS/L99_IN.BIK` for the tutorial) and the player starts the level at its first section; it is given back just
  before the movie plays.

## World memory manifest

Every world has a small manifest file, `<world>_sec.mem`, that records the budgets its stream and parts need. All
values are 32-bit little-endian unsigned integers.

| Offset | Size | Meaning |
| --- | --- | --- |
| `0x00` | 4 | size of the world stream file `<world>_sec.wld` in bytes |
| `0x04` | 4 | budget for the world stream in bytes |
| `0x08` | 4 | part count N |
| `0x0c` | 8 x N | one record per part, parts numbered from 1 |

| Part record offset | Size | Meaning |
| --- | --- | --- |
| `0x00` | 4 | size of the part file `<world>_ms<n>.sec` in bytes |
| `0x04` | 4 | budget for the part in bytes |

- File length is 12 + 8 x N bytes: `level99s_sec.mem` is 172 bytes (20 parts), `level99d_sec.mem` 100 bytes
  (11 parts).
- The PS2 archive holds 159 manifests: 158 for 79 level world pairs (128 for the two worlds of each of the 64
  levels that have a `.lev` file, and 30 for level numbers 70 to 74, 90, 91, 94, 96, 97, 98, 106, 117, 125 and 135,
  which have none), plus `objarena_sec.mem` (20 bytes, 1 part, stream budget 30,036, part budget 73,893).
- The archive holds 160 world streams. The one without a manifest is `courtyard_sec.wld` (246,655 bytes).
- Every level with a `.lev` file has two worlds, named after the level with `s` and `d` appended (`level99s`,
  `level99d`).
- In all 159 manifests, the recorded stream size equals the size of that world's `_sec.wld` entry and every part
  size equals the size of that part's `_ms<n>.sec` entry.
- The 159 manifests list 1,564 parts. Part budgets range from 0.99 to 1.52 times the part file size. The largest
  part budget is 372,229 bytes (a part of `level11s`). Stream budgets range from 13,332 bytes (`level110d`) to
  1,594,404 bytes (`level70s`).
- The manifest budgets are the sizes seen in play: on the tutorial, every loaded part and both streams had exactly
  the manifest's budget and used all of it.

## Measured use on the tutorial

Ten savestates in `level99` (first and last shown). Both worlds were fully resident: all 20 parts of `level99s` and
all 11 parts of `level99d` were loaded in every savestate.

| Contents of the sector budget | Count | Bytes (first) | Count | Bytes (last) |
| --- | ---: | ---: | ---: | ---: |
| World parts | 31 | 6,513,867 | 31 | 6,513,867 |
| Resources with a hashed archive name | 218 | 4,203,092 | 225 | 4,397,107 |
| Shared character header | 1 | 2,126,704 | 1 | 2,126,704 |
| World streams (two worlds) | 2 | 1,128,806 | 2 | 1,128,806 |
| Global data (`warriors.glr`) | 1 | 641,842 | 1 | 641,842 |
| Level file (`level99.lev`) | 1 | 570,488 | 1 | 570,488 |
| Animations with a plain archive name | 34 | 134,560 | 66 | 253,184 |
| Other pieces of the sector budget | | 971,169 | | 1,013,474 |
| Free | | 925,984 | | 571,040 |
| Total offered | | 17,216,512 | | 17,216,512 |

| Other budgets | First | Last |
| --- | ---: | ---: |
| Level and script budget in use | 1,164,928 | 1,262,240 |
| Filter budget in use | 61,056 | 61,392 |
| Spare sector budget in use | 0 | 0 |
| Global data used of its 641,842 | 635,328 | 635,328 |
| Level file used of its 570,488 | 555,200 | 555,200 |

- World data (level file, streams and parts) for the tutorial totals 8,213,161 bytes, 47.7 % of the sector budget.
- Resources loaded on demand grew during play (animations from 34 to 66, other resources from 218 to 225), and the
  free space in the sector budget fell from 925,984 to 571,040 bytes.

## Budgets for every level

Computed from the PS2 disc: the level-file budget, the two world-stream budgets, the number of parts in both
worlds, the sum and maximum of their part budgets, and the total if the whole level were resident at once. The
global data (641,842) and the shared character header (2,126,704) come on top of every level.

| Level | `.lev` bytes | Level-file budget | Stream budgets | Parts | Part budgets | Largest part | Whole level |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| level1 | 46,992 | 262,144 | 45,416 | 2 | 2,202 | 1,141 | 309,762 |
| level2 | 910,416 | 937,728 | 1,131,204 | 40 | 8,057,272 | 290,213 | 10,126,204 |
| level3 | 588,384 | 606,035 | 1,512,118 | 40 | 7,455,464 | 302,837 | 9,573,617 |
| level5 | 799,504 | 823,489 | 1,465,441 | 43 | 9,567,383 | 311,189 | 11,856,313 |
| level9 | 689,712 | 710,403 | 853,896 | 27 | 6,559,495 | 293,637 | 8,123,794 |
| level11 | 702,608 | 723,686 | 1,398,537 | 50 | 10,623,450 | 372,229 | 12,745,673 |
| level14 | 848,896 | 874,362 | 1,111,490 | 30 | 6,659,206 | 296,565 | 8,645,058 |
| level20 | 906,528 | 933,723 | 1,413,576 | 48 | 11,218,048 | 299,989 | 13,565,347 |
| level31 | 691,952 | 712,710 | 1,131,506 | 32 | 7,408,336 | 286,821 | 9,252,552 |
| level34 | 663,056 | 682,947 | 1,393,066 | 33 | 6,257,717 | 301,205 | 8,333,730 |
| level51 | 967,216 | 996,232 | 1,520,123 | 69 | 15,908,393 | 300,693 | 18,424,748 |
| level52 | 833,680 | 858,690 | 1,465,559 | 42 | 9,997,138 | 299,429 | 12,321,387 |
| level54 | 799,664 | 823,653 | 1,658,152 | 63 | 13,106,187 | 298,549 | 15,587,992 |
| level55 | 161,360 | 262,144 | 1,421,752 | 30 | 5,408,662 | 300,821 | 7,092,558 |
| level60 | 213,824 | 262,144 | 1,790,088 | 11 | 2,171,191 | 282,869 | 4,223,423 |
| level61 | 188,064 | 262,144 | 1,618,648 | 15 | 3,236,891 | 284,885 | 5,117,683 |
| level62 | 189,728 | 262,144 | 1,352,472 | 11 | 1,936,295 | 293,893 | 3,550,911 |
| level63 | 538,624 | 554,782 | 1,002,120 | 8 | 1,478,504 | 275,589 | 3,035,406 |
| level64 | 352,752 | 363,334 | 1,348,872 | 13 | 2,588,401 | 279,045 | 4,300,607 |
| level80 | 586,496 | 604,090 | 1,384,945 | 31 | 7,656,587 | 304,181 | 9,645,622 |
| level81 | 714,944 | 736,392 | 1,183,704 | 37 | 5,909,817 | 284,501 | 7,829,913 |
| level82 | 1,205,264 | 1,241,421 | 1,170,362 | 46 | 10,257,206 | 296,053 | 12,668,989 |
| level83 | 1,061,520 | 1,093,365 | 1,469,441 | 79 | 14,779,531 | 296,213 | 17,342,337 |
| level84 | 588,528 | 606,183 | 1,241,144 | 63 | 8,452,459 | 279,125 | 10,299,786 |
| level86 | 762,640 | 785,519 | 1,344,812 | 19 | 4,373,215 | 305,605 | 6,503,546 |
| level87 | 964,672 | 993,612 | 1,108,723 | 35 | 8,348,079 | 289,637 | 10,450,414 |
| level92 | 867,056 | 893,067 | 1,412,987 | 44 | 8,827,740 | 300,437 | 11,133,794 |
| level93 | 702,544 | 723,620 | 1,117,127 | 37 | 7,641,209 | 301,317 | 9,481,956 |
| level95 | 915,152 | 942,606 | 1,448,595 | 49 | 9,794,725 | 296,053 | 12,185,926 |
| level99 | 553,872 | 570,488 | 1,128,806 | 31 | 6,513,867 | 290,485 | 8,213,161 |
| level100 | 31,872 | 262,144 | 110,504 | 20 | 107,204 | 43,797 | 479,852 |
| level101 | 226,448 | 262,144 | 701,672 | 6 | 1,361,566 | 278,405 | 2,325,382 |
| level102 | 170,880 | 262,144 | 751,048 | 6 | 1,405,470 | 283,605 | 2,418,662 |
| level103 | 328,336 | 338,186 | 991,960 | 7 | 1,594,083 | 297,221 | 2,924,229 |
| level104 | 217,488 | 262,144 | 702,360 | 10 | 1,646,754 | 282,261 | 2,611,258 |
| level105 | 277,760 | 286,092 | 215,880 | 2 | 229,114 | 159,173 | 731,086 |
| level107 | 388,672 | 400,332 | 1,010,040 | 6 | 1,492,638 | 270,837 | 2,903,010 |
| level108 | 228,256 | 262,144 | 1,078,104 | 13 | 2,869,729 | 292,133 | 4,209,977 |
| level109 | 479,040 | 493,411 | 1,028,952 | 12 | 2,921,820 | 281,557 | 4,444,183 |
| level110 | 267,536 | 275,562 | 104,408 | 3 | 401,759 | 243,477 | 781,729 |
| level111 | 386,576 | 398,173 | 941,800 | 9 | 1,869,437 | 294,293 | 3,209,410 |
| level112 | 706,304 | 727,493 | 1,368,424 | 21 | 5,306,073 | 301,349 | 7,401,990 |
| level113 | 394,880 | 406,726 | 907,624 | 7 | 1,576,275 | 282,757 | 2,890,625 |
| level114 | 440,112 | 453,315 | 1,023,112 | 12 | 2,674,892 | 293,877 | 4,151,319 |
| level115 | 342,128 | 352,391 | 1,044,136 | 10 | 2,244,706 | 295,813 | 3,641,233 |
| level116 | 309,600 | 318,888 | 213,960 | 2 | 215,210 | 176,565 | 748,058 |
| level118 | 458,976 | 472,745 | 612,568 | 5 | 1,059,577 | 253,285 | 2,144,890 |
| level119 | 472,832 | 487,016 | 863,368 | 29 | 7,056,657 | 285,605 | 8,407,041 |
| level120 | 468,864 | 482,929 | 863,368 | 29 | 7,056,657 | 285,605 | 8,402,954 |
| level121 | 468,624 | 482,682 | 650,520 | 4 | 889,412 | 290,805 | 2,022,614 |
| level122 | 327,104 | 336,917 | 599,928 | 4 | 654,308 | 258,709 | 1,591,153 |
| level123 | 177,856 | 262,144 | 159,976 | 2 | 122,986 | 100,101 | 545,106 |
| level124 | 330,896 | 340,822 | 199,400 | 3 | 390,799 | 271,845 | 931,021 |
| level126 | 416,432 | 428,924 | 1,046,632 | 9 | 1,943,581 | 304,661 | 3,419,137 |
| level127 | 184,064 | 262,144 | 580,264 | 5 | 1,043,897 | 286,229 | 1,886,305 |
| level128 | 243,184 | 262,144 | 645,320 | 6 | 1,106,462 | 297,989 | 2,013,926 |
| level129 | 464,432 | 478,364 | 898,584 | 9 | 1,859,245 | 298,277 | 3,236,193 |
| level130 | 477,792 | 492,125 | 332,168 | 4 | 842,580 | 268,677 | 1,666,873 |
| level131 | 202,880 | 262,144 | 426,824 | 5 | 792,713 | 313,477 | 1,481,681 |
| level132 | 309,552 | 318,838 | 352,440 | 3 | 599,455 | 239,109 | 1,270,733 |
| level133 | 314,416 | 323,848 | 386,984 | 8 | 1,711,992 | 287,397 | 2,422,824 |
| level134 | 484,384 | 498,915 | 935,016 | 9 | 2,102,893 | 290,021 | 3,536,824 |
| level136 | 435,920 | 448,997 | 569,640 | 7 | 1,287,891 | 282,117 | 2,306,528 |
| level137 | 453,024 | 466,614 | 511,528 | 5 | 916,745 | 257,013 | 1,894,887 |

- The sector budget offers 17,216,512 bytes; less the global data and the shared character header, 14,447,966 are
  left for one level. Three levels need more than that to hold their whole world at once: `level51` (18,424,748),
  `level83` (17,342,337) and `level54` (15,587,992). In those levels not every part can be resident together.
- The smallest whole level is `level1` at 309,762 bytes.

## When the sector budget has no room

Every resource, world part and world stream lives in the sector budget. Room for a piece of data means one free
block of the sector budget at least as large as that piece's budget (see "Budget sizes from file sizes"); free
space split over smaller blocks does not count. Times are in milliseconds of the game clock.

### What is not loaded

| Wanted | Room asked for | Outcome without room |
| --- | --- | --- |
| World stream, at level start | the stream budget from the world's manifest | the stream is not loaded and that world has no stream for the level; no level on the PS2 disc comes close (see below) |
| World part, during play | the part's budget from the manifest | the part is not loaded on that frame and stays wanted; it loads on a later frame once room exists |
| Model, character data or animation pack for a character, object or car near the camera | the resource's budget | not loaded on that frame; the character, object or car stays without it and it stays wanted |
| Effect drawn from a sprite sheet | the sheet's budget | the effect is not set up on that frame and stays wanted |
| Resource group inside a resource pack | the group's size plus 16,384 bytes | the game waits up to 200 ms for room; a group with no room after 200 ms is left out and stays on disc |

### What is given up for room

Resources that nothing holds any more stay in memory as a cache. They are the only resources released to make
room, the one left unheld longest goes before the others, and no more of them are released than it takes to leave
a large enough free block.

| Resource kind | Never released to make room while |
| --- | --- |
| Character model | something holds it, or its model is still in use |
| Character data | something holds it |
| Object model, car model | something holds it, its model is still in use, or the current level's dependency group names it |
| Texture set, sprite sheet | something holds it, or the current level's dependency group names it |
| Animation pack | something holds it, or one of its animations is playing |

- Character models, character data and animation packs are not protected by the level's dependency group.
- While a world part or a streamed-in model waits for room, loaded world parts, models and sprite-sheet effects
  are given up farthest from the camera first.
- A released resource is read from the disc again the next time something needs it.

### What is always kept

| Data | Kept |
| --- | --- |
| Global data, shared character header | for the whole session |
| Level file, both world streams | until level exit |
| A world part with any sector visible or in use | as long as any of its sectors is visible or in use |
| The highest-numbered part of each world (part 20 of `level99s`, part 11 of `level99d`) | once loaded, until level exit |
| Any resource that something holds | as long as it is held |
| Object models, car models, texture sets and sprite sheets the level's dependency group names | until level exit |

### What the player sees

- During play a shortage shows no message and no loading indicator, and the game does not pause; play goes on
  with what is loaded.
- A world part that is not loaded has no geometry in memory, so that stretch of the level cannot be drawn until it
  arrives.
- A character, object or car whose model is not loaded has no model to draw until the model arrives.
- A resource group left out of a pack is missing for as long as nothing loads it again.

### Shortage on the disc and in the savestates

- The level file and both world streams together need at most 2,562,806 bytes (`level83`). With the global data,
  the shared character header and the 3,200,000-byte opening-movie reserve also in place, 8,685,160 bytes of the
  sector budget are still unclaimed, so no PS2 level runs short of room for its streams.
- `level51`, `level83` and `level54` need 3,976,782, 2,894,371 and 1,140,026 bytes more than the 14,447,966 left
  for one level to hold every part at once; in those levels distant parts give way to nearer ones as the player
  moves.
- The ten tutorial savestates show no sign of a shortage: every part of both worlds was resident and the sector
  budget kept 571,040 to 925,984 bytes free.

Resources in the ten tutorial savestates that nothing held:

| Count | Range over ten savestates |
| --- | --- |
| Resources that nothing held | 133 to 158 |
| Protected by the level's dependency group | 85 to 102 (57 to 65 object models, 28 to 37 texture sets) |
| Unprotected | 46 to 56 (4 to 5 character models, 13 to 14 texture sets, 0 to 2 character data, 29 to 35 animation packs) |
| Animation packs with an animation playing | 0 |
| Resources the dependency group names | 168: 103 object models and 65 texture sets, nothing of any other kind |

## When memory is released

| Data | Released |
| --- | --- |
| Global data (`warriors.glr`) | never; loaded once at startup and kept for the whole session |
| Shared character header (archive entry `2644698417`) | never; loaded on the first level start and kept across levels |
| World parts | during play, only a part none of whose sectors is visible or in use, the farthest from the camera first; all parts at level exit |
| World streams (both worlds) | at level exit |
| Level file budget | at level exit, as a whole |
| Resources (character models, object models, car models, textures, sprite sheets, character data, animations) | during play, only to make room (see "When the sector budget has no room"); at level exit, every resource that nothing uses any more |
| Mission script state | at level exit; the script system starts again empty for the next level |
| Opening-movie reserve | just before the opening movie plays |

- At level exit the level's dependency group is switched off, so resources only it named are no longer protected.
- The system, filter, debug, level and script, sector and spare sector budgets are created once at boot and keep
  their sizes for the whole session; only their contents change between levels.

## Name hashing

Archive names are CRC-32 hashes; how the archive directory is keyed and searched is on the archive directory page
(`ps2-archive-directory`), and the hashes inside packs are on the chunk container page (`chunk-container`). Facts
not stated there:

| Fact | Value |
| --- | --- |
| Hash | standard CRC-32: reflected polynomial `0xedb88320`, start value all ones, result inverted |
| Resource files | a resource the game requests by name is stored as an archive entry whose name is the CRC-32 of the resource name written as a decimal number |
| Example | the shared character header is archive entry `2644698417` (directory index 1546, 2,126,704 bytes) |
| Case | resource names are hashed as given, with no change of case |
| Level dependency groups | each level's group of needed resources is keyed by the CRC-32 of the level name |
| Example | `level99` hashes to 3,815,064,238 (`0xe36542ae`); that value appears at offset 12 of `level99.lev` and several times in `warriors.glr` |

On the tutorial, 218 to 225 resources were held under hashed names. Five sample hashed names from the tutorial,
with their archive entries: 2410998213 (index 1324, 5,568 bytes), 3356791573 (index 2192, 5,568 bytes), 4118067877
(index 2918, 5,568 bytes), 735416874 (index 3358, 133,648 bytes) and 1557308092 (index 528, 138,512 bytes).

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x00338e10` | Set up the boot-time memory budgets | Budgets |
| `0x0040d688` | Create the sector budgets from free memory | Budgets |
| `0x0040dbb8` | Load a level and its worlds into their budgets | Budget sizes from file sizes |
| `0x00410e08` | Read a world memory manifest | World memory manifest |
| `0x00187d28` | Release cached resources to make room | When the sector budget has no room |
| `0x001607b8` | Release a level's memory at level exit | When memory is released |

## Acceptance checks

- The PS2 archive directory lists 10,701 entries, and `warriors.glr` is 635,488 bytes.
- During play the global-data budget is 641,842 bytes (101 % of `warriors.glr`, rounded down).
- On the tutorial (`level99`), the level-file budget is 570,488 bytes (103 % of the 553,872-byte `level99.lev`).
- Every level's level-file budget is at least 262,144 bytes; `level1`, `level55`, `level100` and 11 others get
  exactly 262,144.
- The PS2 archive holds 159 world manifests listing 1,564 parts; each is 12 + 8 x N bytes long, where N is its part count.
- `level99s_sec.mem` lists 20 parts and a stream budget of 842,914 bytes; `level99d_sec.mem` lists 11 parts and
  a stream budget of 285,892 bytes.
- In every manifest, the recorded stream size equals the `_sec.wld` entry size and each part size equals the
  `_ms<n>.sec` entry size.
- On the tutorial, each loaded world part occupies exactly the budget its manifest gives it (for example 276,005
  bytes for part 1 of `level99s`).
- On the tutorial, all 31 world parts are resident at once, totalling 6,513,867 bytes.
- The sector budget measures 17,217,536 bytes on the tutorial, and the level and script budget 2,027,520 bytes.
- The system budget measures 26,049,668 bytes, from `0x00715b80` to `0x01fed804`.
- The spare sector budget is 4,096 bytes and stays empty during play.
- Archive entry `2644698417` is 2,126,704 bytes, the same as the shared character header's budget in play.
- The CRC-32 of `level99` (3,815,064,238) appears at offset 12 of `level99.lev`.
- The whole of `level51` (18,424,748 bytes of level, stream and part budgets) cannot be resident in the sector budget
  at once.

## Open questions

- Every runtime measurement comes from one level (the tutorial). Other levels, especially `level51`, `level83` and
  `level54`, were not measured in play.
- What the filter budget holds is not identified.
- What the player sees after a shared budget refuses a request (freeze, crash or carrying on) was not observed.
  This includes a resource requested directly by name when no room can be made for it; the sector budget never
  ran short in the tutorial savestates.
- How much farther from the camera a loaded part or model must be than the one waiting before it is given up was
  not pinned down.
- Whether a character, object or car still waiting for its model shows anything at all (for example a stand-in)
  was not observed.
- Which disc files are resource packs with groups, and whether any group is ever left out in play, is not known.
- The 75,772 bytes above the system budget are assumed to be the main program stack; not confirmed.
- Where a resource's requested size comes from, and why most hashed resources get 112 or 198 bytes above their file
  size, is not confirmed.
- Whether `courtyard`, the only world without a manifest, is ever loaded in play (and so gets the fallback
  percentages) is not known.
- The Xbox version's budgets were not studied; the Xbox executable is not in the analysis tools, and whether its
  archive carries the same world manifests is not checked.
- Memory used by scenes (cutscenes) and by sound outside main memory is not covered here.
- When the game keeps the level file loaded through the closing movie, and for how long, is not confirmed.
