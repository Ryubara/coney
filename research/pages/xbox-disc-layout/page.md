# Xbox disc layout

## Scope and sources

This page records the file system of the Xbox disc of The Warriors (USA), the files on it with their sizes and roles,
how those files line up with the PS2 disc, and the container fields of the executable `default.xbe` (its header
tables only). The archive inside the `.wad` volumes is on the Xbox asset archive page.

| Source | Size in bytes | Notes |
| --- | ---: | --- |
| Xbox disc image, Warriors, The (USA) | 7,825,162,240 | Every fact on this page was read from it |
| PS2 disc image, Warriors, The (USA) (En, Fr, De, Es, It) | 4,295,917,568 | Used only for the comparison section |
| `default.xbe` on the Xbox disc | 4,636,672 | SHA1 `fe246a74501cd10d2ce89ca5dbda5080d2b2edcc` |

All multi-byte values are little-endian.

## The disc image

The image is a full Xbox game disc dump. The game's file system (XDVDFS) does not start at byte 0: its partition
begins at byte `0x18300000` (405,798,912) of the image. Sector numbers below are 2,048-byte sectors counted from that
partition start.

| Item | Value |
| --- | --- |
| Partition start in the image | byte `0x18300000` |
| Volume descriptor | partition sector 32 |
| Volume descriptor magic | `MICROSOFT*XBOX*MEDIA` at descriptor bytes 0x000 and 0x7EC |
| Root directory | sector 1,715,631, 2,048 bytes |
| Volume timestamp | Windows FILETIME, 2005-09-27 06:33:32 UTC |
| Directories | 2 (`audio`, `bik`) under the root |
| Files | 431, 5,620,148,362 bytes in all |

### Volume descriptor (partition sector 32)

| Offset | Size | Meaning | Value on this disc |
| --- | --- | --- | --- |
| 0x000 | 20 | magic | `MICROSOFT*XBOX*MEDIA` |
| 0x014 | 4 | root directory sector | 1,715,631 |
| 0x018 | 4 | root directory size in bytes | 2,048 |
| 0x01C | 8 | creation time (FILETIME) | 2005-09-27 06:33:32 UTC |
| 0x7EC | 20 | magic again | `MICROSOFT*XBOX*MEDIA` |

### Directory records

Each directory is a binary tree of records stored in its own sectors. A record starts on a 4-byte boundary.

| Offset | Size | Meaning |
| --- | --- | --- |
| 0x00 | 2 | left subtree: record offset in 4-byte units from the directory start, 0 for none; 0xFFFF marks unused space |
| 0x02 | 2 | right subtree, same encoding |
| 0x04 | 4 | first sector of the file or subdirectory |
| 0x08 | 4 | size in bytes |
| 0x0C | 1 | attributes: 0x10 directory, 0x80 normal file (the only two values on this disc) |
| 0x0D | 1 | name length |
| 0x0E | n | name, single-byte characters, no terminator |

Read in tree order, the names come out sorted without regard to case.

## Files on the disc

### Root directory

| File | Bytes | First sector | Role |
| --- | ---: | ---: | --- |
| `default.xbe` | 4,636,672 | 1,712,438 | The executable (container fields below) |
| `XBoxWad.idx` | 169,700 | 1,715,548 | The resource index of the archive: 8 volumes, 10,587 entries |
| `System.wad0` | 1,679,360 | 1,714,728 | Archive volume: `global.pak` and `scene_list.cnk` (2 entries) |
| `Main.wad0` | 304,005,120 | 411,328 | Archive volume: 5,666 entries (resources, scenes, Lua, levels, animations) |
| `Main.wad1` | 304,064,512 | 258,492 | Archive volume: 2,651 entries (texture resources, streamed world) |
| `Main.wad2` | 303,992,832 | 102,751 | Archive volume: 1,202 entries (streamed world) |
| `Main.wad3` | 46,329,856 | 68,832 | Archive volume: 183 entries (streamed world) |
| `Paks.wad0` | 363,806,720 | 1,715,632 | Archive volume: 252 packages |
| `Paks.wad1` | 366,997,504 | 2,262,456 | Archive volume: 491 packages |
| `Paks.wad2` | 45,049,856 | 2,450,830 | Archive volume: 140 packages |

The eight `.wad` volumes hold 1,735,925,760 bytes; the index assigns 1,726,598,311 of them to entries and the rest
is padding between entries and at volume ends.

### `audio` directory (373 files)

| File | Count | Bytes | Role |
| --- | ---: | ---: | --- |
| `xbox000.xwb` to `xbox368.xwb` | 369 | 2,217,542,292 | XACT wave banks, magic `WBND`, version 3 |
| `xbox000.xsb` | 1 | 4,544,708 | XACT sound bank, magic `SDBK`, version 11 |
| `dsstdfx.bin` | 1 | 18,608 | Binary image; its first 32 bytes are zero |
| `Warriors.bin` | 1 | 15,244 | Binary image; its first 64 bytes are zero |
| `version.txt` | 1 | 2 | The text `47` |

Each wave bank names itself at byte 0x30 (16 bytes); all 369 names equal the file name without extension. The wave
count is the 32-bit value at byte 0x2C:

| Bank | Waves | Bytes |
| --- | ---: | ---: |
| `xbox000.xwb` | 542 | 3,987,604 |
| `xbox001.xwb` | 1,014 | 58,997,248 |
| `xbox002.xwb` | 19,276 | 920,578,048 |
| `xbox003.xwb` | 2,228 | 390,520,832 |
| `xbox004.xwb` | 99 | 6,486,016 |
| `xbox005.xwb` to `xbox368.xwb` | 1 each (364 banks) | the rest |

The banks hold 23,523 waves in all. The sound bank `xbox000.xsb` gives the same number three times, as 16-bit values
at bytes 26, 28 and 30. An identical copy of `xbox000.xsb` is also stored inside the archive (entry 208 of the index).

### `bik` directory (48 files)

All 48 files are Bink movies with the magic `BIKi`. There are 16 movies, each in three versions: a plain name, a
`_hd` name and a `_w` name.

| Version | Frame size | Files |
| --- | --- | ---: |
| plain (`l1_in.bik`) | 640 x 480 | 16 |
| `_hd` (`l1_in_hd.bik`) | 1280 x 720, except `trailer_hd.bik` at 640 x 480 | 16 |
| `_w` (`l1_in_w.bik`) | 640 x 480 | 16 |

| Movie | Frames | Rate | Bytes (plain / `_hd` / `_w`) |
| --- | ---: | --- | --- |
| `l1_in` | 5,137 (5,136 in `_hd`) | 30 | 58,882,436 / 66,178,888 / 59,396,140 |
| `l9_in` | 2,458 | 30 | 34,104,920 / 34,116,336 / 34,109,148 |
| `l9_out` | 363 | 30 | 5,045,548 / 5,182,296 / 5,059,760 |
| `l31_in` | 1,172 | 30 | 16,786,616 / 16,792,260 / 16,774,556 |
| `l31_out` | 1,376 | 30 | 19,450,668 / 19,346,420 / 19,317,752 |
| `l34_out` | 1,633 | 30 | 22,829,472 / 22,778,884 / 22,666,964 |
| `l51_in` | 18,528 (18,530 in `_hd`, `_w`) | 30 | 259,795,556 / 260,650,148 / 259,554,264 |
| `l52_in` | 1,567 | 30 | 22,299,020 / 22,418,204 / 22,384,568 |
| `l54_in` | 1,066 | 30 | 15,243,364 / 15,245,516 / 15,244,096 |
| `l81_out` | 810 | 30 | 11,603,772 / 11,601,624 / 11,592,536 |
| `l84_out` | 180 | 30 | 117,212 / 164,952 / 125,168 |
| `l87_out` | 929 | 30 | 12,809,820 / 13,043,092 / 12,801,424 |
| `l99_in` | 2,856 | 30 | 40,873,152 / 40,871,588 / 40,867,492 |
| `logo` | 115 | 29.97 (30 in `_w`) | 298,712 / 391,896 / 290,156 |
| `plogo` | 360 | 29.97 | 3,576,940 / 3,979,612 / 3,576,940 |
| `trailer` | 1,978 | 29.97 | 25,684,816 / 25,685,164 / 25,685,508 |

The 48 movies hold 1,657,295,376 bytes. In every file the size field at byte 4 plus 8 equals the file size.

## How the Xbox disc relates to the PS2 disc

Both discs carry the same game data kinds; the containers differ.

| Content | PS2 disc | Xbox disc |
| --- | --- | --- |
| Executable | `SLUS_212.15` (4,903,976 bytes) | `default.xbe` (4,636,672 bytes) |
| Archive directory | `WARRIORS.DIR`, 128,428 bytes: 16-byte header then 10,701 records of 12 bytes | `XBoxWad.idx`, 169,700 bytes: 308-byte header then 10,587 records of 16 bytes |
| Archive data | one file, `WARRIORS.WAD` (1,495,371,776 bytes) | eight volumes: `System.wad0`, `Main.wad0`-`Main.wad3`, `Paks.wad0`-`Paks.wad2` |
| Entry position | byte offset into `WARRIORS.WAD` | volume number and 1,024-byte unit offset (details on the archive page) |
| Entry name hash | CRC-32 of the lowercase name with prefix `./ee_files/` and forward slashes | CRC-32 of the lowercase name with prefix `ee_files\` and backslashes |
| Sound | `IOP/BFW.SND` (1,402,861,568) and `IOP/MUSIC.SND` (960,063,488), plus sound banks inside the archive | 369 XACT wave banks and one sound bank in `audio` |
| Movies | 16 Bink files in `PSS` (`L1_IN.BIK` ...) | the same 16 names in `bik`, three versions each |
| Other files | `SYSTEM.CNF`, `MODULES/*.IRX`, `MODULES/IOPRP300.IMG` | none |

No index hash is shared between the two directories, because the prefixes and separators differ. The same name
reaches the same content on both discs: for example `scene_list.cnk` hashes to 0x13650964 as `./ee_files/scene_list.cnk`
on the PS2 and to 0x7516D704 as `ee_files\scene_list.cnk` on the Xbox.

Entries with byte-identical content on both discs (SHA1 compared over every entry of both archives):

| Xbox kind | Xbox entries | Identical to a PS2 entry |
| --- | ---: | ---: |
| Lua 4.0 bytecode | 503 | 467 (all 467 PS2 scripts) |
| Scene records | 2,814 | 2,765 (all PS2 scene records) |
| Object lists (text) and `cn12.met` | 81 | 74 |
| `cn12.bmp` | 1 | 1 |
| All other entries: packages, resources, scene lists, streamed world, sound bank copy | 7,188 | 0 |

The PS2 archive holds 881 packages, 64 level resources, 2,765 scene records, 467 Lua scripts and one memory card
icon (magic `00 00 01 00`). The Xbox archive has 884 packages, 95 level resources, 2,814 scene records and 503 Lua
scripts; it has no PS2 sound banks and no memory card icon, and adds the XACT sound bank copy and the streamed world in
a different form (`.xlev` and `.xsec` entries).

## Executable container (`default.xbe`)

Only the container is recorded here: header fields, certificate, section table and library list.

### Image header

| Offset | Field | Value |
| --- | --- | --- |
| 0x000 | magic | `XBEH` |
| 0x104 | base address | 0x00010000 |
| 0x108 | size of all headers | 3,028 |
| 0x10C | size of image in memory | 6,046,176 |
| 0x110 | size of image header | 388 |
| 0x114 | timestamp | 0x433717C9 (2005-09-25 21:34:01 UTC) |
| 0x118 | certificate address | 0x00010184 (file offset 0x184) |
| 0x11C | number of sections | 19 |
| 0x120 | section headers address | 0x00010370 (file offset 0x370) |
| 0x124 | initialisation flags | 0x80000005 |
| 0x130 | stack commit | 0x0001EA60 (125,536) |
| 0x134 | heap reserve | 0x00100000 |
| 0x138 | heap commit | 0x00001000 |
| 0x140 | original PE image size | 0x005ECB20 |
| 0x144 | original PE checksum | 0 |
| 0x148 | original PE timestamp | 0x433717BA (2005-09-25 21:33:46 UTC) |
| 0x14C | debug path | `d:\Warriors\Build\rundata_XBox\WarriorsDVD.exe` |
| 0x150, 0x154 | debug file name (ASCII, UTF-16) | `WarriorsDVD.exe` |
| 0x160 | number of library versions | 9 |
| 0x174 | logo bitmap size | 690 |

### Certificate (file offset 0x184)

| Offset | Field | Value |
| --- | --- | --- |
| 0x00 | certificate size | 492 |
| 0x04 | timestamp | 2005-09-28 16:18:30 UTC |
| 0x08 | title id | 0x5454009B (publisher letters `TT`, title number 0x009B) |
| 0x0C | title name (UTF-16, 40 characters) | `The Warriors` |
| 0x5C | alternate title ids (16) | all zero |
| 0x9C | allowed media | 0x00000202 |
| 0xA0 | game region | 0x00000001 |
| 0xA4 | game ratings | 0x00000002 |
| 0xA8 | disc number | 0 |
| 0xAC | certificate version | 0x00000003 |

### Section table

| Section | Flags | File offset | Size in file | Size in memory |
| --- | --- | --- | ---: | ---: |
| `.text` | 0x16 | 0x001000 | 3,456,764 | 3,456,764 |
| `D3D` | 0x07 | 0x34D000 | 129,464 | 144,012 |
| `D3DX` | 0x07 | 0x36D000 | 12,676 | 12,680 |
| `XGRPH` | 0x07 | 0x371000 | 144,820 | 148,016 |
| `DSOUND` | 0x07 | 0x395000 | 156,204 | 156,836 |
| `XACTENG` | 0x07 | 0x3BC000 | 53,012 | 53,096 |
| `BINK` | 0x26 | 0x3C9000 | 73,732 | 73,732 |
| `BINKY16` | 0x36 | 0x3DC000 | 1,472 | 1,472 |
| `BINK16` | 0x36 | 0x3DD000 | 2,324 | 2,324 |
| `BINK32` | 0x16 | 0x3DE000 | 2,424 | 2,424 |
| `WMADEC` | 0x07 | 0x3DF000 | 111,308 | 111,308 |
| `XPP` | 0x07 | 0x3FB000 | 26,084 | 26,084 |
| `.rdata` | 0x06 | 0x402000 | 247,116 | 247,116 |
| `.data` | 0x07 | 0x43F000 | 99,220 | 1,535,728 |
| `DOLBY` | 0x06 | 0x458000 | 29,036 | 29,056 |
| `BINKDATA` | 0x07 | 0x460000 | 15,748 | 15,752 |
| `BINKBSS` | 0x26 | 0x464000 | 11,048 | 11,048 |
| `$$XTIMAGE` | 0x38 | 0x467000 | 10,240 | 10,240 |
| `$$XSIMAGE` | 0x38 | 0x46A000 | 4,096 | 4,096 |

The sections hold 4,586,788 bytes of the file.

### Library versions

Nine libraries are listed, all at version 1.0.5849: `XAPILIB`, `D3D8`, `D3DX8`, `XGRAPHC`, `DSOUND`, `XACTENG`,
`XBOXKRNL`, `LIBCPMT` and `LIBCMT`.

## Entry points

No Xbox addresses are given (see the open questions). The addresses below are in the PS2 executable `SLUS_212.15`
and serve the PS2 counterpart of the Xbox archive directory.

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x00149160` | Read the archive directory into memory | How the Xbox disc relates to the PS2 disc |
| `0x001490b8` | Find an archive entry by name | How the Xbox disc relates to the PS2 disc |

## Acceptance checks

- The Xbox disc image's XDVDFS partition starts at byte 0x18300000 and its sector 32 begins with
  `MICROSOFT*XBOX*MEDIA`.
- The disc's file tree has 2 directories and 431 files holding 5,620,148,362 bytes.
- `XBoxWad.idx` is 169,700 bytes and `default.xbe` is 4,636,672 bytes with SHA1
  `fe246a74501cd10d2ce89ca5dbda5080d2b2edcc`.
- The eight `.wad` volumes have the sizes listed in the root directory table.
- The `audio` directory holds 369 wave banks with 23,523 waves in all, and `xbox002.xwb` holds 19,276 of them.
- The `bik` directory holds 48 Bink movies: 16 names in three versions, the `_hd` versions at 1280 x 720 except the
  trailer.
- Every Lua script and every scene record of the PS2 archive has a byte-identical entry in the Xbox archive.
- `default.xbe` carries the title id 0x5454009B, the title name `The Warriors` and 19 sections, the first `.text`.

## Open questions

- The roles of `audio/dsstdfx.bin`, `audio/Warriors.bin` and `audio/version.txt` are not confirmed from the disc.
- Why `trailer_hd.bik` is 640 x 480 while the other `_hd` movies are 1280 x 720, and what distinguishes the `_w`
  versions from the plain ones at the same frame size.
- The meaning of the certificate's allowed media value 0x00000202 and ratings value 2 is not confirmed.
- The Xbox executable (`default.xbe`) is not available in the address study tools this session, so no Xbox addresses
  could be listed; the PS2 addresses above are counterparts only.
