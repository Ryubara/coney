# Game flags

## Scope and sources

The game has two kinds of flag. World flags are named points in a level, each with a heading, that the level
scripts place and then use to put characters somewhere, send them somewhere or give them something to do there.
Script flags are 128 on-off values kept in the player's profile, which the scripts set and test. This page records
what both hold, where they come from on the disc and what they change in play. The full per-script lists of world
flags are on the level flag lists page (ref-level-flags); the profile record is on the save data page (save-data).

Sources, all from the PS2 disc image "Warriors, The (USA) (En,Fr,De,Es,It).iso":

| Source | Used for |
| --- | --- |
| `WARRIORS.DIR` / `WARRIORS.WAD` | The 467 compiled Lua 4.0 scripts; 94 of them place world flags |
| `global.lua` (WAD entry 4,105, 169,601 bytes), the script shared by the levels | The tag table that gives the script flags their meaning |
| `SLUS_212.15` | Flag room, name length, search by name, script flag storage (entry points below) |

## Two kinds of flag

| Kind | How many | Lives | Set by |
| --- | --- | --- | --- |
| World flag | 7,058 placements on the disc; up to a level's flag room plus 4 at once | While the level is loaded | Level scripts, and two of the game's own per level |
| Script flag | 128, numbered 1 to 128 | In the profile record, saved to the memory card | The shared script's tag handling |

## World flags

### What a flag holds

Every placement in a script gives five values and makes one flag of its own; a name placed twice gives two flags.

| Value | What it holds | Range on the disc |
| --- | --- | --- |
| Name | A text name; the game keeps the first 15 characters | 3 to 30 characters; 445 placements use more than 15 |
| Position | x, y and z | Each written with at most one decimal place; 23 placements compute it while the level runs |
| Heading | A direction, kept as given | Whole numbers from 0 to 360 |
| Activity | What a character does at the flag (see Activity values) | 0 to 36; 0 in 4,906 placements |
| Group | A number that ties flags together (see Group values) | 0 to 97; 0 in 6,400 placements |

- A new flag is switched on. Two scripts on the disc switch flags off and on again (15 uses).
- A script gets back a number for each flag it places and keeps it to refer to that flag later; most scripts store
  it under the flag's own name.

### Where flags come from

| Source | Scripts | Placements |
| --- | ---: | ---: |
| Story and Coney level scripts `level<N>.lua` | 29 | 5,395 |
| Chapter scripts of those levels | 3 | 4 |
| Rumble arena scripts `level<N>_<mode>_init.lua` | 56 | 1,580 |
| Rumble mode scripts `kinghill.lua` and `royal.lua` | 2 | 4 |
| Scripts whose names are not known (entries 6,932, 6,933, 7,105, 10,537) | 4 | 75 |

- Every level also has two flags of the game's own, both at (0, 0, 0) with heading 0, activity 0 and group 0. They
  take two places of the level's flag room.
- `level95.lua` places the most flags (393), then `level11.lua` (388) and `level5.lua` (313).

### Flag room

Each level's scripts set the size of its flag pool: the second number of the level's pool sizes (two numbers and a
list of four). A level whose scripts give n has room for n + 4 world flags, two of them taken by the game's own
flags, so its scripts can place n + 2.

- No level on the disc places more than n. The armies stages `level60.lua` to `level64.lua` place exactly n, as do
  43 of the 56 Rumble arena scripts.
- The largest room is 644 (`level11.lua`); the Rumble arenas set 18 to 310.

### Finding flags

- A search by name compares the kept name (the first 15 characters) with the given name, letter case included. A
  name longer than 15 characters is never found.
- A search that finds nothing gives the empty flag value, the same value the scripts use for no flag.
- The 445 long names include sets that share their first 15 characters, for example `_xwFlagWgnTicket05` and
  `_xwFlagWgnTicket06` (kept as `_xwFlagWgnTicke`).
- Only three scripts search for flags by name, all of them `level95` scripts, with names built from parts.

### What scripts do with flags

Counts of script uses across all 467 scripts on the disc.

| Use | Uses | Scripts |
| --- | ---: | ---: |
| Put a character at a flag's position | 1,471 | 162 |
| Link flags into walking networks | 774 | 29 |
| Send a character to walk or run to a flag | 304 | 84 |
| Read a flag's position | 303 | 54 |
| Send a character to a flag to do its activity | 97 | 28 |
| Give a character's group a flag to move to | 69 | 28 |
| Let a gang use the activity flags of the level | 37 | 17 |
| Move a flag to a new position | 37 | 19 |
| Put a whole gang at flags | 34 | 14 |
| Ask which character is using a flag | 29 | 9 |
| Switch a flag off or on | 15 | 2 |
| Set off an explosion at a flag | 4 | 1 |
| Search for a flag by name | 3 | 3 |

- Every Rumble arena script places a list of 9 or 10 player-1 start flags (`fP1_...`) and a list for player 2
  (`fP2_...`); the level list page gives the first flag of every player-1 list.

### Activity values

The activity value of a flag, with the number of placements, the number of distinct names and two of the names. The
last column is what the names suggest; none of it was checked in game.

| Activity | Placements | Names | Example names | Suggested by the names |
| ---: | ---: | ---: | --- | --- |
| 0 | 4,906 | 2,814 | `fP1_01`, `PedNet_01` | No activity: start points and walking points |
| 1 | 64 | 54 | `_xWPiss01`, `_xwTakePiss01` | Relieving oneself |
| 4 | 124 | 119 | `_xfSit04`, `_xwBench01` | Sitting |
| 5 | 181 | 115 | `_xwFireBarrell02`, `_xfFirebarrel01` | Warming hands at a fire barrel |
| 6 | 66 | 52 | `wPhone01`, `_xwPhone02` | Using a phone |
| 7 | 191 | 174 | `fSmokeCat`, `_xwSmoke01` | Smoking |
| 8 | 303 | 193 | `_xExit04`, `PedExit_01` | Exits where people leave the area |
| 9 | 30 | 23 | `_xwLookInside01`, `_xwWindowLook01` | Looking into windows |
| 10 | 89 | 66 | `fTrash_01`, `_xwTrash01` | Going through trash |
| 11 | 11 | 11 | `_xWGirl01`, `_xwStrip20` | Calling after girls |
| 12 | 122 | 54 | `Tag_01`, `tagflag_1` | Tag (graffiti) spots |
| 13 | 193 | 68 | `_xwFlagWgnCap01`, `_xwFlagCarCap01` | Parked vehicles of four kinds (car, wagon, cop car, cup); not known |
| 14 | 41 | 20 | `storePharmacy01`, `storeJewelry` | Store fronts |
| 17 | 41 | 41 | `fBopStreet_01`, `_xWStand01` | Standing about |
| 19 | 27 | 27 | `_xfDance03`, `fDance01` | Dancing |
| 20 | 1 | 1 | `fParty_1` | A party |
| 21 | 12 | 12 | `fHideEliminator2_01`, `fHideFox` | Hiding places |
| 22 | 74 | 58 | `_xwBrowse01`, `wBrowse01` | Browsing shop windows |
| 23 | 3 | 3 | `fPlayPinball`, `_xWPinBall01` | Playing pinball |
| 25 | 1 | 1 | `fStoreFlag03` | Not known |
| 26 | 49 | 18 | `_xwFlagWgnTicket05`, `_xwFlagCarTicket01` | Tickets on parked vehicles; not known |
| 27 | 1 | 1 | `fWorld38` | Not known |
| 29 | 119 | 119 | `_xWDrink01`, `_xwDrinking01` | Drinking |
| 30 | 69 | 62 | `_xfLeanBack01`, `wLeanBack01` | Leaning back against a wall |
| 31 | 3 | 3 | `_xfLeanLeft02` | Leaning to the left |
| 32 | 2 | 2 | `_xfLeanLeft01`, `fPinball103` | Not known |
| 33 | 323 | 77 | `_xfHideZone01` | Hide zones |
| 34 | 2 | 2 | `fDrinkStart01` | Drinking |
| 35 | 5 | 2 | `_xwReadMagazine` | Reading a magazine |
| 36 | 5 | 2 | `_xwLeanCounter` | Leaning on a counter |

Activity values 2, 3, 15, 16, 18, 24 and 28 appear on no flag.

### Group values

| Groups | Placements | Flags that carry them |
| --- | ---: | --- |
| 0 | 6,400 | Most flags |
| 1 to 5 | 405 | Mostly walking network flags (`PedNet_...`, `fBumNet_...`, `fCopPoint...`, `SewerNet...`) |
| 7 to 9 | 9 | Network flags `CNet...`, one phone flag and one `fWorld` flag |
| 24 to 45 | 107 | Store flags (`storeJewelry`, `storePharmacy`, `storeFront` and others) and some network flags |
| 64 | 102 | Dealer, flash and paint flags such as `fDealNet...` and `fFlash...` |
| 65, 68, 97 | 35 | Network flags (`PedNet_...`, `BumNetwork...`, `fAptAreaNet...`) |

## Script flags

### Script flag storage

| Property | Value |
| --- | --- |
| Count | 128 flags, numbered 1 to 128 |
| Place in the profile record | Offset `0x4F4`, four 32-bit words (see save-data) |
| Numbering | Flag n is bit n − 1 of the set |
| Setting a flag | Takes the flag number and an on or off value; with no value the flag is switched off |
| Testing a flag | Gives whether flag n is on |
| New profile | All 128 off |
| Saved | With every autosave, as part of the profile record |

The scripts also keep numbers beside the flags in numbered slots; slots 1 to 5 are used, by 25 scripts. These
numbers are not part of the profile record.

### Script flags in use

All script flag uses on the disc are in WAD entry 4,105, the shared script, and all of them track tags (graffiti
spots). A table in that script gives each tagging level its number of tags and a first flag. A tagging level's tags
are recorded as one flag each, in the range below. Flags 101 to 115 hold the tags of the level being played, in the
level's own order.

| Level | Title | Tags | Script flags |
| --- | --- | ---: | --- |
| `level2` | Real Heavy Rep | 5 | 1 to 5 |
| `level92` | The Best | 5 | 6 to 10 |
| `level5` | Adios Amigo | 7 | 11 to 17 |
| `level9` | All-City | 4 | 18 to 21 |
| `level11` | Sharp Dressed Man | 10 | 22 to 31 |
| `level14` | Set Up | 9 | 32 to 40 |
| `level20` | Scout's Honor | 10 | 41 to 50 |
| `level31` | Boys In Blue | 2 | 51 to 52 |
| `level34` | Blackout | 7 | 53 to 59 |
| `level51` | Desperate Dudes | 10 | 60 to 69 |
| `level52` | No Permits, No Parley | 6 | 70 to 75 |
| `level87` | Payback | 15 | 76 to 90 |

- 90 tags in all. Flags 91 to 100 and 116 to 128 are used by no script on the disc.
- The flags of a tagging level are written from flags 101 to 115 when a run of that level has more of its tags done
  than the flags already show, so a level's count of recorded tags never drops (from the shared script; not seen
  in game).
- Starting a story level from its beginning switches flags 101 to 115 off (from the shared script; the exact
  starting conditions were not confirmed).
- `level2.lua` places five tag flags (`Tag_01` to `Tag_05`, activity 12), matching its five tags in the table;
  `level5.lua`, `level11.lua`, `level34.lua`, `level51.lua`, `level52.lua` and `level92.lua` also place as many
  activity-12 `Tag_...` flags as the table gives them.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x00379fd0` | Place a named world flag for a script | What a flag holds |
| `0x00415c48` | Find a world flag by its name | Finding flags |
| `0x004158f8` | Make room for a level's world flags | Flag room |
| `0x0037b8a8` | Switch one script flag on or off | Script flag storage |
| `0x0037b910` | Test one script flag | Script flag storage |

## Acceptance checks

- `WARRIORS.WAD` holds 467 compiled Lua 4.0 scripts; 94 of them place world flags, 7,058 placements in all.
- `level102_brawl_init.lua` (entry 6,930) places 25 flags and sets a flag room of 25; its first flag is `fP1_1` at
  -9.1, 8.9, -11.1 with heading 128.
- Every constant flag position on the disc has at most one decimal place, and every heading is a whole number from
  0 to 360.
- No level's scripts place more flags than the level's flag room.
- 323 flags have activity 33, all named `_xfHideZone` followed by a number.
- A new profile's record holds 16 zero bytes at `0x4F4`.
- After every tag of Real Heavy Rep is sprayed and the game autosaves, the low five bits of the byte at record
  `0x4F4` are set.
- Flags 91 to 100 and 116 to 128 stay off in every profile.

## Open questions

- What the activity and group values do in game; the meanings in the activity table come from flag names only.
- The unit of positions and the direction a heading of 0 faces.
- Which of two flags with the same name, or the same first 15 characters, a search by name finds.
- What the game's own two flags per level are used for.
- What the number slots kept beside the script flags hold between levels, since they are not saved.
- The exact conditions under which flags 101 to 115 are switched off at a level start.
- Why `level9.lua`, `level14.lua`, `level20.lua`, `level31.lua` and `level87.lua` place a different number of
  activity-12 flags from the tag table's count (23, 10, 8, 3 and 18 against 4, 9, 10, 2 and 15 tags).
- The Xbox version's scripts and save layout were not compared.
