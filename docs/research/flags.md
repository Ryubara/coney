# World flags

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), static analysis only
(Ghidra, and the disc's compiled scripts read with `coney-tools`' Lua walker). No runtime claims.

## Purpose

A **flag** is a named point with a facing that a level script places in the world: a spawn point, a waypoint, the
end of a walk, an objective. The scripts create thousands of them (`AddFlag` is called 7,058 times in 94 chunks) and
use them wherever a place is needed: `HuCreate` at a flag's position, `TeleportToFlag`, AI goals, paths and cameras.
Player starts that are not literal numbers in a `HuCreate` are flags: the hub's Warchief (`fWchiefStart_<n>`) and
every Rumble arena's players (`fP1`, `fP2`).

In one paragraph: flags live in a pool the level script sizes with `CfgSetDatabaseSizes`; `AddFlag(name, {x, y, z},
heading, a, b)` takes the next free slot, stores the name (15 characters), the position, the heading in degrees and
two small integers, registers the flag as a world object and returns its **handle**. Scripts keep the handles in Lua
tables (`fWchiefStart[1]`, `fP1[1]`); `FindFlag(name)` finds one by name. A flag's position and heading are its own,
unless it has a **parent** object, whose current position and facing it then follows. Nothing in the `.lev` file
makes flags: besides the scripts' flags, `InitLevel` adds exactly two of its own (`CrimeScene` and `GangCall`). The
pool, and every flag, is freed when the level unloads.

## Original structure

`WorldObjects/flags.cpp` (`0x004158f8`-`0x00417ad0`, [Source map](source-map.md); the source path and the pool name
`WorldFlag` are strings at `0x00589508` and `0x005894f8`). Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x004158f8` | `FlagPool_Create(n)` | allocates the pool of `n + 2` flags and the slot array | confirmed (code) |
| `0x00415a38` | `FlagPool_Destroy` | frees both (from `UnloadLevel`, `0x001607b8`) | confirmed (code) |
| `0x00415af0` | `FlagPool_Take` | the first empty slot; counts it | confirmed (code) |
| `0x00415b68` | `Flag_New(heading, name, pos, a, b, parent)` | takes a slot and constructs the flag in it | confirmed (code) |
| `0x00415c18` | `Flag_Add` | `Flag_New` with no parent (`NilHandle`) | confirmed (code) |
| `0x00415e70` | `Flag_Construct` | fills the record (below) and registers the handle | confirmed (code) |
| `0x00415c48` | `Flag_FindByName` | linear search by name, `strcmp` (`0x00430c4c`) | confirmed (code) |
| `0x004161a8` | `Flag_GetParent` | `+0x48` | confirmed (code) |
| `0x004161e8` | `Flag_Position` | the parent's position when the parent exists, else `+0x10` | confirmed (code) |
| `0x00416258` | `Flag_Heading` | the parent's heading when the parent exists, else `+0x44` | confirmed (code) |
| `0x00415dc8` | `Flag_SetEnabled` | writes `+0xd4` (for `FlagEnable`, `0x00415ce8`) | confirmed (code) |
| `0x00417a60` | `AsFlag(object)` | the object when its type bits (vtable `+0x24`) have `0x80`, else null | confirmed (code) |
| `0x00416bb8` | `Flag_GetPosition(handle)` | resolves the handle (`0x00390288`), then `Flag_Position` | confirmed (code) |

The script side: `AddFlag` (wrapper `0x00379fd0`), `GetFlagPos` (`0x0037a288`), `FindFlag` (`0x0037a770`),
`SetFlagPos`, `TeleportToFlag` (`0x0036cdc0` → `0x00385db0`) and the `global.lua` helper `FlagPos`; the full list is
in the [script bindings](../references/bindings/index.md).

The rest of `flags.cpp` and `WorldPath.cpp`, confirmed (code) at each address; names are ours:

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00415d30` / `0x00415dd0` | `FlagPool_Count` / `FlagPool_Get(i)` | the number of flags made (`0x00514858`); slot `i`'s record | confirmed (code) |
| `0x00416e68` | `Flags_ForEach(fn, arg)` | calls `fn(flag, arg)` for every flag made (one caller, the AI at `0x002e723c`) | confirmed (code) |
| `0x00415de8` | `Flag_FindIndexByName` | the slot index of the first flag with that name, else −1 | confirmed (code) |
| `0x00415fa0` | `Flag_Destruct` | unregisters the handle (`0x00390038`), clears the script handler (`+0x4c`), frees when asked | confirmed (code) |
| `0x00416010` / `0x00416250` | `Flag_SetName` / `Flag_SetHeading` | the name at `+0x34` (15 characters) / the heading `+0x44` | confirmed (code) |
| `0x004161b0` | `Flag_SetUser` | writes the user `+0xdc` (six callers in the AI and `0x00238ef0`) | confirmed (code) |
| `0x004162d0` | `Flag_ClipCount` | the number of clip names of the flag's activity (`0x00589678`), 0 above 36 | confirmed (code) |
| `0x00415d40` / `0x00415d90` | `Flag_RegisterContext` / `Flag_UnregisterContext` | vtable slots: register the flag as a context action once (handle at `+0xe0`) / unregister it | confirmed (code) |
| `0x00416038` | `Flag_OnMessage` | vtable slot: hands a message to the flag's script handler; for message 8 (a human reached it) runs `0x00416b18` and calls the Lua handler with (flag, human; the arguments inferred) | confirmed (code) |
| `0x00416dc0` / `0x00416e10` | `Vec_HeadingXY` / `Vec_HeadingYX` | the heading from one point to another, `atan2(dy, dx)` / `atan2(dx, dy)` | confirmed (code) |
| `0x004176d8` | `Flags_NearestUsable(human, pos, dist)` | the nearest enabled flag with no user that `0x00416718` admits the human to; the squared plan distance to `dist` | confirmed (code) |
| `0x00417670` | `Human_NearestUsableFlag` | the same from the human's position, only when its brain's byte `+0x2d1` is set | confirmed (code) |
| `0x004177d8` | `Flag_FindNearestByActivity(pos, activity, dist, skip, reach)` | the nearest enabled flag of an activity other than `skip`, straight-line distance; with `reach` set only one `Nav_CanReach` reaches; squared distance to `dist` ([AI](ai.md) uses it for exits) | confirmed (code) |
| `0x00416b68` / `0x004161d8` | `Flag_SetPosition(handle, pos)` / `Flag_SetPositionRaw` | resolves the handle and writes `+0x10` with `w` = 1 / writes `+0x10` as given | confirmed (code) |
| `0x004161b8` / `0x00416ed0` | `Flag_ReleaseUser(flag, h)` / `Flag_GetOwner(handle)` | clears the user to `NilHandle` only if it is `h` / `FlagGetOwner`: the user, `NilHandle` for a non-flag | confirmed (code) |
| `0x004171e8` | `Flags_FarthestOfActivity` | the farthest enabled flag of an activity from an object | confirmed (code) |
| `0x00417910` | `Flags_NearestBeyond(r, ...)` | the nearest enabled flag of an activity more than `r` from a point and within 90° of a direction | confirmed (code) |
| `0x00416be0` | `Flags_GangCall(gang, pos)` | `GangCallForHelp`: unless one is pending (game state `+0x3ec` ≠ −1), stores the gang's number and the time (`+0x3f0`) and moves the `GangCall` flag to `pos` | confirmed (code) |
| `0x00416cb0` / `0x00416c60` | `DebugText_Add` / `DebugText_Reset` | 32 timed debug strings of 64 characters at `0x006fd978` (expiry `0x006fd8f8`); only written (one caller, `0x0016d20c`) | confirmed (code) |
| `0x00417aa8` / `0x00417ad0` | static initialiser / stub | sets `0x006fd8f0` to `NilHandle` | confirmed (code) |
| `0x00415690` / `0x004156d0` | `WorldPath_Construct` / `WorldPath_Destruct` | a 0x38-byte path: name `+0x00` (16), eight flag pointers `+0x10`, slot `+0x30`, count `+0x32`, `+0x34` | confirmed (code) |
| `0x004156f8` / `0x00415720` | `WorldPath_SetName` / `Path_AddPoint` | the name; appends a flag | confirmed (code) |
| `0x00415740` / `0x00415878` | `Path_Add` / `Path_FindByName` | [`AddPath`](../references/bindings/world.md#addpath): the first of 32 slots at `0x006fd870`; by name | confirmed (code) |

## Data

### The pool {#pool}

`CfgSetDatabaseSizes(objectTasks, flags, {boxes...})` (`0x0041d628`) calls `FlagPool_Create(flags + 2)`, which adds 2
again: the pool holds **`flags + 4`** records of **0xf0 bytes** (allocated as `WorldFlag`, aligned to 16), and a
zeroed array of as many slot pointers. Globals, confirmed (code) at `0x004158f8` and `0x00415af0`:

| Address | Meaning |
| --- | --- |
| `0x00514854` | number of records |
| `0x00514858` | number of flags made (only ever counts up) |
| `0x0051485c` | the records |
| `0x00514860` | the slot array: slot `i` holds the address of record `i` once it is used, else 0 |

`FlagPool_Take` scans the slot array for the first 0, points it at record `i` and counts it. **A full pool is not
checked:** `Flag_New` then calls the constructor of a null pointer (confirmed (code) at `0x00415b68`); the scripts
size the pool to fit. Nothing frees a single flag: the slot array fills from 0 upwards, which is why
`Flag_FindByName` can walk slots `0` to `count - 1` (inferred from the absence of any other writer of the count).

### The flag record (0xf0 bytes) {#record}

Written by `Flag_Construct` (`0x00415e70`), confirmed (code); the meanings are ours.

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | ptr | vtable `0x00545e68` (slot `+0x2c` returns the handle, slot `+0x14` the name, inferred) |
| `+0x10` | f32 × 4 | position `x, y, z` from `AddFlag`, then `w` = 1.0 |
| `+0x20` | f32 × 4 | a rotation, set to the identity quaternion at `0x00511790` and not changed by `AddFlag` |
| `+0x30` | u32 | the flag's **handle** (`0x0038fef8` / `0x0038ffd0`: the world objects' handle table) |
| `+0x34` | char[16] | the **name**: at most 15 characters copied (`0x00431378`), `+0x43` = 0 |
| `+0x44` | f32 | the **heading** in degrees, as `AddFlag` was given it |
| `+0x48` | u32 | the **parent** handle: an object the flag follows (`NilHandle` from `AddFlag`) |
| `+0x4c` | | an embedded list node (vtable `0x00544b98`), linked to the flag (`0x00384a10`) |
| `+0xcc` | u32 | 0 |
| `+0xd0` | u16 | `AddFlag`'s fourth argument: the **activity** ([Activities](#activities)) |
| `+0xd4` | u32 | **enabled**: 1 from `AddFlag`; `FlagEnable` writes it (`0x00415dc8`) |
| `+0xd8` | s32 | `AddFlag`'s fifth argument (read as 16 bits, sign-extended): the **group** ([Groups](#groups)) |
| `+0xdc` | u32 | the **user**: who is using the flag now, `NilHandle` at first (`FlagGetOwner` reads it, `0x00416ed0`; confirmed (code)) |
| `+0xe0` | u32 | 0 |

The fourth and fifth arguments are 0 in 4,340 of the 7,057 calls. Every flag the scripts add, with both, is in
[World flags](../references/flags.md).

### Activities (`+0xd0`) {#activities}

The fourth argument says what an ambient human does at the flag. Confirmed (code):

- `0x00416f08` returns the nearest enabled flag with a given activity; for activity 8 only one that `0x0028ff38`
  accepts for the human's brain. `0x00417028` and `0x004172c8` pick the nearest and the farthest flag of an activity
  within 89 degrees of a direction. Confirmed (code).
- `0x00416718` says whether a human may use the flag: the flag must be enabled (`+0xd4`), and each activity admits
  some role categories (the character type's byte `+0x11b`, [Characters](characters.md)), some only while
  `0x00226ff0` returns 0 (not traced), and activities 35 and 36 only a brain of kind 6. Activities 0, 3, 8, 12, 14,
  15, 18 and 33 admit no one through it. Confirmed (code).
- `0x004162f8` picks the clip: the table at `0x005896a0` holds three clip names of 0x1a bytes per activity (37 rows
  of 0x4e; idle, enter, exit where there are three), with the count of names per activity at `0x00589678`; activities
  4, 7 (for a woman, byte `+0x14b` of the type), 17, 19, 22 and 34 choose among variants instead. Confirmed (code).
- `0x00416b18`: a human that reaches an activity-8 flag is removed (`0x002271c0`); these are the exits. Confirmed (code).
- `0x00416530`, `0x00416690`: per-activity sound and timing tables at `0x0058a310` and `0x0058a1e8` (8 bytes per
  activity), confirmed (code); their meaning is not traced.

The activities by their first clip, and the flag names that carry each (names inferred from the scripts):

| Activity | First clip | Flags named | Activity | First clip | Flags named |
| --- | --- | --- | --- | --- | --- |
| 1 | `gen_piss` | `_xwTakePiss` | 20 | `gen_disco_dj` | `fParty` |
| 2 | `buy_object` | | 21 | `gen_hiding_idle` | `fHide...` |
| 3 | `wreck_idle` | | 22 | `browse_store01` | `_xwBrowse` |
| 4 | `sit_chr_lng_C` | `_xwChairK`, `_xwBench` | 23 | `play_pinball` | `fPlayPinball` |
| 5 | `warmhands_idle` | `_xwFireBarrell` | 24 | `pick_lock` | |
| 6 | `phone_idle` | `_xwPhone` | 25 | `market_vendor01` | `fStoreFlag` |
| 7 | `smoke_idle` | `_xwSmoke` | 26 | `write_on_pad_C` | `_xwFlagCarTicket` |
| 8 | none: exit | `PedExit`, `_xExit` | 27, 28 | `laundry_wash`, `laundry_dry` | |
| 9 | `tv_idle` | `_xwWindowLook` | 29 | `gen_beer_idle02` | `_xWDrink` |
| 10 | `trash_idle` | `_xwTrash` | 30-32 | `lean_back_C`, `lean_l_loop`, `lean_r_loop` | `_xwLeanBack` |
| 11 | `gen_whistle` | `xfWistleGirl` | 33 | none | `_xfHideZone` (323) |
| 12 | none | `Tag` (spray-tag spots) | 34 | `siton_ledge2_C` | `fDrinkStart` |
| 13 | `hubcap_idle` | `_xwFlagCarCap` | 35 | `read_mag_stand_idle` | `_xwReadMagazine` |
| 14 | none | `storeJewelry`, `storeFront` | 36 | `lean_counter_C` | `_xwLeanCounter` |
| 15, 18 | none | | 16 | `speech_idle` | |
| 17 | `fidget_crossarms` | `_xWStand` | 19 | `dance_male001` | `fDance` |

### Groups (`+0xd8`) {#groups}

The fifth argument is a number other code compares. One reader is traced, confirmed (code): when a crime of type 1 is
reported (`0x0041b8b0`), the game finds the nearest activity-14 flag (a store front) within 10 m, marks it used (bit
16 of `+0xd8`, with a gang number in bits 18-22) and switches off (`0x00417540`, `FlagEnable`'s writer) every flag
of activity 2 or 22 within 10 m whose group equals the store's low byte: the store's buyers and browsers leave. On
store flags the group is the store's number (24-45). On flags of activity 0 it looks like a bit set of path networks,
from the names (inferred): 1 `PedNet` and `fPedStrip`, 2 `fCopPoint` and `fCopNet`, 4 `fBumNet` and `SewerNet`, 64
`fDealNet`, and sums of them; that reader is not traced.

## Behaviour

### Where flags come from {#sources}

- **The level scripts.** Each level's `AddFlagsBoxesPaths` (in `<level>.lua`, or in a mode's
  `level<N>_<mode>_init.lua` for the Rumble arenas) calls `CfgSetDatabaseSizes` first, then `AddFlag` for every flag
  and keeps the handles in globals and tables, for example `fWchiefStart = {}` then
  `fWchiefStart[1] = AddFlag("fWchiefStart_1", {-188.6, 95, -194.3}, 89, 0, 0)` (inferred from the disassembly of
  `level95.lua`). The table index and the number in the name agree in the hub, but scripts index by the table, not by
  the name.
- **`InitLevel`** (`0x0015fe90`, step 7 on [Level loading](level-loading.md#initlevel)) adds `CrimeScene` and
  `GangCall` at the origin with heading 0, after the level script (confirmed (code) at `0x0016036c`, `0x0016038c`;
  the two `+2`s of the pool size make room for them, inferred).
- **Nothing else.** `Flag_Add` has these three callers only, and `Flag_New` only `Flag_Add` (confirmed (code)); no
  `.lev` chunk holds flags ([Level loading](level-loading.md#the-level-file)).

### Making a flag: `AddFlag` {#addflag}

`AddFlag(name, {x, y, z}, heading, a, b)` reads the name, the heading as a float, `a` and `b` as 16-bit integers and
the three numbers of the table as floats, calls `Flag_Add`, pushes the handle and writes the three numbers back into
the table unchanged (confirmed (code) at `0x00379fd0`). The flag is enabled (`+0xd4` = 1), with no parent and no user.

### Finding a flag by name: `FindFlag` {#findflag}

`Flag_FindByName` compares the name with each made flag's stored name with `strcmp` (case-sensitive) and returns the
handle of the **first** match, or `NilHandle` (confirmed (code) at `0x00415c48`). The stored name is cut to 15
characters, so a longer name given to `FindFlag` never matches (inferred from the two). Only three scripts call
`FindFlag`; everything else uses the handles `AddFlag` returned.

### Position and heading {#position}

- **Position:** `Flag_Position` (`0x004161e8`) returns the parent's position (its vtable slot `+0xac`) when the
  parent handle resolves to a live object, else the flag's own `+0x10`. `GetFlagPos(flag)` returns it as an `M_Vector4`
  user type (fields `x y z w`, the class `tolua` registers), confirmed (code) at `0x0037a288`.
- **`FlagPos(flag)`** is not a binding: `global.lua` defines it as `GetFlagPos(flag)` copied into a new table
  `{p.x, p.y, p.z}`, the shape `HuCreate` and `Teleport` take (inferred from the disassembly of `global.lua`).
- **Heading:** `Flag_Heading` (`0x00416258`) returns the parent's heading (from its transform, `0x00335f48`) when the
  parent exists, else `+0x44`, in **degrees**.
- **`TeleportToFlag(object, flag, heading)`** (`0x00385db0`): heading −1 takes the flag's heading, any other value is
  used as whole degrees; the rotation is about `z` (degrees × 0.0174533, `0x00335ea0`); the position is the flag's
  (`Flag_Position`), set through the object's vtable slot `+0x6c`; a human also gets its vtable slot `+0x14c` (its
  movement reset). There is **no ground snap**, unlike `HuCreate` ([Characters](characters.md#creation)). Confirmed
  (code).

### Player starts at flags {#player-starts}

**The hub (`level95`).** `AddWarchief` creates the player with `HuCreate` at `FlagPos(fWchiefStart[1])`, heading
222, as the type the chapter's `WarchiefTable` names; `HuCreate` snaps it to the ground. `StartLevel` (inferred from
the disassembly of `level95.lua`):

1. if unlockable `(6, 4)` (the tutorial) is still locked, sets `WCLoc = 5` and arms the tutorial's message handler;
2. if unlockable `(6, 3)` is unlocked or `LoadLight` is set, opens the quick map (`DoQuickMap`) and stops here;
3. otherwise prepares the **door walk** (`PrepareDoorWalk(2, WCLoc, FinalStartLevel)`: `WalkTable.start =
   fWchiefStart[WCLoc]`, `WalkTable.term = fWchiefEnd[WCLoc]`, the door from the environment's door list) and
   schedules `StartTheWalk` 100 ms later, which calls `TeleportToFlag(player, WalkTable.start, -1)`: the Warchief
   stands on the start flag, facing the flag's heading, and walks to the end flag.

`WCLoc` is set once by the main chunk: **`random(1, 5)`**. The claim "chosen at random" holds in this sense: `random`
(`0x00386488` → `0x003353f0`) returns `low + v mod (high − low + 1)` with an unsigned modulo, so the result is in `[1,
5]` inclusive, where `v` is the next entry of a fixed table of 1,024 32-bit numbers at `0x005117e0`, its index
(`0x006eb880`) advanced by 1 and masked to 10 bits on every draw (`0x00335390`) **before** the entry is read, so the
first draw of a session reads entry 1, not entry 0; `0x003353f0` divides unsigned, with a trap (`break 7`, at
`0x003353b8`) for a zero span. The index starts at 0 (a static initialiser, `0x00335608`, zeroes it with nine
neighbouring generator indices) and is never seeded; it is shared by `random` and many C++ callers (the character set-up
`0x00218008` among them), so which door depends on how many draws the session has made so far. Confirmed (code) for the
generator; that the draw count varies between visits is inferred.

The five start flags (from `AddFlagsBoxesPaths`): 1 (−188.6, 95, −194.3) 89°; 2 (−188.6, 102.7, −197.5) 89°; 3
(−163.7, 80.7, −197.5) 358°; 4 (−174.4, 80.5, −194.3) 358°; 5 (−185.2, 112.7, −193.7) 182°.

**A Rumble arena** (`level101`-`level137`; how the main menu's QUICK RUMBLE gets there:
[Front end](frontend.md#quick-rumble)). `ConfigRumble` (in each arena's `level<N>.lua`) reads the menu's choices
with `GetRumbleModeData` into `RM_LuaData` (`ParseLuaData`: `Rumble.gameMode` from index 1, `Rumble.gameType` from
2, `Rumble.gangSize` from 3, ...), then runs `doFile("level" .. Level .. "_" .. RumbleInfo[Rumble.gameType] ..
"_init")`, whose `AddFlagsBoxesPaths` sizes the pool and adds the mode's flags, among them the lists `fP1` and `fP2`,
then `doFile(RumbleInfo[Rumble.gameType])` (the mode's rules, `brawl.lua` ...). Inferred from the disassembly.
Player 1 is made by the mode's `AddRumbleGang1`: `HuCreate("P11", Rumble.gang1[1], FlagPos(fP1[1]), 270, nil, 1,
gang, true)`, then `TeleportToFlag(P11, fP1[1], -1)`: the player stands exactly on the flag (no snap after the
teleport) and faces the flag's heading. `AddDummyPlayer` first creates a stand-in human (type 352) at
`FlagPos(fP1[1])` for the camera (inferred: `AddDummyCamera` follows it). The values per arena and mode are
in [Level starts](../references/level-starts.md). The players are placed only when the level has finished loading:
`InitLevel` calls the start callback `DoRules`, which calls the mode's `StartRumble` and so `AddRumbleGang1`.

## Coney's implementation

Written from this page, [Scripts](scripting.md#errors-in-a-fresh-state) and
[Characters](characters.md#type-to-model) (2026-10-05):

- **The flags** (`src/world_objects/flags.h`, `WorldFlags`): `createPool(n)` makes room for `n + 4`, `add` keeps a
  flag's handle, name (cut to 15 characters), position, heading, parent, user, enabled flag and the two integers in
  creation order, `findByName` is case-sensitive and returns the first match, and `position` and `headingDegrees`
  follow a live parent.
- **The bindings** (`src/scripting/level_bindings.h`): `AddFlag` takes its handle from the counter every other world
  object's handle comes from, so a flag and a human never share one; `FindFlag` returns 0 for no flag; `GetFlagPos`
  returns a table with `x`, `y`, `z` and `w` (1); `TeleportToFlag(object, flag, heading)` puts a human on the flag,
  with the flag's heading for -1 or no heading and the whole degrees given otherwise; `CfgSetDatabaseSizes` sizes the
  pool. `GetPosition` answers for a human or a flag.
- **The game's random numbers** (`src/core/game_random.h`, `GameRandom`, one per game state, shared by every Lua
  state): the 1,024-entry table is read at run time from the player's own executable (`SLUS_212.15`, `0x005117e0`,
  through its ELF program headers, `src/fileio/executable.h`); it is never stored in the repository.
- **The starts**: `InitLevel`'s step 7 adds the `CrimeScene` and `GangCall` flags after the level script, and step 13
  calls the start callback the script set (`SetStartGameCallback`), once. Gameplay watches player 1's teleports and
  moves the player there; the play mode starts a teleported player on the flag without snapping him to the ground.
- **Disc check (NTSC-U, 2026-10-05, positions and counts only):** `coney_tests "[disc][story]"`: at `level95`
  checkpoint 1 the Warchief (type 1, Cleon) is made at `fWchiefStart_1` facing 222 and teleported to
  `fWchiefStart_5` (-185.2, 112.7, -193.7) facing 182, with 395 flags; with the Rumble menu's default set-up P11
  (type 91, a Baseball Fury) stands on `fP1[1]` of `level102` (-9.1, 8.9, -11.1) facing 128 and of `level103`
  (-66.4, 23.2, 0.3) facing 95, with no script error; the table is 1,024 entries, all distinct.

Coney's choices, where the page is silent or Coney differs:

- A pool that is full grows (and counts the overflow) instead of refusing the flag. A second `CfgSetDatabaseSizes`
  starts a new pool: the old flags keep their handles but `FindFlag` no longer finds them by name.
- `GetFlagPos` of a handle that names no flag returns nil; `TeleportToFlag` with anything but a human and a flag does
  nothing.
- A draw advances the shared index and then reads the entry (the page gives the index and the mask, not the order).
  Without the disc's executable a fixed xorshift generator stands in, so a test without the disc is still
  deterministic. `random(a, b)` with `b = a - 1`, a division by zero in the original, gives `a`.
- The start callback runs right after the level script, before the level loads (the original calls it at the end of
  `InitLevel`); nothing between the two reads the humans. `--play-level` then runs the scripts for one second (30
  steps) so that what the start callback schedules has happened before the player is placed.
- The hub's door walk after the teleport (`fWchiefWalk`) is not played: the Warchief stands on the door's flag.
- The Rumble's other humans are kept but not drawn.

## Notes for implementers

- A flag needs: its handle (in the same handle space as the other world objects, since `TeleportToFlag`,
  `WalkingDistance` and others accept either), its name, position, heading in degrees, parent, user, enabled flag
  and the two integers.
  Keep them in creation order; `FindFlag` returns the first match.
- `GetFlagPos` must return something the scripts can index with `.x`, `.y` and `.z` (an `M_Vector4`); without it the
  `global.lua` helper `FlagPos` fails on its fifth instruction (`p.x` on nil), the error Coney meets in `level102.lua`
  (see [Scripts](scripting.md#open-questions)).
- The pool size from `CfgSetDatabaseSizes` is a capacity, not a count; a reimplementation may grow instead.
- Flags last until the level unloads.

`FlagEnable` writes a flag's enabled word (`WorldFlag::enabled`), which the nearest-exit search already reads; other
handles are ignored (2026-10-06).

## Open questions

- The reader of the group on path-network flags, the activity sound and timing tables, and `+0xcc` and `+0xe0`.
- What gives a flag a parent (`Flag_New`'s last argument is always `NilHandle` through `Flag_Add`; another writer of
  `+0x48` is not traced) and what sets the user `+0xdc`.
- The vtable slots of the flag (`0x00545e68`) beyond the handle and name getters.
