# Audio data (sound list, banks, BFW.SND, MUSIC.SND)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`) and the NTSC-U disc. The
disc checks (2026-10-06) were made with `coney-tools audio` and throwaway scripts outside the repository and report
counts only. How the game plays this data is on [Sound](../sound.md).

## Purpose

Where every sound, piece of music and line of speech lives on the disc, how a sound's name leads to its bytes, and
how the bytes decode. In one paragraph: a sound is found by the CRC-32 of its name in the **sound list** (chunk
`0x29` of `warriors.glr`); its record gives a size, an offset, a sample-rate index and a **sound class** (chunk
`0x48`) whose flags say whether it streams from `IOP/BFW.SND` or plays from a **bank** (`<name>.msb` with its index
`<name>.msd` in the WAD) loaded into sound RAM. Music is separate: the **music list** (chunk `0x31`) points into
`IOP/MUSIC.SND`. Everything is headerless PS2 SPU2 ADPCM; stereo data is two mono streams interleaved in blocks.
Cutscene movies (`PSS/*.BIK`) carry their own Bink audio and are not covered here.

## Original structure

The game drives Sony's SCEE **MultiStream** library (its `SOUND_*` error strings, `0x0054b708`-`0x0054d4b8`; the
IOP half is `MODULES/IOP.IRX`, [File I/O](../file-io.md#the-iop-stream)). Public knowledge of that library and of
the SPU2 is cited as such; everything else is from this executable.

| Address | Name (ours) | Role | Evidence |
| --- | --- | --- | --- |
| `0x0010f320` / `0x0010f900` | `SoundList_Load` | pops chunk `0x29`, makes one 4-byte entry per record (a pointer to it) | confirmed (code) |
| `0x001119d0` | `SoundList_Find` | binary search of the entries by the record's `+0x08` | confirmed (code) |
| `0x0010f360` / `0x0010f9d8` | `MusicList_Load` | pops chunk `0x31`; per 104-byte record a 16-byte track `{record, crc32(name), 1, 2000}` | confirmed (code) |
| `0x0010f3a0` | `SoundClasses_Load` | pops chunk `0x48`; the class table pointer `0x00598670` | confirmed (code) |
| `0x0010f3d8` / `0x0010f988` | `StereoTable_Load` | pops chunk `0x49`; count `0x00598678`, records `0x00598674` | confirmed (code) |
| `0x0010efe0`-`0x0010f2f0` | sound record accessors | the fields below, one function each | confirmed (code) |
| `0x0014c620` | `AudioDevice_LoadBank(name, partial)` | reads `<name>.msb` into sound RAM and `<name>.msd` into IOP memory | confirmed (code) |
| `0x0014caf8` | `AudioDevice_Start(task)` | starts a sound: a stream from `BFW.SND` or a bank sample by hash | confirmed (code) |
| `0x0014d528` / `0x0014d590` | `AudioDevice_StartMusic` | opens `MUSIC.SND` on a stream pair and starts it | confirmed (code) |
| `0x00150078` | `RateToPitch(rate)` | `rate × 4096 / 48000`, the SPU2 pitch word | confirmed (code) |
| `0x0014d2d8` | `AudioDevice_Duration(size, rate)` | `⌊⌊size × 3.5⌋ / (2 × rate)⌋ × 1000` ms: whole seconds only | confirmed (code) |

## Data

### PS2 ADPCM (all sound data)

Public SPU2 knowledge: 16-byte frames of 28 samples. Byte 0 holds the shift (low nibble) and the predictor (high
nibble, filters `(0,0) (60,0) (115,-52) (98,-55) (122,-60)` / 64); byte 1 holds the flags (bit 0 end, bit 1 repeat,
bit 2 loop start); 14 bytes of signed 4-bit samples follow, low nibble first. A sample is
`(nibble << 12 >> shift) + (s1 × f0 + s2 × f1 + 32) >> 6`, clamped to 16 bits. No file has a VAG header.

**Corroboration:** of 855,701 frames in 300 random streamed sounds of `BFW.SND`, none has a predictor above 4 or a
shift above 12; the decoded sounds end in silence (the last 5 % at a median 1 % of the sound's RMS); music and
stereo sounds decode to channels that correlate at 0.87-1.0, against 0.0-0.25 when split at a wrong interleave.

### The sound list (chunk `0x29`) {#sound-list}

`u32 count` (25,395), then `count` records of 16 bytes, **sorted by hash** (the lookup is a binary search):

| Offset | Type | Meaning | Evidence |
| --- | --- | --- | --- |
| `+0x00` | u32 | size in bytes of ADPCM (a stereo sound's is a placeholder, 64; see [stereo](#stereo)) | confirmed (code) at `0x0010f258`, `0x0014caf8` |
| `+0x04` | u32 | offset: in `BFW.SND` for a streamed sound, in its bank for a bank sound | confirmed (code) at `0x0010f268`; bank offsets from the data |
| `+0x08` | u32 | CRC-32 of the name as written (`vags/character/voices/5/attack_01`; [hash](../sound.md#original-structure)) | confirmed (code) at `0x0010f080` |
| `+0x0c` | u8 | pitch variation in percent: each play picks a factor in `1 ± v / 100` | confirmed (code) at `0x0014caf8` |
| `+0x0d` | u8 | volume in percent (0-250; 100 as recorded) | confirmed (code) at `0x0010f188` |
| `+0x0e` | u8 | sample-rate index into the table at `0x0050a9e0` | confirmed (code) at `0x0010f238` |
| `+0x0f` | u8 | sound class, an index into chunk `0x48` | confirmed (code) at `0x0010f2a0` |

The **rate table** (`0x0050a9e0`, 73 `u16`): index `i` is `750 × i` Hz for the steps of 750, with 8000, 11025,
16000, 22050, 44100 and 48000 slotted in (index 11 = 8000, 17 = 11025, 25 = 16000, 34 = 22050, 36 = 22500,
65 = 44100, 71 = 48000; index 72 is 9999, unused). Records use 27 indexes; 36 (22,500 Hz) has 17,975 records and 34
(22,050 Hz) 4,331.

### Sound classes (chunk `0x48`) {#sound-classes}

246 records of 5 bytes with no count (1,232 bytes with padding), indexed by the record's `+0x0f`:

| Byte | Meaning | Evidence |
| --- | --- | --- |
| 0 | near distance, m: full volume within it | confirmed (code) at `0x0010f1b0`, `0x0011a170` |
| 1 | far distance ÷ 5: silent beyond `5 × b` m | confirmed (code) at `0x0010f1e8`, `0x0011a170` |
| 2 | flags, below | confirmed (code) |
| 3 | priority, lower is more important (2-21) | confirmed (code) at `0x001120c8`, `0x00112700` |
| 4 | channels, 1 or 2 (only class 224 is 2) | confirmed (code) at `0x0010f2a0` |

| Flag | Meaning | Evidence |
| --- | --- | --- |
| `0x01` | loops until stopped | confirmed (code) at `0x0010f2c8` (a stream's "once" flag is its inverse) |
| `0x02`, `0x08` | positional (3D): either bit | confirmed (code) at `0x0010f018` |
| `0x04` | streamed from `BFW.SND`; clear: played from a bank in sound RAM | confirmed (code) at `0x0010f2f0`; the data split exactly so (below) |
| `0x10` | directional: louder in front of the source (character voices) | confirmed (code) at `0x0010efe0`, `0x0011a170` |
| `0x20` | streams on the reserved channels 10-12 instead of 5-9 (the `small_loops` ambience) | confirmed (code) at `0x0010f050`, `0x00112560` |
| `0x40` | no reader found (set on four classes, voices among them) | inferred |

Interface sounds have near = far = 0 and no positional bit; speech (`vags/speeches/...`) is `0x06` with priority 4;
character voice lines are `0x1c` or `0x4c`.

### The stereo table (chunk `0x49`) {#stereo}

`u32 count` (248), 12 bytes of zeros, then `count` records `{u32 hash, u32 interleave, u32 lastBlock, u32 blocks}`
(`0x0010f098`, `0x0010f138`, `0x0010f0e8`; confirmed (code)). Every one of the 248 is a class-224 (stereo) sound of
the list. In `BFW.SND` a stereo sound at the record's offset is `blocks` blocks of `interleave` bytes (always
`0x8000`) of the left channel then `interleave` of the right; the game asks for `2 × interleave × blocks` bytes and
ends the stream `lastBlock` bytes into the last block (`SOUND_SetMIBEndOffset`, `0x00150660`). These are 32,250 Hz
music-like beds.

### Banks: `.msb` and `.msd` in the WAD {#banks}

A bank is `<name>.msb` (ADPCM samples back to back) and `<name>.msd` (`{u32 hash, u32 offset}` pairs into the
`.msb`, sorted by offset, ended by a zero pair). Every one of the 2,675 hashes in the 21 banks is in the sound list,
and the list's record gives the sample's size (the `.msd` spans match it exactly), rate and class; its offset is the
bank offset. A bank sample is an ordinary one-shot sample: flags 0, ending with a frame flagged 1 and a silent frame
flagged 7. The 21 banks: `sound` (319 sounds, the default in levels), `menu` (14, the front end), `pause` (4),
`armies` (261), `gallery`, `level3`, `level81`, `spookorama`, `birdie`, `diego`, `lizzie`, `luther` (188-311 each),
one with no recovered name (205), and the load-screen banks `load_00` ... `load_06` and `armload` (2 each: the
left and right halves of a load-screen sound). `AudioDevice_LoadBank` builds `./ee_files/%s.msb` and `%s.msd`
(`0x0054b6a0`, `0x0054b6b8`), finds them in the WAD index, streams the `.msb` into sound RAM (stream 0) and the
`.msd` to the IOP, then registers the index (`0x00151dc0`). Confirmed (code); the `load_NN` names were found by
hashing the format `%s_%2.2d` (`0x00547000`) the load screen uses.

The same samples also exist in `BFW.SND` (a bank sample's bytes were found there at other offsets), but the game
plays bank sounds only from the bank: the start call passes the hash, not an offset (`0x0014e370`).

### `IOP/BFW.SND` (1,402,861,568 bytes) {#bfw-snd}

Raw ADPCM, every streamed sound of the list at its record's offset: 22,472 mono and 248 stereo. All 22,720 start on
2,048-byte boundaries (sectors) and carry no end flags: the game stops a stream at its size. The file is opened by
the IOP as `cdrom0:\IOP\BFW.SND;1` (`0x0054b650`) on streams 1-12 (`0x0014bbd8`). Confirmed (code) and corroborated by the
decode checks above.

### The music list (chunk `0x31`) and `IOP/MUSIC.SND` {#music}

`u32 count` (345), then 104-byte records:

| Offset | Type | Meaning | Evidence |
| --- | --- | --- | --- |
| `+0x00` | f32 | volume, 1.0 on the disc; `SndCfgMusicInfo` overwrites it | confirmed (code) at `0x0010d870`, `0x0010e558` |
| `+0x04` | u32 | sample rate: 30,000 (293 tracks), 44,100 (38) or 32,250 (14) | confirmed (code) at `0x0014d590` |
| `+0x08`, `+0x0c` | u32 | 64 and 32 in every record; no reader found | inferred |
| `+0x10` | u32 | offset in `MUSIC.SND` (2,048-aligned) | confirmed (code) at `0x0014d528` |
| `+0x14` | u32 | channels, 2 in every record | confirmed (code) |
| `+0x18` | u32 | interleave: bytes per channel per block (`0x6d80`-`0x8000`) | confirmed (code) at `0x0014d590` |
| `+0x1c` | u32 | blocks | confirmed (code) at `0x0014d528` |
| `+0x20` | u32 | bytes of each channel in the last block (`SOUND_SetMIBEndOffset`) | confirmed (code) at `0x0014d590` |
| `+0x24` | u32 | CRC-32 of the name | confirmed (code) at `0x0010ef18` (the hash is recomputed from the name) |
| `+0x28` | char[64] | the name, `music/<track>` | confirmed (code) |

`MUSIC.SND` (960,063,488 bytes) holds the tracks back to back, each `2 × interleave × blocks` bytes, left block then
right block. The game opens it as `cdrom0:\IOP\MUSIC.SND;1` (`0x0054b6e0`) on stream 1 (with 2 as the right
channel) or stream 3 (with 4), at the record's rate. The track names are listed in
[Sound and music](../../references/sound.md#music-track) (301 of the 345 are configured).

## Coney's implementation

`coney-tools audio` ([repo:python/src/coney_tools/audio.py](repo:python/src/coney_tools/audio.py)) reads the
tables, lists the banks, streams and music with counts and hashes, and decodes one sound or track to a WAV file in
your scratch folder for listening ([coney-tools](../../guides/coney-tools.md#audio)). The engine reads the same data in
`src/audio/` (`sound_data.cpp`, `sound_bank.cpp`, `adpcm.cpp`, `sound_stream.cpp`; [Sound](../sound.md#coneys-implementation)).

## Open questions

- The bank `0xf83ec65e` (`.msd`, WAD entry 9,804): its name (205 sounds, mostly breakables and pick-ups).
- Music records' `+0x08` (64) and `+0x0c` (32): MultiStream header fields?
- Class flag `0x40`.
- The sound list holds records with equal hashes (the first pair at records 115 and 116): which one the game's binary
  search finds.
