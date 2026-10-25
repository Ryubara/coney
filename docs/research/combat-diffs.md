# Combat: where Coney differs from the original

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`) through
[Combat](combat/index.md) and [Combat moves](combat-moves.md); Coney's side read from `repo:src/combat/`,
`repo:src/human/` and `repo:src/ai/` as of 2026-10-07 (main at `f4562750`), as behaviours only.

## Purpose

A worklist for the combat implementers: each place Coney's fight plays differently from the original, what the
original does (with a link to the research) and where Coney's side lives. Coney's documented **choices** and
**stand-ins** (on [Combat](combat/index.md#coneys-implementation)) are listed too when they change what the player sees.
Remove an entry when it is fixed, in the same commit. The original's evidence levels are on the linked pages; the
Coney side is confirmed by reading the source.

## Original structure

The original functions behind each entry are on the page it links, in that page's own Original structure and code
index ([Combat](combat/index.md#original-structure), [Combat moves](combat-moves.md#original-structure)). The few an
entry here names with an address (the mid-target test, the snap's search and filter, `Player_Special`) are graded where
they are cited.

## Data

This page holds no data of its own: the commands, flags, tables and constants are on [Combat](combat/index.md#data) and
[Combat moves](combat-moves.md#data).

## Behaviour

The differences, by subject. Each entry says what the original does, linking the page that grades it, and what
Coney does instead; an address cited here carries its level beside it.

### Commands and input

- **Commands the dispatcher ignores**: 3 (the AI's counter, `Player_TryCounterGrab`), `0x36` (push 21), `0x37` /
  `0x38` (ground strikes 193 / 194), `0x39` (throw the melee weapon), `0x32`, `0x33` and `0x27` reach no handler
  ([AI and script commands](combat-moves.md#ai-commands)). The AI's block goal presses R1 (command 3), so an AI never
  counters a grab or tackle. Coney: `repo:src/combat/player_combat.cpp`.
- **The chain commands** `0x11` / `0x12` of the AI are read as square's by Coney's choice; the original writes them
  for the AI's chains only ([Attack kinds](../references/attacks.md#attack-kind)).

### Timing and targets

The original's rules are in one place: [Timing](combat-moves.md#timing), [Input](combat-moves.md#input) and
[Targets](combat-targets.md#targeting). Coney (main at `f4562750`): `repo:src/combat/attacks.cpp`,
`repo:src/human/fighter.cpp`.

- **The hit update of a body without shapes.** The original deals a free attack's damage only through the strike
  test (`Strike_Contact`, `0x0021b290`, confirmed (code)): an attack whose clip switches no strike shape never hits.
  Coney does the same for every human with a skeleton (`Human::testStrikes`), within one update of the original at
  1.0 m ([Runtime results](combat-moves.md#runtime)), but a victim with no strike shapes (the sandbox's targets) and
  an attack whose clip switches no shape still take the hit on one update per id: the measured contact, or update 2
  (`repo:src/combat/attacks.cpp`).
- **The attack's target.** Coney searches afresh for every attack along the stick with the attack's own far range
  × 1.1 (`Fighter::steer`). The original keeps the current target for square in the stance (searching only without
  one or beyond 3 m), searches for cross with a fixed 2.0 m, keeps the target on a square chain step, and uses the
  far range only through `Player_FindAttackTarget` (cross chain steps, the specials).
- **Taking a target by nearness.** Coney takes a target only from L1 or an attack; the original's stance logic takes
  the nearest enemy within 2 m every update (and locks it with the street's `CfgAutoLockAndCombat`), swaps it once the
  target is beyond 3 m and tracks enemies to 6 m.
- **Blend into an attack.** The original enters each new attack over 0.2 s and swaps a chain step's clip in place;
  Coney fades every attack in over 0.1 s (`kCombatFade`).

### Square and cross

- **Order of tests**: the original tests the moving attacks first (with only a low-target check inside them), then,
  in the stance, strafes, snaps, the target re-pick, the low, mid and front-grab strikes, the tandem, the counters
  and `S1` ([Square](combat-moves.md#square)). Coney tests the target's state first, outside the stance too, so a
  run at a mounted or grabbed human plays 212 or 120 instead of 24. Coney: `repo:src/combat/attacks.cpp`.
- **Cross at a low or held target**: the original plays 194 at a low target, 661 at a mid one and 120 at a front
  grab ([Cross](combat-moves.md#cross)); Coney's cross has no target branch (always `X1` standing).
- **Mid target**: the original's 212 is chosen by a height and state test (`Human_IsMidOrBusy`, `0x002250a0`,
  confirmed (code): the attacker 0.7-1.3 m above the target, or, unless he is more than 0.7 m below it, the
  target in a state or attack phase its flags name), not
  "mounted";
  [Grounded and mid strikes](combat-moves.md#ground).
- **Strafe attacks 31-33** are not built ([Strafe attacks](combat-moves.md#strafe)).
- **Counters from square and cross** (76 / 9 against a grab or tackle intro) are not built; only R1 at the catch is
  ([Counters](combat-moves.md#counters)).
- **Tandems** are built for any fighter's square, cross and circle, with Coney's choices for the alignment, the
  grabber's let-go and the target search, and no scoring; the AI's side (its target view's `0xc12200` test and kind
  22's choice at a rear-held target) is issue #84's ([Coney's tandem](combat-moves.md#tandem-coney)).
- **The moving attacks' windows** open late: on the disc Coney's run attack 24 switches its shapes on at update 4 and
  off at 9, the walk attack 23 at 6 and 12, against 1 and 7, 4 and 10 at runtime
  ([How a moving attack strikes](combat/index.md#moving-strikes)). Coney fires the clips' events by the clip time from 0
  on the clip's first update ([Animation](formats/animation.md#animation-tasks)); why the original's open about two
  updates earlier is open ([below](#open-questions)).
- **The armed run attack 501** does not test the fight stance in Coney; the original's armed branch does
  ([Square](combat-moves.md#square)).
- **The snap's target**: the original's snap search has no line test: what an earlier reading here called the clear
  line (`0x00222a90`) is `Human_IsFriendly`, which the snap's filter `TargetFilter_Enemy` (`0x00279410`) calls
  (confirmed (code) in `Player_FindSnapTarget`, `0x0027aa38`), so a snap takes no human friendly to the player.
  Coney's search (`Fighter::snapTarget`) leaves friendliness to the targets it is given, and does not keep the
  snap's target as the current target ([Combat](combat/index.md#attacks)).
- **Object targets**: only glass panes; doors and loose objects are not targets, and the approach from far × 1.5 is
  missing ([Breakables](combat-targets.md#breakables)).

### Stance

- **The fight stance** is a stand-in (locked onto a target), not the original's enter and leave rules
  ([The fight stance](combat-targets.md#fight-stance)). Every gate that tests the stance (moving attacks, strafes, the
  snaps' path) inherits it.

### Grabs, tackles and the mount

- **The tackle** always connects with 5 / 6 (the front hit), without the original's connect gates; 7, the hit from
  the rear, never plays ([Grab and tackle](combat-grabs.md#grab)).
- **Wall throws** (155-161) are picked by the original's ray and a head-on one turns the pair square to the face at
  once, not over 0.01 s, and the slam's `0x41` sweep (the dust puff, the impact sound and a world object struck) is
  not built ([Throws](combat-throws.md#throws)). **Coney's choice**: the thrown body does not strike its own thrower.
- **Where a plain throw goes**: the pair is turned onto the stick over 1/6 s from the front hold and the rear hold
  spins to the front first, as in the original, but the throw clips' own root turn is about a third larger than at
  runtime (149 turns the thrower −120° for −90°) and the front victim lands 19.2° right of the stick (2.1° at
  runtime): an open question on the root turn and the task's rate
  ([Where a throw goes](combat-throws.md#throw-direction)).
- **The mounted victim** cannot struggle (250), get the mounter off or reverse (242 / 243)
  ([The mounted victim](combat-moves.md#mounted-victim)).
- **The mount's power strike** stops at its first part: the extensions (227 / 228, 233 / 234; 229 is a filler that
  never plays) a press in the window plays are missing ([The mount](combat-grabs.md#mount)).
- **The mount's power strike does not end the mount**: in the original the strike breaks the pin (the mounter stands
  in the fight stance, the victim stays grounded and is no longer mounted); Coney goes back to the mount (210 / 207)
  ([The mount](combat-grabs.md#mount)).
- **Circle with no target** does not search at far × 1.25 for a grab or tackle target in every case
  ([Grab and tackle](combat-grabs.md#grab), step 2).
- **Escapes**: human flag `0x20000` (got off at once) is not modelled; the release clips by side are partly stand-ins
  ([Grabbed](combat-grabs.md#grabbed)).
- **Mugging**: the victim's qualification (class, money, brain state) is not tested ([Mugging](combat-throws.md#mugging)).

### Being hit

- **Damage modifiers**: the held weapon's bonus (`CfgObj` `+0x58` × the power class factor), the ×2 flag and the
  ×3 against a cuffed human are missing ([Damage](combat-reactions.md#damage)).
- **Stun and ground times** are not scaled as the original scales them ([Reactions](combat-reactions.md#reactions)).
- **Reactions while held**: a hit on a human in a grab or mount does not play the held reactions.
- **Knocked out and the mission failing**: the defeat path is in progress on the mission branches
  ([Knocked out](combat-reactions.md#defeat)).
- **The power meter** is fixed at 400 with a refill of 60 per second; the original takes both from the human's power
  class and the Warrior upgrades ([Power meter](combat-rage.md#power-meter)).

### Specials, rage and stealth

- **Rage** starts only through the block branch (L1 + R1 while blocking), not by the original's other routes
  ([Rage](combat-rage.md#rage)).
- **Stealth** (hiding in shadow, 630 / 633, the stealth kill 637-641) is not built ([Stealth](combat-moves.md#stealth)).
- **Flash revive** (command `0x27`): built for player 1, every Warrior revivable from his creation
  ([Combat: revive](combat-reactions.md#revive)); an AI Warrior reviving ([SaveHuman](ai-goals.md#goal-save-human)), a
  co-op partner's flash and a knocked-out human waking on his own are not.

### Weapons

- **Sets 4-6** (barrels, bottles, the ghetto blaster): square and cross play the unarmed moves; the original throws
  (505-507, 467-472, 551-553) or smashes (473 / 475) ([Weapons](combat-throws.md#weapons)).
- **Knife and bottle kills** (484-496) and **throwing a melee weapon** (491 / 502, next entry) are not built.
- **Throwing a melee weapon with cross + square** (`0x22`, 502 / 491): the original throws a knife, baton or bat unless
  a target is within 2.0 m (then it does nothing, and the cross's release swings 36). Coney refuses the command for a
  player holding any object, as `Player_Special` (`0x00287730`, confirmed (code)) does, and never throws: with nobody
  near the cross's release swings where the original throws. The special 653 and circle + cross's strong grapple no
  longer start with an object in hand (fixed with issue #228; [Cross with a bat in hand](combat-throws.md#bat-cross)).
  Coney: `repo:src/combat/player_combat.cpp`.
- **Clip lengths**: Coney ends a clip when its time reaches its duration, ceil(F / rate) updates; the original ends
  it on the update its frame would reach F, one update sooner on 50 of the 174 clips in the move tables (19, 20, 34
  and the dive among them) and on 389, which they do not list ([When a clip ends](formats/animation.md#clip-end)).
  Coney ends clip 34 alone a step early (a stand-in) so that cross works from k37 as in the original; the rule can
  now replace it. Coney:
  `repo:src/animation/anim_task.cpp`, `repo:src/human/human_animator.cpp`.
- **Wear**: weapons never break; the original wears them on the `use` event.
- **Armed chain**: Coney's armed square plays the slot's swing each time; the original alternates 39 / 40 (baton)
  and 45 / 46 (knife) for a square pressed in the swing. With a bat the two agree: 34 has no chain window, so the
  original never plays 35 from the pad either ([Cross with a bat in hand](combat-throws.md#bat-cross)).
- **Blocks against weapons**: the original's rules by set (no block of an armed hit unarmed, the bat's 629, the armed
  block clips 621-624) are missing ([Weapons](combat-throws.md#weapons)).
- **Armed AI**: no AI human holds or uses a weapon.

### The AI in a fight

- **Attack kinds**: the AI plays its strikes and chains; its grabs, tackles, throws, specials and the kinds that need
  commands Coney ignores do nothing ([Attack kinds](../references/attacks.md#attack-kind)).
- **A human held in another human's grab** (the player in an AI's, or an AI in the player's) struggles, strikes back
  and escapes by its command as `Player_UpdateGrabbed` decides, the grabber playing its side an update later; the
  **reversal** (R1) is not offered this way ([Grabbed](combat-grabs.md#grabbed)).

## Coney's implementation

The Coney side of each entry is named in it. The files the entries cite: `repo:src/combat/player_combat.cpp` (the
dispatcher and the commands it takes), `repo:src/combat/attacks.cpp` (which attack a press starts),
`repo:src/human/fighter.cpp` (targets and the steer), `repo:src/platform/play_level_objects.cpp` (moving attacks on
objects), `repo:src/animation/anim_task.cpp` and `repo:src/human/human_animator.cpp` (clip timing). Coney's choices
and stand-ins as a whole are listed on [Combat](combat/index.md#coneys-implementation).

## Open questions

- **The moving attacks' windows**: why the original's run and walk attacks switch their shapes on about two updates
  before Coney's clip events do ([How a moving attack strikes](combat/index.md#moving-strikes)). Found by tracing the clip's
  first update in the original's animation task against Coney's.
- **A throw's root turn**: why the throw clips' root turn is about a third larger in Coney than at runtime, and
  where the task's rate differs ([Where a throw goes](combat-throws.md#throw-direction)).
