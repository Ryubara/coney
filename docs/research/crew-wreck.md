---
kind: feature
owns:
  functions: [
    "0x0029d558", "0x0029d5f0", "0x0029d928", "0x002bf118", "0x002dd0c8", "0x002dd148", "0x002dd210", "0x00394f30",
  ]
---
# AI: the crew's wreck order

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records.

## Summary

Given the steal / wreck command (Wreck'em, command 5) at a store, the player's Warriors first carry off the store's
goods, then wreck what is near the chief: they break the display cases' glass, swing what they hold, smash objects
and push over what can be carried. A Warrior never takes an object through an intact pane; he breaks the pane first.
Each Warrior runs the WarriorVandalSteal goal: its class and the rest of its functions are on [AI goals:
WarriorVandalSteal](ai-goals.md#goal-warrior-vandal-steal), GetItem (the fetch) is on [AI humans:
GetItem](ai.md#get-item), DestroyItem (the strike) on [AI goals: DestroyItem](ai-goals.md#goal-destroy-item), the
car goal the wreck can also give on [AI goals: DestroyCar](ai-goals.md#goal-destroy-car), and the panes themselves on
[World objects: panes](objects.md#pane).

## Rules

- A Warrior never takes an object through an intact pane: he swings at the glass until it breaks.
- A pane can break only while it lies within 50 m of a player's camera.
- While he winds up a throw of what he holds, the wreck gives him nothing new.
- An object worth nothing is a smash target only when it is a mask.
- He strikes an object standing at its own spot on the ground, whatever its size.
- Once the store's stock is gone, he looks for any store object near the chief before he wrecks.

## Map

| Behaviour | Function | Section |
| --- | --- | --- |
| The wreck step of each update: the throw wait, the held swing, the car, the smash target, the pane | `0x002bf118` `WarriorVandalStealGoal_TryVandalise` | [The wreck step](#try-vandalise) |
| What may be smashed | `0x0029d558` `ObjFilter_VandalisableInRange`, `0x00394f30` `Object_IsVandalisable` | [The smash target](#smash-target) |
| Where he stands to find and to strike it | `0x0029d5f0` `Ai_FindObject`, `0x002dd210` `DestroyItemGoal_Process` | [The stand point](#stand-point) |
| The fetch after the stock | `0x002bee78` `WarriorVandalStealGoal_TryFetch` | [After the stock](#after-stock) |
| The panes and goods at level80's store, run on the original | `0x002dc9f8` `GetItemGoal_Process` | [The store runs](#store-runs) |

## Behaviour

### The wreck step {#try-vandalise}

Confirmed (code) at the cited addresses unless a line says otherwise.

`WarriorVandalStealGoal_TryVandalise` (`0x002bf118`) runs from the goal's Process (`0x002bf748`) when the fetch gave
him nothing. It needs the gang's leader, and it answers busy at once while `Human_IsSwingingHeld` (`0x002286a0`) holds:
held flag `0x800000`, or `0x1000010` while he plays one of the eleven object throw clips with an object in hand. Then
it needs the gang's tactic to be the steal tactic (type `0x26`).

With an object in hand (`0x002bf118`): a breaking object (its class flag `0x10000`) goes on to the steps below. Any
other goes on only when it is throwable or of kind 4 or 6; he then drops his target, stops while any held flag is
set, turns once (goal byte `+0x2a`) to the nearest pane within 15 m of him (`GlassTaskManager_FindNearest`, distance
squared under 225), and otherwise turns to the crowd ahead (`Gang_CountAhead`) and queues the swing, attack kind 0 on
himself after `0x7d` ms, with line `0x8e` (30 %) or `0x8f` every 8 s when he may gesture. Anything else in hand ends
the step.

On every other update only (brain `+0x34`, `0x002bf118`), in this order:

1. A car within 15 m of the leader, the leader within 20 m of him, a free spot round it and a clear ray from the
   leader to it: the car goal (`DestroyCarGoal_Push` `0x002ddb40`, 20 m).
2. A smash target ([below](#smash-target)) from `Ai_FindObject` (`0x0029d5f0`), searched from him within the 15 m at
   `0x005148b8`, with a clear ray, a claim and the reach test. Anim set 4 or 5 is fetched (`Goal_GetItem`, kind 4,
   after `Human_DropHeld`), unless he holds a breaking object and the target's type word `+0x5e` has bit 4; any other
   is struck (`DestroyItemGoal_Push` `0x002dd0c8`). Its handle goes to goal `+0x10`.
3. At most once in a random 1500-3000 ms (goal `+0x20`): `Glass_ClaimPane` (`0x0029d928`) searched round the
   **leader** within 15 m, and DestroyItem on that pane.

### The smash target {#smash-target}

Confirmed (code) at the cited addresses.

The search's filter (`ObjFilter_VandalisableInRange` `0x0029d558`, object `0x0053f230`) takes an object that
`Object_IsVandalisable` passes, whose hit counters `+0x10d` and `+0x10e` are both above 0, and that lies within the
range of the human the filter holds (the leader, 15 m).

`Object_IsVandalisable` (`0x00394f30`) reads the object's type (`ObjTypeList_Get`): a type worth under 1 (byte
`+0x5a`) passes on only when its class is `dyn_masks` (the string at `0x00580f28`); kinds 30, 40 and 42 (byte `+0x86`:
break-and-enter door, explosive, fire barrel) fail; an object whose zone (short `+0x114`) is the excluded vandalise
zone fails (the zone id at `0x005148bc`, `0xffff` for none until `CfgExcludedVandalizeZone` sets it; level55's script
sets it in `Main`); then the object passes
when its record (vtable `+0xf4`) has `+0x40` & `0x30`, or when its type's anim set (byte `+0x87`) is 4 or 5.

### The stand point {#stand-point}

Confirmed (code) at the cited addresses.

The object search (`Ai_FindObject` `0x0029d5f0`), the pane search (`Glass_ClaimPane` `0x0029d928`) and DestroyItem
(`0x002dd210`) each pass `Nav_FindPolygonUnder` (`0x00252410`) the object's position, a stand-off and the direction
from the object toward him. DestroyItem's stand-off is the smaller of the type's collision sizes `+0x78` and `+0x7c`
(`Cfg_AddObjectType` `0x00390f18` keeps them as the script gives them), 0.75 m for a pane. `Nav_FindPolygonUnder`
never reads either: it overwrites f12 with its 10 m drop at `0x00252454` before any read and never reads a1. Its point
is the object's own spot, 0.05 m up, dropped onto marked navigation ground, else the nearest point on a nearby
navigation edge. So every search and every strike stands at the object's spot whatever its size. DestroyItem swings
once he is within 1 m of that point in 3D (squared distance at most 1.0), after a turn to the object when more than
15° off; farther, he walks there at a run from 2 m (3D), at most three times. The object search skips an object a
player is nearer than 1 m to, in 3D (squared distance under 1.0), over every player.

### After the stock {#after-stock}

Confirmed (code) at the cited addresses unless a line says otherwise.

`WarriorVandalStealGoal_TryFetch` (`0x002bee78`) first asks the steal clusters for an object
(`WarriorSteal_ClaimObject` `0x00321540`). When they give none, he waits 500 ms times his place in the gang, then
searches from the **leader** (`Ai_FindObject` 15 m, with the ray, the claim and the reach test) with the filter
`0x0053f2b0`, whose test reads the object's byte `+0x109`, the store mark the clusters keep too. When a gang
mate stands within 1.5 m of the found object's navigation point, the object's claim is dropped and he fetches nothing;
otherwise GetItem (kind 4). Only when the fetch gives nothing does the wreck step run.

### The store runs {#store-runs}

GetItem's swing at glass is on [AI humans: GetItem](ai.md#get-item) (`GetItemGoal_Process` `0x002dc9f8`). At level80's
pharmacy, with the crew put between the display cases and the order given, the two Warriors broke 8 panes (the first
at update 247), swung at glass 7 times, took the 20 objects the clusters gave them by update 655, picked up 2 objects
no stock hook saw and from update 675 pushed DestroyItem 6 times, with no car goal in 4500 updates: confirmed
(runtime), run r-20261026-wreck-store-inside. By the store front's west windows (alarmed, which `Glass_ClaimPane`
skips) no pane broke and nothing of the store's was taken, while 13 objects were picked up and DestroyItem was pushed
5 times: confirmed (runtime), run r-20261026-wreck-store. The two extra pick-ups inside are TryFetch's own search
([above](#after-stock)): inferred from the filter and from their objects lying outside the stock hook.

A pane has the body a strike breaks only within 50 m of a player's camera ([World objects](objects.md#pane-draw)). With
the level's lesson camera current, far from the store, the same swings break nothing; the scenarios therefore make
player 1's follow camera current first (confirmed (runtime), run r-20261026-wreck-store-inside).

## In Coney

The wreck step is `WarriorVandalStealGoal::tryVandalise()` (`repo:src/ai/steal_goals.cpp`); DestroyItem, the smash
target search and the pane claim are `repo:src/ai/smash_goals.cpp`; the level's side (smash targets, panes, broken
objects, held objects, `LevelItems::smashable()`) is `repo:src/gamemodes/level_items.cpp`; the clear ray through glass
is `clearRayTo()` in `repo:src/ai/perception.cpp`. Every search and DestroyItem stand at `standPoint()`, the
object's spot at his own height moved onto the planner's navigation point
([The stand point](#stand-point)). Issue #241.

Stand-ins, marked at the code: the car goal (issue #716); the turn to the crowd ahead and its lines; the breaking
flag `0x10000` (no held object counts as breaking, so the drop before a fetch never runs); and the record test
`+0x40` & `0x30`, for which the classes a strike breaks (`dyn_masks`, `overhead_weapon`, `dyn_cashreg`) stand in;
the drop onto marked ground under the object, which is his own height then the planner's point; the object search
looks at player 1 only. Coney's choices: `Human_IsSwingingHeld`'s "object in hand" is any held object; a held object
with no spawn record stops the wreck. Not ported: the excluded vandalise zone (issue #241), TryFetch's own search
and its wait ([q-after-stock](#q-after-stock)). The goal's Process keeps differences issue #644 lists, each marked at
the code. The compare of each port with the original side by side also brought GetItem's wait for an object in
flight, its reach measured from its standing point and the claim the goal's Resume drops after DestroyItem.

Coney's disc test (`repo:tests/platform/disc_level80_wreck_test.cpp`, level80 checkpoint 3 through the game session,
the same placings, 4500 updates): inside, 9 panes broken, 19 objects taken and objects held for 8331 member
updates; outside, nothing broken or taken. The held-object check stays a `CHECK_NOFAIL` until
[q-after-stock](#q-after-stock) is answered.

## Open questions

### After the stock {#q-after-stock}

Once the clusters' objects are taken or out of reach, Coney's crew pushes no DestroyItem, inside and outside, where
the original's turns to TryFetch's own search and then DestroyItem; outside, Coney's crew stays in GetItem to the end
of the run. Coney's checkpoint 3 crew has five Warriors where the scenario's has two; the three outside walk
at the closed store, and the two inside stay on three `dyn_revival` objects behind a pane they never break, counting
no try. What depends on it: the disc test's held-object check. To find out: port TryFetch's search and its wait, read
why that pane does not break and whether the original's clusters hold those objects (it claimed 20 of the zone's 23),
and check the crew's size against the original's.

## How this was checked

The store runs are the scenarios `repo:research/traces/scenarios/wreck_store_inside.toml` and
`repo:research/traces/scenarios/wreck_store.toml` with the patch `repo:research/traces/patches/crew-wreck.toml`,
recorded with `coney-tools pcsx2 record` (PCSX2 v2.9.129) on 2026-10-26; the counts are the hooks' call counts.
