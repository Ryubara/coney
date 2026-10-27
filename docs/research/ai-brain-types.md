---
kind: topic
system: ai
owns:
  functions: [
    "0x0021c0a8", "0x002912b0", "0x00291960", "0x00292d80", "0x00293640", "0x00293768", "0x002b75b8", "0x002fef40",
    "0x002ffb30", "0x00300678", "0x00302340", "0x00302cd8", "0x00303178", "0x00303260", "0x00303468", "0x003035d8",
    "0x00303e90", "0x00303f10", "0x00304228", "0x00304608", "0x00304fa8", "0x003052f0", "0x00306040", "0x003063b0",
  ]
---
# AI brain types: think and event handlers

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records or, for the older ones, How this was checked.

## Summary

Each kind of AI human reacts to the world in its own way: a cop reports crimes and chases, a gang soldier fights
anyone who hits him and picks up weapons, a civilian calls the police or flees, a dealer sells, a shopkeeper guards
his store, a Warrior follows the player, picks up weapons and offers to swap them. That difference lives in the
brain type's two handlers: the **think**, run one character step in five, which looks round and pushes the type's
own goals, and the **event** handler, which answers what happens to the human (a hit, an attack warning, violence
nearby, a crime, the triangle prompt). This page is those handlers, the event record they read, the help calls that
spread a fight, the shared handler every type falls back on and the weapon swap a Warrior offers.

How it works, in a paragraph: `Brains_RegisterTypeHandlers` fills a think and an event handler per type, and
`Brain_InstallHandlers` gives each brain its type's pair ([Types](ai/index.md#types)). An event reaches a brain
through `Brain_OnEvent`, which runs the perception, the reaction goal and then the type's handler; each type handler
answers the ids it knows and passes the rest to `Brain_DefaultOnEvent`. Violence (event `0x14`) is also how a fight
spreads: a hit human calls for help, and every hearer in range picks a side and joins.

What this page is not: the brain itself, its update and the goal stack are on [AI humans](ai/index.md); the fight a
handler starts on [AI fights](ai-fights.md); how a gang's members share a fight on [Gang fights](gang-fights.md); the
gangs' tactics on [AI gangs](ai-gangs.md).

## Map

| Behaviour | Function | Section |
| --- | --- | --- |
| The event record | `0x0028f928` `Brain_OnEvent` | [The event record](#event-record) |
| A help call | `0x00293640` `Gang_BroadcastHelpCall`, `0x00293768` `AI_AlertNearby` | [Help calls](#help-calls) |
| Taking a side | `0x002912b0` `Brain_PickSideInFight` | [Help calls](#help-calls) |
| Every type's fallback | `0x00292d80` `Brain_DefaultOnEvent` | [The shared event handler](#shared-events) |
| Cops | `0x00300678` `CopBrain_Think`, `0x00302340` `CopBrain_OnEvent` | [Cops](#think-cop) |
| Gang soldiers | `0x00304608` `GangBrain_Think`, `0x00304fa8` `GangBrain_OnEvent` | [Gang soldiers](#think-gang) |
| Violence seen by a soldier or a Warrior | `0x00291960` `Brain_OnViolenceSeen` | [Gang soldiers](#think-gang) |
| Civilians | `0x002fef40` `CivilianBrain_Think`, `0x002ffb30` `CivilianBrain_OnEvent` | [Civilians](#think-civilian) |
| Dealers | `0x00302cd8` `DealerBrain_Think`, `0x00303178` `DealerBrain_OnEvent` | [Dealers](#think-dealer) |
| Shopkeepers | `0x00303f10` `CivlCoDiBrain_Think`, `0x00304228` `CivlCoDiBrain_OnEvent` | [Shopkeepers](#think-shopkeeper) |
| The Warriors | `0x003052f0` `WarriorBrain_Think`, `0x003063b0` `WarriorBrain_OnEvent` | [The Warriors](#think-warrior) |
| The player | `0x003035d8` `PlayerBrain_Update`, `0x00303260` `PlayerBrain_Think`, `0x00303e90` `PlayerBrain_OnEvent` | [The player](#think-player) |
| The weapon swap | `0x00306040` `WarriorBrain_OnPrompt`, `0x00303468` `PlayerBrain_OnPrompt` | [The weapon swap](#warrior-swap) |

The entry points are the two tables the handlers are called through, think `0x00715568` and event `0x007155a0`, one
pair per type, which `Brain_Think` and `Brain_OnEvent` call. Confirmed (code) at `0x0028a360`.

## Data

The handlers read the brain ([the brain](ai/index.md#brain)) and the event record below; the record's fields are under
[The event record](#event-record). The brain counters they keep:

| Field | Meaning | Evidence |
| --- | --- | --- |
| brain `+0x38` | the think counter: "every *n* thinks" below counts it (`Brain_Think` `0x0028f6c0`) | confirmed (code) |
| brain `+0x148` | the pursuit (tackle) meter, kept by the cop, gang and Warrior thinks (`CopBrain_Think` `0x00300678`) | confirmed (code) |
| brain `+0x200` | attack warnings since the last update, raised by the event handlers (`Brain_DefaultOnEvent` `0x00292d80`) | confirmed (code) |
| human `+0x1b2` | the swap prompt string, set by the Warrior think (`WarriorBrain_Think` `0x003052f0`) | confirmed (code) |

## Behaviour

What each brain type does at its think (one character step in five, [The update](ai/index.md#update)) and with each event
([Events](ai/index.md#events)). Confirmed (code) at the addresses cited unless marked; "every *n* thinks" counts the think
counter brain `+0x38`, so it is every 5 × *n* steps. `R` is the sight range (`Brain_GetSightRange` `0x0028bf00`,
[The enemy scan](ai-fights.md#enemy-scan)). Goal ids are on [AI goal types](../references/goal-types.md).

`Human_CanSeeHuman` and its squared near and shadow distances are under [Sight](ai-fights.md#sight).

### The event record {#event-record}

Every handler takes the brain and a record: `+0x00` the human who caused it (the attacker, the aggressor), `+0x04`
the other party (the attacker in event 1, the crime object in event `0x17`), `+0x0c` and `+0x11` flag bytes, `+0x20`
the **event id**, `+0x24` a third object or human (the victim in event `0x14`, the offender in event 7). Field roles
inferred from the readers below.

| Id | Meaning | Who answers it |
| --- | --- | --- |
| 0 | triangle pressed on this human (the prompt) | player, Warrior, dealer (buying), shopkeeper, bum |
| 1 | he was hit (`+0x04` the hitter; `+0x11` = 1 when it counts as an attack) | cop, gang, civilian, dealer, Warrior, shopkeeper, shared |
| 2 | reset: target, actions, goals and enemies cleared | shared (cops ignore it) |
| 6, 7 | an object taken or a theft (offender at `+0x24` for 7; inferred) | cop and civilian (7), shopkeeper (6, 7) |
| 9, `0x11` | (not read by any brain type; cops drop them) | cop |
| 10 | a shopkeeper's customer check (inferred) | shopkeeper; cops drop it |
| `0xb` | a new enemy, sent by [the enemy scan](ai-fights.md#enemy-scan) | shared, Warrior; player and shopkeeper drop it |
| `0xc` | (dropped by the player, the Warriors and cops) | — |
| `0xd` | the goal stack became empty ([Brain_UpdateGoals](ai/index.md#update-goals)) | goals and tactics |
| `0xf` | "use this flag" (a free tag spot nearby; inferred) | sent by the gang think to itself |
| `0x10` | the [attack warning](ai-fights.md#block) | every type |
| `0x14` | violence seen (`+0x00` the aggressor, `+0x24` the victim; `+0x04` 0 for a fight between humans) | cop, gang, civilian, Warrior, shopkeeper; the player drops it |
| `0x15` | he has been put on the ground (a knock-down clip ended, or a throw or grab put him down; `+0x24` himself): his attackers taunt ([Gang fights](gang-fights.md#waiting) rule 5) | shared |
| `0x17` | a crime seen (`+0x04` the crime object, which gives its position) | cop, gang, civilian |
| `0x18` | danger near (a fire, a car; `Brain_OnDangerNear` `0x00291728`) | Warrior |

Hearing: an event reaches a brain only within its hearing radius (`Brain_IsInHearRange` `0x002935d8`: brain `+0x134`,
50 m when the brain is made; `+0x138`, 20 m, for event 20). Confirmed (code) at `0x0028a570` for the defaults.

### Help calls {#help-calls}

Confirmed (code) at the cited addresses unless a line says otherwise.

`Gang_BroadcastHelpCall(range, caller, a, b, c, newEnemy)` (`0x00293640`). Despite the name, a help call is **not**
limited to the gang: it is event `0x14` (20, violence) sent to everyone nearby. Confirmed (code) at `0x00293640`,
`0x00293768` and `0x002935d8`, and the arguments at each of the 16 call sites:

1. **The record**: `+0x00` = a (the aggressor), `+0x04` = b, `+0x08` = c, byte `+0x0c` = newEnemy, `+0x20` = `0x14`,
   `+0x24` = the caller (the victim).
    - Every caller passes b = 0, so a help call always has `+0x04` = 0.
    - c is 0 except where the shared handler's event 1 and the BigBrawler, BigFighter and BigFighterA hit handlers
      pass the hit event's `+0x00` (with a = its `+0x04`, the hitter).
    - newEnemy is 1 only from the shared handler's event `0xb` (below).
2. **Who hears it** (`AI_AlertNearby` `0x00293768`):
    - `Humans_FindAhead` lists up to 60 humans within *range* of the caller's position, in table order, skipping the
      caller and slots without bit 0 in `0x00715390`. Players are included.
    - The filter `Brain_IsInHearRange` then keeps a human only when his squared distance is within his own brain's
      `+0x138` (event 20's radius, 20 m by default). So the reach is min(range, the receiver's `+0x138`).
3. **The ranges at the call sites**:
    - 5 m: `Brain_CallForHelp`.
    - 10 m: the Grabbed and Mounted goals (a civilian held).
    - 15 m: Scout's tactic.
    - 20 m: the shared handler, the Big* goals and the player's mugging.
    - 30 m: the cop's crime report, a civilian's attack warning, the dealer, the shopkeeper and a busy gang member hit
      by a player.
4. **What a hearer does**: the event goes through his human's vtable `+0x44` (`Human_OnEvent`): his script handlers,
   then `Gang_OnEvent`, then `Brain_OnEvent`.
    - A gang with a tactic consumes event 20 there, and some tactics act on it: Attack, Crowd, Scout, and HanginOut or
      Idle with `respond` ([Tactics](ai-gangs.md#tactic-kinds)).
    - Otherwise his type handler takes it: a cop the crime path, a gang member or Warrior `Brain_OnViolenceSeen`
      (its `+0x04` = 0 test always holds for a help call), a civilian his own handler. The player and the
      shopkeeper drop it.
5. **The fight that follows**: `Brain_OnViolenceSeen` picks a side (`Brain_PickSideInFight`) and calls
   `Brain_Fight(side, −1, 0)`. With no HelpRespond goal (`0x1d`) above his base, it also calls `Gang_SendHelper(self,
   side, 0, −1)` (`0x002b75b8`).
    - Despite its name, `Gang_SendHelper` sends nobody: it pushes a **HelpRespond** goal on **the brain it is given**
      (`HelpRespondGoal_Init(goal, brain, side, 0, −1)`). The hearer helps himself.
    - Only a tactic's "answering violence" picks another member, and then calls it for him.
    - **HelpRespond's flag `+0x14`** (`HelpRespondGoal_Init`'s fourth argument) is never 1: its only caller is
      `Gang_SendHelper`, and all seven callers of that pass 0 (`AttackTactic_OnViolence`, `AttackTactic_Process`,
      `Brain_OnViolenceSeen`, `CopBrain_OnEvent`, `HanginOutTactic_OnEvent`, `IdleTactic_OnEvent`,
      `ScoutTactic_OnViolence`). So `HelpRespondGoal_Process` always takes the "ends once the brain lists enemies"
      branch. Confirmed (code).
    - **HelpRespond's Start** (`0x002b76a0`), with the side still a valid target: within 1.1 × the brain's range
      `+0x140` **and** walkable in a straight line (`Human_CanWalkStraightToHuman`) → a turn to him and a 3 s head look,
      nothing more: the look is not an action, so the goal ends when the turn does
      ([HelpRespond](ai-goals.md#goal-help-respond)). Within that range but **not** walkable → 60 % (`Random_Int(100)` <
      60) a **LookAround** action first (type 10, `LookAroundAction_Init` `0x002fe248`: clip `0x29e` with the field of
      view widened to 4.71 rad while it plays, [AI code: actions](ai-code.md#actions)), then, as beyond the range, a
      move action to him (gait 2, or 4 beyond 10 m; with an empty enemy list he leaves the fight stance and goes at gait
      4, 5 on level `0x53`). Confirmed (code).

6. **The new-enemy flag** (`+0x0c`) has one reader, the cop's handler (`0x00302340`, at its event `0x14`). There, a
   cop victim gives crime 2 (assault on a cop) only when the side picked is not the caller and `+0x0c` is 0. So when a
   cop broadcasts that he has a new enemy, the cops who hear it do not report an assault on a cop. They still take a
   side and fight.

   No other reader was found in `Brain_PickSideInFight`, `Brain_OnViolenceSeen`, the gang and civilian handlers, or
   the Attack, Crowd and Scout tactics' violence handlers.

**Picking the side** (`Brain_PickSideInFight(brain, aggressor, victim)`, `0x002912b0`; confirmed (code)). The callers
pass the hearer's brain, the event's `+0x00` (the aggressor) and `+0x24` (the victim). It returns the human to fight,
or 0:

1. No aggressor: take the victim's brain target (`+0x124`); none → 0.
2. Three tests, all from the hearer *H*: `Fv` = `Human_IsFriendly(H, victim)`, `Ta` = `Human_IsThreatTo(H,
   aggressor)`, `Fa` = `Human_IsFriendly(H, aggressor)` (all three computed first).
3. `Fv` and not `Fa` and not `Ta` (a friend is hit by someone neutral): unless *H*'s brain is a cop's (type `+0x04` =
   1), the two gangs become enemies (`Brain_MakeGangsEnemies(H's brain, aggressor's brain)`: `Gang_SetEnemyBit` on
   brain `+0x20c` of each), and `Ta` counts as true.
4. `Ta` → the **aggressor**.
5. Otherwise `Fa` and not `Fv` (a friend hit someone *H* does not count as a friend) → the **victim**; else 0.

So a friend's attacker is always chosen once he is (or is made) a threat; a cop sides without making gang enemies,
and only against an aggressor who is already a threat or the victim of a friend. It has no out-value: the "reason
2" of the civilian handler's event `0x14` is that handler's own disturbance reason ([Civilians](#think-civilian)),
not something `Brain_PickSideInFight` returns.

### The shared event handler {#shared-events}

Confirmed (code) at the cited addresses unless a line says otherwise.

`Brain_DefaultOnEvent` (`0x00292d80`) is every type's fallback (each type handler passes the ids it does not take):

1. **1** with `+0x11` set: a [help call](#help-calls) within 20 m (`Gang_BroadcastHelpCall` `0x00293640`) naming the
   hitter.
2. **2**: target cleared, actions, goals and the enemy list cleared.
3. **`0xb`** (a new enemy), unless that enemy is throwing at him: a help call within 20 m flagged "new enemy"; an
   Investigate goal (`0x5e`) on top is popped; when the top is then Chase (`0xc`), its actions are cleared.
4. **`0x10`** (the attack warning):
    1. The near radius is 3 m (9), or 0 when the attacker is hidden in shadow, throwing, and this brain's `+0x2d4` is
       clear. Unless `Human_CanSeeHuman(+0x12c, R, near, 2 m, self, attacker)` and the attacker is not friendly,
       nothing more.
    2. Brain `+0x200` + 1 (the [block](ai-fights.md#block)'s trigger); unless `Human_ReactToThreat` already holds, the
       two gangs
       are made enemies (`Brain_MakeGangsEnemies` `0x00290328`); a help call within 20 m.
    3. By the top goal: StationaryShooter (`0x8d`) with free actions: clear the actions and, with `Goal_TryBlock(goal,
       90)`, command 4 (block). BigFighter / BigFighterA (`0x85`, `0x86`): the goal's own handler (`0x002ea0b8`,
       `0x002ebe20`). Fight (`0xf`): when his target may be countered (`Human_CanCounterTackle` or
       `Human_CanCounterGrab` against it) and `Random_Int(100)` < `Human_CounterChance`, clear the actions and, on an
       empty queue, **command 3** (the counter).
    4. When the attacker has a pad or is a Warrior (type 3): `Brain_TryPatternBlock` (`0x00292158`).
5. **`0x15`**: for each human holding an attack slot on him (brain `+0x1a4`), except classes `0x77`, `0x6a` and
   `0xb2`, whose gang kind's field `0x6d` is not 4, whose top goal is Fight (`0xf`) or BigFighter
   (`0x85`), and who is not a Warrior under a tactic that answers 2 at slot `+0x34`:
    - his own target: a taunt of kind 4 (id 0, or `0xf` under BigFighter; class `0x80` only while it holds `+0x364`);
    - any other: skipped when nearer than the middle of his melee ranges ((near + far) / 2), else a taunt of kind 3,
      id `0x10` or `0x8f` at 50 %.
   The taunter clears his actions, walks off (gait 0), turns to the human (start delay 50-100 ms) and taunts
   (`TauntAction_Init`); a non-target also delays his next attack by twice the taunt clip's length (anim 598 or 599).
   Under the Moe boss tactic (`0xc`), a BigFighter's taunt tells the tactic (`BossMoeTactic_FireAnimDone`).

   The detail, confirmed (code) at `0x00292d80`:
    - The brain handling the event is the **downed man's**; "his own target" is the downed man's target (brain
      `+0x124`), or, when the top of the downed man's stack is a Grounded goal (`0x17`), the target that goal noted
      as he went down (goal `+0x10`, `GroundedGoal_Start` `0x002b4a38`). So the one he was fighting taunts him and
      the rest cheer.
    - The gang field read is `+0x6d` of the taunter's gang kind (`GangConfig_GetField6D`): `CfgGang` v3, the
      downed spacing ([Gangs](../references/gangs.md)). A gang that lets four swing at a downed man (Gang1, the
      Turnbulls, the Huns) does not gloat; it keeps hitting.
    - `Random_Int(100)` is drawn for every candidate that passes the class, gang, goal and Warrior tests, before
      the distance test, and picks the line (under 50 `0x10`, else `0x8f`); the target's kind 4 says no line
      (`0xf` under BigFighter; class `0x80` taunts only with a hat, `+0x364`).
    - The distance is the taunter's to the downed man against the **downed man's** melee ranges, (near + far) / 2
      squared. A downed man driven by a pad gets kind 4 from a BigFighter whatever the distance.
    - The two actions are queued only when clearing left no action (`+0x2e` < 1); the taunt's delay is −1 (a random
      0-500 ms).
    - The put-off is `Brain_SetNextAttackTime` (`0x00290e48`): `+0x1e8` = max(`+0x1e8`, now) + 2 × the clip's
      play time (`Anim_GetPlayTime` `0x00101a00`, the clip the human's lookup gives for 598 or 599, over its
      rate).
    - It returns 1 (handled) whatever happens.

Event `0x15` is sent to a man as he is put on the ground: by `HitReact_OnSpecialEnd` (`0x0026a1f8`, the end of a
knock-down clip) and by `Throw_ClearVictimLink` (`0x0022bdc0`, a throw, grab or mount putting its victim down).
Confirmed (code); the detail is in [Gang fights](gang-fights.md#waiting) rule 5.

### Cops (type 1) {#think-cop}

Confirmed (code) at the cited addresses unless a line says otherwise.

**Think** (`CopBrain_Think` `0x00300678`):

1. `Brain_MaybeSpotPlayers` (`0x0028fae8`).
2. The radar blip: mode 2 when a player (player 2 too in co-op) is in his enemy list, 3 while his top goal is
   CallForBackup (`0x79`), else 0; kept in human `+0x19e` and brain `+0x28c`.
3. Threat response set, no fight goal, no Chase goal (`0xc`) and a non-empty enemy list: brain `+0x28f` = 1 and
   `Brain_PushFightGoal(brain, −1)` (no time limit).
4. **Pursuit counter** brain `+0x148` (s16), when he has a target: + the target's gait when the target moves (gait ≠
   0) and either holds an object or moves along the cop's facing (velocity · facing ≥ 0), up to 36; else − 2, down to
   0. Its reader is not traced (inferred from what raises it: how hard the target is running from him).
5. With crime type 12 (Trespassing) enabled, every 3 thinks, the gang having no tactic: when he may respond
   (`CopBrain_CanRespond` `0x00302038`) or already has a fight or chase goal, `CopBrain_ChaseTarget` (`0x00301248`).
6. Every 17 thinks: the nearest **store flag** (activity 14) with group bit `0x10000` within 20 m; the nearest player
   to it within 10 m whose human byte `+0x5b7` is set, a valid enemy, and he may respond → **crime 1** (break and
   enter), below. Then the think ends.
7. Every 20 thinks: each enabled **tag spot** (activity 12) within `R` with a human on it (flag `+0xdc`) who is not
   friendly and to whom a clear ray runs, if he may respond → **crime 13** (vandalism), the spot passed along.
8. Every 4 thinks: each player picking a lock or stealing a stereo whom he sees (`Human_CanSeeHuman(+0x12c, R, 0.4 ×
   R, 2 m)`, the 0.4 × `R` being squared too) → crime 1 (lock) or 11 (theft).
9. Every 8 thinks: each player holding a world object of type hash `0x3c4e590b` whom he sees (as 8) → crime 11.

**Responding to a crime** (steps 6-9 and the event handler): pop a MoveToUseFlag goal (4) on top; count the backup
needed (`CopBrain_CountBackup` `0x00302118`); brain `+0x28f` = 0; `Brain_PushFightGoal(−1)`; when backup is needed,
`Goal_CallForBackup(cop, offender, crime, count)` (`0x002c4710`, goal `0x79`); then `Goal_SpotCriminal(cop, crime,
offender's position, offender, victim, flag)` (`0x002c4060`, goal `0x78`) on top. [Crimes and the police](ai-gangs.md#crimes)
has the report itself.

**Events** (`CopBrain_OnEvent` `0x00302340`):

- **1** with `+0x11` = 1, and **`0x10`**: `CopBrain_ReportCrime(cop, attacker)` (`0x00301cf8`): nothing for a
  friendly attacker; one throwing at him first calls for help (`Brain_CallForHelp` `0x00291d08`, done if answered);
  `+0x200` + 1. Without a fight goal and up: when the attacker is beyond `R` or out of line of sight, the cop treats
  it as a crime seen (`Brain_OnCrimeSeen` with event `0x17`) and stops. Otherwise a help call within 30 m, the gangs
  made enemies, a **crime report of type 2** (cop assault) at the attacker, MoveToUseFlag popped, backup counted;
  when not busy, `Brain_Fight(attacker, −1)` (brain `+0x28f` = the attacker hidden in shadow) or, already
  fighting, `Brain_AddEnemy` + `Brain_SetTarget`; CallForBackup (crime 2) when needed.
- **2, 9, 10, `0xc`, `0x11`**: dropped.
- **7** (a theft; offender at `+0x24`): not blocked, no gang tactic, may respond, the offender not friendly, in his
  field of view and a clear ray to him → crime 11, as above.
- **`0x14`** (violence seen), with threat response and up: the side to take (`Brain_PickSideInFight` `0x002912b0`).
    - **None** (a fight between strangers): not blocked, no tactic, may respond, the aggressor not friendly; the
      backup for crime 6 (gang fight). None needed: when the aggressor's gang last called (gang `+0x5f4`) more than
      30 s ago or never, and he is in line of sight, `Goal_LeaveArea(cop, aggressor's position, 2, aggressor)`
      (`0x002c4ec8`, goal `0x7a`: warn, then walk away). Needed: fight goal, CallForBackup and SpotCriminal for
      crime 6.
    - **A side**, if he is a valid enemy: the crime by the victim's brain type: civilian, gang, shopkeeper or dealer
      (4, 2, 6, 5) → 8 (mugging) when the offender is mugging, else 0 (assault); a cop victim (when the offender is
      not the event's `+0x24` and byte `+0x0c` is 0) → 2; else 14 (none). A civilian assault while two gangs fight
      within his far range of the victim (`Humans_TwoGangsFighting` `0x0029e168`) is ignored; so is everything while
      he has a target, is blocked, or holds SpotCriminal, CallForBackup or LeftTurf (`0x78`, `0x79`, `0x3f`).
      Without a fight goal: MoveToUseFlag popped; with a Respond goal (`0x74`) above the goal base (brain `+0x2d`)
      only when `Human_CanSeeHuman(+0x12c, R, 3 m, 2 m)` sees the offender; gangs made enemies; `+0x28f` = 0;
      `Brain_Fight(offender, −1)`. With one: gangs made enemies, `Brain_SetTarget(offender)`. Then, with no gang
      tactic and (no fight goal before, or the offender's gang `+0x5e8` clear): CallForBackup when needed and
      SpotCriminal for the crime. Last, with no HelpRespond goal (`0x1d`) above the base, `Gang_SendHelper(cop,
      offender, 0, −1)` (`0x002b75b8`).
- **`0x17`**: `Brain_OnCrimeSeen(brain, event, 0)` (`0x00291668`, then `CopBrain_OnCrimeSeen` `0x00301538`).
- Others: [the shared handler](#shared-events).

### Gang soldiers (type 2) {#think-gang}

Confirmed (code) at the cited addresses unless a line says otherwise.

**Think** (`GangBrain_Think` `0x00304608`):

1. `Brain_MaybeSpotPlayers`.
2. Unless human `+0x19e` is 4 or 5 (a blip a goal owns, such as CallGang's), the radar blip: 2 when a player is in
   his enemy list, else 0.
3. Threat response set, no fight goal and a non-empty enemy list: `Brain_PushFightGoal(−1)`.
4. The pursuit counter, as the cop's (step 4).
5. **A weapon**, every 6 thinks: wants one (`+0x265`), holds nothing, and his gang kind's pick-up factor *f*
   (`GangConfig_GetField6E` `0x001644f8`) is not 0: with chance 25 × *f* %, when no human is within 1.5 m (his
   sectors refreshed if older than 1 s, `0x0028fe90`; `Sectors_AllClear` `0x0029eaa0`), the nearest pickable object
   within **2 m** (`Ai_FindObject` `0x0029d5f0`, mask `0x30000`); for a gang of kind 1 any, else not one whose
   type class (`+0x87`) is 4: actions cleared and, on an empty queue, a pick-up action (`0x002faf98`, kind `0x21`).
6. **A hat**, every 12 thinks, when he may go for one (`Brain_CanSeekHat` `0x0028bfa8`: allowed (`+0x266`), not busy,
   bare-headed (`+0x364` empty), nobody attacking him, class `0x82`-`0x86`, a top goal that is not EngageEnemy,
   GetItem or LeftTurf, and the gang tactic, if any, allows it): his class's hat (class `+0x14c`) within **10 m** →
   `Goal_GetItem(human, 4, hat)` unless a GetItem goal is already above the goal base.
7. Every 15 thinks, with no target, no held flags and none of the busy states `0x7bf9e9f7ff0`: the first free
   (`+0xdc` none), enabled **tag spot** (activity 12) within 10 m with a clear ray → he sends himself event `0xf`
   with the flag (inferred from the flag passed: go and tag it; the reader is the goal on top).

**Events** (`GangBrain_OnEvent` `0x00304fa8`):

- **1** with `+0x11` set, the hitter not friendly: a hitter throwing at him first gets a help call (done if
  answered); the gangs made enemies unless already threats. By the top goal: GrabTarget (`0x1f`) → its broken-grab
  handler; BumLogic (`0x4f`) → the bum gives the hitter an item (`Bum_GiveItem`), done; the boss goals `0x8b`,
  `0x8a`, `0x85`, `0x86`, `0x89` → their handlers; a Riot goal (`0x54`) anywhere on the stack → `Riot_OnAttacked`.
  None of them, or one that declines: when not busy, `Brain_Fight(hitter, −1, 1)`, or `Brain_AddEnemy` +
  `Brain_SetTarget` when already fighting; when busy and the hitter is a player, a help call within 30 m and the
  hitter's head turns to him for 3 s.
- **`0x14`** with `+0x04` = 0, his gang not of kind `0x17`: `Brain_OnViolenceSeen` (below).
- **`0x17`**: `Brain_OnCrimeSeen` → `GangBrain_OnCrimeSeen` (`0x00304c78`): when he may react
  (`Brain_MayReactToCrime`) and, for a crime with an offender, the offender is a threat to him: without a Mark goal
  (`0x9a`), nothing when two gangs fight within the far range of the scene; then an **Investigate** goal (`0x5e`,
  `InvestigateGoal_Init(5 m, point, offender, 2, 0, line 13, chance)`) at the offender's (or the crime object's)
  position, the chance `0x00510ade` for a lookout (Scout `0x6f` or PathScout `0x70` on the stack) else `0x00510add`;
  with a Mark goal, a **ReactNoise** goal (`0x99`) at the point instead.
- Others: the shared handler.

**`Brain_OnViolenceSeen`** (`0x00291960`), used by gang members and Warriors:

1. Without a Mark goal (`0x9a`): with threat response, away from trains, no target, no held flags, actions free, and
   a top goal other than EngageEnemy, GetItem and LeftTurf: the side to take (`Brain_PickSideInFight`).
    - A **Warrior** whose chosen side is busy (grabbing, held...) and within 1.1 × his far range: his head turns to
      him for 3 s and, at 10 % when he may gesture (`Ambient_MayGesture`), actions cleared, a turn to him and a taunt
      (kind 3, id `0x10`); done.
    - Otherwise `Brain_Fight(side, −1, 0)` (or `Brain_SetTarget` when fighting) and, with no HelpRespond goal above the
      base, `Gang_SendHelper(self, side, 0, −1)`.
2. With a Mark goal: when a side is found, a ReactNoise goal on top (popping an earlier one) at the side's position.

### Civilians (type 4) {#think-civilian}

Confirmed (code) at the cited addresses unless a line says otherwise.

**Think** (`CivilianBrain_Think` `0x002fef40`):

1. `Brain_MaybeSpotPlayers`; timer B (`+0x2c8`) cleared once past.
2. Only while his gang has no tactic. Threat response set and no fight goal → target cleared, **threat response
   0** (a civilian calms down as soon as his fight ends).
3. **Reacting to the player**, when all hold: the script's response byte `+0x228` is 1
   ([binding](../references/bindings/ai.md)), timer B is 0, class `+0x11b` is not 6, he may react
   (`CivilianBrain_CanReactToCrime` `0x002ff198`), the nearest player is within **10 m**, that player's brain `+0x2d5`
   is clear, nobody in the player's gang is under attack (`Gang_AnyMemberHasAttackers`), and he has line of sight.
   A MoveToUseFlag goal on top is popped; then ped type 3 (brain `+0x26c`) pushes **Hostile** (`0x72`,
   `HostileGoal_Push(10 m, …)`), any other **PedReaction** (`0x6b`) saying line `0xaf`.

**Events** (`CivilianBrain_OnEvent` `0x002ffb30`):

- **0**: a bum (class `+0x11b` 6 or 7) under BumLogic (`0x4f`) takes the money (`Bum_TakeMoney`).
- **1**: a bum with BumLogic anywhere on the stack gives the hitter an item (`Bum_GiveItem`); then, with `+0x11` = 1,
  the attack warning below.
- **`0x10`** → `CivilianBrain_OnAttackWarning(brain, attacker)` (`0x002ff898`): one throwing at him first calls for
  help; a help call within 30 m; when he may react: MoveToUseFlag (4) or PlaySpecialIdle (`0x23`) on top is popped;
  ped type 3 and the attacker not friendly → threat 2 and `Brain_Fight(attacker, −1, 0)`; otherwise
  **PedReaction** (`0x6b`, no line). A CallPolice goal (`0x6d`) or Peddler (`0x50`) on top hears of it too. A
  non-friendly attacker raises `+0x200`; when he is busy and the attacker has a pad, the attacker's head turns to him
  for 3 s.
- **7** (a theft by the human at `+0x24`, within 15 m): when he may react, the crime can be reported
  (`GameState_CanReportCrime` `0x0041cf80`), `Random_Int(100)` ≤ 60, the thief in his field of view and in line of
  sight, a spawner in state 1 exists and game state `+0x29f` is set: MoveToUseFlag popped, he becomes the crime's
  reporter and pushes **CallPolice** (`0x6d`, `CallPoliceGoal_Push(self, 11, thief's position, −1, thief)`).
- **`0x14`** (violence seen), unless his brain is "dead" (`+0x09`) or he does not react to violence (`+0x267`):
    1. No side (strangers fighting): ped type 3 ignores it; others within 15 m of the victim who may react pop
       MoveToUseFlag and push PedReaction (no line) toward the human at `+0x24`.
    2. A side, unless the other party's top goal is Hostile (`0x72`): the crime kind as the cop's (4, 2, 6, 5 → 8 or
       0; a cop victim → 2 with chance 100 %; else 14), the chance otherwise `0x00510adf`. Only within 15 m of the
       offender and when he may react; MoveToUseFlag popped.
    3. Ped type 3 in a gang of kind other than `0x17`: nothing more. Ped type 3 in a kind-`0x17` gang, his human
       `+0x3b8` clear and the victim's `+0x3b8` = 1: threat 2, `Brain_Fight(offender, −1, 0)`, line `0xcd`; done.
    4. **Calling the police**, when the crime can be reported, the kind is not 14, the offender's gang last called 40
       s ago or more (or never), `Random_Int(100)` ≤ the chance, line of sight, the crime is not an assault on a
       civilian while two gangs fight within his far range, and the crime has responders (game state `+0x294 +
       kind`): with a spawner in state 1 (and, for a cop victim, fewer than 4 humans within 10 m of the offender),
       he reports and pushes **CallPolice** (kind, the offender's position, victim, offender); a player offender may
       queue tutorial hint 7 once. With no spawner in state 1 but one in state 0: he reports and pushes **CallGang**
       (`0x6e`, `Goal_CallGang(15 m, self, offender, 2, 0, 0)`).
    5. Otherwise, any but ped type 3: **PedReaction** (`0x6b`) against the offender, no line.
- **`0x17`** within 15 m of the crime object: `Brain_OnCrimeSeen` (→ `CivilianBrain_OnCrimeSeen` `0x002ff2b0`).
- Others: the shared handler.

### Dealers (type 5) {#think-dealer}

Confirmed (code) at the cited addresses unless a line says otherwise.

**Think** (`DealerBrain_Think` `0x00302cd8`): `Brain_MaybeSpotPlayers`; threat response set and no fight goal →
threat response 0; every 5 thinks, while fighting (threat response set), wanting a weapon (`+0x265`), empty-handed
and not blocked: the nearest pickable object within 2 m (mask `0x30000`) → actions cleared, a pick-up action (kind
`0x21`).

**Events** (`DealerBrain_OnEvent` `0x00303178`): **0** under Dealer (`0x80`) → `DealerGoal_OnBuy`
([Buying](ai-scripting.md#dealer-buy)); **1** with `+0x11` = 1 and **`0x10`** → `DealerBrain_OnHit` (`0x00302ff0`,
[Buying](ai-scripting.md#dealer-buy)); **`0xb`** and **`0x14`** dropped; others the shared handler. Confirmed (code).

### Shopkeepers (type 6) {#think-shopkeeper}

Confirmed (code) at the cited addresses unless a line says otherwise.

Type 6 (the `civl_co_di` class kind) is the **shopkeeper**: its handlers serve the Shopkeeper goal (`0x82`).

**Think** (`CivlCoDiBrain_Think` `0x00303f10`): the same as the dealer's think.

**Events** (`CivlCoDiBrain_OnEvent` `0x00304228`); `ShopkeeperBrain_OnDisturbance` (`0x00304070`) is the
shopkeeper's **disturbance handler** `(brain, offender, reason, object, call help)`: a thrower first calls for help;
with call help, a help call within 30 m; when he is free to fight (`Brain_IsFreeToFight` `0x00304030`) and has a
Shopkeeper goal, goals above it are popped and the goal is told (`ShopkeeperGoal_OnDisturbed`); a non-friendly
offender raises `+0x200`.

- **0** under Shopkeeper: the goal's prompt handler (`0x002e63e0`, the sale; [AI goals](ai-goals.md)).
- **1** with `+0x11` = 1: disturbance (the hitter, reason 1, no object, call help).
- **6, 7**, with a Shopkeeper goal: when the store's box (the goal's `+0x3c` handle) contains the object (event `+0x04`
  for 6, `+0x00` for 7): disturbance (the human at `+0x24`, reason 0, the object, no call).
- **10**, with a Shopkeeper goal whose `+0x54` and `+0x30` are clear: when a player is within the goal's range
  (`+0x38` × 0.25, at least 16; compared with a squared distance, inferred from the comparison), goals above the
  Shopkeeper goal are
  popped.
- **`0xb`**: dropped. **`0x10`**: disturbance (the attacker, reason 1, call help). **`0x14`**: disturbance (the side
  `Brain_PickSideInFight` picks, reason 2, no call).
- Others: the shared handler.

### The Warriors (type 3) {#think-warrior}

Confirmed (code) at the cited addresses unless a line says otherwise.

**Think** (`WarriorBrain_Think` `0x003052f0`):

1. `Brain_MaybeSpotPlayers`; threat response set, no fight goal and enemies → `Brain_PushFightGoal(−1)`; the pursuit
   counter as the cop's.
2. **Pick-ups**, every 6 thinks: [The Warriors' pick-ups](ai-gangs.md#warrior-pickups). The search is 1.5 m with mask
   `0x10000`; the mask is `0x30000` (and the search's last flag 1) when his gang's leader is neither grabbing from
   behind nor tagging, the gang's `+0x32` is 1, a member of the gang is under attack and his own enemy list is empty.
   The object must also pass `WorldObject_CanBePickedBy`.
3. Outside co-op (game state `+0x158` = 0): holding a weapon of kind 4 or 6 with an empty enemy list and no steal
   tactic (`0x26`) → he drops it.
4. Every 8 thinks: holding a world object whose type byte `+0x64` is `0x9a` while his human `+0x5b7` is clear → drop
   it (inferred from the store flag `+0x5b7`: loot only inside a store).
5. **Hats** for classes `0x12`, `0x14` and `0xbc`, every 6 thinks, under the follow (`0x12`) or hold (3) tactic and
   with no hostiles counted (`+0x152` = 0): unless his `+0x364` already holds the named hat object (`0x0056a2d0`), the
   `dyn_warr_cb` object within 20 m → `Goal_GetItem(4)` unless a GetItem goal is above the goal base.
6. **The pick-up window** `0x00510a9c` (`AI_SetPickupWindow`): once its time passes it is cleared; then, when he is
   free (not busy, not blocked, no held flags `0x1c1ee60`), the first player carrying (`+0x364`) a world object of
   type hash `0xbbbef927` within 10 m and in line of sight: actions cleared, a turn to him, a wait action, and line
   `0x6d` at him.
7. **The swap prompt** (human `+0x1b2`): the nearest player within 1.5 m, not blocked, no held flags, gait below 3;
   one of them holding an object; this Warrior not sparring (`+0x2e5` clear), not blocked, standing (gait 0), in the
   player's gang (or `+0x121` set), with no attacker slots taken on him, and on the right side of the player
   (`Human_GetSideOf` = 0). The prompt string is `0xc` (both hold), `0xd` (he holds) or `0xe` (the player holds);
   the swap itself is the event-0 prompt ([The weapon swap](#warrior-swap)).
8. Timer `+0x2dc`: once past, cleared with byte `+0x2e4`.
9. **Getting out of the way**, every 12 thinks, when he is out of the fight stance, not in shadow, not busy or
   blocked, standing, with no target, no attackers, and a top goal other than GetItem (`0x2c`) and PlaySpecialIdle
   (`0x23`):
    - when a ray toward a point ahead of him (his near range, 3 m, inferred from the field read) is blocked and a player
      is within 30 m:
      actions cleared (unless the front one is of class 8) and a turn (toward the player, inferred from the point passed);
    - the nearest member of his gang within 1 m who is free, has neither Scatter (`0x37`) nor HoldPosition (`0x36`)
      and no actions is pushed aside (`Brain_PushAside`).

**Events** (`WarriorBrain_OnEvent` `0x003063b0`): **0** → `WarriorBrain_OnPrompt`; **1** → first
`WarriorBrain_OnHitByChief` (`0x00306190`); otherwise, with `+0x11` set and a non-friendly hitter: gangs made enemies
unless already threats and, unless one of the busy states `0x1f80974000` holds, `Brain_Fight(hitter, −1, 1)`;
**`0xb`**: the new enemy noted (brain `+0x27c`, flag `+0x280` = 1), consumed; **`0xc`** consumed; **`0x14`** with
`+0x04` = 0 → `Brain_OnViolenceSeen`; **`0x18`** → `Brain_OnDangerNear`; then (except for consumed ids) the shared
handler.

### The player (type 0) {#think-player}

Confirmed (code) at the cited addresses unless a line says otherwise.

**Update** (`PlayerBrain_Update` `0x003035d8`, every step; it pushes no goals of its own on him):

1. `Brain_UpdateAlertness`; a target that is no longer up is dropped.
2. Every 300 updates, `+0x152` = how many AI humans of all 32 gangs hold him in their enemy lists.
3. Every 2 s (brain `+0x160`), `PlayerBrain_ScanNearEnemies(7.5 m, target)` (`0x0028af88`).
4. `WarChief_AutoCommand` ([Automatic commands](ai-gangs.md#warrior-auto-commands)) every update; every 40 updates
   `WarChief_CheckCrewInRange`, quiet and forced, so it only refreshes the [out-of-range flag](ai-gangs.md#warrior-range-flag)
   (call at `0x003037c4`).
5. Brain `+0x110` = his heading.
6. While `+0x2e4` is set (a Warrior is hitting back at him, [`WarriorBrain_OnHitByChief`](ai-code.md#brain-types)),
   every 13 updates each AI member of his gang within 15 m who is free, not sparring (`+0x2e5` clear) and has no
   TauntPlayer goal gets **TauntPlayer** (`0x63`, `TauntPlayerGoal_Push(member, chief, 1, −1)` `0x002d09f0`): the
   taunting ring ([TauntPlayer](ai-goals.md#goal-taunt-player)).

**Think** (`PlayerBrain_Think` `0x00303260`): in two-player play only (game state `+0x224` = 2, `+0x158` = 0) and
unless his `+0x3ac` is 1: the swap prompt between the two players, as the Warrior's step 7 (within 1.5 m, both out
of the fight stance, not blocked, standing, one holding something, the other player's attack slots empty).

**Events** (`PlayerBrain_OnEvent` `0x00303e90`): **0** → `PlayerBrain_OnPrompt` (`0x00303468`); **`0xb`**, **`0xc`**,
**`0x14`** dropped; others the shared handler (so the attack warning, `0x10`, reaches a player's brain too).

### The weapon swap {#warrior-swap}

Confirmed (code) at the cited addresses unless a line says otherwise.

Triangle beside a Warrior (or, in two-player play, beside the other player) whose swap prompt is up
([step 7](#think-warrior); [HUD: the talk prompt](hud-messages.md#talk-prompt)) swaps what the two hold in their hands.
Confirmed (code) at `0x0024d810`, `0x00306040`, `0x00303468`, `0x00233b08`, `0x0021c0a8`, `0x00101360` unless marked.

**How the press gets there.** The swap has no context record: it comes from triangle's pick-up search
([Crimes: triangle](crimes.md#triangle), step 5; `Human_PickUpSearch`, `0x0024d810`), which runs only while the
presser has no pick target (human `+0x33c`). The search gathers world objects within 1.5 m and the humans ahead of him
within 1.5 m (`Humans_FindAhead`), sorts them all by distance, nearest first, and sends each in turn message 0 with
the presser as the subject; of the humans only brain types 0 (a player) and 3 (a Warrior) are asked. A human's
message 0 reaches his brain as event 0: `WarriorBrain_OnPrompt` or `PlayerBrain_OnPrompt`, the brain's own human
being the **receiver**. A handler that returns true ends the press; one that refuses (false) lets the search go on to
the next candidate, so a refused swap can still pick up an object nearby or, with nothing to take and something in
hand, **drop** the presser's object (inferred from the search's order; the refusal itself is silent).

**A Warrior** (`WarriorBrain_OnPrompt`, `0x00306040`) refuses unless all hold:

1. the receiver is talkable (his interface's "has a handler or `+0x1b2`", `0x0021d570`);
2. neither of the two has his actions blocked (`HumanRecord_AreActionsBlocked`, `0x00228228`);
3. neither has any of the held flags `0x5c0221f` (busy: attacks and reactions, [EngageEnemy](ai-fights.md#engage-enemy));
4. the way is clear: the receiver is not grabbed and a 0.2 m capsule swept from him to the presser hits nothing but
   the presser (`Human_IsWayClearTo`, `0x0021c0a8`, collision mask `0x2803`).

Then his actions are cleared, he **turns to face the presser over 0.2 s** (`Human_TurnToFacePoint`, `0x00221c20`),
and the swap runs (below). The presser does not turn.

**The other player** (`PlayerBrain_OnPrompt`, `0x00303468`) checks only 1 and 2 (no held flags, no clear way), clears
the receiver's actions and **sets his rotation at once** to face the presser (the heading from his position to the
presser's, written straight into his transform: no turn over time), then the same swap. What puts the prompt up
between the players is [`PlayerBrain_Think`](#think-player) (two-player game state `+0x224` = 2, not co-op Rumble,
the receiver not a war chief).

**The swap** (`Human_SwapHeldObjects(receiver, presser)`, `0x00233b08`):

1. Each side's held thing (human `+0x338`) counts only when it is a **world object**; a held human counts as nothing
   and stays where it is. Nothing else changes hands: not the hat (`+0x364`), not the inventory (items, money, flashes,
   [Player state](player-state.md)).
2. Each side holding a world object **drops** it (`Human_DropHeld`, `0x00257f38`, [Objects: drop](objects.md#pickable)):
   its
   anim set is popped, it gets a physics body and a removal time.
3. For each dropped object, the **other** human takes it: every task on his animation stack (`+0xd8`) gets slot
   `+0xc8` with `0x10` (not traced), then the object is placed **straight into his hand** without a clip, as
   `HuPlaceItemInHand` does ([Objects: scripted placement](objects.md#pickable)): `Human_PlaceObjectInHandByPickupClip`
   (`0x0024c6d8`) picks
   the **low** pick-up clip for the type's `pickup_anim` (`CfgObj` `+0x65`: 461 by default, 503 for 2, 481 for 3, 498
   for 4, 463 for 5, 465 for 6, 549 for 7; the height is not tested), makes the object his pick target (`+0x33c`) and
   reads the clip's **first type-9 event** without playing it (`Anim_SendPickupPlacement`, `0x00101360`), sending the
   object message `0x1b` **take** at once with the event's bone and offset; the take puts it in his hand (`+0x338`)
   and applies its anim set (message 3). When the placement succeeds the object's record is unpinned. Then his state
   gets `0x20000000` (the anim state is rebuilt next update, so his idle and moves follow the new anim set) and the
   object's removal time is cleared (`WorldObject_SetRespawnTimer(obj, 0)`).
4. A **left-hand** or **hat** object (`pickup_anim` 5 or 6) has no type-9 event in its clip (463, 465), so it is not
   attached: it stays dropped at the giver's feet (inferred from the placement path, as for `HuPlaceItemInHand`).

So the exchange is **instant**: no clip plays on either side, and both objects change hands on the update of the
press; when only one holds something it simply passes to the other. Any held world object can be swapped (bats,
bottles, knives, pipes, two-handed objects; the prompt's own test is only "one of them holds something").

**The line.** Afterwards, when the **presser** holds something and is not hidden in shadow (brain `+0x2d4` clear),
he says speech command **146 `give_me`** (`Human_SayCommand(1.0, presser, 146, no callback, no interrupt, uncut,
no target, duckable)`, [Speech](../references/speech.md#speech-command-146)). The handler then returns true.

## In Coney

- **The gloat** (`gloatOver`, [The shared event handler](#shared-events)): built as `gloatOver`
  (`repo:src/ai/brain_events.h`), with the taunt action, the
gang clip table's names and the event sent as a man comes to lie on the ground
  (`repo:src/ai/brains.h`, `reportPutDowns`); the BigFighter branch and class `0x80`'s hat are not.
- **Events and help calls** (`repo:src/ai/brain_events.h`, [Shared events](#shared-events)): a brain answers its human's
  events with its type's handler (gang soldiers and Warriors: a stranger's hit starts a fight, violence is
  `Brain_OnViolenceSeen`), then the shared one (a hit, a new enemy or an attack warning sends a 20 m help call; the
  reset clears the brain; an attack warning under a fight goal may be countered). A help call is violence (`0x14`)
  to everyone within its range and the hearer's help hearing; a soldier or Warrior takes a side
  (`Brain_PickSideInFight`), fights him and pushes HelpRespond. **Coney stand-ins**: the threat test is the enemy list
  or an enemy gang; the cop's, civilian's, dealer's and shopkeeper's handlers are not built (they take the shared
  one); the top goals' own hit handlers, the Mark goal's noise reaction and the look-around are not built.

A think counts, keeps the tackle meter of a cop, a gang soldier or a Warrior, and for a Warrior runs step 7, [the swap
prompt](#think-warrior) (`repo:src/ai/swap_prompt.h`): talkable (`ScriptState::talkable`) with `GSTRING.HUD` 0xc, 0xd
or 0xe while a player of his gang within 1.5 m, below a jog, has him standing in front, nobody attacks him and one
of the two holds an object (**Coney's reading**: the hand's object, `+0x338`); the blocked and held-flag tests, the
sparring byte and `+0x121` are not modelled, nor the two-player prompt. The other steps are not built. **The swap**
(`PlayLevelMode::trySwap`, `ai::swapHeldObjects`): player 1's triangle, after the dealers' offers, asks the Warriors
ahead of him within 1.5 m nearest first; one that is talkable, not grabbed, with a clear line to him and both of them
in the free combat mode (**Coney's stand-in** for the blocked and held-flag tests) clears his actions and turns to
him over 0.2 s, and the two hands swap at once (a left-hand or hat object stays at its giver's feet); player 1 says
146 `give_me` when he now holds something and is not hidden. **Coney's stand-ins**: the Warriors are asked before
the objects' search rather than among them by distance; the two-player swap is not built.

## Open questions

- **Who sends some events**{#q-event-senders}: events 6, 9, 10 and `0x11` have no sender found, nor a reader of event
  `0xf` (the tag-spot message); what reads the pursuit meter brain `+0x148` besides the fight goal, and what brain
  `+0x28f` controls. A search of the stores of those ids, as was done for `0x15`
  ([Gang fights](gang-fights.md#waiting)), would answer it.
- **The think handlers of types 2, 3 and 5 in detail**{#q-thinks}: which goals the Warriors' think pushes for an ally.
- **The gloat in Coney's sparring fight**{#q-gloat}: the original queued one gloat in a run of level99's sparring fight
  and Coney none ([How this was checked](#runtime-gloat)); comparing the put-downs in both would show why.

## How this was checked

### The gloat in the sparring fight {#runtime-gloat}

PCSX2-MCP fork 2.9.129, 2026-10-21, scenario `warriors_taunts` with the `taunt-init` hook on `TauntAction_Init`
`0x002fb0f0`: in level99's sparring fight against a still player, one
gloat was queued in 1,198 updates: a cheer (kind 3, line `0x10`) by one Warrior at update 1507, as a mount let the
player go (the call at `0x00293428`). It started 57 updates later, at update 1564, once his queued actions had
ended (inferred), and said its line there (`Human_SayCommand` `0x002205e0` called from `TauntAction_Start`,
return address `0x002fb224`). Nothing taunted at kind 4: the still player has no target of his own. Coney's
sparring disc test over the same span saw no gloat start in any of its eight AI sequences. Why Coney's sparring
fight gives none where this run of the original gave one is an [open question](#open-questions); it is compared with the
taunts on [#596](issue:596).
