# Feel comparison (on-foot movement and the follow camera)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`) and Coney at commit
`ae87e1b`. Runtime claims were made in PCSX2 2.9.94 (2026-10-05), reading memory over PINE once per character update
while a scripted pad played, from copies of the test save states with the pad input patched in
([Driving PCSX2](../guides/research-workflow.md#driving-pcsx2)); each claim names its save. The saves' "street"
is in **`level99`'s world**: the player's spots, the ground height (0.223) and the objects there (a 1.505 m trash can,
a material 30 fence 19.04 m ahead of the fence save) are exactly where `level99`'s collision mesh puts them.

## Purpose

What it takes for Coney's player to *feel* like the original's: the same scripted analog inputs were played in the
original and in Coney, from the same spot, and the per-update traces of position, speed, heading, clip and camera were
compared. This page lists every difference beyond noise with the original's value and its evidence, and which Coney
value or rule to change. The mechanics themselves are on [Characters](characters.md) and [Camera](camera.md), which
were corrected in place where the runs contradicted them.

## Method

- **Inputs.** Each run is a Coney input script (`FRAME stick left X Y`, `press l2`, `tap triangle`,
  [Input scripts](../guides/building.md#input-scripts)) with the frame read as the update number. On the original
  side a PINE client wrote the same raw stick bytes Coney makes from a script value (`160 + round(0.95 × v)` for
  `v > 0`, `95 − round(0.95 × |v|)` below, 128 at rest) and the same button bits, through the patched pad read, at
  the update the script names (±1 update of latency, aligned by the clip change). The original's stick table at
  `0x0050b8d0` is the same map as Coney's `pad::stickValue` (dead band 95-160, `(raw − 160) / 95`; read from the
  save's RAM), so both sides saw identical stick values. Deflections used: 35, 50 (diagonal), 60, 70 (diagonal), 80,
  96 and 100 %, plus turns of 15°, 45°, 90° and 180°, stick sweeps of 1.5°, 3° and 6° per update, the right stick at
  30, 60 and 100 %, L2 and triangle.
- **Original trace.** Every update (game time `*(0x0050b734) + 0x48` advancing by 1/30 s): the transform table
  (`0x00714b00`, position and heading), human `+0x1ac` speed, `+0x1a8` gait, `+0x3a0` vertical speed, record `+0x20`
  clip, `+0x08` flags, `+0x14a` stamina, and the follow camera (`*(0x005d9158)`): `+0x10` position, `+0x180` look-at,
  `+0x250` wanted position, `+0x3b4` target pitch, `+0x388` lag, `+0x32c` / `+0x330` leash band. No update was
  missed in any run.
- **Coney trace.** A throwaway tool (outside the repository) stepped Coney's `Human` and `FollowCamera` headless on
  `level99`'s collision mesh, from the save's position and heading, with the camera placed behind at the save's view
  heading, and printed the same quantities per update. Coney's own `--trace` option now writes them
  ([Building](../guides/building.md#tracing)).
- **Saves.** Slot 1 (a clear street, facing −x) for walks, runs, turns, sprint, jumps and the right stick; slot 7
  (the fence) for the running fence climb; slot 8 (the trash can and roof) for the short-wall and wall climbs.

## Comparison

"Coney" is Coney at `ae87e1b` driven by the same script. Rates are per character update (1/30 s) unless marked.

| Quantity | Original | Coney | Difference | Coney value to change |
| --- | --- | --- | --- | --- |
| Turn limit per update, player: walk / jog / run / sprint | **20° / 18° / 18° / 16°** | 12° / 6° / 4° / 2.5° | 1.7× to 6.4× faster | `LocomotionTuning::walkTurnDegrees`, `jogTurnDegrees`, `runTurnDegrees`, `sprintTurnDegrees` |
| Heading error at which the turn eases to full | **2.0 rad** | 1.5 rad | ease is gentler | `kTurnEaseError` |
| Stick turned 90° at a run | 16.1°, then 18° per update; facing reached in 6 updates (0.2 s) | 4° per update, 23 updates | | (above) |
| Stick turned 45° at a run | 6.3°, 9.8°, 10.8°, 10.0°, 8.4°, 4.5° | 2.2°, 3.7°, then 4° | | (above) |
| Stick turned 90° in a sprint | 14.3°, then 16° per update | 2.5° per update | | (above) |
| Stick released at a run (7.80 m/s) | **run stop 417** for 24 updates, the body slides **1.63 m** | stops dead, idle at once | missing | play the run stop after a run's skid as after a sprint's |
| Stick reversed (180°) at a run | turns 18°, then skid: 417 for 24 updates sliding 1.63 m the old way, then the walk/run start toward the stick, turning 20° per update | speed 0 at once, then turns 12° per update while gaining 0.8 m/s per update, no clip | missing | (above, and the turn limits) |
| First update with the stick pushed (from idle) | the start clip begins, speed 0 | the start clip begins and the body moves at 0.8 m/s | Coney moves one update early | no locomotion speed on the update the start clip begins |
| Walk start (413) / run start (414) length | 13 updates each | 14 updates each | +1 update | the start clip's length or its hand-over test |
| Root-motion speed of a clip that moves the body | walk start 0.762 m/s, landing 436 4.230 m/s | 0.786, 4.361 | Coney 3.1 % fast: the original scales root motion by the body scale (0.97) | multiply root motion by `Human::scale()` |
| Walk / run / sprint speed | 1.629 / 7.801 / 10.245 m/s | the same | none | |
| Gain per update | 0.8 m/s | 0.8 m/s | none | |
| Walking body: distance kept from a wall face | **0.480 m** (two walls and the fence) | 0.34 m | 0.14 m closer in Coney | the player's sphere is 0.35 × **1.4286** (body `+0x60`) × 0.97 = **0.485** m: `BodyTuning::radius` 0.5 for a player |
| Running into a wall 60-70° off its line | speed drops to the slide's share (4.0, 1.5, 0.86, 0.65, 0.56, …, 0.40 m/s) and the player ends in the **idle** pressing the stick | keeps 7.80 m/s and slides along the wall at about 6.9 m/s | Coney slides, the original stops | keep the swept (slid) velocity as the next update's current speed (the speed is its length) |
| Jump during the run start | allowed once faster than 3.3 m/s (tap at 3.35 m/s jumped) | refused until the start clip ends | Coney late | drop the start-clip test from the jump |
| Run jump: arc | 5.5 m/s up, apex 1.06 m, the same per-update heights | the same | none | |
| Run jump: time in the air / take-off to landing | **24** updates, **6.24 m** | 23 updates, 5.94 m | Coney lands 1 update early | let the last airborne move end below the ground (0.19 m at runtime) and land on the next update |
| Speed on the landing update | 7.80 m/s (full), then 436 at 4.23 | 0.82 m/s, then 3.27, 4.36 | Coney brakes early | keep the horizontal speed on the landing update |
| Landing 436 | 11 updates, then a gait blend from 4.23 m/s gaining 0.8 per update | 11 updates | none (but speed above) | |
| Air turn | 4° per update | 4° per update | none | |
| Sprint: speed, stamina drain and refill | 10.245 m/s; 20 per second; 40 per second | the same, update for update | none | |
| Running fence climb (slot 7, tap at the same update) | 440 for **11** updates, snap 1.11 + 1.17 m, ends **0.48 m** from the face; 441 15; 442 13; run again 39 updates after the tap | 440 for 13 updates, snap 0.98 + 0.98 m, ends 0.68 m from the face; 441 15; 442 13; 41 updates | 440 2 updates long | the start point (the body stops at its 0.485 m sphere) |
| Short wall (trash can, 1.28 m up) | the feet rise in **2** updates (1.12 m, then 0.17 m) | 1 update | | |
| Wall onto a roof (2.64 m up) | rise of 2.30 m, then +0.097 m per update for 4 updates | 1 update | | |
| Camera leash band (wanted distance) | **4.8-5.3 m** (the configured default distance and + 0.5) | 3.0-3.5 m | Coney 1.8 m closer | `FollowSettings::leashNear` / `leashFar` from the default distance, not 3.0 |
| Camera distance walking / running | 5.49 m / 5.65 m | 3.68 m / 3.85 m | (band) | (above) |
| Camera pitch walking / running | 12.6° / 11.1° | 12.3° / 10.3° | follows from the band | |
| **Auto-follow** (camera swings toward the facing) | **runs** whenever the player walks, runs, sprints or is in the air: the auto-centre rule, within about 5°/s for angles of 30-90° | none | missing | add the auto-centre rule (`0x00129f88`), [Camera](camera.md#heading) |
| Stick held 90° to the side | the player **runs in a circle**: camera and facing turn together at about 122°/s running, 127°/s walking | a wide arc, turned only by the leash (about 35°/s) | (auto-follow) | (above) |
| **Sprint camera** | from the first update at the sprint gait the band moves 4.8-5.3 → **3.0-3.5** and the target pitch 13° → **7°**, both over **14 updates**; back over 14 updates once the run stop slows the body | none | missing | add the sprint zoom (values below) |
| Camera in a sprint | 4.70 m, pitch 5.2° | 3.85 m, 9.7° | (sprint camera) | |
| Right-stick yaw at 30 / 60 / 100 % | 74.8 / 106.7 / 150.0 °/s | the same | none | |
| Right-stick pitch upper limit | **40°** | 30° | 10° | `FollowSettings::upperPitchDegrees` 40 at the default zoom |
| Look-at height after a climb's rise | eases **20 % of the way per update** (then the last 0.6 m in 2 updates); pitch stays at 3.6° or more | snaps with the feet; pitch falls to −22° (roof) and −8° (trash can) | Coney looks up from below | ease the look-at height |
| Camera during a fence climb | 4.9-5.3 m throughout, the fence (material 30) between camera and player | collapses to **0.5 m** while the body passes through | Coney's ray hits the fence | the camera's ray skips material 30 (and, inferred, the other see-through fences) |
| Camera passing a low one-sided face just behind the player (slot 7, 0.19 m behind) | unaffected | pulled in to 0.5 m for about 1 s | Coney pulls in | (the collision step; [Open questions](#open-questions)) |

Evidence: confirmed (runtime) for every "Original" value, PCSX2 2.9.94, the saves named in [Method](#method), each
run read per update; the turn values are also confirmed (code), below.

### Details behind the table

- **Turn rates are set by the scripts.** `config_preload2.lua` calls `CfgSetTurnRates(11, 16, 18, 18, 20, 24)` and
  `CfgTurnRate(true, false, 2.0, 0.8)` ([CfgSetTurnRates](../references/bindings/config.md#cfgsetturnrates)): the
  six player entries of the table at `0x005101b0` (the even words; the odd words, the non-players' 1.5°, 2.5°, 4°,
  6°, 12°, 24°, keep their `.data` values) become 11°, 16°, 18°, 18°, 20°, 24°, and the ease reaches its full rate at
  2.0 rad. `Human_MaxTurn` (`0x002213d8`) and its gait lookup (`0x002212d0`, the even word for a pad-controlled
  human, per-player record `+0x1b`) give the player: 16° sprinting (gait 5, `0x00223a98`), 18° running (`0x00223a60`)
  and jogging (`0x00223a50`), 20° otherwise; 4° and 6° in the states `0x00227f68` / `0x00227f40`; 11° in the states
  `0x00223980` / `0x00223ad0`, and 11° × 0.25 under `0x00227d98`; 24° in a combat stance (`0x00228340`); 16° × 0.25
  = 4° with record flags `0x1000080`. Confirmed (code) at the cited addresses and confirmed (runtime): the values
  read in the save's RAM are those, and every measured step fits
  `limit × (1 − cos(π · error / 2.0)) / 2 + 0.8 × previous step`, clamped to the limit (90° at a run:
  18 × 0.891 = 16.0, then the clamp; 45°: 6.1, then 9.9 with the carry). In the air the turn stayed at 4° per
  update, the non-player run value (inferred: `Human_AirControl` reads the other column).
- **Auto-follow.** The auto-centre rule's rate, `(a − 45°) × 2.444 + 45°` per second from 22.5° to 90° (negative
  just above 22.5°: at 22.6° the camera turned 4°/s the other way), 200°/s from 90° to 100° and falling to 60°/s at
  157.5°, fits the measured rotation of the wanted position with `a` = the angle between the player's new facing and
  the view from the camera's position at the start of the update to its look-at point (mean error 5.4°/s over 656
  updates; the other definitions tried were 8-30°/s worse). It turned nothing while standing, during the walk and
  run start clips and during the landing clip 436, and turned at walk, run, sprint and in the air. The save has the
  per-pad option bytes `0x0050b240` and `0x0050b248` at 1 (the auto-centre rule). Confirmed (runtime) in slot 1.
  Both are 1 by **default**: their bytes in the executable's `.data` are 1 for both pads; `0x0050b248` (and `+1`)
  is set to 1 again by every camera reset (`0x00122b80`); `0x0050b240` is changed only by the options menu (its first
  choice writes 1, its second 0, `0x001d7ff0` → `0x00418aa8`, which also stores it in the profile at `+0x448` + pad ×
  4) and by loading a profile (`0x00421ad0`). The camera reads both at `0x00129e00` / `0x00129e2c` and takes the
  default rule when either is 0. Confirmed (code). So a fresh boot uses the auto-centre rule; what a new profile
  saves at `+0x448` is not traced.
- **Sprint camera.** Band and target pitch per update after the first update at gait 5 (the band's near edge; the
  far edge is 0.5 more): 4.569, 4.379, 4.221, 4.085, 3.968, 3.863, 3.770, 3.686, 3.607, 3.533, 3.462, 3.391, 3.315,
  3.216, then 3.000; the target pitch falls 0.4286° per update (6° in 14 updates, 12.9°/s). On the way back the same
  14 values in reverse, starting 8 updates after the run stop began (when the body had slowed to about 2.5 m/s).
  The view's pitch follows its target at the usual rate. Confirmed (runtime), slot 1, stick 100 % and L2.
- **Run stop.** From a run or a sprint, 417 plays for 24-25 updates with record `+0x08` = `0x80000`; the speed is
  the clip's (0, 2.24, 5.00, 5.10, 5.23, 5.38, 4.19, 3.19, 2.49, 2.16, 2.12, 2.06, 1.93, 1.74, 1.43, 1.14, 0.89,
  0.75, 0.65, 0.55, 0.47, 0.39, 0.30, 0.20 m/s), 1.63 m in all, the facing held. A walk released stops at once with
  the idle (35, 60 and 80 % sticks), as Coney does.
- **Walls.** The player's physics body has `+0x60` = `+0x64` = 1.4286 (= 0.5 / 0.35) in the save, and its shape's
  radius `+0x40` is 0.3395 (0.35 × 0.97); the walking sphere is therefore 0.485 m, and the measured stops were 0.480 m
  from the face (a straight wall, an angled wall and the fence). After a blocked move the velocity is the slid
  velocity, so the next update's speed starts from its length: running at 20° off a wall's normal settled at
  `0.8 k / (1 − k)` with `k` = sin 20° (0.42 m/s measured, 0.416 predicted) until the speed fell under a quarter of
  the sneak-walk speed (0.396 m/s) and the idle played. Confirmed (runtime), slot 1; the body fields read from the
  save.
- **Jump during a start clip.** The tap that jumped came 7 updates into the run start (414), at 3.35 m/s; record
  `+0x08` held `0x10000000` then, so that record bit does not block the jump. Confirmed (runtime), slot 7.

## Coney's implementation

The fixes below are in Coney ([Characters](characters.md#coneys-implementation),
[Camera](camera.md#coneys-implementation)); the values are from Coney's own trace (`--trace`,
[Building](../guides/building.md#tracing)) on the sandbox's parkour lane with the same kind of scripted stick:

| Quantity | Original | Coney now |
| --- | --- | --- |
| Turn limits walk / jog / run / sprint / combat stance; ease | 20° / 18° / 18° / 16° / 24°; 2.0 rad | the same (defaults, and from the preload's calls for a level) |
| Stick turned 90° at a run | 16.1°, then 18° per update | 16.0°, then 18° |
| Released or reversed at a run | run stop 417, 24 updates; a reversal turns 18° first | 417 for 24-25 updates; a reversal turns 18°, then the start toward the stick at 20° per update |
| First update with the stick pushed | the start clip begins, speed 0 | the same |
| Walk / run start length | 13 updates | 13 |
| Walk start speed | 0.762 m/s | 0.762 (the first moving update 0.57) |
| Walking body | 0.485 m sphere, steep walls brake | the same |
| Jump during the run start | from 3.3 m/s | the same |
| Run jump: air time, last airborne height, landing update | 24 updates, −0.19 m, 7.80 m/s | 24, −0.19 m, 7.80 m/s, then 436 at 4.23 m/s for 10 more |
| Camera band; distance running | 4.8-5.3 m; 5.65 m | the same |
| Sprint camera; distance and pitch in a sprint | 14 updates in and out; 4.70 m, 5.2° | the same; 4.70 m, 5.2°; out again 8 updates after the run stop begins |
| Upper pitch limit | 40° | 40° |
| Auto-follow | the auto-centre rule | the rule; but circling with the stick sideways about 190°/s against 122°/s ([Camera](camera.md#coneys-implementation)) |
| Fence climb camera | 4.9-5.3 m throughout | the ray passes through material 30 |
| Look-at height after a climb's rise | 20 % per update, then 2 updates | the same |

Still different: the first moving update of a start clip and the run stop's first updates (the fades,
[Characters](characters.md#coneys-implementation)), the circling rate, and the low one-sided face that still pulls the
camera in (the collision step is one ray).

## Notes for implementers

The fixes in order of how much they change the feel:

1. **Turn rates**: player limits 20° (walk, standing), 18° (jog, run), 16° (sprint) per update, 24° in a combat stance;
   ease full at 2.0 rad; carry 0.8 as now. The air turn stays at 4°.
2. **Camera auto-follow**: the auto-centre rule of [Camera](camera.md#heading), with `a` measured from the camera's
   position at the start of the update; not while standing or while a start or landing clip plays; held off 0.334 s
   after right-stick input. Holding the stick sideways must make the player circle.
3. **Camera distance**: leash band = default distance and default + 0.5 (4.8-5.3 m with `CfgFollowCamera`'s 4.8),
   hard band as now; the sprint zoom (3.0-3.5 m and 7° over 14 updates, back over 14); upper pitch limit 40°.
4. **Run stop after a run**: the skid at gait 4 plays 417 with its root motion, as after a sprint; a reversal skids
   too.
5. **Walls**: the player's walking sphere 0.485 m; keep the slid velocity, so running into a wall at a steep angle
   brakes to a near stop and the idle.
6. **Camera collision**: the ray skips the fence materials (at least 30); the look-at height eases 20 % per update
   after a rise.
7. **Small timings**: no body speed on the update a start clip begins; start clips one update shorter; root motion
   × the body scale (0.97); jump allowed during a start clip; the landing one update later with the horizontal
   speed kept on that update; the fence climb's first clip ends against the fence (0.485 m).

## Open questions

- **A `--trace` option for `coney`** (answered): `--play-level NAME --trace FILE` writes those columns per update
  ([Building](../guides/building.md#tracing)).
- **The 0.25 m step at runtime** (answered): a test step made in the street's collision mesh was walked onto at
  0.10-0.245 m in one update and stopped the player from 0.255 m, at the distances the 0.485 m sphere predicts
  ([Characters](characters.md#walls)). A real kerb in a later level would corroborate it.
- **The auto-follow option's default** (answered): 1 for both bytes, so the auto-centre rule runs on a fresh boot
  ([Details](#details-behind-the-table)); the default rule (`0x0012a400`) runs only after the options menu turns it
  off. Still open: what a new profile saves, and the "not seen" reading at `level99` checkpoint 1
  ([Camera](camera.md#runtime-checks)).
- **The sprint camera's code**: which function moves the band and the pitch (a zoom to the minimum distance and a
  pitch of 7°, inferred from the values), and what ends it (the speed, the gait or the run stop's end).
- **The look-at height's ease**: 20 % per update measured where the code read gives 30 % (`+0x398` may scale it), and
  why the last 0.6 m goes in two updates.
- **The camera's collision** against fences and one-sided faces: which materials or flags the ray skips.
- **The landing update**: why the feet may end 0.19 m below the ground on the last airborne update (the landing
  test's segment, [Falling and landing](characters.md#falling)).
