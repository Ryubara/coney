---
kind: feature
---
# Slow motion: what turns it on and off

Verified against: `SLUS_212.15` (NTSC-U), its Ghidra listing, and the disc's character animation sets.

## Summary

A finishing move (a grab or mount power move, a tandem's end, the bottle smash) slows the characters to a fifth of their
speed for a moment while the frames, the cameras and game time keep their rate. The step itself, the factor
`CfgFollowCamera` gives it and the clips that carry the events are on the camera page
([Slow motion](camera.md#slow-motion)). This page is about the switch: which events and calls turn slow motion on and
off, and why a power move must never be cut short, which is what left Coney's players in slow motion for good after a
power move that did not kill.

## Rules

- A player's clip turns slow motion on with event `0x2e` and off with `0x2f`; the step is slowed while any player is
  marked.
- Four other things turn a player's slow motion off: his paired victim clip ending or being cut off, his knock-out, any
  human leaving normal mode (for both players), and his leaving the game.
- An attacker's own power-move clip has no off of its own besides its `0x2f`: the move plays out because nothing in a
  power move can cut it, neither the power meter nor a third human's hit.

## Map

| Behaviour | Function | Level |
| --- | --- | --- |
| The clip events switch it | `Anim_FireFrameEvents` (`0x00101dd8`) calls `SlowMotion_On` (`0x0041ab30`) and `SlowMotion_Off` (`0x0041ab60`) | confirmed (code) |
| A paired victim clip ends, is cut off or freed | `AnimTaskPairedChained_End` (`0x00108f10`), `AnimTaskPairedChained_CutOff` (`0x00108dd0`), `AnimTaskPairedChained_Release` (`0x00109018`) | confirmed (code) |
| A knock-out | `Human_KnockOut` (`0x00230328`) | confirmed (code) |
| Leaving normal mode | `Human_ResetForMode` (`0x002325e0`) | confirmed (code) |
| A player leaves | `PlayerList_Remove` (`0x00419c68`) | confirmed (code) |
| A power move stops being a grab | `Attack_StartPaired` (`0x00262ac8`) → `Grab_BreakOnHit` (`0x0022b960`) | confirmed (code) |
| The meter drains and lets go only in a grab | `Human_UpdateMeters` (`0x002562d0`) | confirmed (code) |
| The meter does not refill in a paired move | `Human_UpdateDown` (`0x00256a60`) | confirmed (code) |
| A hit on the paired attacker plays no reaction | `Human_ApplyPendingDamage` (`0x00265f70`) | confirmed (code) |

## Behaviour

### The clip events {#events}

`Anim_FireFrameEvents` (`0x00101dd8`) runs the events of the top task's clip as its cursor passes their frames, so a
clip that hands over to its next task fires the rest of its events first. On `0x2e` it calls `SlowMotion_On`
(`0x0041ab30`) and on `0x2f` `SlowMotion_Off` (`0x0041ab60`), each with the human's player number (`+0x1b0`), and only
for a human that has one (not −1) while the game has one player (game state `0x0051489c` `+0x224` is 1). `SlowMotion_On`
marks the player (`0x0051489c` `+0x220 + player`) and sets the step `0x005102cc` to the factor × 1/30 s;
`SlowMotion_Off` clears the mark and, when neither player is marked, sets the step to 1/30 s, whatever set it before.
Confirmed (code).

### The other ways off {#off}

`SlowMotion_Off` (`0x0041ab60`) has six callers besides the event, all confirmed (code):

- **A paired victim clip** (the type 6 task, `PairedTask_Init` `0x00108a78`): its end, cut-off and release
  (`0x00108f10`, `0x00108dd0`, `0x00109018`) clear the shared state and call it for the task's own human (`+0x28`,
  the victim) when that human is a player (player record `+0x1b` set). The attacker's chained clip (type 3, built by
  `Attack_StartPaired` `0x00262ac8`) has no such call, so a grabbing player's slow motion ends only by his clip's
  `0x2f`, his knock-out or a mode reset. Confirmed (code).
- **A knock-out**: `Human_KnockOut` (`0x00230328`, called by `Human_OnHealthOut` `0x002674c0`, by `HuSetConscious` and
  when a human is destroyed) calls it for a knocked-out human with a player number. Confirmed (code).
- **Leaving normal mode**: `Human_ResetForMode` (`0x002325e0`, from `Human_SetNormalMode` `0x0023a210`,
  `Scene_BindHuman` `0x003541a0` and `GeneralParlayGoal_Init` `0x002cb348`), for any human and whatever its argument,
  calls it for both players' humans (game state `+0x228` and `+0x22c`) when the step is not 1/30 s or either player's
  scripted mark `+0x3bb` is set, and clears those marks. `Human_SetSlowMo` (`0x0023b778`, `HuSetSlowMo`) sets
  `+0x3bb` when it slows a player's step and clears it when it gives the step back, and only `Human_Init` and
  `Human_Construct` write it otherwise. Confirmed (code).
- **A player leaves**: `PlayerList_Remove` (`0x00419c68`) calls it twice. Confirmed (code).

### Nothing cuts a power move short {#power-move}

The power strike and its extensions are paired moves, no longer a grab, so the grab's own endings cannot reach them.
Confirmed (code) at the addresses cited:

- `Attack_StartPaired` (`0x00262ac8`), for a pair that is not a throw, calls `Grab_BreakOnHit` (`0x0022b960`), which
  ends a grab or tackle on both sides (`Grab_End` twice, so the grab's states `0xc0` / `0x30` go) and sets the
  attacker's state `0x1000` and the victim's `0x2000`. `Player_UpdatePowerMove` (`0x0027df38`) starts the extensions
  through it too. Its other callers start the other paired moves the same way: `Grab_StartPairedMove` (`0x00263380`),
  `Grab_Reverse` (`0x0026d150`), `Grab_Strike` (`0x0026e5f8`, a defeating strike) and the mount's `Mount_StartStrike`
  (`0x00270b38`), `Mount_GetOff` (`0x00271098`), `Mount_Strike` (`0x00271ef0`) and `Mount_StartReversal`
  (`0x002723e8`). This page covers the grab's power-move path; what the others do after the call is not traced here.
- `Human_UpdateMeters` (`0x002562d0`) drains the power meter (record `+0x148`) only while the human is grabbing
  (`0xc0`), tackling or blocking, and on an empty meter lets go, lets the victim escape or gets off only while he is
  grabbing or tackling, and not while his clips hold any of `0xc02201`.
- `Human_UpdateDown` (`0x00256a60`), which refills the power meter, returns before the refill while the human's state
  holds any of `0x1c18003ff0`, which takes in the paired attacker's `0x1000`: so the meter neither drains nor refills
  during a power move.
- `Human_ApplyPendingDamage` (`0x00265f70`): a third human's hit on a human in state `0x1000` who is not grabbing takes
  his health and, as a pair's member, the share `0x00510274` of his power, then clears the pending damage with no
  reaction.

At runtime the meter held through the power strike and did not let go (confirmed (runtime), run
r-20261025-power-move-meter): with PoizoCiv grabbed and the meter written to 110, the strike left 9; 30 frames into
57 it still read 9 and 57 still played; 57 ran to its end and the fight idle 358 followed with no let-go (95), and the
meter then refilled (21 another 60 frames on).

### Which power moves slow time without killing {#non-lethal}

From the playable Warriors' own animation sets (Coney's loader; anim ids and event frames only), most grab power moves
that carry the pair are extensions, and several deal far less than a kill: Vermin's and Cowboy's 59 (75 damage, on at
frame 10 / 11 and off at 15 of 24 / 28 frames), Ajax's 61 (75) and Snow's 61 (69), Cochise's 65 (80, on at 19 and off
at 20, with its extension window `0x2c` at 22), and the mount's 227 (Vermin) and 229 (Cochise, Ajax, Snow). Every
Warrior's 67 and 235 deal 1000, and Rembrandt's only grab one, 67, follows 65's 1000. Inferred from the clips' events
and the Anim Range Lists' damage.

## In Coney

- `src/animation/anim_task.*`: `AnimTaskStack::firedEvents()` lists the events each advance fired, a clip handing over
  firing up to its end; `Human::noteSlowMotion()` (`src/human/human.cpp`) takes the last `0x2e` / `0x2f` from them.
- `src/human/player.cpp` (`Player::update()`) switches `camera::SlowMotion` (`src/camera/slow_motion.*`) after the
  characters' step: a human that left normal mode (`Human::setNormalMode()`, so also a scene's bind) resets it for
  every player (`SlowMotion::reset()`), the driven human's knock-out turns his off (`Human::endSlowMotion()`, from
  `Brains::reportDowns()` and `HuSetConscious(false)`), then his clip's event.
- `src/combat/player_combat.*`: `PlayerCombat::holdsVictim()` leaves the grab's power move out of the hold, so the meter
  neither drains nor lets go while it plays. Coney keeps the grab's combat mode until the power move ends; until issue
  #733 its meter drained there and an empty meter let go with 95 / 94, which in slow motion (the move five times as
  many updates long, the drain on game time) cut an extension before its `0x2f` and left the step at 1/150 s.
- `PlayerCombat::updatePower()` holds the meter during the grab's power move (`PowerMeter::hold()`): no drain and no
  refill, as `Human_UpdateDown` skips the paired attacker. Until this was fixed it refilled there at the class's rate.
- The mount's power strike keeps the tackle in Coney (`CombatMode::Tackling`), so its meter still drains while it plays,
  where the original's `Grab_BreakOnHit` ends the tackle too (#684). Coney plays no mount extension (227, 229, which
  carry the slow-motion pair), so no mount move is cut before its `0x2f` today; whoever ports those extensions must
  end the tackle at the power strike first, or the stuck slow motion of #733 comes back.
- Coney drains no power while blocking, and its empty-meter let-go tests the meter alone, without the original's
  `0xc02201` and victim tests (#719); neither reaches a power move, which `holdsVictim()` leaves out.
- **Coney stand-ins**: a paired victim clip's end does not turn a player victim's slow motion off, Coney's victim clips
  being plain chained clips ([q-victim-clip](#q-victim-clip)); a player's removal and the one-player test are not built,
  Coney having one player.

## Open questions

### A player as the victim {#q-victim-clip}

Which Coney paths play a slow-motion victim clip on a player (a tandem's 166 or 184, an AI's power move on a grabbed
player). Each needs the victim clip's end to turn the player's slow motion off, which Coney does not build yet. To
answer it: list the Coney calls that start a paired victim clip on a human that may be the player (the `playPaired`
calls in `src/human/fighter_grab.cpp` and `src/human/fighter_counter.cpp`, and the tandem's), and in PCSX2 let an AI
grabber's power move or a tandem play on the player with a watch on his slow-motion mark (game state `0x0051489c`
`+0x220`) to see the victim clip's end clear it.

## How this was checked

The callers of `SlowMotion_Off` and the functions above were read in Ghidra. The power meter run (2026-10-25) is
r-20261025-power-move-meter, on the `pedestrian-grab` state with the `scripted-pad` and `right-stick` patches, the pad
driven frame by frame. The clip survey loaded each playable Warrior's set with Coney's loader on the disc and printed
the ids, frames and damage of the clips that carry `0x2e` / `0x2f`.
