# PS2 disc layout

The files on the NTSC-U PS2 disc of The Warriors (`SLUS_212.15`), as stored in its ISO 9660 file system. Every
value on this page was read from the disc image "Warriors, The (USA) (En,Fr,De,Es,It).iso".

## Volume

| Field | Value |
| --- | --- |
| File system | ISO 9660, primary volume descriptor at sector 16 |
| System identifier | `PLAYSTATION` |
| Volume identifier | `THEWARRIORS` |
| Publisher and data preparer | `ROCKSTAR GAMES` |
| Application identifier | `PLAYSTATION` |
| Volume creation date | 2005-09-25 20:50:11 |
| Logical block size | 2,048 bytes |
| Volume space | 2,097,616 sectors (4,295,917,568 bytes, the whole image; single-layer DVD) |
| Directories | the root plus `MODULES`, `IOP` and `PSS` |
| Files | 32 |
| Sum of file sizes | 4,274,269,343 bytes |

The files are stored back to back in sector order with no unused sectors between them. Each file starts on a sector
boundary; the unused tail of a file's last sector is zero for the files checked (`WARRIORS.DIR`). After the last
file (`PSS/TRAILER.BIK`) the volume has 10,254 unused sectors.

## File list

Ordered by start sector. Sizes are in bytes. Dates are the ISO directory record dates.

| Path | Start sector | Size | Date | Role |
| --- | ---: | ---: | --- | --- |
| `SYSTEM.CNF` | 304 | 57 | 2005-09-25 | Boot configuration (text) |
| `SLUS_212.15` | 305 | 4,903,976 | 2005-09-25 | Main executable (ELF) |
| `MODULES/IOP.IRX` | 2,700 | 143,132 | 2005-07-19 | IOP module, the game's own |
| `MODULES/IOPRP300.IMG` | 2,770 | 275,345 | 2005-05-16 | IOP replacement module image |
| `MODULES/LIBSD.IRX` | 2,905 | 28,661 | 2005-05-16 | IOP module, sound library |
| `MODULES/MCMAN.IRX` | 2,919 | 95,909 | 2005-05-16 | IOP module, memory card manager |
| `MODULES/MCSERV.IRX` | 2,966 | 7,417 | 2005-05-16 | IOP module, memory card server |
| `MODULES/PADMAN.IRX` | 2,970 | 44,869 | 2005-05-16 | IOP module, controller manager |
| `MODULES/SIO2MAN.IRX` | 2,992 | 6,641 | 2005-05-16 | IOP module, SIO2 manager |
| `WARRIORS.DIR` | 2,996 | 128,428 | 2005-09-25 | Archive directory (see the archive directory page) |
| `WARRIORS.WAD` | 3,059 | 1,495,371,776 | 2005-09-25 | Archive data |
| `IOP/BFW.SND` | 733,221 | 1,402,861,568 | 2005-09-25 | Streamed audio bank |
| `IOP/MUSIC.SND` | 1,418,212 | 960,063,488 | 2005-09-25 | Streamed music bank |
| `PSS/L1_IN.BIK` | 1,886,993 | 58,558,968 | 2005-09-22 | Movie |
| `PSS/L31_IN.BIK` | 1,915,587 | 16,767,680 | 2005-09-18 | Movie |
| `PSS/L31_OUT.BIK` | 1,923,775 | 19,479,396 | 2005-09-18 | Movie |
| `PSS/L34_OUT.BIK` | 1,933,287 | 22,788,160 | 2005-09-18 | Movie |
| `PSS/L51_IN.BIK` | 1,944,415 | 140,089,108 | 2005-09-24 | Movie |
| `PSS/L52_IN.BIK` | 2,012,818 | 22,345,988 | 2005-09-18 | Movie |
| `PSS/L54_IN.BIK` | 2,023,730 | 15,270,360 | 2005-09-18 | Movie |
| `PSS/L81_OUT.BIK` | 2,031,187 | 11,607,724 | 2005-09-18 | Movie |
| `PSS/L84_OUT.BIK` | 2,036,855 | 139,516 | 2005-09-11 | Movie |
| `PSS/L87_OUT.BIK` | 2,036,924 | 12,819,700 | 2005-09-18 | Movie |
| `PSS/L99_IN.BIK` | 2,043,184 | 21,897,112 | 2005-09-22 | Movie |
| `PSS/L9_IN.BIK` | 2,053,876 | 34,138,400 | 2005-09-18 | Movie |
| `PSS/L9_OUT.BIK` | 2,070,546 | 4,933,168 | 2005-09-18 | Movie |
| `PSS/LOGO.BIK` | 2,072,955 | 303,228 | 2005-09-22 | Movie |
| `PSS/PLOGO.BIK` | 2,073,104 | 3,560,952 | 2005-09-23 | Movie |
| `PSS/TRAILER.BIK` | 2,074,843 | 25,638,616 | 2005-09-18 | Movie |

All ISO file names carry the version suffix ";1". The directory and the data file of the archive pair share the same
timestamp (2005-09-25 05:26:55).

## Share of the disc

| Group | Files | Bytes | Share of file bytes |
| --- | ---: | ---: | ---: |
| Archive pair (`WARRIORS.DIR`, `WARRIORS.WAD`) | 2 | 1,495,500,204 | 35.0 % |
| Audio banks (`IOP/*.SND`) | 2 | 2,362,925,056 | 55.3 % |
| Movies (`PSS/*.BIK`) | 16 | 410,338,076 | 9.6 % |
| Executable, boot file and IOP modules | 9 | 5,506,007 | 0.1 % |

## Boot file

`SYSTEM.CNF` is 57 bytes of text in three lines, each ended by CR LF:

| Key | Value |
| --- | --- |
| `BOOT2` | "cdrom0:\SLUS_212.15;1" |
| `VER` | `1.03` |
| `VMODE` | `NTSC` |

The boot file names the executable `SLUS_212.15`, which starts with the ELF magic `7F 45 4C 46`.

## IOP modules

Each `.IRX` file is an ELF file with an `.iopmod` section; the module name and version below are the strings and
numbers stored in that section.

| File | Module name in the file | Version |
| --- | --- | --- |
| `IOP.IRX` | `multi_streamer` | 1.02 |
| `LIBSD.IRX` | `Sound_Device_Library` | 3.03 |
| `MCMAN.IRX` | `mcman` | 2.38 |
| `MCSERV.IRX` | `mcserv` | 2.16 |
| `PADMAN.IRX` | `padman` | 4.16 |
| `SIO2MAN.IRX` | `sio2man` | 3.01 |

`IOP.IRX` is the only module dated 2005-07-19; the others share the date 2005-05-16 with `IOPRP300.IMG`.

`IOPRP300.IMG` begins with the magic `RESET` and holds a ROMDIR table of 16-byte records (10-byte name, 2-byte
extended-info size, 4-byte size). It lists 19 records:

| Record | Size | Record | Size |
| --- | ---: | --- | ---: |
| `RESET` | 0 | `FILEIO` | 19,661 |
| `ROMDIR` | 320 | `CDVDMAN` | 83,053 |
| `EXTINFO` | 596 | `CDVDFSV` | 32,653 |
| `SYSMEM` | 6,041 | `LOADFILE` | 10,505 |
| `LOADCORE` | 10,433 | `TIMEMANI` | 6,085 |
| `SIFCMD` | 10,529 | `ROMDRV` | 3,881 |
| `SIFMAN` | 5,969 | `EESYNC` | 1,545 |
| `THREADMAN` | 39,405 | `SYSCLIB` | 10,045 |
| `IOMAN` | 12,545 | `STDIO` | 3,377 |
| `MODLOAD` | 18,565 | | |

The records' sizes, each rounded up to 16 bytes except the last, add up to the file's size of 275,345 bytes.

## Archive pair

`WARRIORS.DIR` (128,428 bytes, 10,701 entries) indexes `WARRIORS.WAD` (1,495,371,776 bytes, exactly 730,162
sectors). The WAD starts in the sector right after the DIR's last sector. The layout of the directory, its entry
records and the name hashes are on the archive directory page.

## Audio banks

| File | Size | Sectors | Notes |
| --- | ---: | ---: | --- |
| `IOP/BFW.SND` | 1,402,861,568 | 684,991 | Largest file on the disc |
| `IOP/MUSIC.SND` | 960,063,488 | 468,781 | |

Both files are whole multiples of 2,048 bytes and have no header magic: each starts with 16 zero bytes. Their data
is laid out in 16-byte frames that fit the PS2 sound processor's ADPCM format: in the first 64 MiB of each file, all
4,194,304 frames have a valid shift and filter byte (filter 0 to 4, shift 0 to 12) and a flag byte of 0 to 7. In
`MUSIC.SND` every frame in that range has flag 0; in `BFW.SND`, 627 frames have flag 2 and a handful have flags 1, 3,
6 or 7. How either bank is split into sounds or tracks is not covered here.

## Movies

All 16 movies are Bink files with the signature `BIKi`. Each header's stored file size equals the ISO size minus 8.
Every movie is 640 x 448 pixels.

| File | Frames | Frame rate | Length | Audio tracks |
| --- | ---: | --- | ---: | ---: |
| `L1_IN.BIK` | 5,137 | 30/1 | 171.2 s | 1 |
| `L31_IN.BIK` | 1,172 | 30/1 | 39.1 s | 1 |
| `L31_OUT.BIK` | 1,376 | 30/1 | 45.9 s | 1 |
| `L34_OUT.BIK` | 1,633 | 30/1 | 54.4 s | 1 |
| `L51_IN.BIK` | 18,274 | 2997/100 | 609.7 s | 1 |
| `L52_IN.BIK` | 1,567 | 30/1 | 52.2 s | 1 |
| `L54_IN.BIK` | 1,066 | 30/1 | 35.5 s | 1 |
| `L81_OUT.BIK` | 810 | 30/1 | 27.0 s | 1 |
| `L84_OUT.BIK` | 180 | 30/1 | 6.0 s | 0 |
| `L87_OUT.BIK` | 929 | 30/1 | 31.0 s | 1 |
| `L99_IN.BIK` | 2,853 | 2997/100 | 95.2 s | 1 |
| `L9_IN.BIK` | 2,458 | 30/1 | 81.9 s | 1 |
| `L9_OUT.BIK` | 363 | 30/1 | 12.1 s | 1 |
| `LOGO.BIK` | 115 | 2997/100 | 3.8 s | 1 |
| `PLOGO.BIK` | 360 | 30/1 | 12.0 s | 0 |
| `TRAILER.BIK` | 1,978 | 30/1 | 65.9 s | 1 |

Thirteen of the names have the form `L<number>_IN` or `L<number>_OUT`; the other three are `LOGO`, `PLOGO` and
`TRAILER`. `L84_OUT.BIK` and `PLOGO.BIK` carry no audio track.

## Differences from the Xbox disc

The Xbox disc ("Warriors, The (USA).iso", an XDVDFS game partition) has no `WARRIORS.DIR` or `WARRIORS.WAD`. Its
root instead holds `XBoxWad.idx` (169,700 bytes) with the data split across `Main.wad0` to `Main.wad3`,
`Paks.wad0` to `Paks.wad2` and `System.wad0`; its executable is `default.xbe`. Its movies sit in `bik/` in lower case,
with three files per PS2 movie (plain, `_hd` and `_w` variants, 48 in all). Its audio is in `audio/`, mainly as wave
bank and sound bank files (`.xwb`, `.xsb`) rather than `.SND` banks. The Xbox has no IOP modules or `SYSTEM.CNF`.

## Behaviour

- When the game starts, it restarts the IOP from `MODULES/IOPRP300.IMG` and loads all six `.IRX` modules from
  `MODULES`.
- `IOP/BFW.SND` is opened when the sound system starts.
- `IOP/MUSIC.SND` is opened each time a music track starts, over that track's part of the file only.
- A movie is played by its name in `PSS` without the extension, for example `L1_IN`. Music and sound effects stop
  when a movie starts.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x00148230` | Start the IOP with the disc's modules | Behaviour |
| `0x0014bbd8` | Start the sound system with the streamed audio bank | Behaviour |
| `0x0014d528` | Open a music track in the music bank | Behaviour |
| `0x0042a938` | Play a movie | Movies |

## Acceptance checks

| Outcome |
| --- |
| The PS2 disc's ISO 9660 volume identifier is `THEWARRIORS` and its volume spans 2,097,616 sectors of 2,048 bytes. |
| The disc holds exactly 32 files in the root, `MODULES`, `IOP` and `PSS`. |
| `SYSTEM.CNF` names "cdrom0:\SLUS_212.15;1" as the boot file, with version `1.03` and video mode `NTSC`. |
| `SLUS_212.15` is 4,903,976 bytes and starts with the ELF magic. |
| `WARRIORS.DIR` is 128,428 bytes at sector 2,996 and `WARRIORS.WAD` is 1,495,371,776 bytes at sector 3,059. |
| `WARRIORS.WAD` ends exactly at sector 733,221, where `IOP/BFW.SND` begins. |
| No unused sectors lie between any two files; 10,254 unused sectors follow the last file. |
| The six `.IRX` files carry the module names `multi_streamer`, `Sound_Device_Library`, `mcman`, `mcserv`, `padman` and `sio2man`. |
| `IOPRP300.IMG` starts with `RESET` and its ROMDIR lists 19 records, the last being `STDIO`. |
| All 16 files in `PSS` start with `BIKi` and are 640 x 448. |
| `PSS/L51_IN.BIK` is the longest movie at 18,274 frames (about 10 minutes 10 seconds at 29.97 frames per second). |
| `PSS/L84_OUT.BIK` and `PSS/PLOGO.BIK` have no audio track. |
| Both `.SND` files are whole multiples of 2,048 bytes and start with 16 zero bytes. |
| The Xbox disc has no `WARRIORS.DIR`; it has `XBoxWad.idx` instead. |

## Open questions

- How `BFW.SND` and `MUSIC.SND` are divided into individual sounds and tracks, their sample rates, and whether
  `MUSIC.SND` interleaves channels; no index for them was found among the disc's files.
- Which game moments each `L<number>_IN` and `L<number>_OUT` movie plays at; the number is presumably a level or
  mission number, which is unconfirmed.
- Whether the version string `1.03` in `SYSTEM.CNF` marks a disc revision that differs from other NTSC-U pressings.
- Where the offset and size of each music track within `MUSIC.SND` are stored was not examined.
- The Xbox archive set (`XBoxWad.idx` and the `.wad0` to `.wad3` files) is listed here by name and size only; its
  layout was not examined.
