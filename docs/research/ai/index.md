---
kind: system
system: ai
owns:
  functions: [
    "0x00147ef0", "0x00231090", "0x00231198", "0x0028a360", "0x0028c1a8", "0x0028c3b0", "0x0028d758", "0x0028d7d8",
    "0x0028d910", "0x0028d960", "0x0028d9f0", "0x0028da60", "0x0028db20", "0x0028e248", "0x0028f240", "0x0028f2b0",
    "0x0028f6c0", "0x0028f8b8", "0x0028f928", "0x0028fbb0", "0x0028fe28", "0x00293950", "0x002939b0", "0x00293b28",
    "0x0029ed58", "0x0029edd8", "0x0029ee30", "0x0029eea0", "0x0029eed8", "0x002f9e48", "0x00419ee0", "0x0041b4f0",
    "0x0041b5e0", "0x0041c2b0", "0x0041c2d0", "0x0041c338", "0x0041c4c8", "0x0041d080", "0x0041d088", "0x0041d738",
    "0x0041d748", "0x0041d758", "0x0041d770", "0x0041d978", "0x0041da50", "0x0041da60", "0x0041da70", "0x0041db08",
    "0x0041dbe8", "0x0041dc80", "0x0041dcf0", "0x0041dd40", "0x0041dd68", "0x004edd40", "0x004edd48",
  ]
---
# AI humans: brains, goals and actions

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records or, for the older ones, How this was checked.

## Summary

How a human that no pad controls decides what to do and does it: the per-human **brain**, its **goal stack**, its
**reaction goal** and its **action queue**; how a brain picks a target, chooses to attack, block or move, and how it
moves; and which brains the first mission (`level99`) uses.

In one paragraph: every human has a **brain** (0x2f0 bytes) with a **type** taken from its character class (0 the
player, 1 cops, 2 gang soldiers, 3 the Warriors, 4 civilians and bums, 5 dealers, 6 one civilian kind). Each brain has
three handlers chosen by its type: an **update** run every character step, a **think** run one step in five, and an
**event** handler. For every AI type the update is the same function: run the **reaction goal** if the human's state
calls for one (grabbed, knocked down, stunned...), else the **top goal** of a stack of up to ten, then the front
**action** of a queue of eight. Goals are long-lived intentions (fight this human, walk to that flag, block for a
while); actions are short steps (attack once, move to the target, play a clip). An attack action does not animate
anything itself: it writes a **command id**, the same id a pad press produces, into the human's per-player record,
and the human's dispatcher plays it exactly as it would for the player ([Tasks](../tasks.md#humans-update)). The fight
goal paces attacks with tokens, attack slots and a per-class delay, and picks each attack by **weighted chance** from
the class's 45-entry attack table (`Att_*` in `config_preload2.lua`).

The system's topics have their own pages: [AI fights](../ai-fights.md) (the fight goal, closing on the target, the
enemy scan, the attack action and blocking), [AI brain types](../ai-brain-types.md) (each type's think and event
handlers, help calls and the weapon swap), [AI moving](../ai-moving.md) (gaits, routes and steering) and
[Gang fights](../gang-fights.md) (how a gang shares out a fight). The function list is [AI functions](functions.md).

## Map

The brain, goal and action code lies in `Human/`, in the stretch after `cns/cnsplayertag.cpp`
(`0x0028a360`-`0x00306630` at least, [Source map](../source-map.md#position), inferred from the position); no path
string names its
files. The script bindings are on [AI bindings](../../references/bindings/ai.md). Names are ours. The rows below give
each function's role in a line; the fight's functions are described on [AI fights](../ai-fights.md), the moves' on
[AI moving](../ai-moving.md) and the gangs' on [AI gangs](../ai-gangs.md).

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0021d408` / `0x0021d3e8` | `Human_GetBrain` / `Human_GetPlayerRecord` | brain `0x006d53f0 + i × 0x2f0`, per-player record `0x00660f50 + i × 0x2c` | confirmed (code) |
| `0x00293b28` | `Brains_Update` | step 5 of `Humans_Update` | confirmed (code) |
| `0x0028f6c0` / `0x0028f8b8` | `Brain_Think` / `Brain_Update` | call handler B / handler A | confirmed (code) |
| `0x0028f928` | `Brain_OnEvent` | perception, reaction goal, then handler C | confirmed (code) |
| `0x0028a360` | `Brains_RegisterTypeHandlers` | fills the per-type handler tables | confirmed (code) |
| `0x0028c1a8` | `Brain_InstallHandlers` | gives a brain its type's handlers | confirmed (code) |
| `0x0028fbb0` | `Brain_UpdateGoals` | handler A of every AI type | confirmed (code) |
| `0x0028f2b0` / `0x0028f240` | `Brain_UpdateReactionGoal` / `Brain_EndReactionGoal` | | confirmed (code) |
| `0x0028d758` / `0x0028d7d8` / `0x0028d910` / `0x0028d960` | `Brain_PushGoal` / `_PopGoal` / `_ClearGoals` / `_FindGoal` | the goal stack | confirmed (code) |
| `0x0028d9f0` / `0x0028da60` / `0x0028db20` / `0x0028fe28` | `Brain_AllocAction` / `_PopAction` / `_ClearActions` / `_RunActions` | the action queue | confirmed (code) |
| `0x00293950` / `0x002939b0` | `Goal_Alloc` / `Goal_Free` | | confirmed (code) |
| `0x0029ed58` / `0x0029ee30` / `0x0029edd8` / `0x0029eea0` / `0x0029eed8` | `Goal_Start` / `_Resume` / `_End` / `_Suspend` / `_Process` | | confirmed (code) |
| `0x0028d2e8` | `Brain_Fight` | enemy, target and fight goal | confirmed (code) |
| `0x0028cfe0` / `0x0028df30` / `0x0028de48` | `Brain_SetTarget` / `Brain_ClaimAttackSlot` / `Brain_HasAttackSlot` | | confirmed (code) |
| `0x0028d538` / `0x0028d190` | `Brain_AddEnemy` / `Brain_PushFightGoal` | | confirmed (code) |
| `0x0028e708` / `0x002911f8` | `Brain_PickAttack` / `Brain_GetAttackWeight` | | confirmed (code) |
| `0x0028e248` | `Brain_QueueAttack` | queues one attack kind as one or more attack actions | confirmed (code) |
| `0x002b2c20` / `0x002b3ab0` | `FightGoal_Init` / `FightGoal_Process` | goal type `0xf` | confirmed (code) |
| `0x0029f098` | `Goal_TryBlock` | | confirmed (code) |
| `0x002b54d8` / `0x002b5520` / `0x002b5808` | `BlockGoal_Init` / `_Start` / `_Process` | goal type `0x1b` | confirmed (code) |
| `0x002fa918` / `0x002fa9a8` / `0x002fad70` / `0x002fad30` | `AttackAction_Init` / `_Start` / `_Update` / `_Abort` | | confirmed (code) |
| `0x00231090` / `0x00231198` | `AttackKind_ToCommand` / `AttackKind_ChainDelay` | | confirmed (code) |
| `0x002fb9e8` / `0x002fc5c0` | `MoveAction_Init` / `MoveAction_Update` | | confirmed (code) |
| `0x002fcf50` | `MoveToHumanAction_Init` | | confirmed (code) |
| `0x00147ef0` | `PlayerRecord_SetCommand` | writes per-player `+0x20` | confirmed (code) |
| `0x00223628` / `0x002235f8` / `0x00223800` | `Human_BlockChance` / `Human_CounterChance` / `Human_AttackDelay` | power-class reads | confirmed (code) |
| `0x0029a8c0` / `0x00251a70` / `0x0029aa88` | `Route_Request` / `Route_AStar` / `Route_Follow` | [path planning](../ai-moving.md#path-planning) | confirmed (code) |
| `0x00289138` | `Human_SteerAroundHumans` | [steering](../ai-moving.md#steering) | confirmed (code) |
| `0x002da3b0` / `0x002da588` | `MoveToFlagGoal_Init` / `_Process` | goal type 1 | confirmed (code) |
| `0x0016d170` / `0x00164c20` | `Gangs_Update` / `Gang_OnEvent` | [gangs](../ai-gangs.md#gangs) | confirmed (code) |
| `0x00306690` / `0x003067d8` | `Tactic_Start` / `Tactic_Process` | [tactics](../ai-gangs.md#tactics) | confirmed (code) |
| `0x00293c68` / `0x002956d0` | `Formations_Update` / `Formation_Plan` | [formations](../ai-gangs.md#formations) | confirmed (code) |

The small helpers after `Scripting/ScriptUtilities.cpp`'s anchor (`0x003865d8`-`0x00386b30`, before
`StringTable/`), used mostly by the AI and the player's targeting. That they are the rest of `ScriptUtilities.cpp`
is inferred from position (no stub lies between its anchor `0x003864e0` and `StringTable/`). Names ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x003868d0` | `SortByKeyAscending(items, n)` | `qsort` of 8-byte `{item, float key}` pairs, smallest key first (comparator `0x00386860`): "the nearest wins" in the targeting code (22 callers) | confirmed (code) |
| `0x003868f8` | `SortByKeyDescending(items, n)` | the same, largest key first (comparator `0x00386898`); used by `Human_TeleportNear` | confirmed (code) |
| `0x00386920` | `SortRecordsByKey(items, n)` | the same order on 32-byte records with the key at `+0x04` (`Player_PickTarget`) | confirmed (code) |
| `0x00386950` | `KeyList_Contains(items, n, item)` | whether any 8-byte pair's first word is `item` (`Brain_ScanEnemies`) | confirmed (code) |
| `0x003865d8` | `Points_Cluster(radius, points, n, callback)` | sorts pointers to points by x then y (comparator `0x003867f0`) and groups each point with those within `radius` of the group's first point (an x-window, then the squared distance), calling `callback(group, count)` per group; `0x00320c68` clusters a zone's live objects within 1 m | confirmed (code) |
| `0x00386798` | `Heading_RelativeTo(object, point)` | the heading from the object's position to the point minus the object's own heading (`0x00335f48` of its transform `+0x10`), wrapped to 0-2π (`0x00335d08`) | confirmed (code); argument roles inferred |
| `0x00386980` | `HashTable_Construct(table)` | an SGI-STL `hash_map` with at least 100 buckets: the next prime of the list at `0x0057dfd8` (193), buckets zeroed, no elements; for `Credits_Load` (`0x0050d33c`) and the Rumble character data map (`0x001f1748`) | confirmed (code) |
| `0x003867f0` / `0x00386860` / `0x00386898` | the comparators (*made*) | x-then-y ascending; key ascending; key descending | confirmed (code) |

### Game-state functions {#warriors-functions}

Confirmed (code) at the cited addresses unless a line says otherwise.

Functions of the game-state module (`0x00417af0`-`0x00424e50`: inventory, statistics, flags, configuration
workers) that belong to this page, by address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00419ee0` | `GameState_FindNearestPlayer` | the nearest listed player to a point, the squared distance out; 40 callers in the AI | confirmed (code) |
| `0x0041b4f0` | `GameState_SetWarriorCommandName` | the name at `+0x2fc` that `GameState_CallWarriorCommandCallback` calls with (human, command) ([Warrior commands](../ai-gangs.md#warrior-commands)) | confirmed (code) |
| `0x0041b5e0` | `GameState_CallWarriorCommandCallback` | from `WarriorCommand_Dispatch` | confirmed (code) |
| `0x0041c2b0` / `0x0041c2d0` | `GameState_TurnWarriorCommands` / `GameState_IssueWarriorCommand` | the workers of `TurnWarriorCommands` (all on or off) and `IssueWarriorCommand` (for player 1) | confirmed (code) |
| `0x0041c338` | `WarChief_CheckCrewInRange` | for a war chief: when the nearest crew member is within 50 m clears byte `+0x414 + player` and returns 0; otherwise (unless told to be quiet) says command `0x12`, or `0x5c` when a member is cuffed or out, sets the byte and returns 1; from `WarriorCommand_Dispatch` and `PlayerBrain_Update`; `GameState_CheckGameOver` reads the byte | confirmed (code) |
| `0x0041c4c8` | `GameState_StoreWarriorCommand` | a player's last command (`+0x41a`) and its argument (`+0x42c`) | confirmed (code) |
| `0x0041d080` / `0x0041d088` | `GameState_SetDealerCustomer` / `GameState_IsDealerFree` | the handle at `+0x284`, one human at a time: a scout calling his gang (`ScoutTactic_OnEvent`) or a dirty dealer running off after a rip-off (`DealerGoal_OnBuy`); cleared by `CallGangGoal_Destroy`, `DealerGoal_End` and the level reset; the Warrior attack command's second fallback target ([Gang fights](../gang-fights.md#targets) rule 4) / whether it no longer resolves | confirmed (code) |
| `0x0041d738` / `0x0041d748` | `Cfg_SetEnemySpotting` / `Cfg_SetWarriorSpotting` | `CfgSetEnemySpotting`: `+0x56f8`; `CfgSetWarriorSpotting` clears `+0x56fc` whatever its argument | confirmed (code) |
| `0x0041d758` / `0x0041d770` | `GameState_SetWarriorVandalize` / `GameState_SetWarriorWeapons` | `CfgSetWarriorVandalize` (`+0x5700`) and `CfgWarriorWeapons` (`+0x5704`) | confirmed (code) |
| `0x0041d978` | `Cfg_SetExcludedVandalizeZone` | `CfgExcludedVandalizeZone`: `0x005148bc` (`0xffff` at set-up) | confirmed (code) |
| `0x0041da50` / `0x0041da60` / `0x0041da70` | `Cfg_SetTurfInvasion` / `Cfg_SetGrappleCounters` / `GameState_SetPowerupPickup` | `CfgEnableTurfInvasion` (byte `+0x56e1`), `CfgEnableGrappleCounters` (`+0x56e3`), `CfgPowerupPickup` (`+0x56e5`) | confirmed (code) |
| `0x0041db08` / `0x0041dbe8` | `GameState_EnableAllWarriorCommands` / `GameState_EnableWarriorCommand` | the per-player allowed bytes `+0x41e + player × 7 + command`: all seven for every war chief / one for one human; both tell the command display (`WarCommandDisplay_SetAllowed`) | confirmed (code) |
| `0x0041dc80` / `0x0041dcf0` / `0x0041dd40` | `GameState_IssueWarriorCommandFor` / `GameState_LockWarriorCommands` / `GameState_SetWarriorCommandCallback` | `WarriorCommand_Dispatch` for a human / byte `+0x418 + player` / the callback name | confirmed (code) |
| `0x0041dd68` | `GameState_SetAutoSwitch` | `WCEnableAutomaticSwitching`: byte `+0x431` (1 at set-up), which turns the war chief's [automatic commands](../ai-gangs.md#warrior-auto-commands) on or off | confirmed (code) |

## Data

### The brain {#brain}

Confirmed (code) at the cited addresses unless a line says otherwise.

60 brains of 0x2f0 bytes at `0x006d53f0`, one per human slot. Confirmed (code) at the accessors cited and the
script bindings on [AI bindings](../../references/bindings/ai.md); meanings marked inferred are inferred from how the
field is used.

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | ptr | the human |
| `+0x04` | int | **type** ([below](#types)) |
| `+0x08` | u8 | enabled: `Brains_Update` skips a brain with 0 |
| `+0x09` | u8 | "dead" (`BrDead`): only the script's goals and actions run ([Handlers](#handlers)) |
| `+0x0a` | u8 | suspended (`BrSuspend`) |
| `+0x14` / `+0x1c` / `+0x24` | PMF | handlers A (update), B (think), C (event), each `{s16 delta, s16 vindex, fn}` |
| `+0x2c` | s8 | the goal stack's top index (-1 empty) |
| `+0x2e` / `+0x2f` | s8 | the action count / the front action's slot |
| `+0x30` | u32 | the time of the last update (ms) |
| `+0x34` | int | update counter (every 90 updates `0x0028bec8`) |
| `+0x38` | int | think counter |
| `+0x3c` | ptr | the **reaction goal** ([below](#reaction-goals)); 0 when none |
| `+0x40` | ptr[10] | the **goal stack** |
| `+0x68` | ptr[8] | the **action queue**, circular |
| `+0x90` / `+0x118` | vec4 / float | the point a move aims at / its radius ([Moving](../ai-moving.md#moving)) |
| `+0xa0` | struct | steering state ([Steering](../ai-moving.md#steering)) |
| `+0xe0` | struct | route state ([Path planning](../ai-moving.md#path-planning)) |
| `+0xf8` | struct | perception, refreshed at each think (`0x00298f88`) |
| `+0x120` | u8 | targetable / attack-slot flags |
| `+0x124` | handle | the **target** |
| `+0x110` / `+0x114` | float | the heading to move along / the speed ([Moving](../ai-moving.md#moving)) |
| `+0x12c` | float | field of view (`BrSetFOV`), radians: 1.92 (110°) when made; civilians π/2, dealers 2π, Warriors π |
| `+0x130` | float | sight range: 30 m when made; civilians 15 m, Warriors 55 m ([The enemy scan](../ai-fights.md#enemy-scan)) |
| `+0x134` / `+0x138` | float | hearing radius: 50 m, and 20 m for event 20 (`Brain_IsInHearRange` `0x002935d8`) |
| `+0x13c` / `+0x140` | float | melee range near / far (`BrSetMeleeRange`): **3.0 / 5.0 m** for every brain ([below](../ai-fights.md#melee-range)) |
| `+0x144` | u32 | the enemy-scan interval: 2000 ms, cops 1000 ms |
| `+0x15c` / `+0x160` | fn / u32 | the enemy-scan filter / the time of the last scan |
| `+0x14a`, `+0x14b` | u8 | attack spacing |
| `+0x152` | u8 | hostiles: how many brains hold this human in their enemy lists, on every brain; raised by `Brain_CountHostile` `0x0028ef20` (from `Brain_AddEnemy` `0x0028d538` and [the enemy scan](../ai-fights.md#enemy-scan)), lowered by `Brain_UncountHostile` `0x0028ef30`; a player's is also recounted every 300 updates ([Fight mode](../ai-gangs.md#warrior-follow)) |
| `+0x164` | handle[16] | the **enemy list** |
| `+0x1a4` | handle[16] | the **attack slots** taken on this human; the maximum at `+0x1e4` (`BrSetNumAttackSlots`) |
| `+0x1e8` | u32 | the earliest time this brain may attack again (ms) |
| `+0x1ec` | u32 | the earliest time this human may be attacked again (ms) |
| `+0x1f0` | handle[4] | active attackers |
| `+0x200` | u8 | an attack is coming (set by the attacker's warning, cleared each update); the block's trigger |
| `+0x208` | ptr | an attack-weight table that overrides `+0x298` |
| `+0x20c` | ptr | the human's **gang** ([Gangs](../ai-gangs.md#gangs)) |
| `+0x212` | s8 | the formation this human follows in, −1 none ([Formations](../ai-gangs.md#formations)) |
| `+0x21c` | int | **threat response** (`GangSetThreatResponse`): 0 never fights; **2** when the brain is made (`0x0028a570`, from `Human_Init`, the store at `0x0028a878`); the civilian, dealer and shopkeeper thinks set it to 0 once no fight goal remains ([Brain types](../ai-brain-types.md)) |
| `+0x220` | int | damage response |
| `+0x265` / `+0x266` / `+0x267` | u8 | wants a weapon / a hat / reacts to violence |
| `+0x26c` | int | ped type |
| `+0x284` | int | why the last move failed: 1 no route, 2 or 4 an edge, 3 stuck |
| `+0x298` | u8[45] | **attack weights**, one per attack kind (`BrSetAttackWeight`; filled from the class's `Att_*` table) |
| `+0x2d1` / `+0x2d2` | u8 | world-flag use allowed / a chance byte (1 / 10 when made; no reader of `+0x2d2` found, [World flags](#ai-flags)) |
| `+0x2d4` / `+0x2d5` | u8 | hidden in shadow (`Brain_SetHiddenInShadow`) / sees a hidden human within 2 m ([The enemy scan](../ai-fights.md#enemy-scan)) |
| `+0x2d8` | u8 | join the player's gang |

### Types {#types}

Confirmed (code) at the cited addresses unless a line says otherwise.

`Human_Init` (`0x00218008`) sets the type from the character class's byte `+0x11a` (`CfgChar`'s second argument,
`behaviour` on [Characters](../../references/characters.md)); a human with `+0x19d` set gets type 2 unless its class
says 3. `Human_MakePlayer` (`0x00229c40`) sets 0; giving the human back to the AI (`0x0022a2a8`) sets the class value
again. `Brains_RegisterTypeHandlers` (`0x0028a360`) fills the tables at `0x00715530` (A), `0x00715568` (B) and
`0x007155a0` (C):

| Type | Who (classes on the disc) | A: update | B: think | C: event |
| --- | --- | --- | --- | --- |
| 0 | the player | `0x003035d8` | `0x00303260` | `0x00303e90` |
| 1 | cops (20) | `0x0028fbb0` | `0x00300678` | `0x00302340` |
| 2 | gang soldiers and thugs (249) | `0x0028fbb0` | `0x00304608` | `0x00304fa8` |
| 3 | **the Warriors** (48) | `0x0028fbb0` | `0x003052f0` | `0x003063b0` |
| 4 | civilians and bums (116) | `0x0028fbb0` | `0x002fef40` | `0x002ffb30` |
| 5 | dealers (15) | `0x0028fbb0` | `0x00302cd8` | `0x00303178` |
| 6 | the `civl_co_di` kind (441): shopkeepers, [below](../ai-brain-types.md#think-shopkeeper) | `0x0028fbb0` | `0x00303f10` | `0x00304228` |

So type 3 is the Warriors' brain, which an ally Warrior runs (this answers [Combat](../combat/index.md#open-questions)'s
"brain
type 3"). The player's update `0x003035d8` only keeps books (target validity, attackers counted, the follower check
`0x002d09f0`); it pushes no goals.

### Goals {#goals}

Confirmed (code) at the cited addresses unless a line says otherwise.

Goals come from a pool of 170 of 0x90 bytes at `0x006e0430` (bitmap `0x006ceac0`). Base fields: `+0x00` the brain,
`+0x04` started, `+0x05` resumed, `+0x08` the **time limit** (−1 none), `+0x0c` vtable. The constructor stores a
duration in `+0x08`; `Goal_Start` (`0x0029ed58`) adds the time it starts, and `Goal_Process` (`0x0029eed8`) ends the
goal (returns 2, its Process not called) once the game time has passed it (the time is above the limit, compared
unsigned); −1 never passes. Confirmed (code) at both addresses; this corrects the earlier reading "wait-until time".
`Goal_Process` also returns 0 without calling Process while the human has no character instance (human `+0xd8`), and
starts or resumes the goal first: `Goal_Start` runs only on an empty action queue (else 0) and sets `+0x04` after the
class's Start, and `Goal_Resume` (`0x0029ee30`) starts a goal never started, else marks it resumed (`+0x05`) and runs
its Resume and clears the mark only on an empty queue. `Goal_End` (`0x0029edd8`) calls the class's End only when
`+0x04` is 1, then its destroy: a goal popped before it started is freed without its End. Confirmed (code) at the four
addresses. Vtable function words: `+0x0c`
the type id, `+0x14` the class's name (a string, such as `MoveToFlag`), `+0x1c` destroy, `+0x24` Start, `+0x2c` End,
`+0x34` Resume, `+0x44` **Process**, `+0x4c` event (default `0x0029ef80`). There are 149 goal classes, types 0-158
with ten unused, all on [AI goal types](../../references/goal-types.md) with the bindings that make them; confirmed
(code), each vtable read. The ones this page uses (names the game's):

| Type | Goal | Constructor | Notes |
| --- | --- | --- | --- |
| `0x01` | MoveToFlag | `0x002da3b0` (vtable `0x00542130`) | [`GoalMoveToFlag`](../ai-scripting.md#move-to-flag) |
| `0x06` | MoveToHuman | `0x002dc4f8` | |
| `0x08` | Melee | `0x002ade10` (vtable `0x00540450`) | [below](../ai-fights.md#fight-approach); Process `0x002aebf8` |
| `0x0b` | EngageEnemy | `0x002af5b0` (vtable `0x00540330`) | the run-in, [below](../ai-fights.md#engage-enemy); Process `0x002afa48` |
| `0x0f` | **Fight** | `0x002b2c20` (vtable `0x005402d0`) | [below](../ai-fights.md#fight) |
| `0x12`-`0x1a` | reaction goals | | [below](#reaction-goals) |
| `0x1b` | **Blocking** | `0x002b54d8` (vtable `0x0053feb0`) | [below](../ai-fights.md#block) |
| `0x1f` | GrabTarget | `0x002bb458` | |
| `0x21` | PlayAnimation (a scene) | `0x002e4980` | pushed by `GoalAddressPerson` ([rows](../ai-goals.md#goal-play-animation)) |
| `0x22` | PlayDynAnimation | `0x002d2eb0` | [`GoalPlayDynAnimation`](../ai-scripting.md#dyn-animation) |
| `0x29` | JoinCinematic | `0x002e53e0` (vtable `0x005421f0`) | `Goal_JoinCinematic` (`0x002e5300`); Process `0x002e5618` |
| `0x30` | TrackHuman | `0x002df250` | [`GoalTrackHuman`](../ai-gangs.md#formations) |
| `0x36` | HoldPosition | `0x002be640` (vtable `0x00540510`) | `Goal_HoldPosition` (`0x002be590`), and the tactic code at `0x00313718`; Process `0x002be818` |
| `0x41` | FindEnemy | `0x002c0430` (vtable `0x00540a50`) | [below](../ai-fights.md#fight-approach); Process `0x002c0748` |
| `0x4f` | BumLogic | `0x002abef8` | `GoalBumLogic` |
| `0x57` | AddressPerson | `0x002cc408` | [`GoalAddressPerson`](../ai-scripting.md#address-person) |
| `0x69` / `0x73` | Pedestrian / Patrol | `0x002aae30` / `0x002c1338` | `FlagNetTraverse` |
| `0x6b` / `0x72` | PedReaction / Hostile | `0x002aa558` / `0x002d55d8` | pushed by the civilian think |
| `0x80` | Dealer | `0x002c6d90` | [`GoalDealer`](../ai-scripting.md#dealer) |
| `0x85` | BigFighter | `0x002e9cd0` (vtable `0x00542940`) | `Goal_BigFighter` (`0x002e9c60`); taunt, guard, attack, pick up ([rows](../ai-goals.md#goal-big-fighter)) |
| `0x9b` | Backoff | `0x002d92e8` | |

### Actions {#actions}

Confirmed (code) at the cited addresses unless a line says otherwise.

A pool of 100 actions of 0x50 bytes at `0x006e63d0` (bitmap `0x006cead8`). Base fields: `+0x00` the brain, `+0x04`
s16 delay in ms before it starts (-1 = a random 0-500), `+0x06` started, `+0x08` vtable. Vtable function words:
`+0x14` Start (2 = already done), `+0x1c` Abort (0 = refuses), `+0x24` destroy, `+0x2c` Update (2 = done). The base
update `0x002f9e48` counts the delay down against the brain's `+0x30`. `Brain_ClearActions` stops at an action that
refuses to abort, so an attack in progress is never cut by a new goal; an action that never started is not asked, so
it is freed even when its Abort would refuse (`Brain_PopAction(brain, 1)` → `Action_AbortAndDestroy`, confirmed (code)
at `0x0028db20` and `0x002f9de0`).

`Action_Update` (`0x002f9e48`) takes the time since the brain's last update (`+0x30`, cut to 16 bits). An action not
started counts its delay down by it (drawing 0-500 ms first when it is −1); while some is left it returns 0, else it
calls Start, returns 2 at once when Start says so (`+0x06` stays clear), and otherwise sets `+0x06` and goes on to
Update in the same call. A started action counts the delay down too (never below 0) and calls Update only once it is
0. Confirmed (code) at `0x002f9e48`.

**A normal end runs Abort too.** When the front action's Update returns 2, `Brain_RunActions` (`0x0028fe28`) pops it
with `Brain_PopAction(brain, 0)` (`0x0028da60`). That goes through `Action_AbortAndDestroy` (`0x002f9de0`), which
calls Abort (`+0x1c`, the second argument passed through, here 0) whenever the started byte `+0x06` is 1, then the
destroy (`+0x24`), and frees the action. So every class's Abort is also its "ended" hook, and its clean-up (a restored
boost, a released control) runs however the action ends. Confirmed (code) at the three addresses. An action whose
Start returned 2 never set `+0x06`, so only its destroy runs.

### Attack kinds and commands {#attack-kinds}

Confirmed (code) at the cited addresses unless a line says otherwise.

An attack action carries one of **45 attack kinds**; `AttackKind_ToCommand` (`0x00231090`, jump table `0x0055c170`)
turns it into the command id written to the per-player record ([Commands](../combat/index.md#commands)):

| Kinds | Command |
| --- | --- |
| 0, 32, 43 | `0x10` (cross) |
| 1, 10, 31, 42 | `0xf` (square) |
| 2, 3, 6 | `0x12` (cross pressed, a chain step) |
| 4, 5, 7, 27, 28, 38, 39 | `0x11` |
| 8 / 9 | `0x14` / `0x13` |
| 11 / 12 / 13 | `0x36` / `0x37` / `0x38` |
| 14, 21 | `0xe` (circle held: tackle) |
| 15 | `0x24` |
| 16, 26, 37 | `0x22` (cross + square) |
| 17 | `0x23` (circle + cross) |
| 18 | 0 |
| 19 / 20 | `0x20` / `0x21` (charge / dive) |
| 22, 25, 29, 30, 33, 40 | `0xd` (circle tapped: grab) |
| 23 | `0x39` |
| 24, 35 | `0xf` or `0x10` at random |
| 34, 44 | `0x19` |
| 36 | 5 (L2 held) |
| 41 | `0x31` |

`Brain_QueueAttack` (`0x0028e248`) turns a chain kind into several actions, each later press delayed by
`AttackKind_ChainDelay` (`0x00231198`: the first event time of clip 11, 12 or 16, `0x001017a0`), so the AI presses
inside the chain window as a player would: kind 2 queues kinds 0 then 2 (`X1`, `XX2`), kind 6 or 8 queues 1, 5 and 6
or 8. The kinds follow the [damage table](../combat/index.md#damage-table)'s indices (0 `X1`, 1 `S1`, 2 `XX2`, 3 `SX2`,
4
`XS2`, 5 `SS2`, 6 `SSX3`, 7-9 `SSS3` and its holds, 10 the snap); inferred from the commands and the queued chains.
Every kind with its delay, command and anim ids, and every attack table: [Attack kinds and
tables](../../references/attacks.md).

### The class damage tables {#damage-tables}

Confirmed (code) at the cited addresses unless a line says otherwise.

`CfgChar`'s damage argument names one of eleven tables in `config_preload2.lua` (`DamageWeak`, `DamageNormal`,
`DamageTough`, `DamageBoss`, `DamageGhost`, `DamageCop`, `DamageRookieCop`, `DamageDestroyerLT`, `DamageMoonLT`,
`DamageWarriors`, `DamageFox`), 45 values each, indexed as [the damage table](../combat/index.md#damage-table) (index =
Lua
index − 1). The values at the indices that write damage, for the tables `level99` uses:

| Index (anim) | Weak | Normal | Tough | Boss | Warriors | Fox |
| --- | --- | --- | --- | --- | --- | --- |
| 0 (`X1`) | 23 | 25 | 27 | 30 | 23 | 23 |
| 1 (`S1`) | 14 | 16 | 18 | 20 | 15 | 15 |
| 2 (`XX2`) | 46 | 50 | 54 | 60 | 46 | 46 |
| 3 (`SX2`) | 37 | 41 | 45 | 50 | 38 | 38 |
| 4 (`XS2`) | 37 | 41 | 45 | 50 | 38 | 38 |
| 5 (`SS2`) | 28 | 32 | 36 | 40 | 31 | 31 |
| 6 (`SSX3`) | 51 | 57 | 63 | 70 | 53 | 53 |
| 7 (`SSS3`) | 42 | 48 | 54 | 60 | 46 | 46 |
| 10 (snaps) | 15 | 20 | 20 | 35 | 27 | 27 |
| 24 (grab strikes) | 20 | 40 | 60 | 65 | 50 | 50 |
| 25 (throws) | 30 | 50 | 70 | 75 | 57 | 57 |
| 29 (wall throws) | 80 | 100 | 150 | 300 | 230 | 230 |

Rembrandt's `S1` of 15 (`DamageFox`) at 115 % gives the 17 seen at runtime ([Damage](../combat/index.md#damage-table)).
Evidence: the values are read from the disc's compiled script (confirmed (code) for the reader); the index mapping is
confirmed (code) on Combat.

## Behaviour

### The update {#update}

Confirmed (code) at the cited addresses unless a line says otherwise.

`Brains_Update` (`0x00293b28`), step 5 of [`Humans_Update`](../tasks.md#humans-update), for each of the 60 brains that
is enabled (`+0x08`) and whose human passes `0x0023d790`:

- **Think** (`Brain_Think`, handler B) when `index % 5 == (counter >> 1) % 5`: each brain thinks once every five
  character steps (6 Hz), staggered across brains, with `counter` the tick counter `0x005104f4`; brains whose human
  fails `0x0023d790` do not think at all. A run matched the formula ([How this was checked](#runtime-think)).
- **Update** (`Brain_Update`, handler A) every step.

Then the [waypoint queues](../ai-moving.md#queues). [Formations](../ai-gangs.md#formations) (step 2) and
[gangs](../ai-gangs.md#gang-update) (step 4) run before the brains.

### Handlers {#handlers}

Confirmed (code) at the cited addresses unless a line says otherwise.

`Brain_InstallHandlers` (`0x0028c1a8`): a brain with `+0x09` set ("dead", `BrDead`) gets A = `Brain_UpdateGoals` and B,
C = empty stubs (`0x004edd40` returns, `0x004edd48` returns 0): no think handler (the type's B, which pushes the type's
own goals, [Think handlers](../ai-brain-types.md)) and no events, but A is the same update every live type uses, so
**the goal stack and the actions still run** ([one update](#update-goals); its skip tests read the gang's `+0xd4` and
the brain's `+0x0a`, never `+0x09`). `BrDead` itself (`Brain_SetDead` `0x00292330`) only clears the actions and sets the
byte (`Brain_SetHeld`, which reinstalls the handlers); goals already on the stack, such as a follow goal, go on running
and queue new actions. To freeze a human a script also flushes it (`BrFlush`) or suspends its gang (`GangSuspend`, as
`kinghill` does to its non-player gangs). Confirmed (code); a type-0 brain set dead also gives up the pad (per-player
`+0x1b` = 0). Otherwise A, B and C come from the type's tables. `level99` makes its enemy gangs dead
(`GangBrDead(true)`) while it scripts them, then lifts it for the fights.

### Brain_UpdateGoals: one update {#update-goals}

Confirmed (code) at the cited addresses unless a line says otherwise.

`Brain_UpdateGoals` (`0x0028fbb0`), in order:

1. Bookkeeping (`0x0028ac18`; for types 1 and 2 `0x0028fff8`; `0x002920e0`; `0x0028bbf0`).
2. **Skip** (only the time is kept) while the human is airborne, its gang is suspended (gang `+0xd4`), or the brain is
   suspended and not held.
3. Every 44 updates, a line-of-sight check on the target.
4. **The reaction goal** (`Brain_UpdateReactionGoal`, [below](#reaction-goals)). When it returns 2 (none active),
   **the goal stack**: `Goal_Process` on the top goal; 2 = done (pop it and process the new top in the same update),
   0 = stop for this update, 1 = run again.
5. **The actions** (`Brain_RunActions`, `0x0028fe28`): start the front action when its delay is over, update it, and
   pop it when it returns 2; then the next action in the same update, and so on until one is still running, the queue
   is empty or a pop is refused, so a chain of actions that are done at once all run in one update.
6. The join-the-player's-gang flag.

A goal is **started** (or resumed after the goal above it was popped) only when the action queue is empty
(`Goal_Start` `0x0029ed58`, `Goal_Resume` `0x0029ee30`). `Brain_PushGoal` (`0x0028d758`) suspends the old top
(`0x0029eea0`), clears the actions and pushes; `Brain_PopGoal` (`0x0028d7d8`) takes the top off the stack, ends it
(`Goal_End`) and frees it, then calls `Goal_Resume` on the new top at once (so it starts or resumes in the pop when the
queue is empty), and when the stack is then empty sends the human (vtable `+0x44`) event `0xd` with the human himself
at `+0x24`, which his script handlers and his gang's can answer. `Brain_ClearGoals` (`0x0028d910`) pops until the stack
is empty, then sets the goal base `+0x2d` to −1. `BrFlush` clears both. Confirmed (code) at the cited addresses.

### Reaction goals {#reaction-goals}

Confirmed (code) at the cited addresses unless a line says otherwise.

The brain's `+0x3c` holds a **reaction goal** that has priority over the stack. `Brain_UpdateReactionGoal`
(`0x0028f2b0`) makes one from the human's state word ([Combat](../combat/index.md#state-flags)) when none is active, and
`Brain_EndReactionGoal` (`0x0028f240`) ends it and resumes the stack's top:

| Goal | When the state has | Constructor |
| --- | --- | --- |
| `0x16` | `0x20000` | `0x002b6dc0` |
| `0x12` | grabbing, `0xc0` | `0x002b5a98` |
| `0x13` | tackling, `0x400` | `0x002b65f8` |
| `0x14` | grabbed `0x30` or mugged `0x200` | `0x002b6818` |
| `0x15` | tackled, `0x800` | `0x002b6b88` |
| `0x17` | knocked down, `0x80000` | `0x002b49f8` |
| `0x18` | stunned (`0x100000`) and not down | `0x002b4b88` |
| `0x19` | `0x4000` | `0x002b4c00` |
| `0x1a` | `0x10000` | `0x002b5110` |

The states are tested in the table's order, so a cuffed human (`0x16`, the arrested goal) is tested first, the
stunned one (`0x18`) after the knocked-down, then on fire (`0x19`, `Human_IsOnFire`) and wounded (`0x1a`). Confirmed
(code) at `0x0028f2b0`. Nothing is made, the stack running instead:

- for a civilian or a dealer (types 4, 5) with no fight goal (`Brain_HasFightGoal`), not on fire, brain `+0x2d7`
  clear, no flee goal (`Brain_HasFleeGoal`: `0x6b`, `0x81` or `0x3f` on the stack) and not down or dead
  (`Human_IsDownOrDead`);
- while a grab goal (`0x1f`) is on the stack and not yet holding its target, a Grabber goal (`0x92`) does not allow it,
  or a BigBull goal (`0x89`) is on the stack;
- while a BumLogic goal (`0x4f`) is on the stack, unless there is a fight goal or a flee goal or `+0x2d7` is set;
- with game state `+0x158` clear (one player), for the character classes `0x77`, `0x78` and `0x4f`, and, while stunned
  and standing, for `0x80`, `0xb2`, `0x87` and `0x62`.

Confirmed (code) at `0x0028f2b0`. When it does make one, it suspends the stack's top and clears the actions
(`Brain_SuspendTop`) and returns 0, so neither the stack nor the new goal runs that update: the goal is processed from
the next update by `Goal_Process`, which starts it once the action queue is empty. An active reaction goal is
processed each update; when it returns 2 it is ended (`Brain_EndReactionGoal`: `Goal_End`, freed, `Goal_Resume` on
the top) and the same tests run at once, so a new reaction goal can follow in that update, and with none the function
returns 2 and the stack runs in the same update. Confirmed (code) at `0x0028f2b0` and `0x0028f240`.

This corrects [Combat](../combat/index.md#block): `+0x3c` is the reaction goal, not a tactic, and type `0x17` is the
knocked-down goal, which is why a downed AI never counters.

The reaction goal lags the state by one update, because the brains run before the human's state update
([Tasks](../tasks.md#humans-update)): inferred from that order, and a run shows it
([How this was checked](#runtime-reaction-goals)).

**A knocked-out human's brain stops** (there is no reaction goal for state `0x40000`). Confirmed (code) at the
addresses cited:

1. `Human_KnockOut(h, 0)` (`0x00230328`; from `Human_OnHealthOut`, `HuSetConscious`, `Human_Destroy`) sets state
   `0x40000` and, unless the human is already dead or wounded (`0x100050000`), first calls `Brain_OnKnockedOut(brain,
   0)` (`0x0028c3b0`). `Human_ReleaseInstance` passes 1 and skips the brain part.
2. `Brain_OnKnockedOut` ends the reaction goal, **pops every goal** (`Brain_ClearGoals`: each goal's End runs), clears
   the actions, **drops the target** (`Human_DropTarget` → `Brain_SetTarget(brain, 0, 0)`, which releases his
   active-attacker place (`Brain_ReleaseActiveAttacker`, target `+0x1f0`) and attack slot (`Brain_ReleaseAttackSlot`,
   `+0x1a4`) on the old target, and restores that target's spacing when no slot holder is left), and, being knocked
   out, empties his enemy list and **disables the brain** (`Brain_ClearEnemies`: brain `+0x08` = 0). It then sends his
   human event `0x12` with his foe.
3. `Brains_Update` (`0x00293b28`) runs a brain only while `+0x08` is set, so a knocked-out human neither thinks nor
   updates until `Human_WakeUp(h, 0)` clears the state and calls `Brain_EnableAndResume`. Its per-human gate
   `Human_ShouldThinkThisUpdate` (`0x0023d790`) guards both the think and `Brain_Update`; it is a cull, not a health
   test: it is false only for a human with no held flag, not paired or blocking, not in a scene state, at gait 0,
   whose brain `+0x2d7` is clear and who is busy with a scene or whose gang has a flag in `+0xd8`'s high half, when
   the human was handled within 500 ms (`+0x2a0`) and is off screen (model drawn more than 1 s ago, or no model
   and `+0x334` ≥ 60).
4. The claims do not test the holder's health: `Brain_ClaimActiveAttacker` (`0x00291008`) and
   `Brain_ClaimAttackSlot` only prune dead handles (`HandleArray16_PruneDead`). A downed holder is released by his own
   knock-out (step 2), not evicted by the claimants.

A wounded human (`0x10000`, `Human_StartWounded`) is not knocked out: it takes the Wounded reaction goal and keeps
its brain.

#### The fight reaction goals {#fight-reactions}

Each runs as [the reaction goal](#reaction-goals) while its state lasts, and ends (2, actions cleared) when it
clears. They pick with `Human_CanStartAttack` and queue with the kind's chain delay (`AttackKind_ChainDelay`) unless
said. Confirmed (code) at each address.

- **Grabbing** (`0x12`, the grabber).
    - **Start** (`0x002b5ad0`) rolls a **hand-over flag** `+0x14`: `Random_Int(100)` < the gang's `CfgGang` value 5
      (`+0x6f`) × 10. A FollowAndDefend (`0x35`) or HoldFlag (`0x7d`) goal on the stack clears it.
    - **Process** (`0x002b60c0`):
        1. Not grabbing: done.
        2. Near a train (`Brain_IsNearTrain`): write command 5 every update. In `Player_UpdateGrabbing` command 5
           (L2 held, `PlayerCmd_IsLetGo` `0x0027bf08`) **lets go**: grabber 95, victim 94
           ([Combat](../combat-grabs.md#grabbing)). Confirmed (code).
        3. The target is the held man (`+0xc4`). While actions are queued, wait.
        4. **Presenting him.** This happens in a rear grab when the nearest player is friendly to A, is not busy,
           his actions are not blocked and he is within A's near range (brain `+0x13c`, 3 m), and player 1 (game
           state `+0x228`) exists and his crew is not under the **Defend** command: his current Warrior command,
           game state `+0x41a` + his player index (human `+0x1b0`), is not 2 ([Warrior commands](../ai-gangs.md#warrior-commands);
           written by `GameState_StoreWarriorCommand` `0x0041c4c8`). The timer `+0x10` (now + 3000 ms, set on the
           first such update) must be unset or not yet run out. Confirmed (code); the command byte is read at `0x002b622c`.
        5. **A front grab with the flag**, when the victim has two or more slot holders: queue a set-command
           action (`SetCommandAction_Init(action, B, 0x19, 0x21)`: command `0x19` after a 33 ms start delay, the
           base field `+0x04`) and wait. Command `0x19` is the grab **spin** (`PlayerCmd_IsGrabSpin` `0x0027bd98`,
           as R1 pressed): front to rear, 78 / 79 (`0x0026d570`), into the rear hold 84 / 85
           ([Combat](../combat-grabs.md#grabbing)). So he turns the victim round, and step 6 then holds him up facing the
           others. Confirmed (code) at `0x002b60c0` and `0x0027f3b0`.
        6. **A rear grab with the flag**, when the victim has a number of slot holders other than 1, or while
           presenting. Find the nearest player among the victim's slot holders, else the nearest of his active
           attackers, and turn to face him (`TurnToPointAction`). Wait. (Holding him up for a friend to hit.)
        7. Otherwise pick a kind and act on it:
            - 45: wait;
            - 24 (a strike in the grab): 40 % of the time two kind-24 attacks, the second after the chain delay;
            - 25 or 29 (the throws): the stick angle from `Grabbing_PickMove`;
            - 26-28 (power strikes): no angle.
    - **Grabbing_PickMove** (`0x002b5b98`) picks a direction *d* (0 **right**, 1 ahead, 2 **left**, 3 behind the
      grabber). The angle returned is atan2(forward.y, forward.x) + *d* × 90° − 90°, wrapped to [0, 2π): the
      mathematical angle from `+x`, anticlockwise, so *d* = 0 is a quarter turn clockwise from his forward, his right
      (in Coney's headings, θ anticlockwise from `+y`: θ + *d* × 90°). The sector mappings agree: a flag on A's right
      (sector 2) sends him to 2, away from it. Confirmed (code) at `0x002b5fe8`-`0x002b6040`. The first rule that
      gives one wins:
        1. With a HoldFlag goal (`0x7d`), away from its flag: flag in A's sector 0, 1 or 7 → 3; 2 → 2; 3-5 → 1;
           6 → 0.
        2. When the **grabber's power class** has byte `+0x39` = 1, a random choice among the walls next to them.
           The power class is the `CfgPowerClass` record at `0x006619a0` + class × 0x44, read through
           `Human_GetClassRecord` (`0x00222b78`); `+0x39` is its argument 22, clamped to 0-1. The walls: the victim's
           sector 4 → 1, the victim's 6 → 0, the victim's 2 → 2, A's sector 4 → 3. Each test is `Sectors_IsWall`
           (`0x0029ea10`). That is the sector **wall bit** (`0x4`) from the lazy wall probe
           ([Neighbour sectors](../ai-moving.md#neighbour-sectors)): a straight walk 1.5 m out that `Human_CanWalkStraightTo`
           refuses on the navigation mesh, not a collision ray. Both sector sets are refreshed when older than 1 s
           (`Brain_GetSectors(…, 1000)`). Confirmed (code) at `0x002b5cf0`-`0x002b5ef0`. Failing that (no wall), a
           random choice among the same four, each taken when the sector's flag 1 is set (someone within 1.5 m), its
           nearest human resolves and `Brain_IsFriendly(A, him)` is 0: the victim's sector 4 → 1, the victim's 6 → 0,
           the victim's 2 → 2, and A's sector 4 → 3. The last tests **A's** sector 4 flag but the friendliness of the
           **victim's** sector 4 human (it reloads the victim's handle, `0x002b5ea8`, `a0` = victim record + `0x20`),
           a slip a faithful port keeps. Confirmed (code) at `0x002b5db4`-`0x002b5ee0`. (Into a wall or into his
           mates.) This fallback, like the walls, runs only when the power class's `+0x39` is 1.
        3. With the gang's tactic of type 2 (Defend), away from the defended human, as in 1.
        4. Otherwise `Random_Int(3)`: right, ahead or left (0-2), never behind.
- **Mounting** (`0x13`, the tackler on top, `0x002b6638`).
    1. Not tackling: done.
    2. Near a train: command 5, which in the mount (`Player_UpdateMounting` `0x0027ec20`) **gets off**: 244 / 245,
       the victim rises with 199 ([Combat](../combat-grabs.md#mount)).
    3. The target is the held man. Pick a kind; 45 and 36 do nothing.
    4. Kind 35 (the ground punches): 40 % of the time two of them, chained.
- **Grabbed** (`0x14`, `0x002b6848`).
    1. Not grabbed and not being mugged: done.
    2. A grabber who is a friend: nothing.
    3. A civilian (type 4) not in a standing reaction calls for help every 30 updates (`Gang_BroadcastHelpCall`,
       10 m).
    4. With threat response 0, other than a dealer or a class-221 human, he only waits.
    5. Otherwise the grabber becomes the target.
    6. Once he has been mugged, the latch `+0x10` stops all struggling.
    7. Otherwise pick a kind:
        - **31** (the struggle strike) is queued with delay = chain delay × (1 − max(0, (health % / 100 − `h`) /
          (1 − `h`))), `h` being the class's hurt fraction (`+0x04`); a class-221 human uses 0. So a healthy man
          struggles at once and a hurt one waits the full chain delay.
        - Any other kind gets a stick angle of his heading or heading + π (50/50): push forward or back.
- **Mounted** (`0x15`, tackled, `0x002b6bb8`): as Grabbed (help call, threat-response rule), without the mugging
  latch. Every kind uses the health-scaled delay and no angle.
- **Grounded** (`0x17`, `0x002b4aa8`; Start `0x002b4a38` notes the time).
    1. While knocked down and someone holds an attack slot on him, queue kind 42 **on himself**. The delay is
       max(0, 1900 − min(time down, 2000)) ms.
    2. A target hidden in shadow is dropped.

    So about 1.9 s after going down, a man still under attack plays kind 42 (anim 250), which `0x002240e8` allows
    only from this goal. Kind 42 is the command `0xf`
    (`AttackKind_ToCommand`, `0x00231090`); a downed human never reaches the dispatcher, so no attack plays:
    the one command cuts his ground time as a press does
    ([Combat](../combat-reactions.md#ground-escape); inferred from the dispatcher not running for a downed human). It is
    not aimed "against the target" ([AI goals](../ai-goals.md#goal-reactions)).

#### Who backs off (FightBackOff) {#fight-back-off-pusher}

Confirmed (code) at the cited addresses unless a line says otherwise.

The only caller of `FightBackOffGoal_Init` (`0x002b7140`) is `GuardSubTactic_Order` (`0x003221e8`), one of the Attack
tactic's coordinated [sub-tactics](../ai-code.md#t2-sub-tactics). Confirmed (code). With its back-off option (`+0x17`)
the order does these things:

- The **gang leader** gets FightBackOff with no time limit, after `Brain_PopToGoalBase` / `Brain_MarkGoalBase`. He
  keeps 1.75 × far plus 1-2 m from his target, faces him and taunts ([FightBackOff](../ai-goals.md#goal-fight-back-off)).
- Every other non-player member joins the leader's formation (set 3, min(2 × others, 9) slots, shape 3.0) and gets
  FollowAndDefend (3.0) on him. Without the option the members also get the attack table `0x00511438` as their
  override.

So FightBackOff is the leader hanging back while his men guard him. No script pushes it.

### Events {#events}

Confirmed (code) at the cited addresses unless a line says otherwise.

`Brain_OnEvent` (`0x0028f928`) runs the perception's handler (`0x00299100` on `+0xf8`), then the reaction goal's
event slot if one is active (`0x0028f988`), then handler C. Event `0x10` is the **attack warning**
([below](../ai-fights.md#block)). Handler C is where a brain answers the world: the civilian's (`0x002ffb30`) takes
event `0x14`
(violence nearby) and may start a fight through `Brain_Fight`; the type event handlers are among `Brain_Fight`'s
callers.

#### Fetching an object: GetItem {#get-item}

Confirmed (code) at the cited addresses unless a line says otherwise.

Every AI pick-up and smash starts from the object search `Ai_FindObject` (`0x0029d5f0`, 21 callers): its steps, the
clear ray that stops at an intact pane, the reach test and the 5 s claim are on [The crew's wreck: the object
search](../crew-wreck.md#object-search).

**GetItem** (type `0x2c`; Init `0x002dc7a0`).

- **Fields:**
    - `+0x20` the object's handle;
    - `+0x28` the gait;
    - `+0x10` a standing point beside the object;
    - `+0x24` its type record;
    - `+0x2c` tries;
    - `+0x30` the saved turn boost;
    - `+0x31` picked;
    - `+0x32` the point is valid.
- **Start** (`0x002dc7f8`) drops an object of kind 4 or 6 already in hand, raises the turn boost by 1, drops the
  target and runs Resume.
- **Resume** (`0x002dc8b0`) finds the standing point: `Nav_FindPolygonUnder` of the object's position, which, as
  in the object search, ignores the size passed, so the point is the object's own spot on the navigation ground. With
  none, the goal will end.

**Process** (`0x002dc9f8`), each update. Confirmed (code).

1. The object is gone: done. Otherwise look at it (500 ms).
2. The object's flag `0x4000000` (bit 26, the object airborne: [World objects](../objects.md)) is set: wait. A
   pick-up is in progress (held flag `0x4000`): note it (`+0x31`) and wait.
3. **Someone holds the object.**
    - A himself: done.
    - A friend: done.
    - An enemy: he becomes the target.
        - Within 1.1 × far, or A armed, with a line of sight: push a fight goal (1000 ms).
        - Else, when `Brain_MayEngage` allows: push EngageEnemy.
        - Else: done.
4. **Nobody holds the object.**
    1. When it was picked (`+0x31`): done.
    2. While actions are queued, wait.
    3. Without the point, after a failed move (brain `+0x284`, then cleared), or after 11 tries: done.
    4. **Clear the way.** Take the nearest non-friend among A's slot holders who is nearer the object than A is. When
       he is within 1 m of A, not class 13, and kind 17 can start, A targets him and queues **kind 17** (the
       circle + cross special).
    5. **Within 1.5 m of the standing point** (`+0x10`, the 3D distance from him):
        - Wait while A has any held flag but `0x40000000`.
        - With a clear ray to the object (`Human_HasClearRayTo`, mask 0):
            - When the ray result is not 2: queue a **pick-up action** (`PickUpItemAction_Init`, `0x21`) and count
              a try.
            - When the ray result is 2: step to the point (when beyond 0.4 m; gait 3, radius 0.36 m), turn to the
              object, and queue attack kind 0 (`X1`) on himself (`Brain_QueueAttack(−1, brain, self, 0, 0x21)`),
              **counting no try**. Result 2 means the ray from his feet + 1.7 m to the object + 0.1 m crossed an
              intact pane: `Ray_IsClear`'s default list (30, 2, 187, 122, 107) lets those materials through, but
              `CollisionMesh_RayTestCell` (`0x00350778`) still writes an excluded triangle's material into the result
              while nothing solid has been hit, so the ray is clear and its result is 2 (`GLASS`). `STOREDOOR_GLASS`
              (187) gives 187, so only material 2 makes him swing. Confirmed (code) at `0x002dce84`-`0x002dcf44`.
              **So an AI never takes an object through an intact pane**: he swings at it instead and keeps
              swinging until the pane breaks (its triangles are disabled, [Objects](../objects.md#pane)) or the goal
              ends. A pane has the body a strike breaks only within 50 m of a camera
              ([Drawing a pane](../objects.md#pane-draw)); a swing at a pane farther from every camera breaks nothing.
              A run where none broke: [How this was checked](#runtime-steal-store).
        - Without a clear ray: turn to it when more than 15° off, else count a try.
    6. **Walk** to the point out of the fight stance: gait 3 within 3 m of it (3D), else the goal's gait (+0x28; 4, a
       run, for
       WarriorVandalSteal's fetches, `0x002bee78`), radius 0.36 m. Confirmed (code) at `0x002dd018`-`0x002dd080`.

The AI's pick-up press itself: [AI and the world: Picking up](../ai-world.md#pick-up).

#### Objects in an AI's hands {#ai-objects}

Who fetches an object, and when an AI lets go. Confirmed (code) at the addresses cited.

**Fetching** (each pushes GetItem, [above](#get-item), on what `Ai_FindObject` returns):

| Who | When | Search |
| --- | --- | --- |
| Melee (`Melee_TryPickUpWeapon` `0x002ade90`) | brain `+0x265` set, actions free, empty-handed; every 20 updates, or when asked | weapons (filter `0x0053f2d0`, mask `0x20000`; also throwables `0x30000` when the target is within 15 m and asked), 20 m, or 10 m when asked; gait 4 |
| Spectate | joined, not a cop or civilian, 25 × gang factor % rolled at Start; every 30 updates | pickable within 15 m ([Spectate](../ai-fights.md#spectate)) |
| The Warriors' think | [The Warriors' pick-ups](../ai-gangs.md#warrior-pickups) | 1.5 m |
| FollowAndAttack / FollowAndDefend / FollowPlayer | `+0x265`, not blocked; every 20 updates or within 15 m of the enemy | smash or throw objects, 20 m |
| AvoidEnemies | empty-handed | throwables |
| Riot, DestroyCar (`DestroyCar_GetWeapon` `0x002ddd48`, 10 m), Destroy, WarriorVandalSteal, the Wander tactic | to smash something | breakables / weapons |
| ObjectThrower, ManWeaponPile, ObjectPile, Steal | their own objects | below |

**Using.** A held weapon changes the [attack pick](../ai-fights.md#pick-attack) (the armed term) and the fight goal's
range test
(armed fighters fight at any range within 1.1 × far). A held throwable is thrown by pressing cross aimed at a
point (human `+0x128` holds the aim's handle): AvoidEnemies throws at an enemy close enough (attack kind 23, unless
the object is of type class 6 with `+0x5a` 0, or class 1 within 2 m); VandalizeItem throws at the object.

**ObjectThrower** (type `0x4e`, `ObjectThrowerGoal_Init` `0x002a3f50(goal, brain, maxThrows, search radius, pace s,
stop radius, flag1, flag2, flag3, callback)`; Process `0x002a4230`):

1. Fight stance on; wait while actions run or a flag (other than `0x40000000`) is held.
2. Every 15 updates: done (2) when a hostile is visible within the stop radius (`HandleList_FillVisible`); done
   after `maxThrows` throws (0 = no limit). Every 45 updates he glances at his nearest enemy (2 s).
3. Holding a throwable (or kind 4 / 6): turn to one of the up-to-three target flags at random, aim there
   (`+0x128`), queue cross (kind 0) after a random pace/2 to pace seconds; count the throw.
4. Empty-handed: after every third throw (from the third on), 50 %: turn to the player's camera and taunt (kind 3,
   line `0x10`) once. Else every 5 updates the nearest weapon within the search radius: drop what he holds, push
   GetItem (gait 2).

**Letting go.**

- Speed: running faster than the run speed drops a kind-4/6 object (step 2 of Speed control, [above](../ai-moving.md#ai-gaits)).
- A Warrior with no enemies drops a kind-4/6 weapon unless his gang's tactic is steal (`0x26`)
  ([pick-ups](../ai-gangs.md#warrior-pickups)).
- GetItem's Start, DestroyItem's and DestroyCar's Start, Cower's Start, CallGang's init and RunFromTrain
  (`RunFromTrainGoal_DropEverything` `0x002f9a80`) drop what is held.

#### World flags {#ai-flags}

Confirmed (code) at the cited addresses unless a line says otherwise.

An AI uses a flag ([World flags](../flags.md#activities)) only when its brain's `+0x2d1` is set (1 when made;
`Brain_SetWorldFlagUse`, `Gang_SetCanUseWorldFlagsById`): `Human_NearestUsableFlag` (`0x00417670`) returns 0
otherwise. The chance byte `+0x2d2` (10 when made) is written by the same setters but no reader was found. Confirmed
(code).

- **Pedestrians** (`0x002ab4b8`, the pedestrian goal's move step): every 20 updates, when he may chat and has
  walked far enough from his last stop, the nearest usable flag within 10 m that he can walk straight to in under
  15 m gets a **MoveToUseFlag** goal (radius 0.3 m, his gait); a re-use timer at goal `+0x6c` (65 s, inferred from
  the value)
  keeps him from going back at once.
- **HangOut**, **Patrol**, the **TravelPath** and **Wander** tactics look up a usable flag the same way.
- **MoveToUseFlag** (`0x002dbc10`): done when his actions are blocked or the flag is disabled. Walk to it (up to 3
  tries); while walking, within 5 m, every 15 updates: a human within 0.75 m of the flag ends the goal. At the
  radius: turn to the flag's heading (beyond 15°, turn rate 0.2), then he is placed on the flag's point and heading
  and plays the activity's clips (enter, idle, exit, [Activities](../flags.md#activities); the clip states after this
  are not traced).

#### Cover, cars and trains {#ai-hazards}

Confirmed (code) at the cited addresses unless a line says otherwise.

- **Hiding** is only the hold command's hide tactic and Scatter ([Warrior commands](../ai-gangs.md#warrior-commands)): Hide
  (`0x58`) walks to a wall point near a hiding flag (gait 3 near the leader, else 5), out of the shadow, then turns
  to face out. Shadow itself is the human's hidden state (`Human_IsHiddenInShadow`, state `0x200000`) that
  [the enemy scan](../ai-fights.md#enemy-scan) reads; an AI never seeks shadow on its own (no other goal sets it).
  Inferred from
  the setters' callers.
- **Cars.** No goal in the AI range drives or enters a car: cars run on their own paths ([Cars](../cars.md)). AIs meet
  cars three ways: DestroyCar (`0x2e`) smashes one from a spot round it; the Pursue tactic, on a crime event (23) at
  a car, has members within 30 m drop an Investigate goal and a chasing member push Investigate at the crime point
  (`Chase_InvestigateCar` `0x002b0c88`, 5 m, line 13), unless the offender is friendly; the Wander tactic's
  `Tactic_VandalizeCars` (`0x00316590`). Confirmed (code).
- **Trains.** The train (`TrainRecord_Update` `0x00413d90`) pushes **RunFromTrain** (type 7) on a human near its
  track: he drops everything, turn boost 3, and sprints (gait 5) to a safe point beside the track, or along it when
  no side is clear ([its rows](../ai-goals.md#goal-run-from-train)). While a train is near, the enemy score rules the
  human out (`Brain_IsNearTrain`, [the score](../ai-fights.md#enemy-score)).
- **Doors and breakables on a route.** Doors are route links ([Following a route](../ai-moving.md#route-follow)); a
  follower stuck on the way to his formation slot smashes a breakable within 1.5 m (VandalizeItem, [FollowFormation](../ai-gangs.md#warrior-follow)).

Which world uses an AI reaches (objects, cars, doors, climbs) through the player's own functions, and which it never
reaches, is on [AI and the world](../ai-world.md).

### Moving {#moving}

Confirmed (code) at the cited addresses unless a line says otherwise.

How an AI moves: gaits and speeds, turning, the move action, the corner speed, the move-to-human action
(MoveMelee), path planning, following a route and its jump legs, neighbour sectors, steering round humans, giving way
and waypoint queues. The section is on [AI moving](../ai-moving.md#moving).

### Civilians {#civilians}

Confirmed (code) at the cited addresses unless a line says otherwise.

The civilian think and events are under [Civilians (type 4)](../ai-brain-types.md#think-civilian): toward a nearby
player a civilian of
ped type 3 pushes Hostile (`0x72`, 10 m), others a PedReaction (`0x6b`) with line `0xaf`.

### How the scripts drive the AI {#script-control}

Confirmed (code) at the cited addresses unless a line says otherwise.

How the level scripts drive the AI: the six levers and their order, the brain and gang bindings in effect, the
strategy library and the usage counts by level. The section is on
[AI scripting](../ai-scripting.md#script-control).

### Scripted goals and actions {#scripted}

Confirmed (code) at the cited addresses unless a line says otherwise.

What the first mission's scripts give a brain directly: GoalMoveToFlag, ActLookAt, GoalAddressPerson,
GoalPlayDynAnimation, GoalBumLogic, GoalDealer, GoalRiot and GoalDevilRun. The section is on
[AI scripting](../ai-scripting.md#scripted).

### Gangs, tactics and formations {#gangs}

Confirmed (code) at the cited addresses unless a line says otherwise.

The gang record, its events, update and deletion, the tactics (the scripted tactic kinds), attacking on sight, the boss
fights, spawners, crimes and the police, the Warriors' commands, follow, automatic commands and formations, and the
gang functions. The section is on [AI gangs, tactics and formations](../ai-gangs.md#gangs).

### Gang coordination in a fight {#gang-coordination}

How the members of a gang share out a big fight. There is no fight coordinator: nothing owns a brawl or
hands out targets, and each brain meets five mechanisms on its own. Confirmed (code) on
[Gang fights](../gang-fights.md), and checked at runtime on Rumble's biggest battle
([Gang fights: runtime checks](../gang-fights.md#runtime)):

1. **The target's own limits**: at most 4 attack slots on a man, of which only as many as the attackers' gang allows
   (`CfgGang` values 2 and 3) may swing at once, with a shared gap between swings
   ([How many hit one man at once](../gang-fights.md#limits), [Attack slots, active places and spacing](../ai-fights.md#attack-places)).
2. **A shared score** for choosing among enemies: −999 for a man whose slots are full spreads a gang, +20 for the
   player and +400 for the gang's shared target focus it ([Who goes for whom](../gang-fights.md#targets),
   [Choosing among enemies](../ai-fights.md#enemy-score)).
3. **Waiting**: a slot holder without a place repositions round the man and taunts
   ([What the waiting ones do](../gang-fights.md#waiting), [The reposition](../ai-fights.md#fight-reposition)); every
   fight goal ends
   within 4 s unless its holder still swings, so members re-choose often
   ([Target switches](../gang-fights.md#switches)).
4. **Help calls** (event 20): a member hit or warned calls within 20 m, and only the idle answer
   ([Helping a gang-mate](../gang-fights.md#help), [Help calls](../ai-brain-types.md#help-calls)).
5. **The gang's tactic and team-ups**: TacticAttack's 3 s idle check and its seven leader orders, drawn by the gang
   type's strategy weights when `GangEnableAttackStrategies` is set ([The gang as a whole](../gang-fights.md#gang)).

Rumble's two CPU sides run different tactics (side 1 the Warrior attack command's, side 2 TacticConfront then
TacticAttack; [Rumble's CPU-controlled sides](../gang-fights.md#rumble)). The step-by-step units an implementer builds
from are [AI reimplementation, units 1-7](../ai-reimpl.md#unit-1); the recordings, their settings and the per-human trace
are [Rumble: gang battle trace](../rumble.md#gang-battle-trace).

### The first mission's cast {#level99}

Confirmed (code) at the cited addresses unless a line says otherwise.

From `level99`'s scripts and the `CfgChar` calls (ids and values only). The brain type is the class's `behaviour`
byte; a scripted gang overrides with `GangBrDead`, `GangSetThreatResponse` and the goals it pushes.

| Script group | Types | Brain | Health | Damage / attack tables | Gang |
| --- | --- | --- | --- | --- | --- |
| the player (Rembrandt) | 30, or 32 at checkpoint 1 | 0 as the player (3 as a class) | 1800 | `DamageFox` / `Att_Warrior` | 0 (`Warriors`) |
| Ash | 38, or 40 at checkpoint 1 | 3 | 1800 | `DamageWarriors` / `Att_Warrior` | 0 |
| the teachers (Vermin, Cleon) | 27, 2 (Vermin is 26 in the opening) | 3 | 1800 | `DamageWarriors` / `Att_Warrior` | 19 (`CombatTeacher`); Cleon also 0 (`SCleon`) |
| `GenWarrior`, then the second wave | 275; 272-274, 276 (bum models) | 4 | 300 | `DamageWeak` / `Att_Bum` | 19 (`CombatEnemy`, `CombatEnemy2`) |
| sparring Warriors (`CombatWarriors`) | 58, 59, 60 | **2** | 1400 | `DamageNormal` / `Att_Normal` | 19 |
| the fence Warriors (`FenceWarriors1`, `2`) | 58, 59, 60 | **2** | 1400 | `DamageNormal` / `Att_Normal` | 0 |
| the street civilian (`PoizoCiv`) | 417 | 4 | 600 | `DamageTough` / `Att_Normal` | 23 |
| bums | 271, 272 | 4 | 300 | `DamageWeak` / `Att_Bum` | 23 |
| the dealer (`FlashDealer`) | 430 | 5 | 800 | `DamageBoss` / `Att_Dealer` | 24 |

The sparring Warriors' power class and brain values, as a run read them: [How this was
checked](#runtime-sparring-numbers).

How `level99_combat.lua` runs a fight (call names and arguments only): the enemy gangs start dead to the AI
(`GangBrDead(true)`) with threat response 0 and are walked into place with `GoalMoveToFlag`, `ActLookAt`,
`GoalAddressPerson`, `GoalTrackHuman` and `GoalPlayDynAnimation`; a fight starts with `GangMakeEnemies`,
`GangSetThreatResponse(2)`, `BrFlush` and `GoalFight(enemy, player, 0)`. Allies follow the player through follow
slots (`BrSetNumFollowSlots(player, 4)`, offsets (0, 1), (1, 0), (0, -1), (-1, 0), later (0, 2)…), and a crowd
tactic (`TacticCrowd`) and a gang message handler (`GangSetMsgHandler(…, 18, …)`) frame the fence fight.
`level99_lesson2` uses `GoalDealer` and a run of `GoalMoveToFlag` for Vermin.

**The attack tables** of that cast, by attack kind (the table's Lua index − 1; weights are relative):

- `Att_Bum`: kind 1 (`S1`) 10, kind 5 (`SS2`) 10, kind 7 (`SSS3`) 20; nothing else. Bums only throw square chains.
- `Att_Normal`: kinds 0, 1: 10; 2-5: 30; 6, 7: 60; 10: 40; 12-14: 30; 19, 20: 30; 21, 22: 55; 24: 50; 25: 75; 28:
  40; 29, 30: 60; 31: 100; 33-35: 50; 36: 10; 39: 30; 40: 65; 42: 200; 43, 44: 100.

## In Coney

Steps 3-8 of [What an implementer needs](#implementer) are in `repo:src/ai/` (the path data
in `repo:src/world/path_map.h`), each original function tagged with `@orig` in the code; tests in `repo:tests/ai/` and
`repo:tests/world/path_map_test.cpp`.

- **Brains** (`Brains`, `Brain`): a type from the class's behaviour byte, a think one step in five staggered by slot,
  an update every step at the brains' place in the characters' step (`Humans::setBrains`), so before every
  dispatcher. The player has a type-0 brain that only keeps his enemies, which feed `Player::setNearestEnemy`.
- **Goals and actions**: a stack of 10 (Start, Process, End, Resume; Stop, Again, Done) and a circular queue of 8
  (a delay, Start, Update, an Abort that can refuse); goals start or resume only on an empty queue, the generic
  time limit ends a goal in `Brain::runGoal`, a pop starts or resumes the new top at once and sends event `0xd` when
  the stack is empty, only a started goal is ended and only a started action is aborted, and the actions chain in one
  update. A goal leaves the stack before its End runs: an End can reach Lua (a flag's message), whose handler may push
  the next goal (level99's `P1.ReachCenter` pushes `GoalAddressPerson`), which must stay on the stack. **Stand-ins**:
  Coney's goal loop is bounded at 20 passes (issue #635); the character
  instance gate (human `+0xd8`) always passes, as every human with a brain has its body; the Melee goal still keeps the
  time limit itself (its own Process does not read `+0x08`) until its port moves it to the base. The reaction goal is
  made, held off and handed back as the original does, with its exemptions; the arrested, on-fire and wounded goals,
  the Grabber and BigBull goals' tests and the `+0x2d7` test are not built (the function database lists each with its
  issue).
- **Hiding** (`repo:src/ai/hiding.h`, `repo:src/human/human_hiding.cpp`, [Stealth](../stealth.md#shadow-ground)):
  the ground snap keeps its triangle's flags (`Human::onShadowGround`, bit `0x10`); after the level's step
  `ai::updateHiding` runs the ground rule for player 1: on shadow ground with nobody hunting him (no other fightable
  brain with him as its target or enemy, **Coney's reading** of the hostile count) he enters the hidden state, off it
  or hunted he leaves it (kept 4 s when walking with a target), and stepping on shakes off hunters beyond twice their
  far melee range with no line of sight. Hidden, his target does not lock, a sprint ends it, his idle and walk take
  the stealth style (634 in, 630, 633 at its own speed, 631 starts, 394 out), the radar turns blue
  ([HUD](../hud.md#coneys-implementation)), and `Human_CanSeeHuman` sees him only within 2 m from shadow ground. Not
  yet: the other players' Warriors and their hide order, the molotov test, the camera's raised look-at and the half
  volume footsteps.
- **Reactions**: grabbing, tackling, grabbed, knocked down and stunned, one update after the state.
- **Play**: `fighter` lines in sandbox layouts ([Sandbox](../../guides/sandbox.md#ai-fighters)), the debug menu's *AI
  fighters* page, and a level's scripted cast. Gameplay (`repo:src/gamemodes/gameplay_mode.h`) makes the level's
  `Brains` and `ScriptedBrains` before its script runs and holds every AI call on a human (`ScriptedBrains::hold`)
  until the play mode has loaded the level and made the humans; `release` then makes each `HuCreate`d human (player 1
  bound to the player's brain, the others AI humans of the class `aiConfigFrom` gives their type, drawn with their own
  model, `repo:src/platform/play_level_cast.cpp`) and replays the held calls in order. A later `HuCreate` of player 1
  is an AI human until `HuChangePlayerGang` hands player 1 over ([Characters](../characters.md#coneys-implementation)),
  and `GangDelete` takes every member but player 1 out of the world at once ([Deleting a gang](../ai-gangs.md#gang-delete)).
  A teleport moves an AI human;
  `GetPosition` and the look-ats read a human's live position.
- **Scripted goals**: `MoveToFlagGoal` (offset target, radius, the face-the-flag turn, a new move each time one ends
  short, message 8 and the gang's notice through `FlagServices`), `TurnAction` (look-at, to a point, to a heading;
  15°, 3 s), `PlayDynAnimationGoal` with `PlayAnimAction` (slot 668), `AddressPersonGoal` with `PlayAnimationGoal`
  (its scene), `TrackHumanGoal` and `DealerGoal`. A goal's callback is scheduled 33 ms after its End with (handle,
  completed) (`scheduleGoalCallback`). `Brain::flush` ends the goals (their callbacks fire) and clears the actions;
  `Brain::setDead` clears the actions only, stops the think and the attack warnings, and hands a player's pad over
  (`Brain::setPadControl`, `human::Player::setPadControlled`), after which the player's brain runs goals like an AI's.
- **Bindings**: the AI bindings (`repo:src/scripting/ai_bindings.h`) and the gang bindings
  (`repo:src/scripting/gang_bindings.h`, `GangCreate` in `repo:src/scripting/script_bindings.cpp`) hand their calls to
  the binding context's AI host. `ScriptedBrains` is that host and the goals' `ScriptServices`: it names brains by
  handle (`bind`, with `HuCreate`'s gang), finds flags and look-at targets, and runs the callbacks, message handlers
  and tactic callbacks in a `ScriptSystem`. An event goes first to the human's own handlers (`SetMsgHandler`,
  `Brain::services`), and a flag arrival sends the flag message 8 with (flag, human)
  ([Scripts: message handlers](../scripting.md#message-handlers)).
- **Gangs** (`Gangs`, `Gang`, in `Brains`): 32 records with kind, name, enemy and friend masks, members (10, 16 for
  the police kinds), suspension, message handlers and a tactic; `Gangs::friends` and `enemies`; the whole-gang
  switches (`GangBrDead`, `GangBrFlush`, `GangSetThreatResponse` on the current members, `GangSuspend`). An event goes
  to the human's gang before its brain (`deliverEvent`): the handler for 18, 2 and `0x11` with (member, other, value),
  any other id used when the call returns true, then the tactic. `Brains::update` sends event 18 once when a member's
  health runs out, so `level99`'s `P1.BumDied` gets the count still standing (`Gang::standing`).
- **Formations** (`Formations`, `Formation`, stepped before the gangs): sets of 9 slots in 1/16 m turned by the
  leader's heading, the plan when he stops, every 1 or 2 s or 1 m from the plan point, the nearest follower per
  usable slot, the rest queued nearest-first behind, and the crossing-paths swap.
- **Tactics** (`Tactic`, stepped by the gangs): started on the first update with each AI member's goal base marked
  over the goals he has (its goals go on top, and its end pops back down to them), 2 past the time limit, the
  callback with (gang id, code); a gang with a tactic gets no fight goal from `GoalFight`.
  `TacticCrowd` seats its members (idle and fightless when cheering, spectating 4-6 s when watching), gestures,
  cheers in turn and reacts (a clip, then the cheer) on its tick, on `TacticTrigger` and on violence nearby.
- **The knock-out** (`Brain::knockOut`, [Reaction goals](#reaction-goals)): when a human's health runs out his brain ends
  its reaction goal, pops every goal, clears the actions, drops the target (releasing the attack slot and active place
  on it) and the enemies, and stops until he is up again; then event `0x12` reaches his human with his foe.
- **The Diego and Vargas fight** (`BossDiegoVargasTactic`, `repo:src/ai/tactic_boss.h`, `repo:src/ai/boss_goals.h`,
  [the fight](../ai-gangs.md#boss-diego-vargas)): `TacticBossScenarioA` gives class 120 a `BigBrawlerGoal`, class 119 a
  `BigThrowerGoal` in stage 2 and a `BigBrawlerGoal` in stage 3, the others `StationaryThrowerGoal`; holds the
  boss at 67 % and 34 % until he is tired, then starts the break (clips 671, 672, 673) and answers 18 while its
  clip plays and 1 once it is done. Six hits in the fight push `TiredGoal`, which stuns him for the stage's fatigue
  or damage limit. An attack warning names the attacker as the peer standing where it was announced (**Coney
  reading**). **Coney stand-ins**: no object appears in a boss's hand (the throw clip plays alone), the enemy scan is
  his gang's enemies within sight, the camera turn faces player 1, no event 24 or help call, and each tactic code
  is answered once.

**Coney choices for the scripted goals, gangs and tactics.** With no scene system a scene ends at once and its callback
is scheduled with (handle, 1) after 33 ms. A play-anim action plays its clip (668 the level's dynamic clip by name, any
other id the anim set's) faded in over 0.2 s, at rate 1 for a dynamic clip, then the idle, holding `0x80000` (busy and
gated; which bits the original's clip holds is not traced); with no such clip it ends at once, so
`GoalPlayDynAnimation` still completes. "Not on its feet" stands in for the state
words (PlayDyn's `0x7bf9e9f7ff0`, the crowd's free test, the standing count's three tests); the headcount's "living" is
health left. AddressPerson's turn leads the target by one second of its velocity. `BrSuspend` clears the actions,
then suspends. A full gang drops its first member; a human with no gang is no one's friend. A handler's call counts as
returning true when it runs (Coney's script system does not hand back the result). Event 18 is sent at the brains'
next step after the health runs out, with no attacker. **Stand-in:** event 1 (damage taken, whose sender is not
traced) is sent at the brains' step after a human's health falls, with the nearest other human that can fight as the
attacker and the damage as its value. The fall is measured before the gangs' tactics run, since a tactic can give the
health back at once (Coney's choice, inferred from the fight: the original's sender is untraced): `TacticBossScenarioA`
holds a boss above his cap, and every hit he takes there must still reach his BigBrawler, or he never tires and the
stage never ends ([the boss fight](../ai-gangs.md#boss-diego-vargas)). A
formation slot is usable when the leader's planner finds the line to it walkable (always without a planner) and keeps
the leader's height. The idle goal stands still (its Process is
not traced); the spectate goal follows [its research](../ai-fights.md#spectate) for Melee and the dealer's wary goal,
the crowd's
spectators standing still (their arguments are not traced). Its stand-ins: a spectator holds no target but for the
EngageEnemy run-in; no fight stance, taunt or weapon pick-up; the watch action is a turn to face him; the keep-distance
move is a move action to the band's edge; the chase is always allowed; the tackling man's kind-`0x24` attack and the
shadow test are not built; without join the watched man is the nearest member of any other gang. Both of a crowd
reaction's clip actions are `PlayAnimAction`. The dealer rolls
dirty at Start; his wary scan looks for members of enemy gangs. Player 1's triangle within 1.75 m of a dealer whose offer
stands (after the objects' prompts and the stereos) makes the deal: the table's terms against the inventory, the item
and money moved without notifying the inventory callbacks, and his line said at the buyer (96 at most every 5 s, 97,
101, 105). With both money clips loaded a sale starts the pair (`money_take` on the dealer, `money_give` on the
buyer, both as 668) and completes when the dealer's clip passes its action event `0x41` (`DealerGoal::finishPair()`);
without them it completes at once. His gestures are play-anim actions of the `FDealer` gang's clips (603 and 668 by
variant, blended over 0.5 s; another gang's dealer plays his own 603 and 668) with his lines: the greeting (94),
the offer (95), the refusals, thanks (98) or goodbye (99) as the buyer leaves, after which a new visit starts in
state 1, and the wary look (100).

**The Rumble tactics** (`src/ai/tactic_attack.*`, `src/ai/tactic_confront.*`, [Rumble mode](../rumble.md#coney)).
`TacticAttack` and `TacticConfront` follow the table above; `TacticAttack` is built from
[Unit 2](../ai-reimpl.md#unit-2) and gives the real [Melee goal](../ai-fights.md#melee-goal). **Stand-ins:** the
confront goal (60)
closes on the other gang's leader to its distance and waits. **Coney choices:** a gang's leader is its first standing
member not a player's (else the first standing); the confront's gang radii are 0 and there is always a way between
the leaders, so 9 never fires; the attack's team-ups and spot line, and the confront's postures and formation are not
built. A tactic replaced from
inside its own update or callback is freed after the gangs' update, as the original queues the free (`0x00306630`).

**The character bindings' goals and gangs** (`src/ai/scripted_humans.*`, `src/ai/scripted_goals.*`). `GoalBackoff`
(`0x9b`), `GoalBumLogic` (`0x4f`) and `GoalMoveToUseFlag` (4) are built from their constructors; **stand-ins** for their
unread Process: the back-off walks straight away from the other human while nearer than its distance (in a fight
stance it turns its back to him; the original keeps a band with the move-to-human action and faces him,
[Backoff](../ai-goals.md#goal-backoff)), the bum stands
still and never calls back, and the use-flag goal walks to the flag as `GoalMoveToFlag` does, facing its heading, then
stands there (the flag reserved until the goal ends). `GangInvincible` sets god mode on the members and on later
ones; `GangSetTargetable` sets each member's targetable byte, kept on the human. `GangAddSpawner` keeps up to four
spawners per gang, which spawn (below); `GangClearWanted` has no wanted state to clear; `GangClearResponders` deletes
non-police gangs named `Responder<n>`. `BrSetThugWantsWeapon` and `SetInterrogateParam` are kept only.
`GoalTag` pushes the [tag goal](../ai-goals.md#goal-tag) (`0x5a`, `TagGoal`): the flag claimed until the goal ends, a run
(gait 4) to it, done short of 1.5 m or when the flag is gone, the human is not alive or another holds the flag; at the
flag the other tag is made blank and the spray starts as `HuTag`'s does ([Crimes](../crimes.md#coneys-implementation)),
and the goal ends once the human is no longer tagging. **Coney's reading**: the Tag action's command presses are not
built; the spray starts at once.

**The spawners** (`src/ai/spawners.*`, [Spawners](../ai-gangs.md#spawners)): after each characters' step every spawner in
use whose state is ready (1, 6, 7, 8 and 10 always; 2 past its deadline; 3 and 5 by player 1's distance, taken in 3D)
makes a human once its delay has passed, while fewer of its humans are alive than its limit and it has not made its
total (-1 none). The human is made through the scripts' own `HuCreate` (named `<spawner><count>`, the spawner's model
string as the fifth argument, in its gang), of the next type in turn (the index moved on first, as `0x0016d810` does),
then the callback, when it names a function, is called with its handle, the gang's id and the spawner's name. 6, 8 and
10 place the human out of sight as [above](../ai-gangs.md#spawner-placement) and [the search](../ai-gangs.md#spawner-search)
say: from player 1's camera (Coney has one player, so no second camera refuses a node), the start node the planner's
route ends use, 17 outward searches, the node then checked against the gang's turf boxes; no node, no spawn this update.
**Stand-ins**: 7 is placed as 8 with a value of 0, not on a flag as [its placement](../ai-world.md#spawner-reinforcements)
says; the others stand at the spawner; no door opens; the dispatch states 4 and 9 (no crimes are routed) and the top-up
11 (no gang limits kept) never spawn. A gang runs its spawners only while the level's count is under its cap ([The
level's cap](../ai-gangs.md#spawners)): the police count under `SetCopSpawnMax` for a police gang, the spawned count under
`SetSpawnMax` for any other, 16 each after the level reset, asked once per gang. A human counts off when he is down or
deleted (the original: when his brain is torn down), and the counts start at 0 with each level's spawners; humans
leaving their gang and a dispatch's completed squad are not taken off (neither is built). `GangSetMaxConcurrent` changes
the limit (16 bits; a negative -n spawns waves of n as [above](../ai-gangs.md#spawners) says, from `GangAddSpawner`'s limit
too). An off-screen spawner (`GangSetSpawnerMustBeOffScreen`, or added in state 11) skips an update while player 1's
camera sees the 0.3 m sphere 1.6 m above it ([Off screen](../ai-gangs.md#spawner-unseen)): within the far clip, in view
and
with no wall of the collision mesh on the ray to its centre; **stand-in**: the six-plane frustum is taken as a cone of
half the field of view widened by the sphere.

**The scripts' goals at one human** (`src/ai/engage_goals.*`). `GoalMoveToHuman` (6) drops and re-issues a move
(`MoveAction`, its gait and radius) to where the target is every second, waits 30 updates after a failed route, and
ends within its radius in 3D, when the target is no longer alive in the world, or when its human is down.
`GoalEngageEnemy` (11) is the fight's run-in ([EngageEnemy](../ai-fights.md#engage-enemy), above) at the enemy by
handle, taking him
as its enemy and target; **stand-in** for the untraced wrapper `0x002af528`: where the fight's goal would end, it
pushes a fight goal of 4000 ms and goes on, so it fights the enemy until he is gone or down. The
valid-target test `0x0028d4b0` is taken as alive and in the world. `BrSetType` sets types 1-6 (0 and past 6 are
ignored, **stand-in**: 0's pad hand-over is not built), `BrSetAttackWeight` one kind's weight.
`CfgSetDefaultFollowSlotSet` writes its sets into every formation in use and keeps them for those made later
(`Formations::setDefaults`, through `HumanBindingHost::setDefaultFollowSlots`); every other rules binding
(`rulesCall` in `repo:src/scripting/human_bindings.cpp`, among them `WCEnableAllCommands`, which `EnableAllButtons`
calls) hands the rules back through `ScriptedHumans::applyRules`, which only keeps the defaults for formations made
later (`Formations::keepDefaults`). So the four slots `P1.SetupSnapAttacks` sets survive its `EnableAllButtons`, and
with the disc the bums settle 1.27-1.44 m from the player on four sides and three snaps pass lesson 7
(`repo:tests/platform/disc_level99_snaps_test.cpp`, [the second wave](#level99-snaps)). **Coney's reading**: that
`CfgSetDefaultFollowSlotSet` itself rewrites formations in use is not traced.
`FlagNetAddLink` builds the level's flag network (`src/world_objects/flag_net.h`, 128 nodes) and `FlagNetTraverse`
pushes `PedestrianGoal` (`0x69`) on a human that is not a player's; **stand-in** for its untraced Process: walk
(mode 2 jog, 3 run) to the node nearest the human, then on to a random linked node within 1 m, for ever, standing at a
node with no links; the variant `chance` picks and the two flags are kept, not used.

**The story's goals, gangs and Warrior commands** (`src/ai/scripted_story.*`, `src/ai/story_goals.*`,
`src/scripting/story_bindings.*`, for `level80` and `level87`). `GoalMoveToExitFlag` (2) and `HuExitWorld` set the
brain dead (not a player's) and walk to the exit flag; **stand-ins** for the camera tests: arriving within 8 m of
player 1 picks the nearest other exit (activity 8), arriving farther is `HuDelete`, and every 8 s a human more than
60 m from player 1 is killed (`HuKill`). `GoalTravelPath` (`0x38`) pushes a `GoalMoveToFlag` per point of an
`AddPath` path (a number handle, as Coney's VM has no user types), then stops, loops or turns round. `GoalMelee`
pushes a finding goal (`0x41`) and fights a target it names; **stand-in**: with no target this FindEnemy searches for
the nearest hostile within the sight range, once a second, and a fight started under it keeps a searching one.
`GoalThrowObject` (`0x5d`) walks into range and turns; **stand-in** for the throw: what it holds is let go.
`GoalPlayDynIdle` (`0x23`) walks to the flag, turns to its heading and stands for its time; the clips are kept, not
played. `GangExitWorld` sends each AI member out; once none is alive the callback gets the gang's id and the gang is
deleted unless kept. Turf boxes, leader, respond percentage, hear ranges, investigate response, world-flag use and
attack strategies are kept for readers not built. **Warrior commands** (`src/ai/crew.*`,
[Warrior commands](../ai-gangs.md#warrior-commands), [follow](../ai-gangs.md#warrior-follow)): 0 follow gives the crew's
gang
the follow tactic (`WarriorFollowTactic`: nine slots on the chief's formation, each AI member that can act flushed and given
`GoalFollowPlayer(9 m, chief, 3)`,
which joins the formation, stands while the chief is within 9 m in a walkable straight line and otherwise pushes
`FollowFormation` (0.75 m; after a failed walk it waits 31 updates), turns to the chief's heading when more than 60° off
it every 2-4 s (1-2 s after it takes up again), and in fight mode turns to or moves to (3 s) the nearest attacker of the
crew within 20 m or the chief's target, never attacking). `FollowFormation` walks to its slot, or with no usable slot
behind the human ahead in the queue (2.5 m), by the walk distance (straight, else the planned route's length) at
`FollowFormation_PickSpeed`'s speed; every formation plan retargets the running moves ([follow
retarget](../ai-gangs.md#follow-retarget)), and `MoveAction::retarget` keeps a route whose last node sees the new point,
else starts the move again (or, in a jump, drop or fence climb, ends it on its next update). A move with no route slows
for the turn onto its point (the circle part of [the corner speed](../ai-moving.md#corner-speed)), and arrival is in 3D,
in
plan within 1 m. **Stand-ins** there: "someone's enemy" is any fightable brain holding the follower among its enemies;
the `+0x2d5` match, the trains' part of the straight line, fight stance, pick-ups, idle clips, the vandalising while the
walk fails, the 8 s reshuffle, banter and lines are not built; a move with no route has only the corner speed's turn
limit (the trial walk over the polygons and the 11-update period are [fix list](../ai-pathing.md#pathing-fixes) item 7)
and
its arrival does not take one update's travel off (item 5); `FollowFormation`'s time limit is read as "done once that
long has passed". 2 defend
still tracks the chief (2 m) (**stand-in** for the defend tactic). 1 attack gives the gang the attack tactic
(`WarriorAttackTactic`, type 1): each AI member that can act is flushed and given `FollowAndAttackGoal` on the chief,
which joins his formation and fights the best enemy within 60 m of him: a fight goal (4000 ms) within 1.1 × the far
melee range, else the fight's run-in while it may close on him, else a straight move to him; with no enemy it stays
within 8 m of the chief, turning to his heading every 4 s, and farther walks back to its slot (`FollowFormation`,
0.75 m). **Stand-ins** there: its enemies are its own plus the nearest attacker of the crew; dogs get the same goal,
not `AvoidEnemies`; pick-ups, the fight stance, the fidgets, the chief's clip, the gang's target and the answer line
are not built.
3 hold gives the gang the hold tactic (`WarriorHoldTactic`, type 3): each AI member that can act is flushed and
holds his own spot (`HoldPositionGoal`, 1.5 m), then turns to a heading spread 360° / (members − 1) round the
chief's after 0-1 s with enemies about, else 2-4 s; events 17 (no other human), 19 and 22 (argument 1) hold again.
The hold walks back (gait 2, to 0.5 m) when outside the radius, turns to an enemy more than 60° off, and against one
after him fights (4000 ms) from inside the radius once he is within 1.1 × the far melee range and in sight.
**Stand-ins** there: the enemy is the best-scoring one by the melee terms, "in sight" a clear straight line; the
fidgets, the shuffle, the way round an object, the answer line and event 20's look and taunt are not built; a gang
whose brain `+0x2d5` is set gets this tactic too, not the hiding one. 4, 5 and 6 start nothing. The level's start resets
each player's last command to 0, enables every
command and unlocks the menu (`InitLevel`). Gameplay dispatches follow, forced, for a new player 1 and when a scene
that held him ends, once no scene camera is up; it unlocks his menu every 10 updates and runs the
[automatic commands](../ai-gangs.md#warrior-auto-commands) each update: under follow, attack 1.5 s after an attacker
came within
his far melee range (**stand-in**: Coney's player brain keeps no target, so his attackers stand in for it); under
attack or defend, follow again 1.5 s after no member of the gang has an enemy. Defend on a rear grab, mug or tag,
the player modes 2 and 3 and steal at a store are not built. `GangStartSpawner` switches a spawner ([above](../ai-gangs.md#spawners)).

### Warrior commands, revive and the weapon swap: the gaps {#crew-gaps}

Coney against the original, command by command (user checklist items 11, 16, 8 and 37), as of this page's commit.
The menu itself (`repo:src/hud/war_command_display.cpp`, `repo:src/gamemodes/gameplay_war_commands.cpp`) matches
[HUD](../hud.md#warrior-command-menu): every slot reaches the dispatcher (`dispatchWarriorCommand`,
`repo:src/scripting/story_bindings.cpp`), which hands the tactic to `ScriptedStory::startWarriorCommand`
(`repo:src/ai/scripted_story.cpp`). The original's columns: [Warrior commands](../ai-gangs.md#warrior-commands) and
[the lines](../ai-gangs.md#warrior-command-lines).

The menu's "???" (user playtest, `level80` checkpoint 2) is `GSTRING.COMMAND` entry 7, the text the original shows for
a **disabled** highlighted command ([HUD](../hud.md#warrior-command-menu)). That checkpoint's script switches every command
off and follow back on (`WCEnableCommand` on the player's human) as its first calls, before the level has made his
brain; Coney found no player for the handle then and dropped the switch, so follow too stayed off, greyed and labelled
"???", and the level start's forced follow was refused. Fixed (2026-10-11): the player index comes from the human's
creation until his brain exists (`playerOf`, `repo:src/scripting/story_bindings.cpp`); a command the script leaves off
still shows "???", as in the original.

| Command (menu slot) | Original tactic and members' goal | Chief's line / answer | Coney | Where |
| --- | --- | --- | --- | --- |
| 0 follow (up) | `WarriorFollowTactic` (`0x12`, 9 m): `FollowPlayer` mode 3; a repeat adds `FollowFormation` (`WarriorFollowTactic_Repeat` `0x003114e0`) | 2 `follow` (86 hidden) / 131 | **partial**: tactic and goals built; no line, no answer, no repeat | `repo:src/ai/crew.cpp` |
| 1 attack (up-left) | `WarriorAttackTactic` (1): `FollowAndAttack` on the chief; dogs `AvoidEnemies`; gang's shared target from the chief's | 1 `attack` / 132; gesture 676 / 680 | **partial**: tactic and goal built; no line, answer, gesture or shared target; dogs attack | `repo:src/ai/crew.cpp` |
| 2 defend (up-right) | `Tactic_Defend` round the chief (2.25 m): [`FollowAndDefend`](../ai-goals.md#goal-follow-and-defend), which fights and [rescues](../ai-goals.md#goal-save-human) a cuffed or knocked-out chief | 3 `defend` / 130; gesture 675 / 679 | **stand-in**: each member gets `GoalTrackHuman` (2 m), which never attacks and never rescues | `repo:src/ai/scripted_story.cpp` |
| 3 hold (down) | `WarriorHoldTactic` (3): `HoldPosition` 1.5 m; hide (`0x13`): [`Hide`](../ai-goals.md#goal-hide) at wall points | 4 `hold` (70 hidden) / 133 | **partial**: the hold built; the hide is not (the hold stands in); no line, no answer | `repo:src/ai/crew.cpp` |
| 4 scatter (down-right) | `WarriorScatterTactic` (`0x25`, 75 m): [`Scatter`](../ai-goals.md#goal-scatter) to hiding flags | 158 `scatter` / 159 | **missing**: the tactic is cleared and each member flushed, so the crew **stands still** | `repo:src/ai/scripted_story.cpp` |
| 5 wreck (down-left) | `WarriorStealTactic` (`0x26`): [`WarriorVandalSteal`](../ai-goals.md#goal-warrior-vandal-steal) | 5 `steal` / 141, or 6 `vandal` / 140 | **missing**: as scatter, the crew stands still | `repo:src/ai/scripted_story.cpp` |
| 6 (no slot) | nothing; recorded as last | none | **works** | `repo:src/scripting/story_bindings.cpp` |

Around the commands:

| Behaviour | Original | Coney |
| --- | --- | --- |
| Crew out of range | unforced (and forced defend): nearest member beyond 50 m refuses the command; 18 `range` / 92 `range2`; the per-player flag ([out-of-range flag](../ai-gangs.md#warrior-range-flag)) also refreshed quietly every 40 updates, shown as the menu's out-of-range text and read by the mission-failure check | **missing**: always given |
| Forced and quiet orders | forced commands and a chief already speaking say nothing | silent always (no lines at all) |
| Repeating the current order | unforced: line again, no rebuild | refused silently (matches, minus the line) |
| [Automatic commands](../ai-gangs.md#warrior-auto-commands) | follow → attack / defend / steal; attack, defend → follow; steal → follow beyond 15 m | **partial**: follow → attack and back only (`CrewOrders`, `repo:src/ai/crew.h`) |
| Hiding in shadow | forced hold (hide) after 500 ms or the run time; the old command back on leaving | **missing** |
| Revive ([Combat](../combat-reactions.md#revive)) | d-pad left (command `0x27`) near a revivable downed member with a flash: 351, 353 / 354, flash spent, healed, 76 / 75 | **partial**: player 1 revives (`repo:src/gamemodes/gameplay_revive.cpp`) a revivable member, but no code flags the crew revivable ([open question](../combat-reactions.md#revive)); no AI rescue (`SaveHuman`) |
| [Weapon swap](../ai-brain-types.md#warrior-swap) | instant; the object is attached to the bone of the low pick-up clip's first type-9 event and drawn there; 146 `give_me` | **matches**: the hands swap, the anim set follows, the line plays, and the object is drawn in his right hand (issue #78) |

**Item 37: the given weapon.** In the original the swap puts the object in the Warrior's hand exactly as
`HuPlaceItemInHand` does (`Human_PlaceObjectInHandByPickupClip`, `0x0024c6d8`: the bone and offset of the low pick-up
clip's first type-9 event, message `0x1b` take, his anim set applied), so it hangs from that bone like the player's
([Objects: in a hand](../objects.md#held)). **He fights with it**, confirmed (code): his anim set is the weapon's, so the
same pad commands his attacks press play the armed swings ([Combat](../combat-throws.md#armed-moves)); the attack pick adds
the armed bonus to the strikes ([Picking the attack](../ai-fights.md#pick-attack)); armed fighters fight from anywhere
within 1.1 × the
far range ([pick-ups: using](../ai-gangs.md#warrior-pickups)). He keeps a knife, baton or bat; outside co-op an object of
kind 4 or 6 is **dropped** at his next think with no enemies unless the steal tactic runs
([Think, step 3](../ai-brain-types.md#think-warrior)). In Coney, `ai::swapHeldObjects` (`repo:src/ai/swap_prompt.cpp`)
moves the handle
and `PlayLevelMode::trySwap` (`repo:src/platform/play_level_objects.cpp`) sets his anim set, so his attack pick
counts him armed (`repo:src/ai/attack_views.cpp`), and `PlayLevelMode::heldDrawOf`
(`repo:src/platform/play_level_world.cpp`) draws the object in his own right hand, as player 1's
([Objects: a weapon given to a Warrior](../objects.md#given-weapon), issue #78). The disc test
`repo:tests/platform/disc_given_weapon_test.cpp` gives a bat through the swap at `level80` checkpoint 3 and checks that
his anim set makes the attack dispatcher's square and cross the bat's swings (34, 36; 37 at a grounded target). A live
fight was not staged there: a `Brain::fight` order on a passer-by put 2 m in front of him started no attack in 60 s
(whether he would unarmed was not checked; an open question for the fight goal, Coney reading). Whether his swings hit
with the weapon's damage and wear it is not checked, and the kind 4 / 6 drop is not built.

**Fix for the implementer**, in the order the player notices:

1. **Scatter and wreck** do nothing (the crew freezes): build `WarriorScatterTactic` with the `Scatter` goal and
   `WarriorStealTactic` with `WarriorVandalSteal` ([Warrior commands](../ai-gangs.md#warrior-scatter), [steal](../ai-gangs.md#warrior-steal));
   add the steal automatic command and its return to follow.
2. **The voice lines** (item 16): call the chief's line from the dispatcher for unforced orders (volume 1.5,
   interrupting; the hidden variants), note in each command tactic whether it plays, and have one free member answer
   when it ends ([the lines](../ai-gangs.md#warrior-command-lines)); Coney's `sayCommand` service already takes volume and
   interrupt. Add the attack and defend gestures (676 / 680, 675 / 679).
3. **Defend**: replace `GoalTrackHuman` with `Tactic_Defend` round the chief and `FollowAndDefend`, so the crew
   fights for him and (with `SaveHuman`) frees or revives him.
4. **Draw the Warrior's weapon** (item 37): place every human's held object at his own pose, not only player 1's
   (the same bone and offset as the swap's placement); then add the kind 4 / 6 drop rule.
5. **Revive** (item 8): built for player 1 ([Combat](../combat-reactions.md#revive)); the crew's flag (`Human_Init`'s) and
   the AI rescue remain.
6. **The 50 m range refusal** with 18 / 92, the out-of-range flag with its menu text and its mission-failure shortcut
   ([out-of-range flag](../ai-gangs.md#warrior-range-flag)), then the hide tactic and the hide-in-shadow order.

**The fourth mission's brain and human calls** (`src/scripting/mission4_bindings.*`, for `level34`). `BrSetPedType`
keeps the low 16 bits at the brain (**stand-in**: neither the civilian brain nor the mugging reads it yet).
`HuSetWounded` wounds once: the fight, grab or throw ends, health drops to a quarter and the brain is flushed; healing
clears the mark (**stand-in**: no wounded clips, and nothing reads the 14 s stamp). `ChangeBlocker` sets or clears flag
8 on the path polygon holding the point whose bounding box's middle is nearest (**Coney choice** for the centre).
`GoalStationaryThrower` (`0x8c`, `src/ai/riot_goals.*`) walks back within 0.5 m of where the goal began, turns to the
nearest hostile within three quarters of its sight range, plays clip `0x225` and waits 1000 × delay to 1000 × delay +
1000 ms (**stand-ins**: nothing is thrown, the eight object types are kept; the one wait in five with another idle is
not built). `GoalRiot` (84) follows [GoalRiot](../ai-scripting.md#riot): the free act at Init, the turf gate on the nearest
player (`pointInTurf` over the gang's turf volume boxes), the one-draw decision, the roam (toward the player every 27th
destination, the turf every 79th, else the 10 m wander, else a point 5 m away; arrival 0.5 m, gait 4, the move deadline
rolling over each second, 30 failed moves to leave), the acts counting down, the fight pick (gang soldiers only, the
sixth argument letting the player be picked) and the exit flag in the gang's turf at gait 4. **Stand-ins**: the
smash and loot searches find nothing (no vandalisable objects or store loot are hooked), so an act only counts down;
a turf box's centre and radius are its middle and half its diagonal; "on an area" and "reached in
a straight line" are both the planner's straight-line test and no point is dropped to the ground; human `+0x333` is
0; a running move is replaced rather than retargeted; the shouts, taunt and head glances are not made.

**The story's tactics** (`src/ai/story_tactics.*`; Attack and Confront are the Rumble's above). Group moves give the
leader the moving goal and have the others track him (3, 1, 0.75 and 4 m; **stand-in** for `Goal_FollowPlayer` in
the formation): MoveToFlag and WalkinTall fire 8 once the leader's walk ends, WalkinTall returns 7 while a member is
within `range` of a hostile, TravelPath walks the `AddPath` path (mode 1 looping, else 2); Wander's leader and a
pathless TravelPath's stand. HanginOut and UseFlag walk the members to the flag (HanginOut narrowing view and sight
unless fully aware); UseFlag returns 7 once player 1 is within `range` of the flag and sends them off. Idle holds
places, and with `dynIdle` breaks the dynamic idles off on events 1, 11 and 16, returning 15 when none is left.
Defend tracks the human at `range`: 11 once he is gone or out of health, else 9 with no enemy. HoldTheLine sends
min(line length, 60 % of the members) to the line's two flags in turn, the rest to the third: 9 with no enemy.
Pursue makes the target gang's leader an enemy and melees: 9 once that gang is gone or leaderless, 7 while a member is
within `range` of one of it. Scout (`src/ai/tactic_scout.*`) posts each member where he stands (the Scout goal:
threat response 0, a 500 ms scan, back to his post at a walk or a jog and turned to his heading); the members at
their posts scan (`ScanSchedule`), and every 200 ms one with an enemy and no fight is given one. A member at his post
who is hit by someone he sees, warned of an attack or spots an enemy (events 1, `0x10`, `0xb`) fights him and, with
a responder spawner (state 9 or 10), the enemy's gang's second wanted timer not running and no other caller, gets a
CallGang goal: he runs (gait 5) to the farthest phone flag (activity 6) within `range` (twice his far melee range
when negative), or calls where he stands, and after 1.5 s the call is made and the enemy's gang gets its second
wanted timer. **Stand-ins**: ManWeaponPile, Vandalize, Steal and AvoidEnemies hold their places, their goals not
traced; the scouts' radar blip, clips, glances, roaming, help call within 15 m, investigation of an unseen hit and
the gang's alert state, the caller's lines, clips and call spot, and the responders themselves (the level only logs
the call); banter, answering violence, the anim substitutions, HoldTheLine's 12 and 14 and Pursue's search time are
not built.

**Open in Coney.** The pattern read at Start and in the pick; the reposition's band-keeping move and taunt; the move's
sight checks; choke points and the waypoint queues; the dynamic obstacles; the legs of edges 8, `0x10`,
`0x40` and `0x80` (taken as plain walking, with `+0x284` 2 and 4 never set); the move's object to face; the turn clip
(398) on the spot; GoalMoveToFlag's interval gesture, the fight stance's switch-off and the gang's notice; the scene
system, the dynamic clip slot and clips by id; the head look-ats; the sender of message 1 and its attacker; the gang's
alert state, bounds, return to calm and neutral rule; the anim
substitutions; the crowd's cheer idles; the formation's ground ray, line of sight and assignment mode `+0x275`; the
dealer's run to a flag, line of sight, shove and icons; the other tactics; the attack's steer, the
post-block pause and
the run-stop.

**The hub's goals and gangs** (`level95`; `repo:src/ai/hub_goals.h`, `repo:src/ai/scripted_hub.h`): `GoalAreaWalker`
(`0x47`), `GoalBoxer` (`0x9e`), `GoalGrabTarget` (`0x1f`), `GoalPeddler` (`0x50`), `GoalPlayGenAnim` (`0x26`) and
`GoalShopkeeper` (`0x82`) follow their binding pages; a fleeing gang (`GangCanFlee`) sends its class-11 members off with
the pedestrian reaction goal (`0x6b`) and line 9 ([its flight](../ai-goals.md#goal-ped-reaction): the nearest reachable
exit flag, until no camera sees him, else AvoidEnemies 7-14 m). **Stand-ins**: the boxer fights with his own attack
weights; the grab target is held by standing at it, facing it, with no damage; the goals' lines (`beckon`,
`phone_gang`...) are kept, not played; the flight runs 10 s at gait 4, 10 m legs away from the enemy; a gang's starting
count is noted when `GangCanFlee` turns it on.

**Gangs of a kind** (2026-10-06): `GangMakeEnemiesOfType` makes a gang and every gang in use of a kind enemies both
ways (`Gangs::makeEnemiesOfType`). `GangSetAlwaysSeen` keeps the gang's byte (`Gang::alwaysSeen()`), which
[the enemy scan](../ai-fights.md#enemy-scan) reads (`ai::scanSees`); **Coney stand-in**: the fight and spectate goals'
skip of the line of sight for such a gang is not built.

**The fifth and sixth missions' goals** (2026-10-06; `repo:src/ai/chase_goals.h`): `ActTurnToDir` queues the turn to
a heading ([Turning](../ai-scripting.md#look-at)); `GoalGuardFlag` (43), `GoalLeadChase` (73), `GoalBigLedgeThrower` (138)
follow their binding pages and `GoalDevilRun` (152) [its section](../ai-scripting.md#devil-run): the hindmost segment, the
chaser farthest back, the pace and a move action to the path's last point whose speed the pace sets
(`MoveAction::setSpeed()`). **Coney choices and stand-ins**: the lead chase steers straight from point to point (no
route planning) and counts a point reached within 1 m; the guard's LeftTurf on an event and its time word are not built;
the devil runner's attack fights the chaser (the melee and engage-enemy goals are not built) and the urgency's brain
byte is not kept; the ledge thrower has no objects to pick up or throw yet, so each throw is a turn to the nearest enemy
(or a target flag) and the wait, and its animation name is kept, not applied.

### What an implementer needs {#implementer}

Build in this order; each step is testable without the game.

1. **The shared input path.** Give every human a per-player record (command `+0x20`, stick angle and magnitude) and
   run the dispatcher and the locomotion for every human from it ([Tasks](../tasks.md#humans-update)). An AI writes the
   record; nothing else about a human differs.
2. **The held flags** ([Tasks](../tasks.md#held-flags)): without them an AI cannot know when its attack is over.
3. **The brain shell**: type from the class's `behaviour`, think one step in five (staggered by index), update every
   step, brains before the dispatcher.
4. **Goals and actions**: a goal stack of 10 with Start / Process / End / Resume and the return codes 0, 1, 2; an
   action queue of 8 with a delay, Start, Update and a refusable Abort; goals start only on an empty queue.
5. **The fight goal and the attack action** for `level99`'s fights: target, attack slots, the attack delay, the
   weighted pick from the class's `Att_*` table, move-to-human when out of reach, `Brain_QueueAttack`'s chains, and
   the attack action's wait on `0x5c0221f`. The command is written **once**, in Start (not every update); the attack
   delay is halved when the target targets the attacker; the target's `+0x1ec` takes a separate per-kind time, not
   the attack delay ([The attack action](../ai-fights.md#attack-action)). The move actions steer by a heading and speed
   in the brain
   (`+0x110`, `+0x114`), not by a stick ([Moving](../ai-moving.md#moving)).
6. **The block goal** and the counter, driven by the attacker's warning: `+0x200` counts events `0x10`, sent only to a
   human whose range and field of view (`+0x130`, `+0x12c`) take in the attacker ([Blocking](../ai-fights.md#block)). An
   AI human
   **never blocks**: the block goal's command 4 starts nothing, because the dispatcher starts a block only on R1 held on
   a real pad. Give it the no-reaction flag `0x800` (damage lands, no reaction; cleared from the goal's sixth update)
   and, on every update while the block time runs, the counter roll: command 3 at the counter chance, which counters
   only a grab (69-71 → 76) or a tackle (2-4 → 9) aimed at it.
7. **Reaction goals** for the states a fight produces (stunned, knocked down, grabbed), one update after the state.
8. **Scripted goals** for `level99`, in this order; each sub-step is testable on a synthetic level:
    1. **The route planner** ([Path planning](../ai-moving.md#path-planning)): the path polygons' inside test exists
       ([Level loading](../level-loading.md#path-data)); add the walkable-line test, the C / D records as a graph, A\*
       with the edge costs and the 65000 cap, the retry and `0x80` detour, the route pool with its shortcuts and
       use counts.
    2. **The move action** on it ([The move action](../ai-moving.md#move-action), [Following](../ai-moving.md#route-follow)):
       straight when the line is walkable, else waypoints with the 0.25 m radius; the corner speed; the stuck test and
       brain `+0x284`. The search mask is `0xff`, and a leg of kind 8 or `0x80` is a climb through the player's `Climb_TryStart`
       ([link kinds](../ai-moving.md#route-follow)): `level99`'s Vermin crosses a fence that way.
       Steering round humans ([Steering](../ai-moving.md#steering)) is built; choke points and
       [queues](../ai-moving.md#queues) can follow later.
    3. **`GoalMoveToFlag`** ([GoalMoveToFlag](../ai-scripting.md#move-to-flag)): offset target, arrival radius, the
       face-the-flag turn, re-planning when a move ends short, flag message 8 and the gang notice in End.
    4. **The turn actions** and `ActLookAt` ([Turning](../ai-scripting.md#look-at)): heading to brain `+0x110`, done within
       15° or after 3 s; the fourth argument is a start delay.
    5. **Lua callbacks** scheduled 33 ms after a goal's End with (handle, completed)
       ([Scripted goals](../ai-scripting.md#scripted)), then `GoalPlayDynAnimation` (slot 668) and `GoalAddressPerson` with
       its scene goal `0x21`.
    6. **`GoalFight`, `BrFlush`, `BrDead`**: flush ends goals (callbacks fire); dead clears actions only.
    7. **Gangs** ([Gangs](../ai-gangs.md#gangs)): records, enemy and friend masks, `GangBrDead`, `GangSetThreatResponse`
       on
       the current members, `Gang_OnEvent` before `Brain_OnEvent`, and `GangSetMsgHandler`'s event 18 with the count
       still standing (`level99` ends its fights on it).
    8. **Formations and `GoalTrackHuman`** ([Formations](../ai-gangs.md#formations)): slot sets in 1/16 m, nearest-follower
       assignment, the 1 or 2 s re-plan, a slotless tracker that only turns.
    9. **Tactics**: the base (a tactic suppresses the fight goal), then `TacticCrowd` and `TacticTrigger`
       ([Tactics](../ai-gangs.md#tactics)); `GoalDealer` ([GoalDealer](../ai-scripting.md#dealer)) for `level99_lesson2`.

## Open questions

- Movement and world use: MoveToUseFlag's clip states once he is on the flag; the pedestrian's flag re-use timer
  (goal `+0x6c`, 65 s, inferred from the value); the gait AvoidEnemies keeps in its `+0x3c`; why ChaseSupport jogs
  within 3 m;
  ManWeaponPile's Process (`0x002a3498`, not read).
- The target's pattern bytes `+0x5d0` / `+0x5d1`. Event senders and the think handlers' detail:
  [AI brain types](../ai-brain-types.md#open-questions); moving: [AI moving](../ai-moving.md#open-questions); gang
  fights: [its page](../gang-fights.md#open-questions).
- `GoalBumLogic`'s begging (type 2's prompt, the chance, the callback, the 12 s timer) and what sets `+0x36`; what
  `GoalBackoff`, `GoalMoveToUseFlag` and the pedestrian goal (`0x69`, with its two variants
  and `FlagNetTraverse`'s flags) do each update (Process), and the use-flag goal's two floats; what
  `GangInvincible` sets on a member (`0x0016a000`).
- Which record `+0x08` bits the play-anim action's clip holds (`0x0025a3e0`), its rate for a dynamic clip and its blend
  out; Coney holds `0x80000`.
- The formation's assignment mode `+0x275`, and `GoalFollowPlayer`'s modes 1, 2 and 4.
- What reads the turn action's `+0x10` (`ActLookAt`'s turn value) and the play-anim action's flag (loop or hold?).
- What the rip-off's `gen_push` clip does to the buyer ([Buying](../ai-scripting.md#dealer-buy)).
- Who sends a gang's event 18 (a member down or dead), and with which attacker: `Gang_OnEvent` reads it, but its
  sender was not found (`0x0022dd98` and `0x0022e020` are the arrest's).
- What a scene's end passes to the callback `Goal_PlayAnimation` hands it (`0x00353f40`).
- The tactic event codes (`TacticGetString`, `0x00315c58`) passed to a tactic's callback.
- Whether `CfgSetDefaultFollowSlotSet` (`Cfg_SetDefaultFollowSlotSet` `0x00294788`) rewrites the slots of
  formations in use or only the pool's defaults for formations made later.
- A turf box's radius `+0x40` (Coney: half its diagonal). Human `+0x333` is the detail level from the camera
  distance ([Characters](../characters.md#ai-locomotion)).
- The hold's damage (`0x00510acc`) of `GoalGrabTarget` and the boxing attack weights (`0x00511120`) of
  `GoalBoxer`.
- Which sound each hub goal line plays (`beckon`, `store_greet`, `phone_gang`, `dead_meat`, `cower`, `mug_grunt`).
- The Warrior commands' tactics (`0x00310e00`, `0x00320530`, `0x00313400`, `0x00319570`, `0x00320b60`), the exit
  goal's on-screen test and the finding goal's search values (90, 30, 10).
- Which class `+0x11b` value 13 is ([Combat](../combat/index.md#open-questions)), and what the byte
  `*(0x0051489c) + 0x56e3` that lets every AI counter is.
- What reads `RunCarrotGoal`'s hostile flag (goal `+0x25`, set by `RunCarrotGoal_Init` `0x002e1230`,
  [AI goals](../ai-goals.md#by-address)); Coney does not model it.

## How this was checked

All five were run (2026-10-05, PCSX2 2.9.94) with the scenarios `civ_fight`, `civ_block`, `civ_block_fov`,
`civ_approach`, `civ_knockdown` and `tick_split` in `repo:research/traces/scenarios/` and the call hooks in
`repo:research/traces/patches/`. Brain *i* is at `0x006d53f0 + i × 0x2f0`, its per-player record at `0x00660f50 + i
× 0x2c`. The results are below and on the topic pages: [the attack action](../ai-fights.md#runtime-attack-action),
[the block](../ai-fights.md#runtime-block) and [moving](../ai-moving.md#runtime-civilian-move).

The first checks used the street civilian as a stand-in (same `Att_Normal` table, type 2 set by hand); the
sparring fight below then confirmed the Warriors' own numbers.

### The second wave in the snap lesson {#level99-snaps}

The second wave (`CombatEnemy2`'s `SecondWave01`-`04`, bum models) is never sent to fight in lesson 7: it **surrounds
the player and stands there**, so that whichever way he snaps, a bum is there to hit
([the lesson](../scripting.md#level99-lessons)).

- **Set-up** (`P1.SetupSnapAttacks`, the return function of the scene `l99_c7`), inferred from the disassembly of
  `level99_combat.lua`: `BrDead(player, false)`; `GangSetTargetable(CombatEnemy2, true)`; the player's follow slots:
  `BrSetFollowSlotSet(player, 0)`, `BrSetNumFollowSlots(player, 4)` and slots 0-3 of set 0 at **(0, 1), (1, 0),
  (0, −1), (−1, 0)** m (ahead, right, behind, left); Rudy (`GenWarrior`) gets a bottle and walks to `fRudyStand`,
  whose message 8 (`P1.RudyAtFence`) plays his drink and a line; then for each `CombatEnemy2` member
  `HuSetNoTarget(false)` and **`GoalTrackHuman(member, player, 0.5)`**; then `EnableAllButtons()`,
  `EnableCommand(player, 1, 0)` and `EnableCommand(player, 40, 0)`, the lesson's text, and `P1.StartSnaps` 5 s later.
  No `GangBrDead(false)`, `GangSetThreatResponse` or `GoalFight` touches the gang, so its members stay dead to the AI
  with threat response 0 (as set up, [the cast](#level99)) and never attack; `GoalTrackHuman` still runs.
- **Confirmed (runtime)**, PCSX2 2.9.94, `level99` checkpoint 1 with `P1.PowerMovesDone` scheduled from lesson 2
  (call hook): during the scene the four hold `JoinCinematic` (`0x29`); when it ends each has **TrackHuman** (`0x30`)
  on top, brain `+0x09` still 1, and they stand **1.0-1.4 m** from the player at about 0°, +104°, −95° and −175°
  from his facing, without actions. With the stick at 0.6 straight up for 3 s the player walked off; they followed
  and stood within 1.3 m of him again 3 s after he stopped. None attacked.
- So the four slots and `GoalTrackHuman`'s 0.5 m are what put a bum within the snap's 2 m
  ([Attacks](../combat/index.md#attacks)) on every side.

### Think staggering {#runtime-think}

The `tick_split` scenario: a hook on `Brain_Think` `0x0028f6c0` over 215 steps matched `index % 5 == (counter >> 1) %
5` on every step; brains whose human fails `0x0023d790` (in the street save, Vermin and the suspended Ash) did not
think at all.

### Reaction goals {#runtime-reaction-goals}

The `civ_knockdown` scenario, a civilian fighting the player: the knocked-down goal `0x17` (vtable `0x00540210`)
appeared on
the update after the state word took `0x80000` and stayed until the update after it cleared (88 updates), then the
fight goal resumed from the stack; the stunned goal `0x18` came with `0x100000`, the tackling `0x13` and grabbing
`0x12` goals with the civilian's own tackle and grab. So the reaction goal lags the state by one update, because the
brains run before the human's state update ([Tasks](../tasks.md#humans-update)).

### The steal order at the pharmacy {#runtime-steal-store}

PCSX2 2.9.94, scenario `repo:research/traces/scenarios/steal_store.toml`
              (level80's pharmacy under the steal order, no stick input): both Warriors' claimed objects stood behind
              glass; each reached this branch every 1.2-1.3 s (23 times each in 60 s), no pane broke (hook
              `glass-break`), no owned object was taken (hook `take-owned`) and no money or loot was added. Why
              the queued kind 0 broke nothing there is not known (an [open question](../crimes.md#open-questions)).

### The sparring Warriors' numbers {#runtime-sparring-numbers}

Read on the sparring Warriors (classes 58-60), the three `CombatWarriors` in
[the sparring fight](../ai-fights.md#level99-fight): power class 40 (`GenWarrior` uses 39), whose record gives block
chance `+0x08`
0.2, while hurt `+0x0c` 0.1, attack-delay factors `+0x1c` / `+0x20` 20 and 20, counter chance `+0x24` 0.08 and pattern
threshold `+0x37` 0. With the global `0x00510ac8` = 60 that is a 12 % block try (6 % hurt) and an 8 % counter roll;
the threshold 0 means `+0x15` is never set at Start, so the counter is rolled throughout a block. Their brains have
field of view 1.92 rad, range 30 m, melee range 3 m / 5 m and threat response 2, and the player's brain allows 4
attack slots.
