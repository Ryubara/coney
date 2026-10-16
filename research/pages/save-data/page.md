# PS2 save data on the memory card

## Scope and sources

This page records what the PS2 version of the game keeps on a memory card: the save directory and its three files,
the layout of the save file and of the profile record it holds, the stored options with their values and defaults,
and the messages the player sees while the game checks, loads, saves or deletes. The icon files in the same
directory (`WARR.ICO` and `icon.sys`) are covered on the memory card icon page.

Sources, all from the PS2 disc image "Warriors, The (USA) (En,Fr,De,Es,It).iso":

| Source | Size (bytes) | Used for |
| --- | ---: | --- |
| `SLUS_212.15` | 4,903,976 | Directory and file names, file sizes, record layout, defaults (code addresses below) |
| `WARRIORS.DIR` / `WARRIORS.WAD` | 128,428 / 1,495,371,776 | The archive holding the language scripts and the icon |
| WAD entries 3774 to 3778 | 42,607 / 38,257 / 43,443 / 41,311 / 41,600 | Compiled Lua 4.0 language scripts (German, English, Spanish, French, Italian) holding the message table |

"Message N" below is entry N of the HUD message table that each language script fills; the quoted text is the
English script (WAD entry 3775). In the quotes, "[card]" stands for the phrase "memory card (8MB)(for PlayStation®2)"
as the script spells it (byte `0xAC` between "for" and "PlayStation", byte `0xAE` for the registered sign), and
"[slot]" stands for "MEMORY CARD slot 1". All values are little-endian.

## Files on the card

The game uses memory card slot 1 only. Everything it writes sits in one directory:

| Path on the card | Size (bytes) | Contents |
| --- | ---: | --- |
| `BASLUS-21215/` | (directory) | The save directory, named after the disc's product code with the region prefix `BA` |
| `BASLUS-21215/BASLUS-21215` | 7,704 | The save file: six profile records of 1,284 bytes each |
| `BASLUS-21215/WARR.ICO` | 79,128 | The 3D icon, a byte-for-byte copy of the archive entry `warr.ico` |
| `BASLUS-21215/icon.sys` | 964 | The icon settings file the console's browser reads |

- There is one save file per card. It holds every profile; there are no per-profile files.
- The save file has no checksum and no header of its own. The only integrity test is on the sizes: a save
  directory whose three files do not have exactly the sizes above counts as corrupt.
- A new save needs at least 95 KB free on the card. The messages that mention free space show this figure.

## Save file layout

The save file is six records back to back. Record *n* (0 to 5) starts at byte 1,284 × *n*.

| Offset | Size | Contents |
| --- | ---: | --- |
| `0x0000` | 1,284 | Profile slot 0 |
| `0x0504` | 1,284 | Profile slot 1 |
| `0x0A08` | 1,284 | Profile slot 2 |
| `0x0F0C` | 1,284 | Profile slot 3 |
| `0x1410` | 1,284 | Profile slot 4 |
| `0x1914` | 1,284 | Profile slot 5 |

- At most six profiles exist at once.
- A slot is in use when the first byte of its name is not zero. An unused slot still holds a full record: version
  `0x11`, an all-zero name and otherwise the values of a new profile.
- Every save writes all 7,704 bytes. The record of the profile being played is refreshed from the game; the other
  five records are written back as they were read, apart from their 16-byte headers.

## Profile record

One record is 1,284 (`0x504`) bytes. Offsets are from the start of the record.

| Offset | Size | Type | Field | Range and meaning | New profile |
| --- | ---: | --- | --- | --- | --- |
| `0x000` | 4 | u32 | Version | Always `0x11` (17) | `0x11` |
| `0x004` | 8 | char[8] | Profile name | Up to 8 characters from A to Z, 0 to 9, `_`, `!`, `?`, `.` and space; no terminator when all 8 are used; all zero in an unused slot | Name entered |
| `0x00C` | 4 | u32 | Completion flag | 0 or 1; becomes 1 once the profile's difficulty is 2 and every story mission is complete (inferred from the level table) | 0 |
| `0x010` | 360 | struct[30] | Mission bests | 30 records of 12 bytes, one per mission number 0 to 29 (see below) | All zero |
| `0x178` | 80 | u32[20] | Locked set | 640 bits, one per unlockable; a set bit means still locked | All ones |
| `0x1C8` | 80 | u32[20] | New-unlock set | 640 bits, one per unlockable; a set bit marks an unlock (meaning inferred) | All zero |
| `0x218` | 596 | struct | Rumble gangs | The Rumble mode gang block (see below) | All zero |
| `0x46C` | 64 | u32[16] | Rumble owned set | A second copy of the first 64 bytes of the Rumble block | All zero |
| `0x4AC` | 4 | u32 | Money | Banked money in dollars | 0 |
| `0x4B0` | 20 | struct | Option block | Brightness and volumes (see [Options](#options)) | See Options |
| `0x4C4` | 4 | u32 | P1 invert camera | 0 off, 1 on | 0 |
| `0x4C8` | 4 | u32 | P1 camera auto adjust | 0 off, 1 on | 1 |
| `0x4CC` | 4 | u32 | P1 vibration | 0 off, 1 on | 1 |
| `0x4D0` | 4 | u32 | P2 invert camera | 0 off, 1 on | 0 |
| `0x4D4` | 4 | u32 | P2 camera auto adjust | 0 off, 1 on | 1 |
| `0x4D8` | 4 | u32 | P2 vibration | 0 off, 1 on | 1 |
| `0x4DC` | 4 | u32 | Split screen merge | 0 off, 1 on | 1 |
| `0x4E0` | 4 | u32 | Subtitles | 0 off, 1 on | Kept from before the profile was made |
| `0x4E4` | 4 | u32 | Pro Logic II | 0 off, 1 on | 0 |
| `0x4E8` | 4 | u32 | Screen shape | 0 Normal, 1 Wide | 0 (inferred) |
| `0x4EC` | 4 | u32 | Difficulty | 0 to 3 (see below); only the low byte is used | Chosen on the difficulty screen |
| `0x4F0` | 4 | u32 | Game state word | Meaning not found | 0 |
| `0x4F4` | 16 | u32[4] | Script flags | 128 flags the mission scripts set and test, numbered 1 to 128 | All zero |

Bit sets (the locked set, the new-unlock set, the Rumble owned set and the script flags) number their bits from 0
across the words: bit *i* is bit *i* mod 32 of word *i* / 32, which on the card is bit *i* mod 8 of byte *i* / 8.
Script flag *n* is bit *n* − 1.

### Mission best record

| Offset | Size | Type | Meaning |
| --- | ---: | --- | --- |
| `+0x00` | 4 | u32 | Best mission score: the summed score of the players on that mission's best run |
| `+0x04` | 4 | u32 | Not seen set; 0 |
| `+0x08` | 1 | u8 | Rank earned on the best run |
| `+0x09` | 1 | u8 | Player 1's percentage (0 to 100) on the best run |
| `+0x0A` | 2 | | Not seen set; 0 |

A mission's best record changes only when a finished run's score beats the stored score.

### Rumble gang block

| Offset | Size | Type | Meaning |
| --- | ---: | --- | --- |
| `+0x000` | 64 | u32[16] | Owned soldier types: 512 bits, one per soldier type |
| `+0x040` | 520 | struct[10] | Ten gang slots of 52 bytes |
| `+0x248` | 4 | u32 | Current gang slot |
| `+0x24C` | 4 | u32 | Current gang member |
| `+0x250` | 4 | u32 | 1 after a gang has been made (meaning not confirmed) |

| Gang slot offset | Size | Type | Meaning |
| --- | ---: | --- | --- |
| `+0x00` | 4 | u32 | 1 when the slot holds a gang |
| `+0x04` | 10 | char[10] | Gang name |
| `+0x0E` | 2 | | Zero byte after the name, then padding |
| `+0x10` | 36 | u32[9] | Soldier type of each of the nine members; `0x160` (352) is an empty place |

The owned set appears twice in the record (record offsets `0x218` and `0x46C`); on loading, the second copy is the
one that takes effect.

### Difficulty values

The difficulty field holds the position of the choice on the CHOOSE DIFFICULTY screen (message 143), which lists
messages 144 to 147:

| Value | Choice on screen |
| ---: | --- |
| 0 | SUCKER |
| 1 | BOPPER |
| 2 | HARDCORE SOLDIER |
| 3 | UNLEASH THE FURY |

## Options

The OPTIONS screen (message 278) sets these values; all of them are stored per profile. Loading a profile applies
its options at once: brightness, both cameras, both controllers' vibration, the split screen merge, subtitles,
Pro Logic II and the screen shape.

| Option on screen | Stored at | Values | Default | Restore Default gives |
| --- | --- | --- | --- | --- |
| Lighting (brightness) | `0x4B4`, u8 | 0 to 100 | 40 | 40 |
| SoundFX volume | `0x4B8`, f32 | Volume level, 1.0 full (range inferred) | 0.9 | 0.9 |
| Music volume | `0x4BC`, f32 | Volume level, 1.0 full (range inferred) | 0.9 | 0.9 |
| Invert P1 Cam / Invert P2 Cam | `0x4C4` / `0x4D0` | 0 off, 1 on | 0 | 0 |
| Auto Adjust P1 Cam / Auto Adjust P2 Cam | `0x4C8` / `0x4D4` | 0 off, 1 on | 1 | 1 |
| Vibration, P1 Controller / P2 Controller | `0x4CC` / `0x4D8` | 0 off, 1 on | 1 | 1 |
| 2P Split Screen Merge | `0x4DC` | 0 off, 1 on | 1 | 1 |
| Subtitles | `0x4E0` | 0 off, 1 on | 0 with the console set to English, 1 with any other console language | 0 |
| Pro Logic II | `0x4E4` | 0 off, 1 on | 0 | 0 |
| Video (Normal / Wide) | `0x4E8` | 0 Normal, 1 Wide | 0 (inferred) | Not changed |

- The option block at `0x4B0` is 20 bytes: `+0x00` u32 (meaning not found, 0 in a new profile), `+0x04` u8
  brightness followed by 3 padding bytes, `+0x08` f32 SoundFX volume, `+0x0C` f32 music volume, `+0x10` u32 (meaning
  not found, 0 in a new profile).
- Brightness lightens the scene: level *b* adds *b* / 255 to each of red, green and blue of the level's
  moonlight, reflected and ambient lights; lamps keep their own colour.
- Turning a controller's vibration on gives a short rumble on that controller.
- The Gore and Enable Headphones settings on the same screen are not in the record.

## New profile

Choosing CREATE NEW PROFILE and entering a name (ENTER NAME, message 137) fills the first unused slot:

- The slot gets the new profile record: version `0x11`, the entered name, completion flag 0 and every other field at
  the default listed in the [Profile record](#profile-record) table, which is the state of a game that has not been
  played yet.
- Brightness, subtitles and difficulty are the exception: the new profile keeps the values in effect when it is
  made (the profile screens set them while it is being created).
- A new profile reaches the card with the next autosave.
- A name already used by another profile is refused with message 134: "A profile already exists with this name.
  Please enter a different name."

## When the game saves

There is no save command; the game only autosaves. An autosave happens:

- when a mission ends and play leaves the level; the cash both players carry is banked into the money field first;
- when the mission scripts ask for one;
- on closing the pause menu after a setting there was changed (inferred);
- on leaving Rumble mode after its gangs changed.

No autosave starts while a card message is already on screen.

While a save is written the screen shows message 169, "Autosaving...", for 2 seconds. Message 155 explains the
feature: "The Warriors uses an autosave feature which is activated by default. When 'Autosaving…' appears on your
screen, do not remove [card] in [slot], controller, or reset/switch off console."

## Card dialogs

The game checks the card at start-up, when RELOAD PROFILES is chosen in the PROFILE MANAGER, before every
autosave and before a profile is deleted. The tables give what the player sees in each situation and context.

### Progress messages

| Message | Text | Shown | For |
| ---: | --- | --- | --- |
| 181 | "Checking [card] in [slot]. Do not remove [card], controller, or reset/switch off the console." | 3 s | Start-up, reload, delete |
| 170 | "Loading data. Do not remove [card] in [slot], controller, or reset/switch off the console." | 3 s | Start-up, reload |
| 169 | "Autosaving..." | 2 s | Autosave |
| 171 | "Deleting data. Do not remove [card] in [slot], controller, or reset/switch off the console." | 3 s | Delete |
| 174 | "Formatting [card] in [slot]. Do not remove [card], controller, or reset/switch off console." | 3 s | After agreeing to format |

Before a reload the game asks message 168, "Are you sure you want to reload profiles? Any unsaved profiles will be
lost.", with Yes and No.

### No card

| Context | Message | Choices |
| --- | --- | --- |
| Start-up | 156, 158: "No [card] inserted in [slot]. At least 95KB of free space is required to save The Warriors game data. Do you want to start the game anyway?" | Yes, Retry |
| Reload | 159: "No [card] inserted in [slot]." | OK, Retry |
| Autosave | 156, 157: "No [card] inserted in [slot]. At least 95KB of free space is required to save The Warriors game data." | Continue without saving, Retry |
| Delete | 159 | OK (the profile is not deleted) |

After the player has picked Continue without saving, later autosaves with no card in the slot show nothing.

### Card not formatted

| Context | Message | Choices |
| --- | --- | --- |
| Start-up | None; the game starts | |
| Reload, delete | 160: "No save data for The Warriors is present on the [card] in [slot]." | OK, Retry |
| Autosave | 172: "[card] in [slot] is unformatted. Format [card]?" | Yes, Continue without saving |
| After Yes | 173: "Are you sure you want to format [card] in [slot]?" | Yes, No |

After Yes to message 173 the card is formatted (message 174) and checked again. A failed format shows message 182.

### No save on the card

| Context | Free space | Message | Choices |
| --- | --- | --- | --- |
| Start-up | 95 KB or more | None; the game starts | |
| Start-up | Less than 95 KB | 175, 177: "Insufficient free space on [card] in [slot]. The Warriors requires 95KB of free space to save data. Do you want to start the game anyway?" | Yes, Retry |
| Reload, delete | Any | 160 | OK, Retry |
| Autosave | 95 KB or more | None; the save directory and its three files are made (message 169) | |
| Autosave | Less than 95 KB | 175, 176: "Insufficient free space on [card] in [slot]. The Warriors requires 95KB of free space to save data." | Continue without saving, Retry |

### Corrupt save

A save counts as corrupt when any of its three files has the wrong size.

| Context | Message | Choices |
| --- | --- | --- |
| Start-up | 178: "The Warriors save file on the [card] in [slot] is corrupt. Continuing beyond this point may cause data to be overwritten. Do you want to start the game anyway?" | Yes, Retry |
| Reload | 179: "The Warriors save file on the [card] in [slot] is corrupt and cannot be loaded." | OK, Retry |

After a corrupt save has been reported, the next autosave deletes the old files and writes a new save directory.

### Overwrite and card change

| Situation | Message | Choices |
| --- | --- | --- |
| Autosave after the player chose Continue without saving, card has room, no save on it | 161: "A [card] is inserted in [slot]. The Warriors uses an autosave feature. Do you want to enable autosaving?" | Yes, Continue without saving |
| As above, a save of the game is on the card | 162: as 161 with "Enabling the autosave feature will overwrite previous data created by The Warriors." before the question | Yes, Continue without saving |
| Autosave, the card in slot 1 has changed and holds a save of the game | 163: "[card] in [slot] has changed. The Warriors uses an autosave feature. Enabling the autosave feature will overwrite previous data created by The Warriors. Do you want to enable autosaving?" | Yes, Continue without saving |
| Autosave, the card has changed, no save on it, room for one | 164: "[card] in [slot] has changed. Do you want to enable autosaving?" | Yes, Continue without saving |
| Autosave, the card has changed, no room | 165, 166: "[card] in [slot] has changed. There is insufficient free space to save The Warriors game data. The Warriors requires 95KB of free space to save data." | Continue without saving, Retry |
| Delete, the card has changed | 167: "[card] in [slot] has changed. The selected profile will not be deleted. You may choose to reload profiles from the profile manager." | OK (the profile is not deleted) |

Yes in messages 161 to 164 turns autosaving back on and checks the card again. Writing over an existing save of the
game needs no further question once autosaving is on.

### Failures

| Failed operation | Message | Choices by context |
| --- | --- | --- |
| Format | 182: "Format failed! Please check the [card] in [slot] and try again." | Continue without saving, Retry |
| Autosave | 183: "Autosave failed! Please check the [card] in [slot] and try again." | Continue without saving, Retry |
| Load | 184: "Load failed! Please check the [card] in [slot] and try again." | Start-up: Start Game, Retry; reload: OK, Retry |
| Delete | 185: "Delete failed! Please check the [card] in [slot] and try again." | Cancel (the profile is not deleted), Retry |

A load also fails when the save file reads back shorter than 7,704 bytes. After a failed autosave the next autosave
writes a new save directory.

### Choice labels

| Message | English label |
| ---: | --- |
| 186 | Yes |
| 187 | No |
| 188 | OK |
| 189 | Retry |
| 190 | Cancel |
| 192 | Continue without saving |
| 193 | Start Game |

## Differences from the Xbox version

The same message table holds Xbox-only texts (messages 194 to 205), which speak of blocks on the Xbox hard disk and
the Xbox Dashboard instead of a memory card; the PS2 game does not show them. The Xbox save layout is not covered
here.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x00421708` | Write one profile record from the game | Profile record |
| `0x00421ad0` | Read one profile record into the game | Options |
| `0x00421e68` | Create a new profile in a free slot | New profile |
| `0x0015be00` | Run the memory card check and its dialogs | Card dialogs |

## Acceptance checks

- After the first autosave on an empty formatted card, slot 1's card holds the directory `BASLUS-21215` with the
  files `BASLUS-21215` (7,704 bytes), `WARR.ICO` (79,128 bytes) and `icon.sys` (964 bytes).
- The file `BASLUS-21215` splits into six 1,284-byte records, and every record starts with the bytes `11 00 00 00`.
- A profile named on the ENTER NAME screen appears as its ASCII letters at byte 4 of its record; unused records hold
  eight zero bytes there.
- A freshly made profile's record, once autosaved, holds the float 0.9 (bytes `66 66 66 3F`) at `0x4B8` and `0x4BC`
  and 1 at `0x4C8`, `0x4CC` and `0x4DC`.
- Selecting Restore Default on the OPTIONS screen and letting the game autosave leaves 40 at `0x4B4` and 0 at
  `0x4E0` and `0x4E4` in the current profile's record.
- Changing the Lighting value in OPTIONS, closing the pause menu and reloading the profile from the card restores
  the changed brightness.
- Starting the game with no card in slot 1 shows the "No memory card ... At least 95KB of free space is required"
  message with the choices Yes and Retry.
- Shortening the `BASLUS-21215` file on the card makes the next start-up show the corrupt-save message 178 with Yes
  and Retry.
- Choosing RELOAD PROFILES first asks "Are you sure you want to reload profiles? Any unsaved profiles will be lost."
- Starting with an unformatted card shows no card message; the first autosave then asks to format the card.
- The English language script (WAD entry 3775) gives message 169 as "Autosaving..." and message 192 as "Continue
  without saving".

## Open questions

- The option block's words at `+0x00` and `+0x10`, and the game state word at record `0x4F0`: what they hold.
- The new-unlock set at `0x1C8`: that it marks new unlocks is inferred, not confirmed on screen.
- The Rumble block's word at `+0x250`, and what a gang slot's two bytes at `+0x0E` hold beyond the name's end.
- Mission best `+0x04`: never seen written; its purpose is not known.
- Which score category the percentage at mission best `+0x09` comes from.
- What the completion flag at record `0x00C` changes for the player.
- When messages 155 (autosave notice) and 180 ("The save file format has changed. Do you want to delete the Warriors
  save file?") are shown; neither was traced, and the effect of a record whose version is not `0x11` is open.
- When message 131 ("This profile appears to be damaged and cannot be used.") appears.
- The default screen shape value in a new profile (taken from the display at start-up) and the volume range.
