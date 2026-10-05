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
| `+0xd0` | u16 | `AddFlag`'s fourth argument |
| `+0xd4` | u32 | **enabled**: 1 from `AddFlag`; `FlagEnable` writes it (`0x00415dc8`) |
| `+0xd8` | s32 | `AddFlag`'s fifth argument (read as 16 bits, sign-extended) |
| `+0xdc` | u32 | the **user**: who is using the flag now, `NilHandle` at first (`FlagGetOwner` reads it, `0x00416ed0`) |
| `+0xe0` | u32 | 0 |

The fourth and fifth arguments are 0 in almost every call; their readers are not traced.

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
(`0x00386488` → `0x003353f0`) returns `low + v mod (high − low + 1)` with an unsigned modulo, so the result is in
`[1, 5]` inclusive, where `v` is the next entry of a fixed table of 1,024 32-bit numbers at `0x005117e0`, its index
(`0x006eb880`) advanced by 1 and masked to 10 bits on every draw (`0x00335390`). The index starts at 0 (a static
initialiser, `0x00335608`, zeroes it with nine neighbouring generator indices) and is never seeded; it is shared by
`random` and many C++ callers (the character set-up `0x00218008` among them), so which door depends on how many draws
the session has made so far. Confirmed (code) for the generator; that the draw count varies between visits is
inferred.

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

## Open questions

- The meaning of `AddFlag`'s fourth and fifth arguments (`+0xd0`, `+0xd8`) and of `+0xcc` and `+0xe0`.
- What gives a flag a parent (`Flag_New`'s last argument is always `NilHandle` through `Flag_Add`; another writer of
  `+0x48` is not traced) and what sets the user `+0xdc`.
- The vtable slots of the flag (`0x00545e68`) beyond the handle and name getters.
