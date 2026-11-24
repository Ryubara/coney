---
kind: topic
system: characters
owns:
  functions: [
    "0x00146078", "0x002212d0", "0x002213d8", "0x00221760", "0x0022ae40", "0x00234188", "0x00240e38", "0x002427e8",
    "0x00248170", "0x00248270", "0x00248df0",
  ]
---
# Characters: locomotion

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records or, for the older ones, How this was checked.

## Summary

How pad input becomes movement. Each update the pad's stick, already turned into camera space, gives a
direction and a magnitude; the magnitude chooses walk (from 0.12) or run (above 0.95), the speed ramps towards
it at 24 m/s², and the heading turns towards the stick at a limited rate. An AI human's locomotion, the step
control that stops a move at a wall and the wheelchair's control are here too. The speed classes that give the
speeds are on [Classes](characters-classes.md#speed-classes); the walls, the ground and falls on
[The ground](characters-ground.md); sprint and the jumps on [Traversal](characters-traversal.md).

## Map {#original-structure}

The functions of the locomotion:

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00221760` | `Human_GaitForSpeed` | a speed to gait 0, 2, 3, 4 or 5 | confirmed (code) |
| `0x002213d8` | `Human_MaxTurn` | the turn limit per update | confirmed (code) |
| `0x00146078` | `PlayerRecord_Update` | pad → stick angle and magnitude | confirmed (code) |
| `0x00240e38` | `Human_PlayerLocomotion` | stick → velocity and heading | confirmed (code) |
| `0x00248df0` | `Human_Lean` | a lean from the turn rate | confirmed (code) |

## Data

### Movement constants {#movement-constants}

Confirmed (code) at the readers; values from `.data`.

| Address | Value | Use |
| --- | --- | --- |
| `0x005102cc` | 1/30 s | the characters' step (`Humans_Update` runs at 30 Hz) |
| `0x005102e8` | 0.95 | stick magnitude above which a run is allowed |
| (code) | 0.12 | stick dead zone |
| (code) | 24 | speed change per second of step (m/s²), so 0.8 m/s per update |
| `0x005101b0`-`0x005101dc` | pairs (player, other): 11° / 1.5°, 16° / 2.5°, 18° / 4°, 18° / 6°, 20° / 12°, 24° / 24° in play | turn limits per update; `.data` holds 1.5°-24° in both words, and `config_preload2.lua`'s `CfgSetTurnRates(11, 16, 18, 18, 20, 24)` sets the player's |
| `0x00510308` / `0x00510304` | 1 / 0 | turn smoothing on (eased) |
| `0x0051030c` | 0.8 | turn step carried over from the last update |
| `0x00510310` | **2.0 rad** in play (`CfgTurnRate(true, false, 2.0, 0.8)`) | heading error at which the turn reaches its full rate |

The values "in play" were read in the street save's RAM and match every measured turn (observed in PCSX2,
[Feel comparison](feel.md#details-behind-the-table)); the setters are confirmed (code) on
[CfgSetTurnRates](../references/bindings/config.md#cfgsetturnrates).

## Behaviour

### From pad to intent {#input}

Confirmed (code) at the cited addresses unless a line says otherwise.

`PlayerRecord_Update` (`0x00146078`), each update, for a record with a pad and control on: flip the buffer; copy the
buttons; take the pad record's **camera-turned** left stick (`+0x00`, `+0x04`, [Front end](frontend.md#pad-record)),
store its angle (`atan2`, `0x003357a8`) and its length clamped to 1. With `+0x1f` set the angle is π/2 and the length
0. Confirmed (code). The stick is thus already relative to the camera: pushing up moves away from it, which is what
the game does at runtime (observed in PCSX2, [How this was checked](#runtime-reads)).

### Buttons {#buttons}

The pad's buttons become **command ids** through tables of 12-byte entries `{u16 mask, u32 command, u16 buttons,
u16 extra}` that `AddCommand` fills. Each update `0x00147940` matches them and stores the command in the per-player
record's `+0x20`, or in the pending `+0x24` when the entry's mask is `0xfe` (`0xff` enables it for pad 0). The buttons
are the pad word's bits ([Pad record](frontend.md#pad-record)). The nine tables, what each trigger means, the
order in which they are matched and every command they make are on [Combat, Commands](combat/index.md#commands)
(confirmed (code) and (runtime)). An earlier reading here took square (`0x80`) for d-pad left and cross (`0x40`) for
down: the combinations are L2 + square (`0x21`), L2 + cross (`0x20`), cross + square (`0x22`) and circle + triangle
(`0x24`), and the d-pad combinations `0x29`-`0x2c` and L3's 9 need select held as well.

Traversal uses two buttons: **triangle pressed** (command 10) starts a climb, a context action or a jump
([Jumping](characters-traversal.md#jump), [Climbing](characters-traversal.md#climb)), and **L2** sprints. The sprint
reads the button itself (`0x00147f98(record, 1)`, "is the button of mask 1 held") rather than command 5. Confirmed
(code) at `0x0027c120` and `0x0027ce90`.

### Locomotion {#locomotion}

Confirmed (code) at the cited addresses unless a line says otherwise.

`Human_PlayerLocomotion` (`0x00240e38`) for a pad-controlled human; an AI human moves by its brain instead
([An AI's locomotion](#ai-locomotion)).
Confirmed (code) for the steps; the state predicates are named by what they test where known.

1. **Current speed** = the length of the velocity (vtable slot `+0x94`).
2. **Target speed.** With the stick above the dead zone (0.12): walk by default; **run** when the magnitude is above
   0.95 (`0x00225c10`); **jog** instead of run when the human carries an object of class 4 or 6 (`0x00225a50`,
   `0x00224000`); **sprint** when the state flag `0x1000000` is set and the human has stamina (`0x00225dc0`, record
   `+0x14a` ≠ 0). Three state tests override this: one (`0x00223ad0`) keeps the target at 0, one (`0x00227d98`) uses the
   record's walk speed `+0x170` unscaled, and two others (`0x00228340`, or the global `0x0051031c`) the base speed. With
   `0x00510258` set, run and sprint come from an analog button's pressure instead (more than 100 sprints); it is 0
   in play. Below the dead zone the target is 0.
3. **Skid** (the test at `0x00241300`). When the stored gait `+0x1a8` is 4 or 5, the current speed is **at least the
   run speed** (`0x00221670`: human `+0x3a4` × record `+0x178`, compared as floats, `c.le.S`) and the previous
   update's stick (the per-player record's other buffer) was over 0.95 (`0x005102e8`), then a stick now under 0.2,
   or one pointing more than 120° away from the velocity (dot product below -0.5; the dot is worked out only for
   some camera modes, `0x0011f9b0`), zeroes the velocity and sets the state code to **9** (the call at
   `0x00241530`), unless the human is airborne (`0x00227f90`) or `0x00227f68` holds. Code 9 is what plays the
   **run stop**: the idle builder (`Human_BuildIdleTasks`, `0x0025f770`) builds the run stop (`0x0025c0e8`: slot
   33, 417, holding `0x80000`, the idle after it, a 0.1 s fade) instead of the idle when the code is 9, and the anim
   state choice (`0x00259578`) picks state 0 for code 9. Confirmed (code); the code 9 and its caller observed in PCSX2.
4. **Turn.** Target heading = stick angle − π/2; current heading = the facing from the rotation. The error is
   wrapped to (−π, π]. The limit per update comes from `Human_MaxTurn` (`0x002213d8`) through the gait lookup
   `0x002212d0`, which reads the table's player word for a pad-controlled human: in play **20° walking or standing,
   18° jogging and running, 16° sprinting** (600, 540 and 480 degrees per second); 4° and 6° in two special states
   (`0x00227f68`, `0x00227f40`), 11° in two others, 24° in a combat stance ([Movement
   constants](#movement-constants)). With smoothing on, the step is the limit times an ease
   `(1 − cos(π · error / E)) / 2` with **E = 2.0 rad** in play (full from 2.0 rad), plus 0.8 times the previous step
   (−0.5 times it when the error changed sign), clamped to [0, limit]. When the error is smaller than the step, the
   heading snaps to the target. The new heading becomes the rotation (unless `+0x3bb` locks it) and the velocity's
   direction. Confirmed (code) and observed in PCSX2: a 90° turn at a run went 16.0°, then 18° per update (the
   facing reached in 6 updates); at a walk 17.9°, then 20°; in a sprint 14.3°, then 16°; 45° at a run 6.3°, 9.8°,
   10.8°, 10.0°, 8.4°, 4.5° ([Feel comparison](feel.md)). The `.data` values (12°, 6°, 4°, 2.5°, 1.5 rad), which an
   earlier reading of this page gave, are what the scripts replace.
5. **Accelerate.** Speeding up adds 0.8 m/s per update (24 m/s²) until the target; slowing down either drops to the
   target at once or, in some states (`0x00223db8`), at the same rate.
6. **Velocity** = facing × speed, with `z` = `+0x3a0` (the vertical velocity is kept); some states zero it.
7. **Lean** (`0x00248df0`): the heading change times a factor (0.4 × `+0x1ac`, or 8 × `+0x1ac` when walking) is
   clamped to 2°, 3°, 5° or 7° by gait and stored at `+0x298`; the lean at `+0x29c` follows it by 0.625 of the
   difference per update, at most 1°, 1.2°, 1.3° or 1.8° per update. What `+0x1ac` holds is not traced.

**Idle, walk, run.** `Human_GaitForSpeed` (`0x00221760`) maps a speed to a gait: 5 (sprint) at or above the sprint
speed, else 4 (run) at or above the run speed, 3 (jog), 2 (walk), else 0 (idle). Confirmed (code). The gait does
not pick a clip: one gait blend covers walk to sprint ([Clip selection](characters-animation.md#clip-selection)), and
what happens at runtime is below.

**Gaits at runtime** (PCSX2 2.9.94, `level99` checkpoint 1, Rembrandt standing; the left stick held straight up at
the magnitude given, by the stick-table method in [Driving PCSX2](../guides/research-workflow.md#driving-pcsx2);
speed is `+0x1ac` sampled every 5 ms, the clip is record `+0x20`). Observed in PCSX2 ([How this was
checked](#runtime-reads)):

| Stick magnitude | What happens |
| --- | --- |
| 0.10 | nothing (inside the 0.12 dead zone) |
| 0.13, 0.5, 0.94 | **walk start** (413) at a steady 0.76 m/s for about 0.45 s, then **walk** (408) at 1.63 m/s, gait 2: the walk speed does not depend on how far the stick is pushed |
| 0.96, 1.0 | **run start** (414) for about 0.36 s, the speed following the clip (2.77, 2.49, 2.56, 2.63, 2.84, 3.35, 3.90, 4.54, 5.11, 5.40, 5.61, 5.69 m/s, one value per update), then **run** (410), gaining 0.8 m/s per update up to 7.80 |
| released from a walk | speed 0 at once and **idle** (388) |
| released from a sprint (10.245 m/s) | **run stop** (417) for 24 updates (0.8 s), moved by the clip 1.63 m with the facing held, then the idle ([Sprint](characters-traversal.md#sprint)) |
| released from a run (7.80 m/s) | the run stop only when the speed that update is not below the run speed by a rounding error, about one release in ten; otherwise the idle (388) at once, at 0 m/s ([The run stop](#run-stop)) |
| reversed (180°) at a run | one turn step (18°), then the same skid and run stop, sliding 1.63 m the old way; then the walk or run start toward the stick |

So the start clips drive the speed while they play (their root motion, [Animation](formats/animation.md#root-motion)),
and the gait clip's speed is the target afterwards. The walk start's "about 0.45 s" is its 0.333 s played at rate 0.75
(0.444 s). The run start's shorter 0.36 s is not explained; it may begin part-way through, as the
walk-start-to-run-start swap does (speculative, [q-run-start-length](#q-run-start-length)). A jog was not reached from
the stick alone (it needs a carried object, see the locomotion steps above).

**Per update** (the feel pass, slot 1, read every update; observed in PCSX2): on the update the stick is first
seen the start clip begins and the body does not move; the walk start then moves it at a steady **0.762 m/s** and the
run start at 2.77, 2.49, 2.56, 2.63, 2.84, 3.35, 3.90, 4.54, 5.11, 5.40, 5.61, 5.69 m/s; both last **13 updates**,
and the gait blend then gains 0.8 m/s per update. The walk start's clip speed from the disc is 0.786 m/s: the body
moves at **the clip's speed × the body scale** (0.97), as does the landing clip 436 (4.230 m/s against the clip's
4.361) and, inferred, every clip that moves the body. The gait speeds (walk, run, sprint) are not scaled.

The first updates in detail (the second feel pass, slot 1; speed `+0x1ac` and the position's change
agree to 0.003 m/s; the stored gait `+0x1a8` in brackets). Observed in PCSX2 ([How this was checked](#runtime-reads)):

| Stick, straight up | Update the stick is seen | Then, one value per update |
| --- | --- | --- |
| 35 % or 60 % | walk start 413 begins, 0 m/s (gait 0) | 0.762 (1) for 12 updates, then the walk 408 at 1.562 (1), 1.629 (2) |
| 100 % | run start 414 begins, 0 m/s (0) | 2.773, 2.486, 2.558, 2.629, 2.844 (2); 3.346, 3.895, 4.540, 5.112, 5.399, 5.614, 5.686 (3); then the run 410 at 6.486, 7.286, 7.801 (4) |
| released after a run | run stop 417 begins, 0 m/s (0) | 2.244 (2), 4.996, 5.101, 5.229, 5.382, 4.193 (3), 3.190, 2.490, 2.163, 2.124, 2.056, 1.928, 1.740 (2), 1.434, 1.143, 0.886, 0.753, 0.645, 0.552 (1), 0.469, 0.390, 0.296, 0.198 (0); the idle 388 on the 25th |

The 35 % and 60 % runs were identical update for update. There is no ramp on the first moving update of any of
the three clips: it is the clip's own root speed (times 0.97). A walk released stops at once (the idle on the next
update, 0 m/s). The gaits matter to the camera, whose auto-follow runs only at gait 2, 4 or 5
([Camera](camera.md#heading)).

#### The run stop at a run {#run-stop}

Whether a run released plays the run stop is decided by the skid's speed test ([Locomotion](#locomotion) step 3),
and at a steady run that test compares two floats that are equal to within a few units in the last place.
The scenario that showed it, with its figures, is under [How this was checked](#runtime-run-stop).

### An AI's locomotion {#ai-locomotion}

Confirmed (code) at the cited addresses unless a line says otherwise.

An AI human has no stick: its goals and actions write the brain's heading `+0x110`, speed `+0x114` (from a gait,
`Brain_SetMoveGait` `0x0028aac0`), aim point `+0x90` and radius `+0x118` ([AI, Moving](ai-moving.md#moving)), and its
control function, `Human_UpdateControl` (`0x00243848`, the default `+0x1bc`), turns them into a velocity and a
rotation each update. For a pad-controlled human the same function only picks the player's control (locomotion, air,
fight stance, button tap or wheelchair) and runs it. Confirmed (code) at `0x00243848` unless marked.

**The gaits an AI asks for.** `Human_SpeedForGait` (`0x0022ae40`, jump table `0x0055bec0`) returns the human's own
clip speeds from its 0x180 record, **unscaled** by `+0x3a4`: gait 1 the sneak walk `+0x16c`, 2 the walk `+0x170`,
3 the jog `+0x174`, 4 the run `+0x178`, 5 the sprint `+0x17c` ([Speed classes](characters-classes.md#speed-classes);
Rembrandt 1.585, 1.629, 4.857, 7.801, 10.245 m/s), 0 and anything else 0. An AI's sprint drains stamina as a player's
does ([Sprint](characters-traversal.md#sprint), `Human_UpdateMeters` runs for every human), but nothing in this control
reads stamina, so an AI keeps sprinting at 0 (inferred: no reader found).

**The speed this update.** Start from the brain's speed, then, in order:

1. **Stopped** (zero velocity, the rotation kept, the lean cleared) while the human is busy (`Human_IsBusy`,
   `0x00223cb0`, confirmed (code)), its `+0x280` is not −1, or it is blocking or ducking (`0x00223ad0`).
2. **Caps by state**: hurt (state `0x10000`, `Human_IsWounded`, `0x00227d98`, confirmed (code)) → the record's walk
   speed `+0x170` when asked for more than 0.1; in a fight stance (state `0x3`) → the base speed (`+0x164` × `+0x3a4`,
   the combat walk) when asked for more than 0.1, else 0; carrying an object of kind 4 or 6 and asked for more than the
   run speed → it **drops** the object (`0x00257f38`) unless its type's `+0x87` is 6 and `+0x5a` is 0; otherwise at most
   25 m/s. With brain `+0x0b` (the turn boost, [AI](ai-gangs.md#brain-boosts)) above 0, a human (`+0x1b0` = −1) in a
   fight stance gets anim slot 14 = 380 (the player's combat walk) and its speeds recomputed; at 0 or below, slot 14
   goes back to 372.
3. **Speeding up from rest** (current speed exactly 0): unless the state code is 3 or 7 or the anim state is 4, the
   state code becomes **6** (run start) when asked for at least the run speed (**8** instead when brain `+0x0c` is
   above 0), or **5** when asked for exactly the walk speed; the [clip
   selection](characters-animation.md#clip-selection) plays the run or walk start from those. The speed this update is
   min(asked, **2.0**).
4. **Speeding up while moving**: + 8 m/s² × 1/30 (0.267 m/s per update); when that still leaves more than 2 m/s to
   go, + 32 m/s² (1.067 per update) instead; never past the asked speed. So an AI ramps 0.267 per update for the last
   2 m/s and faster before, where a player gains 0.8 ([Locomotion](#locomotion) step 5).
5. **Slowing down**: − 32 m/s² (1.067 per update), not below the asked speed. Except for **Diego and Vargas**
   (classes `0x78`, `0x77`) running or sprinting and asked for 0 with no held flag `0x110c0880`: velocity 0 and state
   code **9**, the [run stop](#run-stop).

**The direction.** With human `+0x333` (the detail level, 0-4, from the distance to the nearest camera,
`Human_UpdateLod` `0x0023d660`) below 2, or any held flag `0x310c0880`, the AI moves toward the brain's point `+0x90`;
at detail 2 or more it simply moves along the heading `+0x110` at the speed and the rotation is set to that heading
at once: far from the camera there is no turn limit and none of the clips below. Close up (confirmed (code)):

- **Turning** happens only while the brain asks for a speed above 0 (or the human is hurt): the facing turns toward
  the heading `+0x110` by at most `Human_MaxTurn` (`0x002213d8`) per update, **at a constant step, no easing** (the
  player's turn eases, [Locomotion](#locomotion) step 4). For an AI the limit is the table's second word
  ([Movement constants](#movement-constants)): **12° walking or standing, 6° jogging, 4° running, 2.5° sprinting,
  24° in a fight stance, 1.5° grabbing, blocking or ducking**, a quarter of 1.5° hurt and of 2.5° with a held flag
  `0x1000080`, and 4° / 6° in the two special states (`0x00227f68`, `0x00227f40`); the gait words are multiplied
  by brain `+0x0b` + 1 (divided by 1 − `+0x0b` when it is negative, `0x002212d0`). The turn decides the facing; the
  velocity is the new facing × the speed, so an AI runs wide arcs (a run turns 120° per second).
    - **Which word.** Each pair is (player, AI); `Human_MaxTurn` takes the AI word, the pair's second (`+4`), for a
      human with no player slot (`+0x1b0` = −1) or whose brain is dead (`+0x09`). The stance's AI word is
      **`0x005101dc`** (24° in `.data`); nothing in the executable stores to it. `CfgSetTurnRates` (`0x0023a7a0`,
      script `0x0035e140`) writes only the six **player** words `0x005101b0`, `0x005101b8`, ... `0x005101d8`
      (degrees in [0, 90), × π/180), so its sixth argument (24 in `config_preload2.lua`) sets the player's stance
      turn, `0x005101d8`, and never an AI's. The two are equal in play only because both are 24. Confirmed (code) at
      `0x00221520` and `0x0023a964`.
    - **Wounded first.** The hurt test (state `0x10000`) comes before the stance test, so a hurt AI in a fight
      stance turns a quarter of `0x005101b4` (1.5°): **0.375° per update**, not 24°. While hurt it also turns with
      the brain's speed at 0, and never starts a turn on the spot. Confirmed (code) at `0x002213d8`, `0x00243848`.
- **In a fight stance** the velocity is instead along the line to the point (it **strafes**: the facing follows
  `+0x110`, the feet go to `+0x90`), and record `+0xdc` gets the direction of the movement relative to the facing,
  0 to 2π (`0x002726d8`), which drives the eight-direction blend ([Clip
  selection](characters-animation.md#clip-selection)). In detail (confirmed (code) at `0x00243848`): the speed is the
  stance walk when the brain's `+0x114` is above 0.1 and 0 otherwise (step 2 above), the direction is the unit vector
  from the human's position to `+0x90` (speed 0 when the point is on him), and the turn toward `+0x110` runs whenever
  `+0x114` is above 0, so an ask between 0 and 0.1 turns him without moving him. Wounded comes first: a hurt human in a
  stance gets the walk speed, not the stance walk.

**The move request is rebuilt every update** (confirmed (code)). `Brain_UpdateGoals` (`0x0028fbb0`, every brain,
every character step) begins with `Brain_StopMove` (`0x0028ac18`): `+0x114` = 0 and `+0x110` = the human's current
facing (from his rotation). It leaves `+0x90` and `+0x118` alone. The brains run before this control in the same
step ([Tasks](tasks.md#humans-update), steps 5 and 8), so the control sees only what a goal or action wrote in this
update; nothing a finished goal or action wrote survives into the next update. With nothing written, the AI asks for
speed 0 along its own facing: no velocity, no turn, and no turn on the spot (the heading error is 0). A stale `+0x90`
does nothing then, because the speed is 0. The only writers of `+0x110` are the reset, the movement actions (move
`0x002fc5c0`, move-to-human `0x002fd4e8` and its abort `0x002fd0a8`, `0x002fe740`, the shuffle `0x002feb48`, the turn
`0x002fdd08`, the step `0x002fe3a8`), the route legs (`0x0029b848`, `0x0029baa8`, `0x0029bca0`) and a few goals that
are not fights (pedestrian reaction, avoid enemies, the ledge thrower, run from the train, the phone, the parlay) and
the player's brain (`0x003035d8`); found by a scan of every store to `+0x110`.

- **Held flag `0x20000000`** (a turn clip playing, below) or any of `0x110c0880`, or the state code 5 or 6 (a start
  clip): no velocity; the clip moves the body.
- The vertical velocity is kept (`+0x3a0`), and the lean (`0x00248df0`) gets the heading change, as for a player.

**Turning on the spot** (`0x0025c240`): with a speed of 0 this update, the heading more than **15°** off the
facing and no held flag, the AI plays a turn clip and turns the rest of the way over the clip's length
(`Human_TurnToOver`, `0x0023cf88`: a constant rate on top of the clip's own turn, which is taken as 60° or 120°,
inferred from the subtraction). Positive angles turn left. Confirmed (code):

| Heading off by | Clip (most AI) | Clip (brain `+0x0b` below 0, or move style 4 or 6) | In a fight stance |
| --- | --- | --- | --- |
| 15°-90° left | 396 `ANIM_FAST_TURN_LEFT` | slot 8 + 1 (391, slow left) | slot 15 + 1 |
| 15°-90° right | 395 `ANIM_FAST_TURN_RIGHT` | slot 8 (390, slow right) | slot 15 |
| over 90° left | 398 `ANIM_FAST_TURN_QUICK_LEFT` | slot 8 + 3 (393) | slot 15 + 3 |
| over 90° right | 397 `ANIM_FAST_TURN_QUICK_RIGHT` | slot 8 + 2 (392) | slot 15 + 2 |

The clip is played by `0x0025cb90`: a fade of 0.1 s, the clip, then the idle (slot 0, or slot 11 in a fight stance)
looping, holding the given flag (`0x20000000` here) while it plays, state code 0. This is the turn (398) seen at
runtime before an AI's run-in ([AI, Moving](ai-moving.md#moving)).

**Arriving with one clip** (`0x0025c5b8`, the global `0x005104e8` = 1): while the brain asks for more than the jog
speed, its `+0x11c` is 0, the human is not down, not hurt and holds no flag, and its point is at least 0.1 m beyond
the radius (`d` = the distance in plan − `+0x118`), the AI may cover `d` with a single clip instead of the gait.
Confirmed (code):

- **From rest** (stored gait `+0x1a8` 0): for an asked gait below 4 a step, slot 1 + (0 ahead within 45°, 1 behind
  beyond 135°, −1 right, −2 left: 401, 402, 400, 399); for 4 or more a dash, slot 2 + (0, 1, 3 right, 2 left: 403,
  404, 406, 405); in a fight stance slots 12 and 13 instead of 1 and 2.
- **At a run or sprint** (gait 4 or 5) with the point within 45° ahead: the run stop, slot 33 (417).
- With `L` the clip's root displacement: the clip plays when `L` ≥ `d`, or from rest when `L` × 1.25 ≥ `d`. When
  `L` < `d` + the radius it plays as it is; otherwise the body is turned so the clip's displacement points at the
  point and moved over the clip's time so it ends there (`Human_MoveToOver`). The clip holds `0x80000`, and the
  control sets no velocity that update.

So an AI running at its point stops with the run stop once the point is closer than the run stop's slide (about
1.6 m for Rembrandt), and one starting near its point takes a step or dash. Whether a given goal's move reaches
this test is the move action's business ([AI, The move action](ai-moving.md#move-action)).

**Other effects**: when the brain asks for any gait above 0 (`Brain_UpdateGait`, `0x0028ab08`, tested in
`Human_UpdateControl`; confirmed (code)), the human lets go of carried props (`Human_ReleaseCarriedProps`, `0x0024ce40`)
and, after a state 18 or with held flag `0x20000`, its animation is rebuilt (`0x0025f450`); a human in a scene state
also lets go of its props.

### Step control {#step-control}

Confirmed (code) at the cited addresses unless a line says otherwise.

The `TakeStep` and `GiveWay` actions ([AI code](ai-code.md#actions)) move an AI one step by swapping its control
function for the **step control** (`Human_StepControl`, `0x00243420`) for a single update. Confirmed (code) at the
addresses cited.

**Starting a step.** `TakeStepAction_Start` (`0x002fe3a8`) writes the action's one argument, a **heading** in radians,
into the brain's heading `+0x110`, then calls `Human_EnterStepControl` (`0x002432b0`). The step has no distance
argument: the clip decides how far the body goes. `Human_EnterStepControl` does nothing when either of these holds:

- the human has any of state bits **`0x180f3ff0`**. That is `0x10`-`0x2000`: grabbed or grabbing, mugging or
  mugged, the tackle and mount, and a paired power move ([Combat: State flags](combat/index.md#state-flags)). It is also
  `0x10000`-`0x80000` (`0x10000` is hurt) and `0x8000000` and `0x10000000`, whose meanings are not traced;
- the step control is already installed.

Otherwise it saves the current control (`Human_SaveControl`: `+0x1bc` copied to `+0x1c4`) and installs the step control
(the 8-byte member-function pointer at `+0x1bc` = `0x00243420ffff0000`: `+0x1be` = −1 and the function `0x00243420` at
`+0x1c0`), confirmed (code).

**One update of the step control, for an AI.** The per-player record's `+0x1b` is 0 for an AI.

1. **No move from the control.** The two outputs are the zero vector (`0x005116c0`) and the human's current rotation
   (transform table `0x00714b10`). Any displacement comes from the clip's root motion; inferred from the
   [one-clip arrival](#ai-locomotion), which uses the same clips and measures their root displacement.
2. **Wait while not free.** The control returns at once, still installed, while any of these holds:
   - `Human_IsBusy`: state `0x79b9e1e0f30`, airborne, state `0x100000000`, the per-player `+0x1e`, or a held flag
     of `0xaeebf7ff`;
   - grabbing;
   - `+0x280` is not −1;
   - wounded;
   - state `0xe0000`;
   - any held flag.

   The step then starts on the first free update.
3. **Pick the clip.** Let `d` be the brain's heading `+0x110` minus the facing, wrapped to −π..π. The turn boost is
   brain `+0x0b` ([AI](ai-gangs.md#brain-boosts)). The facing to end on is the heading, offset so that the clip's own
   direction points along the heading:

   | `d` | End facing | Fight stance | Boost 0 or below | Boost above 0 |
   | --- | --- | --- | --- | --- |
   | within ±22.5° (π/8) | heading | 366 `ANIM_FIGHT_MOVEMENT_STEP_FORWARD` | 401 `ANIM_MOVEMENT_STEP_FORWARD` | 403 `ANIM_MOVEMENT_DASH_FORWARD` |
   | 112.5° (5π/8) or more either way | heading − π | 367 `..._STEP_BACKWARD` | 402 `ANIM_MOVEMENT_STEP_BACKWARD` | 404 `ANIM_MOVEMENT_DASH_BACKWARD` |
   | 22.5° to 112.5°, positive | heading − π/2 | 365 `..._STEP_RIGHT` | 400 `ANIM_MOVEMENT_STEP_RIGHT` | 406 `ANIM_MOVEMENT_DASH_RIGHT` |
   | 22.5° to 112.5°, negative | heading + π/2 | 364 `..._STEP_LEFT` | 399 `ANIM_MOVEMENT_STEP_LEFT` | 405 `ANIM_MOVEMENT_DASH_LEFT` |

   In a fight stance the boost is ignored. The clip ids are the [anim ids](../references/anim-ids.md#anim-364).

   **Which way is positive.** Both angles are the game's headings (`Quat_Heading`, [Maths](maths.md)): a heading `h`
   faces `(sin h, cos h)` in the ground plane, so it grows from `+y` toward `+x`, clockwise seen from above (`+z`
   up). A positive `d` therefore means the heading lies **to the human's right**: from the facing `f`, his right
   (forward × up) is `(cos f, −sin f)`, the heading `f + π/2`. Confirmed (code): `Quat_AxisY` (`0x003363b0`) gives
   the forward `(−sin θ, cos θ)` for a turn θ about `+z`, `Vec_Heading` (`0x00335d98`) is `atan2(x, y)`, so the heading
   is −θ. The clips agree: the generic `gen_step_right` clip's root moves along body `+x` (root velocity `+0.98` m/s on
   x at its first key; `gen_step_left` `−0.86`; `gen_step_forward` `+1.23` on y; read from the disc as numbers), and
   body `+x` turned by the end facing `h − π/2` points along `h`. So the human always steps toward the heading:
   STEP_RIGHT with a positive `d`, ending side-on with his right toward it; with `d` = +90° the end facing is his
   current one and he does not turn. Runtime was not needed: the code and the clip data leave no other reading.
   A port whose headings grow anticlockwise (the turn θ, minus the game's heading) sees every sign flipped: there a
   **negative** `d` picks STEP_RIGHT, and the end facings are heading + π/2 for the right step and heading − π/2 for
   the left.
4. **Play it.** `Human_PlayOneShotClip` (`0x0025cb90`) plays the clip:
   - a blend-out of the current pose over 0.1 s, then the clip chained in over 0.2 s;
   - after the clip, the idle loops (slot 0, or slot 11 in a fight stance);
   - while the clip plays it holds held flag **`0x80000`**, with state code 0 and the push sphere off.

   The clip-end callback (`0x25c5b0`, not yet a function of the Ghidra project) is an empty function (`jr ra`, confirmed
   (code)).
5. **Turn during the clip.** The human is not turned first. `Human_TurnToOver` (`0x0023cf88`, called from
   `Human_StepControl`; confirmed (code)) turns it to the end facing at a constant rate over **half the clip's play
   time** (`Anim_GetPlayTime` × 0.5), while the clip plays. The angle is negated before the call, the same convention as
   [turning on the spot](#ai-locomotion): `Human_TurnToOver` takes the turn θ about `+z` (anticlockwise seen from
   above), which is minus the heading. A turn under 0.01 rad is skipped.
6. **Leave.** `Human_LeaveStepControl` (`0x00243360`, confirmed (code)) runs in the same update. It restores the saved
   control (`Human_RestoreControl`), then sets `+0x1bc` to `Human_UpdateControl` (`0x00243848`), or to
   `Human_MoveAttached` (`0x00244e78`) while grabbed or being mugged. So the step control is installed for a single
   update, unless step 2 held it.

For a pad player (`+0x1b` set) the step control restores the saved control. It then picks the player's control and
runs it in the same update: air when turn-limited (state `0x400000000`), the fight stance with a target, button tap,
the wheelchair, or else `PlayerLocomotion` (`0x00240e38`). Confirmed (code).

**How the action ends.**

- **Update** (`TakeStepAction_Update`, `0x002fe428`, confirmed (code)) returns running (0) while held flags
  **`0x20080000`** (`Human_HasHeld20080000`, `0x00228448`: `0x80000` the step clip, `0x20000000` a turn clip) are set,
  or while the step control is still installed. Otherwise it returns done (2). The step lasts as long as its clip.
- **Abort** (`TakeStepAction_Abort`, `0x002fe3d8`, confirmed (code)) always calls `Human_LeaveStepControl`. With
  argument 0 it agrees; with a non-zero argument it refuses while `0x20080000` is held.

**GiveWay.** `GiveWayAction_Start` (`0x002fe5d8`) runs these steps in order (confirmed (code)):

1. The AI looks at the other human for 2000 ms (`Brain_LookAtTarget`, weight 0.5).
2. With the action's boost flag (`+0x14`), it saves the turn boost `+0x0b` and raises it by 1. A boost of 0
   therefore becomes 1, which selects the **dash** clips.
3. It runs `TakeStepAction_Start`.

Abort (`GiveWayAction_Abort`, `0x002fe658`) restores the saved boost once `TakeStepAction_Abort` agrees. **It also runs
when the step ends normally**: a finished action is popped through `Action_AbortAndDestroy`, which calls Abort with
argument 0 (always agreed) for any started action ([AI: Actions](ai/index.md#actions)). So the boost goes back however
the GiveWay ends; nothing else restores it. The destroy (`GiveWayAction_Destroy`, at `0x2fe6a8`, not yet a function of
the Ghidra project) then clears brain `+0xcc` bit 1. Confirmed (code). `Brain_GiveWayTo` (`0x00289ed0`) sets the boost
flag when the mover is a pad player (`+0x1b0` ≠ −1) whose stored gait `+0x1a8` is 3 (jog) or more, so an AI dashes out
of a running player's way and steps out of a walker's.

**Who may give way** (`Brain_GiveWayTo`, before its free-sector search, which is in
[AI: Giving way](ai-moving.md#giving-way)). All of these must hold:

- the stander's brain has a task (`+0x04` ≠ 0) or its `+0x09` is set;
- **neither human is a threat to the other**: `Human_IsThreatTo` (`0x00222a48`, confirmed (code)) is false both ways. It
  is `Brain_IsThreat` on the two brains: the gangs are enemies (`Gang_IsEnemyOf` on brain `+0x20c`), or both are pad
  players, with a Warrior's (type 3) `+0x2e5` mode able to force it either way. It tests only; nothing is changed;
- **the stander is idle under its control**: `Human_IsIdleUnderControl` (`0x00225390`, confirmed (code)). Its control is
  `Human_UpdateControl` or the step control, it has no state bit of **`0x7bf9e1f3ff0`**, and it has **no held flag
  at all**.

Then a free sector gives a GiveWay action on the sector's heading (the stander's facing + sector × 45°), started
after a random 0-124 ms.

### Wheelchair control {#wheelchair}

Confirmed (code) at the cited addresses unless a line says otherwise.

[`HuSetWheelchairControl`](../references/bindings/character.md#husetwheelchaircontrol) (`0x00234188`), confirmed
(code):

- **On**: look-behind switch 11 (`0x0050b23c`) = 0 ([Camera](camera.md)); unless the human already runs it: state
  flag `0x80000000000`, the current control saved (`0x00221038`: `+0x1bc` copied to `+0x1c4`), the control set to
  `wheelchairControl` (`0x002427e8`, `0x00244620`), every player command off for the human, and commands 46 and 47
  added (kind 7, pad bits `0x4` L1 and `0x8` R1).
- **Off**: switch 11 = 1, the flag cleared; if the human runs `wheelchairControl`, the saved control comes back
  (`0x00221080`), commands 46 and 47 are removed and all commands are on again.

**`wheelchairControl`** (`0x002427e8`), each update. A human whose per-player record `+0x1b` is 0 (inferred: not
pad-driven) or without the flag goes back to its normal control and runs that instead, so the Rumble CPU drives its
path with its usual locomotion. Otherwise it reads only **L1** (`0x4`), **R1** (`0x8`) and **cross** (`0x40`) from
the pad; the sticks are not read.

- `speed` = the length of the velocity; `max` = the sprint speed (`0x002216c0`: human `+0x3a4` × record `+0x17c`).
- **L1 and R1**: `speed` + 0.5, at most `max`, no turn. From a standstill (and anim state not 4) it instead sets
  the record's state code (`+0x14`) to 5, and while the code is 5 or 6 the control asks for no velocity
  (inferred: a start clip plays first).
- **L1 alone**: `speed` − 0.04 (not below 0) and a turn of +(0.03 + 0.05 × (1 − speed / max)) rad; **R1 alone**:
  the same turn negated. So one wheel turns 0.08 rad an update at a standstill and 0.03 at top speed. Positive
  turns raise the heading (counter-clockwise from above), so L1 (the right wheel) turns left and R1 right.
- **Neither**: `speed` − 0.04 (not below 0). **Cross** (the brake) then takes 0.5 more off, not below 0, whatever
  the shoulders do; with neither shoulder held it replaces the 0.04 decay.
- The new heading is the old one plus the turn (written as a rotation about z); the velocity is `speed` along it, zero
  while any of state flags `0x110c0880` is set or the state code is 5 or 6. Above 4.5 the human's animation layer gets
  flag 2 and event groups 3, 6, 18, 19, 24, 25, 29, 30, 32 and 33 (`Human_StrikeAllOn`, `0x00248170`, confirmed (code));
  at or below 4.5 they are cleared (`0x00248270`). The pending turn fields `+0x298` / `+0x29c` are zeroed.

The constants, confirmed (code) in `Human_MoveWheelchair`, are at `0x005104d4` (0.5 push), `0x005104d8` (0.04 decay),
`0x005104dc` (0.5 brake), `0x005104e0` (0.03) and `0x005104e4` (0.08). Changes are per update; whether the update is
1/30 s or 1/60 s here is not traced.

## In Coney {#coneys-implementation}

- `src/human/locomotion.*` is the pure maths: the camera-relative stick, dead zone and run threshold
  (`PlayerRecord_Update`), the target speed and gait, the per-gait turn limit with its ease and carry, acceleration,
  the skid rule, the slope factor and the gait blend's target (`0x0025ec28`); the sprint's target speed; and the
  body's lean (`Human_Lean`, `0x00248df0`). The turn limits and ease are the values in play (20°, 18°, 18°, 16°, 24°
  in a combat stance, full at 2.0 rad): the defaults, and for a level `--play-level` runs, what the preload's
  `CfgSetTurnRates` and `CfgTurnRate` calls recorded.
- `src/human/locomotion_gate.*` is the locomotion gate ([Tasks](tasks.md#locomotion-gate)): `stickBusy()`
  (`Human_IsBusy`'s mask `0xaeebf7ff`, in the air or held: no stick step) and `stickVelocityGated()` (`0x110c0880`, or
  state code 5 or 6: the stick turns the human but sets no velocity).
**Disc test** (`[player]`, counts only): Rembrandt's speeds from his clips are the runtime values (walk 1.629, jog
4.857, run 7.801, sprint 10.245 m/s); at level99's start he lands at z 0.25, idles in 388, takes the walk start 413
at a 30 % stick (not moving on its first update, then 0.762 m/s: the clip's root motion × 0.97), walks in 408 at
1.629 m/s, gains 0.8 m/s per update to the run (410), skids into the run stop (417) on release and then idles, and
never leaves the ground or passes through the scenery he is run into; the same script gives the same path twice.
On the default sandbox's ledges he walks onto the 10 and 25 cm blocks (the 25 cm block's 3 m wide face is two
slivers the 0.25 m rule skips) and stops at the 50 cm one, 0.484 m short of its face, where the sphere meets its
top edge.
- **Standing**: a human not asked to move counts as moving while faster than a quarter of the walk speed. The
  original's getter `0x00221580` is the **sneak-walk** speed (record `+0x16c` × `+0x3a4`, 1.585 for Rembrandt;
  confirmed (code)), so the original's threshold is 0.396 m/s against Coney's 0.407. **Coney's reading**: a start clip
  no longer asked for gives way to the idle at once, whatever its root motion's speed: the idle builder fades over a
  start clip still playing (`0x0025f770`, above), which the speed test alone would never reach.
- **Locomotion while a start clip plays** keeps turning (only the horizontal velocity waits for the clip), as the
  locomotion gate gives it (confirmed (code)): the start clip holds `0x10000000`, which zeroes the stick's velocity
  but is outside `Human_IsBusy`'s mask, so the turn still runs ([Tasks](tasks.md#locomotion-gate)). In the air
  only a jump steers; a fall keeps its velocity, and no clip's root motion moves the body while airborne. Standing
  (no gait blend yet), the update the start clip begins sets no velocity, as at runtime.
- **The start clips hand over early**: the walk and run starts give way to the gait blend on the update after which
  less than an update of them is left, so they last 13 updates as at runtime (playing to the end would take 14).
  From their first moving update they move at the clip's root speed × 0.97, as at runtime (0.762 m/s for the walk
  start, 2.773, 2.486, … for the run start), through the sampler's reading of the clip's keys
  ([Animation](formats/animation.md#coneys-implementation)).
- **Root motion** of every clip that moves the body on the ground is scaled by the body scale (0.97 for Rembrandt);
  a wall or short wall climb's rise at its second clip is not (it matched the runtime unscaled).
- **The run stop** (417) plays after a skid, after the 0.1333 s move fade, which blends only the pose: the gait blend
  it fades from has no root velocity, so the body moves at the clip's 2.244, 4.996, 5.101, … m/s from its first
  update, as at runtime. The skid's own update still turns one step and the run stop then holds the facing. The skid
  needs the run speed ≤ the speed, compared exactly ([The run stop at a run](#run-stop)): a sprint always skids, a
  steady run only when its speed lands on or above the run speed. **Coney's reading** of the speed: the velocity's
  length with each multiply, add and the square root rounded toward zero, as the PS2's floating-point unit rounds
  (`core/ps2_float.h`); the velocity itself is Coney's, so a steady run reaches the run speed on about 6 % of updates
  against the original's 10 of 93. What builds the run stop is not traced. It holds `0x80000`, which the locomotion
  gate reads (busy and no stick velocity). The skid sets no state code (the original's 9): the run stop's own bits
  gate the same updates.
- **The idle's fade** (0.15 s, 5 updates) holds `0x10000000`, so after a stop, a release or a block the stick turns
  the human but the walk start waits for the fade to end ([Tasks](tasks.md#locomotion-gate)). Over those updates the
  last stick held turns him on, its magnitude × 0.8 each update, eased from rest at the standing turn's 20° limit;
  **Coney's reading** of the ease (after `run_circle`'s release it turns 0.666°, 1.136°, 1.409°, 1.515°, within
  0.004° of the original's). Coney never sets state code 5 (turning in place): nothing reads it apart from
  the gate, and the fade holds the same updates.
- **The lean**: the four limits go with walk (and standing), jog, run and sprint; the drawing rolls the body by the
  lean about its forward axis through the feet.
- **The gait blend's leading clip** uses a tolerance of 0.001 when it compares the value with its target.

### Notes for implementers {#notes-for-implementers}

- **Gaits**: a walk up to 0.95 stick magnitude (any amount above 0.12 gives the same walk), a run above it; the start
  clips (413, 414) first, then the gait clip; idle at once on release.
- **Clips that move the body** move it at the clip's root speed × the body scale from their **first** moving update,
  with no fade-in of that speed: the walk start 0.762 m/s on every one of its 12 moving updates, the run start 2.77,
  2.49, 2.56, … and the run stop 2.24, 5.00, 5.10, … ([Per update](#locomotion)). A first update slower by 0.75 (the
  start clips' playback rate) or a stop that ramps from 0.31 m/s is a difference to remove.
- **The stick is camera-relative** before the human sees it: turn the stick by the camera's heading first, then
  apply the dead zone (0.12) and the run threshold (0.95) to its length.
- **Turn with a limit and an ease**, not instantly: in play 20° per update walking, 18° jogging and running, 16°
  sprinting, eased below 2.0 rad (the values `config_preload2.lua` sets, [Locomotion](#locomotion)).

## Open questions

- **Jog**: when a pad-controlled human jogs other than when carrying (a movement style, a script).
- **The idle threshold's getter** (answered): the sneak-walk speed ([Coney's implementation](#coneys-implementation)).
- **A steady run's velocity in floats**: the original's run measures at or above the run speed on 10 of 93 updates,
  Coney's on about 6 %; how its velocity is built (the order of the multiplies) is not traced.
- **The run start's speed on its 7th and 11th updates** in `run_circle` (3.35 and 5.40 m/s against Coney's 3.25 and
  5.54): which key the original samples there.
- **The trace's clip id in the gait blend** (record `+0x20`): 410 where Coney reports the blend's leading clip (411,
  409) in `run_circle`'s steps 54-56; which clip of the blend the original stores.
- **The circling run's turn**: on `run_circle`'s step 75 Coney turns 16.92° against the original's 16.44°, which the
  camera's auto-follow then grows to 6.6°; whether the stick is turned by the camera of this update or the last.

### The run start's length {#q-run-start-length}

Its 0.36 s against the walk start's 0.444 s is not explained; whether it begins part-way through, as the
walk-start-to-run-start swap does.

## How this was checked

Code claims were read in Ghidra in `SLUS_212.15`.

### The old runtime reads {#runtime-reads}

The turn limits "in play" were read in the street save's RAM (PCSX2 2.9.94) and match every measured turn. The gait
readings (the stick held straight up from a standstill, speed `+0x1ac` sampled every 5 ms) are from `level99`,
checkpoint 1 (read over PINE on 2026-10-04), and the per-update readings from two "feel passes" in the street from save
states; the second was run on 2026-10-05. They predate run records.

### The run stop at a run {#runtime-run-stop}

Observed in PCSX2, scenario `run_stop` (slot 1, a straight run released, then a run circling behind the camera
released, as `run_circle`), with hooks on the state-code setter and on the skid test logging the gait, both stick
buffers, the run speed and the speed register `f28` as raw words:

- The run speed is `0x40f9a3ad` (7.8012300). The speed the locomotion measures at a steady run (the length of the
  velocity, `0x0023fea8`'s vtable `+0x94`) jitters between `0x40f9a3aa` and `0x40f9a3ae`: of 93 updates at the run
  in one recording, 10 were at or above the run speed and 83 one to three units below it.
- On every release seen the gait was 4, the previous stick 1.0 and the stick 0. Where the speed on the release update
  was below (`0x40f9a3ab`, `0x40f9a3ac`, straight and circling alike, three recordings) no code 9 was set and the idle
  388 began on the release update itself at 0 m/s (the skid test's call re-labelled one update later after the recorder
  fix, [Recording a trace](../guides/research-workflow.md#hooks)), holding `0x10000000` for 5 updates; where it was not
  (one circling release), the state code went to 9 from `0x00241538`, the idle builder set it back to 0 (`0x0025fa2c`)
  and the run stop 417 played for 24 updates with `0x80000`, at the speeds in the table above.
- So **turning has nothing to do with it**: the circling run of `run_circle` (step 192) and a straight run both go
  either way. After a sprint (10.245 m/s) the speed is far above the run speed, so a sprint always ends in the run
  stop; a jog or walk (gait 3 or less) never does.

In the idle that follows a release without the run stop, the stick's last angle and magnitude (`+0x5d8`, `+0x5dc`)
keep turning the body: while the idle's fade holds `0x10000000`, the locomotion turns toward the last stick angle with
a magnitude × 0.8 each update (`0x002411cc` onward), which is the 0.7°, 1.1°, 1.4°, 1.5° seen after `run_circle`'s
release.
