# Camera (the follow camera)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Runtime claims were made in
PCSX2 2.9.94 (2026-10-04) by reading the camera object over PINE in `level99`, checkpoint 1, and say so.

## Purpose

The camera behind the player in normal play: how a level script creates it, what it is configured with, how it is
made current, and what its update does as far as it has been read. It is what the first playable milestone needs to
show the player walking. The lens (field of view, clip planes, view window) is on
[The streamed world](world.md#player-camera); the other camera kinds (locked, scene, mugging, power) are not covered.

In one paragraph: there is one **`Cam_Follow`** object per player, a singleton made on first use. `level99.lua` creates
the follow camera with the `global.lua` helper `CameraCreateFollow("follow", player)`, which sets it up on the player
(`CamSetupFollow`) and configures it (`CfgFollowCamera`): distance 3 to 6.6 m (4.8 by default), a pitch of 13°, a 65°
field of view, a near plane of 0.1 and a look-at point 1.4 m above the player's feet. `CameraMakeActive` makes it
current with no blend. It is a leash camera: each update it keeps its look-at point on the player, is dragged back into
a 3.0-3.5 m band when the player moves away, covers 22% of its wanted move per update, has code to swing round behind
the player's facing once the angle passes 22.5° (not seen running in `level99`, [Runtime checks](#runtime-checks)),
holds a 13° pitch, turns with the right stick at 60-150°/s, and swings or pulls in when the world is in the way, using
ray casts and sphere pushes against the collision mesh.

## Original structure

`Camera/` (`0x0011e1b0`-`0x0013b118`, [Source map](source-map.md)): `Cam_ICamera.cpp` holds the base and the camera
manager, `Cam_Follow.cpp` the follow camera. Names are ours unless a class string gives them.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0011f9e0` | `Cam_GetFollow(player)` | the player's `Cam_Follow`, made on first use (singleton at `0x005d9158 + player × 4`) | confirmed (code) |
| `0x00124778` | `Cam_Follow::Cam_Follow` | constructor (0x480 bytes, vtable `0x00535d50`) | confirmed (code) |
| `0x0011f9b0` / `0x0011f9c8` | current camera / previous camera of a player | `0x005d9150[i]` / `0x005d9148[i]` | confirmed (code) |
| `0x0011bfa8` | `CamSetupFollow` (binding `0x00365a48`) | puts the camera on its target | confirmed (code) |
| `0x0011c0b8` | `CfgFollowCamera` (binding `0x0036ab88`) | distances, pitch, lens, offset | confirmed (code) |
| `0x001259e8` / `0x00125a40` / `0x00125ac8` | set minimum / maximum / default distance | | confirmed (code) |
| `0x0011b770` → `0x0011ee08` | `CameraMakeActive` (binding `0x003656a0`) | makes a camera current, optionally blending | confirmed (code) |
| `0x0011fac8` | the blend camera (type 5) | used while blending between two cameras | confirmed (code) |
| `0x00125cc0` | `Cam_Follow` activation | | confirmed (code) |
| `0x0012adf0` → `0x0012ae58` | `Cam_Follow` update (vtable slot `+0x134`) | the per-update follow logic | confirmed (code) |
| `0x00130990` | `Cam_Follow` world collision | side probes, swing away, sphere pushes, next update's lag | confirmed (code) |
| `0x00129050` | `Cam_Follow` right stick and zoom | yaw and pitch rates, zoom and centre buttons | confirmed (code) |
| `0x00129c78` → `0x0012a400` / `0x00129f88` | `Cam_Follow` auto-follow | swing behind the player's facing | confirmed (code) |
| `0x0012d688` / `0x0012d4e8` | yaw / pitch about the look-at point | pitch clamped to `[+0x3b0, +0x3ac]` | confirmed (code) |
| `0x0012d908` | current pitch of the view | | confirmed (code) |
| `0x0012d7a8` | move the distance band | also recomputes the lower pitch limit | confirmed (code) |
| `0x001254f0` | zoom step | `+0x400` and the upper pitch limit from the distance | confirmed (code) |
| `0x00128b20` | head look | turns the player's head toward the camera heading | confirmed (code) |
| `0x0012a7d8` | height probe | short ray cast (flag `0x200`) | confirmed (code) for the call |
| `0x0012e170` | keep the watched target in view | yaw toward it, at most 270°/s | confirmed (code) |
| `0x0011e878(dt)` | cameras' update | run at the end of the characters' update ([Characters](characters.md#update)) | confirmed (code) |
| `0x00120450` / `0x00120488` | push / pop the camera stack | the camera to return to after a scene; array `0x005d9190`, index `0x0050b180` | confirmed (code) |
| `0x00367580` → `0x00353818` | `ScenePlayCinematic` (binding) | starts a scene; stores `BlendCam` at the scene's `+0x94` | confirmed (code) |
| `0x0039d870` / `0x0039f450` | scene start / scene end | take the camera over, give it back | confirmed (code) |
| `0x001562c8` | cameras to the device | the lens and draw distance each frame ([The streamed world](world.md#player-camera)) | confirmed (code) |

## Data

### The follow camera object {#the-follow-camera-object}

0x480 bytes. Fields written by the constructor and `CfgFollowCamera`, confirmed (code); "runtime" values are from
`level99` with the player standing, after the tutorial script's own calls.

| Offset | Meaning | Constructor | `CfgFollowCamera` in `level99` (runtime) |
| --- | --- | --- | --- |
| `+0x180` | look-at point (target position + offset) | | player position + (0, 0, 1.4) |
| `+0x200` | the target's offset (set by `CamSetupFollow`) | | |
| `+0x210`-`+0x21c` | look-at offset from the target's feet | (0, 0, 1.4) | (0, 0, 1.4) |
| `+0x300` | minimum distance | 4 | 3 |
| `+0x304` | maximum distance | 9 | 6.6 |
| `+0x308` | default distance (also written to `+0x344` and `+0x32c`) | 6 | 4.8 |
| `+0x30c` (and `+0x3b4`) | pitch, radians | 15° | 0.2269 (13°) |
| `+0x310` (and `+0x394`) | field of view, degrees | 65 | 65 |
| `+0x32c` / `+0x330` | the leash band: default and default + min(0.5, max − min) (also `+0x344` / `+0x348`) | 6 / 6.5 | 3.0 / 3.5 (changed by the tutorial's calls, inferred) |
| `+0x320` | handle of a human or object the camera keeps in view | | |
| `+0x33c` / `+0x340` | the hard band: the leash band widened by max(5%, 0.2 m) and max(6%, 0.35 m) | | |
| `+0x350` | heading target (a direction; −FLT_MAX for none) | | |
| `+0x354` | pitch override (−FLT_MAX for none) | | |
| `+0x358` | right-stick yaw rate, rad/s | | |
| `+0x368` | stick hold timer: 0.334 s after any camera input | | |
| `+0x380` | recovered distance (eases 10% per update) | | |
| `+0x388` | position lag: share of the wanted move covered per update | 0.22 | |
| `+0x3ac` | an upper pitch limit | 50° | |
| `+0x3b0` | a lower pitch limit: `atan((1 − offset.z) / max)`, at least −20° | −20° | |
| `+0x3b8` | right-stick pitch rate, rad/s | | |
| `+0x400` | zoom distance: minimum, default or maximum | 6.5 | |
| `+0x40c` | timed-move timer, seconds (heading target, pitch override) | | |
| `+0x434` / `+0x438` | side factors: 1 when clear, toward 0.125 when blocked | | |
| `+0x444` | number of targets | | |
| `+0x418` | | 0.06 | |
| `+0x440` | the current distance | | 1.85 standing (runtime) |
| slot `+0x1a4` | near plane | 0.1 | 0.1 |
| `0x005148a0` (global) | slow-motion factor | | 0.2 |

The far clip is 115 for this camera ([The streamed world](world.md#player-camera)).

### `level99`'s values {#level99-values}

`CameraCreateFollow` passes the `global.lua` constants ([Scripts](scripting.md#cameras-from-lua)): minimum 3, maximum
6.6, default 4.8, pitch 13°, field of view 65, near plane 0.1, offset (0, 0, 1.4), slow motion 0.2. Confirmed
(runtime) in the object above.

## Behaviour

### Setting up {#setting-up}

1. `CamSetupFollow(name, target)` (`0x0011bfa8`): get the player's `Cam_Follow`, store the target (slot `+0x1bc`),
   place the camera at the target's position plus the offset at `+0x200`, call `0x00124f38(180, -1)` (a reset of its
   heading and state, inferred) and return the camera's handle. Confirmed (code).
2. `CfgFollowCamera(min, max, default, pitchDegrees, fov, near, {offset}, slowmo)` (`0x0011c0b8`): the three
   distance setters (each clamps against the others; the maximum also recomputes the lower pitch limit `+0x3b0`), the
   pitch in radians, the field of view, the offset, the near plane (slot `+0x1a4`) and the global slow-motion factor.
   Confirmed (code).
3. `CameraMakeActive(camera, seconds, ...)` (`0x0011b770` → `0x0011ee08`): with 0 seconds the camera becomes current
   at once; otherwise the blend camera (type 5, `0x0011fac8`) runs between the old and the new one. `CameraReset`
   (`0x00365a10`) then calls the camera's slot `+0x13c`. Confirmed (code).

### The follow update {#update}

`0x0012ae58` runs once per character step (30 Hz; it stores its `dt` argument at `+0x1a0`, where the heading rules read
it) for each follow camera. It is a **leash camera**: the camera does not sit at a fixed spot behind the player but
stays where it is until the player drags it, and a few separate rules turn it. The steps below are in the order the
update runs them. Confirmed (code) at the cited addresses unless marked; angles are given in degrees, the code holds
radians.

1. **Look-at point** (`+0x180`): the target's position plus the offset `+0x210` (runtime: feet + 1.4 m). The offset
   itself eases toward its wanted value by 15% per update (`+0x200` toward `+0x210`). In one target state (the flag the
   update tests is not traced) the look-at height is instead feet + 1.75 − 0.1 = 1.65 m.
2. **Field of view** eases toward `+0x394` at `+0x39c` degrees per second, at most 7.5, or over the timed move's
   remaining time `+0x40c` when one runs.
3. **Right stick** (`0x00129050`, [below](#right-stick)) gives a yaw rate `+0x358` and a pitch rate `+0x3b8`; the zoom
   button steps the distance.
4. **Auto-follow** (`0x00129c78`): with the player moving and no right-stick input, the camera swings round behind the
   player's facing ([Heading](#heading)).
5. **Leash** to the distance band `+0x32c`-`+0x330`: when the distance from the camera to the look-at point leaves the
   band, the camera is moved along that line back to the nearer edge. `level99`'s band is 3.0-3.5 m.
6. **Pitch toward its target** `+0x3b4` (`0x0012d4e8`, a rotation about the look-at point clamped to
   `[+0x3b0, +0x3ac]`); see [Pitch](#pitch).
7. **Look-at height smoothing** while the camera is in its "height hold" state (`+0x453` set; entered when the target
   is high above the camera's ground, inferred): the look-at height moves 30% of the way per update toward the wanted
   one (40.5% or 48% in the two special modes returned by `0x00125588`), scaled by `+0x398`.
8. **A heading target** `+0x350`, a direction the camera must face (set by the centre button, scenes and scripts): the
   camera turns toward it by `angle × dt / +0x40c`, so it arrives as the timer `+0x40c` runs out, and the target is
   cleared once within 0.1° (`0x3ae4c389`).
9. **Pitch input**: with a right-stick pitch rate, the target pitch moves by `rate × dt`, clamped to the two limits, and
   the camera follows it. A pitch override `+0x354` (scripts) is reached at once, at 10°/s, or over `+0x40c`. With
   neither, the pitch returns to its target at most 85°/s (`1.4835` rad/s).
10. **Head look** (`0x00128b20`): while the right stick turns the camera, the player's head turns toward the camera's
    heading if it is within 150° of the body's facing.
11. **Position lag**: the new position is the old one plus `+0x388` × the move this update wanted, so the camera covers
    22% of the distance per update (27% while recovering from a collision; 0.8 or 0.05 in two locked-on states). The
    value is chosen by the collision step of the previous update ([below](#collision)). Moves under 10⁻⁵ m are dropped.
12. **Hard band**: the camera is then kept between `+0x33c` and `+0x340`, which are the leash band widened by
    max(5% of the near edge, 0.2 m) and max(6% of the far edge, 0.35 m); when they shrink they ease back by 1% per
    update. While the target is in a task of type 11 (a grapple, speculative) the band is the leash band × 0.85 and
    × 1.2; while a lock-on button is held the far edge is doubled, at most 1.1 × the maximum distance.
13. **World collision** (`0x00130990`, [below](#collision)), then the camera is placed (vtable slot `+0x1bc`). A last
    line-of-sight test (`0x00337028`) between the look-at point and the camera puts the previous position back if it
    fails.
14. **Timers**: `+0x40c`, `+0x410` and `+0x414` count down by `dt`; when `+0x40c` reaches 0 the timed move and its
    heading target end. A fade timer `+0x424` sets the alpha byte `+0x1b4` of the two objects at `0x005fdeb8` and
    `0x005fdebc` to `min(10 × t, 1) × 225` (inferred: the player models fading when the camera is close).

With 2 targets (co-op) two factors become 1.9 / 0.3 instead of 2.0 / 0.25; they are passed to the collision step
(a probe height and a sphere-radius factor, inferred).

### Heading {#heading}

The yaw rotation is done by `0x0012d688(angle)`, about the look-at point. Four rules call it, confirmed (code):

- **Auto-follow** (`0x0012a400`), the default: let *a* be the angle between the camera's horizontal forward and the
  player's facing. Nothing happens below 22.5° or above 157.5°, so running towards the camera does not spin it. The
  rate is `(a − 22.5°) × 2.667` per second below 45° (0 to 60°/s), 60°/s from 45° to 135°, and above 135° it falls
  from 120°/s back to 60°/s at 157.5°. Each update turns by `min(a, rate × dt)` toward the facing.
- **Auto-centre option** (`0x00129f88`, used instead when the per-pad option bytes `0x0050b240` and `0x0050b248` are
  both set; they are 1 in the `level99` save used at runtime): nothing below 22.5°; from 22.5° to 90° the rate is
  `(a − 45°) × 2.444 + 45°` per second (negative below 26.6°, so the step is then a small turn the other way; 45°/s at
  45°, 155°/s at 90°); 200°/s (`3.4907` rad/s) from 90° to 100°; from 100° to 157.5° it falls linearly
  (`(157.5° − a) × 2.435 + 60°`) from 200°/s to 60°/s; beyond 157.5° only when the player is moving (an argument
  the update sets from the gait). Each update turns by `min(a, rate × dt)` toward the facing. The cosine of each
  threshold is computed with `0x004b8a70` (cosine, inferred from the thresholds' use).
- **When either rule runs** (`0x0012ae58`, the call at `0x0012bd80`): one target (`+0x444` = 1), the target passes
  `0x00123500` (not in states `0x180050000` of record word `+0x00`), no camera flags in `+0x460 & 0xffff0000`, nothing
  watched (`+0x320` = −1), no yaw from earlier steps this update, and no camera input in the last 0.334 s
  (`+0x368` = 0). Inside `0x00129c78` the default rule also needs the "moving" argument, which is set when the
  target's gait `+0x1a8` is 4 or 5 (run, sprint) with record flags `+0x08` clear (`0x00223a60`, `0x00223a98`); the
  auto-centre rule needs `+0x474` = 1, `+0x455` = 0, no right-stick input, the top animation task's clip without
  descriptor flag `0x8000` (`0x00175be8`), and none of the human flags `0x18003ff0`.
- **Keep the target in view** (`0x0012e170(factor)`, called with 0.4 or 0.25): when the human or object the camera
  watches (`+0x320`) leaves `fov × factor` of the view, the camera yaws 35% of the excess per update, at most 270°/s
  (`4.712` rad/s). A ray test (mask `0x200`) skips it when the world hides the target.
- **The right stick** (rate `+0x358`) and the **heading target** (`+0x350`, step 8 above).

### Pitch {#pitch}

- The target pitch `+0x3b4` is 13° in `level99` (`CfgFollowCamera`).
- The **lower limit** `+0x3b0 = atan((1 − offset.z) / far)`, at least −20° (`0x0012d7a8`, run when the distance band
  moves); the target pitch is raised to it.
- The **upper limit** `+0x3ac` follows the zoom step (`0x001254f0`): 50° at the minimum distance, 40° above the
  default, and 30° at the default when the camera option `0x0050b19c` is 1 (50° otherwise). The same function sets the
  zoom distance `+0x400` to the minimum, maximum or default.
- Without input the pitch is driven back to the target at most 85°/s (step 9).

### The right stick {#right-stick}

`0x00129050` reads the right stick's raw bytes from the pad record (`0x005dd810 + pad × 0x50`, `+0x1a` x and `+0x1b`
y, 0-255 with 128 at rest), confirmed (code). It acts only on a player's current camera, and only when the per-pad
camera options `0x0050b1b0` and `0x0050b1b8` allow it.

| Input | Raw range | Effect |
| --- | --- | --- |
| Stick left | x ≤ 64 | yaw rate `+0x358` from 150°/s at x = 0 to 60°/s at x = 64 (`2.618 − (x / 64) × π/2` rad/s) |
| Stick right | x ≥ 176 | yaw rate from −60°/s at x = 176 to −150°/s at x = 255 |
| Stick up | y ≤ 8 | pitch rate `+0x3b8` from 85°/s at y = 0 to 55°/s at y = 8 |
| Stick down | y ≥ 232 | pitch rate from −55°/s at y = 232 to −85°/s at y = 255 |
| Between | | no turn: a dead zone of ±48 horizontally and nearly the whole travel vertically |

- Inversion flags per pad swap the signs: `0x0050b1f0` for yaw, `0x0050b1f8` for pitch.
- Any input sets `+0x368` to 0.334 s, which holds off the automatic rules.
- The yaw rate is also capped at `+0x434` / `+0x438` × 150°/s, two side factors the collision step lowers when the
  camera is blocked on that side.
- The **zoom button** (configured at `0x0050b234`; a tap shorter than 0.17 s when it shares a button with
  `0x0050b230`) moves the band by `+0x400 − near` (`0x0012d7a8`) and steps `+0x400` through minimum, default and
  maximum (`0x001254f0`).
- The **centre button** (`0x0050b22c`) sets the heading target `+0x350` to the player's facing with `+0x40c` = 0.2 s,
  so the camera swings behind the player in 0.2 s.

### World collision {#collision}

`0x00130990` (about 3,000 lines decompiled) runs after the camera's wanted position is known. Confirmed (code) for the
calls and constants; the overall reading is inferred:

- **A height ray** down from the look-at point (mask `0x200`) catches a low ceiling and lowers the camera.
- **Side probes**: rays from the look-at point fan out to both sides of the camera, at a probe angle of 7° at the near
  distance down to 4° at the far one (`7° − 3° × t`, `t` the position in the distance band), at fractions 1.0, 0.7,
  0.5, 0.3 and 0.15 of the distance. The free angle found on each side is limited to 3 × the probe angle.
- **Swinging away**: when one side is clearly freer (the two differ by more than 7.5°), the camera yaws toward it by
  20% of the needed angle per update. When the view is fully blocked it turns toward the target direction at up to
  480°/s (`8.378` rad/s); a latch (`+0x479`: 1 one way, 2 the other) stops it reversing, and when it would reverse it
  gives up and remembers the target's position (`+0x2c0`).
- **Sphere pushes** (`CollisionMesh_SpherePush`) with radius 1.0 and half the wanted distance push the camera out of
  walls; a small sway (`+0x3f8`, at most 0.028 rad, from a random value between 0.125 and 0.25) is added while pushed.
- **The smoothing it chooses for the next update** (`+0x388`): 0.22 normally and 0.27 while recovering. When nothing
  is in the way the value moves toward its new setting by only 0.5% per update.
- **Recovered distance** `+0x380` eases toward the distance the probes allow by 10% per update. The side factors
  `+0x434` / `+0x438` drop toward 0.125 on a blocked side and go back to 1 when it is clear.

**At runtime** (stick magnitude 1.0, `level99` checkpoint 1), after running for 1.2 s (a run needs a stick magnitude of
at least 0.95, [method](../guides/research-workflow.md#driving-pcsx2)): the camera was 3.16 m from the look-at point
(3.08 m horizontally) and 0.70 m above it, a pitch of about −12.8°, which is the 3.0-3.5 m leash band and the 13° target
pitch. Confirmed (runtime).

### Runtime checks {#runtime-checks}

PCSX2 2.9.94, `level99` checkpoint 1, Rembrandt, read over PINE once per update; stick magnitudes by the
[stick-table method](../guides/research-workflow.md#driving-pcsx2), right stick by the keyboard (full deflection).
"Wanted position" is `+0x250`, the camera's own position `+0x10`. Confirmed (runtime) unless marked:

- **Position lag.** With the player moved 2 m away in one write, the gap between the camera and its wanted position
  shrank by a factor of 0.78 per update (0.120, 0.094, 0.074, 0.057, 0.045 m), with `+0x388` reading 0.226: 22% per
  update. `+0x388` was 0.157 standing at the start spot (pulled in by a wall) and climbed to 0.227 within 0.5 s of
  running into the open, the 0.5%-per-update drift of [World collision](#collision).
- **Leash band.** While running, the wanted position stays 3.50 m from the look-at point (the band's far edge);
  standing, it stays wherever it was inside the band (3.00 m at the start). At the start spot the camera itself is
  pulled in to 1.85 m by the walls and recovers toward 3.5 m over about 1.5 s of running.
- **Right stick yaw.** Full deflection right (raw x = 255): the wanted position turns by exactly 5.00° per update
  (150°/s) from the first update; the camera's own yaw follows with the position lag, reaching about 5° per update
  after 0.4 s, and coasts for a few updates after release. The 0.334 s hold timer `+0x368` read 0.301 (one update
  counted down) while the stick was held.
- **Right stick pitch.** Full deflection up: `+0x3b8` = 85°/s; the pitch target `+0x3b4` stopped at the upper limit
  `+0x3ac` = 30°, which is the option `0x0050b19c` = 1 case at the default zoom distance 4.8. The view's pitch eased
  from 15° to 30° behind it.
- **Auto-follow was not seen.** Running at 7.8 m/s (gait 4) with the facing held 63-78° away from the view for
  1.5 s, in the open, the wanted position turned only as the moving look-at point dragged it: no rotation of its own,
  with the auto-centre option on (as saved) and with `0x0050b240` written to 0 (the default rule). By the code either
  rule should then turn about 3.4° (auto-centre) or 2° (default) per update. Every gate listed under
  [Heading](#heading) that can be read over PINE passed (`+0x444` = 1, record words `+0x00` and `+0x08` zero,
  `+0x460` = 0, `+0x320` = −1, `+0x368` = 0, `+0x455` = 0, `+0x474` = 1, the run clip's descriptor flags 0); the
  condition that blocks it was not found (the update's locals cannot be read without breakpoints). So in
  `level99` the camera turns only with the right stick and the leash.

### Scenes take the camera and give it back {#scenes}

`level99` starts with `SuperRunScene(IntroScene)` at checkpoint 1 ([Scripts](scripting.md)); `IntroScene` is a table
(`SceneId` `l99_c1`, the humans and objects that take part, `ReturnFunc` = `P1.StartTraining`). The `global.lua`
helpers fill in defaults and call the engine. Confirmed (code) for the script (`global.lua`, read as bytecode) and the
engine at the cited addresses:

1. **`SuperRunScene(t)`** hides the HUD, clears the gang's wanted level, blacks the screen at once
   (`ScreenQueueEffect(1, 0)`), revives the scene's humans, and preloads the scene (`ScenePreload(id,
   "gPlayCutScene")`), keeping `t` in `tblScene[id]`.
2. **`gPlayCutScene(id)`** sets defaults: `Bars` true, `Delay` 0, and when `BlendCam` is not given, **`BlendCam` = −1
   and `FadeIn` true** (a given `BlendCam` sets `FadeIn` false). It joins each human to the scene (`GoalJoinCinematic`,
   or the animation / fixed-scene variants), adds the objects, and calls `ScenePlayCinematic(id, Delay,
   "PreCashTheWorld", Bars, not NoSkip, Looping, Freeze, BlendCam, Final, Chain)`.
3. **Scene start** (`0x00353818` → `0x0039d870`). When the scene has its own camera (scene data `+0x22`), a scene
   camera (type 4, `0x0011e1b0(4, …)`) is made or reused, given the current camera's view (slot `+0xac` → `+0xb4`),
   and **the current camera is pushed** on the camera stack (`0x00120450`; for a blend, locked or other wrapper camera,
   types 5-8, the camera inside it). The scene camera then becomes current at once (`0x0011ee08` with 0 seconds).
   A scene without a camera keeps the current one, saves its position (scene `+0x80`, the camera at `+0x90`) and
   moves it to the scene's anchor.
4. **While a scene camera is current**, `CameraMakeActive` for player 1 does not switch: it replaces the camera on the
   stack, so the script changes what the scene returns to (`0x0011ee08`, when the global at `0x0051489c + 0x410` is
   set; inferred to mean "a scene is playing").
5. **Scene end** (`0x0039f450`): the camera is **popped** (`0x00120488`), reset (slot `+0x13c`, as `CameraReset`) and
   made current with **`BlendCam` seconds**: above 0 the blend camera (type 5) runs between the scene camera and it;
   0 or −1 is a cut. In one game-mode case (the mode object at `0x0015e718` reporting 8) it first takes the scene
   camera's view, field of view and near plane. The scene camera is then released (`0x0011e440`) and **the cameras'
   update runs once with dt = 0.17 s** (`0x0011e878(0.17)`), so the follow camera settles before the next frame. A
   scene without a camera puts the kept camera back at its saved position and resets it (`0x0039ec60`).
6. **The script's end callback** (`global.lua`, run when the scene ends) calls `ReturnFunc` (for the intro,
   `P1.StartTraining`), turns gang spotting back on unless `BlendCam` was 0, and with `FadeIn` fades the screen in over
   0.5 s (`ScreenQueueEffect(0, 0.5)`).

So the intro hands back to the follow camera with a **cut hidden by a 0.5 s fade-in**, the follow camera having been
reset and run for 0.17 s. Confirmed (runtime): after the intro the stack index `0x0050b180` is −1, its slot 0 still
holds the popped follow camera, and the follow camera is the current and previous camera of player 1.

## Coney's implementation

`src/camera/follow_camera.*` is the follow camera of `Cam_Follow_Update` (`0x0012ae58`), stepped after the human
by `src/human/player.*` and drawn by `--play-level` ([Building](../guides/building.md#playing-a-level)):

- the look-at point is the feet + 1.4 m; the wanted position stays put unless its distance leaves the 3.0-3.5 m band,
  then moves along that line to the band; the camera moves 22% of the way to it each step, held inside a hard band
  of 2.8-3.85 m;
- the pitch eases toward 13° at 85°/s, between the lower limit (the larger of -20° and the slope of 0.4 m over
  6.6 m) and the upper;
- the right stick turns the wanted position at the raw rates (yaw up to 150°/s outside the ±48 dead zone, pitch near
  the ends of the travel) and holds off for 0.334 s after any input;
- the camera's forward vector turns the player's stick before the human sees it.

The world viewer keeps its own free camera with the player camera's lens
([The streamed world](world.md#coneys-implementation)).

**Coney choices** where the research is silent:

- **No auto-follow**: the camera does not swing behind the player by itself, as measured in level99
  ([Runtime checks](#runtime-checks)).
- **Collision** is one ray (collision mask `0x200`) from the look-at point to the camera; a hit pulls the camera to
  0.2 m short of it, never nearer than 0.5 m. The side probes and swing-away rules are not implemented.
- **The upper pitch limit** is fixed at 30°, the value read at runtime with the camera option set; the zoom
  levels that would change it are not modelled.
- **A fresh camera** (at the start, or after the player is put back) sits behind the player at 3.0 m and 13°.

## Notes for implementers

- **A first follow camera** that matches the numbers: look at the player's feet + 1.4 m; keep the camera where it is
  unless its distance to the look-at point leaves the 3.0-3.5 m band, then move it along that line to the band; move
  22% of the way to the wanted position each 30 Hz step; hold a 13° pitch; 65° horizontal field of view, near 0.1,
  far 115.
- **Heading**: in `level99` as measured the camera does not swing behind the player by itself
  ([Runtime checks](#runtime-checks)); the leash alone turns it as the player runs across the view. Implement the
  leash and the right stick first; the auto-follow rates ([Heading](#heading)) can be added behind an option once
  the condition that enables them is known. The player's stick is turned by the camera's heading before it
  reaches the character ([Characters](characters.md#input)), so the camera must ease, never snap.
- **Right stick**: yaw 60-150°/s outside a ±48 raw dead zone, applied to the wanted position at once (the lag
  smooths it); pitch only near the ends of the travel; 0.334 s of no auto-follow after any input.
- **Collision**: start with a ray from the look-at point and pull in to the hit; the side probes and swing-away rules in
  [World collision](#collision) can come later.
- **Update order**: the cameras update at the end of the characters' 30 Hz step, after movement
  ([Characters](characters.md#update)), and the device takes the lens once per frame.
- Keep the camera deterministic (no real time) so the test mode can compare frames.

## Open questions

- **The tutorial's camera calls**: which call set `+0x32c` / `+0x330` to 3.0 / 3.5 (probably `P1.SetupCam` in
  `level99_combat.lua` through `CamSetFollowZoom`; inferred).
- **Why auto-follow did not run** at runtime ([Runtime checks](#runtime-checks)): a gate not yet identified, perhaps
  in the update's locals (`sp+0x1c8`, the `0x0012e9a8` look-at step) or a mode the tutorial sets. A breakpoint at
  `0x0012bd80` would settle it.
- **The collision step** (`0x00130990`) in full (Coney casts one ray): the exact probe pattern, its margins, when
  the height ray lowers the camera, and what `+0x10a`-`+0x10c` (side angle history) feed.
- **The target state** that lifts the look-at point to 1.65 m, and the two modes of `0x00125588`.
- **The slow-motion factor** `0x005148a0`: what slows down, and when.
- **Scenes**: the scene camera's own update (type 4) and the "a scene is playing" flag at `0x0051489c + 0x410` (see
  [Scenes](#scenes)).
