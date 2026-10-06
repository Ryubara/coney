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
| `+0x12c` | float | field of view (`BrSetFOV`) |
| `+0x13c` / `+0x140` | float | melee range near / far (`BrSetMeleeRange`) |
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
| `+0x21c` | int | **threat response** (`GangSetThreatResponse`): 0 never fights |
| `+0x220` | int | damage response |
| `+0x265` / `+0x266` / `+0x267` | u8 | wants a weapon / a hat / reacts to violence |
| `+0x26c` | int | ped type |
| `+0x284` | int | why the last move failed: 1 no route, 2 or 4 an edge, 3 stuck |
| `+0x298` | u8[45] | **attack weights**, one per attack kind (`BrSetAttackWeight`; filled from the class's `Att_*` table) |
| `+0x2d1` / `+0x2d2` | u8 | world-flag use allowed / its chance |
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
| 6 | the `civl_co_di` kind (441) | `0x0028fbb0` | `0x00303f10` | `0x00304228` |

So type 3 is the Warriors' brain, which an ally Warrior runs (this answers [Combat](combat.md#open-questions)'s "brain
type 3"). The player's update `0x003035d8` only keeps books (target validity, attackers counted, the follower check
`0x002d09f0`); it pushes no goals.

### Goals {#goals}

Goals come from a pool of 170 of 0x90 bytes at `0x006e0430` (bitmap `0x006ceac0`). Base fields: `+0x00` the brain,
`+0x04` started, `+0x05` resumed, `+0x08` wait-until time (-1 none), `+0x0c` vtable. Vtable function words: `+0x0c`
the type id, `+0x14` the class's name (a string, such as `MoveToFlag`), `+0x1c` destroy, `+0x24` Start, `+0x2c` End,
`+0x34` Resume, `+0x44` **Process**, `+0x4c` event (default `0x0029ef80`). There are 149 goal classes, types 0-158
with ten unused, all on [AI goal types](../references/goal-types.md) with the bindings that make them; confirmed
(code), each vtable read. The ones this page uses (names the game's):

| Type | Goal | Constructor | Notes |
| --- | --- | --- | --- |
| `0x01` | MoveToFlag | `0x002da3b0` (vtable `0x00542130`) | [`GoalMoveToFlag`](#move-to-flag) |
| `0x06` | MoveToHuman | `0x002dc4f8` | |
| `0x08` | Melee | `0x002ade10` | `Goal_Melee`; popped by `Brain_PushFightGoal` |
| `0x0b` | EngageEnemy | `0x002af5b0` | a fight sub-goal |
| `0x0f` | **Fight** | `0x002b2c20` (vtable `0x005402d0`) | [below](#fight) |
| `0x12`-`0x1a` | reaction goals | | [below](#reaction-goals) |
| `0x1b` | **Blocking** | `0x002b54d8` (vtable `0x0053feb0`) | [below](#block) |
| `0x1f` | GrabTarget | `0x002bb458` | |
| `0x21` | PlayAnimation (a scene) | `0x002e4980` | pushed by `GoalAddressPerson` |
| `0x22` | PlayDynAnimation | `0x002d2eb0` | [`GoalPlayDynAnimation`](#dyn-animation) |
| `0x29` | JoinCinematic | `0x002e53e0` (vtable `0x005421f0`) | `GoalJoinCinematic` (`0x002e5300`); Process `0x002e5618` |
| `0x30` | TrackHuman | `0x002df250` | [`GoalTrackHuman`](#formations) |
| `0x36` | HoldPosition | `0x002be640` (vtable `0x00540510`) | `GoalHoldPosition` (`0x002be590`), and the tactic code at `0x00313718`; Process `0x002be818` |
| `0x41` | FindEnemy | `0x002c0430` | popped by `Brain_PushFightGoal` |
| `0x4f` | BumLogic | `0x002abef8` | `GoalBumLogic` |
| `0x57` | AddressPerson | `0x002cc408` | [`GoalAddressPerson`](#address-person) |
| `0x69` / `0x73` | Pedestrian / Patrol | `0x002aae30` / `0x002c1338` | `FlagNetTraverse` |
| `0x6b` / `0x72` | PedReaction / Hostile | `0x002aa558` / `0x002d55d8` | pushed by the civilian think |
| `0x80` | Dealer | `0x002c6d90` | [`GoalDealer`](#dealer) |
| `0x85` | BigFighter | `0x002e9cd0` (vtable `0x00542940`) | |
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

`Brain_InstallHandlers` (`0x0028c1a8`): a brain with `+0x09` set ("dead", `BrDead`) gets A = `Brain_UpdateGoals`
and B, C = empty stubs (`0x004edd40`, `0x004edd48`), so only what the script pushed runs; a type-0 brain set dead also
gives up the pad (per-player `+0x1b` = 0). Otherwise A, B and C come from the type's tables. `level99` makes its enemy
gangs dead (`GangBrDead(true)`) while it scripts them, then lifts it for the fights.

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

### Events {#events}

`Brain_OnEvent` (`0x0028f928`) runs the perception's handler (`0x00299100` on `+0xf8`), then the reaction goal's
event slot if one is active (`0x0028f988`), then handler C. Event `0x10` is the **attack warning**
([below](#block)). Handler C is where a brain answers the world: the civilian's (`0x002ffb30`) takes event `0x14`
(violence nearby) and may start a fight through `Brain_Fight`; the type event handlers are among `Brain_Fight`'s
callers.

### Starting a fight and choosing the target {#targets}

- `GoalFight(human, target, …)` is `Brain_StartFight` (`0x002b2b90`): clear the actions, then `Brain_Fight`.
- **`Brain_Fight`** (`0x0028d2e8`) needs a threat response (`+0x21c` ≠ 0). It adds the target to the enemy list
  (`Brain_AddEnemy`, `0x0028d538`, which tells the gang's tactic with event `0xb`), takes it as the target
  (`Brain_SetTarget`, `0x0028cfe0`) and pushes the fight goal (`Brain_PushFightGoal`, `0x0028d190`, only when the
  gang has no tactic at gang `+0x40`; it pops goals 8 and `0x41` first).
- **`Brain_SetTarget`** releases the old target's attack slot and claims one on the new target
  (`Brain_ClaimAttackSlot`, `0x0028df30`): the target's list `+0x1a4` holds at most `+0x1e4` attackers, and a closer
  attacker takes the slot of the farthest.
- **Re-targeting**: the fight goal looks for the nearest threat once a second (`0x0028fe90`, `0x00290138`).

### The fight goal {#fight}

`FightGoal_Process` (`0x002b3ab0`), each update while the fight goal is on top:

1. No valid target or no attack slot (`Brain_HasAttackSlot`) → done. A target farther than (`+0x140` × 1.1)² → done.
2. Every 30 updates `0x002221b0`; once a second, re-target.
3. **The block try** (`Goal_TryBlock`, [below](#block)).
4. While actions are queued, wait.
5. **Pacing**: the goal's attack timer (`+0x24`, a random 750-1000 ms plus a parameter at start) and two tokens
   (`0x002911a8`, `0x00290ea8`) must allow an attack.
6. **Pick the attack** (`Brain_PickAttack`, `0x0028e708`): a weighted random choice over the 45 kinds. The weights
   are `Brain_GetAttackWeight` (`0x002911f8`: the brain's `+0x298`, or the override at `+0x208`), filtered by what
   the human can do now (`0x002240e8`) and adjusted for the number of attackers and the grab chance.
7. The fight stance on or off (`0x0022fef0` / `0x0022fe80`).
8. Target **out of reach** (`0x00230d00`): queue a move-to-human action (`MoveToHumanAction_Init`, vtable
   `0x005431e0`) with a 1000 or 2000 ms limit. **In reach**: queue the attack (`Brain_QueueAttack`); a class whose
   `+0x11b` is 13, 6 or 7 taunts instead one time in five (`0x002205e0`, anim `0x11`).
9. Otherwise reposition (`0x002b2fc8`).

### The attack action {#attack-action}

The attack action (vtable `0x00542ce0`, `AttackAction_Init` `0x002fa918`):

- **Start** (`0x002fa9a8`): checks the target and keeps the start time in the action's `+0x18`. It sets the brain's
  `+0x1e8` (its next attack, `0x00290e48`) to now + **the attack delay** (`Human_AttackDelay`, `0x00223800`:
  `CfgAttackDelay[kind]` × the power class's `+0x1c`, or `+0x20` when the target is down), **halved** when the
  target's own target is this human or the brain is type 3. `CfgAttackDelay` is a table of ms **indexed by attack
  kind** (`0x00228870` reads `0x006b6658 + kind × 4`); `config_preload2.lua` sets 200 for most kinds, 400 for 12 and
  13, 500 for 19 and 21, 1000 for 20, 300 for 32-34 and 0 for 23, 31 and 42. When neither human is busy, it sets the target
  brain's `+0x1ec` (when it may be attacked next, `0x00290e78`) from a **separate per-kind time** (`0x00231590`),
  scaled by `0x00510ad0` / the target brain's spacing byte `+0x14a` (or `+0x14b`, by `0x00223b48`) when that is not 0.
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

- The goal stack was `0x41`, `0x08` (fight), `0x0f`; the front action was the attack action, the move-to-human
  action or, when far, a move action (goal `0x0b`).
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
        - In its **last second** (`+0x10` − now < 1000), when `+0x15` = 1 and the human is not ducking
          (`0x00223b28`), queue a **punishing attack** (`0x002b5718`): `Brain_PickAttack` with mask
          `0x224778ffff0000` (class `0x6a` instead takes kind `0x10` or `0x11`, class `0x59` kind `0x10`), then
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
`0x0028ab28`; `0x0028aac0(brain, 0)` stops), `+0x90` the point aimed at and `+0x118` its radius; the human's state
update moves it from those. A gait's speed is `Human_SpeedForGait` (`0x0022ae40`): 1.6286 m/s walking, 7.8012 m/s
running. Confirmed (runtime): the civilian's per-player stick magnitude stayed 0 through its move and move-to-human
actions; only the attack action wrote a stick (1.0, for throws).

From 8 m behind the player (confirmed (runtime)): a turn on the spot (clip 398), the run start 414, the run 410 at
7.80 m/s, then at 1.29 m a charge (`0x20`). Closer, the move-to-human action walks in the fight-stance clips 372-380 at
about 2.3 m/s.

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
       (`0x00250708`, `0x0024f718`); each failure lowers the trial speed by 1 + 0.75 × human `+0x333`, down to the
       walking speed. It keeps the first corner's speed `+0x0c`, the second's `+0x44` and a braking distance²
       `+0x48`, and uses `+0x0c` while farther than that, else `+0x44`, never above the gait's speed.
    9. **Stuck** (`0x002fc330`): every 60 updates while moving, less than 0.2 m covered → brain `+0x284` = 3, done.
- Brain `+0x284` says why a move failed: 1 no polygon or no route, 2 or 4 an edge it cannot take, 3 stuck.

#### Path planning {#path-planning}

The route request (`0x0029a8c0`), with its state at brain `+0xe0` (`+0x04` the route, `+0x08` the waypoint radius,
`+0x0c` a state, `+0x10` the waypoint index, `+0x14` an edge mask). Confirmed (code). It plans over the level's
[path data](level-loading.md#path-data): the **polygons** are the walkable areas, the **C records** the graph's
**nodes** and the **D records** its **edges**.

1. Find the human's polygon (`0x00247958`, cached at human `+0x1b4`; fallbacks `0x002505b0`, `0x0024e218`). None →
   `+0x284` = 1, fail.
2. **Straight line first**: when the segment to the point stays inside the polygons (`0x0024fbf8`, below), no route
   is made and the action steers straight.
3. Otherwise both ends must be in one polygon, or both polygons must be on the graph (polygon `s16 +0x02` ≠ 0;
   inferred meaning). Each end's node is the nearest of its polygon's nodes (the A record's count and first index)
   that it reaches in a straight line (`0x00251150` → `0x00250e98`, `0x0024f290`), trying up to 30 by distance.
4. **A\*** (`0x00251a70`), from the destination's node to the start's, so the parent links read in walking order:
    - the open list is a binary min-heap on f; costs are `u16` in 1/16 m; the heuristic is the straight distance × 16
      (`0x002517b0`);
    - an edge is skipped when its flags (the low 16 bits of the D record's word) share nothing with the mask, or its
      cost reaches 65000 (`0x005105a4`); the search fails on an empty heap or at 128 nodes;
    - **edge cost** (`0x00251890`) = distance × 16, + 200 for flag 8, + 320 for `0x80` and + 80 for 4 (these three
      only when `0x0051059c` = 1 and the mask lacks `0x100`), + 1600 when the word's bit 31 is set, and + (40 × routes
      already through the node − 8), saturating at `0xffff`. The last term spreads AIs over parallel routes.
5. `0x002511c8` retries a failed search with `mask | 0x8c`; when the route uses a `0x80` edge (under the condition
   of the extra costs) it searches again without `0x80`, capped at the first cost, and keeps the cheaper route.
6. **Building** (`0x002513a8`; a pool of 32 routes of `0x110` bytes at `0x006ca250`, a count then `s16` node
   indices): leading nodes the start reaches directly are skipped (only when `0x005105a0` = 0), the chain is shortened
   by looking up to 4 nodes ahead for one that links back, trailing nodes are dropped while the destination is
   reachable from the node before, and each node's use count (C `+0x1f`) is raised; freeing the route
   (`0x00251680`) lowers it.

**The walkable-line test** (`0x0024fbf8`): the segment is refused when blocked (`0x00221f80`, inferred: collision),
then walked through the polygons that have neither flag 8 nor the caller's mask, against their edges by slab
(`0x0024e938`); it passes when it never leaves the polygons.

`0x00251d28` is a second, Dijkstra-like search that stops at the first node beyond a distance inside a cone of
directions, skipping bit-31 edges (inferred: for fleeing).

#### Following a route {#route-follow}

`0x0029aa88` gives the current waypoint (done at state 5). Every 6th call, or when asked, `0x0029b6d8` moves on: it
skips waypoints already reachable in a straight line (`0x0029b4b8`), for legs over 5 m only when the turn against the
previous leg is under about 135°, and sets the waypoint radius to 0.25 m; at the end it frees the route. Confirmed
(code). Edge flags change how a leg is taken (what each looks like is not traced):

- flag 4, a **choke point**: claimed in a table of 40 `{point, human}` at `0x006ce978` (`0x00293e40`); the nearer
  human keeps it, and the other holds with speed 0 when under 2 m from it. A claim on a whole edge goes to one of
  the 20 queue records at `0x006cde30` instead ([Queues](#queues));
- flag `0x80`: passed when the gait is above 3, human `+0xe0` bit 2 is set, the waypoint is within 4.5 m and the
  jump test `0x002826f0` passes (inferred: a jump);
- flags 8, `0x10` and `0x40`: `0x0029b848`, `0x0029baa8` and `0x0029bca0`; an edge it cannot take sets `+0x284` = 2
  (4 for `0x10`).

#### Steering round humans {#steering}

`Human_SteerAroundHumans` (`0x00289138`, avoidance state at brain `+0xa0`); confirmed (code) for the structure, the
names of the cases inferred. It runs only at gait 2 or less, deciding again every gait × 3 updates:

- it looks ahead min(1.5 × speed, 10 m) (× 10 when slow and blocked for 16 checks) among up to 60 humans
  (`0x002274a8`), and `0x00288cc8` picks the one in the way;
- by the relative bearing and the other's heading (thresholds 30°, 45° and 50°) it sidesteps, overtakes or goes round
  on the free side (`0x00337308`), or yields;
- the faster human has right of way (the lower address on a tie); the one yielding slows to 0, or to 0.75 × speed
  within 3 m; two humans heading for the same route node follow at 0.75 × speed;
- the other's motion is predicted from its velocity for a player and from its brain `+0x90` for an AI.

#### Waypoint queues {#queues}

20 records of 0x90 at `0x006cde30` (mask `0x006ce970`), updated after the brains (`0x00299778`); inferred: queues at
choke edges. `0x00299538` lays up to 6 points one metre apart along the edge (`+0x00`, 16 bytes each; count `+0x88`)
between its endpoints `+0x60` / `+0x64`, with 6 humans at `+0x68` and their "settled" flags at `+0x80`. Each update,
humans whose brain `+0xec` is 1 or 2 keep their point and the others are reassigned (`0x00299a08` when there are more
humans than points, else `0x00299b50`), then up to 5 passes swap pairs whose paths cross. Confirmed (code).

### Civilians {#civilians}

The civilian think (`0x002fef40`) refreshes its perception every 1.5 or 6 s (`0x0028fae8`) and reacts to a nearby
player by pushing goal `0x72` (flee) or `0x6b`. Its event handler (`0x002ffb30`) reacts to violence (event `0x14`)
and may fight (`Brain_Fight`) when its threat response allows.

### Scripted goals and actions {#scripted}

What `level99`'s scripts give a brain directly. Confirmed (code) at the addresses cited unless marked; argument names
and defaults are on [AI bindings](../references/bindings/ai.md).

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
interval (ms), `+0x3e` arrived, `+0x3f` option, `+0x40` face the flag's heading. The order of the three floats is
inferred from the calling convention.

- **Start** (`0x002da408`): the target is the flag's position (`0x00417a60`) moved by `distance` at `angle`
  (`0x003376c0`); with an interval, the next tick is now + interval; then Resume.
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

#### GoalDealer {#dealer}

Type `0x80` (`0x002c6c88`, constructor `0x002c6d90`, vtable `0x00540c90`); `level99_lesson2`'s `FlashDealer`. The
class overrides the dealer type: 426-430 → 0 (flash), 431-435 → 2, 436-440 → 1 (weapons). Fields: `+0x10` home,
`+0x24` state (1 at construction), `+0x28` type, `+0x2c` range, `+0x30` next scan, `+0x3a` run chance, `+0x3b` dirty
chance, `+0x3c` option, `+0x3d` greeted, `+0x3e` player in range, `+0x41` dirty (rand100 < dirty chance, rolled in
the constructor), `+0x42` dealing.

- **Start** (`0x002c6e78`): threat response (brain `+0x21c`) 0, brain `+0xcc` |= 2, a spinning icon by type
  (`dyn_flashdeal`, `dyn_weapdeal`), the home position, then Resume. **Resume** (`0x002c6f98`): fight stance on and
  the type's idle. **End** (`0x002c70a0`): icon removed, the player's interaction target cleared if it is this dealer,
  threat response back to 2.
- **Process** (`0x002c7fd8`), in order:
    - the player in range = distance ≤ range (`0x00419ee0`); leaving after a deal plays a gesture; beyond 2 × range
      the greeting is forgotten;
    - in range and in sight, the buy clip is loaded into the player's slot 668;
    - states 4 and 5 run to a flag of kind 8 (`0x004177d8`) with a move-to-flag goal (gait 4);
    - every 2 s while its threat response is 0, an enemy within 16 m and in sight makes it push goal `0x002b4098`
      (inferred: wary) and gesture;
    - more than 1 m from home and not playing 668: walk home (gait 4); more than 15° off the player: turn to him;
    - the first time, a greeting gesture and, with the option, a radar icon (`0x002c7ee0`);
    - state 1 with the player within 1.5 m: state 3, dealing, human `+0x1b2` = 1 (inferred: the player may now buy).
- The run and dirty chances are not read by Process (open question).

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
  or dead human, or a type-3 brain already holding goal 9; otherwise it pops goals 8 and `0x41`, pushes the melee
  goal (8) and then the fight goal (`0xf`).
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
  free member with no actions queues two actions, a clip action (`0x002fb868`, vtable `0x00542d20`) with anim `0x8f`
  (`0x10` at rand100 < 50 when `what` is 0; `0xb1` when `what` is 1 and `on` is 0) after 0-750 ms, then a second clip
  action (`0x002fa300`, vtable `0x00542be0`) with the cheer `0x256` in one of three variants. A cheering crowd's
  Process calls it with (0, 1) on each tick.
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

#### Spawners {#spawners}

A gang has four **spawners** of 0x130 at gang `+0x640` (`GangAddSpawner`, `0x00166ff8`; a fifth is ignored). Their
update, `0x001681a0`, runs each in use against its **state** at `+0x52`; the states, their names and the scripts'
uses are in [Spawner states](../references/spawner-states.md). Confirmed (code):

| Offset | Meaning |
| --- | --- |
| `+0x00` / `+0x14` | position / name |
| `+0x50` | in use; cleared once the spawner has made its total (`+0x56` against `+0x58`) |
| `+0x52` | state; `GangAddSpawner`'s kind, `GangStartSpawner`'s mode (`0x00168cd0` accepts 0-5, 7, 8, 9, 11) |
| `+0x5a` | how many of its humans may be alive at once |
| `+0x60` / `+0x64` | delay between spawns / the next spawn time |
| `+0x68` | the state's **value**: seconds for 2, metres for 3, 5, 6 and 8 (`GangStartSpawner`'s last argument) |
| `+0x70` / `+0x74` | a door it opens to let each human out, and how long it stays open |
| `+0x7c` | state 2's deadline: the value in seconds after the state was set |
| `+0xac` / `+0xb4` | the dispatch queue: the entry being served, then 4 entries of 0x1c (a time, a count at `+0x14`, bytes) |

1. **Ready?** 0 never; 1, 6, 7, 8 and 10 always; 2 once past its deadline; 3 while player 1 is within the value
   (`0x00336d88`), 5 while he is farther; 11 while the gang's living members (`0x00166158`) are fewer than gang
   `+0xb04`, and back to 0 once the gang has spawned (`+0xb02`) its total (`+0xb00`).
2. **Dispatch** (4 and 9): when a queued entry is due (`0x0016dda8`) and a gang slot is free (`0x0016d458`), a new gang
   `Responder<n>` is made: of type 1 for 4, only while the game's count `+0x324` is under `+0x326`, of the spawner's own
   type for 9, which also takes its owner's friend and enemy masks. The state becomes 6 (from 4) or 10 (from 9), and
   returns once the entry's squad is complete; an entry whose byte `+0x17` is 3 may also start `Tactic_RiotCop`.
3. **Place** the human: 6, 8 and 10 at the value's distance from the player, out of the camera's view (`0x001673b8`);
   7 out of view and sent to the gang's first live member (`0x001679e8`, the second part inferred); the others at the
   spawner. State 11 skips a spot a camera can see (`0x001202e8`).
4. **Spawn** (`0x00167ea8`, named `<spawner><count>`), open the spawner's door, count it.

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
commands** ([Commands](../references/commands.md#warrior-command)). The menu (HUD `0x001a6c58`), `WCIssueCommand`
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

The tactics' types and behaviour are not traced: the names follow, attack, hold, scatter and wreck are read from
the lines (inferred).

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
       is walkable, else waypoints with the 0.25 m radius; the corner speed; the stuck test and brain `+0x284`.
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
`Att_Normal`) each get `GoalFight(warrior, player, 0)` from `P1.SendWarriors` when the scene `l99_c5` ends, about 290
updates after `P1.SetupWarriors` starts it. Results: [the attack delay](#attack-action), [the block](#block) and
[the cast's numbers](#level99).

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

## Coney's implementation {#coney}

Steps 3-8 of [What an implementer needs](#implementer) are in `repo:src/ai/` (the path data
in `repo:src/world/path_map.h`), each original function tagged with `@orig` in the code; tests in `repo:tests/ai/` and
`repo:tests/world/path_map_test.cpp`.

- **Brains** (`Brains`, `Brain`): a type from the class's behaviour byte, a think one step in five staggered by slot,
  an update every step at the brains' place in the characters' step (`Humans::setBrains`), so before every
  dispatcher. The player has a type-0 brain that only keeps his enemies, which feed `Player::setNearestEnemy`.
- **Goals and actions**: a stack of 10 (Start, Process, End, Resume; Stop, Again, Done) and a circular queue of 8
  (a delay, Start, Update, an Abort that can refuse); goals start or resume only on an empty queue.
- **Fighting**: `FightGoal`, the weighted pick, `Brain_QueueAttack`'s chains timed by the chain clip's first event,
  `AttackAction` (the command once in Start, the delay halved when the target targets the attacker or the brain is
  type 3, then a wait on `0x5c0221f`), `MoveToHumanAction` (a heading and speed in the record's `move`, no stick).
- **Blocking**: `BlockGoal` never produces a block: Coney's block starts only on R1 held in the record's buttons,
  which only a pad writes, so its command 4 does nothing. It turns the human's hit reactions off
  (`Fighter::setHitReactionsOff`, bit `0x800`) from its start until its sixth update with the human free; rolls the
  counter on every update of the block time (`BlockGoal::counterRoll`: the roll, then `counterTest`, a grab's or a
  tackle's intro on a target aiming at the human) and writes command 3 on success; extends the block while the
  target still attacks, which stops the rolls and queues a punishing attack in the block's last second.
- **Reactions**: grabbing, tackling, grabbed, knocked down and stunned, one update after the state.
- **Configuration** (`aiConfigFrom`): the class's `CfgChar`, power class 40's `CfgPowerClass`, `CfgAttackDelay` and
  `CfgBaseChanceToBlock` from the scripts' recorded calls. Without the disc the reference values stand in: power
  class 40's AI fields (block 0.2, 0.1 hurt, delay factors 20, counter 0.08), base block chance 60, the
  `config_preload2` delays, `Att_Normal`.
- **Play**: `fighter` lines in sandbox layouts ([Sandbox](../guides/sandbox.md#ai-fighters)), fighters in
  `--play-level`, and the debug menu's *AI fighters* page.
- **Routes** (`world::PathMap`, `RoutePlanner`): the path data decoded ([Path data](level-loading.md#path-data)), the
  inside test, the walkable-line test, the request (the human's polygon, the straight line, the ends' nodes within 30
  tries), A\* from the destination's node with the edge costs and the 65000 cap, the retry with `0x8c` and the jump
  detour, and routes from a pool of 32 with the leading, shortcut (4 ahead) and trailing cuts and the nodes' use
  counts. A brain gets the level's planner from `Brains::setPlanner` (the play mode gives one from the level file's
  path data, `PlayScenery::pathMap`); with none, every move goes straight.
- **Moving** (`MoveAction`, `RouteFollower`): straight when the line is walkable, else the route's waypoints (0.25 m,
  moving on when reached and every 6th call, skipping what is in a straight line); the straight re-check every 30
  updates; the turn on the spot beyond 30° while standing; the corner speed; the stuck test; brain `+0x284`
  (`Brain::moveFailure`). The human's locomotion turns it to a brain's heading at speed 0 (`human::Human::locomote`).
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
  and tactic callbacks in a `ScriptSystem`.
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
  edges, which a move does not ask for, and 162 not linked at all.

**Coney choices.** A fighter is class 58 (brain type 2, 1400 health) with the sparring Warriors' runtime brain values (4
attack slots, melee 3 / 5 m, sight 30 m, field of view 1.92 rad), drawn and animated as the player's character, in a
gang of kind 19 made the enemy of the player's (the Warriors' kind), and an "engaging" toggle stands in for the script's
`GoalFight` (an idle fighter takes the player on within its far melee range). Who fights whom in the characters' step is
whoever's gangs are not friends (`Humans::setOpposition`); without gangs, the humans added pad-controlled and the others
fight each other, and only the former fight the sandbox's passive targets. A target is in reach within 0.9 × its first
attack's far range; a move runs beyond 4 m, lasts 1000 or 2000 ms (2000 beyond twice the reach) and stops at 0.9 × the
reach. The pacing timer resets only after an attack; with nothing else to do a fighter stands still. The target's
`+0x1ec` takes the kind's unscaled `CfgAttackDelay`, whether or not either human is busy. Command `0x11` chains as
square. A reaction goal clears the actions and the move. A block ends when its target is not on its feet (Coney has no
state word); the counter test leaves out the face-to-face and class gates. Each brain's generator is seeded by its slot.
A think only counts (the types' think handlers are not traced).

**Coney choices for moving.** The inside test counts an edge going down in y as +1 (the sign under which the route
nodes lie in their polygons; the clockwise polygons then contain nothing). A polygon's A record takes the next nodes
in order (the counts add up to the C records in every file; `+0x08` does not always hold the running index). A move
searches with the edge mask `0x3` (on the disc every edge has one flag of 1, 2, 4, 8, `0x10` or `0x80`), so `0x10`
edges are never asked for. "Fails at 128 nodes" is the open list's size (counted as nodes closed, a third of
`level99`'s reachable pairs failed). The use term is 40 × uses − 8 on the node entered; `0x0051059c` is 1 and
`0x005105a0` 0. An end off every polygon counts on the nearest within 1 m and takes its nearest node; the shortcut
takes any edge that links back. The walkable line cuts the segment at every crossed edge rather than walking by slab,
with no collision test. A corner is the turn at the next two waypoints, simulated as an arc at the gait's turn rate
from the waypoint, the trial falling by 1 m/s; the braking distance is 0.5 s at the first corner's speed, within which
the slower of the two corners' speeds is used. A move clears `+0x284` at its start and waits while the human is busy
(`Human_IsBusy`). The look-at's turn value is kept, not read; no turn is ever refused its abort. GoalMoveToFlag's
angle is a world direction (the headings' convention).

**Coney choices for the scripted goals, gangs and tactics.** With no scene system a scene ends at once and its callback
is scheduled with (handle, 1) after 33 ms; with no clip by id from outside the dispatcher (`ScriptServices::playClip`)
a play-anim action ends at once, so `GoalPlayDynAnimation` still completes. "Not on its feet" stands in for the state
words (PlayDyn's `0x7bf9e9f7ff0`, the crowd's free test, the standing count's three tests); the headcount's "living" is
health left. AddressPerson's turn leads the target by one second of its velocity. `BrSuspend` clears the actions,
then suspends. A full gang drops its first member; a human with no gang is no one's friend. A handler's call counts as
returning true when it runs (Coney's script system does not hand back the result). Event 18 is sent at the brains'
next step after the health runs out, with no attacker. A formation slot is usable when the leader's planner finds the
line to it walkable (always without a planner) and keeps the leader's height. The idle and spectate goals stand
still (their Process is not traced). Both of a crowd reaction's clip actions are `PlayAnimAction`. The dealer rolls
dirty at Start; his wary scan looks for members of enemy gangs.

**Open in Coney.** The dispatcher's answer to an AI's command 3 (76 against a grab, 9 against a tackle, as paired moves)
is not built, and neither are grabs and tackles between two humans that would call for it; the pattern read at Start;
the per-kind time `0x00231590` and the spacing bytes; the pick's adjustments; line of sight (the move's sight checks);
the steering round humans, choke points and the waypoint queues; the dynamic obstacles; the legs of edges 8, `0x10`,
`0x40` and `0x80` (taken as plain walking, with `+0x284` 2 and 4 never set); the move's object to face; the turn clip
(398) on the spot; GoalMoveToFlag's interval gesture, the fight stance's switch-off, message 8 (Coney's flags take none)
and the gang's notice; the level scripts run alone before play and their humans are not AI humans yet, so `level99`'s
goals reach no brain in play; the scene system, the dynamic clip slot and clips by id; the head look-ats; a human's own
message handlers (`SetMsgHandler`); the gang's alert state, bounds, return to calm, neutral rule and spawners; the anim
substitutions; the crowd's cheer idles; the formation's ground ray, line of sight and assignment mode `+0x275`; the
dealer's run to a flag, gestures, buy clip and icons; the other tactics; the attack's steer, the post-block pause and
the run-stop.

## Open questions {#open-questions}

- The per-kind time `0x00231590` that sets the target's `+0x1ec`, and the spacing bytes `+0x14a`, `+0x14b`.
- The attack pick's adjustments in detail (`0x002240e8` and the attacker-count terms), and the two tokens.
- What the edge flags mean in play (4, 8, `0x10`, `0x40`, `0x80`, `0x100`), polygon `+0x02` and flag 8, and the
  globals `0x0051059c`, `0x005105a0`, `0x005112b4`.
- The edge mask a move searches with (route state `+0x14`): who sets it, and does a human that can climb ask for
  `0x10`?
- A\*'s "fails at 128 nodes": nodes closed, or the open heap's size? And the use term: (40 × uses) − 8, or
  40 × (uses − 8), and on which node of the edge?
- Which edge sign the inside test counts +1, and what the clockwise polygons (most of them, nearly all with polygon
  flag 1 or 2) are for if they contain nothing.
- The move action's braking distance `+0x48` (how it is worked out) and how a corner's arc is predicted
  (`0x0022aae8`, `0x002fbef0`).
- GoalMoveToFlag's offset (`0x003376c0`): is the angle a world direction or turned by the flag's heading?
- `GoalFollowPlayer`'s Process (vtable `0x00541d70`) and the formation's assignment mode `+0x275`.
- What reads the turn action's `+0x10` (`ActLookAt`'s turn value) and the play-anim action's flag (loop or hold?).
- What drives the dealer's run and dirty chances, and what goal `0x002b4098` (type `0x10`, Spectate; the dealer's wary
  goal, the crowd's timed goal) does beyond waiting.
- Who sends a gang's event 18 (a member down or dead), and with which attacker: `Gang_OnEvent` reads it, but its
  sender was not found (`0x0022dd98` and `0x0022e020` are the arrest's).
- What a scene's end passes to the callback `Goal_PlayAnimation` hands it (`0x00353f40`).
- The tactic event codes (`TacticGetString`, `0x00315c58`) passed to a tactic's callback.
- What the player gang's type-3 tactic (vtable `0x005439e0`) is called and does, and what `0x0041c4e0` decides.
- The perception struct (`+0xf8`).
- The think handlers of types 2, 3 and 5 in detail; what goals the Warriors' think pushes for an ally.
- Which class `+0x11b` value 13 is ([Combat](combat.md#open-questions)), and what the byte
  `*(0x0051489c) + 0x56e3` that lets every AI counter is.
