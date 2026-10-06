# Camera (the follow camera)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Runtime claims were made in
PCSX2 2.9.94 (2026-10-04) by reading the camera object over PINE in `level99`, checkpoint 1, and say so; those of
[In the street](#street) (2026-10-05) in the street saves of `level99`'s world, read every update while a scripted
pad played ([Feel comparison](feel.md)); those of 2026-10-06 from the `level99` checkpoint 1 state files' memory and a
copy of slot 6 driven over PINE (L1 held, no stick).

## Purpose

The camera behind the player in normal play: how a level script creates it, what it is configured with, how it is
made current, and what its update does as far as it has been read. It is what the first playable milestone needs to
show the player walking. The lens (field of view, clip planes, view window) is on
[The streamed world](world.md#player-camera). Of the other camera kinds ([Types](#types)) only the blend and the locked
camera, which `level99` uses, are covered.

In one paragraph: there is one **`Cam_Follow`** object per player, a singleton made on first use. `level99.lua` creates
the follow camera with the `global.lua` helper `CameraCreateFollow("follow", player)`, which sets it up on the player
(`CamSetupFollow`) and configures it (`CfgFollowCamera`): distance 3 to 6.6 m (4.8 by default), a pitch of 13°, a 65°
field of view, a near plane of 0.1 and a look-at point 1.4 m above the player's feet. `CameraMakeActive` makes it
current with no blend. It is a leash camera: each update it keeps its look-at point on the player, is dragged back into
a band 0.5 m deep (3.0-3.5 m after `CfgFollowCamera`, which starts it at the minimum; 4.8-5.3 m from checkpoint 2, where
`CamSetFollowZoom(1)` moves it to the default, [Script calls](#script-calls)) when the player moves away, covers 22% of
its wanted move per update, swings round toward the player's facing once the angle passes 22.5° (seen in the street, not
at checkpoint 1, [Runtime checks](#runtime-checks)), holds a 13° pitch, pulls in to 3.0-3.5 m and lowers its pitch to 7°
while the player sprints, turns with the right stick at 60-150°/s, and swings or pulls in when the world is in the way,
using ray casts and sphere pushes against the collision mesh. While the player holds a lock-on in a fight it pulls in to
2.4-2.9 m and frames the enemy 27° off centre ([Combat camera](#combat-camera)). The tutorial's cut-aways are locked
cameras reached and left with 1 s blends ([Blends](#blends)).

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
| `0x0011c470` | `Camera_SetFollowZoom` (`CamSetFollowZoom`) | band to the minimum, default or maximum | confirmed (code) |
| `0x0011c3b8` | `Camera_SetFollowPitch` (`CamSetFollowAngle`) | target pitch, reached at once | confirmed (code) |
| `0x0011dcf0` | `Camera_SetFollowSecondary` (`CamSetSecondary`) | a human to keep in view | confirmed (code) |
| `0x0011c270` | `Camera_TargetList` (`CamTarget`) | the shared target list `0x005d91a8` | confirmed (code) |
| `0x00124d00` / `0x00124f38` | follow reset (vtable `+0x13c`) / place behind the target | | confirmed (code) |
| `0x00125e50` | follow activation step | place at the band, snap the look-at point | confirmed (code) |
| `0x00143078` / `0x00143590` | blend camera start / update | | confirmed (code) |
| `0x00135680` | locked camera update | | confirmed (code) |
| `0x00233c50` / `0x0012e9a8` | combat camera test / enemy framing | | confirmed (code) |
| `0x001210f8` / `0x00121298` | shake start / shake and rumble update | | confirmed (code) |
| `0x0041ab30` / `0x0041ab60` | slow motion on / off | the characters' step `0x005102cc` | confirmed (code) |

## Data

### Types {#types}

Every camera is one of fourteen classes; each class's vtable function word `+0x1ec` (the `+0x1e8` slot) returns its
type, confirmed (code) for all fourteen ([the list](../references/cameras.md#type), with tags, vtables and sizes).
The factory `0x0011e1b0(type, name, player)` allocates types 0, 1, 4 and 0x10 afresh; for 2, 3, 5, 7 and 8 it calls a
getter that makes the camera on first use and keeps it (per player in `0x005d9158`-`0x005d918c`, or one in
`0x0050b16c`); for 0xc and 0xd it calls the getter with "do not make" and so only returns an existing one. Types 6,
9 and 0xb are never made by the factory, only by their getters. No class returns 10, 14 or 15. confirmed (code).

### Switches {#switches}

`CamEnable(switch, on, player)` (`Camera_EnableFeature`, `0x0011de58`) writes one of fourteen flags; a level's camera
reset `0x00122b80` sets them back (switches 1 and 13 to 0, the rest to 1), confirmed (code). The flags, scopes,
defaults and readers are on [the list](../references/cameras.md#switch). What the readers show, inferred from the
code at the cited addresses:

- **Two-player views.** `0x00121888` decides each update whether each player's view is shown. With switch 3 on and
  two players, a view is kept only while its player still counts (`0x00123500`: 0 while `0x00227e60` holds for the
  human, and for one state of `0x00223b70` unless unlock 6/15 is set), so the other player gets the whole screen;
  switch 13 keeps both views. With switch 3 off both views always show. Switches 4 and 12 apply the same test to
  the follow and rail cameras' targets.
- **Shake** (`0x00121298`): the shake amount drives the pad's rumble byte (pad record `+0x41`) in every case; the
  random view offset is added only while switch 6 is on.
- **Right stick** (`0x00129050`): returns at once while `0x0050b1b0[player]` or switch 0 (`0x0050b1b8[player]`) is
  0, so neither the stick nor the zoom buttons act.
- **Look-behind** (switch 11, `0x0050b23c`, confirmed (code) at `0x0012b4b4`): passed to `0x00129050` as its eighth
  argument (forced to 0 on some updates by a local, not traced). With it 0 the reverse-camera button
  (`0x0050b230`) never turns the view round, and a press on a button shared with the zoom counts as a zoom tap.
  `Human_SetWheelchairControl` clears it.
- **Power camera** (switch 9, `0x0050b1d4`): an animation event of type `0x39` (`0x00101dd8`) switches a player whose
  current camera is the follow, rail, fixed or power camera to the power camera (type 6) with the event's shot id;
  with the switch off the event is ignored. `level99_lesson2.lua` turns it off while the flash dealer respawns.
- Switches 2, 7 and 8 are read only by the rail camera (type 9), which `level99` never makes; switch 7 adds a lead
  along the target's way (`+0x35c`) to the rail camera's look-at point (`0x0013e708`, inferred).
- Switches 3 and 4 change nothing with one player: both only gate the second player's view and target
  (`0x00121888`, `0x001282a0`; inferred). `level99` turns them off around its cut-aways and fights.

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
| `+0x32c` / `+0x330` | the leash band: default and default + min(0.5, max − min) (also `+0x344` / `+0x348`) | 6 / 6.5 | 3.0 / 3.5 at checkpoint 1; **4.8 / 5.3** in the street, and 3.0 / 3.5 while sprinting ([In the street](#street)) |
| `+0x320` / `+0x3fc` | handle of a human or object the camera keeps in view (`CamSetSecondary`) / its range | | |
| `+0x3cc` | the band's near edge saved by the [combat camera](#combat-camera) (0 for none) | | 0 |
| `+0x46f` | the combat camera is on | | 0; 1 with L1 held at a target |
| `+0x33c` / `+0x340` | the hard band: the leash band widened by max(5%, 0.2 m) and max(6%, 0.35 m) | | |
| `+0x34c` | the band's wanted near edge: `+0x32c` eases toward it ([Sprint zoom](#sprint-zoom)); −1 for none | | 3.0 while sprinting, else −1 |
| `+0x36c` | seconds the target has run or sprinted (zeroed when it stops on the ground) | | |
| `+0x3c8` | a band saved by another zoom (`0x00126558` / `0x00126878`, not the sprint); while it is set the sprint zoom leaves the band alone | 0 | 0 |
| `+0x3e0` / `+0x3e8` / `+0x3e4` | the sprint zoom's saved band near edge, zoom distance and target pitch (`+0x3e4` −FLT_MAX for none) | | 4.8 / 6.6 / 13° |
| `+0x448` | sprint zoom state: 0 off, 1 zoomed in, otherwise the game time (ms) at which to zoom back out | 0 | |
| `+0x45b` / `+0x45c` / `+0x45d` | `+0x45b`: the collision step's main ray was blocked this update; `+0x45c`: a sticky copy of it; `+0x45d`: a sticky "the view was blocked" latch. The latches hold until the player stops, and auto-follow stays off while they are set ([Heading](#heading)) | 0 | 0 in the street; 1 for the whole run at checkpoint 1 |
| `+0x466` / `+0x467` / `+0x468` | sprint zoom latched / armed for this sprint / enabled (`CamEnable(5, on)`) | | 0 / 0 / 1 |
| `+0x474` | auto-centre latch: cleared while the stick points more than 157.5° from up, set again when the target moves | | 1 |
| `+0x350` | heading target (a direction; −FLT_MAX for none) | | |
| `+0x354` | pitch override (−FLT_MAX for none) | | |
| `+0x358` | right-stick yaw rate, rad/s | | |
| `+0x368` | stick hold timer: 0.334 s after any camera input | | |
| `+0x380` | recovered distance (eases 10% per update) | | |
| `+0x388` | position lag: share of the wanted move covered per update | 0.22 | |
| `+0x398` | scale of the height hold's 30% ease (1 normally; 0.25 after `0x00125888`) | | 1 |
| `+0x3ac` | an upper pitch limit | 50° | 30° at checkpoint 1; **40°** in the street (band 4.8-5.3) |
| `+0x3b0` | a lower pitch limit: `atan((1 − offset.z) / max)`, at least −20° | −20° | |
| `+0x3b8` | right-stick pitch rate, rad/s | | |
| `+0x400` | zoom step: the preset the zoom button goes to **next** (the default while the band is at the minimum, the maximum while it is at the default, the minimum while it is at the maximum) | 6.5 | 4.8 at checkpoint 1; 6.6 from checkpoint 2 |
| `+0x40c` | timed-move timer, seconds (heading target, pitch override, and the sprint zoom's 0.5 s) | | |
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
2. `CfgFollowCamera(min, max, default, pitchDegrees, fov, near, {offset}, slowmo)` (`0x0011c0b8`, player 0's follow
   camera only): the three distance setters (each clamps against the others; the default also sets the band to
   default .. default + min(0.5, max − min); the maximum recomputes the lower pitch limit `+0x3b0`), the pitch as both
   the configured and the target pitch, the field of view as both the configured and the wanted one, then
   `0x00125888`: the view is turned to the pitch at once and the field of view eases to the new value over 1 s
   (`+0x39c` = the difference per second). Then the offset (`+0x210`), the near plane (slot `+0x1a4`) and the
   slow-motion factor `0x005148a0`. **Last, with one player camera, the band is moved to the minimum** (3.0-3.5 m) and
   the zoom step to the default, exactly as `CamSetFollowZoom(0)` ([Script calls](#script-calls)); with two, as
   `CamSetFollowZoom(1)`. Confirmed (code); confirmed (runtime): every checkpoint 1 state read band 3.0 / 3.5, zoom
   step 4.8, upper pitch limit 30°.
3. `CameraMakeActive(camera, seconds, ...)` (`0x0011b770` → `0x0011ee08`): with 0 seconds the camera becomes current
   at once; otherwise the blend camera (type 5, `0x0011fac8`) runs between the old and the new one ([Blends](#blends)).
   `CameraReset` (`0x00365a10`) then calls the camera's slot `+0x13c`. Confirmed (code).
4. **Activation** (slot `+0x144`, `0x00125cc0`, whenever the follow camera becomes current directly, including at the
   end of a blend): it recounts the targets, snaps the look-at point (no ease), keeps the camera's direction from the
   look-at point but puts it at its distance clamped to the band, sets the hard band to the band, clears the wanted
   near edge `+0x34c` (−1) and sets the recovered distance `+0x380` to the band's near edge. Confirmed (code).

**`CameraReset` on the follow camera** (`0x00124d00(camera, 1)`), confirmed (code): `0x00124f38(180, −1)` places the
camera **behind the target** (heading 180° from its facing, inferred from `0x001250a8`) at a distance picked from the
current one clamped to the band: the minimum when it is at most halfway from the minimum to the default (zoom step
then the default), the default when at most halfway from the default to the maximum (zoom step the maximum), else the
maximum − 0.5 (zoom step the minimum); then the previous look-at points are set to the current one, the wanted near
edge cleared, **the target pitch set back to the configured pitch** (13°) and reached at once, the wanted field of view
set back to the configured one (over 1 s), and a line-of-sight test from the target pulls the camera in if the world is
in the way. It does not move the band. For a follow camera the other player's follow camera is reset too.

### The follow update {#update}

`0x0012ae58` runs once per character step (30 Hz; it stores its `dt` argument at `+0x1a0`, where the heading rules read
it) for each follow camera. It is a **leash camera**: the camera does not sit at a fixed spot behind the player but
stays where it is until the player drags it, and a few separate rules turn it. The steps below are in the order the
update runs them. Confirmed (code) at the cited addresses unless marked; angles are given in degrees, the code holds
radians.

1. **Look-at point** (`+0x180`, `0x00127d88`, called with 0 from the update at `0x0012b740`): the target's position
   plus the offset (runtime: feet + 1.4 m). The offset itself eases toward its wanted value by 15% per update
   (`+0x200` toward `+0x210`). Unless the target is jumping or falling (`0x00227f90`), the look-at point's move this
   update is **limited by its length** `d` (in 3D, from the previous look-at point `+0x270`): above 0.8 m it moves
   20% of the way; from 0.4 to 0.8 m it moves `1 − 2 × (d − 0.4)` of the way (100% at 0.4 m, 20% at 0.8 m); below
   0.4 m all of it. So a climb's rise of 1.3-2.6 m is followed at 20% an update until 0.8 m is left, then in two more
   updates, and a jump is followed directly. **While the player is hidden in shadow** (record flag `0x200000`, tested
   by `0x00228168`, set by `Brain_SetHiddenInShadow` `0x0028ee88` → `0x0022ff88`) and not running or sprinting, with
   one target, the look-at height is instead set straight to feet + 1.75 − 0.1 = **1.65 m** (no ease;
   `0x0012b3fc`-`0x0012b7e0`). Confirmed (code). `level99` has no hiding spot on its path (inferred).
2. **Field of view** eases toward `+0x394` at `+0x39c` degrees per second, at most 7.5, or over the timed move's
   remaining time `+0x40c` when one runs.
3. **Right stick** (`0x00129050`, [below](#right-stick)) gives a yaw rate `+0x358` and a pitch rate `+0x3b8`; the zoom
   button steps the distance.
4. **Auto-follow** (`0x00129c78`): with the player moving and no right-stick input, the camera swings round behind the
   player's facing ([Heading](#heading)).
5. **Leash** to the distance band `+0x32c`-`+0x330`: when the distance from the camera to the look-at point leaves the
   band, the camera is moved along that line back to the nearer edge. `level99`'s band is 3.0-3.5 m at checkpoint 1
   and 4.8-5.3 m in the street. The band itself eases toward a wanted near edge `+0x34c` when one is set
   (`0x0012aae0`, run early in the update, before the look-at point): the [sprint zoom](#sprint-zoom).
6. **Pitch toward its target** `+0x3b4` (`0x0012d4e8`, a rotation about the look-at point clamped to
   `[+0x3b0, +0x3ac]`); see [Pitch](#pitch).
7. **Camera height smoothing** while the camera is in its "height hold" state (`+0x453` set; entered when the target is
   high above the camera's ground, inferred): the **wanted position's** height (not the look-at point's) moves 30% of
   the way per update toward the look-at-relative height it held (`+0x378` + `+0x324`), times `+0x398` (1 except after
   `0x00125888`, which sets 0.25). The share is 30% at the close zoom, 40.5% at the default and 48% at the far one:
   `0x00125588` reads the zoom level from the zoom step `+0x400` (with one player camera: 2 far when `+0x400` ≤ the
   minimum, 1 default when `+0x400` > the default, else 0 close; with two, only 1 or 2). Confirmed (code) at
   `0x0012c0c0`-`0x0012c14c` and `0x00125588`. This is the 30% an earlier reading of this page gave for the look-at
   point; the look-at point's own ease is the distance limit of step 1.
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
  both set; they are 1 in the `level99` save used at runtime and 1 by default,
  [Feel](feel.md#details-behind-the-table)): nothing below 22.5°; from 22.5° to 90° the rate is `(a − 45°) × 2.444 +
  45°` per second (negative below 26.6°, so the step is then a small turn the other way; 45°/s at 45°, 155°/s at 90°);
  200°/s (`3.4907` rad/s) from 90° to 100°; from 100° to 157.5° it falls linearly (`(157.5° − a) × 2.435 + 60°`) from
  200°/s to 60°/s, unless the call's fourth argument (the update's local at `sp + 0x1d4`, not traced) is set, which
  keeps 200°/s up to 157.5°; beyond 157.5° only when the target's record has state flag 4 (`0x002265f0(target, 4)`, the
  same flag that can stand in for "moving"). `a` is the angle between the target's facing (its rotation in the transform
  table) and the camera's own horizontal forward (vtable slot `+0x224`, flattened), so the view as placed at the end of
  the previous update. Each update turns by `min(a, rate × dt)` toward the facing. The cosine of each threshold is
  computed with `0x004b8a70` (cosine, inferred from the thresholds' use).
- **When either rule runs** (`0x0012ae58`, the call at `0x0012bd80`): one target (`+0x444` = 1), the target passes
  `0x00123500` (not in states `0x180050000` of record word `+0x00`), no camera flags in `+0x460 & 0xffff0000`, nothing
  watched (`+0x320` = −1), no yaw from earlier steps this update, and no camera input in the last 0.334 s (`+0x368` =
  0). Inside `0x00129c78` (arguments read at the call, `0x0012bd30`-`0x0012bd84`), confirmed (code): the default rule
  needs "running": the target's gait `+0x1a8` is 4 or 5 with no blocking record flags (`0x00223a60`, `0x00223a98`). The
  auto-centre rule needs "**moving**": gait **2, 4 or 5** (walk, run, sprint; `0x00223a40` adds the walk), or state flag
  4 of the record; **not 0, 1 or 3**, so not while the body moves slower than a walk or at a jog's speed. It also needs
  `+0x474` = 1 (cleared on an update whose stick vector in the per-player record, `+0x00` / `+0x04`, points more than
  157.5° from +y, and set again at the start of any update whose gait is not 0, so it blocks only the updates with the
  stick pulled back that far; whether that vector is the camera-turned stick is not traced), `+0x455` = 0, no
  right-stick input, the top animation task's clip without descriptor flag `0x8000` (`0x00175be8`), none of the human
  flags `0x18003ff0`, and the collision bytes `+0x45b` and `+0x45d` clear: the collision step sets `+0x45b` when its
  main ray from the look-at point is blocked ([World collision](#collision)), so **auto-follow stops on the update after
  one in which the view was blocked**. `+0x45d` is a latch: it is set at `0x00132fa4` when the step's local "view
  blocked" (`sp + 0x364`, set at `0x001327a0` from the blocked local `sp + 0x368`, which `0x0013207c`, `0x001321e4` and
  the swing-away give-up `0x001325a4` set) is true, and `+0x45c` copies `+0x45b` the same way (`0x00132fb4`). Both are
  cleared by `0x00124778` (`0x00124980`) and at the end of the collision step (`0x00132fd0`-`0x0013314c`) on an update
  whose view is not blocked (`sp + 0x364` = 0) when either the camera is no longer held in (its wanted distance from the
  look-at point, `sp + 0xc0` to `sp + 0x20`, is within 10⁻⁵ of the distance the step allows, `sp + 0x348`), or the
  allowed distance plus 3% of that gap reaches the wanted distance the step started from (`sp + 0x34c`), or the zoom
  button was pressed this update (the stick step's flag, the step's stack argument `0x4c8`). Otherwise the wanted
  position is moved out to the allowed distance plus 3% of the gap and the latches stay. Confirmed (code); the meaning
  of the locals is inferred. While the player runs, the leash keeps the wanted position at the band's far edge beyond
  what the walls allow, so the latches hold until he stops (runtime). So **one blocked update keeps auto-follow off
  until the player stops** ([Runtime checks](#runtime-checks)). Confirmed (runtime), slot 1, stick 100 % sideways: the
  rule turned 110-129°/s every update at gait 4; with `+0x45b` written to 1 before each of 21 updates it turned 0 on
  each of them, and 129°/s again on the next. The gaits explain what [In the street](#street) saw: no turn in the walk
  and run start clips (gait 0-1 while the walk start moves at 0.76 m/s, 3 in the run start's middle) or the landing clip
  (4.23 m/s, gait 3), and a turn during the run start's first five updates and the run stop's slower updates (gait 2).
- **Keep the target in view** (`0x0012e170(factor, range)`, called with 0.25 and `+0x3fc` for the human
  `CamSetSecondary` gives, `+0x320`, and with 0.4 in one fight case): when the target's direction from the camera is
  more than `fov × factor` from the view's, the camera yaws 35% of the excess per update, at most 270°/s
  (`4.712` rad/s), toward it. With a range above 0 it acts only while the target is within the range and a ray
  (mask `0x200`) from the look-at point reaches it; `level99` passes 0, so no range and no ray. It runs only when
  nothing else turned the camera this update, `+0x463` and `+0x454` are clear, and it replaces auto-follow, which
  needs `+0x320` to be NilHandle. Confirmed (code).
- **The right stick** (rate `+0x358`) and the **heading target** (`+0x350`, step 8 above).

### Pitch {#pitch}

- The target pitch `+0x3b4` is 13° in `level99` (`CfgFollowCamera`).
- The **lower limit** `+0x3b0 = atan((1 − offset.z) / far)`, at least −20° (`0x0012d7a8`, run when the distance band
  moves); the target pitch is raised to it.
- The **upper limit** `+0x3ac` follows the zoom step (`0x001254f0`): 50° at the minimum distance, 40° above the
  default, and 30° at the default when `0x0050b19c` is 1 (50° otherwise). `0x0050b19c` is not an option: it is the
  **number of player cameras**, counted by `0x00122ed0` (written at `0x00123248`) over the camera slots from
  `0x005d9148` up to the player count (`*(0x0051489c) + 0x224`), so 1 means one player and the 30° limit is the
  single-player case. Beside it, `0x0050b198` is the number of split-screen views in use and `0x0050b1a8` /
  `0x0050b1aa` the views' grid (from `0x0050b1a0`). Confirmed (code). The same function sets the
  zoom distance `+0x400` to the minimum, maximum or default.
- Without input the pitch is driven back to the target at most 85°/s (step 9).

### Sprint zoom {#sprint-zoom}

While the player sprints, the camera pulls in to the minimum distance and lowers its target pitch to 7°, and both go
back 250 ms after the sprint ends. Confirmed (code) at the cited addresses and confirmed (runtime) in the street save
(slot 1, stick 100 % and L2, every field below read every update; PCSX2 2.9.94):

1. **Detecting the sprint** (in the update, `0x0012b310`-`0x0012b3f4`): the target's stored gait `+0x1a8` is 5
   with no blocking record flags (`0x00223a98`) → "sprinting" this update (a local, `sp + 0x1e8`). On the first such
   update of a sprint (`+0x36c`, the run time, still 0) the switch `+0x468` is copied to the arm byte `+0x467`.
2. **Latching** (in the update, between the right-stick step and the band's ease, `0x0012b504`-`0x0012b5d8`): while
   sprinting and armed, when the player's **brain** (`0x0021d408`) has no enemies (byte `+0x152`, the one `BrHasEnemies`
   reads) or the nearest of its up to 16 enemies (handles at `+0x164`, `0x004db5b0` with the squared distance of
   `0x00336ce0`, FLT_MAX `0x00548ad8` for none) is closer than **12 m** (squared distance < 144), `+0x467` is cleared
   and `+0x466` set; with `+0x466` set and `+0x448` 0, `+0x448` = 1. On the first update **not** sprinting with `+0x466`
   set: `+0x466` = 0 and `+0x448` = the game time + **250 ms**. (A time left in `+0x448` by an earlier sprint, as in the
   save, also lets the function run; it then has nothing to do until the next sprint.)
3. **The zoom function** `0x00128cf0(camera, sprinting, 0)` runs every update once the game time has passed `+0x448`
   (so at once for 1, after 250 ms for a time; call at `0x0012c5c8`).
    - **Sprinting**, with no other zoom's band saved (`+0x3c8` = 0): the first time, it saves the band's near edge in
      `+0x3e0`, the zoom distance in `+0x3e8` and sets the timer `+0x40c` to **0.5 s**; with one player camera
      (`0x0050b19c` = 1, [Pitch](#pitch)) it sets the wanted near edge `+0x34c` to the **minimum distance** (`+0x300`,
      3.0) and steps the zoom distance to the default (`0x001254f0`, which also sets the upper pitch limit to 30°);
      otherwise (two players) it leaves the band and sets `+0x3e0` to the maximum − 0.5 and `+0x3e8` to the default.
      It then saves the target pitch in `+0x3e4` and moves the target pitch toward **7°** (0.122173 rad): on the
      update the timer reads 0.5 nothing; then by `|7° − pitch| / +0x40c × dt` per update, which is a straight line
      arriving as the timer runs out (without a timer, 30°/s).
    - **Not sprinting**: `+0x34c` = the saved `+0x3e0`, `+0x3e0` = 0, the zoom distance back to `+0x3e8`, `+0x40c` =
      0.5 s; the target pitch moves back to `+0x3e4` the same way while the timer runs. Once both the band and the
      pitch are within 10⁻⁵ of their goals, `+0x448` = 0 and `+0x3e4` = −FLT_MAX: the zoom is over.
4. **The band's ease** (`0x0012aae0`, every update while `+0x34c` > 0): with `d` = `+0x34c` − `+0x32c` and `T` =
   `+0x40c`, the near edge moves by `d × |d| / T × dt` (or `d × 3.5 × dt`, `d × 4.5 × dt` with `+0x448` 0, when no
   timer runs), clamped at `+0x34c`; the far edge is the near edge + 0.5; when within 10⁻⁵ the edge snaps and `+0x34c`
   = −1. The zoom step follows the band's near edge as `CamSetFollowZoom` sets it ([Script calls](#script-calls)): at
   or below `min + 0.4 × (default − min)` (3.72 m) the step is the default (30° with one player camera), at or below
   `default + 0.6 × (max − default)` (5.88 m) the maximum (40°), else the minimum (50°) (`0x001254f0`). Confirmed
   (runtime): 40° until the near edge passed 3.72 m, then 30° (sprint and [combat camera](#combat-camera)).

So the band moves 4.8 → 4.569 (`1.8² / 0.4667 / 30` = 0.231), 4.379, 4.221, … 3.216, then 3.0 when the timer's last
float (about 1.5 × 10⁻⁸) makes the step reach the goal: 14 updates, the measured curve to 0.001 m; the target pitch
falls 6° / 14 = 0.4286° per update. At runtime `+0x34c` read 3 and `+0x40c` 0.4667 on the first update at gait 5,
`+0x466` was 1 from then until the run stop's first update, when `+0x448` became that time + 250 ms; 8 updates later
(267 ms) `+0x34c` read 4.8 and the band and pitch went back over the next 14 updates, and on the 15th `+0x448` read 0
and `+0x3e4` −FLT_MAX. The saved zoom distance was 6.6 (the maximum): the upper pitch limit read 30° in the sprint and
40° again from the 6th update of the way back.

`CamEnable(5, on)` (`Camera_EnableFeature`, `0x0011de58` → `0x00126a30`) sets the switch `+0x468`; turning it off
also clears `+0x34c`, `+0x3e4`, `+0x448`, `+0x466` and `+0x467`. The constructor sets it to 1 (it read 1 in the
save). Confirmed (code).

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

- **Materials it ignores**: at `0x00130a5c` it copies a list from `0x00548ab0` to `sp + 0x40`: **30 `LOW_FENCE`,
  122 `RAILING`, 107 `CHAINLINK_NOCLIMB`**, ended by 1. Every `CollisionMesh_RayCast` of the step passes it as its
  fourth argument, the materials to skip (`0x001311a8`, `0x001312e0`, `0x00131730`, ...). So the camera sees
  through low fences, railings and unclimbable chain-link. Confirmed (code).
- **A height ray** (`0x00130c28`) straight down from the look-at point (`0x00511770`, (0, 0, −1)), as long as the
  look-at offset plus 0.5 m (1.9 m), mask `0x200`: when it hits a face whose normal's `z` is at most cos 15° (a slope,
  or the top of something under the look-at), `0x0012f3e0` adjusts the camera's height (what it changes is not traced).
- **The main ray** (the cast at `0x001311b4`, the recast at `0x001312e4`): from the look-at point toward the wanted
  position, its full length, with mask **`0x200 | 0x800 | 1`** (`| 1` only with one target). Through the [ray cast's
  rules](collision.md#ray-cast) that skips triangles with type bit 9 (`0x200`), and **tests disabled triangles too**
  (mask bit 0); one-sided faces are hit only from their front. When the hit is a disabled triangle (flag bit 0 clear,
  and not `0x800`) and either the target's point (`+0x1e0`) is not in front of its plane or the look-at point is less
  than **0.5 m** in front of it, the ray is cast again without mask bit 0, so the disabled triangle is ignored; a hit
  sets `+0x45b` (which latches auto-follow off until the player stops, `+0x45d`, [Heading](#heading)) and its distance
  becomes the limit the rest of the step works from. When that ray hits something that is not a ceiling (normal `z`
  above cos 150°), a second ray (`0x001314d0`) from the target's point to the look-at point checks whether the obstacle
  is between them; if so (and the latch `+0x479` is not −1), the main ray is cast again from a point moved along the
  view by `0.8 / tan(3 × probe angle)` and that distance is added to its hit. Confirmed (code) for the masks, the 0.5 m
  and the recast; the meaning of the second ray inferred.
- **Side probes** (casts from `0x00131744`): the main ray turned about the vertical through the look-at point by **3, 2
  and 1 × the probe angle** to each side (the loop counts down from 2), each as long as the main ray, with the same mask
  and the same disabled-triangle recast. The probe angle is 7° at the near edge of the distance band down to 4° at the
  far one (`7° − 3° × t`, `t` the position in the band). The free angle found on each side is limited to 3 × the probe
  angle. A table of fractions 1.0, 0.7, 0.5, 0.3 and 0.15 is set up beside them; where it is used is not traced.
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
  `+0x3ac` = 30°, which is the one-player (`0x0050b19c` = 1) case at the close zoom (zoom step 4.8). The view's pitch
  eased from 15° to 30° behind it.
- **Auto-follow was not seen.** Running at 7.8 m/s (gait 4) with the facing held 63-78° away from the view for
  1.5 s, in the open, the wanted position turned only as the moving look-at point dragged it: no rotation of its own,
  with the auto-centre option on (as saved) and with `0x0050b240` written to 0 (the default rule). By the code either
  rule should then turn about 3.4° (auto-centre) or 2° (default) per update. Every gate listed under
  [Heading](#heading) that can be read over PINE passed (`+0x444` = 1, record words `+0x00` and `+0x08` zero,
  `+0x460` = 0, `+0x320` = −1, `+0x368` = 0, `+0x455` = 0, `+0x474` = 1, the run clip's descriptor flags 0); the
  condition that blocks it was not found then (the update's locals cannot be read without breakpoints).
- **Why: the blocked-view latch.** A later run from the same spot (left stick 100 % up for 35 updates, then 70 %
  up and 70 % left, then released) read `+0x458`-`+0x45f` every update. `+0x45b` was 1 for the first 19 updates,
  while the walls held the camera at 1.85 m, and 0 after. `+0x45c` and `+0x45d` were 1 from the start and stayed 1
  through the whole run, clearing on the second update after the player stopped (gait 0). The camera's wanted
  position turned 0° of its own on every update (its angle about the new look-at point, before and after the
  update). The same run with `+0x45c` and `+0x45d` written to 0 before every update: they came back while `+0x45b`
  was 1, stayed 0 after, and once the stick went diagonal the wanted position turned **0.6-1.6° per update** of its
  own (18-47°/s), the auto-follow rule at work. So a blocked view at the start of a run keeps auto-follow off until
  the player stops. In the street the run starts in the open, nothing is latched, and the rule turns. Confirmed
  (runtime).

### In the street {#street}

PCSX2 2.9.94, the street saves (in `level99`'s world, [Feel comparison](feel.md)), Rembrandt, the camera read every
update while a scripted pad played; the per-pad option bytes `0x0050b240` / `0x0050b248` were 1 (the auto-centre
rule) and `0x0050b19c` was 1. Confirmed (runtime) unless marked:

- **Band and distance.** The leash band was **4.8-5.3 m** (`CfgFollowCamera`'s default 4.8 and + 0.5), the hard band
  as described (far edge + 0.35 m): the camera stood 5.30 m from the look-at point, 5.49 m walking and 5.65 m
  running, at pitches of 13°, 12.6° and 11.1°.
- **Auto-follow runs.** The wanted position turned about the look-at point by the auto-centre rule's rate
  (`(a − 45°) × 2.444 + 45°` per second above 22.5°, 200°/s from 90° to 100°, falling to 60°/s at 157.5°) when `a`
  is the angle between the player's new facing and the camera's view at the start of the update (its position then
  to its look-at point): within 5°/s on average from 30° to 90°, over 656 updates of walks, runs, sprints and turns;
  just above 22.5° it turned slightly the other way, as the rule's negative rate says. It turned while walking,
  running, sprinting and in the air, and not while standing, during the walk and run start clips or during the
  landing clip 436 (the gaits, [Heading](#heading)). Because the stick is turned by the camera, a stick held 90° to
  the side makes the player run in a circle. **The circling rate** (re-measured 2026-10-05, slot 1, stick held 90°
  to the side for 120 updates after 30 updates straight up; rates averaged over updates 100-158):

  | Stick | Gait | Player's turn | Rule's turn about the look-at point | Leash drag | `a` |
  | --- | --- | --- | --- | --- | --- |
  | 35 %, 60 % or 80 % sideways | walk, 1.63 m/s | **143°/s** | 127°/s | 16°/s | 77° |
  | 100 % sideways | run, 7.80 m/s | **191°/s** | 122°/s | 72°/s | 72° |
  | 70 % / 70 % (a full diagonal) | run | 61°/s | 24°/s | 42°/s | 34° |

  "Rule's turn" is the wanted position's rotation about the **new** look-at point (what the auto-centre rule adds;
  the leash moves it only along that line), "leash drag" the rotation of the old wanted position from the old to
  the new look-at point (the target's sideways move); they add up to the camera's turn, and the player's facing
  turns with the camera, `a` steady. The 122°/s and 127°/s an earlier reading gave as the circling rate are the
  rule's share alone: the original circles at **about 190°/s** at a run. The rule's turn is 3-10°/s above the
  formula for the measured `a` (the camera's forward taken as the view from its last position to the last look-at
  point; the other definitions tried fit worse). Confirmed (runtime).
- **Sprint zoom.** From the first update at the sprint gait the band's near edge went 4.8 → 4.569, 4.379, 4.221,
  4.085, 3.968, 3.863, 3.770, 3.686, 3.607, 3.533, 3.462, 3.391, 3.315, 3.216, 3.0 (the far edge 0.5 more), and the
  target pitch `+0x3b4` fell by 0.4286° per update from 13° to **7°**: both over 14 updates. In the sprint the camera
  settled 4.70 m from the look-at point at a pitch of 5.2°. Both went back the same way over 14 updates, starting
  8 updates after the run stop began (the body at about 2.5 m/s). A run (gait 4) did not change them.
- **Right stick.** Yaw at 30, 60 and 100 % to the right (raw x 189, 217, 255): 74.8, 106.7 and 150.0°/s applied to
  the wanted position from the first update, as the table above gives. Pitch up at 100 %: the target rose 85°/s to
  the upper limit **40°** and stayed there after release.
- **Look-at height after a climb's rise** (slot 8, the feet rising 1.28 m onto a trash can and 2.64 m onto a roof):
  the look-at point moved **20 % of the way** to feet + 1.4 m per update for 4-7 updates, then covered the last
  0.5-0.6 m in 2 updates, so the view's pitch stayed at 3.6° or more. During a jump it follows the feet directly.
- **Fences.** Through a running fence climb (slot 7, material 30) the camera stayed 4.9-5.3 m away while the fence stood
  between it and the player, and passed through the fence afterwards without pulling in: its collision does not see that
  fence, because every ray of the collision step excludes its material ([World collision](#collision)). Nor did the face
  that pulled Coney's camera in 0.19 m behind the player: it is not a low face but a **disabled** two-sided panel of
  `level99`'s mesh (triangle 27, material 91, flags `0xf442`: bit 0 clear, 4.1 m wide and 2.65 m tall across the run's
  path at `y` = 31.17), read from the save's RAM; slot 7 has 8 disabled triangles (that panel, two of material 187
  `STOREDOOR_GLASS`, four of material 2 `GLASS`) and slot 1 none, while every triangle on the disc is enabled, so the
  game switched them off ([Collision](collision.md#chunks), `0x0034fba0`). The main ray tests disabled triangles but
  recasts without them when the look-at point is within 0.5 m of the plane ([World collision](#collision)), which is the
  case here (inferred; the panel's data and the disabled counts are confirmed (runtime), read from the saves' RAM).

### Scenes take the camera and give it back {#scenes}

The rest of scene playback (records, roles, letterbox, skipping) is on [Scenes](scenes.md).

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

### Script calls in `level99` {#script-calls}

Every camera call of `level99.lua`, `level99_combat.lua`, `level99_lesson1.lua`, `level99_lesson2.lua` and the
`global.lua` helpers they reach (read from the scripts' bytecode; the bindings: [mission 1
coverage](../references/bindings/mission1.md)). Confirmed (code) at the cited functions.

| Call (where) | Effect |
| --- | --- |
| `CameraCreateFollow("follow", player)` (`AddCameras`) | `CamSetupFollow` + `CfgFollowCamera(3, 6.6, 4.8, 13, 65, 0.1, {0, 0, 1.4}, 0.2)` ([Setting up](#setting-up)): band 3.0-3.5 m, zoom step 4.8, upper pitch limit 30° |
| `CameraMakeActive(MainCam, 0)` then `CameraReset(MainCam)` (`AddCameras`) | current at once; then placed behind the player at 3.0 m, pitch 13° |
| `CamEnable(3/4, false)` (combat setup), `true` again (lesson 1) | no effect with one player ([Switches](#switches)) |
| `CamTarget(1, MainCam, player)` / `CamTarget(0, …)` (lesson 1) | removes the player from the shared target list, later adds it back. The follow camera takes its targets from the list's first two entries and falls back to its last target (`+0x31c`) when the list is empty (`0x001282a0`), so with one player nothing changes |
| `CamSetFollowZoom(1)` (lesson 1 setup; lesson 2 after `DealerPoizo` and the dealer's respawn) | **band 4.8-5.3 m**, zoom step 6.6, upper pitch limit 40° |
| `CameraCreateLocked(name, pos, fov, heading, pitch, 0, 0.1, far)` + `CameraMakeActive(name, 0)` | a cut to a [locked camera](#locked-cameras) (nine in the tutorial: fov 50 or 65, far 72.6-150) |
| `CameraReset(MainCam)` + `CameraMakeActive(MainCam, 1)` | back to the follow camera, placed behind the player, with a **1 s blend** ([Blends](#blends)); after `VerminWait` with 0 s, a cut |
| `CamSetFollowAngle(-10)` (lesson 2 `P3.BreakFence`, between the reset and the blend) | target pitch −10° clamped to the limits, so the lower limit `atan(−0.4 / 5.3)` = **−4.3°** (the camera looks up at the fence); reached at once; it stays until the next `CameraReset` (after `DealerPoizo`) |
| `CamSetSecondary(Teacher.Vermin, 0, p)` / `(NilHandle, 0, p)` (lesson 2, both players) | keep Vermin in view ([Heading](#heading), factor 0.25, no range) instead of auto-follow; NilHandle ends it |
| `CamEnable(0, false/true)` (lesson 2 `P3.Player2Jumps` … `P3.VerminJumped`) | right stick and zoom buttons off while the camera watches Vermin's jump |
| `CamEnable(9, false/true)` (lesson 2 `P3.DealerHit` … `P3.CheckForFlash`) | power-move cameras off while the dealer respawns |
| `ScreenQueueEffect(2, 1)` / `(3, 1)` around `VerminCar`, `PedCam`, `ClimbPoizo`, `JumpCam`, `VerminFencePoizo` | letterbox in / out over 1 s ([Screen effects](../references/screen-effects.md)); `VerminWait`, `FenceCam` and `DealerPoizo` have none |

`CamSetFollowZoom(preset, player = −1)` (`0x0011c470`), for each player's follow camera (or one): the look-at point is
snapped (`0x00127d48`, its previous values set to it, so no ease), then the band is moved so that its near edge is
the preset distance (kept 0.5 m deep, clamped to `[min, max]`, and saved in `+0x344`/`+0x348`), the lower pitch limit
recomputed from the new far edge, and the zoom step set to the next preset: **0** → near edge the minimum, step the
default (30°); **1** → the default, step the maximum (40°); **2** → the maximum (band max − 0.5 .. max), step the
minimum (50°). With two player cameras 0 acts as 1. The camera itself is not moved: the leash drags it into the new
band over the next updates. Confirmed (runtime): checkpoint 1 states read 3.0 / 3.5, step 4.8, 30°; a run left to
itself from `l99-warriors-fight-start` until lesson 1, and the street saves, read 4.8 / 5.3, step 6.6, 40°.

`CamSetFollowAngle(degrees)` (`0x0011c3b8`): snaps player 0's look-at point, then for every follow camera sets the
target pitch to `degrees` clamped to `[+0x3b0, +0x3ac]`, turns the view to it at once (the wanted, previous and own
positions all set to the result) and clears the sprint latch `+0x466`. Confirmed (code).

### Blends between cameras {#blends}

`CameraMakeActive(camera, seconds > 0)` makes the blend camera (type 5) current when there is a previous camera and
the new one is not itself a blend (`0x0011ee08` → `0x00143078`). Its update (`0x00143590`), each step, confirmed
(code):

1. Runs the destination camera's own update (and the source's, when it is another camera), so the follow camera keeps
   leashing to the player during the blend.
2. `t = min(elapsed / seconds, 1)`, elapsed counting by `dt`.
3. Look-at point = `lerp(start look-at, destination's look-at, t)`; orientation = a slerp of the start matrix toward
   the destination's by `t` (`0x00336a00`); position = `lerp(start position, destination's position, t)`, then pushed
   out of the collision mesh (`CollisionMesh_SpherePush`). The start values are the source's view when the blend
   began: linear in time, no easing.
4. The field of view is the destination's; a second lens value (slot `+0x20c`, inferred the far clip) is
   `min(current, lerp(start, destination, t))`.
5. When `elapsed ≥ seconds`, the destination becomes current directly (`0x0011ee08` with 0 s), which runs its
   activation ([Setting up](#setting-up), step 4).

### Locked cameras {#locked-cameras}

A locked camera (type 1, `CameraCreateLocked`) stays where it was put, looking along its heading and pitch. Its update
(`0x00135680`) places it, aims it at a point 3 m ahead along its forward, keeps it out of walls with a line-of-sight
test from its position, runs `0x00135ca8` (the humans `CamLockLocked` gives it, none in `level99`) and applies the
[shake](#shake). Confirmed (code); the tracking step is not traced.

### Combat camera {#combat-camera}

While the player fights with a lock-on, the follow camera pulls in and frames the enemy. Confirmed (code) in the
update at `0x0012bb00`-`0x0012bbe8`; confirmed (runtime) as noted.

- **When**: one target, the target is player 1, the player is pad-controlled (per-player `+0x1b`), and
  `0x00233c50` holds: the mode `0x00510228` is not 0 (1 by default; `CfgAutoCloseMode` sets it, no script calls it),
  one player, the player rides nothing (`+0xc4`), and the player has a fight target (`0x00226e60`) with record flags
  `0x8` and `0x4` both set (L1 held at a target; with mode 2, also any update in the lock-on movement state
  `0x00241b90`). Confirmed (runtime), slot 6 copy, L1 held facing the pedestrian: flags `0xd`, `+0x46f` 1 on the
  third update after the press, 0 on the second after the release. In `level99_combat.lua`'s sparring L1 is disabled
  (`EnableCommand(player, 6/8, 0)`) and the camera stayed off.
- **On entry** (`+0x46f` set): the band's wanted near edge is saved in `+0x3cc` (the sprint zoom's saved edge if
  one is held, else a wanted edge in progress, else the near edge) and set to **2.4 m**; unless `+0x3d0` holds a
  pitch, the **target pitch becomes 15°**. The band eases by `d × 4.5 × dt` per update (no timer, [Sprint
  zoom](#sprint-zoom) step 4), so 4.8 → 4.44, 4.134, 3.874, …, 2.4 in about 50 updates (runtime, to 0.001 m); the
  zoom step follows it (30° below 3.72 m).
- **Each update while on**: `0x0012e9a8` takes the enemy's point (its position plus half its `+0x4e0` vector, inferred
  half a second of its velocity; an object's position + 0.5 m) and the angle at the look-at point between the camera's
  horizontal view and the direction to the enemy; outside 25°-29° it yaws by `(angle − 27°) × 0.455` toward 27°; the
  turn is capped at 640°/s (`11.17` rad/s) when the enemy is more than 29° off, not when it is under 25°. So **the enemy
  is held 27° off the view's centre**, beside the player. This counts as this update's turn: auto-follow and
  keep-in-view do not run.
- **On exit**: the wanted near edge is set back to the saved `+0x3cc`, which is cleared; the band eases back the same
  way (runtime: 2.4 → 2.76, 3.066, …, 4.8). **The target pitch stays 15°** until the next `CameraReset` or
  `CfgFollowCamera` (runtime: 15° 150 updates later).
- Shakes started while it is on are 0.66 as strong, and the pad's rumble thresholds are lowered by a quarter
  ([Shake](#shake)).

### Shake and rumble {#shake}

A shake is started on a camera through its vtable slot `+0x15c` (`0x001263e8` for the follow camera, `0x001210f8` in
the base), confirmed (code): a hit reaction (`0x0026a6d0`, with the attack's strength bits `(flags & 0x30) >> 4` as the
level, on the attacker's player camera and, from strength 2, the victim's), rage start (`0x00236d28`, level 1) and an
animation event (`0x00101dd8`). Levels: **1**: amplitude 0.5, 0.10 s, rumble base 0; **2**: 0.75, 0.15 s, `0x30`;
**3**: 1.0, 0.18 s, `0x60`; 0 stops it. The update (`0x00121298`, every camera kind): the amplitude eases 65% per
update toward the level's while its time lasts; the time counts down by `dt × min(1, 54 × step)` (so slower in [slow
motion](#slow-motion)); the pad's rumble byte (`+0x41` of the pad record) gets `255 × current / level amplitude` when
that exceeds `(base >> 2) + 0x28`, capped at `base + 0x60`; a random view offset scaled by the amplitude is added only
while switch 6 is on. The offset's exact form is not traced.

### Slow motion {#slow-motion}

`0x005148a0` (`CfgFollowCamera`'s last argument, 0.2 in `global.lua`) scales **the characters' step**: an animation
event of type `0x2e` on a player's clip (`0x00101dd8`, one player only) calls `0x0041ab30`, which sets the step
`0x005102cc` to `0x005148a0 / 30` (1/150 s) and marks the player; type `0x2f` (`0x0041ab60`) unmarks it and, when no
player is marked, sets the step back to 1/30 s. Every character update then advances `dt` = 1/150 s, so humans,
animation and the cameras' character-step logic run at 20% speed while frames keep their rate. Confirmed (code);
which clips carry events `0x2e` / `0x2f` (inferred: rage and power moves) is not surveyed. Two other writers,
`0x0030c8c8` and `0x0030c8f8` (not traced), set the step to 1/60 s and 1/120 s.

## Coney's implementation

`src/camera/follow_camera.*` is the follow camera of `Cam_Follow_Update` (`0x0012ae58`), stepped after the human
by `src/human/player.*` and drawn by `--play-level` ([Building](../guides/building.md#playing-a-level));
`src/camera/follow_collision.*` holds its rays against the collision mesh:

- the look-at point is the feet + 1.4 m, its move each update limited by its length `d` as in [step 1](#update):
  all of it within 0.4 m, `1 − 2 × (d − 0.4)` of it to 0.8 m, 20 % beyond; in the air (a jump or a fall) it
  follows the feet directly. A climb's rise of 2.64 m is followed at 20 % for 6 updates, then in 3 (the last one a
  few millimetres);
- the wanted position stays put unless its distance leaves the leash band, 4.8-5.3 m (the default distance and
  0.5 m more, as in the street), then moves along that line to the band; the camera moves 22% of the way to it each step,
  held inside the hard band (4.56-5.65 m), which widens at once and shrinks by 1% of the difference an update;
- **auto-follow** (`0x00129c78`): the auto-centre rule ([Heading](#heading)) turns the wanted position toward the
  player's facing at the rule's rate of the angle `a` between the facing and the camera's view at the start of the
  update, at the stored gaits 2, 4 and 5 (walk, run, sprint; so not standing, not in the walk start at 0.76 m/s nor
  at a jog's speed, whatever clip plays). It is held off from a blocked main ray until the player stops (the `+0x45d`
  latch, set by `+0x45b` and cleared on the second update standing, as at runtime), for 0.334 s after right-stick
  input, and while the left stick points more than 157.5° from up (`+0x474`). With the
  option off (the debug menu's *Auto-centre*) the default rule (`0x0012a400`) runs instead, at the run and sprint
  gaits only. With the stick held sideways the player runs in a circle: Rembrandt in the sandbox turns about 197°/s
  at a run and 144°/s at a 35 % walk, against the original's 191°/s and 143°/s (disc test `[disc][player][sandbox]`);
- the **sprint zoom** ([Sprint zoom](#sprint-zoom)) with the page's fields: the first update at the sprint gait arms
  and latches it (`+0x467`, `+0x466`) when the player has no enemies or the nearest is within 12 m (the brain's query
  is a hook, `Player::setNearestEnemy()`, with no enemies until Coney has brains; out of range the arm waits); the
  zoom function (`0x00128cf0`) saves the band, the zoom distance and the
  target pitch, starts the 0.5 s timer, sets the wanted near edge to the minimum distance and steps the zoom to the
  default (upper pitch limit 30°); the band's ease (`0x0012aae0`, early in the next updates) moves the near edge by
  `d × |d| / T × dt`, which gives the street's 4.569, 4.379, … 3.216, 3.0 to 0.001 m over 15 updates; the target
  pitch goes to 7° in a straight line over 14. The first update off the sprint gait sets the way back for 250 ms
  later; 8 updates on the zoom function puts back the band, the zoom (40° again) and the timer, the same curves run
  back, and on the 15th update the zoom is over. `enableSprintZoom()` is `CamEnable(5, on)` (`0x00126a30`);
- the pitch eases toward its target at 85°/s, between the lower limit (the larger of -20° and the slope of 0.4 m over
  6.6 m) and the upper one of the zoom distance (`0x001254f0`): 50° at the minimum, 40° above the default, 30° at the
  default with one player camera (`0x0050b19c` = 1);
- the **height hold** (step 7): `holdHeight()` eases the wanted position's height 30 % × `+0x398` of the way an
  update toward the height above the look-at point it held;
- the right stick turns the wanted position at the raw rates (yaw up to 150°/s outside the ±48 dead zone, pitch near
  the ends of the travel) and holds off for 0.334 s after any input;
- **collision** ([World collision](#collision)): the main ray from the look-at point to the camera with mask
  `0x200 | 0x800 | 1`, so it tests disabled triangles, and every ray passing through materials 30 `LOW_FENCE`, 122
  `RAILING` and 107 `CHAINLINK_NOCLIMB` (`0x00548ab0`); a hit on a
  disabled triangle is cast again without them when the look-at point is less than 0.5 m in front of its plane or the
  player's feet are not in front of it. A hit that stands sets `+0x45b` and pulls the camera to 0.2 m short of it,
  never nearer than 0.5 m. The **side probes** turn the main ray about the look-at point's vertical by 1, 2 and 3 ×
  the probe angle (7° at the band's near edge to 4° at its far edge) each way;
- the camera's forward vector turns the player's stick before the human sees it;
- **the script calls** ([Script calls](#script-calls)): `src/camera/cameras.*` keeps player 1's cameras (the follow
  camera, the locked ones, which is current, the blend, the scene stack, the switches, the target list, the watched
  human, the shake and slow motion), and `src/scripting/camera_bindings.*` makes `CamSetupFollow`, `CfgFollowCamera`
  (`FollowCamera::configure()`: band 3.0-3.5 m, zoom step 4.8, 30°), `CamSetFollowZoom`, `CamSetFollowAngle`
  (clamped, reached at once), `CameraReset` (behind the player at the nearest preset, pitch back to 13°),
  `CameraCreateLocked`, `CameraMakeActive`, `CamEnable` (switches 0, 5 and 6 act), `CamTarget` and `CamSetSecondary`
  (keep in view, 0.25 of the field of view, 35 % of the excess, at most 270°/s) real. The zoom step follows the band's
  near edge as it eases (3.72 m and 5.88 m);
- **blends** ([Blends](#blends)): `CameraBlend` lerps the position and look-at point and slerps the orientation from
  the view shown when it began to the destination's live view, linear in time, the far clip never growing; at the end
  the destination becomes current directly, which runs the follow camera's activation (`FollowCamera::activate()`).
  A **locked camera** looks along its angles at a point 3 m ahead, its far clip at most 150;
- the **combat camera** ([Combat camera](#combat-camera)): with L1 held while the player has a fight target (`Fighter::target()`)
  the band's wanted near edge goes to 2.4 m (4.8, 4.44, 4.134, ... at 4.5/s) and the target pitch to 15°, the enemy's
  point (position + half its velocity) is turned toward 27° off the view's centre (0.455 of the excess, at most
  640°/s beyond 29°), and on release the saved band comes back while the pitch stays;
- the **shake** ([Shake](#shake)) at the three strengths, 0.66 in combat, its time counted with the characters' step,
  and the rumble byte it drives. Player 1's camera shakes when a reaction plays to his hit (at the hit code's strength
  bits), when he reacts himself from strength 2, and at level 1 when his rage starts (`Fighter::reactionShake()`,
  `Fighter::rageStarted()`, read by `human::Player` after the step);
- **slow motion** ([Slow motion](#slow-motion)): the player's clip events `0x2e` / `0x2f` set the characters' step
  `SlowMotion::stepSeconds()` to `CfgFollowCamera`'s factor of 1/30 s and back, and every human of the step
  (`Humans::update()`) advances by it: animation, motion, stamina, gravity, an attack's steer and a grab's alignment;
- **in play** (`--play-level`, the story): gameplay (`GameplayMode`) makes player 1's cameras before each level's
  script, gives them to the bindings (`BindingContext::cameras`, read at each call) and to the level (`ScriptedCast`),
  whose player steps them (`Player::setCameras()`); `CamSetSecondary` finds its human at its live position (the
  scripts' brains). The level streams round the current camera and draws through its lens, the far clip capping the
  draw distance;
- `--trace FILE` writes the camera's position, look-at point, wanted position, distance, angles, band and
  auto-follow turn after every step, with the player's state ([Building](../guides/building.md#tracing)).

The world viewer keeps its own free camera with the player camera's lens
([The streamed world](world.md#coneys-implementation)).

**Coney choices** where the research is silent:

- **One player camera**: `0x0050b19c`, the number of player cameras (`0x00122ed0`), is 1 (the debug menu's *One
  player camera*), as Coney has no split screen. Off is the two-player case: the sprint keeps the band and only lowers
  the pitch, and the way back goes to the maximum distance less 0.5, as the page says.
- **The zoom distance** starts at the maximum (6.6 m, upper pitch limit 40°), as read in the street, for a camera no
  script configures (the sandbox, `--play-level` without scripts).
- **Locked cameras' angles**: the page does not give their conventions, so the heading is read as a human's (0 facing
  +y, anticlockwise), the pitch positive looking down and the roll positive turning the top to the right. The line of
  sight test, the blend's sphere push and keep-in-view's ray (for a range above 0) are left out.
- **A follow camera that is not current** is not updated; it only notes where the player is, so a reset or the
  activation places it on him.
- **The shake**: one shake on the manager, applied to whichever camera is current; the view offset (form not traced) is
  a random share in [-1, 1] of the amplitude × 0.05 m on each axis; after its time the amplitude eases back to 0 at the
  same 65 %. Coney has one player camera, so a player's hit shakes player 1's. The animation event that starts a shake
  is not wired: its type is not traced.
- **Slow motion**: the combat timers (stun, ground and game time, `nowMs`) still count whole updates of 1/30 s, and the
  cameras update by the frame's 1/30 s; whether the original's game time follows the step is not traced. A locked
  camera's roll is not drawn yet: the play mode builds its view from the eye and look-at point with the world's up.
- **The sprint time** `+0x36c` counts only at the sprint gait and is zeroed off it, so every sprint arms the zoom (a
  run before the sprint, as in the street's runs, would otherwise keep it from arming). A sprint that starts while
  the band is still going back saves the band it was going back to, so a quick second sprint does not keep a band
  left half-way.
- **The side probes** give each side's room as the largest clear multiple of the probe angle with every smaller one
  clear; when the two differ by more than 7.5° the wanted position turns toward the roomier side by 20 % of half the
  difference (the turn that would even them up) each update. The fully blocked view's 480°/s turn, the side factors
  `+0x434` / `+0x438`, the second ray from the target, the sphere pushes and the next update's lag are not
  implemented.
- **Disabled triangles**: the main ray's rule is in place, but which triangles the game switches off in a level
  (`0x0034fba0` from a game object's box, [Collision](collision.md#enable)) is not known from the data yet, so every
  triangle of a level stays enabled in Coney and the street's disabled panel still pulls the camera in there.
- **The height hold** is never entered by the player yet: what sets `+0x453` is not traced, nor the two special
  modes' 40.5 % and 48 %.
- Beyond 157.5° the auto-centre rule's falling line is carried on for a running player (5°/s at 180°). The other
  gates of [Heading](#heading) (human flags, the clip's descriptor flag `0x8000`, state flag 4, the watched target)
  are not modelled.
- **The blocked-view latch** clears on the second update the player stands (gait 0, on the ground), as at runtime;
  the clearing condition is now on the page ([Heading](#heading)), and `+0x45c` is not modelled apart from it.
- **A fresh camera** (at the start, or after the player is put back) sits behind the player at 4.8 m and 13°. `--start`
  with a distance and a yaw places it there instead, its wanted position with it and the hard band stepped once
  (`FollowCamera::place()`), so a trace scenario starts with the camera of the original's save state.

## Notes for implementers

- **`level99`'s calls** ([Script calls](#script-calls)): `CfgFollowCamera` leaves the band at the minimum (checkpoint 1
  plays at 3.0-3.5 m); `CamSetFollowZoom(1)` from checkpoint 2 moves it to 4.8-5.3 m without moving the camera;
  `CameraReset` puts the camera behind the player and the pitch back to 13°; `CameraMakeActive(…, 1)` is a linear
  1 s blend from the locked camera's view to the live follow camera; `CamSetSecondary` swaps auto-follow for
  keep-in-view; `CamSetFollowAngle(-10)` clamps to the lower limit (−4.3°) and stays until the next reset.
- **Combat camera** ([Combat camera](#combat-camera)): with L1 held at a target, band to 2.4 m (4.5/s ease), target
  pitch 15° (kept afterwards), enemy held 27° off centre; restore the saved band on release.
- **A first follow camera** that matches the numbers: look at the player's feet + 1.4 m; keep the camera where it is
  unless its distance to the look-at point leaves the 3.0-3.5 m band, then move it along that line to the band; move
  22% of the way to the wanted position each 30 Hz step; hold a 13° pitch; 65° horizontal field of view, near 0.1,
  far 115.
- **Heading**: in the street the auto-centre rule ([Heading](#heading)) turns the camera toward the facing whenever the
  player moves, with the angle measured from the camera's position at the start of the update
  ([In the street](#street)); with the stick held sideways the player runs in a circle. At `level99`'s checkpoint 1 it
  was not seen. Implement it, with the leash and the right stick. The player's stick is turned by the camera's heading
  before it reaches the character ([Characters](characters.md#input)), so the camera must ease, never snap. Gate it on
  the gait (walk, run or sprint; not idle, sneak speed or jog), not on which clip plays, and hold it off from a blocked
  main ray until the player stops (the `+0x45d` latch; this is why checkpoint 1 has none). With the stick held sideways
  the original circles at about 190°/s at a run and 143°/s at a walk, the rule's turn plus the leash's drag.
- **Sprint zoom** ([Sprint zoom](#sprint-zoom)): on the first update at the sprint gait save the band, zoom and
  target pitch and start a 0.5 s timer; the band's near edge moves by `d × |d| / T × dt` toward the minimum distance
  (`d` what is left, `T` the timer, after it has counted down once), the target pitch in a straight line to 7°;
  250 ms after the sprint gait ends, the same back to the saved values.
- **Look-at point**: limit its move per update by the distance rule of [step 1](#update) (20% above 0.8 m, a
  linear share from 0.8 to 0.4 m, all of it below), except while jumping or falling; any trigger on "a rise" is not
  what the original does.
- **Right stick**: yaw 60-150°/s outside a ±48 raw dead zone, applied to the wanted position at once (the lag
  smooths it); pitch only near the ends of the travel; 0.334 s of no auto-follow after any input.
- **Collision**: start with a ray from the look-at point and pull in to the hit, with the main ray's mask, its
  disabled-triangle rule and the excluded materials 30, 122 and 107 ([World collision](#collision)); the triangles'
  enabled bits must follow the game (doors, glass and barriers the game switches off), or a disabled panel pulls the
  camera in. The side probes and swing-away rules can come later.
- **Update order**: the cameras update at the end of the characters' 30 Hz step, after movement
  ([Characters](characters.md#update)), and the device takes the lens once per frame.
- Keep the camera deterministic (no real time) so the test mode can compare frames.

## Open questions

- **The circling rate** (answered): the original circles at about 190°/s at a run and 143°/s at a walk; the 122°/s
  and 127°/s first given were the rule's share ([In the street](#street)). Still open: why the rule's measured turn
  is 3-10°/s above the formula (the exact forward vector of vtable slot `+0x224`).
- **The sprint zoom** (answered, [Sprint zoom](#sprint-zoom), with its gates: no enemies or the nearest within 12 m,
  and `0x0050b19c` the number of player cameras).
- **The fourth argument of the auto-centre rule** (`sp + 0x1d4` in the update), which keeps 200°/s above 100°, and
  state flag 4, which allows a turn beyond 157.5°.
- **The collision step** (`0x00130990`) beyond its rays (partly answered: the main ray, its recast and the side
  probes' angles, the excluded materials and the `+0x45d` latch, [World collision](#collision)): the table of
  fractions, what
  `0x0012f3e0` changes after the height ray, and what `+0x10a`-`+0x10c` (side angle history) feed.
- **Slow motion**: which clips carry the events `0x2e` / `0x2f` ([Slow motion](#slow-motion)).
- **Shake**: the view offset's form, and the type of the anim event (`0x00101dd8`) that starts one.
- **Slow motion's game time**: whether the combat timers (stun, ground) count the shorter step or the frames.
- **Locked cameras**: the conventions of `CameraCreateLocked`'s heading, pitch and roll.
- **The combat camera's 0.4 keep-in-view** (`0x0012e170(0.4)` in the update's one-target case): which human it keeps.
- **Scenes**: the scene camera's own update (type 4) and the "a scene is playing" flag at `0x0051489c + 0x410` (see
  [Scenes](#scenes)).
