# Sound

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), static analysis only
(Ghidra, and the disc's compiled scripts and `warriors.glr` read with `coney-tools`). No runtime claims.

## Purpose

What the game plays and how it finds a sound by name. This page covers so far the **sound list** (every sound the
game can play by name), **speech** (the lines humans say by kind, in their voice) and the **ambient table** the level
emitters draw from. Music, the IOP side and the sound banks are not traced. The names are listed in
[Sound and music](../references/sound.md) and [Speech commands and voices](../references/speech.md).

## Original structure

`Audio/SoundMatrix.cpp` builds the voice table (its allocation tag `SoundVoice`, source path and the folder
`vags/character/voices/` are strings at `0x00548188`, `0x00548198` and `0x005481c8`;
[Source map](source-map.md#audio)). Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00143f68` | `Crc32(table, name)` | CRC-32 of a name as written (no case folding), table at `0x005d91e0` | confirmed (code) |
| `0x00110db0` | `SoundList_Find(hash)` | whether the sound list holds a name hash | confirmed (code) |
| `0x001164a8` | `VoiceTable_Build(n)` | allocates `n` voice sets and counts each one's lines per speech command | confirmed (code) |
| `0x00114b20` | `VoiceTable_NextLine(set, command)` | the next line's sound for a voice set and command, or none | confirmed (code) |
| `0x00114c98` | `VoiceTable_IsBlocked` | a fixed list of lines not played in certain levels | confirmed (code) |
| `0x002205e0` | `Human_SayCommand` | a human says a speech command (the work of `SoundPlayCommand`) | confirmed (code) |
| `0x0021e400` / `0x0021e698` | `Human_PlaySpeech` | plays a line at the human with an end callback (the second also cuts a line off) | confirmed (code) |
| `0x0041cc40` | `WarChief_SayCommand` | the line a war chief says for a Warrior command ([AI](ai.md#warrior-commands)) | confirmed (code) |

## Data

### The sound list {#sound-list}

Chunk `0x29` ("Static Sounds") of `warriors.glr`, 406,336 bytes: a 32-bit count (25,395), then records of 16 bytes.
The words at chunk offsets 12 + 16k are the CRC-32s of the sounds' names (`vags/character/voices/5/attack_01`,
`vags/ambient/beach/foghorn1`): every voice line the game probes for and 595 of the 597 ambient names the scripts
give are among them, 23,186 distinct hashes in all. Inferred; the record's other fields are not traced.

### Speech commands {#speech-commands}

A table of 207 records `{u32 id, char *name}` at `0x0050aaa8` (id equal to the index), read by `0x001164a8` and
`0x00114b20`: `0 nothing`, `1 attack`, `2 follow` ... `206 hide_response`. Confirmed (code). The names are listed in
[Speech](../references/speech.md).

### The voice table {#voice-table}

`SndAllocateCharacterVoices(n)` (`config_preload.lua` passes 350) makes `n` voice sets of 207 entries of 3 bytes
(0x26d per set, pointer at sound system `+0x1e0 + 0x34`), confirmed (code) at `0x001164a8`:

| Byte | Meaning |
| --- | --- |
| `+0` | the next line to play (0-based) |
| `+1` | how many lines the set has for the command |
| `+2` | the chance in percent that the command is said, 100 at the start (`SndSetCommandSoundPercent(set, command, percent)`, set -1 for all) |

The line count is found at start-up: for each set `s` and command `c`, the names `vags/character/voices/<s>/<c>_01`,
`_02` ... (format at `0x005481e0`) are hashed and looked up in the sound list until one is missing or 54 are
found.

A human's voice set is `CfgChar`'s voice (character type `+0x118`, [Characters](characters.md)); the human keeps it
at `+0x3b0`. `HuSetStateRespVoiceIndex` stores a second set at `+0x3b4` for its state responses (inferred).

## Behaviour

### Saying a speech command {#speech}

`SoundPlayCommand(human, command, callback, flag, target, flag2)` resolves the human and calls `Human_SayCommand`
(`0x002205e0`), confirmed (code):

1. Nothing when the human cannot speak (`+0x199` clear). A line already playing is cut off when the fourth argument
   (`interrupt`, default true) is set; otherwise nothing is said while it plays.
2. `VoiceTable_NextLine(+0x3b0, command)` picks the line. With a chance below 100 it rolls 0-99 and says nothing
   when the roll is higher than the chance. With no lines it says nothing. Otherwise it takes line `next + 1`, adds
   one to `next` and wraps it to 0 at the count: **a set's lines for a command play in turn, not at random**. A line
   on the fixed list of `0x00114c98` (current level, set, command and line) is skipped; the counter still moves.
3. Commands 1-6, `0x12`, `0x28`, `0x46`, `0x56` and `0x9e` also set human `+0x16c` (`0x001106d0`; meaning not
   traced).
4. The line plays at the human (`0x0021e400`; the war chief's lines use `0x0021e698`, which can also cut a line
   off), unless speech is off (game state `+0x410`). The **third argument is a callback name**: the function is
   called with the speaker's handle when the line ends, or at once when no line plays. With a target, the speaker
   looks at it for the line's length plus 0.5 s.

### Speech lines by name

`HuSpeak` and `HuSpeakNI` take a sound name (`vags/speeches/l31/l31_t7_001`). The `global.lua` helper `SetVag(name)`
builds one from a short name: `l11_t25_006` stands for `vags/speeches/l11/l11_t25_006`, the folder being the name's
first part (inferred: 537 of the 566 names so formed are in the sound list; the rest may be in a level's own bank).

### The ambient table {#ambient}

`AddAmbientSound(slot, name)` (`global.lua`, 1,077 calls) stores a name's hash in slot `slot` of the ambient table
(ambient manager `+0x1a014 + 4 × slot`); an emitter (`AddAmbientSoundEmitter2(name, p1, p2, slot, sound, count,
...)`) plays, at random intervals, a random sound of slots `slot` to `slot + count - 1`, or the one named sound when
`slot` is -1 ([Sound bindings](../references/bindings/sound.md#addambientsoundemitter2)). Confirmed (code) for the
slot store; the emitters' timing is inferred.

## Coney's implementation

None yet: Coney plays no sound.

## Open questions

- The sound list record's other three words, and how a name found there is played (bank, offset).
- What human `+0x16c` does for the command lines of step 3.
- When the game itself says each speech command (fights, crowds, the police).
- Which bank holds the 29 speech lines and 2 ambient sounds missing from the sound list.
