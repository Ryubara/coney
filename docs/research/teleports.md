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
does is on [Combat power, rage and reviving](combat-rage.md#rage); a scene's start and end are on
[Scenes](scenes.md#humans).

## Rules

- A placement moves a human and changes nothing else he has: his health, its maximum, his meters, a stun and his clip
  are as they were. Only a creation starts a human afresh.
- A placement drops the human onto the ground at most 0.5 m below the point, unless he is in a scene.
- `TeleportToFlag` also stops his move across the ground; `Teleport` does not.
- A scene's end keeps the health; after a skip it places each human at his end pose.
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
6. When brain `+0x2e` is above 0 and brain `+0xec` is not 4, asks the front action's slot `+0xc` and pops the action
   (`Brain_PopAction`) when it answers 1. Inferred from the goals' slot `+0xc`, which gives a goal's type: the count
   of actions queued and a `MoveTo` (action type 1, [Moving](ai-moving.md)); what `+0xec` = 4 means is not traced
   ([q-placement-rest](#q-placement-rest)).
7. Sets bit `0x80000000` at `+0x40` of the object at `+0x1a0` (not traced).

Nothing in it, nor in the two functions it calls to set the transform, writes the health (record `+0x144`), its
maximum (`+0x146`), the power (`+0x148`), the stamina (`+0x14a`), the rage, a stun or the animation. Confirmed (code)
at `0x0023d440` and `0x0021b048`.

| Field | Meaning | Evidence |
| --- | --- | --- |
| human `+0x1b3` | set to 1 by a placement; its only reader is `Human_GetNavPolygon` | confirmed (code) |
| human `+0x54` | state bit `0x40000000` set by a placement, with the time at `+0x50` | confirmed (code) |
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
- clears `+0x300`, `+0x304`, `+0x331` and `+0x332`, the fields `Human_ClearTurn` clears (an attack's steer);
- sets `+0x2e0`-`+0x2ec` to (0, 0, 0, 1) (what they hold is not traced);
- sets the transform again as it is, and the time at `+0x50`.

It writes no meter, no stun and no animation. A scene's start makes the same reset ([Scenes](scenes.md#humans)).

### A scene's end {#scene-end}

When a human's scene clip ends (`SceneTask_ClipDone`, `0x003a00b8`) the human leaves the scene (`+0x280` = −1 unless
the scene is skipping), its flags `0x2000` and `0x800000` are cleared and `0x20000000` set, and message 10 goes to the
object it holds; the human is not placed, its clip having moved it there. At the scene's end (`SceneTask_End`,
`0x0039f450`), after a skip each human gets `+0x280` = −1 and is placed at its role's end pose through the placement
above, so it drops onto the ground; then its speech and voice stop, its hat is told and its join goal is popped.
Neither function writes the health or a meter. Confirmed (code) at `0x003a00b8` and `0x0039f450`.

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
  `0x002843f8`, [the flash](combat-rage.md#rage)), while the handler still ends the mission: no clip, no sound, then the
  closing scene `l99_c9`. Confirmed (code) at `0x002843f8`.

## In Coney

- `Human::place()` (`src/human/human.h`) is the placement and `Human::resetMotion()` the motion reset; `Human::spawn()`
  is a creation only (a human made, a character change), which starts the human afresh. The scripts' teleports
  (`Player::teleport()`, `ai::ScriptedBrains::humanTeleported()`), a scene's release and the hand-over place. The level
  gives the placement its collision unless a scene holds the human (`PlayLevelMode::placeGround()`). Coney's own
  placements that stand for a walk (the debug menus' teleport, `--start`) place and then settle the player as a walk
  would leave him (`Human::settle()`: idle, out of a stance or an attack, his health and meters kept).
- Until issue #234 every placement rebuilt the human as a creation does, so in the story the player reached the dealer
  at 900 of 900 and the flash did nothing visible before the closing scene. Disc test:
  `tests/platform/disc_flash_test.cpp`, the level99 case scripted through checkpoints 1 to 3.
- Coney's choices: `Teleport` makes the motion reset too, as `TeleportToFlag` does; a climb in progress ends at a
  placement; the brain's front action is not popped ([q-placement-rest](#q-placement-rest)). After a scene clip played
  to its end Coney places the human where the clip left it, with no drop, since Coney's scene drives a frame and not
  the human.
- The hand-over moves Coney's one player human onto the chosen one, so it takes the chosen human's meters as
  `Human_MakePlayer` sets them (`human::takeMetersOf()`).
- A player who falls out of the world is put back at the level's start, placed with his health kept
  (`Human::putBack()`), where the original fails the mission (a known difference,
  [Characters](characters.md#coneys-implementation)).

## Open questions

### The rest of a placement {#q-placement-rest}

What brain `+0xec` = 4 means, which of Coney's actions are the original's type 1 (`MoveTo`) that a placement pops, what
`+0x2e0`-`+0x2ec` and the object at `+0x1a0` are, and how a climb in progress carries on after a placement are not
traced. To settle them: read the brain's `+0xec` writers and the action types' slot `+0xc` in Ghidra, and teleport a
climbing human in PCSX2 and read his state.
