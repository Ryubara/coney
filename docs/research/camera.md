# Camera (the follow camera)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Runtime claims were made in
PCSX2 2.9.94 (2026-10-04) by reading the camera object over PINE in `level99`, checkpoint 1, and say so.

## Purpose

The camera behind the player in normal play: how a level script creates it, what it is configured with, how it is
made current, and what its update does as far as it has been read. It is what the first playable milestone needs to
show the player walking. The lens (field of view, clip planes, view window) is on
[The streamed world](world.md#player-camera); the other camera kinds (locked, scene, mugging, power) are not covered.

In one paragraph: there is one **`Cam_Follow`** object per player, a singleton made on first use. `level99.lua`
creates the follow camera with the `global.lua` helper `CameraCreateFollow("follow", player)`, which sets it up on the
player (`CamSetupFollow`) and configures it (`CfgFollowCamera`): distance 3 to 6.6 m (4.8 by default), a pitch of 13°,
a 65° field of view, a near plane of 0.1 and a look-at point 1.4 m above the player's feet. `CameraMakeActive` makes it
current with no blend. Each update the camera keeps its look-at point on the player, holds its distance and pitch,
eases its heading round behind the player's motion and pulls in when the world is in the way, using ray casts and
sphere pushes against the collision mesh.

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
| `0x00130990` | `Cam_Follow` world collision | ray casts and sphere pushes | confirmed (code) for the calls |
| `0x0012a7d8`, `0x0012e170` | height probes | short ray casts (flag `0x200`) | confirmed (code) for the calls |
| `0x0011e878(dt)` | cameras' update | run at the end of the characters' update ([Characters](characters.md#update)) | confirmed (code) |
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
| `+0x32c` / `+0x330` | a distance pair: default and default + min(0.5, max − min) (also `+0x344` / `+0x348`) | 6 / 6.5 | 3.0 / 3.5 (changed by the tutorial's calls, inferred) |
| `+0x388` | | 0.22 | |
| `+0x3ac` | an upper pitch limit | 50° | |
| `+0x3b0` | a lower pitch limit: `atan((1 − offset.z) / max)`, at least −20° | −20° | |
| `+0x400` | | 6.5 | |
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

`0x0012ae58` (about 1,500 lines of decompiled code) was only skimmed. What is known, confirmed (code) unless marked:

- It reads the target human's state through the same predicates as the locomotion (walking, running, sprinting,
  `0x00223a20`-`0x00223a98`) and changes its distance and height targets with them (factors such as 1.9 / 0.3 and
  2.0 / 0.25 appear for two cases; their meaning is not traced).
- It keeps the look-at point at the target's position plus the offset (`+0x180`; confirmed (runtime)).
- **World collision** (`0x00130990`): ray casts against the collision mesh (`CollisionMesh_RayCast`, flag `0x200`)
  between the look-at point and the wanted camera position, a ray through `WorldManager_RayCast`, and sphere pushes
  (`CollisionMesh_SpherePush`) with radii of 1.0 and 2.25 and one scaled by the distance ([Collision](collision.md)).
  The result is the current distance `+0x440`, shorter than the default when something is in the way (1.85 instead of
  4.8 with the player standing at the tutorial's start; inferred that a wall caused it).
- Height probes (`0x0012a7d8`, `0x0012e170`) cast short rays (flag `0x200`) to follow steps and slopes (inferred).
- It finishes by moving the camera (vtable slot `+0x1bc`) and copying its matrix.

**At runtime**, after running for 1.2 s: the camera was 3.16 m from the look-at point (3.08 m horizontally) and 0.70 m
above it, a pitch of about −12.8°, matching the 13° pitch at a distance between the minimum and the default.
Confirmed (runtime).

## Coney's implementation

None yet. Coney's world viewer has a free camera with the player camera's lens
([The streamed world](world.md#coneys-implementation)).

## Notes for implementers

- **A first follow camera** that matches the numbers: look at the player's feet + 1.4 m, stay at 4.8 m (between 3
  and 6.6) behind the player, pitched 13° down, 65° horizontal field of view, near 0.1, far 115; pull in along the
  view ray to the first collision hit (minus a margin) when the world is in the way.
- **Heading**: the player's stick is turned by the camera's heading before it reaches the character
  ([Characters](characters.md#input)), so the camera must not snap behind the player each frame or the controls
  would spin; ease it.
- **Update order**: the cameras update at the end of the characters' 30 Hz step, after movement
  ([Characters](characters.md#update)), and the device takes the lens once per frame.
- Keep the camera deterministic (no real time) so the test mode can compare frames.

## Open questions

- **The follow update** (`0x0012ae58`) in full: how the heading eases behind the player, the smoothing rates, what
  `+0x32c` / `+0x330`, `+0x388`, `+0x400` and `+0x418` do, and how the distance recovers after a collision.
- **The tutorial's camera calls**: which call set `+0x32c` / `+0x330` to 3.0 / 3.5 (probably `P1.SetupCam` in
  `level99_combat.lua` through `CamSetFollowZoom`; inferred).
- **Right stick control**: whether the player can turn or pitch the camera, and how.
- **The slow-motion factor** `0x005148a0`: what slows down, and when.
- **Scenes**: how `SuperRunScene` takes the camera from the follow camera and gives it back.
