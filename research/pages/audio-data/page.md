# Audio data formats

How the game's sound effects, speech and music are stored on the two discs: the four audio chunks of the global
list file, the sound banks in the archive, the two big stream files on the PS2 disc and the XACT banks on the Xbox
disc. Playback is not covered here.

Facts come from the NTSC-U PS2 disc (`WARRIORS.WAD` entries `warriors.glr`, the `.msb` and `.msd` banks, and the
files `IOP/BFW.SND` and `IOP/MUSIC.SND`), from the Xbox disc's `audio` folder and from the PS2 executable's data.
How the archive and its chunk files are laid out is on the pages ps2-disc-layout, archive-contents, chunk-container
and global-lists; the Xbox file system is on xbox-disc-layout. All numbers are little-endian.

## Where the audio lives

| Data | PS2 location | Count | Encoding |
| --- | --- | --- | --- |
| Sound list | chunk `0x29` of `warriors.glr` | 25,395 records, 23,185 sounds | table |
| Sound classes | chunk `0x48` of `warriors.glr` | 246 classes | table |
| Stereo table | chunk `0x49` of `warriors.glr` | 248 records | table |
| Music list | chunk `0x31` of `warriors.glr` | 345 tracks | table |
| Streamed sounds | `IOP/BFW.SND`, 1,402,861,568 bytes | 22,720 sounds | headerless SPU ADPCM |
| Bank sounds | 21 pairs of `.msb` data and `.msd` index in `WARRIORS.WAD` | 2,675 bank entries, 465 sounds | headerless SPU ADPCM |
| Music | `IOP/MUSIC.SND`, 960,063,488 bytes | 345 stereo tracks | headerless SPU ADPCM |

Every sound is either streamed or in banks, never both: the 22,720 streamed sounds and the 465 bank sounds make up
the 23,185 distinct sounds of the list. No audio file on the PS2 disc has a header of its own; every length, rate
and layout comes from the tables in `warriors.glr`.

## Sound names and hashes

- A sound's name is a lower-case path with `/` separators. Every name recovered so far starts with `vags/`, for
  example `vags/misc/aihealth`, `vags/ambient/beach/2gulls` and `vags/speeches/l14/l14_t1_009`.
- A sound is keyed by the standard CRC-32 of its name's bytes, the same CRC-32 as in zlib and PNG.
  `vags/misc/aihealth` hashes to `0x9d2e732d` and `vags/interface/powerup` to `0x7b3534e9`.
- The sound list holds hashes only. A second copy of the list keeps the last 15 characters of each name (see
  "Annotated list file").
- A music track is keyed the same way by its full name, which its record also stores: `music/103_loop` hashes to
  `0x0b2059c0`.
- A name whose hash has no record in the sound list finds no sound.

## Sound list (chunk 0x29)

The chunk's data is a 4-byte record count followed by the records; the data is padded with 12 bytes to a multiple
of 16 (the padding is not all zero: its last word is `0x00000f99`).

| Offset | Size | Type | Meaning |
| --- | --- | --- | --- |
| `0x00` | 4 | u32 | Data size in bytes (see below for stereo sounds) |
| `0x04` | 4 | u32 | Byte offset: into `IOP/BFW.SND` for a streamed sound, into one bank's `.msb` for a bank sound |
| `0x08` | 4 | u32 | CRC-32 of the sound's name |
| `0x0c` | 1 | u8 | Random pitch variation in percent (0, 5, 10, 15, 20, 25, 30 or 35) |
| `0x0d` | 1 | u8 | Volume in percent (0 to 250 on the disc) |
| `0x0e` | 1 | u8 | Index into the sample-rate table (see "Sample rates") |
| `0x0f` | 1 | u8 | Sound class: index into chunk `0x48` (0 to 245) |

- The 25,395 records are sorted by hash in ascending unsigned order.
- A streamed sound has exactly one record. A bank sound has one record per bank that holds it, each with that
  bank's offset, so its records sit next to each other with the same hash, size, volume, rate and class. The 465
  bank sounds have 2,675 records; one sound appears in up to 12 banks.
- Every data size is a multiple of 16 bytes, from 16 to 7,224,944 bytes.
- 23,586 records have no pitch variation.

| Sound | Size | Offset | Rate | Class | Data |
| --- | --- | --- | --- | --- | --- |
| `vags/ambient/beach/2gulls` | 129,872 | 10,240 | index 34, 22,050 Hz | 45 | streamed, the first sound in `BFW.SND` |
| `vags/misc/hudalarm` | 49,120 | 995,475,456 | index 36, 22,500 Hz | 117 | streamed |
| `vags/misc/aihealth` | 2,208 | 994,617,344 | index 34, 22,050 Hz | 117 | streamed |
| `vags/speeches/l14/l14_t1_009` | 64 | 1,111,922,688 | index 49, 32,250 Hz | 224 | streamed, stereo |
| `vags/interface/powerup` | 3,760 | 459,696 in `sound.msb` | index 28, 18,000 Hz | 4 | 9 records, one per bank |
| `vags/load_screen/load_00_l` | 873,008 | 0 in `load_00.msb` | index 46, 30,000 Hz | 2 | bank |

## Sample rates

The rate byte of a sound record indexes a table of 72 rates in hertz held in the executable's data.

| Index | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | 0 | 750 | 1,500 | 2,250 | 3,000 | 3,750 | 4,500 | 5,250 |
| 8 | 6,000 | 6,750 | 7,500 | 8,000 | 8,250 | 9,000 | 9,750 | 10,500 |
| 16 | 11,000 | 11,025 | 11,250 | 12,000 | 12,750 | 13,500 | 14,250 | 15,000 |
| 24 | 15,750 | 16,000 | 16,500 | 17,250 | 18,000 | 18,750 | 19,500 | 20,250 |
| 32 | 21,000 | 21,750 | 22,050 | 22,250 | 22,500 | 23,250 | 24,000 | 24,750 |
| 40 | 25,500 | 26,250 | 27,000 | 27,750 | 28,500 | 29,250 | 30,000 | 30,750 |
| 48 | 31,500 | 32,250 | 33,000 | 33,750 | 34,500 | 35,250 | 36,000 | 36,750 |
| 56 | 37,500 | 38,250 | 39,000 | 39,750 | 40,500 | 41,250 | 42,000 | 42,750 |
| 64 | 43,500 | 44,100 | 44,250 | 45,000 | 45,750 | 46,500 | 47,250 | 48,000 |

| Rate | Streamed sounds | Bank entries |
| --- | --- | --- |
| 22,500 Hz | 17,643 | 332 |
| 22,050 Hz | 3,590 | 741 |
| 16,500 Hz | 321 | 665 |
| 16,000 Hz | 312 | 24 |
| 32,250 Hz | 240 | 2 |
| 44,100 Hz | 137 | 84 |
| 48,000 Hz | 96 | 0 |
| Other rates (20 more) | 381 | 827 |

The sounds use 27 of the 72 rates.

## Sound classes (chunk 0x48)

The chunk's data is a plain array of 5-byte classes with no count: 246 classes in 1,230 bytes, padded with 2 zero
bytes to 1,232. Every class is used by at least one sound.

| Offset | Size | Type | Meaning |
| --- | --- | --- | --- |
| 0 | 1 | u8 | Near distance in metres (0 to 255) |
| 1 | 1 | u8 | Far distance in units of 5 metres (0 to 40, so up to 200 metres) |
| 2 | 1 | u8 | Flags (below) |
| 3 | 1 | u8 | Priority (2 to 21) |
| 4 | 1 | u8 | Channels: 1, or 2 for class 224 only |

| Flag | What the disc shows | Classes | Distinct sounds |
| --- | --- | --- | --- |
| `0x01` | Ambient loop classes (city, small loops, level ambience); 58 of the 64 are also streamed; 31 of the 41 loop-flagged bank entries are in these classes | 64 | 236 |
| `0x02` | Every class with it has non-zero near and far distances; never on the same class as `0x08` | 204 | 5,743 |
| `0x04` | Exactly the streamed sounds: every sound in such a class is in `IOP/BFW.SND`, every other sound is in banks | 201 | 22,720 |
| `0x08` | Every class with it has non-zero near and far distances; mostly character voices | 20 | 16,499 |
| `0x10` | Only together with `0x02` or `0x08`; mostly character speech; meaning unknown | 32 | 17,460 |
| `0x20` | Only in flag values `0x27` (5 classes) and `0x25` (1); machine, fan, air-conditioner, factory and noise loops, plus a few such as clock ticks, crickets and a siren; meaning unknown | 6 | 45 |
| `0x40` | Only in flag values `0x42`, `0x46` and `0x4c`; gang members' voice commands (attack, defend, follow, hold, scatter, steal, vandal and others) plus 5 groans; meaning unknown | 5 | 231 |
| `0x80` | Never set | 0 | 0 |

- The 22 classes with neither `0x02` nor `0x08` all have a near and far distance of 0.
- The `0x20` classes are 68, 86, 103, 104, 173 and 196; the `0x40` classes are 7, 8, 124, 130 and 143.

| Class | Near | Far | Flags | Priority | Channels | Used by |
| --- | --- | --- | --- | --- | --- | --- |
| 4 | 0 m | 0 m | `0x00` | 12 | 1 | bank interface sounds such as `vags/interface/powerup` |
| 45 | 5 m | 50 m | `0x06` | 20 | 1 | `vags/ambient/beach/2gulls` |
| 117 | 0 m | 0 m | `0x04` | 12 | 1 | `vags/misc/hudalarm`, `vags/misc/aihealth` |
| 224 | 0 m | 0 m | `0x04` | 3 | 2 | all 248 stereo sounds |

## Stereo table (chunk 0x49)

The chunk's data starts with a 16-byte header, a u32 record count (248) and 12 zero bytes, followed by 248 records
of 16 bytes and no padding (3,984 bytes in all).

| Offset | Size | Type | Meaning |
| --- | --- | --- | --- |
| `0x00` | 4 | u32 | CRC-32 of the sound's name |
| `0x04` | 4 | u32 | Interleave: bytes per channel block (32,768 in every record) |
| `0x08` | 4 | u32 | Bytes of audio in the last block of each channel (64 to 32,752) |
| `0x0c` | 4 | u32 | Number of blocks per channel (2 to 71) |

- The records are in the order of their sounds' offsets in `IOP/BFW.SND`, not in hash order.
- Every record matches a sound of class 224, and every class 224 sound has a record. All 248 are streamed.
- The sound list's size field for these 248 sounds holds 64, not the data's length. Their data fills whole blocks
  of 32,768 bytes for both channels in `IOP/BFW.SND` (layout under "Streamed sounds"), and one further 2,048-byte
  sector follows before the next sound. A sound of 3 blocks per channel takes 196,608 bytes plus that sector.
- 240 of the 248 play at 32,250 Hz; the others are at 22,500 Hz (3), 44,100 Hz (3) and 27,000 Hz (2). By their name
  tails most are cutscene speech, for example `vags/speeches/l14/l14_t1_009`: 3 blocks with 24,240 bytes in the
  last, 89,776 bytes and 157,108 samples per channel, 4.87 seconds.

## Sound banks

A bank is a pair of archive entries with the same name: `<bank>.msb` holds the sound data and `<bank>.msd` indexes
it. The `.msd` is an array of 8-byte pairs ending with one all-zero pair.

| Offset | Size | Type | Meaning |
| --- | --- | --- | --- |
| `0x00` | 4 | u32 | CRC-32 of the sound's name |
| `0x04` | 4 | u32 | Byte offset of the sound's data in the `.msb` |

- The pairs are in ascending offset order. The first offset is 0, and no hash appears twice in one bank.
- A sound's length is the size field of the sound-list record with the same hash and the same offset. Every one of
  the 2,675 pairs over all banks equals the hash and offset of a sound-list record, and the pairs and the bank
  records of the sound list are the same set.
- The `.msb` is the bank's sounds back to back with no header and no gaps: each sound ends where the next begins and
  the last ends at the end of the file.

| Bank | `.msb` bytes | Sounds |
| --- | --- | --- |
| `armies` | 1,330,592 | 261 |
| `armload` | 1,055,936 | 2 |
| `birdie` | 1,244,624 | 204 |
| `diego` | 919,344 | 190 |
| `gallery` | 1,475,456 | 287 |
| `level3` | 1,292,176 | 243 |
| `level81` | 1,607,392 | 311 |
| `lizzie` | 1,359,248 | 227 |
| `load_00` to `load_06` | 1,555,040 to 1,813,120 | 2 each |
| `luther` | 1,116,816 | 188 |
| `menu` | 175,536 | 14 |
| name not recovered (directory hashes `0x115d636b` and `0xf83ec65e`) | 1,211,904 | 205 |
| `pause` | 57,152 | 4 |
| `sound` | 1,613,664 | 319 |
| `spookorama` | 1,142,160 | 206 |

- The seven `load_` banks and `armload` each hold one stereo piece as two mono sounds of equal size whose names end
  `_l` and `_r`, for example `vags/load_screen/load_00_l` and `vags/load_screen/load_00_r`.

| Bank | Bytes per sound | Rate | Length |
| --- | --- | --- | --- |
| `armload` | 527,968 | 32,250 Hz | 28.65 s |
| `load_00` | 873,008 | 30,000 Hz | 50.93 s |
| `load_01` | 777,520 | 30,000 Hz | 45.36 s |
| `load_02` | 806,416 | 22,050 Hz | 64.00 s |
| `load_03` | 823,120 | 30,000 Hz | 48.02 s |
| `load_04` | 906,560 | 30,000 Hz | 52.88 s |
| `load_05` | 820,112 | 44,100 Hz | 32.54 s |
| `load_06` | 872,560 | 30,000 Hz | 50.90 s |

- Each bank sound ends in a fixed way: 2,630 of the 2,675 end with a frame flagged end, a stop frame (flags `0x07`,
  sample bytes all `0x77`) and one last 16-byte frame, which is all zero in 2,659 of the 2,675 entries.
- 41 bank entries are looped in the data: their first frame carries flags `0x06` and their last audio frame flags
  `0x03`. 31 of them are in a class with flag `0x01`; 10 are not, and 2 entries in such classes carry no loop
  flags.

## Streamed sounds

`IOP/BFW.SND` (1,402,861,568 bytes) holds the 22,720 streamed sounds, each at the offset its sound-list record
gives.

- Every offset is a multiple of 2,048 and every sound lies inside the file. No two sounds overlap or share data.
- A mono sound is its size field's bytes of ADPCM frames; the next sound starts at the next 2,048-byte boundary or
  later.
- In a random 600 mono streamed sounds the first frame is all zero in 585. 587 of the 600 have no frame flagged end;
  the other 13 end with an end-flagged frame followed by a frame with flags `0x07`. The size field, not the data,
  marks where a sound ends.
- A stereo sound (class 224) is blocks of 32,768 bytes, alternating left and right: left block 0, right block 0,
  left block 1 and so on. The last pair of blocks is full length, but only the stereo table's last-block count of
  bytes in each is audio.
- The first 10,240 bytes of the file and 567 other stretches totalling 17,027,072 bytes (the largest 12,828,672) are
  not covered by any record.
- The streamed sounds last about 29.5 hours in all.

## Music list (chunk 0x31)

The chunk's data is a 4-byte track count (345) followed by 104-byte records, padded with 4 bytes to a multiple of 16
(the padding holds `0x000006c9`).

| Offset | Size | Type | Meaning |
| --- | --- | --- | --- |
| `0x00` | 4 | f32 | Volume (1.0 in every record on the disc) |
| `0x04` | 4 | u32 | Sample rate in hertz: 30,000 (293 tracks), 44,100 (38) or 32,250 (14) |
| `0x08` | 4 | u32 | 64 in every record; meaning unknown |
| `0x0c` | 4 | u32 | 32 in every record; meaning unknown |
| `0x10` | 4 | u32 | Byte offset in `IOP/MUSIC.SND` |
| `0x14` | 4 | u32 | Channels (2 in every record) |
| `0x18` | 4 | u32 | Interleave: bytes per channel block (a multiple of 16, for example 29,376) |
| `0x1c` | 4 | u32 | Number of blocks per channel |
| `0x20` | 4 | u32 | Bytes of audio in the last block of each channel (never more than the interleave) |
| `0x24` | 4 | u32 | CRC-32 of the name |
| `0x28` | 64 | char | Name, zero-padded, for example `music/103synth1_118c` |

All 345 names are distinct, all start with `music/`, and each record's hash is the CRC-32 of its name.

## Music file

`IOP/MUSIC.SND` (960,063,488 bytes) holds the 345 music tracks.

- Each track starts at a multiple of 2,048 and is laid out like a stereo streamed sound: left block, right block,
  alternating, each block the record's interleave long. The last pair is full length and holds the last-block count
  of audio bytes per channel.
- `music/103synth1_118c` takes 2,232,576 bytes (38 pairs of 29,376-byte blocks). Each channel holds 37 full blocks
  and 28,832 bytes, 1,115,744 bytes of audio or 1,952,552 samples.
- The tracks follow each other in offset order from offset 0 with at most one 2,048-byte sector between two tracks;
  the last ends at 960,062,208.
- The tracks run from 1.68 seconds (`music/armies_inter_02`) to 351.97 seconds (`music/long_ambience1`), 445.8
  minutes in all.

| Track | Offset | Rate | Interleave | Blocks | Last block | Length |
| --- | --- | --- | --- | --- | --- | --- |
| `music/103synth1_118c` | 0 | 30,000 Hz | 29,376 | 38 | 28,832 | 1,952,552 samples, 65.09 s |
| `music/103_loop` | 2,234,368 | 30,000 Hz | 30,912 | 29 | 30,704 | 1,568,420 samples, 52.28 s |
| `music/armies_inter_02` | 354,422,784 | 30,000 Hz | 28,800 | 1 | 28,752 | 50,316 samples, 1.68 s |
| `music/long_ambience1` | 672,079,872 | 30,000 Hz | 31,104 | 194 | 30,688 | 10,559,080 samples, 351.97 s |

## ADPCM encoding

All PS2 audio data is the console's standard SPU ADPCM, mono per channel, in 16-byte frames of 28 samples.

| Byte | Meaning |
| --- | --- |
| 0 | Low nibble: shift (0 to 12); high nibble: filter (0 to 4) |
| 1 | Flags: `0x01` end, `0x02` repeat, `0x04` loop start |
| 2 to 15 | 28 signed 4-bit samples, low nibble first |

- The five filters' coefficient pairs, in 64ths, are (0, 0), (60, 0), (115, -52), (98, -55) and (122, -60).
- In the 1,910,052 frames of a random 600 mono streamed sounds no filter is above 4 and no shift above 12.
- 13 of the 1,710,037 bank frames fall outside these ranges, all 13 of them in the 16 bank entries whose final frame
  is not zero.
- Every 16-byte frame gives 28 samples: the 129,872 bytes of `vags/ambient/beach/2gulls` hold 227,276 samples.

## Annotated list file

The archive holds a second list file (directory hash `0xe87e18d5`, 1,041,808 bytes) beside `warriors.glr`
(directory hash `0xf5c908b1`, 635,488 bytes). It has the same nine chunks, and all but the sound list are byte for
byte the same.

- Its sound list has the same count and the same 25,395 records in the same order, but each record is 32 bytes: 16
  bytes of name text, then the usual 16-byte record.
- The name text is the last 15 characters of the sound's name, ending in a zero byte (14 characters for the 11
  names that are only 14 long). For the first record it reads `s/173/engage_03`.
- Wherever a full name is known, its last 15 characters equal the stored text (3,755 records checked).

## Xbox audio

The Xbox disc uses Microsoft XACT instead: the `audio` folder holds 369 wave banks `xbox000.xwb` to `xbox368.xwb`
(2,217,542,292 bytes in all), one sound bank `xbox000.xsb` (4,544,708 bytes), `Warriors.bin` (15,244 bytes),
`dsstdfx.bin` (18,608 bytes) and `version.txt`. There is no `BFW.SND` or `MUSIC.SND`.

| Wave bank offset | Size | Meaning |
| --- | --- | --- |
| `0x00` | 4 | Magic `WBND` |
| `0x04` | 4 | Version: 3 in all banks |
| `0x08` | 32 | Four regions as offset and length: bank data (`0x28`, 40), entry table (`0x50`, 24 x entries), names (0, 0: no names), wave data |
| `0x28` | 4 | Flags: 0 for `xbox000` (in memory), 1 for all others (streamed) |
| `0x2c` | 4 | Number of entries |
| `0x30` | 16 | Bank name, zero-padded, the file name without extension |
| `0x40` | 4 | Entry size: 24 |
| `0x44` | 4 | 64 in all banks |
| `0x48` | 4 | Alignment: 4 for `xbox000`, 512 for `xbox001`, 2,048 for all others |

| Entry offset | Size | Meaning |
| --- | --- | --- |
| `0x00` | 4 | Flags: `0x10000`, `0x10002` or 0 |
| `0x04` | 4 | Format: bits 0 to 1 codec (1 Xbox ADPCM, 0 PCM), bits 2 to 4 channels, bits 5 to 30 sample rate in hertz |
| `0x08` | 4 | Offset in the wave data region (a multiple of the alignment) |
| `0x0c` | 4 | Length in bytes |
| `0x10` | 8 | Loop start and length: 0 in every entry |

| Bank | Entries | Holds |
| --- | --- | --- |
| `xbox000` | 542 | in-memory sounds, 5.9 minutes |
| `xbox001` | 1,014 | streamed sounds, 72.4 minutes |
| `xbox002` | 19,276 | streamed sounds, mostly speech, 1,310.7 minutes |
| `xbox003` | 2,228 | streamed sounds, including 237 stereo at 32,250 Hz |
| `xbox004` | 99 | streamed sounds |
| `xbox005` to `xbox368` | 1 each | one stereo music track each: 362 at 26,500 Hz, 2 at 30,000 Hz |

- 23,521 of the 23,523 entries are Xbox ADPCM (36 bytes per channel for every 64 samples); 2 are 16-bit PCM. Sample
  rates match the PS2 rates of the same kinds of sound, for example 240 stereo entries at 32,250 Hz on both.
- The Xbox music is resampled: 344 of the 345 PS2 tracks have a single-track Xbox bank of the same length within
  0.05 seconds, for example `xbox005` (65.08 s, `music/103synth1_118c`), `xbox144` (1.68 s) and `xbox270`
  (351.97 s). 20 of the 364 Xbox music banks match no PS2 track.
- The sound bank `xbox000.xsb` starts with the magic `SDBK` and version 11. Its header gives the count 23,523, the
  total of wave bank entries, three times, and it names the 369 wave banks. It holds no sound names.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x0040d900` | Load the game-wide lists, the four audio tables among them | Where the audio lives |
| `0x0014c620` | Load a sound bank and its index | Sound banks |
| `0x00110d78` | Find a sound by its name | Sound names and hashes |

## Acceptance checks

- The sound list in `warriors.glr` holds 25,395 records sorted by hash, with 23,185 distinct hashes.
- 22,720 sound-list records are in classes flagged streamed, and each lies inside `IOP/BFW.SND` at a multiple of
  2,048.
- `vags/ambient/beach/2gulls` is the record with hash `0x66381d3b`; it decodes from offset 10,240 of `IOP/BFW.SND`
  to 227,276 mono samples, 10.31 seconds at 22,050 Hz.
- `vags/misc/hudalarm` decodes to 85,960 samples, 3.82 seconds at 22,500 Hz, ending in near silence.
- The sound classes chunk holds 246 five-byte classes, and class 224 is the only one with 2 channels.
- The stereo table lists 248 sounds, all with an interleave of 32,768; `vags/speeches/l14/l14_t1_009` decodes to two
  channels of 157,108 samples, 4.87 seconds at 32,250 Hz.
- The archive holds 21 bank pairs; each `.msd` ends with a zero pair, and its sounds fill the `.msb` exactly.
- `vags/interface/powerup` has 9 sound-list records, one for each bank that holds it, each 3,760 bytes at 18,000 Hz.
- `menu.msb` (175,536 bytes) holds 14 sounds, starting with `vags/interface/menu/synthy_14` at offset 0.
- The music list names 345 tracks, all stereo, filling `IOP/MUSIC.SND` from offset 0 to 960,062,208.
- `music/103synth1_118c` decodes to 65.09 seconds of stereo music at 30,000 Hz; `music/long_ambience1` to 351.97
  seconds.
- The Xbox `audio` folder holds 369 wave banks with 23,523 entries; `xbox270.xwb` holds one stereo track of 351.97
  seconds at 26,500 Hz.

## Open questions

- The name of the 21st bank (directory hashes `0x115d636b` for the `.msb` and `0xf83ec65e` for the `.msd`) and of
  the annotated list file (`0xe87e18d5`) were not recovered.
- The meaning of the music records' constant fields at `0x08` (64) and `0x0c` (32).
- The meaning of class flags `0x10`, `0x20` and `0x40`.
- Whether the two distance flags `0x02` and `0x08` differ in any way a player can notice.
- What the first 10,240 bytes and the other 567 unreferenced stretches of `IOP/BFW.SND` hold; they decode as ADPCM.
- Why 10 bank sounds outside looping classes carry loop flags, and why 16 bank entries end in a non-zero frame.
- Which PS2 sounds each Xbox wave bank entry matches (the Xbox banks hold no names), and what the 20 Xbox music
  tracks without a PS2 match are.
- What the Xbox wave bank entry flags (`0x10000`, `0x10002`), `Warriors.bin` and the Xbox sound bank's records hold.
- Whether the Xbox archive carries the same four audio chunks.
