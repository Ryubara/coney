---
kind: feature
owns: {functions: ["0x004125b8", "0x004134f8", "0x00414548", "0x004146e0", "0x00415378"]}
---
# Trigger boxes and spheres: the update

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records.

## Summary

A mission script hears a human come into a place, stay there and leave it through three messages to a trigger: 3
(entered), 5 (still inside) and 4 (left). The triggers are the volume boxes a script adds with `AddVolumeBox` (kind 0
for anyone, kind 2 for the players only) and the trigger sphere round an object (`TriggerSphereCfg`). This page is
how one update of a trigger decides what to send and when: the stay timer behind message 5, the order the messages
go out in and what stops an update part-way. What the triggers are, their records and pools, the inside test and the
other messages (6, damage; 7, theft) are on [Scripting: trigger boxes and spheres](scripting.md#triggers); how a
message reaches Lua is on [Message handlers](scripting.md#message-handlers).

## Rules

- Message 5 comes on the trigger's own beat, not a period after each human came in: the stay timer is re-armed
  whenever it falls due, whether or not anyone is inside, so a human who entered just before a beat gets his first 5
  on that beat.
- The timer is due only once the game time is strictly past it.
- A box's period is its handler component's repeat period (1000 ms unless something set it); a sphere's is its own,
  1000 ms.
- Each message goes out at once, in the order the humans are found. A handler that switches a box off stops the rest
  of that update; one that switches a sphere off ends only the found humans, and the sphere's occupants are still
  checked unless the sphere was freed.
- An occupant whose handle no longer names anything is skipped: no message 4, and it stays listed.
- `AddVolumeBox` makes nothing for kind 1; kinds 0, 2 and 3 each take a record from their pool.

## Map

| Behaviour | Function | Section |
| --- | --- | --- |
| A sphere's check: timer, found humans, occupants | `0x004146e0` `TriggerSphere_Update` | [A sphere's check](#sphere) |
| Whether a sphere accepts a point: radius, then the mode's test | `0x00414548` `TriggerSphere_Accepts` | [The accept test](#accepts) |
| A kind-0 box's update | `0x00415378` `VolumeBox_Update` | [A box's update](#box) |
| A kind-2 box's update, over the two players | `0x004134f8` `PlayerBox_Update` | [A players' box](#player-box) |
| The humans within a radius of a point | `0x002274a8` `Humans_FindAhead` | [Who is found](#found) |
| A box's repeat period: the handler component's getter | vtable `0x00544b98`, slot `+0x4c` | [The stay timer](#stay-timer) |
| `AddVolumeBox`: the pool by kind, the corners | `0x004125b8` `VolumeBox_Add` | [Adding a box](#add) |

## Behaviour

### The stay timer {#stay-timer}

Each trigger keeps the time its next message 5 is due: a sphere at `+0x178`, a kind-0 box at `+0x1e0`, a kind-2 box
at `+0xf8`. At the start of each update or check, before any human is looked at, the trigger compares the game clock
(`0x0050b734`, `+0x48`) with it; when the clock is strictly greater, the update is a due one and the timer becomes the
clock plus the period. A due update sends 5 to every occupant still inside; an update that is not due sends none.
Confirmed (code) at `0x004146e0` (sphere), `0x00415378` (box) and `0x004134f8` (players' box). In `level99` at
checkpoint 2 two enabled boxes with no occupants each moved their timer on by 2002 ms over 120 frames, exactly as the
clock moved, staying within a second ahead of it: confirmed (runtime), run r-20261030-box-stay-timer-empty.

The periods: a sphere uses its own `+0x164` (1000 from `0x00414480`; `TriggerSphereCfg` does not change it, confirmed
(code) at `0x00414bc0`). A box asks its handler component (box vtable `+0x3c` returns the component at box `+0x160`)
for its repeat period through slot `+0x4c` of the component's vtable (`0x00544b98`), a two-instruction getter Ghidra
has no function for that returns component `+0x74`, 1000 from `ScriptHandler_Init` (`0x00384a10`) unless set.
Confirmed (code) at `0x00415378` and `0x00544b98`. The only writer
traced is `TriggerSphereCfg`'s last argument (`0x00414bc0`, slot `+0x44`), and every script passes it a flag, a human
or a prop, never a box, so in the game every box's period is 1000 ms. Inferred from the decompiled scripts (all
`TriggerSphereCfg` calls).

### Who is found {#found}

`Humans_FindAhead` (`0x002274a8`) walks the 60 human slots in slot order (not nearest first) and lists those within a
radius of a point, at most a given count, leaving out one human and any slot whose flag word (`0x00715390`) lacks a
mask bit. Confirmed (code) at `0x002274a8`. A sphere asks for 60 within its radius of its object, leaving out the
object's own human; a box asks for 60 within its bounding sphere (centre `+0x30`, radius `+0x40`), leaving out
nobody. Both pass mask 1. Confirmed (code) at `0x004146e0`, `0x00415378`.

### A sphere's check {#sphere}

`TriggerSphere_Update` (`0x004146e0`), for an armed sphere (`+0x180`), confirmed (code) at `0x004146e0`:

1. The stay timer ([above](#stay-timer)), then the sphere's centre from its object.
2. For each found human, in order: when the sphere accepts him (`TriggerSphere_AcceptsObject`, `0x00414688`: the
   radius and the mode's line test) and he is new, he is added in the first slot that no longer resolves
   (`HandleArray60_AddInFreeSlot`, `0x004f5cc8`, nothing when all 60 resolve) and message 3 goes to the object
   whether or not he found a slot; accepted and listed, message 5 on a due check; not accepted and listed, he is
   removed and message 4 goes. Each message is sent at once. After each human the loop over the found humans ends
   when the sphere was disarmed (the branch at `0x004148ec`), going on to step 3, and the check skips to the end when
   the sphere was freed during it (`TriggerSphere_Free`, `0x00414338`, only marks the sphere being updated,
   `0x00514824`, in `0x00514828`). Confirmed (code).
3. Then, unless the sphere was freed, each of the 60 occupant slots, whether or not the sphere is still armed: a
   handle that does not resolve (`0x00390268`) is skipped and kept; a human flagged dead (`0x00227eb0`) or no longer
   accepted is removed and message 4 goes. The pass stops when the sphere was freed. Confirmed (code). So a handler
   that disarms with `TriggerSphereCfg` (`0x00414bc0`, which keeps the occupants) still has 4 sent for each occupant
   who left; `TriggerSphereEnable` off empties the list, so nothing follows.
4. A sphere freed during the check is freed now.

### The accept test {#accepts}

`TriggerSphere_Accepts` (`0x00414548`) takes the position of the sphere's object, refuses a point farther than the
radius (`+0x174`, squared distance in 3D), and then by mode (`+0x17c`): mode 1 asks `Nav_IsConnected` (`0x0024dee8`)
whether the two points are joined, mode 2 casts a clear-ray test (`Ray_IsClear`) between the two points each moved
by 0.01 times a fixed vector (`0x00511740`), and any other mode accepts. `TriggerSphere_AcceptsObject` (`0x00414688`)
is the same test on an object's position. Confirmed (code) at `0x00414548`, `0x00414688`.

### A box's update {#box}

`VolumeBox_Update` (`0x00415378`), for an enabled box (`+0x68`), confirmed (code) at `0x00415378`:

1. The stay timer, then the box's bounds (`Box_ComputeBounds`).
2. For each found human, while the box is still enabled: a human flagged dead is passed over; one inside
   (`Box_ContainsObject`) and new is added in a free slot and gets 3; inside and listed gets 5 on a due update; not
   inside and listed is removed and gets 4. Each message is sent at once, through the box's own slot `+0x44`.
3. Then, while the box is enabled, each of the 60 occupant slots: a handle that does not resolve is skipped and kept;
   a human flagged dead gets 4 and then his slot is cleared; one no longer inside is removed and then gets 4.

### A players' box {#player-box}

`PlayerBox_Update` (`0x004134f8`), for an enabled kind-2 box, confirmed (code) at `0x004134f8`: the stay timer, then
the two player handles (`0x0051489c` `+0x228`, `+0x22c`) in that order, each while the box is still enabled. A handle
that does not resolve is passed over. A player alive and inside is added (two slots, `+0xf0`) and gets 3 when new, 5
on a due update when listed; one dead or not inside who is listed is removed and gets 4. There is no pass over the
occupants: a listed player is only ever tested as one of the two players.

### Adding a box {#add}

`VolumeBox_Add` (`0x004125b8`), confirmed (code) at `0x004125b8`: kind 0 takes the next record of
`FreeList<VolumeBox>` and clears its occupants, kind 2 the next of `FreeList<PlayerBox>`, kind 3 one from the turf
factory, and kind 1 returns `NilHandle` at once. For a full pool, or any other kind, the record pointer stays null and
the function goes on to name and fill it: there is no guard. Then the name, the enable flag (slot `+0x5c`), the first
corner as given at `+0x10`, the opposite corner (corner plus size) at `+0x20`, the bounds after each, the flags byte
`+0x6a`, and the box's handle as the result. The corners are not sorted. No `AddVolumeBox` call in the game's scripts
has a negative size (974 calls), so for every box the game makes the first corner is the lowest. Inferred from the
decompiled scripts.

## In Coney

- `world_objects::TriggerSpheres::update()` and `world_objects::VolumeBoxes::update()` (`src/world_objects/`) are the
  three updates; `VolumeBoxes::setStayPeriod()` gives a box its handler's period, which `GameplayMode::updateBoxes()` reads
  from the box's `TriggerSphereCfg` record (`TriggerSphere::handlerPeriodMs`), 1000 ms when it has none. Unit tests:
  `tests/world_objects/volume_boxes_test.cpp`, `tests/world_objects/trigger_spheres_test.cpp`.
- `VolumeBoxes::add()` makes no box for kind 1 and, Coney's choice, none for a kind other than 0, 2 and 3, where the
  original goes on with a null record; `makeAddVolumeBox()` then returns `NilHandle` and uses no handle. Coney's boxes
  come from no fixed pool, so a full pool never happens ([q-box-pools](#q-box-pools)). Coney sorts the corners, which
  for every box the game adds is the same as keeping them.
- `TriggerSpheres::accepts()` is the accept test, its modes' tests a stand-in ([q-line-tests](#q-line-tests)); any
  mode other than 1 and 2 accepts on the radius alone.
- Coney's choices in the found list ([q-found-mask](#q-found-mask)): the subjects are Coney's humans in its own order
  (by handle), a dead human is never found, a box finds the humans inside it rather than within its bounding sphere,
  and nobody beyond the first 60 found is tested by a sphere. A players' box takes the first two players among the
  subjects.
- Coney's choice: a trigger's occupants are a packed list that appends, where the original fills the first of its
  fixed slots that does not resolve (`0x004f5cc8`), so the order of the occupants, and of several 4s in one pass, may
  differ.
- Issue [#690](issue:690) made the stay timer re-arm on every due update with the strict test, the messages go out at
  once and stop as the original's do when a handler switches the trigger off, and an unresolved sphere occupant stays
  listed.

## Open questions

### The modes' tests {#q-line-tests}

What `Nav_IsConnected` (`0x0024dee8`) tests between two points, and which vector `0x00511740` holds for mode 2's
ray, are not traced; Coney's modes 1 and 2 use its clear-line callback from points raised 1 m instead. To settle it:
read `0x00511740` and step `0x0024dee8` in PCSX2 with the dealer's sphere in `level99`.

### The found humans {#q-found-mask}

What bit 1 of the human slots' flag word (`0x00715390`) means is not traced, so whether a dead human can be found
(and so get 3 from a sphere before the occupant pass sends him 4) is open. To settle it: watch the word of a human
in PCSX2 as he dies.

### The box pools {#q-box-pools}

What happens on the PS2 when a level adds more boxes of a kind than `CfgSetDatabaseSizes` made room for is not
traced: the code writes through a null record. No level is known to do it. To settle it: count each level's
`AddVolumeBox` calls by kind against its pool sizes.
