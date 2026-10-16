# PS2 archive directory

The PS2 disc keeps almost all game data in one archive made of two files: `WARRIORS.DIR`, a directory of fixed-size
entry records, and `WARRIORS.WAD`, the data those records point into. The archive stores no file names, only a 32-bit
hash of each name. Every value on this page was read from the NTSC-U disc image (SLUS-21215).

## The two files

| File | Start sector | Size in bytes | Size in sectors | Notes |
| --- | ---: | ---: | ---: | --- |
| `WARRIORS.DIR` | 2,996 | 128,428 | 63 (62.7 used) | Directory; the rest of its last sector is zero |
| `WARRIORS.WAD` | 3,059 | 1,495,371,776 | 730,162 | Data; starts in the sector after the DIR's last sector |

Both files carry the same ISO timestamp, 2005-09-25 05:26:55. The disc has no directory named `ee_files` (see the name
hash below).

## Directory layout

All numbers are 32-bit little-endian unsigned integers. The file is a 16-byte header followed directly by one 12-byte
record per entry, with nothing after the last record: 16 + 12 x 10,701 = 128,428 bytes, the DIR's exact size.

### Header

| Offset | Size | Type | Value on the disc | Meaning |
| ---: | ---: | --- | --- | --- |
| 0x00 | 4 | u32 | 10,701 (`0x000029CD`) | Number of entry records |
| 0x04 | 4 | u32 | 0 | Unknown, zero |
| 0x08 | 4 | u32 | 0 | Unknown, zero |
| 0x0C | 4 | u32 | 0 | Unknown, zero |

### Entry record

Record n starts at byte 16 + 12 x n of the DIR. An entry's index is its position in this table, counting from 0.

| Offset | Size | Type | Meaning | Units and range on the disc |
| ---: | ---: | --- | --- | --- |
| 0x00 | 4 | u32 | Start of the entry's data | Bytes from the start of `WARRIORS.WAD`; always a multiple of 2,048; 0 to 1,495,349,248 |
| 0x04 | 4 | u32 | Length of the entry's data | Exact length in bytes, not rounded; 3 to 5,658,864 |
| 0x08 | 4 | u32 | Name hash | CRC-32 of the entry's lower-case path (see below) |

### Ordering

| Property | Value |
| --- | --- |
| Order of records | Strictly ascending by data offset; entry 0 starts at WAD offset 0 |
| Order by hash | None; the hashes are not sorted |
| Distinct hashes | 10,701 (no two entries share a hash) |
| Entries with size 0 | None |
| Overlapping entries | None |

## How the directory maps onto the data file

| Property | Value |
| --- | --- |
| Offset unit | Bytes, relative to the WAD's first byte |
| Offset alignment | 2,048 bytes (one disc sector) for all 10,701 entries; only 5,360 are also 4,096-aligned |
| Placement | Every entry starts at the previous entry's end rounded up to the next multiple of 2,048 (true for all 10,700 consecutive pairs) |
| Disc position of an entry | Disc sector 3,059 plus the entry offset divided by 2,048; the highest entry offset is WAD sector 730,151 |
| Last entry | Entry 10,700 at offset 1,495,349,248, size 21,913, ending at byte 1,495,371,161 |
| WAD size | The last entry's end rounded up to 2,048 bytes (615 bytes of padding after it) |
| Sum of entry sizes | 1,484,729,244 bytes |
| Padding in total | 10,642,532 bytes |
| Sizes that are an exact multiple of 2,048 | 48 entries, which therefore have no padding after them |

The padding between entries is not zero-filled. Of the 10,700 gaps after entries 0 to 10,699, 48 are empty, 56 are all
zero, and the remaining 10,596 hold nonzero bytes that belong to no entry. The 615-byte tail after the last entry
holds 152 nonzero bytes. Entry sizes in the directory, not the padding, mark where each entry's data ends.

## Name hash

The hash field holds the standard CRC-32 (the zip and PNG checksum: reflected polynomial `0xEDB88320`, initial value
and final XOR `0xFFFFFFFF`) of the ASCII string made of the prefix `./ee_files/` followed by the entry's file name in
lower case, with no terminator. For example, the name `accoustic.anm` gives the string `./ee_files/accoustic.anm`,
whose CRC-32 is `0x10CE8A36`, the hash stored for entry 3,615.

Evidence, all computed against the hashes on the disc:

| Test | Result |
| --- | --- |
| 6,313 candidate names from earlier research, hashed with the prefix `./ee_files/` | All 6,313 match a stored hash, each at the index the earlier research gave |
| The same names hashed with no prefix, with `ee_files/` (no leading dot), or in upper case | 0 matches |
| Name-like strings found inside the WAD's own data, hashed the same way | 304 distinct names match an entry (303 `.anm` names and `cn12.bmp`) |
| Chance matches among those strings | One: the string `-61.622` has the same CRC-32 (`0xB25ECE76`) as `level110_1.pak`, entry 7,082 |

All recovered names are bare lower-case file names with an extension and no folder part. Because the hash is only 32
bits, a string that matches a hash is a name only when it is also plausible; the `-61.622` case shows that chance
matches occur.

## Example entries

| Index | Name | Hash | WAD offset | Size |
| ---: | --- | --- | ---: | ---: |
| 0 | (unknown) | `0xF3D298EB` | 0 | 17,840 |
| 1 | (unknown) | `0xA524F9A0` | 18,432 | 263,680 |
| 2 | (unknown) | `0x3B78FA64` | 282,624 | 5,552 |
| 3,615 | `accoustic.anm` | `0x10CE8A36` | 160,950,272 | 7,856 |
| 3,616 | `armies.msb` | `0xE0ADC29F` | 160,958,464 | 1,330,592 |
| 3,742 | `cn12.bmp` | `0x76DC09A1` | 165,752,832 | 98,358 |
| 6,914 | `level101_objs.txt` | `0x6C0DCA31` | 420,075,520 | 3 |
| 7,082 | `level110_1.pak` | `0xB25ECE76` | 447,918,080 | 889,616 |
| 8,614 | `level64_1.pak` | `0xB6A6F294` | 918,982,656 | 5,658,864 |
| 10,699 | `write_on_pad_e.anm` | `0xCB543D03` | 1,495,345,152 | 2,192 |
| 10,700 | (unknown) | `0x8617E7C0` | 1,495,349,248 | 21,913 |

Entry 8,614 (`level64_1.pak`) is the largest entry. The smallest entries are 3 bytes long, for example
`level101_objs.txt` and `level104_objs.txt`.

## Entries per extension

Counts of entries whose name is confirmed by its hash, grouped by the name's extension. Byte totals are the sums of
the entries' sizes in the directory.

| Extension | Entries | Total bytes | Smallest | Largest | Index range |
| --- | ---: | ---: | ---: | ---: | --- |
| `anm` | 563 | 2,590,304 | 448 | 70,800 | 3,615 to 10,699 |
| `bmp` | 1 | 98,358 | 98,358 | 98,358 | 3,742 |
| `cnk` | 1 | 66,400 | 66,400 | 66,400 | 10,456 |
| `glr` | 1 | 635,488 | 635,488 | 635,488 | 10,671 |
| `ico` | 1 | 79,128 | 79,128 | 79,128 | 10,669 |
| `lev` | 64 | 31,902,768 | 31,872 | 1,205,264 | 6,864 to 9,605 |
| `lua` | 356 | 9,913,977 | 191 | 308,938 | 3,675 to 10,676 |
| `mem` | 158 | 14,400 | 20 | 380 | 6,870 to 9,714 |
| `met` | 1 | 2,625 | 2,625 | 2,625 | 3,743 |
| `msb` | 20 | 26,148,688 | 57,152 | 1,813,120 | 3,616 to 10,526 |
| `msd` | 20 | 19,920 | 24 | 2,560 | 3,617 to 10,527 |
| `pak` | 236 | 467,414,304 | 9,152 | 5,658,864 | 4,106 to 9,718 |
| `scn` | 2,760 | 197,515,792 | 464 | 322,864 | 3,621 to 10,687 |
| `sec` | 1,910 | 310,644,352 | 1,002 | 346,962 | 6,868 to 9,713 |
| `txt` | 63 | 1,111,944 | 3 | 109,512 | 6,914 to 9,722 |
| `wld` | 158 | 70,148,764 | 12,948 | 1,316,524 | 6,871 to 9,715 |
| Named, all extensions | 6,313 | 1,118,307,212 | | | |
| Not yet named | 4,388 | 366,422,032 | 20 | 2,215,040 | |
| All entries | 10,701 | 1,484,729,244 | 3 | 5,658,864 | 0 to 10,700 |

The unnamed entries fall in 106 runs of consecutive indexes. The first run covers indexes 0 to 3,614 (3,615 entries,
none named); the other 773 unnamed entries are spread over 105 shorter runs. All `lev`, `mem`, `sec`, `txt` and
`wld` entries lie between indexes 6,864 and 9,722; `mem` and `wld` have the same count, 158 each.

## Differences from the Xbox disc

The Xbox disc has no `WARRIORS.DIR` or `WARRIORS.WAD`. Its archive set is `XBoxWad.idx` (169,700 bytes) with data
files `Main.wad0` to `Main.wad3`, `Paks.wad0` to `Paks.wad2` and `System.wad0`. Whether the Xbox index uses the same
record layout or name hash was not examined.

## Behaviour

| Outcome | Value |
| --- | --- |
| Directory at start-up | The entry count and all 10,701 records are held in memory, and `WARRIORS.WAD` is open for reading |
| Header bytes 4 to 15 | Not used by the game |
| Name lookup | A path finds the entry whose stored hash is the CRC-32 of that path in lower case |
| Letter case | Upper- and lower-case spellings of a path find the same entry |
| Path with no matching hash | Finds no entry |

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x0040c5e0` | Open the disc archive at start-up | Behaviour |
| `0x001490b8` | Find an archive entry by its path | Behaviour |

## Acceptance checks

| Outcome |
| --- |
| The PS2 disc's `WARRIORS.DIR` is 128,428 bytes and its first four bytes give an entry count of 10,701. |
| Bytes 4 to 15 of `WARRIORS.DIR` are zero. |
| `WARRIORS.DIR` is exactly 16 bytes plus 12 bytes for each of its 10,701 entries. |
| Every entry's offset is a multiple of 2,048 and every entry's offset plus size lies within the WAD's 1,495,371,776 bytes. |
| The entries' offsets rise strictly with their index, and each entry starts at the previous entry's end rounded up to 2,048. |
| Entry 0 starts at WAD offset 0 with size 17,840; entry 10,700 starts at 1,495,349,248 with size 21,913. |
| The WAD's size equals the end of entry 10,700 rounded up to 2,048 bytes. |
| All 10,701 stored hashes are distinct. |
| The CRC-32 of `./ee_files/accoustic.anm` is `0x10CE8A36`, the hash of entry 3,615. |
| The CRC-32 of `./ee_files/level64_1.pak` is `0xB6A6F294`, the hash of entry 8,614, the largest entry at 5,658,864 bytes. |
| 6,313 known names each hash to a distinct entry, with the per-extension counts in the table above (2,760 `scn`, 1,910 `sec`, 563 `anm`, 356 `lua`, 236 `pak`). |
| No entry with index 0 to 3,614 has a known name. |
| The padding between entries is not all zero. |

## Open questions

- The meaning of header bytes 4 to 15, which are zero on this disc and unused by the game (see Behaviour).
- The sector reads themselves happen in the IOP module `IOP.IRX`, which is not part of `SLUS_212.15`; its addresses
  are not listed here.
- The names of the 4,388 unnamed entries, including all of indexes 0 to 3,614 and entry 10,700.
- Whether the entry order (by offset) follows any grouping beyond what the index ranges per extension show.
- Where the nonzero padding bytes between entries come from; they belong to no entry.
- Whether the Xbox `XBoxWad.idx` shares the PS2 record layout and the `./ee_files/` name hash.
