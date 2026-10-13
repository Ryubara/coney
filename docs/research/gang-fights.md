# Gang fights: how a gang fights together

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), read in Ghidra on 2026-10-12;
checked at runtime on Rumble's 9-a-side brawl in PCSX2 2.9.94 on 2026-10-12 and 2026-10-13 ([Runtime checks](#runtime)).

How the members of a gang share out a big fight: who goes for whom, how many may hit one man at once, what the rest
do while they wait, when they switch, how they help each other and what the gang as a whole adds. There is **no
fight coordinator**: no object owns a brawl and hands out targets. The coordination comes from five mechanisms that
each brain meets on its own: the **target's own limits** (attack slots and active places), a **shared score** for
choosing among enemies, **help calls** (event 20), the **gang's tactic** (whose Process and events push goals on the
members) and, under TacticAttack, short **team-up orders** from the leader. The single fighter's side (the fight goal,
the attack check, the reposition, the run-in) is on [AI humans](ai.md) and only linked here. Confirmed (code) at the
addresses cited unless marked; the per-gang values are confirmed (disc) from `config_preload2.lua` through
[Gangs](../references/gangs.md).

## Who goes for whom {#targets}

1. **Each member keeps his own enemy list** (brain `+0x164`, 16). It fills from the periodic scan
   ([The enemy scan](ai.md#enemy-scan)), from being hit or warned (the attacker is added both ways by `Brain_AddEnemy`
   `0x0028d538`), from [help calls](ai.md#help-calls) and from the tactic's events. Nothing copies one member's list to
   another, except the **shared target** (rule 4).
2. **The choice is a score** ([Choosing among enemies](ai.md#enemy-score)): Melee re-picks with `Brain_PickBestEnemy`
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
   an AI (pad `+0x1b0` = −1) and not friendly, else to the AI held in game state `+0x280` or, failing that, `+0x284`
   (two handles; what fills them is not traced). `Gang_SetSharedTarget` (`0x00165b70`) accepts it only when it passes
   `Brain_ValidateEnemy` for the gang's leader and is not of a friendly gang, and then **adds it to every AI member's
   enemy list**. With +400 in the score (and the scan always re-adding it) the whole crew converges on him. A forced
   command (`WCIssueCommand(..., true)`, as Rumble uses) never sets it. No other gang sets one.

## How many hit one man at once {#limits}

Two limits on the **target**, both described in [Attack slots, active places and spacing](ai.md#attack-places):

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

## What the waiting ones do {#waiting}

A slot holder without a place, or whose own next-attack time (`+0x1e8`) has not come, gets a wait reason from
[Brain_CheckAttack](ai.md#check-attack) and [repositions](ai.md#fight-reposition):

1. **They circle in a band of 4 to 4.75 m** round the man (move-to-human, 2000 ms), the one he is fighting (his own
   target, or the first slot holder when he has none) staying close (inner radius 2 × his capsule radius). Members
   under a Defend tactic (or tactic type `0xd`) stay close too.
2. **They taunt**: once every random 1000-2000 ms, when at least 2.75 m away, fewer than two men hold slots on the
   waiter, and the target is empty-handed (11 % for reason 7), a fidget on anim `0x25b` with line `0x11`.
3. **Some reasons hold a waiter back**: 4, an empty-handed waiter does not walk into an armed man near his own
   leader; 6, a thrower does not throw through a mate in a 40° cone; 8, an empty-handed man lets a slot holder with a
   heavy object go first; 5, everyone but a cop waits 15-16 m off while the police have the man.
4. **On the run-in** (EngageEnemy) attackers fan out: the aim point is turned 9° per attack-slot index, alternating
   sides, when the target faces away ([EngageEnemy](ai.md#engage-enemy) step 12).
5. **Event `0x15`** makes every slot holder whose top goal is Fight taunt (kind 4 at his own target, kind 3 otherwise
   when he is beyond the middle of his melee ranges, his next attack delayed by twice the taunt): [The shared event
   handler](ai.md#shared-events). What sends it is not traced (open question).

## Target switches and what interrupts a fight {#switches}

1. **The fight goal ends** (and Melee re-scores, +3 for the previous target) when the target fails
   `Brain_ValidateEnemy`, when he no longer holds a slot on him (displaced, [limits](#limits) rule 1),
   when the target is beyond
   5.5 m (unless he holds a throwable), every 30 updates when no straight walkable line is left, and at its deadline:
   4000 ms for every fight goal Melee pushes ([The durations](ai.md#fight-durations)), unless at that moment he still holds
   an active place on an attackable target and has no FollowAndDefend goal. So in a long brawl **every member
   re-chooses at least every 4 s**.
2. **The re-target** (`FightGoal_Process` `0x002b3ab0`, at most once a second, `+0x14`): the first word of his
   [sector record](ai-moving.md#neighbour-sectors), refreshed when older than 1 s, which is the **nearest human within
   1.5 m in sector 0** (straight ahead, ±22.5°). When that human is someone other than the target, a threat
   (`Brain_IsThreat` `0x00290138`: an enemy gang, or both players) and no GrabTarget goal (`0x1f`) is on the stack, he
   becomes the target with the slot **forced** (`Brain_SetTarget(brain, him, 1)`). Confirmed (code); this replaces
   the "inferred" reading under [Starting a fight](ai.md#targets).
   The new target is not checked against the enemy list or
   `Brain_ValidateEnemy` there: an enemy who steps in front of a fighter takes him over, and the next update's step 2
   ends the goal if he is not valid.
3. **EngageEnemy** re-targets every 2 s to the nearest enemy he can see when the current one is beyond 5 m.
4. **Interruptions within a fight**: a block (`Goal_TryBlock` on an attack warning, [Blocking](ai.md#block)) gives up the
   active place; a [reaction goal](ai.md#reaction-goals) (hit, grabbed, tackled, down, stunned) runs above the stack and
   the fight goal resumes when it ends; a knock-out ends every goal and releases his slot and place on his target
   (`Brain_OnKnockedOut` `0x0028c3b0`); a tactic's order pops the member to his goal base and gives new goals (the
   3 s idle check, a team-up order below, the attack tactic's events 19 and 22).

## Helping a gang-mate {#help}

1. **Being hit or warned spreads the fight.** A member warned of an attack (event `0x10`) or hit (event 1) sends a
   help call within 20 m ([Help calls](ai.md#help-calls),
   [The shared event handler](ai.md#shared-events)); a busy gang member
   hit by a player calls within 30 m. Every hearer with no target, no held flags and free actions picks the side
   (`Brain_PickSideInFight` `0x002912b0`: his friend's attacker), starts a fight on him and pushes HelpRespond, which
   walks him over until he lists an enemy. A member who already has a target ignores the call: **help comes from the
   idle**.
2. **A grabbed member does not call for help** (only a civilian in the Grabbed reaction does, every 30 updates within
   10 m, [The fight reaction goals](ai.md#fight-reactions)).
   Mates are drawn by the score instead: a rear-grabbed enemy is
   +6 and +1 grabbed. The other way round, a member holding an enemy from behind **presents him** to a nearby friendly
   player ([Grabbing](ai.md#fight-reactions)), and an AI mate may start a tandem on a held enemy
   ([Combat moves: tandem](combat-moves.md#tandem-ai), issue #84).
3. **A downed or cuffed mate** is rescued only by a member running FollowAndDefend (the Defend tactic, or the
   Warriors' defend command), which pushes [SaveHuman](ai-goals.md#goal-save-human) on a cuffed or knocked-out chief;
   the Defend tactic also sends its members to the defended human on events 17 and 18. No rule was found that makes a
   plain gang soldier protect a downed mate (inferred from the callers of `Goal_SaveHuman` `0x002b7d80`). A downed man
   himself, still attacked, plays the get-up attack after about 1.9 s ([Grounded](ai.md#fight-reactions)).
4. **Under TacticAttack, idle members are sent to the fight** every 3 s (`AttackTactic_Process` `0x003081a8`): each
   member with an **empty goal stack** is popped to his base and given Melee, and then a HelpRespond goal toward the
   **farthest** member that has goals (`Gang_SendHelper` `0x002b75b8`). Confirmed (code): the loop keeps a candidate
   only when his squared distance is greater than the best so far, so this corrects "the nearest" in
   [The scripted tactics](ai-gangs.md#tactic-kinds). On violence nearby (event 20, `AttackTactic_OnViolence`
   `0x00307878`) the **leader** picks the side and every member with an empty stack melees that man, takes him as enemy
   and target and gets HelpRespond toward him.

## The gang as a whole {#gang}

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

## The player's presence {#player}

1. **He is the favourite target**: +20 in every score, and +4 more when he targets the scorer.
2. **He is hit by more at once than a Warrior** is: the player's brain is type 0, so the attackers' gang raises his
   standing spacing to its v2 (2 for most gangs), while an AI Warrior (type 3) keeps 1.
3. A member busy when hit **by a player** calls within 30 m instead of 20 m, and turns his head to him.
4. Warriors (type 3) list only threats ([The enemy scan](ai.md#enemy-scan)); they hold back from a boss a player holds
   (reason 7); the AI learns to block a player's (or Warrior's) repeated pattern (`Brain_TryPatternBlock`
   `0x00292158`, [Blocking](ai.md#block)).
5. His crew follows his commands: the attack command shares his target (rule 4 of [targets](#targets)),
   defend makes them guard and rescue him, and a crew member holding an enemy presents him to the player.

## Rumble's CPU-controlled sides {#rumble}

From [Rumble](rumble.md#game-types) (brawl, one-player and co-op): side 2 (`GangCreate(19, "Gang2")`) gets
`TacticConfront(gang2, gang1, 10, 2, 0, -1, "AIConfrontCallback")`, whose callback switches to `TacticAttack(gang2)`
on results 1, 5 or 6. Under TacticAttack:

1. **Start** (`0x00307fe0`): every standing member without a PedReaction goal gets threat response 2, is popped to
   his base and gets Melee with no time limit (`AttackTactic_GiveMelee` `0x00307788`); the gang says `spot`; the
   flight count is noted (gang type 19 has v9 0, so side 2 never flees). `AttackTactic_ComputeSpacing` (`0x00307668`)
   stores an interval (1000 ms, or 50 ms when the first member's nearest enemy runs, divided by the living members)
   whose reader was not found.
2. **Each second** with no member listing an enemy, code 9 (`TacNoEnemies`).
3. **Every 3 s**, idle members melee and go toward the farthest busy mate ([helping](#help) rule 4).
4. **Events**: 1 and 11 (a member hit or spotting someone) give Melee to a member with no fight goal while no team-up
   runs; 2 (a member died) fires `TacMemberDied` unless fled; 18 runs the flight check; 19 and 22 give everyone Melee
   again; 20 runs `AttackTactic_OnViolence`.
5. **Team-ups** only if the arena's scripts enable them (gang `+0xd9`). Rumble does not: `+0xd9` stayed 0 for both
   sides in every recorded match and no order ran ([Runtime checks](#runtime)).

Side 1's AI members (with the player as chief) get the forced attack command, `WCIssueCommand(player, 1, true)`:
FollowAndAttack on the player, best enemy within 60 m of him, and no shared target. Their gang type 3 lets one of
them swing at a standing man at a time (v2 1) and four at a downed one (v3 4).

## Runtime checks {#runtime}

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
   per standing minute): Melee's re-pick (`Brain_PickBestEnemy`) sets one 13 to 31 times and clears one 7 to 28 times
   (it clears, then sets: a change of enemy shows as a clear and a set in one update); the fight goal's forced
   re-target (`FightGoal_Process`, rule 2 of [the switches](#switches)) switches 2 to 17 times; side 1's
   FollowAndAttack fight (`Brain_Fight`, forced) 1.8 to 4.7; side 2's GangBrain events (`GangBrain_OnEvent`, forced)
   1.0 to 3.2; clears by `Human_DropTarget`, the Stunned goal and a slot displacement (`Brain_ClaimAttackSlot`) 0 to
   7.5. Calls that set the same target again (most of them, through `Human_SetTarget` `0x00226c30`) change nothing.
7. **Help.** Help calls (the `help-call` hook on `Gang_BroadcastHelpCall` `0x00293640`) come from three sites of
   `Brain_DefaultOnEvent` `0x00292d80`, which the hook tells apart by return address: on a hit (event 1, call at
   `0x002930d0`, returning to `0x002930d8`, with an amount in `a3`), on an attack warning (event `0x10`, call
   `0x00292f00`, return `0x00292f08`) and on a **new enemy** (event `0xb`, call `0x002934f4`, return `0x002934fc`:
   unless that enemy is throwing at him, `Human_IsThrowingAt` `0x002281d8`, a 20 m call with the new-enemy flag `t0` =
   1 and `a2`, `a3` zero; confirmed (code), [the shared handler](ai.md#shared-events) item 3). Event `0xb` comes from
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

## Coney needs {#coney}

Coney's state from reading `repo:src/ai/` (2026-10-12); each item names the rule above it implements.

1. **The periodic enemy scan for every brain** ([The enemy scan](ai.md#enemy-scan)): `scanEnemies` and its schedule exist
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
   sets it from the chief's lock-on target and adds it to every AI member's list.
6. **The re-target forces the slot** and does not require the enemy list or validity (Coney's `retargetFromSectors`
   checks both, and its threat test is a stand-in).
7. **Waiting attackers circle and taunt**: Coney's ring only steps out to the outer radius and turns to face; the
   original's move-to-human holds them inside 4-4.75 m (they keep moving), and the taunt every 1-2 s is not called.
   Wait reasons 4, 6 and 8 are never raised (their inputs are not computed).
8. **Event `0x15`'s slot-holder taunts** are not built (needs its sender, an open question).
9. **The gang's alert state** (`+0x32`): built (Unit 2) as state, clip-group counts and the members' `+0x14d`;
   nothing plays the clip groups or reads `+0x14d` yet (Coney has no turf-only target test).
10. **Flight**: TacticAttack's flee check (v9 × the starting count, class 11 only) under `GangCanFlee`: built (Unit 2).
11. **The tandem on a held enemy** ([Combat moves](combat-moves.md#tandem-ai), issue #84): not built.
12. **FightBackOff** for the guard-and-back-off order and the [pusher](ai.md#fight-back-off-pusher): not built.

Already faithful (keep): attack slots and their displacement, active places and spacing bytes from `CfgGang` v2 and
v3, the shared swing gap, `Brain_CheckAttack`'s order, the score terms and weights, help calls and side-picking,
HelpRespond, the leader rule, TacticConfront's switch to TacticAttack.

## What a trace records to check these rules {#trace}

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

## Open questions {#open-questions}

- What fills game state `+0x280` / `+0x284`, the attack command's fallback for the shared target.
- Who reads TacticAttack's interval `+0x2c` / `+0x30` (`AttackTactic_ComputeSpacing` `0x00307668`).
- What sends event `0x15` (the slot holders' taunts).
