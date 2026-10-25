---
kind: feature
owns: {functions: ["0x0023d440", "0x0023f158"]}
---
# Characters: placing a human (teleports)

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records.

## Summary

A level script moves a human with `Teleport` or `TeleportToFlag`: the human vanishes from where he stood and stands
at the new place, facing the new way, and is otherwise the same human. His health, its maximum, his stamina, his rage
and power meters, a stun and the clip he plays all stay. The first mission depends on that: its last lesson teaches
the flash, and the player arrives at the flash dealer hurt only because the teleports on the way leave his health
alone.

Creating a human (`HuCreate`, `Human_Init`) and where each level first puts the player are on
[Characters](characters.md#creation) and [Where a level puts the player](characters.md#level-starts); what the flash
does is on [Combat: the power meter, rage and the flash](combat-rage.md#flash); a scene's start and end are on
[Scenes](scenes.md#humans).

## Rules

- A placement moves a human and changes nothing else he has: his health, its maximum, his meters, a stun and his clip
  are as they were. Only a creation starts a human afresh.
- A placement drops the human onto the ground at most 0.5 m below the point, unless he is in a scene.
- `TeleportToFlag` also stops his move across the ground; `Teleport` does not.
- A scene's end keeps the health; after a skip it places each human at his end pose.
- A placement leaves the camera alone: the follow camera glides after the player, and puts itself behind him only when
  it has lost him; a script that wants a cut calls `CameraReset` itself.
- A flash heals only a hurt player: at full health d-pad right does nothing, so the first mission's flash lesson needs
  the player still hurt when he reaches the dealer.

## Map

| Behaviour | Function | Section |
| --- | --- | --- |
| A human placed: pair broken, dropped onto the ground, transform and facing set; nothing else written | `0x0023d440` `Human_SetTransform` | [A placement](#placement) |
| `TeleportToFlag`: the placement, then the motion reset | `0x00385db0` `Object_TeleportToFlag` | [The motion reset](#motion-reset) |
| `Teleport`: the placement only | `0x00385bb8` `Object_Teleport` | [The motion reset](#motion-reset) |
| The motion reset: move velocity and steer cleared | `0x0023f158` `Human_ResetMotion` | [The motion reset](#motion-reset) |
| A scene's end: the health untouched, a skipped human placed | `0x003a00b8`, `0x0039f450` | [A scene's end](#scene-end) |
| The follow camera after a placement: it glides, and resets itself when lost | `0x0012ae58` `Cam_Follow_Update` | [The camera](#camera) |
| A human made player 1 by a hand-over | `0x00229c40` `Human_MakePlayer` | [The hand-over](#handover) |
| The flash lesson that needs the health kept | `level99_lesson2.lua` `P3.DealerPoizo` | [The level99 flash lesson](#flash-lesson) |

## Behaviour

### A placement {#placement}

`Human_SetTransform` (`0x0023d440`, the human's set-transform slot `+0x6c`) takes a transform (a point and a
rotation) and, confirmed (code) at `0x0023d440`:

1. Breaks the pair when the human is in one (`Human_BreakPair`, [breaking a pair](combat-grabs.md#pair-break)).
2. Marks the human placed: `+0x1b3` = 1, state bit `0x40000000` at `+0x54` and the time at `+0x50`.
3. Casts a ray from the point itself, straight down (`(0, 0, −1)` at `0x00511770`), 0.5 long, through
   `WorldManager_RayCast`. When the human is in no scene (`+0x280` is −1) and the ray hits, the point is lowered by
   the hit's distance: the feet go onto the hit, with no gap. A miss, or a human in a scene, keeps the point as given.
   Confirmed (code) at `0x0023d440` and `0x00511770`.
4. Sets the transform through the position slot `+0xb4` (`Human_SetPosition`, `0x0021b048`, writes the pose slot) and
   keeps it at `+0x10`-`+0x2c`. Confirmed (code) at `0x0021b048`.
5. Sets the stick's last angle `+0x5d8` to the new facing's angle from x and its push `+0x5dc` to 0, and the brain's
   facing `+0x110`.
6. When brain `+0x2e` (the count of actions queued) is above 0 and brain `+0xec` is not 4, asks the front action's
   type (slot `+0xc` of its vtable at `+0x08`) and, when it is 1, pops it (`Brain_PopAction`, `0x0028da60`, which
   aborts a started action first through `Action_AbortAndDestroy`, `0x002f9de0`). Type 1 is the move: `MoveAction_Init`
   (`0x002fb9e8`) gives a move the vtable `0x00542f20`, whose slot `+0xc` returns 1. Confirmed (code) at
   `0x0023d440`, `0x0028da60`, `0x002fb9e8` and the vtable `0x00542f20`. What `+0xec` = 4 means is not traced: no
   `sw` to an offset `+0xec` in the program stores to a brain ([q-placement-rest](#q-placement-rest)).
7. Sets bit `0x80000000` in the flags (`+0x40`) of the human's physics body (`+0x1a0`): the body's "re-pose and
   re-sort on the next update" bit ([Physics](physics.md#bodies)). Confirmed (code) at `0x0023d440`.

Nothing in it, nor in the two functions it calls to set the transform, writes the health (record `+0x144`), its
maximum (`+0x146`), the power (`+0x148`), the stamina (`+0x14a`), the rage, a stun or the animation. Confirmed (code)
at `0x0023d440` and `0x0021b048`.

| Field | Meaning | Evidence |
| --- | --- | --- |
| human `+0x1b3` | set to 1 by a placement; its only reader is `Human_GetNavPolygon` | confirmed (code) |
| human `+0x54` | object flag `0x40000000` set by a placement, with the time at `+0x50`; no reader traced | confirmed (code) |
| brain `+0x110` | the brain's facing: set to the new heading | confirmed (code) |
| human `+0x1a0` | the physics body; a placement sets its re-pose bit `0x80000000` | confirmed (code) |
| human `+0x280` | the scene the human is in, −1 for none; a placement drops only a human in none | confirmed (code) |
| human `+0x5d8`, `+0x5dc` | the stick's last angle and push: the new facing, no push | confirmed (code) |
| record `+0x144` | the health; a placement does not write it | confirmed (code) |

In the running game: `HuSetHealthPercent(player, 35)` gave Cleon 535 of 1530 in level80, and `TeleportToFlag(player,
fC1Vermin1, -1)` moved him from (−193.4, 127.5) to (−199.0, 126.4) with his health still 535 of 1530. Confirmed
(runtime), run r-20261024-teleport-keeps-health.

### The motion reset {#motion-reset}

`Object_TeleportToFlag` (`0x00385db0`) calls the object's transform slot `+0x6c` (for a human, the placement above) and
then, for a human, slot `+0x14c`, `Human_ResetMotion` (`0x0023f158`). `Object_Teleport` (`0x00385bb8`) calls only the
transform slot. Confirmed (code) at `0x00385db0` and `0x00385bb8`.

`Human_ResetMotion`, confirmed (code) at `0x0023f158`:

- calls slot `+0x74`, `Human_SetMoveVelocity` (`0x0023cf00`), with the zero vector (`0x005116c0`): the move velocity
  `+0x30` is zero, its speed `+0x1ac` 0 and the gait `+0x1a8` the one for speed 0. The vertical speed (`+0x3a0`) is a
  field of its own and is not written (confirmed (code) at `0x0023cf00`);
- clears `+0x300`, `+0x304`, `+0x331` and `+0x332`, the fields `Human_ClearTurn` clears (an attack's steer), and the
  turn's rate `+0x308`, which `Human_ClearTurn` leaves;
- sets `+0x2e0`-`+0x2ec`, the steer's slide velocity ([the fight stance](combat-targets.md#fight-stance)), to
  (0, 0, 0, 1);
- sets the transform again as it is, and the time at `+0x50`.

It writes no meter, no stun and no animation. A scene's start makes the same reset ([Scenes](scenes.md#humans)).

### A scene's end {#scene-end}

When a human's scene clip ends (`SceneTask_ClipDone`, `0x003a00b8`) the human leaves the scene (`+0x280` = −1 unless
the scene is skipping), its flags `0x2000` and `0x800000` are cleared and `0x20000000` set, and message 10 goes to the
object it holds; the human is not placed, its clip having moved it there. At the scene's end (`SceneTask_End`,
`0x0039f450`), after a skip each human gets `+0x280` = −1 and is placed at its role's end pose through the placement
above, so it drops onto the ground; then its speech and voice stop, its hat is told and its join goal is popped.
Neither function writes the health or a meter. Confirmed (code) at `0x003a00b8` and `0x0039f450`.

### The camera {#camera}

Neither teleport calls camera code, nor do the placement and the motion reset they call: confirmed (code) at
`0x00385bb8`, `0x00385db0`, `0x0023d440` and `0x0023f158`. The follow camera sees only that its target stands
elsewhere on its next update (`Cam_Follow_Update`, `0x0012ae58`, [the follow update](camera.md#update)):

1. Its look-at point closes the gap a share at a time: all of a move up to 0.4 m, 20% of one beyond 0.8 m (step 5), so
   after a short teleport the view glides to the new place over a few updates. Confirmed (code) at `0x00127d88`. In
   the running game a 2 m `Teleport` at level99's street end moved the look-at point 0.4, 0.32, 0.256, 0.205, 0.164,
   0.321 and 0.335 m over seven updates, and the camera 0.059 m in all: confirmed (runtime), run
   r-20261025-camera-glides-after-teleport.
2. When the camera is lost, it is put behind the target at once, as `CameraReset` puts it (step 7, with one player
   camera): its position of the last update more than 1.3 × the maximum distance from the look-at point across the
   ground, or more than 30 m above or below it. Confirmed (code) at `0x0012b918`-`0x0012b9c8`. With level99's 6.6 m
   maximum the limit is 8.58 m. The look-at point moves at most 20% of a long move on the first update, and from then
   on the hard band keeps the camera within its far edge of the look-at point (about 5.7 m), so only a teleport of
   about 20 m or more away from a camera 4.8 m behind is lost; a shorter one is followed. Inferred from steps 5, 7 and
   19 of the update. In the running game a 30 m `Teleport` put the camera behind the player on the first update after
   it (from 4.8 m behind the old place to 4.7 m behind the new): confirmed (runtime), run
   r-20261025-camera-glides-after-teleport.
3. A script that wants the view behind the new place calls `CameraReset` after the teleport, and the reset reads where
   the target stands then (`Cam_Follow_PlaceAtHeading` gathers its targets first, `0x001250a8`). level99's lessons
   do it before handing back from a cut-away (`level99_lesson1.lua`, `level99_lesson2.lua`, after each
   `TeleportToFlag(player, ...)`), and `level95.lua`'s `StartDoorWalk` does it after its teleport when it has no door
   camera ([The door walks](camera.md#door-walks)). Confirmed (code) at `0x001250a8`.
4. A scene that had its own camera ends by resetting the follow camera it hands back to, after its humans are let go,
   so the view starts behind the place the scene left the player ([Scenes take the camera](camera.md#scenes)).
   Confirmed (code) at `0x0039f450`.

### The hand-over {#handover}

`HuChangePlayerGang` makes a chosen AI human player 1 (`Human_MakePlayer`, `0x00229c40`, [the level99
hand-over](characters.md#level99-handover)). The chosen human keeps its own health and maximum, each scaled by its
Warrior class's health percentage (`CfgWarriorClass` byte `+0x05`, in single precision, rounded to the nearest), and
gets its power (`+0x148`) and stamina (`+0x14a`) at their maximums; its rage is not written. The old player's health
plays no part. Confirmed (code) at `0x00229c40`.

### Falling out of the world {#out-of-world}

The original puts no one back: a player more than 20 m below the collision mesh's lowest point fails the mission
([Characters](characters.md#ground), [Camera](camera.md#death-camera)). Confirmed (code) at `0x002403ec`.

### The level99 flash lesson {#flash-lesson}

The first mission's last checkpoint teaches the flash, and it works only because the player is hurt when he reaches
the dealer. Script locators are given as file and function.

- `level99_lesson1.lua` `P2.SetupLesson1` sets the new Rembrandt to 35 % when checkpoint 2 hands him the player.
  Confirmed (code), `level99_lesson1.lua` `P2.SetupLesson1`.
- `level99.lua` `RunLevel` does the same for a start at checkpoint 3, and `F.PlayerHealth` again where it is called.
  Confirmed (code), `level99.lua` `RunLevel`.
- Between there and the dealer no script sets his health; he is teleported several times (`P3.PoizoClimb`,
  `P3.PoizoJump`, `P3.WoodenFences`, then `P3.DealerPoizo` to the dealer), each a `TeleportToFlag` that keeps it
  ([above](#placement)). Inferred from the scripts: no other health call in the chapters on that path.
- After the purchase, `P3.CheckForFlash` sees the flash and `P3.FlashSpeak` turns command `0x28` on for pad 0 (its
  pressed-table entry at `0x005ddde8` goes from mask `0xfe` to `0xff`) and sets `P3.UseFlash` as the
  `PadSetHandlerEx` handler. Confirmed (runtime), run r-20261024-flash-dealer-665.
- d-pad right then plays clip 665 and its event uses the flash: `Flash_Use` (`0x00284280`) is entered from
  `Human_HandleMessage` (return address `0x002473c4`) with 665 as the anim, and `P3.UseFlash` schedules
  `P3.MissionOver` 1.5 s later. Confirmed (runtime), run r-20261024-flash-dealer-665 (from the state library's
  `breakable-fence-flash`, 314 of 900 health, the dealer reached with the stick at 60 %).
- At full health the press does nothing, as no upgrade (6, 8) is unlocked so early (`Player_UseItemCommand`
  `0x002843f8`, [the flash](combat-rage.md#flash)), while the handler still ends the mission: no clip, no sound, then
  the closing scene `l99_c9`. Confirmed (code) at `0x002843f8`.

## In Coney

- `Human::place()` (`src/human/human.h`) is the placement and `Human::resetMotion()` the motion reset; `Human::spawn()`
  is a creation only (a human made, a character change), which starts the human afresh. The scripts' teleports
  (`Player::teleport()`, `ai::ScriptedBrains::humanTeleported()`), a scene's release and the hand-over place. The level
  gives the placement its collision unless a scene holds the human (`PlayLevelMode::placeGround()`). `TeleportToFlag`
  then resets the motion and `Teleport` does not (`world_objects::TeleportMotion`). The brain's part of a placement,
  its front move popped, is `ai::Brain::placed()`, which every caller that places a human with a brain makes. A
  placement makes the point the human's last ground (`Human::lastGround()`), where Coney's stuck-in-the-air rule
  lands him. Coney's own placements that stand for a walk (the debug menus' teleport, `--start`) settle the player as
  a walk would leave him (`Human::settle()`: idle, out of a stance or an attack, his health and meters kept) and then
  place him.
- Until issue #234 every placement rebuilt the human as a creation does, so in the story the player reached the dealer
  at 900 of 900 and the flash did nothing visible before the closing scene. Disc test:
  `tests/platform/disc_flash_test.cpp`, the level99 case scripted through checkpoints 1 to 3.
- Not ported, each for want of the state it marks: the brain's facing (Coney's brain keeps none: a move sets the
  heading for one update), the placed marks (`+0x1b3` makes `Human_GetNavPolygon` look the polygon up again, and Coney
  caches none; the flag at `+0x54` has no reader traced) and the body's re-pose bit (Coney's body is posed from the
  position at each use). Coney pops a front move whatever `+0xec` holds, having no such state.
- A placement of the player leaves the camera alone ([The camera](#camera)): `camera::FollowCamera::update()` follows
  the new place and puts itself behind him when lost (`FollowCamera::resetWhenLost()`), and the follow camera reads
  where the player stands whenever it places itself (`FollowCamera::setTargetSource()`, which `human::Player` gives
  it), so the scripts' `CameraReset` and a scene's end go behind the new place. Disc test:
  `tests/human/disc_player_placement_test.cpp`; unit tests: `tests/camera/follow_placement_test.cpp`. Coney's own
  jumps that stand for a walk (the debug menus' teleport, `--start`, the debug menus' character change) still put the
  camera behind the player (`Player::resetCamera()`, `CameraReset` with what the scripts set kept).
- Coney's choices: a climb in progress ends at a placement ([q-placement-rest](#q-placement-rest)). After a scene clip
  played to its end Coney places the human where the clip left it, with no drop and no pop of the brain's front move,
  since Coney's scene drives a frame and not the human, and the original places no one there.
- The hand-over moves Coney's one player human onto the chosen one, so he stands afresh, nothing of the old body's
  fight carried over, and takes the chosen human's meters as `Human_MakePlayer` sets them (`human::makePlayerOf()`,
  `human::takeMetersOf()`).
- A player who falls out of the world is put back at the level's start, snapped as a creation snaps (from 1 m above)
  with his health kept (`Human::putBack()`), where the original fails the mission (a known difference,
  [Characters](characters.md#coneys-implementation)).

## Open questions

### The rest of a placement {#q-placement-rest}

What brain `+0xec` = 4 means (no direct store to it was found), who reads object flag `0x40000000` at `+0x54` and the
time at `+0x50`, and how a climb in progress carries on after a placement are not traced. To settle them: watch brain
`+0xec` and human `+0x54` with write and read watchpoints in PCSX2 through a level with waypoint queues, and teleport a
climbing human in PCSX2 and read his state.
