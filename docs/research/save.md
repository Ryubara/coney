# Profiles, options and saving

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Runtime claims were made in
PCSX2 2.9.94 (2026-10-06) from a fresh boot with a new, unformatted 8 MB card in slot 1 (no other card), driving the
pad over PINE (the `scripted-pad` patch, written before the pad code first ran) through STORY → CREATE NEW PROFILE →
a typed name → the default difficulty, brightness and subtitles, reading memory over PINE and listing the card image
afterwards; they say so.

## Purpose

What a **profile** is (the record the game keeps per player name), what the STORY screens write into it, how the
options change the game, and how the **save system** puts profiles on the memory card: the file layout, the
load / save / autosave flow and the card dialogs. The screens' look and their transitions are on
[Front end](frontend.md#profile-manager); the start-up card check on [Front end](frontend.md) (mode 6) and
[Boot](boot.md); what happens after `Menu.startGame` on [Front end](frontend.md#story-start) and
[Scripts](scripting.md#run-next-mission); the inventory, unlockables and statistics on
[Inventory, unlockables and statistics](player-state.md).

In short: a profile is a **1,284-byte record** (version `0x11`, an 8-character name, the unlockables, the per-mission
best scores, the Rumble data, the banked money, the options and 128 script flags). Six of them make the one save file,
`BASLUS-21215/BASLUS-21215` (7,704 bytes), beside `WARR.ICO` and `icon.sys`. There is **no checksum**. The game
saves only by **autosave**: right after a new profile is made, at the end of each mission, and from the hub.

## Original structure

`Warriors/W_SaveSystem.cpp` (the profile slots and the record), `Warriors/W_PS2SaveSystem.cpp` (the card files, a state
machine), `Device/ps2/memorycard/mcbase.cpp` (the card library wrapper, [below](#card-library)),
`GameModes/Gm_MemoryCard.cpp` (mode 6, the dialogs), `GUI/ProfileManagementGUI/PM_*.cpp` (the screens),
`GUI/TextEntryPad.cpp` (name entry), `GUI/OptionMenu.cpp` (the in-game options). Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x006fe2f8` | `g_PS2SaveSystem` | the save system (pointer at `0x00515024`; vtable at `+0x128` = `0x00545fa0`) | confirmed (code), confirmed (runtime) |
| `0x00421348` | `SaveSystem_Init` (slot `+0x14`) | measures the record, builds the template and the six-slot image | confirmed (code) |
| `0x00421708` | `Profile_Write(ss, file, slot)` | serialises the game state into one slot | confirmed (code) |
| `0x00421ad0` | `Profile_Read(ss, file, slot)` | the reverse, applying each option | confirmed (code) |
| `0x00421638` / `0x004219d0` | `ProfileHeader_Write` / `_Read` | the 16-byte slot header | confirmed (code) |
| `0x00421e68` | `Profile_Create(ss, slot, name)` (slot `+0x4c`) | a new profile from the template | confirmed (code) |
| `0x00421f98` / `0x00422028` | `Profile_Delete` (slot `+0x54`) / `Profile_UndoDeletes` (slot `+0x5c`) | | confirmed (code) |
| `0x00421d98` | `SaveSystem_WriteCurrent` | all six headers, then the current slot's record | confirmed (code) |
| `0x0041ff68` | `PS2Save_Service` (slot `+0x1c`) | one step of the card state machine | confirmed (code) |
| `0x00420060` / `0x00420570` / `0x004206a0` | `PS2Save_CreateStep` / `_LoadStep` / `_SaveStep` | states 4, 5, 6 | confirmed (code) |
| `0x00155308` | `Autosave_Request` | pushes mode 6 in save mode | confirmed (code) |
| `0x0015baa0` / `0x0015be00` / `0x0015c2c0` | mode 6 `Enter` / `Update` / `Exit` | the card dialogs | confirmed (code) |
| `0x00418a88`, `0x00418aa8`, `0x00418ac8`, `0x0041a410` | the per-pad and global option setters ([below](#options)) | | confirmed (code) |
| `0x001b4838` | `Gamma_Set(level)` | the brightness | confirmed (code) |

## Data

### The profile record {#record}

`Profile_Write` (`0x00421708`) writes these fields in this order, little-endian; `Profile_Read` (`0x00421ad0`) reads
them back the same way. The record size is measured, not fixed: `SaveSystem_Init` writes a fresh game state into a
scratch memory file and keeps its length (`ss + 0x108`), **0x504 = 1,284 bytes** (confirmed (runtime)). Confirmed
(code) for the order; the runtime dump of a new profile matched every offset.

| Offset | Size | Field | Live copy | Default (new profile) |
| --- | --- | --- | --- | --- |
| `0x000` | 4 | version, always `0x11` | | `0x11` |
| `0x004` | 8 | the name, no terminator when 8 characters long | slot `+0x04` | the typed name |
| `0x00c` | 4 | "story finished on HARDCORE SOLDIER" (unlocks the fourth difficulty, [below](#story-screens)) | slot `+0x28` | 0 |
| `0x010` | 0x168 | the per-mission best records: 30 × 12 bytes, indexed by the level record's mission byte (`+0x0c`): `+0` best score, `+4` a running total, `+8` a grade byte, `+9` a percentage byte | stats object `0x006fe490 + 0x300` (`0x00423098`, `0x004231b8`) | zeros |
| `0x178` | 0x50 | the unlockables' **locked** bits, 640 (a set bit is locked) | `0x006fe8f8` | all ones |
| `0x1c8` | 0x50 | a second 640-bit set of the unlockables (the "new" marks, inferred) | `0x006fe948` | zeros |
| `0x218` | 0x254 | the Rumble custom-gang store ([Front end](frontend.md#rumble-gangs); written and read by `0x00202fc0` / `0x00203040`, confirmed (code)) | `0x0063ef80` | zeros |
| `0x46c` | 0x40 | the store's first 512 bits (the owned character types) again, packed per word | `0x0063ef80` | zeros |
| `0x4ac` | 4 | the **banked money** | `W_GameState + 0x480` | 0 |
| `0x4b0` | 0x14 | the option block: `+0x04` brightness byte, `+0x08` SoundFX volume, `+0x0c` Music volume (floats); `+0x00`, `+0x10` unidentified | `W_GameState + 0x57a0` | 0, 40, 0.9, 0.9, 0 |
| `0x4c4` | 4 × 3 × 2 | per pad (P1 then P2): invert camera, auto-adjust camera, vibration | `W_GameState + 0x440 + pad × 4`, `+0x448 + pad × 4`, byte `+0x56de + pad` | 0, 1, 1 |
| `0x4dc` | 4 | 2P split-screen merge | `W_GameState + 0x450` | 1 |
| `0x4e0` | 4 | subtitles | `W_GameState + 0x438` | 0 (English) |
| `0x4e4` | 4 | Pro Logic II (stored inverted in the menu) | sound manager `0x0050aa84 + 0x3faa8` | 0 |
| `0x4e8` | 4 | the Video setting, Normal or Wide | device slot `+0xd4`; applied on leaving the profile manager | 0 |
| `0x4ec` | 4 | difficulty (stored as a byte) | `W_GameState + 0x43c` | 1 |
| `0x4f0` | 4 | unidentified | `W_GameState + 0x00` | 0 |
| `0x4f4` | 0x10 | the script flags 1-128 (`SetLUASaveDataBool`) | `W_GameState + 0x572c` | zeros |

**Not saved**, confirmed (code) by their absence from `Profile_Write`: the checkpoint (`W_GameState + 0x33a`), the
inventories (only the money banked at the end of each mission, below), the running statistics, the eight
`SetLUASaveDataFloat` slots (`W_GameState + 0x570c`), the language (`+0x120`). Story progress is the unlockables:
completing a level unlocks its `(level, 0, 0)` records ([Scripts](scripting.md#level99)), and `runNextMission` picks the
next mission from them; a loaded profile always starts that mission at checkpoint 1 (inferred from
`runNextMission(1)`).

**The money bank.** Mode 0xb's `Update` (`0x0015d160`), after a mission, adds each player's money (inventory item 2,
`0x0041e420(inv, player, 2)`) to `W_GameState + 0x480` (`0x0041e398`); `0x0041ef38` adds to it as well. Confirmed
(code).

### The save system's slots {#slots}

The save system holds six 0x2c-byte slot records at `ss + 0x00` (cleared by `0x00421dd0`), the record length
(`+0x108`), the image length (`+0x10c` = 6 × `+0x108`), a memory file of the whole image (`+0x110`), the template
(`+0x114`, the record of a fresh game state), the current slot (`+0x118`), a "bad version" flag (`+0x11c`) and
**saving enabled** (`+0x124`). Confirmed (code) at `0x00421348`; values confirmed (runtime): `+0x108` = `0x504`,
`+0x10c` = `0x1e18`.

| Slot offset | Meaning | Accessors (vtable slot) |
| --- | --- | --- |
| `+0x00` | used | `+0x64` |
| `+0x04` | name, 8 bytes, `+0x0c` always 0 | `+0xbc` |
| `+0x0d` | the name before a delete, for undo | |
| `+0x16` | 6 bytes, a date and time (`0x0041f730` turns the console clock from BCD) | `+0xc4` |
| `+0x1c` | created since the last save | `+0x6c` / `+0x8c` |
| `+0x20` | deleted | `+0x74` / `+0x84` |
| `+0x24` | not loadable (the header is skipped on load; `PM_Load` sends it to `PM_Delete`) | `+0x7c` / `+0x94` |
| `+0x28` | the fourth-difficulty unlock | `+0xac` (any slot) |

Other calls: `+0x9c` the number of used slots, `+0xa4` the first free slot (0 when all six are used), `+0xb4` sets
`+0x28` of the current slot when saving is enabled, the difficulty is 2 and every story level (mission bytes 1-23) is
unlocked (`0x00424540`), `+0xcc` loads a slot into the game state (`Profile_Read`). Confirmed (code).

## Behaviour

### A new profile: what each STORY screen writes {#story-screens}

Every write below is confirmed (code); the values on a fresh boot are confirmed (runtime).

1. **`PM_Profile`** (init `0x0020be68`, accept `0x0020c588`): with no profiles it offers only CREATE NEW PROFILE (code
   1) and RELOAD PROFILES (code 3, calls `Menu.reloadProfiles`); with some, USE EXISTING PROFILE (0), CREATE (1, left
   out when all **six** slots are used) and DELETE PROFILE (2, sets `0x0050f5a0` = 1, which makes `PM_Load` lead to
   `PM_Delete`).
2. **`PM_Create`** (init `0x002050f0`, the pad's callback `0x002054a0`): stores the first free slot in `0x0050f594`
   and opens a `TextEntryPad` (`0x001cc2f0`) whose text starts as the pending name at `0x0063f1d8` (**empty** on a fresh
   boot; it keeps the last name typed, and triangle on this screen clears it). Name entry, `0x001cca80`:
   - **Keys**: the characters of global string `0x97` in order, A-Z, 1-9, 0, `_`, `!`, `?`, `.`, then five blank
     spacers, then OK (string `0x99`) and DEL (`0x9a`): 47 keys on a 12-column grid. `_` types a **space**.
   - **Length**: at most **8**; a key when full plays cue `0xe` and does nothing; the eighth character moves the
     cursor to OK. DEL removes the last character (cue `0xc`); a character plays cue 10.
   - **OK**: refused (cue `0xe`) when the name is empty or only spaces; otherwise cue `0xb` and the callback, which
     compares the name with every used slot's name **case-sensitively** (`strcmp`) and, on a match, shows global
     string `0x86` and stays; else copies the name to `0x0063f1d8` and returns 0 (→ `PM_Difficulty`).
   - At runtime the pad started empty with a limit of 8 (`pad + 0x7c`), the eighth key jumped to OK, and the name was
     kept at `0x0063f1d8`.
3. **`PM_Difficulty`** (init `0x002067d8`, accept `0x00206d88`): items SUCKER (`0x90`), BOPPER
   (`0x91`), HARDCORE SOLDIER (`0x92`), and UNLEASH THE FURY (`0x93`) only when some slot has `+0x28` set. The cursor
   starts on BOPPER, or on the fourth item when it is there. Cross writes the **cursor index** to the byte
   `W_GameState + 0x43c` (0 easy, 1 normal, 2 hard, 3 fury, as `GetProfileDifficulty` reports) and plays cue 9.
4. **`PM_Light`** (init `0x00208510`, commands `0x00208cc0`, setter `0x00208c18`): a bar from 0 to 100 that **always
   starts at 40**; left and right step it by 5 (cue 6). Every step calls `Gamma_Set(value)` ([Brightness](#brightness)),
   so the picture changes while the bar moves; the screen also shows a test picture and global string `0x118`. Cross
   sets **saving enabled** (`ss + 0x124` = 1) and plays cue 9. At runtime: 40, one step right made `+0x57a4` 45, one
   left 40 again; cross set `+0x124` to 1.
5. **`PM_Subtitles`** (init `0x0020cab8`, accept `0x0020cf30`): ON (`0x95`) and OFF (`0x96`); the cursor starts on OFF
   when the language (`W_GameState + 0x120`) is 0, English, else on ON. Cross writes `W_GameState + 0x438` = 1 for ON,
   0 for OFF, plays cue 9 and sets three flags: `0x0050f5b4` ("a new game was started", read by the autosave
   request), `0x0050f598` ("create the pending profile") and `0x0050f5b0` (**the profile manager is done**).

**How the flow ends** (the open item on [Front end](frontend.md#story-start)): no screen empties the screen flow. A
screen that starts a game sets the done flag `0x0050f5b0`; the controller's update (`0x00204ba0`) returns it, and
mode 0x12's `Update` (`0x0015e238`) then queues a fade to black (`ScreenFx_Queue(1.0, 1)`); when the fade is over
(`0x005fdeb8 + 0x1d4` clear) it stops the music (`0x0010fba8`, inferred) and returns 0, so the loop pops the mode.
`PM_Subtitles` does this for a new profile;
`PM_Load` (`0x002098b0`) and `PM_Continue` (`0x00203800`) do it for a saved one, after loading the chosen slot into
the game state (slot `+0xcc`) and setting saving enabled. Confirmed (code); at runtime the done flag was set on the
cross and mode 0x12 was gone 0.65 s later.

**Mode 0x12's `Exit`** (`0x0015e130`): if `0x0050f598` is set, `Profile_Create(slot 0x0050f594, name 0x0063f1d8)`
(`0x00421e68`): mark the slot used and created, copy the name, copy the **template** into the slot, write its header,
load the slot into the game state, then put back the difficulty, brightness and subtitles chosen on the screens. So a
new profile is a fresh game with the three choices. Then `Menu.startGame` (when the controller finished normally and
Rumble mode was not chosen) and the Video setting is applied (`0x00194e28`). Confirmed (code); at runtime slot 0 held
the name, used = 1, created = 1.

### Brightness {#brightness}

`Gamma_Set(level)` (`0x001b4838`) clamps the level to 255, stores it in the byte `W_GameState + 0x57a4`, and sets the
light manager's **brightness offset** `+0x90` to `(level / 255, level / 255, level / 255, 1)` through `0x0017ec38`,
which adds the change to the colour of every ambient and directional light the manager holds (`0x0017ed60`). The
manager's constructor (`0x0017d640`) sets the same offset to 40 / 255, so 40 is the brightness of a game that never
touched the setting. The other offset, `+0xa0`, belongs to the script call `SetGammaOffset` (`0x0017ec80`). How the
offset lights the world: [The streamed world](world.md#lighting). Confirmed (code); confirmed (runtime): `+0x90` read
0.1569 (40/255) and 0.1765 (45/255) as the bar moved, `+0xa0` stayed 0.

### The other options {#options}

The in-game options menu (`0x001d86a8`) shows Lighting (the brightness bar), Camera (Invert P1 / P2 Cam, Auto Adjust
P1 / P2 Cam, 2P Split Screen Merge, left out in level 102), Vibration (P1 / P2 Controller), Subtitles, Video (Normal,
Wide), Audio (Music and SoundFX sliders, Pro Logic II) and Restore Default. Each writes the live copy in the record
table above through a setter that also updates the code's own global: invert `0x00418a88` (and `0x0050b1f8 + pad`, the
camera's pitch inversion), auto-adjust `0x00418aa8` (`0x0050b240 + pad`), merge `0x00418ac8` (`0x0050b1c4`), vibration
`0x0041a410` (turning it on gives a short rumble), SoundFX `0x00110638` (sound manager `+0x3fa20`), Music `0x001105b0`.
**Restore Default** (`0x001d80e0`) sets brightness 40, both volumes 0.9, invert off, auto-adjust on, merge on,
vibration on, subtitles off, Pro Logic II off and the half-word `W_GameState + 0x454` to 0. Confirmed (code).

### The save file on the card {#card-files}

| Path | Size | What |
| --- | --- | --- |
| `BASLUS-21215/` | | the save directory |
| `BASLUS-21215/BASLUS-21215` | 7,704 | six profile records, slot *n* at *n* × 1,284 |
| `BASLUS-21215/WARR.ICO` | 79,128 | the icon, copied from the disc's `WARR.ICO` |
| `BASLUS-21215/icon.sys` | 964 | the standard `PS2D` header: title "The Warriors", all three icons `WARR.ICO` (`0x0014ad90`) |

Confirmed (code) at `0x00420060`; confirmed (runtime): the card image after the first save held exactly these three
files with these sizes, every slot header was version `0x11`, slot 0 held the typed name, difficulty 1, brightness 40
and the defaults above, slots 1-5 were empty records. A string `BASLUS-21215/bugstar.dat` (`0x0041f8b0`,
`0x0041f960`) is a 20-byte debug file the shipped game does not write on this path (it was absent).

**Integrity**: no checksum or CRC. Loading checks the three files' sizes (`0x0041fcc8`: `WARR.ICO` 79,128,
`icon.sys` 964, the data file the image length; any other size is "corrupt") and each header's version (`0x11`; another
sets `ss + 0x11c`). **Space**: a new save needs `round_up_5(ceil(7,704 / 1024) + 83)` = **95 KB** free (`0x0041fb78`);
the check is made only when the directory does not exist yet. Confirmed (code).

**The card state machine** (`ss + 0x184`, one step per frame from `PS2Save_Service`): 1 idle; 2 detect (after slot
`+0xf4`); 3 format (slot `+0x11c`); 4 **create**: make the directory, write a zeroed data file, read `WARR.ICO` from the
disc and write it, write `icon.sys` (sub-steps 0-11 in `+0x188`); 5 **load**: read the data file into the image and
parse the six headers; 6 **save**: write the whole image over the data file, then `icon.sys` again. Confirmed (code);
at runtime the first save ran format (1.6 s), detect, create (1.3 s) and save (0.4 s).

### The card library {#card-library}

`Device/ps2/memorycard/mcbase.cpp` (`0x00149f30`-`0x0014b9d0`) wraps Sony's `libmc` for the save system's state
machine above: a card record `{port, slot, type, free, format, changed, present}`, ten file records handed out from a
queue, and each call retried up to six times while the library reports it busy, then (on the blocking path) waited
for with `sceMcSync` and its error printed. `PS2Save_CreateStep`, `_LoadStep` and `_SaveStep` use the asynchronous
calls (`0x0014b3d0`-`0x0014b9d0`) one step per frame; detection, the size checks and the `bugstar.dat` debug file use
the blocking ones. A second, asynchronous card library (`0x001520b0`-`0x00153258`: format, directory, list, size,
find, read, delete, run by an operation number) has no caller in this build (inferred unused). Confirmed (code):

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00149f30` | `MCBase_InitLibrary` | `InitLibrary`: the `Queue<MC_FILE *>` of 10 free file records (`0x005df190`, 8 bytes each: card, descriptor), then `sceMcInit` up to six times, printing the library's error (old `mcserv.irx` or `mcman.irx`, failure) when it fails | confirmed (code) |
| `0x0014a188` | `MCCard_Clear` | a card record `{port, slot, type, free, format, changed, present}`: all unknown (-1), not present | confirmed (code) |
| `0x0014a1a8` | `MCCard_Copy` | copies a 28-byte card record | confirmed (code) |
| `0x0014a1e8` | `MCCard_ApplyInfo` | reads `sceMcGetInfo`'s result: present when the type is 2 (a PS2 card) and the result above -10; changed when it is negative (another card); free space × 1024 | confirmed (code) |
| `0x0014a228` | `MCCard_Detect` | `iFindFirstPS2MemoryCard`: `sceMcGetInfo` (up to six tries) and waits; 0 no card, 1 a new card, 2 the same card | confirmed (code) |
| `0x0014a358` | `MCCard_StartDetect` | the same request without waiting | confirmed (code) |
| `0x0014a440` | `MCCard_Init` | a card record for `port`, `slot`, then a detection, waiting or not | confirmed (code) |
| `0x0014a4b8` | `MCBase_FileOpen` | `fopen(card, name, mode)`: `rb` read, `wb` create and write, `we` write, `rw` both; waits and takes a free file record (the descriptor), or prints the error | confirmed (code) |
| `0x0014a758` | `MCBase_FileDelete` | `sceMcDelete`, waiting; true on success | confirmed (code) |
| `0x0014a7f0` | `MCBase_FileClose` | `sceMcClose`, waiting; returns the record to the queue | confirmed (code) |
| `0x0014a8d8` | `MCBase_FileRead` | `fread`: `sceMcRead`, waiting; the bytes read, or -1 with the error printed | confirmed (code) |
| `0x0014aa78` | `MCBase_FileWrite` | `fwrite`: `sceMcWrite`, waiting; 1, or 0 with the error printed | confirmed (code) |
| `0x0014ac50` | `MCBase_AsciiToSjis` | the icon title to Shift-JIS: letters `0x82xx`, space `0x8140`, brackets; returns the line-break offset (`\n`) | confirmed (code) |
| `0x0014ad90` | `MCBase_BuildIconSys` | `icon.sys`: `PS2D`, the line break, background colours and light vectors, the title, the three icon names | confirmed (code) |
| `0x0014b158` | `MCBase_GetDir` | `iGetDir`: `sceMcGetDir` up to six tries and waits; the entry count, 0 on failure (card removed below -9) | confirmed (code) |
| `0x0014b2a8` | `MCBase_GetFileAttributes` | one directory entry's attributes, -1 when the file is missing | confirmed (code) |
| `0x0014b310` | `MCBase_IsSubdirectory` | attribute bit `0x08` set | confirmed (code) |
| `0x0014b360` | `MCBase_GetFileSize` | one entry's size, -1 when missing | confirmed (code) |
| `0x0014b3d0` | `MCBase_FileOpenStart` | `fopen` without waiting (the asynchronous path) | confirmed (code) |
| `0x0014b4c8` | `MCBase_IsOpenComplete` | `isfopenAsyncComplete`: polls `sceMcSync`; on success takes a file record, else prints the error | confirmed (code) |
| `0x0014b6b0` | `MCBase_FileCloseNoWait` | `sceMcClose` without waiting; returns the record | confirmed (code) |
| `0x0014b770` | `MCBase_FileReadStart` | `sceMcRead` without waiting | confirmed (code) |
| `0x0014b7f8` | `MCBase_FileWriteStart` | `sceMcWrite` without waiting | confirmed (code) |
| `0x0014b880` | `MCBase_FormatStart` | `sceMcFormat` without waiting | confirmed (code) |
| `0x0014b8e8` | `MCBase_MakeDirectory` | `MakeDirectory`: `sceMcMkdir`; an existing directory is a warning | confirmed (code) |
| `0x0014b9d0` | `MCBase_PollResult` | `sceMcSync` without waiting; 1 and the result when the request is done | confirmed (code) |
| `0x001520b0` | `McAsync_Reset` | ends the current operation: kept as the previous one, cleared, the request block reset from defaults (`0x0054d650`) | confirmed (code) |
| `0x001521b8` | `McAsync_Finish` | records an operation's result (ok, error) in a 12-byte log and resets | confirmed (code) |
| `0x00152208` | `McAsync_Format` | operation 2: `sceMcFormat` | confirmed (code) |
| `0x001522b8` | `McAsync_CountFiles` | operation 5: `sceMcGetDir("/<dir>/*")`, the entries less `.` and `..` | confirmed (code) |
| `0x001523d8` | `McAsync_MakeDirectory` | operation 4: `sceMcMkdir`, only on a formatted card | confirmed (code) |
| `0x001524c8` | `McAsync_DirectoryExists` | operation 3: `sceMcGetDir` of the directory itself | confirmed (code) |
| `0x001525c0` | `McAsync_DirectorySize` | operation 7: the directory's size in KB (each file rounded up, plus the entries' clusters and 2) | confirmed (code) |
| `0x00152740` | `McAsync_ListDirectory` | operation 6: reads the directory 16 entries at a time into the caller's buffer | confirmed (code) |
| `0x001529a0` | `McAsync_FindFile` | operation 8: a file's directory entry by name | confirmed (code) |
| `0x00152bf0` | `McAsync_CheckFile` | operation 11: checks the names (directory 9 characters, file 32), the directory and then the file | confirmed (code) |
| `0x00152f78` | `McAsync_ReadFile` | operation 10: change directory, open, read into the caller's buffer, close | confirmed (code) |
| `0x00153118` | `McAsync_DeleteFile` | operation 9: `sceMcDelete("/<dir>/<file>")` | confirmed (code) |
| `0x00153258` | `McAsync_Update` | the dispatcher: polls `sceMcSync`, watches the card (`sceMcGetInfo`), runs the current operation's step; no code calls it | confirmed (code) |

### Mode 6: load, save and the dialogs {#mode-6}

Mode 6 (`0x005e5810`) runs in one of two kinds, `0x005e5d80`: **1 load** (the boot, and `SSMC_StartLoadSequence`) and
**0 save** (`Autosave_Request`, `SSMC_StartSaveSequence`, and with `0x0050c700` set `SSMC_StartDeleteSequence`). In save
kind `Enter` first writes the current game into the current slot of the image (`0x00421d98`). Each frame `Update`
services the save system and, when it is idle, runs the next step (a function pointer at `0x0050c734`). The steps,
confirmed (code); each dialog is a message box (`0x001c7128`) whose cursor starts on the choice given (the "default"):

| Card state | Load kind | Save kind |
| --- | --- | --- |
| no card | string `0x9c` + 95 + `0x9e`: Yes (start anyway) / Retry, default Yes | `0x9c` + 95 + `0x9d`: Continue without saving / Retry, default **Retry** |
| a different card than before | | `0xa1`-`0xa4` (enable autosave?): Yes / Continue without saving |
| unformatted | finish quietly (no profiles) | `0xac` (format?): Yes / Continue without saving, default **Continue**; Yes → `0xad` (sure?): Yes / No, default **No**; Yes formats (`0xae` for 3 s) and starts again |
| formatted, no save, too little space | `0xaf` + 95 + `0xb1`: Yes / Retry, default Retry | `0xaf` + 95 + `0xb0`: Continue without saving / Retry, default Retry |
| formatted, no save, space | finish (no profiles) | create (`0xa9` "Autosaving..." for 2 s), then save |
| a save | sizes wrong: `0xb2`: Yes / Retry, default Yes (Yes deletes and recreates it at the next save); else load (`0xaa` for 3 s) | save (`0xa9` for 2 s) |

"Continue without saving" skips this save only: saving stays enabled and the next autosave asks again. A failure
shows `0xb7` (save), `0xb8` (load), `0xb9` (delete) or `0xb6` (format) with Continue without saving / Retry (the card
dialog `0x0015b918`). The texts are in `config_strings_en.lua`. At runtime, with the unformatted card, the boot
finished quietly, the first autosave showed `0xac` with the cursor on Continue without saving, and choosing Yes twice
formatted the card and saved.

**When it saves** (`Autosave_Request`, `0x00155308`): when saving is enabled (`ss + 0x124`), the current level index
(`W_GameState + 0x56dc`) is not 0 or a new game was started (`0x0050f5b4`), and mode 6 is not already on top. It is
called by mode 0xb after every mission (and so right after a new profile, [Front end](frontend.md#story-start)), by
the Rumble menu's `Exit` (`0x0015ea40`) when its data changed (`0x0063f1d0`), by the hub's `SSMC_StartSaveSequence`,
and through `0x00154f28` (from the gameplay loop `0x00158728` and two GUI callers, not traced). Confirmed (code); at
runtime mode 6 was pushed 0.65 s after the cross on `PM_Subtitles`, with `level99` selected but not yet loaded.

**Too many profiles, no space**: `PM_TooManyProfiles` and `PM_NoSpace` are reachable only through `PM_Greet`'s
results 1 and 2, which `PM_Greet` never returns (`0x00207e28`); a full card is handled by mode 6 and six profiles by
hiding CREATE NEW PROFILE. Confirmed (code); that the two screens are left from the Xbox flow is speculative.

### Script bindings {#bindings}

[`GetProfileDifficulty`](../references/bindings/level.md#getprofiledifficulty) (`+0x43c`),
[`CfgSubtitles`](../references/bindings/config.md#cfgsubtitles) (`+0x438`),
[`SetLUASaveDataBool`](../references/bindings/level.md#setluasavedatabool) /
[`GetLUASaveDataBool`](../references/bindings/level.md#getluasavedatabool) (flags 1-128 saved),
[`SetLUASaveDataFloat`](../references/bindings/level.md#setluasavedatafloat) (not saved),
[`UM_Unlock`](../references/bindings/level.md#um_unlock) (the saved locked bits),
[`SSMC_StartSaveSequence`](../references/bindings/level.md#ssmc_startsavesequence),
[`SSMC_StartLoadSequence`](../references/bindings/level.md#ssmc_startloadsequence),
[`SSMC_StartDeleteSequence`](../references/bindings/level.md#ssmc_startdeletesequence),
[`SSMC_detect`](../references/bindings/level.md#ssmc_detect), [`SSMC_format`](../references/bindings/level.md#ssmc_format),
[`ShowProfileManager`](../references/bindings/hud.md#showprofilemanager).

### Game-state functions {#warriors-functions}

Functions of the game-state module (`0x00417af0`-`0x00424e50`: inventory, statistics, flags, configuration
workers) that belong to this page, by address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0041e3a8` / `0x0041e3b0` | `Value_Get` / `Value_Set` | read / write the first word of a record; used by `Profile_Read`, `Profile_Write` and the Rumble soldier price (`0x00424d60`, `RMBuySoldiers_SetupPrice`) | confirmed (code); the field's meaning inferred |
| `0x0041f6b0` | `PS2SaveSystem_Construct` | from the static initialiser: the base (`0x004212c0`), the card state `+0x180`-`+0x190` zeroed, vtable `0x00545fa0` at `+0x128`, the card record at `+0x148` cleared | confirmed (code) |
| `0x0041f700` | `PS2SaveSystem_InitLibrary` | vtable slot: initialises the card library and sets the state `+0x184` = 1 (idle) | confirmed (code) |
| `0x0041f730` | `PS2SaveSystem_GetDate` | vtable slot: the console clock, BCD to binary, into 6 bytes (the slot's date) | confirmed (code) |
| `0x0041f848` | `PS2SaveSystem_HasSaveDirectory` | vtable slot: opens card 1 and tests for the `BASLUS-21215` directory | confirmed (code) |
| `0x0041fa38` | `PS2SaveSystem_StartDetect` | vtable slot: opens card 1 for a check and sets the state to 2 (detect), sub-step 0 | confirmed (code) |
| `0x0041faa8` | `PS2SaveSystem_IsCardFormatted` | vtable slot: opens the card and returns whether it is present and formatted (`+0x144`, `+0x140`) | confirmed (code) |
| `0x0041fb08` | `PS2SaveSystem_HasRoom` | vtable slot: true when the directory exists already, else when the free space (`+0x158`) holds the needed KB (slot `+0x134`) × 1024 | confirmed (code) |
| `0x0041fbb8` | `PS2SaveSystem_StartFormat` | vtable slot: starts a format and sets the state to 3 | confirmed (code) |
| `0x0041fc28` | `PS2SaveSystem_FilesExist` | vtable slot: whether both the data file and `WARR.ICO` are in the save directory (their attributes kept at `+0x194`) | confirmed (code) |
| `0x0041fd60` / `0x0041fdc8` / `0x0041fed0` | `PS2SaveSystem_StartLoad` / `PS2SaveSystem_StartCreate` / `PS2SaveSystem_StartSave` | vtable slots: open the data file for reading and set state 5 / make the directory and set state 4 / delete `icon.sys`, open the data file for writing and set state 6 | confirmed (code) |
| `0x0041fe20` | `PS2SaveSystem_DeleteFiles` | vtable slot: deletes `icon.sys`, `WARR.ICO` and the data file | confirmed (code) |
| `0x0041fe78` | `PS2SaveSystem_RunToIdle` | vtable slot: services the state machine (slot `+0x1c`) until it reports idle (slot `+0x24`) | confirmed (code) |
| `0x00420928` | `PS2Save_IconLoaded` | the completion of the create step's read of the disc's `WARR.ICO`: copies its 79,128 bytes from the file manager's buffer into `+0x190` and moves to the next sub-step | confirmed (code) |
| `0x004209a0` | `PS2SaveSystem_StaticInitStub` | `PS2SaveSystem_StaticInit(1, 0xffff)` | confirmed (code) |
| `0x004212c0` | `SaveSystem_Construct` | base save system: vtable `0x00546198`, the six slot dates cleared, the record length, image file, template, bad-version flag and saving-enabled flag (`+0x108`, `+0x110`, `+0x114`, `+0x11c`, `+0x124`) zeroed | confirmed (code) |
| `0x004214c0` | `SaveSystem_BuildTemplate` | writes the current (fresh) game state with `Profile_Write` into a 5 KB memory file, keeps the length as the record length (`+0x108`), allocates `+0x114` of that size and copies the bytes in: the template a new profile starts from. Called by `SaveSystem_Init` | confirmed (code) |
| `0x00421578` / `0x004215d8` | `SaveSystem_WriteHeaders` / `SaveSystem_ReadHeaders` | `ProfileHeader_Write` / `_Read` for slots 0-5 in turn; the writer runs from `SaveSystem_WriteCurrent`, the reader from the card load step | confirmed (code) |
| `0x004221a0` | `SaveSystem_CountUsedSlots` | vtable `+0x9c`: how many of the six slots answer "used" (`+0x64`) | confirmed (code) |
| `0x00422208` | `SaveSystem_FirstFreeSlot` | vtable `+0xa4`: index of the first unused slot, 0 when all six are used | confirmed (code) |
| `0x004222a0` | `SaveSystem_MarkHardcoreDone` | vtable `+0xb4`: when saving is enabled, the profile difficulty (`W_GameState + 0x43c`) is 2 and every story level is unlocked, sets the current slot's `+0x28` (the fourth difficulty) | confirmed (code) |
| `0x00422358` | `SaveSystem_LoadSlot` | vtable `+0xcc`: makes the slot current (`+0x118`) and reads it from the image file into the game state (`Profile_Read`); returns 1 | confirmed (code) |
| `0x00422380` / `0x004223b0` | `Script_SSMCDetect` / `Script_SSMCFormat` | the `SSMC_detect` / `SSMC_format` bindings: start the card detect (vtable `+0xf4`, `PS2SaveSystem_StartDetect`) or format (`+0x11c`, `PS2SaveSystem_StartFormat`) on `g_PS2SaveSystem` | confirmed (code) |
| `0x004223e0` | `SaveSystem_InitHook` | empty; the first call of `SaveSystem_Init` | confirmed (code) |
| `0x00423050` | `Stats_GetLevelBest` | copies the 12-byte best record of a level-table index (its mission byte, record `+0x0c`, picks one of the 30) from stats `+0x300` | confirmed (code) |
| `0x00423098` | `Stats_UpdateLevelBest` | on entering play after a mission (`Gm_InGame_Enter`): for a mission byte below 30, the two players' summed score; if over the stored best it becomes the best, with player 1's bonus percentage (`+9`) and rank (`+8`); returns 1 for a new best | confirmed (code) |
| `0x004231b8` | `Stats_AddLevelTotal` | adds a value to the current mission's running total (`+4` of its best record); from `Gm_Level_Suspend` | confirmed (code) |
| `0x00423200` / `0x00423280` / `0x00423300` | `Stats_WriteBests` / `Stats_ReadBests` / `Stats_BestsSize` | write / read the 0x168-byte best records (`0x006fe790`) to or from the profile file (from `Profile_Write` / `Profile_Read`); the size is 0x168 | confirmed (code) |
| `0x00424a20` / `0x00424b90` | `Unlockables_Write` / `Unlockables_Read` | write / read the locked set then the new set, twenty 32-bit words each, to or from the profile file | confirmed (code) |

## What Coney stores instead of a memory card {#coney}

Coney keeps each profile as one file holding the 1,284-byte record above (same fields and order, little-endian) in the
platform's per-user data folder, writes it whenever the original would autosave, and has no format, space or icon
dialogs.

### Coney's implementation {#coneys-implementation}

- [`ProfileRecord`](repo:src/warriors/profile_record.h) writes and reads the record field by field; a record of
  another size or version is an error, and the store lists that file as a damaged ("not loadable") profile under the
  name in its header. `SavedProgress` holds the fields with no other home in Coney's game state
  (`GameState::saved`), with the money bank's add and the unlock and script-flag bits.
- [`DiskProfileStore`](repo:src/warriors/disk_profile_store.h) is the save system: six slots, `profile-1.sav` to
  `profile-6.sav` in one folder. `create` makes a profile from the template with the three screen choices and writes
  it at once (the autosave after a new game); `save` is the autosave (only while saving is enabled); `remove` deletes
  the file, with no undo.
- The folder ([`profileFolder`](repo:src/platform/profile_folder.h)) is `--profiles DIR`, else `profiles` in SDL's
  per-user data folder; test mode without `--profiles` keeps the profiles in memory for the run
  (`SessionProfileStore`). The start-up flow picks the store; the mission-complete mode (0xb) autosaves after its pop.
- Mode 6 ([`MemoryCardMode`](repo:src/gamemodes/memory_card_mode.h)) has the load kind, at boot and for RELOAD
  PROFILES (`SSMC_StartLoadSequence`): the store's `reload()` reads the folder again. After PM_Delete,
  `SSMC_StartDeleteSequence` pushes it with nothing to write (the file is already gone). Either way it shows one black
  frame, and the menus below fade in and re-open the screen on top so it lists the profiles held (Coney's choice).
- `SSMC_StartSaveSequence` (the hub) autosaves through the store when a profile is in use and the level is not the
  front end; **stand-in**: it writes at once, with no mode 6 on top, and the "new game just started" case is not
  applied. `SetLUASaveDataBool` and `GetLUASaveDataBool` use the saved script flags 1-128; other numbers are kept
  for the run only (the original's fields past the saved words are not named).
- Not yet: the money bank's adds (Coney has no inventories).
- **Coney's choices**: bit *i* of the unlockable and script-flag sets is byte *i* / 8, bit *i* % 8 (the little-endian
  layout of 32-bit words, inferred); the three bytes after the brightness byte are written as zero; the date and time a
  slot keeps (`+0x16`) are not stored; the fourth-difficulty unlock (slot `+0xb4`) leaves the "every story level
  unlocked" test to its caller until the unlockables have records.

## Open questions

- The option block's `+0x00` and `+0x10` and the record's `W_GameState + 0x00` word: what they hold.
- The second 640-bit unlockables set (`0x006fe948`): that it marks new unlocks is inferred.
- The bit order of the unlockable and script-flag sets in the record (byte *i* / 8, bit *i* % 8 is inferred).
- Which message a slot-1 card change shows (`0xa1`-`0xa4`) in each case, not traced.
