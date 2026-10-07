# AI and the world: objects, cars, doors and climbs

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra
(2026-10-07); each claim states its evidence level.

## Purpose

How an AI human touches the world, seen from the Human side. It covers which world uses an AI reaches, through
which of the player's own functions, and which it never reaches. [AI humans](ai.md) explains the brains, goals and
actions; this page fills the gaps between that page and the Human pages ([Characters](characters.md),
[Combat](combat.md), [Combat moves](combat-moves.md)). Where another page already covers a topic, it is linked
here, not repeated.

| Topic | Where it is covered |
| --- | --- |
| Which goal fetches an object, the object search, letting go | [AI: Fetching an object](ai.md#get-item), [Objects in an AI's hands](ai.md#ai-objects) |
| World flags (activities) | [AI: World flags](ai.md#ai-flags) |
| Hiding, cars and trains in brief | [AI: Cover, cars and trains](ai.md#ai-hazards) |
| Route links: climbs, jumps, doors, charges | [AI: Following a route](ai.md#route-follow), [Jump legs](ai.md#route-jump) |
| How the scripts drive the AI | [AI: How the scripts drive the AI](ai.md#script-control) |
| An AI's attacks and the held object's effect | [Combat: An AI's attacks](combat.md#ai-attacks) |
| Each weapon set's swings, throws and blocks | [Combat moves: Weapons](combat-moves.md#weapons) |
| Steps and giving way | [Characters: Step control](characters.md#step-control), [AI: Giving way](ai.md#giving-way) |

## Behaviour

### An AI presses the player's buttons {#buttons}

An AI human uses the world through the same functions as a player. Its actions write a **command** into its
per-player record (`PlayerRecord_SetCommand`, `0x00147ef0`), and `Player_UpdateActions` (`0x0027c120`) runs that
command for every human whose per-player `+0x1e` is 0, AI humans included ([Combat: The dispatcher](combat.md#dispatch)).
So the world use is the player's code, and a clean-room engine needs no second copy of it. Confirmed (code) at the
addresses cited.

| World use | How the AI asks | What runs | Commands |
| --- | --- | --- | --- |
| Pick an object up | `PickUpItem` action (below) | `Player_OnCommand33` → `Human_PickUpSearch` | `0x33` |
| Swing a held weapon | an attack kind with a set 1-3 object in hand | square or cross, armed branch | `0xf`, `0x10` |
| Throw a held object | cross with a set 4-6 object, aim at human `+0x128` | `Player_CrossWithWeapon` (`0x002880d8`) | `0x10` |
| Throw a melee weapon | kind with command `0x39` | `Player_ArmedSpecial` | `0x39` |
| Hit a car part | kind 11 or 12 with the car record in `+0xe0` (below) | the push or the grounded strike | `0x36`, `0x37` |
| Smash a breakable on a route | kind 19 at a run (below) | the running charge | `0x20` |
| Climb a fence or wall | the route follower calls the player's climb directly | `Climb_TryStart` (`0x002826f0`) | none |

The commands and their handlers are listed in [Combat moves: Commands only scripts and the AI send](combat-moves.md#ai-commands).

### What an AI never does {#never}

Confirmed (code) unless marked:

- **No mini-game.** `MiniGame_Start` (`0x0022dc40`) returns at once for a human that is not a pad player (human
  `+0x1b0` = −1). So an AI never steals a car stereo, picks a lock, mashes an object or plays a button mini-game.
  For a player, the mode in the player state's `+0x46` picks the game: 3 the stereo theft, 2 the lock pick, 4 the
  object mash, 5 the button game ([Crimes](crimes.md)). Before that, the held object is dropped.
- **No car.** No function in the Human or AI range puts a human into a car or drives one, and cars run on their own
  paths ([Cars](cars.md)). An AI meets a car only by hitting it ([Smashing a car](#cars)), by investigating a
  crime at one ([AI](ai.md#ai-hazards)) or by being hit by it. Inferred from the absence of any such caller.
- **No block.** An AI never starts a block, because the block reads the pad ([Combat: The dispatcher](combat.md#dispatch)).
- **No cover.** No goal seeks cover or shadow on its own ([AI: Cover, cars and trains](ai.md#ai-hazards)).
- **No closed door.** An AI never opens a closed door by itself ([Doors](#doors)).

### Picking up {#pick-up}

The `PickUpItem` action (vtable `0x00542ea0`) is the AI's press of the pick-up button. Confirmed (code) at the
addresses cited.

1. **Init** (`0x002faf98`) keeps the object's handle (−1 when none).
2. **Start** (`0x002fb000`) makes the object the human's object target (`Human_SetObjectTarget`, `0x00227080`) and
   writes command `0x33`.
3. The dispatcher runs `Player_OnCommand33` (`0x00281188`). It does nothing while the human holds any flag of
   `0x2fefefff` or has any state bit of `0x7bf9e9f7ff0`; otherwise it runs the player's pick-up search
   (`Human_PickUpSearch`, `0x0024d810`; [Combat: Breakables](combat.md#breakables),
   [World objects: Pickable objects](objects.md#pickable)).
4. **Update** (`0x002fb0a8`) reports running on its first update, then while held flag `0x4000` (a pick-up playing)
   is set, then done.

Two goals look for a weapon themselves, besides those listed in [AI: Objects in an AI's hands](ai.md#ai-objects).
Both push GetItem with gait 4 and use `Ai_FindObject` with a line of sight, a claim and a reach test
([AI: The object search](ai.md#get-item)):

| Goal | Helper | When | Radius | Filter mask | Extra test |
| --- | --- | --- | --- | --- | --- |
| Hostile | `Hostile_GrabWeapon` (`0x002d5668`) | no queued action, the goal's taunt count `+0x1e` still 0, human `+0x3b8` not 1, nothing held | 5 m | `0x20000` (weapons) | the object's type class `+0x87` is not 4 |
| DestroyCar | `DestroyCar_GetWeapon` (`0x002ddd48`) | actions not blocked, nothing held, no spot yet | 10 m | `0x10000` | `WorldObject_CanBePickedBy` (`0x00395148`) |

The masks are the filter's (`0x0053f2d0`) type bits; `0x10000` is the throwable bit, inferred from Melee asking for
both as `0x30000` ([AI](ai.md#ai-objects)).

**Hostile, when it does not fetch** (`HostileGoal_Process`, `0x002d58b8`, with no action queued):

- with the target more than 22.5° off (15° when the target stands still), turn to him;
- else, holding a throwable (`Human_HeldIsThrowable`), queue cross (kind 0) at him;
- else, at most every 3.0-3.5 s, say line `0xaf` and, unless holding a kind 4 or 6 object, play clip 595 (a random
  variant 0-3, after 0-249 ms; the argument meanings are inferred), and count the taunt in `+0x1e`;
- once that count is above 0, the next free update attacks him instead (`Hostile_Attack`) and the goal ends.

When it pushes GetItem, `Hostile_GrabWeapon` also sets the goal's `+0x1c` and drops what is held.

### Smashing a car {#cars}

**DestroyCar** (type `0x2e`, [its rows](ai-goals.md#goal-destroy-car)): the AI takes a **spot** round the car and
strikes the car part at that spot. Confirmed (code) at `0x002dde50` and the car functions cited.

**The spots.** Each car keeps 10 spot owners at `+0x12da` (one byte each, a human index or −1).

- **Reserve** (`Car_ReserveSpot`, `0x0038d810`): the first spot that meets all of these:
    - it is free;
    - it is not **blocked**: `Car_IsSpotBlocked` (`0x0038da88`) says a spot is blocked when every part of its mask is
      already off (car `+0x11f8`, [Cars](cars.md#car-object));
    - no player is within 1 m of the spot;
    - a ray (mask 8) from the spot to its part is clear;
    - the human can reach the spot on the nav mesh (`Nav_CanReach`).
- On a coupe (type index 1, [Cars: Types](cars.md#types)) spots 6 and 7 are skipped, and count as blocked.
- Each spot's part comes from `Car_GetSpotPartMask` (`0x0038da08`) and its position from `Car_GetSpotPosition`
  (`0x0038db40`), both a switch on the spot (jump tables `0x0057e410` and `0x0057e440`). The table below gives
  them.
- `Car_FindSpotOfHuman` (`0x0038d7d8`) and `Car_ReleaseSpot` (`0x0038d998`) find and free a human's spot; 10 means
  none.

**Where each spot is.** Every part keeps its world matrix in its 0xa0-byte record at car `+0x1a0 + 0xa0 × p`:
rows at `+0x00`, `+0x10`, `+0x20` and the position at `+0x30` (`Car_CachePartTransforms`, `0x00387780`, through
`CarInstance_GetPartLtm`). A spot is the part's position moved **0.8 m** along one of its rows, then its height set
to the car's height (`+0x18`) less **0.6 m** (**0.95 m** for a van, type 4). The part's position (`+0x30`) is also
the point the AI turns to and strikes at. Confirmed (code) at `0x0038da08` and `0x0038db40`:

| Spot | Part (mask) | Position, any type but the van | Position, van |
| --- | --- | --- | --- |
| 0 | 2, front bumper (`0x4`) | position − 0.8 × row 1 | position + 0.8 × row 2 |
| 1 | 3, back bumper (`0x8`) | position − 0.8 × row 1 | position + 0.8 × row 2 |
| 2 | 11, front side panel (`0x800`) | position + 0.8 × row 2 | same |
| 3 | 10, front side panel (`0x400`) | position + 0.8 × row 2 | same |
| 4 | 16, door (`0x10000`) | position + 0.8 × row 2 | same |
| 5 | 14, door (`0x4000`) | position + 0.8 × row 2 | same |
| 6 | 20, door (`0x100000`) | position + 0.8 × row 2 | position + 0.8 × (row 2 − row 1) |
| 7 | 18, door (`0x40000`) | position + 0.8 × row 2 | position + 0.8 × (row 2 − row 1) |
| 8 | 13, back side panel (`0x2000`) | position + 0.8 × row 2 | same |
| 9 | 12, back side panel (`0x1000`) | position + 0.8 × row 2 | same |

The part names are [Cars](cars.md#type-record)' reading of the part ids. Read from three parked cars in a state
file (a coupe and two sedans; the owner's quick-save 5, read from a copy), confirmed (runtime):

- each bumper's row 1 points into the car, so spots 0 and 1 stand 0.8 m beyond the bumpers;
- each side part's row 2 points straight out of its side, so spots 2-9 stand 0.8 m out from the side;
- odd spots (3, 5, 7, 9) are on one side and even spots (2, 4, 6, 8) on the other;
- each side has, front to back, the front side panel, the front door, the back door and the back side panel.

On a sedan, at 1.09 m car height, the spots stand at 0.49 m. A coupe skips spots 6 and 7, its back doors. The van's
own part frames were not read, so its rows are not checked.

**Start** (`0x002ddc18`) drops the target, drops a held object of set 1 (a knife), and takes the spot the human
already holds, if any.

**Process**, each update:

1. Done when the car is gone. Every 30 updates, look at the car for 1.5 s.
2. Wait while actions are queued. A failed move (brain `+0x284` not 0) clears that flag and ends the goal.
3. With a leash (goal `+0x14` > 0), done when farther than the leash from the gang's leader.
4. **No spot yet.** Done if the goal has had a spot before (`+0x1a`). Otherwise fetch a weapon
   (`DestroyCar_GetWeapon`, above), or else reserve a spot (done when none is free) and make the car the object
   target (`+0x128`).
5. When the spot has become blocked, release it, clear the object target and wait.
6. Mark that the goal has had a spot. When the human is more than 0.5 m from the spot, walk there (gait 2 within 2 m,
   else gait 4), out of the fight stance.
7. **At the spot.** Find the spot's part (`Car_GetSpotPartMask`, `Car_FindPartRecordByBits`). When the facing is more
   than 15° off the part, turn to it. Otherwise:
   - **the kind**: 11 (`0x36`, the push 21 `ATTACK_PUSH`) when the part is at least 1.33 m above the human's feet,
     12 (`0x37`, the grounded strike 193) when lower, and 0 (cross) whenever an object is held, so an armed AI
     swings its weapon at the car;
   - drop the human target, make the car the object target, and copy the part's position into the record's
     `+0xe0`-`+0xec`, where a player's target search puts a car target ([Combat: Target selection](combat.md#targets));
   - queue the attack at himself (`Brain_QueueAttack`);
   - every 8 s, when he may gesture: say line `0x8e` (30 %) or `0x8f` (70 %).

The car takes the hit through its own handler (`Car_OnHit`, `0x0038bea0`): 0.34 per hit from a weapon or thrown
object, 0.51 from a human with held flag `0x400000`, 0.115 otherwise. A gang-locked car ignores hits from other
gangs ([Cars: Windows, hits and the stereo](cars.md#windows)).

### Breakables, climbs and charges on a route {#route-objects}

The route follower handles these link kinds ([AI: Following a route](ai.md#route-follow)). On the Human side,
confirmed (code):

- **The charge** (`Route_SmashLeg`, `0x0029bca0`), at a breakable door or pane:
  1. aims at the link's point, at the route's radius, with gait 4;
  2. turns the body to face the point **at once** (`Human_SetRotation`), and sets its velocity to **8 m/s** toward it;
  3. writes the command of attack kind 19 (`0x20`, the running charge) into the per-player record.

  The player's charge then breaks the object ([Combat: Breakables](combat.md#breakables)).
- **The climb** (`Route_ClimbLeg` `0x0029b848`, and `Route_TryClimbNear` `0x0029b9b0` for a fast climber, human flag
  `0x2`, at a gait above 3 within 4.5 m) calls the player's `Climb_TryStart`
  ([Characters: Climbing](characters.md#climb)). The executable has no separate vault (no such function or
  caller), so a low fence is the same climb; inferred.

### Doors {#doors}

An AI never opens a closed door on its own. Confirmed (code) at the addresses cited.

- **On a route**, a closed door's link (kind `0x10` with the avoid bit) refuses the move with brain `+0x284` = 4,
  an open door's link is walked, and a breakable door is charged ([AI: Following a route](ai.md#route-follow)).
- **Scripted**: `OpenDoorAnimated(door, human)` sends the door message 0. `Door_OpenAnimated` (`0x00396fa8`) makes
  the door the human's object target and sets its action code (record `+0x14`) to **26** (`Human_SetAction`,
  `0x002266a8`; [World objects: Doors](objects.md#doors)).
    - `Human_ChooseAnimState` (`0x00259578`) turns action 26 into anim state **34** (`0x22`).
    - Anim state 34's builder (table `0x005106e0`) is `Human_StartObjectSteerIdle` (`0x0025ff18`). It runs
      `Human_StartObjectSteer` (`0x0025e948`), then puts the anim state and the action back to 0.
    - `Human_StartObjectSteer` plays clip **666** `ANIM_SPECIAL_OPEN_DOOR`, then the idle. While the clip plays it
      holds flag `0x4000`, gives the human a push weight of 1e9, and steers him onto the object target over 0.1 s.
    - On the clip's animation event, with an object target, `Human_HandleMessage` (`0x00245920`) calls
      `Door_OpenBy(door, human)` (`0x00396f08`) and then clears the object target.
    - `Door_OpenBy` sends the door DoorOpen (message `0x0b`) with the human, which swings the door ±170° away from
      him. It also runs the objects' script callback.

  Confirmed (code) at each address. This action 26 is not the jump's anim state 26 ([Characters](characters.md)),
  which is the anim-state field `+0x18`.
- **Gang spawners** (`GangSpawner_Update`, `0x001681a0`). When a member is spawned, the spawner opens its door
  (spawner `+0x70`, gang `+0x6b0` for the first). It keeps the new human at spawner `+0x10` and notes the time at
  `+0x78` and `+0x80`. Each update after that:
    - while that human has no character instance yet (human `+0xd8` is 0,
      [Characters](characters.md#the-record)), the open time is renewed. So the door stays open until the human
      has his model (inferred: the model is still loading);
    - once the time is more than `+0x6b4` ms past, the door closes;
    - otherwise the door is told to open again every 500 ms.

  [AI: Spawners](ai.md#spawners) gives the slot's other fields.

### Reacting to the world {#reactions}

What an AI does about world things it does not use:

- Dealers: [AI: GoalDealer](ai.md#dealer).
- Breakables smashed by gangs: VandalizeItem, DestroyItem and Riot ([AI goals](ai-goals.md)).
- A crime at a car: the Pursue tactic ([AI](ai.md#ai-hazards)).
- Trains: RunFromTrain ([AI](ai.md#ai-hazards)).

No goal reacts to a door opening or to an object breaking as such: those reach the AI only as noises and crime
events ([AI](ai.md)). Inferred: no other caller of the door or breakable functions in the AI range.

## Open questions

- The van's part frames, so its spots 0, 1, 6 and 7 can be placed without guessing (only sedans and a coupe were read).
- An AI's push (kind 11) and grounded strike (kind 12) at a car: the AI has dropped its human target, so neither
  handler (`0x0027de78`, `0x00287fe0`) has a target to steer to, and neither reads the record's `+0xe0` car copy.
  How the clip's strike reaches the car part (the strike spheres against the car's boxes, inferred) is not traced.
