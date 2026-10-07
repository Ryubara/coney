# AI code index: brains, actions and tactics

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra
(2026-10-07); each row states its evidence level.

## Purpose

The function-by-function index of the AI code from `0x00288000` up: the brain core, the actions, the brain types and
the tactics. [AI humans](ai.md) explains how these pieces work together and is the page to read first; this one
names every function, says in a line what it does, and links to the section of [AI humans](ai.md) that covers it.

## Actions {#actions}

The action classes in `0x002f9e48`-`0x002fee38` ([AI humans: actions](ai.md#actions) has the pool and the base
fields). Each class has a vtable whose word `+0x0c` returns its type id and `+0x34` its name; the names below are the
executable's. Start returns 2 when the action is already done, Abort 0 when it refuses, Update 2 when it is done.
Confirmed (code) for the vtables and their slots.

| Class (type) | Vtable | Init | Start | Abort | Update | What it does |
| --- | --- | --- | --- | --- | --- | --- |
| `MoveTo` (1) | `0x00542f20` | `0x002fb9e8`, `0x002fbae0` | `0x002fc420` | `0x002fc560` | `0x002fc5c0` | walks to a point ([Moving](ai.md#move-action)) |
| `MoveMelee` (2) | `0x005431e0` | `0x002fcf50` | `0x002fcff0` | `0x002fd068` | `0x002fd4e8` | keeps a fight distance band from a human |
| `MoveMeleeLine` (3) | `0x00542fa0` | `0x002fe6e8` | `0x002fe720` | - | `0x002fe740` | sidesteps round a human for 2 s |
| `Dive` (4) | `0x005431a0` | `0x002fda38` | `0x002fda68` | `0x002fdae8` | `0x002fdb28` | dives toward a point |
| `Shuffle` (5) | `0x00542f60` | `0x002feaf8` | `0x002feb28` | - | `0x002feb48` | shuffles sideways in the fight stance |
| `TurnToDir` (6) | `0x00543160` | `0x002fdc28` | `0x002fdc68` | `0x002fdcc8` | `0x002fdd08` | turns to a heading |
| `Turn` (7) | `0x00543120` | `0x002fde78` | `0x002fdec8` | `0x002fdcc8` | `0x002fdf50` | turns by n × 45° |
| `TurnTo` (8) | `0x005430e0` | `0x002fe000` | `0x002fe058` | `0x002fdcc8` | `0x002fe0a8` | turns to a point |
| `LookAt` (9) | `0x005430a0` | `0x002fe160` | `0x002fdc68` | `0x002fdcc8` | `0x002fe1b0` | turns to a human, following him |
| `LookAround` (10) | `0x00543060` | `0x002fe248` | `0x002fe280` | `0x002fe320` | `0x002fe360` | plays the look-around clip with a wide view |
| `TakeStep` (11) | `0x00543020` | `0x002fe380` | `0x002fe3a8` | `0x002fe3d8` | `0x002fe428` | one step on a heading |
| `GiveWay` (12) | `0x00542fe0` | `0x002fe568` | `0x002fe5d8` | `0x002fe658` | `0x002fe6c8` | a step aside for a human, looking at him |
| `Attack` (13) | `0x00542ce0` | `0x002fa918` | `0x002fa9a8` | `0x002fad30` | `0x002fad70` | [The attack action](ai.md#attack-action) |
| `PlayFidget` (15) | `0x00542c60` | `0x002f9f48` | `0x002f9f78` | `0x002fa0e0` | `0x002fa1e0` | an idle fidget clip, with an optional line |
| `ParlayStart` (17) | `0x00542c20` | `0x002fa220` | `0x002fa248` | - | `0x002fa270` | waits for the pad to start a parley |
| `PlayGenAnim` (20) | `0x00542b60` | `0x002fa5a0` | `0x002fa5e0` | `0x002fa608` | `0x002fa650` | a gang clip of an anim id |
| `PlayGenToIdleAnim` (21) | `0x00542b20` | `0x002fa6e0` | `0x002fa710` | `0x002fa770` | `0x002fa800` | a clip that ends in the idle |
| `PlayDynAnim` (22) | `0x00542be0` | `0x002fa300` | `0x002fa338` | `0x002fa398` | `0x002fa3e0` | a dynamic clip with a variant |
| `PlayDynLoopAnim` (23) | `0x00542ba0` | `0x002fa468` | `0x002fa4a0` | `0x002fa4c8` | `0x002fa510` | a dynamic clip played until its flags clear |
| `PlaySound` (24) | `0x00542d20` | `0x002fb868` | `0x002fb898` | - | `0x002fb8a0` | says a speech command |
| `Nothing` (25) | `0x00542ee0` | `0x002faf70` | - | - | `0x002faf90` | does nothing |
| `PickUpItem` (26) | `0x00542ea0` | `0x002faf98` | `0x002fb000` | `0x002fb050` | `0x002fb0a8` | picks up an object |
| `Revive` (27) | `0x00542de0` | `0x002fb418` | `0x002fb440` | `0x002fb470` | `0x002fb4a8` | revives a downed ally |
| `SetCommand` (28) | `0x00542ca0` | `0x002faeb0` | `0x002faee0` | - | `0x002faf20` | presses one pad command |
| `Taunt` (29) | `0x00542e60` | `0x002fb0f0` | `0x002fb128` | `0x002fb278` | `0x002fb2f0` | a taunt or gesture by kind |
| `Tag` (30) | `0x00542da0` | `0x002fb4f0` | `0x002fb520` | `0x002fb578` | `0x002fb5c0` | sprays a tag |
| `UsePhone` (31) | `0x00542d60` | `0x002fb650` | `0x002fb680` | `0x002fb6f0` | `0x002fb738` | stands at a phone flag |
| `UnlockHuman` (32) | `0x00542e20` | `0x002fb338` | `0x002fb360` | `0x002fb390` | `0x002fb3c8` | frees a cuffed human (a mini-game) |
| `Stop` (`0x4141`) | `0x00543220` | `0x002fcf28` | - | - | `0x002fcf48` | stops the human |

Every function of the classes, and the helpers among them:

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002f9e48` | `Action_Update` | base Update | counts the start delay down (−1 = a random 0-500 ms) against the brain's last update time, calls Start once, then the class's Update | confirmed (code) |
| `0x002f9f48` | `FidgetAction_Init` | `PlayFidget` init | fidget kind, line, delay | confirmed (code) |
| `0x002f9f78` | `FidgetAction_Start` | `PlayFidget` Start | done for a dog (class 221), a brain of kind 6, or without a fidget clip (`0x00225f58`) unless holding a kind-11 object; says the line (1.5 × louder when the target is a player), plays the fidget (`0x00231ac8`) | confirmed (code) |
| `0x002fa0e0` | `FidgetAction_Abort` | `PlayFidget` Abort | refused for a Warrior whose crew is following a free chief who has no enemies around (command neither 0, 4 nor 5); else stops the fidget (`0x00231b60`) | confirmed (code) |
| `0x002fa1e0` | `FidgetAction_Update` | `PlayFidget` Update | done once the fidget's flags clear (`0x00228520`) | confirmed (code) |
| `0x002fa220` | `ParlayStartAction_Init` | `ParlayStart` init | a deadline | confirmed (code) |
| `0x002fa270` | `ParlayStartAction_Update` | `ParlayStart` Update | with the deadline passed or none, waits for pad 0's button `0x10`; writes game state `+0x412` = 1 (pressed) or 3 (timed out) | confirmed (code); the parley meaning inferred from the class name |
| `0x002fa300` | `PlayAnimAction_Init` | `PlayDynAnim` init | anim id, variant, blends ([The dealer's gestures](ai.md#dealer-gestures)) | confirmed (code) |
| `0x002fa338` | `PlayAnimAction_Start` | `PlayDynAnim` Start | [The dealer's gestures](ai.md#dealer-gestures) | confirmed (code) |
| `0x002fa398` | `PlayAnimAction_Abort` | `PlayDynAnim` Abort | clears the clip's held flags | confirmed (code) |
| `0x002fa3e0` | `PlayAnimAction_Update` | `PlayDynAnim` Update | plays once, waits while its flags last | confirmed (code) |
| `0x002fa468` | `DynLoopAnimAction_Init` | `PlayDynLoopAnim` init | anim id, variant, blend | confirmed (code) |
| `0x002fa4a0` | `DynLoopAnimAction_Start` | `PlayDynLoopAnim` Start | stops the gait | confirmed (code) |
| `0x002fa4c8` | `DynLoopAnimAction_Abort` | `PlayDynLoopAnim` Abort | drops the overlay task while its flags are held | confirmed (code) |
| `0x002fa510` | `DynLoopAnimAction_Update` | `PlayDynLoopAnim` Update | starts the clip (`0x0025a3e0`, blend 0.1, looping), then waits while its flags are held | confirmed (code) |
| `0x002fa5e0` | `PlayAnimIdAction_Start` | `PlayGenAnim` Start | stops the gait | confirmed (code) |
| `0x002fa608` | `PlayAnimIdAction_Abort` | `PlayGenAnim` Abort | rebuilds the idle when the clip's flags are still held | confirmed (code) |
| `0x002fa650` | `PlayAnimIdAction_Update` | `PlayGenAnim` Update | plays once, waits while its flags last | confirmed (code) |
| `0x002fa6e0` | `GenToIdleAnimAction_Init` | `PlayGenToIdleAnim` init | anim id, "hold" flag | confirmed (code) |
| `0x002fa710` | `GenToIdleAnimAction_Start` | `PlayGenToIdleAnim` Start | drops a held kind-4 or kind-6 object, stops the gait | confirmed (code) |
| `0x002fa770` | `GenToIdleAnimAction_Abort` | `PlayGenToIdleAnim` Abort | with the hold flag: rebuilds the idle while `0x20000` is held; else refused while `0x1c16a40` is held and he is up | confirmed (code) |
| `0x002fa800` | `GenToIdleAnimAction_Update` | `PlayGenToIdleAnim` Update | plays the clip as an action clip holding `0x20000` (`0x22000` without the hold flag), waits while held | confirmed (code) |
| `0x002fa878` | `Action_ExecuteAttack` | binding worker | queues an attack on a human (`Brain_QueueAttack`) | confirmed (code) |
| `0x002faeb0` | `SetCommandAction_Init` | `SetCommand` init | a command id | confirmed (code) |
| `0x002faee0` | `SetCommandAction_Start` | `SetCommand` Start | writes the command to his player record (`PlayerRecord_SetCommand`) | confirmed (code) |
| `0x002faf20` | `SetCommandAction_Update` | `SetCommand` Update | one update, then waits while any flag is held | confirmed (code) |
| `0x002faf70` | `NothingAction_Init` | `Nothing` init | base fields only | confirmed (code) |
| `0x002faf98` | `PickUpItemAction_Init` | `PickUpItem` init | the object's handle | confirmed (code) |
| `0x002fb000` | `PickUpItemAction_Start` | `PickUpItem` Start | sets the object as his pick-up target (`0x00227080`) and presses command `0x33` | confirmed (code) |
| `0x002fb050` | `PickUpItemAction_Abort` | `PickUpItem` Abort | refused while the pick-up runs (`0x00228408`); else clears the target | confirmed (code) |
| `0x002fb0a8` | `PickUpItemAction_Update` | `PickUpItem` Update | waits while the pick-up runs | confirmed (code) |
| `0x002fb0f0` | `TauntAction_Init` | `Taunt` init | kind, speech line, delay | confirmed (code) |
| `0x002fb128` | `TauntAction_Start` | `Taunt` Start | done for a brain of kind 6, or without a taunt clip unless kind 13; plays it (`0x0025f028`) and says the line; kinds 0-2 and 8 are gestures, others counted in `0x005112ac` | confirmed (code) |
| `0x002fb278` | `TauntAction_Abort` | `Taunt` Abort | refused while the taunt plays (`0x00223c30`); else uncounts it | confirmed (code) |
| `0x002fb2f0` | `TauntAction_Update` | `Taunt` Update | waits while the taunt plays | confirmed (code) |
| `0x002fb338` | `UnlockHumanAction_Init` | `UnlockHuman` init | base fields | confirmed (code) |
| `0x002fb360` | `UnlockHumanAction_Start` | `UnlockHuman` Start | presses command `0x32` | confirmed (code) |
| `0x002fb390` | `UnlockHumanAction_Abort` | `UnlockHuman` Abort | refused while he is in the mini-game | confirmed (code) |
| `0x002fb3c8` | `UnlockHumanAction_Update` | `UnlockHuman` Update | waits while he is in the mini-game | confirmed (code) |
| `0x002fb418` | `ReviveAction_Init` | `Revive` init | base fields | confirmed (code) |
| `0x002fb440` | `ReviveAction_Start` | `Revive` Start | presses command `0x27` | confirmed (code) |
| `0x002fb470` | `ReviveAction_Abort` | `Revive` Abort | refused while reviving (`0x00228468`) | confirmed (code) |
| `0x002fb4a8` | `ReviveAction_Update` | `Revive` Update | waits while reviving | confirmed (code) |
| `0x002fb4f0` | `TagAction_Init` | `Tag` init | the tag spot and its argument | confirmed (code) |
| `0x002fb520` | `TagAction_Start` | `Tag` Start | starts the tag (`Human_Tag`) | confirmed (code) |
| `0x002fb578` | `TagAction_Abort` | `Tag` Abort | refused while tagging | confirmed (code) |
| `0x002fb5c0` | `TagAction_Update` | `Tag` Update | presses command 10 each update, waits while tagging (or while his top task is kind `0x17`) | confirmed (code) |
| `0x002fb650` | `UsePhoneAction_Init` | `UsePhone` init | the phone flag, a duration, a line | confirmed (code) |
| `0x002fb680` | `UsePhoneAction_Start` | `UsePhone` Start | switches his anims to the phone set (`Human_UseAnim` with the name at `0x005686c0`), deadline = now + duration | confirmed (code) |
| `0x002fb6f0` | `UsePhoneAction_Abort` | `UsePhone` Abort | back to his own anims | confirmed (code) |
| `0x002fb738` | `UsePhoneAction_Update` | `UsePhone` Update | done at the deadline; says the line once when someone is near him (`+0x348`/`+0x34c`); faces the flag's heading, standing | confirmed (code) |
| `0x002fb868` | `PlaySoundAction_Init` | `PlaySound` init | speech command, volume, a flag | confirmed (code) |
| `0x002fb8a0` | `PlaySoundAction_Update` | `PlaySound` Update | says the command when he may speak (`0x00291ed0`); done at once | confirmed (code) |
| `0x002fb918` | `Action_MoveTo` | binding worker | a move action for a scripted human | confirmed (code) |
| `0x002fbae0` | `MoveAction_InitEx` | `MoveTo` init | point, radius, gait, speed (at least the walk's), two flags | confirmed (code) |
| `0x002fbbe0` | `MoveAction_SetSpeed` | `MoveTo` helper | stores the speed (`+0x40`) | confirmed (code) |
| `0x002fbbe8` | `MoveAction_SetTarget` | `MoveTo` helper | a new destination farther than 0.07 m: stored, the route is kept or the action restarted | confirmed (code) |
| `0x002fbd18` | `MoveAction_CollectPoints` | `MoveTo` helper | the next points of the route, with the leg kinds | confirmed (code) |
| `0x002fbef0` | `MoveAction_TrialSpeed` | `MoveTo` helper | the speed a simulated walk along the points allows ([Moving](ai.md#move-action)) | confirmed (code) |
| `0x002fc158` | `MoveAction_CornerSpeed` | `MoveTo` helper | the corner speed of the next waypoints | confirmed (code) |
| `0x002fc330` | `MoveAction_CheckStuck` | `MoveTo` helper | every 60 updates while moving: less than 0.2 m covered → brain `+0x284` = 3 | confirmed (code) |
| `0x002fc3e8` | `MoveAction_ResetSpeed` | `MoveTo` helper | speed = the gait's speed | confirmed (code) |
| `0x002fc560` | `MoveAction_Abort` | `MoveTo` Abort | marks it aborted, clears the move and the route | confirmed (code) |
| `0x002fcd90` | `Move_CornerSpeedLimit` | `MoveTo` helper | the highest speed that still turns through a corner circle, stepping the gait down | confirmed (code) |
| `0x002fceb8` | `Action_Stop` | binding worker | a `Stop` action for a scripted human | confirmed (code) |
| `0x002fcf28` | `StopAction_Init` | `Stop` init | base fields | confirmed (code) |
| `0x002fcff0` | `MoveMeleeAction_Start` | `MoveMelee` Start | deadline; a brain of kind 6 or 7 leaves the fight stance; saves the turn boost | confirmed (code) |
| `0x002fd068` | `MoveMeleeAction_Abort` | `MoveMelee` Abort | faces the target (kind 6 or 7), restores the turn boost | confirmed (code) |
| `0x002fd128` | `Brain_IsKind6Or7` | helper | the brain's class kind `+0x11b` is 6 or 7 | confirmed (code) |
| `0x002fd158` | `MoveMeleeAction_PickSide` | `MoveMelee` helper | which way to circle (2 or 6), or 8 for straight in, from free sides round him (`0x0029e9c8`, `0x0029ea48`) and whether he is grabbed from the rear | confirmed (code) |
| `0x002fd4e8` | `MoveMeleeAction_Update` | `MoveMelee` Update | keeps between the band's near and far distances: steps back (−10) or in (+10) over 10 updates, circles to the side picked, a turn boost when closing on a target facing away; done in band | confirmed (code) |
| `0x002fda38` | `DiveAction_Init` | `Dive` init | a point | confirmed (code) |
| `0x002fda68` | `DiveAction_Start` | `Dive` Start | drops his target, faces the point, plays dive clip 2 (`0x0025a3e0`) | confirmed (code) |
| `0x002fdae8` | `DiveAction_Abort` | `Dive` Abort | refused while `0x5c0221f` is held | confirmed (code) |
| `0x002fdb28` | `DiveAction_Update` | `Dive` Update | gait 4 while `0x5c0221f` is held | confirmed (code) |
| `0x002fdb90` | `Action_TurnToHeading` | binding worker | a `TurnToDir` action | confirmed (code) |
| `0x002fdc28` | `TurnAction_Init` | `TurnToDir` init | heading, turn rate | confirmed (code) |
| `0x002fdc68` | `TurnAction_Start` | turn Start | done while `0x1c16a40` is held; else a 3 s limit | confirmed (code) |
| `0x002fdcc8` | `TurnAction_Abort` | turn Abort | marks it aborted; refused while turning in place (`0x00228500`) | confirmed (code) |
| `0x002fdd08` | `TurnAction_Update` | turn Update | sets brain `+0x110` to the heading; done within 15° (0.2618 rad) when not turning, at the limit, or aborted | confirmed (code) |
| `0x002fdde8` | `Action_Turn` | binding worker | a `Turn` action | confirmed (code) |
| `0x002fde78` | `TurnByAction_Init` | `Turn` init | n eighths of a turn | confirmed (code) |
| `0x002fdec8` | `TurnByAction_Start` | `Turn` Start | heading = brain `+0x110` + n × 45° | confirmed (code) |
| `0x002fdf50` | `TurnByAction_Update` | `Turn` Update | the turn Update | confirmed (code) |
| `0x002fdf70` | `Action_TurnToTarget` | binding worker | a `TurnTo` action | confirmed (code) |
| `0x002fe000` | `TurnToPointAction_Init` | `TurnTo` init | a point, turn rate | confirmed (code) |
| `0x002fe058` | `TurnToPointAction_Begin` | `TurnTo` Start | heading to the point, then the turn Start | confirmed (code) |
| `0x002fe0a8` | `TurnToPointAction_Update` | `TurnTo` Update | the turn Update | confirmed (code) |
| `0x002fe0c8` | `Action_LookAt` | binding worker | a `LookAt` action | confirmed (code) |
| `0x002fe160` | `LookAtAction_Init` | `LookAt` init | the human's handle | confirmed (code) |
| `0x002fe1b0` | `LookAtAction_Update` | `LookAt` Update | the heading to the human, taken each update ([Look at](ai.md#actions)) | confirmed (code) |
| `0x002fe248` | `LookAroundAction_Init` | `LookAround` init | 2000 ms, 110° | confirmed (code) |
| `0x002fe280` | `LookAroundAction_Start` | `LookAround` Start | deadline = clip `0x29e`'s length; field of view 4.71 rad and sight range `0x14d` while it plays (old values saved) | confirmed (code) |
| `0x002fe320` | `LookAroundAction_Abort` | `LookAround` Abort | restores the field of view and range | confirmed (code) |
| `0x002fe380` | `TakeStepAction_Init` | `TakeStep` init | a heading | confirmed (code) |
| `0x002fe3a8` | `TakeStepAction_Start` | `TakeStep` Start | brain heading, starts the step (`0x002432b0`) | confirmed (code) |
| `0x002fe3d8` | `TakeStepAction_Abort` | `TakeStep` Abort | ends the step; refused while stepping (`0x00228448`) | confirmed (code) |
| `0x002fe428` | `TakeStepAction_Update` | `TakeStep` Update | waits while the step's locomotion handler (`0x00243420`) runs | confirmed (code) |
| `0x002fe4b0` | `Action_GiveWay` | binding worker | a `GiveWay` action for a human with fewer than 8 actions | confirmed (code) |
| `0x002fe568` | `GiveWayAction_Init` | `GiveWay` init | a step, the human, a boost flag; marks the brain (`+0xcc` bit 1) | confirmed (code) |
| `0x002fe5d8` | `GiveWayAction_Start` | `GiveWay` Start | looks at him for 2 s, optional turn boost, then the step | confirmed (code) |
| `0x002fe658` | `GiveWayAction_Abort` | `GiveWay` Abort | the step's Abort, the boost restored | confirmed (code) |
| `0x002fe6c8` | `GiveWayAction_Update` | `GiveWay` Update | the step's Update | confirmed (code) |
| `0x002fe6e8` | `MoveMeleeLineAction_Init` | `MoveMeleeLine` init | the human, a point | confirmed (code) |
| `0x002fe720` | `MoveMeleeLineAction_Start` | `MoveMeleeLine` Start | a 2 s limit | confirmed (code) |
| `0x002fe740` | `MoveMeleeLineAction_Update` | `MoveMeleeLine` Update | sidesteps over 6 updates to the side the point lies on, stopping when that side's slots are blocked; gait 2 or the human's | confirmed (code) |
| `0x002feaf8` | `ShuffleAction_Init` | `Shuffle` init | the human, a duration | confirmed (code) |
| `0x002feb48` | `ShuffleAction_Update` | `Shuffle` Update | in the fight stance, shuffles 20 updates to a random side (51 % right), stopping at a blocked side; done at the deadline | confirmed (code) |
| `0x002fee38` | `Objects_GetDistance` | binding worker | the distance between two objects | inferred |
| `0x002feeb0` | `Peds_SetGlobalRules` | binding worker | stores twelve pedestrian rule values (`0x006ea188`-`0x006ea19e`), every third squared (distances) | confirmed (code) |

## Brain types {#brain-types}

The think and event handlers per brain type ([Types](ai.md#types) has the table and what each does).

| Address | Name | Role | What it does | Evidence |
| --- | --- | --- | --- | --- |
| `0x002fef40` | `CivilianBrain_Think` | type 4 think | civilians' think | confirmed (code) at the table `0x00715568` |
| `0x002ff198` | `CivilianBrain_CanReactToCrime` | type 4 helper | alive and free, top goal not one of `0x6b`, 2, `0x6d`, `0x6e`, `0x5d`, `0x11`, `0x50`, `0x8c`, no fight goal and no gang tactic (goal `0x4f` asks its own test) | confirmed (code) |
| `0x002ff2b0` | `CivilianBrain_OnCrimeSeen` | type 4 crime witness | under goal `0x6d` the goal handles it; else, if he may react and sees the offender within his sight range, he may phone the police (a car or store crime, game state `+0x2a1`, a chance `0x00510adf`, 100 % for a car of state 3) or flee | confirmed (code) for the tests; the phoning inferred |
| `0x002ffb30` | `CivilianBrain_OnEvent` | type 4 event | civilians' events | confirmed (code) at the table `0x007155a0` |
| `0x00300678` | `CopBrain_Think` | type 1 think | cops' think | confirmed (code) at the table `0x00715568` |
| `0x00301248` | `CopBrain_ChaseTarget` | type 1 helper | unless goals `0x78`, `0x79` or `0x3f` run: a target in sight and not of the player's gang (or of it) gets a fight goal and an arrest goal of kind 12 (`0x002c4060`) | confirmed (code) |
| `0x00301538` | `CopBrain_OnCrimeSeen` | type 1 crime witness | a cop who sees the offender picks the response by what was hit (a car or store: 13, a near human: 2, else 5; 12 when crime 12 is on) and, unless the offender's gang is not wanted (`+0x5e8`), fights and arrests him; a crime against himself makes him an enemy | confirmed (code) |
| `0x00302038` | `CopBrain_CanRespond` | type 1 helper | police on (game state `+0x288`), alive, top goal not `0x75`, `0x76`, `0x73`, 1, `0x5e`, `0x47` ... | confirmed (code) |
| `0x00302118` | `CopBrain_CountBackup` | type 1 helper | how many more cops a call needs (a third of the suspects less those already on it), when the gang's last call (`+0x5f4`) is 30 s old | confirmed (code) |
| `0x00302340` | `CopBrain_OnEvent` | type 1 event | cops' events | confirmed (code) at the table `0x007155a0` |
| `0x00302cd8` | `DealerBrain_Think` | type 5 think | dealers' think | confirmed (code) at the table `0x00715568` |
| `0x00302df8` | `DealerBrain_CanReactToHit` | type 5 helper | [The dealer: buying](ai.md#dealer-buy) | confirmed (code) |
| `0x00302eb0` | `DealerBrain_FleeOrFight` | type 5 helper | rolls the run chance: flee or fight 15 s ([The dealer: buying](ai.md#dealer-buy)) | confirmed (code) |
| `0x00303260` | `PlayerBrain_Think` | type 0 think | the player's think | confirmed (code) at the table `0x00715568` |
| `0x00303468` | `PlayerBrain_OnPrompt` | type 0 helper | event 0 (triangle on a human): faces the human (`0x00233b08`); if he holds something and is not in shadow, he says line `0x92` | confirmed (code) |
| `0x003035d8` | `PlayerBrain_Update` | type 0 update | target upkeep, hostiles counted, the [automatic commands](ai.md#warrior-auto-commands) | confirmed (code) |
| `0x00303988` | `WarChief_AutoCommand` | type 0 helper | [Automatic commands](ai.md#warrior-auto-commands) | confirmed (code) |
| `0x00303db8` | `WarChief_IsTargetInRange` | type 0 helper | the chief's target within his far melee range and not a Warrior hitting back | confirmed (code) |
| `0x00303e68` | `Brain_HasAttackers` | helper | the attacker list (`+0x1a4`) is not empty | confirmed (code) |
| `0x00303e90` | `PlayerBrain_OnEvent` | type 0 event | event 0 → the prompt; 11, 12, 20 consumed; others to the shared handler (`0x00292d80`) | confirmed (code) |
| `0x00303f10` | `CivlCoDiBrain_Think` | type 6 think | think of the `civl_co_di` kind | confirmed (code) at the table `0x00715568` |
| `0x00304030` | `Brain_IsFreeToFight` | type 6 helper | alive and without a fight goal | confirmed (code) |
| `0x00304228` | `CivlCoDiBrain_OnEvent` | type 6 event | events of the `civl_co_di` kind | confirmed (code) at the table `0x007155a0` |
| `0x00304c78` | `GangBrain_OnCrimeSeen` | type 2 crime witness | a gang member who sees a crime by someone hostile to him (`0x00290138`) and has no goal `0x9a` reacts toward the offender's position | confirmed (code); the reaction not traced |
| `0x00304fa8` | `GangBrain_OnEvent` | type 2 event | gang members' events | confirmed (code) at the table `0x007155a0` |
| `0x003052f0` | `WarriorBrain_Think` | type 3 think | [The Warriors' pick-ups](ai.md#warrior-pickups) and the fight push | confirmed (code) |
| `0x00306040` | `WarriorBrain_OnPrompt` | type 3 helper | triangle on a Warrior: when both are free and in reach (`0x0021c0a8`), faces him; line `0x92` as the player's | confirmed (code) |
| `0x00306190` | `WarriorBrain_OnHitByChief` | type 3 helper | a Warrior hit by his chief hits back with an `AttackTarget` goal (type 9, `0x002aee48`), counted in the chief's brain `+0x2e4` | confirmed (code) |
| `0x003063b0` | `WarriorBrain_OnEvent` | type 3 event | Warriors' events | confirmed (code) at the table `0x007155a0` |
