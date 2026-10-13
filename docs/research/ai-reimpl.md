# AI reimplementation plan: units and specifications

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), read in Ghidra on
2026-10-12 (the ObjectPile goal, the fidget and taunt actions and the pattern block on 2026-10-13). No runtime check
yet; each claim states its evidence level.

## Purpose

The AI's goal, tactic and action engine is mostly named and cited ([AI humans](ai.md), [AI code index](ai-code.md),
[AI goals](ai-goals.md)), but a citation is not something an implementer can code from. This page splits what Coney
has not reimplemented yet into **units** an implementer can finish with tests in one sitting (about 5 to 15
functions each), ordered by dependency and payoff, and gives the first units a full specification: inputs and
outputs, the fields read and written with their offsets, constants, branches and edge cases. Later units point at
the sections that describe them, which get the same treatment before their unit starts.

**Scope.** The AI is the stretch `0x00287a18`-`0x00323298` that the [source map](source-map.md#position) places in
`Human/` (brains, goals, actions, tactics, formations), plus the gang record's functions in `GameModes/`
(`0x0016xxxx`) and the Warriors' command dispatcher in `Warriors/`. It is **not** the progress tracker's
`TaskEngine` subsystem (`0x00386f58`-`0x00408ac8`), which is the world-task engine of [Tasks](tasks.md): objects,
particles, cars, scenes. On 2026-10-12 the AI stretch holds 2,008 functions, of which 462 are reimplemented
(listed in `repo:docs/progress/functions.toml`) and 1,546 are not; the [plan](#plan) below covers all 1,546 in 132
units.

**Already tagged is not finished.** Several functions in the gang-fight units carry an `@orig` tag in Coney but are
built as stand-ins (for example, TacticAttack's melee goal). A unit lists those under "to correct": the implementer
replaces the stand-in, and the function stays in `functions.toml`.

Shared conventions on this page: *brain* `+n` is the AI brain record ([The brain](ai.md#brain)), *human* `+n` the
human record, *gang* `+n` the gang record ([The gang](ai-gangs.md#gang-record)), *now* the game time in ms (game state
`0x0050b734`, `+0x48`). A human's position is entry *human* `+0x92` of the 32-byte position table at `0x00714b00`.
A human is an AI when *human* `+0x1b0` (its pad) is −1. *Living* in "living members" means the member's handle
still resolves (`Gang_CountLiving(g, 0)`, `0x00166158`, counts resolving handles); *standing* means also not down
or dead (`Gang_CountLiving(g, 1)`, `Human_IsDownOrDead` `0x00227e60`; only the value 1 selects it, any other
counts resolving handles; confirmed (code) at `0x00166158`). The HUD's gang counts all pass 1
([HUD](hud-functions-lua.md#gang-count-what)). Neither is `Gang_CountStanding` (`0x00166220`, `GangGetStandingCount`), which
tests knocked out, state `0x100000000` and wounded separately (confirmed (code) at `0x00166220`). Goal kinds
are the type ids of [Goals](ai.md#goals).

## Unit 1: threats and the gang's shared target {#unit-1}

Gang-fight needs 1, 5 and 6 of the gang-fight research (issue #273). One small unit, because three other parts of
Coney use a stand-in threat test today (`repo:src/ai/brain_events.cpp`, `repo:src/ai/give_way.cpp`,
`repo:src/ai/fight_goal.cpp`).

### `Gang_IsEnemyOf` (`0x00168fe0`, 124 bytes) {#gang-is-enemy-of}

`Gang_IsEnemyOf(a, b)`: whether gang *a* treats gang *b* as an enemy. Confirmed (code) at `0x00168fe0`.

1. False when one gang's kind (*gang* `+0x2c`) is 1 (the police) and the other's is `0x17`, either way round.
2. False during a **truce**: the global `0x0050cb7c` holds an end time; while it is not 0 and *now* is below it,
   no gang is anyone's enemy.
3. Otherwise bit (*b*'s id, *gang* `+0x30`, masked to 0-31) of *a*'s enemy mask (*gang* `+0x38`). The test reads
   *a*'s mask only, so it is not symmetric by itself; `GangMakeEnemies` sets both masks.

### `Brain_IsThreat` (`0x00290138`, 244 bytes) {#brain-is-threat}

`Brain_IsThreat(a, b)`: whether brain *b*'s human is a threat to brain *a*. Confirmed (code) at `0x00290138`.
Callers include the fight goal's re-target, `Brain_CheckAttack`'s reason 5, `Brain_MayEngage` (`0x00290588`), the
gang witness (`0x00304c78`), the shooters' nearest-threat tests and `Human_IsThreatTo` (`0x00222a48`, which only
looks the two brains up).

1. *a* = *b*: false.
2. **The Warrior hit-back flag.** When *a* is a Warrior (brain type, *brain* `+0x04`, is 3) with *brain* `+0x2e5`
   set, and *b*'s human is a pad player: true. The same the other way round: *b* a Warrior with `+0x2e5` set and
   *a*'s human a pad player: true. The flag is set while a Warrior hits back at a player who hit him
   ([The enemy scan](ai.md#enemy-scan)); the test is against **any** pad player, not only his chief. This
   corrects "a Warrior ... and the chief" on [AI humans](ai.md#enemy-scan).
3. Otherwise true when `Gang_IsEnemyOf(a's gang, b's gang)` (*brain* `+0x20c`), or when both humans are pad
   players; else false.

### `Gang_SetSharedTarget` (`0x00165b70`, 276 bytes) {#gang-set-shared-target}

`Gang_SetSharedTarget(gang, handle)`. Confirmed (code) at `0x00165b70`; callers `Gang_Reset` (`0x00164808`, with the
null handle) and `WarriorCommand_Dispatch` (`0x0041c4e0`).

1. The null handle (`0x006ebd30`): stored at *gang* `+0x10` and nothing else.
2. Otherwise, with no leader (`Gang_GetLeader`) nothing happens. The handle's human is checked with
   `Brain_ValidateEnemy(leader's brain, human)`; when that gives no human, nothing happens (the old target stays).
3. When the human's gang is friendly to this gang (`Gang_AreFriends` = 1), nothing happens.
4. Else *gang* `+0x10` = the handle, and every member slot (`+0x48`, 16 handles) whose human resolves and is an AI
   adds that human to its enemy list (`Brain_AddEnemy`). Players are skipped.

The score's +400 for the shared target ([Choosing among enemies](ai.md#enemy-score)) then makes the crew converge.

### To correct in this unit (already tagged)

| Address | Name | What Coney must change | Evidence |
| --- | --- | --- | --- |
| `0x0028fbb0` | `Brain_UpdateGoals` | Calls `Brain_MaybeScanEnemies` (`0x0028fa18`) for **every** brain on every update that is not skipped, after the 44-update line-of-sight check and the turf-leave check (every 5 updates when the gang has turf), before the reaction goal ([Brain_UpdateGoals](ai.md#update-goals)). Coney calls the scan only from the Scout tactic. | confirmed (code) |
| `0x0041c4e0` | `WarriorCommand_Dispatch` | Every command first clears the crew gang's shared target; the unforced attack command then sets it (`Gang_SetSharedTarget`) to the chief's lock-on target when he is locked on, the target is an AI and not friendly; else to the AI handle in game state `+0x280`, else `+0x284`. A forced command never sets it. | confirmed (code) in the gang-fight analysis (#273), not re-read here |
| `0x002b3ab0` | `FightGoal_Process` | The once-a-second re-target to the nearest human straight ahead (sector 0, within 1.5 m) needs only: not the current target, `Brain_IsThreat`, and no GrabTarget goal (`0x1f`) on the stack. It then calls `Brain_SetTarget(brain, him, 1)` (slot forced) without checking the enemy list or `Brain_ValidateEnemy`. | confirmed (code) in the gang-fight analysis (#273) |
| `0x00222a48` | `Human_IsThreatTo` | `Brain_IsThreat` on the two humans' brains, in place of Coney's enemy-list stand-in. | confirmed (code) |

**Tests (synthetic):** a table over brain types 0-6 × pad/AI × the `+0x2e5` flag; a truce end time in the future
and in the past; kinds 1 and `0x17`; a shared target that is invalid, friendly or valid (every AI member's list
gains him, a player member's does not); the null handle clears without touching lists; with several brains, each is
scanned on its own schedule within the 5-token budget; a re-target onto a human whose slots are full displaces the
farthest holder.

**Coney status (implemented).** `repo:src/ai/threats.h` (`isThreat`, both tags), `Gangs::enemies`, `Gangs::setTruce`
and `Gang::setSharedTarget` (`repo:src/ai/gangs.h`); the scan runs from `Brain::update` through the gangs' one
`ScanSchedule` (`repo:src/ai/scan_schedule.h`), which the Scout tactic now shares instead of scanning itself; the
scan's step 2 (the gang's shared target) is in; the fight goal's re-target forces the slot, and a displaced holder
loses his target ([Attack slots](ai.md#attack-places)); the attack command sets the shared target from the chief's
lock-on (`ScriptedStory::startWarriorCommand`). Tests: `repo:tests/ai/threats_test.cpp` and the dispatcher case in
`repo:tests/ai/scripted_story_test.cpp`. Stand-ins and questions left for an analyst:

- The hit-back flag `+0x2e5` exists on Coney's brain but nothing sets it until `WarriorBrain_OnHitByChief` is built.
- The fallbacks `+0x280` / `+0x284` are not filled, so with no lock-on the attack command leaves the shared target
  clear. They are now known: the crime reporter (a civilian phoning the police or calling a gang) and the human
  calling his gang or running off after a rip-off ([Gang fights](gang-fights.md#targets) rule 4).
- Coney clears the shared target where the command's tactic is started (after the dispatcher's refusals and the
  repeat check). Does the original clear it before the refusals, so a refused or repeated command also clears it?
- A brain with no gang takes no scan token in Coney. Does the original's `Brain_MaybeScanEnemies` take a token for a
  brain whose gang is none?

## Unit 2: TacticAttack in full, without team-ups {#unit-2}

Gang-fight needs 2, 3, 9 and 10. Before this unit `repo:src/ai/tactic_attack.h` gave members a stand-in melee goal
on the nearest gang enemy; the unit gives them the real Melee goal (`repo:src/ai/melee_goal.h`), the idle pass
toward the farthest busy mate, the violence event, flight and the gang's alert state (Coney status at the end of
this section).

**The record** (`AttackTactic_Construct` `0x003075c8`, vtable `0x00543320`; confirmed (code)):

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | gang * | the gang |
| `+0x08` | s32 | time limit (−1 none; the binding passes −1) |
| `+0x0c` | | the Lua callback (`Tactic_SetCallback`) |
| `+0x10` | sub-tactic * | the running team-up order ([Unit 3](#unit-3)); 0 none |
| `+0x1c` | vtable | `0x00543320` |
| `+0x20` | u32 | next enemy check; *now* + 1000 at construction |
| `+0x24` | u32 | next idle pass; *now* + 3000 at construction |
| `+0x28` | u32 | team-up timer; 0 = not armed |
| `+0x2c`, `+0x30` | u16 | the spacing interval and its base ([below](#attack-spacing)) |
| `+0x32` | u8 | the gang fled ([flight](#attack-flight)); 0 at construction |

### `Tactic_Attack` (`0x00307548`, 124 bytes)

The script binding `TacticAttack(gang, callback)`: finds the gang by id (`Gang_Get`); none → nothing. Takes a
tactic from the pool (`Tactic_Alloc`), constructs it with the callback and no time limit, and sets it on the gang
(`Gang_SetTactic`, which also receives a null tactic when the pool is full). Confirmed (code).

### `AttackTactic_Start` (`0x00307fe0`), to correct {#attack-start}

In order (confirmed (code)):

1. `AttackTactic_ComputeSpacing`, then `AttackTactic_GiveMelee` (below).
2. The gang says its spot line (`Gang_SaySpotLine(gang, 0x16)`, [Gang functions](ai-code.md#gang-functions)).
3. **The cower clip group `0x29c`** of the gang's clip table (*gang* `+0x1b0`): for gang kind 6 with team-ups
   enabled (*gang* `+0xd9`), two clips from the table at `0x005110c8`; otherwise, when the gang can flee
   (*gang* `+0xdf`) and the kind's flee share (`GangConfig_GetField72`, `CfgGang` v9) is not 0, the one clip
   `gen_cower_plead`. Kind 6 with `+0xd9` skips the flee-share test.
4. When the gang can flee and *gang* `+0xe0` is still −1, *gang* `+0xe0` = `Gang_CountLiving(gang, 0)` (the
   count the flight check compares with).

### `AttackTactic_ComputeSpacing` (`0x00307668`, 284 bytes) {#attack-spacing}

Takes the first member (in slot order) whose enemy list (*brain* `+0x164`) is not empty, and the enemy on that list
nearest to him (`HandleArray16_NearestHandle`). No such enemy: `+0x2c` = 300. Otherwise `+0x30` = 1000, or 50
when that enemy's *human* `+0x1a8` is above 2, and `+0x2c` = `+0x30` / `Gang_CountLiving(gang, 0)` (integer
division). Confirmed (code). Nothing in the executable reads `+0x2c` or `+0x30` (no reader found): Coney stores
them and needs no behaviour from them.

### `AttackTactic_GiveMelee` (`0x00307788`, 236 bytes) {#attack-give-melee}

For each of the 16 member slots whose human resolves and is not down or dead, and whose brain has **no**
PedReaction goal (`0x6b`, `Brain_FindGoal` < 0): threat response (*brain* `+0x21c`) = 2; the stack popped to the goal
base and the base marked (`Brain_PopToGoalBase`, `Brain_MarkGoalBase`); `Goal_Melee(his handle, −1)` (the Melee goal
with no time limit). Players are included: the loop has no AI test. Confirmed (code). Called from Start, events
`0x13` and `0x16`, and `AttackTactic_OnStrategyEnd`.

### `AttackTactic_Process` (`0x003081a8`), to correct {#attack-process}

Returns the tactic code for `Tactic_Process` (0 = carry on). Confirmed (code). In order:

1. **The enemy check.** When *now* > `+0x20`: `+0x20` = *now* + 1000. If the gang has not fled (`+0x32` = 0), walk
   the members: the first resolving member whose enemy list is not empty ends the update at once with 0 (so the
   idle pass and the team-up timer **do not run in that update**). With no such member, return 9 (`TacNoEnemies`).
   When the gang has fled, the check does nothing and the update goes on to step 2.
2. **The idle pass.** When *now* > `+0x24`: `+0x24` = *now* + 3000; for each resolving member whose goal stack is
   empty (*brain* `+0x2c` = −1): pop to base, mark it, `Goal_Melee(him, −1)`; then the **farthest** other resolving
   member whose stack is **not** empty (squared distance strictly greater than the best so far, starting from 0) is
   chosen, and when there is one the idle member gets `Goal_HelpRespond(him, that mate, 0, −1)` (`0x002b75b8`, see
   below). Players are not excluded. The stacks are read **as the pass goes**: the search runs right after this
   member's Melee is pushed, inside the same loop, so a member given Melee earlier in the pass counts as busy for
   the later ones (the member himself is skipped by handle). The distance is **3-D**: `Vec_DistSq` (`0x00336ce0`)
   subtracts x, y and z, sets w to 0 and sums x² + y² + z² (`vaddy.x`, `vaddz.x`), between the two transforms'
   positions (`0x00714b00` + index × `0x20`); the flat variant `Vec_DistSq2D` (`0x00336c98`) is not the one called.
   Confirmed (code) at `0x003081a8`.
3. **The team-up timer.** Only with no order running (`+0x10` = 0) and not fled: when `+0x28` is 0 it is set to
   *now* + 7000; else when *now* > `+0x28`, `+0x28` = 0 and `AttackTactic_StartSubTactic` runs ([Unit 3](#unit-3)).
   An attempt that starts nothing re-arms the timer on the next update, so attempts come every 7 s.
4. Return 0.

### `Gang_SendHelper` (`0x002b75b8`), to correct

The name is misleading: it is the HelpRespond **pusher**. Arguments (helper handle, target handle, *flag*, time
limit); both handles must resolve to humans; it allocates a goal, initialises HelpRespond
(`HelpRespondGoal_Init` `0x002b7670`, [HelpRespond](ai-goals.md#goal-help-respond)) on the helper's brain toward the
target with the flag and limit, and pushes it. Confirmed (code). The tag is on Coney's HelpRespond push
(`sendHelper`, `repo:src/ai/brain_events.h`).

### `AttackTactic_OnViolence` (`0x00307878`, 408 bytes) {#attack-on-violence}

Event `0x14` (violence nearby). With no leader, nothing. The leader's brain picks a side
(`Brain_PickSideInFight(leader's brain, event +0x00, event +0x24)`, `0x002912b0`); with no side picked, nothing.
Then for each resolving member who is not down or dead and whose goal stack is **empty**: pop to base, mark,
`Goal_Melee(him, −1)`, `Brain_AddEnemy(him, picked)`, `Brain_SetTarget(him, picked, 0)`; and when no HelpRespond
goal (`0x1d`) sits above his goal base (`Brain_FindGoal` ≤ *brain* `+0x2d`), `Goal_HelpRespond(him, picked, 0,
−1)`. Confirmed (code).

### `AttackTactic_OnEvent` (`0x00308478`), to correct

The event record: `+0x20` the event code, `+0x24` the human it concerns; nothing happens when that is not a human.
Always returns 0. Confirmed (code).

| Code | Condition | Action |
| --- | --- | --- |
| 1, `0xb` | the human's gang (*brain* `+0x20c`) is this gang, `Brain_HasFightGoal` is false, and no order runs (`+0x10` = 0) | pop to base, mark, `Goal_Melee(him, −1)` |
| 2 | the gang has not fled | the callback with 13 (`TacMemberDied`), through slot `+0x54` (`Tactic_FireCallback`) |
| `0x12` | | `GangTactic_CheckFlee` ([below](#attack-flight)) |
| `0x13`, `0x16` | | `AttackTactic_GiveMelee` |
| `0x14` | | `AttackTactic_OnViolence` |

### `Brain_HasFightGoal` (`0x00290348`, 104 bytes)

Despite the name, whether a **Melee** goal (`8`) or a goal `0x3f` is on the brain's stack (two `Brain_FindGoal`
calls); the fight goal `0xf` is not tested. Confirmed (code).

### Flight and `AttackTactic_End` {#attack-flight}

`GangTactic_CheckFlee` (`0x00307d40`, event `0x12`) is already tagged; its rule, from the gang-fight research
(#273), to check Coney against: when the gang can flee (`+0xdf`) and `+0xe0` was recorded, and the standing, uncuffed
members number at most round(v9 × `+0xe0` / 100), every standing member of class role 11 (`CharClass` `+0x11b`)
gets threat response 0, Melee and a PedReaction (type 9) away from his nearest enemy, and `+0x32` is set.

Details settled from the code (confirmed (code) at `0x00307d40`):

- A member who already has a PedReaction (`0x6b`, `Brain_FindGoal` ≥ 0) is skipped. Each other fleeing member is
  **popped to his goal base and the base marked** (`Brain_PopToGoalBase`, `Brain_MarkGoalBase`) before
  `Goal_Melee(him, −1)` and `PedReactionGoal_Push(him, nearest enemy, 9)`, exactly as `GiveMelee` does; the
  nearest enemy is taken from his enemy list (*brain* `+0x164`) before the pushes. `+0x32` is set only when at
  least one member was sent.
- The check **does not test `+0x32`**: every later event `0x12` runs it again. Members still holding their
  PedReaction are skipped; one whose PedReaction has ended (or a member newly meeting the count test) gets the
  flight again. Only kind 6 with team-ups (`+0xd9`) returns at once; `+0xe0` must be above 0 (signed byte), the
  share (`GangConfig_GetField72`) not 0 and the rounded threshold not 0.

`AttackTactic_End` (`0x003080c8`, 132 bytes): releases clip group `0x29c` (`GangClips_ReleaseGroup`) under the same
condition Start used to install it (can flee and v9 ≠ 0, or kind 6 with `+0xd9`). Confirmed (code).

### The gang's alert state (`0x00164a28` and four clip helpers) {#alert-state}

`Gang_SetAlertState(gang, state)` (`0x00164a28`, 316 bytes), confirmed (code). Nothing happens unless *state*
differs from *gang* `+0x32`. It stores *state*, then:

- *state* 1 (fighting): unless the gang kind is 5, installs the combat fidgets (`Gang_AddCombatFidgetClips`
  `0x001694b8`: group `0x25b`, the four `combat_fidget_*` clips of `0x0050cab8`) unless the kind is `0x18`, and the
  fight taunts (`Gang_AddFightTauntClips` `0x00169508`: group `0x253` with five clips, group `0x257` with a random
  three of five, group `0x256` with three) unless the kind is `0x17`; then the music mood is updated
  (`SystemMusic_UpdateMood`). Every AI member's *brain* `+0x14d` = 0.
- *state* 0 (calm): unless the kind is 5, removes the same groups (`0x001694e8`, `0x00169570`) under the same kind
  exceptions. Every AI member's *brain* `+0x14d` = 1.
- Any other value: no clip change, AI members' `+0x14d` = 1.

`+0x14d` = 1 keeps the turf limit on whom a member may take ([The gang](ai-gangs.md#gang-record)). A new brain starts
at **1**: `Brain_Init` (`0x0028a570`, from `Human_Init`) stores 1 at `+0x14d`. Confirmed (code). Callers: the Melee
goal when the gang has no tactic, and `Tactic_Start` (below).

**What `Tactic_Start` passes** (`0x00306690`, confirmed (code)): `Gang_SetAlertState(gang, flag == 1)`, where *flag*
is the tactic's vtable slot `+0x3c`. It always calls, so the value is **1 (fighting)** for a tactic whose flag is 1
and **0 (calm)** otherwise, never another value. The base slot (`0x004ef350`) returns *type* < `0x12` from slot
`+0x34`; TacticAttack's type slot (`0x004ef348`) returns 0, so TacticAttack's Start sets **1**. A tactic of type
`0x12` or above puts its gang back to calm (0) when it starts.

**The clip names** (confirmed (code); pointer tables read in `SLUS_212.15`):

| Group | Installed by | Clips |
| --- | --- | --- |
| `0x25b` | `Gang_AddCombatFidgetClips` (table `0x0050cab8`) | `combat_fidget_call_front`, `combat_fidget_shift`, `combat_fidget_boxersdance`, `combat_fidget_shuffle` |
| `0x253` | `Gang_AddFightTauntClips` (`0x0050caf0`) | `taunt1_adv` to `taunt5_adv` |
| `0x257` | the same (`0x0050cb08`, a random three of the five) | `gen_tnt_point`, `gen_tnt_cheer`, `gen_tnt_spit`, `gen_tnt_wave`, `gen_tnt_pointdwn` |
| `0x256` | the same (`0x0050cb20`) | `gen_tnt_cheer1`, `gen_tnt_cheer2`, `gen_tnt_cheer3` |
| `0x29c` | `AttackTactic_Start`, kind 6 with `+0xd9` (`0x005110c8`) | `punk_grab_struggle`, `punk_grab_struggle_react` |
| `0x29c` | `AttackTactic_Start`, can flee and v9 ≠ 0 | `gen_cower_plead` |

Each name is an `.anm` file name without the extension. How a longer list fills a group with a random subset in
order: `GangClips_Set` (`0x00164178`, [Gang functions](ai-code.md#gang-functions)).

**Tests (synthetic):** a gang of four with one PedReaction holder (no Melee, response unchanged); the 1 s check with
one member listing an enemy (returns 0 and the idle pass waits for the next update) and with none (returns 9);
the idle pass with three idle and two busy mates at known distances (each idle one gets HelpRespond toward the same
farthest busy one); event `0x14` with and without a picked side; event 1 for a member of another gang (ignored);
event 2 after flight (no callback); the alert state set twice to 1 (clips installed once) and to 2.

**Coney status (implemented).** `repo:src/ai/tactic_attack.h` (`TacticAttack`, `tacticAttack`, every tag above but
the team-up ones), the real `MeleeGoal` in place of the stand-in (Coney's story and scout tactics give it too),
`Brain::hasFightGoal` and the turf-limit byte `Brain::keepsToTurf` (`repo:src/ai/brain.h`), and the gang's alert state,
clip groups, `canFlee` and flee count (`repo:src/ai/gangs.h`). `Tactic_Start` puts a gang tactic's gang in the
fighting state and calms the gang of a tactic of type `0x12` or above. The flight moved from the hub's per-update
check (`ScriptedHub`) into the attack tactic's event 18; the hub hands the gangs its categories and flee shares
(tested in `repo:tests/ai/scripted_hub_test.cpp`). The living count of Start and the spacing is `Gang::living()`:
every member, the dead included, since a Coney member's handle resolves until the human is deleted. That is not the
script binding's living head count (`ScriptedBrains::gangHeadCount`, members with health left, Coney's reading) nor
the HUD's gang counter, which also counts health. Tests: `repo:tests/ai/tactic_attack_test.cpp`. Stand-ins left:

- "Cuffed" in the flight is the human's scripted arrested state.
- The clip groups keep only their clip counts, and nothing plays the groups yet; the random three of five taunts is
  not drawn. The names are now in the [alert state](#alert-state) table.
- The spot line at Start is not built: `Gang_SaySpotLine` (`0x00165d40`) is specified in
  [Gang functions](ai-code.md#gang-functions) and its sound in [Sound events](sound-events.md).
- The music mood update is not built. `SystemMusic_UpdateMood` (`0x0041a060`) is the game's mood judge (it ends in
  `GameState_SetMusicMood`); Coney's system music judges the mood every frame with its own stand-in rule
  ([Sound](sound.md), the System music entry of the Coney status), so the call from the alert state adds nothing
  until that judge is built from the code.

Answered for this unit (2026-10-13, from the code; each against Coney's choice):

- `Tactic_Start` passes 1 for TacticAttack (its type is 0, below `0x12`), and 0 for a tactic of type `0x12` or above
  ([alert state](#alert-state)). Coney's 1 matches for TacticAttack; a Coney tactic of type ≥ `0x12` now passes 0.
- The idle pass reads the stacks as it goes, and the distance is 3-D ([Process](#attack-process)). Both match Coney.
- The flight pops and marks before its pushes, and runs again on every event 18, skipping members that still have
  a PedReaction ([flight](#attack-flight)). Both match Coney. Three details of Coney's check differed from the code
  and are now fixed: it skips a member who already has a PedReaction, skips a cuffed member in the sending loop, and
  does nothing when the share or the rounded threshold is 0, setting "fled" only when a member was sent.
- A new brain's `+0x14d` is 1 (`Brain_Init`). Matches Coney.

## Unit 3: team-up framework, the sub-tactic pool and the draw {#unit-3}

Gang-fight need 4, first half. The strategy slot of every tactic, the pool of order records, the draw that picks
one of seven orders and the call back when an order ends. With [Unit 4](#unit-4) to [Unit 6](#unit-6) it completes
the team-ups. Confirmed (code) at each address.

### The strategy slot (`0x00306b50`, `0x00306b88`, `0x00306bd0`)

- `Tactic_SetStrategy(t, order)`: `Tactic_EndStrategy(t)`, then `+0x10` = *order* (which may be null when the pool
  was full).
- `Tactic_UpdateStrategy(t)`, from every tactic's `Tactic_Process`: when `+0x10` is set and `SubTactic_Process`
  returns 1, `Tactic_EndStrategy(t)`.
- `Tactic_EndStrategy(t)`: when `+0x10` is set: the order's End (`SubTactic_End`), then the tactic's slot `+0x1c`
  (for TacticAttack `AttackTactic_OnStrategyEnd`, called while `+0x10` still points at the order), then the record
  is freed (`SubTactic_Free`) and `+0x10` = 0.
- `AttackTactic_OnStrategyEnd` (`0x00308150`): when the order's kind (its vtable slot `+0x2c`) is **1**, i.e. one
  of the two guard orders, `AttackTactic_GiveMelee`. The other orders end without giving Melee. This corrects "when
  it says so".

### The order record and pool (`0x00321828`-`0x00321ab8`)

8 records of `0x30` bytes at `0x006eb6f0`, a bit mask at `0x006eb6e0`.

| Offset | Meaning |
| --- | --- |
| `+0x00` | the gang |
| `+0x04` | the time limit: relative until started, then absolute; −1 none (compared unsigned, so never passed) |
| `+0x08` | started (u8) |
| `+0x0c` | vtable: `+0x0c` Start, `+0x14` End, `+0x1c` Process, `+0x2c` kind |
| `+0x10`... | per order ([Unit 4](#unit-4) to [Unit 6](#unit-6)) |

| Address | Name | Spec |
| --- | --- | --- |
| `0x00321828` | `SubTactic_PoolClear` | mask = 0 |
| `0x00321838` | `SubTactic_Alloc` | the first record whose bit is clear: sets the bit and returns it; none of 8 free → null |
| `0x00321890` | `SubTactic_Free` | clears the bit of the record at that address; an address not in the pool is ignored |
| `0x003219a0` | `SubTactic_Start` | when `+0x04` ≠ −1, `+0x04` += *now*; the class's Start; `+0x08` = 1 |
| `0x00321a00` | `SubTactic_Process` | starts it when `+0x08` is 0; returns 1 (done) when *now* > `+0x04`, else the class's Process (1 done, 0 not) |
| `0x00321a70` | `SubTactic_End` | the class's End |
| `0x00321a98` / `0x00321ab8` | `SubTactic_Reset` / `SubTactic_ResetAll` | clear the mask on a full reset (arguments 1, `0xffff`) |
| `0x003218d8` | `SubTactic_LeaderGesture(order, leader, clip, arg)` | returns 0 when there is no leader, when clearing his actions fails (`Brain_ClearActions` pops every queued action and returns 0 if one refuses), he has a held flag in `0x2fefefff` or a state flag in `0x7bf9e9f7ff0`; else allocates an action (may fail) and initialises a gesture (`GenToIdleAnimAction_Init(action, brain, clip, arg, 0)`), and returns 1 |

The kinds (vtable slot `+0x2c`, read from the five vtables `0x00544220`, `0x00544290`, `0x005442c8`, `0x00544300`,
`0x00544338`): Grab 6, Charge 0, Guard 1, Leader 4, Throw 5. The Starts of Grab (`0x00321c48`), Charge
(`0x00322040`) and Guard (`0x00322440`) and Charge's End (`0x00322048`) are empty.

### `GangConfig_SetStrategy` / `GangConfig_GetStrategyEntry` (`0x001645d0`, `0x00164610`)

The seven weights of a gang kind's strategy (`CfgGang`'s sixth argument) are bytes `+0x73`-`+0x79` of its
configuration record (28 records of `0x7a` at `0x0050caa8`); the getter returns byte *i*. The weight table per
strategy is in the gang-fight research (#273) and [Gangs](../references/gangs.md).

### `AttackTactic_StartSubTactic` (`0x00307a10`, 812 bytes) {#start-sub-tactic}

1. Nothing unless team-ups are enabled (*gang* `+0xd9`), there is a leader, he is not busy (`Human_IsBusy`) and
   `L` = `Gang_CountLiving(gang, 0)` is at least 2.
2. **The enemy count `E`.** `Gangs_FindWithinRadius(r, gang, out, 32, Gang_CanJoinFightAgainst)` with *r* the
   leader's sight range (`Brain_GetSightRange`). Despite its buffer, that function returns **at most one** gang: the
   first, by gang id 0 to 31, that is in use (*gang* `+0x18`), is not this gang, has its centre (*gang* `+0x00`)
   within *r* of this gang's centre, has its own radius (*gang* `+0x14`) ≤ *r*, and passes the filter. No gang →
   no order. `E` = that gang's `Gang_CountLiving(g, 0)`. This corrects "the at most two nearest enemy gangs".
   The filter (`Gang_CanJoinFightAgainst` `0x0016bfc8`): the other gang is this one's enemy
   (`Gang_IsEnemyOf(other, this)`), it is fighting (alert `+0x32` = 1) or runs a tactic of kind `0x14` whose
   `+0x4c` is 0, it has living members and not all of them are unable to help (`Gang_NoneAbleToHelp`).
3. **The draw.** `W` = the sum of the kind's seven weights; `r` = `Random_Int(stream 0x006eb8a0, W)`, which returns
   0 to `W` **inclusive**. The order is the first *i* with `r` < `w0 + ... + wi`; when none qualifies (`r` = `W`,
   including every all-zero strategy) the order is **5**. So order 5 gets one extra chance in `W + 1`, and a kind
   with no weights always draws order 5.
4. **The order**, allocated from the pool (null when full) and stored with `Tactic_SetStrategy`:

| Draw | Order | Constructor arguments |
| --- | --- | --- |
| 0 | Charge | count max(`L` / 2, `L` − `E`) (integer halves), limit 10000 ms |
| 1, 2 | Guard | back-off = (draw is 2), deadline 15000 ms |
| 3, 5 | Throw | count max(`L` / 2, `L` − `E`), pile = (draw is 3), limit 20000 ms |
| 4 | Leader | count `L` − 1, limit 15000 ms |
| 6 | Grab | no limit |

**Tests (synthetic):** a fake order class through the pool: the ninth allocation fails and the tactic stores null;
start once, the limit passed → done → the tactic's slot `+0x1c` hears it before the record is freed; the draw over
a scripted random stream: weights (20, 20, 20, 20, 20, 20, 0) with `r` = 0, 19, 20, 119 and 120 (orders 0, 0, 1,
5, 5), weights all 0 (order 5); no draw without `+0xd9`, with a busy leader, one living member, or no enemy gang in
range; `E` from the lowest-id qualifying gang, not the nearest.

## Unit 4: the Charge, Get-their-leader and Grab orders {#unit-4}

Gang-fight need 4, second part. Shared shape of the Charge, Leader, Guard and Throw Processes (confirmed (code)):

1. Done (1) when there is no leader, or he is cuffed or knocked out. Not done (0) while his actions are blocked
   (`HumanRecord_AreActionsBlocked`).
2. **The opening**, once (a per-order "opened" byte): when `Gang_CountLiving(gang, 1)` (standing) is at least 2,
   the leader says a command line when `Ambient_MayGesture(leader)` allows it (the camera within 30 m and a free
   speaker slot for his reaction kind; `Human_SayCommand(1.5, leader, line, ...)`), and gestures
   (`SubTactic_LeaderGesture`) with clip A in the fight stance (`Human_IsInFightStance`) or clip B out of it. With
   fewer than 2 standing, no line and no gesture.
3. **The order goes out once** (a per-order "ordered" byte) on the first update on which the leader's speech handle
   (*human* `+0x178`) no longer resolves, i.e. when his line has ended or none was said.
4. Return 0; the order lasts until its time limit ([Unit 3](#unit-3)).

**A free member**, for Charge and Leader: his human resolves, `Human_IsBusy` is false, his actions are not blocked,
no goal `0x3f` is on his stack, and his top goal is not EngageEnemy (`0xb`). Charge also skips the leader and a
member who has attackers (`Brain_HasAttackers`: his attack-slot list `+0x1a4` not empty); Leader does neither.

### Charge (`0x00321db8`, `0x00321de8`, `0x00322050`)

- Init: gang, limit, vtable `0x00544290`, count `+0x10`; `+0x14` opened, `+0x15` ordered = 0.
- Process: line 1 (`attack`), clips `0x2a4` / `0x2a8`.
- **Order** (`ChargeSubTactic_Order`): walks the 16 member slots until the count is reached. For each free member:
  his enemy list is **sorted farthest first** (`HandleArray16_SortForCharge`: the sort by squared distance from
  him with the comparator `0x00321d80`, which orders larger distances first; the list is rewritten packed, empty
  slots last) and its first entry is checked with `Brain_ValidateEnemy`. Valid: the member counts toward the total,
  his goals down to and including a fight goal (`0xf`) are popped when one is on the stack, and EngageEnemy is
  pushed on that enemy with flags `0x8f` (`EngageEnemyGoal_Init(goal, brain, enemy handle, 0x8f)`,
  [EngageEnemy](ai.md#engage-enemy)). Invalid: the member is skipped (no second choice) and does not count. This
  corrects "their nearest valid enemy": the charge goes for the **farthest** listed enemy.

### Get their leader (`0x00322708`-`0x00322a40`)

- Init: gang, limit, vtable `0x00544300`, count `+0x10`, `+0x14` = −1 (saved global), `+0x18` opened,
  `+0x19` ordered, `+0x1a` saved byte.
- Start (`0x00322988`): saves the gang kind's configuration byte `+0x6c` (`CfgGang` v2, the standing spacing,
  [How many hit one man at once](ai.md#attack-places)) in `+0x1a` and the global `0x00510b54` in `+0x14`, then sets
  the byte to 4 and the global to `0x12` (the enemy-leader score term, read as an integer). The byte is per gang
  **kind**, so every gang of that kind is affected while the order lasts.
- End (`0x003229f8`): restores both.
- Process: line 1 (`attack`), clips `0x2a4` / `0x2a8`.
- **Order**: for each free member (leader included), the first entry of his enemy list (in list order, no
  validation) who is the leader of his own gang (`Gang_GetLeader` of that human's gang). When found: counts,
  pops down to and including a fight goal, pushes EngageEnemy (`0x8f`) on him.

### Grab (`0x00321ad8`-`0x00321cd8`)

- Init: gang, limit (−1), vtable `0x00544220`; `+0x10`, `+0x11` (ordered) = 0. No line, no gesture.
- Process: done when there is no leader or he is cuffed or knocked out. Once: the order; when it ordered nothing,
  done. Then done when the leader has no GrabTarget goal (`0x1f`) any more.
- **Order** (`GrabSubTactic_Order`): scans the human pool (60 records of `0x6d0` from `0x00640c80`) for the first
  human who is not down or dead and whose class (*human* `+0xcc`) is 221 (the dogs); the leader must have no goal
  `0x3f` (tested per candidate, so with one nothing is ordered). Pops the leader down to and including a fight goal
  and pushes GrabTarget on that human with no limit (`GrabTargetGoal_Init`). Returns 1 when ordered.
- End (`0x00321c50`): pops the leader down to and including GrabTarget (`0x1f`) when it is on his stack.

**Tests (synthetic):** Charge with four free members and count 2 (two ordered, the leader never); a member whose
farthest enemy is invalid (skipped, not counted, the next member tried); a member under attack skipped. Leader with
an enemy list holding a rival leader in third place; the byte and global swapped for the order's length and
restored at End. Grab with no class-221 human (done at once), with one (leader gets GrabTarget; done when it ends).
Every Process: no order while the leader's line plays; exactly one order.

## Unit 5: the Guard orders and the FightBackOff goal {#unit-5}

Gang-fight needs 4 and 12. Confirmed (code) at each address. Depends on FollowAndDefend (`0x002bca20`, a crew unit
in [the plan](#plan)); until that unit lands, Coney's FollowAndDefend stand-in is used.

### Guard (`0x00322188`-`0x00322510`)

- **Init**: gang, vtable `0x005442c8`, base limit −1 (the order times itself), `+0x10` = *now* + deadline (15000 ms)
  or 0 without one, `+0x14` opened, `+0x15` ordered, `+0x16` saved formation byte = `0xff` (none), `+0x17`
  back-off, `+0x18` closing said.
- **Order** (`GuardSubTactic_Order`), with the leader's formation *F* (`Human_GetFormation(leader, 0)`):
  1. When `+0x16` is `0xff`, save *F* `+0x275` there.
  2. `n` = `Gang_CountLiving(gang, 0)`. For each member slot that resolves and is standing: the leader decrements
     `n`, and with back-off is popped to base, marked, and given FightBackOff with no limit. Any other AI member
     joins *F* (`Formation_Join`), gets the attack table `0x00511438` as his override (*brain* `+0x208`) unless
     back-off, is popped to base, marked, and gets `FollowAndDefendGoal_Init(3.0, goal, brain, leader handle)`.
  3. *F*: slot set 3 (`Formation_SelectSet`), min(2 × `n`, 9) slots, shape radius 3.0 (`Formation_MakeShape`),
     *F* `+0x275` = 1, *F* `+0x274` = 1, then `Formation_Plan`.
- **Process**: the opening with line 3 (`defend`), or 2 (`follow`) with back-off, clips `0x2a3` / `0x2a7`; then,
  once the line has ended, the order once. After that, while no line plays: with `+0x10` set and *now* past it,
  the first time (`+0x18` = 0) the leader says line 1 (`attack`, **not** gated by `Ambient_MayGesture`) and
  gestures `0x2a4` / `0x2a8` (with fewer than 2 standing, neither) and `+0x18` = 1; on a later update with no line
  playing, done (1).
- **End** (`0x00322448`): when the leader exists and a byte was saved, *F* back to slot set 0, *F* `+0x274` = 1,
  *F* `+0x275` = the saved byte. Without back-off, every resolving member's `+0x208` = 0. It neither removes the
  members from *F* nor pops FollowAndDefend: `AttackTactic_OnStrategyEnd` gives Melee next (kind 1), which pops to
  the goal base.

### FightBackOff (type `0x1c`; `0x002b7140`-`0x002b7258`)

Record: `+0x00` brain, `+0x08` time limit, `+0x0c` vtable `0x0053fdf0`, `+0x10` the threat's handle (null at
init), `+0x14` the taunt timer (0), `+0x18` the saved turn boost.

- Start: saves the turn boost (*brain* `+0x0b`) and sets it one higher (`Brain_SetTurnBoost`); End restores it.
- `FightBackOff_PickPoint` (`0x002b71d8`): despite the name, picks the **nearest human on his enemy list**
  (`HandleArray16_NearestHandle` on *brain* `+0x164` from his position) into `+0x10` (the null handle when none).
  This corrects "the nearest flag" on [AI goals](ai-goals.md#goal-reactions).
- **Process** (never returns done; it ends only by its limit or a pop):
  1. Return 0 while actions are queued (*brain* `+0x2e` > 0), or while he has any held flag but `0x40000000`.
  2. Fight stance on. On every 45th update (*brain* `+0x34` % 45 = 0) re-pick the threat.
  3. The threat counts only when it resolves, is not in state `0x100000000` and, when his gang has turf, is inside
     it (`Brain_IsTargetInvalid`, which despite its name returns the usable human).
  4. On a re-pick update while his gait is idle and his gang has more than one living member: a glance at a random
     living mate (`Random_IntBelow(stream 0x006eb880, count)`; the head look, weight 0.5, 2000 ms; it may pick
     himself).
  5. With a threat: closer than `f` = 1.75 × his far melee range (*brain* `+0x140`): look at him (2500 ms) and
     queue a move-to-human that keeps him between `f` + 1 and `f` + 2 m (2000 ms limit); the taunt timer = 0;
     return. Else, when his heading is more than 60° (1.0471976 rad) off the threat: queue a turn to face him at
     his maximum turn rate; the taunt timer = 0; return.
  6. Otherwise the taunt timer: 0 → *now* + 4000; passed → a taunt action (kind 3, line `0x10`, no limit) and the
     head look relaxes (2000 ms), timer 0.

**Tests (synthetic):** a gang of five with a leader: Guard without back-off (four in formation, slots 8, table set;
End clears the tables and restores the byte), with back-off (leader gets FightBackOff, tables untouched); a
deadline passed with and without enough standing members. FightBackOff with a threat at 1, 9 and 20 m (move,
turn or taunt after 4 s), with an empty enemy list (only the taunt timer runs), never done.

## Unit 6: the Throw orders and the ObjectPile goal {#unit-6}

Gang-fight need 4, last part. Confirmed (code). Depends on the GetItem goal (tagged) and the object search.

- **Init** (`0x00322b78`): gang, limit (20000 ms), vtable `0x00544338`, count `+0x10`, pile `+0x16`; `+0x14`
  opened, `+0x15` ordered.
- **Start** (`0x00323060`): without the pile option the count becomes every living member
  (`Gang_CountLiving(gang, 0)`), replacing max(`L` / 2, `L` − `E`).
- **Process** (`0x00323160`): the opening with line 10 (`throw`), clips `0x2a4` / `0x2a8`; then the order once.
- **End** (`0x003230a0`): pops every resolving member down to and including his ObjectPile goal (`0x60`) when one
  is on the stack. GetItem goals are left to finish.
- **Order** (`ThrowSubTactic_Order`):
  1. Objects within 30.0 m of the gang's centre (`ObjectManager_FindObjects`, at most 384), sorted nearest first.
  2. A current member is chosen when there is none: the first slot that resolves, is not the leader, is standing,
     has no attackers, no goal `0x3f`, and whose top goal is neither ObjectPile (`0x60`) nor GetItem (`0x2c`). None
     → the order stops.
  3. For each object in turn: skip it when it is attached (held, `Task_IsAttached`), when its flags (its slot
     `+0x54`) have bit 26, or when it is claimed by this gang (object `+0xec` = the gang) and the claim has not
     run out (object `+0xf0` > *now*). With the pile option it must be of the type named `dyn_pile` (type list
     entry `+0x43`); otherwise its type's kind byte (`+0x87`) must be 4 or 5.
  4. **Reachable**: a point on the navigation mesh at the object's edge toward the member (`Nav_FindPolygonUnder`
     with radius max(type `+0x78`, `+0x7c`) along the direction from the object to the member), and a path from
     the member to it (`Nav_CanReach`). Not reachable: the same member is tried on the next object.
  5. Reachable: the count goes up; when a Melee goal (`8`) is on his stack, every goal above it is popped; the
     object is claimed (`+0xf0` = *now* + 5000, `+0xec` = the gang); the member gets `Goal_ObjectPile(him, object,
     −1)` with the pile option, else a pushed GetItem goal (`GetItemGoal_Init(goal, brain, object, 4, −1)`). The
     next object gets a new member.
  6. Stops when the objects run out or the count is reached.

### The ObjectPile goal (type `0x60`, `0x002bada8`-`0x002bb3cc`) {#object-pile}

Confirmed (code) at each address; read in Ghidra on 2026-10-13. The member walks to the pile, picks an object up,
throws it at the nearest enemy he can see, and goes back for the next, until the pile is gone, his end time passes or
something else takes him over. The class list is on [AI goals](ai-goals.md#goal-object-pile).

**Record** (a goal of the pool, [Goals](ai.md#goals)): `+0x00` brain, `+0x04` started, `+0x05` resume pending,
`+0x08` time limit (−1, none), `+0x0c` vtable `0x0053faf0`, `+0x10` **the pile's handle**, `+0x14` **the throw
target's handle**, `+0x18` **the end time** (0 = none), `+0x1c` **walking** (set while his own GetItem walk is on
top), `+0x1d` **interrupted** (once set, the goal ends).

- **`Goal_ObjectPile(human handle, pile handle, duration)`** (`0x002bada8`): resolves the human; nothing when the
  handle does not resolve to a human. Otherwise allocates a goal, runs Init and pushes it (`Brain_PushGoal`, which
  suspends the old top).
- **Init** (`0x002bae38`): the fields above; `+0x10` = the pile handle, `+0x14` = the null handle (`0x006ebd30`),
  `+0x18` = the duration argument, `+0x1c` = `+0x1d` = 0.
- **Start** (`0x002bae80`, vtable `+0x24`): `+0x18` = 0 when it holds −1, else `+0x18` + *now* (the duration made
  an end time). *Brain* `+0x21c` (the threat response, [The brain](ai.md#brain)) = 0. The Throw order passes −1,
  so its members have no end time.
- **Resume** (`0x002baec0`, `+0x34`): `+0x1c` = 0.
- **Suspend** (`0x002baf08`, `+0x3c`): when `+0x1c` is 0, `+0x1d` = 1. So the goal survives being covered by its
  own GetItem walk (which sets `+0x1c` first), but any other goal pushed over it (a hit reaction, a fight) ends it as
  soon as it is back on top.
- **End** (`0x002baec8`, `+0x2c`): drops his target (`Brain_SetTarget(brain, 0, 0)`) and sets *brain* `+0x21c` = 2.
- **Event** (`+0x4c`): the default `Goal_DefaultOnEvent` (`0x0029ef80`); score hook `+0x54` the default
  `0x0029f3a8`.

**Process** (`0x002bb010`), each update; returns 0 (go on) unless it says done (2):

1. `+0x1d` set: done.
2. The pile's handle no longer resolves to a world object: done.
3. Actions queued (*brain* `+0x2e` > 0): return 0.
4. Mid-throw (`Human_IsSwingingHeld`, `0x002286a0`: held flag `0x800000`, or `0x1000010` while holding an object
   and playing one of the object throw clips): return 0.
5. `+0x18` not 0 and below *now* (unsigned): done.
6. **Armed?** He is armed for the pile when `Human_HeldIsThrowable(him, 0)` (`0x00223ea0`) or
   `Human_HoldsKind4Or6` (`0x00224000`) is true. For an AI (pad −1) the first is true only when the held object's
   type kind byte (`+0x87`) is 5, or it is 1 with the type byte `+0x86` = 11 (the knife); the second when the kind
   byte is 4 or 6.
7. **Not armed** (fetch):
    1. Holding anything else: drop it (`Human_DropHeld`, `0x00257f38`). Then drop the target
       (`Brain_SetTarget(brain, 0, 0)`).
    2. Squared distance from his position to the pile's position (world object `+0x10`) at most 1.0 (within 1 m):
       queue a PickUpItem action on the pile object (`PickUpItemAction_Init(action, brain, pile, delay 33 ms)`,
       `0x002faf98`; nothing when the action pool is full); return 0.
    3. Farther: leave the fight stance (`0x0022fef0`), `+0x1c` = 1, and push a GetItem goal on the pile
       (`GetItemGoal_Init(goal, brain, pile handle, gait 4, limit −1)`, [GetItem](ai.md#get-item)); return 0. The
       push suspends this goal, which `+0x1c` keeps alive; GetItem's end resumes it, clearing `+0x1c`.
8. **Armed** (throw):
    1. The target is `+0x14` resolved and checked with `Brain_ValidateEnemy(brain, it)` (`0x0028d358`). None: on
       every 10th update (*brain* `+0x34` % 10 = 0) pick one (`ObjectPileGoal_PickTarget`, below); return 0.
    2. No line of sight to him (`Human_HasLineOfSight(self, him, 0, 0)`, [Sight](ai.md#sight)): `+0x14` = the null
       handle; return 0.
    3. `d²` = squared distance between the two; `R²` = `Attack_ReachSquared(d², self, kind 0)` (`0x00230d00`, the
       reach of attack kind 0 with what he holds: 30 m for held set 5, 15 m for set 4, else the attack table's
       reach; [Combat code index](combat.md#code-index)).
    4. `d²` > `R²` (out of reach): enter the fight stance (`0x0022fe80`) and queue a MoveMelee action on him
       (`0x002fcf50`, [MoveMelee](ai-moving.md#move-melee)) with the band from 2 *r* to max(`R`, 2 *r* + 1) m, *r*
       his capsule radius (0.35 × his scale, `0x00219860`), no delay, limit 2000 ms; return 0.
    5. In reach: only on every 30th update (*brain* `+0x34` % 30 = 0), else return 0. Set him as the target with
       the slot forced (`Brain_SetTarget(brain, him, 1)`). Unless the attack cool-down is over (*now* ≥ *brain*
       `+0x1e8`), return 0. Queue a TurnTo action to his position at his maximum turn rate (`Human_MaxTurn`,
       `0x002213d8`; no delay; skipped when the pool is full), then `Brain_QueueAttack(brain, him, kind 0, delay 33
       ms, time −1.0)`, which queues one attack action of kind 0 (the throw, for an armed human). Return 0.

**`ObjectPileGoal_PickTarget`** (`0x002baf20`): `+0x14` = the nearest of a list to his position
(`HandleArray16_SortNearest`, then entry 0). The list is his enemy list (*brain* `+0x164`, 16 handles) when it is
not empty; otherwise a fresh list of the humans within his sight range (`Brain_GetSightRange`) that he can see
(`Humans_ListNearVisible`, `0x0029c7e8`) and that pass `Filter_IsThrowTarget` (`0x0029c0d8`: not cuffed, a threat to
him, `Brain_ValidateEnemy`). An empty list gives the null handle. The enemy list is not filtered by sight here: a
listed enemy out of sight is picked, then dropped at step 8.2 on the next update.

**Tests (synthetic):** a member 0.8 m from a pile (PickUpItem queued, no goal pushed) and 6 m from it (GetItem
pushed with gait 4, the goal survives the suspend and `+0x1c` clears on resume); another goal pushed over it (done
on its next Process); an end time passed and none (−1); armed with a kind-5 object and an enemy in sight beyond the
reach (a MoveMelee band from 0.7 m to the reach, for a target of scale 1) and in reach (turn plus attack only on
updates divisible by 30, and only after the cool-down); a target out of sight (cleared, re-picked on the next 10th
update); an empty enemy list (the visible-humans fallback); the pile object removed (done).

**Tests of the order (synthetic):** three throwables at 5, 10 and 40 m and three free members (two ordered, nearest
object first); an object claimed 2 s ago by the same gang (skipped) and by another gang (taken); pile option with a
`dyn_pile` and a crate; an unreachable object (the same member gets the next one); End pops ObjectPile.

## Unit 7 and later: what each needs first {#unit-7}

Unit 7 (the fight goal's Start, Resume, Suspend and End, the fidget and taunt actions, the pattern block, and the
waiting ring of [The reposition](ai.md#fight-reposition), gang-fight need 7) is described by its sections and by
the three below: the fidget and taunt actions and `Brain_TryPatternBlock`. Gang-fight needs 8 (event `0x15`'s
taunts; its senders are on [Gang fights](gang-fights.md#waiting) rule 5) and 11 (the tandem, issue #84) are not
units yet.

The two actions use the action base of [Actions](ai.md#actions): `+0x00` brain, `+0x04` delay (s16 ms), `+0x06`
started, `+0x08` vtable; Start returns 2 for "already done" (the action is dropped with only its destroy run) and 0
to go on; Abort gets a **mode**, 0 when the action ended normally (`Brain_PopAction(brain, 0)` after Update returned
2) and 1 when it is cleared (`Brain_ClearActions`, which stops at the first action whose Abort returns 0). In both
Starts, the **line** is said with `Human_SayCommand(volume, human, line, 0, 0, 1, null handle, 1)` (`0x002205e0`,
[Saying a speech command](sound.md#speech)) at volume 1.0, or 1.5 when his current target is a pad human (target
`+0x1b0` ≠ −1); a line of 0 says nothing.

### The fidget action (`PlayFidget`, type 15) {#fidget-action}

Confirmed (code) at each address; read in Ghidra on 2026-10-13.

**Init** (`FidgetAction_Init(action, brain, anim, idle, line, delay)`, `0x002f9f48`): vtable `0x00542c60`;
`+0x0c` the **anim id** (or substitution group), `+0x10` the **idle** flag, `+0x14` the **line** (s16), `+0x04` the
delay. The fight goal's reposition passes group `0x25b` (the combat fidgets), idle 0, its line; FollowAndDefend's
fidget passes anim 0 with idle 1 (`0x002b32d4`, `0x002bd574`).

**Start** (`FidgetAction_Start`, `0x002f9f78`):

1. Done (2) for a cop brain (*brain* `+0x04` = 1), a dog (human `+0xcc`, the class id, = `0xdd`), or a class whose
   role byte (`CharClass` `+0x11b`) is 6.
2. **Free to fidget.** Required unless idle is 0 and he holds a world object whose type byte `+0x86` is 11 (the
   knife): `Human_IsFreeForFidget` (`0x00225f58`: no held flag at all, none of the state flags `0x7bf9e9f7ff0`,
   nothing held). Not free: done.
3. Say the line (above).
4. Idle 0 on a dealer brain (*brain* `+0x04` = 5): done, after the line.
5. Play it: `Human_PlayHeldIdle(human, anim, idle)` (`0x00231ac8`) plays, as an overlay clip and only when no held
   flag is set, group `0x25c` (the idle fidgets) when idle is 1; with idle 0, anim `0x25a` when he holds a type-11
   object, else the given anim. Return 0.

**Update** (`0x002fa1e0`): 0 while held flag `0x100` (a fidget plays) is set, else done.

**Abort** (`FidgetAction_Abort(action, mode)`, `0x002fa0e0`):

1. **Refused (0)** only when all hold: mode 1 (cleared); a Warrior brain (*brain* `+0x04` = 3); idle 1; his gang's
   (*brain* `+0x20c`) leader is a pad human; the gang has a tactic (*gang* `+0x40` ≠ 0); the leader's gait is idle;
   no member of the gang has attackers (`Gang_AnyMemberHasAttackers`, `0x00169658`); and the leader's last command
   (game state `*0x0051489c` + leader `+0x1b0` + `0x41a`, [Warrior commands](ai-gangs.md#warrior-commands)) is not
   0, 4 or 5. So an idle Warrior's fidget is not cut by a re-planned follow while the chief stands still and nobody
   fights.
2. Otherwise stop the fidget (`Human_StopFidget`, `0x00231b60`: while held flag `0x100` is set, drop the overlay
   when he is moving, else rebuild the idle when his clip is `0x25b` or `0x25c`) and accept (1).

**Tests (synthetic):** a cop, a dog and a class-6 human (done at once); a human holding a crate (not free, done) and
one holding a knife with idle 0 (plays `0x25a`); a line said at 1.0 and at 1.5 (target a pad human); a dealer with
idle 0 (line, then done); an AI Warrior's idle fidget cleared while the chief stands with command 1 (refused) and
with command 4 (stopped); a normal end (stopped).

### The taunt action (`Taunt`, type 29) {#taunt-action}

Confirmed (code) at each address; read in Ghidra on 2026-10-13. This corrects the class table of
[AI code](ai-code.md#actions), which read the cop test as the class kind and the class exemption as taunt kind 13.

**Init** (`TauntAction_Init(action, brain, kind, arg, line, delay)`, `0x002fb0f0`): vtable `0x00542e60`; `+0x0c` the
**kind**, `+0x10` a second argument (passed on to the clip builder, which ignores it), `+0x14` **wait** = 1,
`+0x18` the **line** (s16), `+0x04` the delay.

**Start** (`TauntAction_Start`, `0x002fb128`):

1. Done (2) for a cop brain (*brain* `+0x04` = 1) or a class whose role byte (`CharClass` `+0x11b`) is 6.
2. **The clip**, unless the kind is 8 (a line with no clip): when he is not free to fidget (as above) he is done,
   except a class whose role byte is 13, which goes on. Then `Human_BuildAnimState(human, kind, arg, 0)`
   (`0x0025f028`): for a kind 0-7, and only when the human has an animation controller (human `+0xd8`), it plays anim
   id `0x253` + kind (`0x253`-`0x25a`) as an animation task (`AnimTask5_Construct`, `0x00106240`) with 0.05 s
   blends, first setting held flag (`Human_SetHeldFlag`, `0x00226640`) `0x200000` for the anims `0x256`-`0x259`
   (kinds 3-6) and `0x100000` for the others. A kind of 8 or more plays nothing.
3. Say the line (above).
4. `+0x14` (wait) = 0 for kinds 0, 1, 2 and 8, else 1. Return 0.

The kinds the shared event handler uses ([The shared event handler](ai.md#shared-events)) are 3 and 4, anims 598
and 599 (`0x256`, `0x257`).

**Update** (`0x002fb2f0`): with wait 0, done at once; with wait 1, 0 while held flags `0x300000` (either taunt
flag) are set, else done.

**Abort** (`TauntAction_Abort(action, mode)`, `0x002fb278`):

1. Mode 0 (normal end): accept (1).
2. Otherwise stop the taunt first (`Human_StopTaunt`, `0x00231c58`: while held flags `0x300000` are set, drop the
   overlay when he is moving, else rebuild the idle when his clip is in `0x253`-`0x25a`). Then refuse (0) only when
   wait is 1 and flags `0x300000` are still held; else accept (1).
3. Every accepted abort decrements the global `0x005112ac`. Nothing else in the executable reads or writes it (the
   only code references are the two in this function), so it is a count that is never raised; inferred vestigial,
   and Coney need not keep it.

**Tests (synthetic):** a cop and a class-6 human (done); kind 8 with a line (no clip, done on the next update); kind 3
on a free human (anim `0x256`, flag `0x200000`, waits); kind 0 (anim `0x253`, no wait); a busy human (done) and a busy
class-13 human (plays); cleared while the clip plays (stopped; refused while the flags remain).

### `Brain_TryPatternBlock(brain, attacker)` (`0x00292158`) {#pattern-block}

Confirmed (code); read in Ghidra on 2026-10-13. Called by the shared event handler on an attack warning (event
`0x10`) when the attacker has a pad or is a Warrior ([The shared event handler](ai.md#shared-events), step 4.4),
after *brain* `+0x200` was raised for this warning. It lets an AI in a fight block a player who repeats one pattern.

1. `t` = his own class threshold, `Human_GetBlockPatternThreshold` (`0x002236c8`): (16 − class `+0x37`) / 16, or 0
   when `+0x37` is 0. `t` = 0: nothing.
2. The attacker's pattern: `f0` = byte `+0x5d0` × 0.05, `f1` = byte `+0x5d1` × 0.05 (`0x00418398`). Neither
   `f0` ≥ `t` nor `f1` ≥ `t`: nothing.
3. His actions are blocked (`HumanRecord_AreActionsBlocked`, `0x00228228`: any of the state flags `0x7bf9e9f7ff0`):
   nothing.
4. His top goal (*brain* `+0x2c` the top index, goals at *brain* `+0x40`) must exist and be a Fight goal (type
   `0xf`); else nothing.
5. Clear his actions (`Brain_ClearActions`). When one refused (the queue, *brain* `+0x2e`, is not empty): nothing.
6. `Goal_TryBlock(fight goal, 0)` (`0x0029f098`, [Blocking](ai.md#block)): with the chance argument 0 it uses his
   block chance, and it **always** blocks when **his current target** (*brain* `+0x124`, through
   `Brain_ValidateEnemy`) is pad-controlled (per-player `+0x1b`) and meets the same pattern test. So the block is
   certain only when the warning's attacker is the human he is fighting; another attacker gets the ordinary chance
   (a quarter of it for a Warrior). It pushes the block goal (`0x1b`) unless one is already above his goal base
   (*brain* `+0x2d`) and returns 1 when it pushed.
7. When it pushed: if he is stunned and standing (`Human_IsStunnedStanding`, `0x00223bc0`: state `0x100000` without
   `0x80000`), end the stun (`Human_EndStun`, `0x0022f8d8`); then write command 4 (block) to his per-player record
   (`0x00660f50` + index × `0x2c`, `+0x20`). As [Blocking](ai.md#block) says, command 4 does not make an AI block;
   the block goal's other effects do.

**Tests (synthetic):** a class threshold of 0 (nothing); a player whose pattern byte reaches `t` attacking his
current target, who is in a Fight goal with an empty queue (block goal pushed, command 4, a standing stun ended);
the same with the pattern below `t`, with a non-Fight top goal, with an action that refuses to abort, and with the
attacker not his target (the ordinary chance, seeded).

## Reimplementation plan {#plan}

Units in order. 1-7 come first (gang combat is the project's top priority) and need little else. Then the brain
core and the actions, which the rest call; then the fight goals and the tactics; then the **crew** units, which wait
for the open crew and follow work (follow goals and the move retarget, `fix-pathing`; scatter and steal commands,
`fix-crew-commands`) to merge, so they do not collide with it; then police, civilian and world goals, and the
bosses last. Within a family, units follow the address order and keep a class together (a class of more than 15
functions is split in twelves). The full lists, with each function's address, name, size and citing section, are
kept by the track (`UNITS.md`); the "Spec" column links the sections that describe each unit. Sizes are bytes.

| Unit | Family | Title | Functions | Bytes | Spec |
| ---: | --- | --- | ---: | ---: | --- |
| [1](#unit-1) | gang fights | Threats and the gang's shared target | 3 | 644 | [Unit 1](#unit-1) |
| [2](#unit-2) | gang fights | TacticAttack in full (without team-ups) | 11 | 1,852 | [Unit 2](#unit-2) |
| [3](#unit-3) | gang fights | Team-up framework: strategy slot, sub-tactic pool and the draw | 16 | 1,864 | [Unit 3](#unit-3) |
| [4](#unit-4) | gang fights | Charge, Get-their-leader and Grab orders | 12 | 2,736 | [Unit 4](#unit-4) |
| [5](#unit-5) | gang fights | Guard orders and the FightBackOff goal | 10 | 2,572 | [Unit 5](#unit-5) |
| [6](#unit-6) | gang fights | Throw orders and the ObjectPile goal | 10 | 3,288 | [Unit 6](#unit-6) |
| [7](#unit-7) | gang fights | Fight goal life cycle, waiting ring, fidget and taunt actions | 13 | 1,944 | [Unit 7](#unit-7) |
| 8 | core | Brain core: `Steering`, `Brains` | 11 | 1,444 | [brain-steering](ai-code.md#brain-steering), [original-structure](ai.md#original-structure), [ai-pools](ai-code.md#ai-pools) ... |
| 9 | core | Brain core: `Brain` | 12 | 3,540 | [brain](ai.md#brain), [brain-record](ai-code.md#brain-record), [moving](ai.md#moving) ... |
| 10 | core | Brain core: `Brain` | 12 | 1,544 | [brain-record](ai-code.md#brain-record), [perception](ai-code.md#perception), [enemy-list](ai-code.md#enemy-list) ... |
| 11 | core | Brain core: `Brain` | 12 | 2,068 | [brain-events](ai-code.md#brain-events), [attack-slots](ai-code.md#attack-slots), [brain-record](ai-code.md#brain-record) ... |
| 12 | core | Brain core: `Brain` | 12 | 1,136 | [goal-stack](ai-code.md#goal-stack), [attack-slots](ai-code.md#attack-slots), [update](camera.md#update) ... |
| 13 | core | Brain core: `Brain` | 12 | 1,440 | [enemy-list](ai-code.md#enemy-list), [perception](ai-code.md#perception), [wanted-timers](ai-code.md#wanted-timers) ... |
| 14 | core | Brain core: `Brain` | 12 | 1,508 | [wanted-timers](ai-code.md#wanted-timers), [attack-slots](ai-code.md#attack-slots), [attack-action](ai.md#attack-action) ... |
| 15 | core | Brain core: `Brain` | 12 | 1,452 | [brain-events](ai-code.md#brain-events), [brain-record](ai-code.md#brain-record), [script-levers](ai-scripting.md#script-levers) ... |
| 16 | core | Brain core: `Brain` | 12 | 1,308 | [script-bindings](ai-code.md#script-bindings), [perception](ai-code.md#perception), [address-person](ai-scripting.md#address-person) |
| 17 | core | Brain core: `Brain` | 8 | 2,104 | [perception](ai-code.md#perception), [enemy-list](ai-code.md#enemy-list), [engage-enemy](ai.md#engage-enemy) ... |
| 18 | core | Brain core: `AI` | 11 | 840 | [ai-pools](ai-code.md#ai-pools), [brain-events](ai-code.md#brain-events), [perception](ai-code.md#perception) |
| 19 | core | Brain core: `Goal` | 12 | 2,556 | [original-structure](ai.md#original-structure), [ai-pools](ai-code.md#ai-pools), [goal-left-turf](ai-goals.md#goal-left-turf) ... |
| 20 | core | Brain core: `Goal` | 12 | 6,288 | [goal-ped-logic](ai-goals.md#goal-ped-logic), [goal-call-police](ai-goals.md#goal-call-police), [crimes](ai-gangs.md#crimes) ... |
| 21 | core | Brain core: `Goal` | 12 | 2,788 | [goal-copper-patrol](ai-goals.md#goal-copper-patrol), [path-planning](ai-moving.md#path-planning), [goal-respond](ai-goals.md#goal-respond) ... |
| 22 | core | Brain core: `Goal` | 12 | 2,164 | [goal-cower](ai-goals.md#goal-cower), [goal-call-gang](ai-goals.md#goal-call-gang), [goal-mark](ai-goals.md#goal-mark) ... |
| 23 | core | Brain core: `Goal` | 7 | 1,556 | [goal-join-fixed-scene](ai-goals.md#goal-join-fixed-scene), [goal-join-animation](ai-goals.md#goal-join-animation), [goal-shot-tests](ai-goals.md#goal-shot-tests) ... |
| 24 | core | Brain core: `Formation` | 12 | 2,340 | [formation-code](ai-code.md#formation-code), [ai-config](ai-code.md#ai-config) |
| 25 | core | Brain core: `Formation` | 8 | 2,076 | [formation-code](ai-code.md#formation-code), [formations](ai-gangs.md#formations) |
| 26 | core | Brain core: `Queue`, `Nav` | 14 | 3,036 | [queue-code](ai-code.md#queue-code), [route-jump](ai-pathing.md#route-jump), [queues](ai-moving.md#queues) ... |
| 27 | core | Brain core: `Cfg`, `Peds` | 14 | 532 | [ai-config](ai-code.md#ai-config), [actions](ai-code.md#actions) |
| 28 | core | Brain core: `Perception` | 12 | 3,272 | [perception](ai-code.md#perception) |
| 29 | core | Brain core: `Perception` | 12 | 4,524 | [perception](ai-code.md#perception) |
| 30 | core | Brain core: `Perception`, `Human`, `GameState`, `Queues` | 10 | 3,108 | [perception](ai-code.md#perception), [ai-pools](ai-code.md#ai-pools), [human-filters](ai-code.md#human-filters) ... |
| 31 | core | Brain core: `HeadLook` | 14 | 572 | [perception](ai-code.md#perception) |
| 32 | core | Brain core: `PairLink`, `RouteState` | 12 | 1,456 | [formation-code](ai-code.md#formation-code), [route-code](ai-code.md#route-code), [path-planning](ai-moving.md#path-planning) |
| 33 | core | Brain core: `Route` | 7 | 2,188 | [route-claims](ai-pathing.md#route-claims), [route-jump](ai-pathing.md#route-jump), [route-code](ai-code.md#route-code) |
| 34 | core | Brain core: `Filter` | 12 | 1,496 | [human-filters](ai-code.md#human-filters), [wanted-timers](ai-code.md#wanted-timers), [goal-cop-interact](ai-goals.md#goal-cop-interact) |
| 35 | core | Brain core: `Humans`, `HandleList`, `Handle`, `ObjFilter` | 14 | 2,716 | [human-filters](ai-code.md#human-filters) |
| 36 | core | Brain core: `Ai`, `Glass`, `Sectors`, `LineTestCache` | 12 | 4,812 | [think-gang](ai.md#think-gang), [goal-helpers](ai-goals.md#goal-helpers), [goal-area-walker](ai-goals.md#goal-area-walker) ... |
| 37 | core | Brain core: `FlagNet`, `FlagList`, `Stub`, `AiLists` | 13 | 1,952 | [goal-travel-flag-net](ai-goals.md#goal-travel-flag-net), [goal-flag-list](ai-goals.md#goal-flag-list), [goal-idle](ai-goals.md#goal-idle) ... |
| 38 | actions | Actions: `Action`, `ParlayStartAction`, `PlayAnimAction` | 13 | 1,576 | [goal-stack](ai-code.md#goal-stack), [actions](ai-code.md#actions) |
| 39 | actions | Actions: `DynLoopAnimAction`, `PlayAnimIdAction`, `GenToIdleAnimAction`, `NothingAction` | 12 | 980 | [actions](ai-code.md#actions) |
| 40 | actions | Actions: `PickUpItemAction`, `UnlockHumanAction`, `ReviveAction` | 12 | 752 | [actions](ai-code.md#actions) |
| 41 | actions | Actions: `TagAction`, `UsePhoneAction`, `PlaySoundAction` | 10 | 1,036 | [actions](ai-code.md#actions) |
| 42 | actions | Actions: `MoveAction`, `StopAction`, `MoveMeleeAction`, `DiveAction` | 15 | 4,256 | [actions](ai-code.md#actions) |
| 43 | actions | Actions: `TurnByAction`, `TurnToPointAction`, `LookAroundAction`, `MoveMeleeLineAction` ... | 13 | 2,452 | [actions](ai-code.md#actions) |
| 44 | fight | Fight goals: `Attack`, `Gang`, `ManWeaponPileGoal`, `ObjectThrowerGoal` ... | 13 | 7,988 | [cars](ai-world.md#cars), [formation-code](ai-code.md#formation-code), [human-filters](ai-code.md#human-filters) ... |
| 45 | fight | Fight goals: `AttackTargetGoal`, `AttackTarget`, `EngageEnemyGoal`, `ChaseGoal` | 14 | 5,688 | [goal-attack-target](ai-goals.md#goal-attack-target), [brain-types](ai-code.md#brain-types), [goal-engage-enemy](ai-goals.md#goal-engage-enemy) ... |
| 46 | fight | Fight goals: `Chase`, `ChaseSupportGoal`, `GroundedGoal`, `StunnedGoal` | 15 | 6,192 | [goal-chase](ai-goals.md#goal-chase), [t2-pursue](ai-code.md#t2-pursue), [goal-chase-support](ai-goals.md#goal-chase-support) ... |
| 47 | fight | Fight goals: `OnFireGoal`, `OnFire`, `WoundedGoal`, `Wounded` ... | 13 | 2,604 | [goal-reactions](ai-goals.md#goal-reactions), [brain-events](ai-code.md#brain-events), [block](ai.md#block) |
| 48 | fight | Fight goals: `RingGoal`, `StandGroundGoal`, `GrabTargetGoal` | 15 | 5,004 | [goal-ring](ai-goals.md#goal-ring), [goal-stand-ground](ai-goals.md#goal-stand-ground), [goal-grab-target](ai-goals.md#goal-grab-target) |
| 49 | fight | Fight goals: `Gang0`, `FindEnemyGoal`, `HTLDefenseGoal` | 11 | 2,912 | [goal-copper-guard-arrested](ai-goals.md#goal-copper-guard-arrested), [goal-find-enemy](ai-goals.md#goal-find-enemy), [goal-htl-defense](ai-goals.md#goal-htl-defense) |
| 50 | fight | Fight goals: `HTLDefenseFightGoal`, `HTLOffenseGoal` | 11 | 3,272 | [goal-htl-defense-fight](ai-goals.md#goal-htl-defense-fight), [goal-htl-offense](ai-goals.md#goal-htl-offense) |
| 51 | fight | Fight goals: `HTLOffenseFightGoal`, `HTLGrabbingGoal`, `ThrowObjectGoal`, `TauntPlayerGoal` | 15 | 4,836 | [goal-htl-offense-fight](ai-goals.md#goal-htl-offense-fight), [goal-htl-grabbing](ai-goals.md#goal-htl-grabbing), [goal-throw-object](ai-goals.md#goal-throw-object) ... |
| 52 | fight | Fight goals: `CowerGoal`, `Cower`, `HostileGoal`, `Hostile` | 14 | 3,852 | [goal-cower](ai-goals.md#goal-cower), [goal-hostile](ai-goals.md#goal-hostile) |
| 53 | fight | Fight goals: `CallGangGoal`, `CallGang`, `BackoffGoal`, `BoxerGoal` ... | 11 | 3,144 | [goal-call-gang](ai-goals.md#goal-call-gang), [goal-backoff](ai-goals.md#goal-backoff), [goal-boxer](ai-goals.md#goal-boxer) ... |
| 54 | fight | Fight goals: `AvoidEnemiesGoal`, `AvoidEnemies`, `ConfrontGoal`, `Confront` | 13 | 5,964 | [goal-avoid-enemies](ai-goals.md#goal-avoid-enemies), [goal-confront-start](ai-goals.md#goal-confront-start) |
| 55 | fight | Fight goals: `MaceGoal`, `GrabberGoal`, `GangBrain` | 13 | 5,612 | [goal-mace](ai-goals.md#goal-mace), [goal-grabber](ai-goals.md#goal-grabber), [types](ai.md#types) ... |
| 56 | tactics | Tactics: `Tactic` | 12 | 1,540 | [t1-tactic-core](ai-code.md#t1-tactic-core), [tactics](ai-gangs.md#tactics), [t1-boss](ai-code.md#t1-boss) ... |
| 57 | tactics | Tactics: `Tactic` | 12 | 2,512 | [t1-boss-luther](ai-code.md#t1-boss-luther), [t1-boss-birdie](ai-code.md#t1-boss-birdie), [t1-boss-virgil](ai-code.md#t1-boss-virgil) ... |
| 58 | tactics | Tactics: `Tactic`, `AddressTactic`, `AvoidEnemiesTactic` | 15 | 3,744 | [t2-bindings](ai-code.md#t2-bindings), [t1-address](ai-code.md#t1-address), [t1-avoid-enemies](ai-code.md#t1-avoid-enemies) |
| 59 | tactics | Tactics: `ConfrontTactic`, `CrowdTactic` | 9 | 2,496 | [t1-confront](ai-code.md#t1-confront), [t1-crowd](ai-code.md#t1-crowd) |
| 60 | tactics | Tactics: `DefendTactic`, `DominationTactic` | 8 | 2,580 | [t1-defend](ai-code.md#t1-defend), [t1-domination](ai-code.md#t1-domination) |
| 61 | tactics | Tactics: `HanginOutTactic` | 13 | 3,492 | [t1-hangin-out](ai-code.md#t1-hangin-out) |
| 62 | tactics | Tactics: `HideTactic`, `HoldTheLineTactic` | 10 | 5,520 | [t1-hide](ai-code.md#t1-hide), [tactic-kinds](ai-gangs.md#tactic-kinds), [t1-hold-the-line](ai-code.md#t1-hold-the-line) |
| 63 | tactics | Tactics: `IdleTactic` | 11 | 2,932 | [t1-idle](ai-code.md#t1-idle), [tactic-kinds](ai-gangs.md#tactic-kinds) |
| 64 | tactics | Tactics: `InfoTactic`, `ManWeaponPileTactic` | 12 | 2,684 | [t2-info](ai-code.md#t2-info), [t2-bindings](ai-code.md#t2-bindings), [t2-man-weapon-pile](ai-code.md#t2-man-weapon-pile) |
| 65 | tactics | Tactics: `MoveToFlagTactic` | 8 | 2,076 | [t2-bindings](ai-code.md#t2-bindings), [t2-move-to-flag](ai-code.md#t2-move-to-flag) |
| 66 | tactics | Tactics: `PursueTactic` | 11 | 3,308 | [tactic-kinds](ai-gangs.md#tactic-kinds), [t2-pursue](ai-code.md#t2-pursue) |
| 67 | tactics | Tactics: `RingTactic`, `RiotCopTactic`, `PathScoutTactic` | 13 | 3,376 | [t2-bindings](ai-code.md#t2-bindings), [t2-ring](ai-code.md#t2-ring), [t2-riot-cop](ai-code.md#t2-riot-cop) |
| 68 | tactics | Tactics: `ScoutTactic`, `ShadowTactic` | 13 | 4,052 | [t2-scout](ai-code.md#t2-scout), [t2-bindings](ai-code.md#t2-bindings), [t2-shadow](ai-code.md#t2-shadow) |
| 69 | tactics | Tactics: `StandGroundTactic`, `TauntTactic` | 12 | 2,780 | [t2-stand-ground](ai-code.md#t2-stand-ground), [t2-bindings](ai-code.md#t2-bindings), [t2-taunt](ai-code.md#t2-taunt) |
| 70 | tactics | Tactics: `TravelPathTactic` | 8 | 3,236 | [tactic-kinds](ai-gangs.md#tactic-kinds), [t2-travel-path](ai-code.md#t2-travel-path) |
| 71 | tactics | Tactics: `UseFlagTactic` | 9 | 2,864 | [tactic-kinds](ai-gangs.md#tactic-kinds), [t2-use-flag](ai-code.md#t2-use-flag) |
| 72 | tactics | Tactics: `VandalizeTactic`, `WalkinTallTactic` | 13 | 3,868 | [t2-bindings](ai-code.md#t2-bindings), [t2-vandalize](ai-code.md#t2-vandalize), [t2-walkin-tall](ai-code.md#t2-walkin-tall) |
| 73 | tactics | Tactics: `WanderTactic` | 12 | 4,628 | [t2-bindings](ai-code.md#t2-bindings), [t2-wander](ai-code.md#t2-wander) |
| 74 | crew | Crew: `Player`, `PlayerBrain`, `Follow` | 8 | 3,200 | [brain-record](ai-code.md#brain-record), [original-structure](combat-moves.md#original-structure), [perception](ai-code.md#perception) ... |
| 75 | crew | Crew: `SaveHumanGoal`, `FollowAndAttackGoal` | 12 | 6,496 | [goal-save-human](ai-goals.md#goal-save-human), [goal-follow-and-attack](ai-goals.md#goal-follow-and-attack) |
| 76 | crew | Crew: `FollowAndDefendGoal`, `HoldPositionGoal`, `WarriorVandalStealGoal` | 15 | 9,024 | [goal-follow-and-defend](ai-goals.md#goal-follow-and-defend), [goal-save-human](ai-goals.md#goal-save-human), [goal-hold-position](ai-goals.md#goal-hold-position) ... |
| 77 | crew | Crew: `StealGoal`, `ScatterGoal` | 14 | 3,268 | [goal-steal](ai-goals.md#goal-steal), [goal-steal-process](ai-goals.md#goal-steal-process), [goal-scatter](ai-goals.md#goal-scatter) |
| 78 | crew | Crew: `Scatter`, `FollowPlayerGoal`, `FollowObjectGoal` | 12 | 2,496 | [goal-scatter](ai-goals.md#goal-scatter), [goal-follow-player](ai-goals.md#goal-follow-player), [goal-follow-object](ai-goals.md#goal-follow-object) |
| 79 | crew | Crew: `FollowFormationGoal`, `FollowFormation`, `WarChief`, `WarriorBrain` ... | 13 | 4,516 | [goal-follow-formation](ai-goals.md#goal-follow-formation), [brain-types](ai-code.md#brain-types), [t1-warrior-follow](ai-code.md#t1-warrior-follow) ... |
| 80 | crew | Crew: `HoldTactic` | 5 | 1,228 | [t1-hold](ai-code.md#t1-hold) |
| 81 | crew | Crew: `WarriorScatterTactic`, `StealTactic` | 15 | 3,984 | [t2-warrior-scatter](ai-code.md#t2-warrior-scatter), [t2-bindings](ai-code.md#t2-bindings), [t2-steal](ai-code.md#t2-steal) |
| 82 | crew | Crew: `WarriorAttackTactic`, `WarriorStealTactic`, `StealClusters` | 8 | 1,520 | [t2-warrior-attack](ai-code.md#t2-warrior-attack), [t2-warrior-steal](ai-code.md#t2-warrior-steal) |
| 83 | police | Police: `CallPoliceGoal`, `CallPolice`, `ArrestedGoal`, `Arrested` ... | 14 | 3,984 | [goal-call-police](ai-goals.md#goal-call-police), [goal-reactions](ai-goals.md#goal-reactions), [goal-copper-guard-arrested](ai-goals.md#goal-copper-guard-arrested) ... |
| 84 | police | Police: `CopperGuardGoal`, `CopStation`, `CopperRespondGoal`, `ArrestHumanGoal` ... | 8 | 1,956 | [goal-copper-guard](ai-goals.md#goal-copper-guard), [wanted](crimes.md#wanted), [goal-copper-respond](ai-goals.md#goal-copper-respond) ... |
| 85 | police | Police: `PatrolGoal`, `BreakAndEnterGoal` | 15 | 6,492 | [goal-patrol](ai-goals.md#goal-patrol), [goal-break-and-enter](ai-goals.md#goal-break-and-enter) |
| 86 | police | Police: `RespondGoal`, `RiotCopGoal`, `SpotCriminalGoal`, `CallForBackupGoal` | 15 | 6,664 | [goal-respond](ai-goals.md#goal-respond), [goal-riot-cop](ai-goals.md#goal-riot-cop), [goal-spot-criminal](ai-goals.md#goal-spot-criminal) ... |
| 87 | police | Police: `LeaveAreaGoal`, `DisturbanceGoal` | 11 | 2,792 | [goal-leave-area](ai-goals.md#goal-leave-area), [goal-disturbance](ai-goals.md#goal-disturbance) |
| 88 | police | Police: `CopInteractGoal`, `IssueWarningGoal`, `RiotGoal`, `Riot` | 14 | 5,688 | [goal-cop-interact](ai-goals.md#goal-cop-interact), [goal-issue-warning](ai-goals.md#goal-issue-warning), [goal-riot-helpers](ai-goals.md#goal-riot-helpers) ... |
| 89 | police | Police: `CopBrain` | 7 | 9,812 | [brain-types](ai-code.md#brain-types), [think-cop](ai.md#think-cop) |
| 90 | world | Civilian and world goals: `AreaWalkerGoal`, `HookerGoal`, `PathBlockerGoal`, `Ped` ... | 15 | 7,540 | [goal-area-walker](ai-goals.md#goal-area-walker), [goal-hooker](ai-goals.md#goal-hooker), [goal-path-blocker](ai-goals.md#goal-path-blocker) ... |
| 91 | world | Civilian and world goals: `TravelFlagNetGoal`, `InteractGoal`, `Interact`, `PedInteractGoal` ... | 14 | 6,112 | [goal-travel-flag-net](ai-goals.md#goal-travel-flag-net), [goal-interact](ai-goals.md#goal-interact), [goal-ped-react-noise](ai-goals.md#goal-ped-react-noise) |
| 92 | world | Civilian and world goals: `PedReactionGoal`, `PedestrianGoal`, `Pedestrian` | 12 | 3,688 | [goal-ped-reaction](ai-goals.md#goal-ped-reaction), [goal-pedestrian](ai-goals.md#goal-pedestrian) |
| 93 | world | Civilian and world goals: `Bum`, `BumGoal` | 14 | 2,960 | [bum-logic](ai-scripting.md#bum-logic), [goal-bum](ai-goals.md#goal-bum) |
| 94 | world | Civilian and world goals: `BumLogicGoal`, `PeddlerGoal`, `Peddler`, `GuardFlagGoal` ... | 14 | 2,868 | [bum-logic](ai-scripting.md#bum-logic), [goal-peddler](ai-goals.md#goal-peddler), [goal-guard-flag](ai-goals.md#goal-guard-flag) ... |
| 95 | world | Civilian and world goals: `DealerGoal`, `DealerFlee`, `DealerFleeGoal`, `IdleGoal` ... | 13 | 3,680 | [goal-dealer](ai-goals.md#goal-dealer), [goal-idle](ai-goals.md#goal-idle), [goal-general-parlay](ai-goals.md#goal-general-parlay) |
| 96 | world | Civilian and world goals: `HideGoal`, `TagGoal` | 7 | 1,684 | [goal-hide](ai-goals.md#goal-hide), [path-planning](ai-moving.md#path-planning), [goal-tag](ai-goals.md#goal-tag) |
| 97 | world | Civilian and world goals: `DestroyGoal`, `HangOutGoal` | 14 | 5,212 | [goal-destroy](ai-goals.md#goal-destroy), [goal-hang-out](ai-goals.md#goal-hang-out) |
| 98 | world | Civilian and world goals: `HangOut`, `InvestigateGoal`, `Investigate`, `Roam` | 12 | 6,196 | [goal-hang-out](ai-goals.md#goal-hang-out), [t1-hangin-out](ai-code.md#t1-hangin-out), [goal-chase](ai-goals.md#goal-chase) ... |
| 99 | world | Civilian and world goals: `HarassGoal`, `PlayDynAnimGoal`, `PlayDynIdleGoal`, `PlaySpecialIdleGoal` | 14 | 4,344 | [goal-harass](ai-goals.md#goal-harass), [goal-play-dyn-animation](ai-goals.md#goal-play-dyn-animation), [goal-play-dyn-idle](ai-goals.md#goal-play-dyn-idle) ... |
| 100 | world | Civilian and world goals: `StandIdleGoal`, `Scout`, `PathScoutGoal` | 15 | 3,600 | [goal-stand-idle](ai-goals.md#goal-stand-idle), [goal-scout](ai-goals.md#goal-scout), [goal-path-scout](ai-goals.md#goal-path-scout) |
| 101 | world | Civilian and world goals: `PathScout`, `ReactNoiseGoal`, `ReactNoise` | 13 | 1,940 | [goal-path-scout](ai-goals.md#goal-path-scout), [brain-events](ai-code.md#brain-events), [goal-react-noise](ai-goals.md#goal-react-noise) |
| 102 | world | Civilian and world goals: `MarkGoal`, `Mark`, `MoveToExitFlagGoal`, `MoveToExit` | 12 | 3,236 | [goal-mark](ai-goals.md#goal-mark), [goal-move-to](ai-goals.md#goal-move-to) |
| 103 | world | Civilian and world goals: `MoveToFlagNetFlagGoal`, `MoveToUseFlagGoal`, `UseFlag` | 15 | 3,428 | [goal-move-to](ai-goals.md#goal-move-to), [tactic-kinds](ai-gangs.md#tactic-kinds) |
| 104 | world | Civilian and world goals: `MoveToPositionGoal`, `MoveToHumanGoal`, `DestroyItemGoal`, `VandalizeItemGoal` | 15 | 3,160 | [goal-move-to](ai-goals.md#goal-move-to), [goal-warrior-vandal-steal](ai-goals.md#goal-warrior-vandal-steal), [goal-destroy-item](ai-goals.md#goal-destroy-item) ... |
| 105 | world | Civilian and world goals: `DestroyCarGoal`, `DestroyCar`, `TrackHumanGoal`, `TravelPath` ... | 15 | 4,208 | [goal-warrior-vandal-steal](ai-goals.md#goal-warrior-vandal-steal), [goal-destroy-car](ai-goals.md#goal-destroy-car), [goal-track-human](ai-goals.md#goal-track-human) ... |
| 106 | world | Civilian and world goals: `DevilRun`, `WanderGoal`, `ShadowGoal`, `PlayParlayAnimationGoal` ... | 13 | 3,016 | [goal-devil-run](ai-goals.md#goal-devil-run), [goal-wander](ai-goals.md#goal-wander), [goal-shadow](ai-goals.md#goal-shadow) ... |
| 107 | world | Civilian and world goals: `JoinFixedSceneGoal`, `JoinCinematicGoal`, `JoinAnimationGoal`, `Shopkeeper` | 14 | 1,856 | [goal-join-fixed-scene](ai-goals.md#goal-join-fixed-scene), [goals](ai.md#goals), [humans](scenes.md#humans) ... |
| 108 | world | Civilian and world goals: `ShopkeeperGoal`, `Shot` | 8 | 3,136 | [goal-shopkeeper](ai-goals.md#goal-shopkeeper), [goal-shot-tests](ai-goals.md#goal-shot-tests) |
| 109 | world | Civilian and world goals: `HideAndSeekGoal`, `LeftTurfGoal` | 14 | 5,804 | [goal-hide-and-seek](ai-goals.md#goal-hide-and-seek), [t1-boss-virgil](ai-code.md#t1-boss-virgil), [goal-guard-flag](ai-goals.md#goal-guard-flag) ... |
| 110 | world | Civilian and world goals: `RunFromTrainGoal`, `CivilianBrain` | 14 | 8,620 | [goal-run-from-train](ai-goals.md#goal-run-from-train), [brain-types](ai-code.md#brain-types), [think-civilian](ai.md#think-civilian) |
| 111 | world | Civilian and world goals: `DealerBrain`, `CivlCoDiBrain`, `ShopkeeperBrain` | 8 | 3,116 | [brain-types](ai-code.md#brain-types), [think-dealer](ai.md#think-dealer), [types](ai.md#types) ... |
| 112 | boss | Bosses and special fighters: `BossDiego`, `BossDiegoScript`, `BossLizzies`, `BossLizziesGoal` ... | 14 | 4,996 | [goal-boss-diego](ai-goals.md#goal-boss-diego), [goal-boss-lizzies](ai-goals.md#goal-boss-lizzies), [goal-boss-luther](ai-goals.md#goal-boss-luther) ... |
| 113 | boss | Bosses and special fighters: `BossChatterGoal`, `BossDiegoGoal`, `BigBrawlerGoal` | 9 | 9,752 | [goal-boss-chatter](ai-goals.md#goal-boss-chatter), [goal-boss-lizzies](ai-goals.md#goal-boss-lizzies), [melee-range](ai.md#melee-range) ... |
| 114 | boss | Bosses and special fighters: `BigFighterGoal` | 12 | 3,076 | [goal-big-fighter](ai-goals.md#goal-big-fighter), [t1-boss-moe](ai-code.md#t1-boss-moe), [t1-boss-virgil](ai-code.md#t1-boss-virgil) |
| 115 | boss | Bosses and special fighters: `BigFighterGoal` | 6 | 4,656 | [t1-boss-moe](ai-code.md#t1-boss-moe), [goal-big-fighter](ai-goals.md#goal-big-fighter) |
| 116 | boss | Bosses and special fighters: `BigFighterAGoal` | 13 | 5,232 | [goal-big-fighter-a](ai-goals.md#goal-big-fighter-a), [brain-record](ai-code.md#brain-record), [t1-boss-chatterbox](ai-code.md#t1-boss-chatterbox) |
| 117 | boss | Bosses and special fighters: `BigThrowerGoal`, `BigLedgeThrowerGoal` | 13 | 2,492 | [goal-big-thrower](ai-goals.md#goal-big-thrower), [goal-big-ledge-thrower](ai-goals.md#goal-big-ledge-thrower) |
| 118 | boss | Bosses and special fighters: `StationaryThrowerGoal` | 4 | 816 | [goal-stationary-thrower](ai-goals.md#goal-stationary-thrower) |
| 119 | boss | Bosses and special fighters: `StationaryShooterGoal` | 12 | 3,776 | [goal-stationary-shooter](ai-goals.md#goal-stationary-shooter) |
| 120 | boss | Bosses and special fighters: `StationaryShooterGoal` | 6 | 3,328 | [goal-stationary-shooter](ai-goals.md#goal-stationary-shooter) |
| 121 | boss | Bosses and special fighters: `ShooterGoal` | 15 | 5,300 | [goal-shooter](ai-goals.md#goal-shooter), [t1-boss-luther](ai-code.md#t1-boss-luther) |
| 122 | boss | Bosses and special fighters: `StationaryShooterAGoal`, `LutherShooterGoal` | 6 | 4,152 | [t1-boss-luther](ai-code.md#t1-boss-luther), [goal-luther-shooter](ai-goals.md#goal-luther-shooter) |
| 123 | boss | Bosses and special fighters: `StationaryShooterBGoal` | 12 | 4,780 | [goal-stationary-shooter-b](ai-goals.md#goal-stationary-shooter-b) |
| 124 | boss | Bosses and special fighters: `StationaryShooterBGoal`, `BigDefenderGoal` | 11 | 5,004 | [goal-stationary-shooter-b](ai-goals.md#goal-stationary-shooter-b), [t1-boss-birdie](ai-code.md#t1-boss-birdie), [goal-big-defender](ai-goals.md#goal-big-defender) |
| 125 | boss | Bosses and special fighters: `BigBullGoal`, `BossTactic` | 12 | 7,732 | [goal-big-bull](ai-goals.md#goal-big-bull), [t1-boss](ai-code.md#t1-boss) |
| 126 | boss | Bosses and special fighters: `BossDiegoVargasTactic`, `BossLizziesTactic` | 15 | 1,900 | [t1-boss-diego-vargas](ai-code.md#t1-boss-diego-vargas), [t1-boss-lizzies](ai-code.md#t1-boss-lizzies) |
| 127 | boss | Bosses and special fighters: `BossMoeTactic` | 11 | 1,964 | [t1-boss-moe](ai-code.md#t1-boss-moe) |
| 128 | boss | Bosses and special fighters: `BossRoofTactic` | 15 | 2,820 | [t1-boss-roof](ai-code.md#t1-boss-roof) |
| 129 | boss | Bosses and special fighters: `BossLutherTactic` | 13 | 2,072 | [t1-boss-luther](ai-code.md#t1-boss-luther), [brain-events](ai-code.md#brain-events), [slow-motion](camera.md#slow-motion) |
| 130 | boss | Bosses and special fighters: `BossBirdieTactic` | 11 | 2,548 | [t1-boss-birdie](ai-code.md#t1-boss-birdie) |
| 131 | boss | Bosses and special fighters: `BossVirgilTactic` | 9 | 2,164 | [t1-boss-virgil](ai-code.md#t1-boss-virgil) |
| 132 | boss | Bosses and special fighters: `BossChatterboxTactic` | 9 | 1,436 | [t1-boss-chatterbox](ai-code.md#t1-boss-chatterbox) |

## Open questions {#open-questions}

- None open here: what fills game state `+0x280` and `+0x284`, what sends event `0x15` and whether Rumble enables
  the team-ups are answered on [Gang fights](gang-fights.md) (issue #298).
