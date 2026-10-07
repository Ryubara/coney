# Combat: where Coney differs from the original

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`) through
[Combat](combat.md) and [Combat moves](combat-moves.md); Coney's side read from `repo:src/combat/`,
`repo:src/human/` and `repo:src/ai/` as of 2026-10-07 (main at `f4562750`), as behaviours only.

## Purpose

A worklist for the combat implementers: each place Coney's fight plays differently from the original, what the
original does (with a link to the research) and where Coney's side lives. Coney's documented **choices** and
**stand-ins** (on [Combat](combat.md#coneys-implementation)) are listed too when they change what the player sees.
Remove an entry when it is fixed, in the same commit. The original's evidence levels are on the linked pages; the
Coney side is confirmed by reading the source.

## Commands and input

- **Commands the dispatcher ignores**: 3 (the AI's counter, `Player_TryCounterGrab`), `0x36` (push 21), `0x37` /
  `0x38` (ground strikes 193 / 194), `0x39` (throw the melee weapon), `0x32`, `0x33` and `0x27` reach no handler
  ([AI and script commands](combat-moves.md#ai-commands)). The AI's block goal presses R1 (command 3), so an AI never
  counters a grab or tackle. Coney: `repo:src/combat/player_combat.cpp`.
- **The chain commands** `0x11` / `0x12` of the AI are read as square's by Coney's choice; the original writes them
  for the AI's chains only ([Attack kinds](../references/attacks.md#attack-kind)).

## Timing and targets

The original's rules are in one place: [Timing](combat-moves.md#timing), [Input](combat-moves.md#input) and
[Targets](combat-moves.md#targeting). Coney (main at `f4562750`): `repo:src/combat/attacks.cpp`,
`repo:src/human/fighter.cpp`.

- **The hit update.** Coney lands every attack on one update per id: the measured contacts for 11, 13-17 and 19 and
  the grab strikes, and **update 2 for every other id**, the special 653 included, whatever the distance. The
  original lands a hit only when a strike shape touches the target, inside the clip's shape window: 653's hands are
  live at 2-8 and touched at 6-7 at runtime, so in Coney cross + square hits about 4 updates early and hits a target
  the hands never reach. The snaps, strafes, low and mid strikes, 193 / 194, 212, 661, 120 and 22 have the same
  problem. A moving attack's shapes (charge, dive, 23, 24) are used only against objects; on a human it too hits
  2 updates in (`repo:src/platform/play_level_objects.cpp`).
- **The attack's target.** Coney searches afresh for every attack along the stick with the attack's own far range
  × 1.1 (`Fighter::steer`). The original keeps the current target for square in the stance (searching only without
  one or beyond 3 m), searches for cross with a fixed 2.0 m, keeps the target on a square chain step, and uses the
  far range only through `Player_FindAttackTarget` (cross chain steps, the specials).
- **The search's wide passes.** Coney runs the × 0.9 any-angle pass whatever the current target; the original runs
  it only with no current target and otherwise falls back to × 0.7 at any angle (Coney drops that pass).
- **Taking a target by nearness.** Coney takes a target only from L1 or an attack; the original's stance logic takes
  the nearest enemy within 2 m every update (and locks it with the street's `CfgAutoLockAndCombat`), swaps it once the
  target is beyond 3 m and tracks enemies to 6 m.
- **Blend into an attack.** The original enters each new attack over 0.2 s and swaps a chain step's clip in place;
  Coney fades every attack in over 0.1 s (`kCombatFade`).

## Square and cross

- **Order of tests**: the original tests the moving attacks first (with only a low-target check inside them), then,
  in the stance, strafes, snaps, the target re-pick, the low, mid and front-grab strikes, the tandem, the counters
  and `S1` ([Square](combat-moves.md#square)). Coney tests the target's state first, outside the stance too, so a
  run at a mounted or grabbed human plays 212 or 120 instead of 24. Coney: `repo:src/combat/attacks.cpp`.
- **Cross at a low or held target**: the original plays 194 at a low target, 661 at a mid one and 120 at a front
  grab ([Cross](combat-moves.md#cross)); Coney's cross has no target branch (always `X1` standing).
- **Mid target**: the original's 212 is chosen by a height and state test (`0x002250a0`), not "mounted";
  [Grounded and mid strikes](combat-moves.md#ground).
- **Strafe attacks 31-33** are not built ([Strafe attacks](combat-moves.md#strafe)).
- **Counters from square and cross** (76 / 9 against a grab or tackle intro) are not built; only R1 at the catch is
  ([Counters](combat-moves.md#counters)).
- **Tandems** are not built ([Tandems](combat-moves.md#tandem)).
- **The run attack's window**: Coney's 24 shapes run from update 1; the clip's events give 2-7.
- **The armed run attack 501** does not test the fight stance in Coney; the original's armed branch does
  ([Square](combat-moves.md#square)).
- **The special** always plays 653 / 645: the original's id adds 4 × the variant and the side (`0x00263c90`,
  [Combat](combat.md#run-attacks)).
- **The snap's target**: Coney does not test the clear line (`0x00222a90`) and does not keep the snap's target as
  the current target ([Combat](combat.md#attacks)).
- **Object targets**: only glass panes; doors and loose objects are not targets, and the approach from far × 1.5 is
  missing ([Breakables](combat.md#breakables)).

## Stance

- **The fight stance** is a stand-in (locked onto a target), not the original's enter and leave rules
  ([The fight stance](combat.md#fight-stance)). Every gate that tests the stance (moving attacks, strafes, the
  snaps' path) inherits it.

## Grabs, tackles and the mount

- **The tackle** always connects with 5 / 6 (the front hit), without the original's connect gates; 7, the hit from
  the rear, never plays ([Grab and tackle](combat.md#grab)).
- **Wall throws** (155-161) never play: no caller sets the wall-in-reach input ([Throws](combat.md#throws)).
- **The mounted victim** cannot struggle (250), get the mounter off or reverse (242 / 243)
  ([The mounted victim](combat-moves.md#mounted-victim)).
- **The mount's power strike** stops at its first part: the extensions (227 / 229, 233 / 235) a press in the window
  plays are missing ([The mount](combat.md#mount)).
- **Circle with no target** does not search at far × 1.25 for a grab or tackle target in every case
  ([Grab and tackle](combat.md#grab), step 2).
- **Escapes**: human flag `0x20000` (got off at once) is not modelled; the release clips by side are partly stand-ins
  ([Grabbed](combat.md#grabbed)).
- **Mugging**: the victim's qualification (class, money, brain state) is not tested ([Mugging](combat.md#mugging)).

## Being hit

- **Damage modifiers**: the held weapon's bonus (`CfgObj` `+0x58` × the power class factor), the ×2 flag and the
  ×3 against a cuffed human are missing ([Damage](combat.md#damage)).
- **Stun and ground times** are not scaled as the original scales them ([Reactions](combat.md#reactions)).
- **Reactions while held**: a hit on a human in a grab or mount does not play the held reactions.
- **Knocked out and the mission failing**: the defeat path is in progress on the mission branches
  ([Knocked out](combat.md#defeat)).
- **The power meter** is fixed at 400 with a refill of 60 per second; the original takes both from the human's power
  class and the Warrior upgrades ([Power meter](combat.md#power-meter)).

## Specials, rage and stealth

- **Rage** starts only through the block branch (L1 + R1 while blocking), not by the original's other routes
  ([Rage](combat.md#rage)).
- **Stealth** (hiding in shadow, 630 / 633, the stealth kill 637-641) is not built ([Stealth](combat-moves.md#stealth)).
- **Flash revive** (command `0x27`, 665) is not built.

## Weapons

- **Sets 4-6** (barrels, bottles, the ghetto blaster): square and cross play the unarmed moves; the original throws
  (505-507, 467-472, 551-553) or smashes (473 / 475) ([Weapons](combat-moves.md#weapons)).
- **Knife and bottle kills** (484-496) and **throwing a melee weapon** (491 / 502) are not built.
- **Wear**: weapons never break; the original wears them on the `use` event.
- **Armed chain**: Coney's armed square plays the slot's swing each time; the original alternates 34 / 35 (39 / 40,
  45 / 46).
- **Blocks against weapons**: the original's rules by set (no block of an armed hit unarmed, the bat's 629, the armed
  block clips 621-624) are missing ([Weapons](combat-moves.md#weapons)).
- **Armed AI**: no AI human holds or uses a weapon.

## The AI in a fight

- **Attack kinds**: the AI plays its strikes and chains; its grabs, tackles, throws, specials and the kinds that need
  commands Coney ignores do nothing ([Attack kinds](../references/attacks.md#attack-kind)).
- **A held AI** does nothing in the grab: no struggle, strike back or escape ([Grabbed](combat.md#grabbed)).
