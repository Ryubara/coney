# AI gangs, tactics and formations {#gangs}

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra;
every claim is confirmed (code) at the cited address unless it says otherwise, as on [AI humans](ai/index.md), which
this page
was split from because that page had grown too large. The brain a gang member runs is on [AI humans](ai/index.md#brain);
the
tactic and goal classes are indexed on [AI code index](ai-code.md) and [AI goals](ai-goals.md).

## Purpose

A gang is the group a human fights with: the Warriors around the player, each enemy gang of a level, the police
and a riot's crowd. The gang decides who its members fight and how (its tactic and formation), makes new members
through its spawners while the level allows it, answers the scripts' gang messages, and for the Warriors carries the
player's commands and the follow that keeps the crew beside him. The AI brains ([AI humans](ai/index.md#brain)), the
crimes and police ([Crimes](crimes.md)) and every mission's encounters depend on it.

## Original structure

The gang module is `0x00162518`-`0x0016e388`; its function-by-function index is on
[AI code index: the gang module](ai-code.md#gang-functions), and the tactic and goal classes are on
[AI code index](ai-code.md) and [AI goals](ai-goals.md). The functions this page's behaviour starts from:

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0016d170` | `Gangs_Update` | Each update: every gang's update, its spawners asked once per gang | confirmed (code) |
| `0x00166708` | `Gang_CachePlayerInfo` | The players' state for the gang, and whether its spawners may run (the cap) | confirmed (code) |
| `0x001681a0` | `GangSpawner_Update` | One spawner's update and spawn | confirmed (code) |
| `0x001672a8` | `GangSpawner_OnHumanGone` | A spawned human gone: the counts taken off | confirmed (code) |
| `0x001664d8` | `Gang_RemoveMember` | A member leaves the gang | confirmed (code) |

## Gang fights {#gang-fights}

How a gang shares out a big fight, team-ups and Rumble's CPU sides: [Gang fights](gang-fights.md).

## Data: the gang record {#gang-record}

32 records of 0xb10 at `0x005e6e30` (`0x0016c388(id)`). Confirmed (code):

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` / `+0x14` | vec4 / float | centre and radius of the members' bounds (every 5th update, `0x00166b60`) |
| `+0x18` | int | in use |
| `+0x2c` | int | gang kind (1, 5, `0x17`, `0x18` special-cased) |
| `+0x30` | s16 | id, also its bit in other gangs' masks |
| `+0x32` | s16 | alert state (`0x00164a28`): 1 fighting, 0 calm |
| `+0x38` / `+0x3c` | u32 | **enemy** / **friend** masks, a bit per gang id |
| `+0x40` | ptr | the current **tactic**, 0 for none |
| `+0x44` | handle | the **leader** ([tactics](#tactic-kinds)) |
| `+0x48` | handle[16] | **members** |
| `+0xd4` | u8 | suspended: its members' brains skip their update ([One update](ai/index.md#update-goals)) |
| `+0xe4` | u32[] | the **Lua handler** per event id (interned name) |
| `+0x1b0` | table | gang-wide anim substitutions (`0x00164178`, `0x001642f0`) |

- **`GangMakeEnemies(a, b)`** (`0x0016acf0`), both ways: clear the friend bit, set the enemy bit (`0x001690c8`).
  `GangMakeFriends` (`0x0016ad80`) does the reverse. Id −1 is ignored.
- **Friends** (`0x00168f58`): the same gang or kind, both kinds in {1, `0x17`}, a global truce (`0x0050cb7c` in the
  future), or the friend bit.
- **`GangSetThreatResponse(gang, r)`** (`0x0016b3d0`) writes brain `+0x21c` of the **current** members only.
- **Alert state** (`0x00164a28`), on a change: 1 installs the fight anim substitutions (`0x00169508`), 0 removes them;
  every AI member's brain `+0x14d` = 0 when fighting, 1 when calm. With no tactic, `0x00166cf8` returns a fighting
  gang to calm once no member has a fight goal (8 or `0x3f`) or an active goal `0xc`.

## Behaviour

The sections from here to [Formations and follow slots](#formations) describe what a gang does, each naming the
original function where its logic lives.

## Events and GangSetMsgHandler {#gang-events}

`Human_OnEvent` (`0x0021d4e8`) offers an event, until one consumes it, to the human's own script handlers
(`0x00384c38`), then **`Gang_OnEvent`** (`0x00164c20`, the brain's gang `+0x20c`), then `Brain_OnEvent`
([Events](ai/index.md#events)). Confirmed (code).

- `GangSetMsgHandler(gang, msg, name)` (`0x0016ab38` → `0x00164bb8`) stores the interned name at `+0xe4 + msg × 4`.
- `Gang_OnEvent` calls that handler only while scripting runs (`0x00512b28` = 1), through script-system `+0x8c` with
  three values. Event **18** (a human died or was
  knocked out, [Script events](../references/script-events.md)) calls `handler(member, other, standing)`, where
  `other` is the attacker (the null handle when none) and `standing` the members not dead or knocked out
  (`0x00166220`); event `0x11` passes the event's `+4`, event 2 the headcount; any other id goes through the generic
  marshaller (`0x00384ce0`) and is consumed when the Lua function returns true.
- Then, when the gang has a tactic, the event goes to the tactic's event slot and its result is returned. With no
  members (`0x004de0c8` on `+0x48`) only event `0x16` with `+0x08` = 0 (a member left) goes on. A tactic not yet
  started (`+0x04` = 0, the flag `Tactic_Process` `0x003067d8` tests) is first run through the gang's update,
  `0x00166708(gang, 0)` (`0x00165144`-`0x00165158`), whose `Tactic_Process` starts it. So when a script sets a tactic
  and adds members in the same step, the start's goals are given before the event's: the follow tactic's 22 with 1
  then pops them to the base (`WarriorFollowTactic_GiveGoals` calls `Brain_PopToGoalBase` and `Brain_MarkGoalBase`)
  and gives them once again, not twice. Confirmed (code).

## The gang update {#gang-update}

`Gangs_Update` (`0x0016d170`, step 4 of [`Humans_Update`](tasks.md#humans-update)), for each gang in use that has
members, confirmed (code):

1. `0x00166708(gang, 1)`: the player's position and heading cached (1 s) and the time checks.
2. With a tactic: **`Tactic_Process`** (`0x003067d8`); a non-zero result fires the tactic's Lua callback (vtable
   `+0x54`).
3. One gang in five per update (a counter `0x0050cbec`, 0-4): the bounds, the return to calm, and the neutral rule
   (`0x00169d30`: a gang of kind `0x17` / `0x18` whose members all have threat response 0 is made neutral with the
   kind-0 gangs).
4. `0x00306630` frees the tactics queued for freeing (up to 32, `0x006eb290`).
5. A gang with flag `+0xd3` and no living members calls the handler at `+0x158` with its id and is freed
   (`0x0016a1e8`).

## Deleting a gang {#gang-delete}

`GangDelete(id)` (`0x00373200` → `0x0016a1e8`; ids of 32 and above do nothing) → `0x0016d068` → `0x00164738`,
confirmed (code):

1. Every gang in use clears this gang's bits from its enemy and friend masks.
2. The gang's Lua message handlers are cleared, so the members' removal below calls no script handler.
3. **Each member still in the gang is destroyed, at once**: taken out of the member list (`0x001664d8`: clears the
   leader `+0x44` when it was the leader, offers event `0x16` to the gang's tactic), then `Human_Destroy(h, 0)`
   ([Characters: destroying a human](characters.md#destroy)). Nothing is deferred: the humans, their held objects
   (destroyed, not dropped), radar blips, HUD panel and pad (for a player) are gone when `GangDelete` returns.
4. The tactic, if any, is stopped and freed.
5. The record is reset (`0x00164808`): not in use, not suspended, members, spawners, timers and handlers cleared,
   leader the null handle.

**A player is no exception.** A listed player in the gang is destroyed like the others: its pad is freed and it is
removed from the player list (the other player, if any, becomes player 1); no other human is made the player. Scripts
move the player first with `HuChangePlayerGang` ([Characters: players](characters.md#players)); level 99 does so at
checkpoint 2 ([the hand-over](characters.md#level99-handover)).

**`GangBrFlush(id)`** (`0x0016ba18`) only clears the goals and actions of each current member's brain; it removes and
destroys nothing. Called just before `GangDelete` in the same script step, as in level 99, it has no visible effect.

## Tactics {#tactics}

A pool of 30 tactics of 0x90 at `0x006ea1b0` (mask `0x006ea1a0`; alloc `0x00306558`, free `0x003065b0`). Base fields:
`+0x00` gang, `+0x04` started, `+0x08` time limit (−1 none; Process returns 2 once past it), `+0x0c` Lua callback,
`+0x1c` vtable (`{s16 delta, fn}` pairs: `+0x0c` Start, `+0x14` End, `+0x24` Process, `+0x34` type id, `+0x3c` a
flag, true for types below `0x12`, `+0x4c` event, `+0x54` fire the callback). Confirmed (code). The `+0x3c` flag has
two readers: Start puts the gang on alert when it is 1, and `Brain_AddEnemy` sends event `0xb` when it is 0 (below).
**It does not gate any goal handling**; the old reading "members keep their own goals" was wrong.

- **Setting one** (`Gang_SetTactic`, `0x00165640`) ends and frees the old tactic and stores the new one. It does not
  start it. **`Tactic_Start`** (`0x00306690`) has one caller, `Tactic_Process` (`0x003067d8`), on the gang's first
  update after the binding. Every binding a script step makes before that update, such as a `GoalTag` right after
  the `Tactic*` call, has already run. Start does this:
    1. Makes the time limit absolute.
    2. Sets the gang's alert state to the `+0x3c` flag: 1 (fighting) when it is 1, else 0 (calm), so a tactic of
       type `0x12` or above calms its gang ([Alert state](ai-reimpl.md#alert-state)). Types `0x16`-`0x18` and
       `0x21` also set gang `+0xd1` = 0 and `+0xd2` = 1.
    3. For every member that is not a player (`+0x1b0` = −1): `Brain_MarkGoalBase` (`0x0028d8a0`) and
       `Human_DropTarget` (`0x00226f70`, clears the target). **Nothing is popped.**
    4. Runs the class's Start, then marks the tactic as started.

  `Tactic_Process` returns 2 past the time limit, else the class's Process.
- **The goal base** (brain `+0x2d`) is how a tactic keeps the goals a member had. Confirmed (code):
    - `Brain_MarkGoalBase` stores the current top index (`+0x2c`) as the base, only when the stack is not empty. On
      an empty stack the base stays as it was.
    - `Brain_PopToGoalBase` (`0x0028d8c0`) pops while top > base, then sets the base to −1. A base of −1 pops
      everything.
    - `Brain_ClearGoals` (`0x0028d910`, the script's flush) pops everything.

  So a member's goals at Start, including any the script pushed in the same step, stay **under** the base. A
  class's Start that gives goals (for example `DefendTactic_GiveGoals`) calls `PopToGoalBase` and then
  `MarkGoalBase` before each push. That pops only what is above the mark, nothing at Start, and pushes the tactic's
  goal **on top**. When it ends, the old goals are back on top. Where [AI code](ai-code.md) says a tactic helper
  "flushes" a member, it means this pop-to-base and mark, not `Brain_ClearGoals`. Confirmed (code, by their callees)
  for Defend, Info, ManWeaponPile, Pursue, WarriorScatter, Scout, StandGround, Steal, Taunt and Vandalize. Coney
  must not clear the stack at a tactic's start.

  **`Tactic_End`** (`0x00306850`, from `Gang_StopTactic`, `Gang_Destroy` and the old boss tactic) pops every
  non-player member down to his base. That removes the tactic's goals and anything pushed onto him **after** Start,
  and never the goals he had at Start. A member whose stack was empty at Start keeps base −1, so End empties his
  stack.

  **Example** (level3, checkpoint 2): the script calls `TacticDefend(gang, leader, 2.25)` and then `GoalTag(leader,
  ...)` in the same step. The TagGoal is pushed first, because Defend's Start waits for the gang's update. Start then
  marks it under the leader's base and pops nothing. `DefendTactic_GiveGoals` (`0x00310288`) skips the defended
  human, so the leader keeps tagging. A member who is down, knocked out or has no AI brain (brain `+0x04` = 0) is
  also skipped. The other members get FollowAndDefend on top of their own goals. Events 19, and 22 with `+0x08` = 1,
  run `GiveGoals` again. Each run pops only the goals above the base and pushes a new FollowAndDefend.
- **Firing the callback** (`0x00306938`) calls the Lua function at once with two values: the gang's id (`+0x30`) and
  the code. **`TacticClear`** ends and frees it.
- **Fights under a tactic**: `Brain_PushFightGoal` (`0x0028d190`) returns at once when the gang has a tactic, so
  `GoalFight` picks the target but **pushes no fight goal**; the tactic fights. It also does nothing for a knocked-down
  or dead human, or a type-3 brain already holding goal 9; otherwise it pops goals 8 and `0x41`, pushes FindEnemy
  (`0x41`), Melee (8) and then the fight goal (`0xf`) ([Starting a fight](ai-fights.md#targets)).
- **`Brain_AddEnemy`** (`0x0028d538`) adds the enemy to `+0x164` (16), makes the enemy's brain add this human back
  (`0x0028ef20`, unless both are players), and, when the tactic's `+0x3c` answers 0, sends it event `0xb` with the
  enemy.

**TacticCrowd** (type `0x1b`, vtable `0x005437a0`; `0x00316450`): turns a gang into **onlookers**. Init (`0x0030f4d0`):
no time limit, `+0x28` the option.

- **Start** (`0x0030f6b0`): each living member is flushed and given one goal: with the option, an idle-in-place goal
  (`0x002caf78`); without, a timed goal (`0x002b4098`, 4-6 s). With the option (**cheering**), the anims `0x256`, 599
  and 668 are substituted by cheers; without (**watching**), `0x25c`.
- **Process** (`0x0030fe78`, always 0): every 1-2 s, a watcher with record `+0x08` clear plays `0x25c` at 51 %; a
  cheering crowd sends, every 2 s, the next member in turn into one of three cheer idles (state `0x20000000`).
- **Event** (`0x00310100`): `0x10` (a member warned of an attack) fires the callback with event 6 and is not used;
  `0x14` (violence nearby, its strength at `+8`, cheering crowds only, above 29) makes idle members look at the fight
  for 20 s and, at min(strength, 100) %, queue a reaction, and is used (`0x0030fa18`); `0x13` and `0x16` re-seat the
  crowd.
- **The reaction** (`0x0030fc48(what, on)`), run when the periodic switch `+0x2a` is on or `what` is non-zero: each
  free member with no actions queues two actions, a speech action (`PlaySound`, `0x002fb868`, vtable `0x00542d20`)
  saying command `0x8f` (`0x10` at rand100 < 50 when `what` is 0; `0xb1` when `what` is 1 and `on` is 0) after
  0-750 ms, then a clip action (`0x002fa300`, vtable `0x00542be0`) with the cheer `0x256` in one of three variants. A
  cheering crowd's Process calls it with (0, 1) on each tick.
- **`TacticTrigger(gang, what, on)`** (`0x00316fa0`), crowd tactics only: what 0 sets the periodic switch to `on`;
  what 1 runs the reaction at once (`0x0030fe58`).

### The scripted tactics {#tactic-kinds}

Every `Tactic<Name>` binding finds the gang (`0x0016c388`), takes a tactic from the pool, runs the class's
constructor (base fields, the callback name interned by `0x003068f8`, then its own fields) and sets it (`0x00165640`),
so the class's Start runs on the gang's next update. A tactic **fires its callback** with a code, the event names of
[`TacticGetString`](../references/bindings/ai.md#tacticgetstring) (0 `TacRunning` ... 18 `TacAnimStart`), in two
ways: a non-zero **Process** result (each update; codes such as 7 `TacInRange` are returned on every update the
condition holds, so the callback runs repeatedly), and calls from the **event** slot. Confirmed (code) for each class
below unless marked.

Shared behaviour, confirmed (code):

- **Leader**: gang `+0x44` holds the leader's handle; `0x00165678` returns him while he is alive, not a player, not down
  (`0x00223b70`) and not out of the fight (`0x00227e60`), else another member (`0x00165738`).
- **Moving as a group** (MoveToFlag, TravelPath, WalkinTall, Wander, Confront): the leader is popped to his goal base
  and re-marked (`0x0028d8c0`, `0x0028d8a0`), and the moving goal is pushed above his old goals; every other member
  that is not a player and not down joins the leader's
  [formation](#formations) (`0x00295f28`) and gets `Goal_FollowPlayer(distance, member, leader, mode)`. The slot set:
  the script's `slotSet`, or, when it is −1, set 3 with min(members − 1, 9) slots (`0x00295db0`, `0x00295dd8`,
  `0x00296488`); the leader's set before is remembered.
- **Event mapping** (the human events of [Script events](../references/script-events.md), offered to the gang's tactic
  by `Gang_OnEvent`): 1 damage → code 5 `TacDamage`; 10 saw the player → 4 `TacSeePlayer`; 11 a member spotted
  someone → the member's brain notes it (`0x0028c0c8`) and 3 `TacSeeEnemy`; 16 attacked → 6 `TacAttacked`; 8 arrived
  at a flag → 8 `TacArrived` (MoveToFlag, WalkinTall). Event 20 (violence nearby) is consumed (result 1); events 2, 17,
  18, 19 and 22 for one of the gang's own members make the tactic re-issue its members' goals. Each class handles only
  some of these (below).
- **Banter** (HanginOut, MoveToFlag, TravelPath, Wander, Idle with `banter` true): every 3 s, unless commands are
  locked (game state `+0x411`), a scene plays (`+0x410`) or fewer than two members live, two idle members are picked
  at 51 % each; the first says speech command 20 `statement` (`0x002208f0`), and when his line ends the second says
  21 `response`; then the next pair after 3 s ([Speech](../references/speech.md)).
- **Answering violence** (HanginOut, Idle with `respond` true): on event 20 against a gang member, while the share
  of members already in goal `0x1d` HelpRespond is below the gang's percentage (`+0xd7`, `GangSetRespondPercentage`),
  one more free member is sent to fight the attacker (`0x002b75b8`).
- **The spot line**: `0x00165d40(gang, command)` makes one non-player member say a speech command once
  (gang `+0xd2` armed, mode `0x56f8` = 1) when he sees a hostile within his sight range; 22 `spot` by default.

| Binding | Type | Vtable | Members get | Process codes | Events |
| --- | --- | --- | --- | --- | --- |
| `TacticAttack` | `0x00` | `0x00543320` | melee (`Goal_Melee`), threat response 2 | 9 when no member has an enemy (checked each 1 s) | 1/11: an idle own member melees; 2 → 13 `TacMemberDied`; 20: idle members attack the offender |
| `TacticDefend` | `0x02` | `0x00543800` | `FollowAndDefend` (53) round the human, pushed on top of each member's goals; the **defended human gets nothing** and keeps his own; dogs (type 221) `AvoidEnemies` (32) | 11 when the human is gone or dead; 9 when no member has an enemy (1.5 s) | 2 on the human → 11; 17/18 on the human: members rush to him; 20 near him: his attacker's gang becomes an enemy |
| `TacticHoldTheLine` | `0x04` | `0x00543a40` | `HTLDefense` (100) spaced along the line, the rest `HTLOffense` (102) at `flag3` | 9 no enemies; 12 an enemy crossed (1.75 s); 14 after `hits` hits within `window` s | 2 on a defender: an attacker takes his spot, 13 |
| `TacticManWeaponPile` | `0x06` | `0x00543b60` | `ManWeaponPile` (82) | none | 1 → 5; 2 → 13; 16 → 6 |
| `TacticPursue` | `0x14` | `0x00543c20` | `Chase` (12) after the target gang's leader | 9 target gone or search over; 7 a member sees a target in range | 1 → 5 (not for a player's hit); 16 → 6 |
| `TacticWalkinTall` | `0x15` | `0x005440a0` | leader `MoveToFlag` at walk, others follow at 0.75 m | 7 when the nearest enemy gang is within `range` (1 s) | 1, 8, 16 |
| `TacticWander` | `0x16` | `0x00544100` | leader `Wander` (58), others follow at 4 m | none | 1, 10, 11, 16 |
| `TacticTravelPath` | `0x17` | `0x00543f80` | leader `TravelPath` (56), others follow at 1 m | none | 1, 10, 11, 16 |
| `TacticHanginOut` | `0x18` | `0x00543920` | `HangOut` (59) at the flag | none | 1, 10, 11, 16, 20 |
| `TacticMoveToFlag` | `0x19` | `0x00543bc0` | leader `MoveToFlag`, others follow at 3 m | none | 1, 8, 10, 11, 16 |
| `TacticVandalize` | `0x1c` | `0x00544040` | `Destroy` (91) | 1 when the zone has nothing left (3 s) | 1, 10, 11, 16; 23 a zone object broken → 16 `TacObjectDestroyed` |
| `TacticSteal` | `0x1d` | `0x00543ec0` | `Steal` (92) | none | 1, 10, 11, 16 |
| `TacticAvoidEnemies` | `0x20` | `0x00543380` | `AvoidEnemies` (32) | none | 1 → 5; 16 → 6 |
| `TacticUseFlag` | `0x21` | `0x00543fe0` | `MoveToUseFlag` (4) | 7 once the player came within `range` and every member left the flag | 1/11/16 alert (no callback) |
| `TacticConfront` | `0x23` | `0x00543740` | `Confront` (60) in formation | 9, 1, 7, 2 (below) | 1 → 5; 16 → 6 |
| `TacticIdle` | `0x24` | `0x00543aa0` | `Idle` (0) where they stand | 15 `TacAnimDone` once broken off | 1, 10, 11, 16, 20 |
| `TacticScout` | `0x27` | `0x00543da0` | `Scout` (111) or `PathScout` (112) | none | 1/11/16: the member fights and calls the gang |

Class details, confirmed (code) unless marked:

- **Attack** (constructor `0x003075c8`): Start (`0x00307fe0`) gives each member not in a `PedReaction` goal threat
  response 2 (brain `+0x21c`) and a melee goal, and has the gang say `spot`. Process (`0x003081a8`): every 3 s,
  members with no goal melee and go toward the farthest busy one; every 7 s, a team-up
  ([Gang fights](gang-fights.md#gang)).
- **Confront** (`0x0030e670`): with `targetGang` −1 the first member's brain target gang (`+0x264`) is taken, and
  without one nothing starts. Start (`0x0030ec20`) puts the posture anims in the gang's substitution table for anim
  `0x253` (four defaults from `0x005113a8` when none is given; the fifth marks a last, separate anim) and says the spot
  line (`spotLine` 135 `shadow` switches it to 136 `shadow_spot`). Process (`0x0030eec0`), every 250 ms between the
  two gangs' bounds: no route between the leaders (`0x0024e078`) → 1 when the other leader is in view, else 9; inside
  `approachRange` + both radii → 7, and 1 once inside `criticalRange`; leaving the approach range again → 2. Every
  500 ms, members facing the other leader (within 0.99 of his heading) play a random posture anim (`0x0025a3e0`).
- **HanginOut** (`0x00311ad8`): Start (`0x00312348`) without `fullAware` narrows each member's view (brain `+0x12c`
  − 20°) and sight range (`+0x130` × 0.75), gives each a `HangOut` goal round the flag (`range`, `harass`) and
  substitutes anim `0x25b` with eight idles (`0x00511480`).
- **Idle** (`0x003149f0`): `clearAnims` is passed to each member's `Idle` goal. With `dynIdle` set, events 1, 11 and
  16 do not fire the callback but end every member's `PlayDynIdle` goal (`0x00314fb8`), and Process returns 15 once
  none is left.
- **TravelPath** (`0x0031cbc0`): the leader's `TravelPath` goal (`0x002e0748`) takes the path, mode 1 when `loop` is
  true else 2, `reverse`, `gait`, `startPoint` and `delay` × 1000 ms. With no path the leader gets a `TravelFlagNet`
  goal (70) from where he stands. Every few seconds a free member within 5 m of the leader may use a usable flag
  within 5 m (`MoveToUseFlag`, inferred: props on the way).
- **UseFlag** (`0x0031d878`): each second members beyond `range` of the flag walk to it (`MoveToFlag`, gait 3) and those
  near it use it (`MoveToUseFlag`) with sight range `view`; once the nearest player is within `range` of the flag the
  tactic stops re-seating, gives each member's `MoveToUseFlag` goal a random 0-1 s delay (`0x002dbae8`) and returns 7
  once every such goal reached state 3 (inferred: they have left the flag).
- **HoldTheLine** (`0x00313f38`): `flag1`-`flag2` is the line and `flag3` the side the attackers wait on. The
  defenders are min(line length in metres, 60 % of the members), spaced evenly; the others stand near `flag3`.
  Code 12 fires when an enemy at `flag1`'s height (±0.5 m) is past the line and farther than `distance` from
  `flag3`; 14 when members took `hits` hits (event 16 with `+4` = 1, inferred: thrown objects) within `window`
  seconds.
- **Pursue** (`0x00317c40`): the members' `Chase` goals (`0x002b04f0`) get the two angles, the gait and the target
  leader. Process every 150 ms: 9 when the target gang is gone, empty or has no leader, or no route exists and the
  gangs are not in contact; 7 when a member (with LOS, `0x002223e8`) is within `range` of a target, who is added as
  his enemy. Every 100 ms it reads the members' `Chase` goals (`0x00318020`; whether they still see the target,
  inferred) and arms the spot line from them; once the search time (`searchMs`, set when the target is lost) has
  passed with no chase still running, the tactic ends the search (`0x00318130`) and returns 9.
- **Scout** (`0x0031a430`): Start (`0x0031af98`) gives each member a scout goal and substitutes anim `0x29c`. Process
  (`0x0031b030`), every 200 ms: members with enemies melee, and the gang's alert state is set when any is fighting.
  A hit, a sighting or an attack (`0x0031a818`) makes that member melee and, when the crime rules allow, call his gang
  (`Goal_CallGang`, radius `range` or twice the member's brain `+0x140`).
- **Vandalize** / **Steal**: each member's goal takes the zone, the delay and `leaderRange`; the vandal brain's
  `+0x28d` is set. Vandalize's code 1 comes from `0x0039a580(zone)` reporting nothing left to break.

## Attacking on sight {#aggro-on-sight}

Why an enemy-gang member attacks the player he sees, and what holds him back. Confirmed (code) at the addresses
cited; the `level87` facts are from its scripts on the disc.

**The chain.**

1. **The scan.** Every AI brain, in `Brain_UpdateGoals`, runs `Brain_MaybeScanEnemies` (`0x0028fa18`): every 2 s near
   the camera, every 8 s in the fight stance or farther than 30 m from it, at most 5 scans in one update. It lists
   the members of enemy gangs within the sight range (brain `+0x130`, 30 m when made; `HuSetLOSRange`), in front
   (field of view `+0x12c`, 110° when made; `BrSetFOV`) unless within the near radius, and in line of sight
   ([The enemy scan](ai-fights.md#enemy-scan), [Sight](ai-fights.md#sight)). The gangs must be enemies first:
   `GangMakeEnemies`, or a
   script form's `Enemies`.
2. **A new enemy** goes to the scanner's own event handler as event `0xb` (11): his script handlers, then
   `Gang_OnEvent`, which hands it to the gang's tactic, then his brain ([Events and
   GangSetMsgHandler](#gang-events)). `Brain_AddEnemy` sends the same event when the tactic's `+0x3c` flag is 0
   (types `0x12` and up).
3. **A gang with no tactic**: the gang soldier's think (`GangBrain_Think` step 3, [Gang
   soldiers](ai-brain-types.md#think-gang)) pushes
   the fight (`Brain_PushFightGoal(−1)`) as soon as his threat response (`+0x21c`) is not 0 and his enemy list is
   not empty. That is the plain "attack on sight". The shared handler's event `0xb` also sends a 20 m help call
   flagged "new enemy" ([The shared event handler](ai-brain-types.md#shared-events)).
4. **A gang with a tactic**: `Brain_PushFightGoal` returns at once, so the think starts no fight
   ([Tactics](#tactics)). The tactic turns the event into a callback code instead: HanginOut, Idle, Wander,
   TravelPath, MoveToFlag, Vandalize and Steal turn event 11 from one of their own members into code **3
   `TacSeeEnemy`** (`HanginOutTactic_OnEvent` `0x00312580`, `IdleTactic_OnEvent` `0x00315228`), event 1 into 5, 16
   into 6 and event 10 (`Brain_MaybeSpotPlayers`) into 4 `TacSeePlayer`. Confront answers its own process codes (1
   inside the critical range, or no route and the other leader in view; 7 inside the approach range; 2 leaving it; 9
   the target gone) and events 1 → 5, 16 → 6 ([The scripted tactics](#tactic-kinds)).
5. **The script decides.** The callback is the Lua `Strategy(gang, code)` of the strategy library (`global.lua`),
   which calls the gang's `StrategyPreset(gang, code, state)`. Each preset is a table from the current tactic and the
   code to the next tactic; a code the table does not name does nothing. So under a tactic, **whether a member
   attacks on sight is the script's choice**, made through code 3.

A HanginOut tactic without `fullAware` narrows each member's sight to the leader's range × 0.75 and his field of view
− 20° (`HanginOutTactic_SetAwareness` `0x00311c78`), so a hanging-out Destroyer spots the player from 22.5 m at most.

**`level87` (Mission 3: Payback).** `GangSetup(form)` (`global.lua`) makes every Destroyer gang (kind 19) an enemy
of `GangWarriors` (the form's `Enemies`), keeps the threat response of 2 (no form sets `SetThreat`) and starts the
form's `DefaultTactic` (HANGINGOUT when none, nothing when `Halt` is set) with the callback `"Strategy"`. The
presets' rows that matter (code → next tactic; 5 and 6, a hit or an attack warning, give ATTACK in every row):

| Preset | Row (tactic) | 3 `TacSeeEnemy` | Other codes |
| --- | --- | --- | --- |
| `StratHunter` (the default) | HANGINGOUT, MOVETOFLAG | CONFRONT | 4 nothing; 8 → HANGINGOUT |
| `StratHunter` | IDLE | **ATTACK** | 4 nothing |
| `StratHunter` | CONFRONT | - | **1 → ATTACK**; 2 → PURSUE; 7 nothing; 9 → HANGINGOUT |
| `StratHunter` | PURSUE / ATTACK | - | 7 → ATTACK, 9 → HANGINGOUT / 9 → PURSUE |
| `StratAggressive` | any but ATTACK, PURSUE, CONFRONT | CONFRONT | 7 → CONFRONT; others → CONFRONT |
| `StratRunAround` | WANDERWALK | CONFRONT | CONFRONT as `StratHunter`'s, 9 → WANDERWALK |
| `StratHomeIn` | HOMEIN | CONFRONT | 8 → HANGINGOUT; CONFRONT 1 → ATTACK, 2 and 9 → HOMEIN |
| `StratManPile` | HANGINGOUT / MANPILE | MANPILE / - | MANPILE 1 → ATTACK |
| `StratPassive` | any but ATTACK, CONFRONT | CONFRONT | 7 → CONFRONT; CONFRONT 1 → ATTACK, 2 and 9 → the default |

CONFRONT walks the gang up to the Warriors' leader in formation (`TacticConfront(gang, GangWarriors, approach,
critical, mode, ...)`) and its code 1 at the critical range turns it into ATTACK (`TacticAttack`: threat response 2
and a Melee goal for every member). So the usual Mission 3 sequence is: **seen → CONFRONT (the walk-up and the spot
line) → ATTACK at 2-3 m**; a hit at any point → ATTACK at once.

| Gang (form) | Tactic | Preset | Sight changes | What starts the fight |
| --- | --- | --- | --- | --- |
| `EarlyDest` (back alleys, `EDForm`) | CONFRONT from the start (14 m, 2 m, `WalkAggressive`) | Hunter | - | Confront's code 1 at 2 m, no sighting needed |
| `LiquorDest` (chapter 2's drunks) | IDLE | Hunter | `HuSetLOSRange` 4, `HuBlockLook` | code 3 within 4 m → ATTACK |
| `Wander` (2 min after the drunks die) | WANDERWALK | RunAround | - | code 3 → CONFRONT → ATTACK |
| `Chairs` (chapter 4, roof chairs) | HANGINGOUT | Aggressive | narrowed (not `FullAware`) | code 3 → CONFRONT → ATTACK |
| `Roofers` (chapter 4) | IDLE | Hunter | - | code 3 → ATTACK |
| `HStairs` (chapter 4, on the stairs) | IDLE | Hunter | `HuSetLOSRange` 1 | a hit (5, 6), or the player within 1 m |
| `RoofPursuit` (chapter 4) | HOMEIN (to where the player was) | HomeIn | - | code 3 → CONFRONT → ATTACK |
| `Event7`, `FlameTosser` (chapter 4) | none at set-up (`Halt`) | ManPile / Hunter | `FlameTosser`: 1 m, `BrDead` | their chapter's own script |
| the stoops (`SetUpStoop`) | USEFLAG (5 m, view 15 m) | Passive | - | Use-flag's code 7 → CONFRONT (`Hold`) → ATTACK |

`RespondOff` (HStairs, Roofers, Chairs, RoofPursuit, FlameTosser) only stops the tactic sending a member to help a
fight it hears about; it does not stop code 3. No form holds a gang passive by its threat response; the passive ones
are held by a short sight (`HuSetLOSRange` 1 or 4) or by `Halt`.

**Coney status and fix** (checked 2026-10-11, `repo:src/ai`; item 5 of the user's list):

- Coney runs the enemy scan only for members under TacticScout (`ScoutTactic::update` →
  `ScanSchedule::maybeScan`); `Brains::update` and `Brain::think` never scan. So no Destroyer under HANGINGOUT, IDLE,
  WANDERWALK or HOMEIN ever gets event 11, no code 3 reaches `Strategy`, and the gang stays in its tactic until a hit
  (codes 5 and 6) turns it to ATTACK. That is exactly "they see the player and do nothing until he attacks".
- Nothing sends event 10 (`kEventSawPlayer` has no sender), so code 4 never fires; `level87`'s presets ignore it, but
  other levels' may not.
- A gang soldier with no tactic never fights an enemy in his list: Coney has no `GangBrain_Think` step 3.
- The rest of the chain is in place: `codeOf` maps the events to codes, `StoryTactic::event` and
  `Tactic::fireCallback` call the Lua callback, `Brain::pushFightGoals` refuses under a tactic, and the scan itself
  (`scanEnemies`) sends event `0xb` to the human's handlers.

Fix, in order:

1. Call the scan (`ScanSchedule::maybeScan`) for every AI brain in the brain update, as `Brain_UpdateGoals` does,
   with the 5-per-update budget and the 2 s / 8 s interval (detail level and fight stance), not only for scouts.
2. Add `GangBrain_Think` step 3 (and the cop's and Warriors' equivalents, [Brain
   types](ai/index.md#brain-type-handlers)): with
   threat response, no fight goal and a non-empty enemy list, `pushFightGoals`.
3. HanginOut's awareness (× 0.75, − 20° unless `fullAware`) at its Start, undone at its End.
4. Then `Brain_MaybeSpotPlayers` (event 10, code 4) for completeness.

**Suggested tests** (synthetic unless named):

1. A kind-19 gang under TacticHanginOut with a recording callback, enemies with the player's gang; the player 15 m in
   front in the open: one call with code 3 within 2 s. The player behind him beyond 3 m, beyond 22.5 m, or behind a
   wall: no call. With `fullAware`: called at 25 m.
2. The same under TacticIdle with `HuSetLOSRange(h, 4)`: the player at 5 m → nothing; at 3.5 m → code 3.
3. A type-2 soldier in a gang with no tactic and the player in sight: a fight goal on the player at his next think;
   under any tactic: none.
4. The scan budget: eight brains due in one update, five scan, the other three on the next.
5. `level87`'s strategy library with the disc's scripts (`[disc]`): a HANGINGOUT gang given code 3 is moved to
   CONFRONT; CONFRONT given code 1 to ATTACK; IDLE given code 3 to ATTACK.
6. The player's path (`[disc][story]`): `level87` from its checkpoint 2 start, the pad's left stick at about 60 %
   toward a Destroyer group without pressing an attack; within 10 s a Destroyer has a fight goal on the player and
   the player takes a hit.

**Runtime.** Not observed in PCSX2 for this page: no save state reaches Mission 3 yet, and getting there means
playing two missions. A check for later, from such a state: log the tactic callback (`0x00306938`, the gang id and
the code) while the player walks into a hanging-out group's view; the expected order is 3, then 7 and 1 from
Confront, then the ATTACK tactic's members in Melee.

**Runtime, gangs with no tactic** (2026-10-12, PCSX2 2.9.94; confirmed (runtime)): at `level80` checkpoint 4, two
Destroyer gangs with no tactic and threat response 2 were made enemies of Cochise and Swan by `GangMakeEnemies`. One
update later every member had scanned (five an update), and within six updates all twelve had FindEnemy and Melee with
Fight or Spectate. After the attack order made them enemies of the Warriors too, Destroyers turned on the player
although he never attacked: five within 21 updates in one run, two within 25 in another and none in 450 in a third, as
the scan lists only the Warriors in a soldier's field of view. That is steps 1 and 3 of the chain above, with nobody hit
first; Coney does neither yet. Of the fix list above, the open PR #394 does item 1 (the scan in every brain); item 2
(the think step that fights a listed enemy) is still missing there. Details, timings and the test:
[Scripts](scripting.md#level80-fight-start).

## The boss fights {#boss-fights}

Every boss fight on the disc sets one of the `TacticBossScenarioA`-`H` tactics, and each gives the boss an ordinary
fighting goal. None uses the boss goals BossChatter, BossLizzies, BossLuther or BossBigMo (types `0x93`-`0x96`,
[AI goals](ai-goals.md#goal-boss-lizzies)). Those are pushed only by the old Boss tactic's Start (`0x003089b0`,
[type `0x09`](ai-code.md#t1-boss)), which only `TacticBoss` reaches. No script calls `TacticBoss` (the bindings scan,
[`TacticBoss`](../references/bindings/ai.md#tacticboss)), and `GoalBossLizzies` is not called either. Confirmed (code:
the only callers of the four pushers and of `Tactic_Boss` `0x00308870` are those two).

| Level | Binding | Tactic | The boss's goal, by stage |
| --- | --- | --- | --- |
| [`level5`](../references/bindings/story.md#level5) (mission 7) | `TacticBossScenarioA` | [BossDiegoVargas](ai-code.md#t1-boss-diego-vargas) | [below](#boss-diego-vargas): BigBrawler (Diego); BigThrower then BigBrawler (Vargas) |
| [`level81`](../references/bindings/story.md#level81) (mission 8) | `TacticBossScenarioH` | [BossChatterbox](ai-code.md#t1-boss-chatterbox) | 1 [BigLedgeThrower](ai-goals.md#goal-big-ledge-thrower), 2 [BigFighterA](ai-goals.md#goal-big-fighter-a) |
| [`level93`](../references/bindings/story.md#level93) (mission 10) | `TacticBossScenarioG` | [BossVirgil](ai-code.md#t1-boss-virgil) | 1 BigLedgeThrower, 2 [HideAndSeek](ai-goals.md#goal-hide-and-seek), 3 [BigFighter](ai-goals.md#goal-big-fighter) |
| [`level31`](../references/bindings/story.md#level31) (mission 11) | `TacticBossScenarioF` | [BossBirdie](ai-code.md#t1-boss-birdie) | StationaryShooterB ([type `0x8f`](ai-goals.md#goal-stationary-shooter-b)) |
| [`level55`](../references/bindings/story.md#level55) (mission 17) | `TacticBossScenarioB` | [BossLizzies](ai-code.md#t1-boss-lizzies) | StationaryShooter ([type `0x8d`](ai-goals.md#goal-stationary-shooter)), for class `0xed` |
| [`level84`](../references/bindings/story.md#level84) (mission 18) | `TacticBossScenarioE` | [BossLuther](ai-code.md#t1-boss-luther) | StationaryShooterA ([type `0x8e`](ai-goals.md#goal-shooter)) |
| [`level82`](../references/bindings/story.md#level82) (flashback 1) | `TacticBossScenarioD` | [BossRoof](ai-code.md#t1-boss-roof) | [Grabber](ai-goals.md#goal-grabber) and [BigDefender](ai-goals.md#goal-big-defender); the survivor AvoidEnemies or [BigBull](ai-goals.md#goal-big-bull) |
| [`level11`](../references/bindings/story.md#level11) (flashback 5) | `TacticBossScenarioC` | [BossMoe](ai-code.md#t1-boss-moe) | BigFighter, its level set to the stage |

The levels come from the bindings scan of the disc's scripts (one level each). The other members of each gang melee
(Moe's carry maces, [Mace](ai-goals.md#goal-mace)). Confirmed (code) at each tactic's AssignGoal.

## The Diego and Vargas fight {#boss-diego-vargas}

`TacticBossScenarioA` (level 5, [binding](../references/bindings/ai.md#tacticbossscenarioa); tactic vtable
`0x00543440`) gives each Hurricane a goal by character (`BossDiegoVargasTactic_AssignGoal` `0x00309ea0`, again on
gang events 19 and 22). Confirmed (code) at the addresses cited unless marked.

| Who, stage | Goal | Built from the tactic's tables |
| --- | --- | --- |
| Diego (120), any stage | BigBrawler (type `0x84`, `BigBrawlerGoal_Init` `0x002e8288`) | `diegoFatigue`, `diegoDamage`, `diegoProne` (all three), `diegoCycles[stage]`, no flag, no objects |
| Vargas (119), stage 2 | BigThrower (`BigThrowerGoal_Init` `0x002ed0a8`, vtable `0x00542880`) | `flags[1]`, `vargasCycles[2]`, `vargasFatigue[2]`, `vargasDamage[2]`, `vargasObjects` |
| Vargas, stage 3 | BigBrawler | `vargasFatigue`, `vargasDamage`, `vargasProne`, `vargasCycles[3]`, `flags[2]`, `vargasObjects` |
| anyone else | StationaryThrower (`StationaryThrowerGoal_Init`) | `minionObjects` |

After pushing a BigBrawler goal the tactic writes the stage into it (`BigBrawlerGoal_SetStage` `0x002e8840`, `+0x3f`).

**What the bytes mean.**

- **fatigue**: seconds. After a run of hits (BigBrawler) or a run of throws (BigThrower) the boss pushes a **tired
  goal** (type `0x90`, `TiredGoal_Init` `0x002e7970`, vtable `0x00542a00`) lasting `fatigue × 1000` ms
  (`BigBrawlerGoal_GetFatigueMs` `0x002e8818` reads entry `stage − 1`).
- **damage**: percent of his maximum health. The tired goal ends early once he has lost more than `damage` % of his
  maximum since it began (`+0x1a` = max × damage / 100, `+0x18` the health at the start;
  `BigBrawlerGoal_GetDamagePercent` `0x002e8830`).
- **cycles**: for the BigThrower, the throws before he tires (`+0x2a`; at half of them he taunts once, `0x002fb0f0`
  kind 2; after a throw at or past half, a 2 s wait); 0 means he never tires. For the BigBrawler the byte is stored
  (`+0x3d`) and **never read**.
- **prone**: stored (`+0x3a`-`+0x3c`) and **never read**; no reader was found in the goal's functions or the tactic.
- The defaults when a table is missing (`GoalBigBrawler` from a script): fatigue 5, 4, 3; damage 30, 20, 10; prone
  100.

**BigBrawler** (`0x002e8288`; fields `+0x10` a flag handle, `+0x14` the target's handle, `+0x18` state, `+0x1c` the
re-pick time, `+0x20` the chosen attack kind (45 none), `+0x24`-`+0x33` eight object ids, `+0x34` fatigue[3], `+0x37`
damage[3], `+0x3a` prone[3], `+0x3d` cycles, `+0x3f` stage, `+0x40`/`+0x41` the saved brain bytes `+0x0b`/`+0x0c`,
`+0x42` the next object index, `+0x43` the flag step, `+0x44` the last attack was kind 12 or 13, `+0x45` hits
taken, `+0x46` a counter flag). The constructor clears brain `+0x11e` (he cannot be chased) and, given a ninth
argument, puts that object in his hand (the tactic passes none).

- **Start** (`0x002e8418`): brain `+0x21c` = 0, may pick up off, field of view 2π, `+0x11e` = 0, human flags
  `|= 0x223c0`, the human's vtable `+0xe4` with 1e9 (inferred: a hit-react resistance), and the brain bytes `+0x0b`,
  `+0x0c` saved. **End** (`0x002e8530`): undoes them (threat response 2, field of view 1.92 rad) and restores the
  two bytes.
- **Process** (`BigBrawlerGoal_Process` `0x002e8f78`), by state: **0** (stage 1 only) a taunt, line `0x57` and anim 643
  (`ANIM_RAGE_START`) after 0-750 ms, then 2. **1** pick the best enemy (with the goal's own score hook below); if he
  is the current target and busy, close to the near range, say a line (`PlaySound`, `0x002fb868`) and wait 1 s;
  else an event 24 broadcast within 30 m (`0x00293768`), an EngageEnemy goal (taunt `0x47`) whose run-in distance
  `+0x30` is set to 6.25 (2.5 m), the cross + square special (kind 16) on an enemy already within 0.55 × far, else a
  31 % taunt, a turn to him, and brain `+0x0c` raised by one for Diego (`+0x0b` too, but it stays 0 for both bosses,
  below); then 2.
  **2** fight: an object in hand (a throwable for an AI, `Human_HeldIsThrowable(A, 0)`: set 5
  or a knife; or held set 4 or 6, `Human_HoldsKind4Or6`) is **thrown**: a turn to the nearest enemy (brain `+0x164`),
  then a set-command action of **command `0x10`** (cross; `SetCommandAction_Init(action, B, 0x10, 0)`, no delay; see
  [the throw](#boss-throw) below); otherwise a target re-picked every 4 s, an attack kind chosen
  (`Brain_PickAttack`) and kept until it can be queued in reach (moving in within its reach), with a 10 % shout
  while closing. **3** the flag cycle (Vargas, stage 3): walk to the flag (0.5 m), turn toward the camera, play 549
  `ANIM_GHETTO_PICK_UP` (the next of the eight objects appears in his hand, `0x002e8ee0` → `0x0024c280`, while the
  tactic has fewer than two objects alive, `0x0030a680`), step 2 m back along the flag's heading, then 2.
- **Hits** (`BigBrawlerGoal_OnHit` `0x002e8bc8`, from the tactic's event 1): a running boss shouts (line 8); in state 2
  a help call (20 m) and one more hit counted; at the **sixth** hit the tired goal is pushed, the count reset, and with
  a flag the state becomes 3.
- **Attack warnings** (`BigBrawlerGoal_OnAttackWarning` `0x002e8858`, event `0x10`): in state 2, unless tired, a grab or
  tackle he could escape is answered with command 3 (the counter); a player attacker becomes his target; an attacker
  within 1.5 m, while the boss has 10 % health or more and is free, is shoved off: line 11 or 14 and anim 653
  `ANIM_SPECIAL_ATTACK1_FRONT`.
- **Score hook** (`BigBrawlerGoal_AdjustEnemyScore` `0x002e8620`, goal vtable `+0x54`): −999 beyond his sight range, or
  when the enemy's task record (human `+0xd8`) holds a time (`+0x2c`) 751 ms or more in the past (meaning not traced);
  in state 2 also beyond 1.15 × far (except Diego in stage 3),
  else + 4 per metre inside the range, + 15 for a player, + 10 when knocked down; in state 1 + 4 per metre of distance
  and + 20 for a player not in a hold (`0xe0000`).

**The tired goal** (`0x90`; Start `0x002e7a68`, End `0x002e7b30`, Process `0x002e7f50`): Start saves and clears his
god mode (human flag `0x10`), no-reaction (`0x800`) and unstunnable flags, clears `0x910`, sets `0x8000000`,
**stuns him** (`Human_Stun`) and sets the deadline. Process: line `0x95` once, then line 8 while he stands stunned;
when the deadline passes or the damage limit is reached, his saved god mode back and `0x800` set; then the stun is
ended, and once he is free, god mode on, line `0x96` and anim 643; done on the next update. End restores the saved
flags and clears `0x8000000`. So **fatigue** is how long he stays open and **damage** how much of his health the
players may
take in that window.

**The health caps and the break** (`BossDiegoVargasTactic_CheckHealth` `0x0030a150`, each tactic update):

1. **Diego, stage 1 at 66 % health or below** (stage 2: 33 %): unless the tired goal is on top, his health is put back
   to **67 %** (34 %): he cannot go lower while not tired. While tired and not yet breaking, the **break** starts
   (`TiredGoal_StartBreak` `0x002e7c18`): stun ended, fight stance off, flags `0x800` and god mode `0x10` set, anim
   **671** `ANIM_SPECIAL_IDLE` chained to 672 (`0x0025a3e0`'s special case: held flag `0x20000`, state code 18), blend
   0.3 s, line `0x22`, `+0x21` = 1; health set to exactly 66 % (33 %). While held flag `0x20000` lasts the tactic's
   Process returns **18** (`TacAnimStart`) to the callback.
2. **The break is done** (`TiredGoal_IsBreakDone` `0x002e7e68`) once his state code is 18 with no held flags left, and
   **2 s** more have passed; the tactic then returns **1** (`TacFinished`). Until then, outside state 18 and not busy, god
   mode is cleared again.
3. **Vargas, stage 3**: at 66 % (first break) and 33 % (second, tactic `+0x69` = 1), the same start; when done,
   `TiredGoal_EndBreak` (`0x002e7da8`) plays 673 `ANIM_SPECIAL_IDLE_END` (blend 0.3 s), clears `0x10` and `0x800`, and the
   break count `+0x69` rises. Stage 3 reports 1 once neither boss is standing (`0x0030a458`).

**The clips 671-673.** They are `missing_anim_filler` only in the generic anim set ([anim ids](../references/anim-ids.md)).
The two bosses' own anim sets carry them, and a character's set answers an id before the generic one
(`CharacterInstance_GetAnim` `0x00175080`: an override slot or gang clip, else the character data's clip). No level 5
script loads or overrides them (`level5*.lua` set no `ANIM_SPECIAL_IDLE` with `HuUseAnyAnim`). From the disc's anim
headers (the lookup confirmed (code); the names from the disc):

| Anim id | Diego (`hurr_de`) | Vargas (`hurr_va`) |
| --- | --- | --- |
| 671 `ANIM_SPECIAL_IDLE` | `hurr_de_collapse_c` | `var_tired_enter` |
| 672 `ANIM_SPECIAL_IDLE_START` | `hurr_de_collapse_b` | `var_tired_cycle` |
| 673 `ANIM_SPECIAL_IDLE_END` | `hurr_de_collapse_e` | `var_tired_exit` |

The same sets give Diego 643 `hurr_de_rage` and 653 `hurr_de_360_attack`, and Vargas 549
`hurr_va_pickup_large_obj`, 643 `var_rage1` and 653 `var_clearing`. Any other character in a tired goal would play
the filler.

**BigThrower** (Vargas, stage 2; Process `0x002ed718`): state 0 a taunt (line `0x11`, anim 643); 1 walk to the flag (0.5
m) or tire (above); 2 turn toward the camera and play 549 (`ghetto_pickup`; for others than Vargas the pickup clip is
overridden with the name at `0x00567a88`), the next object appears in his hand (`0x002ed498`); 3 aim: turn within 45°,
line of sight (six misses drop the target), then 4; 4 throw (`BigThrowerGoal_HoldDistance`, then a set-command action of
command `0x10` with a 100 ms start delay) and back to 1 when the hand is empty. Start (`0x002ed168`) and End
(`0x002ed260`) as the BigBrawler's, plus that override.

### The throw {#boss-throw}

Command `0x10` (cross) reaches the dispatcher like a pad press. With a held object of set 4, 5 or 6 it goes to
`Player_CrossWithWeapon` (`0x002880d8`, [Combat](combat/index.md#dispatch)). Confirmed (code). For an AI (brain type
not 0) the clip is thrown at **his current target**: the human's target handle (`Human_GetTargetHandle` `0x00226e20`,
`+0xc8`, refreshed from the brain's target). He turns to it over the time to the clip's first event, then
`Human_StartMeleeThrow` (`0x00261e78`). A player instead searches (`Player_PickThrowTarget`, 10 m for set 4, 20 m
for 5 and 6). The clip by held set and gait:

| Held set | At a jog, run or sprint | At a walk, outside the stance | Standing or in the stance |
| --- | --- | --- | --- |
| 4 (overhead) | 507 | 506 | 505 |
| 6 (ghetto) | 553 | 552 | 551 |
| 5 (one hand) | 472 | 471 | 467 |

Set 5 first tries the bottle smash (473 / 475) on a human in reach. Vargas's objects (the names in `VargasObjTypes`
in `level5_chapter3.lua`: `dyn_vargas_bookcase_a`, `dyn_vargas_sofa`, `dyn_vargas_stove_a`, `dyn_vargas_washer_a`,
`dyn_fridge_c`, `dyn_porcelain_weap_b`, `dyn_porcelain_weap_c`) are all `overhead_weapon`
([Objects](../references/objects.md)), set 4, so he throws 505-507 (the generic clips; his set has none). The hit
deals the object's `CfgObj` `+0x58` ([Combat moves](combat-moves.md)). In BigThrower state 4 the goal has just
validated the brain's enemy (brain `+0x124`), inferred to be the same human. In the BigBrawler the throw also goes to
his target, which need not be the nearest enemy he turned to first.

### The BossDiego program {#boss-diego-ops}

**No script calls `GoalBossDiego`.** The Diego fight uses `TacticBossScenarioA` ([Diego and
Vargas](ai-gangs.md#boss-diego-vargas)), so this goal is never built in play. Confirmed (code) for the interpreter;
confirmed (disc), per [AI bindings](../references/bindings/ai.md#goalbossdiego), that no script uses it.

**The binding's arguments** (`Goal_BossDiego` `0x002a1aa8`, `BossDiegoGoal_Start` `0x002a1b50`):

- **The variant** `+0x10`:
    - variant 0 runs the program at `0x00510e28`, keeps his weapon, resets the inventory and gives one revive;
    - variant 1 runs `0x00510d28` with 3500 health and a 1 s start delay.
- **Three flag handles** `+0x14`-`+0x1c`.
- **The rest of Start**: melee ranges 1 / 2 m, steering off, threat and damage response 0, and the game flag `+0x430`
  set.

**The program** is a list of (op, argument) s16 pairs. `BossDiegoScript_Next` (`0x0029fa28`) reads them:

- a negative op jumps back by that many pairs;
- op `0x97` jumps to label *n*;
- op `0x96` is label *n*;
- op 100 is "fetch the next pair". Every op below returns to 100 when it is done.

`+0x34` is the op's timer and `+0x2c` a counter.

| Op | Does |
| --- | --- |
| 101 | calm: threat and damage response 0, no pick-ups |
| 105 | fight-ready: pick-ups on, responses 2, a PathBlocker goal (10) |
| 110 | the attack weight override = table *arg* of `0x00510c40` (45 bytes each) |
| 115 | fight player 1 (game `+0x228`): responses 2, target, `Goal_Melee`(*arg* s) |
| 116 | the target (or player 1). Beyond 2 m: calm, normal mode, and run to him (gait 5, radius 1.0 or 1.5 m, 50/50), then op 117. Within 2 m: `Goal_Melee`(3-6 s). |
| 117 / 118 | When idle, the counter rises. At 2 the counter resets and op 125 runs with a random 5-9 s. Otherwise responses 2 and `Goal_Melee`(3-6 s). |
| 120 / 121 | target player 1 / the current target |
| 125 → 126 → 127 | calm, untargeted and stunned for *arg* s; then unstunned, normal mode and no-react; once idle, responses 2 |
| 130 | god mode = (*arg* ≠ 0) |
| 140 / 141 | Alternate every 10 s between `Goal_Melee`(10 s) and `Goal_AvoidEnemies`(20 m, 30 m). |
| 160-163 | Calm, no pick-ups, then walk to flag *arg* (0-3 from the goal's handles, 4 the named flag at `0x00562ba8`). The gait is 2, 4, 4 or 3; the radius 1 m, or 5 m for 162. |
| 180 → 181 | clear actions; after 200 ms call the Lua function named at `0x00562bb8` when it exists |
| 192 / 199 | wait for the timer |
| 193 | 50 %: jump to label *arg* |
| 194 | anim slot 4 = clip `0x198` (*arg* 0) or `0x203` (1) |
| 195 / 196 / 197 | A Lua call, then clear, unstunned and no-react, and jump to label 2, 3 or 1. Op 196 also makes the human at `0x006e9404` pop to his goal `0x48` and run op 197. |
| 198 | wait *arg* s |
| 201 | with an object in hand, press cross at the target (a throw); wait *arg* s |
| 202 | after the timer, count `+0x3c` + 1 |
| 203 | responses 2 |
| 204 | a porcelain weapon in hand: one of 19 names at `0x00510bc8` (*arg* 0) or of 2 at `0x00510bc0` (*arg* 1) |
| 205 | look at the target; wait *arg* s |
| 210 | the player's gang avoids him (*arg* 1, `BossDiego_GangAvoid`) or stops (0) |
| 211 | press cross (*arg* 0), cross + square (1) or circle + cross (2, 3), then calm |
| 212 | *arg* 0: auto-escape, double damage, no-react, untouchable, flags `0x8000100`; *arg* 1: auto-escape and untouchable only |

Not needed for any level: Coney can leave `GoalBossDiego` out until a script calls it.

## Brain bytes `+0x0b` and `+0x0c` {#brain-boosts}

- **`+0x0b`, turn boost** (`Brain_SetTurnBoost` `0x0028cdf8`, 56 callers; forced 0 for Diego and Vargas, classes
  `0x77`, `0x78`, and for a class whose `+0x11b` is 6 or 7): an AI human's turn limit per update
  (`Human_GetTurnRateForGait` `0x002212d0`, through `Human_MaxTurn`) is the gait's AI word × (b + 1) for b ≥ 0, or
  ÷ (1 − b) for b < 0; a pad-controlled human ignores it. Above 0 it also swaps the AI's fight-stance walk (anim slot
  14) from 372 to 380, the player's combat walk (`0x00243848`). EngageEnemy raises it by one for its run-in.
  **`GoalRunCarrotRun`**: Start saves the byte (`RunCarrotGoal_Start` `0x002e1320`), End restores it, and every fourth
  update `RunCarrotGoal_UpdateSpeed` sets it to the saved value + 0 (no boost), + 2 (a speed boost up to 0.5) or + 3
  (above 0.5): a runner being caught turns 3 or 4 times as fast as normal, so he keeps to the path at speed. Confirmed
  (code).
- **`+0x0c`** (`Brain_SetStartBoost` `0x0028ce60`; forced 0 for a class whose `+0x11b` is 6 or 7): when above 0, an AI
  that starts moving from rest gets state code 8 instead of 6 (`0x00243848`; what 8 changes is not traced).

## The Warriors' pick-ups {#warrior-pickups}

`WarriorBrain_Think` (`0x003052f0`), every sixth think, while the brain may pick up (`+0x265`), holds nothing, and its
gang kind's pick-up factor (`0x001644f8`) is not 0: with chance 25 × factor %, when nobody is close around it
(`0x0029eaa0`: none of its eight [sectors](ai-moving.md#neighbour-sectors) has flag bit 1 or 2 set, that is no human
within 1.5 m ([Neighbour sectors](ai-moving.md#neighbour-sectors)); this corrects an earlier reading as a goal check),
the nearest pickable object within 1.5 m (`0x0029d5f0`) is fetched (`0x002faf98`). When the leader is free, the gang's
`+0x32` is 1, a member is under attack and the Warrior's own enemy list is empty, the search's filter mask is `0x30000`
instead of `0x10000` and its last flag 1; the radius stays 1.5 m. **`CfgWarriorWeapons(false)`** (game state `+0x5704`
= 0) refuses an object whose type class (`+0x87`) is 4, a weapon; other objects are still taken. A Warrior holding a
weapon of kind 4 or 6 with no enemies drops it unless its gang's tactic is type `0x26`. Confirmed (code).

## Spawners {#spawners}

A gang has four **spawners** of 0x130 at gang `+0x640` (`GangAddSpawner`, `0x00166ff8`; a fifth is ignored). Their
update, `0x001681a0`, runs each in use against its **state** at `+0x52`; the states, their names and the scripts'
uses are in [Spawner states](../references/spawner-states.md). Confirmed (code):

| Offset | Meaning |
| --- | --- |
| `+0x00` / `+0x14` | position / name |
| `+0x50` | in use; cleared once the spawner has made its total (`+0x56` against `+0x58`) |
| `+0x52` | state; `GangAddSpawner`'s kind, `GangStartSpawner`'s mode (`0x00168cd0` accepts 0-5, 7, 8, 9, 11) |
| `+0x51` | a wave is running (a negative `+0x5a`, step 2) |
| `+0x5a` | how many of its humans may be alive at once (`GangAddSpawner`'s `maxConcurrent`, `GangSetMaxConcurrent`) |
| `+0x5c` | its humans alive: +1 per spawn, −1 when one's brain is torn down (`0x0028c4f8` → `0x001672a8`) |
| `+0x60` / `+0x64` | delay between spawns in ms / the next spawn time: now + delay after each spawn (100 ms in state 6, `0x00168158`) |
| `+0x68` | the state's **value**: seconds for 2, metres for 3, 5, 6 and 8 (`GangStartSpawner`'s last argument) |
| `+0x70` / `+0x74` | a door it opens to let each human out, and how long it stays open |
| `+0x7c` | state 2's deadline: the value in seconds after `GangAddSpawner` (only it sets this; `GangStartSpawner` to 2 does not) |
| `+0x88` | a distance from player 1 inside which it waits: 25 m in a gang of kind `0x17`, otherwise 0 (none) |
| `+0x8c` | **must be off screen** (`GangSetSpawnerMustBeOffScreen`; `GangAddSpawner` sets it for kind 11 only) |
| `+0xac` / `+0xb4` | the dispatch queue: the entry being served, then 4 entries of 0x1c (a time, a count at `+0x14`, bytes) |

1. **Ready?** 0 never; 1, 6, 7, 8 and 10 always; 2 once past its deadline; 3 while player 1 is within the value
   (`0x00336d88`), 5 while he is farther; 11 while the gang's living members (`0x00166158`) are fewer than gang
   `+0xb04`, and back to 0 once the gang has spawned (`+0xb02`) its total (`+0xb00`). A running wave (`+0x51`)
   counts as ready in any state, even 0.
2. **Waves**: a **negative** `+0x5a` (from `GangAddSpawner` or `GangSetMaxConcurrent`) of −*n* means waves of *n*.
   With no wave running, a ready spawner waits until `+0x5c` is 0 (every human of the last wave gone), then sets
   `+0x51`; it then spawns while `+0x5c` < *n* and clears `+0x51` on the spawn that makes `+0x5c` reach *n*. A
   positive *n* never sets `+0x51` and simply spawns while `+0x5c` < *n*. State 11 ignores the limit.
3. **Gates**, in order; any one failing skips the spawner this update: player 1 at least `+0x88` away (when non-zero);
   when `+0x8c` is set, [no camera sees the spot](#spawner-unseen); `+0x5c` under the limit (or state 11); the next
   spawn time reached.
4. **Dispatch** (4 and 9, decided in step 1; neither is ready itself): when a queued entry is due (`0x0016dda8`)
   and a gang slot is free (`0x0016d458`), a new gang
   `Responder<n>` is made: of type 1 for 4, only while the game's count `+0x324` is under `+0x326`, of the spawner's own
   type for 9, which also takes its owner's friend and enemy masks. The state becomes 6 (from 4) or 10 (from 9), and
   returns once the entry's squad is complete; an entry whose byte `+0x17` is 3 may also start `Tactic_RiotCop`.
5. **Place** the human: 6, 8 and 10 out of the camera's view ([below](#spawner-placement), `0x001673b8`); 7 on a
   pedestrian flag out of view ([AI and the world](ai-world.md#spawner-reinforcements), `0x001679e8`); the others at
   the spawner.
6. **Spawn** (`0x00167ea8`, named `<spawner><count>`), open the spawner's door, count it.

**The level's cap.** Confirmed (code) at the cited addresses. The update above runs for a gang only when the
gang's update (`Gang_CachePlayerInfo`, `0x00166708`, called with 1 from `Gangs_Update` `0x0016d170`) finds the game's
count under its cap, asked **once per gang** before its four spawners run, so one gang's spawners may each make one
human past it in the same update:

| Gang | Count | Cap | Set by |
| --- | --- | --- | --- |
| police (gang `+0x2c` = 1) | game state `+0x324` | `+0x326` | `SetCopSpawnMax` (`0x00299520`) |
| any other | game state `+0x432` | `+0x434` | `SetSpawnMax` (`0x00299510`) |

- **Up**: every spawn adds 1 to `+0x432`, and a spawn in state 6 also 1 to `+0x324` (`0x001681a0`, after the spawn).
- **Down**: `GangSpawner_OnHumanGone` (`0x001672a8`, from `Brain_TearDown` `0x0028c4f8`) takes 1 off `+0x432` for every
  spawned human, and 1 off `+0x324` (and the spawner's `+0x5a`) when the gang is police and the spawner is in state 4
  or 6; a responder spawner (9 or 10) of another gang takes 1 off its `+0x5a` only. `Gang_RemoveMember` (`0x001664d8`)
  also takes 1 off `+0x432` for a human leaving his gang, unless his spawner is in state 4, 6, 9 or 10. When a
  dispatch's squad is complete (states 6 and 10), the update takes the responder gang's living members off `+0x432`
  (and off `+0x324` in state 6) again.
- **Reset**: the level reset (`GameState_ResetForLevel`, `0x00418c68`) sets both caps to 16 and `+0x324` to 0;
  `+0x432` is zeroed only when the game state is made (`GameState_Construct`, `0x00418588`).
- A spawner's own humans may still be counted after it has made its total: `+0x50` cleared stops its spawning, not
  the count.
- Confirmed (runtime), PCSX2 2.9.129, `level34` checkpoint 2 standing still (2026-10-21): `+0x432` read 20-21 under
  `+0x434` = 45 (`SetSpawnMax(45)`), and `+0x326` = 16 (`SetCopSpawnMax` is not called). With a breakpoint on
  `0x001681a0` for a gang not of the police kind, the update ran within one frame for gang kind 0 (called from
  `0x0016698c`); with `+0x434` written to 10 it did not run for 600 frames while the count fell from 21 to 12 as humans
  went, and ran again within a frame once 45 was written back. With the cap at 14 the count rose from 12 to 14 and
  stayed there for 1,200 frames.

**The type** (`0x0016d810`): the spawner keeps an index at `+0x4c` into its ten types (`+0x24`). Each spawn first
adds 1; at 10, or at a slot holding 0, it goes back to 0; the type at the index is used. So the types are taken **in
turn**, a 0 ending the list. `GangAddSpawner` does not reset the index, so a new spawner starting from 0
(inferred: the gang record starts zeroed) takes the **second** type first when there is one. State 6 (a police
dispatch) uses the model `cops_vc1` and type `0x103` for an entry whose byte `+0x17` is 3 or more, and otherwise the
type in turn with one of the four models at `0x0050cb40` (`Random_Int(3)`, 0-3); the other states use the spawner's
model (`+0x20`). Confirmed (code).

**The human** (`0x0016d860`): a free human (none: nothing spawns), its name, `Human_Init(type, gang, position)`, facing
the spawner's heading (`+0x54`, degrees), brain `+0x210` = the spawner's slot, the spawner's animation (`+0x1c`)
unless it is `nothing`, and then **the callback** (`+0x18`, interned at `GangAddSpawner`): looked up by name (slot
`+0x4c`, so `Table.func` and `Table:func` work) and, when it resolves to a function, called with **three
arguments**: the human's handle (slot `+0x5c`), **the gang's id** (gang `+0x30`, a short, slot `+0x64`) and **the
spawner's name** (gang `+0x654 + 0x130 × slot`, a string, slot `+0x7c`). Nothing else is set up for it: the engine
writes no table or global first. The callback runs before the caller opens the door, counts the spawn and, for
states 6 and 10, adds the human to its responder gang and gives its dispatch goal. Confirmed (code). `level87`'s
`StoopCallBack(human, gang, name)` appends the human to `tblStoop[gang].humans`, a table its own `SetUpStoop` made
for that gang id before the spawner started; called with the handle alone it indexes `tblStoop[nil]` and fails.

### Off screen {#spawner-unseen}

With `+0x8c` set the spawner asks `Camera_AnyPlayerCanSeePoint(radius 0.3, distance 0, point)` (`0x001202e8`) about
a **sphere of radius 0.3 m centred 1.6 m above the spawner's position** (z + 1.6), and skips the update when the
answer is yes. It asks every player's camera (`0x005d9150[i]` for i below the player count `0x0050b198`) and is yes
when any of them sees it. One camera's test, `Camera_CanSeePoint(radius, distance, camera, point)` (`0x00122548`):

1. **Range**: the limit is the smaller of the distance and the camera's view distance (camera `+0x58`, vtable
   `+0x20c`); a distance of 0, as here, means the view distance. Unseen when |point − camera position| − radius
   exceeds it.
2. **Frustum**: unseen when the sphere lies wholly outside any of the camera's six planes
   (`Camera_FrustumTestSphere`, `0x00121fd8`, vtable `+0x16c`: plane *i* at camera `+0x70 + 0x10i` as a normal and
   distance; outside when n · p − d ≤ −radius).
3. **Occlusion**: a ray from the camera's position toward the point, as long as the distance less the radius
   (less 1e-5), through the level's collision mesh (`CollisionMesh_RayCast`, mask 0), skipping the materials
   30 `LOW_FENCE`, 2 `GLASS`, 122 `RAILING` and 107 `CHAINLINK_NOCLIMB`. Any hit: unseen. No hit: seen.

So the sphere **is** occlusion-tested, by one ray to its centre (the radius only shortens the ray and widens the
range and frustum tests). Confirmed (code) at `0x001685e8`-`0x00168624` and `0x00122548`. The test uses the
spawner's own position whatever the state, also for the states that place their humans elsewhere.

### Out of sight {#spawner-placement}

`0x001673b8` places the humans of states 8, 6 and 10. Confirmed (code):

1. **Camera**: player 1's; with two players, for 6 and 10 the player whose camera is nearer the dispatch entry's
   position (kept at `+0xe1` for the other states).
2. **Search**: from the route node of that player's human, a best-first search over the route graph (`0x00251d28`)
   toward a goal point, which returns the first node either more than **70 m** (`0x0050cc60`) from the camera or more
   than the spawner's **value** in metres from it and outside a cone around the camera's forward of half its field
   of view + 10° (`+0x2ac`). The first goal is 100 m straight ahead of the camera; up to 16 more tries each turn the
   forward by a random angle outside that cone and put the goal at 2 × value. No node in 17 tries: no spawn this
   update. In detail ([the search](#spawner-search)):
    - **Origin**: the camera's position (vtable `+0x21c`) with its z replaced by the player human's z (position
      table `0x00714b00`); every distance below is from this point, in 3D.
    - **Start node**: the player human's area (`0x00250708`), then its node nearest the human's position that he
      reaches in a straight line, up to 30 tried by distance (`0x00251150` → `0x00250e98`, as for a route's ends in
      [Path planning](ai-moving.md#path-planning)). No area or no node: no spawn this update, and no try is made.
    - **Cone**: the camera's forward (its orientation's y axis: vtable `+0xac`, then `0x003363b0`) with z then set to
      0 and **not** normalised again; half-angle *h* = (field of view × 0.5 + 10)° in radians.
    - **Try 1**: goal = origin + 100 × the forward taken *before* its z is cleared.
    - **Tries 2-17**: θ = `Random_Float(h, 2π − h)` (`0x00335420`: a uniform draw, the raw random number / 2³² scaled
      into the range); the flattened forward is turned by θ about the vertical (a quaternion about (0, 0, 1),
      `0x00511740`), and goal = origin + 2 × value × that vector. Each try turns the **original** forward, not the
      previous try's; a θ is drawn before every search, so 17 draws are made and the first is unused. The cone the
      search tests keeps the original flattened forward on every try; only the goal moves.
3. **Second player**: with two players, a node that the other camera can see 1.6 m above it (`0x00122548`, its
   distance argument the larger of the value and its view distance capped at 70 m) is refused.
4. **Turf**: the node must lie in one of the gang's turf boxes (`Gang_IsPointInTurf`, `0x001652e8`); a gang with no
   turf takes any. Otherwise no spawn this update.

The human stands on the node found. The function's other branch (a flag near the point, checked for being unseen) is
not reached from its one caller.

#### The search {#spawner-search}

`0x00251d28(value, h, startNode, origin, goal, coneAxis)`, confirmed (code):

- **A\*** over the route nodes (C records of `0x20`) from the start node toward the goal: f = g + h at node `+0x16`,
  g at `+0x18`, h at `+0x1a`, parent at `+0x1c` (all `u16`, 1/16 m). h = 16 × the 3D distance from the node to the
  goal (`0x00251820`); an edge costs as in [Path planning](ai-moving.md#path-planning) (`0x00251890` with mask `0xff`,
  so the `ClimbFilter` extras apply when it is on). Unlike the route search there is **no mask test** and no 65000 cap:
  every edge is taken except one whose D word has **bit 31** set (a hazard-avoided link), which is skipped. A node
  already open is updated only when the new g is lower.
- **Popping** the node with the lowest f (ties: the heap's order) marks it closed (`+0x18` = `0xabcd`) and tests it,
  the start node included: found when its distance from the origin exceeds 70 m, or exceeds the value and the unit
  vector origin → node has a dot product with the cone axis below cos *h*. Found: the goal point is overwritten with
  the node's position and the search returns 1.
- **Limits**: the open list is a heap of at most **1,000** entries (`0x002531c0`), the closed list at most **3,000**
  nodes (`0x002534d0`). An empty open list, a full open list on a push or a full closed list ends the search with
  nothing (0); on leaving, the closed marks are cleared.

## Crimes and the police {#crimes}

A **crime report**, `0x0041b8b0(state, pos, type, offender, victim, severity, mode, count)` on the game state
`0x0051489c`, is how scripts and the game raise the [crime types](../references/crime-types.md). The game state's
crime fields, confirmed (code): `+0x270` the crime scene, `+0x288` reporting on (`ReportCrime`), `+0x290` the
player's last crime type, `+0x294 + type` the responders per type (`CfgCrimeResponders`), `+0x2dc` the Lua callback
(`CfgSetCrimeCallback`), `+0x32b + type` enabled (`CfgEnableCrimeType`; read only for type 12, by the police brain at
`0x00300910` and `0x00301770`). The wanted timer a report starts, the crime level and the unused severity are on
[Crimes: wanted](crimes.md#wanted).

1. Nothing while reporting is off. An offender in a gang of kind 1 (police) or `0x17` is ignored.
2. With an offender: every police gang turns hostile to his gang, and his gang to them except for types 7 and 12
   (`0x0016c3a8`); the callback runs with his gang and the type (`0x0041ae60`).
3. The `CrimeScene` flag moves to the position when it changed.
4. **Responders**, when `mode` is 1: `0x0016df68(type, kind, count, ...)` queues the crime on the nearest spawner of
   any gang in state 4 or 6 ([Spawners](#spawners)); kind 1 for types 0, 2, 3, 5, 8, 11 and 13, kind 3 for 9; count
   the type's responders, or `count` itself for type 4. Types 6, 7, 10, 12 and 14 send none. For types 0, 2, 3 and 8
   the offender's gang also notes the time (gang `+0x5f4`, a wanted timer, inferred). Type 1 (kind 1) queues only
   in step 5.
5. Type 1 acts only when the nearest store front (activity `0xe`) lies within 10 m (squared distance under 100).
   When that store is not yet robbed, it queues its responders with a delay of `CfgBreakAndEnterDelay` and no
   offender (`mode` 1 only), sends message `0x12` to the `strobe` nearest the store's flag when that one is within
   6 m (squared distance under 36), and switches the store's buyers off; robbed or not, the store is then marked
   robbed. Details: [Crimes: which break-ins call the police](crimes.md#break-in-police). Types 0, 2 and 8 score a
   statistic for a player offender against a victim of brain kind 1, 4 or 5, once per victim (`0x004ed948` on
   `0x006fe490`; a statistic, inferred).
6. When the offender is in player 1's gang, `+0x290` takes the type unless it holds 7 or 12, and the HUD is told
   (`0x001b2520`).

Who reports, confirmed (code): `CrimeIsHappening` (`0x0041b6e0`, mode 1), `SpawnCustomCrime` (`0x0041b7a0`, type 4),
an alarmed glass pane ([World objects](objects.md#pane)) and the code at `0x0021b290`, `0x0022d908`, `0x002e6668`,
`0x003961d0` and `0x00394da8` (type 1), `0x00301cf8` (type 2), `0x0041bec8` (type 9), and `0x002a83f8` and
`0x002c4360` (a type from their own records). The crime names are a function (`0x0041d2c0`) with no callers; the
numbers they go with are inferred from those types (4 `Custom`, 1 `BreakAndEnter` for the store, 9
`PrecinctAttack`).

## Warrior commands {#warrior-commands}

A war chief (a player human whose `+0x3ac` is 1) orders the crew, his gang, with one of seven **Warrior
commands** ([Commands](../references/commands.md#warrior-command)). The menu (HUD `0x001a6c58`, R2 and the right
stick: [HUD](hud.md#warrior-command-menu)), `WCIssueCommand`
and the game itself go through one dispatcher, `0x0041c4e0(state, chief, command, forced, pos, arg)`, confirmed
(code):

1. Nothing while commands are locked (game state `+0x411`), the player's menu is locked (`+0x42e` + player), the
   command is disabled for the player (`+0x41e` + player × 7 + command, `WCEnableCommand`; the player is human
   `+0x1b0`) or the human is not a war chief.
2. The command becomes the player's last (`+0x41c` + player). **Unforced** (the menu): the chief is *quiet* when a
   line of his is still playing (his speech handle resolves) and his `+0x194` is clear. Then the **range check**
   (`WarChief_CheckCrewInRange`, `0x0041c338`): when the gang's nearest other member is more than 50 m away (squared
   distance over 2500), the command is **refused** and, unless quiet, the chief says **18 `range`**, or **92
   `range2`** when any member is cuffed or knocked out (`Human_SayCommand(1.0, chief, …, no interrupt)`); game state
   `+0x414` + player records the refusal ([The out-of-range flag](#warrior-range-flag)).
   Within range, giving the current command again (the tactic's `+0x18`
   clear) only repeats its line (`WarChief_SayCommand`, `0x0041cc40`) and, for 0 under a tactic of type `0x12`,
   calls `0x003114e0` on it. **Forced**, only defend (2) goes through the range check (quietly).
3. Otherwise the gang's tactic is cleared and the command's started: 0 `0x00310e00` (given 9.0), 1 `0x00320530`
   (unforced, the chief also plays clip 0x2a4 or 0x2a8, and the gang's target is set from his), 2 `Tactic_Defend`
   around the chief (clip 0x2a3 or 0x2a7), 3 `0x00313400` (the standing tactic of type 3, [the first
   mission](ai/index.md#level99)) or `0x003128a0` for a gang whose brain `+0x2d5` is set, 4 `0x00319570` (given 75.0), 5
   `0x00320b60`; 6 starts nothing.
4. The chief says the command's line (`WarChief_SayCommand`, `0x0041cc40`, [below](#warrior-command-lines)): 0
   `follow` (`follow_hide` while hidden in shadow, `Human_IsHiddenInShadow` `0x00228168`), 1 `attack`, 2 `defend`,
   3 `hold` (`holdhide`), 4 `scatter`, 5 `steal` when the nearest store flag (activity `0xe`) is closer than 64 (8 m
   if that is a squared distance, inferred) and its zone still has objects (`ObjZone_HasObjects`, `0x0039a580`),
   else `vandal`; 6 none. **A forced command says nothing**, and neither does a quiet one (step 2).

### The out-of-range flag {#warrior-range-flag}

`WarChief_CheckCrewInRange(state, chief, quiet, forced)` (`0x0041c338`) keeps one byte per player, game state
`+0x414` + the chief's player index (human `+0x1b0`), that says whether his crew was out of range at the last check.
Confirmed (code) at `0x0041c338` unless marked:

- **Who checks.** The dispatcher (`0x0041c4e0`, calls at `0x0041c600` and `0x0041c738`), as step 2 above, and the
  player's own update, `PlayerBrain_Update` (`0x003035d8`, call at `0x003037c4`): every 40 updates (brain `+0x34`
  modulo 40) it calls the check with `quiet` and `forced` both 1, so **no line is spoken** and the byte simply follows
  the crew ([The player](ai-brain-types.md#think-player), step 4).
- **What it writes.** Only for a war chief (human `+0x3ac` = 1): the gang's nearest other member
  (`Gang_FindNearestOtherMember`) within 50 m (squared distance 2500 or less) clears the byte and the check returns 0;
  farther, it sets the byte to 1, says 18 `range` or 92 `range2` unless `quiet` or `forced` is set, and returns 1. A
  gang with no other member returns 0 and leaves the byte as it was; so does a human who is not a war chief.
- **Who reads it.** An instruction scan of `SLUS_212.15` for offset `0x414` finds two readers:
    - the **Warrior command menu**, `WarCommandDisplay_Update` (`0x001a7e48`, the load at `0x001a8028`): while the menu
      is open and no command has been issued, a set byte replaces the highlighted command's name with entry 6 of the
      command-text table (`0x006007d8`), the **out-of-range** text from `GSTRING.COMMAND` 6
      ([The Warrior command menu](hud.md#warrior-command-menu)). That text's 2.5 s `<DISPLAYTIME>` is in the script's
      text, not set by the code (read from `config_strings_en.lua`). Confirmed (runtime), PCSX2 2.9.94, `level80`:
      with the byte held at 1 the open menu showed that text instead of the command's name.
    - the **mission-failure check**, `GameState_CheckGameOver` (`0x004197a8`, the load at `0x0041996c`): when player 1
      is cuffed (not knocked out) or holds a flash (item 1) and others could still help, a set byte fails the mission
      at once instead of waiting for a member to reach him. A knocked-out player 1 without a flash fails at once
      whatever the byte ([The mission failing](combat-reactions.md#defeat), case 5).
- **No other cue.** Besides the chief's line (unforced, not quiet) and the menu's text, nothing plays or shows: the
  check calls no sound or HUD function, and neither reader plays a sound. The refused command itself is not shown as
  refused: the menu closes after an issue and its text is updated only before one, so the out-of-range text is seen
  the next time the menu opens while the crew is still out of range (inferred from the order of the updates;
  confirmed (code) that the text is set only before an issue).

The command tactics, by command (constructor, vtable, type id; confirmed (code)):

| Command | Constructor | Vtable | Type | Members get |
| --- | --- | --- | --- | --- |
| 0 follow | `WarriorFollowTactic_Create` `0x00310e00` (init `0x00310e90`, distance 9.0) | `0x005438c0` | `0x12` | `FollowPlayer` ([below](#warrior-follow)) |
| 1 attack | `WarriorAttackTactic_Create` `0x00320530` (init `0x003205b0`, member goals `0x00320618`) | `0x00544160` | `0x01` | [`FollowAndAttack`](ai-goals.md#goal-follow-and-attack) (`0x34`, `0x002bbdc0`) on the chief; dogs (class 221) `AvoidEnemies` (4 m, 8, 8) ([code](ai-code.md#t2-warrior-attack)) |
| 2 defend | `Tactic_Defend` round the chief (given 2.25) | `0x00543800` | `0x02` | `FollowAndDefend` ([table](#tactic-kinds)) |
| 3 hold | `WarriorHoldTactic_Create` `0x00313400` (init `0x00313490`, given 1.5), or `WarriorHoldTactic2_Create` `0x003128a0` (init `0x003128f8`) | `0x005439e0` / `0x00543980` | `0x03` / `0x13` | type 3: [`HoldPosition`](ai-goals.md#goal-hold-position) at his own spot, radius 1.5 m, and a turn outward; type `0x13`: [`Hide`](ai-goals.md#goal-hide) (`0x58`) at a wall point near the nearest hiding flag ([code](ai-code.md#t1-hold), [hide](ai-code.md#t1-hide)) |
| 4 scatter | `WarriorScatterTactic_Create` `0x00319570` (init `0x00319600`, given 75.0) | `0x00543d40` | `0x25` | [`Scatter`](ai-goals.md#goal-scatter) (`0x37`) to one of up to six hiding flags within 75 m ([code](ai-code.md#t2-warrior-scatter)) |
| 5 steal / wreck | `WarriorStealTactic_Create` `0x00320b60` (init `0x00320be0`) | `0x005441c0` | `0x26` | [`WarriorVandalSteal`](ai-goals.md#goal-warrior-vandal-steal) (`0x83`); dogs `AvoidEnemies` (7, 12, 12) ([code](ai-code.md#t2-warrior-steal)) |

Confirmed (runtime), PCSX2 2.9.129, level 99's street with the level's Warrior Ash and one Warrior spawned into the
crew ([PCSX2 recipes](../guides/pcsx2-recipes.md#crew-command)): `IssueWarriorCommand(c, true)` for each command left
the gang's tactic (gang `+0x40`) with exactly the vtable in the table, and each member's goal stack held the type the
table names: follow 50 (`FollowPlayer`, a member just added also 51), attack 52, defend 53, hold 54, scatter 55, wreck
131. The dispatcher (`0x0041c4e0`) keeps two bytes per player: game state `+0x41a` + player is the command in force
(`GameState_StoreWarriorCommand`, stored when the tactic starts) and `+0x41c` + player the last one asked for. Confirmed
(code) at `0x0041c4e0`: it returns before storing `+0x41c` when the command's enabled byte (`+0x41e` + player × 7 +
command) is clear, and stores it before the range check of an unforced command, so a command refused for range shows
only in `+0x41c` and a disabled one in neither. Confirmed (runtime): the street's state had all seven enabled bytes
clear; attack asked without `WCEnableCommand` left the tactic, both bytes and the goals unchanged, and the same call
after it gave attack. Automatic switching ([below](#warrior-auto-commands)), confirmed (runtime): with
`WCEnableAutomaticSwitching(true)` and nobody to fight, attack gave way to follow in six runs of seven, seen within 1 s
of the recipe returning (polled every 0.25 s; the 1.5 s delay is from the code; the wall times logged include the
tools' start-up); the first run, right after the state loaded, kept attack for 19 s (not explained). With
`WCEnableAutomaticSwitching(false)` attack held for 15 s in each of three runs.

The names follow, attack, hold, scatter and wreck are read from the lines (inferred). The follow, second hold,
scatter and steal tactics are of type `0x12` or above, so they own their members' goals (`+0x3c`, [Tactics](#tactics)).

What each command tactic does, confirmed (code) at the addresses on [the code index](ai-code.md#tactics-code). Every
one gives each living member that is not a player and not the chief a goal at Start, after flushing him. In a
two-player game a Warrior of player 1's gang more than 10 m from player 1 and nearer the other player gets
`FollowAndDefend` on that player instead. Once the chief's line ends, a random free member answers (`0x003069f0`):
`attack_resp` (132), `scatter_resp` (159), `vandal_resp` (140) or, at a store, `steal_resp` (141); the hold answers
with line `0x85`. None of their Process functions ever ends the tactic (they return 0); a new command replaces it.

- <span id="warrior-attack"></span>**Attack** (`0x01`): `FollowAndAttack` (`0x34`) on the chief, whose Process fights
  the best enemy within 60 m of the chief ([its row](ai-goals.md#goal-follow-and-attack)). Event 19 gives all goals
  again; 22 with `+8` = 1 gives that member his goal again.
- <span id="warrior-hold"></span>**Hold** (`0x03`): `HoldTactic_GiveMemberGoal` (`0x00313718`) pops each member to his
  goal base and pushes HoldPosition (`0x36`) at his own position with radius 1.5 m (the tactic's `+0x20`);
  `HoldTactic_GiveGoals` (`0x00313508`) then queues a turn to a heading spread 360° / (members − 1) from the chief's,
  after 0-1 s with enemies about, else 2-4 s. HoldPosition's Process (`0x002be818`): with no enemy, a 30 % fidget every
  3 s inside the radius, a walk back outside it; an enemy not targeting him: turn to him; one targeting him: a fight
  goal (4000 ms) when in sight and reach from inside the radius, else turn and shuffle. Events 17 (`+4` = 0), 19 and 22
  (`+8` = 1) hold again; 20 (violence) makes a free member look at the fighter for 3 s and taunt at 10 %.
- <span id="warrior-hide"></span>**Hide** (`0x13`, the hold for a gang whose brain `+0x2d5` is set): from the hiding
  flag (activity `0x21`) nearest the chief, ten 10 m rays 15° apart find wall points; each member gets Hide (`0x58`) at
  a free wall point at least 1 m from the chief. Events 1 and 16 (a player attacked while hidden in shadow): when no
  free member is left, the attack command is dispatched for the chief; another gang's tactic stops.
- <span id="warrior-scatter"></span>**Scatter** (`0x25`): up to six hiding flags (activity `0x21`) within 75 m of the
  chief and at least 10 m from him, reachable, nearest first; members get Scatter (`0x37`) to them in turn, at a random
  navigable point 1-1.5 m from the flag (no flags: Scatter with no flag). Events 1 and 16 on a member at his flag: he
  moves on to the farthest flag with no player within 5 m, or with none fights (Melee) for 10 s.
- <span id="warrior-steal"></span>**Steal / wreck** (`0x26`): every 2 s the store flag (activity `0xe`) within 12 m of
  the chief and reachable is picked again; its zone's objects are clustered (1 m). Members get WarriorVandalSteal
  (`0x83`), which steals from the zone's clusters (an object is claimed for 5 s when no enemy stands within 1.5 m of it)
  or, with no store, wrecks what is near.

## The chief's line and the crew's answer {#warrior-command-lines}

Every order the player gives from the menu is heard twice: the chief says the order, then one Warrior answers once
the chief's line has ended. Confirmed (code) at `0x0041c4e0`, `0x0041cc40`, `0x003069f0`, `0x003208e8`,
`0x003209d0`, `0x00319f70`, `0x00310768`:

- **The order** (`WarChief_SayCommand(state, chief, quiet, line)`): by the player's last command (`+0x41a`),
  `Human_SayCommand(1.5, chief, line, no callback, interrupt, 0, no target, 1)`: 0 → 2 `follow` (86 `follow_hide`
  while hidden in shadow), 1 → 1 `attack`, 2 → 3 `defend`, 3 → 4 `hold` (70 `holdhide` while hidden), 4 → 158
  `scatter`, 5 → 5 `steal` or 6 `vandal` (step 4 above), 6 → nothing. Lines 1-6, 18, 70, 86 and 158 are the ones
  that free a stream channel first ([Sound: speech](sound.md#speech)). With `quiet` set (a forced dispatch, or the
  chief already speaking) nothing is said. Game state `+0x416` + player is then set to 1 when no line is playing
  (quiet, or the line found no sound), 0 when it plays.
- **The gesture** (unforced only, step 3): attack plays 676 `cmd_wreckemall_combat` in the fight stance or 680
  `cmd_wreckemall_neutral` out of it; defend 675 `cmd_watchmyback_combat` or 679 `cmd_watchmyback_neutral`
  (`Human_PlayActionClip(chief, clip, 2.0, 0)`); not while the chief has a held flag of `0x2fefefff`, a state flag of
  `0x7bf9e9f7ff0` or an object of kind 4 or 6 in hand. Follow and hold have only `missing_anim_filler` clips (677,
  678, 681, 682) and play none.
- **The answer** (`Tactic_SayAckLine(tactic, line)`, `0x003069f0`): each command tactic's Start notes whether the
  chief's line is playing (the chief is a player, `+0x416` + player is 0 and his speech handle `+0x178` resolves).
  Only then, on the first Process after that line has ended, one member answers; with no line, the answer is
  skipped. The member: with at least two living members, over the 16 slots, each non-player member other than the
  leader who is not busy (`Human_IsBusy`) and whose actions are not blocked; the first such is taken and each later
  one replaces him at 50 %. `Human_SayCommand(1.0, member, line, no callback, no interrupt, 1, no target, 1)`.
  Lines: follow 131 `follow_resp`, attack 132 `attack_resp`, defend 130 `defend_resp`, hold 133 `hold_resp`, scatter
  159 `scatter_resp`, steal 141 `steal_resp` at a store, else 140 `vandal_resp`.

So the automatic commands ([below](#warrior-auto-commands)), the scripts' `WCIssueCommand(…, true)` and the level
start are silent: only the menu (and an unforced `IssueWarriorCommand`) makes the chief and the crew speak.

**Hiding in shadow** (`Player_UpdateHideOrder`, `0x0023e7e0`, and `Human_SetHiddenState`, `0x0023e6e8`; confirmed
(code)): while player 1 (`*0x0051489c + 0x228`) is in shadow (human `+0x1ac` is 0.0), not busy, his gang able to
help, his last command neither hold nor scatter, and no member under attack or his command follow, a timer starts:
500 ms, or under follow the time the nearest hiding member (brain `+0x2d5` set) needs to run to him at gait 4, plus
500 ms. When it passes, the last command is kept in `0x0051480c` and **hold (3) is dispatched forced**; with his
brain's `+0x2d5` set that is the [hide tactic](#warrior-hide) (`0x13`). Leaving the shadow re-dispatches the kept
command, forced, when the gang's tactic is still the hide. Both are silent.

## The default command: follow {#warrior-follow}

**Follow is the default, and the game issues it itself.** Confirmed (code) at the addresses cited:

- **Level start.** `InitLevel`'s game-state reset (`0x00418c68`) sets, for both players, the last command (`+0x41a`)
  to 0, all seven commands enabled (`+0x41e`), the menu unlocked (`+0x42e`), and the automatic commands on (`+0x431` =
  1, [below](#warrior-auto-commands)). `Human_MakePlayer` (`0x00229c40`) makes the new player the **war chief**
  (`+0x3ac` = 1) unless another player of his gang already is, makes him his gang's leader (gang `+0x44`), and then
  **dispatches command 0, forced** (`0x0022a1a8`). So a crew starts every level under the follow tactic as soon as
  its chief is made a player.
- **After a scene.** `SceneTask_End` (`0x0039f450`) pops each bound human's scene goal when it is of type `0x27`-`0x2a`
  (`JoinCinematic` is `0x29`), and, when one of the roles was a player (or the first player's brain `+0x2e4` is set),
  unlocks that player's menu and dispatches **command 0, forced**; `SceneTask_Abort` (`0x0039ec60`) does the same for
  the first player. So the crew leaves `GoalJoinCinematic` and goes straight back to following.
- `Human_SetWarChief` (`0x002398b0`) re-issues the player's last command (`+0x41a`) to the new chief's gang.

A forced dispatch still passes the checks of step 1 above (a locked command system, `WCLockCommands`, or a disabled
command gives nothing) and always rebuilds the tactic.

**The follow tactic** (fields after the base: `+0x20` the follow distance, 9 m; `+0x24` the next formation reshuffle;
`+0x28` the next banter check; `+0x2c` the idle timer; `+0x30` "the leader is a player"; `+0x31` started):

- **Start** (`0x003111a8`) gives the members their goals (`WarriorFollowTactic_GiveGoals` `0x00310f38`): the chief's
  formation takes slot set 9 (`0x00295dd8`); every member that is not the chief, not a player (human `+0x1b0` = −1) and
  not down is popped to his goal base, marked (`Brain_PopToGoalBase`, `Brain_MarkGoalBase`) and gets
  **`GoalFollowPlayer(9 m, chief, mode 3)`** (`0x002de380`, type `0x32`, vtable
  `0x00541d70`). In a two-player game a Warrior of the first player's gang more than 10 m from him whose nearest player
  is the other one gets `FollowAndDefend` (`0x35`, `0x002bca20`) on that player instead. "Not down" is
  `Human_IsDownOrDead` (`0x00227e60`) returning 0: the human has its record (`+0xd4`) and none of the state bits
  `0x180050000` (`0x100000000` dead, `0x80000000` dying, `0x40000` knocked out, `0x10000` tackled;
  [Combat](combat/index.md#state-flags)). It does not read the brain's `+0x09` (`BrDead`), the health, or knocked down
  (`0x80000`): a member made dead to the AI still gets the follow goal. Confirmed (code). The binding `HuIsAlive` is the
  same test. Start also fills the gang's anim group 604 with ten idle clips (`0x00169468`; End `0x00311230` undoes it).
- **Process** (`0x00311638`): done (1) when the gang has no leader; every 8 s the chief's slot set is re-picked
  (`0x00295db0`, one of two); every 1 s (0.25 s while the chief holds something) a banter check: when no scene plays,
  no member has an enemy and every member has been idle for 25 s, one member says an idle line and looks at the
  chief for 4 s. Otherwise it returns 0, so the tactic never ends by itself.
- **Events** (`0x00311928`): 19 for a member (not the chief) with a goal: flushed and given `FollowPlayer` mode 1;
  11 from a Warrior: the gang's warning line (`0x001691a0`, by how many are left and whether the spotter is beyond
  12 m); 20 (violence nearby): consumed, and a free member near a busy offender looks at him for 3 s, with a 10 %
  taunt (`0x00311250`); 22 with argument 1: the goals are given again.
- Repeating the follow command (unforced, under type `0x12`) calls `0x003114e0`: each member holding `FollowPlayer`
  without `FollowFormation` (`0x33`) also gets `FollowFormation(0.75 m, chief, 1)`.

**Adding a member who is already in the gang** (`GangAddMember` → `Gang_AddMember`, `0x00166308`; confirmed (code) at
the cited addresses). There is no early return:

1. A human whose brain `+0x20c` names any gang, the same one included, first has his actions, target, counted enemies
   and brain `+0x164` list cleared. He then leaves through `Gang_RemoveMember(old, h, 1)` (`0x001664d8`):
   - a spawned human (brain `+0x210` ≥ 0) takes his spawner's alive count (`+0x69c` of the spawner record) and the
     game state's `+0x432` down by one and gets `+0x210` = −1, unless the spawner is in state 4, 6, 9 or 10;
   - the leader is cleared when he was it;
   - under a tactic (gang `+0x40`) his brain is popped to its goal base;
   - the gang gets event `0x16` with `+4` = the members left and `+8` = **0**.
2. He is put back: brain `+0x20c`, his attack weights, god mode under an invincible gang, the leader slot for a
   player war chief (human `+0x3ac` = 1).
3. Event `0x16` follows with `+4` = the count before + 1 and `+8` = **1**.

`Gang_OnEvent` (`0x00164c20`) hands an event first to a script handler the gang registered for its type (consumed
when that returns 1), then to the running tactic (vtable `+0x4c`) while the gang is not empty, or for a removal. The
follow tactic answers 22 with `+8` = 1 by `WarriorFollowTactic_GiveGoals` for **every** non-player member who is
not down and not the leader, not only the one added. Each is popped to his goal base and marked; nothing requires an
empty stack. That is the only way back: `Process` gives no goals, and `GangBrFlush` (`0x0016ba18`) clears the goals
and actions but leaves the tactic at gang `+0x40`.

So `level80`'s `EndStartCam` works in the original by its own `GangAddMember` calls:
`GangBrFlush(0)`, `GangBrDead(0, nil)`, then `GangAddMember(0, −1, h)` for the player and both Warriors. Each add
re-runs `GiveGoals`. The player's add also clears and resets the leader before the Warriors' adds; the Warriors' own
adds give everyone the follow again.
**`GoalFollowPlayer`'s Process** (`0x002de7c0`): done (2) when the leader is gone. The init (`0x002de380`) stores
the distance **squared** (`+0x14`; 81 for the tactic's 9 m). Each update, confirmed (code):

1. While brain `+0x284` (the last move's failure) is set, a counter (`+0x24`) counts up; at 31 or more both are
   cleared. So after a failed walk the follower waits about a second before it tries again.
2. **Stand or follow.** It **stands** (no new goal) when all of these hold: the leader is within the distance
   (9 m; 10 m in fight mode), measured in **3D** between the two humans' positions (`Vec_DistSq` on the position
   table `0x00714b00`, so a leader 2.2 m up counts his height), its own brain `+0x2d5` equals the leader's (both set
   or both clear), and it can walk straight to the leader (`Human_CanWalkStraightToHuman` `0x002221e0` on the
   leader's position: the trains' test, then the walkable line from the follower's polygon, mask 0). Otherwise, while
   the counter is 0, it pushes **`FollowFormation(0.75 m, leader, turn-with-leader 1, no time limit)`**
   (`0x002dfba8`) and returns; that goal runs on top until it ends ([below](#warrior-follow)). The test runs only
   while `FollowPlayer` is the top goal: while `FollowFormation` runs above it, nothing stands the follower down
   early.
3. With actions queued it returns. Otherwise the fight mode, or the idle behaviour below.

**A move left over from `FollowFormation`** (confirmed (code) at the cited addresses). Neither `FollowFormation`'s
End (`0x002dfe68`, which restores the edge mask `+0xf4`, the Warrior's flags and the start boost) nor the pop
(`Brain_PopGoal` `0x0028d7d8`: End, free, then `Goal_Resume` on the new top) clears the actions, so a move still
running when `FollowFormation` returns 2 keeps running. Meanwhile `FollowPlayer` does **nothing at all**: `Goal_Resume`
(`0x0029ee30`) only marks it "to resume" while the action count (brain `+0x2e`) is above 0, and `Goal_Process`
(`0x0029eed8`) then returns 0 without calling its Process, every update. Only when the move has ended does
`FollowPlayer`'s Resume (`0x002de540`: rejoin the formation, leave the fight stance, the next turn in 1-2 s and the
next fidget in 2-3 s) run, and its Process (the stand test above) from the update after. So there is no stand test,
no new `FollowFormation` and no turn while a left-over move runs. (Pushing a goal, by contrast, clears the actions:
`Brain_PushGoal` `0x0028d758`.)

So a Warrior stands still anywhere within 9 m of the chief as long as a straight walkable line joins them, and only
walks when the chief gets farther, turns a corner, or crosses a fence.

**Fight mode** (`FollowPlayerGoal_IsFightMode` `0x002de650`, given the goal, the leader and the leader's target,
brain `+0x124`; only a follower whose brain is of type 3, a Warrior, can be in it). Confirmed (code) at
`0x002de650`; the tests in this order, the first that decides wins:

1. The leader has a target: when the target's brain is not of type 3 (not a Warrior), **on**. When it is a Warrior,
   on when the leader is a player whose brain `+0x2e4` is set (a Warrior hitting back at a chief who hit him,
   `WarriorBrain_OnHitByChief` `0x00306190`).
2. **Off** when any of the 16 attack slots on the follower (brain `+0x1a4`) is held by a player.
3. **On** when any player's brain `+0x2e4` is set (the players at game state `+0x224` / `+0x228`).
4. **On** when a member of its gang has attackers (`Gang_AnyMemberHasAttackers` `0x00169658`) and its own brain
   `+0x152` is not 0, else off. `+0x152` counts the brains that have put this one in their enemy lists:
   `Brain_CountHostile` (`0x0028ef20`) raises it, called by `Brain_AddEnemy` (`0x0028d538`, unless a player adds a
   player) and by [the enemy scan](ai-fights.md#enemy-scan) (`Brain_ScanEnemies` `0x0028b358`, its call at `0x0028b9e8`);
   `Brain_UncountHostile` (`0x0028ef30`) lowers it, called by the enemy scan (its call at `0x0028b7d0`),
   `Brain_UncountEnemy` (`0x0028d6a0`) and
   `Brain_UncountAllEnemies` (`0x0028ef48`). A player's `+0x152` is raised and lowered the same way and also
   recounted every 300 updates, which overwrites it ([The player](ai-brain-types.md#think-player)).

So the leader's target alone turns it on (test 1): an AI follower is in fight mode whenever his player chief has
locked on to anyone but a Warrior, whether or not anyone attacks.

**In fight mode** (confirmed (code) at `0x002de7c0`): the stand distance is 10 m (squared 100) instead of the goal's 9 m,
the field of view (brain `+0x12c`) becomes 2π, and with no actions queued:

1. **The target**: his own target when it still passes `Brain_ValidateEnemy` (`0x0028d358`); else the human of his
   **own enemy list** (brain `+0x164`) nearest **to him**, if that human is up and within **20 m of the leader**;
   else the leader's target when it is up: it is used for the steps below even when friendly, and only when it is
   not friendly (`Brain_IsFriendly` `0x00290230`) does it also become his own target (`0x0028cfe0`; `0x002deae0` to
   `0x002deb08`). With no leader's target, or a down one, none: the target is cleared and it goes on as out of fight
   mode below, from leaving the stance (the field of view stays 2π).
2. It enters the fight stance (`0x0022fe80`). The range is the far melee range (brain `+0x140`), or the near one
   (`+0x13c`) when the target's actions are blocked or he is busy.
3. **Beyond the range** it only **turns** to the target (`TurnToPointAction` `0x002fe000`, at the human's max turn).
   **Within the range** it queues `MoveToHumanAction` (`MoveMelee`, [Move-to-human](ai-moving.md#move-melee)) on him
   with the band range + 0.5 m to range + 1.0 m and a 3 s limit: since he is already inside the band's near edge, it
   **backs off**, walking (gait 2), to keep that distance.
4. It never attacks and never starts a move toward the target from fight mode: the band move backs off (and
   `MoveMelee` may side-step, or close in at gait 4 if the target gets beyond the band's far edge while it runs,
   [Move-to-human](ai-moving.md#move-melee) steps 3-4). A Warrior fights only under the attack command
   ([Automatic commands](#warrior-auto-commands)) or when hit (below).

Confirmed (runtime) at `level34` checkpoint 5 ([Scripts](scripting.md#level34-fixes) item 16): Ash and Snow stood
1.14-1.64 m from the standing player, turning to Furies that were 20.5 m (Snow) and 7.3 m (Ash) off when taken as
targets, never walking toward them, and backed off at gait 2 (1.92 m/s) once a Fury came within 3.6 m (Snow) and
4.1 m (Ash). (An earlier version of this page had the turn and the move the other way round, and the target as the
nearest attacker within 20 m of the follower.)

Out of fight mode the goal sets the field of view back to 110° (1.9199 rad) and clears the target, then
leaves the stance, may fetch a pickable object (the roll of [the Warriors' pick-ups](#warrior-pickups), every 30
updates), every 2-4 s turns as its mode says, and every 3-6 s, unless its own or the leader's brain `+0x2d5` is set,
at 30 % (Warriors only while standing) queues a fidget (`PlayFidget`, `0x002f9f48`). The **modes** (goal `+0x18`,
the init's last argument; confirmed (code) at `0x002de7c0`):

| Mode | Every 2-4 s | Used by |
| --- | --- | --- |
| 1 | turn to face the way the leader faces (a point one leader-facing ahead of himself) | event 19 of the follow tactic |
| 2 | turn to face the opposite way (watching the rear) | |
| 3 | turn toward the leader when more than 60° off, slowly (turn boost −8) | the follow tactic |
| 4 | look at the leader's target (`LookAtAction`) when he has one | |
| other | nothing | |

**`FollowFormation`** (type `0x33`, `FollowFormationGoal_Init` `0x002dfba8`, vtable `0x00541c50`; arguments: arrival
distance (0.75 m from the follow code), leader, a turn-with-leader flag, time limit). Confirmed (code):

- **Start** (`0x002dfc30`): saves human `+0xf4` and the start boost; a Warrior also gets human flags `0x100`, `0x200`
  (restored at End `0x002dfe68`). Then **Resume** (`0x002dfcd0`): joins the leader's formation, takes the follow
  point; when the leader runs (gait above 3) and the flag is set, turns to the leader's heading. On levels `0xe`,
  `0x33`, `0x34` and `0x52` a Warrior's `+0xf4` gets bit `0x100`.
- **Process** (`0x002e0088`):
    1. Done (2) when the leader is invalid, the last move failed (brain `+0x284`), or he is no longer a follower of
       the leader's formation. Wait while the front action is of kind 6. Out of the fight stance when the leader is.
    2. Every 6 updates (brain `+0x34` a multiple of 6), **only while it has no slot**, the follow point (the human
       ahead, step 4) is written into the goal (`+0x10`) and into the running move (`Ai_SetRunningMoveTarget`
       `0x0029f790`: when the front action is a move, `MoveAction_SetTarget`, [below](#follow-retarget)). With a slot
       this step does nothing.
    3. Every *n* updates (*n* a random 2-7 drawn at init) the leader's gait is noted; "the leader just stopped" = it
       was above 3 and is now below 3. Unless he just stopped, nothing more while a check time (now + 1000 ms) has
       not passed and actions are queued.
    4. The follow point, written to the goal's `+0x10` and refreshed only here (once a second, or when the leader
       just stopped): `Formation_GetFollowPoint` (`0x00295e38`) finds the follower's entry (`Formation_FollowerIndex`
       `0x002962f0`, the entry whose handle is his, −1 none) and reads its slot (`Formation_GetSlotPoint`
       `0x00296358`, entry `+0x04`; it replans first when the formation's "replan" byte `+0x274` is 1). With a slot
       (≥ 0) the point is that slot's world point in the current set (`Formation_GetMemberPoint` `0x00296410`: the
       three `s16` at slot `+0x06` / `+0x08` / `+0x0a` in 1/16 m, *w* = 1) and it returns 1; radius = the arrival
       distance (0.75 m). With no slot (−1) the point is the **position of the human it queues behind**
       (`Formation_GetAhead` `0x002963b0`: replan first when `+0x274` is 1, then the handle at entry `+0x08`, and
       the leader (`+0x244`) when that handle no longer resolves) and it returns 0; radius **2.5 m**, and from here
       on that human stands in for the leader in steps 6 and 7 (his speed and his `+0x2d5`). Confirmed (code) at the
       addresses cited. A slot is given only by a plan, and only a usable slot is given (below), so "a slot" here
       means a usable one.
    5. **Can it get there** (`Nav_WalkDistanceTo` `0x00247a50`; Ghidra's old name `Nav_StraightDistance` was
       wrong): the straight distance when a walkable line from the human's nav point reaches the point; otherwise,
       when both ends have an area and both areas are on the graph (or it is one area), the **length of a route**
       planned with the brain's mask (`Route_Find` between the two ends' nearest reachable nodes, then
       `Path_LengthFrom`, the route freed at once). It fails only when the point has no area or no route exists.
       On failure a counter (`+0x2a`) is set to 1 and it waits; each update the counter counts up, and on every
       15th count, when the human has no nav polygon or no node reachable near the point, a breakable object within
       1.5 m is smashed (`VandalizeItem` pushed); at 30 it tests again. A success clears the counter.
    6. The leader (or the human ahead) standing (his speed gives gait 0) and the walk distance below the radius:
       done (2). With the leader standing, the move's option bit is 0; otherwise 1.
    7. Beyond 0.2 m (walk distance): with no move at the front of the queue, a new move action to the point
       (`MoveAction_InitEx` `0x002fbae0`, which **copies** the point into the action's `+0x10`; radius 0.1 m at a
       slot, else 2.5 m; the speed below as `+0x40`, never below the walk). With a move already running, only its
       speed (`+0x40`, `MoveAction_SetSpeed` `0x002fbbe0`) and radius (`+0x2c`) are written here, **not its point**.
       The move plans its own route ([Path planning](ai-moving.md#path-planning)), so a follower crosses fences, climbs
       and drops by the route's links. A new move by a follower who has attackers (brain `+0x1a4`) and a clear `+0x2d5`
       also turns him to face the point and raises his start boost by one.

    The distance `d` used for the speed and the radius tests is the **walk distance** from step 5 (the route's
    length when the point is round a corner or over a fence), not the straight one.

- **The speed** (`FollowFormation_PickSpeed` `0x002dfef8`), with `d` the distance, `v` the leader's speed capped at
  1.25 × the sprint speed: within the arrival distance, `v`. Beyond it, `f` = 0.1 × (`d` − arrival distance); when
  `v` < 2 m/s, `f` += 4 × `f` × max(`v` − walk speed, 0); `f` += 1 when `v` > 2 m/s, the leader's `+0x2d5` is set, or
  the follower has attackers. Gait `g` = run (4) beyond 8 m or with the leader's `+0x2d5`, jog (3) beyond 4 m, else
  walk (2). Speed = max(speed of `g`, `v`) × `f`, clamped to [walk speed, 1.25 × sprint speed]. So a follower far
  behind runs, near his slot matches the leader, and slows to a walk on arrival.
- **What limits a follower's speed and destination** (confirmed (code) at the cited addresses unless marked). The
  speed's only caps are the clamp above (1.25 × the follower's own sprint clip speed, 12.81 m/s for Vermin and
  Rembrandt) and the control's 25 m/s ([Characters](characters.md#ai-locomotion)); the move action may lower it
  further at corners ([The corner speed](ai-moving.md#corner-speed)) and a climb leg runs at gait 4. Nothing caps it for
  a straight move, and the follower really moves that fast: confirmed (runtime), Vermin's and Rembrandt's speed
  `+0x1ac` reached **12.807 m/s** and held it on a 75 m run, on a route and on straight moves alike. What keeps a
  follower on the ground is the destination and the line, not the speed:
    - the slot point is put on the ground when the formation is planned (a ray from 1.9 m above, 5.9 m long, so to
      4.0 m below; the point goes **0.2 m above** the hit; with no hit it keeps the plan point's height) and is
      usable only when the leader has a walkable line to it ([Formations](#formations));
    - a follower with no usable slot aims at the human ahead of him, who stands on the ground;
    - `Nav_WalkDistanceTo` fails when the point has no area (none within 3.6 m below it,
      [Path planning](ai-moving.md#path-planning)) or no route reaches it, and then no move is made (step 5);
    - the move goes straight only when the walkable line reaches the point from where the follower stands, else by
      a route ([Path planning](ai-moving.md#path-planning)), and every 31 updates its current aim must still pass the walkable
      line or the move ends and the goal plans again ([The move action](ai-moving.md#move-action) step 5);
    - the running move's point follows every plan ([above](#follow-retarget)), so it is at most one plan old.

### Where a running move's point comes from {#follow-retarget}

Confirmed (code) at the addresses cited; confirmed (runtime) below. A move keeps the point it was made with until
one of these writes it:

1. **Every formation plan.** At the end of `Formation_Plan` (`0x002956d0`), `Formation_RetargetFollowerMoves`
   (`0x00295250`; Ghidra's old name `Formation_SetFollowerSpeeds` was wrong) takes each follower whose **top goal**
   is `FollowFormation` (`0x33`), or type `0x69` or `0x73`, and whose front action is a move (kind 1): the move
   gets the follower's new follow point (`Formation_GetFollowPoint`, after the slots were reassigned) and its
   radius becomes **0.1 m** with a slot or **1.0 m** without (step 7 puts 2.5 m back on its next pass). A plan
   runs when the leader has just stopped (his speed `+0x1ac` falls to 0.005 m/s or below, which sets `+0x274`),
   at the plan time (1000 ms after the last plan at gait 2, else 2000 ms), or once the leader is 1 m (3D) from
   the spot of the last plan. So a follower's destination is never older than one plan: about 1 to 2 s while the
   leader moves, and at once when he stops or moves a metre.
2. **Every 6 updates without a slot** (Process step 2).
3. `MoveAction_SetTarget` (`0x002fbbe8`) ignores a point within 0.0707 m of the old one (distance² ≤ 0.005).
   Otherwise it stores the point; with a route, when the route's last waypoint has a walkable line to the new
   point (`Nav_IsConnected` `0x0024dee8`), the route is kept; else the move is aborted (`MoveAction_Abort`
   `0x002fc560`, which calls `RouteState_Stop` `0x0029aa20` with 1): unless the route state is 1, 2 or 3 (in a
   jump, drop or fence climb), the route is freed and the move is **restarted** (its "started" byte `+0x06` and
   the abort bit cleared, so its Start runs again on the next update and asks for a straight line or a new
   route to the new point); in a jump, drop or fence climb the route is kept and the abort bit stays set, so the
   move ends on its next update (the move's first test is the abort bit; the leg's clips play on) and the goal
   makes a new one.

Runtime, PCSX2 2.9.94, `level80` (the state of the checks below), the crew standing round the player in the
street; the player teleported 20 m away, then, 35 updates later with both followers running at 12.81 m/s on routes,
teleported another 9.7 m: on the **next** update both moves held the new slot points (Vermin's from (−176.88,
181.12) to (−186.12, 178.44)), while the goals' own `+0x10` kept the old ones for another 29 updates until their
once-a-second refresh; at a later plan the two slots swapped and each running move took the other's point at once.
Both arrived, stood and went back to `FollowPlayer`.

**Confirmed (runtime)**, PCSX2 2.9.94, `level80` at the "Let's Go" hint (a state of the scripted-pad and right-stick
patches), Cleon the player, Vermin and Rembrandt on a landing 19 m away and 4.4 m up; the follow command given with R2
and the right stick up, then the player driven by the left stick; one sample per update:

- **The run-in.** Vermin planned a route down the stairs (route state `+0x04` set) and asked for **12.81 m/s**, the
  clamp 1.25 × sprint; his speed climbed through gait 2, 3 and 5. Both stopped 1.8 to 2.4 m from the player.
- **The stairs** (2026-10-20, PCSX2 2.9.129, the same moment from a state of no patches, the order given with R2 and the
  right stick at 0 for 20 frames, one sample per update). The stairs run straight in plan from the landing (−192.9,
  108.3, 5.63) north to the street, with a half landing at z 2.43 for y 114.8 to 118.7; the landing, both flights and a
  patch of pavement at their foot are one path area (polygon 283, 29 vertices, ground byte 8), the street another (95,
  ground 2). Both Warriors ran straight down both flights on the ground (z 5.63 → 2.43 → 1.23 by y 121.7) at 12.81 m/s.
  Each ran to his **slot** from his first move: Vermin's (−192.1, 128.4) lies in area 283, and the straight line to it
  crosses one of 283's own edges, so his move made a one-node route (node 793, the stairs' foot, its last node area
  283); Rembrandt's (−194.6, 128.6) lies in the street, his line passes from 283 into 95 at the stairs' foot and he made
  **no** route. So "a route down the stairs" is Vermin's slot, not the stairs: without a route the straight line already
  follows them. Coney's crew takes the same flights (z 2.43 at y 115 to 118.7, 1.23 by y 121); it differed in going
  first to the leader's position (the replan byte, [Formations](#formations)).
- **Walking** (stick 50 % straight down, the player at 1.63 m/s): both walked at **their own walk speed** (1.65 and
  1.63 m/s), 0.9 to 2.5 m from him: the formula's floor, since *f* is far below 1 so close.
- **Running** (stick 100 %, 7.80 m/s): neither moved while within 9 m. Vermin passed 9 m at update 366; Rembrandt
  started at update 381 (12.2 m) and Vermin at 386 (13.9 m), both asking 12.81 m/s; a turn action of mode 3 was
  queued in between. When the player stopped, the asked speeds fell to 9.48 and 7.62 m/s (gaits 5 and 4), a jog of
  1.94 m/s about 5 m out, then the walk; they stood 1.6 and 2.9 m from him.
- **A climb.** With the player put on a crate stack 2.2 m high (the `0x80` ledge links of nodes 860-865), both
  planned routes over the ledge: Rembrandt's route state went to **4** (a `0x80` climb) 1.6 m from the player and he
  stood on the stack 0.7 m from him; Vermin's route was **held** (route state `+0x16` = 1) while Rembrandt had the
  shared node, then climbed (state 4) on his turn. Two seconds later the formation's replan put Rembrandt's slot on
  the ground: he dropped off (route state **2**) and climbed back up 7 s later.
- **Why they climb: the stand test fails at the crate stack** (2026-10-12, the same state; the follow command given
  with R2 held and the right stick at 100 % up for 15 updates, then the player teleported). With the player on the
  street at (−193.0, 130.4) both Warriors walked up and stood (`FollowPlayer` on top, no actions) 4.5 and 6.3 m from
  him. The player was then put on the crate stack's top at (−190.9, 132.9, 3.4): on the **next** update Rembrandt,
  7.9 m away in 3D (inside 9 m, both `+0x2d5` clear), pushed `FollowFormation` (`0x00541c50` on top), so the walkable
  line from the street to the top failed; Vermin (9.7 m) pushed it one update later. Rembrandt's move planned a route
  (route `+0x04` set) and climbed by a `0x80` link (route state 4); he stood on the top 100 updates later, and Vermin
  climbed after him. Corroboration from the disc's path data (`level80_2`, counted by script, not committed): the top
  is its own area (one polygon, 2.15 × 1.40 m, owning nodes 860-865), and the street's outline leaves the stack out;
  the segment from Rembrandt's spot to the player leaves the street's polygon at *t* = 0.78 and enters the top's at
  *t* = 0.91, 0.98 m apart in plan, far over the 0.02 m the walkable line allows ([Path planning](ai-moving.md#path-planning)),
  so the line is refused whatever the heights.
- **Crossing a fence** (2026-10-12, the same state; the crew standing south of the kind-8 fence pairs of nodes
  396-403 at y ≈ 199.9, the player teleported over it). Both Warriors have human `+0xe0` = 7, so flag `0x2`, fast
  climbers. In every run both pushed `FollowFormation` on the update after the teleport (the fence's hole refuses
  the line), planned routes over the kind-8 pair and climbed (route state 3); no other rule takes them over.
    - Player 5-7 m away (`FollowFormation_PickSpeed` gives the walk, 2.4-2.5 m/s): each walked to the climb's near
      waypoint and climbed **from the walk** (gait 2 when the climb started; the early climb needs a gait above 3);
      the climb held them about 80 updates, then they walked to their slots: about 150 and 210 updates from the
      teleport to standing.
    - Player 14 m away (asked 8.2-8.4 m/s): Vermin ran (gait 4, then 5) and started the climb **from the run**
      2.85 m before the far waypoint (the early climb, `Route_TryClimbNear`), was over it about 18 updates later and
      walked on; Rembrandt was slowed to the walk by the corner speed (his move's slow corner speed `+0x44` = 1.63,
      the walk) before the near waypoint, so he arrived walking and climbed from standing (78 updates). Neither
      ever pressed against the fence for the 31 tries of a failed climb. Stick: none; the player was teleported.

Speculative: the levels whose level-table id is `0xe`, `0x33`, `0x34` or `0x52` (the Warriors' `0x100` mask while
following) are `level14`, `level51`, `level52` and `level82`, if the id is the level's number; all four exist on the
disc (as `level14_2` and so on). Not checked.

## Automatic commands {#warrior-auto-commands}

What turns a following crew into a fighting one is the chief's own brain. `PlayerBrain_Update` (`0x003035d8`) calls
`WarChief_AutoCommand` (`0x00303988`) on every update with the chief's target (brain `+0x124`). For a war chief, while
game state `+0x431` is set (`InitLevel` sets it; `WCEnableAutomaticSwitching(false)` clears it, which only the Rumble
arena scripts call: `brawl`, `kinghill`, `royal`, `survival` and `tagbt`, read from the compiled scripts; the binding
(`0x00374a00`) reads an absent argument as true and nil, the scripts' `false`, as false), it unlocks his menu every 10
updates and then, by his last command (confirmed (code)):

- **0, follow**: when he is mugging (`0x100`), tagging or in a player mode 2 or 3 (record `+0x46`; not traced),
  **defend** at once. Otherwise, when his target is within his far melee range (`+0x140`, 5 m) and is not a Warrior
  hitting back at him (`+0x2e5`), and he has attackers (`+0x1a4`), **attack** after 1.5 s (**defend** while he grabs
  from the rear, `0x80`). Every 20 updates, next to a store flag whose group still has objects, **steal** (5).
- **1, attack** and **2, defend**: back to **follow** 1.5 s after no member of the gang has an enemy (`0x00165a20`)
  and his brain `+0x2d4` is clear (defend also waits until he is free, not cuffed and not mugging).
- **5, steal**: back to **follow** when he is more than 15 m from the store flag.

All of these are forced dispatches. So in the original a crew under follow stands round the chief, turns to face his
enemies, and joins the fight with `FollowAndAttack` 1.5 s after the chief, within 5 m of his target, is attacked.
Being attacked alone is not enough: the attackers count only while the chief has a target of his own in range. Coney
reads the target his fighter keeps (human `+0xc8`, which `Human_SetTarget` `0x00226c70` also gives his brain's
`+0x124`); until 2026-10-20 it stood in his nearest attacker for it, and so attacked for a chief who had none, as at
level80 checkpoint 4, where the Destroyers come for Cleon before the script's attack order (`ai::CrewOrders`).
Under attack, defend or follow, `Brain_PushFightGoal` pushes nothing ([Tactics](#tactics)): the tactic decides.

## Formations and follow slots {#formations}

A leader's **formation** hangs from **human** `+0x1a4` (`0x0021d428`, made on first use); brain `+0x1a4` is the
attack-slot list. A pool of 42 formations of 0x280 at `0x006ceaf0`; `Formations_Update` (`0x00293c68`, step 2 of
`Humans_Update`) runs `0x002956d0` on each in use. Confirmed (code):

| Offset | Meaning |
| --- | --- |
| `+0x000` | 4 slot **sets** × 9 slots × 12 bytes: `s16` x, y, z offset, then `s16` x, y, z world point, in 1/16 m |
| `+0x1b0` | 9 **followers** × {handle, `s32` slot (−1 none), the handle it queues behind} |
| `+0x220` / `+0x230` | the leader's position / forward at the last plan |
| `+0x244` / `+0x248` | the leader / the next plan time |
| `+0x24c` | per set × 9: slot usable |
| `+0x270` / `+0x271` / `+0x272` / `+0x273` | current set / slot count / followers allowed in slots / followers |
| `+0x274` | **replan**: 1 asks for a plan before the next slot is read |

- Init (`0x00294ad8`): 6 slots, 6 allowed, set 0. `BrSetNumFollowSlots(leader, n, n2)` sets `+0x271` = n and `+0x272`
  = n2 (n when −1); `BrSetFollowSlotSet` sets `+0x270`; `BrSetFollowSlot(leader, slot, {x, y}, set)` stores (x, y, 0)
  in 1/16 m in that set. Each replans at once.
- **Joining** (`0x00295f28`) takes a free follower entry (handle, slot −1, queued behind nobody), adds 1 to `+0x273`,
  sets **`+0x274` = 1** and sets the follower's brain `+0x212` = the formation's index; leaving (`0x00296028`) sets it
  to −1 and removes the entry (`Formation_RemoveFollowerAt` `0x00294c98`: `+0x274` = 1, `+0x273` less 1).
  Confirmed (code) at the addresses.
- **The replan byte** `+0x274`: reading a slot point (`Formation_GetSlotPoint` `0x00296358`), whether a slot is
  usable (`Formation_IsSlotUsable` `0x00296138`) or the human a follower queues behind (`Formation_GetAhead`
  `0x002963b0`) first runs `Formation_Plan` when it is 1, so a follower who joins takes his slot on the update he
  joins: `FollowFormation`'s Resume joins and at once reads its follow point, which plans. `Formation_Plan` sets it
  itself when the leader has just stopped (his speed `+0x1ac` at most 0.005 m/s now, at least 0.005 at the last call,
  kept at `+0x240`), and clears it when it plans. Confirmed (code). At runtime (PCSX2 2.9.129, `level80` at "Let's
  Go", follow order given with R2 and the right stick up): Vermin's first move already ran to his slot
  (−192.1, 128.4, 1.4), and Rembrandt's to (−194.6, 128.6, 1.4); Coney, which planned only at the next plan time,
  sent both to the leader's own position for the first 2 s and stopped them 3.4 m short of him.
- **Planning** (`0x002956d0`; nothing while `+0x273` is 0), when `+0x274` is set, at the plan time, or once he is 1 m
  from the planned spot: next plan in 1000 ms at gait 2, else 2000; the plan point is the leader's position, 2 m ahead
  while he runs (`0x00223c10`); the slots' world points are worked out (`0x00294f38`: the offset in 1/16 m, turned by
  the leader's orientation (`0x004dacc0`, so x is to his right and y ahead), added to the plan point and dropped to the
  ground with a ray (`WorldManager_RayCast` `0x0040db88`) that starts 1.9 m above it and runs 5.9 m straight down, so it
  finds ground down to 4.0 m below; on a hit at distance *t* the point becomes the plan point + (*t* − 2.1) down, i.e.
  **0.2 m above the hit**; with no hit it keeps the plan point's height; a slot is usable when the leader has a walkable
  line to its point (`Human_CanWalkStraightTo` `0x002221e0` from the leader), up to `+0x272` of them; **no slot is
  usable at all** while the leader is airborne (state `0x1c00000000`, jumping or falling, `0x00227f90`) or at detail
  level `+0x333` 3 or more: every follower then queues behind the human ahead; followers whose human is gone leave
  (`0x00294e98`); each usable slot takes its nearest unassigned follower; followers left over queue nearest-first behind
  the slotted ones (or the leader); up to 3 passes swap pairs whose paths cross.

**GoalTrackHuman** (vtable `0x00541d10`, constructor `0x002df250`: `+0x10` target, `+0x14` distance, `+0x18` a retry
counter): Start (`0x002df288`) joins the target's formation (`0x0021d428`, `0x00295f28`) and turns the fight stance
on, End leaves it. Process (`0x002df3c0`): no target → done; every 30 updates a 1.5 s head look-at, every 40 the
actions cleared; with no actions, a failed move (brain `+0x284`) counts up the retry counter; the goal point is its
slot's world point (`0x00296358`, `0x00296410`), or **its own position when it has no slot** (so it never walks);
beyond `distance`, once the counter reaches 31 it and `+0x284` are reset, and with the counter at 0 a move action to
the point (gait 2, arrival radius `distance`, option 1, no delay, facing the target); otherwise a turn-to-point when
more than 15° off the target. Confirmed (code); this corrects the binding's "walks to the target
when it has no slot".

## Coney's implementation

The gang and its update are `repo:src/ai/gangs.h`, the spawners `repo:src/ai/spawners.h`, the tactics
`repo:src/ai/tactic.h` and its `tactic_*.h` files with the scripted ones in `repo:src/ai/story_tactics.h`, the
formations `repo:src/ai/formations.h`, the Warrior commands `repo:src/ai/set_command_action.h` and the boss goals
`repo:src/ai/boss_goals.h`. What Coney does differently and what it leaves out is on
[AI humans: Coney's implementation](ai/index.md#in-coney); the sections above say where Coney differs on their subject (the
attack on sight, the follow).

## Open questions

Each open point sits with its subject above (the attack on sight's fix list, the scripted tactics Coney stands in
for, the boss program's unused operations); the reimplementation units still to do are on
[AI reimplementation plan](ai-reimpl.md).
