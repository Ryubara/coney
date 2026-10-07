# AI humans (brains, goals and actions)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra
(2026-10-05); every claim is confirmed (code) at the cited address unless it says otherwise. The script calls of
`level99` were read from the disc's compiled scripts with `coney-tools` (ids, names and values only). The runtime
checks were run in PCSX2 2.9.94 the same day ([Runtime checks](#runtime-checks)); their results are in the body,
marked confirmed (runtime).

## Purpose

How a human that no pad controls decides what to do and does it: the per-human **brain**, its **goal stack**, its
**reaction goal** and its **action queue**; how a brain picks a target, chooses to attack, block or move, and how it
moves; and which brains the first mission (`level99`) uses. With [Tasks](tasks.md), it is what the next milestone
needs: other characters that move in a level and fight back.

In one paragraph: every human has a **brain** (0x2f0 bytes) with a **type** taken from its character class (0 the
player, 1 cops, 2 gang soldiers, 3 the Warriors, 4 civilians and bums, 5 dealers, 6 one civilian kind). Each brain has
three handlers chosen by its type: an **update** run every character step, a **think** run one step in five, and an
**event** handler. For every AI type the update is the same function: run the **reaction goal** if the human's state
calls for one (grabbed, knocked down, stunned...), else the **top goal** of a stack of up to ten, then the front
**action** of a queue of eight. Goals are long-lived intentions (fight this human, walk to that flag, block for a
while); actions are short steps (attack once, move to the target, play a clip). An attack action does not animate
anything itself: it writes a **command id**, the same id a pad press produces, into the human's per-player record,
and the human's dispatcher plays it exactly as it would for the player ([Tasks](tasks.md#humans-update)). The fight
goal paces attacks with tokens, attack slots and a per-class delay, and picks each attack by **weighted chance** from
the class's 45-entry attack table (`Att_*` in `config_preload2.lua`).

## Original structure

The brain, goal and action code lies in `Human/`, in the stretch after `cns/cnsplayertag.cpp`
(`0x0028a360`-`0x00306630` at least, [Source map](source-map.md#position), inferred); no path string names its
files. The script bindings are on [AI bindings](../references/bindings/ai.md). Names are ours.

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
| `0x0029a8c0` / `0x00251a70` / `0x0029aa88` | `Route_Request` / `Route_AStar` / `Route_Follow` | [path planning](#path-planning) | confirmed (code) |
| `0x00289138` | `Human_SteerAroundHumans` | [steering](#steering) | confirmed (code) |
| `0x002da3b0` / `0x002da588` | `MoveToFlagGoal_Init` / `_Process` | goal type 1 | confirmed (code) |
| `0x0016d170` / `0x00164c20` | `Gangs_Update` / `Gang_OnEvent` | [gangs](#gangs) | confirmed (code) |
| `0x00306690` / `0x003067d8` | `Tactic_Start` / `Tactic_Process` | [tactics](#tactics) | confirmed (code) |
| `0x00293c68` / `0x002956d0` | `Formations_Update` / `Formation_Plan` | [formations](#formations) | confirmed (code) |

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

## Data

### The brain {#brain}

60 brains of 0x2f0 bytes at `0x006d53f0`, one per human slot. Confirmed (code) at the accessors cited and the
script bindings on [AI bindings](../references/bindings/ai.md); meanings marked inferred come from how the field is
used.

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
| `+0x90` / `+0x118` | vec4 / float | the point a move aims at / its radius ([Moving](#moving)) |
| `+0xa0` | struct | steering state ([Steering](#steering)) |
| `+0xe0` | struct | route state ([Path planning](#path-planning)) |
| `+0xf8` | struct | perception, refreshed at each think (`0x00298f88`) |
| `+0x120` | u8 | targetable / attack-slot flags |
| `+0x124` | handle | the **target** |
| `+0x110` / `+0x114` | float | the heading to move along / the speed ([Moving](#moving)) |
| `+0x12c` | float | field of view (`BrSetFOV`), radians: 1.92 (110°) when made; civilians π/2, dealers 2π, Warriors π |
| `+0x130` | float | sight range: 30 m when made; civilians 15 m, Warriors 55 m ([The enemy scan](#enemy-scan)) |
| `+0x134` / `+0x138` | float | hearing radius: 50 m, and 20 m for event 20 (`Brain_IsInHearRange` `0x002935d8`) |
| `+0x13c` / `+0x140` | float | melee range near / far (`BrSetMeleeRange`): **3.0 / 5.0 m** for every brain ([below](#melee-range)) |
| `+0x144` | u32 | the enemy-scan interval: 2000 ms, cops 1000 ms |
| `+0x15c` / `+0x160` | fn / u32 | the enemy-scan filter / the time of the last scan |
| `+0x14a`, `+0x14b` | u8 | attack spacing |
| `+0x152` | u8 | attackers on this human (player brain, counted every 300 updates) |
| `+0x164` | handle[16] | the **enemy list** |
| `+0x1a4` | handle[16] | the **attack slots** taken on this human; the maximum at `+0x1e4` (`BrSetNumAttackSlots`) |
| `+0x1e8` | u32 | the earliest time this brain may attack again (ms) |
| `+0x1ec` | u32 | the earliest time this human may be attacked again (ms) |
| `+0x1f0` | handle[4] | active attackers |
| `+0x200` | u8 | an attack is coming (set by the attacker's warning, cleared each update); the block's trigger |
| `+0x208` | ptr | an attack-weight table that overrides `+0x298` |
| `+0x20c` | ptr | the human's **gang** ([Gangs](#gangs)) |
| `+0x212` | s8 | the formation this human follows in, −1 none ([Formations](#formations)) |
| `+0x21c` | int | **threat response** (`GangSetThreatResponse`): 0 never fights; **2** when the brain is made (`0x0028a570`, from `Human_Init`, the store at `0x0028a878`); the civilian, dealer and shopkeeper thinks set it to 0 once no fight goal remains ([Brain types](#brain-type-handlers)) |
| `+0x220` | int | damage response |
| `+0x265` / `+0x266` / `+0x267` | u8 | wants a weapon / a hat / reacts to violence |
| `+0x26c` | int | ped type |
| `+0x284` | int | why the last move failed: 1 no route, 2 or 4 an edge, 3 stuck |
| `+0x298` | u8[45] | **attack weights**, one per attack kind (`BrSetAttackWeight`; filled from the class's `Att_*` table) |
| `+0x2d1` / `+0x2d2` | u8 | world-flag use allowed / a chance byte (1 / 10 when made; no reader of `+0x2d2` found, [World flags](#ai-flags)) |
| `+0x2d4` / `+0x2d5` | u8 | hidden in shadow (`Brain_SetHiddenInShadow`) / sees a hidden human within 2 m ([The enemy scan](#enemy-scan)) |
| `+0x2d8` | u8 | join the player's gang |

### Types {#types}

`Human_Init` (`0x00218008`) sets the type from the character class's byte `+0x11a` (`CfgChar`'s second argument,
`behaviour` on [Characters](../references/characters.md)); a human with `+0x19d` set gets type 2 unless its class
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
| 6 | the `civl_co_di` kind (441): shopkeepers, [below](#think-shopkeeper) | `0x0028fbb0` | `0x00303f10` | `0x00304228` |

So type 3 is the Warriors' brain, which an ally Warrior runs (this answers [Combat](combat.md#open-questions)'s "brain
type 3"). The player's update `0x003035d8` only keeps books (target validity, attackers counted, the follower check
`0x002d09f0`); it pushes no goals.

### Goals {#goals}

Goals come from a pool of 170 of 0x90 bytes at `0x006e0430` (bitmap `0x006ceac0`). Base fields: `+0x00` the brain,
`+0x04` started, `+0x05` resumed, `+0x08` the **time limit** (−1 none), `+0x0c` vtable. The constructor stores a
duration in `+0x08`; `Goal_Start` (`0x0029ed58`) adds the time it starts, and `Goal_Process` (`0x0029eed8`) ends the
goal (returns 2, its Process not called) once the game time has passed it; −1 never passes. Confirmed (code) at both
addresses; this corrects the earlier reading "wait-until time". Vtable function words: `+0x0c`
the type id, `+0x14` the class's name (a string, such as `MoveToFlag`), `+0x1c` destroy, `+0x24` Start, `+0x2c` End,
`+0x34` Resume, `+0x44` **Process**, `+0x4c` event (default `0x0029ef80`). There are 149 goal classes, types 0-158
with ten unused, all on [AI goal types](../references/goal-types.md) with the bindings that make them; confirmed
(code), each vtable read. The ones this page uses (names the game's):

| Type | Goal | Constructor | Notes |
| --- | --- | --- | --- |
| `0x01` | MoveToFlag | `0x002da3b0` (vtable `0x00542130`) | [`GoalMoveToFlag`](#move-to-flag) |
| `0x06` | MoveToHuman | `0x002dc4f8` | |
| `0x08` | Melee | `0x002ade10` (vtable `0x00540450`) | [below](#fight-approach); Process `0x002aebf8` |
| `0x0b` | EngageEnemy | `0x002af5b0` (vtable `0x00540330`) | the run-in, [below](#engage-enemy); Process `0x002afa48` |
| `0x0f` | **Fight** | `0x002b2c20` (vtable `0x005402d0`) | [below](#fight) |
| `0x12`-`0x1a` | reaction goals | | [below](#reaction-goals) |
| `0x1b` | **Blocking** | `0x002b54d8` (vtable `0x0053feb0`) | [below](#block) |
| `0x1f` | GrabTarget | `0x002bb458` | |
| `0x21` | PlayAnimation (a scene) | `0x002e4980` | pushed by `GoalAddressPerson` ([rows](ai-goals.md#goal-play-animation)) |
| `0x22` | PlayDynAnimation | `0x002d2eb0` | [`GoalPlayDynAnimation`](#dyn-animation) |
| `0x29` | JoinCinematic | `0x002e53e0` (vtable `0x005421f0`) | `GoalJoinCinematic` (`0x002e5300`); Process `0x002e5618` |
| `0x30` | TrackHuman | `0x002df250` | [`GoalTrackHuman`](#formations) |
| `0x36` | HoldPosition | `0x002be640` (vtable `0x00540510`) | `GoalHoldPosition` (`0x002be590`), and the tactic code at `0x00313718`; Process `0x002be818` |
| `0x41` | FindEnemy | `0x002c0430` (vtable `0x00540a50`) | [below](#fight-approach); Process `0x002c0748` |
| `0x4f` | BumLogic | `0x002abef8` | `GoalBumLogic` |
| `0x57` | AddressPerson | `0x002cc408` | [`GoalAddressPerson`](#address-person) |
| `0x69` / `0x73` | Pedestrian / Patrol | `0x002aae30` / `0x002c1338` | `FlagNetTraverse` |
| `0x6b` / `0x72` | PedReaction / Hostile | `0x002aa558` / `0x002d55d8` | pushed by the civilian think |
| `0x80` | Dealer | `0x002c6d90` | [`GoalDealer`](#dealer) |
| `0x85` | BigFighter | `0x002e9cd0` (vtable `0x00542940`) | `GoalBigFighter` (`0x002e9c60`); taunt, guard, attack, pick up ([rows](ai-goals.md#goal-big-fighter)) |
| `0x9b` | Backoff | `0x002d92e8` | |

### Actions {#actions}

A pool of 100 actions of 0x50 bytes at `0x006e63d0` (bitmap `0x006cead8`). Base fields: `+0x00` the brain, `+0x04`
s16 delay in ms before it starts (-1 = a random 0-500), `+0x06` started, `+0x08` vtable. Vtable function words:
`+0x14` Start (2 = already done), `+0x1c` Abort (0 = refuses), `+0x24` destroy, `+0x2c` Update (2 = done). The base
update `0x002f9e48` counts the delay down against the brain's `+0x30`. `Brain_ClearActions` stops at an action that
refuses to abort, so an attack in progress is never cut by a new goal.

### Attack kinds and commands {#attack-kinds}

An attack action carries one of **45 attack kinds**; `AttackKind_ToCommand` (`0x00231090`, jump table `0x0055c170`)
turns it into the command id written to the per-player record ([Commands](combat.md#commands)):

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
or 8. The kinds follow the [damage table](combat.md#damage-table)'s indices (0 `X1`, 1 `S1`, 2 `XX2`, 3 `SX2`, 4
`XS2`, 5 `SS2`, 6 `SSX3`, 7-9 `SSS3` and its holds, 10 the snap); inferred from the commands and the queued chains.
Every kind with its delay, command and anim ids, and every attack table: [Attack kinds and
tables](../references/attacks.md).

## Behaviour

### The update {#update}

`Brains_Update` (`0x00293b28`), step 5 of [`Humans_Update`](tasks.md#humans-update), for each of the 60 brains that
is enabled (`+0x08`) and whose human passes `0x0023d790`:

- **Think** (`Brain_Think`, handler B) when `index % 5 == (counter >> 1) % 5`: each brain thinks once every five
  character steps (6 Hz), staggered across brains. Confirmed (runtime): a hook on `0x0028f6c0` over 215 steps matched
  the formula on every step, with `counter` the tick counter `0x005104f4`; brains whose human fails `0x0023d790`
  (in the street save, Vermin and the suspended Ash) do not think at all.
- **Update** (`Brain_Update`, handler A) every step.

Then the [waypoint queues](#queues). [Formations](#formations) (step 2) and [gangs](#gang-update) (step 4) run before
the brains.

### Handlers {#handlers}

`Brain_InstallHandlers` (`0x0028c1a8`): a brain with `+0x09` set ("dead", `BrDead`) gets A = `Brain_UpdateGoals` and B,
C = empty stubs (`0x004edd40` returns, `0x004edd48` returns 0): no think handler (the type's B, which pushes the type's
own goals, [Think handlers](#brain-type-handlers)) and no events, but A is the same update every live type uses, so
**the goal stack and the actions still run** ([one update](#update-goals); its skip tests read the gang's `+0xd4` and
the brain's `+0x0a`, never `+0x09`). `BrDead` itself (`Brain_SetDead` `0x00292330`) only clears the actions and sets the
byte (`Brain_SetHeld`, which reinstalls the handlers); goals already on the stack, such as a follow goal, go on running
and queue new actions. To freeze a human a script also flushes it (`BrFlush`) or suspends its gang (`GangSuspend`, as
`kinghill` does to its non-player gangs). Confirmed (code); a type-0 brain set dead also gives up the pad (per-player
`+0x1b` = 0). Otherwise A, B and C come from the type's tables. `level99` makes its enemy gangs dead
(`GangBrDead(true)`) while it scripts them, then lifts it for the fights.

### Brain_UpdateGoals: one update {#update-goals}

`Brain_UpdateGoals` (`0x0028fbb0`), in order:

1. Bookkeeping (`0x0028ac18`; for types 1 and 2 `0x0028fff8`; `0x002920e0`; `0x0028bbf0`).
2. **Skip** (only the time is kept) while the human is airborne, its gang is suspended (gang `+0xd4`), or the brain is
   suspended and not held.
3. Every 44 updates, a line-of-sight check on the target.
4. **The reaction goal** (`Brain_UpdateReactionGoal`, [below](#reaction-goals)). When it returns 2 (none active),
   **the goal stack**: `Goal_Process` on the top goal; 2 = done (pop it and process the new top in the same update),
   0 = stop for this update, 1 = run again.
5. **The actions** (`Brain_RunActions`, `0x0028fe28`): start the front action when its delay is over, update it, and
   pop it when it returns 2.
6. The join-the-player's-gang flag.

A goal is **started** (or resumed after the goal above it was popped) only when the action queue is empty
(`Goal_Start` `0x0029ed58`, `Goal_Resume` `0x0029ee30`). `Brain_PushGoal` (`0x0028d758`) suspends the old top
(`0x0029eea0`), clears the actions and pushes; `Brain_PopGoal` (`0x0028d7d8`) ends and frees the top and, when the
stack is then empty, sends the human event `0xd`. `BrFlush` clears both.

### Reaction goals {#reaction-goals}

The brain's `+0x3c` holds a **reaction goal** that has priority over the stack. `Brain_UpdateReactionGoal`
(`0x0028f2b0`) makes one from the human's state word ([Combat](combat.md#state-flags)) when none is active, and
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

The stack's goals `0x1f`, `0x92`, `0x89` and `0x4f`, and the character classes `0x77`, `0x78`, `0x4f`, `0x80`,
`0xb2`, `0x87` and `0x62`, can hold it off. This corrects [Combat](combat.md#block): `+0x3c` is the reaction goal, not
a tactic, and type `0x17` is the knocked-down goal, which is why a downed AI never counters.

Confirmed (runtime), a civilian fighting the player: the knocked-down goal `0x17` (vtable `0x00540210`) appeared on
the update after the state word took `0x80000` and stayed until the update after it cleared (88 updates), then the
fight goal resumed from the stack; the stunned goal `0x18` came with `0x100000`, the tackling `0x13` and grabbing
`0x12` goals with the civilian's own tackle and grab. So the reaction goal lags the state by one update, because the
brains run before the human's state update ([Tasks](tasks.md#humans-update)).

#### The fight reaction goals {#fight-reactions}

Each runs as [the reaction goal](#reaction-goals) while its state lasts, and ends (2, actions cleared) when it
clears. They pick with `Human_CanStartAttack` and queue with the kind's chain delay (`AttackKind_ChainDelay`) unless
said. Confirmed (code) at each address.

- **Grabbing** (`0x12`, the grabber).
    - **Start** (`0x002b5ad0`) rolls a **hand-over flag** `+0x14`: `Random_Int(100)` < the gang's `CfgGang` value 5
      (`+0x6f`) × 10. A FollowAndDefend (`0x35`) or HoldFlag (`0x7d`) goal on the stack clears it.
    - **Process** (`0x002b60c0`):
        1. Not grabbing: done.
        2. Near a train (`Brain_IsNearTrain`): write command 5 every update (inferred: let go).
        3. The target is the held man (`+0xc4`). While actions are queued, wait.
        4. **Presenting him.** This happens in a rear grab when the nearest player is friendly to A, is not busy
           and is within near (3 m), and player 1 exists and is not in state 2. The timer `+0x10` (now + 3000 ms,
           set on the first such update) must not yet have run out.
        5. **A front grab with the flag**, when the victim has two or more slot holders: queue a set-command
           action (command `0x19`, `0x21`; inferred: shove him off to the others) and wait.
        6. **A rear grab with the flag**, when the victim has a number of slot holders other than 1, or while
           presenting. Find the nearest player among the victim's slot holders, else the nearest of his active
           attackers, and turn to face him (`TurnToPointAction`). Wait. (Holding him up for a friend to hit.)
        7. Otherwise pick a kind and act on it:
            - 45: wait;
            - 24 (a strike in the grab): 40 % of the time two kind-24 attacks, the second after the chain delay;
            - 25 or 29 (the throws): the stick angle from `Grabbing_PickMove`;
            - 26-28 (power strikes): no angle.
    - **Grabbing_PickMove** (`0x002b5b98`) picks a direction *d* (0 left, 1 ahead, 2 right, 3 behind the grabber,
      angle = heading + *d* × 90° − 90°). The first rule that gives one wins:
        1. With a HoldFlag goal (`0x7d`), away from its flag: flag in A's sector 0, 1 or 7 → 3; 2 → 2; 3-5 → 1;
           6 → 0.
        2. For a class whose record byte `+0x39` is 1, a random choice among the walls next to them: the victim's
           sector 4 → 1, the victim's 6 → 0, the victim's 2 → 2, A's sector 4 → 3. Failing that, a random choice
           among the sides where a human not friendly to A stands next to the victim. (Into a wall or into his
           mates.)
        3. With the gang's tactic of type 2 (Defend), away from the defended human, as in 1.
        4. Otherwise `Random_Int(3)`: left, ahead or right, never behind.
- **Mounting** (`0x13`, the tackler on top, `0x002b6638`).
    1. Not tackling: done.
    2. Near a train: command 5.
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
    only from this goal (inferred: a get-up attack). This corrects [AI goals](ai-goals.md#goal-reactions)' "against
    the target".

#### Who backs off (FightBackOff) {#fight-back-off-pusher}

The only caller of `FightBackOffGoal_Init` (`0x002b7140`) is `GuardSubTactic_Order` (`0x003221e8`), one of the Attack
tactic's coordinated [sub-tactics](ai-code.md#t2-sub-tactics). Confirmed (code). With its back-off option (`+0x17`)
the order does these things:

- The **gang leader** gets FightBackOff with no time limit, after `Brain_PopToGoalBase` / `Brain_MarkGoalBase`. He
  keeps 1.75 × far plus 1-2 m from his target, faces him and taunts ([FightBackOff](ai-goals.md#goal-fight-back-off)).
- Every other non-player member joins the leader's formation (set 3, min(2 × others, 9) slots, shape 3.0) and gets
  FollowAndDefend (3.0) on him. Without the option the members also get the attack table `0x00511438` as their
  override.

So FightBackOff is the leader hanging back while his men guard him. No script pushes it.

### Events {#events}

`Brain_OnEvent` (`0x0028f928`) runs the perception's handler (`0x00299100` on `+0xf8`), then the reaction goal's
event slot if one is active (`0x0028f988`), then handler C. Event `0x10` is the **attack warning**
([below](#block)). Handler C is where a brain answers the world: the civilian's (`0x002ffb30`) takes event `0x14`
(violence nearby) and may start a fight through `Brain_Fight`; the type event handlers are among `Brain_Fight`'s
callers.

### Brain types: think and event handlers {#brain-type-handlers}

What each brain type does at its think (one character step in five, [The update](#update)) and with each event
([Events](#events)). Confirmed (code) at the addresses cited unless marked; "every *n* thinks" counts the think
counter brain `+0x38`, so it is every 5 × *n* steps. `R` is the sight range (`Brain_GetSightRange` `0x0028bf00`,
[The enemy scan](#enemy-scan)). Goal ids are on [AI goal types](../references/goal-types.md).

`Human_CanSeeHuman` and its squared near and shadow distances are under [Sight](#sight).

#### The event record {#event-record}

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
| `0xb` | a new enemy, sent by [the enemy scan](#enemy-scan) | shared, Warrior; player and shopkeeper drop it |
| `0xc` | (dropped by the player, the Warriors and cops) | — |
| `0xd` | the goal stack became empty ([Brain_UpdateGoals](#update-goals)) | goals and tactics |
| `0xf` | "use this flag" (a free tag spot nearby; inferred) | sent by the gang think to itself |
| `0x10` | the [attack warning](#block) | every type |
| `0x14` | violence seen (`+0x00` the aggressor, `+0x24` the victim; `+0x04` 0 for a fight between humans) | cop, gang, civilian, Warrior, shopkeeper; the player drops it |
| `0x15` | a fight lull: his attackers answer with a taunt (inferred) | shared |
| `0x17` | a crime seen (`+0x04` the crime object, which gives its position) | cop, gang, civilian |
| `0x18` | danger near (a fire, a car; `Brain_OnDangerNear` `0x00291728`) | Warrior |

Hearing: an event reaches a brain only within its hearing radius (`Brain_IsInHearRange` `0x002935d8`: brain `+0x134`,
50 m when the brain is made; `+0x138`, 20 m, for event 20). Confirmed (code) at `0x0028a570` for the defaults.

#### The shared event handler {#shared-events}

`Brain_DefaultOnEvent` (`0x00292d80`) is every type's fallback (each type handler passes the ids it does not take):

1. **1** with `+0x11` set: a help call to his gang within 20 m (`Gang_BroadcastHelpCall` `0x00293640`) naming the
   hitter.
2. **2**: target cleared, actions, goals and the enemy list cleared.
3. **`0xb`** (a new enemy), unless that enemy is throwing at him: a help call within 20 m flagged "new enemy"; an
   Investigate goal (`0x5e`) on top is popped; when the top is then Chase (`0xc`), its actions are cleared.
4. **`0x10`** (the attack warning):
    1. The near radius is 3 m (9), or 0 when the attacker is hidden in shadow, throwing, and this brain's `+0x2d4` is
       clear. Unless `Human_CanSeeHuman(+0x12c, R, near, 2 m, self, attacker)` and the attacker is not friendly,
       nothing more.
    2. Brain `+0x200` + 1 (the [block](#block)'s trigger); unless `Human_ReactToThreat` already holds, the two gangs
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

What sends event `0x15` is not traced.

#### Cops (type 1) {#think-cop}

**Think** (`CopBrain_Think` `0x00300678`):

1. `Brain_MaybeSpotPlayers` (`0x0028fae8`).
2. The radar blip: mode 2 when a player (player 2 too in co-op) is in his enemy list, 3 while his top goal is
   CallForBackup (`0x79`), else 0; kept in human `+0x19e` and brain `+0x28c`.
3. Threat response set, no fight goal, no Chase goal (`0xc`) and a non-empty enemy list: brain `+0x28f` = 1 and
   `Brain_PushFightGoal(brain, −1)` (no time limit).
4. **Pursuit counter** brain `+0x148` (s16), when he has a target: + the target's gait when the target moves (gait ≠
   0) and either holds an object or moves along the cop's facing (velocity · facing ≥ 0), up to 36; else − 2, down to
   0. Its reader is not traced (inferred: how hard the target is running from him).
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
offender's position, offender, victim, flag)` (`0x002c4060`, goal `0x78`) on top. [Crimes and the police](#crimes)
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

#### Gang soldiers (type 2) {#think-gang}

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
   with the flag (inferred: go and tag it; the reader is the goal on top).

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

#### Civilians (type 4) {#think-civilian}

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

#### Dealers (type 5) {#think-dealer}

**Think** (`DealerBrain_Think` `0x00302cd8`): `Brain_MaybeSpotPlayers`; threat response set and no fight goal →
threat response 0; every 5 thinks, while fighting (threat response set), wanting a weapon (`+0x265`), empty-handed
and not blocked: the nearest pickable object within 2 m (mask `0x30000`) → actions cleared, a pick-up action (kind
`0x21`).

**Events** (`DealerBrain_OnEvent` `0x00303178`): **0** under Dealer (`0x80`) → `DealerGoal_OnBuy`
([Buying](#dealer-buy)); **1** with `+0x11` = 1 and **`0x10`** → `DealerBrain_OnHit` (`0x00302ff0`,
[Buying](#dealer-buy)); **`0xb`** and **`0x14`** dropped; others the shared handler. Confirmed (code).

#### Shopkeepers (type 6) {#think-shopkeeper}

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
  (`+0x38` × 0.25, at least 16; compared with a squared distance, inferred), goals above the Shopkeeper goal are
  popped.
- **`0xb`**: dropped. **`0x10`**: disturbance (the attacker, reason 1, call help). **`0x14`**: disturbance (the side
  `Brain_PickSideInFight` picks, reason 2, no call).
- Others: the shared handler.

#### The Warriors (type 3) {#think-warrior}

**Think** (`WarriorBrain_Think` `0x003052f0`):

1. `Brain_MaybeSpotPlayers`; threat response set, no fight goal and enemies → `Brain_PushFightGoal(−1)`; the pursuit
   counter as the cop's.
2. **Pick-ups**, every 6 thinks: [The Warriors' pick-ups](#warrior-pickups). The search is 1.5 m with mask
   `0x10000`; the mask is `0x30000` (and the search's last flag 1) when his gang's leader is neither grabbing from
   behind nor tagging, the gang's `+0x32` is 1, a member of the gang is under attack and his own enemy list is empty.
   The object must also pass `WorldObject_CanBePickedBy`.
3. Outside co-op (game state `+0x158` = 0): holding a weapon of kind 4 or 6 with an empty enemy list and no steal
   tactic (`0x26`) → he drops it.
4. Every 8 thinks: holding a world object whose type byte `+0x64` is `0x9a` while his human `+0x5b7` is clear → drop
   it (inferred: loot only inside a store).
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
   the swap itself is the event-0 prompt (`WarriorBrain_OnPrompt` `0x00306040`).
8. Timer `+0x2dc`: once past, cleared with byte `+0x2e4`.
9. **Getting out of the way**, every 12 thinks, when he is out of the fight stance, not in shadow, not busy or
   blocked, standing, with no target, no attackers, and a top goal other than GetItem (`0x2c`) and PlaySpecialIdle
   (`0x23`):
    - when a ray toward a point ahead of him (his near range, 3 m, inferred) is blocked and a player is within 30 m:
      actions cleared (unless the front one is of class 8) and a turn (toward the player, inferred);
    - the nearest member of his gang within 1 m who is free, has neither Scatter (`0x37`) nor HoldPosition (`0x36`)
      and no actions is pushed aside (`Brain_PushAside`).

**Events** (`WarriorBrain_OnEvent` `0x003063b0`): **0** → `WarriorBrain_OnPrompt`; **1** → first
`WarriorBrain_OnHitByChief` (`0x00306190`); otherwise, with `+0x11` set and a non-friendly hitter: gangs made enemies
unless already threats and, unless one of the busy states `0x1f80974000` holds, `Brain_Fight(hitter, −1, 1)`;
**`0xb`**: the new enemy noted (brain `+0x27c`, flag `+0x280` = 1), consumed; **`0xc`** consumed; **`0x14`** with
`+0x04` = 0 → `Brain_OnViolenceSeen`; **`0x18`** → `Brain_OnDangerNear`; then (except for consumed ids) the shared
handler.

#### The player (type 0) {#think-player}

**Update** (`PlayerBrain_Update` `0x003035d8`, every step; it pushes no goals of its own on him):

1. `Brain_UpdateAlertness`; a target that is no longer up is dropped.
2. Every 300 updates, `+0x152` = how many AI humans of all 32 gangs hold him in their enemy lists.
3. Every 2 s (brain `+0x160`), `PlayerBrain_ScanNearEnemies(7.5 m, target)` (`0x0028af88`).
4. `WarChief_AutoCommand` ([Automatic commands](#warrior-auto-commands)) every update; every 40 updates
   `WarChief_CheckCrewInRange`.
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

### Starting a fight and choosing the target {#targets}

- `GoalFight(human, target, …)` is `Brain_StartFight` (`0x002b2b90`): clear the actions, then `Brain_Fight`.
- **`Brain_Fight`** (`0x0028d2e8`) needs a threat response (`+0x21c` ≠ 0). It adds the target to the enemy list
  (`Brain_AddEnemy`, `0x0028d538`, which tells the gang's tactic with event `0xb`), takes it as the target
  (`Brain_SetTarget`, `0x0028cfe0`) and calls `Brain_PushFightGoal` (`0x0028d190`) with its duration argument.
- **`Brain_PushFightGoal(brain, duration)`** does nothing when the gang has a tactic (gang `+0x40`), the human is
  down or dead (`0x00223b70`, `0x00227dd8`), or a type-3 brain holds goal 9 (AttackTarget). Otherwise it **pops**
  any goal 8 (Melee) and `0x41` (FindEnemy) already on the stack, with every goal above them, then **pushes three**:
  `Goal_Melee` (`0x002add08`, the `GoalMelee` binding's worker) pushes FindEnemy (`0x41`) and then Melee (8), both
  given the duration, and sets the gang's alert state to 1 when the gang has no tactic; then the fight goal (`0xf`,
  `FightGoal_Init` with the duration). The stack ends **FindEnemy, Melee, Fight** (top). So both earlier
  statements were half right: it pops and then pushes. Confirmed (code) at `0x0028d190` and `Goal_Melee`; the
  order confirmed (runtime), [in lesson 12](#level99-fight).
- **`Brain_SetTarget`** releases the old target's attack slot and claims one on the new target
  (`Brain_ClaimAttackSlot`, `0x0028df30`): the target's list `+0x1a4` holds at most `+0x1e4` attackers, and a closer
  attacker takes the slot of the farthest.
- **Re-targeting**: once a second the fight goal takes the human nearest him in his sector record (`Brain_GetSectors`
  `0x0028fe90`, refreshed when older than 1 s; its first word read as the nearest human, inferred) as the target
  (`Brain_SetTarget`) when that human is not already the target, is a threat (`Brain_IsThreat` `0x00290138`,
  [The enemy scan](#enemy-scan)) and no GrabTarget goal (`0x1f`) is on the stack. Confirmed (code) at `0x002b3c30`.

### The fight goal {#fight}

The fight goal attacks a target already within reach; [Closing on the target](#fight-approach) brings it there.

**The deadline**: `FightGoal_Init` sets `+0x20` = now + a random 750-1000 ms and `+0x24` = now + its duration
argument. Once `+0x24` has passed, the goal ends unless the fighter is still one of the target's active
attackers (`0x002911a8`), the target's byte `+0x11f` is set (`0x00290ea8`) and the fighter has no goal `0x35`;
a class-13 fighter without `+0x29` ends at once ([GoalRiot](#riot) for the 8 s case). `GoalFight` passes −1, so
its deadline (now − 1, unsigned) is already past and these checks run from the first update. This corrects the
earlier reading of these checks as attack pacing.

#### The fight goal in order {#fight-order}

`FightGoal_Process` (`0x002b3ab0`), every update. Confirmed (code); this corrects an earlier reading that put the
block try before the range checks and left out the tackle try.

1. The update counter `+0x18` is raised by 1.
2. **Done** when the target fails `Brain_ValidateEnemy`, or when it holds no attack slot for A.
3. **The tackle try** (`FightGoal_TryTackle`, [below](#try-tackle)). When it queued a tackle, wait.
4. **Done** when the target is beyond 1.1 × far (5.5 m), unless A holds a throwable object or one of kind 4 or 6.
5. Every 30 updates, **done** when there is no walkable straight line to the target (`0x002221b0`).
6. **Re-target** at most once a second (`+0x14`), from A's sector record ([re-targeting](#targets)).
7. **The block try** (`Goal_TryBlock`). When it pushed a block, A gives up his active-attacker place on T and waits.
8. While actions are queued, wait. When the last attack ended (`+0x28`), give up the active-attacker place.
9. **The deadline** (`+0x24`, [The durations](#fight-durations)).
10. When no kind is chosen (`+0x10` = 45), **pick** one (`Brain_PickAttack` with `Human_CanUseAttackKind`).
11. **The stance.** Drop the fight stance for kinds 19 and 20, and always for a class whose `+0x11b` is 6 or 7.
    Otherwise take the stance.
12. **May A attack now?** `Brain_CheckAttack(B, kind, T holds an object)` (`0x002906b8`,
    [below](#check-attack)).
    - Any answer but 1: [reposition](#fight-reposition) and wait.
    - Answer 1 and the target is beyond the kind's reach (`Attack_ReachSquared` `0x00230d00`): queue a
      move-to-human action and wait.
        - It holds between 2 × T's capsule radius and max(reach, that + 0.1 m).
        - Its limit is 2000 ms, or 1000 ms for a class-13 fighter or with a FollowAndDefend goal (`0x35`).
    - Answer 1 and in reach:
        1. **The grab and snap try** ([below](#try-grab)). When it returns 1, wait. It may change the kind and set
           the stick angle.
        2. When `Human_CanStartAttack` refuses the kind: for kind 22, also queue a move to between 0.8 × near and
           0.8 × near + 1 m (1000 ms). Then pick again with `Human_CanStartAttack` and wait.
        3. Otherwise **queue the attack** (`Brain_QueueAttack(angle, B, T, kind, 0)`) and clear the kind to 45.
           Note that A holds an active-attacker place (`+0x28` = 1).
        4. A class whose `+0x11b` is 13, 6 or 7, or a dealer (type 5), may say line `0x11` (a taunt) after it: 20 %,
           when gestures are allowed (`Ambient_MayGesture`).

`Brain_QueueAttack`'s first argument is the **stick angle** in radians, −1 for none. It is written with the command
([The attack action](#attack-action)).

#### Picking the attack kind {#pick-attack}

`Brain_PickAttack(B, T, filter)` (`0x0028e708`) returns one of the 45 [attack kinds](#attack-kinds), or 45 for none.
`filter` is a member-function pointer called as `filter(A, T, kind)`. It is passed as one 64-bit value in the old GCC
layout, `s16` delta, `s16` index (−1 for a non-virtual function; otherwise a vtable slot), then the function address; so
the constants the decompiler shows, `0x002240e8ffff0000` and `0x00224778ffff0000` (read at `0x00563ff8` by
`0x002b5768`), are delta 0, index −1 and the function, not a kind mask. `Brain_PickAttack` applies the delta to A and
calls through the vtable when the index is not negative (`0x0028e708`). Confirmed (code). Two filters are used:

- `Human_CanUseAttackKind` (`0x002240e8`) by the fight goal's first pick, by FightGoal Resume and by EngageEnemy;
- `Human_CanStartAttack` (`0x00224778`) by every other caller. It is the same per-kind test plus range, and the
  fight goal falls back to it when a kind cannot start.

Confirmed (code) at `0x0028e708` unless marked.

1. **Set-up.**
    - `n` = the number of attack slots taken on T (T's brain list `+0x1a4`); `m` = T's maximum (`+0x1e4`, 4 by
      default).
    - `c` = the number of slots taken on A himself (B `+0x1a4`); `M` = B `+0x1e4`.
    - `running` = A at gait 4 or 5 with no held flags (`0x00223a60`, `0x00223a98`).
    - `armed` = A holds an object.
    - `rear` = T is grabbed from the rear (state `0x20`).
    - **The pattern read**: when T is pad-controlled, T's target is A, and A's class threshold
      `t = (16 − class +0x37) / 16` (`0x002236c8`) is above 0:
        - `f` = the larger of T's two pattern bytes (`+0x5d0`, `+0x5d1`) × 0.05 (`0x00418398`);
        - `pattern` = (`t` ≤ `f`).
2. **T cannot be approached.** This is when `Brain_CanBeChased` on T's brain (`0x0028abc0`) is false, or B
   `+0x2d3` is clear. Return kind 0 (`X1`) when A holds a throwable object or one of kind 4 or 6; otherwise
   return 45.
3. **The armed bonus.** `bonus` = 100, or 0 for a class whose `+0x11b` is 9, 10 or 13.
4. **Candidates.** For every kind `k` from 0 to 44 that passes `filter`, these rules apply in order:
    1. **Running.** While `running`, only kinds 0, 19, 20 and 21 are candidates. When not running, 19 and 20
       (the charge and the dive) are not candidates. Kinds 0 and 21 are always allowed.
       (`AttackKind_IsCharge` `0x00229b60` is true for 0 and 19-21.)
    2. **The hurt rule.** Kinds 31-34 and 42-44 (the moves made while grabbed or on the ground) are left out when
       A is hurt (`0x00222ff8`) and B is attackable (`Brain_IsAttackableBy(B, none)`).
    3. **The weight.** `w` = the weight: B `+0x298[k]`, or the override table at B `+0x208`.
    4. **Crowded.** When `c` > 1:
        - kinds 10, 25 and 36: `w` += `w` × `c` / `M` (the snap, the throw from a grab);
        - kinds 14, 21 and 24: `w` ×= 1 − `c` / `M` (the tackles, a strike in a grab).
    5. **A busy target.** For kind 22 (the grab) when `n` > 1: `w` += `w` × `n` / `m`.
    6. **Armed.** When `armed`, kinds 0-9 with `w` ≠ 0 get `w` += `bonus`.
    7. **A rear-grabbed target.** When `rear`, kind 22 with `w` ≠ 0 gets `w` += 200.
    8. **The pattern.** When `pattern`, kinds 6-9 (the three-hit enders) get `w` += `w` × `f`.

    Each scaled weight is truncated to an integer. The kind and its weight are appended, and `w` is added to the
    total.
5. **The draw.** `r` = `Random_Int(total)`. The answer is the first candidate whose running sum of weights exceeds
   `r`. With no candidates, or a total of 0, the answer is 45.

So a crowded AI favours the snap and the throw and gives up tackles. Several attackers on one target make grabs
likelier. A weapon in hand makes the plain strikes likelier by a flat 100. A rear-grabbed target, held by a gang
mate, invites a grab (+200).

**The per-kind test** `Human_CanUseAttackKind(A, T, k)` (`0x002240e8`), confirmed (code). "A free" below means A's
state word has none of `0x7bf9e9f7ff0`.

| Kinds | Allowed when |
| --- | --- |
| 0, 1 | A holds a world object whose type byte `+0x87` is 1-3: A free. Otherwise as 2-9. |
| 2-9, 11 | A free, and T's state has none of `0xe3000` |
| 10 (snap) | A free, and A holds an attack slot on the human in sector 2, 3, 4, 5 or 6 of his [sector record](#neighbour-sectors) (flag bit 1, within 1.5 m). That is a human to his side or behind. |
| 12, 13 (grounded strikes) | T ≠ A. Either T is high or busy (`Human_IsHighOrBusy` `0x00225200`), or T lacks state `0x2000` and has held flag `0x400000`. Then A free. |
| 14 | T is A (1), or T is knocked down and A is free |
| 15 | A free; then A holds spray paint (1), or A is not pad-controlled |
| 16-18 | A free and T has none of `0x40100f0800`; for class `0x77` only without an object of kind 4 or 6 in hand |
| 19, 20 (charge, dive) | A free, T not down (`Human_IsNotDown` `0x00225500`), A at run speed |
| 21 (tackle) | A free, T's human flag `+0xe0` bit `0x40` clear, T free, T without held flag `0x40` |
| 22 (grab) | A free, A's power meter (record `+0x148`) ≥ his maximum power / 5, T's `+0xe0` bit `0x40` clear, and T's state none of `0x7bfdc8f7fd0` |
| 23 | A holds a throwable object |
| 24-30 (moves in a grab) | A grabbing (`0xc0`), none of `0x7bf9e9f7f00`, and the human A holds (`+0xc4`) is T |
| 31, 33, 34 | A grabbed (`0x30`), none of `0x7bf9e9f7f00`, and T holds A (T `+0xc4` = A) |
| 32 | A grabbed from the rear (`0x20`), none of `0x7bf9e9f7f00`, and T `+0xc4` ≠ A |
| 35-40 (moves while mounted) | A tackling (`0x400`), none of `0x7bf9e9f73f0`, A `+0xc4` = T |
| 41 | the same, and A carries cuffs |
| 42-44 | A knocked down (`0x80000`) with the Grounded reaction goal (`0x17`) running: allowed. Otherwise A tackled (`0x800`), none of `0x7bf9e9f73f0`, and T `+0xc4` = A. |
| others | never |

`Human_CanStartAttack(A, T, k)` (`0x00224778`) first refuses in these cases:

- T has held flags `0xc08200`, unless T has state `0x2000` and A has state `0x1000`;
- A has held flags `0x5c7eee0`;
- A has damage pending (record `+0x118`);
- the distance is beyond the kind's far reach.

It then runs the same per-kind state tests. Confirmed (code) for the guard and the reach; the per-kind cases were
read for 10-15 only and are inferred to mirror the table for the rest.

#### May A attack now: Brain_CheckAttack {#check-attack}

`Brain_CheckAttack(B, kind, armed target)` (`0x002906b8`) returns 1 for "go". Every other value is a reason to wait,
which [the reposition](#fight-reposition) reads. The tests run in this order. Confirmed (code); the meanings in
brackets are inferred.

| Result | When |
| --- | --- |
| 2 | B's own next-attack time (`+0x1e8`) has not come |
| 0 | the kind is 45 |
| 3 | T may not be attacked by A (`Brain_IsAttackableBy`) |
| 1 | a charge kind (0, 19-21) while A is out of the fight stance (a charge needs no place) |
| 4 | The target holds an object, A's brain is not type 3, the game mode word is not 60-64, and neither T nor T's target is busy. Then, when A is empty-handed, his turn boost `+0x0b` < 1, and A and T face each other (facing dot < 0), and A is his gang's leader or within 15 m of him. (Hold back from an armed man near the leader.) |
| 8 | Only for an empty-handed A who is not yet one of T's active attackers (and not class 13 or 6). Another human in T's slot list, not A, not busy, without held flag `0x300000` and holding a heavy object (`0x00224080`: anim set not 1 or 5), is ready to attack (his cooldown passed). (Let him go first.) |
| 5 | The police have him. This applies when A is not a cop and T's target is not A. Either T's gang is wanted (`+0x5e8`) and a cop holds a slot on T, or T's target is a cop. For a cop A, it applies when T's target threatens him (`0x00290138`). Also for a cop when T is tackled or has state `0x10000000` or `0x10000000000` and is held by someone other than A. |
| 6 | A holds a throwable object or one of kind 4 or 6, and a gang mate in a 40° cone ahead (`Gang_NearestInCone` `0x0029df78`) is tackling or is nearer than T. A shuffle action is queued. (Do not throw through a friend.) |
| 7 | A is a Warrior (type 3), T is class 13 and not targeting A, A is empty-handed and not yet an active attacker, and a player in T's slot list who is busy tackling, throwing or front-grabbing stands within 0.75 × near of T. (Do not hit the boss while a player has him.) |
| 1 | A takes, or already has, one of T's **active-attacker places** (`Brain_ClaimActiveAttacker`, [below](#attack-places)) |
| 1 / 0 | kind 22 only: 1 when A is the near human in sector 3, 4 or 5 of T's record (behind him), else 0 |
| 0 | otherwise (no place free) |

#### Attack slots, active places and spacing {#attack-places}

A target limits his attackers twice. Confirmed (code) at the addresses cited.

- **Attack slots** (T brain `+0x1a4`, at most `+0x1e4`, 4 by default from `Brain_Init` `0x0028a570`) are taken when an
  attacker targets him (`Brain_ClaimAttackSlot` `0x0028df30`, from `Brain_SetTarget`). A full list gives the slot of
  the farthest holder to a nearer newcomer, or to one forced in, and clears the holder's target.
- **Active-attacker places** (T brain `+0x1f0`, four handles) are taken by `Brain_ClaimActiveAttacker`
  (`0x00291008`) only when an attack is about to be queued. Only the first `s` places are used:
    - `s` is T's **spacing byte** `+0x14a`;
    - while T is down, out of the fight or arrested (state any of `0xe0000`), `s` is `+0x14b`.
- **The spacing bytes** start at 1 (`0x0028a570`). Each slot claim raises them:
    - `+0x14a` to at least the attacker's gang's `CfgGang` value 2 (gang record byte `+0x6c`);
    - `+0x14b` to at least its value 3 (`+0x6d`);
    - a Warrior target (type 3) keeps `+0x14a` at 1.

  Both return to 1 when T's slot list empties (`Brain_SetTarget` `0x0028cfe0`).

  So `CfgGang`'s second and third values are **how many of that gang may swing at one standing (or one downed) man
  at once**.
- **A claim succeeds** in three cases:
    - A already has a place;
    - a free place exists and either T's `+0x1ec` has passed or someone already holds a place;
    - A is T's own target and `+0x1ec` has passed, in which case A displaces the first holder.

  A holder is released when his fight goal next waits (`+0x28`, `Brain_ReleaseActiveAttacker` `0x00291178`), when he
  blocks, and when the fight goal is suspended.
- **When T may be attacked again** (`+0x1ec`, `Brain_SetAttackableTime` `0x00290e78`): the attack action's Start, when
  neither human is busy, adds `t` to max(`+0x1ec`, now).
    - `t` = `Attack_GetNextAttackDelay(A, kind)` (`0x00231590`) × 1.0 (`0x00510ad0`) / `s`, rounded. When `s` is 0,
      `t` is used unscaled.
    - `Attack_GetNextAttackDelay` is the **playing time** in ms of the kind's anim on A:
        - 11-21 for kinds 0-9 (kind 0: 11, or 194 against a downed target);
        - 25 for 10, 21 for 11, 193 / 194 for 12 / 13, 237 for 14, 664 for 15;
        - the longer of 653 / 655 for 16, and of 657 / 659 for 17;
        - 0 / 1 for 19 / 20;
        - 3 + 5 for 21, 70 + 72 for 22, 466 + 467 for 23;
        - the throw clips for 25-29; 118 for 30; the grab-escape clips for 31-34; 225 for 35-39, 248 for 40, 252 for
          41, 246 for 43, 242 for 44;
        - the object set's clips when A holds a bat or bottle set (sets 2, 3), with kinds 0, 1 and 5;
        - 100 ms for anything else, and for kinds 0 and 1 against a downed target.

  So the gap a target gets between attacks is one swing's length shared among the `s` attackers allowed.

#### The reposition {#fight-reposition}

When `Brain_CheckAttack` says wait, the fight goal calls `FightGoal_Reposition(d², goal, T, reason)` (`0x002b2fc8`). It
queues a fidget now and then, and always a move-to-human action that **holds A in a ring** round T. Confirmed (code).

1. **A is close-in** when all of these hold:
    - T is empty-handed, or both are armed and A is one of fewer than two slot holders;
    - the reason is not 7, 8 or 3;
    - T's own target is A, or T has no target and A is first in T's slot list.
2. **A tactic ring** applies when A's gang has a tactic of type 2 (Defend) or `0xd`.
3. **The ring's radii:**
    - inner = 2 × T's capsule radius when close-in or in a tactic ring, else (near + far) / 2 = **4 m**;
    - outer = 0.95 × far = **4.75 m**, and never less than inner + 0.1 m;
    - for reason 5 (the police have him), a non-cop uses inner = 3 × far = 15 m and outer = 16 m;
    - the limit is 2000 ms, or 6000 ms for reason 5.
4. **A taunt**, once per random 1000-2000 ms (`+0x20`). All of these must hold:
    - T is at least 2.75 m away (a quarter of (1.1 × far)²);
    - fewer than two humans hold slots on A;
    - A is not close-in, not in a tactic ring, and T is empty-handed;
    - gestures are allowed.

   For reason 7 it happens only 11 % of the time. It is a fidget action on anim `0x25b` with speech line `0x11`. A
   Warrior uses line `0xc3` instead 31 % of the time, when the game flag `0x56e1` is set.
5. **The move-to-human action** (inner, outer, the limit above).

So waiting attackers circle in a band of 4-4.75 m (5 m far range), and the one T is fighting stays close. The
"shuffle" is this band-keeping move, not a dedicated sidestep.

#### The tackle try {#try-tackle}

`FightGoal_TryTackle(goal, T)` (`0x002b2e28`), step 3 of the fight goal. Confirmed (code).

- `k` = A's gang's `CfgGang` value 7 (gang record byte `+0x70`).
    - When `k` is 0, or B's tackle weight (kind 21) is 0: clear B's **tackle meter** (brain `+0x148`, s16) and do
      nothing.
    - When the meter ≤ (7 − `k`) × 6: do nothing. With `k` = 1 the threshold is 36, the meter's cap, so it never
      fires.
- The kind is 21 (the tackle). A cop (type 1) uses kind 0 (`X1`) instead 75 % of the time.
- It goes ahead only when all of these hold:
    - `Human_CanStartAttack` passes (in reach);
    - T may be attacked by A;
    - T's gang is not wanted with a cop already on him;
    - A takes an active-attacker place (not forced).

  It then queues the attack (no angle), clears the meter, notes the place, and the goal waits.
- **The tackle meter** is kept by the think handlers of cops (`0x00300908`), gang soldiers (`0x00304840`) and the
  Warriors (`0x0030543c`). These run once per 0.2 s.
    - It rises by T's gait (`+0x1a8`, capped at 36) while T moves and either holds a weapon (`0x00224060`) or moves
      away (T's velocity, `0x003a1f00`, has a non-negative dot product with A's facing).
    - Otherwise it falls by 2 (not below 0).

  So a target who keeps running off, or who swings a weapon, is tackled sooner, and a high `k` makes it faster.

#### The grab and snap try {#try-grab}

`FightGoal_TryGrab(d², goal, T, &angle)` (`0x002b3360`) runs once A may attack and is in reach. It returns 1 to
make the fight goal wait. Confirmed (code). Sectors are the eight 45° sectors of [the sector record](#neighbour-sectors)
(sector *k* at record `+8k`, flags at `+8k + 4`). The stick angle of a sector *k* is A's heading − *k* × 45°.

- The angle starts at −1.
- **Kind 10 (the snap)**: the first of A's sectors 4, 5, 3, 6, 2 whose near human (flag bit 1) has A's attack slot
  gives the stick angle: heading + π for 3-5, heading + π/2 for 6, heading − π/2 for 2. The kind becomes 1 (square),
  so the press is square with the stick to that side or back. This is how the player's snap is made
  ([Combat](combat.md)).
- **Kind 22 (the grab)**: when A is in T's sector 2 or 6 (at his side) and T is grabbed from the rear, A moves in
  (move-to-human, 2 × T's radius to the grab reach, 1500 ms) and returns 1. At his side otherwise, the grab becomes
  kind 0 (`X1`).
- **Kinds 12, 13 (strikes on a downed man)**: when T is tackled, or has state `0x2000` or `0x10000000000`, and a
  friend of A holds him (T `+0xc4`), A moves in to 0.6 × near to 0.6 × near + 1 m (1500 ms) and returns 1.
- **Other kinds: a rear grab.** All of these must hold:
    - A is empty-handed;
    - A's gang's `CfgGang` value 8 (`+0x71`) `g` is above 0, and B's grab weight is above 0;
    - A is the near human in T's sector 4 (behind him);
    - `Random_Int(100)` < `g` × 25;
    - the grab can start.

  The kind then becomes 22.
- **Then, for all kinds:** when T is grabbed from the rear, the kind is not 22, and the grab is usable
  (`Human_CanUseAttackKind`), the kind becomes 22 and the try returns 1. "Usable" needs one of these:
    - B's grab weight is above 0;
    - B has goal `0x88`;
    - the game word `+0x158` is set.

  A man held from behind by a mate gets grabbed in front.
- **A heavy object in hand** (`0x00224080`):
    - Take the nearest gang mate who is not T and not busy.
    - Kinds 1, 3 and 5-9 with that mate within near (3 m): the kind becomes 0 and the try returns 1.
    - Kinds 0, 2 and 4: the try returns 0 when any of these hold:
        - the mate is tackled, or has state `0x10000000` or `0x10000000000`;
        - T is beyond far;
        - the mate is both farther than T and farther than 1.6 m.
    - Otherwise:
        - against a rear-grabbed T, the kind is picked again with `Human_CanStartAttack`;
        - against T grabbing from the rear or mugging, the try returns 0;
        - else A steps in to the kind's reach (1500 ms).

      The try then returns 1. (Inferred: a swing with a heavy object is shortened or held back when a mate stands
      close.)

The fight goal's own time limit (goal `+0x08`) is always −1 (`FightGoal_Init`); its duration goes only to `+0x24`.

### Closing on the target {#fight-approach}

Below the fight goal sit the Melee goal (8) and the FindEnemy goal (`0x41`) that `Brain_PushFightGoal` pushed
([Starting a fight](#targets)). When the fight goal ends, most often at once because the target is beyond 1.1 × the
far range, Melee runs; it sends the fighter in with an **EngageEnemy** goal (`0xb`) and pushes a fresh fight goal
once he is in range. Confirmed (code) at the addresses cited; the sequence confirmed (runtime) below.

#### Melee ranges {#melee-range}

Every brain starts with near `+0x13c` and far `+0x140` from two globals (`0x0028a650`, `0x0028a664`, reading
`0x00510ab4` / `0x00510ab8`), which [`CfgSetMeleeRange`](../references/bindings/config.md#cfgsetmeleerange) sets
from `config_preload2.lua` to **3 and 5 m** (1 and 4 in the executable's image, before the config runs). Nothing per
class or power class changes them: the only other writers are `BrSetMeleeRange` (no script calls it) and
`GoalBossDiego` (`0x002a1b50`, 1 and 2 m). So the far range is **5 m** for the sparring Warriors (power class 40)
and every other AI. Confirmed (code) at the writes above; confirmed (runtime), PCSX2 2.9.94, read from the owner's
save states 1 and 6 (all 19 brains 3.0 / 5.0) and in the lesson-12 trace below.

#### The Melee goal {#melee-goal}

`Goal_Melee` builds it with `0x002ade10(goal, brain, duration, 4000)`: `+0x08` the time limit (the duration),
`+0x10` the target's handle (−1 at first), `+0x14` 1.1 × far, `+0x18` **4000**, the duration of every fight goal it
pushes, `+0x1c` a step counter. End (`0x002ade60`) sets brain `+0x2d3` and calls `0x00226f70`. Brain `+0x2d3` ("may
approach", 1 when the brain is made) is cleared below when the target cannot be chased (`0x0028abc0`: the target
brain's [reachable](../references/bindings/character.md#humarkreachable) byte `+0x11e` clear, a train near it, or on
fire), when the last move failed (`+0x284`), or when the target is outside the gang's turf (`0x0028ff58`).

**Process** (`0x002aebf8`), when the goal's time limit has not passed:

1. While actions are queued, wait.
2. **Choose the target.** With threat response ≠ 0, the best-scoring valid enemy in the enemy list `+0x164`
   (`Brain_PickBestEnemy` `0x0029f230`, [the score](#enemy-score)), set as the target. With threat response 0, only
   the current target, kept while valid and holding an attack slot on it (else the target is cleared and the goal is done).
3. **No target**: done when the enemy list is empty, or when its nearest enemy is hidden in shadow (`+0x2d4`) or
   fails `0x002225d8`; otherwise push a [Spectate goal](#spectate) (`0x10`) and wait.
4. **With a target** (`0x002ae2e8`), with `d` the distance and `R` = 1.1 × far: a new target resets the counter and
   sets `+0x2d3`.
    - **Armed** (holding a weapon, `0x00223ea0` or `0x00224000`), or **unarmed, `+0x2d3` set and `d` ≤ `R`**: when
      the line of sight (`0x00222288`, eye heights 1.7 m) holds, either walk to him in the fight stance (a move
      action, gait 2, radius about 1.0 m) when `d` > 1 m and the straight line to him is not walkable
      (`0x002221b0`), or **push a fight goal with duration 4000** and run again (returns 1); without the line, a
      move-to-human action (2000 ms), an EngageEnemy goal, or a 100 ms wait.
    - **Unarmed, `+0x2d3` set, `d` > `R`**: **push an EngageEnemy goal** on the target when `0x00290588` allows it
      (below), else a move-to-human action to 2 × far (4000 ms) and a 100 ms wait.
    - Without `+0x2d3` (the target cannot be chased): pick up a weapon nearby (`0x002ade90`, brain `+0x265`), a
      throw of a held object (kind `0x17`) at a target within 4 × `R`, at most one in the game every 6 s, or a
      positioning move (`0x002ae028`, four modes by distance and a draw); not traced further.

`0x00290588(brain, target)` allows the chase unless the brain is not type 1, the target's own target is someone
else, and the gang's wanted timer (gang `+0x5e8`) is set and `0x0028de98` refuses; a type-1 brain (police) also
checks `0x00290138`. With no wanted timer it allows it.

#### The FindEnemy goal {#find-enemy}

Built by `0x002c0430` with 90.0, 30.0, the byte at `0x00510adb` and the duration (`+0x1c`); its time limit is its
own `+0x20` = the push time + duration (none for −1). **Process** (`0x002c0748`): past `+0x20`, done. Target still
valid → **`Brain_Fight(brain, target, duration, 0)`**, which pops Melee and FindEnemy and pushes the three goals
again. No valid target → fight stance off (`0x0022fed0`) and done, except for a brain with `+0x28d` set and
`+0x264` ≥ 0 in a gang whose `+0x18` is 1, which may push a Chase goal (12, `0x002b04f0`) on a new enemy. So
FindEnemy runs only when Melee has ended and restarts the fight while the target lasts.

#### EngageEnemy: the run-in {#engage-enemy}

`EngageEnemyGoal_Init` (`0x002af5b0`): `+0x10` the target's handle, `+0x20` the attack kind (45 = none chosen),
`+0x2c` a taunt (0), `+0x30` **2.56** (a squared distance: 1.6 m), `+0x34` "start slowly" (the human's gait
`+0x1a8` below 3 and not class 13), `+0x36` 1. **Start** (`0x002af670`): `+0x35` (**charge armed**) is set when the
target is at least far (5 m) away; a type-2 brain may taunt (anim `0x47`); next shout `+0x18` = now + 4000, next
re-target `+0x1c` = now + 2000; raises brain `+0x0b` by one (restored by End, `0x002af8d0`, which also clears the
actions). It has no time limit. **Process** (`0x002afa48`), each update:

1. Actions blocked → done; `0x00228428` → wait. The fight stance is dropped (it runs, not shuffles).
2. **Stopping** (`+0x39`, set by the stop below): wait until the human's `+0x1a8` is 0, turn to the target
   (`0x00221cd8`, 0.3) and end (2).
3. Target no longer valid, hidden in shadow, or `0x00290588` refusing → stop.
4. Every 2 s (not class 13): the nearest enemy replaces the target when it differs, the target is farther than far
   and the new one is seen (`Human_CanSeeHuman`, near 3 m, in shadow 2 m).
5. The target brain's `+0x1ec` is cleared (he may be attacked at once).
6. **Re-plan** when the target's heading turned more than 45°, he is within 1.6 m, he slowed from a run, or 250 ms
   have passed; a shout every 4-4.5 s (`0x002af928`). Otherwise wait while the move action runs.
7. The last move failed (`+0x284`) → stop.
8. **Gait 4 (run)**; 5 (sprint) when the target runs (gait > 3) and the fighter's stamina is above 50 %
   (`Human_GetStaminaPercent` `0x00222fa0`, [Sight](#sight)).
9. **Stop** (when the target is in sight, `0x00222288`) if he cannot be attacked by this human now
   (`Brain_IsAttackableBy`, `0x00290ea8`), at any distance; and, within 0.75 × far (**3.75 m**), if his actions are
   blocked (he is busy, as when another Warrior is hitting him) or the charge is not armed and he walks or stands
   (when he runs, it arms the charge instead).
10. **Give up** (done) when the target is out of sight, at least 10 m away, and either 20 m away or brain `+0x28d`
    set, and his gang's `+0xdc` is 0.
11. **Charge**: within 1.6 m with the charge armed or the target running, [below](#engage-charge).
12. **Move**: aim at the target's position led by his facing × his speed (`+0x1ac`), turned 9° per attack slot
    index, alternating sides, when he faces away (sector 3-5 of `0x0029eb38`; inferred: to fan the attackers out);
    aim straight at him when no lead applies or the led point is not reachable in a walkable line (`0x002221e0`).
    The running move action gets the new point and gait; else a new [move action](#move-action) (radius 0.5 m, a
    random 0-500 ms start delay the first time when "start slowly").

When EngageEnemy ends, Melee is on top again and, now within `R`, pushes the fight goal.

#### The charge in EngageEnemy {#engage-charge}

Step 11 of `EngageEnemyGoal_Process` (`0x002afa48`) runs within the run-in distance (`+0x30`, 2.56 → 1.6 m) and when
T runs (gait > 2) or the charge is armed (`+0x35`). Confirmed (code).

1. **A taken sector.** Take the sector of T's record that A approaches from (`Human_GetSectorOf`
   `0x0029eb38` of A's bearing relative to T's heading). When another human is the near one there, the charge is
   disarmed and A only moves (step 12). When A himself is that human, A waits this update.
2. **The kind** (once; `+0x20`, 45 = none):
    - A **cop** (type 1) takes kind 0 (`X1`) with 50 % (80 % against a gang member, class `+0x11b` 10), else 21 (the
      tackle). It is forced to 0 when T's `+0xe0` has bit `0x40`.
    - **Anyone else** uses `Brain_PickAttack(B, T, Human_CanUseAttackKind)`. When that is not a charge kind
      (`AttackKind_IsCharge` `0x00229b60`: 0, 19, 20, 21), the engage goal **stops** (step 2 of the goal: halt, turn
      to T, end). Melee then pushes a fight goal.
3. **The start test.** When `Human_CanStartAttack(A, T, kind)` (`0x00224778`) passes:
    1. Clear the actions; when an action refuses, wait.
    2. Queue one attack action directly (not `Brain_QueueAttack`), no delay. Kind 0 carries A's heading as the
       stick angle.
    3. Restore the move gait, disarm, and clear the kind.

   When it fails, A keeps running in (step 12).

So a running AI's charge is the run-in `X1`, the L2 charge or dive (19, 20 when its table has weight there) or the
tackle. Anything else ends the run-in and becomes a fight goal.

#### The durations {#fight-durations}

The duration given to `Brain_Fight` (2000 or 8000 by the AI's own fights, −1 by `GoalFight`) reaches three goals:

- the first **fight goal**'s deadline `+0x24` (−1: already past, [The fight goal](#fight) step 5);
- **Melee**'s time limit: it counts from when Melee first runs (after that fight goal ends), and once passed Melee
  ends the next time it is on top, without running. −1: **no limit**;
- **FindEnemy**'s `+0x20`, counted from the push. −1: **no limit**, so it re-starts the fight for as long as the
  target is valid.

Every **later** fight goal, pushed by Melee, gets **4000** whatever the duration was, so under `GoalFight` the
second and later fight goals have a 4 s deadline. The engage goal has no time limit. Confirmed (code) at
`0x0029ed58`, `0x0029eed8`, `0x002ade10`, `0x002ae2e8` and `0x002c0748`.

#### Choosing among enemies: the score {#enemy-score}

`Brain_PickBestEnemy(goal, flags)` (`0x0029f230`), called by Melee (flags `0xfffffffd`, every term but `0x2`) and by
23 other goals with their own masks: the current target is taken as "the previous one", the target is cleared, and
the 16 slots of the enemy list (brain `+0x164`) are walked. A slot whose human fails `Brain_ValidateEnemy` is
skipped; every other gets a score (`Brain_ScoreEnemy`, below) and then the goal's own adjustment (goal vtable `+0x54`;
every goal seen uses the default `Goal_AdjustEnemyScoreDefault` `0x0029f3a8`, which adds **3** when the candidate is
the previous target). The best score wins only when it is **above 0** and above every earlier one (ties keep the
first); after every 5 slots the walk stops early if a winner exists. The winner (or none) becomes the target
(`Brain_SetTarget`). Confirmed (code).

**`Brain_ValidateEnemy(brain, human)`** (`0x0028d358`) returns the human when he may be chosen: not dead or out of
the fight (state flags `0x100000000`, `0x80000000`, `0x40000`), not being mugged (`0x200`), his brain's targetable
byte `+0x120` set; when he is under arrest (`0x20000`, `0x00223b70`) only for a scorer of brain type 0 or 3; a human
with an interrogation set (`+0x5a0`) who is grabbed from the rear or at 50 % health or less only while his `+0x360`
is empty; and for a scorer whose `+0x14d` is set and whose gang has a turf (gang `+0x17c`), only inside it
(`0x0028ff58`). Confirmed (code).

**`Brain_ScoreEnemy(scorer, enemy, flags)`** (`0x0029ce98`) returns −999 for none, an enemy near a train this update
(`Brain_IsNearTrain` `0x0028ab30`: brain `+0x2e0` equals the game time) or one the scorer may not take
(`Brain_CanTakeSlotOn` `0x0028dc80`: the enemy's attack slots are full, the scorer holds none, and no current
attacker is farther from the enemy than the scorer). Otherwise the sum of these terms, `w` being the weights that
`CfgSetTargetingPoints` / `CfgSetTargetingPointsEx` set (values from `config_preload2.lua`; the executable's
defaults in brackets where they differ):

| Term (flag) | When | Points |
| --- | --- | --- |
| base | the scorer cannot chase him (`Brain_CanBeChased` `0x0028abc0` false on the enemy, or scorer `+0x2d3` clear, or the enemy outside the scorer gang's turf) | −5 (`0x00510b94`) |
| `0x1` distance | always with this flag: beyond the scorer's sight range `R` (brain `+0x130`; a gang of kind 1 uses at least 30 m, `0x0028bf00`) the score is **−999** | + 4.0 × (`R` − distance) (`0x00510b4c`) [3.0] |
| `0x2` near the leader | the scorer gang's leader (`Gang_GetLeader` `0x00165678`) is not the enemy and is within `R` of him | + 3.0 × (`R` − that distance) (`0x00510b50`) |
| `0x4` enemy leads his gang | the enemy is his gang's leader and the scorer's brain is not type 3 | + 6 (`0x00510b54`) [3] |
| `0x8` stunned | `0x100000` set and `0x80000` clear | + 1 (`0x00510b58`) |
| `0x10` out of view | outside the scorer's field of view (`Human_IsInFieldOfView` `0x00222710` with brain `+0x12c`) | −5 (`0x00510b5c`) |
| `0x20` down | knocked down (`0x80000`) | + 1 (`0x00510b60`) |
| `0x40` grabbed | grabbed (`0x30`) | + 1 (`0x00510b64`) |
| `0x80` grabbed from the rear | `0x20` | + 6 (`0x00510b68`) |
| `0x200` targets me | the enemy's target (brain `+0x124`) is the scorer | + 4 (`0x00510b6c`) [1] |
| `0x400` running | gait 4 with record `+0x08` clear | + 3 (`0x00510b70`) |
| `0x800` train | `Brain_IsNearTrain` × 6 (`0x00510b74`); always 0 here, since such an enemy already returned −999 | 0 |
| `0x1000` tagging | `0x2000000` | + 3 (`0x00510b78`) |
| `0x2000` targets me armed | as `0x200`, and he holds a weapon (`0x00224060`) | + 3 (`0x00510b7c`) |
| `0x4000` a player | the enemy has a pad (`+0x1b0` ≠ −1) | + 20 (`0x00510b80`) [3] |
| `0x8000` arrested | `0x20000` | −10 (`0x00510b98`) |
| `0x10000` police | the enemy's brain is type 1 | + 30 (`0x00510ba0`) |
| always | no walkable straight line from the scorer to him (`0x002221b0`) | −5 (`0x00510b90`) [−5] |
| always | he may not be attacked now (`Brain_IsAttackableBy(enemy brain, none)` false, [Sight](#sight)) | −10 (`0x00510b9c`) |
| always | he is the scorer gang's chosen target (gang `+0x10`) | + 400 (`0x00510ba4`) |

The last four, and `0x8000` and `0x10000`, have no script setter; the sixth argument of `CfgSetTargetingPoints` is
read and dropped, and its ninth (`0x00510b84`, 3) is the previous-target bonus above. Since a winner needs a score
above 0, an enemy beyond the sight range is never picked, and a near one that is out of view, unreachable in a
straight line and not attackable may not be either. Confirmed (code) at the addresses cited; the values confirmed
(disc) from `config_preload2.lua`.

#### Sight and "may be attacked now" {#sight}

- **`Human_HasLineOfSight(a, b, hit, exclude)`** (`0x00222288`): a ray from a's position + 1.7 m (z) to b's position +
  1.7 m (`Ray_IsClear` `0x0024df40` → `WorldManager_RayCast`, [Collision](collision.md#ray-cast), mask 0); if it hits,
  a second ray to b's position + 1.0 m. True when either reaches b. Without an exclusion list the rays pass through
  materials 30 `LOW_FENCE`, 2 `GLASS`, 187 `STOREDOOR_GLASS`, 122 `RAILING`, 107 `CHAINLINK_NOCLIMB` and 1 `NONE`
  (the list built in `Ray_IsClear`), so an AI **sees through** fences, railings and glass. Its distance is not limited;
  callers test range themselves. Through `hit` it returns a word of the cast; Melee reads it as the material crossed
  and, when it is one of 30, 2, 187, 122, 107, walks straight at the target (gait 4, out of the fight stance) instead of
  fighting (inferred: to get round the fence or glass between them). Confirmed (code) for the rays; the meaning of
  `hit` inferred.
- **`Human_IsInFieldOfView(fov, human, point)`** (`0x00222710`): true when the angle between the human's facing and the
  direction to the point is within fov / 2 (dot product against `cos(fov × 0.5)`). Confirmed (code).
- **`Human_CanSeeHuman(fov, range, near², shadow², looker, target)`** (`0x002223e8`): false beyond `range`; a target
  hidden in shadow (his brain `+0x2d4`) only when the squared distance is below `shadow²`; within `near²` (squared
  distance) the field of view is skipped, else `Human_IsInFieldOfView(fov)` must pass; then the line of sight decides.
  EngageEnemy's re-target passes 9 and 4 (3 m and 2 m). Confirmed (code).
- **`Brain_IsAttackableBy(target brain, attacker)`** (`0x00290ea8`): normally the target brain's byte `+0x11f`
  ("attackable": 1 when the brain is made, `0x0028a570`; set by `GangSetAttackable` through `Gang_SetAttackable`
  `0x0016bb58` and by the end of a scripted goal, `0x002d4248`; cleared by `GoalGrabTarget` while it holds,
  `0x002bb560`). Two exceptions: a civilian (type 4) in a gang of kind 23 attacked by a Warrior (type 3) whose own
  target is not that Warrior, and who has no target or an AI one, may be attacked only when he is the Warrior gang's
  chosen target (gang `+0x10`); and a human of class 221 with `+0x11f` clear is still attackable by an attacker that
  has no goal `0x1f`. Confirmed (code).
- **Sprint check** (EngageEnemy step 8): `Human_GetStaminaPercent` (`0x00222fa0`) = stamina (record `+0x14a`) × 100 /
  `Human_StaminaMax` (`0x00223188`, [Sprint](characters.md#sprint)); above 50 the AI sprints after a running target.
  AI stamina drains and refills by the player's rules (the drain is keyed on gait 5, not on the pad). Confirmed (code).

#### The enemy scan {#enemy-scan}

How an AI finds enemies on its own: the enemy list (brain `+0x164`, 16 slots) is rebuilt by a periodic scan, and the
fight and Melee goals pick from it ([the score](#enemy-score)). Confirmed (code) at the addresses cited.

**When** (`Brain_MaybeScanEnemies` `0x0028fa18`, from `Brain_UpdateGoals` each update): when the gang changed since
the last scan (gang `+0x34` ≥ brain `+0x160`), or when `+0x160` + interval has passed. The interval is brain `+0x144`
(2000 ms, cops 1000 ms) × 4 in the fight stance or when the human's detail level `+0x333` is above 0 (× 1 otherwise),
so a calm AI near the camera scans every 2 s and a fighting one every 8 s. A scan also needs a token
(`AI_TakeScanToken` `0x00293cd0`): at most 5 scans run in one update across all brains; a refused brain tries again
on the next update.

**The detail level** human `+0x333` (`Human_UpdateLod` `0x0023d660`, each move): from the distance `d` to the nearest
camera, plus 0.01 m per ms since the time at the human's draw record `+0xd8` `+0x2c` (inferred: time since last drawn):
0 below 30 m, 1 below 60 m, 2 below 100 m, 3 below 115 m, else 4 (thresholds `0x005102f0`; the switch `0x005102d4`
is 1, 0 would force level 0). The AI uses it to do less far away: [steering](#steering) is off above 2, the
[corner speed](#move-action) and the [riot](#riot) scale with it.

**The scan** (`Brain_ScanEnemies(brain, filter)` `0x0028b358`). A human who is down or dead empties his list. Else,
with `R` the sight range (`Brain_GetSightRange` `0x0028bf00`: `+0x130`, at least 30 m in a gang of kind 1) and `c` =
cos(field of view / 2):

1. For each of the 32 gangs that is active (gang `+0x18`) and an enemy of his (`Gang_IsEnemyOf` `0x00168fe0`: the
   enemy bit of the other's id in gang `+0x38`; never between kinds 1 and 23, nor during a global truce), each of its
   16 members within `R`:
    1. A member hidden in shadow (his brain `+0x2d4`) is skipped beyond 2 m, and within 2 m unless the scanner's
       `+0x2d5` is set.
    2. Unless the member's gang is always seen (gang `+0xdc`): `Human_MaySpectate` (`0x002225d8`) must allow him:
       always for a Warrior and for a brain that may not start a fight (unless its class `+0x11b` is 10); otherwise,
       with fewer than 3 fights (`+0x2d0`), an AI scanner lists only a member less than 1.9 m above him.
    3. The filter `+0x15c` must accept him: `Brain_IsValidEnemyOf` (`0x0029bf60`, `Brain_ValidateEnemy` on the
       scanner's brain) for every type but the Warriors, whose filter is `Filter_IsThreatTo` (`0x0029bf98`): a cop
       only within 10 m and when `Brain_IsThreat` (`0x00290138`, below) calls him a threat; a civilian (type
       4) only of ped type 3 with a threat response and not grabbed from the rear; any other only when he is such a
       threat; each then `Brain_ValidateEnemy`.
    4. Unless his gang is always seen, sight: within the near radius (3 m; for a cop scanning a running member 5.2 m,
       for a member walking or standing 1.5 m) only the line of sight counts, unless the member is a player sneaking
       in shadow (`Human_IsHiddenFromBrain` `0x00223c50`); beyond it, or when sneaking, he must also be in front
       (direction · facing > `c`). Then `Human_HasLineOfSight` must pass.
2. The scanner gang's chosen target (gang `+0x10`) is added when it passes `Brain_ValidateEnemy`.
3. Old entries not found again are kept while within `R`, still valid, and within 3 m or in line of sight; others are
   dropped. Each old entry's hostile count (his brain `+0x152`) is lowered first.
4. More than 16: sorted by distance, the 16 nearest kept. Each human new to the list is sent to the scanner's own
   event handler as event `0xb` (a new enemy; a tactic hears of it through `Brain_AddEnemy`); every listed human's
   brain `+0x152` is raised by one (`Brain_CountHostile` `0x0028ef20`), and the radar marks the list
   (`HUD_RadarMarkEnemies`).

**`Brain_IsThreat(brain, other)`** (`0x00290138`; also `Human_ReactToThreat` `0x00222a48`, which the type
event handlers call): never himself; a Warrior whose `+0x2e5` is set (hitting back at his chief) and the chief are
threats to each other; otherwise a threat when the gangs are enemies or both are players.

#### The Spectate goal {#spectate}

Type `0x10` (`SpectateGoal_Init` `0x002b4098`, vtable `0x00540270`), arguments (keep distance, goal, brain, *join*,
*may engage*, shortest and longest pause (ms), time limit, *taunt*). Fields: `+0x10` the watched human's handle,
`+0x14` the next look, `+0x18` the next re-pick (now + 3 s), `+0x1c` / `+0x20` the pause range, `+0x24` the keep
distance, `+0x28` *join*, `+0x29` *may engage*, `+0x2a` *taunt*, `+0x2b` may pick up a weapon (rolled at Start),
`+0x2c` chase when far. Confirmed (code).

- **Start** (`0x002b4200`): picks whom to watch (`SpectateGoal_PickTarget` `0x002b4158`): with *join*, the nearest
  human of his own enemy list; otherwise the nearest member of the nearest other gang (`0x0016c808`). `+0x2b` =
  `Random_Int(100)` < 25 × (a per-gang-kind number, `0x001644f8`); `+0x2c` = 50 % chance, when no goal `0x98` is on
  the stack. **End** (`0x002b42f0`): target cleared, actions cleared, brain `+0x284` = 0.
- **Process** (`0x002b4330`), each update: the target is cleared; a failed last move clears `+0x2c`. With *join*:
  fight stance; for a brain of type other than 1 and 4 that may pick up a weapon and is empty-handed, every 30
  updates the nearest pickable weapon within 15 m (`0x0029d5f0`) is fetched (`Goal_GetItem`). Every 3 s the watched
  human is picked again. Then:
    1. No valid watched human: done with *join*, else wait (until the time limit).
    2. With *join* he is made an enemy (`0x0028ff78`); tackling → an attack action of kind `0x24`.
    3. Unless the watched human's gang has `+0xdc` set, out of sight → done; in shadow (`+0x2d4`) and beyond 3 m → done.
    4. With *join* and *may engage*, not after a failed move, when `0x00290588` allows a chase: an **EngageEnemy**
       goal (taunt `0x8f`) when the target runs (one update in five) or `+0x2c` is set and he is beyond the far range.
    5. Otherwise, with his actions free: fight stance; every pause (random between the two lengths) an optional taunt
       (`0x002fb0f0`, with *taunt*); beyond 10 m a move toward him (gait 2, 4 when he runs or with *join*, radius far +
       2 m); else with *join*, between the keep distance and 2 m more, 51 %: a watch action (`0x002feaf8`, 3 s);
       else a move-to-human action that holds him between 0.95 × and 1 × the keep distance (4 s limit). The keep
       distance 0 means the far range.

Melee's Spectate (no target, [The Melee goal](#melee-goal) step 3) is `SpectateGoal_Init(k, goal, brain, join 1,
may engage 1, 1000, 3000, 2000, taunt 1)`, with *k* = 0 (the far range, 5 m) or, when goal `0x98` is on the stack, a
random distance between 0.75 × near and far: a 2 s spell watching the nearest enemy, with a taunt every 1-3 s, joining
in with EngageEnemy when he runs. The dealer's wary goal is (8 m, join 0, 0, 2000, 4000, 8000, 0): he backs off to 8 m from
the nearest fighter for up to 8 s.

#### Fetching an object: GetItem and the object search {#get-item}

**The object search** `Ai_FindObject(A, needRay, filter, claim, needReach, centre, allowLow)` (`0x0029d5f0`) is the
general search behind every AI pick-up and smash (21 callers: Spectate, Melee,
the Warriors' think, Riot, Destroy, Steal and others). Confirmed (code).

1. List the world objects round `centre` (A's position by default; `ObjectManager_FindObjects` on the object list
   `+0x840`, at most 384), nearest first.
2. Take the first object that passes all of these:
    1. It is not attached to a task (not held or carried).
    2. Its flags (vtable `+0x54`) lack bit 26.
    3. It is not claimed by A's own gang: object `+0xec` is the gang and `+0xf0` the claim's end.
    4. The caller's `filter` object accepts it. This is where each caller's radius and kind test live, for example
       15 m and pickable for Spectate, and 20 m and breakable for Riot.
    5. For an AI A, no player stands within 1 m of it.
    6. With `needRay`, a clear ray from A to it (`Human_HasClearRayTo`, mask 8). Unless `allowLow`, the ray must
       not end in result 2 (inferred: blocked low).
    7. With `needReach`, a navigation polygon under it (within the larger of the type's half-sizes `+0x78`,
       `+0x7c`) that A can reach (`Nav_CanReach`).
3. With `claim`, the object is claimed for A's gang for **5000 ms**, so gang mates do not run for the same object.

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
- **Resume** (`0x002dc8b0`) finds the point beside the object: the polygon under it within its half-size. With none,
  the goal will end.

**Process** (`0x002dc9f8`), each update. Confirmed (code).

1. The object is gone: done. Otherwise look at it (500 ms).
2. The object's bit 26 is set: wait. A pick-up is in progress (held flag `0x4000`): note it (`+0x31`) and wait.
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
    5. **Within 1.5 m:**
        - Wait while A has any held flag but `0x40000000`.
        - With a clear ray to the object (`Human_HasClearRayTo`, mask 0):
            - When the ray result is not 2: queue a **pick-up action** (`PickUpItemAction_Init`, `0x21`) and count
              a try.
            - When the ray result is 2: step to the point (when beyond 0.4 m; gait 3, radius 0.36 m), turn to the
              object, and press **cross** (`Brain_QueueAttack` kind 0 on himself, delay 33 ms). This is the
              player's context pick-up press.
        - Without a clear ray: turn to it when more than 15° off, else count a try.
    6. **Walk** to the point out of the fight stance: gait 3 within 3 m, else the goal's gait, radius 0.36 m.

#### Objects in an AI's hands {#ai-objects}

Who fetches an object, and when an AI lets go. Confirmed (code) at the addresses cited.

**Fetching** (each pushes GetItem, [above](#get-item), on what `Ai_FindObject` returns):

| Who | When | Search |
| --- | --- | --- |
| Melee (`Melee_TryPickUpWeapon` `0x002ade90`) | brain `+0x265` set, actions free, empty-handed; every 20 updates, or when asked | weapons (filter `0x0053f2d0`, mask `0x20000`; also throwables `0x30000` when the target is within 15 m and asked), 20 m, or 10 m when asked; gait 4 |
| Spectate | joined, not a cop or civilian, 25 × gang factor % rolled at Start; every 30 updates | pickable within 15 m ([Spectate](#spectate)) |
| The Warriors' think | [The Warriors' pick-ups](#warrior-pickups) | 1.5 m |
| FollowAndAttack / FollowAndDefend / FollowPlayer | `+0x265`, not blocked; every 20 updates or within 15 m of the enemy | smash or throw objects, 20 m |
| AvoidEnemies | empty-handed | throwables |
| Riot, DestroyCar (`DestroyCar_GetWeapon` `0x002ddd48`, 10 m), Destroy, WarriorVandalSteal, the Wander tactic | to smash something | breakables / weapons |
| ObjectThrower, ManWeaponPile, ObjectPile, Steal | their own objects | below |

**Using.** A held weapon changes the [attack pick](#pick-attack) (the armed term) and the fight goal's range test
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

- Speed: running faster than the run speed drops a kind-4/6 object (step 2 of Speed control, [above](#ai-gaits)).
- A Warrior with no enemies drops a kind-4/6 weapon unless his gang's tactic is steal (`0x26`)
  ([pick-ups](#warrior-pickups)).
- GetItem's Start, DestroyItem's and DestroyCar's Start, Cower's Start, CallGang's init and RunFromTrain
  (`RunFromTrainGoal_DropEverything` `0x002f9a80`) drop what is held.

#### World flags {#ai-flags}

An AI uses a flag ([World flags](flags.md#activities)) only when its brain's `+0x2d1` is set (1 when made;
`Brain_SetWorldFlagUse`, `Gang_SetCanUseWorldFlagsById`): `Human_NearestUsableFlag` (`0x00417670`) returns 0
otherwise. The chance byte `+0x2d2` (10 when made) is written by the same setters but no reader was found. Confirmed
(code).

- **Pedestrians** (`0x002ab4b8`, the pedestrian goal's move step): every 20 updates, when he may chat and has
  walked far enough from his last stop, the nearest usable flag within 10 m that he can walk straight to in under
  15 m gets a **MoveToUseFlag** goal (radius 0.3 m, his gait); a re-use timer at goal `+0x6c` (65 s, inferred)
  keeps him from going back at once.
- **HangOut**, **Patrol**, the **TravelPath** and **Wander** tactics look up a usable flag the same way.
- **MoveToUseFlag** (`0x002dbc10`): done when his actions are blocked or the flag is disabled. Walk to it (up to 3
  tries); while walking, within 5 m, every 15 updates: a human within 0.75 m of the flag ends the goal. At the
  radius: turn to the flag's heading (beyond 15°, turn rate 0.2), then he is placed on the flag's point and heading
  and plays the activity's clips (enter, idle, exit, [Activities](flags.md#activities); the clip states after this
  are not traced).

#### Cover, cars and trains {#ai-hazards}

- **Hiding** is only the hold command's hide tactic and Scatter ([Warrior commands](#warrior-commands)): Hide
  (`0x58`) walks to a wall point near a hiding flag (gait 3 near the leader, else 5), out of the shadow, then turns
  to face out. Shadow itself is the human's hidden state (`Human_IsHiddenInShadow`, state `0x200000`) that
  [the enemy scan](#enemy-scan) reads; an AI never seeks shadow on its own (no other goal sets it). Inferred from
  the setters' callers.
- **Cars.** No goal in the AI range drives or enters a car: cars run on their own paths ([Cars](cars.md)). AIs meet
  cars three ways: DestroyCar (`0x2e`) smashes one from a spot round it; the Pursue tactic, on a crime event (23) at
  a car, has members within 30 m drop an Investigate goal and a chasing member push Investigate at the crime point
  (`Chase_InvestigateCar` `0x002b0c88`, 5 m, line 13), unless the offender is friendly; the Wander tactic's
  `Tactic_VandalizeCars` (`0x00316590`). Confirmed (code).
- **Trains.** The train (`TrainRecord_Update` `0x00413d90`) pushes **RunFromTrain** (type 7) on a human near its
  track: he drops everything, turn boost 3, and sprints (gait 5) to a safe point beside the track, or along it when
  no side is clear ([its rows](ai-goals.md#goal-run-from-train)). While a train is near, the enemy score rules the
  human out (`Brain_IsNearTrain`, [the score](#enemy-score)).
- **Doors and breakables on a route.** Doors are route links ([Following a route](#route-follow)); a follower stuck
  on the way to his formation slot smashes a breakable within 1.5 m (VandalizeItem, [FollowFormation](#warrior-follow)).

### The attack action {#attack-action}

The attack action (vtable `0x00542ce0`, `AttackAction_Init` `0x002fa918`):

- **Start** (`0x002fa9a8`): checks the target and keeps the start time in the action's `+0x18`. It sets the brain's
  `+0x1e8` (its next attack, `0x00290e48`) to now + **the attack delay** (`Human_AttackDelay`, `0x00223800`:
  `CfgAttackDelay[kind]` × the power class's `+0x1c`, or `+0x20` when the target is down), **halved** when the
  target's own target is this human or the brain is type 3. `CfgAttackDelay` is a table of ms **indexed by attack
  kind** (`0x00228870` reads `0x006b6658 + kind × 4`); `config_preload2.lua` sets 200 for most kinds, 400 for 12 and
  13, 500 for 19 and 21, 1000 for 20, 300 for 32-34 and 0 for 23, 31 and 42. When neither human is busy, it sets the target
  brain's `+0x1ec` (when it may be attacked next, `0x00290e78`) from a **separate per-kind time** (`0x00231590`:
  the attacker's clip length for the kind, [Attack slots, active places and spacing](#attack-places)), scaled by
  `0x00510ad0` / the target brain's spacing byte `+0x14a` (or `+0x14b`, by `0x00223b48`) when that is not 0; the
  attacking gang's `CfgGang` sets those bytes ([Combat](combat.md#ai-attacks)).
  This corrects the earlier reading that both took the attack delay. Then it writes the command
  (`AttackKind_ToCommand`) into the per-player record `+0x20` (`PlayerRecord_SetCommand`, `0x00147ef0`) and, when
  the action has an angle (`+0x1c` ≥ 0), a stick of magnitude 1.0 at that angle.
- **Update** (`0x002fad70`): re-writes the command only while now < the action's `+0x18`, which Start set to now, so
  **never**: the command is written **once** (confirmed (runtime), below). It is done at once when the action's
  `+0x14` is 1, otherwise once the record's `+0x08` has none of **`0x5c0221f`**: the attack's held flags
  ([Tasks](tasks.md#held-flags)) decide when the AI is free again. This corrects the earlier "writes the command again
  while waiting".
- **Abort** (`0x002fad30`): refused while `+0x08` has any of `0x5c0221f`.

So an AI attack is a press and a wait on the same flags the player's moves hold, and its chain timing is the
player's.

Confirmed (runtime), the street civilian (`PoizoCiv`, class 417, `Att_Normal`, power class 2) made a type-2 brain and
set on the player with `GangMakeEnemies` and `GoalFight` (no save reaches `level99`'s `CombatWarriors` fight, so this
is a stand-in with the same attack table). Hooks on `0x00147ef0` and `0x00147ef8`:

- The goal stack was `0x41` (FindEnemy), `0x08` (Melee), `0x0f` (Fight); the front action was the attack action,
  the move-to-human action or, when far, a move action (goal `0x0b`, [EngageEnemy](#engage-enemy)).
- Commands seen: `0xd` (grab, then a throw with a stick of 1.0), `0xf`, `0x10`, `0x11`, `0x12`, `0xe` and `0x20`
  (charge). Each is written once, in the attack action's Start, and read by the dispatcher in the same step; the next
  step's record update clears it ([Tasks](tasks.md#humans-update)).
- **Chains**: the second press came 9 updates after `S1`'s (`0xf` → `0x11` or `0x12`), 12 after `SS2`'s, 13 after
  `X1`'s (`0x10` → `0x12`), inside the chain window as `AttackKind_ChainDelay` plans.
- `+0x1e8` was set to now + 700 ms at a chain's first press, + 1100 ms at the second, about + 1400 ms at the third,
  and + 1750 ms for `0xe` and `0x20`: each press of a chain pushes the next attack further.

Confirmed (runtime), the three sparring Warriors against a still player (`warriors_passive`,
[the sparring fight](#level99-fight)): with factor 20 (power class 40), `+0x1e8` went to now + **4000 ms** after a
200 ms kind, + 8000 ms after `0x37` / `0x38` (kinds 12, 13: 400 ms), and + 2000 ms when halved. So a sparring Warrior
waits about **4 s between attacks**; the still player took 22 attack commands from the three in about 40 s, many of
them grabs.

### Blocking and countering {#block}

- **The warning.** `+0x200` is a **count** of attack warnings since the last update: the default event handler
  (`0x00292d80`) adds one for each event `0x10`, as do `0x002ff898`, `0x00302ff0` and `0x00304070`, and
  `Brain_Update` clears it (`0x0028f914`). Event `0x10` comes from `0x0021d5c0`, called by `Attack_Start` (call at
  `0x0026267c`) and the square path (`0x00287594`), which sends it to a human only if `0x002223e8` passes: within the
  brain's range `+0x130` and its **field of view** `+0x12c` (half-angle, radians), or 2 m when the attacker's brain
  `+0x2d4` is set, and then a line of sight (`0x00222288`). Confirmed (code); the counting confirmed (runtime).
- **When** (`Goal_TryBlock`, `0x0029f098`): only when the brain's `+0x200` is not 0, that is when an attack on the
  human was announced this update. The chance is `Human_BlockChance` (`0x00223628`: the power class's `+0x08`, or
  `+0x0c` while hurt, × the global `0x00510ac8`), **a quarter of it for brain type 3**. A player attacker whose
  pattern meets the class's `+0x37` threshold (`0x002236c8`, `0x00418398` on human `+0x5d0`) is always blocked. Yes
  pushes the block goal.
- **An AI human cannot block.** Confirmed (code) at `0x0027c120` and `0x00147f98`. The dispatcher starts a block
  only when R1 (mask 8) is **held in a pad record** ([Combat](combat.md#dispatch)): `0x00147f98(record, mask)` reads
  pad record `0x005dd810 + pad × 0x50` with the pad index per-player `+0x19`, and returns 0 when that is −1, as it is
  for every AI human. Command 4 (R1 held) is looked at only inside the block branch, once a block runs; it cannot start
  one. So the block goal's command 4 does nothing for an AI: it never takes state `0x8000` or plays 606, and its hits
  land. What the goal does give an AI is the **no-reaction flag** and the **counter** below.
- **The block goal** (`0x1b`). Confirmed (code) at `0x002b54d8`, `0x002b5520` and `0x002b5808`.
    - **Init** (`0x002b54d8`): `+0x16` = 1 (active); `+0x10` (the end time), `+0x14`, `+0x15` and `+0x17` (an update
      count) = 0.
    - **Start** (`0x002b5520`): `+0x10` = now + a random 1000-3000 ms; `+0x14` = (rand100 < `Human_BlockChance`), which
      allows the counter after a duck ([Combat](combat.md#block)); `+0x18` = whether human `+0xe0` already had
      `0x800`. With a target: `+0x15` = 1 when the class's pattern threshold (`0x002236c8`) is above 0 and the
      target's pattern (`0x00418398` on human `+0x5d0`) meets it, and human `+0xe0` gets **`0x800`** unless `+0x18`.
    - **Flag `0x800`** on human `+0xe0` means **no hit reaction**: `Human_ApplyPendingDamage` (`0x00265f70`) still
      takes the health but skips the reaction. Process clears it (unless `+0x18`) on the first update from its sixth on
      whose record `+0x08` has none of `0x5c7eae0`.
- **Process** (`0x002b5808`), each update, in order:
    1. With a target while `+0x16` = 1: when the target's record `+0x08` has `0x400` (`0x00228560`) or its state has
       any of `0x7bf9e9f7ff0` (`0x00228228`), write command 4 and clear `+0x16`.
    2. While the brain has actions queued: return 0 (wait).
    3. `+0x16` = 0: done (2), unless the human is still blocking or ducking (`0x00223ad0`), then wait.
    4. **The block time is over** (now > `+0x10`): when the target is still mid-attack (record `+0x08` any of
       `0x5c0221f`, `0x00228428`) and its own target is this human, `+0x10` = now + a random 1000-3000 ms and `+0x15` =
       1; otherwise `+0x16` = 0, so the goal ends on a later update. Write command 4.
    5. **The block time runs**:
        - In its **last second** (`+0x10` − now < 1000), when `+0x15` = 1 and the human is not ducking (`0x00223b28`),
          queue a **punishing attack** (`0x002b5718`): `Brain_PickAttack` with the filter `Human_CanStartAttack`
          (`0x00224778`; the value `0x00224778ffff0000` is that member-function pointer, [Picking the attack
          kind](#pick-attack)) (class `0x6a` instead takes kind `0x10` or `0x11`, class `0x59` kind `0x10`), then
          `Brain_QueueAttack` and `+0x16` = 0.
        - Otherwise, with a target and `+0x15` = 0, **roll the counter**: rand100 < `Human_CounterChance`
          (`0x002235f8`: the power class's `+0x24` × 100) **and** the counter test below passes → write **command 3**
          (and set `0x800` again unless `+0x18`).
        - Otherwise write command 4.

    So the counter is rolled on **every update while the block time runs**, from the goal's first update, and
    never once `+0x15` is set (a pattern read at Start, or a block extended in step 4). This corrects the earlier
    reading that the roll comes after the block time.

- **Command 3 for an AI is a grab or tackle counter.** Confirmed (code) at `0x0027c120` and `0x0027d6e0`. The
  dispatcher acts on command 3 only for a human that is not pad-controlled (per-player `+0x1b` = 0), through
  `0x0027d6e0`, which refuses while the human's state has any of `0x7bf9e9f4300`, its record `+0x08` any of
  `0x100101f`, or it holds an object (`0x00231a38`). Then, against its target (`0x00226e60`):
    - the target plays **69-71** (`GRAB_MISS`, `GRAB_INTRO`, `GRAB_PLAYER_INTRO`; `0x00258e88`) → the human plays
      **76** `GRAB_FRONT_COUNTER`;
    - the target plays **2-4** (`TACKLE_MISS`, `TACKLE_INTRO`, `TACKLE_PLAYER_INTRO`; `0x002590f8`) → **9**
      `TACKLE_FRONT_COUNTER`;

    each as a paired move (`Attack_StartPaired`, `0x00262ac8`, flag `0x400000`; [Combat](combat.md#grabbing)). Both
    tests also need: the human free (record `+0x08` none of `0xfc7eaf7`, state none of `0x7bf9e9f7ff0`), not hurt
    (`0x00222ff8`), the target not knocked down (`+0xe0` `0x80000`), the target's own target this human,
    `0x002672d0` 0 both ways (inferred: face to face), a counter chance above 0, and, while the byte
    `*(0x0051489c) + 0x56e3` is 0, a brain of type 3 or a class whose `+0x11b` is 13 (`0x00223e20`). So an AI
    counters grabs and tackles, never strikes; a strike is answered only by the duck counter
    ([Combat](combat.md#block)).

At runtime (confirmed (runtime), PCSX2 2.9.94):

- **The street civilian** (`civ_block`, `civ_block_fov`), at 1.2 m, the player pressing square every 20 updates. With
  its class's field of view `+0x12c` = 1.5708, the player's attacks, aimed from in front but outside that test, sent
  no event `0x10` and it never blocked. With `+0x12c` set to π, six events `0x10` arrived; `Goal_TryBlock` saw
  `+0x200` = 2 once and pushed goal `0x1b` (`+0x10` = now + 1981 ms, `+0x14` = 1, `+0x16` = 1), which wrote command 4
  every update; the civilian never took a block state and kept taking hits.
- **A sparring Warrior** (`warriors_block`, [the sparring fight](#level99-fight)), the player pressing square every
  15 updates: Generic1 had 43 warnings and 5 block tries (with `+0x200` = 2); the one block goal wrote command 4
  every update for about 53 updates while its state stayed `0x3`, it played hit reaction 272 and its health went from
  1295 to 1278. No command 3 came (the player never grabbed or tackled).

New power-class fields (confirmed (code)): `+0x08` block chance, `+0x0c` block chance while hurt, `+0x24` counter
chance, `+0x37` the pattern-reading threshold ([Power classes](characters.md#power-classes)).

### Moving {#moving}

An AI moves by writing brain fields, never a stick: `+0x110` the heading to move along, `+0x114` the speed (through
`0x0028ab28`; `0x0028aac0(brain, 0)` stops), `+0x90` the point aimed at and `+0x118` its radius; the human's control
(`Human_UpdateControl`) moves it from those: speeds, turn limits, start and turn clips are on
[Characters](characters.md#ai-locomotion). A gait's speed is `Human_SpeedForGait` (`0x0022ae40`): 1.6286 m/s walking,
7.8012 m/s running. Confirmed (runtime): the civilian's per-player stick magnitude stayed 0 through its move and
move-to-human actions; only the attack action wrote a stick (1.0, for throws).

From 8 m behind the player (confirmed (runtime)): a turn on the spot (clip 398), the run start 414, the run 410 at
7.80 m/s, then at 1.29 m a charge (`0x20`). Closer, the move-to-human action walks in the fight-stance clips 372-380 at
about 2.3 m/s.

#### Gaits and speeds {#ai-gaits}

An AI never writes a stick: a goal or action sets the brain's move speed `+0x114`, usually from a **gait**, and the
human's control turns that into velocity ([Turning](#ai-turn), below for the speed). Confirmed (code) at the
addresses cited.

- `Brain_SetMoveGait(brain, gait)` (`0x0028aac0`): speed = `Human_SpeedForGait(human, gait)`, 0 for gait 0;
  `Brain_SetMoveSpeed` (`0x0028ab28`) stores any speed directly (FollowFormation, DevilRun, RunCarrot set speeds
  between gaits).
- `Human_SpeedForGait` (`0x0022ae40`, jump table `0x0055bec0`) reads the human's anim record (`+0xd4`) speeds
  ([Speed classes](characters.md#speed-classes)) without the `+0x3a4` multiplier; a human with no anim record gets 0.
- `Human_GaitForSpeed` (`0x00221760`) maps back: 5 at or above the sprint speed, 4 at or above the run, 3 the jog, 2
  the walk, else 0. The move action and the steering use it to tell standing from moving.

| Gait | Name | Record field | Rembrandt (m/s) |
| --- | --- | --- | --- |
| 0 | stand | - | 0 |
| 1 | sneak walk | `+0x16c` | 1.585 |
| 2 | walk | `+0x170` | 1.629 |
| 3 | jog | `+0x174` | 4.857 |
| 4 | run | `+0x178` | 7.801 |
| 5 | sprint | `+0x17c` | 10.245 |
| (stance) | fight-stance walk | `+0x164` | 3.429 (clip 380; 372 for an AI with turn boost ≤ 0, [brain bytes](#brain-boosts)) |

The speeds are each class's own clip speeds; Rembrandt's are confirmed (runtime) on [Characters](characters.md).

**The gait each goal asks for** (the gait argument of its move actions; confirmed (code) at each Process):

| Gait | Goals |
| --- | --- |
| the script's argument | MoveToFlag (1), MoveToFlagNetFlag (3), MoveToUseFlag (4), MoveToPosition (5), MoveToHuman (6), TravelPath (`0x38`), Wander (`0x3a`), DestroyItem, DestroyCar, Patrol (`0x73`) |
| 2 (walk) | pedestrians and their flags (`0x69`, PedFlag, AreaWalker, Hooker, Peddler), StandIdle, Mark, Scout, PathScout, Backoff, Ring, StandGround, Confront, CopPatrol, CopInteract, CopperGuardArrested, BigThrower's hold, the walk back of HoldPosition |
| 3 (jog) | ChaseSupport (3 m), GetItem within 3 m of the object, BigThrower, StationaryShooter's return, Hide near the leader |
| 4 (run) | EngageEnemy, [GoalRiot](#riot), Investigate, Respond, RiotCop, Tag, GrabTarget, Mace, Grabber, BigDefender, LeftTurf (back into the turf), FollowAndAttack / FollowAndDefend beyond reach, the Melee goal's GetItem |
| 5 (sprint) | Scatter, RunFromTrain, CallGang, Hide away from the leader; EngageEnemy after a running target while stamina is above 50 % ([Sight](#sight)) |
| chosen | Shadow: the leader's gait (2 at first, 3 beyond 50 m); AvoidEnemies: 5, or 4 below 50 % power, or the goal's own; FollowFormation and FollowObject: a speed between walk and 1.25 × sprint ([follow](#warrior-follow)); Spectate: 2, or 4 when the watched man runs |

**Speed control** (`Human_UpdateControl` `0x00243848`, the non-pad branch), each update, with `v*` = brain `+0x114`:

1. `Brain_UpdateGait`; a gait change rebuilds the human's move anims.
2. The wanted speed `w`: wounded and `v*` > 0.1 → the walk speed; in the fight stance and `v*` > 0.1 → the stance
   walk (`+0x164`), else 0; otherwise `v*`, at most 25 m/s. Running faster than the run speed with a held object of
   kind 4 or 6 **drops it**, except an object of type class 6 whose `+0x5a` is 0.
3. **Starting** (current speed 0, `w` > 0): unless the anim set's kind is 3 or 7 or its `+0x18` is 4, the human's
   action becomes 6 (a run start) when `w` ≥ the run speed, 8 instead with the start boost `+0x0c` above 0, or 5
   (the walk start) when `w` is exactly the walk speed; the first update's speed is min(`w`, 2 m/s).
4. **Accelerating**: + 8 m/s² × the step (0.267 m/s per update at 30 Hz), or + 32 m/s² (1.067 m/s per update) while
   more than 2 m/s short, never past `w`.
5. **Slowing**: − 32 m/s² (1.067 m/s per update) down to `w`. Diego and Vargas (classes `0x77`, `0x78`) running or
   sprinting with `w` = 0 stop at once with action 9 (a stop clip).
6. **Far away** (detail level `+0x333` 2 or more, [The enemy scan](#enemy-scan)) and no held flag of `0x310c0880`:
   velocity = `w` along brain `+0x110`, and the heading is **set** to `+0x110` at once (no turn limit, no clips).
7. Otherwise the turn ([Turning](#ai-turn)). **Stopped** (`w` = 0): when the heading error is above 15° and no
   flag is held, a turn on the spot starts (`Human_StartTurnOnSpot`, the clip is [Human](characters.md)'s); within
   15° nothing turns. A rear grabber turns at twice the limit and walks.

#### Turning {#ai-turn}

A human with no pad (per-player `+0x1b` = 0) is moved by `Human_UpdateControl` (`0x00243848`, the default control
`Human_SetUpdateControl` `0x00227c48` installs), not by the stick code (`Human_PlayerLocomotion` `0x00240e38`). Each
update it turns the human's heading toward brain `+0x110` by at most `Human_MaxTurn` (`0x002213d8`), **linearly**:
the eased turn of `CfgTurnRate` (`0x00510308`-`0x00510310`) is read only on the pad path, in the air and while
grabbing, so it does not apply to the AI. While moving faster than a jog, a large heading change may instead start a
turn clip (`0x0025c5b8`, [Human](characters.md)); the switch `0x005104e8` is 1. Confirmed (code).

`Human_MaxTurn` picks one word of a pair at `0x005101b0` ([Movement constants](characters.md#movement-constants)):
the player's word for a pad human whose brain is not dead, the **AI word** otherwise. In order:

| State | Pair | AI word per update | Boost applies |
| --- | --- | --- | --- |
| turn-limited (two human flags) | fixed | 4° or 6° (both kinds) | no |
| wounded | `0x005101b0` × 0.25 | 0.375° | no |
| grabbing, blocking or ducking | `0x005101b0` | 1.5° | no |
| sprinting (gait 5) | `0x005101b8` | 2.5° | yes |
| running (gait 4) | `0x005101c0` | 4° | yes |
| jogging (gait 3) | `0x005101c8` | 6° | yes |
| fight stance | `0x005101d8` | 24° | no |
| held flags `0x1000080` | `0x005101b8` × 0.25 | 0.625° | no |
| otherwise (walk, stand) | `0x005101d0` | 12° | yes |

"Boost applies": the gait rows go through `Human_GetTurnRateForGait` (`0x002212d0`), which multiplies the AI word by
the [turn boost](#brain-boosts) `+0x0b` (× (b + 1), or ÷ (1 − b) when negative); the other rows do not. At 30 Hz a
walking AI turns 360°/s, a running one 120°/s, unboosted. Confirmed (code); the AI words are `.data` values that
`CfgSetTurnRates` does not change.

#### The move action {#move-action}

Vtable `0x00542f20`, confirmed (code) at the addresses cited. `MoveAction_Init` (`0x002fb9e8`) takes the radius, the
point, the gait (its speed goes to `+0x40`), an option bit, a start delay, an object to face (`+0x3c`, 0 for none) and
a "flag kind `0x12`" bit.

- **Start** (`0x002fc420`; Ghidra has no function there): done (2) when `0x00223b70` holds or the human is within
  0.1 m. Otherwise it resets the avoidance state (`0x002890a8`), writes the point and radius to brain `+0x90` /
  `+0x118` and asks for a route ([Path planning](#path-planning)): none needed → it steers straight at the point;
  none possible → done (2).
- **Update** (`0x002fc5c0`), each update:
    1. Done when `0x00223b70` holds or the abort bit is set; wait while the human is busy or an avoidance wait runs.
    2. Every 30 updates (offset by the human's index), with a route: when the final point is in sight
       (`0x002221e0`), drop the route and go straight.
    3. With a route: done within the radius; aim at the route's current waypoint ([Following](#route-follow)).
    4. **Dynamic obstacles** (`0x00414188`, 4 records of `0x150` at `0x006f3a10`): a blocked segment → speed 0 and
       a 1000 ms wait.
    5. Every 31 updates the point must still be in sight, else done. Done when within 1 m in plan but more than
       1.5 m apart in height.
    6. **Steering** round other humans ([below](#steering)) may replace the aim by a detour point (radius 0.3).
    7. Heading = the direction to the aim, or to the object `+0x3c` when set; write `+0x90`, `+0x118` and `+0x110`.
    8. **Speed**: with an object to face, the gait's speed. Otherwise turn on the spot (speed 0) while stopped and
       more than 30° off; else the **corner speed** (`0x002fc158`): each of the next 3 waypoints' corners
       (`0x002fbd18`) farther than 0.35 m is simulated at a trial speed (`0x0022aae8`, `0x002fbef0`; 4 to 7 sample
       points by turn radius, bands 3, 5 and 7 m), and every predicted point must lie in a path polygon
       (`0x00250708`, `0x0024f718`); each failure lowers the trial speed by 1 + 0.75 × the detail level `+0x333`,
       down to the walking speed. It keeps the first corner's speed `+0x0c`, the second's `+0x44` and a braking
       distance² `+0x48`, and uses `+0x0c` while farther than that, else `+0x44`, never above the gait's speed.
    9. **Stuck** (`0x002fc330`): every 60 updates while moving, less than 0.2 m covered → brain `+0x284` = 3, done.
- Brain `+0x284` says why a move failed: 1 no polygon or no route, 2 or 4 an edge it cannot take, 3 stuck.
- **Nothing keeps a move going between actions** (confirmed (code)). `Brain_UpdateGoals` (`0x0028fbb0`) starts
  every think with `Brain_StopMove` (`0x0028ac18`): speed `+0x114` 0 (`Brain_SetMoveGait(brain, 0)`) and heading
  `+0x110` = the human's facing; only a move action's Update (step 7 above) writes `+0x90`, `+0x110`, `+0x118` and
  the speed back, each update. **Abort** (`0x002fc560`) sets the abort bit, ends the avoidance (`0x00289108`) and
  frees the route (`0x0029aa20`); it writes none of the move fields, and **destroy** (vtable `+0x24`, `0x004eef40`)
  is empty. So when a move is replaced (`GoalMoveToFlag`'s Resume, `0x002da550`: `Brain_ClearActions`, then
  `Human_DropTarget` `0x00226f70`, which only clears the target) the human is asked to stand for every think in
  which the new action is still counting its start delay: `Action_Update` (`0x002f9e48`) takes the time since the
  brain's previous think (`+0x30`) off the delay, the first time too, and returns before the class's Update while
  any is left. `GoalMoveToFlag`'s delay is `Random_IntRange(0, 250)` ms, so a draw within one update (about 33 ms)
  starts the move in the same think with no gap, and a longer one leaves up to 7 thinks at speed 0; the control
  slows a walk to rest in two updates and restarts it at once at the walking speed
  ([Characters](characters.md#ai-locomotion)).

#### The move-to-human action (MoveMelee) {#move-melee}

Type 2, `MoveMelee` (vtable `0x005431e0`), made by `MoveToHumanAction_Init(near, far, action, brain, human, delay,
limit)` (`0x002fcf50`): the fight's footwork, which keeps a distance band from one human. The fight goal's approach,
[the reposition](#fight-reposition), Spectate and Backoff use it. Confirmed (code) at the addresses cited.

Fields: `+0x0c` the human's handle, `+0x10` / `+0x14` near² / far², `+0x18` the side counter, `+0x1c` the radial
counter, `+0x20` the time limit (made absolute at Start), `+0x24` "no stance" (class `+0x11b` 6 or 7,
`Brain_IsKind6Or7` `0x002fd128`), `+0x25` the saved turn boost.

- **Start** (`0x002fcff0`): the limit becomes now + limit; a "no stance" human leaves the fight stance; the turn
  boost is saved. **Abort** (`0x002fd068`, never refused): a "no stance" human turns to face the human; the turn
  boost is restored.
- **Update** (`0x002fd4e8`), each update, with `d²` the squared distance:
    1. Done (2) while the mover is in a standing reaction, or when the human is no longer up.
    2. With the radial counter at 0 and the limit not passed: `d²` < near² → counter −10 (back off); `d²` > far² →
       +10 (close in), and when also within the mover's far melee range (`+0x140`) the turn boost is raised by one
       (with conditions on the two facings, the human's state and held objects).
    3. With the side counter at 0 and the limit not passed, the side (`MoveMeleeAction_PickSide` `0x002fd158`): 2 →
       side counter +10 (circle one way), 6 → −10 (the other way), both clearing the radial counter; 8 → none.
    4. The move direction: radial counter > 0 → toward the human, gait 4 (run; 2 for "no stance"); < 0 → away
       from him, gait 2; each update the counter steps one toward 0. A side counter adds ±1 × the mover's right
       vector (gait at least 2) and steps toward 0 likewise.
    5. **Done** when `d²` lies in the band, the side counter is 0 and |radial counter| < 6. After the time limit,
       done once both counters are 0.
    6. Brain `+0x110` = the heading of the move for a "no stance" human; otherwise he keeps facing the human while
       he steps (inferred from the vectors: the stance walk strafes). The aim point `+0x90` = his position + 2 × the
       move direction, radius 0; brain `+0x11c` (strafe) = not "no stance"; the gait goes to `Brain_SetMoveGait`.
- **The side** (`0x002fd158`, from the mover's [sector record](#neighbour-sectors), at most 1 s old): keep circling
  while the sector the mover is heading into is free; else prefer whichever of sectors 2 and 6 (beside the human) is
  free and less crowded (`Sectors_GetCost`: flags + 2 × humans); with both free, the side by the human's flag
  `0x10000`. When the two sides cost the same: straight in (8), unless the human is grabbed from the rear or grabbing
  from the rear; then, when the human is a threat, circle toward his back (sector 4, or 0 for a rear grab), raising
  the turn boost by one when the mover is one of his active attackers.

So a waiting attacker backs off over 10 updates when too close, runs in over 10 when too far, and circles 10 updates
at a time to a free side, facing his man.

#### Path planning {#path-planning}

The route request (`0x0029a8c0`), with its state at brain `+0xe0` (`+0x04` the route, `+0x08` the waypoint radius,
`+0x0c` a state, `+0x10` the waypoint index, `+0x14` an edge mask). Confirmed (code). It plans over the level's
[path data](level-loading.md#path-data): the **polygons** are the walkable areas, the **C records** the graph's
**nodes** and the **D records** its **edges**. A D record on node N naming node M is the step **M → N** (A\* expands
from the destination, and the follower reads the link from the waypoint before on the waypoint's own records,
`0x00251070`); inferred from both readers. Most links come in pairs, but a jump down is one-way (`level99`: node 184
on the ground holds a kind-4 link from 395 on a roof 4 m up, and 395 none back).

**The edge mask** (`u16` route state `+0x14`, brain `+0xf4`) is **`0xff`** for every human: the route state's
initialiser (`0x0029a388`) sets it, nothing in the move action or `GoalMoveToFlag` changes it, and the goals that do
change it put `0xff` back in their End. So a scripted move admits every link kind on the disc (1, 2, 4, 8, `0x10`,
`0x80`); only `0x100` is outside it. Confirmed (code) at the writers, and at runtime
([Vermin's fence](#route-follow)). The other writers: `0x13` in the Start of the goals at `0x002aafd0` and
`0x002c1470`, `0xbf` (no `0x40`) after a failed move in `0x002ab4b8`, and `| 0x100` for brain type 3 (`0x002cc908`)
and for the formation in four levels (`FollowFormationGoal_Start`, `0x002dfe30`).

1. Find the human's polygon (`0x00247958`, cached at human `+0x1b4`; fallbacks `0x002505b0`, `0x0024e218`). None →
   `+0x284` = 1, fail.
2. **Straight line first**: when the walkable-line test (`0x0024fbf8`, below) passes from the human's position
   (`+0x2b0`, at its own height) to the point with the mask 0, no route is made and the action steers straight
   (`0x0029a8c0`, the route request). Confirmed (code).
3. Otherwise both ends must be in one polygon, or both polygons must be on the graph (polygon `s16 +0x02` ≠ 0;
   inferred meaning). Each end's node is the nearest of its polygon's nodes (the A record's count and first index)
   that it reaches in a straight line (`0x00251150` → `0x00250e98`, `0x0024f290`), trying up to 30 by distance.
4. **A\*** (`0x00251a70`), from the destination's node to the start's, so the parent links read in walking order:
    - the open list is a binary min-heap on f; costs are `u16` in 1/16 m; the heuristic is the straight distance × 16
      (`0x002517b0`);
    - an edge is skipped when its flags (the low 16 bits of the D record's word) share nothing with the mask, or its
      cost reaches 65000 (`0x005105a4`); the search fails on an empty heap or at 128 nodes;
    - **edge cost** (`0x00251890`) = distance × 16, + 200 for edge flag 8 (the D record's kind bit, not path flag
      8), + 320 for `0x80` and + 80 for 4 (these three
      only when `0x0051059c` = 1 and the mask lacks `0x100`), + 1600 when the word's bit 31 is set, and + (40 × routes
      already through the node − 8), saturating at `0xffff`. The last term spreads AIs over parallel routes.
      `0x0051059c` is the script's `ClimbFilter(on)` (`Climb_SetFilter`, `0x002511b8`).
5. `0x002511c8` retries a failed search with `mask | 0x8c` only when the mask (without `0x100`) is not `0xff`, so
   never for the default mask; when the route uses a `0x80` edge (under the condition of the extra costs) it
   searches again without `0x80`, capped at the first cost, and keeps the cheaper route.
6. **Building** (`0x002513a8`; a pool of 32 routes of `0x110` bytes at `0x006ca250`, a count then `s16` node
   indices): leading nodes the start reaches directly are skipped (only when `0x005105a0` = 0), the chain is shortened
   by looking up to 4 nodes ahead for one that links back, trailing nodes are dropped while the destination is
   reachable from the node before, and each node's use count (C `+0x1f`) is raised; freeing the route
   (`0x00251680`) lowers it.

The polygons come in **areas**, an outline and its holes ([Path data](level-loading.md#path-data)); the human's
polygon and a node's polygon are an area's first path. **Path flag 8** (`u16` at polygon `+0x48`, bit 3) takes a
polygon out of every test below, so an opened door's hole stops cutting the area. Confirmed (code) at the addresses.

- **The walkable-line test** (`0x0024fbf8`, arguments: the start's polygon, the two points, a polygon-flag mask). It
  reads no collision geometry: only the hazard spheres below and the path polygons. Confirmed (code):
    1. Refused when a **hazard sphere** blocks the segment (`0x00221f80`, below).
    2. It crosses the segment with every edge of the start area's polygons (the outline and its holes, the list from
       polygon `+0x20`) whose `u16` flags at `+0x48` have neither 8 nor any bit of the mask and whose box meets the
       segment's. The crossing is in plan (`0x0024e938`: x and y only, z is interpolated), at segment parameter t in
       [0, 1], and a crossing at exactly t = 1 is ignored. Edges come from the slab lists when the polygon has them,
       else all. It keeps the **nearest** crossing. No such polygon at all → refused.
    3. No crossing: it passes when the end point's area (`0x00250708`) is the start's.
    4. A crossing: the end point needs an area. Its polygons are crossed the same way, keeping the **farthest**
       crossing, and the line passes only when the two crossing points are within 0.02 m (dist² < 0.0004): the
       segment leaves one area where it enters the next. Any edge in between, a hole's included, refuses the line.
- **What blocks a line at a fence** is the fence's **hole** in the path polygons, not its collision. Confirmed
  (runtime), from the path data of a `level99` state at checkpoint 3 (PCSX2 2.9.94, over the straight line Vermin's
  `GoalMoveToFlag` asks for, (47.49, 42.97) to (46.30, 20.22); both ends lie in the street's area, whose outline is
  polygon 0): the segment crosses two holes of that area, a 4-vertex hole of flags 7 spanning x 44.14 to 50.61 and
  y 24.38 to 25.19 (the climbable fence, 6.5 × 0.8 m) at t = 0.78 and 0.82, and the hole of fence door 2 (x 44.93 to
  49.50, y 30.79 to 31.56, flags 7) at t = 0.50 and 0.54. The nearest and farthest crossings are metres apart, so the
  line is refused and a route is made ([Vermin's fence](#route-follow)). An opened door's hole gets flag 8
  ([Objects](objects.md#nav-links)) and drops out; the fence's never does. A test that treats the area as the union
  of its polygons (a hole counted as walkable) lets the line through.
- **The hazard spheres** (`0x00221f80`, confirmed (code)): 64 records of `0x40` bytes at `0x0065ff50`, `+0x00` the
  centre, `+0x10` / `+0x20` its box (min / max), `+0x30` (r + 0.3)², `+0x34` (r + 0.8)², `+0x38` a word (0 from the
  only adder), `+0x3c` in use; their count is `0x00510170`, cleared at level start (`0x00217f68`). The test runs only
  when the count is non-zero and `0x0051057c` is non-zero (1 in the ELF's data; no code writes it). A sphere blocks
  the segment P → Q when its box meets the segment's box (x, y and z), the segment passes within r + 0.8 m of the
  centre in 3D (`0x00336d28`, squared distance to the nearest point of the segment), and the segment heads at it:
  (Q − P)/|Q − P| · (P − C)/|P − C| < −0.707 (within 45°). The points are tested at their own heights; there is no
  lift above the ground. `0x002198b8(r, word, centre)` adds one (and marks the route links within r of it to be
  avoided, `0x00253078` → `0x00252c90`: bit 31 of every D record whose edge passes within r + 0.3, counted so overlapping
  spheres release it only once), and `0x002199c0` removes one. The only adder is `0x003a5a90`, called by the fire
  particle types (`0x003c40a0`, and `0x003c8b28` from `0x003ca050`, [Particles](particles.md)) with r random in 0.5 to
  0.75 m: an AI's straight line refuses to run into a fire. In the checkpoint 3 state the count was 0, so
  `0x00221f80` returned 0 at once. Confirmed (runtime) for the count; that fires are the only source is confirmed
  (code) from the single call site.
- **A node in a straight line** (`0x0024f290`, the end-node search's test): refused when a hazard sphere blocks it
  (`0x00221f80`) or when the segment crosses an edge of any of the area's polygons without flag 8 or the mask (0
  there).
- **A point's area** (`0x00250708`): `0x00250760` takes the first area, by the area list (`+0x24`), whose first path
  lacks flag 8, whose `+0x4a` equals the byte of the ground found by a ray down from 0.4 m above the point, and whose
  box (+0.35 m) holds it; then the inside test `0x0024ea60` over its polygons, skipping flags 8 and `0x10`.

`0x00251d28` is a second A* search that stops at the first node beyond a distance outside a cone of
directions, skipping bit-31 edges; its one caller places spawned humans out of sight ([The search](#spawner-search)).

#### Following a route {#route-follow}

`0x0029aa88` gives the current waypoint (done at state 5). Every 6th call, or when asked, `0x0029b6d8` moves on: it
skips waypoints already reachable in a straight line (`0x0029b4b8`), for legs over 5 m only when the turn against the
previous leg is under about 135°, and sets the waypoint radius to 0.25 m; at the end it frees the route. Confirmed
(code).

**When the straight line is tested** (confirmed (code); each call is the walkable-line test of
[Path planning](#path-planning) with the mask 0):

- **Asking for a route** (`0x0029a8c0`): from the human's position to the point; passing means no route at all.
- **Skipping a waypoint** (`0x0029b4b8`, every 6th update and when `0x0029b6d8` moves on): waypoint *i* + 1 is tried
  from where the human stands (`0x002221e0`), and only when the links into waypoint *i* and out of it are both of
  kind 1 or 2 (word & 3); the first waypoint counts as reached by a kind-1 link. So a waypoint at either end of a
  climb, jump, door or charge link is never skipped, and the human walks to it whatever the line test says.
- **Dropping the route** (the move action's update, every 30 updates, [The move action](#move-action)): when the
  final point passes `0x002221e0`, the route is freed and the human goes straight.
- **Building the route** (`0x002513a8`, calls at `0x00251494` and `0x002515e8`): leading nodes the start reaches,
  and trailing nodes from which the destination is reached, are dropped.

`0x002221e0` first asks the trains: unless brain `+0x2e0` equals the current game time (`*(0x0050b734) + 0x48`),
`Trains_IsPathClear` must pass ([ObjStartTrain](../references/bindings/world.md#objstarttrain)); then the
walkable-line test from the human's polygon (`0x00247958`) and its position in the position table (`0x00714b00`).

**Link kinds.** A leg's kind is the D record that steps from the waypoint before to the current one (`0x00251070`);
the follower reads it on the update after `0x0029b6d8` moves on (route state `+0x12` = 1), at waypoint index > 0.
Confirmed (code) at `0x0029ad04`-`0x0029adb8`:

| Leg's kind | What the follower does | Handler |
| --- | --- | --- |
| 8 or `0x80` | a **climb** (8 a fence or wall, `0x80` the same taken at a run) | `0x0029b848` |
| 4 | a **jump** (route state 1) or a **drop** off the edge (state 2), [Jump legs](#route-jump) | `0x0029baa8` |
| any, avoid bit (31) set, `0x40` | the **charge** at a breakable door or pane ([Objects](objects.md#nav-links)) | `0x0029bca0` |
| any other, avoid bit set | refused: brain `+0x284` = 4 for kind `0x10` (a closed door), else 2; the move ends | |
| 1, 2, `0x10` (an open door) | walked | |

The **climb** (`0x0029b848`): each update it aims at the waypoint (brain `+0x90`, heading `+0x110`, `+0x11c` = 1),
turns the body to it (`0x0021b100`) and calls `Climb_TryStart` (`0x002826f0`), the player's own climb
([Characters: Climbing](characters.md#climb)): the forward probes must find a climbable face. On success the route
state `+0x0c` = 3 (kind 8) or 4 (kind `0x80`), brain `+0x11d` = 1, and the clips play; otherwise it runs on at gait 4
(`0x0028aac0`) and tries again, giving up (move done) after 31 failed updates (counter `+0x13`, cleared when the
waypoint changes). When the **next** leg is 8 or `0x80`, a human with flag `0x2` (human `+0xe0`, a fast climber,
`HuSetFastClimber`) running faster than gait 3 starts the climb early, within 4.5 m of the waypoint (`0x0029b9b0`).

Before a leg of kind 4 or `0x80`, with the detail level `+0x333` < 2, the link must be clear: another human on it
(`0x0029a3f0`, `0x0029a6c8`, [Jump legs](#route-jump)) makes this one hold with speed 0 when within 2 m of the
waypoint. Every waypoint is
also claimed in a table of 40 `{node, human}` at `0x006ce978` (`0x00293e40`): the nearer human keeps it and the
other is told to wait (`0x00294088`); a kind-4 link with a queue (`0x002510f8`) goes to one of the 20 queue records
at `0x006cde30` instead ([Queues](#queues)). Confirmed (code); the queue's waiting is inferred.

**Confirmed (runtime)**, PCSX2 2.9.94, `level99` reloaded at checkpoint 3 (call hook, [below](#level99-save)) with
no input: Vermin (brain 5), set at (47.49, 42.97) by the script's `GoalMoveToFlag(Vermin, fVerminFencePoizo, 4, -1,
-1, 0.5, 0, true)`, planned with mask `0xff` a route of two nodes, 205 (46.29, 25.29) then 204 (46.29, 24.29), linked
by kind 8 (the leading nodes 137, 208 and 209 cut, the fence door 2 between 208 and 209 being open: its links'
avoid bit, set in the file, is cleared while the level loads). He ran straight, entered route state 3 at y = 26.6,
crossed the fence in 38 updates (route state 3, waypoint 1) and left it at y = 22.3 with the waypoint index at 2,
then ran on to (46.31, 20.18) and stopped. Over the graph read from that state, any mask without 8 (`0x3`, `0x13`,
`0x93`) finds no route between the two points: the yard behind the fence is reached only over its kind-8 links.

#### Jump legs {#route-jump}

A kind-4 link crosses a gap or drops down an edge. The follower reaches the take-off waypoint like any other, starts
the jump (or the drop) on the update after, and leaves the air to the landing code. Confirmed (code) at the addresses
cited, and at runtime where marked.

**Where it starts.** There is no edge test and no distance of its own: the jump starts on the update after the
follower moves on from the take-off waypoint to the landing one. Moving on is the usual arrival test, which the
human's state update runs every update, in the air too (`0x0023f9d0`, from `Human_StateUpdate` `0x0023fea8`): while
the front action is a move action, brain `+0x11d` is set when the distance to the aim point (brain `+0x90`) less one
update's travel (human speed `+0x1ac` / 30) is within the aim radius (brain `+0x118`); the distance is in 3D, or in
plan when the 3D distance is 1 m or less. The move action then asks the follower to move on (`0x002fc674`), and
`0x0029b6d8` makes the landing waypoint current with the radius 0.25 m and route state `+0x12` = 1 (the leg is new).
The take-off waypoint is never skipped (its outgoing link is not of kind 1 or 2), and moving on over a kind-4 link
keeps the waypoint claims (`0x00294088` is not called). So the jump starts within 0.25 m plus one update's travel of
the take-off point.

**The two points** (`0x0029b2b8`). Nodes whose C record byte `+0x1e` is non-zero come in pairs: `0x002510f8` finds the
linked node with the same byte, and the pair is an edge to jump from or to.

- **Take-off** (a waypoint whose outgoing link is kind 4): the human's point in the queue on its edge
  (`0x002941c0`), when a queue holds the node and the human and he can walk there in a straight line (`0x002221e0`);
  otherwise the node. The claim makes the queue on demand (`0x00293e40` → `0x00293d08`): `0x00299538` lays
  trunc(edge length) + 1 points, at most 6, evenly from the partner node to this one, and the queue update gives each
  human the nearest free point (`0x00299b50`). A lone human so takes off from the nearest of those points.
- **Landing** (the waypoint the kind-4 link enters): with a partner node, the take-off point projected onto the line
  through the two nodes (`0x00336e08`, not clamped to the segment); while the landing waypoint is current, the
  take-off point is the human's own position. Otherwise the node. So the jump goes square across the landing edge.

**The handler** (`0x0029baa8`), run by the follower on every update while the leg is new:

1. Let *d* be the landing point less the human's position and *D* its length in plan. Aim at the point (brain
   `+0x90`, radius `+0x118` = the waypoint radius), set the heading `+0x110` along *d* in plan, clear `+0x11c`, and
   turn the body to that heading (`0x0021b100`).
2. **Drop or jump.** With the link's avoid bit (31) set, jump. Otherwise *t* = `0x003378b0`(7.84, 0, *d*.z) (below):
   when *t* > 0 and *D* / *t* < 10, **drop**: brain speed `+0x114` = *D* / *t*, route state 2. Otherwise **jump**:
   `Human_BeginJump(human, 1)` (below); refused → the handler returns 2 and the move action ends (`GoalMoveToFlag`
   queues another and plans again); accepted → route state 1.
3. It returns 0, so the move action does nothing else that update: no steering round humans, no corner speed.

**`0x003378b0(a, b, c)`** returns the larger real root of *a t*² + *b t* + *c* = 0, or 0 when there is none. 7.84 is
half the fall's gravity (15.68 m/s², `Human_StateUpdate`), so with (7.84, 0, *d*.z) it is √(−*d*.z / 7.84): the time
to fall from rest to the landing point's height, real only when the point is below. The **drop** therefore runs at
the speed that covers the remaining plan distance in that time, recomputed each update until the human leaves the
ground (the follower then stops running, below), so it slows as it nears the edge and leaves it at the speed that
lands it on the point (inferred from the formula). Over 10 m/s, or with the landing point level or higher, it jumps
instead. A drop of more than about 7.1 m lands faster than the fall-damage threshold of 14.9 m/s
([Characters: Falling](characters.md#falling)).

**`Human_BeginJump` for an AI** (`0x0023db48`): with the argument 1 it refuses while record `+0x08` holds any of
`0x5cfeafb` or the state flags any of `0x7bf9e9f7ff0` (airborne and landing among them). The gait (3 or more) and
3.3 m/s tests apply only to a brain of type 0, the player: an AI jumps at any gait, from a standstill too, and its
take-off gait `+0x3c0` is not written. No run-up or lining up is needed; the handler has already turned the body.

**The launch** (`Human_LaunchJump`, `0x002217f0`, on the same update). For a human whose per-player record `+0x1b` is
0 it takes the velocity from `0x0029ade0` instead of the player's run speed ([Characters: Jumping](characters.md#jump)):

- route state 1: turn the body to the landing point again; try the vertical speeds *v* = 0.5, 1.25, 2.0, … m/s
  (steps of 0.75) and, for each, the time *t* at which the arc *v t* − 7.84 *t*² comes down to *d*.z (the larger
  root of −7.84 *t*² + *v t* − *d*.z = 0); keep the first *v* for which *t* > 0 and *D* / *t* ≤ 10 m/s, giving up
  after *v* passes 5.5 m/s (`0x00510188`, so at most 5.75). The velocity is *D* / *t* along *d* in plan and *v*
  upwards: an arc that ends on the landing point;
- any other route state: route state 5 (the route ends) and the velocity is left as it was.

Then the fall starts and anim state 26 (clip 434), as for the player.

**In the air** the follower does not run: `MoveAction_Update` returns 0 at once while `Human_IsBusy` holds, and it
holds while any of the state flags `0x1c00000000` (jumping, falling) is set (`0x00227f90`). The arrival test above
still runs, so `+0x11d` is set in the air when the arc passes within 0.25 m plus one update's travel of the landing
point.

**Landing** (`Human_Land`, `0x0023e090`), for an AI whose front action is a move action (and unless `0x0015e718`
reports mode `0x11`):

- **Near**: a route in state 1 or 2 and the human within 1.5 m (3D) of the current waypoint (`0x0029b170`): the actions
  run again at once with the busy test off (`0x005112b4` = 1 around `Brain_RunActions`). With `+0x11d` set the move
  action moves on to the next waypoint and its leg; without it the leg is still new and the handler runs again from
  where the human stands (inferred: another jump when `Human_BeginJump` allows it, else the move ends and is planned
  again).
- **Otherwise** (no route, another route state, or farther than 1.5 m): `Brain_PopAction(brain, 1)` ends the move
  action; `GoalMoveToFlag` then queues a new move from where the human landed, which plans a new route. This applies
  to any landing during a move, a fall off an edge included.

**The clear test, claims and queue for a lone human.** The clear test (`0x0029a3f0`, before a leg of kind 4, with
the detail level `+0x333` < 2) looks only at other humans: one within 4 m in plan and ahead of this one (along its facing),
among those `0x002278a8` gathers along the leg and `0x00290230` accepts, blocks the link when it is moving (a move
action and a speed above 0); one standing is pushed aside (`0x0028a248`) and the link counts as blocked for that
update. Blocked and within 2 m (3D) of the waypoint, the human holds there with speed 0. The claim (`0x00293e40`) is
refused while another human holds the node (the nearer of the two takes it, for the next update). With no other
human near, neither changes anything; the queue still picks the take-off point (above).

**Confirmed (runtime)**, PCSX2 2.9.94, `level99` checkpoint 3.4 (the rooftops), from the window-jump state of
[Objects](objects.md#pane) with the player run at the window (stick 100 % ahead, triangle at frame 59) so that
`vReachWindow` sends Vermin to `fStop_03`: Vermin ran from (62.80, −3.30) at 7.80 m/s to the take-off point
(50.744, −2.830, 4.252), set the arrival flag 0.004 m from it, and on the next update had route state 1, waypoint 1,
the aim (46.619, −2.830, 4.252) (the landing point, square across from him: its y is his), the heading 3π/2 and clip
434. The launch gave *v* = 3.5 m/s (0.5, 1.25, 2.0 and 2.75 need more than 10 m/s) and 9.546 m/s in plan (0.318 m
per update), as the arc above gives for *D* = 4.121 m and *d*.z = +0.05 (*t* = 0.432 s). The arc stopped against the
window at x = 47.76, where the two type-11 panes stand (x 47.40); he fell 4 m into the gap, landed 4.2 m from the
waypoint, and the move action ended on that update; nine updates later a new route took him along the street and up
the fence (route state 3 at (56.78, 7.24)). Putting the player in `vReachWindow` without his own jump gave the same
jump and fall.

#### Neighbour sectors {#neighbour-sectors}

Each human has a record of who stands round him in eight 45° sectors. The fight's footwork
([MoveMelee](#move-melee)), the grab and snap tries, [the steering](#steering) and giving way all read it.
Confirmed (code) at the addresses cited, except where marked. Code index: [Neighbour sectors](ai-code.md#sectors).

**The record**: 60 records of 0x48 bytes at `0x006e8318`, one per human slot (`Sectors_GetOwner` `0x0029e470`
maps back). Per sector *k* (0-7), 8 bytes at `+8k`:

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0` | handle | the **nearest human** in the sector (the null handle when none) |
| `+4` | u16 | **flags**: 1 someone within 1.5 m, 2 someone very close (below), 4 no walkable line out (a wall), 8 a free player near |
| `+6` | u8 | 1 while the wall probe has still to run |
| `+7` | u8 | **how many** humans stand in the sector within 1.5 m |

Then `+0x40` holds the owner's heading at the last refresh and `+0x44` that time (ms). The distance is not kept: it
exists only while the record is rebuilt. `Sectors_Construct` (`0x0029e250`) clears the handles. `Sectors_Reset`
(`0x0029e2b0`, also called from `Brain_Init`) zeroes the heading and time and sets every probe byte.

**Sector numbering** (`Sectors_SectorOf` `0x0029eb10` → `Human_GetSectorOf` `0x0029eb38`): take the bearing of the
point from the owner minus the owner's heading, wrapped to [0, 2π). **Sector *k* is centred on *k* × 45°**, with
edges at ±22.5° (0.3927 rad), so 0 is straight ahead. A heading grows to the left ([Combat: axes](combat.md#grab)),
so the index rises **anticlockwise** seen from above: 2 is his left, 4 behind, 6 his right. A bearing exactly on an
edge goes to the lower index on the left half (0 to π) and to the higher index on the right half.

**Refreshing** (`Brain_GetSectors(brain, maxAge)` `0x0028fe90` → `Sectors_Update` `0x0029e5d8`): the record is
rebuilt when now ≥ `+0x44` + maxAge; otherwise it is returned as it is. Callers pass 1000 ms (MoveMelee, the
steering, the fight goal's re-target and grab tries) or 500 ms (giving way). A rebuild:

1. Stores the owner's current heading at `+0x40` and gives every sector the null handle, count 0 and probe byte 1.
2. Lists up to 60 humans within **1.5 m** of the owner, in every direction, each with his **squared** distance
   (`Humans_FindAhead(1.5, …, mask 1)` `0x002274a8`). Despite its name this is a plain radius search round a point.
   It takes every human slot whose word at `0x00715390` has bit 1 set, except the owner. There is no state check,
   so downed and busy humans count.
3. Each listed human raises his sector's count by one. The nearest in each sector becomes its handle.
4. The flags are rewritten from the nearest human's squared distance *d²*: *d²* < 1.5 → **3** (bits 1 and 2,
   within 1.22 m); otherwise *d²* < 2.5 → **1**. Every listed human is within 1.5 m (*d²* ≤ 2.25), so in practice:
    - bit 1 means "someone in the sector";
    - bit 2 means "someone within 1.22 m";
    - an empty sector gets 0, which also clears bit 4.

   (The thresholds look like metres compared against a squared distance. What is described here is what the code
   does.)
5. **Bit 8** is set only when the owner is an AI (human `+0x1b0` = −1). It marks a nearby player who is not already
   part of this fight (inferred meaning). Each player human is checked (game state `+0x228`, count `+0x224`). Bit 8
   is set in the player's sector when all of these hold:
    - he is not busy (`Human_IsBusy`);
    - he is within **5.5 m** (30.25 squared);
    - he is not the owner's target (brain `+0x124`) and not the target of the owner's target;
    - his own target is neither the owner nor the owner's target.
6. Sets `+0x44` = now.

**The wall probe** (`Sectors_ProbeWall` `0x0029e4b0`) runs lazily, at most once per sector per rebuild, and only
when a reader needs it (the probe byte is 1):

- **The point**: 1.5 m from the owner's position at heading `+0x40` + *k* × 45°, at the owner's height (the
  direction is flat, inferred from `Vec_FromHeading`).
- **The test**: bit 4 is set when `Human_CanWalkStraightTo(owner, point, 0)` (`0x002221e0`) fails. This is not a
  physics ray. It fails in two cases:
    - a train's path is in the way, unless the owner's brain is already near a train;
    - there is no walkable straight line on the navigation mesh (`Nav_IsWalkableLine` from the owner's polygon,
      [Path planning](#path-planning)).
- **After the probe** the probe byte is 0. Only the next rebuild clears bit 4. `Sectors_ResetHeading` (`0x0029e2e8`,
  used only by BigThrower) re-arms the probes with a new heading but keeps an old bit 4.

**Readers** (`0x0029e980`-`0x0029eb10`):

| Function | Answer |
| --- | --- |
| `Sectors_IsBlocked` `0x0029e9c8` | true at once when bit 2 is set; otherwise probe, then any of bits 2, 4 or 8 (a very close human, a wall or a free player) |
| `Sectors_IsFree` `0x0029ea48` | none of bits 1, 2 or 8; then probe, and no wall |
| `Sectors_IsWall` `0x0029ea10` | probe, then bit 4 |
| `Sectors_GetCost` `0x0029e980` | probe, then flags + 2 × count |
| `Sectors_AllClear` `0x0029eaa0` | no sector has bit 1 or 2 (no human within 1.5 m at all) |
| `Sectors_IsHeldBy` `0x0029ead8` | whether the given human is the nearest in a sector |
| `Sectors_TurnWay` `0x0029ec90` | +1 or −1, the shorter way round from one sector to another |

**A point in a sector** (`Sectors_GetPoint(distance, out, record, k)` `0x0029e350`): the owner's position plus
*distance* along his **current** heading + *k* × 45° (not the stored `+0x40`), at his height (inferred, as for the
probe). The steering passes 1 m ([Steering round humans](#steering), the standing case). The other caller is
`Human_DropCarried` (`0x00232c60`).

**Giving way** (`Brain_GiveWayTo(mover, stander, moverPos, moverStep)` `0x00289ed0`). It is called from
`Brain_PushAside` (`0x0028a248`), which the steering calls for a standing blocker.

1. It acts only when all of these hold:
    - the stander's brain is not a player's (type ≠ 0), or its byte `+9` is set;
    - neither human threatens the other (`0x00222a48`, checked both ways);
    - the stander is idle under AI control (`0x00225390`).
2. Take the point on the mover's line (his position + his step) closest to the stander (`Line_ClosestPoint`
   `0x00336e08`). Let *s* be the stander's sector **of the direction from that point to him**, which points straight
   away from the mover's path.
3. With the stander's record at most 500 ms old, try the sectors in this order (mod 8): *s*, *s* + 1, *s* + 2,
   *s* − 1, *s* + 3, *s* + 4, *s* − 2. *s* − 3 is never tried. The first sector that is `Sectors_IsFree` wins. So
   he prefers to step straight off the path, then leans anticlockwise. (`Sectors_TurnWay` is called with *s* and
   the mover's sector, but its answer is unused.)
4. **A free sector**: clear his actions. When that is refused, report success without moving. Otherwise queue a
   `GiveWay` action (`GiveWayAction_Init` `0x002fe568`) with:
    - the heading of that sector's centre;
    - a start delay of `Random_Int(125)` ms (0-124);
    - the turn-boost flag, set when the mover is a player at gait 3 or more.

   The step is a single `TakeStep` ([Actions](ai-code.md#actions)): he looks at the mover for up to 2 s, then steps
   once on that heading. The step's length is the step clip's; no distance is passed.
5. **No free sector**: go through the candidate sectors in the same order. For each one that has a nearest human,
   ask that human to give way in turn (`Brain_PushAside`, recursively). The first who does wins. If none does, the
   result is nothing (0).

**The blocker test** (`Steering_FindBlocker(state, list, n, myPos, myDir, myStep, outFrac)` `0x00288cc8`) runs over
the list from `Humans_FindAhead`: every human within min(2 `L`, 10 m) of the walker, in any direction
([Steering](#steering)).

1. Skip the walker's target (brain `+0x124`) and anyone behind him: the dot product of the walker's unit move
   direction with the offset to the other human is below 0.
2. Predict the other human's step:
    - a player: his facing × 0.75 × his speed `+0x1ac`;
    - an AI: toward his brain's aim point `+0x90`, of length min(the distance to it, 0.75 × his brain speed
      `+0x114`) (`Steering_ClampStep` `0x00288f40`).
3. Let *r* be the relative step: my step − his. A ray from my position along *r*, of length |*r*|, is tested against
   a **disc of radius 0.63 m** round him (`Steering_RayHitsHuman` `0x00289d68`; 0.3969 = 0.63²). There is no hit
   when:
    - he is farther than |*r*| + 0.63;
    - he lies behind the ray;
    - the ray misses the disc.

   Otherwise the result is the hit distance *t*, measured to the disc's edge.
4. The blocker is the human with the smallest *t* / |*r*|. That fraction is also returned. So the test sweeps a circle
   along the relative motion over one step; it is not a corridor or a cone.

#### Steering round humans {#steering}

`Human_SteerAroundHumans` (`0x00289138`) bends a [move action](#move-action)'s walk round the one human most in the
way. `MoveAction_Update` (`0x002fc5c0`) calls it every update with the steering state (brain `+0xa0`), the move's
speed for this update, its **aim point** (the next path point, in and out), its destination (action `+0x10`) and its
arrival radius (action `+0x2c`). Confirmed (code) from the disassembly of `0x00289138`-`0x00289d60` (the decompiler
drops several arguments) unless marked; not checked at run time. The names of the cases are ours.

**The state** (brain `+0xa0`; helpers on [AI code: steering](ai-code.md#brain-steering)):

| Offset | What |
| --- | --- |
| `+0x00` | the **held point**: the point stored by the last successful detour, used as the aim between decisions |
| `+0x10` | the predicted **contact point** of the last decision (C, step 10) |
| `+0x20` | the brain |
| `+0x24` | the human being avoided (non-zero = avoiding; `Steering_SetAvoiding` also raises the brain's turn boost) |
| `+0x28` | u8, updates since the last decision; `Steering_Clear` sets 255, which forces a decision on the next update |
| `+0x29` | u8, slow decisions in a row (step 5) |
| `+0x2a` | enabled |
| `+0x2b` | u8, decisions since the detour was taken (step 6) |
| `+0x32` / `+0x34` | u16 updates left / the **speed override** (`Steering_SetSpeed(speed, n)`) |
| `+0x3c` | the detour's **score** (below) |

**Speed overrides.** `Steering_SetSpeed` only stores a speed and a count of updates; the count drops by one at the
start of every call. While it is non-zero the move action takes `+0x34` as its speed instead of its own (action
`+0x40`, read at `0x002fc778`), and the brain's move speed `+0x114` follows from that (`Brain_SetMoveSpeed`). So an
override acts from the next update on, for n updates.

**One call**, in order (P my position, s my speed, human `+0x1ac`):

1. Not enabled, or brain `+0xec` set (inferred: held in a [waypoint queue](#queues)) → return 0. Human `+0x333` above
   2 → avoidance off, return 0.
2. **Between decisions** (the counter `+0x28` below 3 × human `+0x333`; with `+0x333` = 0 it decides every update):
   when avoiding, aim := held point and return 1, else return 0.
3. **A decision**: counter 0, score := −1e9. When avoiding the arrival radius counts as 0, and if s is a standing
   gait (gait 0, below 0.5 m/s) aim := held point and return 1.
4. Within 1 m of the destination (squared distance ≤ 1) → return 0.
5. **Look-ahead.** L = 0.75 × the move's speed (the distance covered in 0.75 s). While that speed is below 4 the
   counter `+0x29` counts up, and on the 16th such decision in a row L is × 10 once and the counter restarts; at 4 or
   more it resets. `Humans_FindAhead` (`0x002274a8`) lists up to 60 humans within **min(2L, 10) m** of P, in every
   direction, not counting me.
6. **Expiry.** When avoiding, `+0x2b` counts decisions: while it is below 4 × ⌊11 − s⌋ the walk heads for the held
   point instead of the aim; after that `Steering_Clear` ends the detour.
7. **My step.** D = the unit direction from P to that point, in plan (z = 0); S = D × min(L, its distance − the
   arrival radius).
8. **The blocker.** `Steering_FindBlocker` (`0x00288cc8`): of the listed humans, skipping my target (brain `+0x124`)
   and anyone behind (D · (B − P) < 0), the one whose 0.63 m disc (`0x00289d68`) a ray from P along the relative step
   (S minus his predicted step) hits first. His predicted step is his facing × 0.75 × his speed when his brain
   `+0x04` is 0 (inferred: a player), else `Steering_ClampStep(0.75)` toward his aim (below). It returns him and
   **t** = the hit distance ÷ the relative step's length: the fraction of the 0.75 s step at which the two touch
   (1e9 for none; [the ray test](#neighbour-sectors)). No blocker → when avoiding aim := held point and return 1, else
   return 0.
9. When I lead a formation (human `+0x1a4`) and he is one of its followers, his own steering gets the gait-0 speed for
   5 updates (`Steering_SetGait`, so he stops); this call goes on.
10. **Keep the old detour?** When avoiding and the stored contact point `+0x10` is nearer to P than the new one
    (|t × S|), aim := held point and return 1. Otherwise `+0x10` := **C = P + t × S**, where I would meet him.
11. **Vectors**, all in plan and of unit length unless said: B his position; Q his predicted point, B + his facing
    (`Quat_AxisY`, 1 m) when his brain `+0x04` is 0, else his brain's aim point `+0x90`; N = (B − P) normalised, the
    bearing to him; H = (Q − B) normalised, his heading; **E = D × my up axis** (`Quat_AxisZ`), that is (Dy, −Dx):
    perpendicular to my move, 1 m long. Two dot products choose the case: **b = D · N** (how straight ahead he is)
    and **h = D · H** (how alike our headings are), against **cos 30°, cos 50° and cos 45°**.
12. **Right of way**: I have it when s is above his speed (human `+0x1ac`), or equal and my human's address is the
    lower.
13. **Standing** (case 0): his brain `+0x04` is 0 and his speed below 0.05; or his brain's move speed `+0x114` is ≤ 0;
    or his steering override is active (`+0x32` ≠ 0) with a speed ≤ 0.
14. **The case** otherwise, tested in this order:

    | b (bearing) | h (headings) | Case |
    | --- | --- | --- |
    | ≥ cos 30° | < −cos 45° (coming at me) | 1, head-on |
    | ≥ cos 30° | > cos 45° (same way) | 2, overtake, only with right of way; without it return 0 (follow) |
    | cos 50° to cos 30° | \|h\| > cos 45° | 2 |
    | ≥ cos 50° | \|h\| ≤ cos 45° (crossing) | 3, crossing |
    | < cos 50° | ≥ cos 45° (same way, off to the side) | none: without right of way the speed is 0.75 × s for 15 updates; return 0 |
    | < cos 50° | < cos 45° | 3 |

15. **Same route node** (cases 0-3): when both brains follow a route (the route state at brain `+0xe0`: `+0x04`
    non-zero for both, and my `+0x16` set) and `RouteState_CurrentNode` (`0x0029b248`) is the same node for both,
    the speed is **0.75 × his speed** for 5 updates; return 1, aim unchanged.
16. **Yielding** (cases 1-3): without right of way and with the contact within 3 m (|t × S|² < 9), cases 1 and 3 stop
    (speed 0 for 1 update) and case 2 matches his speed for 5 updates; return 1, aim unchanged.
17. **The detour point X**:
    - **Case 0** (he stands): X = B + σE, σ = −1 when E · N ≥ 0, else +1: **1 m from his centre, perpendicular to
      my move, on the side of him that my line passes**. aim := X and `Steering_TryDetour(X)`; on success return 1.
      Otherwise k = `Sectors_SectorOf(my position, X)` (one of 8 sectors of 45°; inferred: X's sector),
      k' = (k + 4) mod 8, the opposite one, and X' = his position + 1 m along his facing + k' × 45°
      (`Sectors_GetPoint`; his sector record refreshed when older than 1,000 ms). `TryDetour(X')`, and on success
      aim := X'. Return 1 either way (the aim stays X when both fail).
    - **Cases 1 and 2**: X = C + σρE. ρ = +1 when B is on E's side of my line (`Steering_SideSign`, E · (B − P) ≥ 0),
      else −1; σ = +1 when my segment P → aim crosses his B → Q (`Segment_Intersect2D`, `0x00337308`), else −1. So
      **1 m sideways from where we would meet**, away from his side, or towards it when our paths cross (he is
      moving over to the other side).
    - **Case 3**: X = B + `Steering_ClampStep(0.75 t)` + σE, σ = −1 when H · E ≥ 0, else +1. `Steering_ClampStep(f)`
      (`0x00288f40`) is his unit direction to his aim (his brain `+0x90`) × min(his brain `+0x114` × f, his distance
      to it). So **where he will be when we meet, then 1 m to the side he comes from** (behind him).
18. **Corner check** (cases 1-3): when s minus `Move_CornerSpeedLimit` (`0x002fcd90`: the highest speed, stepping
    the gait down, that still turns through the circle from P along my facing, `Quat_AxisX`, to X) is more than 1,
    the speed is 0.75 × s for 1 update; return 1, no detour.
19. Otherwise `Steering_TryDetour` (`0x00289010`): when the human can walk straight to the point
    (`Human_CanWalkStraightTo`) and `Ground_ProbeBelow` does not return 1, avoiding := the blocker, score := t and
    held point := the point; then aim := X. Return 1 either way. **Note:** in cases 1-3 the point checked and held is
    the **aim the move passed in**, not X (`a2` is the aim at `0x00289cfc`), so X steers only the deciding update and
    the human then heads for the held aim until the next decision; case 0 checks and holds its own point. Inferred to
    be a slip in the original; a faithful port keeps it.

**The score `+0x3c`** is t, the fraction of the 0.75 s step at which the two would touch (smaller is sooner). It is
−1e9 at every decision and after `Steering_Clear`, t once a detour is taken, and 0 after `Steering_EndAvoid`. Nothing
in this function or its helpers reads it: a new detour is weighed against the held one by the contact points'
distances (step 10). Who reads it is not traced.

**The return value**: 0 = no steering, the move keeps its own aim; 1 = steering, and the aim may have changed. On 1
the move action recomputes its direction to the aim and uses an arrival radius of **0.3 m** (brain `+0x118`) for
this update instead of its own; either way the aim goes to brain `+0x90` (`0x002fc9a0`-`0x002fca98`).

`Brain_GiveWayTo` (`0x00289ed0`) and `Brain_PushAside` (`0x0028a248`) are the standing side of it: an AI standing in
a mover's way steps into a free 45° sector round it ([Giving way](#neighbour-sectors)).

#### Giving way {#giving-way}

A standing AI in a mover's way steps aside one sector (45°) with a **GiveWay** action. Confirmed (code) at
`Brain_GiveWayTo` (`0x00289ed0`) unless marked. The step itself, its clips and how it ends are on the Human side:
[Characters: Step control](characters.md#step-control).

**Entry.** `Brain_PushAside(mover, stander, point, step)` (`0x0028a248`) is the only way in. Its seven callers
are:

- `Route_IsLinkBusy` and `Route_IsJumpLinkBusy` (a human on a route link);
- `Gang_ClearWayForLeader`;
- `WarriorBrain_Think`;
- `SaveHumanGoal_Process`;
- `Brain_GiveWayTo` itself (below);
- the script's `ActGiveWay` (`Action_GiveWay`, `0x002fe4b0`). Here the other human is the mover, his position the
  point and his forward axis the step, for a stander with fewer than 8 queued actions.

`Brain_PushAside` checks the stander's brain flags `+0xcc`, then calls `Brain_GiveWayTo`:

- bit 1 set: the stander is already giving way, and the answer is yes;
- bit 2 or bit 4 set: the answer is no;
- otherwise it sets bit 4 on both brains for the call, so the recursion below cannot loop, and clears it after.

**The tests**, all needed, else no:

1. The stander's brain has a task (`+0x04` ≠ 0) or its `+0x09` is set.
2. **Neither is a threat to the other.** `Human_IsThreatTo` (`0x00222a48`) must be false both ways. It is
   `Brain_IsThreat` on the two brains ([Characters: Step control](characters.md#step-control) gives the exact
   test).
3. **The stander is idle under its control.** `Human_IsIdleUnderControl` (`0x00225390`) needs these:
   - the control is `Human_UpdateControl` or the step control;
   - no state bit of `0x7bf9e1f3ff0`;
   - no held flag.

**Which way.** All angles are relative to the stander's facing `f` (`Quat_Heading` of its transform-table rotation)
and wrapped to 0-2π. `Human_GetSectorOf` (`0x0029eb38`) gives the sector, 0-7, each 45° wide and centred on k × 45°,
rising anticlockwise:

1. The mover's path is the line through `point` and `point + step`. `Line_ClosestPoint` (`0x00336e08`) gives the
   point `c` on that **infinite** line nearest the stander: `a + d · dot(p − a, d)`, with `d` the unit direction.
2. `s` is the sector of the heading from `c` to the stander: straight away from the path.
3. The sector of the mover's own heading is also computed, and `Sectors_TurnWay` (the shorter way round) is called
   with both. Its result is unused (confirmed (code): the return value is dropped).
4. The stander's sector record is refreshed when it is older than **500 ms** (`Brain_GetSectors(brain, 500)`,
   [AI code: Neighbour sectors](ai-code.md#sectors)).
5. Sectors are tried in the order **s, s+1, s+2, s−1, s+3, s+4, s−2**, modulo 8. That is seven of the eight; s−3 is
   never tried. The first **free** sector wins. `Sectors_IsFree` (`0x0029ea48`) needs no near human (flags `0xb`
   clear) and no wall: the sector is probed on first use and bit `0x4` must stay clear.

**A free sector.** All queued actions are popped (`Brain_ClearActions`; when one refuses, the answer is still yes but
nothing is queued). Then a GiveWay action is allocated:

- heading **f + sector × 45°**;
- the mover as the human to look at;
- a start delay of `Random_Int(125)` = **0-124 ms**;
- the **boost flag**: set when the mover is a pad player (`+0x1b0` ≠ −1) whose stored gait `+0x1a8` is 3 or more,
  so the stander **dashes** away from a jogging or running player, and steps for anything slower.

`GiveWayAction_Init` (`0x002fe568`) sets brain `+0xcc` bit 1 while it lives. A distance from the mover to the point
is computed and not used.

**No free sector.** For the same seven sectors, in order, the human in each sector (when any) is asked to make room:
`Brain_PushAside(stander, that human, the stander's own position, step)`. The first yes ends the search with yes.
So a crowd can ripple outwards, at most one level deep per call because of bit 4. With no yes, the answer is no.

#### Waypoint queues {#queues}

20 records of 0x90 at `0x006cde30` (mask `0x006ce970`), updated after the brains (`0x00299778`); inferred: queues at
choke edges. `0x00299538` lays up to 6 points one metre apart along the edge (`+0x00`, 16 bytes each; count `+0x88`)
between its endpoints `+0x60` / `+0x64`, with 6 humans at `+0x68` and their "settled" flags at `+0x80`. Each update,
humans whose brain `+0xec` is 1 or 2 keep their point and the others are reassigned (`0x00299a08` when there are more
humans than points, else `0x00299b50`), then up to 5 passes swap pairs whose paths cross. Confirmed (code).

### Civilians {#civilians}

The civilian think and events are under [Civilians (type 4)](#think-civilian): toward a nearby player a civilian of
ped type 3 pushes Hostile (`0x72`, 10 m), others a PedReaction (`0x6b`) with line `0xaf`.

### How the scripts drive the AI {#script-control}

The level scripts never run AI code themselves. They work six levers, all through the bindings on [AI
bindings](../references/bindings/ai.md) and [Gang bindings](../references/bindings/gang.md). This section says what each
lever does to a brain and how the levers interact. The usage counts were read from the compiled scripts of levels 99,
80, 87, 2, 3, 5, 34 and 95 (each chunk with its chapters, without the `scenetest` files); they count each binding's name
read in the code, which is almost always a call. Confirmed (code) at the addresses cited unless marked.

#### The levers and their order {#script-levers}

1. **The goal stack.** Every `Goal*` binding that the eight levels use resolves the human and calls `Brain_PushGoal`
   (`0x0028d758`) once, checked for all 27 of their workers: the new goal goes **on top**, the old top is suspended (it
   resumes when the new one ends), and the human's queued actions are cleared. No `Goal*` binding flushes first, so a
   script that wants the goal to be the only one calls `BrFlush` (or `BrFlushGoals`) just before, as the scripts usually
   do ([Goals](#goals), [one update](#update-goals)). `GoalJoinCinematic` with its last argument 1 also runs the goal's
   first update at once. `GoalMelee` pushes two goals (FindEnemy, then Melee).
2. **Actions.** `ActLookAt`, `ActTurnTo` and `ActTurnToDir` queue one action behind any queued ones; the goal on top
   keeps running ([ActLookAt](#look-at)).
3. **The gang's tactic.** A `Tactic*` binding finds the gang, builds the tactic and sets it (`0x00165640`), which ends
   and frees the gang's old tactic. When it starts, **every member that is not a player is flushed** (goals and actions)
   and the tactic gives the goals ([Tactics](#tactics)). A tactic of type `0x12` or above owns its members' goals and
   gives them again on events 17, 19 and 22; under any tactic `Brain_PushFightGoal` does nothing, so a member's own
   fights come from the tactic. `TacticClear` ends and frees it (`Gang_StopTactic`). The tactic's Lua callback receives
   (gang id, code) for each non-zero Process result and event code (`TacFinished` ... `TacAnimStart`, [Scripted
   tactics](#tactic-kinds)); the strategy library [below](#script-strategies) is built on it.
4. **The brain's own handlers.** Between script calls, a brain's think and event handlers ([Brain
   types](#brain-type-handlers)) keep pushing goals of their own: a fight (`Brain_Fight`, which needs threat response
   `+0x21c` ≠ 0, and pushes nothing under a tactic), a reaction to violence (`+0x267`), an investigation (`+0x224`) and
   so on. They push on top of a scripted goal, which resumes afterwards. The script turns them off in three ways:
    - `BrDead(h, true)` / `GangBrDead(gang, true)`: the brain keeps only `Brain_UpdateGoals` and no think or event
      handler, so **only what the script pushed runs** ([Handlers](#handlers)). Actions are cleared; goals stay.
    - `BrSetThreatResponse(h, 0)` / `GangSetThreatResponse(gang, 0)`: no fights; `BrSetReactToViolence(h, false)`: no
      reaction to violence; `BrSetInvestigateResponse` / `GangSetInvestigateResponse` set how it answers noises
      (`+0x224`, [Investigate](ai-goals.md#goal-investigate)).
    - Suspension, below.
5. **Freezing.** `BrSuspend(h, on)` (`Brain_SetSuspended` `0x002923a0`) clears the human's actions and sets brain
   `+0x0a`; `GangSuspend(gang, on)` sets gang `+0xd4`. While either holds, `Brain_UpdateGoals` keeps only the time: no
   goal, reaction goal or action runs ([one update](#update-goals); the brain's own flag counts only while the human is
   not held).
6. **Relations and perception.** `GangMakeEnemies` / `GangMakeFriends` (both ways), `GangMakeEnemiesOfType`,
   `GangMakeNeutralOfType`, `GangSetTargetable` (brain `+0x120`), `GangSetAlwaysSeen`, `BrSetFOV`, `GangSetHearRange`
   and `GangSetRespondPercentage` change whom the [enemy scan](#enemy-scan) lists and who answers violence; they push
   nothing themselves.

**Members joined later.** Every `Gang*` setter that writes brains (threat, investigate, targetable, world flags, dead,
flush) walks the gang's **current** members only (`0x0016b3d0`, `0x0016b4f0`, `0x0016bac0`, `0x0016be30`, `0x0016aa98`,
`0x0016ba18`). A human added afterwards (`GangAddMember`, or a spawner) keeps his own values (threat response 2 from
`Brain_Init`). Spawned members are configured by the spawner's Lua callback instead (`GangCallback`
[below](#script-strategies)).

**Order matters.** Because a tactic's Start flushes its members, a goal pushed before `Tactic*` is lost, and a goal
pushed after it is replaced the next time the tactic re-gives goals (types `0x12` and above). Scripts that script a gang
member therefore clear the tactic first (`TacticClear`, 43 uses) and often make the gang dead (inferred from the call
order).

#### Brain and gang bindings in effect {#script-binding-effects}

| Binding | Worker | What it does to the brain |
| --- | --- | --- |
| `BrFlush` | `0x00292530` | ends and frees every goal (callbacks fire, "completed" 0), then clears the actions |
| `BrFlushGoals` | `0x00292618` | the goals only (`Brain_ClearGoals`) |
| `BrDead(h, on)` | `0x00292330` | `+0x09`; clears the actions, reinstalls the handlers ([Handlers](#handlers)) |
| `BrSuspend(h, on)` | `0x002923a0` | clears the actions; `+0x0a` (the update is skipped) |
| `BrSetType(h, t)` | `0x00292410` | stores the type and installs that type's handlers (`0x0028ceb8`, `0x0028c1a8`); level 95 makes humans type 2 (gang) |
| `BrSetPedType(h, t)` | `0x00292460` | `+0x26c`; 3 makes a civilian fight back ([Civilians](#think-civilian)) |
| `BrSetThreatResponse` | `0x00292708` | `+0x21c` (0 never fights) |
| `BrSetInvestigateResponse` | `0x002927a8` | `+0x224` |
| `BrSetReactToViolence` | `0x00292bc8` | `+0x267` |
| `BrSetThugWantsWeapon` | `0x00292848` | `+0x265` (may fetch weapons, [Objects](#ai-objects)) |
| `BrCanUseWorldFlags` | `0x00292c10` | `+0x2d1` / `+0x2d2` ([World flags](#ai-flags)) |
| `BrSetFOV` | `0x002928d8` | degrees to radians into `+0x12c` |
| `BrSetNumFollowSlots`, `BrSetFollowSlotSet`, `BrSetFollowSlot` | `0x00292a60`, `0x00292ac0`, `0x00292b10` | the formation slots round a leader ([Formations](#formations)) |
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
| `GangSetMsgHandler` | `0x0016ab38` | a Lua handler per event id ([Events](#gang-events)) |
| `GangMakeEnemies` / `GangMakeFriends` | `0x0016acf0` / `0x0016ad80` | both gangs' enemy and friend masks |

#### The strategy library {#script-strategies}

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

#### Usage by level {#script-usage}

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
  first mission's cast](#level99)).
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
  `TacticBossScenarioA` for the Diego and Vargas fight ([its section](#boss-diego-vargas)), `GoalBackoff(player, …, 5,
  5000)` to hold the players back during scenes, `BrSetPedType(h, 3)` for civilians who fight back.
- **34 (the riot)**: rioters with `GoalRiot(h, 10, 100, 2, fight, gangFight)` ([GoalRiot](#riot): within 10 m of a
  player, always act, two acts, a fight chance of 0 or 25 %); the looters get `BrSetThreatResponse(h, 0)` and
  `BrSetReactToViolence(h, false)`; `GangSetMaxConcurrent` paces the riot spawner (3, 6, 10); `TacticAttack` and
  `GoalFight(h, player, −1)` for the Furies; `GoalMoveToExitFlag` and `GangExitWorld` clear them out;
  `GoalStationaryThrower`.
- **95 (the hub)**: `GoalPlayDynIdle` and `ActLookAt` for the hub's idle humans; `GoalBoxer(h, HeavyBag)` at the
  clubhouse; `BrSetType(h, 2)` and `GangMakeNeutralOfType(GangWarriors, 19)` for side-job gangs; `GoalShopkeeper`,
  `GoalPeddler`, `GoalGrabTarget` (the vigilante job's stalker), `GangClearBums`, and spawners checked with
  `GangIsASpawner`.

### Scripted goals and actions {#scripted}

What `level99`'s scripts give a brain directly ([How the scripts drive the AI](#script-control) has the overview).
Confirmed (code) at the addresses cited unless marked; argument names and defaults are on [AI
bindings](../references/bindings/ai.md).

- **Lua callbacks.** A goal that takes a callback name interns it through the script system (`*(0x00512b04)`, vtable
  `+0xcc`; 0 for none) and calls back in its **End** through vtable `+0xa4`, which is `ScheduleFuncArg2`
  (`0x00357430`): the callback is **scheduled 33 ms later** with two values, the human's handle and a "completed" flag
  (`0x002d2f98` passes the interned name, the handle, the flag and 33). A goal cut short (popped, flushed) still calls
  back, with "completed" 0. This corrects an earlier reading of the fourth value as the goal type.
- **`GoalFight`** is `Brain_StartFight` (`0x002b2b90`): clear the actions, then `Brain_Fight(brain, target, -1, 0)`
  ([Starting a fight](#targets)). Its third argument is read and never used.
- **`BrFlush`** (`0x00292530`): `Brain_ClearGoals`, then `Brain_ClearActions`. Each goal is ended, so its callback
  fires; an action that refuses to abort stays ([Actions](#actions)).
- **`BrDead` / `GangBrDead`** (`Brain_SetDead`, `0x00292330`): clear the **actions only** (the goals stay), set brain
  `+0x09` and reinstall the handlers ([Handlers](#handlers)). Inferred: a dead player brain no longer leaves the
  record to the pad, as `level99` sets its Warriors dead (`GangBrDead`) while it walks them through a scene.

#### GoalMoveToFlag {#move-to-flag}

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
       a [move action](#move-action) to the target (the radius, the gait, the option bit, a start delay of 0-250 ms,
       and the "flag kind `0x12`" bit), and wait.

    So the goal plans again each time a move action ends short of the radius.
- **End** (`0x002da4a0`), only when arrived: the flag gets message 8 with the human (flag vtable `+0x44`), and the
  human's gang hears of it (`Gang_OnEvent`, [Gangs](#gangs)).

GoalMoveToFlag uses only the move action and the [route planner](#path-planning), not the FlagNet goals `0x69` /
`0x73`.

#### Turning: ActLookAt {#look-at}

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

#### GoalAddressPerson {#address-person}

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

#### GoalPlayDynAnimation {#dyn-animation}

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

#### GoalBumLogic {#bum-logic}

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

#### GoalDealer {#dealer}

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

##### Gestures and lines {#dealer-gestures}

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

##### The icon {#dealer-icon}

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

**The radar icon** (`DealerGoal_AddRadarIcon` `0x002c7ee0`, at the greeting, only with the option and when the dealer
has no blip yet): blip type 2, 4 or 3 with icon 29, 31 or 30 at 0.8 for types 0, 1, 2 ([GUI](gui.md#radar-icons)).

##### Process {#dealer-process}

`DealerGoal_Process` (`0x002c7fd8`; confirmed (code)), each update, with *d* the nearest player's distance
(`GameState_FindNearestPlayer` `0x00419ee0`):

1. **Range**: `+0x3e` = (*d* ≤ range). Out of range: if he was in range last update and the dealer was dealing, the
   leaving gesture and line (above; first a turn to him, only when the dealer's actions are not blocked); then the
   **reset** (`DealerGoal_Reset` `0x002c7de8`): the dealer's own 668 override removed when a deal was made; `+0x3e`,
   `+0x3f`, `+0x40`, `+0x42`, `+0x45` cleared; each player's 668 override removed; state 1 (unless 4 or 5); next scan
   0. Beyond 2 × range the greeting is forgotten (`+0x3d` = 0).
2. **In range**: the sight test `Human_HasLineOfSight(dealer, player)` (`0x00222288`, [Sight](#sight)); and, once per
   visit for each player, `money_give.anm` is bound to that player's anim 668 (`Human_SetAnimOverride` `0x00221aa0`),
   in sight or not, so the clip is loaded before the pair plays it. Nothing plays it here: the money pair does.
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
   dealer is talking, a [Spectate goal](#spectate) is pushed (keep 8 m, 2-4 s between looks, an 8 s limit) when the
   member walks or stands (gait below 3); a turn to the player, the wary gesture and line; done.
7. **Home**: more than 1 m from home and not playing 668: a move action home (gait 4, radius 1 m); done.
8. **Facing**: more than 15° off the player: not yet greeted → turn to him (`TurnToPointAction`); done. Greeted → turn
   (and done) only when the player stands (gait 0) within 2 m.
9. While the dealer is talking: done. **State 1** and the player within **1.5 m**, not talking himself: the offer
   line, `+0x1b2` = 1, the prompt registered, dealing (`+0x42`) = 1, **state 3**; done.
10. Not greeted: a turn to him, the greeting gesture and line, the radar icon (with the option); greeted, state 1.
11. Every update that gets here: `+0x1b2` = 1 and the **kind-4 prompt** registered through the dealer's vtable
    `+0x124` (`GSTRING.HUD` text of his type, [Crimes](crimes.md#context-records), reach 1.75 m). So the prompt is
    offered from the greeting on, not only in state 3, while the dealer is idle and the player in range and in sight.

##### Buying {#dealer-buy}

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
which does step 6 when `+0x45` is set ([Combat](combat.md#rage)). A second event 0 during the pair completes it at
once. Confirmed (code) at `0x002c74d8` and `0x002473bc`; the frame from the disc's clip.

**Attacked** (`DealerBrain_OnHit` `0x00302ff0`, events `0x10` and 1): a help call to his gang (30 m); then, when the
dealer is not down and his top goal is the dealer goal, goal 1, or a Spectate goal without a fight goal, and his brain
is not type 2: with `GoalDealer` on the stack its state becomes **5** and its **run chance** `+0x3a` is rolled (50 %
without the goal). `Random_Int(100)` below it → a flee goal (`DealerFlee_Push` `0x002c88a8` → `0x002c8970`, vtable
`0x00540c30`): after 25-50 ms he steps back to 2-4 m from the attacker, then runs at gait 5 to the nearest flag of
activity 8 (`0x00416f08`) with line 9. Otherwise threat response 2, `Brain_Fight` for 15 s, and the same goal with
its "fight" byte set, which after the step back puts `dyn_swhbld_super` in his hand and ends. So the run chance is
read only when he is attacked.

#### GoalRiot {#riot}

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
[Types](#types)); any other never fights. One draw: *p* = `Random_Int(100)` < the player-fight chance `+0x35`. The
candidates are the humans within 15 m (`0x002274a8`, up to 60, in human-slot order) and the first that passes is
taken: brain not type 3 (an ally Warrior); no player controls him (human `+0x1b0` = −1) unless *p*; not friendly
(`0x00290230`: same brain or `Gang_AreFriends`); no fight goal; nobody in his attack-slot list (brain `+0x1a4`);
reached from the rioter in a straight line. Then threat response `+0x21c` = 2, `Brain_Fight(rioter, him, 8000, 1)`
and the taunt `0x11` (`0x002205e0`). So the binding's sixth argument is the chance that **the player** may be picked,
not a gang member.

**How the 8 s fight ends.** `Brain_Fight` → `Brain_PushFightGoal(brain, 8000)` pops any Melee and FindEnemy goals,
pushes FindEnemy (`0x41`) and Melee (8), each given the 8000 ([Closing on the target](#fight-approach)), and then the
fight goal (15) with **its deadline `+0x24` = now + 8000 ms**
(`FightGoal_Init`, `0x002b2c20`; `+0x20` is the separate 750-1000 ms first-attack timer). The fight goal's usual
ends apply (no target, no attack slot, out of range, [The fight goal](#fight)). Past the deadline, at each update
with no actions queued, it **ends** (returns 2) when any of these holds: the fighter's class `+0x11b` is 13 and goal
`+0x29` is 0; the fighter is not among the target's four active attackers (target brain `+0x1f0`, `0x002911a8`);
the target brain's byte `+0x11f` is 0 (`Brain_IsAttackableBy(target, nil)`, not traced); the fighter has a goal
`0x35` (FollowAndDefend). Otherwise it goes on attacking. Confirmed (code) at `0x002b3d0c`-`0x002b3d6c`. Under the
fight goal the riot goal is in state 3, so when the fight (and the melee goals beneath it) are done, the rioter
leaves.

#### GoalDevilRun {#devil-run}

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
       points: the [move action](#move-action) plans its own route; the path only measures progress.
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
       (`Brain_SetTarget`), pushes a [Melee goal](#melee-goal) (`0x002ade10`: time limit 2000, its fight goals
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

### Gangs, tactics and formations {#gangs}

#### The gang {#gang-record}

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
| `+0xd4` | u8 | suspended: its members' brains skip their update ([One update](#update-goals)) |
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

#### Events and GangSetMsgHandler {#gang-events}

`Human_OnEvent` (`0x0021d4e8`) offers an event, until one consumes it, to the human's own script handlers
(`0x00384c38`), then **`Gang_OnEvent`** (`0x00164c20`, the brain's gang `+0x20c`), then `Brain_OnEvent`
([Events](#events)). Confirmed (code).

- `GangSetMsgHandler(gang, msg, name)` (`0x0016ab38` → `0x00164bb8`) stores the interned name at `+0xe4 + msg × 4`.
- `Gang_OnEvent` calls that handler only while scripting runs (`0x00512b28` = 1), through script-system `+0x8c` with
  three values. Event **18** (a human died or was
  knocked out, [Script events](../references/script-events.md)) calls `handler(member, other, standing)`, where
  `other` is the attacker (the null handle when none) and `standing` the members not dead or knocked out
  (`0x00166220`); event `0x11` passes the event's `+4`, event 2 the headcount; any other id goes through the generic
  marshaller (`0x00384ce0`) and is consumed when the Lua function returns true.
- Then, when the gang has a tactic and members, the event goes to the tactic's event slot and its result is
  returned.

#### The gang update {#gang-update}

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

#### Deleting a gang {#gang-delete}

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

#### Tactics {#tactics}

A pool of 30 tactics of 0x90 at `0x006ea1b0` (mask `0x006ea1a0`; alloc `0x00306558`, free `0x003065b0`). Base fields:
`+0x00` gang, `+0x04` started, `+0x08` time limit (−1 none; Process returns 2 once past it), `+0x0c` Lua callback,
`+0x1c` vtable (`{s16 delta, fn}` pairs: `+0x0c` Start, `+0x14` End, `+0x24` Process, `+0x34` type id, `+0x3c`
"members keep their own goals", true for types below `0x12`, `+0x4c` event, `+0x54` fire the callback). Confirmed
(code).

- **Setting one** (`0x00165640`) ends and frees the old tactic. **`Tactic_Start`** (`0x00306690`): the time limit made
  absolute, the gang's alert state set from the `+0x3c` answer (types `0x16`-`0x18` and `0x21` also set gang
  `+0xd1` / `+0xd2`), every member that is not a player (`+0x1b0` = −1) flushed (`0x0028d8a0`) and `0x00226f70`,
  then Start, then started. **`Tactic_Process`** (`0x003067d8`) starts it when needed, returns 2 past the time limit,
  else the class's Process. **Firing the callback** (`0x00306938`) calls the Lua function at once with two values:
  the gang's id (`+0x30`) and the code. **`TacticClear`** ends and frees it.
- **Fights under a tactic**: `Brain_PushFightGoal` (`0x0028d190`) returns at once when the gang has a tactic, so
  `GoalFight` picks the target but **pushes no fight goal**; the tactic fights. It also does nothing for a knocked-down
  or dead human, or a type-3 brain already holding goal 9; otherwise it pops goals 8 and `0x41`, pushes FindEnemy
  (`0x41`), Melee (8) and then the fight goal (`0xf`) ([Starting a fight](#targets)).
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

##### The scripted tactics {#tactic-kinds}

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
- **Moving as a group** (MoveToFlag, TravelPath, WalkinTall, Wander, Confront): the leader is flushed (`0x0028d8c0`,
  `0x0028d8a0`) and given the moving goal; every other member that is not a player and not down joins the leader's
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
| `TacticDefend` | `0x02` | `0x00543800` | `FollowAndDefend` (53) round the human; dogs (type 221) `AvoidEnemies` (32) | 11 when the human is gone or dead; 9 when no member has an enemy (1.5 s) | 2 on the human → 11; 17/18 on the human: members rush to him; 20 near him: his attacker's gang becomes an enemy |
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
  members with no goal melee the nearest member that has one (`0x002b75b8`); every 7 s a gang with `+0xd9` set and
  at least two living members starts one of seven coordinated sub-tactics chosen by weights per gang kind
  (`0x00307a10`, not traced further).
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

#### The boss fights {#boss-fights}

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

#### The Diego and Vargas fight {#boss-diego-vargas}

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
  **2** fight: an object in hand is used on the
  nearest enemy (`0x002faeb0` kind `0x10`; inferred: thrown); otherwise a target re-picked every 4 s, an attack kind chosen
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

The clips 671-673 are `missing_anim_filler` in the anim id table, so the level supplies them (inferred: through the
bosses' dynamic animations).

**BigThrower** (Vargas, stage 2; Process `0x002ed718`): state 0 a taunt (line `0x11`, anim 643); 1 walk to the flag
(0.5 m) or tire (above); 2 turn toward the camera and play 549 (`ghetto_pickup`; for others than Vargas the pickup clip
is overridden with the name at `0x00567a88`), the next object appears in his hand (`0x002ed498`); 3 aim: turn within
45°, line of sight (six misses drop the target), then 4; 4 throw (`0x002faeb0` kind `0x10`, 100 ms) and back to 1
when the hand is empty. Start (`0x002ed168`) and End (`0x002ed260`) as the BigBrawler's, plus that override.

##### The BossDiego program {#boss-diego-ops}

**No script calls `GoalBossDiego`.** The Diego fight uses `TacticBossScenarioA` ([Diego and
Vargas](ai.md#boss-diego-vargas)), so this goal is never built in play. Confirmed (code) for the interpreter;
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

#### Brain bytes `+0x0b` and `+0x0c` {#brain-boosts}

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

#### The Warriors' pick-ups {#warrior-pickups}

`WarriorBrain_Think` (`0x003052f0`), every sixth think, while the brain may pick up (`+0x265`), holds nothing, and its
gang kind's pick-up factor (`0x001644f8`) is not 0: with chance 25 × factor %, when nobody is close around it
(`0x0029eaa0`: none of its eight [sectors](#neighbour-sectors) has flag bit 1 or 2 set, that is no human within 1.5 m
([Neighbour sectors](#neighbour-sectors)); this corrects an earlier reading as a goal check), the nearest pickable
object within 1.5 m (`0x0029d5f0`) is fetched (`0x002faf98`). When the leader is free, the gang's `+0x32` is 1, a member
is under attack and the Warrior's own enemy list is empty, the search's filter mask is `0x30000` instead of `0x10000`
and its last flag 1; the radius stays 1.5 m. **`CfgWarriorWeapons(false)`** (game state `+0x5704` = 0) refuses an object
whose type class (`+0x87`) is 4, a weapon; other objects are still taken. A Warrior holding a weapon of kind 4 or 6 with
no enemies drops it unless its gang's tactic is type `0x26`. Confirmed (code).

#### Spawners {#spawners}

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
5. **Place** the human: 6, 8 and 10 out of the camera's view ([below](#spawner-placement), `0x001673b8`); 7 out of
   view and sent to the gang's first live member (`0x001679e8`, the second part inferred); the others at the
   spawner.
6. **Spawn** (`0x00167ea8`, named `<spawner><count>`), open the spawner's door, count it.

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

##### Off screen {#spawner-unseen}

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

##### Out of sight {#spawner-placement}

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
      [Path planning](#path-planning)). No area or no node: no spawn this update, and no try is made.
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

###### The search {#spawner-search}

`0x00251d28(value, h, startNode, origin, goal, coneAxis)`, confirmed (code):

- **A\*** over the route nodes (C records of `0x20`) from the start node toward the goal: f = g + h at node `+0x16`,
  g at `+0x18`, h at `+0x1a`, parent at `+0x1c` (all `u16`, 1/16 m). h = 16 × the 3D distance from the node to the
  goal (`0x00251820`); an edge costs as in [Path planning](#path-planning) (`0x00251890` with mask `0xff`, so the
  `ClimbFilter` extras apply when it is on). Unlike the route search there is **no mask test** and no 65000 cap:
  every edge is taken except one whose D word has **bit 31** set (a hazard-avoided link), which is skipped. A node
  already open is updated only when the new g is lower.
- **Popping** the node with the lowest f (ties: the heap's order) marks it closed (`+0x18` = `0xabcd`) and tests it,
  the start node included: found when its distance from the origin exceeds 70 m, or exceeds the value and the unit
  vector origin → node has a dot product with the cone axis below cos *h*. Found: the goal point is overwritten with
  the node's position and the search returns 1.
- **Limits**: the open list is a heap of at most **1,000** entries (`0x002531c0`), the closed list at most **3,000**
  nodes (`0x002534d0`). An empty open list, a full open list on a push or a full closed list ends the search with
  nothing (0); on leaving, the closed marks are cleared.

#### Crimes and the police {#crimes}

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
   the offender's gang also notes the time (gang `+0x5f4`, a wanted timer, inferred).
5. Type 1 also marks the nearest store flag (kind `0xe`) robbed and sends message `0x12` to the `strobe` object
   nearest it (within 36, 6 m if squared). Types 0, 2 and 8 score a statistic for a player offender against a victim
   of brain kind 1, 4 or 5, once per victim (`0x004ed948` on `0x006fe490`; a statistic, inferred).
6. When the offender is in player 1's gang, `+0x290` takes the type unless it holds 7 or 12, and the HUD is told
   (`0x001b2520`).

Who reports, confirmed (code): `CrimeIsHappening` (`0x0041b6e0`, mode 1), `SpawnCustomCrime` (`0x0041b7a0`, type 4),
an alarmed glass pane ([World objects](objects.md#pane)) and the code at `0x0021b290`, `0x0022d908`, `0x002e6668`,
`0x003961d0` and `0x00394da8` (type 1), `0x00301cf8` (type 2), `0x0041bec8` (type 9), and `0x002a83f8` and
`0x002c4360` (a type from their own records). The crime names are a function (`0x0041d2c0`) with no callers; the
numbers they go with are inferred from those types (4 `Custom`, 1 `BreakAndEnter` for the store, 9
`PrecinctAttack`).

#### Warrior commands {#warrior-commands}

A war chief (a player human whose `+0x3ac` is 1) orders the crew, his gang, with one of seven **Warrior
commands** ([Commands](../references/commands.md#warrior-command)). The menu (HUD `0x001a6c58`, R2 and the right
stick: [HUD](hud.md#warrior-command-menu)), `WCIssueCommand`
and the game itself go through one dispatcher, `0x0041c4e0(state, chief, command, forced, pos, arg)`, confirmed
(code):

1. Nothing while commands are locked (game state `+0x411`), the player's menu is locked (`+0x42e` + player), the
   command is disabled for the player (`+0x41e` + player × 7 + command, `WCEnableCommand`; the player is human
   `+0x1b0`) or the human is not a war chief.
2. The command becomes the player's last (`+0x41c` + player). Unforced, giving the current command again only
   repeats its line (`0x0041cc40`) and, for 0 under a tactic of type `0x12`, calls `0x003114e0` on it.
3. Otherwise the gang's tactic is cleared and the command's started: 0 `0x00310e00` (given 9.0), 1 `0x00320530`
   (unforced, the chief also plays clip 0x2a4 or 0x2a8, and the gang's target is set from his), 2 `Tactic_Defend`
   around the chief (clip 0x2a3 or 0x2a7), 3 `0x00313400` (the standing tactic of type 3, [the first
   mission](#level99)) or `0x003128a0` for a gang whose brain `+0x2d5` is set, 4 `0x00319570` (given 75.0), 5
   `0x00320b60`; 6 starts nothing.
4. The chief says the command's line (`0x0041cc40`, [Speech](../references/speech.md)): 0 `follow` (`follow_hide`
   while `0x00228168` holds, hiding inferred), 1 `attack`, 2 `defend`, 3 `hold` (`holdhide`), 4 `scatter`, 5
   `steal` when the nearest thing of kind `0xe` is closer than 64 (8 m if that is a squared distance, inferred) and
   `0x0039a580` accepts it, else `vandal`; 6 none.

The command tactics, by command (constructor, vtable, type id; confirmed (code)):

| Command | Constructor | Vtable | Type | Members get |
| --- | --- | --- | --- | --- |
| 0 follow | `WarriorFollowTactic_Create` `0x00310e00` (init `0x00310e90`, distance 9.0) | `0x005438c0` | `0x12` | `FollowPlayer` ([below](#warrior-follow)) |
| 1 attack | `WarriorAttackTactic_Create` `0x00320530` (init `0x003205b0`, member goals `0x00320618`) | `0x00544160` | `0x01` | [`FollowAndAttack`](ai-goals.md#goal-follow-and-attack) (`0x34`, `0x002bbdc0`) on the chief; dogs (class 221) `AvoidEnemies` (4 m, 8, 8) ([code](ai-code.md#t2-warrior-attack)) |
| 2 defend | `Tactic_Defend` round the chief (given 2.25) | `0x00543800` | `0x02` | `FollowAndDefend` ([table](#tactic-kinds)) |
| 3 hold | `WarriorHoldTactic_Create` `0x00313400` (init `0x00313490`, given 1.5), or `WarriorHoldTactic2_Create` `0x003128a0` (init `0x003128f8`) | `0x005439e0` / `0x00543980` | `0x03` / `0x13` | type 3: [`HoldPosition`](ai-goals.md#goal-hold-position) at his own spot, radius 1.5 m, and a turn outward; type `0x13`: [`Hide`](ai-goals.md#goal-hide) (`0x58`) at a wall point near the nearest hiding flag ([code](ai-code.md#t1-hold), [hide](ai-code.md#t1-hide)) |
| 4 scatter | `WarriorScatterTactic_Create` `0x00319570` (init `0x00319600`, given 75.0) | `0x00543d40` | `0x25` | [`Scatter`](ai-goals.md#goal-scatter) (`0x37`) to one of up to six hiding flags within 75 m ([code](ai-code.md#t2-warrior-scatter)) |
| 5 steal / wreck | `WarriorStealTactic_Create` `0x00320b60` (init `0x00320be0`) | `0x005441c0` | `0x26` | [`WarriorVandalSteal`](ai-goals.md#goal-warrior-vandal-steal) (`0x83`); dogs `AvoidEnemies` (7, 12, 12) ([code](ai-code.md#t2-warrior-steal)) |

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

#### The default command: follow {#warrior-follow}

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
  not down is flushed and gets **`GoalFollowPlayer(9 m, chief, mode 3)`** (`0x002de380`, type `0x32`, vtable
  `0x00541d70`). In a two-player game a Warrior of the first player's gang more than 10 m from him whose nearest player
  is the other one gets `FollowAndDefend` (`0x35`, `0x002bca20`) on that player instead. "Not down" is
  `Human_IsDownOrDead` (`0x00227e60`) returning 0: the human has its record (`+0xd4`) and none of the state bits
  `0x180050000` (`0x100000000` dead, `0x80000000` dying, `0x40000` knocked out, `0x10000` tackled;
  [Combat](combat.md#state-flags)). It does not read the brain's `+0x09` (`BrDead`), the health, or knocked down
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

**`GoalFollowPlayer`'s Process** (`0x002de7c0`): done (2) when the leader is gone. Every 31 updates while the
follower is not in fight mode, or when it lost a straight walkable line to the leader, it pushes
`FollowFormation(0.75 m, leader)` (`0x002dfba8`), which walks it to its formation slot. Its **fight mode**
(`0x002de650`) is on for a Warrior when a player's brain `+0x2e4` is set (a Warrior is hitting back at a chief who
hit him, `WarriorBrain_OnHitByChief` `0x00306190`), or when a member of its gang has attackers and its own brain
`+0x152` (inferred: how many hold it as an enemy) is not 0; it is off while one of its own attackers is a player. In
fight mode it widens its field of view to 2π, keeps a valid target, else takes the nearest attacker within 20 m or
the leader's target, enters the fight stance and **turns to the target** within the far melee range (the near one
when the target is busy) or moves to him (`MoveToHumanAction`, 3 s); it does **not attack**. Out of fight mode it
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
    2. Every 6 updates, without a slot yet, the follow point is refreshed into the running move.
    3. Every *n* updates (*n* a random 2-7 drawn at init) the leader's gait is noted; "the leader just stopped" = it
       was above 3 and is now below 3. Unless he just stopped, nothing more while a check time (now + 1000 ms) has
       not passed and actions are queued.
    4. The follow point: its slot point, radius = the arrival distance; without one, the follower ahead of him in
       the formation's queue, radius 2.5 m.
    5. No straight walkable distance to the point (`Nav_StraightDistance`): a counter starts; every 15th count with
       no nav polygon or no reachable node near the point, a breakable object within 1.5 m is smashed
       (VandalizeItem); he waits until the counter reaches 30.
    6. The leader standing (his speed gives gait 0) and the follower within the radius: done (2).
    7. Beyond 0.2 m: a move action to the point (radius 0.1 m at a slot, else 2.5 m) at the speed below, or the
       running move's speed and radius updated. A new move by a follower who has attackers (brain `+0x1a4`) and a
       clear `+0x2d5` also turns him to face the point and raises his start boost by one.
- **The speed** (`FollowFormation_PickSpeed` `0x002dfef8`), with `d` the distance, `v` the leader's speed capped at
  1.25 × the sprint speed: within the arrival distance, `v`. Beyond it, `f` = 0.1 × (`d` − arrival distance); when
  `v` < 2 m/s, `f` += 4 × `f` × max(`v` − walk speed, 0); `f` += 1 when `v` > 2 m/s, the leader's `+0x2d5` is set, or
  the follower has attackers. Gait `g` = run (4) beyond 8 m or with the leader's `+0x2d5`, jog (3) beyond 4 m, else
  walk (2). Speed = max(speed of `g`, `v`) × `f`, clamped to [walk speed, 1.25 × sprint speed]. So a follower far
  behind runs, near his slot matches the leader, and slows to a walk on arrival.

#### Automatic commands {#warrior-auto-commands}

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
Under attack, defend or follow, `Brain_PushFightGoal` pushes nothing ([Tactics](#tactics)): the tactic decides.

#### Formations and follow slots {#formations}

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

- Init (`0x00294ad8`): 6 slots, 6 allowed, set 0. `BrSetNumFollowSlots(leader, n, n2)` sets `+0x271` = n and `+0x272`
  = n2 (n when −1); `BrSetFollowSlotSet` sets `+0x270`; `BrSetFollowSlot(leader, slot, {x, y}, set)` stores (x, y, 0)
  in 1/16 m in that set. Each replans at once.
- **Joining** (`0x00295f28`) takes a free follower entry and sets the follower's brain `+0x212` = the formation's
  index; leaving (`0x00296028`) sets it to −1.
- **Planning** (`0x002956d0`), when the leader has just stopped, at the plan time, or once he is 1 m from the planned
  spot: next plan in 1000 ms at gait 2, else 2000; the plan point is the leader's position, 2 m ahead while he runs
  (`0x00223c10`); the slots' world points are worked out (`0x00294f38`: the offset in 1/16 m, turned by the leader's
  orientation (`0x004dacc0`, so x is to his right and y ahead), added to the plan point and dropped to the ground
  with a ray from 1.9 m above to 5.9 m below; a slot is usable when the leader sees its point (`0x002221e0`), up to
  `+0x272` of them); followers whose human is gone leave (`0x00294e98`); each usable slot takes its nearest unassigned
  follower; followers left over queue nearest-first behind the slotted ones (or the leader); up to 3 passes swap
  pairs whose paths cross.

**GoalTrackHuman** (vtable `0x00541d10`, constructor `0x002df250`: `+0x10` target, `+0x14` distance, `+0x18` a retry
counter): Start (`0x002df288`) joins the target's formation (`0x0021d428`, `0x00295f28`) and turns the fight stance
on, End leaves it. Process (`0x002df3c0`): no target → done; every 30 updates a 1.5 s head look-at, every 40 the
actions cleared; with no actions, a failed move (brain `+0x284`) counts up the retry counter; the goal point is its
slot's world point (`0x00296358`, `0x00296410`), or **its own position when it has no slot** (so it never walks);
beyond `distance`, once the counter reaches 31 it and `+0x284` are reset, and with the counter at 0 a move action to
the point (gait 2, arrival radius `distance`, option 1, no delay, facing the target); otherwise a turn-to-point when
more than 15° off the target. Confirmed (code); this corrects the binding's "walks to the target
when it has no slot".

#### Gang functions {#gang-functions}

Every function of the gang module (`0x00162518`-`0x0016e388`) not described above, by address. Offsets are gang
record offsets unless a spawner (0x130 at `+0x640`) or the configuration record (0x7a) is named. Script bindings
are given by their Lua name ([Gang bindings](../references/bindings/gang.md)).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001624e0` | `LoadScreenObj_Construct` | Sets the vtable `0x00538900` and clears the three slots (`0x00162568`); the object at `0x005e6da8` | Confirmed (code) |
| `0x00162518` | `LoadScreenObj_Destruct` | Restores the vtable, finishes the level load screen, frees the object when bit 0 of the flag is set | Confirmed (code) |
| `0x00162568` | `LoadScreenObj_ClearSlots` | Sets the three ids at `+4` to `0xffff` | Confirmed (code) |
| `0x00163c08` | `LoadScreens_StaticInit` | File-scope initialiser: constructs the objects at `0x005e6da8` and `0x005e6dc8` (memory-card load screen) | Confirmed (code) |
| `0x00163c48` | `LoadScreens_GlobalCtor` | Calls `0x00163c08(1, 0xffff)` at start-up | Confirmed (code) |
| `0x00163ed8` | `GangClips_Clear` | Empties the 27 anim substitution entries (0x28 bytes: a name, `+0x20` the loaded slot) at gang `+0x1b0` | Confirmed (code) |
| `0x00163f10` | `GangClips_GetGroup` | First slot and count per anim id: `0x25b` 0/8, `0x25c` 8/4, `0x253` 12/5, `0x257` 17/3, `0x256` 20/3, `0x29c` 23/4 (27 slots) | Confirmed (code) |
| `0x00163fc0` | `GangClips_Get` | The clip of a group's entry *i*; for a negative *i*, a random loaded entry (group `0x29c`: only entries made for that anim id) | Confirmed (code) |
| `0x00164100` | `GangClips_FindPending` | First entry with a name and no loaded slot, or null | Confirmed (code) |
| `0x00164138` | `GangClips_AnyPending` | Whether any named entry is not loaded yet | Confirmed (code) |
| `0x00164178` | `GangClips_Set` | Loads names into a group's slots; a longer list fills them with a random subset in order | Confirmed (code) |
| `0x001642b0` | `GangClips_ReleaseAll` | Releases every entry's dynamic anim slot | Confirmed (code) |
| `0x001642f0` | `GangClips_ReleaseGroup` | Releases the slots of one group (`GangClips_GetGroup`) | Confirmed (code) |
| `0x00164370` | `GangConfig_InitTable` | The 28 gang configuration records (0x7a bytes, `0x0050caa8`): track names empty, `+0x6c` 1, `+0x6d` 1, `+0x6e` 4, `+0x6f`, `+0x72` and the strategy 0 | Confirmed (code) |
| `0x00164410` | `GangConfig_SetMusicTrack` | Copies music track *n* (35 characters at most) of a gang's configuration (`+n × 0x24`) | Confirmed (code) |
| `0x00164458` | `GangConfig_GetMusicTrack` | Track *n*, or null when empty | Confirmed (code) |
| `0x00164480` / `0x00164498` | `GangConfig_SetField6C` / `GangConfig_GetField6C` | Byte `+0x6c` of a gang's configuration (`CfgSetGang`) | Confirmed (code) |
| `0x001644b0` / `0x001644c8` | `GangConfig_SetField6D` / `GangConfig_GetField6D` | Byte `+0x6d` | Confirmed (code) |
| `0x001644e0` / `0x001644f8` | `GangConfig_SetField6E` / `GangConfig_GetField6E` | Byte `+0x6e` | Confirmed (code) |
| `0x00164510` / `0x00164528` | `GangConfig_SetField6F` / `GangConfig_GetField6F` | Byte `+0x6f` | Confirmed (code) |
| `0x00164540` / `0x00164558` | `GangConfig_SetField70` / `GangConfig_GetField70` | Byte `+0x70` | Confirmed (code) |
| `0x00164570` / `0x00164588` | `GangConfig_SetField71` / `GangConfig_GetField71` | Byte `+0x71` | Confirmed (code) |
| `0x001645a0` / `0x001645b8` | `GangConfig_SetField72` / `GangConfig_GetField72` | Byte `+0x72` (0 or 50) | Confirmed (code) |
| `0x001645d0` | `GangConfig_SetStrategy` | Stores the seven strategy entries as bytes from `+0x73` | Confirmed (code) |
| `0x00164610` | `GangConfig_GetStrategyEntry` | Strategy byte *i* | Confirmed (code) |
| `0x00164630` | `Gang_Construct` | Null handles for the leader, members and `+0x88[16]`, clears the anim substitutions, constructs the four spawners (`+0x640`, 0x130 each), then resets the record | Confirmed (code) |
| `0x001647e0` | `Gang_ClearMsgHandlers` | Clears the 26 event handlers at `+0xe4` | Confirmed (code) |
| `0x00164808` | `Gang_Reset` | Not in use or suspended; members, turf boxes, callbacks, handlers, masks and tactic cleared; the spawners reset; wanted timers 0; `+0xd7` 30, `+0xdd` 0x18 (no tactic kind); leader the null handle | Confirmed (code) |
| `0x00164a28` | `Gang_SetAlertState` | On a change: fighting (1) installs the combat fidget (not kind `0x18`) and taunt (not kind `0x17`) substitutions and updates the music mood; calm (0) removes them; kind 5 keeps neither; AI members' brain `+0x14d` = 1 when calm, 0 when fighting | Confirmed (code) |
| `0x00164b68` | `Gang_SetEmptyHandler` | Interns (or clears) the Lua name at `+0x158` called when the gang is emptied ([The gang update](#gang-update)) | Confirmed (code) |
| `0x00164bb8` | `Gang_SetMsgHandlerName` | Interns the name for event *n* at `+0xe4 + n × 4` | Confirmed (code) |
| `0x00164c10` | `Gang_GetMsgHandler` | The interned handler for event *n* | Confirmed (code) |
| `0x001651b8` | `Gang_SetMusicTrackName` | Interns (or clears) name *n* (0-2) at `+0x14c`: the kind's three music tracks, copied at `GangCreate` | Confirmed (code) |
| `0x00165220` | `Gang_AddTurfBoxPtr` | Stores a turf box in the first free of the eight slots at `+0x15c`; `+0x17c` counts them | Confirmed (code) |
| `0x00165260` | `Gang_RemoveTurfBoxPtr` | Clears that box's slot and the count | Confirmed (code) |
| `0x001652a0` | `Gang_IsHumanInTurf` | `Gang_IsPointInTurf` at the human's position | Confirmed (code) |
| `0x001652e8` | `Gang_IsPointInTurf` | True when the gang has no turf boxes or the point is in one of them | Confirmed (code) |
| `0x00165368` | `Gang_HasIntruderInTurf` | True when this gang has no turf boxes, or a standing, uncuffed member of the other gang is inside one | Confirmed (code) |
| `0x00165430` | `Gang_AreMembersReady` | `GangGoodToGo`: false when a member (not of class role 6) is cuffed, in a scene, knocked out or in either flagged state, or, unless told otherwise, engaged | Confirmed (code) |
| `0x00165548` | `Gang_CountMembersWithGoal` | Members whose brain has goal *g* (`Brain_FindGoal`) | Confirmed (code) |
| `0x001655e0` | `Gang_StopTactic` | Stops and queues the tactic for freeing; its kind (vtable `+0x34`) kept at `+0xdd` | Confirmed (code) |
| `0x00165738` | `Gang_PickLeader` | Leader `+0x44`: first member that is a player (or with player 1 in a flagged state), or a cop class (`+0x11b` 12-13) not cuffed; else the first standing, uncuffed member; else the first handle | Confirmed (code) |
| `0x00165918` | `Gang_SetMembersInFormation` | Every AI member other than the leader joins (`Formation_Join`) or leaves the leader's formation | Confirmed (code) |
| `0x00165a20` | `Gang_AnyMemberHasEnemy` | Whether a member's enemy list (brain `+0x164`) is not empty | Confirmed (code) |
| `0x00165aa0` | `Gang_AnyMemberTargetsGang` | Whether any member's brain list `+0x164` holds a human of the given gang | Confirmed (code) |
| `0x00165b70` | `Gang_SetSharedTarget` | Target `+0x10`: checked through the leader's brain; unless friendly, every AI member adds it as an enemy. The null handle just clears it | Confirmed (code) |
| `0x00165c88` | `Gang_AnyMemberHolding` | Whether any member holds an object | Confirmed (code) |
| `0x00165cf8` | `Gang_SetSpotLine` | Speech hash `+0x608` (CRC of the name) said once on spotting a player; 0 for none | Confirmed (code) |
| `0x00165d40` | `Gang_SaySpotLine` | While flag `+0xd2` is set and the game is in play: a free member (the leader unless busy) that sees the nearest player of a gang the members target says the command line, or the stored line (`+0x608`, then cleared); the flag is cleared | Confirmed (code) |
| `0x00165f80` | `Gang_FindMemberForItem` | The nearest member within the radius of the point that is not busy and does not already hold an object of that name | Confirmed (code) |
| `0x001660e0` | `Gang_SetMembersBrainByte265` | Writes brain `+0x265` of every current member | Confirmed (code) |
| `0x00166220` | `Gang_CountStanding` | Members not knocked out, wounded or in flagged state `0x100000000` | Confirmed (code) |
| `0x00166308` | `Gang_AddMember` | Moves a human in: out of its old gang (its actions, target and enemies cleared), brain `+0x20c` set, its attack weights loaded, god mode when the gang is invincible, a player of flag `+0x3ac` made leader, type `0xed` gets its own anim substitution; event `0x16`. The cap (16 for kinds 1 and `0x17`, else 10) only calls a no-op (`0x0016d0e0`) | Confirmed (code) |
| `0x001664d8` | `Gang_RemoveMember` | Takes a human out of the list: when asked, its spawner's alive count (`+0x5c`) and the global count go down unless the spawner is in state 4, 6, 9 or 10; clears the leader; pops the brain to its base goal under a tactic; offers event `0x16` | Confirmed (code) |
| `0x00166b60` | `Gang_UpdateBounds` | Centre `+0x00` and radius `+0x14` of the box around the members not cuffed or knocked out | Confirmed (code) |
| `0x00166cf8` | `Gang_CheckReturnToCalm` | With no tactic and alert state 1: calm (state 0) once no member has a fight goal, or a chase goal with an active goal `0xc` | Confirmed (code) |
| `0x00166e10` | `Gang_RecordDownedForPlayer` | For the player whose gang this is: counts the AI members of brain type 3 knocked out and cuffed and adds them to stats 3 and 1 (`0x006fe490`) | Inferred |
| `0x00166fc8` | `Gang_SetName` | Copies the name (15 characters) to `+0x1c` | Confirmed (code) |
| `0x001672a8` | `GangSpawner_OnHumanGone` | A spawned human's brain is torn down: the spawner's alive count `+0x5c` and the global count go down, and so does `+0x5a` for a police dispatch spawner (4 or 6, with the game's dispatch count `+0x324`) or a responder spawner (9 or 10) | Confirmed (code) |
| `0x00167370` | `Gang_NextPlayerIndex` | `(+0xe1 + 1)` modulo the player count, 0 with one player | Confirmed (code) |
| `0x00168158` | `GangSpawner_ResetDelay` | Spawner *i*'s next spawn time `+0x64`: now + its delay `+0x60`, or + 100 ms in state 6 | Confirmed (code) |
| `0x00168aa0` | `Gang_FindSpawnerByName` | The slot of the active spawner with that name, or −1 | Confirmed (code) |
| `0x00168b58` | `Gang_HasSpawnerNamed` | Whether an active spawner has that name | Confirmed (code) |
| `0x00168be0` | `Gang_SetSpawnerStateByName` | The first active spawner of that name gets the state; state 2 also sets its end time (`+0x68` seconds), and its next time is now + `+0x60` | Confirmed (code) |
| `0x00168cd0` | `Gang_SetSpawnerState` | `GangStartSpawner`: the named spawner's state (0-5, 7, 8, 9, 11; others keep it), its value unless −1, next spawn now | Confirmed (code) |
| `0x00168e00` | `Gang_SetSpawnerValueByName` | `0x0016d758` on the spawner of that name | Confirmed (code) |
| `0x00168e68` | `GangSpawner_SetMustBeOffScreen` | The named spawner's flag `+0x8c` | Confirmed (code) |
| `0x00168eb8` | `GangSpawner_SetMaxConcurrent` | The named spawner's limit `+0x5a` | Confirmed (code) |
| `0x00168f08` | `Gang_WriteEnemyMask` | Writes the enemy mask `+0x38`; a change stamps the time at `+0x34` | Confirmed (code) |
| `0x00168f30` | `Gang_WriteFriendMask` | Writes the friend mask `+0x3c`, stamping `+0x34` likewise | Confirmed (code) |
| `0x00168f58` | `Gang_AreFriends` | True for the same gang or kind, both kinds in {1, `0x17`}, during a truce, or with the friend bit | Confirmed (code) |
| `0x00168fe0` | `Gang_IsEnemyOf` | False for kinds 1 and `0x17` together and during a global truce (`0x0050cb7c` in the future); else the enemy bit of the other gang's id | Confirmed (code) |
| `0x00169060` | `Gang_SetFriendBit` | Clears the other gang's enemy bit and sets its friend bit | Confirmed (code) |
| `0x001690c8` | `Gang_SetEnemyBit` | Clears the other gang's friend bit and sets its enemy bit (one way) | Confirmed (code) |
| `0x00169130` | `Gang_ClearRelationBits` | Clears the other gang's friend and enemy bits (neutral, one way) | Confirmed (code) |
| `0x001691a0` | `Gang_SayWarnLine` | Once (flag `+0xd0`), while warnings are on: the speaker says a warning picked by distance (over 12 m, or when forced), group size (1, 2-5, 6 and more), whether members already target the speaker's gang, whether one holds an object and whether the target is a player; the speech id goes to `+0x610` and, from far and not yet engaged, the target to `+0x614` | Confirmed (code) |
| `0x00169468` / `0x00169498` | `Gang_AddIdleFidgetClips` / `Gang_RemoveIdleFidgetClips` | Anim substitution group `0x25c` (4 slots): a random four of ten `fidget_*` clips (`0x0050cac8`) | Confirmed (code) |
| `0x001694b8` / `0x001694e8` | `Gang_AddCombatFidgetClips` / `Gang_RemoveCombatFidgetClips` | Group `0x25b` (8 slots): four `combat_fidget_*` clips (`0x0050cab8`) | Confirmed (code) |
| `0x00169508` / `0x00169570` | `Gang_AddFightTauntClips` / `Gang_RemoveFightTauntClips` | Installed while fighting: groups `0x253` (five `taunt*_adv`), `0x257` (a random three of five `gen_tnt_*`) and `0x256` (three `gen_tnt_cheer*`) | Confirmed (code) |
| `0x001695b8` / `0x001695e8` | `Gang_AddCheerClips` / `Gang_RemoveCheerClips` | Group `0x256` alone | Confirmed (code) |
| `0x00169608` | `Gang_CycleSpotLine` | Advances `+0x60c`, back to 1 past the given count | Confirmed (code) |
| `0x00169630` | `Gang_CycleWarnLine` | Advances `+0x60d`, which stays at 1 | Confirmed (code) |
| `0x00169658` | `Gang_AnyMemberHasAttackers` | Whether a member's brain has attackers | Confirmed (code) |
| `0x001696e0` | `Gang_IsBeingHunted` | Whether a gang (not of kind `0x17`/`0x18`) that has this one as enemy has a member chasing one of its humans (goal `0xc` active) or holding goal `0x75` or `0x76` | Confirmed (code) |
| `0x001698a0` | `Gang_SetTimer5EC` | `+0x5ec` = now + the given ms, or 0 | Confirmed (code) |
| `0x00169b00` | `Gang_CalmMembers` | Each AI member: target and enemies cleared, actions cleared, goals `0xf` and `0x41` popped, and goal `0x74` brought to the top and marked done | Confirmed (code) |
| `0x00169cc8` | `Gang_ClearMembersBrainByte290` | Brain `+0x290` = 0 on every member | Confirmed (code) |
| `0x00169d30` | `Gang_CheckNeutralRule` | A gang of kind `0x17` or `0x18` whose members all have threat response 0 is made neutral with the kind-0 gangs | Confirmed (code) |
| `0x00169dd8` | `Gang_NoneAbleToHelp` | True when the gang has members and none other than the given human is free (not cuffed, knocked out or wounded) | Confirmed (code) |
| `0x00169ea0` | `Gang_CanReachToHelp` | Whether a member other than the human, not cuffed or down, has a navigation route to the human's nav area | Confirmed (code) |
| `0x00169fe0` | `Gang_ProbeGroundFlag10` | Casts a ray down from 0.1 m above the point and reports a hit whose surface has flag `0x10` (`0x00249050`) | Confirmed (code) |
| `0x0016a000` | `Gang_SetInvincible` | `+0xd8` and god mode on every current member | Confirmed (code) |
| `0x0016a088` | `Gang_UpdateIdleFlag` | Flag `+0xdb` (only while `0x0050cab4` is set): with no tactic, set when no spawner is active and no member has a goal; with one, set when the tactic allows it and the given distance exceeds the radius + 20 m | Confirmed (code) |
| `0x0016a220` | `Gang_SetSuspendedById` | `GangSuspend`: byte `+0xd4` | Confirmed (code) |
| `0x0016a260` | `Gang_SetInvincibleById` | `GangInvincible` | Confirmed (code) |
| `0x0016a2a8` | `Gang_EnableAttackStrategiesById` | `GangEnableAttackStrategies`: byte `+0xd9` | Confirmed (code) |
| `0x0016a2e8` | `Gang_SetRespondPercentage` | `GangSetRespondPercentage`: byte `+0xd7` (30 after a reset) | Confirmed (code) |
| `0x0016a328` | `Gang_AddTurfBoxById` | `GangAddTurfBox`: only a volume box of kind 3 is added | Confirmed (code) |
| `0x0016a3a8` | `Gang_RemoveTurfBox` | `GangRemoveTurfBox`: `Gang_RemoveTurfBoxPtr` | Confirmed (code) |
| `0x0016a3f8` | `Gang_AddMemberByHandle` | `GangAddMember(gang, human)`: `Gang_AddMember` when the handle resolves | Confirmed (code) |
| `0x0016a458` | `Gang_GetHeadCountById` | `GangGetHeadCount`: `Gang_CountLiving`, 0 for id −1 | Confirmed (code) |
| `0x0016a4a8` | `Gang_GetStandingCount` | `GangGetStandingCount`: `Gang_CountStanding` | Confirmed (code) |
| `0x0016a4e8` | `Gang_IsGoodToGoById` | `GangGoodToGo`: `Gang_AreMembersReady`, 0 for id −1 | Confirmed (code) |
| `0x0016a538` | `Gang_SetLeaderById` | `GangSetLeader`: writes the leader handle `+0x44` | Confirmed (code) |
| `0x0016a578` | `Gang_GetLeaderById` | `GangGetLeader`: the leader's handle, or the null handle | Confirmed (code) |
| `0x0016a5e8` | `Gang_SetSightRange` | `GangSetLOS`: brain `+0x130` of every member | Confirmed (code) |
| `0x0016a670` | `Gang_ExitWorld` | `GangExitWorld(gang, flag, fn, delete)`: every member walks to the flag, or with none to the farthest exit flag (activity 8) in front of player 1 (`Goal_MoveToExitFlag`); with `delete`, flag `+0xd3` and the empty handler are set so the gang is freed when they are gone ([The gang update](#gang-update)). A gang with no living members is deleted at once, after calling `fn` with its id | Confirmed (code) |
| `0x0016a870` | `Gang_EngageEnemy` | `GangEngageEnemy(gang, target)`: an engage goal on the target for every member | Confirmed (code) |
| `0x0016a910` | `Gang_LookAt` | `GangLookAt(gang, target)`: each member's brain marked dead and flushed, then turns to the target after a random delay under 500 ms | Confirmed (code) |
| `0x0016aa98` | `Gang_SetMembersDeadFlag` | `GangBrDead`: `Brain_SetDead` on every member's brain | Confirmed (code) |
| `0x0016ab38` | `Gang_SetMsgHandlerById` | `GangSetMsgHandler`: `Gang_SetMsgHandlerName` on the gang ([Events](#gang-events)) | Confirmed (code) |
| `0x0016ab90` | `Gang_ClearHandlers` | `GangClearHandlers`: clears the gang's event handlers and every member's own script handlers | Confirmed (code) |
| `0x0016ac40` | `Gang_SetFriendById` | `GangSetFriend`: `Gang_SetFriendBit` one way | Confirmed (code) |
| `0x0016ac98` | `Gang_SetEnemyById` | `GangSetEnemy(a, b)`: `Gang_SetEnemyBit` one way | Confirmed (code) |
| `0x0016acf0` | `Gang_MakeEnemies` | `GangMakeEnemies(a, b)`: `Gang_SetEnemyBit` both ways; id −1 ignored | Confirmed (code) |
| `0x0016ad80` | `Gang_MakeFriends` | `GangMakeFriends(a, b)`: `Gang_SetFriendBit` both ways | Confirmed (code) |
| `0x0016ae10` | `Gang_SetNeutral` | `GangSetNeutral(a, b)`: `Gang_ClearRelationBits` one way | Confirmed (code) |
| `0x0016ae60` | `Gang_MakeEnemiesOfType` | `GangMakeEnemiesOfType`: `Gang_SetHostileToKind(gang, kind, 1)` | Confirmed (code) |
| `0x0016ae90` | `Gang_MakeNeutralOfTypeById` | `GangMakeNeutralOfType`: `Gang_MakeNeutralWithType(gang, kind, 0)` | Confirmed (code) |
| `0x0016aec0` | `Gang_AddSpawnerById` | `GangAddSpawner`: `Gang_AddSpawner` on the gang | Confirmed (code) |
| `0x0016afc8` | `Gang_StartSpawner` | `GangStartSpawner`: `Gang_SetSpawnerState` by gang id | Confirmed (code) |
| `0x0016b018` | `Gang_SetSpawnerModelById` | `GangSetSpawnerModel`: `Gang_SetSpawnerValueByName` | Confirmed (code) |
| `0x0016b070` | `Gang_SetSpawnerMustBeOffScreen` | `GangSetSpawnerMustBeOffScreen`, by gang id | Confirmed (code) |
| `0x0016b0b0` | `Gang_SetSpawnerMaxConcurrent` | `GangSetMaxConcurrent`, by gang id | Confirmed (code) |
| `0x0016b108` | `Cfg_SetGangMusic` | `CfgGangMusic`: the kind's three music track names in its configuration | Confirmed (code) |
| `0x0016b178` | `Cfg_SetGang` | `CfgGang`: the eight configuration values and the strategy of one gang kind ([CfgGang](../references/bindings/config.md#cfggang)) | Confirmed (code) |
| `0x0016b280` | `Gang_SetSpotLineById` | `GangSetCustomSpotDialog`: `Gang_SetSpotLine` | Confirmed (code) |
| `0x0016b2c8` | `Gang_ShowMembersIcon` | `GangAttachSpinningIcon`: the overhead icon on every member | Confirmed (code) |
| `0x0016b358` | `Gang_RemoveMembersIcon` | `GangRemoveSpinningIcon` | Confirmed (code) |
| `0x0016b3d0` | `Gang_SetThreatResponse` | `GangSetThreatResponse`: brain `+0x21c` of the current members only | Confirmed (code) |
| `0x0016b460` | `Gang_SetDamageResponseById` | `GangSetDamageResponse`: brain `+0x220` of every current member | Confirmed (code) |
| `0x0016b4f0` | `Gang_SetInvestigateResponseById` | `GangSetInvestigateResponse`: brain `+0x224` | Confirmed (code) |
| `0x0016b580` | `Gang_IsWantedById` | `GangIsWanted`: the wanted timer `+0x5e8` (kind 1) or `+0x5f0` (otherwise) is running | Confirmed (code) |
| `0x0016b5e0` | `Gang_ClearWantedById` | `GangClearWanted`: `Gang_ClearWanted(gang, 1)` | Confirmed (code) |
| `0x0016b618` | `Gang_HasSpawnerById` | `GangIsASpawner`: `Gang_HasSpawnerNamed`, 0 for id −1 | Confirmed (code) |
| `0x0016b678` | `Gang_QueueRespondersAt` | `GangQueueResponders`: when a spawner is in responder state 9 or 10 (`0x0016cb78(0)`), queues a gang call to the point; the target human's gang gets a 10 s second wanted timer | Confirmed (code) |
| `0x0016b728` | `Gang_Respond` | `GangRespond(gang, point, human)`: a non-police gang gets the cower substitution; every member gets brain `+0x28d` and a type-4 respond goal to the point and human; the human's gang gets a 10 s wanted timer (police) or second wanted timer | Confirmed (code) |
| `0x0016b8b0` | `Gangs_ClearRespondersBinding` | `GangClearResponders`: calls `Gangs_ClearResponders` | Confirmed (code) |
| `0x0016b980` | `Gang_SetCanSaveAlliesById` | `GangCanSaveAllys`: brain `+0x2d6` of every member (gang in use) | Confirmed (code) |
| `0x0016ba18` | `Gang_FlushBrainsById` | `GangBrFlush`: clears each member's goals and actions ([Deleting a gang](#gang-delete)) | Confirmed (code) |
| `0x0016bac0` | `Gang_SetTargetableById` | `GangSetTargetable`: brain `+0x120` | Confirmed (code) |
| `0x0016bbf0` | `Gang_SetHearRange` | `GangSetHearRange(gang, range, which)`: brain `+0x138` (which 1, default 20 m) or `+0x134` (default 50 m); −1 restores the default | Confirmed (code) |
| `0x0016bcf0` | `Gang_SetCanFlee` | `GangCanFlee`: byte `+0xdf` (1 after a reset) | Confirmed (code) |
| `0x0016bde8` | `Gang_SetAlwaysSeen` | `GangSetAlwaysSeen`: byte `+0xdc` | Confirmed (code) |
| `0x0016be30` | `Gang_SetCanUseWorldFlagsById` | `GangCanUseWorldFlags(gang, on, x)`: brain `+0x2d1` = on, `+0x2d2` = x (0 when off) | Confirmed (code) |
| `0x0016bee0` | `Gang_SetReactToViolence` | `GangSetReactToViolence`: brain `+0x267` of every member (gang in use) | Confirmed (code) |
| `0x0016bf78` | `Gang_ReactsToGang` | Whether the second gang reacts to the first: its flag `+0xde`, not of kind `0x17`/`0x18`, and the first is its enemy | Confirmed (code) |
| `0x0016bfc8` | `Gang_CanJoinFightAgainst` | The second gang is the first's enemy, fighting (or under tactic kind `0x14` with `+0x4c` 0), has living members and one able to help | Confirmed (code) |
| `0x0016c078` | `Gang_FindNearestOtherMember` | Sorts the members by distance to the human and returns the first that is not it | Confirmed (code) |
| `0x0016c120` | `Gang_ComputeFacing` | A heading for a human by mode: 2 random, 3 / 4 towards or from a handle's object, 5 / 6 towards or from a flag | Inferred |
| `0x0016c298` | `Gangs_ResetAll` | Ends the truce and resets all 32 records | Confirmed (code) |
| `0x0016c2f0` | `Gangs_DestroyAll` | Destroys the 32 records, then rebuilds the tactic pool | Confirmed (code) |
| `0x0016c358` | `Gangs_SetTruce` | Truce until now + the given ms (`0x0050cb7c`), 0 to end it | Confirmed (code) |
| `0x0016c470` | `Gang_MakeNeutralWithType` | `GangMakeNeutralOfType(gang, kind, calm)`: clears both relation bits with every gang of that kind that is not a friend either way; with `calm`, both sides are calmed, a 3 s truce starts and the crime report is cancelled; kind 0 clears brain byte `+0x290` | Confirmed (code) |
| `0x0016c730` | `Gangs_FindNearestEnemy` | The nearest gang in use (by centre) that is this gang's enemy, and its squared distance | Confirmed (code) |
| `0x0016c808` | `Gangs_FindNearestFighting` | The nearest fighting gang (or under tactic kind `0x14` with `+0x4c` 0) passing an optional filter, with living members and one able to help | Confirmed (code) |
| `0x0016c978` | `Gangs_FindNearestAttacked` | The nearest gang one of whose members has attackers | Confirmed (code) |
| `0x0016ca50` | `Gangs_FindWithinRadius` | The first gang whose centre is within the radius and whose own radius is not larger, passing an optional filter; writes it and the squared distance when asked | Confirmed (code) |
| `0x0016cb78` | `Spawners_AnyInState` | Whether any active spawner of any gang is in state 4 or 6 (kind 1) or in 9 or 10 (kind 0) | Confirmed (code) |
| `0x0016cc60` | `Gangs_ClearResponders` | `GangClearResponders`: each gang in use not of kind 1 with no second wanted timer is deleted if a member has goal `0x74`; one with the timer has it cleared and, when it is player 1's gang, the all-clear callback runs | Confirmed (code) |
| `0x0016cdf0` | `Gang_Create` | `GangCreate(kind, name)`: −1 when the name is taken; else the first free record is reset, put in use, calm, given the kind, name, id, spot-line flag `+0xd2`, the kind's three music track names (`CfgGangMusic`) and no active spawner; kinds 1, `0x17` and `0x18` get their own anim substitutions; a new kind-1 gang makes every wanted gang hostile to the police (unless game `+0x28c` is set) | Confirmed (code) |
| `0x0016d0e0` | `Gangs_ResolveAllMembers` | Resolves every member handle of every gang in use and discards the result (no effect) | Confirmed (code) |
| `0x0016d410` | `Gangs_FindFreeSlot` | The first record not in use, or −1 | Confirmed (code) |
| `0x0016d498` | `Gangs_FindByName` | The id of the gang in use with that name (`+0x1c`), or −1 | Confirmed (code) |
| `0x0016d548` | `Humans_PollKnockedOut` | Asks each of the 60 human handles whether it is knocked out and discards the answer (no effect) | Confirmed (code) |
| `0x0016d5c0` | `KillHumans_Stub` | `KillHumans`: does nothing | Confirmed (code) |
| `0x0016d5d0` | `Gangs_StaticInit` | File-scope initialiser: constructs the 32 gang records | Confirmed (code) |
| `0x0016d630` | `Gangs_GlobalCtor` | Calls `0x0016d5d0(1, 0xffff)` at start-up | Confirmed (code) |
| `0x0016d650` | `GangSpawner_Construct` | Clears a spawner (0x130 bytes): name, callback, anim and model 0, not in use, door and queue handles null, types 0, type index `+0x4c` 0 | Confirmed (code) |
| `0x0016d6d8` | `GangSpawner_SetName` | Interns the name at `+0x14` | Confirmed (code) |
| `0x0016d718` | `GangSpawner_SetCallback` | Interns the Lua callback name at `+0x18` | Confirmed (code) |
| `0x0016d758` | `GangSpawner_SetModel` | Interns the model name at `+0x20` | Confirmed (code) |
| `0x0016d798` | `GangSpawner_SetAnim` | Interns the anim name at `+0x1c` | Confirmed (code) |
| `0x0016d7d8` | `GangSpawner_SetTypes` | Copies the ten human types to `+0x24` | Confirmed (code) |
| `0x0016d808` | `GangSpawner_GetTypes` | The address of the types (`+0x24`) | Confirmed (code) |
| `0x0016db30` | `Gang_SetCopStation` | `SetCopStation(gang, spawner, ...)`: writes the twelve values to the named spawner's `+0x90`-`+0xa6` | Confirmed (code) |
| `0x0016dc70` | `Spawner_QueueCall` | Adds an entry to a spawner's dispatch queue (full at 4): due in the given seconds, the target, point, kind and count bytes, and the calling human's gang id | Confirmed (code) |
| `0x0016de30` | `GangSpawner_PopQueueEntry` | Clears the queue entry being served (`+0xac`) and counts it out of `+0xb0` | Confirmed (code) |
| `0x0016def8` | `GangSpawner_ClearQueue` | Empties the four dispatch queue entries | Confirmed (code) |
| `0x0016e0f0` | `Responders_QueueGangCall` | Picks the spawner in state 9 or 10 nearest the point (one with a full queue of 4 is skipped) and queues a type-4 call on it (`Spawner_QueueCall`); 0 when there is none | Confirmed (code) |

### The first mission's cast {#level99}

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

**The sparring Warriors' numbers** (classes 58-60), confirmed (runtime) on the three `CombatWarriors` in
[the sparring fight](#level99-fight): power class 40 (`GenWarrior` uses 39), whose record gives block chance `+0x08`
0.2, while hurt `+0x0c` 0.1, attack-delay factors `+0x1c` / `+0x20` 20 and 20, counter chance `+0x24` 0.08 and pattern
threshold `+0x37` 0. With the global `0x00510ac8` = 60 that is a 12 % block try (6 % hurt) and an 8 % counter roll;
the threshold 0 means `+0x15` is never set at Start, so the counter is rolled throughout a block. Their brains have
field of view 1.92 rad, range 30 m, melee range 3 m / 5 m and threat response 2, and the player's brain allows 4
attack slots.

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

### The class damage tables {#damage-tables}

`CfgChar`'s damage argument names one of eleven tables in `config_preload2.lua` (`DamageWeak`, `DamageNormal`,
`DamageTough`, `DamageBoss`, `DamageGhost`, `DamageCop`, `DamageRookieCop`, `DamageDestroyerLT`, `DamageMoonLT`,
`DamageWarriors`, `DamageFox`), 45 values each, indexed as [the damage table](combat.md#damage-table) (index = Lua
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

Rembrandt's `S1` of 15 (`DamageFox`) at 115 % gives the 17 seen at runtime ([Damage](combat.md#damage-table)).
Evidence: the values are read from the disc's compiled script (confirmed (code) for the reader); the index mapping is
confirmed (code) on Combat.

## What an implementer needs {#implementer}

Build in this order; each step is testable without the game.

1. **The shared input path.** Give every human a per-player record (command `+0x20`, stick angle and magnitude) and
   run the dispatcher and the locomotion for every human from it ([Tasks](tasks.md#humans-update)). An AI writes the
   record; nothing else about a human differs.
2. **The held flags** ([Tasks](tasks.md#held-flags)): without them an AI cannot know when its attack is over.
3. **The brain shell**: type from the class's `behaviour`, think one step in five (staggered by index), update every
   step, brains before the dispatcher.
4. **Goals and actions**: a goal stack of 10 with Start / Process / End / Resume and the return codes 0, 1, 2; an
   action queue of 8 with a delay, Start, Update and a refusable Abort; goals start only on an empty queue.
5. **The fight goal and the attack action** for `level99`'s fights: target, attack slots, the attack delay, the
   weighted pick from the class's `Att_*` table, move-to-human when out of reach, `Brain_QueueAttack`'s chains, and
   the attack action's wait on `0x5c0221f`. The command is written **once**, in Start (not every update); the attack
   delay is halved when the target targets the attacker; the target's `+0x1ec` takes a separate per-kind time, not
   the attack delay ([The attack action](#attack-action)). The move actions steer by a heading and speed in the brain
   (`+0x110`, `+0x114`), not by a stick ([Moving](#moving)).
6. **The block goal** and the counter, driven by the attacker's warning: `+0x200` counts events `0x10`, sent only to a
   human whose range and field of view (`+0x130`, `+0x12c`) take in the attacker ([Blocking](#block)). An AI human
   **never blocks**: the block goal's command 4 starts nothing, because the dispatcher starts a block only on R1 held on
   a real pad. Give it the no-reaction flag `0x800` (damage lands, no reaction; cleared from the goal's sixth update)
   and, on every update while the block time runs, the counter roll: command 3 at the counter chance, which counters
   only a grab (69-71 → 76) or a tackle (2-4 → 9) aimed at it.
7. **Reaction goals** for the states a fight produces (stunned, knocked down, grabbed), one update after the state.
8. **Scripted goals** for `level99`, in this order; each sub-step is testable on a synthetic level:
    1. **The route planner** ([Path planning](#path-planning)): the path polygons' inside test exists
       ([Level loading](level-loading.md#path-data)); add the walkable-line test, the C / D records as a graph, A\*
       with the edge costs and the 65000 cap, the retry and `0x80` detour, the route pool with its shortcuts and
       use counts.
    2. **The move action** on it ([The move action](#move-action), [Following](#route-follow)): straight when the line
       is walkable, else waypoints with the 0.25 m radius; the corner speed; the stuck test and brain `+0x284`. The
       search mask is `0xff`, and a leg of kind 8 or `0x80` is a climb through the player's `Climb_TryStart`
       ([link kinds](#route-follow)): `level99`'s Vermin crosses a fence that way.
       Steering round humans ([Steering](#steering)), choke points and [queues](#queues) can follow later: without
       them AIs only bump.
    3. **`GoalMoveToFlag`** ([GoalMoveToFlag](#move-to-flag)): offset target, arrival radius, the face-the-flag turn,
       re-planning when a move ends short, flag message 8 and the gang notice in End.
    4. **The turn actions** and `ActLookAt` ([Turning](#look-at)): heading to brain `+0x110`, done within 15° or
       after 3 s; the fourth argument is a start delay.
    5. **Lua callbacks** scheduled 33 ms after a goal's End with (handle, completed) ([Scripted goals](#scripted)), then
       `GoalPlayDynAnimation` (slot 668) and `GoalAddressPerson` with its scene goal `0x21`.
    6. **`GoalFight`, `BrFlush`, `BrDead`**: flush ends goals (callbacks fire); dead clears actions only.
    7. **Gangs** ([Gangs](#gangs)): records, enemy and friend masks, `GangBrDead`, `GangSetThreatResponse` on the
       current members, `Gang_OnEvent` before `Brain_OnEvent`, and `GangSetMsgHandler`'s event 18 with the count
       still standing (`level99` ends its fights on it).
    8. **Formations and `GoalTrackHuman`** ([Formations](#formations)): slot sets in 1/16 m, nearest-follower
       assignment, the 1 or 2 s re-plan, a slotless tracker that only turns.
    9. **Tactics**: the base (a tactic suppresses the fight goal), then `TacticCrowd` and `TacticTrigger`
       ([Tactics](#tactics)); `GoalDealer` ([GoalDealer](#dealer)) for `level99_lesson2`.

## Runtime checks {#runtime-checks}

All five were run (2026-10-05, PCSX2 2.9.94) with the scenarios `civ_fight`, `civ_block`, `civ_block_fov`,
`civ_approach`, `civ_knockdown` and `tick_split` in `repo:research/traces/scenarios/` and the call hooks in
`repo:research/traces/patches.toml`. Brain *i* is at `0x006d53f0 + i × 0x2f0`, its per-player record at `0x00660f50 + i
× 0x2c`. The results are in the body: [the fight and attack action](#attack-action), [think staggering](#update), [the
block](#block), [moving](#moving) and [reaction goals](#reaction-goals).

The first checks used the street civilian as a stand-in (same `Att_Normal` table, type 2 set by hand); the
sparring fight below then confirmed the Warriors' own numbers.

### The sparring fight {#level99-fight}

The scenarios `warriors_passive` (no input) and `warriors_block` (square every 15 updates) record `level99`'s
`CombatWarriors` fight as the script runs it: the three Warriors `Generic1`-`3` (classes 58-60, brain type 2,
`Att_Normal`) each get `GoalFight(warrior, player, 0)` from `P1.SendWarriors` when the scene `l99_c5` ends: in the
recording, 481 updates (16.0 s) after the saved state, which was taken just after `P1.SetupWarriors` started the
16.7 s scene ([Scenes](scenes.md#humans)). Results: [the attack delay](#attack-action), [the block](#block) and
[the cast's numbers](#level99).

**Who fights in lesson 12.** `P1.SendWarriors` (the scene's return function), in order: `GangBrDead(false)` for
the sparring gang and the first fence gang; `HuUseAnim(…, 0, nil)` on each `FenceWarriors1` and
`HuSetNoTarget(true)` on each `FenceWarriors2`; **`TacticCrowd(fence gang, nil, true)`** (cheering) for the two
fence gangs only; the tutorial text; `GangMakeEnemies(Warriors, CombatWarriors)`; then for each of the three
`CombatWarriors`: `HuSetPushable(true)`, `HuSetNoTarget(false)`, `HuSetDemiGodMode(true, 0.25)` and
`GoalFight(warrior, player, 0)`; then the 50 s stopwatch (`P1.TimesUp`) and the grab hint's anim callbacks
(inferred from the disassembly of `level99_combat.lua`). The gangs are made in this order, so their ids are 0
`Warriors`, 1 `CombatEnemy`, 2 `CombatEnemy2`, 3 `CombatTeacher`, 4 `CombatWarriors`, 5 and 6 the fence
Warriors, as read at runtime ([the cast while the scene plays](#level99-scene-state)). So:

- The sparring Warriors' gang (4) has **no tactic**, and no script sets its threat response: it keeps the **2**
  every brain gets when it is made (`0x0028a570`). The scripts set 0 only on `CombatEnemy`, `CombatEnemy2` and
  `CombatTeacher` (and 2 on `CombatEnemy2` when its fight starts). Their `GoalFight` therefore runs in full:
  `Brain_Fight` adds the player as an enemy, takes him as the target and `Brain_PushFightGoal` pushes FindEnemy,
  Melee and the fight goal ([Starting a fight](#targets)). The three take the player on together (the player's brain
  allows 4 attack slots), demi-gods at a quarter of their health, until the stopwatch ends the lesson. Confirmed
  (runtime): they fought in the `warriors_*` recordings.
- **How they reach him.** They stand 8.6-9.0 m away, beyond 1.1 × their 5 m far range ([Melee
  ranges](#melee-range)), so each fight goal ends at its first update and the Melee goal sends them in with an
  [EngageEnemy run](#engage-enemy); a fight goal (4000 ms) follows once each is in range
  ([Closing on the target](#fight-approach)). Nothing else in the lesson gates them: brain type 2, threat response
  2, no tactic, the player reachable (`+0x11e` = 1) and no wanted timer on the gang. Confirmed (runtime), PCSX2
  2.9.94, the `warriors_passive` state with each Warrior's goal stack, position and `+0x140` read every update over
  PINE (an analyst run, not a committed scenario): 490 updates after the state, all three had **FindEnemy, Melee,
  EngageEnemy** (top) at 8.96, 8.59 and 8.77 m, far range 5.0. They ran in at up to **7.80 m/s** (the run gait;
  one slowed to about 2 m/s for some 20 updates while steering round another). The first reached the player and
  stayed at 0.71 m with an action running (inferred: the run-in's charge, step 11); its EngageEnemy ended at update
  564. The other two stopped at 2.1-2.6 m and swapped EngageEnemy for a fight goal at updates 546 and 562. Each stack
  then read **FindEnemy, Melee, Fight**.
- **Why the other two stop short** (confirmed (runtime), PCSX2 2.9.94, the same state, the three Warriors' and the
  player's state word, gait, speed `+0x1ac` and clip read every update; [EngageEnemy](#engage-enemy) step 9 confirmed
  (code) at `0x002afa48`). The first Warrior's run-in ends in the charge (clip 0), which hits the player and plays the
  extreme reaction 296 on him: his state word becomes `0x180000`, inside the busy mask `0x7bf9e9f7ff0` that step 9
  tests (`HumanRecord_AreActionsBlocked`), and stays so through 296 and the getting-up 198. Step 9 runs only on a
  re-plan (step 6: every 250 ms, or on the target's turn, slowing or coming within 1.6 m), so each runner stops at the
  first re-plan that finds the player busy within 0.75 × far (3.75 m): one 4-5 updates after the hit, at 2.12 m (the
  player had been free until then, so the 3.75 m line had passed), the other 5 updates after crossing 3.75 m, at
  3.15 m. The stop sets the move speed to 0 (`0x0028aac0`) and the run (410, 7.80 m/s) **ramps down by 1.067 m/s per
  update to 0 in 7 updates**, about 0.83 m, with no run-stop clip (410 straight to the idle 388); they stood at 2.10
  and 2.49 m, the player flung farther meanwhile. Then the stop's turn to face (0.3 s), EngageEnemy ends, and the
  fight goal walks them in with the combat-walk clips (380, 387; 3.43 m/s) while he is still down. So the 2.1-2.6 m
  is not a distance of its own: it is where a 3.75 m test, sampled every 250 ms and started when the player became
  busy, leaves a 0.83 m stop. Analyst run (not a committed scenario), from 470 updates after the state.
- The fence gangs (5, 6) get the **crowd tactic** and no `GoalFight`: they cheer from the fence
  ([TacticCrowd](#tactics)). They are friends of the Warriors' gang.
- **Which wins**, confirmed (code) at `0x0028d2e8` and `0x0028d190`: a threat response of 0 stops `GoalFight`
  completely (`Brain_Fight` returns at once: no enemy, no target, no goal); under a gang tactic it adds the enemy
  and takes the target, but **pushes no fight goal**, and the tactic decides what the members do. A scripted
  `GoalFight` never overrides either. Neither applies to the sparring Warriors.

#### The second wave in the snap lesson {#level99-snaps}

The second wave (`CombatEnemy2`'s `SecondWave01`-`04`, bum models) is never sent to fight in lesson 7: it **surrounds
the player and stands there**, so that whichever way he snaps, a bum is there to hit
([the lesson](scripting.md#level99-lessons)).

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
  ([Attacks](combat.md#attacks)) on every side.

#### Reaching the fight without a playthrough {#level99-save}

The state is not a quick-save slot; it is made once and named by the scenarios' `state`
([Recording a trace](../guides/research-workflow.md#recording-a-trace)). With the `call-brains` hook on a copy of a
`level99` state (slot 1 is checkpoint 3):

1. Call `0x0041abc0(1)` (`SetCheckPoint`'s worker), then `0x00160d78(name)` (`MenuLoadLevel`'s worker) with
   `"level99"` written to free memory: the level reloads at checkpoint 1, and its tutorial starts about 60 s later.
2. Schedule script functions by name with `0x003863d8(name, delayMs)` (`ScheduleFunc`'s worker), the name in free
   memory: `AddFenceWarriors1`; then `GangMakeFriends`'s worker `0x0016ad80(0, fenceGang)` (the fence gang's id, 5
   here, is brain `+0x20c`'s); then `P1.SetupWarriors`.
3. Save while the scene plays ([Driving PCSX2](../guides/research-workflow.md#driving-pcsx2)), and put the patched
   words back before using it as a source.

#### The cast while the scene plays {#level99-scene-state}

Confirmed (runtime), in that state (`l99-warriors-before-fight`) read both from the state file and over PINE; type ids
read from each goal vtable's `+0x0c` function, confirmed (code) at the constructors in [Goals](#goals):

- Brains 0-18 are in use. Everyone in the scene (Rembrandt, Vermin, Cleon, `Generic1`-`3`) is dead to the AI (`+0x09`
  = 1, think the stub `0x004edd40`) with one goal, **JoinCinematic** (`0x29`), whose actions are a move (vtable
  `0x00542f20`, `MoveAction_Init`'s) and then a turn (vtable `0x005430e0`, the one `Action_TurnToTarget` makes).
- Ash (type 3) is suspended (`+0x0a` = 1) but not dead, holding one goal, **HoldPosition** (`0x36`).
- `GenWarrior` and the second wave (type 4) and the two teachers have threat response 0; the sparring and fence
  Warriors 2. The fence Warriors (gangs 5, 6, type 2) are the only brains running their own think (`0x00304608`).
- Gangs (record order = id): 0 the player and Ash, kind 0, alert 1, enemies 1 and 2, friends 5 and 6, with a
  **tactic of type 3** (vtable `0x005439e0`, init `0x00313490`); 1 `GenWarrior`, 2 the second wave (both kind 19,
  enemy 0); 3 the teachers and 4 the three `CombatWarriors` (kind 19, no masks yet: `GangMakeEnemies` comes with the
  fight); 5 and 6 the fence Warriors (kind 0, friends with 0).
- Inferred: the type-3 tactic is set by `0x00313400(…, gang id, …)` from `0x0041c4e0`, which `Human_MakePlayer`
  (`0x00229c40`), `Human_SetWarChief` and `Gang_OnEvent` call: the player gang's standing tactic, and the source of
  Ash's HoldPosition (`0x00313718` pushes it).

### Game-state functions {#warriors-functions}

Functions of the game-state module (`0x00417af0`-`0x00424e50`: inventory, statistics, flags, configuration
workers) that belong to this page, by address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00419ee0` | `GameState_FindNearestPlayer` | the nearest listed player to a point, the squared distance out; 40 callers in the AI | confirmed (code) |
| `0x0041b4f0` | `GameState_SetWarriorCommandName` | the name at `+0x2fc` that `GameState_CallWarriorCommandCallback` calls with (human, command) ([Warrior commands](#warrior-commands)) | confirmed (code) |
| `0x0041b5e0` | `GameState_CallWarriorCommandCallback` | from `WarriorCommand_Dispatch` | confirmed (code) |
| `0x0041c2b0` / `0x0041c2d0` | `GameState_TurnWarriorCommands` / `GameState_IssueWarriorCommand` | the workers of `TurnWarriorCommands` (all on or off) and `IssueWarriorCommand` (for player 1) | confirmed (code) |
| `0x0041c338` | `WarChief_CheckCrewInRange` | for a war chief: when the nearest crew member is within 50 m clears byte `+0x414 + player` and returns 0; otherwise (unless told to be quiet) says command `0x12`, or `0x5c` when a member is cuffed or out, sets the byte and returns 1; from `WarriorCommand_Dispatch` and `PlayerBrain_Update`; `GameState_CheckGameOver` reads the byte | confirmed (code) |
| `0x0041c4c8` | `GameState_StoreWarriorCommand` | a player's last command (`+0x41a`) and its argument (`+0x42c`) | confirmed (code) |
| `0x0041d080` / `0x0041d088` | `GameState_SetDealerCustomer` / `GameState_IsDealerFree` | the handle at `+0x284` (set by the dealer goal and the scout tactic) / whether it no longer resolves | confirmed (code); role inferred |
| `0x0041d738` / `0x0041d748` | `Cfg_SetEnemySpotting` / `Cfg_SetWarriorSpotting` | `CfgSetEnemySpotting`: `+0x56f8`; `CfgSetWarriorSpotting` clears `+0x56fc` whatever its argument | confirmed (code) |
| `0x0041d758` / `0x0041d770` | `GameState_SetWarriorVandalize` / `GameState_SetWarriorWeapons` | `CfgSetWarriorVandalize` (`+0x5700`) and `CfgWarriorWeapons` (`+0x5704`) | confirmed (code) |
| `0x0041d978` | `Cfg_SetExcludedVandalizeZone` | `CfgExcludedVandalizeZone`: `0x005148bc` (`0xffff` at set-up) | confirmed (code) |
| `0x0041da50` / `0x0041da60` / `0x0041da70` | `Cfg_SetTurfInvasion` / `Cfg_SetGrappleCounters` / `GameState_SetPowerupPickup` | `CfgEnableTurfInvasion` (byte `+0x56e1`), `CfgEnableGrappleCounters` (`+0x56e3`), `CfgPowerupPickup` (`+0x56e5`) | confirmed (code) |
| `0x0041db08` / `0x0041dbe8` | `GameState_EnableAllWarriorCommands` / `GameState_EnableWarriorCommand` | the per-player allowed bytes `+0x41e + player × 7 + command`: all seven for every war chief / one for one human; both tell the command display (`WarCommandDisplay_SetAllowed`) | confirmed (code) |
| `0x0041dc80` / `0x0041dcf0` / `0x0041dd40` | `GameState_IssueWarriorCommandFor` / `GameState_LockWarriorCommands` / `GameState_SetWarriorCommandCallback` | `WarriorCommand_Dispatch` for a human / byte `+0x418 + player` / the callback name | confirmed (code) |
| `0x0041dd68` | `GameState_SetAutoSwitch` | `WCEnableAutomaticSwitching`: byte `+0x431` (1 at set-up), which turns the war chief's [automatic commands](#warrior-auto-commands) on or off | confirmed (code) |

## Coney's implementation {#coney}

Steps 3-8 of [What an implementer needs](#implementer) are in `repo:src/ai/` (the path data
in `repo:src/world/path_map.h`), each original function tagged with `@orig` in the code; tests in `repo:tests/ai/` and
`repo:tests/world/path_map_test.cpp`.

- **Brains** (`Brains`, `Brain`): a type from the class's behaviour byte, a think one step in five staggered by slot,
  an update every step at the brains' place in the characters' step (`Humans::setBrains`), so before every
  dispatcher. The player has a type-0 brain that only keeps his enemies, which feed `Player::setNearestEnemy`.
- **Goals and actions**: a stack of 10 (Start, Process, End, Resume; Stop, Again, Done) and a circular queue of 8
  (a delay, Start, Update, an Abort that can refuse); goals start or resume only on an empty queue. A goal leaves the
  stack before its End runs: an End can reach Lua (a flag's message), whose handler may push the next goal (level99's
  `P1.ReachCenter` pushes `GoalAddressPerson`), which must stay on the stack.
- **Fighting**: `FightGoal` in the original's order (`repo:src/ai/fight_goal.h`): the valid-enemy and slot check, the
  tackle try, the 1.1 × far range, the walkable line every 30 updates, the re-target, the block try, the wait on the
  actions, the deadline, the pick, `Brain_CheckAttack`, then the ring, the move into the kind's reach or the grab and
  snap try and the press. `Brain_QueueAttack`'s chains are timed by the chain clip's first event; `AttackAction`
  presses the command once in Start (the delay halved when the target targets the attacker or the brain is type 3,
  then a wait on `0x5c0221f`), with a full stick along its angle when it has one, and, when neither human is busy,
  moves the target's `+0x1ec` on by the kind's swing time over his spacing. `MoveToHumanAction` sets a heading and
  speed in the record's `move` (no stick).
- **The attack choice** (`repo:src/ai/attack_choice.h`, `repo:src/ai/attack_views.h`): `Human_CanUseAttackKind`'s table
  and `Human_CanStartAttack`'s guard as pure tests over two views of the humans, and `Brain_PickAttack`'s draw with
  every adjustment (the running rule, the hurt rule, the crowd on A, the busy target for the grab, the armed bonus,
  the rear-grabbed bonus, the pattern read). `attackerViewOf` and `targetViewOf` fill the views from Coney's fighters.
  **Stand-ins**: "free" is on its feet in no pair and not reacting; no AI holds an object, spray paint or cuffs; the
  pattern read is not made; the kinds whose commands no Coney handler takes (8, 9, 11-13, 15, 18, 23, 34, 36, 41,
  44) are left out of every pick.
- **Taking turns** (`repo:src/ai/attack_places.h`, `repo:src/ai/fight_book.h`, `repo:src/ai/fight_checks.h`): each
  brain keeps four active-attacker places, the two spacing bytes (raised by each slot claim to the attacker gang's
  `CfgGang` values 2 and 3, a Warrior target keeping 1 standing, back to 1 when its slot list empties) and the tackle
  meter, kept by the cops', gang soldiers' and Warriors' thinks. `CfgGang`'s values 2, 3, 5, 7 and 8 are read per
  gang kind from the scripts' calls. `Brain_CheckAttack` answers in the page's order; a waiting attacker holds the
  reposition ring. **Stand-ins**: the rows about held objects (4, 6, 8) never apply; the police row applies when the
  target's own target is a cop; the ring is a move to its outer edge, standing within it, with no taunt; the fight
  stance is not built (the charge and the dive count as out of it); the sectors are the target's quarters, so "behind"
  is his rear quarter and the snap's man is any attacker within 2.5 m beside or behind; the swing time reads the
  attacker's own set, not the bat and bottle sets' clips (25 and 29 the throws, 26-28 the power strikes, 31 the
  struggle strikes 96 and 108, 32-34 the escapes 100 and 112).
- **A fight's three goals** ([Closing on the target](#fight-approach), `repo:src/ai/melee_goal.h`,
  `repo:src/ai/engage_goals.h`): `Brain::fight` (with a duration, `GoalFight`'s none, a rioter's 8000 ms) pops any Melee
  or FindEnemy goal with everything above it and pushes FindEnemy, Melee and the fight goal, whose deadline is the
  duration. The fight goal never closes a distance; the **Melee** goal (no limit or the duration from its first run)
  takes the best-scoring enemy (below), pushes a fight goal of 4000 ms within 1.1 × the far range (after a walk at gait
  2 when the straight line does not reach him) and an **EngageEnemy** goal beyond it; **FindEnemy** starts the fight
  again while the target can be fought. EngageEnemy runs at gait 4 (5 after a runner), re-plans every 250 ms, leads a
  target facing away, stops and turns to him within 3.75 m when he is busy or the charge is not armed and he walks or
  stands, and attacks out of the run within 1.6 m with the charge armed: a cop's X1 (50 %, 80 % at a gang member) or
  tackle, anyone else's pick, which ends the run-in when it is not a charge kind, pressed as one attack action once it
  can start (the X1 along the runner's heading); another attacker nearer the target on the same side of him disarms
  the charge (the sector's stand-in). EngageEnemy stops for a target in sight it may
  not attack (`Brain_IsAttackableBy`), re-targets the nearest enemy it sees within 9 m, and gives up out of sight 20 m
  away. **Stand-ins**: the gang's wanted timer is not kept (the chase always allowed); Melee's own line-of-sight branch
  is not built; with no target but a valid enemy Melee spectates (without the shadow and may-spectate tests); a
human whose last move failed runs straight at
  the target for 2 s and may approach again (the weapon pick-up, throw and positioning moves are not traced); the type-3
  AttackTarget gate and goal `0x35` are not built; past its deadline a fight goal ends only without an attack slot (the
  slot standing in for the target's active attackers). `level99`'s sparring Warriors are sent from 8.6-9.0 m ([the
  sparring fight](#level99-fight)) and run in this way: `coney_tests "the disc's level99: the sparring Warriors*"`
  checks that all three run in with EngageEnemy, close in and attack.
- **Sight and targeting** (`repo:src/ai/perception.h`, `repo:src/ai/targeting.h`, [Sight](#sight),
  [the score](#enemy-score)): the line of sight is the two rays (1.7 m, then 1.0 m) through the level's collision mesh,
  passing the six see-through materials; the field of view and `Human_CanSeeHuman` (range, then the line); the brains
  get the mesh from `Brains::setCollision`, and an attack warning needs the line too. `Brain_ValidateEnemy`,
  `Brain_CanBeChased`, `Brain_IsAttackableBy` (the attackable byte `+0x11f`, `GangSetAttackable`, and the street
  civilian and dog exceptions), `Brain_CanTakeSlotOn`, `Brain_ScoreEnemy` with every term and the weights of
  `CfgSetTargetingPoints` / `CfgSetTargetingPointsEx` (read from the scripts' calls), and `Brain_PickBestEnemy` with
  the goals' adjustment (the previous target's 3 points). **Stand-ins**: Coney has no shadows, trains, fires,
  muggings, interrogations, tagging or turf-only rule, so those tests pass and those terms give nothing; only the
  level's static collision blocks a sight ray; a gang's chosen target (`+0x10`) is kept but nothing sets it yet.
- **Blocking**: `BlockGoal` never produces a block: Coney's block starts only on R1 held in the record's buttons,
  which only a pad writes, so its command 4 does nothing. It turns the human's hit reactions off
  (`Fighter::setHitReactionsOff`, bit `0x800`) from its start until its sixth update with the human free; rolls the
  counter on every update of the block time (`BlockGoal::counterRoll`: the roll, then `counterTest`: a type-3 brain,
  free and not hurt, face to face with a target aiming at it in a grab's or a tackle's intro) and writes command 3 on
  success; extends the block while the target still attacks, which stops the rolls and queues a punishing attack in
  the block's last second.
- **The counter** (`repo:src/combat/ai_counter.h`, `Fighter::answerCounter`): command 3 from a human no pad drives,
  free, unhurt and empty-handed, is kept for one update; the human in a grab's or tackle's intro at it takes it on its
  own update when the two face each other, and the pair plays: the hold ends, the counterer plays 76 (9 against a
  tackle) and the attacker 77 (10) from the counterer's set, takes the counter's damage from the counterer's Anim
  Range List and stands stunned after it. **Coney readings**: the class-13 way through the type gate is left out and
  its byte `*(0x0051489c) + 0x56e3` taken as 0, so only the Warriors' brains counter; the tackle counter ends as the
  grab's (stunned); no power is spent; Coney's tackle holds its victim from the intro's start, whose reaction goal
  then holds the block goal off, so in play only grabs are countered.
- **Reactions**: grabbing, tackling, grabbed, knocked down and stunned, one update after the state.
- **Configuration** (`aiConfigFrom`): the class's `CfgChar`, power class 40's `CfgPowerClass`, `CfgAttackDelay` and
  `CfgBaseChanceToBlock` from the scripts' recorded calls. Without the disc the reference values stand in: power
  class 40's AI fields (block 0.2, 0.1 hurt, delay factors 20, counter 0.08), base block chance 60, the
  `config_preload2` delays, `Att_Normal`.
- **Play**: `fighter` lines in sandbox layouts ([Sandbox](../guides/sandbox.md#ai-fighters)), the debug menu's *AI
  fighters* page, and a level's scripted cast. Gameplay (`repo:src/gamemodes/gameplay_mode.h`) makes the level's
  `Brains` and `ScriptedBrains` before its script runs and holds every AI call on a human (`ScriptedBrains::hold`)
  until the play mode has loaded the level and made the humans; `release` then makes each `HuCreate`d human (player 1
  bound to the player's brain, the others AI humans of the class `aiConfigFrom` gives their type, drawn with their own
  model, `repo:src/platform/play_level_cast.cpp`) and replays the held calls in order. A later `HuCreate` of player 1
  is an AI human until `HuChangePlayerGang` hands player 1 over ([Characters](characters.md#coneys-implementation)),
  and `GangDelete` takes every member but player 1 out of the world at once ([Deleting a gang](#gang-delete)).
  A teleport moves an AI human;
  `GetPosition` and the look-ats read a human's live position.
- **Routes** (`world::PathMap`, `RoutePlanner`): the path data decoded ([Path data](level-loading.md#path-data)), the
  inside test, the walkable-line test, the request (the human's polygon, the straight line, the ends' nodes within 30
  tries), A\* from the destination's node with the edge costs and the 65000 cap, the retry with `0x8c` and the jump
  detour, and routes from a pool of 32 with the leading, shortcut (4 ahead) and trailing cuts and the nodes' use
  counts. A brain gets the level's planner from `Brains::setPlanner` (the play mode gives one from the level file's
  path data, `PlayScenery::pathMap`); with none, every move goes straight.
- **Moving** (`MoveAction`, `RouteFollower`): straight when the line is walkable, else the route's waypoints (0.25 m,
  moving on when reached and every 6th call, skipping what is in a straight line); the straight re-check every 30
  updates; the turn on the spot beyond 30° while standing; the corner speed; the stuck test; brain `+0x284`
  (`Brain::moveFailure`). A human its brain drives moves by the AI's own rules (`human::Human::aiLocomote`, the player's
  locomotion unchanged): a constant turn step by gait (12° walking or standing, 6° jogging, 4° running, 2.5° sprinting,
  0.375° wounded) times the turn boost plus one, or divided by one minus a negative boost (`Brain::setTurnBoost`,
  [`+0x0b`](#brain-boosts)), with no easing; a speed that starts at the asked speed up to 2 m/s, gains 8 m/s² (32 while
  more than 2 m/s short) and loses 32 m/s²; a wounded human asked to move walks. The move's corner speed predicts the
  turn with the same steps. **Stand-in**: a standing human more than 15° off turns at the standing step instead of
  playing turn clips 395-398. Every search uses the mask `0xff`; a leg of kind 8 or `0x80` is a climb: the human runs at
  the waypoint (gait 4) and tries the player's climb start toward it each update (`PlayerRecord::climbToward`), giving
  up after 31 failed updates; a leg whose avoid bit is set is refused, but for a charge. `level99`'s Vermin climbs his
  fence this way.
- **Neighbour sectors** (`Sectors`, `repo:src/ai/sectors.h`, `Brain::sectors`): eight 45° sectors per brain (0 ahead,
  rising anticlockwise; an edge goes to the lower index on the left half), rebuilt when the caller's age (1000 or 500
  ms) has passed: each human within 1.5 m counted, the nearest kept, the flags from his squared distance (3 below 1.5,
  1 below 2.5), the free-player flag 8 for an AI within 5.5 m; the lazy wall probe 1.5 m out at the stored heading along
  the path data's walkable line; the readers (blocked, free, wall, cost, all clear, held by, the turn way) and the
  point in a sector. **Coney choices**: a record never built is always rebuilt (the original's clock is far past 0);
  a player is busy for flag 8 when not standing or with a busy record bit; with no trains, the probe's train test
  never fails.
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
  ([Scripts: message handlers](scripting.md#message-handlers)).
- **Gangs** (`Gangs`, `Gang`, in `Brains`): 32 records with kind, name, enemy and friend masks, members (10, 16 for
  the police kinds), suspension, message handlers and a tactic; `Gangs::friends` and `enemies`; the whole-gang
  switches (`GangBrDead`, `GangBrFlush`, `GangSetThreatResponse` on the current members, `GangSuspend`). An event goes
  to the human's gang before its brain (`deliverEvent`): the handler for 18, 2 and `0x11` with (member, other, value),
  any other id used when the call returns true, then the tactic. `Brains::update` sends event 18 once when a member's
  health runs out, so `level99`'s `P1.BumDied` gets the count still standing (`Gang::standing`).
- **Formations** (`Formations`, `Formation`, stepped before the gangs): sets of 9 slots in 1/16 m turned by the
  leader's heading, the plan when he stops, every 1 or 2 s or 1 m from the plan point, the nearest follower per
  usable slot, the rest queued nearest-first behind, and the crossing-paths swap.
- **Tactics** (`Tactic`, stepped by the gangs): started on the first update with the AI members flushed, 2 past the
  time limit, the callback with (gang id, code); a gang with a tactic gets no fight goal from `GoalFight`.
  `TacticCrowd` seats its members (idle and fightless when cheering, spectating 4-6 s when watching), gestures,
  cheers in turn and reacts (a clip, then the cheer) on its tick, on `TacticTrigger` and on violence nearby.
- **Disc check (NTSC-U, counts only):** `coney_tests "[disc][routes]"` decodes all 64 levels' path data; 42,373 of the
  43,234 route nodes lie inside the polygon that owns them. Of 500 seeded pairs of `level99`'s 415 nodes, 101 are a
  straight line, 52 routed (200 route nodes), 34 refused (a polygon off the graph), 151 linked only over flag `0x10`
  edges, which the mask `0x3` Coney once used left out (the original's and now Coney's is `0xff`,
  [Path planning](#path-planning)), and 162 not linked at all.

**Coney choices.** A fighter is class 58 (brain type 2, 1400 health) with the sparring Warriors' runtime brain values (4
attack slots, melee 3 / 5 m, sight 30 m, field of view 1.92 rad), drawn and animated as the player's character, in a
gang of kind 19 made the enemy of the player's (the Warriors' kind), and an "engaging" toggle stands in for the script's
`GoalFight` (an idle fighter takes the player on within its far melee range). Who fights whom in the characters' step is
whoever's gangs are not friends (`Humans::setOpposition`); without gangs, the humans added pad-controlled and the others
fight each other, and only the former fight the sandbox's passive targets. A target is in reach within 0.9 × its first
attack's far range; a move runs beyond 4 m, lasts 1000 or 2000 ms (2000 beyond twice the reach) and stops at 0.9 × the
reach. Command `0x11` chains as
square. A reaction goal clears the actions and the move. The fight reaction goals (grabbing, mounting, grabbed,
mounted, grounded) are built in `repo:src/ai/fight_reactions.h`; their stand-ins: no trains, no presenting to a
friendly player or front-grab hand-over press, the throw direction only the random left, ahead or right, no help call,
and a held AI's presses reach no handler (its holder drives it). A block ends when its target is not on its feet
(Coney has no state word). Each brain's generator is seeded by its slot.
A think only counts (the types' think handlers are not traced).

**Coney choices for moving.** The inside test counts an edge going down in y as +1 (the sign under which the route
nodes lie in their polygons; the clockwise polygons then contain nothing). A polygon's A record takes the next nodes
in order (the counts add up to the C records in every file; `+0x08` does not always hold the running index).
"Fails at 128 nodes" is the open list's size (counted as nodes closed, a third of
`level99`'s reachable pairs failed). The use term is 40 × uses − 8 on the node entered; `0x0051059c` is 1 and
`0x005105a0` 0. An end off every polygon counts on the nearest within 1 m and takes its nearest node; the shortcut
takes any edge that links back. The walkable line crosses every edge of the two areas rather than the slab lists',
finds the start's area from the point, and has no hazard spheres (Coney has no fire to add them). A climb leg's climb
that has ended moves the
follower on past the leg's waypoint (the original's step there is open); the fast climber's early start within
4.5 m, the charge (`0x40`), the link's clear test and the waypoint claims are not built. A jump leg (kind 4) takes
its take-off and landing points, the drop or the arc and the 1.5 m landing test as above; the brain does not run while
the human is airborne, so the move notices the landing by the human's landing count, and the arc counts as having
reached the landing point when it lands within the waypoint radius plus one update's travel at the launch's plan
speed (the original tests the arrival in the air, every update).
A corner is the turn at the next two waypoints, simulated as an arc at the gait's turn rate
from the waypoint, the trial falling by 1 m/s; the braking distance is 0.5 s at the first corner's speed, within which
the slower of the two corners' speeds is used. A move clears `+0x284` at its start and waits while the human is busy
(`Human_IsBusy`). The look-at's turn value is kept, not read; no turn is ever refused its abort.

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
attacker and the damage as its value. A formation slot is usable when the leader's planner finds the
line to it walkable (always without a planner) and keeps the leader's height. The idle goal stands still (its Process is
not traced); the spectate goal follows [its research](#spectate) for Melee and the dealer's wary goal, the crowd's
spectators standing still (their arguments are not traced). Its stand-ins: a spectator holds no target but for the
EngageEnemy run-in; no fight stance, taunt or weapon pick-up; the watch action is a turn to face him; the keep-distance
move is a move action to the band's edge; the chase is always allowed; the tackling man's kind-`0x24` attack and the
shadow test are not built; without join the watched man is the nearest member of any other gang. Both of a crowd
reaction's clip actions are `PlayAnimAction`. The dealer rolls
dirty at Start; his wary scan looks for members of enemy gangs. Player 1's triangle within 1.75 m of a dealer whose offer
stands (after the objects' prompts and the stereos) makes the deal: the table's terms against the inventory, the item
and money moved without notifying the inventory callbacks, and his line said at the buyer (96 at most every 5 s, 97,
101, 105); the gestures, the shove and the pair `money_take.anm` / `money_give.anm` are not played, so a sale
completes at once (as the original does when the pair cannot load).

**The Rumble tactics** (`src/ai/tactic_attack.*`, `src/ai/tactic_confront.*`, [Rumble mode](rumble.md#coney)).
`TacticAttack` and `TacticConfront` follow the table above. **Stand-ins:** the tactic's melee goal (8,
`TacticMeleeGoal`) takes the nearest member of an enemy gang as the enemy and target, runs to him (2 s at a time) beyond
the fight goal's reach (90 % of the far melee range) and pushes the fight goal within it (the original's: [The Melee
goal](#melee-goal)); the confront goal (60) closes on the other gang's leader to its distance and waits. **Coney
choices:** a gang's leader is its first standing member not a player's (else the first standing); the confront's gang
radii are 0 and there is always a way between the leaders, so 9 never fires; the attack's coordinated sub-tactics,
`PedReaction` exception and spot line, and the confront's postures and formation are not built. A tactic replaced from
inside its own update or callback is freed after the gangs' update, as the original queues the free (`0x00306630`).

**The character bindings' goals and gangs** (`src/ai/scripted_humans.*`, `src/ai/scripted_goals.*`). `GoalBackoff`
(`0x9b`), `GoalBumLogic` (`0x4f`) and `GoalMoveToUseFlag` (4) are built from their constructors; **stand-ins** for their
unread Process: the back-off walks straight away from the other human while nearer than its distance, the bum stands
still and never calls back, and the use-flag goal walks to the flag as `GoalMoveToFlag` does, facing its heading, then
stands there (the flag reserved until the goal ends). `GangInvincible` sets god mode on the members and on later
ones; `GangSetTargetable` sets each member's targetable byte, kept on the human. `GangAddSpawner` keeps up to four
spawners per gang, which spawn (below); `GangClearWanted` has no wanted state to clear; `GangClearResponders` deletes
non-police gangs named `Responder<n>`. `BrSetThugWantsWeapon` and `SetInterrogateParam` are kept only.

**The spawners** (`src/ai/spawners.*`, [Spawners](#spawners)): after each characters' step every spawner in use whose
state is ready (1, 6, 7, 8 and 10 always; 2 past its deadline; 3 and 5 by player 1's distance, taken in 3D) makes a
human once its delay has passed, while fewer of its humans are alive than its limit and it has not made its total
(-1 none). The human is made through the scripts' own `HuCreate` (named `<spawner><count>`, the spawner's model string
as the fifth argument, in its gang), of the next type in turn (the index moved on first, as `0x0016d810` does), then
the callback, when it names a function, is called with its handle, the gang's id and the spawner's name.
6, 8 and 10 place the human out of sight as [above](#spawner-placement) and [the search](#spawner-search) say:
from player 1's camera (Coney has one player, so no second camera refuses a node), the start node the planner's
route ends use, 17 outward searches, the node then checked against the gang's turf boxes; no node, no spawn this
update. **Stand-ins**: 7 is placed as 8 with a value of 0 (`0x001679e8` is not on the page) and does not send its
human to the gang's first live member; the others stand at the spawner; no door opens; the dispatch states 4 and 9
(no crimes are routed) and the top-up 11 (no gang limits kept) never spawn; `SetSpawnMax` is not read.
`GangSetMaxConcurrent` changes the limit (16 bits; a negative -n spawns waves of n as [above](#spawners) says, from
`GangAddSpawner`'s limit too). An off-screen spawner (`GangSetSpawnerMustBeOffScreen`, or added in state 11) skips an
update while player 1's camera sees the 0.3 m sphere 1.6 m above it ([Off screen](#spawner-unseen)): within the far
clip, in view and with no wall of the collision mesh on the ray to its centre; **stand-in**: the six-plane frustum is
taken as a cone of half the field of view widened by the sphere.

**The scripts' goals at one human** (`src/ai/engage_goals.*`). `GoalMoveToHuman` (6) drops and re-issues a move
(`MoveAction`, its gait and radius) to where the target is every second, waits 30 updates after a failed route, and
ends within its radius in 3D, when the target is no longer alive in the world, or when its human is down.
`GoalEngageEnemy` (11) is the fight's run-in ([EngageEnemy](#engage-enemy), above) at the enemy by handle, taking him
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
attack strategies are kept for readers not built. **Warrior commands, stand-in** for the untraced tactics: the crew's
tactic is cleared and its AI members flushed, then 0 follow and 2 defend track the chief (2 m), 1 attack finds
enemies, 3 hold stands; 4, 5 and 6 start nothing; the lines are not said. `GangStartSpawner` switches a spawner
([above](#spawners)).

**The fourth mission's brain and human calls** (`src/scripting/mission4_bindings.*`, for `level34`). `BrSetPedType`
keeps the low 16 bits at the brain (**stand-in**: neither the civilian brain nor the mugging reads it yet).
`HuSetWounded` wounds once: the fight, grab or throw ends, health drops to a quarter and the brain is flushed; healing
clears the mark (**stand-in**: no wounded clips, and nothing reads the 14 s stamp). `ChangeBlocker` sets or clears flag
8 on the path polygon holding the point whose bounding box's middle is nearest (**Coney choice** for the centre).
`GoalStationaryThrower` (`0x8c`, `src/ai/riot_goals.*`) walks back within 0.5 m of where the goal began, turns to the
nearest hostile within three quarters of its sight range, plays clip `0x225` and waits 1000 × delay to 1000 × delay +
1000 ms (**stand-ins**: nothing is thrown, the eight object types are kept; the one wait in five with another idle is
not built). `GoalRiot` (84) follows [GoalRiot](#riot): the free act at Init, the turf gate on the nearest player
(`pointInTurf` over the gang's turf volume boxes), the one-draw decision, the roam (toward the player every 27th
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
within `range` of one of it. Scout melees with members that have enemies. **Stand-ins**: ManWeaponPile, Vandalize,
Steal, AvoidEnemies and Scout hold their places, their goals not traced; banter, answering violence, the anim
substitutions, HoldTheLine's 12 and 14 and Pursue's search time are not built.

**Open in Coney.** The pattern read at Start and in the pick; the reposition's band-keeping move and taunt; the sector
record; the move's sight checks;
the steering round humans, choke points and the waypoint queues; the dynamic obstacles; the legs of edges 8, `0x10`,
`0x40` and `0x80` (taken as plain walking, with `+0x284` 2 and 4 never set); the move's object to face; the turn clip
(398) on the spot; GoalMoveToFlag's interval gesture, the fight stance's switch-off and the gang's notice; the scene
system, the dynamic clip slot and clips by id; the head look-ats; the sender of message 1 and its attacker; the gang's
alert state, bounds, return to calm and neutral rule; the anim
substitutions; the crowd's cheer idles; the formation's ground ray, line of sight and assignment mode `+0x275`; the
dealer's run to a flag, gestures, buy clip and pair, shove and icons; the other tactics; the attack's steer, the
post-block pause and
the run-stop.

**The hub's goals and gangs** (`level95`; `repo:src/ai/hub_goals.h`, `repo:src/ai/scripted_hub.h`): `GoalAreaWalker`
(`0x47`), `GoalBoxer` (`0x9e`), `GoalGrabTarget` (`0x1f`), `GoalPeddler` (`0x50`), `GoalPlayGenAnim` (`0x26`) and
`GoalShopkeeper` (`0x82`) follow their binding pages; a fleeing gang (`GangCanFlee`) sends its class-11 members off
with the pedestrian reaction goal (`0x6b`) in mode 9. **Stand-ins**: the boxer fights with his own attack weights; the
grab target is held by standing at it, facing it, with no damage; the goals' lines (`beckon`, `phone_gang`...) are
kept, not played; the flight runs 10 s at gait 4, 10 m legs away from the enemy; a gang's starting count is noted
when `GangCanFlee` turns it on.

## Open questions {#open-questions}

- Movement and world use: MoveToUseFlag's clip states once he is on the flag; the pedestrian's flag re-use timer
  (goal `+0x6c`, 65 s inferred); the gait AvoidEnemies keeps in its `+0x3c`; why ChaseSupport jogs within 3 m;
  ManWeaponPile's Process (`0x002a3498`, not read).
- Who sends events `0x15`, 6, 9, 10 and `0x11`, and who reads event `0xf` (the tag-spot message); what reads the
  tackle meter brain `+0x148` besides the fight goal, and what brain `+0x28f` controls.
- The per-gang `CfgGang` values 2, 3, 5, 7 and 8 (attackers at once, tackle and rear-grab chances) as
  `config_preload2.lua` sets them; what commands 5 and `0x19` do in a grab (inferred: let go, shove off); the
  target's pattern bytes `+0x5d0` / `+0x5d1`; `Human_CanStartAttack`'s cases beyond kinds 10-15.
- `GoalBumLogic`'s begging (type 2's prompt, the chance, the callback, the 12 s timer) and what sets `+0x36`; what
  `GoalBackoff`, `GoalMoveToUseFlag` and the pedestrian goal (`0x69`, with its two variants
  and `FlagNetTraverse`'s flags) do each update (Process), and the use-flag goal's two floats; what
  `GangInvincible` sets on a member (`0x0016a000`).

- Link kind 2 and mask bit `0x100` in play, polygon `+0x02`, the globals `0x005105a0` and `0x005112b4`, and the
  goals that search with the mask `0x13` (`0x002aafd0`, `0x002c1470`).
- Which record `+0x08` bits the play-anim action's clip holds (`0x0025a3e0`), its rate for a dynamic clip and its blend
  out; Coney holds `0x80000`.
- Which climb clips Vermin's fence plays (tall or short fence, standing or running) and how the climb's end moves
  the follower to the next waypoint (route state 3 → 0).
- A\*'s "fails at 128 nodes": nodes closed, or the open heap's size? And the use term: (40 × uses) − 8, or
  40 × (uses − 8), and on which node of the edge?
- Which edge sign the inside test counts +1, and what the clockwise polygons (most of them, nearly all with polygon
  flag 1 or 2) are for if they contain nothing.
- The move action's braking distance `+0x48` (how it is worked out) and how a corner's arc is predicted
  (`0x0022aae8`, `0x002fbef0`).
- The formation's assignment mode `+0x275`, and `GoalFollowPlayer`'s modes 1, 2 and 4.
- What reads the turn action's `+0x10` (`ActLookAt`'s turn value) and the play-anim action's flag (loop or hold?).
- What the rip-off's `gen_push` clip does to the buyer ([Buying](#dealer-buy)).
- Who sends a gang's event 18 (a member down or dead), and with which attacker: `Gang_OnEvent` reads it, but its
  sender was not found (`0x0022dd98` and `0x0022e020` are the arrest's).
- What a scene's end passes to the callback `Goal_PlayAnimation` hands it (`0x00353f40`).
- The tactic event codes (`TacticGetString`, `0x00315c58`) passed to a tactic's callback.
- Whether `CfgSetDefaultFollowSlotSet` (`0x00294788`) rewrites the slots of formations in use or only the pool's
  defaults for formations made later.
- A turf box's radius `+0x40` (Coney: half its diagonal). Human `+0x333` is the detail level from the camera
  distance ([Characters](characters.md#ai-locomotion)).
- The hold's damage (`0x00510acc`) of `GoalGrabTarget` and the boxing attack weights (`0x00511120`) of
  `GoalBoxer`.
- Which sound each hub goal line plays (`beckon`, `store_greet`, `phone_gang`, `dead_meat`, `cower`, `mug_grunt`).
- The pedestrian reaction goal's (`0x6b`) Process and end in mode 9 (the flight): its length and route.
- The Warrior commands' tactics (`0x00310e00`, `0x00320530`, `0x00313400`, `0x00319570`, `0x00320b60`), the exit
  goal's on-screen test, the finding goal's search values (90, 30, 10) and the throw's Process (`0x002cf690`).
- The think handlers of types 2, 3 and 5 in detail; what goals the Warriors' think pushes for an ally.
- Which class `+0x11b` value 13 is ([Combat](combat.md#open-questions)), and what the byte
  `*(0x0051489c) + 0x56e3` that lets every AI counter is.
- What stopped Vermin's route jump at the `level99` window (x 47.76, by the panes at x 47.40) in the window-jump
  state, and whether a normal play-through clears it first; a jump that lands on its point is not yet seen at runtime.
