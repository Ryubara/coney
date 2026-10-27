---
kind: topic
system: sound
owns:
  files: ["Device/ps2/sound/msaudiodevice.cpp"]
  ranges: ["0x0014d528-0x00151f5c"]
---
# Sound: the PS2 sound device

Verified against: `SLUS_212.15` (NTSC-U); the IOP module `IOP.IRX` was not read.

## Summary {#device}

Nothing the player sees: the layer under [Sound](sound.md)'s audio manager that turns its tasks into SPU2 voices and
disc streams. The audio manager decides what plays, how loud and where; this device sends that to the IOP, which
streams `BFW.SND` and `MUSIC.SND` from the disc and mixes the voices. Below the device is SCEE's MultiStream library,
which batches commands for the IOP's `IOP.IRX`. The data it plays is on [Audio data](formats/audio.md); how a disc read
error is shown is on [File I/O](file-io.md#disc-errors).

## Map

`Device/ps2/sound/msaudiodevice.cpp` (`0x0014ba18`-`0x0014d508`; the code before it, from `0x0014b158`, is the memory
card's `mcbase.cpp`) is the audio manager's platform device, confirmed (code) from the [source map](source-map.md)'s
anchors. After its static-init stub come five more of its methods, the music's (`0x0014d528`-`0x0014d758`), then
SCEE's MultiStream library (`0x0014d760`-`0x00151e68`), and two GIF callback helpers that [Movies](movies.md) uses.
Confirmed (code) for the functions; that the library is SCEE's MultiStream is inferred from its own error strings
(`SOUND_PlayStream`, `SOUND_FlushIOPCommand` ...).

A **slot** on this page is an offset into the device's vtable (`0x00537f88`); each entry is 8 bytes,
`{this adjustment, function}`, confirmed (code) at `0x0014ba18`.

| Behaviour | Function | Section |
| --- | --- | --- |
| Start-up: streams, buffers, fast load | `0x0014bbd8` `AudioDevice_Init` | [Start-up](#device-startup) |
| Each frame: send the commands | `0x0014c140` `AudioDevice_Update` | [Commands](#commands) |
| Load a bank | `0x0014c620` `AudioDevice_LoadBank` | [Sound: banks](sound.md#banks) |
| Start a voice or a stream | `0x0014caf8` `AudioDevice_Start` | [Per voice](#per-voice) |
| Disc errors | `0x00150cf0` `MS_HandleCDErrors` | [Disc errors](#disc-errors) |

### The functions {#device-functions}

The device's and the library's functions, by address (names ours where the library's string gives none):

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0014ba18` | `AudioDevice_Construct` | the device's constructor (vtable `0x00537f88`): the 13 streams' buffer sizes (below) | confirmed (code) |
| `0x0014bbb0` | `AudioDevice_SetStreamSizes` | stores one stream's four sizes at `+0x888 + 16 × stream`: mono IOP buffer, pair IOP buffer, SPU2 buffer, pair-child flag | confirmed (code) |
| `0x0014bbd8` | `AudioDevice_Init` | slot `+0x08`: MultiStream start-up, stream buffers in IOP and sound RAM, `BFW.SND` opened on streams 1-12 ([Start-up](#device-startup)) | confirmed (code) |
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

## Data

The device object, the stream table and the command buffer:

| What | Where | Meaning | Evidence |
| --- | --- | --- | --- |
| the device | `0x005df1e0`, pointer `0x0050bce4` | one object, built by its static initialiser `0x0014d4d8`; vtable `0x00537f88` | confirmed (code) |
| the command buffer | `0x005dfdc0`, 2 KB | commands `{id, length, 16-bit words}` waiting to be sent to the IOP (`0x0014daa8`) | confirmed (code) |
| the IOP's reply | `0x005e2dc0`, 2 KB | each stream's state, position and flags, the voices' idle bits, the disc-error bytes (`0x0014ee88`) | confirmed (code) |
| stream state | `0x005e3ac8` | per stream: 0 free, 2 starting, 3 reserved or stopped, 5 stopping (`0x0014e260`) | confirmed (code) |
| stream sizes | device `+0x888 + 16 × stream` | mono IOP buffer, pair IOP buffer, SPU2 buffer, pair-child flag (`0x0014bbb0`) | confirmed (code) |
| sound-RAM cursor | device `+0x840` | the next free byte of sound RAM, from `0x5010` (`0x0014bf88`) | confirmed (code) |
| bank space | device `+0x844`, `+0x834` | where the bank goes, after the stream buffers, and the room left: `0x1c0000` minus the buffers (`0x0014bbd8`) | confirmed (code) |
| bank index | device `+0x884` | a 4 KB IOP block for the `.msd` index (`0x0014bbd8`) | confirmed (code) |
| disc-error code | `0x005e5580` | 4, 5 or 3 by the error (-1, -3, other), for the disc-error message (`0x0014c140`) | confirmed (code) |

## Behaviour

### Commands {#commands}

Confirmed (code) at the cited addresses unless a line says otherwise.

Each library call appends a command, `{id, length, 16-bit words}`, to the 2 KB EE buffer (`SOUND_StartCommand`,
`SOUND_AddData`, `SOUND_EndCommand`), under one semaphore. The buffer goes to the IOP in one RPC per send
(`MS_FlushCommands`, `0x0014fae8`), which also asks for the IOP's status; the reply (`MS_ReadReply`, `0x0014ee88`)
gives each stream's state, position and flags, the SPU2 voices' idle bits and the disc-error bytes, so the EE learns
of a stream's end on a later send (at least a frame late, inferred from the one send per update). The device's update
(slot `+0x10`, `0x0014c140`, each frame) sends without waiting; loading uses a blocking copy (`0x0014c250`). A buffer
that fills mid-frame is sent at once, waiting for the previous send (`0x0014daa8`). What a command does on the IOP
side is inferred from the EE side's arguments and callers, as `IOP.IRX` was not read.

### Start-up {#device-startup}

Confirmed (code) at the cited addresses.

`AudioDevice_Init` (slot `+0x08`, `0x0014bbd8`): the IOP connection, a reset (command 7), the stream limit 13, the
**fast load** thread (an EE RPC server, id `0x12344321`, that the IOP asks before it reads the disc, so the game's
own reads and the streams share the drive; `0x001513d0`), then the stream buffers from the constructor's table, in
IOP memory and sound RAM, the sound-RAM cursor starting at `0x5010`:

| Streams | Use | IOP buffer | Sound-RAM buffer |
| --- | --- | --- | --- |
| 0 | loading banks (`.msb` into sound RAM, `.msd` into IOP memory) | `0x60000` | none |
| 1+2, 3+4 | the [stereo pairs](sound.md#stream-pairs): music and scene soundtracks (the child shares the parent's buffers) | `0x20000` | `0x10000` |
| 5-9 | streamed mono sounds | `0x10000` | `0x2000` |
| 10-12 | the small loops (class flag `0x20`) | `0x2000` | `0x2000` |

Then four volume groups at full (`0x1000`), the effects cleared on both cores, and `cdrom0:\IOP\BFW.SND;1` opened on
streams 1-12 (`0x0014bbd8`); music opens `cdrom0:\IOP\MUSIC.SND;1` on its pair when a track starts (slot `+0x20`,
`0x0014d528`). SPU2 voices 13-47 serve bank samples (slot `+0xf8`, `0x0014d0e8`), reserved (3) when taken and free
once the IOP reports them idle (`0x0014f938`).

### Per voice {#per-voice}

Confirmed (code) at the cited addresses.

`AudioDevice_Start` (`0x0014caf8`) starts a task's voice. A bank sample starts by hash (command 0, `0x0014e370`, or
`0x39` from an offset, `0x0014e4b0`); a stream by `SOUND_PlayStream` (command 1, `0x0014de28`) with its file offset and
size (command `0x29`, `0x001501a8`). Volumes go through `SOUND_SetChannelVolumeSmooth` (command `0x60`, `0x001517f0`)
as 15-bit levels scaled by the voice's group (`0x001511c0`); pitch through command 3 (`0x0014e628`). A **positional**
sound (class flag `0x02` or `0x08`) feeds the reverb while effects are on (slot `+0xb0`, command `0x18`,
`0x0014d3e0`); every other sound, and the music's streams 1-4, is kept dry (slot `+0xb8`, command `0x19`,
`0x0014d408`).

### Disc errors {#disc-errors}

Confirmed (code) at the cited addresses.

`MS_HandleCDErrors` (`0x00150cf0`, each update): when the IOP reports a read error the streams pause (command `0x37`)
and the probe file `cdrom0:\SLUS_212.15;1` is tried each update until it opens; then they resume (command `0x35`) and
the IOP asks for the last stream again. Meanwhile the device sets `0x005e5580` to 4, 5 or 3 by the error (-1, -3,
other) for the disc-error message (`0x0014c140`), or, while loading, pushes the error mode
(`0x00157930(0x005e5560, 0, n)`, `0x0014c250`). The readers' side is on [File I/O](file-io.md#disc-errors).

## In Coney

Coney has no device layer of its own to port: `Mixer` (`repo:src/audio/mixer.h`) takes the game's voices as this
device does (a 15-bit level per side, the SPU2 pitch word) and mixes them on the host, and `repo:src/audio/sound_bank.h`
loads a bank as `AudioDevice_LoadBank` does, decoded to PCM ([Sound: In Coney](sound.md#coneys-implementation)).
Coney's deliberate differences: no IOP, so no command buffer, no fast-load thread and no disc-error pause (a read error
is Coney's file layer's, [File I/O](file-io.md)); the stream buffers are Coney's own ring, two seconds ahead of the
voice; reverb is not built ([q-iop](sound-device.md#q-iop)).

## Open questions

- **The IOP side**{#q-iop}: what `IOP.IRX` does with each command, the exact SPU2 voice assignment of streams, the
  reverb's mixing and what commands `0x25`, `0x2a`, `0x3a`-`0x3c`, `0x3e`, `0x3f`, `0x53` and `0x57` mean. Depends on
  it: a faithful reverb and the movie hand-over. To find out: read `IOP.IRX`, an IOP module the shared Ghidra project
  does not hold.
