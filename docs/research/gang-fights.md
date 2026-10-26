---
kind: topic
system: ai
owns:
  functions: [
    "0x00165b70", "0x00307668", "0x00307788", "0x00307878", "0x00307a10", "0x00307d40", "0x00307fe0", "0x00308150",
    "0x003081a8",
  ]
---
# Gang fights: how a gang fights together

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records or, for the older ones, How this was checked.

## Summary

How the members of a gang share out a big fight: who goes for whom, how many may hit one man at once, what the rest
do while they wait, when they switch, how they help each other and what the gang as a whole adds. There is **no
fight coordinator**: no object owns a brawl and hands out targets. The coordination comes from five mechanisms that
each brain meets on its own: the **target's own limits** (attack slots and active places), a **shared score** for
choosing among enemies, **help calls** (event 20), the **gang's tactic** (whose Process and events push goals on the
members) and, under TacticAttack, short **team-up orders** from the leader. The single fighter's side (the fight goal,
the attack check, the reposition, the run-in) is on [AI humans](ai/index.md) and only linked here.

## Map

No object of its own: the mechanisms below live in the brains, the gang record and its tactic
([AI code index](ai-code.md)); each section cites its functions.

| Behaviour | Function | Section |
| --- | --- | --- |
| The shared target | `0x0041c4e0` `WarriorCommand_Dispatch`, `0x00165b70` `Gang_SetSharedTarget` | [Who goes for whom](#targets) |
| How many swing at one man | `0x00291008` `Brain_ClaimActiveAttacker` ([AI fights](ai-fights.md#attack-places)) | [How many hit one man](#limits) |
| Gloating over a downed man | `0x00292d80` `Brain_DefaultOnEvent` ([AI brain types](ai-brain-types.md#shared-events)) | [What the waiting ones do](#waiting) |
| The forced re-target | `0x002b3ab0` `FightGoal_Process` ([AI fights](ai-fights.md#fight-order)) | [Target switches](#switches) |
| TacticAttack's start and its Melee | `0x00307fe0` `AttackTactic_Start`, `0x00307788` `AttackTactic_GiveMelee` | [Rumble's sides](#rumble) |
| Idle members sent to the fight | `0x003081a8` `AttackTactic_Process` | [Helping a gang-mate](#help) |
| Violence near a TacticAttack gang | `0x00307878` `AttackTactic_OnViolence` | [Helping a gang-mate](#help) |
| Flight | `0x00307d40` `GangTactic_CheckFlee` | [The gang as a whole](#gang) |
| The team-up orders | `0x00307a10` `AttackTactic_StartSubTactic`, `0x00308150` `AttackTactic_OnStrategyEnd` | [The gang as a whole](#gang) |
| The interval nothing reads | `0x00307668` `AttackTactic_ComputeSpacing` | [Rumble's sides](#rumble) |

The entry points are the tactic's vtable functions, which the gang update and `Gang_OnEvent` call
([AI gangs](ai-gangs.md#tactics)), and the single fighter's goals on [AI fights](ai-fights.md). Confirmed (code) at the
addresses in the table.

## Data

The records involved are [the brain](ai/index.md#brain) and [the gang](ai-gangs.md#gang-record); each rule cites the
offsets it
reads.

## Behaviour

### Who goes for whom {#targets}

Confirmed (code) at the cited addresses unless a line says otherwise.

1. **Each member keeps his own enemy list** (brain `+0x164`, 16). It fills from the periodic scan
   ([The enemy scan](ai-fights.md#enemy-scan)), from being hit or warned (the attacker is added both ways by `Brain_AddEnemy`
   `0x0028d538`), from [help calls](ai-brain-types.md#help-calls) and from the tactic's events. Nothing copies one
   member's list to
   another, except the **shared target** (rule 4).
2. **The choice is a score** ([Choosing among enemies](ai-fights.md#enemy-score)): Melee re-picks with `Brain_PickBestEnemy`
   (`0x0029f230`, flags `0xfffffffd`) every time a fight goal ends. The terms that make a gang **spread** are the
   distance (4 points a metre nearer), "he targets me" (+4), "he is armed and targets me" (+3) and above all **−999
   when his attack slots are full** and the scorer is not nearer than a holder (`Brain_CanTakeSlotOn` `0x0028dc80`):
   a fifth man cannot choose a target four already hold. The terms that make it **focus** are the player (+20), the
   enemy leader (+6 for a scorer that is not a Warrior, flag `0x4`), the previous target (+3, the default adjustment
   `0x0029f3a8`) and the gang's shared target (+400). Grabbed (+1), grabbed from the rear (+6), down (+1) and stunned
   (+1) draw mates to a man who is already held or hurt.
3. **The crew under the attack command** (Warriors, and Rumble's side 1, [below](#rumble)) runs
   FollowAndAttack, which scores with flags `0xfffffbf9` (no "near the leader", "enemy leads his gang" or "running"
   terms) and drops a winner more than **60 m from the chief** (`FollowAndAttackGoal_Process` `0x002bc198`: squared
   distance chief to enemy over 3600 clears the target). Its fight goals last 4000 ms.
4. **The shared target** (gang `+0x10`) has one writer besides the reset: the Warrior-command dispatcher
   (`WarriorCommand_Dispatch` `0x0041c4e0`). Every command first clears it; an **unforced attack command** (the
   player's menu) then sets it to the chief's lock-on target when he is locked on (`Human_IsLocked`) and that target is
   an AI (pad `+0x1b0` = −1) and not friendly, else to the human held in game state `+0x280` or, failing that,
   `+0x284`, when he resolves and is not friendly (these two are not tested for being an AI):
   - **`+0x280` is the crime reporter**, the one human at a time phoning the police or calling a gang over a crime
     (`GameState_SetCrimeReporter` `0x0041cfe8`; [Crimes](crimes.md) lists it with `GameState_CanReportCrime`). A
     civilian sets himself there when he sees a crime and pushes CallPolice or CallGang
     (`CivilianBrain_OnCrimeSeen` `0x002ff660`, `0x002ff72c`; also `CivilianBrain_OnEvent` and
     `PedReactionGoal_Process`), `CallPoliceGoal_Start` (`0x002a7ed0`) sets him and `CallPoliceGoal_End`
     (`0x002a7f94`) clears it.
   - **`+0x284` is the human calling his gang for backup or running off with the player's money**, one at a time
     (`GameState_SetDealerCustomer` `0x0041d080`, set only while `GameState_IsDealerFree` `0x0041d088` finds it
     empty): a scout who has spotted an enemy and pushes CallGang (`ScoutTactic_OnEvent` `0x0031ab28`, then
     `Goal_CallGang`), or a dirty dealer who has just ripped off the buyer and runs off (`DealerGoal_OnBuy`
     `0x002c7910`, [Buying](ai-scripting.md#dealer-buy) rule 3). `CallGangGoal_Destroy` (`0x002d606c`) and
     `DealerGoal_End` (`0x002c70fc`, when it is him) clear it.

   Both are set to the null handle (`0x006ebd30`) by the level's reset (`GameState_ResetForLevel`, `0x00418d0c` and
   `0x00418d24`) and first written at construction. So with no lock-on, **the attack command sends the crew after
   the snitch** (the civilian phoning the police), else after the scout calling his gang or the dealer who cheated
   the player. Confirmed (code) at the writers cited, each of which passes the brain's own human;
   `GameState_SetCrimeReporter` and `GameState_SetDealerCustomer` are their only writers apart from
   `GameState_Construct` (`0x00418708`, every `sw` to `+0x280` and the setter's callers checked).

   `Gang_SetSharedTarget` (`0x00165b70`) accepts the target only when it passes `Brain_ValidateEnemy` for the
   gang's leader and is not of a friendly gang, and then **adds it to every AI member's enemy list**. With +400 in
   the score (and the scan always re-adding it) the whole crew converges on him. A forced command
   (`WCIssueCommand(..., true)`, as Rumble uses) never sets it. No other gang sets one.

### How many hit one man at once {#limits}

Confirmed (code) at the cited addresses unless a line says otherwise.

Two limits on the **target**, both described in [Attack slots, active places and spacing](ai-fights.md#attack-places):

1. **Attack slots**: at most `+0x1e4` (4 by default) humans may target him. A nearer newcomer, or one forced in
   (the fight goal's re-target, rule 2 of [the switches](#switches)), takes the slot of the farthest
   holder and clears that holder's target, whose fight goal then ends at its step 2 and whose Melee picks again.
2. **Active places**: of the slot holders, only `s` may be in a swing at once, `s` being the spacing byte the
   **attackers' gangs** raised: `CfgGang` value 2 for a standing man, value 3 for a downed one (the largest over every
   gang that has claimed a slot on him since his list was last empty). A **Warrior** target (brain type 3) keeps 1
   standing; the player (type 0) does not. The values per gang type ([Gangs](../references/gangs.md), types as
   `GangCreate`'s first argument):

   | Gang type | Strategy | Standing (v2) | Downed (v3) |
   | --- | --- | ---: | ---: |
   | 0 (Warriors), 2, 4, 5, 6, 9-12, 14-20, 22-24 | various | 2 | 2 |
   | 1 (cops) | StratNormal | 3 | 3 |
   | 3 (`Gang1`, Rumble side 1) | StratOrphans | 1 | 4 |
   | 7 (Turnbulls), 21 (Huns) | StratTurnbulls, StratHuns | 4 | 4 |
   | 8 (Furies) | StratFuries | 2 | 1 |
   | 13 (Hurricanes) | StratHurricanes | 1 | 1 |

   Rumble side 2 (`Gang2`, type 19) is in the first row. So two soldiers of an
   ordinary gang swing at the player together, a Turnbull pack four.
3. **The gap between swings** at one man (`+0x1ec`) is one swing's length divided by `s`, shared by every holder: a
   holder whose turn has not come waits even with a place free (`Brain_ClaimActiveAttacker` `0x00291008` needs the
   gap passed, or a place already held by someone).

### What the waiting ones do {#waiting}

Confirmed (code) at the cited addresses unless a line says otherwise.

A slot holder without a place, or whose own next-attack time (`+0x1e8`) has not come, gets a wait reason from
[Brain_CheckAttack](ai-fights.md#check-attack) and [repositions](ai-fights.md#fight-reposition):

1. **They circle in a band of 4 to 4.75 m** round the man (move-to-human, 2000 ms), the one he is fighting (his own
   target, or the first slot holder when he has none) staying close (inner radius 2 × his capsule radius). Members
   under a Defend tactic (or tactic type `0xd`) stay close too.
2. **They taunt**: once every random 1000-2000 ms, when at least 2.75 m away, fewer than two men hold slots on the
   waiter, and the target is empty-handed (11 % for reason 7), a fidget on anim `0x25b` with line `0x11`.
3. **Some reasons hold a waiter back**: 4, an empty-handed waiter does not walk into an armed man near his own
   leader; 6, a thrower does not throw through a mate in a 40° cone; 8, an empty-handed man lets a slot holder with a
   heavy object go first; 5, everyone but a cop waits 15-16 m off while the police have the man.
4. **On the run-in** (EngageEnemy) attackers fan out: the aim point is turned 9° per attack-slot index, alternating
   sides, when the target faces away ([EngageEnemy](ai-fights.md#engage-enemy) step 12).
5. **Event `0x15`** makes every slot holder whose top goal is Fight taunt (kind 4 at his own target, kind 3 otherwise
   when he is beyond the middle of his melee ranges, his next attack delayed by twice the taunt): [The shared event
   handler](ai-brain-types.md#shared-events). **It is sent to a man when he is put on the ground**, so his attackers
   gloat over
   him. Two senders, each writing id `0x15` at record `+0x20` and the downed man at `+0x24`, and passing the record to
   his own human's event slot (vtable `0x0053f088` `+0x44`, `Human_OnEvent` `0x0021d480`). That offers the event
   first to the man's own script handlers (human `+0xe8`, `0x00384c38`), which can consume it, and only then to the
   gang's `Gang_OnEvent` `0x00164c20` and `Brain_OnEvent` ([Gang events](ai-gangs.md#gang-events),
   [Messages and events](tasks.md#messages)); so a script handler on the downed man that consumes event `0x15`
   stops the taunts (inferred from that order):
   - `HitReact_OnSpecialEnd` (`0x0026a1f8`), the end callback of the **knock-down** clip that `Human_PlayReaction`
     (`0x0026a6d0`, its knock-down branch, after `Human_KnockDown`) and `HitReact_InSpecial` (`0x0026860c`) chain on
     a man knocked down by a hit;
   - `Throw_ClearVictimLink` (`0x0022bdc0`, the store at `0x0022becc`), when a throw, a grab or a mount lets go of
     its victim and puts him down (`Human_GoDown`, when he is not knocked out, wounded or dying and the clip's
     knock-down value says so; its callers are `Throw_ClearLink`, `Human_BreakPair`, `Pair_OnVictimClipEnd`, the grab
     and mount reversal and throw ends).

   Confirmed (code). No other human or AI code loads `0x15` as an event id (the 164 `li ..., 0x15` of the executable
   and the 86 delay-slot `_li ..., 0x15` matches, some of them `0x15x`, checked; the rest are anim ids, lines,
   sizes, call arguments and the world objects' own message `0x15`, which is not a brain event).

### Target switches and what interrupts a fight {#switches}

Confirmed (code) at the cited addresses unless a line says otherwise.

1. **The fight goal ends** (and Melee re-scores, +3 for the previous target) when the target fails
   `Brain_ValidateEnemy`, when he no longer holds a slot on him (displaced, [limits](#limits) rule 1),
   when the target is beyond
   5.5 m (unless he holds a throwable), every 30 updates when no straight walkable line is left, and at its deadline:
   4000 ms for every fight goal Melee pushes ([The durations](ai-fights.md#fight-durations)), unless at that moment he
   still holds
   an active place on an attackable target and has no FollowAndDefend goal. So in a long brawl **every member
   re-chooses at least every 4 s**.
2. **The re-target** (`FightGoal_Process` `0x002b3ab0`, at most once a second, `+0x14`; the first on the update after
   the goal starts or resumes, as `FightGoal_Resume` `0x002b2d00`, which Start calls, sets `+0x14` to now and the test
   is `+0x14` < now, confirmed (code)): the first word of his
   [sector record](ai-moving.md#neighbour-sectors), refreshed when older than 1 s, which is the **nearest human within
   1.5 m in sector 0** (straight ahead, ±22.5°). When that human is someone other than the target, a threat
   (`Brain_IsThreat` `0x00290138`: an enemy gang, or both players) and no GrabTarget goal (`0x1f`) is on the stack, he
   becomes the target with the slot **forced** (`Brain_SetTarget(brain, him, 1)`). Confirmed (code).
   The new target is not checked against the enemy list or
   `Brain_ValidateEnemy` there: an enemy who steps in front of a fighter takes him over, and the next update's step 2
   ends the goal if he is not valid.
3. **EngageEnemy** re-targets every 2 s to the nearest enemy he can see when the current one is beyond 5 m.
4. **Interruptions within a fight**: a block (`Goal_TryBlock` on an attack warning, [Blocking](ai-fights.md#block))
   gives up the
   active place; a [reaction goal](ai/index.md#reaction-goals) (hit, grabbed, tackled, down, stunned) runs above the
   stack and
   the fight goal resumes when it ends; a knock-out ends every goal and releases his slot and place on his target
   (`Brain_OnKnockedOut` `0x0028c3b0`); a tactic's order pops the member to his goal base and gives new goals (the
   3 s idle check, a team-up order below, the attack tactic's events 19 and 22).

### Helping a gang-mate {#help}

Confirmed (code) at the cited addresses unless a line says otherwise.

1. **Being hit or warned spreads the fight.** A member warned of an attack (event `0x10`) or hit (event 1) sends a
   help call within 20 m ([Help calls](ai-brain-types.md#help-calls),
   [The shared event handler](ai-brain-types.md#shared-events)); a busy gang member
   hit by a player calls within 30 m. Every hearer with no target, no held flags and free actions picks the side
   (`Brain_PickSideInFight` `0x002912b0`: his friend's attacker), starts a fight on him and pushes HelpRespond, which
   walks him over until he lists an enemy. A member who already has a target ignores the call: **help comes from the
   idle**.
2. **A grabbed member does not call for help** (only a civilian in the Grabbed reaction does, every 30 updates within
   10 m, [The fight reaction goals](ai/index.md#fight-reactions)).
   Mates are drawn by the score instead: a rear-grabbed enemy is
   +6 and +1 grabbed. The other way round, a member holding an enemy from behind **presents him** to a nearby friendly
   player ([Grabbing](ai/index.md#fight-reactions)), and an AI mate may start a tandem on a held enemy
   ([Combat moves: tandem](combat-moves.md#tandem-ai)).
3. **A downed or cuffed mate** is rescued only by a member running FollowAndDefend (the Defend tactic, or the
   Warriors' defend command), which pushes [SaveHuman](ai-goals.md#goal-save-human) on a cuffed or knocked-out chief;
   the Defend tactic also sends its members to the defended human on events 17 and 18. No rule was found that makes a
   plain gang soldier protect a downed mate (inferred from the callers of `Goal_SaveHuman` `0x002b7d80`). A downed man
   himself, still attacked, plays the get-up attack after about 1.9 s ([Grounded](ai/index.md#fight-reactions)).
4. **Under TacticAttack, idle members are sent to the fight** every 3 s (`AttackTactic_Process` `0x003081a8`): each
   member with an **empty goal stack** is popped to his base and given Melee, and then a HelpRespond goal toward the
   **farthest** member that has goals (`Gang_SendHelper` `0x002b75b8`). Confirmed (code): the loop keeps a candidate
   only when his squared distance is greater than the best so far, so this corrects "the nearest" in
   [The scripted tactics](ai-gangs.md#tactic-kinds). On violence nearby (event 20, `AttackTactic_OnViolence`
   `0x00307878`) the **leader** picks the side and every member with an empty stack melees that man, takes him as enemy
   and target and gets HelpRespond toward him.

### The gang as a whole {#gang}

Confirmed (code) at the cited addresses unless a line says otherwise.

1. **Alert state** (gang `+0x32`, `Gang_SetAlertState` `0x00164a28`): 1 while fighting; it installs the gang's fight
   anim substitutions and sets each AI member's brain `+0x14d` to 0 (calm 1), which lifts the turf limit on whom he
   may take ([The gang](ai-gangs.md#gang-record)). Melee sets it when the gang has no tactic; a tactic's Start sets it
   for types below `0x12`.
2. **The leader** (gang `+0x44`, `Gang_GetLeader` `0x00165678`: while alive, not a player, not down and not out of
   the fight, else another member). He counts in the score (+6 against an enemy leader), in reason 4, in
   `AttackTactic_OnViolence` (he picks the side) and he gives the team-up orders.
3. **Flight** (`GangTactic_CheckFlee` `0x00307d40`, on event 18 under TacticAttack): `AttackTactic_Start`
   (`0x00307fe0`) records the gang's living count at its start in `+0xe0` (when `GangCanFlee` `+0xdf` is set, 1 after
   a reset, and `+0xe0` is still −1). On each member going down, when the standing, uncuffed members number at most
   round(`CfgGang` v9 × that count / 100), each standing member whose class byte `+0x11b` is 11 gets threat response
   0, Melee and a PedReaction (type 9) away from his nearest enemy, and the tactic's fled flag (`+0x32`) is set (no
   more `TacMemberDied`, no more team-ups). v9 is 50 for gang type 3 only and 0 for the rest, so in practice **half of
   Rumble side 1's starting members down makes its class-11 members run**. This corrects
   [AI code](ai-code.md#t1-attack), which read `+0xe0` as a percentage.
4. **Team-ups** (TacticAttack's coordinated sub-tactics, [AI code: attack sub-tactics](ai-code.md#t2-sub-tactics)).
   `AttackTactic_StartSubTactic` (`0x00307a10`) runs 7 s after the tactic is free (no sub-tactic running, gang not
   fled): it needs `GangEnableAttackStrategies` (gang `+0xd9`, **0 after a reset**, `Gang_Reset` `0x001649c0`), a
   leader who is not busy and at least two living members. It counts `E`, the living members of the (at most two)
   nearest enemy gangs within the leader's sight range (`Gangs_FindWithinRadius` with `Gang_CanJoinFightAgainst`),
   and `L`, his own living members, then draws one of seven orders by the weights of the gang type's strategy table
   (`CfgGang`'s sixth argument, bytes `+0x73`-`+0x79`, `GangConfig_GetStrategyEntry` `0x00164610`):

   | Draw | Order | Members | What happens |
   | --- | --- | --- | --- |
   | 0 | **Charge** (10 s) | max(L / 2, L − E) | the leader says `attack` and gestures; free members drop their fight and run in on their nearest enemy (EngageEnemy) |
   | 1 | **Guard** (15 s) | the rest | the leader says `defend`; members form round him (FollowAndDefend, 3 m) with the attack-weight table `0x00511438`; at the end he says `attack` |
   | 2 | **Guard, leader backs off** (15 s) | the rest | as 1, but the leader says `follow` and backs off (FightBackOff) and the members keep their own weights |
   | 3 | **Throw from a pile** (20 s) | max(L / 2, L − E) | the leader says `throw`; members fetch from `dyn_pile` objects within 30 m |
   | 4 | **Get their leader** (15 s) | L − 1 | the leader says `attack`; members with an enemy leader in their list run at him; for its length the gang type's standing cap (v2) is **4** and the enemy-leader score term is **18** for every scorer (the global `0x00510b54`, read as an integer at `0x0029d054`) |
   | 5 | **Throw objects** (20 s) | every living member | the leader says `throw`; members fetch throwable objects (kinds 4, 5) within 30 m |
   | 6 | **Grab** (no limit) | the leader | the leader grabs the first living class-221 human (GrabTarget) |

   Weights by strategy (draws 0-6): StratNormal 20, 20, 20, 20, 20, 20, 0; StratOrphans 0, 0, 0, 5, 0, 5, 0;
   StratLizzies 0, 20, 20, 0, 0, 0, 0; StratPunks 0, 0, 0, 0, 0, 0, 20; StratTurnbulls 20, 0, 0, 20, 20, 20, 0;
   StratFuries 12, 0, 10, 0, 20, 0, 0; StratHurricanes 40, 0, 10, 0, 0, 0, 0; StratBoppers 0, 20, 20, 5, 10, 10, 0;
   StratHiHats 5, 5, 40, 10, 10, 5, 0; StratHuns 0, 10, 15, 0, 20, 0, 0. When the order ends, its End restores what it
   changed and, when it says so (`AttackTactic_OnStrategyEnd` `0x00308150`), the members get Melee again.

### The player's presence {#player}

Confirmed (code) at the cited addresses unless a line says otherwise.

1. **He is the favourite target**: +20 in every score, and +4 more when he targets the scorer.
2. **He is hit by more at once than a Warrior** is: the player's brain is type 0, so the attackers' gang raises his
   standing spacing to its v2 (2 for most gangs), while an AI Warrior (type 3) keeps 1.
3. A member busy when hit **by a player** calls within 30 m instead of 20 m, and turns his head to him.
4. Warriors (type 3) list only threats ([The enemy scan](ai-fights.md#enemy-scan)); they hold back from a boss a player
   holds
   (reason 7); the AI learns to block a player's (or Warrior's) repeated pattern (`Brain_TryPatternBlock`
   `0x00292158`, [Blocking](ai-fights.md#block)).
5. His crew follows his commands: the attack command shares his target (rule 4 of [targets](#targets)),
   defend makes them guard and rescue him, and a crew member holding an enemy presents him to the player.

### Rumble's CPU-controlled sides {#rumble}

Confirmed (code) at the cited addresses unless a line says otherwise.

From [Rumble](rumble.md#game-types) (brawl, one-player and co-op): side 2 (`GangCreate(19, "Gang2")`) gets
`TacticConfront(gang2, gang1, 10, 2, 0, -1, "AIConfrontCallback")`, whose callback switches to `TacticAttack(gang2)`
on results 1, 5 or 6. Under TacticAttack:

1. **Start** (`0x00307fe0`): every standing member without a PedReaction goal gets threat response 2, is popped to
   his base and gets Melee with no time limit (`AttackTactic_GiveMelee` `0x00307788`); the gang says `spot`; the
   flight count is noted (gang type 19 has v9 0, so side 2 never flees). `AttackTactic_ComputeSpacing` (`0x00307668`)
   stores an interval (halfwords `+0x30` = 1000 ms, or 50 ms when the first member's nearest enemy runs, and `+0x2c`
   = that divided by the living members; `+0x2c` = 300 with no enemy) that **nothing else reads**: apart from
   ComputeSpacing reading its own `+0x30` back to divide it (`lhu` at `0x00307710`), no halfword load of `+0x2c` or
   `+0x30` anywhere in the executable is on a tactic (every `lh`, `lhu` and delay-slot `_lh`, `_lhu` at those offsets
   checked; `0x0030c5a0` is a Luther goal's byte at `+0x30`; the word loads at `+0x2c` in the tactic code,
   `0x00306600`-`0x00308400`, are of vtables, brains and gangs). Confirmed (code) by that search; so it is
   write-only and Coney needs no behaviour from it.
2. **Each second** with no member listing an enemy, code 9 (`TacNoEnemies`).
3. **Every 3 s**, idle members melee and go toward the farthest busy mate ([helping](#help) rule 4).
4. **Events**: 1 and 11 (a member hit or spotting someone) give Melee to a member with no fight goal while no team-up
   runs; 2 (a member died) fires `TacMemberDied` unless fled; 18 runs the flight check; 19 and 22 give everyone Melee
   again; 20 runs `AttackTactic_OnViolence`.
5. **Team-ups** only if the gang's scripts enable them (gang `+0xd9`). **Rumble's two sides never get them.** The
   flag has two writers, `Gang_Reset` (`0x00164808`, 0, the store at `0x001649c0`) and
   `Gang_EnableAttackStrategiesById` (`0x0016a2a8`, store at `0x0016a2d0`), whose only caller is the script binding
   `GangEnableAttackStrategies` (`0x00373308`) (confirmed (code), every `sb` to `+0xd9`). In the scripts it is set
   in two ways:
   - **`GangSetup`**, the strategy library's gang constructor in `global.lua`, calls
     `GangEnableAttackStrategies(gang, true)` for every gang it makes unless the gang's table sets `AttackStratsOff`
     ([The strategy library](ai-scripting.md#script-strategies), read from the compiled scripts; the call, with
     `true`, is in `GangSetup`'s code). So in the story every gang made through `GangSetup` gets the team-ups unless
     it opts out; 11 chunks besides `global.lua` name `AttackStratsOff`.
   - **Directly**, in three story chunks only: `level2_clinic.lua` and `level55_punkfight.lua` turn them on for
     some gangs, `level93_chapter3.lua` off for four of its fights.

   That covers every compiled chunk on the disc: of all 467 (the 356 with a recovered name and the 111 without,
   so also any flag chunk `level<n>_<type>_init.lua` of the four arenas whose mode script names are not recovered),
   only `global.lua` and those three name `GangEnableAttackStrategies` (a string search of each chunk's bytes). No
   Rumble chunk names it: not the rules chunks (`brawl.lua`, `kinghill.lua`, ...), not the arena
   scripts `level101.lua`-`level137.lua`, not the 56 named flag chunks. Rumble makes its two sides with
   `GangCreate` directly (`AddRumbleGang1` and `2` in the arena scripts; [Rumble: the sides](rumble.md#sides)),
   not through `GangSetup` (inferred from the chunks' calls).
   The one Rumble gang made through `GangSetup` is level 105's Red Devil band (`brawl.lua`'s `AddRedDevilBand`, no
   `AttackStratsOff`), so its flag is set; but it is set dressing that never fights: halted, threat, damage and
   investigate responses 0, no reaction to violence, and its hit handler only changes its music and animations
   (inferred from the chunk's calls).
   The recordings agree: `+0xd9` stayed 0 for both sides and no order ran ([How this was checked](#runtime)
   rule 2).

Side 1's AI members (with the player as chief) get the forced attack command, `WCIssueCommand(player, 1, true)`:
FollowAndAttack on the player, best enemy within 60 m of him, and no shared target. Their gang type 3 lets one of
them swing at a standing man at a time (v2 1) and four at a downed one (v3 4).

## In Coney

Coney's state from reading `repo:src/ai/` (2026-10-12); each item names the rule above it implements.

1. **The periodic enemy scan for every brain** ([The enemy scan](ai-fights.md#enemy-scan)): `scanEnemies` and its
   schedule exist
   (`repo:src/ai/enemy_scan.h`) but only the Scout tactic calls them, so gang soldiers, Warriors and Rumble fighters
   learn enemies only from hits, help calls and tactics. Run `Brain_MaybeScanEnemies` from each brain's update, 5
   scan tokens an update.
2. **TacticAttack's members choose by the score**: built ([Unit 2](ai-reimpl.md#unit-2)); they run the real Melee
   goal, which picks with `Brain_PickBestEnemy`.
3. **The 3 s idle check sends the idle to the farthest busy mate** (HelpRespond after Melee): built (Unit 2).
4. **The team-ups** ([The gang as a whole](#gang) rule 4): not built (`repo:src/ai/tactic_attack.h`); with
   `GangEnableAttackStrategies`, the seven orders, their weights, counts, lines, gestures and the leader order's two
   temporary changes.
5. **The shared target**: the field exists (`Gang::chosenTarget`) but nothing sets it; the unforced attack command
   sets it from the chief's lock-on target, else from the crime reporter or the gang caller ([targets](#targets) rule
   4), and adds it to every AI member's list.
6. **The re-target forces the slot** and does not require the enemy list or validity (Coney's `retargetFromSectors`
   checks both, and its threat test is a stand-in).
7. **Waiting attackers circle and taunt**: the taunt every 1-2 s is built (2026-10-20: `FightGoal`'s timer, the
   fidget action on anim `0x25b` with the gang's combat fidgets, `cheer2`, and the voices' gesture slots;
   `repo:src/ai/fidget_action.h`); a Warrior's `turf_defend` line is not. Coney's ring only steps out to the outer
   radius and turns to face; the original's move-to-human holds them inside 4-4.75 m (they keep moving). Wait reasons
   4, 6 and 8 are never raised (their inputs are not computed).
8. **Event `0x15`'s slot-holder taunts**: built (2026-10-20, `gloatOver`, `repo:src/ai/brain_events.h`); Coney sends
   the event once a downed man lies still on the ground (no reaction clip driving him, not held), standing in for
   the original's two senders ([waiting](#waiting) rule 5).
9. **The gang's alert state** (`+0x32`): built (Unit 2) as state, the clip groups (with their names and
   `GangClips_Set`'s random subset since 2026-10-20) and the members' `+0x14d`; the waiting taunts play the combat
   fidgets and the gloating the cheers and taunts, the advancing taunts (`0x253`) have no player yet, and nothing
   reads `+0x14d` (Coney has no turf-only target test).
10. **Flight**: TacticAttack's flee check (v9 × the starting count, class 11 only) under `GangCanFlee`: built (Unit 2).
11. **The tandem on a held enemy** ([Combat moves](combat-moves.md#tandem-ai), issue #84): not built.
12. **FightBackOff** for the guard-and-back-off order and the [pusher](ai/index.md#fight-back-off-pusher): not built.

Already faithful (keep): attack slots and their displacement, active places and spacing bytes from `CfgGang` v2 and
v3, the shared swing gap, `Brain_CheckAttack`'s order, the score terms and weights, help calls and side-picking,
HelpRespond, the leader rule, TacticConfront's switch to TacticAttack.

## Open questions

What fills game state `+0x280` / `+0x284` ([targets](#targets) rule 4), who reads TacticAttack's interval (nothing,
[Rumble](#rumble) rule 1), what sends event `0x15` ([waiting](#waiting) rule 5) and whether Rumble enables the team-ups
(its sides never get them, [Rumble](#rumble) rule 5) are answered above. Where the team-ups are on in the story is
known from the scripts (`GangSetup` unless `AttackStratsOff`, and three chunks directly, [Rumble](#rumble) rule 5).

- **Team-ups in a story fight**{#q-teamups}: no recording has seen a story gang with `+0xd9` set give team-up
  orders in a fight. The `g_teamups` column of a trace in a story fight against a `GangSetup` gang would confirm
  rule 5's script reading and show how often the orders run.

## How this was checked

### Runtime checks {#runtime}

Confirmed (runtime), PCSX2 2.9.94: eight recordings of [Rumble's gang battle](rumble.md#gang-battle-trace), every
human on every update, five with the Furies as side 1 (2026-10-12 and 2026-10-13) and three with the sides swapped
([The sides swapped](rumble.md#gang-battle-swap)); the five of 2026-10-13 also ran the hooks of
`repo:research/traces/patches/gang-battle.toml`. Counts are over the fight (the brains going on to `RumbleOver`), per
minute that a fighter stands. The numbers per match are on [Rumble](rumble.md#gang-battle-results).

1. **Each side's tactic.** Side 1 runs the attack command's tactic (`WarriorAttackTactic`, vtable `0x00544160`) for
   the whole match, with FollowAndAttack (52) at the bottom of every member's stack; so side 1 never meets
   TacticAttack's idle check, events, flight or team-ups. Side 2 runs TacticConfront (vtable `0x00543740`) for its
   first 23 to 47 updates, then TacticAttack (`0x00543320`), with FindEnemy (65) at the bottom, Melee (8) above it and
   Fight (15) on top. The alert state (`+0x32`) is 1 from the brains going on (0 under TacticConfront).
2. **No shared target and no team-ups.** The shared target (`+0x10`) stayed empty and `GangEnableAttackStrategies`
   (`+0xd9`) 0 on both sides in every match, and no team-up order ran. The leader (`+0x44`) stays one fighter until he goes
   down, then passes to another (side 1 starts with `P11`).
3. **The limits.** At most 4 of a side target one man, as the 4 attack slots allow. The spacing bytes on a man are
   those of the gang type attacking him: on side 2's fighters (attacked by type 3) 1 standing and 4 downed, on side 1's
   (attacked by type 19) 2 standing and 1 or 2 downed (the downed byte is raised only by a slot taken while he is
   down). The active places held on a man stay within his byte, except in 0 to 2 % of the samples where places are
   held, nearly all within 10 s after he got up (inferred: places taken while he was down, under the downed byte,
   stay until their holders let go; `Brain_ClaimActiveAttacker` `0x00291008` only looks at the first `s` places).
4. **Fight goals last 4 s.** Every fight goal starts with 4000 ms left (a few with 500 to 2500 ms), and 24 to 74 % of
   them run to 4 s or longer (kept while an active place is held; the longest 41 s), so a member re-chooses every few
   seconds ([Target switches](#switches) rule 1).
5. **Where the waiting ones stand.** A fighter with Fight on top and no active place stands a median 0.9 to 1.1 m from
   a target who is fighting him (1 to 5 % of that time in the 4 to 4.75 m band), and 1.5 to 3.5 m from a target
   fighting someone else (6 to 25 % in the band, 29 to 67 % beyond 2.5 m). The band is where a reposition sends a
   waiter, not where waiters spend their time ([What the waiting ones do](#waiting)). Taunts (clip `0x25b`) come 0 to
   1.9 times a standing minute.
6. **What changes a target** (the `set-target` hook on `Brain_SetTarget` `0x0028cfe0`, by its caller; five matches,
   per standing minute): the enemy pick (`Brain_PickBestEnemy`, Melee's on side 2 and FollowAndAttack's on side 1,
   inferred from the goals each side runs: the hook cannot tell the two callers apart) sets one 13 to 31 times and
   clears one 7 to 28 times (it clears, then sets: a change of enemy shows as a clear and a set in one update); the
   fight goal's forced re-target (`FightGoal_Process`, rule 2 of [the switches](#switches)) switches 2 to 17 times;
   **hitting back** at the man who hit him (`GangBrain_OnEvent`, event 1, forced) 1.0 to 4.7, which side 1 does
   through `Brain_Fight` and side 2 directly; clears by `Human_DropTarget`, the Stunned goal and a slot displacement
   (`Brain_ClaimAttackSlot`) 0 to 7.5. Calls that set the same target again (most of them, through `Human_SetTarget`
   `0x00226c30`) change nothing. Every return address the recordings logged, the other call sites and each cause:
   [What each caller means](#set-target-causes). An earlier reading put side 1's `Brain_Fight` switches on its
   FollowAndAttack goal; the hook's second address shows they are all the hit-back.
7. **Help.** Help calls (the `help-call` hook on `Gang_BroadcastHelpCall` `0x00293640`) come from three sites of
   `Brain_DefaultOnEvent` `0x00292d80`, which the hook tells apart by return address: on a hit (event 1, call at
   `0x002930d0`, returning to `0x002930d8`, with an amount in `a3`), on an attack warning (event `0x10`, call
   `0x00292f00`, return `0x00292f08`) and on a **new enemy** (event `0xb`, call `0x002934f4`, return `0x002934fc`:
   unless that enemy is throwing at him, `Human_IsThrowingAt` `0x002281d8`, a 20 m call with the new-enemy flag `t0` =
   1 and `a2`, `a3` zero; confirmed (code), [the shared handler](ai-brain-types.md#shared-events) item 3). Event `0xb`
   comes from
   the enemy scan when `Brain_AddEnemy` lists an enemy for the first time, and is rare once both sides are engaged
   (inferred): 0.1 to 1.3 a standing minute a side (confirmed (runtime)). Per standing minute, five matches (three
   swapped, two not): hit and warning, the Orphans 41 to 70 and the Furies 12 to 37 (the Orphans are hit more); all
   three sites, the Orphans 41 to 71 and the Furies 13 to 38. [Rumble's comparison](rumble.md#gang-battle-metrics)
   rule 11 counts all three. Every helper sent (the `send-helper` hook on `Gang_SendHelper` `0x002b75b8`, 0.1 to 1.6 a
   minute) was sent by a hearer answering a call (`Brain_OnViolenceSeen` `0x00291960`); TacticAttack's 3 s idle check
   never sent one, because side 2's fighters never had an empty goal stack (0 to 0.5 % of the time; side 1's 0.4 to
   12.6 %).
8. **Side and gang** (the swap): the Furies won all eight matches, as side 1 and as side 2, and the Blocking goal
   and few reactions go with the Furies, while side 2 attacks more often than side 1 whichever gang it is
   ([Rumble: the sides swapped](rumble.md#gang-battle-swap)).

#### What each set-target caller means {#set-target-causes}

`Brain_SetTarget` has **102 call sites** (confirmed (code), the cross-references to `0x0028cfe0`). The five hooked
recordings logged 21 distinct return addresses, one per call site; the first table below gives those 21 (and the
second `GetItemGoal_Process` site, which a match could reach), the second table every other site.

The `set-target` hook logs `ra`, the return address into the function that called `Brain_SetTarget`, and `ra2`, the
word at `0(sp)` on entry: the caller's own stack slot 0, since the hook runs before `Brain_SetTarget` moves `sp`.
`ra2` is the caller's own return address wherever that caller saves `ra` at `0(sp)` (confirmed (code) at each
prologue's `sd ra,0x0(sp)`):

- the wrappers `Human_SetTarget` (`0x00226c40`), `Brain_Fight` (`0x0028d308`) and `Human_DropTarget`
  (`0x00226f7c`), for which `ra2` names the cause;
- `GangBrain_OnEvent` (`0x00304fcc`), `Brain_OnViolenceSeen` (`0x0029197c`), `StunnedGoal_Process` (`0x002b4bc0`),
  `SpectateGoal_End` (`0x002b4300`) and `AttackAction_Start` (`0x002fa9cc`), for which `ra2` names only the
  dispatcher that ran them, one value each in the recordings: `Brain_OnEvent` (`0x0028f928`, returning to
  `0x0028f974`), `GangBrain_OnEvent`'s call of `Brain_OnViolenceSeen` (returning to `0x003052a4`), `Goal_Process`
  (`0x0029eed8`, `0x0029ef64`), `Goal_End` (`0x0029edd8`, `0x0029ee08`) and `Action_Update` (`0x002f9e48`,
  `0x002f9ed4`). These add nothing to the cause; `ra` alone gives it.

The other logged callers save `ra` elsewhere (`Brain_PickBestEnemy` at `0x10(sp)`, `0x0029f270`; `FightGoal_Process`
at `0x30`, `EngageEnemyGoal_Process` at `0xa0`, `GetItemGoal_Process` at `0x60`, `Brain_ClaimAttackSlot` at `0x80`,
`WoundedGoal_Process` at `0x50`, `Gang_AddMember` at `0x40`, `Brain_DefaultOnEvent` and `SpectateGoal_Process` at
`0x20`, `GrabbingGoal_Process` and `GrabbedGoal_Process` at `0x10`), so for them `ra2` is a stale stack word (for
`Brain_PickBestEnemy` it varies from call to call) and means nothing. **For the next recording**: logging the word
at `sp + 0x10` as well would recover `Brain_PickBestEnemy`'s caller, since that is where it saves `ra`: `0x002aecb8`
for `MeleeGoal_Process`, `0x002bc224` for `FollowAndAttackGoal_Process` (it has 29 call sites in all), which would
replace rule 6's inferred side split with a measured one.

The functions and conditions are confirmed (code) at the call sites (each call is the instruction 8 bytes before
`ra`); "changes" counts calls that changed the brain's target, over the whole of the five hooked recordings (offsets
100 and 500 and the three swapped; `ra2` was logged in three of them, offsets 100, 500 and swapped 682), confirmed
(runtime).

| `ra` | `ra2` | Caller | Cause | Force | Changes |
| --- | --- | --- | --- | --- | --- |
| `0x0029f298` | | `Brain_PickBestEnemy` (`0x0029f230`) | **AI pick**, first step: clears the target before scoring the enemy list | 0 | 1772 clears |
| `0x0029f368` | | `Brain_PickBestEnemy` | **AI pick**: the best-scoring valid enemy ([Who goes for whom](#targets)); called by Melee (side 2) and FollowAndAttack (side 1; inferred from the goals each side runs, `ra2` cannot tell) | 0 | 2269 sets |
| `0x002b3ca8` | | `FightGoal_Process` (`0x002b3ab0`) | **re-target to the man in front**: the nearest human within 1.5 m straight ahead, a threat, no GrabTarget goal ([switches](#switches) rule 2) | 1 | 835 switches |
| `0x00305220` | `0x0028f974` | `GangBrain_OnEvent` (`0x00304fa8`) | **hit back**: event 1 from a non-friendly attacker, not busy, a fight goal already on the stack: takes the attacker as enemy and target | 1 | 116 switches, 3 sets |
| `0x0028d330` | `0x00305200` | `Brain_Fight` from `GangBrain_OnEvent` | **hit back**, as above with no fight goal on the stack: `Brain_Fight(attacker, -1, 1)` | 1 | 104 switches (plus 51 switches and 9 sets with force 1 where `ra2` was not logged) |
| `0x0028d330` | `0x00291c6c` | `Brain_Fight` from `Brain_OnViolenceSeen` (`0x00291960`) | **help response**: event 20 (a help call, [Helping](#help) rule 1) heard with no fight goal: fights the side picked by `Brain_PickSideInFight` | 0 | 10 sets (plus 4 unlogged with force 0) |
| `0x00291c80` | `0x003052a4` | `Brain_OnViolenceSeen` | **help response** with a fight goal already on the stack | 0 | 37 sets |
| `0x002afcd0` | | `EngageEnemyGoal_Process` (`0x002afa48`) | **engage re-target**: the nearest enemy he can see, every 2 s, when his own is beyond 5 m ([switches](#switches) rule 3) | 1 | 7 switches, 2 sets |
| `0x002fad04` | `0x002f9ed4` | `AttackAction_Start` (`0x002fa9a8`) | **attack action**: a queued attack aimed at a human other than the target makes him the target | 0 | 14 switches, 1 set |
| `0x002dce08` | | `GetItemGoal_Process` (`0x002dc9f8`) | **item fight**: an attack (`Brain_QueueAttack`, kind `0x11`) on a human within 1 m of the item he goes for, when `Human_CanStartAttack` allows it; the other site, ra `0x002dcb08`, targets a non-friendly human who holds the item | 0 | 2 sets |
| `0x0028e0fc` | | `Brain_ClaimAttackSlot` (`0x0028df30`) | **displaced**: a closer attacker took the last attack slot on the man; the farther holder's target is cleared ([limits](#limits) rule 1). The logged brain is the displaced one | 0 | 61 clears |
| `0x00226fd8` | `0x0028fb30` | `Human_DropTarget` from `Brain_MaybeSpotPlayers` (`0x0028fae8`) | **own state**: every think while his state holds any of `0x1f80874000` (dying, wounded, cuffed, knocked out, dead and others) | 0 | 154 clears |
| `0x00226fd8` | `0x0028c414` | `Human_DropTarget` from `Brain_OnKnockedOut` (`0x0028c3b0`) | **knocked out** ([AI](ai/index.md#reaction-goals)) | 0 | 8 clears |
| `0x00226fd8` | `0x0028c564` | `Human_DropTarget` from `Brain_TearDown` (`0x0028c4f8`) | the brain torn down (the human deleted) | 0 | none |
| `0x00226fd8` | `0x002ade80` | `Human_DropTarget` from `MeleeGoal_End` (`0x002ade60`) | **Melee ends**: its End runs when the goal is popped | 0 | 53 clears |
| `0x00226fd8` | `0x002dc884` | `Human_DropTarget` from `GetItemGoal_Start` (`0x002dc7f8`) | **goes for an item** (side 1 only in these matches) | 0 | 38 clears |
| `0x00226fd8` | `0x0030678c` | `Human_DropTarget` from `Tactic_Start` (`0x00306690`) | **new tactic**: each non-player member is popped to his goal base and drops his target (side 2's TacticConfront, then TacticAttack; side 1's at the start) | 0 | 51 clears |
| `0x00226fd8` | not logged | `Human_DropTarget` | any of the five above | 0 | 90 clears |
| `0x002b4bdc` | `0x0029ef64` | `StunnedGoal_Process` (`0x002b4bb8`) | **stunned**: no target while the Stunned goal runs | 0 | 186 clears |
| `0x002b52fc` | | `WoundedGoal_Process` (`0x002b52c0`) | wounded: no target (already cleared by the own-state drop) | 0 | none |
| `0x002b437c`, `0x002b4310` | `0x002b4310`: `0x0029ee08` | `SpectateGoal_Process` (`0x002b4330`), `SpectateGoal_End` (`0x002b42f0`) | spectating: no target | 0 | 9 clears |
| `0x00166370` | | `Gang_AddMember` (`0x00166308`) | joining a gang (the match's set-up, before the fight) | 0 | 5 clears |
| `0x00226cbc` | `0x00226f5c` | `Human_SetTarget` from `Human_SyncTargetFromBrain` (`0x00226ee8`) | the human copying the brain's own target back: never a change | 0 | none (about 330,000 calls) |
| `0x00292dfc` | | `Brain_DefaultOnEvent` (`0x00292d80`) | event 2, the brain's reset ([shared events](ai-brain-types.md#shared-events)) | 0 | none |
| `0x002b6160`, `0x002b6974`, `0x002b69e8` | | `GrabbingGoal_Process`, `GrabbedGoal_Process` | a grab's reaction goals: the grabber takes the man he holds (his pair link `+0xc4`) as target, the held man his grabber, as the mount goals do | 0 | none |

**The other 80 sites**, none of which any of the five recordings logged (confirmed (runtime)). Set or clear and the
force are read at each call (confirmed (code)); the conditions of the first group are confirmed (code) in the
functions named; for the rest the cause is the function's role as [AI goals](ai-goals.md) and [AI code](ai-code.md)
give it. `ra` is the call + 8.

*Goals and handlers a brawl's fighters run or could meet:*

| `ra` | Caller | Cause | Force |
| --- | --- | --- | --- |
| `0x002aec88` | `MeleeGoal_Process` (`0x002aebf8`) | **no slot**: with threat response 0 (brain `+0x21c`) Melee keeps its set enemy instead of picking; when that enemy is valid but his brain gives this man no attack slot (`Brain_HasAttackSlot`), the target is cleared and Melee ends. With a threat response (2 from the brain's creation) Melee picks through `Brain_PickBestEnemy` instead | 0, clear |
| `0x002bc278` | `FollowAndAttackGoal_Process` (`0x002bc198`) | **too far from the leader**: the pick is more than 60 m (squared 3600) from the goal's leader | 0, clear |
| `0x002b4af4` | `GroundedGoal_Process` (`0x002b4aa8`) | **down**: each update while he is knocked down, the target is cleared when the target is hidden in shadow (the target's brain `+0x2d4`) | 0, clear |
| `0x0030797c` | `AttackTactic_OnViolence` (`0x00307878`) | TacticAttack, event 20: each member with an empty goal stack takes the side the leader picked as enemy and target ([AI reimplementation](ai-reimpl.md#attack-on-violence)); side 2's stacks were empty 0 to 0.5 % of the time | 0, set |
| `0x002af3ac` | `AttackTargetGoal_Process` (`0x002af278`) | each update, the goal's own target once validated (a goal a script order gives) | 0, set |
| `0x002b1e88` | `ChaseGoal_Process` (`0x002b1860`) | the chased man, whenever he is in line of sight (when he is hidden in shadow, only within 2 m and when this brain sees hidden humans, `+0x2d5`); also added as an enemy | 0, set |
| `0x002b66cc` | `MountingGoal_Process` (`0x002b6638`) | the man under him (his pair link `+0xc4`) | 0, set |
| `0x002b6cb8` | `MountedGoal_Process` (`0x002b6bb8`) | the man on top of him, after the help call, when his brain is not type 5 and has a threat response | 0, set |
| `0x002bb908` | `GrabTargetGoal_Process` (`0x002bb858`) | the human to grab | 1, set |
| `0x002b92f0` | `SaveHumanGoal_Process` (`0x002b8ad8`) | a human whom `Brain_IsThreat` (`0x00290138`) calls a threat (the goal fights off attackers on its way) | 1, set |
| `0x002a3590` | `ManWeaponPileGoal_Process` | the human he throws at | 0, set |
| `0x0026a280` | `HitReact_OnSpecialEnd` (`0x0026a1f8`) | end of a special hit reaction: when the human's foe is **the player** (brain type 0) and the player's target is this human, the player's target is cleared; never an AI's | 0, clear |
| `0x0022c984`, `0x0022c230` | `Grab_Link` (`0x0022c730`), `Pair_LinkMount` (`0x0022bfd0`) | a grab or mount links: when the victim is **the player** (brain type 0), the player's target becomes the grabber | 0, set |
| `0x00226d88`, `0x00226dc8`, `0x00226b84` | `Human_SetTargetObject` (`0x00226cd0`), `Human_SetTargetFromHandle` (`0x00226af8`) | a human's own target written to his brain: callers are the player's controls (`Player_Square`, `Player_Cross`, `Player_LockOnTarget` and others) and `Human_SyncTargetFromBrain` | 0, set |
| `0x00190518` | `Human_TraceThrowAim` (`0x0018fee0`) | after the aim trace of a throw ([Objects: throws](objects.md#throws)); the condition was not traced | 1, clear |
| `0x00169b80` | `Gang_CalmMembers` (`0x00169b00`) | the gang calmed: each AI member's target and enemies cleared | 0, clear |
| `0x00260128` | `Cuff_OnCufferClipEnd` (`0x002600f0`) | the cuffer's clip ends (cops) | 0, clear |

*Other goals, brains, tactics and bosses* (missions, the street, cops, the player; not on a brawl fighter's stack):

| `ra` | Caller | Set or clear, force |
| --- | --- | --- |
| `0x002baa1c` | `StandGroundGoal_Process` | clear, 0 |
| `0x002baee8`, `0x002bb104`, `0x002bb334` | `ObjectPileGoal_End`, `ObjectPileGoal_Process` (twice) | clear 0, clear 0, set 1 |
| `0x002bd6d4`, `0x002be2b8` | `FollowAndDefendGoal_DefendCrime`, `FollowAndDefendGoal_Process` | set 1, clear 0 |
| `0x002be930`, `0x002be9e8`, `0x002bea74` | `HoldPositionGoal_Process` | clear, 0 |
| `0x002bf9d0`, `0x002bfa9c` | `WarriorVandalStealGoal_Process` | set 0, clear 0 |
| `0x002c4674` | `Goal_ReportCrimeB` | set, 1 |
| `0x002c9230`, `0x002c92c0`, `0x002c9aa8`, `0x002c9c0c`, `0x002ca040`, `0x002ca104`, `0x002ca7cc` | `HTLDefenseGoal_Process` (twice), `HTLDefenseFightGoal_Process` (twice), `HTLOffenseGoal_Process` (twice), `HTLOffenseFightGoal_Process` | clear 0, not traced 0, set 1, clear 0, clear 0, clear 0, set 1 |
| `0x002ce284` | `StealGoal_Start` | clear, 0 |
| `0x002d36f8` | `ScatterGoal_Process` | set, 0 |
| `0x002d9984` | `BoxerGoal_Process` | set, 0 |
| `0x002daaf0` | `MoveToExitFlagGoal_Resume` | clear, 0 |
| `0x002de630`, `0x002deac0`, `0x002deb0c`, `0x002dec18`, `0x002dec2c` | `FollowPlayerGoal_Suspend`, `FollowPlayerGoal_Process` (four) | clear, set, set, clear, clear; 0 |
| `0x002e20c0` | `DevilRunGoal_Pace` | set, 0 |
| `0x002e3308`, `0x002e3a10`, `0x002e3c20`, `0x002e3e4c` | `AvoidEnemiesGoal_Resume`, `AvoidEnemiesGoal_Process` (three) | clear, clear, set, set; 0 |
| `0x002e6624` | `ShopkeeperGoal_Reset` | clear, 0 |
| `0x002e89d4`, `0x002ea1f0`, `0x002ebf70` | `BigBrawlerGoal_OnAttackWarning`, `BigFighterGoal_OnAttackWarning`, `BigFighterAGoal_OnAttackWarning` | set, 1 |
| `0x002ed920`, `0x002ed958`, `0x002eddc8`, `0x002ede64` | `BigThrowerGoal_Process` | set, set, clear, clear; 0 |
| `0x002ef3a8`, `0x002ef614` | `StationaryThrowerGoal_Process` | set, clear; 0 |
| `0x002f80fc`, `0x002f8370` | `HideAndSeekGoal_Found`, `HideAndSeekGoal_OnAttackWarning` | set, 1 |
| `0x002a237c`, `0x002a2398`, `0x002a24b4`, `0x002a26ac`, `0x002a297c`, `0x002a2b50`, `0x002a2e74` | `BossDiegoGoal_Process` | set 1 (four), clear 0 (two), set 0 |
| `0x0030c284` | `BossRoofTactic_OnEvent` | set, 1 |
| `0x0031aa00`, `0x0031ae5c` | `ScoutTactic_OnEvent`, `ScoutTactic_OnViolence` | set, 0 |
| `0x0031e6b4` | `VandalizeTactic_OnDamage` | set, 1 |
| `0x002fefc4` | `CivilianBrain_Think` | clear, 0 |
| `0x00301fc0`, `0x00302668` | `CopBrain_ReportCrime`, `CopBrain_OnEvent` | set 1, set 0 |
| `0x00303644` | `PlayerBrain_Update` | clear, 0 |

So the causes rule 7 of [Rumble's comparison](rumble.md#gang-battle-metrics) needs map as follows: **AI pick**
`0x0029f298` and `0x0029f368` (a clear and a set in one update, a change of enemy); **re-target in front**
`0x002b3ca8`; **hit back** `0x00305220` and `0x0028d330` with `ra2` `0x00305200` (or, unlogged, force 1);
**help response** `0x00291c80` and `0x0028d330` with `ra2` `0x00291c6c` (or, unlogged, force 0); **engage**
`0x002afcd0`; **displaced** `0x0028e0fc`; **reactions and own state** (stunned, wounded, knocked out, down)
`0x002b4bdc`, `0x002b52fc`, `0x002b4af4` (Grounded, never logged: it runs only while he is down, but the clear's cause
is the target hiding in shadow, not his being down) and `Human_DropTarget` from `0x0028fb30` and
`0x0028c414`; **orders** (tactic start, Melee's end, an item) `Human_DropTarget` from `0x0030678c`, `0x002ade80` and
`0x002dc884`. The rows this list left out, with their bucket:

- **Reactions**: the grab's goals `0x002b6160`, `0x002b6974` and `0x002b69e8`. They make the pair partner the
  target, as the mount goals (`0x002b66cc`, `0x002b6cb8`) do, and run only while he grabs or is grabbed (confirmed
  (code), the calls at `0x002b6158`, `0x002b696c` and `0x002b69e0`; the held man only when the grabber is not
  friendly and he has a threat response, the second call a 60 % repeat of the first).
- **Hit back**: `AttackAction_Start` `0x002fad04`. It makes the victim of every attack action that starts the
  target (`if victim != self`, confirmed (code) at `0x002fa9a8`), so it changes the target only for an attack queued
  at someone else. In a brawl those are the block's punishing attack at the man whose blow he blocked
  (`Block_QueuePunish` `0x002b5718`, [Blocking](ai-fights.md#block)) and the grab and mount goals' attacks on their partner
  (`Brain_QueueAttack` from `0x002b643c`, `0x002b67ec`, `0x002b6b4c`, `0x002b6d90`); the fight goal, Melee and the
  tackle attack their own target, and the Grounded get-up attack is aimed at himself. `Brain_QueueAttack`
  (`0x0028e248`) has 38 calling functions (46 call sites); the rest are left out because they belong to goals a
  brawl's TacticAttack does not push: the bosses' and set pieces' goals (BigBrawler, BigFighter, Boxer, the HTL
  goals, ObjectThrower, ...), civilians', cops' and item and object goals, and the crew's FollowAndDefend and
  SaveHuman, which only the Defend tactic or the defend command push ([helping](#help) rule 3; their attacks would
  also be at a man attacking the defended mate). `PedReactionGoal_Process` can run in a brawl (the flight check
  pushes PedReaction type 9 under TacticAttack, [the gang](#gang) rule 3), but its one attack (`0x002aa9d0`, the
  kind 15 shove at the enemy) is only for a civilian, brain type 4 ([PedReaction](ai-goals.md#goal-ped-reaction)
  step 5), not a gang member.
  Inferred from the goal lists, not traced per call site. So a switch here is mostly an
  answer to the man who attacked him (inferred: the hook cannot tell which goal queued the attack); a trace that
  logged the queuing goal could split it from the reactions.
- **Orders**: the item sites `0x002dce08` and `0x002dcb08`. `GetItemGoal_Process` fights for the item it was sent
  for, as its Start's drop (`0x002dc884`) already counts there.
- **Pick**: Spectate `0x002b437c` and `0x002b4310`. In a brawl the Spectate goal is pushed by Melee when its pick
  found no target ([The Melee goal](ai-fights.md#melee-goal) step 3), so its clears belong to the same outcome as the pick
  (inferred: of `SpectateGoal_Init`'s five callers, `0x002b4098`, the others are `AttackTargetGoal_Process`, the
  riot cop, the dealer's wary scan and the crowd tactic, none of which a brawl fighter runs).
- **Orders**: the reset `0x00292dfc` (event 2, which empties the brain from outside: the brain's goals, actions,
  enemies and target), inferred from what it does; no recording logged it.

The sites a brawl could reach but the recordings never logged (Melee's no-slot clear `0x002aec88`,
FollowAndAttack's 60 m clear `0x002bc278`, TacticAttack's violence handler `0x0030797c`, the grab and mount
reactions) are in the second table; a comparison that meets one should count it under its cause there. Two causes
the brief of issue #273 names have no site of their own:

- **A helper sent** (`Gang_SendHelper` `0x002b75b8`) does not set a target itself: neither it nor the HelpRespond
  goal is among the 102 callers (confirmed (code)). Its caller does, on the same man, just before sending:
  `Brain_OnViolenceSeen` sets the side it picked as the target (`0x00291c80`, or `Brain_Fight` with `ra2`
  `0x00291c6c`) and then sends him toward that man (confirmed (code), the calls at `0x00291c78` and `0x00291cdc`),
  and `AttackTactic_OnViolence` does the same for an idle member (`0x0030797c`). Every send in the recordings came
  from `Brain_OnViolenceSeen` ([Runtime checks](#runtime) rule 7), so a helper sent shows in the trace as a **help
  response**. This corrects the earlier reading that the target came from Melee's later pick.
- **Re-target on the target's death**: no caller reacts to the target going down; the fight goal's validation fails,
  the goal ends and the next pick clears and sets (`0x0029f298`, `0x0029f368`), or a slot is lost (`0x0028e0fc`)
  ([switches](#switches) rule 1; inferred: none of the 102 call sites is such a handler).

### What a trace records to check these rules {#trace}

Per human and update (the field set the gang-battle trace uses; brain = `0x006d53f0` + index × `0x2f0`, gang record
`0x005e6e30` + id × `0xb10`):

| Rule | Quantities | Where |
| --- | --- | --- |
| Who goes for whom | target, enemy-list count, gang id, brain type, distance to target, same-gang count on the same target | brain `+0x124`, `+0x164` (16 handles), `+0x20c` → gang `+0x30`, `+0x04` |
| Shared target | gang's shared target | gang `+0x10` |
| How many at once | slot holders on each man, active places held, spacing bytes, swing gap, own cooldown | brain `+0x1a4` (16), `+0x1e4`, `+0x1f0` (4), `+0x14a`, `+0x14b`, `+0x1ec`, `+0x1e8` |
| Waiting | wait reason (hook `Brain_CheckAttack` `0x002906b8` result), ring distance, taunt clip `0x25b` | the hook; human record `+0x20` (clip) |
| Switches | top goal (vtable of `+0x40`[top]) and depth `+0x2c`, reaction goal `+0x3c`, fight goal `+0x10` (kind), `+0x14`, `+0x24`, `+0x28`; target changes with their caller (hook `Brain_SetTarget` `0x0028cfe0`, `ra`) | brain; the hook |
| Helping | help calls (hook `0x00293640`), helpers sent (hook `0x002b75b8`), side picked (hook `0x002912b0`), state flags of mates (grabbed `0x30`, down `0x80000`) | hooks; human record `+0x00` |
| The gang | alert `+0x32`, tactic vtable (`+0x40` → `+0x1c`), leader `+0x44`, team-ups allowed `+0xd9`, can flee `+0xdf`, start count `+0xe0`, running order (tactic `+0x10` → its vtable `+0x0c`) | gang record; the tactic |

The `humans` field set records the brain and gang quantities above (the gang's as `g_alert`, `g_tactic`, `g_leader`,
`g_chosen`, `g_teamups` and `g_order`, the team-up order running), and the hooks `set-target`, `help-call` and
`send-helper` of `repo:research/traces/patches/gang-battle.toml` log the target changes with their caller, the help
calls and the helpers sent. The wait reason (`Brain_CheckAttack`'s result) is not recorded yet: it needs a hook where
the result is returned, not at the entry.

Metrics the comparison can draw from them, per gang: the share of updates with 0, 1, 2, ... members on one enemy and
with 0, 1, 2 active places held; the mean time a member holds one target; switches per minute split by cause
(re-target, fight-goal end, engage re-target); the share of slot holders inside 4-4.75 m while waiting; taunts per
minute; help calls and helpers sent per member down; the share of members idle (empty stack); team-ups started per
minute by kind.
