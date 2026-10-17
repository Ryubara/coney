# Level loading

## Sources

- PS2 disc image "Warriors, The (USA) (En,Fr,De,Es,It).iso" (NTSC-U, `SLUS_212.15`): the archive `WARRIORS.WAD`
  through its directory `WARRIORS.DIR` (10,701 entries), and the `PSS` folder of movies.
- `SLUS_212.15`, studied read-only, for the timings, the choice of load-screen images and what a checkpoint changes.
- Ten PCSX2 savestates taken in the tutorial level `level99` (New Blood), read for the level and checkpoint in play.
- The level list (records, names, worlds, numbers, sections) is on the level list page (`ref-levels`). Memory budgets
  and when loaded data is released are on the memory budgets page (`memory-budgets`). Neither is repeated here.
- Sizes are in bytes, times in milliseconds of the game clock.

## Files a level uses

Every name below is an archive entry name, found in `WARRIORS.DIR` by its name hash. "Name" is the level record's
name (for example `level99`); "world" is the record's world name, which equals the name for all 64 records that have
a level file.

| File | Keyed by | On the disc | Use |
| --- | --- | --- | --- |
| `<name>.lev` | name | 64 files | The level file; one per level, read at every level start |
| `<name>.lua` | name | 64 files (with a `.lev`) | The level script |
| `<name>_objs.txt` | name | 63 of the 64 levels (`level100` has none) | The object list, read at every level start |
| `<name>_<k>.pak` | name and checkpoint k | 160 packs over 63 levels | The pack for checkpoint k; at most one is read per level start |
| `<world>s_sec.wld`, `<world>d_sec.wld` | world | 79 of each | The two world streams, held for the whole level |
| `<world>s_sec.mem`, `<world>d_sec.mem` | world | 79 of each | The world memory manifests (layout on the memory budgets page) |
| `<world>s_ms<i>.sec`, `<world>d_ms<i>.sec` | world and part i | 1,910 headed sector files in all | World parts, loaded and released during play |
| `<world>_sec.wld` | world | `courtyard` and `objarena` only | A single world, used when `<world>s_sec.wld` does not exist |
| `<name>_ls_<n>`, `<name>_ls_<n>_w` | name | 158 images for 31 names (30 levels and `level7`) | Load-screen images (see "The load screen") |
| `PSS\L<number>_IN.BIK`, `PSS\L<number>_OUT.BIK` | level number | 7 opening and 6 closing movies | Story movies around a level |

- A level with both `<world>s_sec.wld` and `<world>d_sec.wld` has two worlds; on the PS2 disc every level with a
  `.lev` file has two. A world name with no `s` stream gets one world, `<world>` itself, with its own `_sec.wld` and
  `_sec.mem` (`objarena`: 28,820-byte stream, 1 part; `courtyard`: 246,655-byte stream, no manifest, 4 sector files).
- Records whose world differs from their name (for example `combatselect`, world `level5`) share that world's
  stream, manifest and parts.
- Besides these, every level start reads the global data and the shared character header (memory budgets page) when
  they are not already held, and one sound bank (see "Sound at level start").

## Files per level

All 64 levels with a `.lev` file on the PS2 disc. Packs gives the numbers k of the `<name>_<k>.pak` entries and their
total size. Parts are given as "listed / on disc": the part count in the world's manifest, and the number of
`_ms<i>.sec` files on the disc for that world. Own files is the sum of the `.lev`, `.lua` and `_objs.txt` files, all
packs, both streams, both manifests and the listed parts. Load images counts the distinct normal-display images the
level's own load screen can show (0 means the default image; n/a means the Rumble images are used instead).

| Level | `.lev` bytes | `.lua` bytes | `_objs.txt` bytes | Packs k (bytes) | `s` stream bytes | `s` parts listed / on disc | `d` stream bytes | `d` parts listed / on disc | Own files, bytes | Load images | Movies |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `level1` | 46,992 | 448 | 3 | 1 (220,624) | 26,800 | 1 / 1 | 18,112 | 1 / 1 | 315,095 | 0 | `L1_IN` |
| `level2` | 910,416 | 134,521 | 53,198 | 1, 2, 3, 4 (12,742,944) | 811,128 | 24 / 24 | 275,956 | 16 / 16 | 21,869,471 | 3 | none |
| `level3` | 588,384 | 170,819 | 13,758 | 1, 2, 3, 4, 5 (19,491,824) | 1,154,234 | 30 / 31 | 283,156 | 10 / 11 | 28,160,413 | 3 | none |
| `level5` | 799,504 | 161,204 | 69,311 | 1, 2, 3, 4 (15,148,384) | 1,098,068 | 23 / 28 | 297,416 | 20 / 37 | 25,871,575 | 3 | none |
| `level9` | 689,712 | 132,548 | 34,836 | 1, 2, 3 (7,414,784) | 553,380 | 22 / 48 | 268,772 | 5 / 7 | 15,009,568 | 3 | `L9_IN`, `L9_OUT` |
| `level11` | 702,608 | 170,160 | 49,907 | 1, 2, 3, 4, 5, 6 (20,348,800) | 1,080,879 | 30 / 35 | 269,740 | 20 / 20 | 32,059,774 | 3 | none |
| `level14` | 848,896 | 153,175 | 50,797 | 1, 2, 3 (9,821,200) | 807,126 | 21 / 22 | 269,756 | 9 / 10 | 17,695,118 | 3 | none |
| `level20` | 906,528 | 187,113 | 67,361 | 1, 2, 3, 4, 5, 6 (21,882,848) | 1,081,816 | 36 / 54 | 278,056 | 12 / 18 | 34,188,588 | 3 | none |
| `level31` | 691,952 | 140,376 | 56,536 | 1, 2, 3, 4, 5, 6 (22,861,360) | 801,358 | 20 / 27 | 284,636 | 12 / 13 | 31,263,034 | 3 | `L31_IN`, `L31_OUT` |
| `level34` | 663,056 | 113,064 | 44,163 | 1, 2, 3, 4, 5 (17,820,848) | 1,085,436 | 23 / 24 | 265,808 | 10 / 11 | 25,528,437 | 3 | `L34_OUT` |
| `level51` | 967,216 | 167,192 | 109,512 | 1, 2, 3, 4, 5, 6, 7 (23,323,552) | 1,127,936 | 39 / 88 | 306,644 | 30 / 50 | 40,106,366 | 3 | `L51_IN` |
| `level52` | 833,680 | 182,309 | 26,126 | 1, 2, 3, 4 (14,809,504) | 1,117,420 | 26 / 29 | 283,492 | 16 / 26 | 26,022,269 | 3 | `L52_IN` |
| `level54` | 799,664 | 171,812 | 29,632 | 1, 2, 3, 4, 5 (13,546,704) | 1,214,140 | 35 / 47 | 300,496 | 28 / 30 | 27,847,838 | 3 | `L54_IN` |
| `level55` | 161,360 | 80,178 | 10,875 | 1, 2, 3 (11,898,976) | 1,095,196 | 19 / 24 | 271,504 | 11 / 11 | 18,459,853 | 3 | none |
| `level60` | 213,824 | 46,855 | 3 | 1, 3 (6,525,008) | 1,233,152 | 7 / 8 | 517,868 | 4 / 5 | 10,436,922 | 1 | none |
| `level61` | 188,064 | 56,181 | 3 | 1 (3,868,064) | 1,316,524 | 10 / 14 | 267,664 | 5 / 6 | 8,572,770 | 1 | none |
| `level62` | 189,728 | 47,106 | 3 | 1 (4,629,488) | 1,062,240 | 7 / 8 | 258,168 | 4 / 4 | 7,871,467 | 1 | none |
| `level63` | 538,624 | 39,852 | 3 | 1 (2,146,016) | 723,600 | 5 / 9 | 254,880 | 3 / 5 | 5,019,877 | 1 | none |
| `level64` | 352,752 | 55,388 | 3 | 1, 3, 4 (11,012,480) | 1,049,612 | 8 / 9 | 269,336 | 5 / 6 | 15,046,261 | 1 | none |
| `level80` | 586,496 | 93,870 | 26,453 | 1, 2, 3, 4 (12,279,792) | 1,063,451 | 19 / 23 | 278,940 | 12 / 12 | 21,021,450 | 3 | none |
| `level81` | 714,944 | 85,086 | 23,861 | 1, 2, 3, 4, 5 (13,698,992) | 845,680 | 27 / 33 | 274,316 | 10 / 12 | 20,985,563 | 3 | `L81_OUT` |
| `level82` | 1,205,264 | 187,495 | 93,930 | 1, 2, 3, 4 (11,911,856) | 825,154 | 29 / 36 | 291,940 | 17 / 19 | 23,533,147 | 3 | none |
| `level83` | 1,061,520 | 154,001 | 48,958 | 1, 2, 3, 4, 5, 6 (18,373,488) | 1,104,452 | 46 / 61 | 303,164 | 33 / 33 | 34,120,359 | 3 | none |
| `level84` | 588,528 | 63,868 | 5,542 | 1, 2, 3, 4 (13,136,432) | 871,416 | 46 / 50 | 281,532 | 17 / 21 | 22,624,756 | 3 | `L84_OUT` |
| `level86` | 762,640 | 179,846 | 30,365 | 1, 2, 3, 4 (14,374,704) | 1,051,788 | 14 / 26 | 259,300 | 5 / 11 | 20,375,411 | 3 | none |
| `level87` | 964,672 | 187,837 | 77,127 | 1, 2, 3, 4, 5 (14,075,120) | 793,823 | 24 / 27 | 276,424 | 11 / 13 | 23,532,567 | 3 | `L87_OUT` |
| `level92` | 867,056 | 136,768 | 31,182 | 1, 2, 3 (8,819,296) | 1,089,181 | 30 / 33 | 278,396 | 14 / 17 | 18,989,457 | 3 | none |
| `level93` | 702,544 | 155,362 | 37,041 | 1, 2, 3, 4, 5, 6 (18,668,784) | 804,528 | 23 / 29 | 270,528 | 14 / 14 | 27,242,485 | 3 | none |
| `level95` | 915,152 | 288,407 | 29,383 | 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 (47,011,408) | 1,102,042 | 32 / 36 | 286,748 | 17 / 20 | 58,176,540 | 3 | none |
| `level99` | 553,872 | 108,385 | 11,954 | 1, 2, 3 (7,713,520) | 811,848 | 20 / 27 | 274,856 | 11 / 11 | 15,186,981 | 3 | `L99_IN` |
| `level100` | 31,872 | 10,007 | none | 1, 2 (638,176) | 50,756 | 19 / 20 | 30,056 | 1 / 2 | 856,751 | 0 | none |
| `level101` | 226,448 | 51,476 | 3 | 1 (307,616) | 494,044 | 4 / 7 | 190,816 | 2 / 2 | 2,475,839 | n/a | none |
| `level102` | 170,880 | 50,785 | 1,925 | 1 (1,008,576) | 474,088 | 3 / 4 | 257,736 | 3 / 3 | 3,177,594 | n/a | none |
| `level103` | 328,336 | 44,420 | 113 | 1 (470,032) | 710,368 | 5 / 7 | 257,796 | 2 / 2 | 3,175,599 | n/a | none |
| `level104` | 217,488 | 49,956 | 3 | 1 (382,640) | 413,756 | 5 / 7 | 264,276 | 5 / 8 | 2,786,565 | n/a | none |
| `level105` | 277,760 | 42,163 | 11,238 | 1 (518,192) | 159,720 | 1 / 1 | 51,272 | 1 / 1 | 1,261,859 | n/a | none |
| `level107` | 388,672 | 45,526 | 2,662 | 1 (813,472) | 787,800 | 5 / 5 | 204,476 | 1 / 1 | 3,560,156 | n/a | none |
| `level108` | 228,256 | 52,227 | 3 | 1 (343,440) | 778,096 | 8 / 8 | 269,836 | 5 / 5 | 4,173,592 | n/a | none |
| `level109` | 479,040 | 50,042 | 5,015 | 1 (488,384) | 736,060 | 9 / 9 | 263,720 | 3 / 3 | 4,588,005 | n/a | none |
| `level110` | 267,536 | 45,898 | 6,317 | 1 (889,616) | 86,588 | 2 / 2 | 12,948 | 1 / 1 | 1,669,889 | n/a | none |
| `level111` | 386,576 | 59,727 | 6,008 | 1 (1,032,864) | 668,100 | 6 / 7 | 250,488 | 3 / 3 | 3,997,473 | n/a | none |
| `level112` | 706,304 | 92,468 | 4,905 | none (0) | 1,056,084 | 15 / 15 | 268,032 | 6 / 6 | 6,652,635 | n/a | none |
| `level113` | 394,880 | 47,380 | 3 | 1 (335,328) | 616,952 | 4 / 4 | 265,956 | 3 / 3 | 3,018,667 | n/a | none |
| `level114` | 440,112 | 53,731 | 19,020 | 1 (825,872) | 788,220 | 10 / 11 | 208,556 | 2 / 3 | 4,632,789 | n/a | none |
| `level115` | 342,128 | 53,942 | 3 | 1 (644,528) | 741,720 | 6 / 6 | 270,132 | 4 / 4 | 3,961,763 | n/a | none |
| `level116` | 309,600 | 42,941 | 3 | 1 (333,008) | 117,344 | 1 / 3 | 91,676 | 1 / 1 | 1,085,064 | n/a | none |
| `level118` | 458,976 | 46,158 | 3 | 1 (355,536) | 468,940 | 4 / 4 | 128,004 | 1 / 1 | 2,393,103 | n/a | none |
| `level119` | 472,832 | 46,641 | 3 | 1 (335,648) | 530,872 | 19 / 19 | 290,812 | 10 / 10 | 8,012,886 | n/a | none |
| `level120` | 468,864 | 53,667 | 3 | 1 (385,888) | 530,872 | 19 / 19 | 290,812 | 10 / 10 | 8,066,184 | n/a | none |
| `level121` | 468,624 | 44,771 | 5,738 | 1 (634,512) | 408,860 | 2 / 2 | 226,836 | 2 / 2 | 2,544,217 | n/a | none |
| `level122` | 327,104 | 44,060 | 3 | 1 (442,512) | 367,208 | 3 / 3 | 220,292 | 1 / 1 | 1,955,635 | n/a | none |
| `level123` | 177,856 | 41,185 | 5,132 | 1 (508,864) | 134,472 | 1 / 1 | 23,348 | 1 / 1 | 1,007,107 | n/a | none |
| `level124` | 330,896 | 42,765 | 8,517 | 1 (515,008) | 135,412 | 2 / 2 | 58,020 | 1 / 1 | 1,440,316 | n/a | none |
| `level126` | 416,432 | 45,500 | 3,468 | 1 (486,064) | 755,760 | 7 / 11 | 261,628 | 2 / 4 | 3,653,384 | n/a | none |
| `level127` | 184,064 | 50,821 | 3 | 1 (354,496) | 456,524 | 3 / 3 | 98,704 | 2 / 2 | 2,060,934 | n/a | none |
| `level128` | 243,184 | 53,213 | 3 | 1 (488,304) | 396,944 | 4 / 4 | 231,468 | 2 / 2 | 2,359,756 | n/a | none |
| `level129` | 464,432 | 54,862 | 3 | 1 (299,904) | 635,228 | 7 / 7 | 238,724 | 2 / 2 | 3,303,539 | n/a | none |
| `level130` | 477,792 | 51,333 | 3 | 1 (325,376) | 117,788 | 2 / 2 | 204,748 | 2 / 2 | 1,923,496 | n/a | none |
| `level131` | 202,880 | 46,936 | 3 | 1 (782,160) | 353,100 | 4 / 4 | 46,384 | 1 / 1 | 2,103,249 | n/a | none |
| `level132` | 309,552 | 45,635 | 3 | 1 (448,784) | 260,628 | 2 / 2 | 81,780 | 1 / 1 | 1,659,304 | n/a | none |
| `level133` | 314,416 | 54,236 | 3 | 1 (327,600) | 265,656 | 6 / 15 | 109,732 | 2 / 3 | 2,614,975 | n/a | none |
| `level134` | 484,384 | 66,200 | 3 | 1 (770,976) | 643,996 | 6 / 6 | 267,224 | 3 / 3 | 4,016,479 | n/a | none |
| `level136` | 435,920 | 55,290 | 3 | 1 (375,040) | 338,544 | 4 / 4 | 211,940 | 3 / 3 | 2,528,969 | n/a | none |
| `level137` | 453,024 | 46,286 | 3 | 1 (375,040) | 317,176 | 3 / 3 | 183,280 | 2 / 2 | 2,172,301 | n/a | none |

- The 64 levels' own files total 782,023,486 bytes. Their manifests list 1,370 parts; the disc holds 1,707
  sector files for their worlds.
- `level95` has the most packs (12, 47,011,408 bytes). `level112` is the only level with a `.lev` file and no pack.
- `level119` and `level120` (The Graveyard and The Bridge) have identical world streams and manifests.

## Worlds

| Fact | Value |
| --- | --- |
| Worlds per level with a `.lev` file | 2: `<world>s` and `<world>d` |
| Largest `s` stream of a `.lev` level | 1,316,524 bytes (`level61s`) |
| Largest `d` stream of a `.lev` level | 517,868 bytes (`level60d`) |
| Sector files no manifest lists | 347 in all: 337 for the 64 `.lev` levels, 10 for the world-only records below |
| Numbering of unlisted sector files | Always above the manifest's part count: a world with N listed parts and M files on the disc has parts N + 1 to M unlisted |
| Example | `level99s` lists 20 parts; the disc holds `level99s_ms1.sec` to `level99s_ms27.sec` |
| Unlisted parts in play | In all ten tutorial savestates exactly the 31 listed parts of `level99s` and `level99d` were resident (memory budgets page); parts 21 to 27 of `level99s` never were |

### Records with worlds but no level file

These 15 records have both world streams and manifests on the disc but no `.lev` file, no script and no pack.

| Record | `s` parts listed / on disc | `d` parts listed / on disc |
| --- | ---: | ---: |
| `level70` | 31 / 31 | 5 / 5 |
| `level71` | 3 / 3 | 2 / 3 |
| `level72` | 1 / 1 | 1 / 1 |
| `level73` | 21 / 21 | 13 / 13 |
| `level74` | 4 / 5 | 2 / 2 |
| `level90` | 1 / 5 | 1 / 1 |
| `level91` | 8 / 8 | 5 / 5 |
| `level94` | 8 / 8 | 5 / 6 |
| `level96` | 26 / 26 | 17 / 19 |
| `level97` | 8 / 8 | 5 / 5 |
| `level98` | 1 / 1 | 1 / 1 |
| `level106` | 6 / 6 | 2 / 2 |
| `level117` | 1 / 1 | 1 / 1 |
| `level125` | 2 / 2 | 1 / 1 |
| `level135` | 7 / 7 | 4 / 5 |

## Checkpoints and sections

A level's checkpoints are numbered from 1. The checkpoint number is the same value as the section number in the
level list, and the level script sets it as the mission goes on.

| Situation | What is loaded | What the player sees |
| --- | --- | --- |
| Reaching a checkpoint during play | Nothing; the inventory and player statistics are stored as the checkpoint's copy | No load screen |
| Entering a level at checkpoint 1 | `<name>_1.pak`, when it exists | Load screen; the opening movie, when the level has one; player statistics reset |
| Entering a level at checkpoint k of 2 or more | `<name>_<k>.pak`, when it exists | Load screen; no opening movie; statistics come back from the checkpoint copy |
| Entering a level at a checkpoint with no pack (for example checkpoint 2 of `level60`) | No pack | Load screen as usual |
| Mission failed, retry from a checkpoint the script has marked for retry in place | Nothing | No load screen; inventory and statistics return to the checkpoint copy |
| Mission failed, other retry | The whole level again, at the current checkpoint | Load screen |
| Mission failed, restart the mission | The whole level again, at checkpoint 1 | Load screen; opening movie, when the level has one |

- Packs match checkpoints one to one in 23 of the 29 levels with sections (orders 1 to 25): k runs from 1 to the
  section count. The exceptions are `level84` (3 sections, packs 1 to 4), `level60` (3 sections, packs 1 and 3),
  `level61` to `level63` (3 sections, pack 1 only) and `level64` (4 sections, packs 1, 3 and 4).
- Every Rumble arena with a `.lev` file except `level112` has exactly one pack, `<name>_1.pak`.
- In the ten tutorial savestates, `level99` is at checkpoint 2 in states 1 to 6 and at checkpoint 3 in states 7 to
  10, with the same level and worlds resident throughout.

## Entering a level

When play begins at a level, the following are in memory or under way, whatever the checkpoint:

| Item | Source | Condition |
| --- | --- | --- |
| Level file | `<name>.lev` | Always |
| Both world streams and manifests | `<world>s_sec.*`, `<world>d_sec.*` (or `<world>_sec.*`) | Always |
| World parts near the camera | `<world>s_ms<i>.sec`, `<world>d_ms<i>.sec` | Those pending near the starting camera, until the wait limit below runs out |
| Object list | `<name>_objs.txt` | Always (`level100` has none) |
| Checkpoint pack | `<name>_<k>.pak` | When it exists for the checkpoint entered |
| Resources the level's dependency group names | archive entries named by hash | Kept for the level (memory budgets page) |
| Sound bank | `sound`, or the bank the level's script asks for | Always one |

| Wait limit at level start | Value |
| --- | --- |
| Level number 100 or below (story levels, front end) | 15,000 ms |
| Level number above 100 (Rumble arenas) | 30,000 ms |

- The load screen stays up until the checkpoint pack is in and no world part near the camera is still pending, or
  until the wait limit has passed, whichever comes first. Parts still missing at that point keep loading during play.
- A level with an opening movie, entered at checkpoint 1, plays the movie after the load screen has gone (see
  "Movies around a level").

### Sound at level start

- The `sound` bank (1,613,664-byte `.msb`) is the bank a level gets when its script has not asked for another.
- Bank names found in level scripts: `gallery` (`level3.lua`), `lizzie` (`level55.lua`), `armies` (`level60.lua` to
  `level64.lua`) and `spookorama` (`level110.lua`).

## The load screen

A level start shows one of two load screens. The memory-card load screen (below) appears only for the front-end
scene `level100` loaded at power-on; every other level start shows the level load screen.

| Element | What the player sees |
| --- | --- |
| Background | Black |
| Image | One full-screen image at a time, 512 x 512, 8-bit |
| Progress bar | A bar that grows from the left; its position and length depend on the display setting (four layouts) |
| Bar colour | Red (170, 43, 43) for most levels; light grey (223, 223, 223) for the five letter missions `level82`, `level92`, `level83`, `level20` and `level11` (orders 19 to 23) |
| Fade in | 200 ms from black, with images and bar fading together |
| Fade out | About 200 ms when loading has finished |

### Load-screen timings

The bar and the images are driven by the clock, not by the amount of data read.

| Fact | Level number 100 or below | Level number above 100 |
| --- | ---: | ---: |
| Time for the bar to fill | 23,000 ms | 30,000 ms |
| Images shown | Up to 3, in turn | 1 |
| Image change times with 3 images | 7,667 ms and 15,333 ms | none |

- If loading finishes before the bar is full, the screen fades out at once with the bar part-filled.
- If loading takes longer, the bar stays full and the last image stays up.

### Load-screen images

Every load-screen image is its own archive entry holding one 512 x 512, 8-bit texture; the
entry's name is the CRC-32 of the texture name as a decimal number, and the texture carries its own name inside.
Entries are 263,680 bytes, except the 15 normal-display images of `level2`, `level3`, `level5`, `level7` and
`level9` (`<name>_ls_0` to `<name>_ls_2`), which are 263,664 bytes.

| Group | Names | Entries |
| --- | --- | ---: |
| Story levels with three images | `<name>_ls_0` to `<name>_ls_2`, each with a `_w` copy, for 24 levels (the 23 missions of orders 1 to 23, and `level95`) | 144 |
| Stage levels | `level60_ls_0` to `level64_ls_0`, each with a `_w` copy | 10 |
| Unused arena image | `level105_ls_0` | 1 |
| `level7`, which has no level file | `level7_ls_0` to `level7_ls_2`, no `_w` copies | 3 |
| Rumble images | `rumble_<m>_ls_0`, by the Rumble game mode number m | 118 |
| Default | `default_ls_0`, `default_ls_0_w` | 2 |
| Memory-card load screen | `memory_card_screen` (with `_w` and the four language endings), `memory_card_loading`, `memory_card_loading_w` | 12 |
| Total | | 290 |

- The `_w` image is used with the wide display setting; with that setting only `_w` names are looked for.
- Level images have no language versions. Rumble images take the ending of the game language: none for English,
  `_sp` Spanish, `_fr` French, `_it` Italian, `_ge` German, after `_w` when present (`rumble_1_ls_0_w_ge`).

| Image not on the disc | Image shown in its place |
| --- | --- |
| An image in the player's language | The same image without a language ending |
| Level image 1 or 2 | Image 0 of the same level |
| Every image of the level's own | `default_ls_0`, also with the wide setting (`default_ls_0_w` is on the disc but never shown) |

- Levels with a load image of their own: the 24 story levels above (3 images each) and `level60` to `level64` (1
  each). `level1`, `level100` and the records without images show `default_ls_0`.
- Arenas (level number above 100) show the image of the Rumble game mode chosen in the Rumble menus, never an
  image named after the arena; this is why `level105_ls_0` is never shown.

| Rumble mode numbers m | Images on the disc |
| --- | --- |
| 1, 2, 3, 9, 10, 11, 12, 14, 19, 23, 24 | 10 each: normal and `_w`, in all five languages |
| 4, 5, 6, 22, 25 | `rumble_<m>_ls_0` only |
| 18 | `rumble_18_ls_0`, `rumble_18_ls_0_sp`, `rumble_18_ls_0_w_sp` |
| 7, 8, 13, 15, 16, 17, 20, 21 | none; these modes show `default_ls_0` |

### Load-screen sound

While the load screen is up a stereo loop plays from one of seven load-screen banks. Each bank holds two sounds, a
left half and a right half, played hard left and hard right together.

| Bank | `.msb` bytes | Right half starts at |
| --- | ---: | ---: |
| `load_00` | 1,746,016 | 873,008 |
| `load_01` | 1,555,040 | 777,520 |
| `load_02` | 1,612,832 | 806,416 |
| `load_03` | 1,646,240 | 823,120 |
| `load_04` | 1,813,120 | 906,560 |
| `load_05` | 1,640,224 | 820,112 |
| `load_06` | 1,745,120 | 872,560 |
| `armload` | 1,055,936 | (two sounds) |

- Each level start uses the next of `load_00` to `load_06`, going round after `load_06`; the count starts at
  `load_00` after power-on.
- Levels numbered 60 to 69 (on the disc `level60` to `level64`, the Armies of the Night stages) play `armload`
  instead; the count still moves on.
- Each `.msd` index holds two pairs and a zero pair (24 bytes). The seven `load_` banks are archive entries 9,728 to
  9,741 (`.msb` and `.msd` alternating), seven of the eight banks the archive contents page lists without a name.

### Memory-card load screen

| Fact | Value |
| --- | --- |
| When | The load of the front-end scene `level100` at power-on |
| First image | `memory_card_screen` in the game language (`_sp`, `_fr`, `_it`, `_ge`; `_w` with the wide setting), for 5,000 ms |
| Second image | `memory_card_loading` (`memory_card_loading_w` with the wide setting), with the loading indicator |
| Nominal length | 21,000 ms |
| Progress bar | None |
| Fade in and out | 200 ms each |

Later loads of `level100` show the level load screen with `default_ls_0`.

## Waits during play

The game also waits for world parts and packs without a load screen: around cutscenes and when a level script asks
for the world near a position to be loaded.

| Fact | Value |
| --- | --- |
| Game clock | Stopped while the wait lasts |
| Loading indicator | Appears once the wait has lasted 1,500 ms |
| Load screen and bar | None |

## Movies around a level

A record whose LT 2 value is `LT_PLAY` has an opening movie, and one whose LT 3 value is `LT_PLAY` a closing movie
(values on the level list page). The movies are `PSS\L<number>_IN.BIK` and `PSS\L<number>_OUT.BIK`.

| Level | Title | Opening movie (bytes) | Closing movie (bytes) |
| --- | --- | --- | --- |
| `level1` | none | `L1_IN.BIK` (58,558,968) | none |
| `level99` | New Blood | `L99_IN.BIK` (21,897,112) | none |
| `level87` | Payback | none | `L87_OUT.BIK` (12,819,700) |
| `level34` | Blackout | none | `L34_OUT.BIK` (22,788,160) |
| `level81` | Encore | none | `L81_OUT.BIK` (11,607,724) |
| `level31` | Boys In Blue | `L31_IN.BIK` (16,767,680) | `L31_OUT.BIK` (19,479,396) |
| `level9` | All-City | `L9_IN.BIK` (34,138,400) | `L9_OUT.BIK` (4,933,168) |
| `level51` | Desperate Dudes | `L51_IN.BIK` (140,089,108) | none |
| `level52` | No Permits, No Parley | `L52_IN.BIK` (22,345,988) | none |
| `level54` | Home Run | `L54_IN.BIK` (15,270,360) | none |
| `level84` | Come Out To Play | none | `L84_OUT.BIK` (139,516) |

- The opening movie plays only when the level is entered at checkpoint 1, once the load screen has gone. While the
  level loads, the 3,200,000-byte opening-movie reserve of the memory budgets page is held for it.
- The closing movie plays on leaving the level. While it plays, the level file is still in memory; it is released
  when it ends.
- In all ten tutorial savestates the `level99` record's movie flags read as opening movie only, matching its LT 2
  `LT_PLAY` and LT 3 `LT_NONE`.

## Leaving a level

- On leaving a level, game sound and music stop and the level's resources, worlds and level file are released as
  the memory budgets page describes ("When memory is released"). The global data and the shared character header
  stay.

## Xbox version

The Xbox disc was not examined for this page. Its archive is split into other files (file access page), and whether
it holds the same load-screen images, banks and per-level files is open.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x0015fe90` | Start a level at a checkpoint behind the load screen | Entering a level |
| `0x001612b0` | Put up the load screen for the level being started | The load screen |
| `0x0040e2d8` | Wait for the checkpoint pack and the world near the camera | Waits during play |
| `0x00111178` | Start the load-screen sound loop | Load-screen sound |
| `0x00158488` | Leave a level, with its closing movie | Movies around a level |

## Acceptance checks

- The PS2 archive holds 64 `.lev` files, and every level with a `.lev` file has two world streams named after its
  world with `s` and `d` appended.
- `level99` has 3 packs (`level99_1.pak` to `level99_3.pak`, 7,713,520 bytes together) and its own files total
  15,186,981 bytes.
- `level95` has 12 packs; `level112` has none.
- The disc holds 27 sector files for `level99s`, while `level99s_sec.mem` lists 20 parts.
- Over all worlds, 347 sector files lie above their manifest's part count.
- `objarena` and `courtyard` are the only single worlds, with streams `objarena_sec.wld` (28,820 bytes) and
  `courtyard_sec.wld` (246,655 bytes).
- The archive holds 290 load-screen images, every one an entry with a 512 x 512 8-bit texture; 275 entries are
  263,680 bytes and 15 are 263,664 bytes.
- The entry for `level99_ls_0` is archive record 1,174 and its resource hash is `0x85995827`, the CRC-32 of
  `level99_ls_0`.
- New Blood's load screen shows `level99_ls_0` on a red bar, `level99_ls_1` from 7.7 s and `level99_ls_2` from
  15.3 s if loading lasts that long; the bar would be full 23 seconds after the screen appears.
- Starting Roots (`level82`) shows a light grey bar instead of a red one.
- Starting a Rumble arena shows one image, the same for every arena of the chosen Rumble mode.
- Starting a level in Rumble mode 4 with the wide display setting shows the default image.
- Reaching a checkpoint in a story level never brings up a load screen.
- Restarting New Blood from its first checkpoint plays the opening movie `L99_IN.BIK` after the load screen;
  resuming it from a later checkpoint does not.
- Finishing Payback (`level87`) plays `L87_OUT.BIK`.
- The archive holds seven load-screen banks `load_00` to `load_06`, each `.msd` pointing at offset 0 and at half the
  `.msb` size; two level starts in a row play different load-screen music.
- During a cutscene wait without a load screen, the loading indicator appears only after one and a half seconds.

## Open questions

- Real load times were not measured: how long the load screen stays up for each level on a PS2, and how often the
  15,000 ms and 30,000 ms wait limits are reached.
- How near to the camera a world part must be for the load screen to wait for it.
- Which Rumble game mode name goes with each mode number m.
- What exactly ends the memory-card load screen period at power-on (the screen or button that the player passes).
- Which mission-failed retries restart in place and which reload the level; this depends on the level script.
- What the records without a `.lev` file (for example `combatselect`, `level70`) do when started.
- Whether the bank names found in level scripts are the banks those levels load in place of `sound`.
- How the sample rate and length of the load-screen loops relate to their sizes.
- The Xbox version's level files, load-screen images and timings.
