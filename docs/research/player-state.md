# Inventory, unlockables and statistics

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), static analysis only
(Ghidra, and the disc's compiled scripts read with `coney-tools`' Lua walker); the pickup callback also at runtime
(PCSX2 2.9.94, 2026-10-06, scenario `store_loot`, [Combat: breakables](combat.md#breakables)).

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
| `0x0041e5b0` | `Inventory_AddItem(inv, player, item, amount, notify)` | every count change, and the callbacks ([below](#pickup-callback)) | confirmed (code), runtime |
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

The code uses five of them by number: 1 the flash (a revive; d-pad right heals with it, or at full health with upgrade
(6, 8) fills and starts rage, [Combat](combat.md#rage)), 2 money, 3 spray-paint charges, 5 handcuffs and 6 handcuff keys
(a cuffed player with upgrade (6, 15) spends one with triangle, [Combat](combat.md#rage)), and 10 for stolen loot (the
pickup and drop code, `0x00232c60`, `0x002334d0`, `0x0023bf00`; confirmed (code)). A human's **pocket** is one item id
(`+0x250`) and a count (`+0x254`), set by `HuPutItemInPocket` (`0x00238190`; confirmed (code)); that it holds the same
ids is inferred from the scripts' calls.

### Adding an item and the callbacks {#pickup-callback}

`Inventory_AddItem(inv, player, item, amount, notify)` (`0x0041e5b0`) does nothing outside players 0-1 and items
0-22; it adds `amount` to the count, floors it at 0 and keeps it within the item's limits (the table at
`0x0058b300`, with the flash's raised by upgrade (6, 7)). A positive amount of item 3 raises the spray-paint hint
flags. Then, in this order, synchronously, after the count has changed. Confirmed (code):

1. **Item 2 (money)**, whatever `notify`: when inventory `+0x1034` names a Lua function that exists, it is called
   with **(player, amount)**.
2. **`notify` = 1** only: the `CfgInventoryCallback` function (`+0xfd4`), if it exists, is called with **(item)**, one
   argument; then the `CfgHuInventoryCallback` one (`+0xff4`) with **(player, item)**. The sign of `amount` does not
   matter; both players' changes call the same functions.

A world pick-up (`Human_PickUpObject`, `0x0023bf00`) passes `notify` = 1 for the item it gives and 0 for the money
that comes with loot. A store's jewellery gives item **10** ×1 (notify) and then its value in money (no notify)
([Combat: breakables](combat.md#breakables)). Confirmed (runtime): at the store each of three pick-ups called
`Inventory_AddItem(player 0, 10, 1, 1)` then `(0, 2, 7, 0)`.

**Mission 1's store** (`level99_lesson1.lua`): a player entering the box `vInsideStore` (message 3) runs
`P2.InsideStore`; while `objectives.Stores` is false it sends Vermin to `fVerminCar`, shows tutorial text `TT_6`, makes
a HUD counter (label `LBL_1`, value `lootCount` of 3) and sets `objectives.Stores`;
`CfgInventoryCallback("P2.UpdateLootCount")` was set before. `P2.UpdateLootCount(item)` counts only item 10: it adds 1
to `lootCount` and updates the counter; at 3 it releases the counter, clears the callback, the tutorial text, the box's
handlers (3 and 4) and the announcement, sets `objectives.Stolen` and schedules `P2.SetupCars` in 3,000 ms, which
teleports the player to `fStoreFront` for the car lesson. Confirmed (runtime): the teleport came 91 updates after the
third pick-up.

### Walking over a power-up {#walk-over}

Objects of class `powerup_item` (spray cans `dyn_spraycan`, flashes, keys, money) are taken by **touching** them, not
with triangle: the triangle search skips the class ([Combat: breakables](combat.md#breakables)). Confirmed (code) at
`0x00219d50`, `0x0023bf00`:

1. **Contact.** When a human's body touches an object (`Human_OnContact`, `0x00219d50`), a class other than
   `powerup_item` (the name at `0x0055a908`) is a solid contact. A power-up is passed through and picked up, unless it
   is `TYPE_REVIVAL` (14) and the human is a player at full health while game state `+0x56e5` is 0, or it is out of
   sight (`0x0021c570`, the same ray test as the search). Types 29 and 34 have their own cases there (not traced).
2. **The take** (`Human_PickUpObject`, `0x0023bf00`) needs flag `0x8000` ([pickable](objects.md#pickable)) and a
   brain of kind 0, 2, 3 or 4, then goes by the object's type:

   | Type | Who | Gives |
   | --- | --- | --- |
   | 13 `TYPE_KEY` | a player below the item's limit | item 6 ×1 |
   | 14 `TYPE_REVIVAL` | a player below the limit | item 1 ×1 (a flash) |
   | 16 `TYPE_SPRAYCAN` | a player below the limit (`0x0041ded0(3)`) | item **3** ×1 (one charge; never past 9) |
   | 28 `TYPE_MONEY` | a player | item 2 × the object's value (`+0x124`) |
   | 12 `TYPE_SPECIAL` | | the named mission items and loot ([Combat: breakables](combat.md#breakables)) |

   Each gift passes `notify` = 1 ([above](#pickup-callback)), plays the item's pick-up sound and removes the object
   (`0x0023be98`); the first spray can, key or flash also queues that item's hint once (game state flags `0x2000`,
   `0x40`, `0x4000`) when hints are on. Any other type, or a refused one, returns 0 and the object stays.
3. Back in the contact, a taken object's physics body is freed.

At runtime (slot 1, a `dyn_spraycan` spawned with `Obj_Spawn` 2.5 m ahead of the player, who walked at it with the
stick at 60%): `Human_PickUpObject` was called from `Human_OnContact` when the player's centre was about 1.1 m from
the can's (the two bodies touching), and `Inventory_AddItem(player 0, 3, 1, 1)` followed in the same update.
Confirmed (runtime). A spray can that is never touched stays; its update (`0x003f2870`) only spins it and shows it
within 40 m (30 m for its icon; inferred).

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

Which records are **new** (not yet seen in a menu) is a separate bit set at `0x006fe948` (`Unlockables_IsDirty` /
`Unlockables_ClearDirty`). `UM_GetUnlockablesByType` fills the unused entries of its result with 65535 and does not
check the 32-entry bound (`0x00423eb0`). Confirmed (code). No engine code reads `extra`; only `UM_GetRecordData`
returns it. A counter at `0x004245d8` counts the unlocked
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

## Coney's implementation

Written from this page, [Scripts](scripting.md#stopwatch), [AI: crimes](ai.md#crimes) and the binding pages
(2026-10-06). `GameState::player` (`src/warriors/player_state.h`) holds:

- **The inventory** (`src/warriors/inventory.h`): two players × 23 slots; `CfgInventoryItem` configures both players'
  slots; `GiveMoney`, `TakeMoney`, `InvSetMoney`, `InvGiveItem`, `InvGiveRevive`, `InvGiveSkeletonKey` and
  `InvSetSpraycanCharges` change counts, clamped at 0; revives at 3, or 4 once the upgrade (6, 7) is unlocked.
  `InvSetMoney` bumps a per-player counter the HUD can watch; the first positive spray-paint amount raises the hint
  flag. Mission complete (mode 0xb) banks each player's money into the profile.
- **The statistics** (`src/warriors/player_stats.h`): the six points tables, the maxima, per-player counters, the
  category points, scores and percentages above. `StatAdd`, `StatGetScore` and `StatResetPlayer` take a player's
  human (from the humans the scripts made); other handles do nothing.
- **The unlockables' records** (`src/warriors/unlock_records.h`): `UM_SetNumUnlockables`, `UM_SetUnlockable`,
  `UM_Reset`, `UM_Unlock`, `UM_IsLevelComplete`, `UM_IsDataUnlocked`, `UM_IsTypeDirty`, `UM_IsDataDirty` and
  `UM_GetRecordData` over the profile's locked and new bits (`SavedProgress`), so a save carries story progress.
- **The checkpoint copy**: `SetCheckPoint` copies the inventories and statistics; `restoreCheckpoint()` puts them back
  for a restart.

Coney's choices, where the page is silent:

- Items other than revives have no upper limit (the table at `0x0058b300` is not described).
- `StatGetScore` is the five scoring categories' points less harmony's, not below 0 (`0x00422998` is not traced).
- An event index past its table is ignored; `StatGetTotal` is not registered.
- `Inventory_AddItem` is `script::addInventoryItem` (`src/scripting/player_bindings.h`), with the money,
  `CfgInventoryCallback` and `CfgHuInventoryCallback` calls in the order above; `CfgMoneyCallback`
  ([config](../references/bindings/config.md#cfgmoneycallback)) sets the money callback. The give bindings keep their
  own paths (`GiveMoney` calls the money callback; the others call none), as their notify flags are not traced.
- `SetMultiplayerCallback`'s function is kept (`PlayerState::multiplayerCallback`); Coney has one player, so the
  two-player sync that calls it never runs.

## Open questions

- What category 13 is, which the mission event 0/1 requires of the gang member knocked out.
- What world `+0x158` is: it gates event 0/1 and upgrade (6, 14).
- What a hat's byte `+0x10b` (1 or 2) separates in style events 2/1 and 2/2.
- How `global.lua`'s `SetupMissionPoints` turns a level's mission and bonus values into maxima.
- Whether scripts other than the four found set events 1/0-1/2 and 2/4.
- The item limits at `0x0058b300`, and what each item's duration (`+0x28`) does.
- The score formula of `0x00422998`, and which category `StatGetTotal`'s statistic id picks.
- Which notify flag `InvGiveItem`, `InvGiveRevive` and the other give bindings pass to `Inventory_AddItem`.
- What the money multiplier at game state `+0x380` (1.0 in mission 1) is.
