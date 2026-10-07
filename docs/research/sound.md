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
`Device/ps2/sound/msaudiodevice.cpp` (`0x0014b158`-`0x0014d508`) is the platform device over Sony's MultiStream library
(`0x0014d528`-`0x00151ed0`, [Audio data](formats/audio.md)).
Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0050aa84` | `g_AudioManager` | pointer to the audio manager (`0x00598aa0` at runtime) | confirmed (code), confirmed (runtime) |
| `0x0050bce4` | `g_AudioDevice` | the MultiStream device, vtable `0x00537f88` | confirmed (code) |
| `0x00143f68` | `Crc32(table, name)` | CRC-32 of a name as written (no case folding), table at `0x005d91e0` | confirmed (code) |
| `0x0010f618` | `AudioManager_Reset` | defaults (below); bank `none` | confirmed (code) |
| `0x0010f810` | `AudioManager_Update(listeners)` | the tasks, the ambient emitters, the music, the device | confirmed (code) |
| `0x00111de8` | `AudioManager_Play(vol, pitch, a8, mgr, owner, hash, pos, fade, p9, p10, duckable)` | start a task | confirmed (code) |
| `0x001120c8` | `AudioManager_NewTask(hash, pos)` | admission, task, voice | confirmed (code) |
| `0x00112560` / `0x00112700` | `Task_GetVoice` / `Task_FindVictim` | voice allocation and stealing | confirmed (code) |
| `0x0011a170` | `Task_Update(task, listeners)` | fades, 3D volume and pan, pitch | confirmed (code) |
| `0x00112b10` | `Tasks_Update` | per update: run each task, free the finished | confirmed (code) |
| `0x0010fcd8` / `0x0010fc70` | `PlaySound2D(hash or name, flags...)` | a sound without position (`SoundPlay2D`, the interface cues) | confirmed (code) |
| `0x0010fdd0` / `0x0010fd48` | `PlaySound3D(hash or name, pos...)` | a sound at a position (`SoundPlay`) | confirmed (code) |
| `0x0010fa50` | `AudioManager_LoadBank(name, partial)` | loads a bank unless it is the current one | confirmed (code) |
| `0x00111178` | `AudioManager_StartLoadScreen` | the load-screen bank and its two sounds | confirmed (code) |
| `0x00110b60` / `0x00110c70` | `AmbientTrack_Play` / `_Stop` | the level's ambience bed | confirmed (code) |
| `0x0010d8e8` | `Music_Play(hash, loop, callback, fadeBars)` | queue a music track | confirmed (code) |
| `0x0010dfe0` | `MusicChannel_Update` | the music state machine | confirmed (code) |
| `0x0010e7d0` | `SystemMusic_Update` | mood-driven track choice | confirmed (code) |
| `0x0010e558` | `Music_UpdateVolumes` | music volume, ducking | confirmed (code) |
| `0x001164a8` | `VoiceTable_Build(n)` | allocates `n` voice sets and counts each one's lines per speech command | confirmed (code) |
| `0x00114b20` | `VoiceTable_NextLine(set, command)` | the next line's sound for a voice set and command, or none | confirmed (code) |
| `0x00114c98` | `VoiceTable_IsBlocked` | a fixed list of lines not played in certain levels | confirmed (code) |
| `0x002205e0` | `Human_SayCommand` | a human says a speech command (the work of `SoundPlayCommand`) | confirmed (code) |
| `0x0021e400` / `0x0021e698` | `Human_PlaySpeech` | plays a line at the human with an end callback (the second also cuts a line off) | confirmed (code) |
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
| `+0x1d8`, `+0x1dc` | "a non-duckable sound is playing" flags (set by tasks with `+0xa4` = 0, cleared each update) |
| `+0x1e0` | the sound matrix: material and animation sound tables, the voice table at `+0x34`, interface cues |
| `+0x24268`, `+0x24274` | handles of the music-like stereo bed and of the ambient track |
| `+0x24270` | set: new 3D sounds and stereo sounds play virtually (set during the load screen) |
| `+0x3fa20` | **sound-effect volume**, 0-1 (options; default 0.9, *runtime* 0.9) |
| `+0x3fa24` | paused (`SoundPauseSound`); pitch updates stop |
| `+0x3fa28` | the next load-screen number, 0-6 (random at start-up, `0x0010f768`) |
| `+0x3fa34` | low-priority sounds started this update (reset each update) |
| `+0x3fa38` | the current bank's name (16 chars; *runtime* `sound`); `+0x3fa48` the bank to load after loading (`none`) |
| `+0x3fa58` | defer `SndLoadBank` to the end of loading (set elsewhere; inferred) |
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
| `+0xa4` | duckable (the play call's last argument; 0 also marks the manager's `+0x1d8`) |
| `+0xc0`, `+0xd0` | position and facing (directional sounds) |
| `+0xe8` | the owner's handle (a human: its voice is cut when it speaks again) |

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

## Behaviour

### Starting a sound {#play}

`AudioManager_NewTask` (`0x001120c8`), confirmed (code):

1. Find the record by hash (binary search); none: nothing plays.
2. **Admission.** With more than 100 tasks alive a sound of priority 20 or above is refused; with more than 240 any
   sound is; a sound of priority 21 or above is refused once 5 have started this update.
3. Take a free task, link it at the tail, give it the next id.
4. **3D cull.** A positional sound farther than `far + 10` m from the listener that does not loop plays virtually;
   during the load screen (`+0x24270`) every positional or stereo sound does.
5. **Voice** (`0x00112560`): a streamed stereo sound takes a stream pair; a streamed mono sound the first free stream
   channel of 5-9 (10-12 for class flag `0x20`), or, failing that and with priority below 12, a victim's; a bank
   sample the first free SPU2 voice of 13-47, or a victim's. No voice: virtual.
6. **Length**: `AudioDevice_Duration(size, rate)` in ms, whole seconds only (`0x0014d2d8`); a virtual sound of
   priority 20 or more is given at most 500 ms.

`AudioManager_Play` (`0x00111de8`) then sets the caller's volume, pitch (1 for a non-duckable sound), owner,
position, facing and fade, runs one `Task_Update` and starts the voice (`0x0014caf8`): a bank sample by hash with
its volumes and pitch; a stream by pointing its channel at the record's offset and size in `BFW.SND` (a loop flag the
inverse of class bit 0); a stereo stream as parent and child channels with the [stereo](formats/audio.md#stereo)
interleave and end offset. Priority-21 sounds get a random volume factor of 1 ± 0-19 % (`0x00112ee0`).

**Pitch.** Sent as the SPU2 pitch `rate × 4096 / 48000` (`0x00150078`) of `rate = record rate × variation ×
caller's pitch × +0x70 × global pitch`; the variation is a random factor in `1 ± v / 100` chosen at the start.
Confirmed (code) at `0x0014caf8`, `0x0011a170`.

### Stealing a voice {#voice-stealing}

`Task_FindVictim` (`0x00112700`) walks the live tasks and picks one that is mono, real (not virtual), not stopping,
streams (or not) as the new sound does, has the same class flag `0x20`, and has priority 5 or more (4 or more for
the last one checked); among them the least important (highest priority number), and between equals of priority 8
or more the quietest (mean of its two volumes). It is taken only when its priority number is higher than the new
sound's; it is then stopped and marked state 3. Confirmed (code); the exact tie order is the list order.

### Task update: fades, 3D volume and pan {#three-d}

Each update `Tasks_Update` (`0x00112b10`) stores the listeners, runs `Task_Update` on every real task and frees
those whose voice has finished or whose state is 2 or 3; a virtual task is freed when its length has passed (never,
if it loops). Confirmed (code). `Task_Update` (`0x0011a170`), confirmed (code):

- **Fade**: mode 1 raises `+0x78` from 0 to 1 over the fade length; mode 2 lowers it to 0 and then stops the sound.
  The ambient track fades in and out over 2,000 ms (`0x00110b60`).
- **Distance attenuation**, per listener, with `d` the distance, `near` and `far` the class's:
  `a = 0` beyond `far`, `a = 1` within `near`, else `a = (1 − (d − near) / (far − near))²`.
- **Directional** sounds (class flag `0x10`, the voices): a further factor from a 200-byte table of percentages at
  `0x0050a910`, indexed by `(cos θ + 1) × 100` where θ is between the sound's facing and the direction to the
  listener; not applied within 1 m.
- **Pan** (pan mode 0): two ear points half a metre either side of the listener (along the listener's side axis,
  inferred) give distances `dL` and `dR`; the nearer ear gets gain 1, the farther `0.75 × a + 1 − |dL − dR|`, clamped
  to 0-1. Mode 1 reads left and right gains from tables at `0x005d8600` and `0x005d8ba4` by the angle to the
  listener in degrees (180 + angle).
- **Volume** per ear: `a × ear gain × record volume % × caller's volume × sound volume (+0x3fa20) × directional ×
  fade × a8 × state factor`, then the loudest over the listeners, then × the duck factor (`+0x3faac`) for a
  duckable directional sound not owned by a player while a non-duckable one plays, × 0.25 for a duckable positional
  sound whose owner's flag `+0x5b7` differs from every listener's (meaning not traced), clamped to 1. Sent only
  when changed.
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
nothing in this build. Volumes reach the device as `int(v × 16383) & 0x7fff` per side (`0x0014d188`). Reverb:
`SoundSetEffect` on both SPU2 cores ([bindings](../references/bindings/sound.md#soundseteffect)). Confirmed (code).

**Pause** (`SoundPauseSound`, `0x0010fb20` / `0x0010fb68`): sets `+0x3fa24` and pauses or resumes every voice through
the device (`0x0014c5e0` / `0x0014c600`). Confirmed (code).

### Banks {#banks}

One bank is in sound RAM at a time. `AudioManager_LoadBank(name)` (`0x0010fa50`) loads it unless it is already
current: the device waits for stream 0, streams `<name>.msb` into sound RAM and `<name>.msd` to the IOP
([banks](formats/audio.md#banks)). Confirmed (code). Who loads which:

- **Front end**: `menu` (`0x0015c4b0`, `0x0015d648`); `0x00159630` loads `sound`.
- **Load screen** (`0x00111178`, from level loading `0x0015fe90`): bank `load_NN` (NN = `+0x3fa28`, then +1 mod 7),
  or `armload` when `0x0041d110` says so (the Armies levels, inferred), and its two sounds
  `vags/load_screen/load_NN_l` / `_r` started as 2D sounds hard left and hard right; `0x00111428` stops both after
  the preload ([Level loading](level-loading.md#loading-screen)).
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

`SoundStopMusicTrack` fades out or stops each channel by its state (`0x0010d9a0`). The music volume
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
- **Ambient table and emitters** {#ambient}: `AddAmbientSound(slot, name)` (`global.lua`, 1,077 calls) stores a
  name's hash in slot `slot` of the ambient table (ambient manager `+0x1a014 + 4 × slot`); an emitter
  (`AddAmbientSoundEmitter2(name, p1, p2, slot, sound, count, ...)`) plays, at random intervals, a random sound of
  slots `slot` to `slot + count - 1`, or the one named sound when `slot` is -1
  ([Sound bindings](../references/bindings/sound.md#addambientsoundemitter2)). The emitters run in
  `0x0010c100` from the manager's update. Confirmed (code) for the slot store; the emitters' timing is inferred.
- The `small_loops` sounds (class flag `0x20`) take the stream channels 10-12, so they never steal from effects.

### Radios {#radios}

`SetupRadio` (`Radio_Setup`, `0x003ac4d0`) makes a world object (a boom box) a radio run by `Radio_Update`
(`0x003ad440`, every update interval of the object) and `Radio_HandleMessage` (`0x003ac7e8`). Everything a radio
plays is a **streamed** sound of the sound list (stream flags, `BFW.SND`) started with `PlaySound3D` (`0x0010fdd0`,
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
   traced; the same six ids sit in the manager's table at `+0x3fa60`).
4. The line plays at the human (`0x0021e400`; the war chief's lines use `0x0021e698`, which can also cut a line
   off), unless a scene is playing (game state `+0x410`, read at `0x0021e400`). The **third argument is a callback
   name**: the function is called with the speaker's handle when the line ends, or at once when no line plays. With
   a target, the speaker looks at it for the line's length plus 0.5 s.

Voice lines are streamed, positional and directional (class flags `0x1c` or `0x4c`, priorities 7-11, far 20-50 m).
No lip-sync data was found: the speaking human only turns to its target. Voice lines in play have no subtitles; only
scenes and movies do ([Scenes: subtitles](scenes.md#subtitles)). Scenes play their own soundtrack
([Scene soundtracks](#scene-sound)).

### Speech lines by name

`HuSpeak` and `HuSpeakNI` take a sound name (`vags/speeches/l31/l31_t7_001`). The `global.lua` helper `SetVag(name)`
builds one from a short name: `l11_t25_006` stands for `vags/speeches/l11/l11_t25_006`, the folder being the name's
first part. Speech lines are class flags `0x06` (streamed, positional) at priority 4, so only priority-2 and -3
sounds outrank them. 537 of the 566 names the scripts form are in the sound list; every bank sound is in the list too,
so the other 29 (and the 2 missing ambient names) are not on the disc at all (inferred: the scripts name lines
that were cut).

### Scene soundtracks {#scene-sound}

An in-engine scene ([Scenes](scenes.md#events)) plays one **scene soundtrack**: a 32,250 Hz stereo stream (class 224,
[stereo table](formats/audio.md)) as long as the scene (*speculative*: it carries the scene's dialogue). It runs in
two steps, so the stream is buffering before the first frame:

1. **Preload** (`0x0010ff68`, called from scene load `0x00352098` and from `SoundPreLoadScene`, `0x00114178`): the hash
   comes from the first type-13 clip event of a role (`0x00101c58`) or, failing that, of the camera track
   (`0x00354d28`). It stops the previous scene sound, claims a stereo stream pair (`0x0011b450`, falling back to
   `0x00112d60` when none is free) and prepares the sound without starting it (`0x001100e8` → `0x00111f78`), keeping
   its task in the manager at `+0x24268`. *Confirmed (code).*
2. **Start**: scene event 13 (`0x00110018`) starts that prepared task and sets `+0x2426c`. *Confirmed (code).*

Music ducks to 0.75 while it plays ([Music](#music)). Scene events 14 and 71 play a sound by hash at a human's
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

## Coney's implementation

`src/audio/`, from this page and [Audio data](formats/audio.md):

- `SoundEngine` (`sound_engine.cpp`): the audio manager: 256 tasks, the admission, voice and stealing rules, virtual
  plays and their lengths, `Task_Update`'s fades, distance attenuation, two-ear pan, ducking and rate; banks (decoded
  to PCM at load), the load screen's banks and halves, the ambient bed, interface cues and scene soundtracks
  (prepared, then started by scene event 13, ducking the music).
- `MusicPlayer` (`music_player.cpp`): the three channels and their states, bar-synchronised starts and cross-fades,
  the volumes and the system music's moods.
- `StreamFeeder` (`sound_stream.cpp`): decodes `BFW.SND` and `MUSIC.SND` streams a little each step, mono or
  block-interleaved stereo.
- `Mixer`: the game's voices as the device takes them: a 15-bit level per side and the SPU2 pitch word, mixed at
  48 kHz; SDL3's device or, in test mode, offline ([Building](../guides/building.md#sound)).
- `SoundPlayer`: what game code calls (play by name hash, 2D or at a position, stop, pause); `engine()` for banks,
  music, the load screen and scenes. Main gives it the engine when a disc is given.
- `ObjectSounds` (`repo:src/audio/object_sounds.h`): the glass panes' and doors' name-hash sounds, played through
  `SoundPlayer` ([World objects](objects.md#coneys-implementation)).
- `GameSound` (`game_sound.cpp`): the game's sound as the rest of the game drives it. The sound bindings
  (`repo:src/scripting/sound_bindings.cpp`: the preloads' configuration, the ambience, the music, `SndSetListener`,
  `HuSpeak`, `HuSpeakNI`, `HuShutUp`, `SoundPlayCommand`) reach it through the binding context; the front end's
  bank, music and cues through `FrontEndAudio`; gameplay (mode 1) tells it of its enter (the defaults of
  [Entering gameplay](level-loading.md#mode-1)), the load screen's start and end, and its exit (every sound and the
  music stopped, the level's emitters and lines forgotten). Each frame it puts the listener at player 1's camera
  (`SndSetListener(1)`: at player 1, 1.8 m above his feet), runs the ambient emitters and moves each line to its
  speaker; when a line ends it calls the line's script callback.
- `VoiceTable` (`voice_table.cpp`, [The voice table](#voice-table)), `AmbientEmitters` (`ambient_emitters.cpp`,
  [Ambience](#ambience)) and `Speech` (`speech.cpp`, [Saying a speech command](#speech)): one line per human at a
  time, positional and directional at him, cut off by an interrupting one.
- **Radios** (`world_objects::Radios`, `repo:src/world_objects/radios.h`; [Radios](#radios)): `SetupRadio` pins the
  object and makes it a radio; gameplay updates each radio every frame through `GameSound::play3D` (a positional
  sound at the radio, half volume during a scene), with player 1's place, the game's random draws and the levels'
  completion, running the states, tables and DJ link picks above and calling `onSegment`. **Coney's stand-ins**: a
  sound stopped because the player went beyond 40 m starts its track again when he comes back; the DJ links of kinds
  0 and 1 have no names, so they play nothing and end at once; `Radio_SetMode`'s modes 4 and up, the pick-up
  (`onPickUp`), the smash and the R1 retune are not built (Coney has no carried boom box).

Coney's stand-ins where this page is open, each marked in the code: the directional table is 1 (as loud behind as in
front); the `+0x268` state factors, the `+0x5b7` owner duck and level 82's ambient swap are not applied; a stereo
sound effect takes channels 5+6 or 7+8; the random factors come from the engine's own seeded source (the game's shared
one would shift the scripts' draws); `SoundStopMusicTrack` fades a playing track over one bar. The speech and
ambience stand-ins: an emitter plays one sound at a time, waiting a random whole number of seconds in its two delays
before the first and after each one ends (level99's pairs, 1-3 to 10-30, read as seconds), from a random point of its
line or a random one of its positions; its range, `arg8`, `arg11`, mode and the name-based types are not read; a
line follows its speaker; a line stopped by `HuShutUp` or cut off drops its callback; `HuShutUp` always stops (the
human's `+0x194` is not modelled); `HuSpeak`'s fifth argument is not read, and neither speaker turns to a look-at
target; a human's voice set is his type's own `CfgChar` voice (no alias rule, no `HuSetVoiceIndex`); the fixed list of
blocked lines is not applied; `SndSetListener` 0 is the camera and 1 the player (inferred); the bank deferral of mode
1's enter ends with the load screen. The system music (`repo:src/gamemodes/system_music.h`) keeps each mood's track
hashes and, while on, loops a random track of the mood each frame's surroundings give when the mood changes; the
mood is 1 while an AI human with health left targets player 1 with a fight or melee goal, else 0 (the hunted mood 2,
its chase goals not built, never comes), the pick draws from the game's random index, and the fades are the music
player's own. `SoundSetEffect` and `SoundEnableEffects` are kept in the game state only. Not built yet: reverb, the
other ambient bindings (`AddAmbientSoundEmitter`, `SetAmbientEmitterVolumeMod`), the game's own speech commands, and
the sounds of the scenes' role clips (clip event 11, [Scenes](scenes.md#events)).
The hub's sound bindings (`repo:src/scripting/hub_world_bindings.cpp`): `EnableAmbientEmitter` switches an emitter
off (it stops its sound and plays nothing more) and on; `SoundPlay` plays a sound once at a point; `SndLoadMatrix`
runs `<name>_preload.lua` when the name changes and frees nothing (Coney keeps no matrix); `HuSay` speaks a line
with no callback.

## Open questions

- What game state `+0x268` is (it scales sounds of others, music and pitch).
- Who sets the system-music mood (game state `+0x40c`) and the deferral flag `+0x3fa58`.
- The listener vector's meaning and which object each listener is (`SndSetListener`'s values).
- The directional table at `0x0050a910` (200 percentages) and the pan tables of pan mode 1: values not yet listed.
- What human `+0x16c` does for the command lines of step 3.
- When the game itself says each speech command (fights, crowds, the police).
- What `a8` (the play call's third volume factor, `+0xa8`) is used for by each caller.
- Which stream channels a stereo sound effect claims (`0x0011b450`, its fallback `0x00112d60`).
- How fast `SoundStopMusicTrack` (`0x0010d9a0`) fades a playing track, and which random the pitch and volume factors
  draw from.
- Whether a scene soundtrack holds the dialogue alone or a full mix, and what the 20 unused stereo sounds are.
- The IOP side (`IOP.IRX`): the exact SPU2 voice assignment of streams and its mixing.
- The emitters' timing (`0x0010c100`): the delays' unit, whether one sound waits for the last, where along the line or
  positions a sound comes from, and what range, `arg8`, `arg11`, mode and the special name types (`0x0010cf58`) do.
- What `HuSpeak`'s fifth argument changes, and whether a line stopped by `HuShutUp` or cut off runs its callback.
- What mode 1's enter means by the volume 0.1 and the three channels it clears (`0x001110c8`, `0x001104c8`), and who
  ends the bank deferral (`+0x3fa58`).
- The blocked lines of `0x00114c98` (level, set, command and line values).
