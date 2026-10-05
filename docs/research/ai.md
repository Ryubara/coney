# AI humans (brains, goals and actions)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra
only (2026-10-05); every claim is confirmed (code) at the cited address unless it says otherwise. The script calls of
`level99` were read from the disc's compiled scripts with `coney-tools` (ids, names and values only). Runtime checks
this page needs are at the end ([Runtime checks wanted](#runtime-checks)).

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

The brain, goal and action code lies in the 711 KB stretch after `Human/cns/cnsplayertag.cpp`
(`0x0028a360`-`0x00306630` at least, [Source map](source-map.md)); no path string names its files. The script
bindings are on [AI bindings](../references/bindings/ai.md). Names are ours.

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
| `+0xf8` | struct | perception, refreshed at each think (`0x00298f88`) |
| `+0x120` | u8 | targetable / attack-slot flags |
| `+0x124` | handle | the **target** |
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
| `+0x20c` | ptr | the human's **gang** |
| `+0x21c` | int | **threat response** (`GangSetThreatResponse`): 0 never fights |
| `+0x220` | int | damage response |
| `+0x265` / `+0x266` / `+0x267` | u8 | wants a weapon / a hat / reacts to violence |
| `+0x26c` | int | ped type |
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
the type id, `+0x1c` destroy, `+0x24` Start, `+0x2c` End, `+0x34` Resume, `+0x44` **Process**, `+0x4c` event (default
`0x0029ef80`). There are 148 goal classes. The ones this page uses:

| Type | Goal | Constructor | Notes |
| --- | --- | --- | --- |
| `0x01` | MoveToFlag | `0x002da3b0` | `GoalMoveToFlag` |
| `0x06` | MoveToHuman | `0x002dc4f8` | |
| `0x08` | Melee | `0x002ade10` | `Goal_Melee`; popped by `Brain_PushFightGoal` |
| `0x0b` | a fight sub-goal | `0x002af5b0` | |
| `0x0f` | **Fight** | `0x002b2c20` (vtable `0x005402d0`) | [below](#fight) |
| `0x12`-`0x1a` | reaction goals | | [below](#reaction-goals) |
| `0x1b` | **Block** | `0x002b54d8` (vtable `0x0053feb0`) | [below](#block) |
| `0x1f` | GrabTarget | `0x002bb458` | |
| `0x22` | PlayDynAnimation | `0x002d2eb0` | `GoalPlayDynAnimation` |
| `0x30` | TrackHuman | `0x002df250` | `GoalTrackHuman` |
| `0x41` | a melee sub-goal | `0x002c0430` | popped by `Brain_PushFightGoal` |
| `0x4f` | Bum | `0x002abef8` | `GoalBumLogic` |
| `0x57` | AddressPerson | `0x002cc408` | `GoalAddressPerson` |
| `0x69` / `0x73` | FlagNet traverse | `0x002aae30` / `0x002c1338` | |
| `0x6b` / `0x72` | a civilian's reaction to the player / flee | `0x002aa558` / `0x002d55d8` | pushed by the civilian think |
| `0x80` | Dealer | `0x002c6d90` | `GoalDealer` |
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

## Behaviour

### The update {#update}

`Brains_Update` (`0x00293b28`), step 5 of [`Humans_Update`](tasks.md#humans-update), for each of the 60 brains that
is enabled (`+0x08`) and whose human passes `0x0023d790`:

- **Think** (`Brain_Think`, handler B) when `index % 5 == (counter >> 1) % 5`: each brain thinks once every five
  character steps (6 Hz), staggered across brains.
- **Update** (`Brain_Update`, handler A) every step.

Then 20 records of 0x90 at `0x006cde30` (mask `0x006ce970`, `0x00299778`; not traced). Formations (step 2) and gangs
(step 4) run before the brains.

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

### Events {#events}

`Brain_OnEvent` (`0x0028f928`) runs the perception's handler (`0x00299100` on `+0xf8`), then the reaction goal's
event slot if one is active (`0x0028f988`), then handler C. Handler C is where a brain answers the world: the
civilian's (`0x002ffb30`) takes event `0x14` (violence nearby) and may start a fight through `Brain_Fight`; the type
event handlers are among `Brain_Fight`'s callers.

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

- **Start** (`0x002fa9a8`): checks the target, then sets the brain's `+0x1e8` (its next attack) and the target
  brain's `+0x1ec` (when it may be attacked next) to now + **the attack delay** (`Human_AttackDelay`, `0x00223800`:
  `CfgAttackDelay` (`0x00228870`) × the power class's `+0x1c`, or `+0x20` when the target is down), then writes the
  command (`AttackKind_ToCommand`) into the per-player record `+0x20` (`PlayerRecord_SetCommand`, `0x00147ef0`) and,
  when the action has one, a stick of magnitude 1.0 at an angle.
- **Update** (`0x002fad70`): writes the command again while waiting, and is done once the record's `+0x08` has none
  of **`0x5c0221f`**: the attack's held flags ([Tasks](tasks.md#held-flags)) decide when the AI is free again.
- **Abort** (`0x002fad30`): refused while `+0x08` has any of `0x5c0221f`.

So an AI attack is a press and a wait on the same flags the player's moves hold, and its chain timing is the
player's.

### Blocking and countering {#block}

- **When** (`Goal_TryBlock`, `0x0029f098`): only when the brain's `+0x200` is set, that is when an attack on the
  human was announced this update. The chance is `Human_BlockChance` (`0x00223628`: the power class's `+0x08`, or
  `+0x0c` while hurt, × the global `0x00510ac8`), **a quarter of it for brain type 3**. A player attacker whose
  pattern meets the class's `+0x37` threshold (`0x002236c8`, `0x00418398` on human `+0x5d0`) is always blocked. Yes
  pushes the block goal.
- **The block goal** (`0x1b`): Start (`0x002b5520`) sets `+0x10` = now + a random 1-3 s (how long it blocks) and
  **`+0x14` = (rand100 < `Human_BlockChance`)**, which answers who sets `+0x14`: the same chance, rolled again, decides
  whether this block may counter. Process (`0x002b5808`) writes command **4 (R1 held) every update** and, when
  rand100 < `Human_CounterChance` (`0x002235f8`: the power class's `+0x24` × 100), **3 (R1 pressed)**.
- **The counter** after a duck is the block goal's answer to message `0xa5` ([Combat](combat.md#block)): yes when the
  human is ducking and `+0x14` and `+0x16` are both 1.

New power-class fields (confirmed (code)): `+0x08` block chance, `+0x0c` block chance while hurt, `+0x24` counter
chance, `+0x37` the pattern-reading threshold ([Power classes](characters.md#power-classes)).

### Moving {#moving}

- The **move action** (vtable `0x00542f20`, `MoveAction_Init` `0x002fb9e8`, update `0x002fc5c0`) walks along the
  level's path areas: `0x002fc158` → `0x002fbef0` → `0x0024f718` tests a segment against the path polygons, and
  `0x00250708` finds the next point ([Level loading](level-loading.md#path-data)).
- **Steering** around other humans: `0x00289138`.
- The **speed** of a gait: `0x0022ae40`. The action writes a stick (angle and magnitude) into the per-player record,
  so the human's own locomotion moves it, with the same turn limits and gaits as the player (inferred from the attack
  action's stick write and the shared input path; the move action's write was not read in full).
- `GoalMoveToFlag` (type 1) and `GoalMoveToHuman` (type 6) queue these actions; the FlagNet goals (`0x69`, `0x73`)
  traverse the flag network.

How the path is planned beyond the segment test is not traced.

### Civilians {#civilians}

The civilian think (`0x002fef40`) refreshes its perception every 1.5 or 6 s (`0x0028fae8`) and reacts to a nearby
player by pushing goal `0x72` (flee) or `0x6b`. Its event handler (`0x002ffb30`) reacts to violence (event `0x14`)
and may fight (`Brain_Fight`) when its threat response allows.

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
   the attack action's wait on `0x5c0221f`.
6. **The block goal** and the counter, driven by the attacker's warning (`+0x200`).
7. **Reaction goals** for the states a fight produces (stunned, knocked down, grabbed).
8. **Scripted goals** for `level99`: `GoalMoveToFlag` along the path data, `GoalFight`, `BrFlush`, `GangBrDead`,
   `GangSetThreatResponse`, follow slots.

## Runtime checks wanted {#runtime-checks}

For the runtime-traces harness; brain *i* is at `0x006d53f0 + i × 0x2f0`, its per-player record at
`0x00660f50 + i × 0x2c`.

1. **A sparring fight** (`level99`, the first `CombatWarriors` fight, a save just before `GoalFight`): the player
   still, then circling with the stick at 60 %. Per update for the enemy's brain: `+0x2c`, the top goal's type
   (`+0x40[+0x2c]`, vtable `+0xc`), `+0x2e`, the front action's vtable, `+0x124`, `+0x1e8`, `+0x200`, and its
   per-player `+0x20`. Expect the attack commands of [Attack kinds](#attack-kinds) at the intervals of the attack
   delay, and pairs like `0x12` after `0x10` for kind 2.
2. **Think staggering**: log the update counter `0x005104f4` and which brains run `Brain_Think` (`0x0028f6c0`) for 20
   steps: one brain in five per step.
3. **The block**: the player attacks a type-2 enemy with square every 20 updates. Sample the enemy's `+0x200`, the
   block goal's `+0x10`, `+0x14`, `+0x16` and the command it writes (4, and 3 at the counter chance).
4. **Moving**: an enemy far from the player in the yard, the player still. Sample the enemy's per-player stick angle
   and magnitude each update while its move-to-human action runs: whether it writes a stick and at what magnitude.
5. **Reaction goals**: knock an enemy down (`SSS3`) and sample `+0x3c` and its vtable until it stands.

## Open questions {#open-questions}

- How a move action plans its path beyond the segment test, and whether it writes the stick or the velocity.
- The attack pick's adjustments in detail (`0x002240e8` and the attacker-count terms), and the two tokens.
- The tactic layer (gang `+0x40`, `TacticCrowd`, `0x00306630`) and the formations (`0x00293c68`), which `level99`'s
  fence fight and follow slots use.
- The records at `0x006cde30` updated after the brains.
- The perception struct (`+0xf8`) and the field of view's use.
- The think handlers of types 2, 3 and 5 in detail; what goals the Warriors' think pushes for an ally.
- Which class `+0x11b` value 13 is ([Combat](combat.md#open-questions)).
