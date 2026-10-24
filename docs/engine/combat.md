# Engine: combat

How Coney builds the player's combat. What the original does is on [Combat](../research/combat.md) and its topic pages;
this page says where each part of it lives in Coney, how the parts work together, which tests hold them and where Coney
chooses because the research is silent or the port differs on purpose. The comments beside the code say why each
function does what it does; this page does not repeat them.

## Layout {#layout}

`src/combat/` holds the player's combat rules as a self-contained core: it decides, and `src/human/` plays what it
decides through the human's animator and lands the hits. Everything runs on the fixed 1/30 s step; time-based meters
take game time in whole milliseconds and carry the fraction of a point, as the original does; the coin flips come from
a seeded generator (`CombatRandom`), so a run with the same seed and input is the same run.

| File | What it does |
| --- | --- |
| `combat/commands.*` | the nine trigger tables (`CommandTables::street()` is the street's), and the matcher that turns each update's buttons into one command in the documented order, with the tap (1-6 samples), long hold (4th sample, or a release within 3) and history hold (7) counted per button |
| `combat/attacks.*` | square's choice (target, run, walk, a snap with a target found, `S1`), cross's (run, walk, `X1`), the armed branch of a knife, baton or bat in hand (501 at a run, the grounded strike, the set's swing), the object attack's clip, the charge and dive condition, the chain table, `attackHitUpdate()` (the measured hits of the [timing table](../research/combat.md#attacks)) and `AttackChain`: the chain read from the record's `+0x08` as the attack's clip holds it ([Tasks](../research/tasks.md#held-flags)), one buffered press, the hit counted in updates, the attack over once its clip has given its bits back |
| `combat/anim_ranges.*` | the Anim Range List decoded from the character data's chunk (direction, reach, far range, damage, hit code, flags), and `applyClassDamage()`: a class's damage table written over it by the index → anim id table, scaled for a player |
| `combat/reactions.*` | the hit code taken apart, the victim's side, `hitReaction()` (the strength, height and direction rules, the combo attacks 13-15 and 17-20, and the table at `0x00510798`), the dying reaction, the block reactions and when a block holds |
| `combat/power_class.h` | a human's power class: the power maximum and refill, the hurt fraction and the power factor while hurt, the stun and ground times and the struggle divisor (`+0x36`); the player's and the street civilian's |
| `combat/being_hit.*` | the victim's decisions: the warnings an attacker's clip sends (events `0x24` duck, `0x26` early block), the duck's counter (event `0x25` in 616, the commands that ask for it, 617-620 by side), the hit armour, the health floor, `blockHit()` and the mash's cut |
| `combat/grabbed.*` | held in another human's grab (`Player_UpdateGrabbed`): the struggle and its cost to the grabber, the escape or reversal roll, the strike back 104 and the counter at the catch (76 / 77) |
| `combat/rage_awards.*` | the rage a hit gives its attacker: the two awards per attack (event 2 and event 1), halved when blocked, and the repeat tracker (`+0x182`: six of a kind within 5 s halves later hits; the throw bonus) |
| `combat/lock_on.*` | the lock-on rule by `CfgLockOn` and the auto settings, the combat walk's clip by the stick's angle (380-387), when a target is dropped, and the grab's turn step (`0x00245310`) |
| `combat/meters.*` | health, the pending damage (the update's largest), `strikeDamage()`, the power meter (its power class's maximum and refill, 400 and 60/s for class 64; drain 15/s, `spend()`) and the rage meter (its Warrior class's maximum, gain, decay and drain, `RageClass`: 78, 144 %, 7.8/s and 9.36/s for class 6; the gain formula truncated per award, the 5 s hold, start); the power maximum scaled by the hurt factor ([Stats](../research/characters.md#stats-coney)) |
| `combat/grab.*` | when a grab may start, the search ranges (far range × 1.25), the nearest-candidate search, the throw by stick side and wall, and one update of a grab (strikes, the power strikes with the rear spin first, throws, the release with too little power, the R1 and circle spins, the L2 let-go, the mugging; nothing spent in rage) |
| `combat/stick_games.*` | the mugging, the stereo theft's rotation (mode 3) and the button mash (mode 1) |
| `combat/player_combat.*` | the dispatcher: block, chain, meters, the routes (grabbing, tackling, mugging, theft) and the commands, in the original's order; the grab breaks at 0 power |
| `combat/combat_tuning.*`, `debug/combat_tunables.*` | the values above as tunables, category **Combat**, registered at start-up beside the game's |
| `human/fighter.*`, `human/fighter_grab.cpp`, `human/fighter_victim.cpp`, `human/fighter_clips.h` | the player's combat inside the human (split as the attacker, the grab and the victim side): builds `PlayerCombat`'s input (the camera-turned stick in the facing frame, the pad's stick, the gait, game time, the target in front, the grab search and the snap's search), plays its clips, turns and slides into an attack, poses a grab (the alignment, the connect, the gate, the snap and the attachment), turns and walks the grab by the stick, lands the hits with their rage, locks onto a target and combat-walks round it; and the player hit (a duck and its counter, the block, the health floor, the hit armour, the reaction, stun, knockdown and mash) and held in a grab (the counter at the catch, the struggle, the strike back, the escape and the reversal) |
| `world_objects/pickups.*`, `gamemodes/level_pickups.*` | triangle with the objects of a level ([Crimes: triangle](../research/crimes.md#triangle), steps 4 and 5): message 0 to the nearest object with a prompt (`SetMsgHandlerEx`), then to each object in reach, a true result taking the press; the search (the [pickable](../research/objects.md#pickable) classes, reach, the two sight rays, the score by the direction from behind the feet) and the clip; the take, which adds a `TYPE_SPECIAL`'s loot with notify and its value in money without and removes the record, or puts any other kind in the hand ([A bat in hand](../research/combat-throws.md#bat)); with something in hand and nothing taken, the drop. The human plays the clip with a 0.2 s blend and takes the object at its first event (`Human::startPickUp`); the play mode gives the fighter the held type's anim set, whose square, cross and two strikes `combat::animSetClips()` gives |
| `human/human_flags.h`, `human/fighter_script.cpp` | the [human flags](../research/combat.md#human-flags) as the fighter keeps and reads them: god mode drops a hit's damage (**Coney's reading**: the reaction still plays), the demi-god floor (`HuSetDemiGodMode`'s fraction, one global) sets god mode when reached, `0x80`, `0x100`, `0x200`, `0x400` and `0x200000` shape the reaction, `0x2000000` gates every rage gain, `0x100000` freezes the meter's drain and decay, `0x4000000` spends no power, `0x100000000000` and an untargetable gang are skipped by the target search; `HuRevive` and `HuSetNormalMode`. The rage handlers (`CfgRageHandlers`) are called after the characters' step (**Coney choice** of when) with the original's arguments: the full one with `(human, true)`, enter and exit with `(human, flag)`, the flag true (1) when no other human rages and nil otherwise (counted before the starter is added and after the ender is taken off, **Coney's reading**; issue #821). |
| `human/turn_and_slide.*` | the attack's steer ([Target selection](../research/combat-targets.md#targets)): a turn and a slide at a constant rate over a time, the last update only for the time left; the time to a clip's first steer-ending event; and the goal, the target led by its velocity and short of it by the reach |
| `human/victim.*` | what the player and the target share as victims: the update's largest hit, the reaction it plays, the stun (its exit waits for the clip playing to end), the knockdown, the ground time, the rise and the mash |
| `human/pair_placement.*` | the pair's geometry: offsets in the grabber's frame from a range record's direction × reach or a clip's type-8 pair event, the alignment (`Pair_AlignStart`), its time, the gate at the connect's end and `Pair_CheckPlace` |
| `human/holdable.h`, `human/human_held.cpp` | what a grab or a tackle holds (`Holdable`): the passive target or a human; a held human's clips are the grabber's, its place the grabber's while attached, and its fighter keeps the hold's state (`Fighter::enterHold`) |
| `human/target_human.*` | a passive target for the sandbox on `Victim`: health, the reaction, the stun, the knockdown, the ground time and the rise, the dying clip, the root motion of its reactions and throws |

## The update {#update}

`human::Player` runs the street's `CommandMatcher` on the pad's buttons and writes the command, the buttons and the
stick into the human's per-player record; the characters' step (`human::Humans`,
[Tasks](../research/tasks.md#humans-update)) then runs the dispatcher from that record for every human, the player's or
not, each update the human is on the ground and not climbing, after the animation and the locomotion. **A move's timing
is its clip's** ([Tasks](../research/tasks.md#held-flags)): the clip's task holds bits of the record's `+0x08` (an
attack its phases `0x7`, starting in the wind-up `0x1`; the grab's and tackle's clips `0x10`; the duck `0x1000`; its
counter `0x2000`; the run attack, the charge and the dive `0x1000000`; 389 its `0x40000000`), the clip's events `0x2c`,
`0x2d` and `0x48` open the chain window, start the end phase and the recovery, and the task gives the bits back when the
clip ends or is cut off. On Rembrandt's clips the events fall exactly on the updates measured at runtime (`S1`'s window
6, end 15, recovery 16; `X1`'s 10, 20, 21; `XX2`'s end 17 and recovery 19). Every reader takes the bits with its own
mask ([Tasks](../research/tasks.md#readers)): the recovery and the run attack's bit (`0x5c7fee0`) drop every press past
the block, the chain buffers while `0x7`, square and cross refuse on `0x100101f`, circle on `0xfc7eaf7`. The stick goes
through the locomotion gate ([Tasks](../research/tasks.md#locomotion-gate)): the attack's phases and the grab bit make
the human busy (no stick step, the clip and an attack's slide move the body), the recovery zeroes the stick's velocity,
so the stick moves the player again on the first update after the clip; 389 holds nothing the gate reads, and the stick
replaces it with the walk start at once. While the fighter's states hold the body (blocking, holding someone, mugging,
held, reacting) the stick does not move it either. Let go of R1, the player stands in the idle with the state code
`+0x14` at 5 for 5 updates, which the gate reads, and takes a press at once. Triangle keeps its own order (climb,
context action, jump) and is refused while the record drops the dispatcher's commands or the human is busy; L2 still
sprints, but not while blocking.

## Lock-on and the combat walk {#lock-on}

The attack's target search, or L1 when there is none (**Coney's choice**: L1 searches as far as a target is kept, 2.5 m;
issue #82), gives the player a target. With the original's settings (`CfgLockOn` 0, `CfgAutoLockAndCombat` 1) L1 alone
does not lock: the auto setting does, so while he has a target the player faces it every update and the stick past the
12 % dead zone walks him at 3.429 m/s in any direction, with the combat-walk clip (380-387) by the stick's angle from
his facing. The target is dropped beyond 2.5 m (unless L1 or a grab holds it), when it leaves the fight or has no health
left, and (**Coney's choice**, issue #82) when it is not standing. Grabbing, the stick past 95 % turns the pair towards
it at most 0.192 rad an update (the carry 0.8, −0.5 when the turn reverses within 45°) and pulled back walks it backward
at 1.125 m/s (front hold) or 1.22 m/s (rear), both bodies by their clips.

## Rage from hits {#rage}

Each hit the player lands or has blocked gives its two awards (`rage_awards.*`): the attack's count A × 1 point and
count B × 4 points, each through the gain formula and truncated on its own, a blocked hit halving each with a shift;
while the repeat tracker's flag is set each award is halved, and while throwing it is multiplied by the throw bonus. The
hit is then noted: six square-ended (or six cross-ended) hits in a row, each within 5000 ms of the last, set the flag;
each grab or mount strike adds 0.27 to the bonus (up to 2.0), which goes back to 1.0 every update the player is neither
grabbing from the front nor throwing. Being hit gives nothing. **Coney's choice**: the counter 76 awards 7 points and
the strike back 104 1 point (the rage seen at runtime, back-derived), as the events of those two moves are not mapped
([q-strike-back-rage](../research/combat.md#q-strike-back-rage)). A throw (147-161) gives two awards of 6 and 5 points,
back-derived from its 8 and 7 rage at runtime (13 and 11 with a throw bonus of 1.54).

## The player hit {#player-hit}

In `fighter_victim.cpp`, `Human::takeHit()`, `warn()` and `catchInGrab()` are the entry points an attacker and the tests
use. An attacker's clip warns him as its events pass: blocking, event `0x24` makes him duck (616) and the attack passes
over him, and event `0x26` plays the block's reaction early. Ducking, square or cross pressed or held while 616's own
`0x25` events fire (frames 6-13) asks for the counter, played on the next update by the side the target stands on: 617
front, 618 right, 619 back, 620 left. Each update the largest pending hit then lands in the original's order: a duck
lets it pass; a held block cancels it with its block reaction unless the hit breaks it (strength 3, or attacks 26-34);
the damage is taken as it is, held at the health floor (a hit cannot take him below 25 % of his maximum); a grabber hit
by a third human loses power; a hit that does not end him while his clip holds any of `0x1c16a40` (the charge, the run
attack and a landing `0x1000000`, the dive `0x400000`, the counter `0x2000`) only takes its health and the move plays on
([Moves a hit cannot stop](../research/combat-reactions.md#protected-moves)); winding up (`+0x08` `0x1`) or in the chain
window (`0x2`) his hit armour holds against a non-player, non-raging attacker not playing 617-620, so the damage lands
and his attack goes on; otherwise the reaction plays and his attack is lost. A plain reaction returns to the fight idle;
a stun hit holds him 200 ms, then 357 when the reaction's clip has ended (the stun's loop 356 does not play); a
knockdown lays him down for 2750 ms, then 199, and a stunned knockdown stays stunned for 200 ms after the rise. Every
command made while he lies in 196 cuts the ground time (and a stun's) by 2750 ms divided by 1, 2 or 3 at random
(**Coney's choice**: the three evenly, issue #821). The power meter's maximum is scaled by the hurt factor while hurt
(below 30 % health: 0.75 × 400).

## Held in a grab {#held}

In `grabbed.*`, R1 on the update the grabber's intro ends counters (76, the grabber plays 77, both
paired, needing and spending power); otherwise he plays the grabber's set's reaction (73 or 75) and the held loop.
Square struggles (96 / 108), costing the grabber its maximum divided by the player's struggle divisor (`+0x36`, 3);
the grab's drain of 15/s goes on through it. Cross strikes back (104 / 116, 20 damage). Circle escapes (100 / 112,
the grabber knocked down and stunned) and R1 reverses (90 / 92, then he holds the grabber from the rear, 84 / 85) on
a won roll: with `t` a quarter of the grabber's maximum (half when it is hurt) and `p` its power, always when `p` ≤
`t`, else 1 in floor(`p` / `t`); never against a raging grabber, and no reversal of one with flag `0x40`. The
grabber's power running out ends the grab, with an escape when the grabber is hurt.

## Strikes {#strikes}

[`human/strike_shapes.h`](repo:src/human/strike_shapes.h): the clip events switch the ten shapes, which are posed on the
drawn skeleton every update, and each human's strike test runs after its move against the spine and head of the humans
it fights and against the doors' enabled triangles and the panes' bodies. Every free attack hits a human this way, each
shape tested along its move since the last update (from where it was posed the update before, none on its first update),
as [where the attacker stands](../research/combat-moves.md#reach) gives; the moves in a hold, and a body without shapes,
still land at the measured hit update. The doors and panes are met where the shapes stand. A charge whose sweep loses
more than half its move to a wall stops dead until its clip ends, and a broken barrier's triangles stand for its body
until its removal. In Coney's play-through the charge strikes the fence on its 11th update, 1.06 m from the line, and
stops 0.51 m from it 3 updates later; the original also strikes on its 11th update, but at 0.55 m, and stops 1 update
later at 0.42 m. The AI's attack pick takes only the square and cross strikes and the snap, so no AI charges a human.
The player's sprint charge and dive into a human are tested in play
([`disc_sprint_attacks_test.cpp`](repo:tests/platform/disc_sprint_attacks_test.cpp): level99, a spawned fighter 9 m
ahead, the stick at 98 %): each strikes him once and plays its whole clip (27 and 62 updates in Coney, 27 and 61 in the
original) with no reaction of the attacker's ([Moves a hit cannot
stop](../research/combat-reactions.md#protected-moves)). Not built: the level mesh's impact sound, a car struck by the
shapes (issue #72), and the weapon spheres of a held object.

The capsule strike (`Human::testStrikes`, `Human::struckCapsule`, [`human/human.cpp`](repo:src/human/human.cpp)): the
clip decoder keeps the descriptor's flags, and a clip with `0x10000` tests its bone shapes against the target's capsule
as above: a segment where it stands, a sphere along its path this update. **Coney choice** (issue #821): the capsule
always has the standing height (head + 0.2); the shorter heights of some states are not built. The unit test plays 645
at a bum (flagged never to be a target) 1.0 m away, which it hits, and 1.6 m away, which it misses. The disc check finds
the two sweeps and no other clip with the flag.

## Attack warnings {#warnings}

`Fighter::warnTarget` (`repo:src/human/fighter.h`) warns the one target of an attack, as the original does
([Attack warnings](../research/combat.md#attack-warnings)). Warning everyone the attacker could fight instead would
let every attack the player starts at one `level5` boss reach the other and clear its queued move, so the boss not being
hit would rarely close in, and would let a bystander answer a stranger's swing (at `level34` checkpoint 5, a player
warned of a Rogue's swing at a Fury 20 m off would make the Warriors enemies of the Rogues; [level34 fix
14](../research/scripting.md#level34-fixes)).

The original's dive is not among `Human_WarnOfAttack`'s call sites; Coney's dive warns the one human its own search
dives at (`Fighter::nearestInReach`), and no one when it finds none
([q-dive-warning](../research/combat.md#q-dive-warning)).

## The clips {#clips}

The clips (anim ids, played through the human's animator, `AnimState::Attack` returning to the fight idle 358 and
`AnimState::Hold` keeping its loop; an attack returns to the fight idle 358 while the player has a target and through
389 to the idle 388 when he has none, **Coney's reading** of the runtime runs, issue #821): the chains `S1` 12, `SS2`
16, `SSS3` 19, `SSX3` 17, `X1` 11, `XX2` 13, `SX2` 15, `XS2` 14 and the snaps; the run attack 24 (501 armed) and the
charge 0 and dive 1, after which the run resumes when the stick is still at a run; the block 606, or the shuffle 607
with the stick pushed; rage 643; the grab 71, 72, then the hold 82 (victim 73, then 83), or from the rear 71, 74, 84
(victim 75, 85); the miss 71, 69, 389; the tackle 4, 5, then 210 (victim 6 when the player's 5 starts, then 207), the
tackle's miss 4, 2, 389; the grab strikes and power strikes with the victim's next id (52, 54, 56, 58, 64); the spins 78
/ 79 to the rear hold 84 / 85 and 80 / 81 back to 82 / 83; the throws with the victim's next id, then 196; the let-go 95
/ 94; the mugging 78, 338, 340 (victim 79, 339, 341) with 342 / 343 while the stick is on target and 344 / 345, 80 / 81
on success; the mount from the front hold 118 (victim 119) into 210 / 207, and in the mount the strikes 219 / 221 and
223 and the power strike 225 (231 in rage) back to 210 (victims 220, 222, 224, 226, 232 back to 207), the pick-up 248 /
249 back to the front hold, and the release 244 / 245, the victim rising with 199; the strong grapple 71, 657, 82
(victim 658, 83), from the rear 71, 659, 84 (victim 660, 85).

## The sandbox target {#target}

The target (`human::TargetHuman`, Coney's own, placed only by a sandbox layout's `target` line,
[Sandbox](../guides/sandbox.md)): it takes the hit with the attacker's hit code and flags, and picks its reaction with
`hitReaction()`; a reaction whose clip has an event of type 7 knocks it down (196) for the ground time, then it rises
with 199; a stun hit (flag `0x400`) plays the reaction, then 356 until the stun time passes and the clip playing has
ended, then 357 and the idle; a hit at 0 health plays the dying reaction and lies in 196 for good; a grounded target
takes 195. Its reactions and throws move it by their clips' root motion. Its numbers are the street civilian's power
class (hurt below 35 %, stun 750 ms, ground 2000 ms). It has no brain, never moves by itself, blocks nothing and is
drawn with the player's model.

## Tests {#tests}

The tests drive all of this with input scripts played through the pad records at partial stick deflections:
`tests/combat/` the core (each attack's timing, the chain, the grab rules, the meters, the reactions, the class damage,
the victim's decisions, the grabbed player, the lock-on and the rage awards), `tests/human/being_hit_test.cpp` the
player hit (the duck and its counter, the block, the floor, the armour, the stun, knockdown and mash, held in a grab,
the rage and repeat tracker, the combat walk and the grab's turn), `tests/human/combat_timing_test.cpp` when every
move takes a press and gives the stick back under button spam and partial stick,
`tests/human/moving_attacks_test.cpp` every attack button at a walk, a run and a sprint with an analog stick, through
both the characters' step and `Human::step`,
with the disc's clip lengths, `tests/human/combat_test.cpp` the human with
synthetic clips (the combo and its reactions and stun, the stun's 750 ms, the block holding the body while the stick at
0.6 turns it, the grab, strike and throw with the rise 2 s later, the R1 spin and the L2 let-go, the tackle, the turn
into an attack, the knockdown, the victim at the front and rear holds' offsets after the connect, a paired clip from the
attacker's set), `tests/human/pair_placement_test.cpp` the pair's geometry, and
`tests/sandbox/disc_sandbox_combat_test.cpp` Rembrandt from the disc in the fight yard (`assets/sandbox/combat.layout`;
the scripts `tests/support/combat_*.txt`: a combo, a grab with a strike, both spins and a throw, a tackle, a mugging;
clip ids, counts and hashes only; and the pelvis of the holds 82-85 against the runtime one: 82-84 within 0.1°, 85
within 2.5°, all within 1 mm).

The disc test (`[disc][combat]`, counts only): Rembrandt's list has 722 records, 160 with damage; every attack combat
starts has one; the grab and tackle ranges come out at 3.12 m and 3.75 m as at runtime; every clip the fighter and
the target play is there (120 ids), 41 of the reactions with a knockdown event. **The file's damage is not the
runtime damage** (the disc check shows it): Rembrandt's file says `S1` 40, `SS2` 40, `X1` 45 where play gives
17, 36, 26, because the character class's table is written over the list ([Damage](../research/combat.md#damage-table)).
`applyClassDamage()` does that write into each human's own copy of the list (`Human`'s `classDamage`). The player
takes his class's table and his Warrior class's percentage from the recorded configuration (`human::playerClassOf()`),
at a level's start and at the debug menus' change of character alike; the disc check gives Rembrandt's `S1` 17 as at
runtime. When the scripts recorded no `CfgChar` call of his type he plays the file's damage.

## Input scripts {#input-scripts}

Coney's `--input-script` lines (frame = one 30 Hz update; [input scripts](../guides/running.md#input-scripts)) that
reproduce each behaviour, from a standing player facing the target with the camera behind; stick values are the in-game
magnitudes. The gaps
between the grab's steps are generous, not measured minimums:

```text
# S1, SS2, SSS3: a press every 6 updates
10 tap square
16 tap square
22 tap square
# X1 then XS2
40 tap cross
46 tap square
# snap attack: full stick 90 degrees off the facing (right, with the camera behind the player), then square
70 stick left 100 0
71 tap square
72 stick left 0 0
# grab (tap), strike, throw forward (away from the camera)
90 tap circle
140 tap square
180 stick left 0 100
181 tap circle
182 stick left 0 0
# tackle (another target): hold circle 7 updates or more
185 press circle
195 release circle
# block for a second, shuffling at 0.8
200 press r1
210 stick left 80 0
230 stick left 0 0
230 release r1
# charge: run, hold L2, press cross
250 stick left 0 100
265 press l2
275 tap cross
300 release l2
300 stick left 0 0
# rage, with a full meter
320 press l1 r1
322 release l1 r1
```

The power strike is `press cross`, then `tap square` a frame later, while grabbing. The stereo theft's mode 3 wants
the stick at 100 % stepped anticlockwise every update, under 90° a step (for example 30° steps: `stick left 100 0`,
`87 50`, `50 87`, `0 100`, `-50 87`, ...), about 12 turns in all; no other button until it ends.

## Coney's choices {#choices}

Where the research is silent or only inferred, Coney chooses; each choice links the open question or issue that tracks
it.

- Inside one trigger table a later matching entry overwrites an earlier one, as the tables do between themselves;
  trigger 4 (query) never matches. Tracked by issue #821.
- **Object targets** ([Target selection](../research/combat-targets.md#targets)): square with no human in front takes
  the nearest whole pane within the object attack's far range, within 54° of the heading, else within 135° (the
  original's × 0.8 is not applied there); its centre is the target point. The original approaches from up to far × 1.5;
  Coney does not. Doors and loose objects are not object targets. Tracked by issue #712.
- **The pick-up** ([Breakables](../research/combat-targets.md#breakables)): the reach is measured in plan from the feet,
  and the second point the search goes round (human `+0x4e0`) is left out; the flag messages (`0x19`) that change what
  is pickable are not modelled. The human does not steer during the clip. Every `TYPE_SPECIAL` is loot (the named
  mission items are not listed). The human's current context record is the nearest object with a prompt in reach. The
  clip is the pick-up animation's pair; the drop before a left-handed or hat clip, and the anim set pushed for the
  follow-on, are not modelled. A dropped object lands 0.3 m ahead of the feet, with no fall. Tracked by issue #821.
- **A weapon in hand** ([A bat in hand](../research/combat-throws.md#bat), [Moving attacks with something in
  hand](../research/combat-throws.md#armed-moves)): the fighter's anim set stands for the held object's set. With set
  1-3 square and cross take the armed branch (501, the grounded strike, the slot's swing; no walk attack, snaps or
  strike on a grabbed target), and the charge and dive are unchanged; the rage is the
  [Rage](../research/combat-rage.md#rage) table's (not its gates on the attacker's brain and the victim's class, nor ids
  `0x269`-`0x26c`'s table-2 award). **Not built**: the armed branch's mugging, the blocks' clips of an anim set, the
  weapon's damage bonus (`CfgObj` `+0x58` × the power class's factor), a breakable's hit kind 2 for the moving attacks,
  and the throws of sets 4-6 (square, cross, the charge and the dive play the unarmed moves); a bat never breaks.
  Tracked by issues #153, #309.
- **The held flags** ([Tasks](../research/tasks.md#held-flags)): the bits each move holds where the research names none.
  Every attack the dispatcher starts (the walk attack, the snaps, the grounded and mounted strikes, the grab strikes,
  power strikes and throws) is built as `Attack_Start`'s (holds `0x7`, sets `0x1`); the charge and dive hold the run
  attack's `0x1000000` (the dive's is `0x400000` at runtime, [Moving strikes](../research/combat.md#moving-strikes);
  both make an object hit kind 2); the grab's connecting clips, its spins, the mugging's clips and the let-go hold the
  grab bit `0x10`; a start clip `0x10000000`, the landing `0x1000000`, the run stop and the climbs `0x80000`. An event
  acts only on a task holding the bits it changes; a new task first clears the bits it holds, so the next attack of a
  chain starts in its wind-up, and one leaving clears only the bits no other task holds; only the newest task's events
  fire. An event at clip frame `f` fires on the update whose clip time, rounded to the nearest frame (a tie going down),
  first reaches `f`, which gives the measured phases from Rembrandt's frames. A clip played once ends when less than 0.1
  ms of it is left, so `XX2`'s clip ends on its 30th update as measured, not a float's rounding later. The grab's moves
  and the mount's strike take square's mask (`0x100101f`), so a move in a hold plays out before the next. The chain ends
  once the record holds none of the attack's phases, the recovery, the counter or the run attack's bit. Every attack
  whose hit was not measured (the snaps, the moving attacks, the throws, the grounded and mounted strikes) hits 2
  updates in, as `S1`. A moving attack's hit on a world object lands instead through its strike window
  (`combat::movingStrikeWindow()`, [Objects](../research/objects.md#coneys-implementation)); its hit on a human still
  lands 2 updates in (the strike shapes are not built). The block's release plays the idle at once, and its fade holds
  `0x10000000` for its 5 updates: the stick turns the player but the walk start waits for the fade, as at runtime. R1
  held blocks only when the record holds no phase outside `0x20081404` (`Human_CanFight`), so it cuts an attack's end
  phase and ends its chain but waits through the wind-up, window and recovery
  ([Input](../research/combat-moves.md#input)). A new attack fades in over 0.2 s; a chain step keeps the 0.1 s combat
  fade (**Coney's reading**: the original swaps it into the running task). Coney never sets state code 5. A buffered
  snap plays only with a snap target within 25's far range (with `CfgSnap` on), else the plain square step, and no step
  from `S1` or `X1` plays while the target is low (down, or 1.3 to 1.8 m below), as
  [Input](../research/combat-moves.md#input) gives. **Coney's readings**: the buffered snap's search runs along its side
  of the facing, not the stick of the moment, and a step refused at a low target drops its press. With `CfgSnap` off
  (the debug menu's "Snap needs target") the stick alone snaps, fresh or buffered. Tracked by
  [q-unmeasured-hits](../research/combat.md#q-unmeasured-hits).
- **The characters' step** ([Tasks](../research/tasks.md#humans-update)): it runs on Coney's fixed 1/30 s step, the
  original's 30 Hz characters' update, without the 60 Hz tick or the timing wheel, which wait for the world's objects;
  the brains are an empty hook until the AI lands; the context actions (triangle) are refused while the dispatcher would
  drop a command or the human is busy. Tracked by issue #821.
- `SS2`, square is always `SSS3` (19), never 20; a grounded target takes 193, never 194; the dive takes the charge's
  conditions. Tracked by issue #821.
- **The snap** (`squareAttack` in `repo:src/combat/attacks.cpp`, the search and steer in `repo:src/human/fighter.cpp`)
  follows [Attacks](../research/combat.md#attacks): the run and walk attacks first, then the snap only when
  `Fighter::snapTarget` finds a human within 2 m, 45° of the stick and 2 m in height, standing, with health left and
  targetable, that is not the current target; without one, `S1`. Its steer is `Attack_SteerToTarget`'s turn and slide
  over 0.1 s, only within the snap's far range. **Coney's readings**: the turn puts the target at the snap's own
  direction from the Anim Range List (to the side for 25 and 27, behind for 29), where the clip strikes, rather than
  straight ahead, and the hit lands on whoever stands within the far range on that side; the snap's target is not kept
  as the target (human `+0xc8`); the dispatcher's gait tests read the gait the last update's velocity left (so a square
  one update after the stick is first pushed fully still snaps, as at runtime, though the run start has moved the body).
  That gait is taken in the step's first pass (`Human::animate`), so the play mode's characters' step (`human::Humans`)
  and a lone `Human::step` both give it. The run attacks also need the record's `+0x08` clear, as the code tests: a
  square or a cross during the run start (`0x10000000`) plays `S1` or `X1`. In a [fight
  stance](../research/combat-targets.md#fight-stance) neither the unarmed run nor the walk attack plays, so a square
  there goes on to the snap or `S1` and a cross to `X1`; the armed 501 does not test the stance. **Stand-in**: the
  stance is the player locked onto a target (`Fighter::lockTarget()`), not the original's own rules for entering and
  leaving it. The original's snap filter takes no human friendly to the player (`Human_IsFriendly`, `0x00222a90`);
  Coney's search leaves that to the targets it is given. With the disc, `level99`'s lesson 7 passes:
  `repo:tests/platform/disc_level99_snaps_test.cpp`. Tracked by [q-snap-steer](../research/combat.md#q-snap-steer).
- A side is "front" up to and including 45° and "rear" beyond 135°; a height difference beyond 1.5 m counts as 0.9 to
  1.5 m. Tracked by issue #821.
- The mount ([The mount](../research/combat-grabs.md#mount)): the victim is placed at clip 210's pair event when 210
  starts rather than slid there over 0.1 s; the mount's moves take square's mask, so one plays out before the next; the
  mount's power strike spends the endurance fraction, as the grab's does (the page names only its need); triangle in the
  mount (the mugging) is not built, nor flag `0x20000`'s getting off at once. Tracked by issue #684.
- The pad's buttons are matched even while the pad is locked (`HuLockPad`), and a command disabled with
  `EnableCommand` is kept pending; the human acts on neither, but the `PadSetHandlerEx` handler hears both
  ([PadSetHandlerEx](../references/bindings/input.md#padsethandlerex)). Only the pad-driven humans carry a pad command:
  Coney's brains write their commands straight to the record. Tracked by issue #821.
- The special (cross + square outside a hold, [Specials](../research/combat.md#attacks)) is 653 (645 in rage), 655 (647)
  when the player stands on its target's rear; circle + triangle (the tag) is not built. Both specials are refused on
  `0xaeebf7ff`. Tracked by issue #821.
- The strong grapple ([Strong grapple](../research/combat-grabs.md#strong-grapple)) is the grab with 657 / 659 (649 /
  651 in rage) as its connect and 658 / 660 on the victim, searched with anim 1's range within 54° of the stick, the
  clear line to the target (`0x0021c0a8`) not tested. Its damage is dealt on the snap to the hold, reported to the
  tutorial as 82 / 84; the paired, tackle and solo paths for other clip flags are not built. Tracked by issue #684.
- The power strike's extension (57 → 59, 63 → 65) takes square's press or cross's `0x10` only on an update with the
  window open, and the grab ends when the last part's clip gives way to 389: the victim, attached until then, is put on
  the ground (196) to get up after its time. The rear power strike's hit is scored as 57, not the spin's 80. Tracked by
  issue #684.
- A grab plays one move at a time; a throw lets go at once (from the rear hold, once its spin to the front has played),
  turning the pair onto the stick from the front hold ([Where a throw
  goes](../research/combat-throws.md#throw-direction)); the rear power strike's spin plays in front of the strike, whose
  timing starts with it; the release with too little power goes straight to the idles, and the grab broken at 0 power
  plays the let-go. A tackle also ends when the power meter is empty, and any hold when the victim has no health left.
  Tracked by issue #684.
- The grab and tackle search takes the nearest candidate by straight-line distance with no facing cone. The attack's
  target search uses the attack's far range in `Player_PickTarget`'s first two passes (the third finds no human the
  second missed); beyond the far range the attacker turns at most 8° at once (read as degrees). Within it the steer
  follows [The steer in detail](../research/combat-targets.md#targets): from the update after the clip starts it turns
  at angle / `T` and slides at (goal − position) / `T`, on top of the clip's root motion, the last update only for the
  time left. The attack's hit code picks the steer: with flag `0x8` (`AttackTable_GetFlags` `0x00254d60`)
  `Attack_SteerLed` (`0x00275678`) turns and slides over the time to the clip's first event + 0.1 s (X1, S1, the chain's
  steps but `SSX3` and `SSS3`, and the snaps); without it `Attack_SteerToTarget` takes the caller's time cut to that,
  the time to the event from `Attack_Start` and the specials (653 and 645 stand at their reach by k3 at runtime), 0.1 s
  from the snap's start. The lead is the target's velocity × (the time to the event + 0.1 s), cut to 0.5 m as above
  ([where the attacker stands](../research/combat-moves.md#reach)). The led steer aims at the target's head ([The led
  steer](../research/combat-targets.md#led-steer)), read once as the attack starts, and the other at its position. On
  the disc Coney's posed heads match the runtime ones: in 388 the head stands 0.001-0.039 m forward and within 0.006 m
  sideways, in 358 0.097-0.186 m forward and 0.038-0.065 m right, 0.02 m beyond the recorded leans. **Coney's reading**:
  the turn faces the led target, not the standing point, which lies behind the attacker when the target is inside the
  reach (the runtime `XX2` at 0.83 m, inside its 1.12 m reach, turned under 1°). A reaction cuts the steer. Tracked by
  issue #683.
- **Posing a grab** follows [Posing a grab](../research/combat-grabs.md#grab-posing): circle plays 71, then 72 (from the
  front) or 74 (from the rear, when the player stands on the victim's rear side), the player turning to face the victim
  over 71's playing time while the victim waits in its idle. When 72 / 74 starts, the alignment turns and slides the
  player over 0.1 × the clip's time (two updates for 72) so the victim stands at the clip's reach straight ahead, and
  turns the victim to face him (or away); beyond the far range × 1.25 the grab fails with 69. The victim then plays 73 /
  75 **from the player's anim set at its rate** (a paired clip, fade 0), and both bodies move by their clips' root
  motion. At the clip's end the gate (1.297 m, 0.2 m in height) releases the grab or the victim is snapped to the hold's
  offset from 82's / 84's range record, (0.380, 1.012) facing the player or (−0.097, 0.222) facing his way, and
  attached: each update it is put at the player's transform × that offset, its own root motion ignored. A spin (78 / 80)
  detaches it so both bodies move by their clips, as at runtime, and its end snaps it to the other hold. A strike, power
  strike or a front hold's plain throw is refused (nothing played or spent) unless the victim stands within 0.3 m of the
  current hold's point; a wall throw and a rear hold's throw are not checked ([Where a throw
  goes](../research/combat-throws.md#throw-direction)). The mounted victim is attached at 210's type-8 pair event,
  (−0.120, 0.032): 0.032 m ahead and 0.120 m to the left, facing the other way. **Coney's choices**: the grab's side is
  decided as the intro starts (a passive target does not move in between); the place a move checks is the current hold's
  point (the strikes' points are the front hold's); the victim's turn spreads evenly over the alignment's updates and a
  slide under 0.01 m is left out; the moves of a hold switch both humans with no fade, the let-go keeps the combat fade.
  Tracked by issue #684.
- The mugging (`MuggingGame`, `LevelPickups::mugEnded()`): the record is `SetInterrogateParam`'s set 0 while its
  required time is set, else the runtime one (5 s, 2.5 s, 50 s off target, 50°, 60°), not the per-class table; the
  target angles are drawn evenly (the first at random). The money moves in the deciding update, the end clips 344 / 345
  or 346 / 347 play, and the mugger's callback runs when his end clip finishes, or at once for a let-go or a hit
  ([Crimes](../research/crimes.md#coneys-implementation)); the speech, the hints, the half-way `no_item` stop, the
  victim's interrogation and pocket item, ped type 5's 1.5 times and the statistic are not built. The theft: clockwise
  steps neither add nor take away; the 250 ms pause ignores the stick. The mash follows
  [Uncuffing](../research/crimes.md#uncuffing): only the held commands of L1 and R1 alternate, the gain is rounded, the
  quit commands fail it and the decay starts at the first alternation. Tracked by
  [q-mug-angle](../research/combat.md#q-mug-angle).
- The block is read only when the player is free (not grabbing, tackling, mugging or in a theft); it turns the player
  towards the stick at the standing turn rate. Tracked by issue #678.
- The stun's loop is 356, as seen at runtime (the code names 355 as a stunned reaction's return). Tracked by issue #685.
- `applyClassDamage()` rounds as the PS2's floating-point unit does (toward zero), which gives the research's 34 for 30
  at 115 %. Tracked by issue #821.
- **The shared Anim Range List is not reproduced**: in the original the class damage is written into the list the
  character's humans share, so the newest human's class sets everyone's damage ([Being
  hit](../research/combat-reactions.md#being-hit-runtime)). Coney gives each human its own copy, as the design plainly
  means; whether the original intends the overwrite is open. Tracked by
  [q-shared-anim-range](../research/combat.md#q-shared-anim-range).
- The player hit: the stun's exit (357) waits for the clip playing to end; a mash also cuts the time of a stun taken
  with the knockdown; held in a grab or holding someone, a hit only takes health (the reactions by those states,
  `0x002688d0` and `0x00268ea8`, are not traced); for a demi-god (flag `0x20000000000`, which only the level scripts
  set, `HuSetDemiGodMode`) the health floor stops a hit that starts above 25 %, and one already at or below it takes
  the whole hit; the escapee takes its escape clip's own damage, as at runtime. Tracked by issue #685.
- The duck's counter is asked for once per duck and the command that asks is spent (no attack starts from it); it
  plays on the next update with no steering, against the attacker that made the player duck, else the nearest target
  within 1.25 × 617's reach, and hits once. Tracked by issue #683.
- The rise's counter ([Down and under attack](../research/combat-reactions.md#ground-escape)) is the duck's played out
  of 199: square or cross on an update whose clip fires its own `0x25` while the player is free to react (held bits only
  within `0x40089000`, not stunned with the manual stun `0x8000000`) plays 617-620 that update, ends the rise and the
  stun, and spends the command. **Coney's stand-in** for the foe (the human that knocked him down, not kept): the target
  search's pick within 1.25 × 617's reach; with none the request lasts while he stays free to react. Only a player
  counters (an AI's Grounded goal says no through the rise). Tracked by issue #231.
- Held: the counter at the catch costs the grabber a quarter of its maximum; a reversed hold has no victim behind it
  (the grabber is an entry point, not a `Holdable`). Tracked by issue #679.
- The player's grab or tackle holds any `Holdable`: the sandbox's passive target or another human without flag `0x40`.
  A held human's fighter keeps the hold's state (held or mounted), which holds its movement, keeps it from acting and
  gives its brain the grabbed (`0x14`) or tackled (`0x15`) reaction goal; attached, its own update does not move it.
  The grab's intro leaves the victim alone (it is stopped at the connect, as `Grab_Connect` does), so an AI may still
  answer it; the tackle's intro stops it at once (**Coney choice**, as before). The answer itself (the AI's counter
  76 or 9) and the AI's own struggle in the hold are not built, and no AI grabs or tackles. A held human gone from
  the targets, or freed by a script, ends the hold. Tracked by issue #684.
- **Breaking a pair** ([Breaking a pair from outside](../research/combat-grabs.md#pair-break)): every placement of a
  human (`Human::spawn`: the scripts' teleports, a scene's release) first breaks any pair it is in
  (`Fighter::breakPair`). The human placed plays nothing; a victim it held plays 145 (front), 107 (rear or a mugging) or
  245 then 199 (mounted) and stands free; a grabber it was held by plays 138, 106 or 244 on its next update (**Coney's
  choice**: the original plays it at once). **Coney's choice**: a held human whose grabber stops updating without a
  break (taken out of the update) frees itself with the same victim clips after 3 of its own updates without its grabber
  keeping the hold (the humans act in an order that alternates each step, so a live grabber keeps it at most 2 apart).
  `Grab_Release`'s refusal in a scene is not built: no human in a pair is taken into one. Tracked by issue #684.
- The combat walk's clip by eight even 45° sectors centred on the clips' directions; the walk starts at its full speed
  (the 5 slower first updates backward are not known). Tracked by
  [q-backward-walk](../research/combat.md#q-backward-walk).
- **The flash** (`PlayLevelMode::stepFlash()`, `useFlash()`): d-pad right with a flash carried and health below the
  maximum plays 665 when nothing holds a move, and the clip's action event `0x41` 19 updates in (`Human::actionEvent()`)
  uses the flash: one spent, interface sound 23 and the clip's animation sound 70, the health filled, the health rings
  asked for; a clip cut first spends nothing. In a grab the flash is used at once and the grab stays. Coney always heals
  in full (it has no difficulty setting) and does not build the full-health rage use or the screen effect reset. The
  clip holds `0x2000` **and sets it as it starts** (**Coney's reading**: the page says the task "holds" it and only a
  set bit keeps the clip alive; [q-flash-held-flag](../research/combat.md#q-flash-held-flag)). Held without being set,
  the bit never reaches the record, the stick step does not count the human busy, and a player in the fight stance
  (after walking up to the dealer, his lock on) has the stance walk's idle put over 665 on the next update: the clip
  showed for one update, the event never came, and nothing was spent or healed (issue #234). A second d-pad right while
  665 plays does nothing (the human is busy, and 665 is already his anim). Tests:
  `repo:tests/platform/disc_flash_test.cpp` (`level80` standing, walking and running; `level99` from the dealer's
  purchase to the use in the stance: 665 from the press, the spend, cue 23 and animation sound 70 on update 19 of it,
  and no play left silent by the engine). Tracked by issue #234.

Not built: the rage of the attacks beyond the chain, the moving attacks and the throws (their events are not mapped),
and the hurt multipliers `+0x10` / `+0x14`. The AI's attacks on the player, its attack warnings, the class damage tables
and a boss's class 13 are on [AI](../research/ai.md); weapons on [A bat in hand](../research/combat-throws.md#bat);
breakables and car windows on [Breakables](../research/combat-targets.md#breakables).
