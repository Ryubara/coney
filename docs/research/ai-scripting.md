# AI scripting: how the scripts drive the AI and the scripted goals {#scripting}

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra;
every claim is confirmed (code) at the cited address unless it says otherwise, as on [AI humans](ai/index.md), which
this page was split from because that page had grown too large. The goals, actions and brain types that scripts drive
are on [AI humans](ai/index.md#goals).

## How the scripts drive the AI {#script-control}

The level scripts never run AI code themselves. They work six levers, all through the bindings on [AI
bindings](../references/bindings/ai.md) and [Gang bindings](../references/bindings/gang.md). This section says what each
lever does to a brain and how the levers interact. The usage counts were read from the compiled scripts of levels 99,
80, 87, 2, 3, 5, 34 and 95 (each chunk with its chapters, without the `scenetest` files); they count each binding's name
read in the code, which is almost always a call. Confirmed (code) at the addresses cited unless marked.

### The levers and their order {#script-levers}

1. **The goal stack.** Every `Goal*` binding that the eight levels use resolves the human and calls `Brain_PushGoal`
   (`0x0028d758`) once, checked for all 27 of their workers: the new goal goes **on top**, the old top is suspended (it
   resumes when the new one ends), and the human's queued actions are cleared. No `Goal*` binding flushes first, so a
   script that wants the goal to be the only one calls `BrFlush` (or `BrFlushGoals`) just before, as the scripts usually
   do ([Goals](ai/index.md#goals), [one update](ai/index.md#update-goals)). `GoalJoinCinematic` with its last argument 1
   also runs the goal's first update at once. `GoalMelee` pushes two goals (FindEnemy, then Melee).
2. **Actions.** `ActLookAt`, `ActTurnTo` and `ActTurnToDir` queue one action behind any queued ones; the goal on top
   keeps running ([ActLookAt](#look-at)).
3. **The gang's tactic.** A `Tactic*` binding finds the gang, builds the tactic and sets it (`0x00165640`), which ends
   and frees the gang's old tactic. When it starts, **every member that is not a player is flushed** (goals and actions)
   and the tactic gives the goals ([Tactics](ai-gangs.md#tactics)). A tactic of type `0x12` or above owns its members'
   goals and gives them again on events 17, 19 and 22; under any tactic `Brain_PushFightGoal` does nothing, so a
   member's own fights come from the tactic. `TacticClear` ends and frees it (`Gang_StopTactic`). The tactic's Lua
   callback receives (gang id, code) for each non-zero Process result and event code (`TacFinished` ... `TacAnimStart`,
   [Scripted tactics](ai-gangs.md#tactic-kinds)); the strategy library [below](#script-strategies) is built on it.
4. **The brain's own handlers.** Between script calls, a brain's think and event handlers ([Brain
   types](ai-brain-types.md)) keep pushing goals of their own: a fight (`Brain_Fight`, which needs threat response
   `+0x21c` ≠ 0, and pushes nothing under a tactic), a reaction to violence (`+0x267`), an investigation (`+0x224`) and
   so on. They push on top of a scripted goal, which resumes afterwards. The script turns them off in three ways:
    - `BrDead(h, true)` / `GangBrDead(gang, true)`: the brain keeps only `Brain_UpdateGoals` and no think or event
      handler, so **only what the script pushed runs** ([Handlers](ai/index.md#handlers)). Actions are cleared; goals stay.
    - `BrSetThreatResponse(h, 0)` / `GangSetThreatResponse(gang, 0)`: no fights; `BrSetReactToViolence(h, false)`: no
      reaction to violence; `BrSetInvestigateResponse` / `GangSetInvestigateResponse` set how it answers noises
      (`+0x224`, [Investigate](ai-goals.md#goal-investigate)).
    - Suspension, below.
5. **Freezing.** `BrSuspend(h, on)` (`Brain_SetSuspended` `0x002923a0`) clears the human's actions and sets brain
   `+0x0a`; `GangSuspend(gang, on)` sets gang `+0xd4`. While either holds, `Brain_UpdateGoals` keeps only the time: no
   goal, reaction goal or action runs ([one update](ai/index.md#update-goals); the brain's own flag counts only while the
   human is not held).
6. **Relations and perception.** `GangMakeEnemies` / `GangMakeFriends` (both ways), `GangMakeEnemiesOfType`,
   `GangMakeNeutralOfType`, `GangSetTargetable` (brain `+0x120`), `GangSetAlwaysSeen`, `BrSetFOV`, `GangSetHearRange`
   and `GangSetRespondPercentage` change whom the [enemy scan](ai-fights.md#enemy-scan) lists and who answers violence;
   they push nothing themselves.

**Members joined later.** Every `Gang*` setter that writes brains (threat, investigate, targetable, world flags, dead,
flush) walks the gang's **current** members only (`0x0016b3d0`, `0x0016b4f0`, `0x0016bac0`, `0x0016be30`, `0x0016aa98`,
`0x0016ba18`). A human added afterwards (`GangAddMember`, or a spawner) keeps his own values (threat response 2 from
`Brain_Init`). Spawned members are configured by the spawner's Lua callback instead (`GangCallback`
[below](#script-strategies)).

**Order matters.** Because a tactic's Start flushes its members, a goal pushed before `Tactic*` is lost, and a goal
pushed after it is replaced the next time the tactic re-gives goals (types `0x12` and above). Scripts that script a gang
member therefore clear the tactic first (`TacticClear`, 43 uses) and often make the gang dead (inferred from the call
order).

### Brain and gang bindings in effect {#script-binding-effects}

| Binding | Worker | What it does to the brain |
| --- | --- | --- |
| `BrFlush` | `0x00292530` | ends and frees every goal (callbacks fire, "completed" 0), then clears the actions |
| `BrFlushGoals` | `0x00292618` | the goals only (`Brain_ClearGoals`) |
| `BrDead(h, on)` | `0x00292330` | `+0x09`; clears the actions, reinstalls the handlers ([Handlers](ai/index.md#handlers)) |
| `BrSuspend(h, on)` | `0x002923a0` | clears the actions; `+0x0a` (the update is skipped) |
| `BrSetType(h, t)` | `0x00292410` | stores the type and installs that type's handlers (`0x0028ceb8`, `0x0028c1a8`); level 95 makes humans type 2 (gang) |
| `BrSetPedType(h, t)` | `0x00292460` | `+0x26c`; 3 makes a civilian fight back ([Civilians](ai-brain-types.md#think-civilian)) |
| `BrSetThreatResponse` | `0x00292708` | `+0x21c` (0 never fights) |
| `BrSetInvestigateResponse` | `0x002927a8` | `+0x224` |
| `BrSetReactToViolence` | `0x00292bc8` | `+0x267` |
| `BrSetThugWantsWeapon` | `0x00292848` | `+0x265` (may fetch weapons, [Objects](ai/index.md#ai-objects)) |
| `BrCanUseWorldFlags` | `0x00292c10` | `+0x2d1` / `+0x2d2` ([World flags](ai/index.md#ai-flags)) |
| `BrSetFOV` | `0x002928d8` | degrees to radians into `+0x12c` |
| `BrSetNumFollowSlots`, `BrSetFollowSlotSet`, `BrSetFollowSlot` | `0x00292a60`, `0x00292ac0`, `0x00292b10` | the formation slots round a leader ([Formations](ai-gangs.md#formations)) |
| `SetInterrogateParam` | `0x002854b0` | overrides the interrogation values (level 99's lesson) |
| `GangCreate` | `0x0016a1c8` | a gang record of the given type (at most 10 members, 16 for police and type `0x17`); no brain changes |
| `GangAddMember(gang, h)` | `0x0016a3f8` | moves the human into the gang: his actions, target and goals are cleared as he leaves the old one, then his brain's gang `+0x20c` is set ([gang bindings](../references/bindings/gang.md#gangaddmember)) |
| `GangSuspend` | `0x0016a220` | gang `+0xd4` (members' updates skipped) |
| `GangBrDead` | `0x0016aa98` | `Brain_SetDead` on each current member |
| `GangBrFlush` | `0x0016ba18` | clears each current member's goals and actions |
| `GangSetThreatResponse` / `GangSetInvestigateResponse` | `0x0016b3d0` / `0x0016b4f0` | `+0x21c` / `+0x224` of current members |
| `GangSetTargetable` | `0x0016bac0` | `+0x120` of current members |
| `GangCanUseWorldFlags` | `0x0016be30` | `+0x2d1` / `+0x2d2` of current members |
| `GangInvincible` | `0x0016a260` | current and later members invincible (human flag `0x10`, gang `+0xd8`) |
| `GangEnableAttackStrategies` | `0x0016a2a8` | gang `+0xd9` (the attack tactic's sub-tactics, [Attack sub-tactics](ai-code.md#t2-sub-tactics)) |
| `GangEngageEnemy(gang, h)` | `0x0016a870` | pushes EngageEnemy on `h` for every member |
| `GangExitWorld(gang, flag, fn, delete)` | `0x0016a670` | pushes MoveToExitFlag (gait 4) to the flag, or to the farthest exit flag (activity 8) ahead of player 1, on every member; with `delete`, the gang is freed once they are gone |
| `GangSetMsgHandler` | `0x0016ab38` | a Lua handler per event id ([Events](ai-gangs.md#gang-events)) |
| `GangMakeEnemies` / `GangMakeFriends` | `0x0016acf0` / `0x0016ad80` | both gangs' enemy and friend masks |

### The strategy library {#script-strategies}

`global.lua` and a library copied into each of `level2`, `level3`, `level5` and `level87` (byte-identical transition
tables in all four) run gangs as a script-side state machine on top of the tactics. Read from the compiled scripts; what
the bindings then do is confirmed (code) as cited.

- **`Super[gang]`** is a table per gang. `GangSetup(def, constructor, …)` makes the gang with `def.Function`, stores the
  table and applies its fields: `Turf`, `Friends`, `Enemies`, `GangDeadMsg` (through `DefaultGang`); `Speed` (default
  1); `SetThreat` → `GangSetThreatResponse`; `Investigate` → `GangSetInvestigateResponse` (−1 when unset or 0);
  `WFlagPercent` → `GangCanUseWorldFlags(gang, true, n)`; `Leader` → `GangSetLeader`; `Wanted` → a crime of kind 6 at
  the leader (`CrimeIsHappening`); `GangNet`; and, unless `AttackStratsOff`, `GangEnableAttackStrategies(gang, true)`.
  Unless `Halt` is set it starts `DefaultTactic` (default `HANGINGOUT`).
- **Spawned members** run `GangCallback(h, gang, …)` (the spawner's Lua callback): the first becomes the `Leader` (and
  with `Wanted` the crime is raised again), the `DefaultTactic` is started again unless `Halt`, `WFlagPercent` gives
  `BrCanUseWorldFlags(h, true, 99)`, then `OnSpawn`. `GangStartTactic(gang)` starts the `DefaultTactic` and sets threat
  response 1 on the current members.
- **The tactic wrappers** (`ATTACK`, `AVOID`, `CONFRONT`, `CROWD`, `DEFEND`, `HANGINGOUT`, `HOLDTHELINE`, `HOMEIN`,
  `IDLE`, `MANPILE`, `MOVETOFLAG`, `PURSUE`, `STEAL`, `TRAVELPATH`, `TRAVERSEFLAGNET`, `USEFLAG`, `VANDALIZE`,
  `WALKINGTALL`, `WANDERWALK`) each take the gang, fill unset `Super` fields with defaults, call one `Tactic*` with the
  callback name `"Strategy"`, and store their own function in `Super[gang].state`. Each sets a default `StrategyPreset`
  when none is set (`StratHunter` for most; `StratAvoid`, `StratDefend`, `StratHoldLine`, `StratHomeIn`, `StratManPile`,
  `StratTravel`, `StratFlagNet`, `StratWorldFlag`, `StratVandalize`, `StratWalkTall` and `StratRunAround` for their own
  tactic). `DEFEND` defends `Super.object` (2.25 m) while it lives, else attacks; `HOMEIN` moves to the player's
  position; `FLEE` clears the tactic and sends each living member out of the world.
- **`Strategy(gang, code)`**, called by every tactic's callback: a gang with no members has its tactic cleared and its
  `Super` entry dropped. Otherwise `IdleCallback` runs when set, then `StrategyPreset(gang, code, state)` (or
  `StratHunter`). A preset builds a table from code to the next wrapper, by the current state, and calls the entry for
  `code`. A state the preset does not list takes **any** code to one wrapper (`ATTACK` for most presets, `DEFEND` for
  `StratDefend`, `MANPILE` for `StratManPile`). In a listed state, a code with no entry, or with an entry set to nil
  (shown as "stay"), changes nothing; except that `StratAggressive`, `StratPassive` and `StratPussy` then go to
  `CONFRONT` unless already there.

The transitions of the presets (codes as `TacticGetString` names them; 13 is `TacMemberDied`, 12 `TacBehindLine`, 11
`TacHumanToDefendDead`, 9 `TacNoEnemies`):

| Preset | State | Code → next |
| --- | --- | --- |
| `StratHunter` (the default) | `HANGINGOUT` | 3 SeeEnemy → `CONFRONT`; 5 Damage, 6 Attacked → `ATTACK`; 4 SeePlayer stay |
| | `MOVETOFLAG` | as `HANGINGOUT`, and 8 Arrived → `HANGINGOUT` |
| | `IDLE` | 3, 5, 6 → `ATTACK`; 4 stay |
| | `CONFRONT` | 1 Finished, 5, 6 → `ATTACK`; 2 TimeOut → `PURSUE`; 9 → `HANGINGOUT`; 7 InRange stay |
| | `ATTACK` | 9 → `PURSUE`; 13 → `ATTACK` (started again) |
| | `PURSUE` | 5, 6, 7 → `ATTACK`; 9 → `HANGINGOUT` |
| | any other | any code → `ATTACK` |
| `StratAggressive` | `ATTACK` | 9 → `PURSUE`; 13 → `ATTACK` |
| | `PURSUE` | 5, 6, 7 → `ATTACK` |
| | `CONFRONT` | 1, 5, 6 → `ATTACK`; 2 → `PURSUE`; 7 stay |
| | any other | 3, 7 → `CONFRONT`; 5, 6 → `ATTACK`; other codes → `CONFRONT` |
| `StratPassive` | `ATTACK` | 13 → `ATTACK` |
| | `CONFRONT` | 1, 5, 6 → `ATTACK`; 7 stay |
| | any other | as `StratAggressive` |
| `StratPursue` | `ATTACK` | 9 → `PURSUE` |
| | `PURSUE` | 5, 6, 7 → `ATTACK` |
| `StratAvoid` | `HANGINGOUT` | 3 → `AVOID`; 5, 6 → `ATTACK`; 4 stay |
| | `AVOID` | 5, 6 → `ATTACK` |
| | `ATTACK` | 9 → `HANGINGOUT`; 13 stay |
| `StratAlwaysAvoid` | any | any code → `AVOID` |
| `StratHoldLine` | `HANGINGOUT` | 3 → `HOLDTHELINE`; 5, 6 → `ATTACK` |
| | `HOLDTHELINE` | 9 → `HANGINGOUT`; 12 → `ATTACK` |
| | `ATTACK` / `PURSUE` | as `StratHunter` |
| `StratDefend` | `DEFEND` | 9 → `DEFEND` (again); 11 → `ATTACK` |
| | `ATTACK` | 9 → `DEFEND`; 13 stay |
| `StratManPile` | `HANGINGOUT` | 3, 5, 6 → `MANPILE` |
| | `MANPILE` | 1, 5, 6 → `ATTACK`; 13 stay |
| | `ATTACK` | 9 → `MANPILE`; 13 stay |
| `StratPussy` | `CONFRONT` | 1, 5, 6 → `FLEE`; 7 stay |
| | any other | 3, 7 → `CONFRONT`; 5, 6 → `FLEE` |
| `StratShadowEnemy` | `HANGINGOUT` | as `StratHunter` |
| | `CONFRONT` | 1, 2, 5, 6 → `ATTACK`; 9 → `PURSUE` |
| | `ATTACK` | 9 → `HANGINGOUT` |
| | `PURSUE` | 5, 6, 7 → `ATTACK`; 9 → `HANGINGOUT` |
| `StratTravel`, `StratFlagNet`, `StratRunAround`, `StratVandalize`, `StratGangNet`, `StratHomeIn` | the moving state (`TRAVELPATH`, `TRAVERSEFLAGNET`, `WANDERWALK`, `VANDALIZE`, `MOVETOFLAG`, `HOMEIN`) | 3 → `CONFRONT`; 5, 6 → `ATTACK`; `VANDALIZE` 1 → `ATTACK`; `HOMEIN` 8 → `HANGINGOUT`; `MOVETOFLAG` 8 → the gang's next flag (`sNEXTGANGFLAG`) |
| | `CONFRONT` | 1, 5, 6 → `ATTACK`; 2 → `PURSUE` (`StratHomeIn`: `HOMEIN`); 9 → the moving state |
| | `PURSUE` | 5, 6, 7 → `ATTACK`; 9 → the moving state |
| | `ATTACK` | 9 → `PURSUE` (`StratRunAround`, `StratVandalize`: the moving state) |
| `StratWalkTall` | `WALKINGTALL` | 5, 6, 7 → `ATTACK`; 8 → `HANGINGOUT` |
| | `HANGINGOUT` | 3 → `WALKINGTALL`; 5, 6 → `ATTACK` |
| `StratWorldFlag` | `USEFLAG` | 7 → `ATTACK` |
| | `ATTACK` / `PURSUE` | 9 → `USEFLAG` |

So a level's gangs mostly loiter (`HANGINGOUT`), square up to an enemy they see (`CONFRONT`), fight when hit or when the
confrontation ends (`ATTACK`), chase when their enemies are gone (`PURSUE`) and go back to loitering when the chase
finds nobody. An implementer needs the tactics and the callback codes; the library itself runs as script.

### Usage by level {#script-usage}

Times each binding is named in the level's scripts:

| Binding | 99 | 80 | 87 | 2 | 3 | 5 | 34 | 95 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| [`GoalMoveToFlag`](../references/bindings/ai.md#goalmovetoflag) | 14 | 10 | 7 | 17 | 12 | 4 | 1 | 12 |
| [`GoalPlayDynIdle`](../references/bindings/ai.md#goalplaydynidle) | - | - | 4 | - | 7 | - | - | 17 |
| [`GoalBumLogic`](../references/bindings/ai.md#goalbumlogic) | 2 | 2 | 7 | 3 | - | 4 | - | 3 |
| [`GoalMoveToUseFlag`](../references/bindings/ai.md#goalmovetouseflag) | 3 | 1 | 3 | - | 2 | 9 | - | - |
| [`GoalPlayDynAnimation`](../references/bindings/ai.md#goalplaydynanimation) | 6 | - | 4 | 1 | - | - | - | 7 |
| [`GoalFight`](../references/bindings/ai.md#goalfight) | 3 | 2 | - | 1 | - | - | 10 | 1 |
| [`GoalBackoff`](../references/bindings/ai.md#goalbackoff) | - | - | - | - | 10 | 6 | - | - |
| [`GoalMoveToExitFlag`](../references/bindings/ai.md#goalmovetoexitflag) | - | 2 | - | 1 | - | - | 11 | - |
| [`GoalAddressPerson`](../references/bindings/ai.md#goaladdressperson) | 5 | - | 1 | 4 | - | - | - | 2 |
| [`GoalDealer`](../references/bindings/ai.md#goaldealer) | 2 | - | 2 | 1 | 1 | 1 | 1 | 4 |
| [`GoalBigLedgeThrower`](../references/bindings/ai.md#goalbigledgethrower) | - | - | - | - | 7 | - | - | - |
| [`GoalBoxer`](../references/bindings/ai.md#goalboxer) | - | - | - | - | - | - | - | 6 |
| [`GoalRiot`](../references/bindings/ai.md#goalriot) | - | - | - | - | - | - | 5 | - |
| [`GoalRunCarrotRun`](../references/bindings/ai.md#goalruncarrotrun) | - | - | - | - | - | 5 | - | - |
| [`GoalTravelPath`](../references/bindings/ai.md#goaltravelpath) | - | 3 | - | - | - | 2 | - | - |
| [`GoalDevilRun`](../references/bindings/ai.md#goaldevilrun) | - | - | - | - | 4 | - | - | - |
| [`GoalAreaWalker`](../references/bindings/ai.md#goalareawalker) | - | - | - | - | - | - | 2 | 1 |
| [`GoalPlayGenAnim`](../references/bindings/ai.md#goalplaygenanim) | - | - | - | - | - | 1 | - | 2 |
| [`GoalThrowObject`](../references/bindings/ai.md#goalthrowobject) | - | - | 1 | - | - | - | 1 | 1 |
| [`GoalGuardFlag`](../references/bindings/ai.md#goalguardflag) | - | - | - | - | 2 | - | - | - |
| [`GoalJoinCinematic`](../references/bindings/ai.md#goaljoincinematic) | - | 2 | - | - | - | - | - | - |
| [`GoalMelee`](../references/bindings/ai.md#goalmelee) | - | - | 2 | - | - | - | - | - |
| [`GoalTag`](../references/bindings/ai.md#goaltag) | - | - | - | - | 2 | - | - | - |
| [`GoalBumLogicTrigger`](../references/bindings/ai.md#goalbumlogictrigger) | - | - | 1 | - | - | - | - | - |
| [`GoalGrabTarget`](../references/bindings/ai.md#goalgrabtarget) | - | - | - | - | - | - | - | 1 |
| [`GoalLeadChase`](../references/bindings/ai.md#goalleadchase) | - | - | - | 1 | - | - | - | - |
| [`GoalPeddler`](../references/bindings/ai.md#goalpeddler) | - | - | - | - | - | - | - | 1 |
| [`GoalShopkeeper`](../references/bindings/ai.md#goalshopkeeper) | - | - | - | - | - | - | - | 1 |
| [`GoalStationaryThrower`](../references/bindings/ai.md#goalstationarythrower) | - | - | - | - | - | - | 1 | - |
| [`GoalTrackHuman`](../references/bindings/ai.md#goaltrackhuman) | 1 | - | - | - | - | - | - | - |
| [`ActLookAt`](../references/bindings/ai.md#actlookat) | 7 | - | 2 | - | - | 3 | 1 | 18 |
| [`ActTurnTo`](../references/bindings/ai.md#actturnto) | - | - | - | - | - | 1 | - | - |
| [`ActTurnToDir`](../references/bindings/ai.md#actturntodir) | - | - | - | 1 | - | - | - | - |
| [`BrFlush`](../references/bindings/ai.md#brflush) | 17 | 16 | 13 | 12 | 19 | 9 | 6 | 48 |
| [`BrDead`](../references/bindings/ai.md#brdead) | 8 | 21 | 9 | 5 | 21 | - | 1 | 22 |
| [`BrSetThreatResponse`](../references/bindings/ai.md#brsetthreatresponse) | - | - | 4 | 2 | - | 6 | 19 | 7 |
| [`BrSetInvestigateResponse`](../references/bindings/ai.md#brsetinvestigateresponse) | - | - | 1 | 2 | - | 1 | 10 | 11 |
| [`BrSetReactToViolence`](../references/bindings/ai.md#brsetreacttoviolence) | - | - | - | 2 | - | 4 | 10 | 9 |
| [`BrSetThugWantsWeapon`](../references/bindings/ai.md#brsetthugwantsweapon) | 1 | 9 | 4 | 2 | 3 | 1 | - | 5 |
| [`BrSuspend`](../references/bindings/ai.md#brsuspend) | 8 | - | - | - | - | 2 | - | 7 |
| [`BrSetType`](../references/bindings/ai.md#brsettype) | - | - | - | - | 1 | - | - | 10 |
| [`BrFlushGoals`](../references/bindings/ai.md#brflushgoals) | - | - | - | - | - | 10 | - | - |
| [`BrSetFollowSlot`](../references/bindings/ai.md#brsetfollowslot) | 8 | - | - | - | - | - | - | - |
| [`BrSetPedType`](../references/bindings/ai.md#brsetpedtype) | - | - | - | - | - | 2 | 2 | - |
| [`BrSetFOV`](../references/bindings/ai.md#brsetfov) | - | - | 2 | - | - | - | - | - |
| [`BrCanUseWorldFlags`](../references/bindings/ai.md#brcanuseworldflags) | - | - | - | - | - | 1 | - | - |
| [`BrHasEnemies`](../references/bindings/ai.md#brhasenemies) | - | - | - | - | - | - | - | 1 |
| [`BrSetFollowSlotSet`](../references/bindings/ai.md#brsetfollowslotset) | 1 | - | - | - | - | - | - | - |
| [`BrSetNumFollowSlots`](../references/bindings/ai.md#brsetnumfollowslots) | 1 | - | - | - | - | - | - | - |
| [`GangCreate`](../references/bindings/gang.md#gangcreate) | 15 | 19 | 50 | 37 | 40 | 47 | 36 | 59 |
| [`GangSetMsgHandler`](../references/bindings/gang.md#gangsetmsghandler) | 1 | 8 | 24 | 33 | 13 | 7 | 11 | 27 |
| [`GangSuspend`](../references/bindings/gang.md#gangsuspend) | 8 | 9 | 7 | 15 | 30 | 16 | 20 | 3 |
| [`GangStartSpawner`](../references/bindings/gang.md#gangstartspawner) | - | - | 10 | 9 | 11 | 7 | 33 | 31 |
| [`GangMakeEnemies`](../references/bindings/gang.md#gangmakeenemies) | 5 | 6 | 2 | 9 | 11 | 1 | 43 | 5 |
| [`GangBrDead`](../references/bindings/gang.md#gangbrdead) | 33 | 3 | 16 | 6 | 2 | 5 | 9 | 1 |
| [`GangAddSpawner`](../references/bindings/gang.md#gangaddspawner) | 1 | - | 13 | 7 | 7 | 9 | 11 | 23 |
| [`GangSetInvestigateResponse`](../references/bindings/gang.md#gangsetinvestigateresponse) | - | - | 11 | 8 | 5 | 13 | 5 | - |
| [`GangAddMember`](../references/bindings/gang.md#gangaddmember) | - | 9 | 2 | 3 | - | 9 | - | 5 |
| [`GangAddTurfBox`](../references/bindings/gang.md#gangaddturfbox) | - | 6 | - | 6 | - | - | 10 | 1 |
| [`GangMakeFriends`](../references/bindings/gang.md#gangmakefriends) | 4 | - | - | 6 | - | - | 12 | 1 |
| [`GangExitWorld`](../references/bindings/gang.md#gangexitworld) | - | - | 1 | 1 | - | 2 | 11 | 4 |
| [`GangBrFlush`](../references/bindings/gang.md#gangbrflush) | 2 | 4 | - | - | 3 | 3 | 3 | 1 |
| [`GangGetHeadCount`](../references/bindings/gang.md#ganggetheadcount) | - | - | 6 | - | 3 | 4 | - | 3 |
| [`GangSetThreatResponse`](../references/bindings/gang.md#gangsetthreatresponse) | 5 | - | - | 2 | - | 2 | 3 | - |
| [`GangCanFlee`](../references/bindings/gang.md#gangcanflee) | - | - | - | 6 | - | - | - | 5 |
| [`GangRemoveTurfBox`](../references/bindings/gang.md#gangremoveturfbox) | - | 3 | - | - | - | - | 6 | - |
| [`GangGetLeader`](../references/bindings/gang.md#ganggetleader) | - | - | - | - | 8 | - | - | - |
| [`GangIsASpawner`](../references/bindings/gang.md#gangisaspawner) | - | - | - | - | - | - | - | 7 |
| [`GangClearHandlers`](../references/bindings/gang.md#gangclearhandlers) | - | - | - | - | - | - | - | 6 |
| [`GangIsWanted`](../references/bindings/gang.md#gangiswanted) | - | - | 2 | - | - | 1 | - | 3 |
| [`GangSetMaxConcurrent`](../references/bindings/gang.md#gangsetmaxconcurrent) | - | - | - | - | - | - | 6 | - |
| [`GangCanUseWorldFlags`](../references/bindings/gang.md#gangcanuseworldflags) | - | - | - | 3 | - | 1 | - | 1 |
| [`GangSetSpawnerMustBeOffScreen`](../references/bindings/gang.md#gangsetspawnermustbeoffscreen) | - | - | - | - | 4 | - | 1 | - |
| [`GangGetStandingCount`](../references/bindings/gang.md#ganggetstandingcount) | - | 2 | 1 | - | - | - | - | 1 |
| [`GangMakeEnemiesOfType`](../references/bindings/gang.md#gangmakeenemiesoftype) | - | - | - | - | 4 | - | - | - |
| [`GangMakeNeutralOfType`](../references/bindings/gang.md#gangmakeneutraloftype) | - | - | - | - | - | - | - | 4 |
| [`GangSetHearRange`](../references/bindings/gang.md#gangsethearrange) | - | - | 4 | - | - | - | - | - |
| [`GangSetRespondPercentage`](../references/bindings/gang.md#gangsetrespondpercentage) | - | - | 4 | - | - | - | - | - |
| [`GangClearBums`](../references/bindings/gang.md#gangclearbums) | - | - | - | - | - | - | - | 3 |
| [`GangClearWanted`](../references/bindings/gang.md#gangclearwanted) | - | - | - | - | - | 1 | - | 2 |
| [`GangEnableAttackStrategies`](../references/bindings/gang.md#gangenableattackstrategies) | - | - | - | 3 | - | - | - | - |
| [`GangClearResponders`](../references/bindings/gang.md#gangclearresponders) | - | - | 2 | - | - | - | - | - |
| [`GangEngageEnemy`](../references/bindings/gang.md#gangengageenemy) | - | 2 | - | - | - | - | - | - |
| [`GangSetAlwaysSeen`](../references/bindings/gang.md#gangsetalwaysseen) | - | - | - | - | 1 | 1 | - | - |
| [`GangGoodToGo`](../references/bindings/gang.md#ganggoodtogo) | - | - | - | - | - | - | - | 1 |
| [`GangInvincible`](../references/bindings/gang.md#ganginvincible) | 1 | - | - | - | - | - | - | - |
| [`GangSetLeader`](../references/bindings/gang.md#gangsetleader) | - | - | - | - | - | - | - | 1 |
| [`GangSetTargetable`](../references/bindings/gang.md#gangsettargetable) | 1 | - | - | - | - | - | - | - |
| [`TacticClear`](../references/bindings/ai.md#tacticclear) | - | 4 | 3 | 10 | 3 | 6 | 6 | 11 |
| [`TacticAttack`](../references/bindings/ai.md#tacticattack) | - | - | 2 | 2 | 4 | 2 | 10 | 4 |
| [`TacticConfront`](../references/bindings/ai.md#tacticconfront) | - | - | 1 | 1 | 3 | 1 | - | 11 |
| [`TacticMoveToFlag`](../references/bindings/ai.md#tacticmovetoflag) | - | - | 4 | 3 | 6 | 3 | - | - |
| [`TacticCrowd`](../references/bindings/ai.md#tacticcrowd) | 2 | - | 1 | 3 | 2 | 1 | - | - |
| [`TacticHanginOut`](../references/bindings/ai.md#tactichanginout) | - | - | 2 | 2 | 2 | 3 | - | - |
| [`TacticScout`](../references/bindings/ai.md#tacticscout) | - | - | 9 | - | - | - | - | - |
| [`TacticTravelPath`](../references/bindings/ai.md#tactictravelpath) | - | - | 2 | 2 | 2 | 2 | - | 1 |
| [`TacticWalkinTall`](../references/bindings/ai.md#tacticwalkintall) | - | - | 2 | 2 | 2 | 2 | - | - |
| [`TacticAvoidEnemies`](../references/bindings/ai.md#tacticavoidenemies) | - | - | 1 | 2 | 3 | 1 | - | - |
| [`TacticUseFlag`](../references/bindings/ai.md#tacticuseflag) | - | 1 | 3 | 1 | 1 | 1 | - | - |
| [`TacticVandalize`](../references/bindings/ai.md#tacticvandalize) | - | - | 1 | 1 | 1 | 1 | 2 | - |
| [`TacticWander`](../references/bindings/ai.md#tacticwander) | - | - | 1 | 1 | 1 | 1 | - | 2 |
| [`TacticDefend`](../references/bindings/ai.md#tacticdefend) | - | - | 1 | 1 | 2 | 1 | - | - |
| [`TacticIdle`](../references/bindings/ai.md#tacticidle) | - | - | 1 | 1 | 1 | 1 | - | 1 |
| [`TacticPursue`](../references/bindings/ai.md#tacticpursue) | - | - | 1 | 1 | 1 | 1 | - | 1 |
| [`TacticSteal`](../references/bindings/ai.md#tacticsteal) | - | - | 1 | 1 | 1 | 1 | 1 | - |
| [`TacticHoldTheLine`](../references/bindings/ai.md#tacticholdtheline) | - | - | 1 | 1 | 1 | 1 | - | - |
| [`TacticManWeaponPile`](../references/bindings/ai.md#tacticmanweaponpile) | - | - | 1 | 1 | 1 | 1 | - | - |
| [`TacticBossScenarioA`](../references/bindings/ai.md#tacticbossscenarioa) | - | - | - | - | - | 3 | - | - |
| [`TacticTrigger`](../references/bindings/ai.md#tactictrigger) | - | - | - | 1 | - | - | - | - |
| [`SetInterrogateParam`](../references/bindings/ai.md#setinterrogateparam) | 2 | - | - | - | - | - | - | - |

What each level does with them, from the call sites (arguments read from the compiled scripts):

- **99 (the first mission)**: the crew and the lesson gangs are scripted dead (`GangBrDead`, 33) and walked with
  `GoalMoveToFlag`; `GoalAddressPerson` starts the teacher's scenes; the sparring partners fight with `GoalFight(h,
  player, 0)` after `GangSetThreatResponse(gang, 2)`; player 2 is frozen with `BrSuspend` during the lessons; the
  player's follow slots are laid out with `BrSetNumFollowSlots(player, 4)` and `BrSetFollowSlot` at 1 m, then 2 m;
  `SetInterrogateParam` sets and resets the interrogation lesson's values; `GoalTrackHuman(h, player, 0.5)` keeps a
  watcher on his spot (0.5 m) tracking the player; `GoalDealer` and `GoalBumLogic` run the dealer and the bum ([The
  first mission's cast](ai/index.md#level99)).
- **80**: turf boxes (`GangAddTurfBox`, `GangRemoveTurfBox`) bound each fight; `GangEngageEnemy` sends a gang of cops at
  one human; `GoalTravelPath` walks cops and a bum along paths; `GoalJoinCinematic` puts two humans into the kiss scene;
  the destroyers fight the player with `GoalFight(h, player, −1)`; `BrDead` (21) scripts the actors.
- **87**: the strategy library; the stealth chapter gives its guards `TacticScout(gang, 10, 0, 10, 0, 20,
  "ch5.StealthSpotted")`, `GangSetHearRange(gang, 1, 3)`, `GangSetRespondPercentage(gang, 100)` and one guard a 270°
  field of view (`BrSetFOV`); two thugs are turned into fighters with `BrSetThreatResponse(h, 2)` and `GoalMelee`.
- **2**: the strategy library; `GangEnableAttackStrategies(gang, true)` and `GangCanFlee(gang, false)` on the clinic's
  gangs; `TacticTrigger(gang, 1, true)` sets the bums' crowd cheering; `GoalLeadChase` runs Jesse's chase: he runs on
  while the chaser is within 30 m (the 50, 70 and 90 passed after it are stored but unused).
- **3**: the strategy library; `GoalBigLedgeThrower` (the balcony thrower, with up to three object types),
  `GoalDevilRun` (the rooftop runners, with speeds and boost distances from the chapter's table), `GoalBackoff`
  (fighters held 1 m back for 2000 ms), `GoalGuardFlag` (the gate guards, 0.5 m), `GoalTag` (the gang leader sprays a
  tag), `GangMakeEnemiesOfType(gang, 20)` and `(gang, 0)`, `BrSetType(h, 2)`.
- **5**: the strategy library; `GoalRunCarrotRun` for Sanchez's run ahead of the Warriors (after `BrFlushGoals`),
  `TacticBossScenarioA` for the Diego and Vargas fight ([its section](ai-gangs.md#boss-diego-vargas)),
  `GoalBackoff(player, …, 5, 5000)` to hold the players back during scenes, `BrSetPedType(h, 3)` for civilians who fight
  back.
- **34 (the riot)**: rioters with `GoalRiot(h, 10, 100, 2, fight, gangFight)` ([GoalRiot](#riot): within 10 m of a
  player, always act, two acts, a fight chance of 0 or 25 %); the looters get `BrSetThreatResponse(h, 0)` and
  `BrSetReactToViolence(h, false)`; `GangSetMaxConcurrent` paces the riot spawner (3, 6, 10); `TacticAttack` and
  `GoalFight(h, player, −1)` for the Furies; `GoalMoveToExitFlag` and `GangExitWorld` clear them out;
  `GoalStationaryThrower`.
- **95 (the hub)**: `GoalPlayDynIdle` and `ActLookAt` for the hub's idle humans; `GoalBoxer(h, HeavyBag)` at the
  clubhouse; `BrSetType(h, 2)` and `GangMakeNeutralOfType(GangWarriors, 19)` for side-job gangs; `GoalShopkeeper`,
  `GoalPeddler`, `GoalGrabTarget` (the vigilante job's stalker), `GangClearBums`, and spawners checked with
  `GangIsASpawner`.

## Scripted goals and actions {#scripted}

What `level99`'s scripts give a brain directly ([How the scripts drive the AI](#script-control) has the overview).
Confirmed (code) at the addresses cited unless marked; argument names and defaults are on [AI
bindings](../references/bindings/ai.md).

- **Lua callbacks.** A goal that takes a callback name interns it through the script system (`*(0x00512b04)`, vtable
  `+0xcc`; 0 for none) and calls back in its **End** through vtable `+0xa4`, which is `ScheduleFuncArg2`
  (`0x00357430`): the callback is **scheduled 33 ms later** with two values, the human's handle and a "completed" flag
  (`0x002d2f98` passes the interned name, the handle, the flag and 33). A goal cut short (popped, flushed) still calls
  back, with "completed" 0. This corrects an earlier reading of the fourth value as the goal type.
- **`GoalFight`** is `Brain_StartFight` (`0x002b2b90`): clear the actions, then `Brain_Fight(brain, target, -1, 0)`
  ([Starting a fight](ai-fights.md#targets)). Its third argument is read and never used.
- **`BrFlush`** (`0x00292530`): `Brain_ClearGoals`, then `Brain_ClearActions`. Each goal is ended, so its callback
  fires; an action that refuses to abort stays ([Actions](ai/index.md#actions)).
- **`BrDead` / `GangBrDead`** (`Brain_SetDead`, `0x00292330`): clear the **actions only** (the goals stay), set brain
  `+0x09` and reinstall the handlers ([Handlers](ai/index.md#handlers)). Inferred: a dead player brain no longer leaves
  the record to the pad, as `level99` sets its Warriors dead (`GangBrDead`) while it walks them through a scene.

### GoalMoveToFlag {#move-to-flag}

Goal type 1 (`0x002da2c0`, constructor `0x002da3b0`, vtable `0x00542130`). Fields: `+0x10` the target point, `+0x20`
the flag, `+0x24` gait, `+0x28` angle, `+0x2c` arrival radius, `+0x30` distance, `+0x38` next tick, `u16 +0x3c`
interval (ms), `+0x3e` arrived, `+0x3f` option, `+0x40` face the flag's heading. Confirmed (runtime) for Vermin's
goal (gait 4, angle −1, radius 0.5, distance −1 read back from the goal).

- **Start** (`0x002da408`): the target is the flag's position (`0x00417a60`) plus `distance` × (cos `angle`, sin
  `angle`) in world x and y, `angle` in degrees (`0x003376c0`); a negative distance is not special. Confirmed
  (runtime): flag `fVerminFencePoizo` at (47.30, 20.20), angle −1, distance −1 → target (46.30, 20.217). With an
  interval, the next tick is now + interval; then Resume.
- **Resume** (`0x002da550`): clear the actions; `0x00226f70(human)`.
- **Process** (`0x002da588`):
    1. The flag gone → done (2).
    2. On each interval tick, when `0x00291ed0` allows (the camera within 30 m and a free entry at `0x006cddf8`),
       `0x002205e0(1.0, human, 0x7d, …)` (inferred: an ambient gesture or line).
    3. Actions queued → wait (0).
    4. Fight stance off (`0x0022fef0`). **Inside the radius**: with the face flag and more than 15° off the flag's
       heading (`0x00416258`), queue a turn action and wait; otherwise set arrived and finish (2). **Outside**: queue
       a [move action](ai-moving.md#move-action) to the target (the radius, the gait, the option bit, a start delay of
       0-250 ms, and the "flag kind `0x12`" bit), and wait.

    So the goal plans again each time a move action ends short of the radius.
- **End** (`0x002da4a0`), only when arrived: the flag gets message 8 with the human (flag vtable `+0x44`), and the
  human's gang hears of it (`Gang_OnEvent`, [Gangs](ai-gangs.md#gangs)).

GoalMoveToFlag uses only the move action and the [route planner](ai-moving.md#path-planning), not the FlagNet goals
`0x69` / `0x73`.

### Turning: ActLookAt {#look-at}

`ActLookAt(human, target, turn, delay)` queues a **turn action** (vtable `0x005430a0`, on the base turn action
`0x002fdc28`, vtable `0x00543160`). It turns until the human faces the target within 15°, then **ends**; it does not
keep facing it. Its fourth argument is the action's **start delay** (`+0x04`, −1 = random 0-500 ms), not a duration.
The turn value goes to `+0x10`, which this update never reads.

- Fields: `+0x0c` the heading aimed at, `+0x14` a time limit, `+0x18` aborted, `+0x1c` the target.
- **Start** (`0x002fdc68`): done at once when record `+0x08` has any of `0x1c16a40`; else limit = now + 3000 ms.
- **Update** (`0x002fe1b0` → `0x002fdd08`): the heading to the target, taken again each update, goes to brain
  `+0x110` (the state update turns the human); done when the target is gone, when aborted, past the limit, or when
  record `+0x08` has none of `0x20080000` and the facing is within 0.2618 rad.
- **Abort** (`0x002fdcc8`): sets `+0x18`; refused only for a forced abort while record `+0x08` has `0x20000000`.

The goals below queue a **turn-to-point** variant (`0x002fe000`, vtable `0x005430e0`), which takes its heading once,
at Start, from a fixed point.

### GoalAddressPerson {#address-person}

Type `0x57` (`0x002cc348`, constructor `0x002cc408`, vtable `0x00541830`). It **never walks**: it turns to the
target, waits for it to come within `approach`, then plays a scene. Fields: `+0x10` target, `+0x14` approach, `+0x18`
range, `+0x1c` speech (a **scene id**; negative for none, flagged at `+0x24`), `u16 +0x20` state, `+0x28` callback.

- **Start** (`0x002cc488`): when the target is within 2 × approach, clear the actions and queue a turn-to-point at
  it.
- **Process** (`0x002cc588`):
    1. The target invalid (`0x0028d4b0`) → done; actions queued → wait.
    2. While the target is within `range`: every 30 brain updates a head look-at on it for 1500 ms (`0x0029a000` on
       human `+0x284`); when more than 60° off, a turn-to-point at it, led by its velocity when it moves.
    3. State 0: once the target is within `approach`, state 1, and with a scene, push **`Goal_PlayAnimation`** (type
       `0x21`, `0x002e48e8`) with the scene and the callback.
    4. State 1: with a scene, state 2 and done; with none it stays, facing the target, until removed.
- **`Goal_PlayAnimation`** (vtable `0x00542370`) plays a **scene**, not a clip: Start (`0x002e49f8`) starts it
  (`0x003541a0`, `Scene_PlayAnimation`), Process (`0x002e4a70`) is done when the human is gone or the scene has
  finished (`0x00354058`), End stops it. Start hands the callback to the scene (`0x00353f40`); the scene system calls
  it (inferred: at the scene's end; what it passes is not traced). AddressPerson's own End is the base's empty one
  (vtable `0x00541830`, `0x004ee038`), so the scene is its only callback. In Process step 2 the turn is led by the
  target's velocity when it moves (`0x00223a20`).

### GoalPlayDynAnimation {#dyn-animation}

Type `0x22` (`0x002d2df8`, constructor `0x002d2eb0`, vtable `0x00541410`). It plays a level-loaded clip in the
human's **dynamic animation slot**, anim id **668**. Fields: `+0x10` callback, `+0x14` interrupted, `+0x15` completed,
`+0x16` clip queued, `+0x17` the option.

- **Constructor**: copies the name (at most 31 characters) to human `+0x468`, sets human `+0x48c` = 668 and requests
  the clip from the resource manager (`*(0x0050cd4c)`); the loaded handle lands in human `+0x488`.
- **Start** (`0x002d2f60`): human `+0xe0` |= `0x10000`. **Suspend** (`0x002d2f88`) sets interrupted, so the goal
  ends as soon as it is back on top.
- **Process** (`0x002d3060`): done when interrupted or the state has any of `0x7bf9e9f7ff0`; wait while actions are
  queued; until queued, wait for the clip to load and for record `+0x08` to hold nothing but `0x40000000`, then
  queue a **play-anim action** (anim 668, blend in and out 0.2 s, the option as its flag); when that action has
  ended, completed = 1 and done.
- **End** (`0x002d2f98`): clears `0x10000` and calls back with "completed"; **Destroy** frees the slot
  (`0x0010bcf8`).
- The **play-anim action** (vtable `0x00542b60`): Start stops the brain (speed 0); the first Update starts the clip
  (`0x0025a3e0`) and keeps the record flags it holds; done when that fails or once record `+0x08` no longer has
  them. Abort always allows.

### GoalBumLogic {#bum-logic}

Type `0x4f` (constructor `0x002abef8`, vtable `0x0053f610`; [binding](../references/bindings/ai.md#goalbumlogic)).
Fields: `+0x10` its spot, `+0x28` the fidget timer, `+0x30` the option (fidgets on), `+0x31` the **type**, `+0x37`
the chance (at most 100). Confirmed (code) at the addresses.

- **Set-up** (`0x002ac9e0`, from the constructor): type 0 (puking) is put in the arrested state
  (`Human_SetArrested`); types 1 and 2 go down (`0x0022f100`, `0x00228388`, state flag `0x20000000`; inferred: on
  the ground). The type's clips from [`LoadBumAnims`](../references/bindings/character.md#loadbumanims) are bound
  into the human's dynamic animation slots (`0x002ac000`): type 0 the seven `puke_` clips as anim ids 668, 320-323,
  669 and 324; type 1 `bm_sleep_itch`, `bm_sleep_idle`, `bm_hit_grd_idle` and type 2 `bum_beg_itch`, `bum_beg_idle`,
  `bum_beg_hit` as 668, 196 (the down pose) and 195 (hit while down). A type-2 bum gets a talk prompt (kind 5,
  global string 10).
- **Process** (`0x002accc8`): a bum more than 1 (squared distance) from its spot walks back (move action, speed
  0.5). With a player near (the nearest player within a squared distance of 100, inferred 10 m) and the option
  set, the bum fidgets: anim 668 every 10 s (`0x002ac900`, blend 0.3 s); a type-0 bum whose `+0x36` is `0xff`
  plays 669 (the big puke) once (`0x002ac970`). [`GoalBumLogicTrigger`](../references/bindings/ai.md#goalbumlogictrigger)
  plays the same two at once.

### GoalDealer {#dealer}

Type `0x80` (`Goal_Dealer` `0x002c6c88`, constructor `DealerGoal_Init` `0x002c6d90`, vtable `0x00540c90`);
`level99_lesson2`'s `FlashDealer` (`GoalDealer(dealer, 0, 17, 0, 0, false)`: type 0, range 17 m, run and dirty
chances 0, no radar icon). The class overrides the dealer type: 426-430 → 0 (flash), 431-435 → 2 (spray paint),
436-440 → 1 (weapons). Confirmed (code) at the addresses cited unless marked.

| Offset | Meaning |
| --- | --- |
| `+0x10` | home: the dealer's position when the goal starts |
| `+0x24` | state: 1 waiting (set at construction and by the reset), 3 dealing, 4 run off after a rip-off, 5 run off when attacked |
| `+0x28` / `+0x2c` | type / range (m) |
| `+0x30` | next wary scan (ms) |
| `+0x34` | next "cash" line (ms) |
| `+0x38` | u16 deals made (the weapons dealer's stock count) |
| `+0x3a` / `+0x3b` | run chance / dirty chance (percent) |
| `+0x3c` | option: add a radar icon at the greeting |
| `+0x3d` | greeted |
| `+0x3e` | a player is in range |
| `+0x3f` | a deal was completed this visit |
| `+0x40` | the last refusal was "carrying the most" |
| `+0x41` | dirty: `Random_Int(100) < dirty chance`, rolled once in the constructor |
| `+0x42` | dealing (the offer was made this visit) |
| `+0x43`, `+0x44` | per player: the buy clip is bound to his anim 668 |
| `+0x45` | the money pair was started by this event |
| `+0x46` | a fight is near (set by the wary scan) |

**Start** (`DealerGoal_Start` `0x002c6e78`): threat response (brain `+0x21c`) 0, brain `+0xcc` \|= 2, the type's
spinning icon (`dyn_flashdeal`, `dyn_weapdeal`, `dyn_spraydeal`, [The icon](#dealer-icon)), the type's item in his
pocket (`Human_SetPocketItem(dealer, item, 1)`), the home position, then Resume. **Resume** (`DealerGoal_Resume`
`0x002c6f98`): fight stance **off** (`Human_LeaveFightStance`), `0x0021d848(dealer, 0)` (clears human `+0x3bf` and
sets bit `0x200` of his model's flags; meaning not traced), and the kind-4 prompt registered (below). **End**
(`0x002c70a0`): icon removed, the player's interaction target cleared if it is this dealer, threat response back
to 2.

#### Gestures and lines {#dealer-gestures}

Every gesture is a **play-anim action** (`DealerGoal_QueueGesture` `0x002c7158` → `PlayAnimAction_Init`
`0x002fa300`, vtable `0x00542be0`, blend in and out 0.5 s) of an anim id and a **variant**. The action's Start writes
the variant to human `+0x3c4`, and the clip lookup (`Human_GetDynamicAnim` `0x00221a00`, called by
`CharacterInstance_GetAnim` `0x00175080`) takes, in order: the human's own override for that anim id (seven slots of
`0x28` bytes from human `+0x3c8`: a name, the loaded clip at `+0x20`, the anim id at `+0x24`); else entry *variant*
of that anim id's group in his **gang's clip table** (gang `+0x1b0`, `GangClips_Get` `0x00163fc0`; a variant below 0
picks a random entry); else the character's own clip for the id. The groups (`GangClips_GetGroup` `0x00163f10`): 603
entries 0-7, 604 8-11, 595 12-16, 599 17-19, 598 20-22, 668 23-26. A gang made with kind 24 (`level99`'s
`GangCreate(24, "FDealer", 0, 0)`) gets them filled (`GangClips_Set` `0x00164178`, from `Gang_Create`
`0x0016cdf0`): 603 with the eight names at `0x0050cbc0`, 668 with the three at `0x0050cbe0`. The lines are speech
commands ([Speech](../references/speech.md)) through `DealerGoal_Say` (`0x002c7248` → `0x002205e0`, volume 1.0).

| Moment | Gesture (anim id, variant → clip) | Line |
| --- | --- | --- |
| greeting (the first time in range and in sight) | 668, random 0-1 → `dlr_becken_1` / `dlr_becken_2` | 94 |
| the offer (state 1 → 3, the player within 1.5 m) | none | 95 |
| no money | 603, random 2-3 → `dlr_refuse_1` / `dlr_refuse_2` | 97 `nocash` |
| carrying the most | 603, random 2-3 | 101 `limit` |
| rip-off | the push (below), no gesture | 105 `ripoff` |
| a deal completed | none (the money pair) | 96 `cash`, at most every 5 s |
| the player leaves after a deal | 603, random 5-6 → `dlr_thanks_1` / `dlr_thanks_2` | 98 |
| the player leaves without one | 603, 4 → `dlr_goodbye1` | 99 (not after a "limit" refusal) |
| a fight near him (wary) | 603, 7 → `dlr_nearfight` | 100 |

603 is `ANIM_FIDGET_FIGHT` and 668 `ANIM_SPECIAL_ACTION` ([anim ids](../references/anim-ids.md)). Entry 0 of 603
is `dlr_offer_flash`, entry 1 `money_give` and entry 2 of 668 `money_take`; no gesture here uses them (the gesture
kinds 1 and 9 of `0x002c7158`, which would, have no caller). A dealer whose gang is not kind 24 plays his
character's own 603 and 668 clips.

**Loading the clips.** The eleven names are `dlr_offer_flash`, `money_give`, `dlr_refuse_1`, `dlr_refuse_2`,
`dlr_goodbye1`, `dlr_thanks_1`, `dlr_thanks_2`, `dlr_nearfight` (603, entries 0-7) and `dlr_becken_1`,
`dlr_becken_2`, `money_take` (668, entries 0-2), each with `.anm`. Setting a slot (`DynAnimSlot_Set` `0x0010bc38`)
only attaches a pack already resident; the others are streamed by the resource manager's
`ResourceMgr_StreamGangs` (`0x00189ed8`), one pack per call: first the script's `SetDynamicAnimation` packs, then
the first unloaded slot (`GangClips_FindPending`) of the **nearest gang** with one, or of the nearest human's own
slots, requested by name (`ResourceMgr_RequestAnimPack` `0x0016f260`) and attached once loaded. Each `.anm` is its
own entry in the disc's WAD (one clip per pack; `money_give` and `money_take` are entries 9805 and 9806,
`dlr_becken_1` 3847). So in `level99` the dealer's gestures and the money pair arrive a few updates after
`GangCreate(24, "FDealer", 0, 0)` while the dealer's gang is the nearest; `level99`'s six `SetDynamicAnimation`
packs (`point_behind`, `point_left`, `dlr_greetagrsive`, `phone_idle`, `phone_enter`, `phone_exit`) do not include
them. Until both money clips are resident the deal completes at once, without the pair ([Buying](#dealer-buy)).
Confirmed (code) at the addresses cited; the entries from the disc.

#### The icon {#dealer-icon}

The icon object's script type is `dyn_icon` (its record holds the pointer at `0x005131e4`: init `0x003e8fa0`, update
`0x003e92e0`, message `0x003e9470`). Init (`DynIcon_Init`): the type's model, **attached to the human**
(`Obj_Attach`), update interval 2 ticks, velocity zero, **angular velocity (0, 0, π)**: it turns about the world z
axis at 180° per second, one turn in 2 s ([World objects](objects.md#objective-markers) for how an angular velocity is
applied). Its local position: **(0, 0, 2.5)** m for `dyn_weapdeal`, `dyn_flashdeal` and `dyn_spraydeal` (model hashes
`0x30f09efb`, `0x51d27ab6`, `0xd939c21d`); (0, 0, 2.25) for the other icons, except `dyn_cross` and `dyn_cuffs`, whose
local position is their **object type record's offset** (`+0x00`, `ScriptObj_GetVectorProperty(obj, 11)` →
`Vec4_FromVec3` `0x00391828`; the values are in the type data, not read here). `dyn_cuffs` (hash `0x464ac521`) also
takes the record's rotation (`+0x0c`, property 12) and is then **detached at once** (`Obj_Detach` keeps the world
pose), so it stays where it appeared instead of following the human, turning at 90° per second (0, 0, π/2); the
player markers (`dyn_play_one`, `dyn_play_two`,
their `_euro` forms) and `dyn_lizziestarget` do not turn. Inferred: the attachment's origin is the human's root at
his feet, so the dealer's icon floats 2.5 m above the ground. The icon is the type's 3D model drawn as a world
object at its own size (no scale is set); it is not a HUD sprite. **The cuffs** (`Human_ShowOverheadIcon`
`0x002271f0` from `Human_Arrest` with attach point 0, the root at the feet; [Crimes](crimes.md#arrest)): the model
`dyn_cuffs_geo`, placed at the feet + the type record's offset, then left in the world there. While the camera
state at `0x005fdeb8` `+0x1e8` / `+0x1ec` is set (inferred: a scene or camera effect), every icon but `dyn_cross`
gets its draw fields `+0xc8` / `+0xcc` set to −255 / −256 (inferred: hidden). Messages: 8 detach, `0x0a` show or hide, `0x15`
remove, `0x20` hide, `0x34` a colour fade (`HuSetSpinningIconColor`, [the spinning icon](characters.md#spinning-icon)).

**Coney's icons** (`repo:src/world_objects/spinning_icons.h`, drawn by `PlayLevelMode` with the world objects): a
human's icon is the type `iconTypeName()` gives (`dyn_p_one` / `dyn_p_two` become `dyn_play_one` / `dyn_play_two`),
placed 2.5 or 2.25 m above his feet, its rotation his heading plus a half turn plus the spin (half rate for the cuffs,
none for the markers), and not drawn while the letterbox is up (all but `dyn_cross`). **Coney's stand-ins**: the spin's
phase follows the game time; `dyn_cross` and `dyn_cuffs` sit at 2.25 m and the cuffs stay attached; the colour fades
(`HuSetSpinningIconColor`) are not drawn.

**The radar icon** (`DealerGoal_AddRadarIcon` `0x002c7ee0`, at the greeting, only with the option and when the dealer
has no blip yet): blip type 2, 4 or 3 with icon 29, 31 or 30 at 0.8 for types 0, 1, 2 ([GUI](gui.md#radar-icons)).

#### Process {#dealer-process}

`DealerGoal_Process` (`0x002c7fd8`; confirmed (code)), each update, with *d* the nearest player's distance
(`GameState_FindNearestPlayer` `0x00419ee0`):

1. **Range**: `+0x3e` = (*d* ≤ range). Out of range: if he was in range last update and the dealer was dealing, the
   leaving gesture and line (above; first a turn to him, only when the dealer's actions are not blocked); then the
   **reset** (`DealerGoal_Reset` `0x002c7de8`): the dealer's own 668 override removed when a deal was made; `+0x3e`,
   `+0x3f`, `+0x40`, `+0x42`, `+0x45` cleared; each player's 668 override removed; state 1 (unless 4 or 5); next scan
   0. Beyond 2 × range the greeting is forgotten (`+0x3d` = 0).
2. **In range**: the sight test `Human_HasLineOfSight(dealer, player)` (`0x00222288`, [Sight](ai-fights.md#sight)); and,
   once per visit for each player, `money_give.anm` is bound to that player's anim 668 (`Human_SetAnimOverride`
   `0x00221aa0`), in sight or not, so the clip is loaded before the pair plays it. Nothing plays it here: the money pair
   does.
3. Nothing more while the dealer's actions are blocked (brain `+0x2e` > 0).
4. **State 4** (after a rip-off): brain `+0x26c` = 5 and a `GoalMoveToExitFlag` (`0x002da8f8`; gait 4, arrival 2 m,
   flag message `0x66`) to the **nearest flag of activity 8** (`Flag_FindNearestByActivity` `0x004177d8`: of the
   level's flags whose `+0xd0` is 8 and whose `+0xd4` is set, the nearest in a straight line to the dealer; with these
   arguments there is no walkability test). With no such flag he stays. **State 5** (attacked, below): the same once
   his current line has ended (the sound handle at human `+0x178`), with gait 5 set first (`0x0028aac0`).
5. Not in sight, or no player: done for this update. The **dealer** busy (`0x00228258`: state flags `0x1f80974000`,
   or record `+0x108` still ahead of the time): the prompt is withdrawn (`+0x1b2` = 0, vtable `+0x12c`); done.
6. **Wary scan**, every 2 s while his threat response is 0: the nearest other live gang (`0x0016c978`) and its member
   nearest the dealer. When that member is within 4 m of the dealer, the dealer within 4 m of home, the member someone's
   target (his brain's attacker list `+0x1a4` is not empty) and in the dealer's sight: `+0x46` = 1, and unless the
   dealer is talking, a [Spectate goal](ai-fights.md#spectate) is pushed (keep 8 m, 2-4 s between looks, an 8 s limit)
   when the member walks or stands (gait below 3); a turn to the player, the wary gesture and line; done.
7. **Home**: more than 1 m from home and not playing 668: a move action home (gait 4, radius 1 m); done.
8. **Facing**: more than 15° off the player: not yet greeted → turn to him (`TurnToPointAction`); done. Greeted → turn
   (and done) only when the player stands (gait 0) within 2 m.
9. While the dealer is talking: done. **State 1** and the player within **1.5 m**, not talking himself: the offer
   line, `+0x1b2` = 1, the prompt registered, dealing (`+0x42`) = 1, **state 3**; done.
10. Not greeted: a turn to him, the greeting gesture and line, the radar icon (with the option); greeted, state 1.
11. Every update that gets here: `+0x1b2` = 1 and the **kind-4 prompt** registered through the dealer's vtable
    `+0x124` (`GSTRING.HUD` text of his type, [Crimes](crimes.md#context-records), reach 1.75 m). So the prompt is
    offered from the greeting on, not only in state 3, while the dealer is idle and the player in range and in sight.

#### Buying {#dealer-buy}

Triangle at the prompt sends message 0 to the dealer ([Crimes](crimes.md#triangle)); his brain
(`DealerBrain_OnEvent` `0x00303178`) passes event 0 to the deal (`DealerGoal_OnBuy` `0x002c74d8`) while the top goal
is `GoalDealer`. The table at `0x005110f8`, 8 bytes per type `{u32 prompt text, u8 item, u8 price, u8 most carried,
u8 amount}`:

| Type | Prompt | Item | Price | Most carried | Amount |
| --- | --- | --- | --- | --- | --- |
| 0 (flash) | 6 | 1 (flash) | $20 | 3 (4 with upgrade (6, 7)) | 1 |
| 1 (weapons) | 7 | 4 | $50 | 8 (counted on `+0x38`, the dealer's deals) | 1 |
| 2 | 5 | 3 (spray paint) | $5 | 9 | 1 |

The deal, for a buyer that is a player and a dealer neither down, dead nor busy: the dealer turns to him (when his
actions are not blocked); unless already in state 3 his current line is cut (`0x0021ec38`); `+0x40` = 0, dealing,
`+0x45` = 0, state 3. Then the first that applies:

1. **Money** (item 2) below the price: the refusal gesture and `nocash`; `+0x34` = now + 5 s; the prompt is withdrawn
   (`+0x1b2` = 0). With two players the deal is first offered to the other (`0x002c73b8`: within range, with the
   money and room), and the prompt stays when he can buy.
2. **Carrying the most**: the refusal gesture and `limit`, `+0x40` = 1, the rest as in 1.
3. **Dirty** (`+0x41`): when the dealer is free (`0x002282d8`), the buyer within the reach of attack kind 11
   (`Attack_ReachSquared` `0x00230d00`: kind 11 is anim 21, its reach from `AttackTable_GetReach`) and his actions
   could be cleared, he faces the buyer at once (`Human_FaceHuman` `0x00221dd8`) and queues a **play-anim-id
   action** (`PlayAnimIdAction_Init` `0x002fa5a0`, vtable `0x00542b60`) of anim **21 `ANIM_ATTACK_PUSH`** (`gen_push`),
   random variant, no blend. The action only plays the clip (`0x0025a3e0`); it writes no command, so what the push
   does to the buyer is whatever the clip's own strike does (not traced). In every case: `ripoff`, the price taken from
   the buyer and added to the dealer's money (human `+0x370`, at most 999), the prompt withdrawn, and **state 4** (he
   runs off). Nothing is given.
4. **The weapons dealer** needs a member of the **buyer's** gang within 10 m of the buyer who is not busy and not
   already holding `dyn_swhbld_super` (`Gang_FindMemberForItem` `0x00165f80`); without one, as in 2.
5. **The money pair**, unless the dealer's front action is of type `0x16`: when both `money_take.anm` and
   `money_give.anm` are in the level's animation cache (`0x0016f980`) and the dealer is not already in the pair (his
   anim is not 668 and his state code `+0x14` is not 17), **start it and return**:
   `Human_PlayDynPair(dealer, buyer, "money_take.anm", "money_give.anm")` (`0x00238828`) binds `money_take` to the
   dealer's 668 and `money_give` to the buyer's, sets both state codes to 17 and makes each the other's partner
   ([HuPlayDynPair](../references/bindings/character.md#huplaydynpair)); `+0x45` = 1. Nothing here aligns the two:
   no position or heading is written beyond the dealer's turn above. When the pair **is** already playing, or either
   clip is missing from the cache, the deal completes now.
6. **Completion**: `cash` (at most every 5 s); the weapons dealer's chosen member drops what he holds and gets
   `dyn_swhbld_super` in hand; for the other types the item's amount is added to the buyer's inventory
   (`Inventory_AddItem`, with the pickup sound `vags/interface/powerup`); the price is taken and added to the dealer's
   money (at most 999); `+0x3f` = 1, `+0x38` + 1.

So, with the clips loaded, **event 0 starts the pair, and the dealer's `money_take` completes it**: its clip event
`0x41` at frame 17 (message `0xc1`, `Human_HandleMessage` `0x002473bc`) calls `DealerGoal_FinishPair` (`0x002c7c20`),
which does step 6 when `+0x45` is set ([Combat](combat-rage.md#flash)). A second event 0 during the pair completes it at
once. Confirmed (code) at `0x002c74d8` and `0x002473bc`; the frame from the disc's clip.

**Attacked** (`DealerBrain_OnHit` `0x00302ff0`, events `0x10` and 1), confirmed (code) at the addresses cited:

1. An attacker throwing at him first gets `Brain_CallForHelp`; when that is answered, nothing more.
2. An attacker whose brain is a **Warrior** (type 3) and no threat to the dealer counts only when the dealer is the
   Warrior gang's chosen target (gang `+0x10`); otherwise nothing more.
3. A help call (`Gang_BroadcastHelpCall`, 30 m). It is event `0x14` ([Help calls](ai-brain-types.md#help-calls)), which
   every other dealer **drops** ([Dealers](ai-brain-types.md#think-dealer)), so it moves no other seller.
4. When he may react (`DealerBrain_CanReactToHit` `0x00302df8`: not down or dead, and his top goal is the dealer goal
   (`0x80`), goal 1, or a Spectate goal (`0x10`) while he has no fight goal) and **the attacker's** brain is not a gang
   soldier's (type 2; the old reading "his brain" was wrong, the test reads the attacker's brain): with `GoalDealer` on
   the stack its state becomes **5** and its **run chance** `+0x3a` is rolled (50 % without the goal), in
   `DealerBrain_FleeOrFight` (`0x00302eb0`). A later hit finds the reaction goal or the fight on top and rolls nothing.
    - `Random_Int(100)` below the chance: **flee**. A `DealerReaction` goal (`DealerFlee_Push` `0x002c88a8` →
      `0x002c8970`, type `0x81`, vtable `0x00540c30`) with the fight byte clear.
    - Otherwise, against a non-friendly attacker: **fight**. When the attacker is no threat yet the two gangs are made
      enemies (`Brain_MakeGangsEnemies`), threat response (`+0x21c`) 2, `Brain_Fight(dealer, attacker, 15000, 0)`
      (the dealer gang has no tactic, so FindEnemy, Melee and Fight are pushed), then the same `DealerReaction` goal
      on top with the fight byte set.
5. A non-friendly attacker raises the brain's `+0x200` (the [block](ai-fights.md#block)'s trigger).

**The reaction goal** (`DealerFleeGoal_Process` `0x002c8af0`): after the Start's 25-50 ms wait (his line
9 `scream`, or a turn to the attacker) he enters the fight stance and backs off to 2-4 m from the attacker
(`MoveToHumanAction`), saying speech command 17 when fighting, or 9 when fleeing and he holds an attack slot on the
attacker. Then:

- **Fight**: `Human_PlaceItemInHand(dealer, dyn_swhbld_super)`, the switchblade, and the goal ends. The fight goals
  under it take over: he fights the attacker with the knife. When they end, `DealerBrain_Think` sets the threat
  response back to 0 and `GoalDealer` resumes in **state 5**: once his line ends he runs (gait 5) to the nearest flag
  of activity 8 and leaves (`GoalMoveToExitFlag`, [the exit](ai-goals.md#goal-ped-reaction)). The knife stays in
  his hand; nothing removes it.
- **Flee**: gait 5, the taunt stopped, and a `GoalMoveToExitFlag` to the nearest flag of activity 8 (with line 9 if
  he held an attack slot). He leaves the world the same way.

So **a seller attacked once never sells again**, whichever way the roll goes; the level's spawner brings a new one.
The run chance is read only when he is attacked.

#### One seller or all {#dealer-attacked}

The original keeps the whole reaction **per seller**. Confirmed (code) at the addresses above:

- The state (5), the roll and the reaction goal live in the attacked dealer's own `GoalDealer` and brain.
- The help call is event `0x14`, which `DealerBrain_OnEvent` drops, and the dealers' gang has no tactic to take it.
- The fight branch makes the dealers' **gang** an enemy of the attacker's gang. That bit is shared by every dealer of
  the gang, but nothing in a dealer reads it: `DealerBrain_Think` pushes no fight on enemies, the dealer handler drops
  the new-enemy event `0xb`, and `DealerGoal_Process` never tests gang relations. Kind 24 (`0x18`) gangs whose
  members all have threat response 0 are made neutral with the kind-0 gangs again (`Gang_CheckNeutralRule`
  `0x00169d30`) once the fighter is calm.
- The **wary scan** (`DealerGoal_Process` step 6) does not use enemies either. It takes the nearest **other gang that
  has a member under attack** (`Gangs_FindNearestAttacked` `0x0016c978`: any member's attacker list non-empty,
  nearest by gang centre), then that gang's member nearest the dealer, and acts only when he is within **4 m** (16 is
  the squared distance) of the dealer, the dealer within 4 m of home, the member has attackers and is in sight. It is
  a seller watching a brawl at his stall, not a seller afraid of an enemy gang.

In `level87` all sellers share one gang, `GangCreate(24, "Dealers", 0, 0)` (`AddDealers`), filled by two spawners
whose callback `df.DealerSpawner` walks each to its flag; `df.RestoreDealersBrains` then gives `GoalDealer(h, type, 10,
run, dirty)`. `StartFactory` passes **run 25** for the spray sellers and **10** for the flash seller; dirty 0, or 40 for
flash and weapons sellers on difficulty 2 and up. Chapter 2's first spray seller is re-given `GoalDealer(h, 2, 10, 50,
0)`. The gang's message 2 (`df.DealerDied`; inferred: sent when a seller dies or leaves) restarts his spawner after
20 s.
`level99`'s flash dealer has run chance 0, so he always pulls the knife.

**Coney status and fix** (`repo:src/ai/dealer_goal.*`, `repo:src/ai/brain_events.cpp`; checked 2026-10-11):

- Coney has no `DealerBrain_OnHit`: a dealer's event 1 and `0x10` go to the shared handler, which makes the dealer
  gang and the player's gang enemies and calls for help. Nothing flees, fights or pulls a knife; `GoalDealer` is never
  told and keeps selling.
- **Item 7 (every seller stops):** Coney's wary scan (`nearestEnemy`, `kWaryDistance` 16) reads 16 as metres and tests
  gang **enemies**. After one hit the shared handler makes the whole dealer gang the player's enemy, so every seller
  with the player within 16 m pushes a wary Spectate goal every 2 s and stops offering. Fix: as the original, the
  nearest other gang with a member under attack, that member within 4 m of the dealer (16 squared), the dealer
  within 4 m of home, the member's attacker list non-empty and in sight.
- **Item 15 (the knife):** add a dealer type handler with `DealerBrain_OnHit` as above (the Warrior and throw tests,
  the 30 m help call, `CanReactToHit`, the gang-soldier attacker exception, state 5, the roll), the `DealerReaction`
  goal (type `0x81`: the wait, the back-off to 2-4 m, the switchblade or the exit flag), `Brain_Fight` for 15000 ms
  with threat response 2, and `GoalDealer`'s state 5 run to the activity-8 flag once the fight is over.
- Keep the dealer handler dropping events `0xb` and `0x14`.
- **The dealer's lines (fixed 2026-10-11, issue 234):** the play mode asked for them (`cash`, `nocash`, a
  boss's taunts) with the speaker's handle only, and the game sound said nothing without a voice set, so every one was
  silent. `GameSound::sayCommand` now takes the voice set of the type the level script made the speaker as
  (`HuCreate`, `CfgChar`) when the caller gives none. Checked on `level99`: a purchase says `cash` once
  (`repo:tests/platform/disc_flash_test.cpp`).

**Suggested tests** (synthetic, `coney_tests "[dealer]"`):

1. Two dealers in one kind-24 gang 30 m apart; the player hits dealer A. B's `GoalDealer` stays in state 1 and still
   offers the prompt to a player at 1.5 m; B pushes no Spectate goal.
2. Wary scan: a member of another gang under attack 3.5 m from a dealer at home → Spectate pushed; at 5 m, or with no
   attackers on him, or an enemy-gang member at 3 m who is not under attack → nothing.
3. Run chance 0 (`level99`'s flash dealer), seeded: one hit → `DealerReaction` with the fight byte, after the back-off
   `dyn_swhbld_super` in hand, a fight goal on the player, threat response 2; once the fight ends, state 5 and a
   `GoalMoveToExitFlag` to the nearest activity-8 flag at gait 5.
4. Run chance 100: one hit → flee, no knife, `GoalMoveToExitFlag` at gait 5; a second hit rolls nothing.
5. A gang soldier (type 2) hitting a dealer: help call only, state unchanged.
6. Through the player's path: `level87` checkpoint 2, the pad's square on a spray seller; the other seller still sells.

### GoalRiot {#riot}

Type 84 (`Goal_Riot` `0x002d0e98`, `RiotGoal_Init` `0x002d0f68`, vtable `0x005414d0`, Process `RiotGoal_Process`
`0x002d1c38`); the binding's arguments are on [`GoalRiot`](../references/bindings/ai.md#goalriot). Confirmed (code)
unless marked. Fields: `+0x10` the wander vector, `+0x20` the move deadline, `+0x24` the next shout, `+0x2c` radius,
`+0x32` act chance, `+0x33` acts left, `+0x34` fight chance, `+0x35` the player-fight chance, `+0x36` **state** (0
roam, 1 smash, 2 loot, 3 leave), `+0x37` decided (set at the first decision, never cleared), `+0x38` failed moves,
`+0x39` the roam counter (starts at 26), `+0x3a` shout. Init: the wander vector (0, 2.5, 0.2), and with
`Random_Int(100)` < 51 (0-100 inclusive) the state starts at 1 or 2 (a second draw < 50: loot).

**The turf gate** (`0x0028ff58(brain, human)` → `0x001652a0`): whether a human's position is inside the turf of
the rioter's gang (brain `+0x20c`; `Gang_IsPointInTurf`, `0x001652e8`, true for a gang with no turf). The riot asks
it of the **nearest player's human**. Its sibling `0x0028ff38(brain, point)` asks it of a point, and every target
below must pass it.

**Each update** (Process): nothing while the human's actions are blocked; the fight stance is dropped; nothing when
there is no player (`GameState_FindNearestPlayer` gives the nearest and its squared distance *d²*). Then by state:

- **0, roam.** With *g* the turf gate: the rioter **decides** when *g* is false, or when *d²* < radius², the brain's
  update counter (`+0x34`) is a multiple of 60 and `Random_Int(100)` < 50; otherwise it roams.
    - **Deciding**: `+0x37` = 1; one draw *r* = `Random_Int(100)`. If *r* < fight chance and *g*: try a fight
      (below); started → state 3. Then if *r* < act chance and *g*: state 1 or 2 (a new draw < 50: loot). Otherwise
      **state 3**. So a decision that neither fights nor acts ends the riot, and a player outside the gang's turf
      ends it at once.
    - **Roaming**: when the move deadline `+0x20` has passed it becomes now + 1000 + 1000 × detail level `+0x333`
      ms (the roll-over is what lets a new destination replace a running move); when `+0x24` has passed it becomes now +
      a random 4000-4500 ms and, with shout, the rioter says speech command `0x59` (`0x002205e0`); every 90 brain
      updates a random head glance (`0x00231cd0`, 750 ms, 22.5°-67.5° to a side, inferred from its maths). While a move
      is running and the deadline has not rolled over, nothing more. With no failed move (brain `+0x284` = 0) it picks a
      destination (below) and gives a move action to it (`MoveAction_Init`, arrival 0.5 m, gait 4), retargeting a
      running move action instead when there is one. After a failed move, `+0x38` counts up: at 30 → state 3; every 5th
      clears `+0x284` so the next update tries again.
- **1, smash.** The target is a **world object within 20 m** (`0x0029d5f0`, the object search `0x0039a850`, up to
  384, sorted by `0x003868d0` (inferred: by distance), the first that passes): not broken (object `+0x54` bit
  `0x10`), class flags
  (vtable `+0x54`) without bit 26 or `0x800000`, **vandalisable** (`Object_IsVandalisable` `0x00394f30`: object kind,
  `CfgObj` `+0x86`, not 30 `TYPE_BREAKANDENTER_DOOR`, 40 `TYPE_EXPLOSIVE` or 42 `TYPE_FIREBARREL`; not of class
  `dyn_masks` when type byte `+0x5a` < 1; not in the excluded vandalize zone (`CfgExcludedVandalizeZone`,
  `0x005148bc`, object `+0x114`); then true when its vtable `+0xf4` record has `+0x40` & `0x30`, or its anim set
  `+0x87` is 4 or 5), not claimed by the rioter's own gang in the last 5 s (object `+0xec` gang, `+0xf0` time), for
  an AI rioter not within 1 m of a player, and with a stand point at the type's reach (the larger of `+0x78` and
  `+0x7c`) on the rioter's side (`0x00252410`) that the rioter reaches in a straight line. The search claims it for
  the gang for 5 s. If its position passes the turf gate, the rioter gets a **VandalizeItem** goal (type 47,
  `0x002dd630` → `0x002dd6b8`, cheer flag 1) on it.
- **2, loot.** The nearest **store flag** (activity 14, `storeJewelry` / `storeFront`, [Flags](flags.md)) within
  20 m; its group (`+0xd8`) must still have objects (`0x0039a580`), and the rioter must reach the flag in a straight
  line (`0x0024e078`). The target is then the nearest object of that object zone (`0x0039a2c0` mode 2) spawned with
  `ObjSpawn` flag 2 (record bit `0x80000`), not removed, live, not broken, without class bit 26 and not claimed by any
  gang within 5 s. If it is a world object whose position passes the turf gate: claimed for the rioter's gang for 5
  s, and `Goal_GetItem(rioter, gait 4, object)` sends him to pick it up ([`GoalGetItem`](../references/bindings/ai.md#goalgetitem)).
- **After 1 or 2** (target found or not): with `+0x37` set, acts left − 1, and at 0 state 3; else (only the free act
  from Init) back to 0. With acts left > 0 the state **stays** 1 or 2, so the next update looks for another target.
- **3, leave.** The nearest enabled exit flag (activity 8) whose position passes the turf gate (`0x00416f08`); none:
  try again next update. Found: push `GoalMoveToExitFlag` (`0x002da8f8`, gait 4, speech `0x59` when shout) on top.

**The roam destination** (`0x002d18b8`), confirmed (code):

1. The roam counter `+0x39` + 1. A multiple of 27 (so the first call, from 26): **toward the player**
   (`0x002d1498`) when the nearest player is 15 m or more away: a random point 15 m from him (`0x0029f3c8`, below)
   that the rioter reaches in a straight line; deadline now + 10 s. A multiple of 79: **somewhere in the turf**
   (`0x002d15d0`): a random one of the gang's turf boxes, a point at a random angle and (box radius `+0x40` − 5) ×
   a random 0.5-1 from its centre `+0x30`, up to 3 tries for a point on an area, then it must pass the turf gate
   and be reached in a straight line from the rioter's polygon; deadline now + 10 s. Either one succeeding is the
   destination.
2. Otherwise a **wander**: the vector `+0x10` gets a random (−1..1, −1..1, 0) added and is scaled back to 2.5 m;
   the destination is the human's position + his rotation (position table `+0x10`) applied to (vector + (0, 10, 0)),
   that is 10 m ahead plus the 2.5 m wander circle, dropped to the ground from up to 5 m (`0x0034f950`). It must pass
   the turf gate and the straight-line test (`0x002221e0`).
3. Failing that, the vector resets to (0, 2.5, 0.2) and the destination is a random point 5 m from the rioter
   (`0x0029f3c8`: a uniform direction about the vertical, up to 5 tries for a point on an area, connected to the
   start (`0x0024dee8`), dropped up to 2 m). None: no move this update.

**The fight** (`RiotGoal_TryPickFight`, `0x002d1288`): only a rioter whose brain is type 2 (gang soldiers and thugs,
[Types](ai/index.md#types)); any other never fights. One draw: *p* = `Random_Int(100)` < the player-fight chance
`+0x35`. The candidates are the humans within 15 m (`0x002274a8`, up to 60, in human-slot order) and the first that
passes is taken: brain not type 3 (an ally Warrior); no player controls him (human `+0x1b0` = −1) unless *p*; not
friendly (`0x00290230`: same brain or `Gang_AreFriends`); no fight goal; nobody in his attack-slot list (brain
`+0x1a4`); reached from the rioter in a straight line. Then threat response `+0x21c` = 2,
`Brain_Fight(rioter, him, 8000, 1)` and the taunt `0x11` (`0x002205e0`). So the binding's sixth argument is the chance
that **the player** may be picked, not a gang member.

**How the 8 s fight ends.** `Brain_Fight` → `Brain_PushFightGoal(brain, 8000)` pops any Melee and FindEnemy goals,
pushes FindEnemy (`0x41`) and Melee (8), each given the 8000 ([Closing on the target](ai-fights.md#fight-approach)), and
then the fight goal (15) with **its deadline `+0x24` = now + 8000 ms** (`FightGoal_Init`, `0x002b2c20`; `+0x20` is the
separate 750-1000 ms first-attack timer). The fight goal's usual ends apply (no target, no attack slot, out of range,
[The fight goal](ai-fights.md#fight)). Past the deadline, at each update with no actions queued, it **ends** (returns 2)
when any of these holds: the fighter's class `+0x11b` is 13 and goal `+0x29` is 0; the fighter is not among the target's
four active attackers (target brain `+0x1f0`, `0x002911a8`); the target brain's byte `+0x11f` is 0
(`Brain_IsAttackableBy(target, nil)`, not traced); the fighter has a goal `0x35` (FollowAndDefend). Otherwise it goes on
attacking. Confirmed (code) at `0x002b3d0c`-`0x002b3d6c`. Under the fight goal the riot goal is in state 3, so when the
fight (and the melee goals beneath it) are done, the rioter leaves.

### GoalDevilRun {#devil-run}

Type 152 (`Goal_DevilRun` `0x002e1760`, constructor `DevilRunGoal_Init` `0x002e1850`, vtable `0x00541ad0`;
[binding](../references/bindings/ai.md#goaldevilrun)): a runner heads for the end of a path while pacing itself
against a gang, the chases of `level3` and `level54`. A **friendly** runner (`hostile` false) runs ahead of the gang
and speeds up as they close on it; a **hostile** one (a pursuer) speeds up as it falls behind and attacks when it
catches up. Confirmed (code) at the addresses cited.

Fields: `+0x10` the path (an `AddPath` object: its point count `u16 +0x32`, its flags `+0x10[i]`), `+0x14` the gang,
`+0x18` the gait, `+0x1c` attack distance, `+0x20` pace distance, `+0x24` maximum speed, `+0x28` urgency, `+0x2c`
the saved field of view, `+0x30` the chaser's handle (−1 at first), `+0x34` the current speed, `+0x38` the hindmost
gang member's segment (`0xff` at first), `+0x39` / `+0x3a` the brain's saved `+0x0b` / threat response, `+0x3b` an
update counter (a byte), `+0x3c` hostile.

- **Start** (`DevilRunGoal_Start` `0x002e18d8`): save the brain's byte `+0x0b` and threat response, speed = the
  gait's (`Human_SpeedForGait`), save the field of view and set it to 2π (the runner sees all round), then Resume.
- **Resume** (`DevilRunGoal_Resume` `0x002e1998`): threat response 0 (it ignores attackers); counter = 16.
- **End** (`DevilRunGoal_End` `0x002e1950`): restore the byte `+0x0b`, the threat response and the field of view.
- **Process** (`DevilRunGoal_Process` `0x002e2230`), each update, counter + 1:
    1. Every 17th update, **the hindmost segment** (`0x002e1b60`): for each of the gang's 16 member slots that
       resolves, its segment (below), keeping the smallest in `+0x38`. When the gang has no member left, the goal
       ends (2).
    2. **The chaser**: re-chosen (`0x002e1c18`) when there is none, every 29th update, and, for a hostile runner,
       when the chaser is down or dead or `0x00223b70` holds for it. The choice takes the gang's live members (not
       down or dead, not `0x00223b70`), projects each on the line of the hindmost segment (points `+0x38` and
       `+0x38` + 1) and keeps the one whose projection is nearest the segment's start: the gang member farthest
       back along the path. None → the goal ends (2).
    3. Every 7th update, **pace** (`0x002e1e10`), below; it may end the update with the fight goals it pushes.
    4. With no action queued: fight stance off and a **move action** (`0x002fbae0`) to the path's **last** point at
       the current speed (at least the gait-2 speed), radius 1.5 m. The runner does not visit the path's other
       points: the [move action](ai-moving.md#move-action) plans its own route; the path only measures progress.
- **Segment of a position** (`0x002e19b0`): walking the path's segments from the first, the distance from the
  position to each segment (`0x00336d28`); the answer is the segment before the first whose distance grows, or the
  last segment (count − 2), never more than the limit passed in.
- **Pace** (`0x002e1e10`), with `R` the runner, `C` the chaser:
    1. The distance `d`: when the runner's segment is the hindmost segment, `d` = 0 if the chaser is already past
       the runner along the segment (friendly: ahead of it; hostile: behind it), else the distance between the
       runner and the chaser's projection on the segment's line (`0x00336ee0`, `0x00336e08`). On different segments,
       `d` = |`R` − `C`| (3D).
    2. A **hostile** runner attacks when the chaser is running (its gait `+0x1a8` above 2) and `d` ≤ the attack
       distance, or when the chaser is slower and `d` ≤ (1.1 × the runner's far melee range `+0x140`)², a distance
       compared with a squared length as written. It then sets threat response 2, targets the chaser
       (`Brain_SetTarget`), pushes a [Melee goal](ai-fights.md#melee-goal) (`0x002ade10`: time limit 2000, its fight goals
       2000) and, in the running case, an engage-enemy goal (`0x47`) whose `+0x30` is set to 4.0; the devil run
       stays below them and resumes when they end.
    3. Otherwise the blend `f`: hostile `min(d / pace distance, 1)`; friendly `max(1 − d / pace distance, 0)` (1
       while the chaser is ahead). Speed = gait speed + (maximum speed − gait speed) × `f`, stored at `+0x34` and
       given to a running move action at once (`0x0029f710` → action `+0x40`). The brain's byte `+0x0b` = the saved
       value + urgency × `f`, rounded (0 for two human types and two states, `0x0028cdf8`); what reads it is not
       traced.

In `level3`'s chase the Warriors run as friendly runners on the rooftop path against their own gang (gait 4, pace
distance 4 m, up to 10 m/s, urgency 3), the player being the one they wait for; the Hi-Hat pursuers run as hostile
runners against the Warriors (gait 5, attack at 1 m, pace distance 13 m, up to 13 m/s, urgency 0.5)
([`level3`](scripting.md#level3)).
