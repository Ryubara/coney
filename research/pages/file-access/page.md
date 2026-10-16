# PS2 file access

## Scope and sources

This page records how the PS2 version gets its data off the disc: which disc files are read, the sizes and alignment
of reads and their buffers, how many reads can be outstanding, and what the player sees when the disc cannot be read.
The layout of the archive directory and its name hash are on the archive directory page (ps2-archive-directory) and
are only referred to here.

Source files, all on the PS2 disc image "Warriors, The (USA) (En,Fr,De,Es,It).iso" (volume `THEWARRIORS`, 2,097,616
sectors of 2,048 bytes):

| File | First sector | Size (bytes) | Role here |
| --- | ---: | ---: | --- |
| `SYSTEM.CNF` | 304 | 57 | Boot file: names `SLUS_212.15` as the program, version 1.03, video mode NTSC |
| `SLUS_212.15` | 305 | 4,903,976 | The executable; source of the code addresses below |
| `MODULES\IOP.IRX` and five other modules | 2,700 to 2,992 | 6,641 to 275,345 | IOP-side drivers, including the streaming driver that performs every archive read |
| `WARRIORS.DIR` | 2,996 | 128,428 | Directory of the archive: 16-byte header, 10,701 records of 12 bytes |
| `WARRIORS.WAD` | 3,059 | 1,495,371,776 | The archive (730,162 sectors); all game data other than movies and streamed sound |
| `IOP\BFW.SND` | 733,221 | 1,402,861,568 | Streamed sound, read by the sound driver |
| `IOP\MUSIC.SND` | 1,418,212 | 960,063,488 | Streamed music, read by the sound driver |
| `PSS\*.BIK` (16 files) | 1,886,993 to 2,074,843 | 139,516 to 140,089,108 | Full-screen movies |

## The archive at start-up

| Fact | Value |
| --- | --- |
| Directory file read | `WARRIORS.DIR`, read whole once at start-up and kept in memory for the whole session |
| Directory records held | 10,701 records of 12 bytes = 128,412 bytes, in a buffer aligned to 16 bytes |
| Archive file | `WARRIORS.WAD`, opened once at start-up on stream 0 of the IOP streaming driver and never closed |
| Entry start | Every one of the 10,701 entries starts on a 2,048-byte sector boundary inside `WARRIORS.WAD` |
| Gap after an entry | 0 to 2,046 bytes of padding up to the next sector; no two entries overlap |
| Entry sizes | 3 bytes (smallest) to 5,658,864 bytes (entry 8,614, the largest); 396 entries are larger than 384 KB |
| Sum of entry sizes | 1,484,729,244 bytes |

## Name lookup

- Every file the game reads during play, other than movies and the two `.SND` streams, is an entry of
  `WARRIORS.WAD` found through the directory by the hash of its name (see the archive directory page).
- Names are matched without regard to letter case.
- A name with no directory record finds nothing; the 10,701 hashes on the disc are all distinct.
- Many resources are stored under a number rather than a word: the entry name is the standard CRC-32 of the
  resource's own name, written as an unsigned decimal number. Example: the English legal screen texture
  `legal_screen` has CRC-32 `0x337abb4b` = 863681355, and its entry is directory record 3,480 (hash `0x82e87aaa`,
  263,680 bytes). The resource chunk inside the entry carries the same `0x337abb4b` as its name hash.

## Reads and buffers

| What | Size | Alignment | Notes |
| --- | ---: | ---: | --- |
| Default read buffer (file stream buffer) | 393,216 bytes (`0x60000`, 384 KB) | 128 bytes | Destination of a background read that names no buffer of its own |
| Background read with no size given | 393,216 bytes | | The size defaults to the default buffer's size |
| File name kept with a read request | up to 63 characters | | 64-byte field including the terminator |
| Read request record | 84 bytes | | Name, size, destination, completion notice and its argument, request number |
| Stream file records | 10 records of 20 bytes | 4 bytes | Allocated once; only one can be open at a time |
| Files open at once on the archive stream | 1 | | A second open while one is open gets nothing |
| Movie frame buffer | 1,146,880 bytes for a 640 x 448 movie | 128 bytes | Width x height x 4, rounded up to a multiple of 128 |

- A background read fetches one whole archive entry (or the given number of bytes of it) into memory in one
  piece. Entries larger than 384 KB are read into buffers given by the requester.
- A direct read fetches the requested number of bytes from the current position inside one entry; the position
  advances by that number.
- Reads go through the IOP streaming driver, which reads `WARRIORS.WAD` from the entry's sector-aligned start plus
  the position, and delivers the bytes straight into main memory.

## Reads in flight

| Fact | Value |
| --- | --- |
| Archive reads in progress at one time | 1 |
| Background reads that can wait | No fixed limit; they wait in a queue |
| Order of completion | Background reads finish in the order they were requested |
| Request numbers | Each background read gets the next number in a running count, starting at 1 |
| Completion notice | Each background read can carry a completion notice with one argument, delivered when its data is in memory |

## Blocking waits

- While the game waits for a read to finish (a direct read, or draining the background queue during loading), the
  controllers are still polled, at most once every 67 ms.

## Files read outside the archive

| Disc file | How it is read |
| --- | --- |
| `SYSTEM.CNF`, `SLUS_212.15` | By the console at boot |
| `MODULES\IOPRP300.IMG` and the six `.IRX` modules | At start-up, from the disc's file system (see the start-up page) |
| `WARRIORS.DIR` | Whole, at start-up |
| `PSS\<name>.BIK` | One movie at a time, streamed by the movie player from the disc's file system |
| `IOP\BFW.SND` | Opened on sound streams 1 to 12 at start-up |
| `IOP\MUSIC.SND` | Opened by the sound driver for music |
| `SLUS_212.15` | Reopened by the sound driver as a probe to test whether the disc can be read again after a disc error |

## Disc errors

The IOP sound driver watches the drive. When it reports a problem, the game shows one of three messages. The texts
come from the game's language files in the archive (entries 3,774 to 3,778, one per language); the message number
is the index in that file's HUD message table. English texts, from entry 3,775:

| Error kind | Condition | Message number | Message the PS2 player sees (English) |
| ---: | --- | ---: | --- |
| 4 | Disc tray open | 381 | The disc tray is open. Please make sure that The Warriors disc is inserted correctly and close the disc tray. |
| 5 | Tray closed, disc still cannot be read (no disc or another disc) | 382 | No disc or the wrong disc is in the disc tray. Please insert The Warriors disc. |
| 3 | Any other read failure the driver reports | 383 | Loading data... |
| 6 | (same message as kind 3) | 383 | Loading data... |

- The English file holds a second text for messages 375, 376 and 383 (the French file also for 381 and 382), chosen
  by the platform number the game reports to its scripts, which is 1 on the PS2. The second text of message 383
  reads "There's a problem with the disc you're using. It may be dirty or damaged."; the PS2 never shows it.
- The same files hold the texts in German (entry 3,774), Spanish (3,776), French (3,777) and Italian (3,778).
- While a disc message is up, game sound is paused. During level play the frozen game picture stays behind the
  message, tinted. The text is drawn at scale 0.55.
- Sound streams are paused when the drive reports the error and resume once the probe file opens again.
- Kinds 4 and 5 clear by themselves: once the disc reads again the message goes and the game carries on from where
  it stopped. No button press is needed.
- A kind 3 failure seen while the game is blocked waiting on a read (a direct read, or a load that waits for the
  queue to drain) puts message 383 on screen for good: the game stops there and only a console reset leaves it.
- A kind 3 failure seen during a background read that is not being waited on leaves the read pending until the
  drive recovers; the message clears at the same time.

## Controller messages

The same error screen shows the controller messages (from the same HUD table):

| Error kind | Condition | Message numbers | PS2 text (English) |
| ---: | --- | --- | --- |
| 1 | No controller connected | 376 | No controllers are connected. Please connect a controller and press the START button to continue. |
| 2 | A player's controller removed | 373, 374, 375 | A controller has been disconnected from controller port N. Please reconnect a controller to controller port N and press the START button to continue. |

- Kind 1 and kind 2 messages stay until START is released on a connected controller, which is then given to the
  player.

## Xbox version

- The Xbox disc has no `WARRIORS.DIR` or `WARRIORS.WAD`: its data sits in `Main.wad0` to `Main.wad3`, `Paks.wad0`
  to `Paks.wad2` and `System.wad0`, with an index file `XBoxWad.idx` (169,700 bytes).
- Xbox movies sit in a `bik` folder in lower-case names, three versions of each movie (plain, `_w` and `_hd`).
- The language files' second texts use Xbox wording (for example "press START" instead of "press the START
  button").

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x0040c5e0` | Open the PS2 archive and its directory at start-up | The archive at start-up |
| `0x00154d50` | Queue a background load of an archive file | Reads in flight |
| `0x001573e8` | Run the disc and controller error screen | Disc errors |
| `0x00157930` | Show the read-failure message that stops the game | Disc errors |

## Acceptance checks

- The PS2 `WARRIORS.DIR` is 128,428 bytes and lists 10,701 entries, every one starting on a 2,048-byte boundary of
  `WARRIORS.WAD`.
- No two archive entries overlap, and the padding after any entry is at most 2,046 bytes.
- The largest archive entry is entry 8,614 at 5,658,864 bytes; 396 entries are larger than 393,216 bytes.
- Directory record 3,480 has hash `0x82e87aaa` and size 263,680 bytes, and its resource chunk's name hash is
  `0x337abb4b`, the CRC-32 of `legal_screen`.
- Opening the disc tray during level play shows "The disc tray is open. Please make sure that The Warriors disc is
  inserted correctly and close the disc tray." over a tinted, frozen game picture, with the sound paused.
- Closing the tray on an empty drive or another disc shows "No disc or the wrong disc is in the disc tray. Please
  insert The Warriors disc."
- Reinserting the game disc and closing the tray clears the message without any button press and play carries on.
- With the language set to German, Spanish, French or Italian, the disc messages appear in that language.
- Starting the game with no controller connected shows "No controllers are connected. Please connect a controller
  and press the START button to continue."
- The Xbox disc holds no `WARRIORS.WAD`; it holds `XBoxWad.idx` and the `Main`, `Paks` and `System` archive parts.

## Open questions

- Which drive conditions the IOP sound driver reports as kind 3, and how a tester can produce one on real hardware.
- Which part of the game sets error kind 6.
- Whether the IOP streaming driver reads whole sectors and trims, or reads to the byte; the archive start is always
  sector aligned, read lengths are not.
- How the Xbox index file `XBoxWad.idx` maps names to the Xbox archive parts.
- Whether the Xbox executable reports platform number 2 to its scripts, so that the Xbox shows the second texts.
- What, if anything, the PS2 refreshes on screen during the 67 ms beat of a blocking wait besides polling the
  controllers.
