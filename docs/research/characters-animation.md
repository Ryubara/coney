---
kind: topic
system: characters
owns:
  functions: [
    "0x00175080", "0x0023a9b8", "0x0023a9f8", "0x0023aab0", "0x0023ab80", "0x0023ac50", "0x00250e00", "0x00250e48",
    "0x002510a8", "0x00251188", "0x002516d0", "0x002516f0", "0x00251710", "0x00251718", "0x00251768", "0x00251820",
    "0x00251a00", "0x00252108", "0x00252ae0", "0x00252b08", "0x00252c28", "0x00253068", "0x00253118", "0x00253170",
    "0x00253190", "0x002531b8", "0x002531c0", "0x00253268", "0x00253350", "0x00253398", "0x002533b8", "0x00253480",
    "0x002534a8", "0x002534c0", "0x002534d0", "0x00253528", "0x00253538", "0x00253560", "0x00253598", "0x002535c0",
    "0x00253e68", "0x00253f78", "0x00254060", "0x002541c8", "0x002544b8", "0x002545a8", "0x002548c8", "0x00254e20",
    "0x00255540", "0x00255cf8", "0x002570e8", "0x00257ce8", "0x00257d70", "0x00257e38", "0x00257ef0", "0x00258330",
    "0x00258968", "0x00259578", "0x0025a178", "0x0025a310", "0x0025aeb8", "0x0025af90", "0x0025b200", "0x0025b9f8",
    "0x0025c240", "0x0025c5b8", "0x0025cb90", "0x0025cd58", "0x0025cf10", "0x0025cf30", "0x0025d020", "0x0025d2d0",
    "0x0025d348", "0x0025d390", "0x0025e0e0", "0x0025e250", "0x0025e900", "0x0025e948", "0x0025ec28", "0x0025eee0",
    "0x0025ef58", "0x0025f028", "0x0025f1b8", "0x0025f450", "0x0025f4d0", "0x0025f770", "0x0025fba0", "0x0025fbd0",
    "0x0025fc00", "0x0025fcb0", "0x0025ff18", "0x0025ff50", "0x002726d8",
  ]
---
# Characters: anim slots, clips and callbacks

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records or, for the older ones, How this was checked.

## Summary

How a human's animation is chosen. The 0x180 record holds 35 anim slots copied from a default table, a movement
style swaps in other ids, and each update the human's state picks the clip to play and the task that builds it.
A script can also register a callback that runs when a clip starts. The animation format and the task system
are on [Animation](formats/animation.md); the functions of the `Animation/` code on
[Animation: code](animation.md); the speeds the clips give are on [Classes](characters-classes.md#speed-classes).

## Map {#original-structure}

The functions of the clip choice and the callbacks:

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00259578` | `Human_ChooseAnimState` | picks the anim state, calls its builder from `0x005106e0` | confirmed (code) |
| `0x0025b200`, `0x0025b9f8`, `0x0025f770` | move, run-start and idle task builders | see [Clip selection](#clip-selection) | confirmed (code) |
| `0x0025ec28` | `Gait_BlendForSpeed` | speed → gait blend target 0-3 | confirmed (code) |
| `0x00175080` | `CharacterInstance_GetAnim(id)` | an anim id to an animation | confirmed (code) |
| `0x0023ac50` | `AnimCallback_Dispatch(human, id)` | runs the script's anim callback ([Animation callbacks](#anim-callbacks)) | confirmed (code) |

### Movement styles, clips and actions {#code-movement}

From `0x00250e00` (inferred from where the rows lie; each row is graded): movement styles and anim slots, the clip
builders ([Clip selection](#clip-selection)), the turn and arrival clips of an AI, and the per-human actions.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00250e00` | `NavLink_DisableDoor` | Marks the door links near a point to be avoided (kind 0x10), as when a door shuts. | confirmed (code) |
| `0x00250e48` | `NavLink_EnableDoor` | Clears the avoid mark of the door links near a point (kind 0x10). | confirmed (code) |
| `0x002510a8` | `NavNode_IsLinkBlocked` | Whether a node's link list holds a link to the given node with a negative cost (a blocked edge). | confirmed (code) |
| `0x00251188` | `PathRequests_Clear` | Clears the 32 path-request slots (0x110 bytes each, at 0x006cc340 downward). | confirmed (code) |
| `0x002516d0` | `PathNode_SetIndex` | Stores a node pointer as its index (offset from the node array 0x006ca21c / 32) at slot i of a list. | confirmed (code) |
| `0x002516f0` | `PathNode_GetAt` | The node pointer at slot i of a list (the node array 0x006ca21c + index x 32). | confirmed (code) |
| `0x00251710` | `Ptr_Set` | Writes a word: a one-line setter. | confirmed (code) |
| `0x00251718` | `PathNode_IsEdgeBlocked` | Whether the edge between list slots i and i+1 is blocked (NavNode_IsLinkBlocked). | confirmed (code) |
| `0x00251768` | `PathNodes_ResetParents` | Sets every path node's parent (+0x1c) to none (0xffff) before a search. | confirmed (code) |
| `0x00251820` | `Path_DistanceCost` | The A* cost of a distance: length x 16, as a u16. | confirmed (code) |
| `0x00251a00` | `PathNodes_ClearOpenMarks` | Clears the +0x18 mark of every node on the open list after a search. | confirmed (code) |
| `0x00252108` | `Nav_NearestEdgePoint` | The nearest point on a nav polygon's edges (within 1.5 m of its box, at or above the point's height less 0.1) to a point; only when closer than 1.5 m, then dropped to the ground. | confirmed (code) |
| `0x00252ae0` | `Nav_FindPolygonUnderThunk` | Nav_FindPolygonUnder with a height of 1.0. | confirmed (code) |
| `0x00252b08` | `Nav_ClampMoveToRange` | Limits a move to a range: beyond it the point is kept only when the nav mesh connects the two (Nav_IsConnected), else the start. | confirmed (code) |
| `0x00252c28` | `NavLink_SetBlocked` | Sets or clears a path polygon's blocked flag 8 (+0x48) at a point. | confirmed (code) |
| `0x00253068` | `PathSearch_ResetCount` | Global 0x005105b0 = 0. | confirmed (code) |
| `0x00253118` | `PathSearch_InitLists` | Static initialiser: the open heap, the closed list and two small lists of the path search. | confirmed (code) |
| `0x00253170` | `PathSearch_StaticInit` | Calls PathSearch_InitLists(1, 0xffff). | confirmed (code) |
| `0x00253190` | `PathHeap_Construct` | Constructs the open heap (count 0). | confirmed (code) |
| `0x002531b8` | `PathHeap_Clear` | Count = 0. | confirmed (code) |
| `0x002531c0` | `PathHeap_Push` | Pushes a node on the binary min-heap keyed by the node's cost (+0x16), at most 1000. | confirmed (code) |
| `0x00253268` | `PathHeap_PopMin` | Removes and returns the cheapest node of the heap. | confirmed (code) |
| `0x00253350` | `PathHeap_Find` | The heap position of a node, or 0xffff. | confirmed (code) |
| `0x00253398` | `PathHeap_IsEmpty` | Count is 0. | confirmed (code) |
| `0x002533b8` | `PathHeap_SiftUp` | Moves a node whose cost dropped up the heap. | confirmed (code) |
| `0x00253480` | `PathList_Construct` | Constructs the closed list. | confirmed (code) |
| `0x002534a8` | `PathList_Get` | Entry i of the closed list. | confirmed (code) |
| `0x002534c0` | `PathList_Clear` | Count (+6000) = 0, last (+0x1772) = -1. | confirmed (code) |
| `0x002534d0` | `PathList_Add` | Appends a node to the closed list, at most 3000. | confirmed (code) |
| `0x00253528` | `PathList_Count` | The closed list's count. | confirmed (code) |
| `0x00253538` | `PathPair_Construct` | Constructs a three-short record (zeroed). | confirmed (code) |
| `0x00253560` | `PathPair_Clear` | Zeroes the three shorts. | confirmed (code) |
| `0x00253598` | `PathCursor_Construct` | Constructs a two-short record (zeroed). | confirmed (code) |
| `0x002535c0` | `PathCursor_Clear` | Zeroes the two shorts. | confirmed (code) |
| `0x00253e68` | `Human_SetMoveStyle` | Replaces the top movement style (pop then push), or applies style 0 when none is stacked. | confirmed (code) |
| `0x00253f78` | `Human_RemoveMoveStyle` | Removes a movement style from the human's stack and re-applies the top one ([Anim slots](#anim-slots)). | confirmed (code) |
| `0x00254060` | `Human_GetMoveStyle` | The movement style byte of the human's record (styles 4 and 6 use the slow turns). | confirmed (code) |
| `0x002541c8` | `Human_CheckMoveStyles` | Drops a held-object movement style (1-6) that no longer matches what is held, style 14 without state 0x100000, and an object held with no style. | confirmed (code) |
| `0x002544b8` | `Human_GetAttackerReach` | The reach (attack table +0x4) of the move the human's attacker is playing on it. | confirmed (code) |
| `0x002545a8` | `AnimRange_SetFarRange` | Writes an anim id's far range (seconds x 1000, mm) into the human's Anim Range List (+0x160). | confirmed (code) |
| `0x002548c8` | `AnimRange_SetDamage` | Writes an anim id's damage (+0x0a) into the human's Anim Range List. | confirmed (code) |
| `0x00254e20` | `AnimRange_GetHitFlags` | The Anim Range List's u16 +0x0e for an anim (-1: the current one); Hit_ResolveBlock tests 0x600. | confirmed (code) |
| `0x00255540` | `Human_UpdateWorkout` | State code 0x1b: the workout on gym equipment (start, repetitions with the pad's pumps, end), its clips and rate. | confirmed (code) |
| `0x00255cf8` | `Human_UpdateSpecialIdle` | State code 0x1d: the special idle (672 start, then 671 looping) from the script's clips; ends the special state. | confirmed (code) |
| `0x002570e8` | `Human_ComputeThrowVelocity` | The launch velocity of a thrown held object: aimed at the target (led by its speed) or along the facing, scaled by the object's weight and the human's scale. | confirmed (code) |
| `0x00257ce8` | `Human_EndThrowAim` | Cancels the throw aim and drops what cannot stay in hand (kinds 4 and 6; any but sets 1 and 2 without human flag 0x2000). | confirmed (code) |
| `0x00257d70` | `Human_EndThrowAimDropHeavy` | Ends the throw aim: drops a held object of kind 4 or 6, then (no human flag 0x2000) drops any other when the human is hurt or holds set 3, unless a cop (brain type 1) holds set 1 or 2 ([Combat: dropping what is held](combat-throws.md#keep-weapon)). | confirmed (code) |
| `0x00257e38` | `Human_EndThrowAimIfHurt` | Ends the throw aim and drops a held object of kind 4 or 6; a hurt human (no human flag 0x2000) also drops any other, unless a cop (brain type 1) holds set 1 or 2 ([Combat: dropping what is held](combat-throws.md#keep-weapon)). | confirmed (code) |
| `0x00257ef0` | `Human_KnockOffHatIfDown` | Class 0x80 down or dead: knocks the hat off ([Hats](characters-appearance.md#hats)). | confirmed (code) |
| `0x00258330` | `Human_KnockOffHat` | Throws the worn hat off (unless HuSetKeepHat or kind 27): spin 8 rad/s, drop at 2 m/s, +0x364 cleared ([Hats](characters-appearance.md#hats)). | confirmed (code) |
| `0x00258968` | `Human_ReleaseAll` | Lets go of everything: breaks a pair, ends a mini-game, drops the held object, clears the object target, ends rage, clears state 0x18800f, empties the move-style stack and sets style 0x12. | confirmed (code) |
| `0x00259368` | `Human_UpdateFaceClip` | Queues the face clip 698 + 3 x the record's +0x1c (and its pair 700 + 3n), fading out the old one over at most 1/30 s; record +0x24 holds the current face clip. | confirmed (code) |
| `0x0025a178` | `Human_BuildCombatIdle` | Anim state 11: the fight-stance idle (slot 11) looping after a fade; from state 2 the fade holds 0x80000. | confirmed (code) |
| `0x0025a310` | `Human_PushRecordOverlay` | Sets the anim state and a held flag and pushes the record's +0x20 clip as a 0.05 s overlay holding that flag. | confirmed (code) |
| `0x0025aeb8` | `Human_TryIdleFidget` | With nothing held and record +0x3bc set, a 30% chance to play the fidget 601 as an overlay holding 0x200000. | confirmed (code) |
| `0x0025af90` | `Human_BuildPairedFourMix` | Builds the four-way gait mix (slot 4, anim state 4, action 3) for the human and for its pair partner (+0xc4), after a fade (0.1 s longer with held flag 0x40000000). | confirmed (code) |
| `0x0025c240` | `Human_StartTurnOnSpot` | An AI's turn on the spot: a turn clip (395-398, slot 8 or 15 variants) and the rest of the angle over the clip ([An AI's locomotion](characters-locomotion.md#ai-locomotion)). | confirmed (code) |
| `0x0025c5b8` | `Human_TryArriveClip` | An AI covering the last metres to its point with one clip: a step or dash from rest, the run stop at a run ([An AI's locomotion](characters-locomotion.md#ai-locomotion)). | confirmed (code) |
| `0x0025cb90` | `Human_PlayOneShotClip` | Plays a clip once over a 0.1 s fade, then the idle (or combat idle) looping, holding the given flag; state code 0. | confirmed (code) |
| `0x0025cd58` | `Human_BuildStanceEightDir` | Anim state 14: the fight-stance eight-direction blend (slot 14) driven by record +0xdc x 4/pi, after a fade of 0.15 s (none from state 2). | confirmed (code) |
| `0x0025cf10` | `Human_LaunchJumpThunk` | Calls Human_LaunchJump. | confirmed (code) |
| `0x0025cf30` | `Human_BuildJumpLoop` | Anim state 25: the jump loop 434 after a 0.1 s fade, then the launch ([Anim states in the air](characters-traversal.md#jump)). | confirmed (code) |
| `0x0025d020` | `Human_BuildFallClip` | Anim state 26: the drop cycle 428, or the long-fall cycle 422 / 425; a long fall from a grab gives the grabber style points and rage. | confirmed (code) |
| `0x0025d2d0` | `Human_ResetToIdleState` | Anim state 0 unless state 0x1c00000000; clears landing 0x2000000000 and the down and stun timers. | confirmed (code) |
| `0x0025d348` | `Human_ResetToMoveState` | Anim state 4 unless state 0x1c00000000; clears landing 0x2000000000. | confirmed (code) |
| `0x0025d390` | `Human_BuildLandingClip` | Anim state 27: the jump end 435 / 436, the drop land 429 or a long-fall clip, then the idle or a gait blend ([Anim states in the air](characters-traversal.md#jump)). | confirmed (code) |
| `0x0025e0e0` | `Human_BuildSlotLoopWithIntro` | Loops the record's +0x88 clip, after the +0x7c clip (holding 0x20000000) unless asked not to. | confirmed (code) |
| `0x0025e250` | `Human_EndDownState` | Clears record +0xbe and gets the human back up: wakes it (0x40000), unarrests it (0x20000), gets it up (0x80000), else back to the fight stance. | confirmed (code) |
| `0x0025e900` | `Human_EndObjectSteer` | End hook of the steer clip: clears the object target and sets push weight back to 1.0. | confirmed (code) |
| `0x0025e948` | `Human_StartObjectSteer` | Plays clip 666 then the idle, holding 0x4000, push weight 1e9, steering onto the object target over 0.1 s. | confirmed (code) |
| `0x0025eee0` | `Human_UpdateGaitBlendTarget` | Sets the top gait blend's target from the speed ([Clip selection](#clip-selection)). | confirmed (code) |
| `0x0025ef58` | `Human_PushScriptOverlay` | Pushes a script clip as a 0.1 s overlay holding 0x100, unless the human is busy. | confirmed (code) |
| `0x0025f028` | `Human_BuildAnimState` | Calls the builder for anim state 0-7 from the table at 0x0055db50. | confirmed (code) |
| `0x0025f1b8` | `Human_BuildIdleClip` | Builds the idle (or a given loop) with stealth and stalking variants ([Clip selection](#clip-selection)). | confirmed (code) |
| `0x0025f450` | `Human_RebuildAnimOnMove` | Forces the anim state to be rebuilt (flag 0x20000000) after state code 18 or while holding 0x20000 (clearing it). | confirmed (code) |
| `0x0025f4d0` | `Human_BuildMoveState` | Anim state 4: the gait blend via Human_BuildMoveTasks, or the stance eight-direction blend ([Clip selection](#clip-selection)). | confirmed (code) |
| `0x0025fba0` | `Human_BuildState25` | Builds the jump loop and sets anim state 0x1a. | confirmed (code) |
| `0x0025fbd0` | `Human_BuildState26` | Builds the fall clip and sets anim state 0x1a. | confirmed (code) |
| `0x0025fc00` | `Human_BuildState27` | Builds the landing clip and sets anim state 0x1b. | confirmed (code) |
| `0x0025fcb0` | `Human_UpdateDealerPair` | The paired clip 668 between two humans: within 1.875 m, a clear way and grabbable, aligns both and plays it on both; otherwise idles both. | confirmed (code) |
| `0x0025ff18` | `Human_StartObjectSteerIdle` | Starts the object steer, then anim state 0 and action 0. | confirmed (code) |
| `0x0025ff50` | `Human_BuildGaitBlendFromValue` | Builds a gait blend from a gait value, rounded down to 0-3 ([Clip selection](#clip-selection)). | confirmed (code) |
| `0x002726d8` | `Human_SetStrafeDirection` | In a fight stance with a brain: record +0xdc = the movement direction relative to the facing (0-2π), for the eight-direction blend. | confirmed (code) |

## Data

### Anim slots {#anim-slots}

The 0x180 record holds the human's **anim slots**: 35 anim ids at `+0x28`-`+0xb3`, copied from the default table at
`0x005105d8` (`0x00253608`) and changed one by one by `0x002535f0(human, slot, id)`. A player gets slot 14 = 380
(player combat walk). `0x00253688(human, style)` swaps in a movement style's ids (cases 1-20, a jump table) and then
recomputes the speeds. Record `+0x20` is the anim id playing (getter `0x002266b8`). Confirmed (code); Rembrandt's ids
were read at runtime and named from the clips on the disc (observed in PCSX2, [How this was checked](#runtime-reads)).

**Slots count from 0**, with no offset or remapping: `0x00253608` copies the table's 0x8c bytes (35 words) to record
`+0x28` as they are, and `0x002535f0` stores slot *s* at `+0x28 + 4s`. The player's slot 14 is the table's fifteenth
word (372 replaced by 380), and the movement styles write by the same numbers: styles 4 and 6 put their own "run stop"
ids (523 `ANIM_BARREL_MOVEMENT_RUN_STOP`, 569 `ANIM_GHETTO_MOVEMENT_RUN_STOP`) in slot 33, where the default is 417
`ANIM_MOVEMENT_RUN_STOP`. Confirmed (code) at `0x00253608`, `0x002535f0` and `0x00253688`. The last slot, 34, holds 0
in the table and no style writes it; whether 0 there means anim id 0 (`ANIM_RUNNING_ATTACK_CHARGE`) or "no clip" is not
traced (the [anim id reference](../references/anim-ids.md) treats it as unset).

| Slot | Id | Clip | Slot | Id | Clip |
| --- | --- | --- | --- | --- | --- |
| 0 | 388 | neutral idle | 18 | 212 | mounting strike |
| 1 | 401 | step forward | 19 | 193 | grounded strike |
| 2 | 403 | dash forward | 20 | 104 | grab front attack |
| 3 | 407 | sneak walk | 21 | 605 | block start |
| 4 | 408 | walk | 22 | 608 | block high front |
| 5 | 409 | jog | 23 | 606 | block sustain |
| 6 | 410 | run | 24 | 607 | block shuffle |
| 7 | 411 | sprint | 25 | 427 | jump start |
| 8 | 390 | slow turn right | 26 | 428 | drop cycle |
| 9 | 412 | sneak walk start | 27 | 429 | drop land |
| 10 | 413 | walk start | 28 | 388 | neutral idle (again) |
| 11 | 358 | combat idle | 29 | 653 | special attack 1 front |
| 12 | 366 | combat shuffle forward | 30 | 434 | jump loop |
| 13 | 368 | combat dash forward | 31 | 668 | special action (`missing_anim_filler` in the generic data) |
| 14 | 372 (player 380) | combat walk forward | 32 | 317 | fire idle |
| 15 | 360 | combat turn | 33 | 417 | run to neutral (run stop) |
| 16 | 12 | attack S1 | 34 | 0 | none, or id 0 (above) |
| 17 | 11 | attack X1 | | | |

Slots 16-24 and 28-33 are named from the ids' `ANIM_*` constants in `royal.lua` and their generic clips (inferred:
what each slot is used for has not been traced, only its default id). The full table, with every id's clips, is the
[anim id reference](../references/anim-ids.md).

Other locomotion ids seen at runtime outside the slots: 414 run start, 418 run 180° turn, 421-426 fall front / back
begin, cycle and land.

## Behaviour

### Clip selection {#clip-selection}

Confirmed (code) at the cited addresses unless a line says otherwise.

Each update the human's animation controller `Human_ChooseAnimState` (`0x00259578`, called from the characters'
update at `0x00255510` and `0x00256c34`) picks an **anim state** and, when it differs from the one in record `+0x18`
(or human flag `0x20000000` forces it), calls that state's builder from the table at `0x005106e0` (one function per
state; most entries are an empty stub at `0x25f4c8`, not yet a function of the Ghidra project). The builders make
[animation tasks](formats/animation.md#animation-tasks) and change animation by "fade over *d*, new task at the bottom"
([Task stack](formats/animation.md#task-stack)). Confirmed (code) at the cited addresses unless marked.

**Choosing the state** (the plain locomotion case; grabs, combat, carried objects and scripted moves test their own
flags first and are not described here), confirmed (code) with the builders read from the table at `0x005106e0`:

| State | When | Builder |
| --- | --- | --- |
| 0, idle | speed below a quarter of the speed `0x00221580` returns | `0x0025f770` |
| 4, move | speed at or above that, or record `+0x14` is 5, 6 or 8 | `0x0025f4d0` |
| 11 | a combat stance (`0x00228340`) at speed 0.01 or less | `0x0025fa48` |
| 14 | a combat stance while moving | `0x0025f4d0` |
| 21, 24 | human flag `0x8000`: 24 when the pending turn `+0x5e4` exceeds `Human_MaxTurn`, else 21 (turns in place, inferred) | `0x0025fc30`, `0x0025fc70` |

**Move (state 4).** `0x0025f4d0` calls `Human_BuildMoveTasks` (`0x0025b200`) with a fade time of 1/15 s (1/6 s when
record `+0x14` is 18 or human flag `0x40000000` is set), which builds one of:

- **From standing** (the top task is not a gait blend, task flag `0x80` clear): a [gait
  blend](formats/animation.md#gait-blend) of slots 4, 5, 6, 7, 7 (walk, jog, run, sprint, sprint) at value 0, flags
  `0x2c1`; then a clip-then-next task (type 3) that plays **slot 10, the walk start** (413), with a blend time of 0, and
  hands over to the gait blend. The fade before it has a duration of **0**: the start clip replaces the idle at once.
  Human flag `0x10000000` is set while the start clip plays (it is what stops the locomotion setting a velocity, [Root
  motion](formats/animation.md#root-motion)).
- **Run start** (`0x0025b9f8`, confirmed (code) in `Human_BuildMoveTasks`: outside a shadow, when the stick asks for a
  run and record `+0x14` is 6, 7 or 8, or for a jog with state flag `0x1000000`, `Human_WantsJog` being what tests a
  carried object of class 4 or 6): the same, but the gait blend starts at 2 (run) and the start clip is **slot 10's id +
  1** (414, run start).
- **Already moving** (the top task is a gait blend): a fade of 0.1333 s and a new gait blend that starts at the old
  one's value (`0x0025ff50` rounds it down to 0, 1, 2 or 3; confirmed (code)) and the old one's normalised time, so the
  cycle carries on.
- Other cases: a looping single clip of slot 14 (combat walk) when the global `0x0051031c` is set (for a player, in
  `0x0025f4d0`; confirmed (code)); a four-clip task of slot 0 and slot 4 under `0x00227d98`; a two-clip mix of 633
  `STEALTH_WALK` and 636 `STEALTH_READY_WALK` while stalking a target (`Human_IsStalkingTarget`, `0x00228188`, [Combat
  moves](combat-moves.md#stealth)).

**Walk start to run start.** While the walk start plays (flag `0x10000000`), if the stick asks for a run or sprint
(`Human_WantsRun`, `0x00225c10`; `Human_WantsSprint`, `0x00225dc0`) above the magnitude at `0x005102e8` (0.95), and less
than half of the clip has played, the controller swaps it for the run start: a fade of min(0.1333 s, the clip's
duration), a gait blend at 2, and the run start begun at the walk start's normalised time (`0x00259578`, confirmed
(code)).

**Gait value from the speed.** In states 4 and 14, every update, `0x0025eee0` sets the top gait blend's target to
`Gait_BlendForSpeed` (`0x0025ec28`), a piecewise-linear map through the [speed
getters](characters-classes.md#speed-classes):

```text
speed > run          → 2 + (speed − run) / (sprint − run)
jog < speed ≤ run    → 1 + (speed − jog) / (run − jog)
walk < speed ≤ jog   → (speed − walk) / (jog − walk)
otherwise            → 0                                  (clamped to 0-3)
```

So the walk clip plays alone up to the walk speed, the blend reaches the run clip at the run speed and the sprint at
the sprint speed; the fifth slot (sprint again) is never more than a neighbour. The blend's value then eases toward
the target at 10 units per second. When the top task is the eight-direction blend (type 13), the target is instead
record `+0xdc` × 4/π (a direction, inferred); for the four-clip task, speed / walk speed clamped to 0-1.

**Idle (state 0).** `Human_BuildIdleTasks` (`0x0025f770`) calls `0x0025f1b8` with **slot 0** (388) as a looping
clip with no task flags, after a fade of **0.15 s**; if a start clip is still playing (flag `0x10000000`) the fade is
1/15 s when less than 0.1333 s of it has played and 0.2 s otherwise. Some ids replace slot 0 in special cases (355,
357, 394, 634). With the state code 9 (the [skid](characters-locomotion.md#locomotion)) it builds the **run stop**
instead (`0x0025c0e8`, slot 33, 417); otherwise releasing the stick fades straight to the idle, which matches the
runtime samples below. This corrects the earlier reading that slot 33 is not used.

**At runtime** (PCSX2 2.9.94, `level99` checkpoint 1, Rembrandt; the task stack read from the instance every 50 ms).
Observed in PCSX2 ([How this was checked](#runtime-reads)):

- Standing: one looping task, idle 388, rate 0.75, no flags.
- Stick 0.5 straight up: a fade of 0 s and a type-3 task playing the walk start (413, 0.333 s) at rate 0.75 with human
  flag `0x10000000`; at its end a gait blend (flags `0x2c1`, rate 1.0, speed 10) with target and value 0, the walk
  clip leading and the jog clip muted.
- Stick 1.0: the run start (414) the same way, then the gait blend at 2. The first update after the start clip ran
  at 6.49 m/s, so the target was 1.553 and the value fell to 1.825 (jog and run mixed) before going back to 2.0 at
  7.80 m/s: the target follows the speed every update.

### Animation callbacks (`AddAnimCallback`) {#anim-callbacks}

Scripts register Lua functions to run when a human starts an animation ([`AddAnimCallback`,
`AddAllAnimCallback`, `DelAnimCallback`](../references/bindings/character.md#addanimcallback)). Confirmed (code) at the
addresses given:

- **The table** (`0x006b6710`): 16 slots of 16 bytes, {human handle, anim id, all-humans flag, interned Lua name}.
  `AddAnimCallback` (`0x0023a9f8`) takes the first slot whose handle no longer resolves; `AddAllAnimCallback`
  (`0x0023aab0`) the first whose handle does not resolve and whose flag is 0, and sets the flag. A full table returns
  nil. `AddAnimCallback` does not test the flag, so it can take (overwrite) an all-humans slot, whose handle never
  resolves. `InitLevel` (`0x0015fe90`) empties every slot (`0x0023a9b8`); an entry of a deleted human becomes free by
  itself.
- **When it fires**: `CharacterInstance_GetAnim(id)` (`0x00175080`), called with its third argument 0, runs the dispatch
  (`AnimCallback_Dispatch`, `0x0023ac50`) for the instance's human (instance `+0x2b8`) when the id resolves to a clip.
  The animation-task code calls it that way each time it takes a clip for an id: every task constructor (a clip
  started, `0x00105678`, `0x00105990`, `0x00106140`, ..., the gait blend `0x0010a310`) and the blend tasks' advance
  when they move to another clip (`0x0010a5b8`, `0x00109588`). So the callback fires **when the human starts playing
  that anim id**, not at the clip's end, and again every time it is started (inferred for the advance cases: they
  re-resolve when the blend crosses to a new clip). Lookups for other uses (`0x00221bf0`, third argument 1) do not
  fire it.
- **The match**: slots in order; a slot matches when its anim id equals the id and either its all-humans flag is set
  or its handle resolves to this human. **Only the first match fires**; the dispatch then returns.
- **The call**: synchronous, inside the animation code: the name is looked up (script slot `+0x4c`,
  [Scripting](scripting.md)), then called with **two arguments, the human's handle and the anim id** (slots `+0x5c` and
  `+0x6c`, call `+0x8c` with 2).
- **Not one-shot**: the slot stays until `DelAnimCallback(human, anim)` (`0x0023ab80`; the first slot whose id matches
  and whose flag is set or whose handle is the human's) or the next `InitLevel`.
- A paired task (`0x00108450`, `0x00108a78`) looks the id up in **the other human's** instance, so the dispatch runs
  for that human. Inferred from an unrecorded PCSX2 read ([How this was checked](#runtime-reads)): at a strong grapple's
  connect the victim's 658, started by `PairedTask_Init` (`0x00108a78`, caller `0x00108af4`), was dispatched for the
  **player** (the grabber, whose set holds 658).
- **A follow-on task fires when it is built, not when it starts playing.** At a grab's connect, `Grab_Connect`
  ([Combat](combat-grabs.md#grab-posing)) builds the connecting clip and, under it, the hold's four-clip mix (type 11,
  `AnimTaskFourMix_Construct`, `0x00106f50`) on both stacks in the same update; the four-mix's constructor takes its
  clips (82 and 86 for the grabber, 83 and 87 for the victim) and so fires the callbacks of the **hold ids at the
  connect**, the length of the connecting clip before the hold plays. Inferred from an unrecorded PCSX2 read, 2.9.94,
  `level99` checkpoint 1 (hooks `anim-dispatch` and `anim-callback-match`, [the queue at
  runtime](hud-messages.md#hint-queue-runtime)): for each grab the dispatch of 82 for the player (from `0x00106fe0`) and
  of 83 for the victim came in the tick the connecting clip started (657 from `AnimTaskChained_Construct`,
  `0x00105990`); in the strong grapple the hold played and the tutorial callback received 82 **34 updates later**, and
  no second dispatch of 82 came when the hold began. So level99's `PlayerState` (registered on the player's 82 and 84)
  runs at the connect, and lesson 5's `P1.Power` (the hit scored with 82, [Strong
  grapple](combat-grabs.md#strong-grapple)) 34 updates after it, never in the same update. A plain grab fires 82 at its
  connect the same way (lessons 2, 6 and 8).
- **Coney's implementation** (`ScriptedBrains::runAnimCallbacks`): the starts are gathered over the characters' step
  and the callbacks called after it. A human that starts the same id more than once in one step gets **one** call
  (**Coney's reading**). Coney hands a chain over to its loop in every layer, the fading ones too, so two attack chains
  ending in the fight idle 358 started it twice in one update. `level99`'s `PlayerState` then ran twice: each time
  `HUDSetTutorialText(nil)` and a hint, and the second nil found nothing showing yet, so one hint stayed queued per
  double start. The box backed up behind them, and the Targeting and Light Combos hints never showed
  ([Hints](hud-messages.md#hints-hudsettutorialtext)). **A grab's hold ids** (82 / 83, 84 / 85 from the rear) fire at
  the connect, as above: `Fighter::connect` reports the start of the hold its chain and the victim's end in
  (`AnimTaskStack::announceFollowOn`), and the hold does not report again when it begins, so `PlayerState`'s grab hint
  and `P1.Power`'s `TT_24d` come the connecting clip's length apart. **Coney's reading**: the four-clip mix's second
  clips (86 / 87) are not reported, since Coney's hold plays the one clip; the paired reaction (658 for the strong
  grapple) is still dispatched for the victim, not for the grabber as above.
- **Coney's order in a step** (inferred from the original's frame, [A frame of play](level-loading.md#a-frame-of-play)):
  the animation callbacks run before the [tutorial callback](hud-messages.md#tutorial-callback), since the original
  calls that one when the victim applies the damage, with the id the attacker has already started; then the hint box
  steps, once a frame, where the original's `HUD_Update` stands; then the scheduled calls. A text set on a free box
  shows within the call that set it ([The queue at runtime](hud-messages.md#hint-queue-runtime)), so texts set by a
  callback and a scheduled call in one step never back up (before, from `level99`'s lesson 6 on, the box showed each
  grab hint `TT_19*` one text late, in place of the lesson's: user checklist item 33b).

## In Coney {#coneys-implementation}

- `src/characters/character_data.*` resolves the 722 anim ids to clips ([Animation](formats/animation.md)): the
  distinct slot values, smallest first, each take the next clip popped off the object stack, which gives Rembrandt's
  408 walk and 413 walk-start as above.
- `src/characters/anim_set.*` answers an anim id from the character's own data, then from the generic character data
  `0x9da2e531` (594 `gen_*` clips) in place of the default table, with the clip's rate from the Anim Range List flags
  ([CfgAnimSpeeds](formats/animation.md)) and its speed (root displacement × rate / duration).
- `src/scripting/anim_callbacks.*` is the [animation callbacks](#anim-callbacks)' 16-slot table and its three
  bindings; a human's task stack reports each anim id it starts (`AnimTaskStack::setStartHook`: a change, a clip
  handing over to its next, a gait blend moving to another clip), and the scripts' brains run the first matching
  callback with (human, anim id). **Coney choice**: the callbacks run after the characters' step, in order, not inside
  the animation code.
- `src/human/human_animator.*` is the anim state and its builders: idle, move, the jump loop (25), the drop cycle
  (26), the landing (27: 436 handing over to a gait blend, or 435 and the idle), the run stop and the climb's three
  clips (Coney's states 101 and 102); the walk or run start played at once and handed to the five-clip gait blend,
  the 0.1333 s fade between moves, the idle's fade by how much of a start clip has played, and the walk start swapped
  for the run start in its first half ([Clip selection](#clip-selection)). Its clips hold their record `+0x08` bits
  while they play ([Tasks](tasks.md#held-flags)): a start clip `0x10000000`, the landing `0x1000000`, the run stop and
  the climb's first and last clips `0x80000`, the combat moves theirs ([Combat](combat/index.md#coneys-implementation)).
- **Dynamic animation slots** ([Animation](animation.md#dynamic-slots)): `human::ScriptState` holds the seven
  slots that `HuUseAnim` and `HuUseAnyAnim` both fill. An id already in a slot reuses that slot, a new id takes the
  first free one, and an eighth id is refused. The play mode hands the idle's clip (`0x184`) to `Human::setIdleClip()`
  and every other one to `HumanAnimator::setOverride()`, which all of the human's own clip lookups go through (a paired
  clip taken from another human's set does not). **Stand-ins**: a clip already playing is not restarted when its slot
  changes (the original's state flag `0x20000000`), and a dynamic clip plays at the rate of the id it replaces.
- **The default anim table**: slots set to the default (`0xffffffff`) are answered by the generic character data
  `0x9da2e531`, whose clip speeds match the runtime-confirmed ones exactly (380, 407, 409, 410, 411); the table at
  resource manager `+0x70` is not decoded.

## Open questions

- **Clip selection** (answered): [Clip selection](#clip-selection), with the task system on
  [Animation](formats/animation.md#animation-tasks); the playback rate is never scaled with the speed. Still open:
  the anim states other than idle and move (11, 21, 24 and the combat ones) and the special idle ids.
- **What slots 16-24 and 28-34 are used for** (16-24 are the attacks and the block, [Combat](combat/index.md#attacks))
  (their default ids are known, [Anim slots](#anim-slots)), and the movement styles of `0x00253688` beyond the ids they
  write.
- **The default anim table** at resource manager `+0x70`, which answers the slots set to `0xffffffff`; Coney uses the
  generic data `0x9da2e531`, which matches every speed checked, but whether the table is that resource is open.

## How this was checked

Code claims were read in Ghidra in `SLUS_212.15`.

### The old runtime reads {#runtime-reads}

The task-stack reads of the clip selection (standing, a stick of 0.5 and of 1.0) and the grab's callback dispatches were
made in PCSX2 2.9.94 (2026-10-04) over PINE in `level99`, checkpoint 1, reading the instance's task stack every 50 ms
and, for the callbacks, with the `anim-dispatch` and `anim-callback-match` hooks. Rembrandt's ids were read the same way
and named from the clips on the disc. They predate run records.
