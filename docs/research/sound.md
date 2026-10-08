# Sound

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`): Ghidra, the disc's data
read with `coney-tools`, and PCSX2 2.9.94 over PINE (2026-10-06, the owner's quick-save slot 1 copied to a file: level99
checkpoint 3, the audio manager's state read while the game ran).

## Purpose

How the game plays its sound effects, speech, ambience and music: the audio manager, its sound tasks and their
voices, priorities, 3D attenuation and panning, volumes, banks, the music player and the speech lines. The data
formats (the sound list, sound classes, banks, `BFW.SND`, `MUSIC.SND` and the ADPCM codec) are on
[Audio data](formats/audio.md). The names are listed in [Sound and music](../references/sound.md) and
[Speech commands and voices](../references/speech.md); the script bindings in
[Sound bindings](../references/bindings/sound.md). Movies (`PSS/*.BIK`) play their own Bink audio.

In one paragraph: every sound is a **task** (up to 256) started by name hash. The sound list gives its sample and
its class; the class gives its priority, distances and flags. A task gets one of 48 SPU2 voices: 13-47 for samples of
the bank loaded into sound RAM, the stream channels 5-9 (10-12 for a few loops) for sounds streamed from
`BFW.SND`; when none is free it steals a less important task's voice or plays **virtually** (tracked, silent, with
its length timed). Each update, a 3D task's left and right volumes are worked out from its distance to the listener
(full within `near`, silent beyond `far`, quadratic between) and from a two-ear pan, times the record's volume, the
options' sound volume and fades. Music streams from `MUSIC.SND` on two stereo stream pairs, switching tracks on
**bar boundaries** with cross-fades, either as scripted tracks or as the **system music** that follows the mood of
the fight.

## Original structure

`Audio/` (`0x0010edd0`-`0x001167b8`, [Source map](source-map.md#audio)): `MusicList.cpp`, `SoundList.cpp`,
`SoundListener.cpp` (the tasks: `FreeList<SoundTask>` at `0x00547128`), `SoundMatrix.cpp` (the voice table, the
allocation tag `SoundVoice` and folder `vags/character/voices/` at `0x00548188`-`0x005481c8`); the music player sits
just before (`0x0010d758`-`0x0010e7d0`, inferred; [Source map](source-map.md#position)).
`Device/ps2/sound/msaudiodevice.cpp` (`0x0014ba18`-`0x0014d508`) is the platform device over SCEE's MultiStream library
(`0x0014d760`-`0x00151ed0`; [The PS2 sound device](#device), [Audio data](formats/audio.md)).
Names are ours; every other function of the unit is in the [Function index](#function-index).

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0050aa84` | `g_AudioManager` | pointer to the audio manager (`0x00598aa0` at runtime) | confirmed (code), confirmed (runtime) |
| `0x0050bce4` | `g_AudioDevice` | the MultiStream device, vtable `0x00537f88` | confirmed (code) |
| `0x00143f68` | `Crc32_Hash(table, name)` | CRC-32 of a name as written (no case folding), table at `0x005d91e0` | confirmed (code) |
| `0x0010f618` | `AudioManager_Reset` | defaults (below); bank `none` | confirmed (code) |
| `0x0010f810` | `AudioManager_Update(listeners)` | the tasks, the ambient emitters, the music, the device | confirmed (code) |
| `0x00111de8` | `AudioManager_Play(vol, pitch, a8, mgr, owner, hash, pos, fade, p9, p10, duckable)` | start a task | confirmed (code) |
| `0x001120c8` | `AudioManager_NewTask(hash, pos)` | admission, task, voice | confirmed (code) |
| `0x00112560` / `0x00112700` | `Task_GetVoice` / `Task_FindVictim` | voice allocation and stealing | confirmed (code) |
| `0x0011a170` | `SoundTask_Update(task, listeners)` | fades, 3D volume and pan, pitch | confirmed (code) |
| `0x00112b10` | `Tasks_Update` | per update: run each task, free the finished | confirmed (code) |
| `0x0010fcd8` / `0x0010fc70` | `PlaySound2DByHash` / `PlaySound2DByName(flags...)` | a sound without position (`SoundPlay2D`, the interface cues) | confirmed (code) |
| `0x0010fdd0` / `0x0010fd48` | `PlaySound3DByHash` / `PlaySound3DByName(pos...)` | a sound at a position (`SoundPlay`) | confirmed (code) |
| `0x0010fa50` | `AudioManager_LoadBank(name, partial)` | loads a bank unless it is the current one | confirmed (code) |
| `0x00111178` / `0x00111428` | `AudioManager_StartLoadScreen` / `_StopLoadScreen` | the load-screen bank and its two sounds ([Banks](#banks)) | confirmed (code) |
| `0x00110b60` / `0x00110c70` | `AmbientTrack_Play` / `_Stop` | the level's ambience bed | confirmed (code) |
| `0x0010d8e8` | `Music_Play(hash, loop, callback, fadeBars)` | queue a music track | confirmed (code) |
| `0x0010dfe0` | `MusicChannel_Update` | the music state machine | confirmed (code) |
| `0x0010e7d0` | `SystemMusic_Update` | mood-driven track choice | confirmed (code) |
| `0x0010e558` | `Music_UpdateVolumes` | music volume, ducking | confirmed (code) |
| `0x001164a8` | `VoiceTable_Build(n)` | allocates `n` voice sets and counts each one's lines per speech command | confirmed (code) |
| `0x00114b20` | `VoiceTable_NextLine(set, command)` | the next line's sound for a voice set and command, or none | confirmed (code) |
| `0x00114c98` | `VoiceTable_IsBlocked` | a fixed list of lines not played in certain levels | confirmed (code) |
| `0x002205e0` | `Human_SayCommand` | a human says a speech command (the work of `SoundPlayCommand`) | confirmed (code) |
| `0x0021e400` / `0x0021e698` | `Human_PlaySpeech` / `Human_PlaySpeechCutting` | plays a line at the human with an end callback (the second also cuts a line off) | confirmed (code) |
| `0x0041cc40` | `WarChief_SayCommand` | the line a war chief says for a Warrior command ([AI](ai.md#warrior-commands)) | confirmed (code) |

## Data

### The audio manager

Fields used here (offsets from `*(0x0050aa84)`); defaults from `0x0010f618` and the options' "restore defaults"
(`0x001d80e0`). Confirmed (code); the values marked *runtime* were read in level99.

| Offset | Meaning |
| --- | --- |
| `+0x00`, `+0x04`, `+0x08` | the task list: head, tail, count (tasks linked by `+0x08` previous, `+0x0c` next) |
| `+0x20` | two listeners of 32 bytes, `{position, vector}`, copied in each update (one per player) |
| `+0x64` | the music player (below) |
| `+0x1d8`, `+0x1dc` | "a non-duckable sound is playing" flags (set by tasks with `+0xa4` = 0, cleared each update; both also set for the whole update while a cinematic runs, scene state `+0x410`, `0x00112b10`) |
| `+0x1e0` | the sound matrix: material and animation sound tables, the voice table at `+0x34`, interface cues |
| `+0x24268`, `+0x24274` | handles of the scene soundtrack ([Scene soundtracks](#scene-sound)) and of the ambient track |
| `+0x2426c` | the scene soundtrack has been started (set by scene event 13, cleared by its stop) |
| `+0x24270` | set: new positional sounds and 2D mono sounds play virtually (set during the load screen; stereo sounds are exempt) |
| `+0x3fa20` | **sound-effect volume**, 0-1 (options; default 0.9, *runtime* 0.9) |
| `+0x3fa24` | paused (`SoundPauseSound`); pitch updates stop |
| `+0x3fa28` | the next load-screen number, 0-6 (random at start-up, `0x0010f768`) |
| `+0x3fa34` | low-priority sounds started this update (reset each update) |
| `+0x3fa38` | the current bank's name (16 chars; *runtime* `sound`); `+0x3fa48` the bank to load after loading (`none`) |
| `+0x3fa58` | defer `SndLoadBank` to the end of loading (set by mode 1's enter, `0x00158334`) |
| `+0x3fa5c` | global pitch factor (`SndSetPitchMod`, 1.0) |
| `+0x3faa8` | pan mode: 0 two-ear pan (default; forced when there are two players), else an angle table (`0x005d8600`) |
| `+0x3faac` | duck factor for directional sounds under a non-duckable one (`SndSetNIDuck`, 0.2) |
| `+0x3fab0`, `+0x3fab4` | music duck factor during scenes (0.75) and whether ducking is on (`SndEnableMusicDuck`, 1) |

### Sound tasks

256 tasks of `0xf0` bytes are allocated at start-up (`0x00111ac0`). Confirmed (code):

| Offset | Meaning |
| --- | --- |
| `+0x04` | pointer to the sound's record ([sound list](formats/audio.md#sound-list)) |
| `+0x3c`, `+0x40` | voice (SPU2 voice or stream channel, -1 none) and stream (999 for a bank sample) |
| `+0x44`, `+0x48`, `+0x4c` | length in ms, start time, **virtual** (no voice) |
| `+0x50`, `+0x54` | a 2D sound's left and right pan gains |
| `+0x58`, `+0x5c` | the volumes last sent, left and right (0-1) |
| `+0x64` | the rate last sent (Hz × the pitch factors) |
| `+0x68`, `+0x6c`, `+0x70` | pitch factors: random variation, the caller's, 0.85 in the `+0x268` state below |
| `+0x74` | the caller's volume (× the priority-21 random factor, below) |
| `+0x78`, `+0x7c`, `+0x80`, `+0x84` | fade level, fade mode (1 in, 2 out), fade start and length in ms |
| `+0x8c` | distance to the listener, m |
| `+0x94` | state (1 playing, 2 faded out, 3 stopped) |
| `+0x9c` | started (0: prepared and primed but not started, [Scene soundtracks](#scene-sound)) |
| `+0xa4` | duckable (the play call's last argument; 0 also marks the manager's `+0x1d8`) |
| `+0xa8` | `a8`, the play call's third volume factor |
| `+0xc0`, `+0xd0` | position and facing (directional sounds) |
| `+0xe0`, `+0xe4` | the play call's ninth and tenth arguments, sent to the IOP with a bank sample's start (`0x0014e370`); `+0xe4` set also keeps a stopped real task until its voice ends (`0x00112c60`) |
| `+0xe8` | the owner's handle (a human: its voice is cut when it speaks again) |
| `+0xec` | the owning human's `+0x16c` flag, copied at the play (`0x0011b3f8`): a stream started with it gets IOP stream priority `0x3f` ([Saying a speech command](#speech)) |

Runtime check (level99 checkpoint 3): 5-6 live tasks; a streamed loop on stream 6, streamed positional sounds on
streams 5, 7, 8 and 9, and one bank sample on SPU2 voice 13 with stream 999.

### The music player {#music-player}

At audio manager `+0x64`: the track list (`+0x0c` count, `+0x10` 16-byte tracks `{record, hash, 1, bar ms}`), the
system music (`+0x1c` playing mood, `+0x20` last mood, `+0x25 + mood` track count, `+0x2c + 12 × mood + 4 × i` track
hashes), the music volume `+0x5c` (`SoundSetMusicVolume`), the options' music volume `+0x60` (default 0.9, *runtime*
0.9), and three channels of `0x4c` bytes at `+0x70` (the queued request is the third, `+0x15c`). A channel: `+0x04`
track index, `+0x08` stream pair (1 or 3), `+0x0c` loop, `+0x10` fade length in bars (0: at once), `+0x11` state,
`+0x14`/`+0x18`/`+0x1c`/`+0x20` fade start, play start, next bar, fade length in ms, `+0x24` fade level, `+0x2c` the
callback name (32 chars). The states are named by strings at `0x00546da8`: `ST_Idle` 0, `ST_Queued` 1, `ST_PreLoading`
2, `ST_Playing` 3, `ST_FadeIn` 4, `ST_Blocked` 5, `ST_MSPMSync` 6, `ST_FadeOut` 7. Confirmed (code) at `0x0010dfe0`.

### The sound list, speech commands and the voice table {#sound-list}

The sound list's records are on [Audio data](formats/audio.md#sound-list): 25,395 sounds, every name the voice table
probes and 595 of the 597 ambient names the scripts give among them. Confirmed (code) at `0x0010f900`.

#### Speech commands {#speech-commands}

A table of 207 records `{u32 id, char *name}` at `0x0050aaa8` (id equal to the index), read by `0x001164a8` and
`0x00114b20`: `0 nothing`, `1 attack`, `2 follow` ... `206 hide_response`. Confirmed (code). The names are listed in
[Speech](../references/speech.md).

#### The voice table {#voice-table}

`SndAllocateCharacterVoices(n)` (`config_preload.lua` passes 350) makes `n` voice sets of 207 entries of 3 bytes
(0x26d per set, pointer at sound system `+0x1e0 + 0x34`), confirmed (code) at `0x001164a8`:

| Byte | Meaning |
| --- | --- |
| `+0` | the next line to play (0-based) |
| `+1` | how many lines the set has for the command |
| `+2` | the chance in percent that the command is said, 100 at the start (`SndSetCommandSoundPercent(set, command, percent)`, set -1 for all) |

The line count is found at start-up: for each set `s` and command `c`, the names `vags/character/voices/<s>/<c>_01`,
`_02` ... (format at `0x005481e0`) are hashed and looked up in the sound list until one is missing or 54 are
found. The set is written in decimal and the line in two digits, inferred from the sound list: it holds
`voices/0/attack_01` and `voices/100/attack_01`, not `voices/00/attack_01`.

A human's voice set is `CfgChar`'s voice (character type `+0x118`, [Characters](characters.md)); the human keeps it
at `+0x3b0`. `HuSetStateRespVoiceIndex` stores a second set at `+0x3b4` for its state responses (inferred).

### The sound matrix {#sound-matrix}

At audio manager `+0x1e0` (`SoundMatrix_Init`, `0x00114220`): the sounds that hits, footsteps and animations play,
filled by the matrix's `<name>_preload.lua` (`SndLoadMatrix`, default `sound`, kept at `+0x23d64`; a new name resets
the matrix first, `0x00114628`). Confirmed (code):

| Offset | Meaning |
| --- | --- |
| `+0x00`-`+0x0c` | pool of 3,000 sound-hash words |
| `+0x10`-`+0x1c`, `+0x20`-`+0x2c` | pools of 1,200 material records and 150 animation records, 28 bytes each |
| `+0x30`, `+0x34` | voice-set count and the [voice table](#voice-table) |
| `+0x38` | the **material table**: 191 × 191 record pointers, `[m1 × 191 + m2]` (row `0x2fc` bytes) |
| `+0x23a3c` | the **animation table**: 162 record pointers, one per animation sound event (`SA.*`) |
| `+0x23cc4` | the [interface cues](#interface-sounds)' hashes |
| `+0x23d64` | the matrix name (16 chars) |
| `+0x23da4`, `+0x23f20`, `+0x24070` | the DJ's [failure-line](#dj-failure-lines) tables |

A record: byte 0 the alternatives in use (`NewMaterialSlots`' count, changed by `SetNumberOfMaterialSlots`), byte 1
the animation cursor, byte 2 the material cursor, `+0x04`/`+0x08`/`+0x0c` the three columns' volumes (the binding's
first three floats; the last three are not read), `+0x10`/`+0x14`/`+0x18` each column's hash array (null for an
unused column). `NewMaterialSound` / `NewAnimSound` fill one alternative's columns (`none` stores 0).
`DuplicateSoundMaterials(a, b)` makes `[b][a]` the same record as `[a][b]`.

A lookup (`0x001146c0` materials, `0x00114800` animations) returns the cursor's alternative, its three hashes and
volumes, then moves the cursor on, wrapping at the count: **the alternatives play in turn, not at random**. For
materials a second material of 0 or 1 means the caller's default (5 for most callers), and an empty entry falls back
to `[m1][default]`. The players: `Sound_PlayMaterialPair` (`0x00110830`, 16 callers: contacts, objects, footsteps)
plays column 2 then column 1 at a position, each at its column volume, the second at a random pitch factor (`× 0.2`)
when the first material is 13; `Sound_PlayMaterialHit` plays column 1 only; `Sound_PlayAnimSound` (`0x00110a08`)
plays an animation entry's column 1. The footstep caller first remaps materials `0x12` → `0x6a`, 6 → 5 and `0x79` →
`0x23` (that last at half volume, `0x00110aa8`).

## Behaviour

### Starting a sound {#play}

`AudioManager_NewTask` (`0x001120c8`), confirmed (code):

1. Find the record by hash (binary search); none: nothing plays.
2. **Admission.** With more than 100 tasks alive a sound of priority 20 or above is refused; with more than 240 any
   sound is; a sound of priority 21 or above is refused once 5 have started this update.
3. Take a free task, link it at the tail, give it the next id.
4. **3D cull.** A positional sound farther than `far + 10` m from the listener that does not loop plays virtually;
   during the load screen (`+0x24270`) every positional sound does (unless the flag at `0x00512c14` is set: only
   `SoundPreLoad`, `0x00113700`, sets it) and every 2D sound that is not stereo. A stereo sound is never made
   virtual by the load screen.
5. **Voice** (`0x00112560`): a streamed stereo sound takes a stream pair, 1+2 or 3+4, shared with the music
   ([Stream pairs](#stream-pairs)), and never fails; a streamed mono sound the first free stream
   channel of 5-9 (10-12 for class flag `0x20`), or, failing that and with priority below 12, a victim's; a bank
   sample the first free SPU2 voice of 13-47, or a victim's. No voice: virtual.
6. **Length**: `AudioDevice_Duration(size, rate)` in ms, whole seconds only (`0x0014d2d8`); a virtual sound of
   priority 20 or more is given at most 500 ms.

`AudioManager_Play` (`0x00111de8`) then sets the caller's volume, pitch (1 for a non-duckable sound), owner,
position, facing and fade, runs one `SoundTask_Update` and starts the voice (`0x0014caf8`): a bank sample by hash with
its volumes and pitch; a stream by pointing its channel at the record's offset and size in `BFW.SND` (a loop flag the
inverse of class bit 0); a stereo stream as parent and child channels with the [stereo](formats/audio.md#stereo)
interleave and end offset. Priority-21 sounds get a random volume factor of 1 ± 0-19 % (`0x00112ee0`).

**Pitch.** Sent as the SPU2 pitch `rate × 4096 / 48000` (`0x00150078`) of `rate = record rate × variation ×
caller's pitch × +0x70 × global pitch`; the variation is a random factor in `1 ± v / 100` chosen at the start, drawn,
like the priority-21 volume factor and the system music's pick, from the audio generator `0x006eb8b0`.
Confirmed (code) at `0x0014caf8`, `0x0011a170`.

### Stealing a voice {#voice-stealing}

`Task_FindVictim` (`0x00112700`) walks the live tasks and picks one that is mono, real (not virtual), not stopping,
streams (or not) as the new sound does, has the same class flag `0x20`, and has priority 5 or more (4 or more for
the last one checked); among them the least important (highest priority number), and between equals of priority 8
or more the quietest (mean of its two volumes). It is taken only when its priority number is higher than the new
sound's; it is then stopped and marked state 3. Confirmed (code); the exact tie order is the list order.

### Task update: fades, 3D volume and pan {#three-d}

Each update `Tasks_Update` (`0x00112b10`) stores the listeners, runs `SoundTask_Update` on every real task and frees
those whose voice has finished or whose state is 2 or 3; a virtual task is freed when its length has passed (never,
if it loops). Confirmed (code). `SoundTask_Update` (`0x0011a170`), confirmed (code):

- **Fade**: mode 1 raises `+0x78` from 0 to 1 over the fade length; mode 2 lowers it to 0 and then stops the sound.
  The ambient track fades in and out over 2,000 ms (`0x00110b60`).
- **Distance attenuation**, per listener, with `d` the distance, `near` and `far` the class's:
  `a = 0` beyond `far`, `a = 1` within `near`, else `a = (1 − (d − near) / (far − near))²`.
- **Directional** sounds (class flag `0x10`, the voices): a further factor from a 200-byte table of percentages at
  `0x0050a910`, indexed by `(cos θ + 1) × 100` where θ is between the sound's facing and the direction to the
  listener; not applied within 1 m. Entries 0-100 are 100; 101-199 fall about linearly to 49 (about `100 − 51 × c`
  for `c = cos θ`); 200 and above give 0. So, inferred, a voice is at full volume anywhere ahead of its speaker's
  side line and at about half straight behind.
- **Pan** (pan mode 0): two ear points half a metre either side of the listener (along the listener's side axis,
  inferred) give distances `dL` and `dR`; the nearer ear gets gain 1, the farther `0.75 × a + 1 − |dL − dR|`, clamped
  to 0-1. Mode 1 reads left and right gains from tables at `0x005d8600` and `0x005d8ba4` by the angle to the
  listener in degrees (180 + angle); `PanTables_Build` (`0x00117450`, at reset) fills their 361 entries with
  `left[a] = cos(45° + a / 2)` and `right[a] = sin(45° + a / 2)`, a constant-power pan.
- **Volume** per ear: `a × ear gain × record volume % × caller's volume × sound volume (+0x3fa20) × directional ×
  fade × a8 × state factor`, then the loudest over the listeners, then × the duck factor (`+0x3faac`) for a
  duckable directional sound not owned by a player while a non-duckable one plays, × 0.25 for a duckable positional
  sound whose owner's flag `+0x5b7` differs from every listener's ([covered ground](sound-events.md#covered)),
  clamped to 1. Sent only when changed.
- **State factor**: when game state `+0x268` is positive (meaning not traced) a sound with task `+0xa0` set whose
  owner is not a player (human `+0x1b0` = -1) is scaled by 0.75, or 0.15 beyond 2 m, and its pitch by 0.85.
- **2D sounds**: `record volume × sound volume × fade × caller's volume × pan gains (+0x50, +0x54) × a8`; a stereo
  bed sends left only to its left channel and right only to its right. One sound (hash `0x510bb577`) ignores the
  sound volume.

**Runtime check** (level99, sound volume 0.9): a streamed bed of class 105 (near 20 m, far 100 m, record volume
40 %) at 75.31 m read left 0.034, the formula's `0.0952 × 0.4 × 0.9 = 0.0343`; one of class 61 (5-100 m, 40 %) at
75.41 m read right 0.024 against `0.0670 × 0.4 × 0.9 = 0.0241`, its far ear 0.75 of that; a 2D loop of record volume
20 % read 0.180 on both sides.

### Volumes and the options

The options' sound and music volumes default to 0.9 (`0x0010f768`, `0x001d80e0`: `0x00110638` sets `+0x3fa20` and
mirrors it at game state `+0x57a8`; `0x001105b0` sets the music player's `+0x60`). `SoundSetSoundVolume` does
nothing in this build. Volumes reach the device as `int(v × 16383) & 0x7fff` per side (`0x0014d188`). Confirmed
(code).

#### Reverb {#reverb}

`SoundSetEffect(type, depth, delay, feedback)` (`AudioManager_SetEffect`, `0x00113000`) sets
the SPU2 effect on both cores: device slot `+0xa0` sends IOP command `0x13` (core, type, depth × 32,767 on the left
and right, delay, feedback; a type of 10 or more is refused), then slot `+0xa8` command `0x15`, the effect's return
volume, the depth on both sides (`0x00113118`). `SoundEnableEffects(on)` keeps the flag at `+0x1c4`; off also sends
command `0x17` on both cores at once (`0x001130c8`). Mode 1's enter (`0x00158334`) sets the bank deferral
`+0x3fa58` and calls `SoundSetEffect` with type 1
([bindings](../references/bindings/sound.md#soundseteffect)). Confirmed (code); that the types are the SPU2 library's
ten reverb presets (off, rooms, studios, hall, space, echo, delay, pipe) is inferred.

**Pause** (`SoundPauseSound`, `0x0010fb20` / `0x0010fb68`): sets `+0x3fa24` and pauses or resumes every voice through
the device (`0x0014c5e0` / `0x0014c600`). Confirmed (code).

### Banks {#banks}

One bank is in sound RAM at a time. `AudioManager_LoadBank(name)` (`0x0010fa50`) loads it unless it is already
current: the device waits for stream 0, streams `<name>.msb` into sound RAM and `<name>.msd` to the IOP
([banks](formats/audio.md#banks)). Confirmed (code). Who loads which:

- **Front end**: `menu` (`0x0015c4b0`, `0x0015d648`); `0x00159630` loads `sound`.
- **Load screen** (`0x00111178`, from level loading `0x0015fe90`): bank `load_NN` (NN = `+0x3fa28`, then +1 mod 7),
  or `armload` when `0x0041d110` says so (the Armies levels, inferred), and its two sounds
  `vags/load_screen/load_NN_l` / `_r` (`armload_l` / `_r`) started as 2D loops (flags `0x41`, `0x10`) hard left and
  hard right with the load-screen flag clear; then it sets `+0x24270`, so later positional and mono 2D sounds play
  virtually. `0x00111428` stops both after the preload and, when the left half was real, blocks (yielding and
  updating the audio manager and the device) until its voice is idle
  ([Level loading](level-loading.md#loading-screen)). A preload made through `SoundPreLoad` (`0x00113700`) sets
  `0x00512c14` for its call, which spares it the virtual rule.
- **Level** (`0x0015fe90`, after loading): the pending bank `+0x3fa48` if a script asked for one, else `sound`; then
  the pending name is reset to `none`. `SndLoadBank(name)` loads at once, or, while `+0x3fa58` is set, only records
  the name as pending (`0x001133d0`).

### Music {#music}

Scripts configure each track with `SndCfgMusicInfo(track, bar, volume)` at the legal screen: the volume overwrites
the record's, and the second number (1,548-3,780 ms; [Sound and music](../references/sound.md#music-track)) is the
track's **bar length**: the configured values are four beats at the tempo in the track names (2,034 ms for the
`118` tracks, 4 × 60 / 118 = 2.034 s; 1,890 for the `127` ones). Confirmed (code) that the player times its changes
in multiples of it (`0x0010dfe0`); "bar" is inferred.

`Music_Play(track, loop, callback, fadeBars)` (`0x0010d8e8`; `SoundPlayMusicTrack` / `SoundLoopMusicTrack`) does
nothing while the game state forbids music (`0x0041cf30`) or when the track is not configured; otherwise it queues the
request, and `MusicChannel_Update` (`0x0010dfe0`) runs each channel, confirmed (code):

1. **Queued**: take the stream pair the old track is not using, open `MUSIC.SND` on it, mute it, start preloading.
2. **Pre-loading** until the device says the stream is ready; then, by the other channel's state: none playing,
   start now (fading in over `fadeBars × bar` ms, or at once); one playing, wait for its **next bar boundary**
   (`start + ⌈(now − start) / bar⌉ × bar`, state `ST_MSPMSync`), and 33 ms before it start the new track and fade the
   old out over its own bar length × the fade bars; one fading in or out, start now and fade the other out.
3. **Playing**: when the stream ends (a track played once), stop it and call the callback (a Lua function name) if
   one was given.
4. **Fade out** to 0, then stop and free the pair.

`SoundStopMusicTrack` (`0x0010d9a0`) works per channel by state: a queued request is dropped; one pre-loading or
blocked is stopped at once; one playing or waiting for the bar fades out over **one bar** of its track from now; one
fading in turns into a fade-out from the level it has reached. The music update (`0x0010df30`) runs on every audio
manager update. The music volume
(`0x0010e558`) is `fade × track volume × music volume (+0x5c) × options' music volume (+0x60)`, × 0.75 while a scene
plays (game state `+0x410`) with ducking on, × 0.5 while game state `+0x268` is positive; each channel's left volume
goes to its left stream and its right to its right.

**System music** (`0x0010e7d0`, every update while game state `+0x3f8` is set): the mood is game state `+0x40c`
(0, 1 or 2; set by `SoundSetSystemMusicState` and the game, not traced). When it changes, or a change is forced,
a random one of that mood's up to three tracks (`SoundSetMusicTrack(mood, a, b, c)`) is played looping. Its fade
in bars: 0 (a cut at the bar) into mood 1; 4 into mood 0 from mood 1 or 2; otherwise 2. With no track for the mood
the music stops. The scripts' callback (`SoundSetMusicStateCallback`) is told (`0x0041b520`).
Confirmed (code); the moods' meaning (inferred from the tracks' names: 0 idle, 1 fight, 2 search) and who sets them
are not traced.

### Ambience

- **Ambient track**: `SoundPlayAmbientTrack(name)` (`0x00110b60`) plays one looping 2D sound with a 2 s fade-in,
  replacing the current one unless it is the same; in level 82 one track is swapped for another
  (`0x63f1c8b3` → `0x0ffbaa27`). Its volume is the task's caller volume (`SetAmbientTrackVolume`). Confirmed (code).
- **Ambient table**: `AddAmbientSound(slot, name)` (`global.lua`, 1,077 calls) stores a name's hash in slot `slot`
  of the ambient table (ambient manager `+0x1a014 + 4 × slot`). Confirmed (code).
- The `small_loops` sounds (class flag `0x20`) take the stream channels 10-12, so they never steal from effects.

#### Ambient emitters {#ambient}

The ambient manager (audio manager `+0x24280`; its functions are on [Animation](animation.md#ambient)) holds up to
512 emitters of `0xd0` bytes. `AddAmbientSoundEmitter2(name, pos1, pos2, slot, sound, count, range, plays, minDelay,
maxDelay, mode, filter)` makes one through `AmbientManager_AddEmitter` (`0x0010cf58`); a name already used returns
the existing emitter and switches it on. Offsets below are from the emitter's start (ambient manager `+0x10 + 0xd0 ×
id`; the Animation page counts from the manager, 16 bytes more). Confirmed (code):

| Offset | Meaning |
| --- | --- |
| `+0x00` | the one sound's hash, when `slot` is -1 |
| `+0x10` | up to five points the sound plays from, 16 bytes each: `pos2`, or `SetAmbientEmitterPositions`' list |
| `+0x60` | `pos1`, the point the range is measured from |
| `+0x70` | the playing sound's handle |
| `+0x74` | range in m; -1 gives the first sound's `far` + 10 |
| `+0x78` | volume (`SetAmbientEmitterVolumeMod`, 1) |
| `+0x7c` | on (`EnableAmbientEmitter`) |
| `+0x80`, `+0x84` | the last play's time (ms) and the delay in whole **seconds** before the next, drawn in `minDelay`-`maxDelay` (`+0x86`, `+0x88`) |
| `+0x8a`, `+0x8f`, `+0x94` | first table slot (-1: the one sound), slot count, next slot (a random one to start) |
| `+0x8c` | plays left (`plays`; -1 without limit); reaching -1 switches the emitter off |
| `+0x8d` | mode (below; the scripts pass 3 or 4) |
| `+0x8e` | number of points (1, or the positions' count) |
| `+0x90` | listener filter (`filter` 0-2, larger is 0) |
| `+0xac` | name kind: 1 when the name contains `_DAM_`, 2 for `_FHT_`, 3 when its lower-case form contains `music` |
| `+0xad` | the name (31 chars) |

**The update** (`AmbientManager_UpdateEmitters`, `0x0010c100`) runs at most **once a second** (game clock) and not at
all while game-state flag 2 is set or the load screen is up (`+0x24270`). For each player and each emitter:

1. **Who hears it.** A player counts unless the filter says otherwise: filter 0 skips a player whose `+0x5b7` is set,
   1 one whose `+0x5b7` is clear, 2 skips none (`+0x5b7`: the player stands on
   [covered ground](sound-events.md#covered), inferred indoors). An emitter that is off, or that no player counts
   for, has its sound stopped.
2. **Volume.** A playing sound's `a8` factor becomes the ambient factor (0.75 while a scene plays, else 1), and a
   `music` emitter's then the duck instead, when it is not 1: 0.5 while a music channel is busy and the mood is
   not 2 (`AmbientSound_ApplyVolume`, `AmbientSound_ApplyDuck`).
3. **Distance**: to the nearest listener from `pos1` (`0x00113188`). Beyond the range, or while `0x005147c8` is set,
   nothing starts, and a playing sound is stopped once the listener is beyond that sound's own `far`.
4. **Kind gates**: a `_DAM_` emitter plays only from 2 s to 15 s after the AI's event stamp
   (`AmbientManager_MarkEvent`, [Animation](animation.md#ambient)) and not while the fight timer is on; a `_FHT_` one
   only while the fight timer is on (mood 1 and 5 s after).
5. **Play**, by mode, at most **two new sounds per update** over all emitters:
    - **1, 2**: when the delay has passed and nothing is playing, take one play, play the next sound (the table slots
      **in turn**, from the random start) at a random one of the points, positional and duckable, at the emitter's
      volume with the ambient factor; then draw the next delay. A play count that reaches -1 switches the emitter
      off instead of playing (so 0 plays allows none). When the delay has passed while its sound still plays, the
      delay is drawn again; after the last play of a count (0 left) the next delay is 1 s.
    - **3**: a loop: whenever the listener is in range and nothing is playing, start the next sound at the first
      point (the ambient factor, or for a `music` emitter outside a scene the duck); beyond the range, stop it. No
      delays, no play count, not counted in the two new sounds.
    - **4**: as 1 and 2, but its first play waits for neither the delay nor a playing sound; it then becomes mode 2.
    - **5**: as 1 and 2, but a playing sound is stopped at the next update (inferred to suit sounds of a second or
      less); a delay not yet passed is set to 0 from now, so it plays at the following update; a count reaching -1
      switches it off after that play.
    - **6**: nothing.
    - **7**: a **conversation** (`AmbientSound_AddStatResponse` → `AmbientManager_AddSphereEmitter`, `0x0010cd68`): up
      to five voice sets (`+0x98`, -1 ends the list) take turns, each saying the `statement` command (20) and the
      next set the `response` (21), the line number moving on after each response; when the listener is farther
      than the line's `far` − 20 m the distant `statement2` / `response2` lines (77, 78) are used, or the same line
      at half volume.

Confirmed (code). `AmbientSound_AddOneOff` (`0x00113b78`) is an emitter of one sound with one play;
`AddAmbientSoundEmitter` (no name) is the same call named `particle task`
([Sound bindings](../references/bindings/sound.md#addambientsoundemitter2)).

### Radios {#radios}

`SetupRadio` (`Radio_Setup`, `0x003ac4d0`) makes a world object (a boom box) a radio run by `Radio_Update`
(`0x003ad440`, every update interval of the object) and `Radio_HandleMessage` (`0x003ac7e8`). Everything a radio
plays is a **streamed** sound of the sound list (stream flags, `BFW.SND`) started with `PlaySound3DByHash` (`0x0010fdd0`,
volume and pitch 1) **at the radio's position**: an ordinary positional sound task, not the music player; each
update moves the task to the radio and stops it once the player is more than 40 m away. While a scene plays
(`0x0051489c` `+0x410`) the task's volume is 0.5, otherwise 1. Confirmed (code).

**The tables** (hashes of the sound list; names found by the CRC-32 of candidate names and checked against the
position of the sound in `BFW.SND`, which keeps each folder's files in upper-case alphabetical order; *inferred*
where the match is one word pair among many tried; none are runtime-checked):

| Table | Entries | Names |
| --- | --- | --- |
| Tracks `0x00512cd8` | 21 (0-20) | 0 `echoes_in_my_mind`, 1 `last_of_an_ancient_breed`, 2 `love_is_a_fire`, 3 `nowhere_to_run`, 4 `you_re_movin_too_slow`, 5 `0x60b5c0c3`, 6 `get_down_radio` (*inferred*), 7 `0x73f3d710`, 8 `0x3be47458`, 9 `0xf33104cd`, 10 `remember`, 11 `0x46b4f271`, 12 `in_the_city`, 13 `baseball_furies_chase`, 14 `the_fight`, 15 `theme_from_the_warriors`, 16 `sho_radioloop_01`, 17 `alberto`, 18 `punkemitter` (*inferred*), 19 `spanish_killer_loop`, 20 `tna_funk` |
| Announcements `0x00512d30` | 13 (0-12) | 0 is track 13 again (never played: `djLine` 0 means none); 1-12 `djlady_01` ... `djlady_12` (`djlady_13` exists and is in no table) |
| DJ links, kind 0 `0x00512d68` | 18 | no names recovered; class 134 |
| DJ links, kind 1 `0x00512db0` | 15 | no names recovered; class 134 |
| DJ links, kind 2 `0x00512df0` | 9 | `dj_rumble_01` ... `dj_rumble_09` in order, except entry 6 is `_08`, 7 `_09`, 8 `_07` |
| Retune | `0x09a4be6a` | one sound, class 72 (1-15 m) |
| Switch | `0x5b601235` | one click, class 49 (1-10 m) |

Every name is `vags/music/<name>`. The tracks use classes 106 (5-50 m), 210 (track 16) and 214 (tracks 18-20); the
announcements class 213 (1-40 m). Confirmed (code) for the tables, sizes and classes (the sound list).

**The record** (via vtable `+0x18c`): `+0x24` the sound task, `+0x28` the current track or clip index, `+0x2c` the
next track (or the announcement), `+0x30` the state, `+0x34` the clip table (0, 1, 2 the DJ link kinds, 3
announcements), `+0x36` the announcement armed, `+0x38` `onPickUp`, `+0x3c` `onSegment`. `SetupRadio` with
`djLine` stores it in `+0x2c`, arms `+0x36` and starts in state 1 with `track`; without, a `track` above 0 starts
state 1 and 0 or less state 9 (off). Confirmed (code).

**The update.** First, an announcement armed and the player within **5 m**: it is disarmed, the sound stopped, the
announcement becomes the clip (kind 3), a random next track is drawn and the state becomes 5. Then by state
(confirmed (code); "draws a track" is `0x00335498(rng, n)`, `Random % n`, with n = **18** once level 84 is complete
and 12 before):

| State | Does |
| --- | --- |
| 1 | player within 40 m and track ≥ 0: play the track, state 2; else stop |
| 2 | track playing: follow the radio. Track ended (within 40 m): an armed announcement plays next; otherwise pick a DJ link with `Random_Int(9)` (0-9): 0-6 kind 1 (one of 15), 7-8 kind 0 (one of 14-18, below), 9 kind 2 (one of 9, or 8 once level 93 is complete); draw the next track; state 5 |
| 5 | play the clip from its kind's table (within 40 m), call `onSegment`; state 6 |
| 6 | clip playing: follow the radio; ended, or a scene running (stops it): state 8 |
| 8 | the next track becomes current (**above 11 it becomes 0**), state 1, call `onSegment` |
| 3 | stop, play the retune sound, call `onSegment`, state 4 |
| 4 | retune sound ended: state 7 |
| 7 | draw a track (not limited to 11), state 1, call `onSegment` |
| 9 | keep the track as next, track −1, play the switch click; state 10 |
| 10 | off: stop any sound |
| 11 / 12 | play the click; when it ends the next track (above 11: 0) plays, state 1 |

The kind-0 pool is 14 entries once level 84 is complete, else 15 once level 31 is, 16 once level 93 is, 17 once
level 81 is, and all 18 before. "Complete" is `0x004241d8(0x006fe998, level)`: the level's `(level, 0, 0)`
[unlockable](player-state.md#unlockables) is unlocked. Confirmed (code); why later missions shrink the pool (stale
news, inferred) is not known. Tracks 18-20 are reached only through `SetupRadio`'s `track`.

**Turning it** (`Radio_SetMode`, `0x003ac600`, which stops the current sound whenever it changes the state): mode 1
retunes (state 3) a radio in state 1, 2, 11 or 12 and does nothing otherwise; 0 and 2 switch it off (state 9); 3
pauses (state 11); 4-9 step to the next track (above 20: 0) and 10 + n selects track n, both through state 3.
Only mode 1 is used: by the player holding **R1** (command 4) while carrying an item of weapon type 6
(`Player_UpdateActions`, `0x0027c624`), and when the radio is smashed (message 1, which also throws debris and marks
the object dead so the next update stops the sound and ends the task). Confirmed (code); that weapon type 6 is the
carried boom box is inferred.

**Messages**: `0x12` plays the current track if nothing is playing and the player is within 40 m; `0x20` stops the
sound; 10 with 0 stops it and leaves the radio off (state 10); `0x1b` (picked up) calls `onPickUp` with the player's
handle and queues hint text `0x11`. Confirmed (code).

### Interface sounds

`SoundCfgInterfaceSound(n, name)` fills a numbered cue table in the sound matrix; `0x0010fc30` plays cue `n` as a 2D
sound with flags `0x12` (the front end's cue 9 on START, `0xf` on back; [Front end](frontend.md)). The 39 cues are in
[Sound and music](../references/sound.md#interface-sound); the menu cues are in the `menu` bank. Confirmed (code).

### Animation sounds {#anim-sounds}

A clip event of **type 11** ([Animation data](formats/animation.md#keyframes-chunk-0x00), fired by
`Anim_FireFrameEvents`, `0x00101dd8`) sends its human message `0x8b` with the event's value, an animation sound id
(`SA.*`, 0-161, the [matrix](#sound-matrix)'s animation table). The value is the **32-bit word at event `+8`**
(`lw a1,0x8(s1)` at `0x0010246c`, pushed with `Msg_PushInt`), not `+4` or `+6`, which are 0 in these events; the
handler switches on the whole word (`sltiu` against 162 at `0x0021f72c`), so a word of 162 or more takes the default
path (`Human_PlayAnimSound` with that value). Reading the s16 at `+8` matches whenever `+0xa`-`+0xb` are 0, which the
footstep ids 1 and 3 need to work as they do. Confirmed (code). Gameplay clips and scene role clips carry them alike:
the footsteps, cloth, grunts and impacts of every move. The human's handler (`Human_OnAnimSoundEvent`, `0x0021f700`)
acts by id. Confirmed (code):

- **Most ids**: the animation entry played at the human's position (`Human_PlayAnimSound`, `0x0021f548`), owned by
  the human. A player's (human `+0x1b0` not -1) play at **twice** the entry's volume times the combat factor (game
  state `+0x24c` while the camera is in combat framing, `0x0021e3a8`), and add column 2, and column 3 under combat
  framing; anyone else's play column 1 only.
- **Footsteps and body falls** (ids 1, 3 and some others): the material pair of a body material (8 for feet) and the
  ground under the human (`+0x1d8`, remapped as above), through `Human_PlayFootstep` (`0x0021f290`); twice as loud for
  a player, half for one hidden in shadow.
- **Held objects** (ids `0x34`, `0x56`): the held object's material against itself or against 17.
- **Vocal ids** (grunts and efforts): the entry's first sound said as a speech line (`Human_SayAnimLine`,
  `0x0021f410`; needs `+0x199`, a player twice as loud), some only when no line plays, some cutting it.
- **Speech-command ids**: `Human_SayCommand` with a fixed command (`0x8b` itself says `onfire`, 107, while `+0x19b`
  is set; others taunts and reactions, some only when `Ambient_MayGesture` allows).

Every id's sound, material and condition is on [Sound events](sound-events.md#anim-sounds).

Types 12, 14, 69, 70 and 71 send messages `0x8c`, `0x8e`, `0xc5`, `0xc6` and `0xc7` with the value; 14 and 71 are the
sound-by-hash events of [Scene soundtracks](#scene-sound). Type 13 starts the scene soundtrack.

### Saying a speech command {#speech}

`SoundPlayCommand(human, command, callback, flag, target, flag2)` resolves the human and calls `Human_SayCommand`
(`0x002205e0`), confirmed (code):

1. Nothing when the human cannot speak (`+0x199` clear). A line already playing is cut off when the fourth argument
   (`interrupt`, default true) is set; otherwise nothing is said while it plays.
2. `VoiceTable_NextLine(+0x3b0, command)` picks the line. With a chance below 100 it rolls 0-99 and says nothing
   when the roll is higher than the chance. With no lines it says nothing. Otherwise it takes line `next + 1`, adds
   one to `next` and wraps it to 0 at the count: **a set's lines for a command play in turn, not at random**. A line
   on the fixed list of `0x00114c98` (current level, set, command and line) is skipped; the counter still moves.
3. Commands 1-6, `0x12`, `0x46`, `0x56` and `0x9e` (and `0x28` while `0x0041d160` is 1) set human `+0x16c`
   (`0x001106d0`) and first stop every real, streamed, mono sound of priority 8 or more whose last volumes are both
   0.2 or less (`0x00113258`), so a stream channel is free. The flag goes with the line to its task (`+0xec`), and
   its stream gets IOP stream priority `0x3f` (`0x0014c8b8` → `0x00151990`; `0x100` is the default). Confirmed
   (code); that this keeps the Warriors' orders audible over other streams is inferred.
4. The line plays at the human (`0x0021e400`; the war chief's lines use `0x0021e698`, which can also cut a line
   off), unless a scene is playing (game state `+0x410`, read at `0x0021e400`). The **third argument is a callback
   name**: the function is called with the speaker's handle when the line ends, or at once when no line plays. With
   a target, the speaker looks at it for the line's length plus 0.5 s.

Voice lines are streamed, positional and directional (class flags `0x1c` or `0x4c`, priorities 7-11, far 20-50 m).
No lip-sync data was found: the speaking human only turns to its target. Voice lines in play have no subtitles; only
scenes and movies do ([Scenes: subtitles](scenes.md#subtitles)). Scenes play their own soundtrack
([Scene soundtracks](#scene-sound)).

### Gang warning barks {#warning-barks}

When a gang first notices a target (`0x001691a0`, from the gang brains at `0x00311928` and `0x00313d18`; once until
the gang's `+0xd0` is cleared, and only while game state `+0x56fc` is 1), a member says a warning bark: the name is
built by `VoiceTable_WarnLineHash` (`0x001148e8`) in the speaker's voice set (human `+0x3b0`) and played with
`Human_PlaySpeech` at volume 1, after stopping his current line. Confirmed (code):

- Target **within 12 m** (squared distance 144 or less): `vags/character/voices/<set>/warn/warn_o_<NN>`, only when
  the caller asks for a near bark (its fourth argument 1); otherwise nothing.
- Farther: `warn_<n|s>c_<NN>` when the target's brain `+0x04` is 1, else `warn_<n|s><o|w|t>_<NN>` by the gang's
  living members (`Gang_CountLiving`): `w` for 2-5, `t` for more than 5, `o` otherwise.
- `s` when one of the gang's members (`+0x48`, up to 16) already tracks (brain `+0x164`, up to 16) a human whose
  brain `+0x20c` matches the speaker's, else `n`. That `s` means "seen again" is speculative.
- `NN` is the gang's counter `+0x60d`, moved on after each bark and held at 1 (`0x00169630`), so a gang's first bark
  uses its starting value and every later one `_01`.

A name that is not in the sound list gives hash 0 and plays nothing. The bark's handle goes to the gang's `+0x610`.

### The DJ's failure lines {#dj-failure-lines}

On the mission-failed screen (mode `0xc`, update `0x0015cc78`; [Pause](pause.md#the-mission-failed-screen)) the radio
DJ comments once: when the mode's handle is idle and nothing has played yet, `DjLines_PickFailureLine` (`0x001170e8`)
gives a hash for the current level, checkpoint (game state `+0x33a`) and failure kind (game state `+0x118`: 0
`wrecked`, 1 `busted`, 2 `obj`), played as a 2D sound (`PlaySound2DByHash`, duckable). The three tables are filled
at start-up (`DjLines_Init`, `0x00116848`); names are under `vags/character/voices/dj/fail/`. Confirmed (code):

1. **Per checkpoint** (`0x00116d48`): `dj_l<level>_c<cp>_<kind>_<NN>` for an entry (level, checkpoint, count, kind):
   (20, 4, 1, obj), (20, 5, 1, obj), (3, 2, 3, obj), (5, 3, 2, obj), (5, 4, 2, wrecked), (55, 2, 3, wrecked), (55, 3,
   3, wrecked), (80, 4, 3, obj), (81, 3, 2, obj), (82, 2, 3, obj), (82, 3, 2, wrecked), (83, 1, 3, wrecked), (86, 1-5,
   1, obj: five entries), (92, 1, 2, obj), (93, 6, 2, wrecked).
2. With none, kind `obj` is treated as `wrecked`, as is every kind in level 82; then **per level** (`0x00116ec0`):
   `dj_l<level>_<kind>_<NN>`, 3 lines `wrecked` for levels 2, 3, 11, 14, 20, 31, 34, 51, 54, 81, 82, 83, 84, 87, 92
   and 93, 2 for level 5; `busted` 3 lines for levels 9, 31 and 52 (52 has `wrecked` too).
3. With none: level 82 plays `dj_l82_wrecked_01`; any other level a **generic** line `dj_<kind>_<NN>`, `NN` random
   in 1-14 for `wrecked` and 1-10 for `busted` (`0x00116fe8`).

In the per-checkpoint and per-level tables each entry's `NN` runs 1 to its count in turn, one step per failure.

### Speech lines by name

`HuSpeak` and `HuSpeakNI` take a sound name (`vags/speeches/l31/l31_t7_001`). The `global.lua` helper `SetVag(name)`
builds one from a short name: `l11_t25_006` stands for `vags/speeches/l11/l11_t25_006`, the folder being the name's
first part. Speech lines are class flags `0x06` (streamed, positional) at priority 4, so only priority-2 and -3
sounds outrank them. 537 of the 566 names the scripts form are in the sound list; every bank sound is in the list too,
so the other 29 (and the 2 missing ambient names) are not on the disc at all (inferred: the scripts name lines
that were cut).

**How a line plays** (`Human_Speak`, `0x00239370` → `Human_PlaySpeech`, `0x0021e400`; `HuSpeakNI` →
`Human_PlaySpeechCutting`, `0x0021e698`), confirmed (code):

1. Nothing plays, and the callback runs at once, when the human is missing, the line is nil, the human's speech is
   off (`+0x198`, `HuEnableSpeaking`) or **a cinematic is playing** (game state `+0x410`): characters do not speak
   over cinematics; their dialogue there is the scene's soundtrack ([Scene soundtracks](#scene-sound)).
2. `HuSpeak` also gives up while a line is alive or the human's "may be cut" byte `+0x194` is clear. `HuSpeakNI`
   stops the current line first, unless that line is itself an uncut `HuSpeakNI` one: it clears `+0x194` while it
   plays, so a second `HuSpeakNI` (or `HuShutUp(false)`) leaves it alone and runs its own callback at once.
   `HuShutUp(true)` always stops.
3. The line is **prepared**, not started: a positional, directional stream 1.8 m above the human's feet, at volume 1
   (× the combat factor of [Animation sounds](#anim-sounds) for a player). **The fifth argument is the play call's
   duckable flag**: false makes a non-duckable line, which plays at pitch 1 and ducks every other duckable
   directional sound of a non-player to `+0x3faac` (0.2) while it lasts.
4. `Human_UpdateSpeech` (`0x0021e940`, each update) starts the stream once it is primed (and the speaking pose with
   it), keeps it at the human's head, and when the line is gone calls the Lua callback with its argument and sets
   `+0x194` again. A line stopped by `HuShutUp` keeps its Lua callback, which runs at that next update; a line cut off
   by a new one loses it (the new line's callback replaces it). The speaker looks at the `listener` for the line's
   length plus 0.5 s.

### Scene soundtracks {#scene-sound}

An in-engine scene ([Scenes](scenes.md#events)) plays one **scene soundtrack**: a 32,250 Hz stereo stream (class 224,
[stereo table](formats/audio.md)) as long as the scene (*speculative*: it carries the scene's dialogue). It runs in
two steps, so the stream is buffering before the first frame:

1. **Preload** (`0x0010ff68`): called from the record's fix-up (`0x00352098`) and each streamed segment's
   (`0x00352788`), from the music update for a pending soundtrack (`0x0010df30`, below) and from `SoundPreLoadScene`
   (`0x00114178`). A fix-up preloads once for **every** role whose clip has a type-13 event (its first,
   `0x00101c58`) and then for the camera track's (`0x00354d28`), each call replacing the last, so the last one
   wins. Each call: stop the current soundtrack if its task is alive (`0x00110078`, which also clears `+0x2426c`),
   claim a stream pair ([Stream pairs](#stream-pairs)), then prepare the sound without starting it (`0x001100e8` →
   `0x00111f78` → `AudioManager_NewTask`, duckable, volume and pitch 1): the stream is set up and primed
   (`0x0011b128`, device slots `+0x90` and `+0x70`) but not started (task `+0x9c` = 0). The task's handle goes to
   `+0x24268` and to the music player's `+0x08`. It goes through `NewTask`'s rules, but the load-screen flag
   (`+0x24270`) spares stereo sounds and the 3D cull does not apply to a 2D sound, so a scene loaded while the load
   screen is up is prepared on a real pair, not virtually; only the admission (more than 240 tasks) could refuse
   it. *Confirmed (code).*
2. **Start**: scene event 13 (`0x00110018`, from the camera track at `0x00355200` and from a role's clip at
   `0x001024ec`) starts **whatever task `+0x24268` holds** (`0x00110258` → device slot `+0x78`, task `+0x9c` = 1) and
   sets `+0x2426c` to 1, or to 0 when the handle is dead. It reads no hash: nothing checks that the prepared sound is
   this scene's. A dead handle starts nothing, and a virtual task (no stream) is refused by the device
   (`0x0014c968`), so neither is heard. *Confirmed (code).*

Between the two, **the scene's start waits for the soundtrack** (`0x0039d870`, at `0x0039de14`, in start step 6 of
[Scenes](scenes.md#starting)), for a cinematic (`+0xed`) or when the current level's id is `0x3c` (which level is
not traced). It re-enters the start (state 4) while the task in `+0x24268` is alive but its stream is not yet
primed (`0x001102a0` → device slot `+0x80`, `0x0014c9c0`: a stream's status byte 1; a virtual task counts as ready),
or while the task is dead and a hash is pending. While it waits and both stereo slots hold music (the slot bytes
read as `0x0101`, `0x0011b760`), it turns the system music off (remembering it in the scene task's `+0xf0`) and
stops the music (`0x00110528` → `SoundStopMusicTrack`), which frees a pair for the pending soundtrack. A dead task
with nothing pending does not hold the scene. The start's 10 s give-up still applies. *Confirmed (code).* A
non-cinematic scene (`ScenePlayFixedScene`, most `ScenePlayAnimation` calls) does not wait.

`AudioManager_Update` (`0x0010f810`) clears `+0x2426c` on every update in which the soundtrack's handle is dead,
so the flag means "a started soundtrack is still alive". *Confirmed (code)* at `0x0010f894`.

*Runtime* (PCSX2 2.9.94, level99, a scratch state taken before the Warriors' fight lesson, the audio manager read
over PINE every 30 ms): the soundtrack is class 224, flags `0x04` (streamed, 2D, played once), priority 3. Scene
one was prepared on pair 1+2 as the cinematic began (scene state `+0x410` 1), started 0.28 s later (`+0x2426c` = 1)
and **kept playing after the scene ended** (`+0x410` 0, task still live and started); 0.4 s later the next scene's
fix-up found it alive and stopped it, found pair 1+2 still marked as a scene sound, recorded the hash as pending,
and the music update prepared it again on pair 1+2 0.43 s later, before that cinematic's start; event 13 then
started it. A third scene's soundtrack outlived its scene by 0.47 s and then ended by itself, and `+0x2426c` fell
to 0 with it. Mono streams (ambience, voices) sat on channels 5-10 throughout.

#### Stream pairs {#stream-pairs}

Stereo streams use two pairs: **1+2** (slot 0) and **3+4** (slot 1). The music
player's first two bytes (manager `+0x64`, `+0x65`) record each slot's user: 0 free, 1 a music channel, 2 a stereo
sound (in practice the scene soundtrack). Mono streams use only 5-9 (and 10-12), so **ambient emitters, speech
lines and voices never compete with a scene soundtrack**. Confirmed (code) at `0x0011b450`, `0x0011b498`,
`0x0011b750`; confirmed (runtime): music on 3+4 with a soundtrack prepared on 1+2, slot bytes 2 and 1.

- **The preload's claim** (`0x0011b450`): refused when either slot already holds a stereo sound; otherwise slot 0
  if free, else slot 1 if free, else refused. The result only gates the preload; the pair itself is picked again
  when the task is made.
- **Its fallback** (`0x00112d60` → `0x0011b558`), when refused: if the previous soundtrack's task is still alive,
  every slot holding a stereo sound is freed, that task is stopped, and the preload goes on. Otherwise the hash is
  stored as **pending** (music player `+0x04`) and nothing is prepared now. The preload stops a live soundtrack
  first, and a stopped task whose `+0xe4` is clear is freed at once (`0x00112c60`), so after one scene's soundtrack
  the next scene's preload takes the pending path (seen at runtime): its slot stays marked until the next music
  update.
- **Each music update** (`0x0010df30`): first, if a hash is pending and the claim would succeed, preload it and
  clear the pending hash; then free every slot marked as a stereo sound when the task in music player `+0x08` is
  dead (`0x0011b5e8`). So a pending soundtrack is prepared on the music update after the one that frees its slot
  (0.43 s after the fix-up in the runtime trace above). A cinematic's start waits for it (above). A scene that does
  not wait and reaches event 13 first starts nothing, and the later re-preload is never started: that scene plays
  silent. *Inferred* from the code; not seen.
- **Making the task** (`0x00112560` → `0x0011b498(…, 2)`): slot 0 if free, else slot 1 if free. With both taken:
  while a started soundtrack is alive (`+0x2426c`) or a cinematic runs (`+0x410`), no slot; otherwise a slot holding
  a stereo sound is taken over (`0x0011b6c0`: the current soundtrack's hash goes to pending and its task is
  stopped). With no slot the pair number is still made from −1 (`0x0011b750` gives pair 3), so the sound plays on
  3+4 over whatever uses them; a stereo sound never goes virtual for want of a pair. Confirmed (code).
- **Music** claims a pair the same way (`MusicChannel_Update`, state Queued, `0x0011b498(…, 1)`): when both slots
  are in use, no started soundtrack is alive and no cinematic runs, **a queued track takes the soundtrack's
  pair**, stopping the prepared soundtrack and leaving its hash pending; it is prepared again when a pair frees
  (a cross-fade's old track ending, or the cinematic start stopping the music). While a started soundtrack is
  alive or a cinematic runs, the track waits in Queued. Confirmed (code). For a cinematic this only delays the
  scene; a scene that does not wait can lose its soundtrack this way (*inferred*).

**Ending.** Nothing stops the soundtrack when a scene ends normally: the end (`SceneTask_End`, `0x0039f450`) stops
it only when the scene was **skipped** (task `+0xe4`), and the abort (`0x0039ec60`) only for a cinematic (`+0xed`);
both through `0x00110078`, whose only other caller is the preload; freeing the scene task and unloading the slot
do not stop it. Otherwise it runs to its own end or until the next preload stops it; at runtime two unskipped
soundtracks outlived their scenes by 0.4-0.5 s (above). Confirmed (code); confirmed (runtime) for the unskipped end.

**Several scenes loaded.** Each load's fix-up preloads its own soundtrack and stops the previous one, so with two
scenes loaded only the last loaded one's soundtrack is prepared. Playing a scene whose slot is already loaded
(`Scene_Play`, `0x00353818`, the start `0x0039d870`) preloads nothing: the only callers of `0x0010ff68` are the two
fix-ups, the pending re-preload and `SoundPreLoadScene`. So a scene played after another scene was loaded later
starts **the other scene's soundtrack** (the start's wait sees a ready task and event 13 starts it), and a scene
played again from its loaded slot after its soundtrack has ended starts nothing (dead handle, nothing pending).
Segment fix-ups preload too, which would stop the playing soundtrack mid-scene if a later segment carried a
type-13 event (*inferred* not to happen: each scene names one stereo sound, below). Confirmed (code).

**Other things that silence scene sound.**

- The soundtrack is 2D: its volume is `record volume × sound volume (+0x3fa20) × fade × pan` ([Task
  update](#three-d)); the music volume, the music duck and the 3D rules do not touch it.
- While a cinematic runs the manager's non-duckable flags are set (`0x00112b10`), so every duckable directional
  sound not owned by a player is scaled by the duck factor (`+0x3faac`, 0.2). Events 14 and 71 play duckable
  sounds with no owner (`0x0010fdd0`, last argument 1), so directional voice lines among them play at a fifth.
  Confirmed (code).
- `Human_PlaySpeech` plays nothing while a scene plays ([Saying a speech command](#speech)), and the start stops
  every human's held sound for a cinematic with roles ([Scenes](scenes.md#starting)).
- `SoundPauseSound` pauses every voice, the soundtrack included.

Music ducks to 0.75 while a cinematic runs ([Music](#music)). Scene events 14 and 71 play a sound by hash at a human's
position (the human transform at `0x00714b00 + index × 0x20`) only when that human's speech handle (`+0x168`) is idle,
storing the handle at `0x0021ef58`; event 71 on a car goes through `0x00110258`. *Confirmed (code).*

On the disc (*inferred*, from scanning the `.scn` files for listed hashes): 228 scenes name a stereo sound, using 228
of the 248 once each; every main level99 scene (`l99_c1`-`c9`, `l99_t1`, `l99_ash_intro`) has its own. The scenes
also name streamed mono sounds 280 times and bank sounds 4 times: the event 14/71 sounds.

### The front end and level99

- **Front end** (level 100): bank `menu` (14 sounds: the interface cues); music loops `MenuTrack` (`global.lua`:
  `music/in_the_city` once level 84 is complete; [Scripts](scripting.md#globallua)) and `music/wonderwheel_132b` from
  `level100.lua`; UI actions and the cues they play are on [Front end](frontend.md).
- **level99**: bank `sound` (*runtime*), sound matrix `sound`, listener 0; its ambience (emitters such as
  `tGulls01`, `tTrainyard01`, `tCreakHollowMetal01`-`03`) and speech (`vags/speeches/l99/...`) are listed in
  [Sound and music](../references/sound.md); `level99_combat.lua` configures `music/155e_fight_1` and
  `music/stripped_war`. The level's script flow is on [Scripting](scripting.md#level99).

### The PS2 sound device {#device}

`Device/ps2/sound/msaudiodevice.cpp` (`0x0014ba18`-`0x0014d508`; the code before it, from `0x0014b158`, is the memory
card's `mcbase.cpp`) is the audio manager's platform device: one object at `0x005df1e0` (built by its static
initialiser `0x0014d4d8`, pointer `0x0050bce4`), vtable `0x00537f88`. A **slot** on this page is an offset into
that vtable; each entry is 8 bytes, `{this adjustment, function}`. Below it, `0x0014d760`-`0x00151ed0` is SCEE's
MultiStream library, named by its own error strings (`SOUND_PlayStream`, `SOUND_FlushIOPCommand` ...). Confirmed
(code) throughout; the IOP side (`IOP.IRX`) was not read, so what a command does there is inferred from the EE
side's arguments and callers.

**Commands.** Each library call appends a command, `{id, length, 16-bit words}`, to a 2 KB EE buffer
(`0x005dfdc0`; `SOUND_StartCommand`, `SOUND_AddData`, `SOUND_EndCommand`), under one semaphore. The buffer goes to
the IOP in one RPC per send (`SOUND_FlushIOPCommand`, `0x0014fae8`), which also asks for the IOP's status; the
reply (`0x0014ee88`) gives each stream's state, position and flags, the SPU2 voices' idle bits and the disc-error
bytes, so the EE learns of a stream's end on a later send (inferred: at least a frame late). The device's update
(slot `+0x10`, `0x0014c140`, each frame) sends without waiting; loading uses a blocking copy (`0x0014c250`). A
buffer that fills mid-frame is sent at once, waiting for the previous send.

**Start-up** (slot `+0x08`, `0x0014bbd8`): the IOP connection, a reset (command 7), the stream limit 13, the
**fast load** thread (an EE RPC server, id `0x12344321`, that the IOP asks before it reads the disc, so the game's
own reads and the streams share the drive), then the stream buffers from the constructor's table, in IOP memory and
sound RAM, the sound-RAM cursor starting at `0x5010`:

| Streams | Use | IOP buffer | Sound-RAM buffer |
| --- | --- | --- | --- |
| 0 | loading banks (`.msb` into sound RAM, `.msd` into IOP memory) | `0x60000` | none |
| 1+2, 3+4 | the [stereo pairs](#stream-pairs): music and scene soundtracks (the child shares the parent's buffers) | `0x20000` | `0x10000` |
| 5-9 | streamed mono sounds | `0x10000` | `0x2000` |
| 10-12 | the small loops (class flag `0x20`) | `0x2000` | `0x2000` |

The bank goes after the stream buffers (device `+0x844`); `0x1c0000` minus the buffers is left for it (`+0x834`).
`.msd` indexes get a 4 KB IOP block (`+0x884`). Then four volume groups at full (`0x1000`), the effects cleared on
both cores, and `cdrom0:\IOP\BFW.SND;1` opened on streams 1-12; music opens `cdrom0:\IOP\MUSIC.SND;1` on its pair
when a track starts (slot `+0x20`). Stream state lives on the EE in `0x005e3ac8` (0 free, 2 starting, 3 reserved
or stopped, 5 stopping); SPU2 voices 13-47 serve bank samples (slot `+0xf8`), reserved (3) when taken and free once
the IOP reports them idle.

**Per voice.** A bank sample starts by hash (command 0, or `0x39` from an offset); a stream by
`SOUND_PlayStream` (command 1) with its file offset and size (command `0x29`). Volumes go through
`SOUND_SetChannelVolumeSmooth` (command `0x60`) as 15-bit levels scaled by the voice's group; pitch through command
3. A **positional** sound (class flag `0x02` or `0x08`) feeds the reverb while effects are on (slot `+0xb0`, command
`0x18`); every other sound, and the music's streams 1-4, is kept dry (slot `+0xb8`, command `0x19`).

**Disc errors** (`SOUND_HandleCDErrors`, `0x00150cf0`, each update): when the IOP reports a read error the streams
pause (command `0x37`) and the probe file `cdrom0:\SLUS_212.15;1` is tried each update until it opens; then they
resume (command `0x35`) and the IOP asks for the last stream again. Meanwhile the device sets `0x005e5580` to 4, 5
or 3 by the error (-1, -3, other) for the disc-error message, or, while loading, pushes the error mode
(`0x00157930(0x005e5560, 0, n)`). Confirmed (code); the readers' side is on [File I/O](file-io.md#disc-errors).

The device's and the library's functions, by address (names ours where the library's string gives none):

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0014ba18` | `AudioDevice_Construct` | the device's constructor (vtable `0x00537f88`): the 13 streams' buffer sizes (below) | confirmed (code) |
| `0x0014bbb0` | `AudioDevice_SetStreamSizes` | stores one stream's four sizes at `+0x888 + 16 × stream`: mono IOP buffer, pair IOP buffer, SPU2 buffer, pair-child flag | confirmed (code) |
| `0x0014bbd8` | `AudioDevice_Init` | slot `+0x08`: MultiStream start-up, stream buffers in IOP and sound RAM, `BFW.SND` opened on streams 1-12 ([The device](#device)) | confirmed (code) |
| `0x0014bf80` | `AudioDevice_Nop38` | slot `+0x38`: empty | confirmed (code) |
| `0x0014bf88` | `AudioDevice_AllocPairBuffer` | allocates a stereo pair's stream buffers (IOP `+0x88c`, SPU2 `+0x890`) and advances the SPU2 cursor `+0x840` | confirmed (code) |
| `0x0014c020` | `AudioDevice_PrintFreeIopRam` | asks the IOP for its free memory (command `0x2d`), waits, prints and keeps it (`0x0050bcec`) | confirmed (code) |
| `0x0014c070` | `AudioDevice_AllocMonoBuffer` | allocates a mono stream's buffers (IOP `+0x888`, SPU2 `+0x890`) | confirmed (code) |
| `0x0014c118` | `AudioDevice_ListsLoaded` | slot `+0x160`: empty on the PS2 | confirmed (code) |
| `0x0014c120` | `AudioDevice_Flush` | slot `+0x168`: sends the command buffer and waits (`0x0014fae8(0)`) | confirmed (code) |
| `0x0014c140` | `AudioDevice_Update` | slot `+0x10`, each frame: deferred command `0x3f` for flagged voices, disc-error check (sets `0x005e5580` to 3, 4 or 5), command buffer sent without waiting, the debug history (slot `+0x118`) when `0x0050bce8` is 1 | confirmed (code) |
| `0x0014c250` | `AudioDevice_UpdateBlocking` | the update used while loading: as slot `+0x10` but on a disc error pushes the error mode (`0x00157930(0x005e5560, 0, n)`) and waits for the send | confirmed (code) |
| `0x0014c358` | `AudioDevice_Nop158` | slot `+0x158`: calls an empty function (`0x0014c378`) | confirmed (code) |
| `0x0014c378` | `AudioDevice_Empty` | empty | confirmed (code) |
| `0x0014c380` | `AudioDevice_DebugHistory` | slot `+0x118`: one step of the streams' debug history (`0x0014c448`, `0x0014c3b8`) | confirmed (code) |
| `0x0014c3b8` | `AudioDevice_DebugPollStreams` | moves the history's 40-entry cursor and reads each of the 13 streams' info | confirmed (code) |
| `0x0014c448` | `AudioDevice_DebugRecordStreams` | records each stream's state (0 idle, 2, 3) in a 40-entry history (`+0x04`) | confirmed (code) |
| `0x0014c5c0` | `AudioDevice_Command25` | slot `+0x18`: IOP command `0x25` with one value (Init passes `0x200`); meaning not traced | confirmed (code) |
| `0x0014c5e0` | `AudioDevice_PauseAll` | slot `+0x60`: IOP command 10, every voice paused | confirmed (code) |
| `0x0014c600` | `AudioDevice_ResumeAll` | slot `+0x68`: IOP command 11 | confirmed (code) |
| `0x0014c620` | `AudioDevice_LoadBank` | slot `+0x30`: waits for stream 0, then loads `<name>.msb` into sound RAM at `+0x844` and `<name>.msd` into IOP memory at `+0x884` through stream 0 (`./ee_files/%s.msb` and `.msd` in the WAD; partial: the first 64 KB of the `.msb`), running the blocking update and the loading callback `0x0050b728` while it waits; then registers the index (`0x00151dc0`) | confirmed (code) |
| `0x0014c8b8` | `AudioDevice_SetStreamPriority` | slot `+0xd8`: for stream channels 5-9 with the flag set, IOP stream priority `0x3f` (`0x00151990`) | confirmed (code) |
| `0x0014c8f8` | `AudioDevice_RateToPitch` | slot `+0x28`: `RateToPitch` | confirmed (code) |
| `0x0014c918` | `AudioDevice_PrimeStream` | slot `+0x70`: for a real streamed task, preloads its stream without starting it (command `0x2f`) | confirmed (code) |
| `0x0014c968` | `AudioDevice_StartPrimed` | slot `+0x78`: starts a primed stream (command `0x2e`); -1 for a virtual or bank task | confirmed (code) |
| `0x0014c9c0` | `AudioDevice_IsPrimed` | slot `+0x80`: a real stream's info byte `+0x12` is 1; true for bank and virtual tasks | confirmed (code) |
| `0x0014ca20` | `AudioDevice_IsPrimedStrict` | slot `+0x88`: as `+0x80` but false for a bank sample | confirmed (code) |
| `0x0014ca88` | `AudioDevice_IsStreamActive` | slot `+0x140`: stream info `+0x11` is 1 | confirmed (code) |
| `0x0014cab8` | `AudioDevice_IsStreamPlaying` | slot `+0x148`: stream info `+0x11` and `+0x12` both 1 | confirmed (code) |
| `0x0014cfe0` | `AudioDevice_StopVoice` | slot `+0x98`: a real task's stream (both of a pair) stopped with command 5, or its SPU2 voice keyed off with command 4 | confirmed (code) |
| `0x0014d0c8` | `AudioDevice_FindFreeVoice` | slot `+0x100`: `MS_FindFreeVoice(lo, hi)` | confirmed (code) |
| `0x0014d0e8` | `AudioDevice_FindFreeBankVoice` | slot `+0xf8`: a free SPU2 voice of 13-47 | confirmed (code) |
| `0x0014d108` | `AudioDevice_FindFreeStream` | slot `+0xe0`: the first free stream from 5 | confirmed (code) |
| `0x0014d128` | `AudioDevice_FindFreeStreamIn` | slot `+0xe8`: the first free stream in a range | confirmed (code) |
| `0x0014d148` | `AudioDevice_ReleaseVoice` | slot `+0xf0`: frees a reserved SPU2 voice (state 3 → 0) | confirmed (code) |
| `0x0014d168` | `AudioDevice_VoiceState` | slot `+0x108`: the voice's EE-side state once the IOP reports it idle, else -1 (`Tasks_Update`'s "finished" test) | confirmed (code) |
| `0x0014d188` | `AudioDevice_VolumeToSpu` | `int(v × 16383) & 0x7fff` | confirmed (code) |
| `0x0014d1b0` | `AudioDevice_SetVolume` | slot `+0x50`: left and right clamped to ±1, sent with `MS_SetChannelVolumeSmooth` | confirmed (code) |
| `0x0014d288` | `AudioDevice_SetPitch` | slot `+0x58`: command 3 (voice, pitch word) | confirmed (code) |
| `0x0014d2b8` | `AudioDevice_PitchToRate` | slot `+0x40`: `pitch × 48000 / 4096` | confirmed (code) |
| `0x0014d2d8` | `AudioDevice_Duration` | slot `+0x48`: `⌊⌊size × 3.5⌋ / (2 × rate)⌋ × 1000` ms | confirmed (code) |
| `0x0014d338` | `AudioDevice_IsValidVoiceCount` | slot `+0x110`: value below `0x201` | confirmed (code) |
| `0x0014d340` | `AudioDevice_SetEffect` | slot `+0xa0`: `MS_EnableEffects(core, type, depth L × 32767, depth R × 32767, delay, feedback)`; `+0xb88` = 1 | confirmed (code) |
| `0x0014d398` | `AudioDevice_SetEffectVolume` | slot `+0xa8`: `MS_SetEffectMasterVolume(core, L × 32767, R × 32767)` | confirmed (code) |
| `0x0014d3e0` | `AudioDevice_VoiceEffectOn` | slot `+0xb0`: command `0x18`: the voice feeds the reverb (for positional sounds while effects are on) | confirmed (code) |
| `0x0014d408` | `AudioDevice_VoiceEffectOff` | slot `+0xb8`: command `0x19`: the voice is dry (other sounds, the music's streams 1-4) | confirmed (code) |
| `0x0014d428` | `AudioDevice_DisableEffects` | slot `+0xc0`: command `0x17` on both cores | confirmed (code) |
| `0x0014d450` | `AudioDevice_ClearEffects` | slot `+0xc8`: command `0x14` on both cores (start-up, the load screen) | confirmed (code) |
| `0x0014d478` | `AudioDevice_VoiceRegister` | slot `+0xd0`: command `0x5c` on the task's voice (`0x00151a98`) | confirmed (code) |
| `0x0014d498` | `AudioDevice_StreamInfoWord` | slot `+0x120`: stream info word `+0x38` | confirmed (code) |
| `0x0014d4c0` | `AudioDevice_UpdateListeners` | slot `+0x178`: empty on the PS2 | confirmed (code) |
| `0x0014d4c8` | `AudioDevice_Nop170` | slot `+0x170`: empty | confirmed (code) |
| `0x0014d4d0` | `AudioDevice_Nop180` | slot `+0x180`: empty | confirmed (code) |
| `0x0014d4d8` | `AudioDevice_StaticInit` | constructs the device at `0x005df1e0` | confirmed (code) |
| `0x0014d508` | `AudioDevice_StaticInitStub` | static-init stub ending `msaudiodevice.cpp` | confirmed (code) |
| `0x0014d528` | `AudioDevice_OpenMusic` | slot `+0x20`: opens `MUSIC.SND` on a stream pair (command `0x1f`) and sets the track's range (offset, interleave × blocks × channels; command `0x29`) | confirmed (code) |
| `0x0014d590` | `AudioDevice_PrepareMusic` | slot `+0x128`: sizes the pair's buffers to the track's interleave, plays the parent stream muted (looping or once), links the child (`SOUND_SetStreamParent_Int` / `_Child_Int`), sets the last block's end and preloads (command `0x2f`) | confirmed (code) |
| `0x0014d718` | `AudioDevice_StopMusic` | slot `+0x138`: stops a stream (command 5) | confirmed (code) |
| `0x0014d738` | `AudioDevice_StartMusic` | slot `+0x130`: starts a preloaded stream (command `0x2e`) | confirmed (code) |
| `0x0014d758` | `AudioDevice_Zero150` | slot `+0x150`: returns 0 | confirmed (code) |
| `0x0014d760` | `MS_InitIOP` | `SOUND_InitIOP`: the semaphore, the library's tables cleared, the RPC bound to `IOP.IRX` | confirmed (code) |
| `0x0014d9c8` | `MS_RpcDone` | the RPC's end callback → `MS_ReadReply` | confirmed (code) |
| `0x0014d9e8` | `MS_CallRpc` | `sceSifCallRpc` of the command buffer (`0x005e2dc0` reply, 2 KB), no wait | confirmed (code) |
| `0x0014da38` | `MS_StartCommand` | `SOUND_StartCommand(id)` | confirmed (code) |
| `0x0014daa8` | `MS_EndCommand` | `SOUND_EndCommand`: appends `{id, length, data}` to the 2 KB buffer, flushing first when full | confirmed (code) |
| `0x0014dc88` | `MS_AddShortWord` | `SOUND_AddData`: a 16-bit word | confirmed (code) |
| `0x0014dcf8` | `MS_AddLongData` | `SOUND_AddLongData`: a 32-bit word | confirmed (code) |
| `0x0014dd78` | `MS_AddString` | `SOUND_AddString` | confirmed (code) |
| `0x0014de28` | `MS_PlayStream` | `SOUND_PlayStream(source, stream, voice, volL, volR, pitch, flags, ...)`: command 1; stream `0x80` any free, `0x81`/`0x82` a free one with or without a flag | confirmed (code) |
| `0x0014e148` | `MS_LoadFile` | `SOUND_LoadFile(dest, stream, source, address)`: `0x7f` sound RAM, `0x7d` IOP memory, `0x7e` EE memory, `0x7b`; through `MS_PlayStream` | confirmed (code) |
| `0x0014e260` | `MS_FindFreeStream` | `SOUND_FindFreeStream`: state 0 or 3 | confirmed (code) |
| `0x0014e2d8` | `MS_FindStreamInRange` | `SOUND_FindFreeStreamRange(lo, hi)` | confirmed (code) |
| `0x0014e370` | `MS_PlayBankSample` | a bank sample by hash on an SPU2 voice (command 0; volumes, pitch, two flags from the task's `+0xe0`/`+0xe4`) | confirmed (code) |
| `0x0014e4b0` | `MS_PlaySampleFromOffset` | the same from an offset into the sample (command `0x39`) | confirmed (code) |
| `0x0014e628` | `MS_SetChannelPitch` | command 3 (voice, pitch) | confirmed (code) |
| `0x0014e6a0` | `MS_StopStream` | command 5; a value of 64 or more names a voice's stream | confirmed (code) |
| `0x0014e770` | `MS_StopChannel` | command 4: an SPU2 voice keyed off | confirmed (code) |
| `0x0014e7d0` | `MS_RequestStatus` | command 9 (value, 0): the status request appended to each send | confirmed (code) |
| `0x0014e858` | `MS_PauseAll` | command 10 | confirmed (code) |
| `0x0014e890` | `MS_ResumeAll` | command 11 | confirmed (code) |
| `0x0014e8c8` | `MS_ResetIop` | command 7 at start-up | confirmed (code) |
| `0x0014e900` | `MS_InitStreams` | command `0xc` (three values and the stream count limit `0x0050bd50`) at start-up | confirmed (code) |
| `0x0014e998` | `MS_SetMaxStreamLimit` | `SOUND_SetMaxStreamLimit(n)` (0-47; 13 here) | confirmed (code) |
| `0x0014ea28` | `MS_AllocateStreamBuffer` | `SOUND_AllocateStreamBuffer(stream, spu, size)`: command `0xd` | confirmed (code) |
| `0x0014eb40` | `MS_ResizeStreamBuffer` | `SOUND_ResizeStreamBuffer`: command `0x47` | confirmed (code) |
| `0x0014ec68` | `MS_ResizeSPUBuffer` | `SOUND_ResizeSPUBuffer`: command `0x52`, at least `0x400` bytes | confirmed (code) |
| `0x0014ee30` | `MS_SetSpuLoadAddress` | command `0xe`: the sound-RAM address for a load (`0x0050bd60`) | confirmed (code) |
| `0x0014ee88` | `MS_ReadReply` | parses the IOP's reply: each stream's state, position and flags, the voices' idle bits (`0x0050c518`, `0x0050c51c`), the disc-error bytes | confirmed (code) |
| `0x0014f938` | `MS_FindFreeVoice` | a voice in `lo`-`hi` that is free and idle; marks it reserved (3) | confirmed (code) |
| `0x0014fa38` | `MS_GetVoiceState` | the voice's state when the IOP reports it idle, else -1 | confirmed (code) |
| `0x0014fac8` | `MS_FlushWait` | `MS_FlushCommands(0)` | confirmed (code) |
| `0x0014fae8` | `MS_FlushCommands` | `SOUND_FlushIOPCommand(noWait)`: sends the buffer by RPC (waiting for the last unless `noWait`, then -1 if busy); keeps send statistics | confirmed (code) |
| `0x0014fcd8` | `MS_EnableEffects` | `SOUND_EnableEffects(core, type, depthL, depthR, delay, feedback)`: command `0x13`; type below 10; picks one of two effect work areas | confirmed (code) |
| `0x0014feb0` | `MS_ClearEffect` | command `0x14` (core), clearing its stored type | confirmed (code) |
| `0x0014ff20` | `MS_SetEffectMasterVolume` | command `0x15` (core, L, R) | confirmed (code) |
| `0x0014ff98` | `MS_ChannelEffectOn` | command `0x18` (voice) | confirmed (code) |
| `0x0014ffe0` | `MS_ChannelEffectOff` | command `0x19` (voice) | confirmed (code) |
| `0x00150028` | `MS_DisableEffect` | command `0x17` (core) | confirmed (code) |
| `0x00150078` | `RateToPitch` | `rate × 4096 / 48000` | confirmed (code) |
| `0x001500a8` | `MS_OpenStreamFile` | command `0x1f` (stream, offset, path): the IOP opens a disc file for a stream; a new id when -1 | confirmed (code) |
| `0x00150148` | `MS_SendCommand25` | command `0x25` (value) | confirmed (code) |
| `0x001501a8` | `MS_SetStreamRange` | command `0x29` (stream, offset, size) | confirmed (code) |
| `0x00150218` | `MS_Command2A` | command `0x2a` (value: 2 and 4 at start-up and after a movie) | confirmed (code) |
| `0x00150260` | `MS_Command2A6` | command `0x2a` (6) | confirmed (code) |
| `0x001502a0` | `MS_Command3A` | command `0x3a` (before a movie) | confirmed (code) |
| `0x001502d8` | `MS_Command3B` | command `0x3b` (before a movie) | confirmed (code) |
| `0x00150310` | `MS_Command3C` | command `0x3c` (after a movie) | confirmed (code) |
| `0x00150348` | `MS_GetVoiceStream` | the stream a voice is playing, or -1 | confirmed (code) |
| `0x00150380` | `MS_SetStreamParent` | `SOUND_SetStreamParent_Int`: command `0x2b` | confirmed (code) |
| `0x00150448` | `MS_SetStreamChild` | `SOUND_SetStreamChild_Int`: command `0x2c` | confirmed (code) |
| `0x00150628` | `MS_RequestFreeIopRam` | command `0x2d` | confirmed (code) |
| `0x00150660` | `MS_SetMIBEndOffset` | `SOUND_SetMIBEndOffset`: command 99 | confirmed (code) |
| `0x00150768` | `MS_StartPreloaded` | command `0x2e`: starts a preloaded stream | confirmed (code) |
| `0x001507d0` | `MS_PreloadStream` | command `0x2f`: fills a stream's buffer without starting it | confirmed (code) |
| `0x00150838` | `MS_GetStreamInfo` | `SOUND_GetStreamInfo(stream, info)`: IOP free memory `+0x00`, state `+0x11` (0 idle, 1 active, 6 starting), primed `+0x12`, play time `+0x42`-`+0x48` | confirmed (code) |
| `0x00150b48` | `MS_NextFileId` | the next stream-file id | confirmed (code) |
| `0x00150b70` | `MS_SetEeLoadAddress` | command `0x31`: the EE address for a load | confirmed (code) |
| `0x00150bc8` | `MS_SetIopLoadAddress` | command `0x32`: the IOP address for a load | confirmed (code) |
| `0x00150c28` | `MS_ResumeAfterDiscError` | command `0x35` | confirmed (code) |
| `0x00150c60` | `MS_PauseForDiscError` | command `0x37` | confirmed (code) |
| `0x00150ca8` | `MS_SetDiscProbeFile` | keeps the file opened to test the disc (`cdrom0:\SLUS_212.15;1`) | confirmed (code) |
| `0x00150cf0` | `MS_HandleCDErrors` | `SOUND_HandleCDErrors`: on the IOP's disc error, pauses the streams, and once the probe file opens again resumes them; returns the error | confirmed (code) |
| `0x00150ed0` | `MS_Command3E` | command `0x3e` (1) at start-up | confirmed (code) |
| `0x00150f18` | `MS_Command3F` | command `0x3f` (voice), sent after a bank sample's start | confirmed (code) |
| `0x00150fc8` | `MS_SetGroupMasterVolume` | `SOUND_SetGroupMasterVolume(group, L, R)` (0-`0x1000`; four groups, full at start-up) | confirmed (code) |
| `0x001510c8` | `MS_ApplyGroupVolume` | command 2: resends the volumes of a group's voices | confirmed (code) |
| `0x001511a8` | `MS_SetVoiceGroup` | a voice's group | confirmed (code) |
| `0x001511c0` | `MS_ScaleByGroup` | a voice's volumes × its group's master volume | confirmed (code) |
| `0x00151250` | `MS_SetVoiceVolumes` | keeps a voice's volumes and scales them by its group | confirmed (code) |
| `0x001512d8` | `MS_ScaleVolume` | `v × master / 4096`, mirrored for inverted-phase volumes | confirmed (code) |
| `0x00151360` | `MS_FastLoadThread` | the EE RPC server thread (id `0x12344321`) the IOP asks during fast loads | confirmed (code) |
| `0x001513d0` | `MS_InitInternalFastLoad` | `SOUND_InitINTERNALFastLoad`: creates that thread (priority 9, a 32 KB buffer); command `0x51` | confirmed (code) |
| `0x001514a8` | `MS_FastLoadRpc` | the server's handler: 999 invalidates a cache range, otherwise asks the game's callback (`0x0050c504`) for the load mode | confirmed (code) |
| `0x00151640` | `MS_SetInternalFastLoad` | `SOUND_SetINTERNALFastLoad(mode)`: command `0x51` | confirmed (code) |
| `0x00151750` | `MS_SendCommand53` | command `0x53` (core) | confirmed (code) |
| `0x00151798` | `MS_SendCommand57` | command `0x57` | confirmed (code) |
| `0x001517f0` | `MS_SetChannelVolumeSmooth` | `SOUND_SetChannelVolumeSmooth(voice, L, R)`: command `0x60` | confirmed (code) |
| `0x001518c0` | `MS_WriteRegister` | command `0x5c` (register, value) | confirmed (code) |
| `0x00151930` | `MS_GetLastStatus` | `0x0050bd74` | confirmed (code) |
| `0x00151980` | `MS_IsRpcBusy` | `0x0050bd30` | confirmed (code) |
| `0x00151990` | `MS_SetStreamPriority` | `SOUND_SetStreamPriority(stream, priority)`: command 100; `0x100` the default | confirmed (code) |
| `0x00151a98` | `MS_SetVoiceRegister` | a value below 32 into a voice's SPU2 register (`0x400` block) through command `0x5c` | confirmed (code) |
| `0x00151bc0` | `MS_LockStart` | `SOUND_MultiThreadSafeCheckStart`: takes the semaphore | confirmed (code) |
| `0x00151c70` | `MS_LockEnd` | `SOUND_MultiThreadSafeCheckEnd` | confirmed (code) |
| `0x00151cd8` | `MS_ScanStreamsFrom` | the first stream from `n` in state 0 or 3 | confirmed (code) |
| `0x00151d28` | `MS_ScanStreamsBetween` | the same within a range | confirmed (code) |
| `0x00151d88` | `MS_ReleaseVoice` | state 3 → 0 | confirmed (code) |
| `0x00151dc0` | `MS_RegisterBank` | command `0x74` (index address, size): the loaded `.msd` | confirmed (code) |
| `0x00151e68` | `Gif_RemoveCallback` | removes a callback from the 8-entry list (`0x005e4448`, [Movies](movies.md)) | confirmed (code) |
| `0x00151ed0` | `Gif_AddCallback` | adds a callback and its data to that list | confirmed (code) |

## Function index {#function-index}

The unit's other functions (`0x0010d758`-`0x0011b770`), by address; names are ours. The device layer's are under
[The PS2 sound device](#device).

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0010d758` | `MusicPlayer_Init` | the [music player](#music-player)'s constructor: the stream-slot bytes (`0x0011b400`), mood 3 (none) at `+0x1c` and `+0x20`, music volume 1.0, options' music volume 0.9, the empty track list, the three channels and their pointers `+0x154`-`+0x15c`, no tracks per mood | confirmed (code) |
| `0x0010d830` | `MusicPlayer_AddTrack` | appends one track to the track list (`0x0010ed50`; `MusicList_Load`) | confirmed (code) |
| `0x0010d850` | `MusicPlayer_AllocTracks` | allocates the track list for `n` tracks (`0x0010edd0`) | confirmed (code) |
| `0x0010d870` | `MusicPlayer_ConfigTrack` | `SndCfgMusicInfo`: finds the track by the name's CRC-32 and stores its volume in the record and its bar length | confirmed (code) |
| `0x0010d9a0` | `Music_StopTrack` | `SoundStopMusicTrack`, per channel by state: queued, dropped; pre-loading or blocked, stopped at once; playing or waiting for the bar, a fade-out over one bar of its track from now; fading in, the same fade-out started from the level it has reached | confirmed (code) |
| `0x0010db18` | `MusicChannel_Start` | starts a pre-loaded channel: fade level 0 when it fades in, else 1; volumes; device slot `+0x130` (start the stream pair); play start = now | confirmed (code) |
| `0x0010dbb0` | `MusicChannel_Preload` | state 2: stops the pair (slot `+0x138`), opens the track of `MUSIC.SND` on it (slot `+0x128`, with the loop flag), mutes both sides (slot `+0x50`), fade level 0 | confirmed (code) |
| `0x0010dd50` | `MusicChannel_Stop` | stops an active channel's pair and frees its slot (`0x0011b740`) | confirmed (code) |
| `0x0010ddd8` | `Music_StopAll` | clears the queued request, stops and resets both playing channels, then device slot `+0x168` | confirmed (code) |
| `0x0010def8` | `Music_IsAnyChannelBusy` | whether any of the three channels is not idle (state `+0x11`) | confirmed (code) |
| `0x0010df28` | `SystemMusic_GetMood` | the playing mood `+0x1c` | confirmed (code) |
| `0x0010df30` | `Music_Update` | each music update: the pending scene soundtrack's re-preload, freeing stereo slots (`0x0011b5e8`), `SystemMusic_Update`, the three channels, the volumes; the debug check `0x0010eb40` when `0x0050a8dc` is set | confirmed (code) |
| `0x0010e468` | `MusicChannel_FadeProgress` | `(now - fade start) / fade length`, clamped to 0-1 | confirmed (code) |
| `0x0010e530` | `MusicChannel_FadeRemaining` | `1 - MusicChannel_FadeProgress` | confirmed (code) |
| `0x0010e710` | `Music_SetVolume` | `SoundSetMusicVolume`: `+0x5c`, clamped to 0-1, and the "changed" flag `+0x64` | confirmed (code) |
| `0x0010e758` | `Music_SetOptionsVolume` | the options' music volume `+0x60`, clamped to 0-1 | confirmed (code) |
| `0x0010e798` | `MusicChannel_IsActive` | channel non-null and its `+0x00` set | confirmed (code) |
| `0x0010e7b8` | `MusicChannel_IsActiveAt` | whether channel `i`'s `+0x00` is set | confirmed (code) |
| `0x0010e9e0` | `SystemMusic_SetTracks` | `SoundSetMusicTrack(mood, a, b, c)`: up to three track hashes for a mood and their count; for the playing mood forces a change and turns the system music on (`0x0041d3d8`-style game-state call) | confirmed (code) |
| `0x0010eab0` | `MusicStream_GetStateName` | the state's name (`ST_Idle` ...), for the debug check | confirmed (code) |
| `0x0010eb40` | `Music_DebugCheck` | a debug pass: formats the channels' state names and re-checks the configured tracks, with no visible output; runs only when `0x0050a8dc` is set, which nothing writes (0 in the executable) | confirmed (code); never runs |
| `0x0010ed20` | `MusicPlayer_FreeTracks` | releases the track list (`0x0010ee90`) | confirmed (code) |
| `0x0010ed40` | `TrackList_Init` | empty list, owner flag 1 | confirmed (code) |
| `0x0010ed50` | `TrackList_Append` | copies a 16-byte track into the next entry | confirmed (code) |
| `0x0010ed90` | `TrackList_Find` | index of the track with that name hash, or -1 | confirmed (code) |
| `0x0010edd0` | `TrackList_Alloc` | allocates `n` 16-byte tracks (tag `MusicTrack`) in the global heap; `MusicList.cpp`'s anchor | confirmed (code) |
| `0x0010ee90` | `TrackList_Release` | tells the device to release each track (slot `+0x20`), clears the owner flag | confirmed (code) |
| `0x0010ef18` | `MusicTrack_Init` | `{record, crc32(name at +0x28), 1, 2000}`; adds the track's size to a running total `0x0050a8f0` | confirmed (code) |
| `0x0010ef90` | `MusicTrack_Copy` | copies a 16-byte track | confirmed (code) |
| `0x0010efc0` | `Handle_Set` | stores a word (a sound handle) | confirmed (code) |
| `0x0010efd0` | `Handle_Copy` | copies a sound handle | confirmed (code) |
| `0x0010efe0` | `Sound_IsDirectional` | class flag `0x10` | confirmed (code) |
| `0x0010f018` | `Sound_IsPositional` | class flag `0x02` or `0x08` | confirmed (code) |
| `0x0010f050` | `Sound_UsesReservedStreams` | class flag `0x20` (stream channels 10-12) | confirmed (code) |
| `0x0010f080` | `Sound_GetHash` | record `+0x08` | confirmed (code) |
| `0x0010f098` | `Sound_GetStereoInterleave` | the [stereo table](formats/audio.md#stereo) record's interleave for this hash, or 0 | confirmed (code) |
| `0x0010f0e8` | `Sound_GetStereoBlocks` | the stereo record's block count, or 0 | confirmed (code) |
| `0x0010f138` | `Sound_GetStereoLastBlock` | the stereo record's last-block bytes, or 0 | confirmed (code) |
| `0x0010f188` | `Sound_GetVolume` | record `+0x0d` / 100 | confirmed (code) |
| `0x0010f1b0` | `Sound_GetNear` | class byte 0, metres | confirmed (code) |
| `0x0010f1e8` | `Sound_GetFar` | class byte 1 × 5, metres | confirmed (code) |
| `0x0010f228` | `Sound_GetPitchVariation` | record `+0x0c`, percent | confirmed (code) |
| `0x0010f238` | `Sound_GetRate` | the rate table `0x0050a9e0` at record `+0x0e` | confirmed (code) |
| `0x0010f258` | `Sound_GetSize` | record `+0x00` | confirmed (code) |
| `0x0010f268` | `Sound_GetOffset` | record `+0x04` | confirmed (code) |
| `0x0010f278` | `Sound_GetPriority` | class byte 3 | confirmed (code) |
| `0x0010f2a0` | `Sound_GetChannels` | class byte 4 | confirmed (code) |
| `0x0010f2c8` | `Sound_IsLooping` | class flag `0x01` | confirmed (code) |
| `0x0010f2f0` | `Sound_IsStreamed` | class flag `0x04` | confirmed (code) |
| `0x0010f320` | `SoundList_LoadChunk` | chunk `0x29` handler: keeps the chunk at `0x0059867c` and hands it to `SoundList_Load` (`0x0010f900`) | confirmed (code) |
| `0x0010f360` | `MusicList_LoadChunk` | chunk `0x31` handler: `0x00598680`, then `MusicList_Load` (`0x0010f9d8`) | confirmed (code) |
| `0x0010f3a0` | `SoundClasses_LoadChunk` | chunk `0x48` handler: the class table `0x00598670` | confirmed (code) |
| `0x0010f3d8` | `StereoTable_LoadChunk` | chunk `0x49` handler: `0x00598688`, then `StereoTable_Load` (`0x0010f988`) | confirmed (code) |
| `0x0010f410` | `SoundAnimData_LoadChunk` | chunk `0x4a` (Sound Anim Data) handler: keeps it at `0x0059868c`, which no code reads | confirmed (code); unused inferred |
| `0x0010f450` | `SoundHandles_FindFree` | the first free slot of the 256-entry handle table `0x00598698` from a start index, or -1 | confirmed (code) |
| `0x0010f4b0` | `SoundHandles_FirstFree` | `SoundHandles_FindFree(1)`: slot 0 is never used | confirmed (code) |
| `0x0010f4d0` | `SoundHandle_Make` | a handle with the slot in the top 16 bits and the serial (`0x00598a98`, counting up) in the low 16 for a task, stored in the slot | confirmed (code) |
| `0x0010f520` | `SoundHandle_Free` | clears the handle's slot | confirmed (code) |
| `0x0010f558` | `SoundHandles_Set` | stores a task pointer in a slot | confirmed (code) |
| `0x0010f578` | `SoundHandle_Resolve` | the task of a handle when its slot holds a task whose first word (the handle) matches, else null; -1 is no sound (154 callers) | confirmed (code) |
| `0x0010f5c8` | `SoundHandles_StaticInit` | the unit's static initialiser: the null handle `0x00598690` = -1, which the game copies wherever it means "no sound" | confirmed (code) |
| `0x0010f5f0` | `SoundHandles_StaticInitStub` | its stub; `MusicList.cpp`'s unit ends here | confirmed (code) |
| `0x0010f768` | `AudioManager_InitDefaults` | seeds the audio random (`0x006eb8b0`) from the timer, picks the first load screen (`Random % 7`, `+0x3fa28`) and sets both option volumes to 0.9 | confirmed (code) |
| `0x0010f7f8` | `AudioManager_ResetLowPriorityCount` | `+0x3fa34` = 0 | confirmed (code) |
| `0x0010f900` | `SoundList_Load` | makes the sound list: `n` 4-byte entries, each a pointer to a 16-byte record of the chunk | confirmed (code) |
| `0x0010f978` | `SoundClasses_SetTable` | stores the class table pointer at `0x00598670` (from `SoundClasses_LoadChunk`) | confirmed (code) |
| `0x0010f988` | `StereoTable_Load` | stores the stereo table's count `0x00598678` and records `0x00598674` | confirmed (code) |
| `0x0010f9a0` | `AudioDevice_ListsLoaded` | device slot `+0x160` after a list is loaded; the PS2 device's is empty (`0x0014c118`) | confirmed (code) |
| `0x0010f9d0` | `Audio_IopCommandHook` | an empty function the IOP command sender calls (`0x0014daa8`) | confirmed (code) |
| `0x0010f9d8` | `MusicList_Load` | makes the track list: per 104-byte record a 16-byte track (`MusicTrack_Init`) | confirmed (code) |
| `0x0010fba8` | `Sound_StopAll` | `AudioManager_StopAll` on the global manager: from `Movie_Play`, the pause menu, mode changes | confirmed (code) |
| `0x0010fbc8` | `AudioManager_Play2DFadeIn` | plays a 2D sound at volume and pitch 1 with fade mode 1 (in), duckable; returns its handle or the null handle (the ambient track) | confirmed (code) |
| `0x0010fc30` | `Sound_PlayInterfaceCue` | plays interface cue `n` of the sound matrix (`0x00111130` → `0x00116828`) as a 2D sound with flags `0x12` | confirmed (code) |
| `0x0010fe30` | `AudioManager_PlayAt` | plays a sound by hash at a position with the default owner (`0x006ebd30`), no fade; returns the handle | confirmed (code) |
| `0x0010fe90` | `AudioManager_PlayOwned` | plays a sound for an owner at a copy of a position (the human speech and grunt calls); returns the handle | confirmed (code) |
| `0x0010fee8` | `SoundTask_SetFadeLevel` | the task of a handle: fade level `+0x78` | confirmed (code) |
| `0x0010ff28` | `SoundTask_SetVolume` | the task of a handle: caller's volume `+0x74` (`SetAmbientTrackVolume` and others) | confirmed (code) |
| `0x0010ff68` | `Scene_PreloadSoundtrack` | stops a live soundtrack, claims a stream pair or falls back (`0x00112d60`), prepares the sound and keeps its handle in `+0x24268` and the music player's `+0x08` ([Scene soundtracks](#scene-sound)) | confirmed (code) |
| `0x00110018` | `Scene_StartSoundtrack` | scene event 13: starts whatever task `+0x24268` holds and sets `+0x2426c` to whether it is alive | confirmed (code) |
| `0x00110078` | `Scene_StopSoundtrack` | stops the soundtrack's task and clears `+0x2426c` | confirmed (code) |
| `0x001100b0` | `Sound_Prepare2DByName` | `Sound_Prepare2D` with the name's CRC-32 (the lock-pick dial, Rumble screens) | confirmed (code) |
| `0x001100e8` | `Sound_Prepare2D` | makes a 2D task at volume and pitch 1, duckable, without starting it (`0x00111f78`); returns the handle | confirmed (code) |
| `0x00110138` | `Sound_PrepareAt` | the same at a position the task keeps a pointer to | confirmed (code) |
| `0x00110188` | `Sound_PrepareFull` | `AudioManager_Prepare` with every argument passed through (`Human_PlaySpeech`) | confirmed (code) |
| `0x001101b8` | `Sound_PrepareAtCopy` | the same as `Sound_PrepareAt` with a copy of the position | confirmed (code) |
| `0x00110210` | `Sound_PrepareAtByName` | `Sound_PrepareAtCopy` with the name's CRC-32 (`WorldObject_PreloadSound`) | confirmed (code) |
| `0x00110258` | `Sound_StartPrepared` | starts a prepared task by handle (`0x00111f40` → `0x0011b298`, device slot `+0x78`); 0 for a dead handle | confirmed (code) |
| `0x001102a0` | `Sound_IsPrepared` | a prepared task's stream is primed (device slot `+0x80`); a bank sound counts as ready, a dead handle as not | confirmed (code) |
| `0x00110318` | `Sound_Stop` | stops the task of a handle (`AudioManager_StopTask`, `0x00112c60`), with the "free its stream" flag | confirmed (code) |
| `0x00110370` | `Sound_FadeOut` | starts a fade-out of the given length on a task not already fading out (`0x0011b1b0`) | confirmed (code) |
| `0x001103d0` | `Sound_SetPositionAndFacing` | copies a position and facing into a task (`+0xc0`, `+0xd0`); false for a dead handle | confirmed (code) |
| `0x00110430` | `Sound_SetPosition` | copies a position into a task (`+0xc0`) | confirmed (code) |
| `0x00110488` | `Sound_Nop` | calls an empty function (`0x001120c0`) | confirmed (code) |
| `0x001104a8` | `Sound_CfgMusicInfo` | `MusicPlayer_ConfigTrack` on the global manager (`SndCfgMusicInfo`) | confirmed (code) |
| `0x001104c8` | `Sound_SetMusicTrack` | `SystemMusic_SetTracks` on the global manager (`SoundSetMusicTrack`; mode 1's enter clears three moods with it) | confirmed (code) |
| `0x001104e8` | `Sound_LoopMusicTrack` | `Music_Play(track, loop 1, no callback, fade bars)` | confirmed (code) |
| `0x00110508` | `Sound_PlayMusicTrack` | `Music_Play(track, loop 0, callback, fade bars)` | confirmed (code) |
| `0x00110528` | `Sound_StopMusicTrack` | `SoundStopMusicTrack` (`0x0010d9a0`) | confirmed (code) |
| `0x00110548` | `Sound_StopAllMusic` | `Music_StopAll` | confirmed (code) |
| `0x00110570` | `Sound_GetMusicVolume` | the music volume (music player `+0x5c`) | confirmed (code) |
| `0x00110590` | `Sound_SetMusicVolume` | `Music_SetVolume` | confirmed (code) |
| `0x001105b0` | `Sound_SetOptionsMusicVolume` | the options' music volume, clamped to 0-1, into the music player (`0x00112ec0`) and game state `+0x57ac` | confirmed (code) |
| `0x00110618` | `Sound_GetOptionsMusicVolume` | the options' music volume (`0x00112eb8`) | confirmed (code) |
| `0x00110638` | `Sound_SetEffectsVolume` | the options' sound-effect volume `+0x3fa20`, clamped, mirrored at game state `+0x57a8` | confirmed (code) |
| `0x00110690` | `Sound_LoadMatrix` | `SndLoadMatrix` on the global manager (`SoundMatrix_Load`, `0x00114628`) | confirmed (code) |
| `0x001106b0` | `Sound_VoiceNextLine` | `VoiceTable_NextLine` through the matrix (`0x00114b00`) | confirmed (code) |
| `0x001106d0` | `Sound_MarkCommandSaid` | for commands 1-6, `0x12`, `0x46`, `0x56` and `0x9e` (and `0x28` while `0x0041d160` is 1): calls `0x00111688` and sets human `+0x16c` | confirmed (code) |
| `0x00110790` | `Sound_PlayMaterialHit` | plays the first sound of a material pair's [matrix](#sound-matrix) entry at a position, volume × the entry's first column volume (`0x0021f700`) | confirmed (code) |
| `0x00110830` | `Sound_PlayMaterialPair` | plays columns 2 and 1 of a material pair's entry at a position, each with its column volume; for first material 13 the second plays at pitch `0x003354e0() × 0.2` (16 callers: contacts, objects, footsteps) | confirmed (code) |
| `0x00110940` | `Sound_PlayMaterialPairPlain` | the same without the material-13 pitch (`0x0021f290`) | confirmed (code) |
| `0x00110a08` | `Sound_PlayAnimSound` | plays column 1 of an [animation sound](#sound-matrix) entry at a position with its column volume | confirmed (code) |
| `0x00110aa8` | `Sound_RemapFootMaterial` | for the footstep caller (`0x0021f290`): material `0x12` → `0x6a`, 6 → 5, `0x79` → `0x23` at half volume, else unchanged at 1 | confirmed (code) |
| `0x00110b18` | `AmbientTrack_PlayByName` | `AmbientTrack_Play` with the name's CRC-32; returns `+0x24274` | confirmed (code) |
| `0x00110cc0` | `AmbientTrack_SetVolume` | `SetAmbientTrackVolume`: the bed task's caller volume, clamped | confirmed (code) |
| `0x00110d50` | `Ambient_TryPlay` | forwards to `AmbientEmitter_TryPlay` (`0x0010d480`) on the ambient manager `+0x24280` | confirmed (code) |
| `0x00110d78` | `Sound_FindByName` | `Sound_Find` with the name's CRC-32 | confirmed (code) |
| `0x00110db0` | `Sound_Find` | the sound list record for a hash, or null (`0x00111c80`); the matrix and voice-table builders use it to skip missing sounds | confirmed (code) |
| `0x00110dd0` | `Sound_GetFarByName` | `AudioManager_GetFarByHash` with the name's CRC-32 | confirmed (code) |
| `0x00110e08` | `Sound_GetFarByHash` | `AudioManager_GetFarByHash` on the global manager | confirmed (code) |
| `0x00110e28` | `Ambient_AddSphereEmitter` | forwards to `AmbientManager_AddSphereEmitter` (`0x0010cd68`) | confirmed (code) |
| `0x00110e58` | `Ambient_AddParticleEmitter` | forwards to `AmbientManager_AddParticleEmitter` (`0x0010ced0`) | confirmed (code) |
| `0x00110e98` | `Ambient_AddEmitter` | forwards to `AmbientManager_AddEmitter` (`0x0010cf58`) | confirmed (code) |
| `0x00110ee0` | `Ambient_SetEmitterPoints` | forwards to `AmbientEmitter_SetPoints` (`0x0010d590`) | confirmed (code) |
| `0x00110f08` | `Ambient_SetEmitterEnabled` | forwards to `AmbientEmitter_SetEnabled` (`0x0010d270`) | confirmed (code) |
| `0x00110f30` | `Ambient_SetTableSlot` | forwards to `AmbientTable_Set` (`0x0010d3c8`): `AddAmbientSound` | confirmed (code) |
| `0x00110f58` | `Ambient_Reset` | forwards to `AmbientManager_Reset` (`0x0010bf40`) | confirmed (code) |
| `0x00110f80` | `Sound_NewMaterialSlots` | forwards to `SoundMatrix_NewMaterialSlots` (`0x00115cb0`) | confirmed (code) |
| `0x00110fa0` | `Sound_NewMaterialSound` | forwards to `SoundMatrix_NewMaterialSound` (`0x00115ee8`) | confirmed (code) |
| `0x00110fc0` | `Sound_SetNumberOfMaterialSlots` | forwards to `0x00116480` | confirmed (code) |
| `0x00110fe0` | `Sound_DuplicateSoundMaterials` | forwards to `0x00116438` | confirmed (code) |
| `0x00111000` | `Sound_NewAnimSlots` | forwards to `0x00116080` | confirmed (code) |
| `0x00111020` | `Sound_NewAnimSound` | forwards to `0x001162a0` | confirmed (code) |
| `0x00111040` | `Sound_AllocateCharacterVoices` | forwards to `VoiceTable_Build` | confirmed (code) |
| `0x00111060` | `Sound_SetCommandSoundPercent` | forwards to `0x00115bb0` | confirmed (code) |
| `0x00111080` | `Sound_WarnLineHash` | forwards to `VoiceTable_WarnLineHash` (`0x001148e8`), for the gangs' warning barks | confirmed (code) |
| `0x001110a8` | `Sound_VoiceLineHash` | forwards to `VoiceTable_LineHash` (`0x00114a38`) | confirmed (code) |
| `0x001110c8` | `Sound_SetEffect` | `SoundSetEffect`: forwards to `AudioManager_SetEffect` (`0x00113000`); mode 1's enter also calls it | confirmed (code) |
| `0x001110e8` | `Sound_EnableEffects` | `SoundEnableEffects`: forwards to `AudioManager_EnableEffects` (`0x001130c8`) | confirmed (code) |
| `0x00111110` | `Sound_CfgInterfaceSound` | forwards to `SoundMatrix_SetCue` (`0x001167b8`) | confirmed (code) |
| `0x00111130` | `AudioManager_GetCueTable` | the sound matrix's interface-cue lookup (`0x00116828`) | confirmed (code) |
| `0x00111150` | `Sound_DistanceToListener` | a copy of a point's distance to the nearest player listener (`0x00113188`), the [emitters'](#ambient) range test | confirmed (code) |
| `0x00111428` | `AudioManager_StopLoadScreen` | stops both halves; when the left one was real, waits (yielding, updating the audio manager and the device) until its voice is idle (device slot `+0x108`) | confirmed (code) |
| `0x00111558` | `SoundTask_SetPan` | a 2D task's left and right pan gains (`+0x50`, `+0x54`) | confirmed (code) |
| `0x001115a8` | `AudioManager_GetBankName` | copies the current bank's name (15 chars) | confirmed (code) |
| `0x001115e0` | `AudioManager_GetPendingBank` | copies the pending bank's name | confirmed (code) |
| `0x00111618` | `AudioManager_SetPendingBank` | keeps a bank name to load after loading (`+0x3fa48`) | confirmed (code) |
| `0x00111650` | `AudioManager_VolumeHook` | an empty function the material and animation sound players call with their volume factor (left at 1) | confirmed (code) |
| `0x00111658` | `AudioManager_GetTask` | the task of a handle, or null | confirmed (code) |
| `0x00111688` | `AudioManager_CullQuietStreamsFwd` | forwards to `0x00113258` | confirmed (code) |
| `0x001116a8` | `AudioManager_StaticInit` | the unit's static initialiser: constructs the global audio manager `0x00598aa0` (music player `0x0010d758`, matrix `0x001141e0`, ambient manager `0x0010bdb0`) and sets its handle fields to -1 | confirmed (code) |
| `0x001118a0` | `AudioManager_StaticInitStub` | its stub | confirmed (code) |
| `0x001118c0` | `SoundList_Clear` | count and capacity 0 | confirmed (code) |
| `0x001118d0` | `SoundList_Alloc` | `n` 4-byte entries (tag `Sound`); `SoundList.cpp`'s anchor | confirmed (code) |
| `0x00111990` | `SoundList_Append` | copies one record pointer into the next entry | confirmed (code) |
| `0x001119d0` | `SoundList_Find` | binary search by the record's hash `+0x08`; index or -1 | confirmed (code) |
| `0x00111aa0` | `SoundList_EntryAt` | entry `i` | confirmed (code) |
| `0x00111ab0` | `SoundList_EntryAt2` | entry `i` (a second copy) | confirmed (code) |
| `0x00111ac0` | `SoundTasks_CreatePool` | the `FreeList<SoundTask>` (`0x0050aa94`): 256 tasks of `0xf0` bytes and their free list, in the global heap at start-up; `SoundListener.cpp`'s anchor | confirmed (code) |
| `0x00111c30` | `AudioManager_ClearLists` | empties the task list, the sound list and other counters | confirmed (code) |
| `0x00111c80` | `AudioManager_FindSound` | binary search of the sound list (`SoundList_Find`), the record or null | confirmed (code) |
| `0x00111cc0` | `TaskList_Append` | links a task at the tail of the task list and counts it | confirmed (code) |
| `0x00111d00` | `AudioManager_FreeTask` | unlinks a task, stops its voice unless already stopped (device slot `+0x98`), clears it (`0x0011a150`) and returns it to the pool | confirmed (code) |
| `0x00111f40` | `AudioManager_StartTask` | starts a task's voice (`0x0011b298`); 0 for none | confirmed (code) |
| `0x00111f78` | `AudioManager_Prepare` | the same without fade and `a8`; a bank sound starts at once, a streamed one is only primed (`0x0011b128(task, 0)`) | confirmed (code) |
| `0x001120a0` | `AudioManager_IsTaskReady` | `0x0011b3c0`: the device's "stream primed" status | confirmed (code) |
| `0x001120c0` | `AudioManager_Nop` | empty | confirmed (code) |
| `0x001129b8` | `AudioManager_UpdateTask` | `SoundTask_Update` with the manager's listeners | confirmed (code) |
| `0x001129e0` | `AudioManager_DeviceEC` | device slot `+0xe8` (`0x0014d128` → `0x00151d28`) | confirmed (code) |
| `0x00112a10` | `AudioManager_DeviceFlush` | device slot `+0x100` (`0x0014d0c8` → `0x0014f938`) | confirmed (code) |
| `0x00112a40` | `AudioManager_GetFarByHash` | the far distance of the sound with a hash (class byte 1 × 5 m), 0 for hash 0 | confirmed (code) |
| `0x00112a90` | `AudioManager_StopAll` | stops every live task (`0x00111d00`), then device slot `+0x168` | confirmed (code) |
| `0x00112c48` | `AudioManager_MarkNonDuckable` | sets both "a non-duckable sound is playing" flags `+0x1d8` / `+0x1dc` when given true (the cinematic case of `Tasks_Update`) | confirmed (code) |
| `0x00112c60` | `AudioManager_StopTask` | stops a task: a bank sample (stream 999) with the flag also releases its stream slot (`0x0011b2f0`); a playing task's voice is stopped (device slot `+0x98`); the task is freed unless `+0xe4` is set and it is real | confirmed (code) |
| `0x00112d00` | `AudioManager_AddSound` | appends one record pointer to the sound list `+0x0c` (`0x00111990`) | confirmed (code) |
| `0x00112d20` | `AudioManager_AllocSounds` | allocates the sound list for `n` sounds (`0x001118d0`, `SoundList.cpp`) | confirmed (code) |
| `0x00112d40` | `AudioManager_AllocTracks` | `MusicPlayer_AllocTracks` on `+0x64` | confirmed (code) |
| `0x00112d60` | `AudioManager_ClaimStereoFallback` | `0x0011b558` on the music player: the soundtrack preload's fallback | confirmed (code) |
| `0x00112d80` | `AudioManager_AddTrack` | `MusicPlayer_AddTrack` on `+0x64` | confirmed (code) |
| `0x00112da0` | `AudioManager_CfgMusicInfo` | `MusicPlayer_ConfigTrack` on `+0x64` | confirmed (code) |
| `0x00112dc0` | `AudioManager_SetMusicTrack` | `SystemMusic_SetTracks` on `+0x64` | confirmed (code) |
| `0x00112de0` | `AudioManager_LoopMusicTrack` | `Music_Play` looping | confirmed (code) |
| `0x00112e08` | `AudioManager_PlayMusicTrack` | `Music_Play` once with a callback | confirmed (code) |
| `0x00112e30` | `AudioManager_StopMusicTrack` | `0x0010d9a0` on `+0x64` | confirmed (code) |
| `0x00112e50` | `AudioManager_StopAllMusic` | `Music_StopAll` on `+0x64` | confirmed (code) |
| `0x00112e70` | `AudioManager_UpdateMusic` | `Music_Update` on `+0x64` | confirmed (code) |
| `0x00112e90` | `AudioManager_GetMusicVolume` | `+0xc0` (music player `+0x5c`) | confirmed (code) |
| `0x00112e98` | `AudioManager_SetMusicVolume` | `Music_SetVolume` on `+0x64` | confirmed (code) |
| `0x00112eb8` | `AudioManager_GetOptionsMusicVolume` | `+0xc4` (music player `+0x60`) | confirmed (code) |
| `0x00112ec0` | `AudioManager_SetOptionsMusicVolume` | `Music_SetOptionsVolume` on `+0x64` | confirmed (code) |
| `0x00112ee0` | `SoundTask_RandomVolume` | for priority-21 sounds a random factor `1 ± Random_Int(20) / 100` (the sign from a second draw); else 1 | confirmed (code) |
| `0x00113000` | `AudioManager_SetEffect` | the [reverb](#reverb): device slot `+0xa0` on cores 0 and 1 (IOP command `0x13`: type, depth on both sides, delay, feedback), then `0x00113118` | confirmed (code) |
| `0x001130c8` | `AudioManager_EnableEffects` | keeps the flag at `+0x1c4`; off also turns the effect off at once (device slot `+0xc0`, IOP command `0x17` on both cores) | confirmed (code) |
| `0x00113118` | `AudioManager_SetEffectVolume` | device slot `+0xa8` on both cores (IOP command `0x15`: the effect's return volume, the depth on both sides) | confirmed (code) |
| `0x00113188` | `Listeners_NearestDistance` | the distance from a point to the nearest player camera's listener (players `+0x224` of the game state), and that listener's index; 10,000 when none | confirmed (code) |
| `0x00113258` | `AudioManager_CullQuietStreams` | stops every real, streamed, mono sound of priority 8 or more whose last left and right volumes are both 0.2 or less: run whenever a command line is said, so it finds a stream channel | confirmed (code) |
| `0x00113340` | `Snd_FadeOut` | `SndFadeOut(handle, ms)` → `Sound_FadeOut` | confirmed (code) |
| `0x00113370` | `Audio_SetPitchMod` | `SndSetPitchMod`: the global pitch factor `+0x3fa5c` | confirmed (code) |
| `0x00113390` | `Audio_EnableMusicDuck` | `SndEnableMusicDuck`: `+0x3fab4` | confirmed (code) |
| `0x001133b0` | `Audio_SetNIDuck` | `SndSetNIDuck`: `+0x3faac` | confirmed (code) |
| `0x001133d0` | `Sound_LoadBank` | `SndLoadBank`: loads at once, or while `+0x3fa58` is set keeps the name pending | confirmed (code) |
| `0x00113438` | `Snd_CfgMusicInfo` | `SndCfgMusicInfo`: clamps the volume to 0-1, then `Sound_CfgMusicInfo` | confirmed (code) |
| `0x00113490` | `Snd_LoadMatrix` | `SndLoadMatrix` → `Sound_LoadMatrix` | confirmed (code) |
| `0x001134b8` | `Snd_SetMusicVolume` | `SoundSetMusicVolume` → `Sound_SetMusicVolume` | confirmed (code) |
| `0x001134e0` | `Snd_PlayMusicTrackHash` | `SoundPlayMusicTrack` by hash, fade bars 1 | confirmed (code) |
| `0x00113510` | `Snd_PlayMusicTrack` | `SoundPlayMusicTrack` by name, fade bars 1 | confirmed (code) |
| `0x00113560` | `Snd_LoopMusicTrackHash` | `SoundLoopMusicTrack` by hash | confirmed (code) |
| `0x00113590` | `Snd_LoopMusicTrack` | `SoundLoopMusicTrack` by name | confirmed (code) |
| `0x001135e0` | `Snd_StopMusicTrack` | `SoundStopMusicTrack` | confirmed (code) |
| `0x00113608` | `Snd_PlayAmbientTrack` | `SoundPlayAmbientTrack` → `AmbientTrack_PlayByName` | confirmed (code) |
| `0x00113630` | `Snd_StopAmbientTrack` | `SoundStopAmbientTrack` → `AmbientTrack_Stop` | confirmed (code) |
| `0x00113658` | `Snd_SetAmbientTrackVolume` | `SetAmbientTrackVolume` → `AmbientTrack_SetVolume` | confirmed (code) |
| `0x00113680` | `Audio_PlaySoundAt` | `SoundPlay(name, pos)`: `PlaySound3DByHash` at volume and pitch 1, duckable | confirmed (code) |
| `0x00113700` | `Audio_PreloadSoundAt` | `SoundPreLoad(name, pos)`: `Sound_PrepareAtCopy` with `0x00512c14` set for the call, which spares the sound the load screen's virtual rule | confirmed (code) |
| `0x00113780` | `Audio_StartPreloadedSound` | `SoundStart(handle)` → `Sound_StartPrepared` | confirmed (code) |
| `0x001137a8` | `Snd_Play2D` | `SoundPlay2D(name)`: `PlaySound2DByHash` with flags 0, 0 and duckable | confirmed (code) |
| `0x001137e8` | `Snd_Stop` | `SoundStop(handle)` → `Sound_Stop` | confirmed (code) |
| `0x00113810` | `Snd_AddAmbientSound` | `AddAmbientSound(slot, name)` → `Ambient_SetTableSlot` | confirmed (code) |
| `0x00113840` | `Sound_AddAmbientEmitter` | `AddAmbientSoundEmitter`: the sound name's hash (0 for none) → `Ambient_AddParticleEmitter` | confirmed (code) |
| `0x00113920` | `Snd_AddAmbientSoundEmitter2` | `AddAmbientSoundEmitter2`: the sound name's hash (0 for none) → `Ambient_AddEmitter` | confirmed (code) |
| `0x00113a08` | `AmbientSound_AddStatResponse` | → `Ambient_AddSphereEmitter` ([effects bindings](../references/bindings/effects.md)) | confirmed (code) |
| `0x00113a58` | `Snd_SetAmbientEmitterPositions` | `SetAmbientEmitterPositions(emitter, p1-p5, n)`: up to 5 points (w = 1) → `Ambient_SetEmitterPoints` | confirmed (code) |
| `0x00113b28` | `Snd_AddAmbientEmitter` | `AddAmbientEmitter`: a particle emitter (range -1) → `Ambient_AddParticleEmitter` | confirmed (code) |
| `0x00113b78` | `AmbientSound_AddOneOff` | → `Ambient_AddParticleEmitter` with one sound, played once | confirmed (code) |
| `0x00113bc8` | `Audio_EnableAmbientEmitter` | `EnableAmbientEmitter` → `AmbientManager_EnableEmitter` | confirmed (code) |
| `0x00113c00` | `Sound_SetAmbientEmitterVolume` | `SetAmbientEmitterVolumeMod` → `AmbientEmitter_SetVolume` | confirmed (code) |
| `0x00113c30` | `Snd_PlayAmbientEmitter` | `PlayAmbientEmitter(emitter)` → `Ambient_TryPlay` | confirmed (code) |
| `0x00113c58` | `Snd_NewMaterialSlots` | `NewMaterialSlots` → `Sound_NewMaterialSlots` | confirmed (code) |
| `0x00113c98` | `Snd_NewMaterialSound` | `NewMaterialSound` → `Sound_NewMaterialSound` | confirmed (code) |
| `0x00113ce8` | `Snd_DuplicateSoundMaterials` | `DuplicateSoundMaterials` → `Sound_DuplicateSoundMaterials` | confirmed (code) |
| `0x00113d18` | `Snd_SetNumberOfMaterialSlots` | `SetNumberOfMaterialSlots` → `Sound_SetNumberOfMaterialSlots` | confirmed (code) |
| `0x00113d50` | `Snd_NewAnimSlots` | `NewAnimSlots` → `Sound_NewAnimSlots` | confirmed (code) |
| `0x00113d88` | `Snd_NewAnimSound` | `NewAnimSound` → `Sound_NewAnimSound` | confirmed (code) |
| `0x00113dd0` | `Snd_AllocateCharacterVoices` | `SndAllocateCharacterVoices` → `Sound_AllocateCharacterVoices` | confirmed (code) |
| `0x00113df8` | `Snd_SetCommandSoundPercent` | `SndSetCommandSoundPercent` → `Sound_SetCommandSoundPercent` | confirmed (code) |
| `0x00113e30` | `Snd_DisableCombatMusic` | `SoundDisableCombatMusic`: the system music off (`GameState_SetSystemMusic(0)`) | confirmed (code) |
| `0x00113e60` | `Sound_SetSystemMusicState` | `SoundSetSystemMusicState(mood)` → `GameState_SetMusicMood` | confirmed (code) |
| `0x00113ea8` | `Sound_EnableSystemMusic` | `SoundEnableSystemMusic` → `GameState_SetSystemMusic` | confirmed (code) |
| `0x00113ed0` | `Snd_SetMusicTrack` | `SoundSetMusicTrack(mood, a, b, c)`: the non-empty names' hashes → `Sound_SetMusicTrack`, then the system music on and the music player's `+0x68` (manager `+0xcc`) set | confirmed (code) |
| `0x00113ff0` | `Snd_SetMusicStateCallback` | `SoundSetMusicStateCallback` → `GameState_SetMusicStateCallback` | confirmed (code) |
| `0x00114018` | `Snd_SetListener` | `SndSetListener(n)`: the audio manager's `+0x60` | confirmed (code) |
| `0x00114028` | `Snd_SetEffect` | `SoundSetEffect(type, depth, delay, feedback)` → `Sound_SetEffect` | confirmed (code) |
| `0x00114060` | `Snd_EnableEffects` | `SoundEnableEffects(on)` → `Sound_EnableEffects` | confirmed (code) |
| `0x00114088` | `Audio_PauseSound` | `SoundPauseSound(on)` → `AudioManager_Pause` / `_Resume` | confirmed (code) |
| `0x001140d0` | `Sound_SetSoundVolume` | `SoundSetSoundVolume`: returns at once | confirmed (code) |
| `0x001140d8` | `Sound_PlayHumanCommand` | `SoundPlayCommand` → `Human_SayCommand` | confirmed (code) |
| `0x00114178` | `Snd_PreLoadScene` | `SoundPreLoadScene(name)` → `Scene_PreloadSoundtrack` with its hash | confirmed (code) |
| `0x001141b0` | `Snd_CfgInterfaceSound` | `SoundCfgInterfaceSound` → `Sound_CfgInterfaceSound` | confirmed (code) |
| `0x001141e0` | `SoundMatrix_Construct` | zeroes the three pools' fields | confirmed (code) |
| `0x00114220` | `SoundMatrix_Init` | allocates the matrix pools: 3,000 sound-hash words, 1,200 material records and 150 animation records of 28 bytes; clears both tables, name `sound`, then `0x00116848` | confirmed (code) |
| `0x00114528` | `SoundMatrix_Reset` | returns every pool entry and clears the material and animation tables | confirmed (code) |
| `0x00114628` | `SoundMatrix_Load` | `SndLoadMatrix(name)`: when the name differs from the current one (`+0x23d64`), resets the matrix, keeps the name and runs the script `<name>_preload` | confirmed (code) |
| `0x001146c0` | `SoundMatrix_GetMaterialSounds` | a material pair's next alternative: up to three hashes and the three column volumes; a second material of 0 or 1 uses the caller's default, and an empty entry falls back to it; the alternatives play **in turn** (cursor byte 2, wrapping at byte 0) | confirmed (code) |
| `0x00114800` | `SoundMatrix_GetAnimSounds` | an animation sound's next alternative (cursor byte 1), hashes and volumes | confirmed (code) |
| `0x001148e8` | `VoiceTable_WarnLineHash` | the name of a [gang warning line](#warning-barks), its hash when the sound exists, else 0 | confirmed (code) |
| `0x00114a38` | `VoiceTable_LineHash` | the hash of `vags/character/voices/<set>/<command>_<NN>` when it exists and is not blocked (`VoiceTable_IsBlocked`), else 0 | confirmed (code) |
| `0x00114b00` | `VoiceTable_NextLineFwd` | forwards to `VoiceTable_NextLine` | confirmed (code) |
| `0x00115bb0` | `VoiceTable_SetCommandPercent` | `SndSetCommandSoundPercent(set, command, percent)`: byte 2 of the entry; set -1 for every set | confirmed (code) |
| `0x00115c30` | `SoundMatrix_ClearMaterials` | clears the 191 × 191 material table | confirmed (code) |
| `0x00115c80` | `SoundMatrix_ClearAnims` | clears the 162-entry animation table | confirmed (code) |
| `0x00115cb0` | `SoundMatrix_NewMaterialSlots` | `NewMaterialSlots(m1, m2, count, columns, v1, v2, v3)`: takes a record, `count` hash words per column (1-3), zeroes them, stores the three volumes; the last three floats are not read | confirmed (code) |
| `0x00115ee8` | `SoundMatrix_NewMaterialSound` | `NewMaterialSound(i, m1, m2, s1, s2, s3)`: sets alternative `i`'s columns to the names' hashes (`none` stores 0, nil leaves it) | confirmed (code) |
| `0x00116080` | `SoundMatrix_NewAnimSlots` | `NewAnimSlots(event, count, columns, v1, v2, v3)`: as the material version, for the animation table (cursor byte 1) | confirmed (code) |
| `0x001162a0` | `SoundMatrix_NewAnimSound` | `NewAnimSound(i, event, s1, s2, s3)` | confirmed (code) |
| `0x00116438` | `SoundMatrix_DuplicateMaterials` | `DuplicateSoundMaterials(a, b)`: entry `[b][a]` = entry `[a][b]` (the pair works both ways) | confirmed (code) |
| `0x00116480` | `SoundMatrix_SetMaterialCount` | `SetNumberOfMaterialSlots(m1, m2, n)`: alternatives used, cursor reset | confirmed (code) |
| `0x001167b8` | `SoundMatrix_SetCue` | `SoundCfgInterfaceSound(n, name)`: cue `n`'s hash (`+0x23cc4`) | confirmed (code) |
| `0x00116828` | `SoundMatrix_GetCue` | cue `n`'s hash | confirmed (code) |
| `0x00116848` | `DjLines_Init` | fills the three tables of the DJ's [failure lines](#dj-failure-lines) | confirmed (code) |
| `0x00116c98` | `DjLines_SetCheckpointEntry` | one entry of the per-checkpoint table (level, checkpoint, count, kind), cursor 1 | confirmed (code) |
| `0x00116cd8` | `DjLines_SetLevelEntry` | one entry of the per-level table (level, count, kind) | confirmed (code) |
| `0x00116d10` | `DjLines_SetGenericEntry` | one entry of the generic table (count, kind) | confirmed (code) |
| `0x00116d48` | `DjLines_CheckpointLine` | the hash of `dj_l<level>_c<cp>_<kind>_<NN>` for a matching entry, NN its cursor, which then moves on (1 to count, in turn); 0 when none matches | confirmed (code) |
| `0x00116ec0` | `DjLines_LevelLine` | the same for `dj_l<level>_<kind>_<NN>` | confirmed (code) |
| `0x00116fe8` | `DjLines_GenericLine` | `dj_<kind>_<NN>` with NN random in 1-count (`0x003353f0`) | confirmed (code) |
| `0x001170e8` | `DjLines_PickFailureLine` | the failure line for (level, checkpoint, kind), in the order of [DJ failure lines](#dj-failure-lines) | confirmed (code) |
| `0x00117190` | `Sound_PlayAtDefault` | `PlaySound3DByHash` at a copy of a position, volume and pitch 1, duckable (seven object-behaviour callers) | confirmed (code) |
| `0x001171d8` | `Sound_StopFwd` | `Sound_Stop` on the global manager; returns 1 | confirmed (code) |
| `0x00117208` | `Sound_SetPositionFwd` | `Sound_SetPosition` with a copy of the position | confirmed (code) |
| `0x00117238` | `Sound_PlayAtDefault2` | the same as `Sound_PlayAtDefault` (17 object-behaviour callers) | confirmed (code) |
| `0x00117280` | `Sound_PlayMaterialPairAt` | `Sound_PlayMaterialPair(1.0, a, b, pos, default 5)`: glass and other objects | confirmed (code) |
| `0x001172c8` | `Sound_AddScriptEmitter` | adds an emitter for an object behaviour: kind 0 with the hash of `vags/ambient/alarms/alarmbell_loop` (`0xfae63afd`) is an `alarm_emitter`, kind 1 a `script_emitter`, else none; range -1, one sound | confirmed (code) |
| `0x001173e8` | `Sound_DisableEmitter` | switches an emitter off (`AmbientManager_EnableEmitter(…, 0)`) | confirmed (code) |
| `0x00117420` | `Sound_SetEmitterEnabled` | `Ambient_SetEmitterEnabled` on the global manager (object behaviours) | confirmed (code) |
| `0x00117450` | `PanTables_Build` | fills the pan-mode-1 gain tables at reset: left `0x005d8600` and right `0x005d8ba4`, 361 entries each, `left[a] = cos(45° + a/2)`, `right[a] = sin(45° + a/2)` for a = 0-360 (the two at a = 90 / 270 stored as 0) | confirmed (code) |
| `0x00119f78` | `MusicChannel_Init` | a channel's defaults: inactive, track -1, pair byte `0xff`, loop 1, fade bars 1, state idle, fade length 2,000 ms, no callback | confirmed (code) |
| `0x00119fc0` | `MusicChannel_Copy` | copies a request into a channel (track, pair, loop, fade bars, callback), state idle | confirmed (code) |
| `0x0011a038` | `MusicChannel_Set` | fills a request: active, track index, loop, fade bars, callback name (32 chars) | confirmed (code) |
| `0x0011a0a8` | `SoundTask_Init` | a fresh task: a new handle (`SoundHandle_Make` at the first free slot), defaults (fade level 1, fade length 5,000 ms, volumes and pitch factors 1, duckable, `a8` 1) | confirmed (code) |
| `0x0011a150` | `SoundTask_ReleaseHandle` | frees the task's handle slot | confirmed (code) |
| `0x0011b108` | `SoundTask_GetRecord` | copies the task's record pointer | confirmed (code) |
| `0x0011b128` | `SoundTask_StartVoice` | `+0x9c` = start; device slot `+0x90` (set up the voice, `0x0014caf8`), and when not starting, slot `+0x70` (prime the stream) | confirmed (code) |
| `0x0011b1b0` | `SoundTask_SetFade` | fade mode 1 (in): level 0 from now; mode 2 (out): from the current level when fading in (start time moved so the level carries on), else from 1; other modes stored as given | confirmed (code) |
| `0x0011b298` | `SoundTask_Start` | starts a prepared voice (device slot `+0x78`), `+0x9c` = 1; true when the device accepted it | confirmed (code) |
| `0x0011b2f0` | `SoundTask_ReleaseStream` | `+0xe4` = flag, then device slot `+0xd0` | confirmed (code) |
| `0x0011b330` | `SoundTask_SetVirtual` | `+0x4c` virtual, `+0x44` length, `+0x48` start = now | confirmed (code) |
| `0x0011b350` | `SoundTask_VirtualDone` | a virtual task has ended: never for a loop; else when its length has passed or it is stopped (state 3) | confirmed (code) |
| `0x0011b3c0` | `SoundTask_IsPrimed` | device slot `+0x80` | confirmed (code) |
| `0x0011b3f8` | `SoundTask_SetOwnerFlag` | task `+0xec` (the owner's human `+0x16c`) | confirmed (code) |
| `0x0011b400` | `StereoSlots_Init` | both [stereo slot](#stream-pairs) bytes free, pending hash 0, soundtrack handle -1 | confirmed (code) |
| `0x0011b450` | `StereoSlots_Claim` | the preload's claim: refused (-1) when either slot holds a stereo sound and the claim is for one; slot 0 if free, else 1 if free, else -1 | confirmed (code) |
| `0x0011b498` | `StereoSlots_Take` | making a task or a music channel: slot 0 if free, else 1; both taken: -1 while a started soundtrack lives or a cinematic runs, else a slot holding a stereo sound is taken over (`0x0011b6c0`) | confirmed (code) |
| `0x0011b558` | `StereoSlots_Fallback` | when the claim fails: a live soundtrack is stopped and its slots freed (true); otherwise the hash becomes pending (false) | confirmed (code) |
| `0x0011b5e8` | `StereoSlots_FreeDead` | frees every slot marked stereo when the task in `+0x08` is dead | confirmed (code) |
| `0x0011b660` | `StereoSlots_FreeStereo` | frees every slot marked stereo | confirmed (code) |
| `0x0011b6c0` | `StereoSlots_Evict` | the current soundtrack's hash becomes pending and its task is stopped | confirmed (code) |
| `0x0011b740` | `StereoSlots_Set` | sets slot `i`'s user byte | confirmed (code) |
| `0x0011b750` | `StereoSlots_PairOf` | stream pair 1 for slot 0, else 3 | confirmed (code) |
| `0x0011b760` | `StereoSlots_BothMusic` | both slot bytes are 1 (music) | confirmed (code) |

### Game-state functions {#warriors-functions}

Functions of the game-state module (`0x00417af0`-`0x00424e50`: inventory, statistics, flags, configuration
workers) that belong to this page, by address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00419030` / `0x00419108` | `Breathing_Start` / `Breathing_Stop` | starts the 2D loop (handle `+0x234`) at volume 0 rising to 1.0, or turns a fading one round; stops it and clears the state `+0x248` | confirmed (code) |
| `0x00419150` | `Breathing_Update` | each frame (from `0x0041a370`): while the camera frames combat (`Camera_IsCombatFraming`) the loop fades in over `+0x250` ms; once it does not, it fades out over `+0x254` ms and stops at 0 | confirmed (code) |
| `0x004193e8` | `Breathing_Configure` | `CfgBreathingSound`'s worker: game state `+0x258` the sound hash, `+0x250` fade-in ms, `+0x254` fade-out ms | confirmed (code) |
| `0x00419fc8` | `GameState_SetMusicMood` | the system music's mood `+0x40c`; mood 4 switches the system music off and stores 3 | confirmed (code) |
| `0x0041a060` | `SystemMusic_UpdateMood` | every 30th update, and from `GameState_SetSystemMusic` and `Gang_SetAlertState`: with system music off (`+0x3f8` 0) mood 4; otherwise, unless locked (`+0x3f4`) outside mood 3, picks a mood from player 1's gang and the nearest other gang: 1 (combat) when one of that gang's members fights ours, or when it is farther than both radii + 30 m but at least `CfgGangSizeForCombatMusic` strong (`0x005148a8`) and in sight of a player; 2 when our gang is hunted (`Gang_IsBeingHunted`); else 0 | confirmed (code); the reading of each test inferred |
| `0x0041b4c0` | `GameState_SetMusicStateCallback` | the name at `+0x384`; from the set-up and `Snd_SetMusicStateCallback` | confirmed (code) |
| `0x0041d480` | `Cfg_SetBreathingSound` | `CfgBreathingSound(v, in, out, name)`: the sound's hash and fades to `Breathing_Configure`, `v` to `+0x24c` | confirmed (code) |
| `0x0041d920` / `0x0041d930` | `Cfg_SetGangSizeForCombatMusic` / `Cfg_SetDisableMusicForScenes` | `CfgGangSizeForCombatMusic` (`0x005148a8`, read by `SystemMusic_UpdateMood`) / `CfgDisableMusicForScenes` (`0x005148ac`) | confirmed (code) |

## Coney's implementation

`src/audio/`, from this page and [Audio data](formats/audio.md):

- `SoundEngine` (`sound_engine.cpp`): the audio manager: 256 tasks, the admission, voice and stealing rules, virtual
  plays and their lengths, `SoundTask_Update`'s fades, distance attenuation, two-ear pan, ducking and rate; banks (decoded
  to PCM at load), the load screen's banks and halves, the ambient bed, interface cues and scene soundtracks as
  [above](#scene-sound): stereo sounds on pairs 1+2 and 3+4, shared with the music through its channels' slots (a
  music channel at index 0 or 1 holds slot 0 or 1), the claim, the pending soundtrack prepared by a later update,
  the take-overs both ways, event 13 starting whatever is prepared, the cinematic's start waiting for it
  (`sceneSoundReady()`, stopping the music when it holds both pairs), and, while a cinematic runs, the music ducked
  and duckable directional sounds of non-players at 0.2. A task whose voice the mixer no longer plays ends at the
  next update, whatever its stream, and a movie stops the music and every task through the engine before Bink's
  sound starts ([Movies](movies.md#coneys-implementation)).
- `MusicPlayer` (`music_player.cpp`): the three channels and their states, bar-synchronised starts and cross-fades,
  the volumes and the system music's moods.
- `StreamFeeder` (`sound_stream.cpp`): decodes `BFW.SND` and `MUSIC.SND` streams a little each step, mono or
  block-interleaved stereo, keeping each stream two seconds ahead of its voice: the game thread fills them once a
  frame, and a stalled frame (0.2-1.1 s on a busy machine in real-time play) emptied the old half-second ring, which
  cut streamed speech and ambient loops short.
- `Mixer`: the game's voices as the device takes them: a 15-bit level per side and the SPU2 pitch word, mixed at
  48 kHz; SDL3's device or, in test mode, offline ([Building](../guides/building.md#sound)).
- `SoundPlayer`: what game code calls (play by name hash, 2D or at a position, stop, pause); `engine()` for banks,
  music, the load screen and scenes. Main gives it the engine when a disc is given.
- `ObjectSounds` (`repo:src/audio/object_sounds.h`): the glass panes', doors' and barriers' sounds: a name hash as a
  positional sound at the object through `SoundPlayer`, a material pair from the sound matrix
  ([World objects](objects.md#coneys-implementation)).
- `SoundMatrix` (`repo:src/audio/sound_matrix.h`) and `MaterialSoundPlayer` (`repo:src/audio/material_sounds.h`): the
  [sound matrix](#sound-matrix) as the preloads' `NewMaterialSlots`, `NewMaterialSound`, `SetNumberOfMaterialSlots`,
  `DuplicateSoundMaterials`, `NewAnimSlots` and `NewAnimSound` fill it (kept by `GameSound`; `SndLoadMatrix` empties it
  for a new name), its lookups (alternatives in turn, the default material's fallback) and its players (a pair's
  columns 2 then 1, a hit's column 1, an animation sound's column 1, the footstep remap) into the engine. A level
  started directly (`--play-level`) gives its preloads the sound before they run, as the story does. **Coney's
  stand-in**: `CAR_HOOD`'s varied pitch is not applied (`0x003354e0` is not on this page).
- `GameSound` (`game_sound.cpp`): the game's sound as the rest of the game drives it. The sound bindings
  (`repo:src/scripting/sound_bindings.cpp`: the preloads' configuration, the ambience, the music, `SndSetListener`,
  `HuSpeak`, `HuSpeakNI`, `HuShutUp`, `SoundPlayCommand`) reach it through the binding context; the front end's
  bank, music and cues through `FrontEndAudio`; gameplay (mode 1) tells it of its enter (the defaults of
  [Entering gameplay](level-loading.md#mode-1)), the load screen's start and end, and its exit (every sound and the
  music stopped, the level's emitters and lines forgotten). Each frame it puts the listener at player 1's camera
  (`SndSetListener(1)`: at player 1, 1.8 m above his feet), runs the ambient emitters and moves each line to its
  speaker; when a line ends it calls the line's script callback.
- **The humans' sounds** (`HumanSoundEvents`, `repo:src/audio/human_sound_events.h`, from
  [Sound events](sound-events.md)): a human keeps the type-11 clip events his animation passes (the newest task's
  clip, a gait blend's leading clip, none from a task with flag `0x10`) and the hits he takes in an outbox
  (`repo:src/human/human_sounds.h`); gameplay hands each scripted human's to `GameSound` every step with what the
  sounds read of him (player or not, the ground under him, his heading, his target). `HumanSoundEvents` plays
  `Human_OnAnimSoundEvent`'s table: footsteps and body falls on the remapped ground, the fixed kick pairs, the cloth
  and swooshes, the fence rattles, the spray loop, the vocal ids as his lines (a woman's from her entries) and the
  speech-command ids through `Speech`; and `Human_PlayImpactSound` for a landed strike, the striking material from
  the limb and strength (`Hit_ResolveBlock`'s tables), the struck one `DEAD` on the ground, `BLOCK` when blocking or
  ducking, `HEAD` for a boss or a high hit, else `TORSO`; a charging attacker's strike adds `HUMAN` against
  `HUMAN`. A human's strike shapes on the level (`FIST` against the triangle met, at 116, once while the shapes stay
  on, unless his bits `0x400800` or anims 2, 4 and `0x1b2` keep it quiet) and on a pane or door (against its type's
  material), and player 1's object attack on a car (`CAR_HOOD` at 0.5 when a part was reached), sound as his, `HUMAN`
  instead of `FIST` while he charges ([Sound events](sound-events.md#strike-object)). The SA id is the event's
  32-bit word at `+8`. A scene's role clip sends its event 11 the same way: the human's animation sound at the place
  the scene holds him and on the ground there (a ray from 1 m above his feet, 1.5 m down; **Coney's reading**, as
  the scene poses him without moving his body); a stand-in the stage draws has no body and stays silent. Player 1's
  combat framing is the follow camera's lock-on test (L1 held at his fight target, not in a grab), so his sounds add
  column 3 under it, and the **angry breathing** (`CfgBreathingSound`'s sound, a 2D loop) fades in over the first
  time while it holds and out over the second after, stopping at 0 (linearly on the engine's clock: **Coney's
  choice**). A disc test (`[disc][story][audio]`) plays STORY into level99's lessons 1-6 and checks the hints' cue,
  the `l99_t1` humans' shuffles and the lock-on's breathing are asked for, as a differential run against the PS2
  found them missing.
  **Coney's stand-ins**: the striking limb comes from the strike clip's name (`kick`, `stomp`, `knee` a foot;
  `headbutt` the head; else a hand), not the striking shape's bone; nobody is hidden in shadow, burning or holding a
  world object, and only the scripts' humans report sounds; there are no punch bags (`BAG`); a strike shape meets the
  level when its segment, as a ray, crosses a level triangle, without the turn limits and state `0x4000000`; a
  pane sounds as `GLASS`; a thrown human against the level (`Human_OnContact`) does not sound yet.
- `VoiceTable` (`voice_table.cpp`, [The voice table](#voice-table)) and `Speech` (`speech.cpp`,
  [Saying a speech command](#speech)): one line per human at a time, positional and directional at him, cut off by
  an interrupting one.
- `AmbientEmitters` (`ambient_emitters.cpp`, [Ambient emitters](#ambient)): both binding forms (the older one named
  `particle task`; the same name, sound and first point is the existing emitter, switched on), modes 1-5, the range
  from `pos1`, the plays, the delays, the points, the volume, the name kinds, the filter on player 1's covered
  ground (his last ground snap's triangle flag `0x20`), the ambient factor and the `music` duck, once a second and
  at most two new sounds an update. **Coney's stand-ins**: one listener (the game's) and one player; the fight
  timer is on while the music's mood is the fight; the `_DAM_` window opens 2 s after a strike on a world object
  or a car (each reports an AI noise, `AI_ReportNoise`, which Coney reads as always stamping the event; a stamp
  already set puts the new one 2 s back), and the stamp is forgotten when gameplay is left; mode 7 (no script makes
  one) and the `0x005147c8` block are not modelled.
- **System music** (`repo:src/gamemodes/system_music.cpp`, [Music](#music)): on a mood change a random track of
  the mood loops through the music player, cross-faded on the bar as `0x0010e7d0` does (a cut into the fight, 4
  bars back to calm from the fight or the hunt, else 2); `SoundSetSystemMusicState` holds a mood (3 hands it back,
  4 turns the music off) and `SoundEnableSystemMusic` ends a hold. **Coney's stand-ins**: the game's mood is the
  fight while an AI targeting player 1 has a fight or melee goal, else calm (no hunted mood, no gang-size or sight
  test); the mood is judged every frame, not every 30th update; the scripts' mood callback is not called.
- **Radios** (`world_objects::Radios`, `repo:src/world_objects/radios.h`; [Radios](#radios)): `SetupRadio` pins the
  object and makes it a radio; gameplay updates each radio every frame through `GameSound::play3D` (a positional
  sound at the radio, half volume during a scene), with player 1's place, the game's random draws and the levels'
  completion, running the states, tables and DJ link picks above and calling `onSegment`. **Coney's stand-ins**: a
  sound stopped because the player went beyond 40 m starts its track again when he comes back; the DJ links of kinds
  0 and 1 have no names, so they play nothing and end at once; `Radio_SetMode`'s modes 4 and up, the pick-up
  (`onPickUp`), the smash and the R1 retune are not built (Coney has no carried boom box).

Coney's stand-ins where this page is open, each marked in the code: the directional table is 1 (as loud behind as in
front); the `+0x268` state factors, the `+0x5b7` owner duck and level 82's ambient swap are not applied; a stopped task
is freed at once (the game keeps one whose `+0xe4` is set), so a preload after a live soundtrack always takes the
pending path; stopping the soundtrack also forgets one pending; the load screen plays only positional sounds virtually
(not yet the other 2D sounds that are not stereo); the soundtrack's pending re-preload runs on every update, as the
game's music update does; the random factors come from the engine's own seeded source (the game's is a separate audio
generator, `0x006eb8b0`, seeded from the timer at start-up, `0x0010f768`). The speech and ambience stand-ins: an emitter
plays one sound at a time, waiting a random whole number of seconds in its two delays before the first and after each
one ends (level99's pairs, 1-3 to 10-30, read as seconds), from a random point of its line or a random one of its
positions; its range, `plays`, `mode`, `filter` and the name kinds are not read; a line follows its speaker; a line
stopped by `HuShutUp` or cut off drops its callback; `HuShutUp` always stops (the human's `+0x194` is not modelled);
`HuSpeak`'s fifth argument is not read, and neither speaker turns to a look-at target; a human's voice set is his type's
own `CfgChar` voice (no alias rule, no `HuSetVoiceIndex`); the fixed list of blocked lines is not applied;
`SndSetListener` 0 is the camera and 1 the player (inferred); the bank deferral of mode 1's enter ends with the load
screen. The system music (`repo:src/gamemodes/system_music.h`) keeps each mood's track hashes and, while on, loops a
random track of the mood each frame's surroundings give when the mood changes; the mood is 1 while an AI human with
health left targets player 1 with a fight or melee goal, else 0 (the hunted mood 2, its chase goals not built, never
comes), the pick draws from the game's random index, and the fades are the music player's own. `SoundSetEffect` and
`SoundEnableEffects` are kept in the game state only. Not built yet: reverb, the other ambient bindings
(`AddAmbientSoundEmitter`, `SetAmbientEmitterVolumeMod`) and the game's own speech commands outside the animation
sounds. The hub's sound bindings
(`repo:src/scripting/hub_world_bindings.cpp`): `EnableAmbientEmitter` switches an emitter off (it stops its sound and
plays nothing more) and on; `SoundPlay` plays a sound once at a point; `SndLoadMatrix` empties the sound matrix and
runs `<name>_preload.lua` when the name changes; `HuSay` speaks a line with no callback.

## Open questions

- What game state `+0x268` is (it scales sounds of others, music and pitch).
- Who sets the system-music mood (game state `+0x40c`).
- The listener vector's meaning and which object each listener is (`SndSetListener`'s values).
- What `a8` (the play call's third volume factor, `+0xa8`) is used for by each caller.
- How long a primed but unstarted soundtrack survives: `Tasks_Update` frees a real task when the device reports its
  voice idle (`0x0014d168`), and whether a primed stream reads as idle was not seen (the trace's waits were under
  0.5 s). Which callers pass the play call's tenth argument (task `+0xe4`) and what the IOP does with it.
- Whether a scene soundtrack holds the dialogue alone or a full mix, and what the 20 unused stereo sounds are.
- The IOP side (`IOP.IRX`): the exact SPU2 voice assignment of streams and its mixing.
- What the AI's ambient event stamp (`0x002936a8`) marks.
- Who ends the bank deferral (`+0x3fa58`).
- The blocked lines of `0x00114c98`: a hand-written list for levels 11, 20, 31, 34, 82, 83, 84, 92 and 95 (some
  by checkpoint, game state `+0x33a`), each naming voice sets, commands and 1-based lines; not yet tabulated.
- The gang counter `+0x60d`'s starting value (the first bark's `NN`) and the meaning of game state `+0x56fc`.
