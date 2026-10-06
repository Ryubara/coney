# Inventory, unlockables and statistics

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), static analysis only
(Ghidra, and the disc's compiled scripts read with `coney-tools`' Lua walker). No runtime claims.

## Purpose

What the game keeps about a player besides the human: the **inventory** (items, money, flashes), the **unlockables**
that the story opens (levels, Rumble content, upgrades, hints) and the **statistics** that score a mission. The ids
are listed in [Inventory items](../references/inventory.md), [Unlockables](../references/unlockables.md) and
[Statistics](../references/statistics.md); this page says how the game uses them.

## Original structure

`W_UnlockManager.cpp` holds the unlockables manager ([Source map](source-map.md)); the end-of-mission screen is
`GUI/GameStats.cpp` (the path is a string at `0x00559f70`). Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0041e250` | `Inventory_SetItem` | `CfgInventoryItem`'s writer, for both players | confirmed (code) |
| `0x0041e420` | `Inventory_Count(inv, player, item)` | an item's count; 0 outside players 0-1 and items 0-22 | confirmed (code) |
| `0x00423718` | `Unlocks_SetRecord` | `UM_SetUnlockable`'s writer | confirmed (code) |
| `0x00424130` | `Unlocks_IsDataUnlocked(mgr, type, data)` | the code's test, as `UM_IsDataUnlocked` | confirmed (code) |
| `0x004211d8` | `Stats_CategoryPoints(stats, group)` | the sum of counts × points of a category | confirmed (code) |
| `0x00422a90` | `Stats_CategoryScore` | the same, harmony inverted | confirmed (code) |
| `0x00422b00` | `Stats_CategoryPercent` | a score as a percentage of its maximum, at most 100 | confirmed (code) |

## Data

### The inventory {#inventory}

Two blocks of `0x3f4` bytes at game state `+0x480`, one per player; item *i* is the 0x2c-byte entry at block
`+0x04 + i × 0x2c`: the object name (31 characters), the count (`+0x20`), the pickup sound's hash (`+0x24`) and a
duration in ms (`+0x28`). There are 23 items, ids 0-22. Confirmed (code) at `0x0041e250` and `0x0041e420`.

The code uses five of them by number: 1 the flash (a revive), 2 money, 3 spray-paint charges, 5 handcuffs and
6 handcuff keys, and 10 for stolen loot (the pickup and drop code, `0x00232c60`, `0x002334d0`, `0x0023bf00`;
confirmed (code)). A human's **pocket** is one item id (`+0x250`) and a count (`+0x254`), set by `HuPutItemInPocket`
(`0x00238190`; confirmed (code)); that it holds the same ids is inferred from the scripts' calls.

### Unlockables {#unlockables}

The manager (`0x006fe998`) holds up to 640 records of 12 bytes: `+0` level, `+1` group, `+2` item, `+3` type, `+4`
a 16-bit extra, `+8` a 32-bit data id; a record is unlocked when its bit in the set at `0x006fe8f8` is clear.
`global.lua` fills the table from its `Unlockables` list. `UM_Unlock(level, group, item)` unlocks every record with
those bytes; completing a level unlocks its `(level, 0, 0)` records. The game tests a record by type and data, the
first match deciding. Confirmed (code) for the layout and the tests.

What each type's data is:

| Type | Data | Tested by | Evidence |
| --- | --- | --- | --- |
| 0 | the next story level | scripts (`runNextMission`) | inferred |
| 1 / 2 / 3 / 4 | a Rumble mode / arena / gang / character type | the Rumble menus (`0x001f8168`, `0x001eaa6c`, `0x001ec9bc`, `0x001ec490`) | confirmed (code) |
| 5 | a clubhouse display: `level95` switches object zones by it | scripts | inferred |
| 6 | an upgrade or a hub feature (below) | the code and the hub scripts | confirmed (code) |
| 7 | a flashback mission's level | the hub's markers | inferred |
| 8 | a Coney Island shop's stock | `level95_coney`'s shops | inferred |
| 9 | an Armies of the Night level (60-64) | the hub | inferred |
| 10 | a workout move | `level95_workout` | inferred |
| 11 | a hint, shown once its record is unlocked (`0x001cea38`, then `0x001ce1e8`) | the code | confirmed (code) |

No engine code reads `extra`; only `UM_GetRecordData` returns it. A counter at `0x004245d8` counts the unlocked
records of types 1-4 and 6 only (a completion count, inferred).

Type 6, by data id (confirmed (code) at the tests cited; ids 1, 2 and 16-28 are tested only by scripts, 3 and 4 by
the hub, [World flags](flags.md)):

| Id | Unlocks | Id | Unlocks |
| --- | --- | --- | --- |
| 5 | brass knuckles: punches deal `+byte 0 %` damage (`0x0021b518`) | 11 | the power meter's maximum `× (1 + byte 2 %)` (`0x00223068`) |
| 6 | steel toe caps: kicks deal `+byte 1 %` damage (`0x0021b570`) | 12 | the stamina maximum `× (1 + byte 3 %)` (`0x00223188`) |
| 7 | one more flash carried, 3 to 4, and sold by dealers (`0x0041df10`) | 13 | knocked-out cops drop handcuff keys (`0x00233260`) |
| 8 | a flash at full health fills rage and starts it (`0x00284830`) | 14 | knocked-out humans may drop power cuffs, 51 % (`0x00232ed4`) |
| 9 | paying a bum $1 marks every dealer on the radar (`0x002acb60`) | 15 | a cuffed player can spend a key to free himself (`0x00284488`) |
| 10 | after four $1 payments a bum takes $10 and joins the gang (`0x002ac728`; joining inferred) | | |

The bytes are `CfgWarriorUpgrade`'s four (`0x006b6650`; 10, 20, 10 and 20 in `config_preload2.lua`).

### Statistics {#statistics}

The stats object (`0x006fe490`) keeps, per player, a counter per event in six **categories**, which are `StatAdd`'s
groups and `CfgSetStatValue`'s tables: 0 mission, 1 bonus, 2 style, 3 combat, 4 crime, 5 harmony. A category's
score is the sum of each event's count times its points (`0x004211d8`); harmony's is its maximum minus that sum
(`0x00422a90`), so harmony events are penalties. The screen shows each score as a percentage of the category's
maximum, capped at 100 (`0x00422b00`). `CfgSetStatTypeMax(combat, crime, harmony, style, mission, bonus)` stores
the six maxima at `0x00715840` in category order. Confirmed (code).

The points tables (16-bit) are at `0x005971b8` (3 events), `0x005971c0` (4), `0x00715510` (13), `0x00715500` (8),
`0x00715818` (12) and `0x00715830` (5). The engine adds through one function per category (`0x004ed908`,
`0x004ed888`, `0x004ed988`, `0x004ed8c8`, `0x004ed948`, `0x004de250`); `0x004f39e8` adds a crime event to every
player, which AI Warriors led by a player use. What each event counts is in
[Statistics](../references/statistics.md#event), read from those callers (confirmed (code)): combat events by the
anim id that lands, unblocked, on a human not down; crimes by victim (civilian, bum or dealer; cop; gang member);
harmony by Warriors cuffed or down (every 2 s, the number still cuffed or down, from `0x00166f54`).

## Open questions

- What category 13 is, which the mission event 0/1 requires of the gang member knocked out.
- What world `+0x158` is: it gates event 0/1 and upgrade (6, 14).
- What a hat's byte `+0x10b` (1 or 2) separates in style events 2/1 and 2/2.
- How `global.lua`'s `SetupMissionPoints` turns a level's mission and bonus values into maxima.
- Whether scripts other than the four found set events 1/0-1/2 and 2/4.
