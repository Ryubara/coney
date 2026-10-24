---
kind: feature
owns: {functions: ["0x0023d440"]}
---
# Characters: placing a human (teleports)

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records.

## Summary

A level script moves a human with `Teleport` or `TeleportToFlag`: the human vanishes from where he stood and stands
at the new place, facing the new way, and is otherwise the same human. In particular a hurt human stays hurt. The
first mission depends on that: its last lesson teaches the flash, and the player arrives at the flash dealer hurt
only because the teleport to the dealer leaves his health alone.

Creating a human (`HuCreate`, `Human_Init`) and where each level first puts the player are on
[Characters](characters.md#creation) and [Where a level puts the player](characters.md#level-starts); what the flash
does is on [Combat power, rage and reviving](combat-rage.md#rage).

## Rules

- A human the scripts move keeps his health, hurt or not: a teleport only places him.
- A flash heals only a hurt player: at full health d-pad right does nothing, so the first mission's flash lesson needs
  the player still hurt when he reaches the dealer.

## Map

| Behaviour | Function | Section |
| --- | --- | --- |
| A human placed: pair broken, transform set, health kept | `0x0023d440` `Human_SetTransform` | [A placement keeps the health](#keeps-health) |
| The scripts' `TeleportToFlag`, which calls it through the object's transform slot | `0x00385db0` `Object_TeleportToFlag` | [Where a level puts the player](characters.md#level-starts) |
| The flash lesson that needs the health kept | `level99_lesson2.lua` `P3.DealerPoizo` | [The level99 flash lesson](#flash-lesson) |

## Behaviour

### A placement keeps the health {#keeps-health}

`Human_SetTransform` (`0x0023d440`) first breaks the pair when the human is in one (`Human_BreakPair`,
[breaking a pair](combat-grabs.md#pair-break)), marks the human placed, lowers the point onto what a short ray
(0.5) straight down hits when the human is not in a scene, sets the transform, the heading and the brain's facing,
and drops the brain's front action when it cannot survive a move. Nothing in it writes the health. Confirmed (code) at
`0x0023d440`.

| Field | Meaning | Evidence |
| --- | --- | --- |
| human `+0x1b3` | set to 1 by a placement (`0x0023d440`) | confirmed (code) |
| human `+0x54` | state bit `0x40000000` set by a placement, with the time at `+0x50` (`0x0023d440`) | confirmed (code) |
| record `+0x144` | the health; a placement does not write it (`0x0023d440`) | confirmed (code) |

In the running game: `HuSetHealthPercent(player, 35)` gave Cleon 535 of 1530 in level80, and `TeleportToFlag(player,
fC1Vermin1, -1)` moved him from (−193.4, 127.5) to (−199.0, 126.4) with his health still 535 of 1530. Confirmed
(runtime), run r-20261024-teleport-keeps-health.

### The level99 flash lesson {#flash-lesson}

The first mission's last checkpoint teaches the flash, and it works only because the player is hurt when he reaches
the dealer. Script locators are given as file and function.

- `level99_lesson1.lua` `P2.SetupLesson1` sets the new Rembrandt to 35 % when checkpoint 2 hands him the player, and
  `level99.lua` `RunLevel` does the same for a start at checkpoint 3. Nothing between there and the dealer sets his
  health (inferred from the scripts: no other health call in the chapters on that path).
- `level99_lesson2.lua` `P3.DealerPoizo` teleports him to the dealer (`TeleportToFlag`), still hurt
  ([above](#keeps-health)). After the purchase, `P3.CheckForFlash` sees the flash and `P3.FlashSpeak` turns command
  `0x28` on for pad 0 (its pressed-table entry at `0x005ddde8` goes from mask `0xfe` to `0xff`) and sets
  `P3.UseFlash` as the `PadSetHandlerEx` handler. Confirmed (runtime), run r-20261024-flash-dealer-665.
- d-pad right then plays clip 665 and its event uses the flash: `Flash_Use` (`0x00284280`) is entered from
  `Human_HandleMessage` (return address `0x002473c4`) with 665 as the anim, and `P3.UseFlash` schedules
  `P3.MissionOver` 1.5 s later. Confirmed (runtime), run r-20261024-flash-dealer-665 (from the state library's
  `breakable-fence-flash`, 314 of 900 health, the dealer reached with the stick at 60 %).
- At full health the press does nothing, as no upgrade (6, 8) is unlocked so early (`Player_UseItemCommand`
  `0x002843f8`, [the flash](combat-rage.md#rage)), while the handler still ends the mission: no clip, no sound, then the
  closing scene `l99_c9`. Confirmed (code) at `0x002843f8`.

## In Coney

`src/human/human.cpp` (`Human::spawn`) is both a human's creation and every placement: the scripts' teleports, a
scene's release, the player's hand-over and the debug teleport. It keeps the health the human had; a human just made
has full health, so a creation still starts full. Until issue #234 it rebuilt the fighter at full health on every
placement, so in the story the player reached the dealer at 900 of 900 and the flash did nothing visible before the
closing scene. Stand-in: the rest of the fighter's state (a stun, the rage and power meters) still restarts on a
placement ([q-teleport-reset](#q-teleport-reset)). Disc test: `tests/platform/disc_flash_test.cpp`, the level99 story
case.

## Open questions

### What else a placement resets {#q-teleport-reset}

What `Object_TeleportToFlag`'s human slot `+0x14c` does is not traced, and whether the rage, the power and a stun
survive a teleport is not checked. A scene's release is taken to place a human the same way (the level99 lesson needs
the 35 % to survive `l99_c2`). To settle it: hurt, enrage and stun a human in PCSX2, `TeleportToFlag` it and read the
meters; then end a scene on him and read them again.
