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
[The streamed world](world.md#player-camera). The other camera kinds ([Types](#types)) are covered from
[Death camera](#death-camera) on, and every function of `Camera/` is in the [function map](#fn-script).

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

`Camera/` (`0x0011b770`-`0x00143ea0`, [Source map](source-map.md)): the script helpers, then the camera manager and
each camera class in turn, in the order of their vtables (`0x00535250`-`0x005378d0`, `0x2c0` bytes apart, 8-byte
`{delta, function}` slots). Function names are ours unless a class string gives them; every address below has the
same name and a plate comment in the Ghidra project. "Vtable `+0xNN`" is the slot whose function word is at that
offset (the call reads its delta at `+0xNN − 4`). Unless a row says otherwise its evidence is the code at that address.

Outside `Camera/`, the functions this page relies on:

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00367580` → `0x00353818` | `ScenePlayCinematic` (binding) | starts a scene; stores `BlendCam` at the scene's `+0x94` | confirmed (code) |
| `0x0039d870` / `0x0039f450` | scene start / scene end | take the camera over, give it back | confirmed (code) |
| `0x001562c8` | cameras to the device | the lens and draw distance each frame ([The streamed world](world.md#player-camera)) | confirmed (code) |
| `0x00233c50` | combat camera test | [Combat camera](#combat-camera) | confirmed (code) |
| `0x0041ab30` / `0x0041ab60` | slow motion on / off | the characters' step `0x005102cc` | confirmed (code) |

### Script helpers {#fn-script}

The functions the camera bindings call ([camera bindings](../references/bindings/camera.md)), `0x0011b770`-`0x0011e1b0`.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0011b770` | `Camera_MakeActiveByHandle` | [`CameraMakeActive`](../references/bindings/camera.md#cameramakeactive) binding helper: resolves the camera handle (must be a camera object), reads the player index (`+0x1b0`) of up to two optional humans and calls Camera_MakeActive (`0x0011ee08`) with the blend time. See [blends](#blends). | confirmed (code) |
| `0x0011b838` | `Camera_GetActiveHandle` | [`CameraGetActive`](../references/bindings/camera.md#cameragetactive): handle (vtable `+0x2c`) of the player's current camera, or the null handle `0x006ebd30`. | confirmed (code) |
| `0x0011b888` | `Camera_DeleteScripted` | [`CamDelete`](../references/bindings/camera.md#camdelete): releases (Camera_Release `0x0011e440`) a camera by handle, but only a fixed (type 0) or locked (type 1) camera; shared kinds are kept. | confirmed (code) |
| `0x0011b920` | `Camera_GetPositionByHandle` | [`CamGetPos`](../references/bindings/camera.md#camgetpos): a camera's position (vtable `+0x21c`) by handle, or the zero vector `0x005116c0`. | confirmed (code) |
| `0x0011b970` | `Camera_TransformPoint` | [`CameraTransformPoint`](../references/bindings/camera.md#cameratransformpoint): transforms a point (w=1) by the camera's matrix (vtable `+0xac`, `0x00335a78`) and writes it back. No script calls it. | confirmed (code) |
| `0x0011ba20` | `Camera_AttachObjectToView` | [`CameraTransform`](../references/bindings/camera.md#cameratransform): object matrix = camera matrix × object matrix (`0x003359b0`), set back on the object (vtable `+0xb4`). No script calls it. | confirmed (code) |
| `0x0011bad8` | `Camera_ResetByHandle` | [`CameraReset`](../references/bindings/camera.md#camerareset): vtable `+0x13c` reset; for a follow camera the other player's follow camera (if made) is reset too. | confirmed (code) |
| `0x0011bb98` | `Camera_SetClipping` | [`CameraSetClipping`](../references/bindings/camera.md#camerasetclipping): near plane (not for a follow camera) and far clip capped at 150. | confirmed (code) |
| `0x0011bc48` | `Camera_CreateLocked` | [`CameraCreateLocked`](../references/bindings/camera.md#cameracreatelocked): factory type 1; position at `+0x1e0` and `+0x10`; fov (vtable `+0x194`); aim from three values (`0x001353b8`); near; far capped at 150; lens to default (vtable `+0x20c` -> `+0x1b4`); returns the handle. | confirmed (code) |
| `0x0011bdc0` | `Camera_LockLocked` | [`CamLockLocked`](../references/bindings/camera.md#camlocklocked): adds/removes a human in a locked camera's kept-in-view list (`0x001358d0`). | confirmed (code) |
| `0x0011be18` | `Camera_CreateThird` | [`CameraCreateThird`](../references/bindings/camera.md#cameracreatethird): factory type 0x10 on a target object (`+0x1e0`); fov (vtable `+0x194`); distance/height at `+0x210`/`+0x214`, angle (`0x001207b0`), offset `+0x220`; near; far capped at 150; returns the handle. | confirmed (code) |
| `0x0011bfa8` | `Camera_SetupFollow` | [`CamSetupFollow`](../references/bindings/camera.md#camsetupfollow): puts the player's Cam_Follow on a target (vtable `+0x1bc`), places it at the target plus the offset `+0x200`, Cam_Follow_PlaceBehind(180, -1), returns its handle. [setting up](#setting-up). | confirmed (code) |
| `0x0011c0b8` | `Cam_ConfigureFollow` | CfgFollowCamera (player 1's follow camera): distances, pitch, fov, near plane, offset, slow-motion factor `0x005148a0`; band to the minimum with one player camera. [setting up](#setting-up). | confirmed (code) |
| `0x0011c270` | `Camera_TargetList` | [`CamTarget`](../references/bindings/camera.md#camtarget): adds or removes a human in the shared target list `0x005d91a8`. [script calls](#script-calls). | confirmed (code) |
| `0x0011c2f0` | `Camera_SetFollowHeading` | [`CamSetFollowHeading`](../references/bindings/camera.md#camsetfollowheading): snaps every player's follow camera to a heading round its target (Cam_Follow_SetHeading), no blend. | confirmed (code) |
| `0x0011c3b8` | `Camera_SetFollowPitch` | [`CamSetFollowAngle`](../references/bindings/camera.md#camsetfollowangle): every follow camera's target pitch, clamped, reached at once (Cam_Follow_SetTargetPitch). [script calls](#script-calls). | confirmed (code) |
| `0x0011c470` | `Camera_SetFollowZoom` | [`CamSetFollowZoom`](../references/bindings/camera.md#camsetfollowzoom): band to the minimum, default or maximum, zoom step and upper pitch limit. [script calls](#script-calls). | confirmed (code) |
| `0x0011c638` | `Camera_SetFollowPosition` | [`CamSetFollowPos`](../references/bindings/camera.md#camsetfollowpos): puts a player's follow camera at a world position at once (Cam_Follow_SetPosition). | confirmed (code) |
| `0x0011c6b8` | `Camera_CreateFixed` | [`CameraCreateFixed`](../references/bindings/camera.md#cameracreatefixed): factory type 0 at a position, target put on the shared target list (CameraTargets_Add), offset, lens; returns the handle. | confirmed (code) |
| `0x0011c858` | `Camera_CreateWin` | [`CameraCreateWin`](../references/bindings/camera.md#cameracreatewin): sets up the shared Cam_Win (`0x00120188`): name (vtable `+0x18c`), target human `+0x1e0`, fov (vtable `+0x194`), distance, angle, height and speed (`+0x1e4`..`+0x1f0`), direction (`0x00143c58`); near 0.1; far capped at 150; returns the handle. | confirmed (code) |
| `0x0011c9e0` | `Camera_SetupPoizo` | [`CamSetupPoizo`](../references/bindings/camera.md#camsetuppoizo): starts the path camera from an existing camera's view, clearing its points; time to the first point, end callback, fov, far. [path cameras](#path-cameras). | confirmed (code) |
| `0x0011cb70` | `Camera_ReversePoizo` | [`CamReversePoizo`](../references/bindings/camera.md#camreversepoizo): reverses the path camera (PoizoCam_Reverse `0x00142578`) if it exists; returns whether it does. | confirmed (code) |
| `0x0011cbb0` | `Camera_AddPoizoPoint` | [`CamAddPoizoPoint`](../references/bindings/camera.md#camaddpoizopoint): appends a path point (position; orientation from heading, pitch, roll) with its time and callback. | confirmed (code) |
| `0x0011cc68` | `Camera_AddPoizoPointCam` | [`CamAddPoizoPointCam`](../references/bindings/camera.md#camaddpoizopointcam): appends a path point taken from another camera's view. | confirmed (code) |
| `0x0011cce8` | `Camera_SetupRail` | [`CamSetupRail`](../references/bindings/camera.md#camsetuprail): a player's rail camera: name, target on the target list, fov, offset, near, far; CamRail_Reset. [rail](#rail). | confirmed (code) |
| `0x0011cf70` | `Camera_SetRailLead` | [`CamLeadRail`](../references/bindings/camera.md#camleadrail): puts a player's rail camera into leading mode 1 or 2 with a lead distance. [rail](#rail). | confirmed (code) |
| `0x0011d098` | `Camera_AddRailPoint` | [`CamAddRailPoint`](../references/bindings/camera.md#camaddrailpoint): appends a rail point (CamRail_AppendPoint). [rail](#rail). | confirmed (code) |
| `0x0011d180` | `Camera_LockRail` | [`CamLockRail`](../references/bindings/camera.md#camlockrail): holds or releases a player's rail camera (`+0x3e4`). [rail](#rail). | confirmed (code) |
| `0x0011d228` | `Camera_ModifyRail` | [`CamModifyRail`](../references/bindings/camera.md#cammodifyrail): a rail setting's target and ease time. [rail](#rail). | confirmed (code) |
| `0x0011d808` | `HoodCam_Setup` | [`CamSetupHood`](../references/bindings/camera.md#camsetuphood): every player's hood camera mounted on a vehicle with mount offset and lens values. | confirmed (code) |
| `0x0011daa8` | `Camera_UseDeathCamera` | [`CamUseDeathCamera`](../references/bindings/camera.md#camusedeathcamera): makes player 1's Cam_Failed (`0x001200e0`) current with no blend on a human; saves the previous camera (`+0x1e4`), sets flags `0x0050b1e8`=1/`0x0050b1ec`=0, fades the screen over ms (`0x0018c988`/`0x0018d058`), hides the HUD (`0x001b1f38`) and stops every gang member and the human (`0x00227388`). | confirmed (code) |
| `0x0011dcf0` | `Camera_SetFollowSecondary` | [`CamSetSecondary`](../references/bindings/camera.md#camsetsecondary): a human/object for a player's follow camera to keep in view (`+0x320`) and its range (`+0x3fc`). | confirmed (code) |
| `0x0011dd78` | `Camera_CanSeeObject` | [`CamCanSee`](../references/bindings/camera.md#camcansee): is a point 0.3 m (1 m for class flag 0x40) above the object seen by any player's camera within range (Camera_AnyPlayerCanSeePoint). | confirmed (code) |
| `0x0011de58` | `Camera_EnableFeature` | CamEnable(switch, on, player): writes one of the 14 camera switches ([switches](../references/cameras.md#switch)). | confirmed (code) |
| `0x0011e0a8` | `Camera_SetSplitMode` | [`CamSetSplitMode`](../references/bindings/camera.md#camsetsplitmode): split layout 0 or 1 into `0x0050b1a0`; when it changes, re-lays out the views (`0x00122ed0`). | confirmed (code) |
| `0x0011e0e0` | `Camera_SetGameAspect` | [`CamSetGameAspect`](../references/bindings/camera.md#camsetgameaspect): Cameras_SetAspectAll (`0x00122ca0`). No script calls it. | confirmed (code) |
| `0x0011e100` | `Camera_SetHudAspect` | [`CamSetHUDAspect`](../references/bindings/camera.md#camsethudaspect): stores the overlay camera aspect `0x0050b208` (`0x00122d38`). No script calls it. | confirmed (code) |
| `0x0011e120` | `Camera_SetHudScale` | [`CamSetHUDScale`](../references/bindings/camera.md#camsethudscale): overlay view-window scale `0x0050b20c`. No script calls it. | confirmed (code) |
| `0x0011e130` | `Camera_RegisterObject` | CamRegisterObject(handle, on): adds (`0x001233c0`) or removes (`0x001233e8`) an object handle in the 4-entry list `0x005d9198`. No script calls it. | confirmed (code) |
| `0x0011e188` | `Camera_GetLastTarget` | [`CamGetLastTarget`](../references/bindings/camera.md#camgetlasttarget): returns the word `0x005d91b8`. No script calls it. | confirmed (code) |
| `0x0011e198` | `Camera_AssignReverseButton` | [`CamAssignRevCamButton`](../references/bindings/camera.md#camassignrevcambutton): the reverse-camera button `0x0050b230`. | confirmed (code) |

### The camera manager {#fn-manager}

`Cam_ICamera.cpp`: the factory, the per-player slots ([Globals](#globals)), the update, the camera stack and the
visibility tests, `0x0011e1b0`-`0x001204f0`.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0011e1b0` | `Camera_Create` | Camera factory (type, name, player): allocates types 0 Cam_Fixed, 1 Cam_Locked, 4 Cam_Scene, 0x10 Cam_3rdPerson; the per-player getters for 2,3,5,7,8 (made on first use); only existing ones for 0xc/0xd. Sets the name (vtable `+0x18c`) and, if it has no player yet, player 0 (vtable `+0x274`). | confirmed (code) |
| `0x0011e440` | `Camera_Release` | Deletes a camera: clears it from a blend (`+0x210` from / `+0x214` to; a scene camera is replaced by the top of the camera stack), from its singleton slot, from the players' base and current slots (a player whose current camera it was falls back to his base follow/rail camera, which is reset), then frees it. param 2 = 1 at level end. | confirmed (code) |
| `0x0011e878` | `Cameras_Update` | Cameras' update (dt): counts the target list `0x005d91a8` into `0x0050b194`; if player 1 has no current and no base camera but a follow camera, makes it both; runs each player's current camera update (vtable `+0x134`). | confirmed (code) |
| `0x0011e978` | `Camera_FromObject` | Returns the object if its class flags (vtable `+0x24`) have 0x200 (a camera), else null. Every handle-taking camera binding uses it. | confirmed (code) |
| `0x0011e9c0` | `Cameras_ClearSlots` | Clears every per-player camera slot (`0x005d9148`-`0x005d918c`) and the single ones (`0x0050b16c` path, `0x0050b170` death, `0x0050b174` win), then re-lays out the views (`0x00122ed0`). | confirmed (code) |
| `0x0011ea80` | `Cameras_ReleaseAll` | Releases every camera in handle slots 0x3c-0x6b (Camera_Release(cam,1)), then Cameras_ClearSlots. | confirmed (code) |
| `0x0011eae0` | `Cameras_GetViewCount` | Returns the number of views/players `0x0050b198`. | confirmed (code) |
| `0x0011eaf0` | `Cameras_AddPlayer` | Gives a new player (index) the kinds of game camera player 1 has (follow, rail, hood), copying their settings (`0x00124c28`, `0x0013b358`, `0x00134ca8`); his base and current camera become the same kind as player 1's base (for a blend, its destination's), activated (vtable `+0x144`). | confirmed (code) |
| `0x0011ed60` | `Cameras_RemovePlayer` | Releases a player's follow, rail and hood cameras and clears his base and current camera. | confirmed (code) |
| `0x0011ee08` | `Camera_MakeActive` | Makes a camera current for a player, optionally blending (Cam_Transition `0x0011fac8`) over the given time; while a scene camera is current it replaces the camera the scene returns to instead. [blends](#blends). | confirmed (code) |
| `0x0011f9b0` | `Camera_GetPlayerCamera` | A player's current camera (`0x005d9150`[player]). | confirmed (code) |
| `0x0011f9c8` | `Camera_GetPlayerBase` | A player's base camera, the one returned to (`0x005d9148`[player]). | confirmed (code) |
| `0x0011f9e0` | `Cam_GetFollow` | A player's Cam_Follow (`0x005d9158`[player]), made on first use when asked (0x480 bytes). | confirmed (code) |
| `0x0011fac8` | `Cam_GetTransition` | A player's blend camera Cam_Transition (`0x005d9180`[player], 0x240 bytes), made on first use when asked. | confirmed (code) |
| `0x0011fbb0` | `Camera_GetPlayerRail` | A player's Cam_Rail (`0x005d9160`[player], 0x3f0 bytes), made on first use when asked. | confirmed (code) |
| `0x0011fc98` | `Cam_GetPower` | A player's power-move camera Cam_Power (`0x005d9188`[player], 0x340 bytes), made on first use when asked. | confirmed (code) |
| `0x0011fd80` | `Cam_GetMug` | A player's mugging camera Cam_Mug (`0x005d9170`[player], 0x250 bytes), made on first use when asked. | confirmed (code) |
| `0x0011fe68` | `Cam_GetMini` | A player's mini-game camera Cam_Mini (`0x005d9178`[player], 0x260 bytes), made on first use when asked. | confirmed (code) |
| `0x0011ff50` | `HoodCam_GetForPlayer` | A player's hood camera Cam_Hood (`0x005d9168`[player]), made on first use when asked. | confirmed (code) |
| `0x00120038` | `Cam_GetPoizo` | The one path camera Cam_Spline (`0x0050b16c`), made on first use when asked. | confirmed (code) |
| `0x001200e0` | `Cam_GetFailed` | The one death camera Cam_Failed (`0x0050b170`, 0x200 bytes), made on first use when asked. | confirmed (code) |
| `0x00120188` | `Cam_GetWin` | The one win camera Cam_Win (`0x0050b174`, 0x200 bytes), made on first use when asked. | confirmed (code) |
| `0x00120230` | `Cameras_MinDistanceSq` | Smallest squared distance from a point to any player's current camera (FLT_MAX with no views). | confirmed (code) |
| `0x001202e8` | `Camera_AnyPlayerCanSeePoint` | Is a point within range of and seen by any player's current camera (Camera_CanSeePoint). | confirmed (code) |
| `0x00120380` | `Cameras_CanSeePointExcept` | As Camera_AnyPlayerCanSeePoint, but a player whose human has `+0x1d8` == 1.0 does not count. | confirmed (code) |
| `0x00120450` | `CameraStack_Push` | Pushes a camera on the 2-deep stack `0x005d9190` (index `0x0050b180`): the camera to return to after a scene. | confirmed (code) |
| `0x00120488` | `CameraStack_Pop` | Pops the camera stack `0x005d9190` (null when empty). | confirmed (code) |
| `0x001204c0` | `CameraStack_Top` | Top of the camera stack `0x005d9190` without popping (null when empty). | confirmed (code) |

### `Cam_3rdPerson` (type 16) {#fn-3rdperson}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001204f0` | `Cam3rdPerson_Construct` | Cam_3rdPerson constructor (0x230 bytes, vtable `0x00535250`): no target, distance 5.1 (`+0x210`), height 1.8 (`+0x214`), fov 50, offset (0,0,1.5), near 0.5, far 75. | confirmed (code) |
| `0x001205c0` | `Cam3rdPerson_Destroy` | Cam_3rdPerson destructor (vtable `+0xc`): restores the vtable and runs the base destructor `0x001209d0`. | confirmed (code) |
| `0x001205e8` | `Cam3rdPerson_Update` | Cam_3rdPerson update (vtable `+0x134`): look-at = target position + offset (`+0x220`); orientation (`+0x200`) slerps 10% to the target's; camera = look-at + orientation × (0,-distance(`+0x210`),0), height (`+0x214`) added, swung by the angle quaternion (`+0x1f0`); shake; look-at matrix (`0x00337028`) to `+0x20`/`+0x10`; base finish `0x00120a30`. | confirmed (code) |
| `0x001207b0` | `Cam3rdPerson_SetAngle` | Cam_3rdPerson: angle in°rees -> quaternion about z at `+0x1f0`. | confirmed (code) |

### The base class `Cam_ICamera` {#fn-base}

The base camera's own methods, the frustum, the views and the shake, `0x00120868`-`0x00123630`. Its fields are in
[The base camera object](#the-base-camera-object).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00120868` | `ICamera_Construct` | Cam_ICamera base constructor (vtable `0x00535510`): fov 60 (`+0x44`), aspect `0x0050b204` (`+0x4c`), far 60 (`+0x54`), near 0.3 (`+0x50`), draw distance 60 (`0x00122128`), identity orientation/position, shake cleared, flags `+0x1d6`/`+0x1d7` = 1, player byte `+0x1d8` = -1; registers a handle (`+0x40`, `0x0038ffd0`). | confirmed (code) |
| `0x001209d0` | `ICamera_Destroy` | Cam_ICamera destructor: frees the handle (`+0x40`, `0x00390038`); deletes when flag bit 1. | confirmed (code) |
| `0x00120a30` | `ICamera_FinishUpdate` | Base update tail (vtable `+0x134` of the base and called by every class's update): stores dt (`+0x1a0`), rebuilds the frustum (`0x00120c50`), probes the ground under the view (`0x00121720`) and decides the views (`0x00121888`). | confirmed (code) |
| `0x00120a68` | `Camera_UpdateViewState` | Runs the ground probe `0x00121720` and the view decision `0x00121888`. | confirmed (code) |
| `0x00120a98` | `Camera_UpdateViewWindow` | View window from the field of view: `+0x48` fov used, `+0x5c`/`+0x60` tan(half) × aspect × 0.75 / tan(half) × 0.75, copies at `+0x64`/`+0x68`; split screen (`0x0050b1a0`) halves or doubles them. [The streamed world](world.md#player-camera). | confirmed (code) |
| `0x00120c50` | `Camera_BuildFrustum` | Builds the 6 frustum planes (`+0x70`..`+0xc0`) from the near and far corner points (`+0xd0`..`+0x14c`, in world space by the camera matrix or the given one) and, unless asked not to, a bounding sphere (`+0x150` centre, `+0x17c` radius). | confirmed (code) |
| `0x001210f8` | `Cam_StartShake` | Base shake start (vtable `+0x15c` entry, call slot `+0x15c`): level 1-3 sets amplitude 0.5/0.75/1.0, time 0.10/0.15/0.18 s, rumble base 0/0x30/0x60; 0 stops. [shake](#shake). | confirmed (code) |
| `0x00121298` | `Cam_UpdateShake` | Shake and rumble update (every camera kind): eases the amplitude 65% toward the level's, counts the time down, drives the pad rumble byte (`+0x41`) and adds a random view offset while switch 6 is on. [shake](#shake). | confirmed (code) |
| `0x00121720` | `Camera_ProbeGroundFlags` | Casts a 25 m ray straight down from 1.5 m along the camera's direction; the triangle hit sets `+0x1d4` (type bit 0x20) and `+0x1d5` (bit 0x10); for a player's current camera the 0x20 flag goes to his colour controller (`0x0018bdd0`, `0x005fdeb8`[player]). | confirmed (code) |
| `0x00121888` | `Cameras_DecideViews` | Each update: decides which view each player shows (switches 3/13, Camera_HumanCounts) and sets the last target `0x005d91b8`. [switches](#switches). | confirmed (code) |
| `0x00121e30` | `ICamera_IsViewEnabled` | Vtable `+0x28c`: a camera's view counts when its byte `+0x1d6` is set and its player's flag `0x0050b1e8`[player] is set (a locked camera shared by both players uses player 1's). | confirmed (code) |
| `0x00121f50` | `ICamera_TestObjectVisible` | Vtable `+0x164`: frustum test (`+0x16c`) of an object's bounding sphere (object vtables `+0x64` centre, `+0x5c` radius). | confirmed (code) |
| `0x00121fd8` | `Camera_FrustumTestSphere` | Vtable `+0x16c`: sphere vs the 6 frustum planes; 0 inside, 1 crossing a plane, 2 outside. | confirmed (code) |
| `0x00122060` | `Camera_FrustumTestBox` | Vtable `+0x184`: a box's 8 corners (`0x00338178`) vs the frustum planes; 0 outside, else 1 when partly inside. | confirmed (code) |
| `0x00122128` | `Camera_SetDrawDistance` | Vtable `+0x1b4`: draw distance `+0x58`; also the device camera's far clip while `0x0050c698` is set. | confirmed (code) |
| `0x001221a0` | `Camera_StoreScriptRef` | Vtable `+0x1e4`: stores a value from the script system (`0x00512b04` slot `+0xcc`) at `+0x1cc` (inferred: the Lua callback to run at the end). | inferred |
| `0x001221e0` | `Camera_CallScriptRef` | Vtable `+0x14c`: when `+0x1cc` is set and the script system is ready (slot `+0x4c`), calls it (slot `+0x8c`) (inferred). | inferred |
| `0x00122248` | `CameraTargets_Contains` | Is an object's handle in the shared target list `0x005d91a8`. | confirmed (code) |
| `0x001222b0` | `CameraTargets_Add` | Adds an object's handle to the shared target list `0x005d91a8` (once). | confirmed (code) |
| `0x00122438` | `CameraTargets_Remove` | Removes an object from the target list `0x005d91a8`; optionally recounts `0x0050b194`. | confirmed (code) |
| `0x001224c0` | `CameraTargets_Swap` | Swaps two entries of the target list `0x005d91a8`. | confirmed (code) |
| `0x00122548` | `Camera_CanSeePoint` | Is a point within range (capped at the camera's far clip) of a camera, inside its frustum and with no collision hit on the ray from the camera. | confirmed (code) |
| `0x00122728` | `Camera_PushOutOfHumans` | Moves a camera point out of nearby humans (handles 0-0x3b, two may be skipped): a human whose radius (`0x00219860`, scale × 0.065 more) plus the near plane reaches the point and whose height span holds it pushes the point out along the view's side axis (inferred). | inferred |
| `0x00122b80` | `Cameras_ResetLevel` | Level camera reset: clears the target and object lists, sets the switches to their defaults ([switches](../references/cameras.md#switch)), split mode 0, re-lays out the views. | confirmed (code) |
| `0x00122ca0` | `Cameras_SetAspectAll` | Stores the game aspect `0x0050b204` and applies it (vtable `+0x19c`) to every camera in handle slots 0x3c-0x6b except blends (type 5). | confirmed (code) |
| `0x00122d38` | `Camera_StoreHudAspect` | Stores the overlay camera aspect `0x0050b208`. | confirmed (code) |
| `0x00122d48` | `Camera_AdjustScreenPoint` | Moves a normalised screen point (0..1) for the view: × away from the centre by half of (player 1's camera aspect × 0.75 - 1), y by half of (the device's vertical scale - 1). HUD, captions, prompts and arrows use it to stay on screen. | confirmed (code) |
| `0x00122ed0` | `Cameras_LayoutViews` | Counts the views: a player's base camera shows when it counts (vtable `+0x28c`) and switch 3 is on; two players on the same locked camera at the same place share one view. Sets `0x0050b198` views, `0x0050b19c` players, fov scaled by `0x0050b200` (at least 40) when going to two views, and the view grid `0x0050b1a8`/`0x0050b1aa` from split mode (0: 2 × 1, 1: 1 × 2, 2: 2 × 2). | confirmed (code) |
| `0x001233c0` | `CameraObjects_Add` | Appends a handle to the registered-object list `0x005d9198`. | confirmed (code) |
| `0x001233e8` | `CameraObjects_Remove` | Removes a handle from the registered-object list `0x005d9198` if present. | confirmed (code) |
| `0x00123428` | `CameraObjects_SendMessage12` | Sends task message 0x12 to the task of each of the 4 registered objects (`0x005d9198`) that is set. | confirmed (code) |
| `0x001234e0` | `CameraObjects_Clear` | Clears the registered-object list `0x005d9198`. | confirmed (code) |
| `0x00123500` | `Camera_HumanCounts` | Does a human still count for the cameras: 0 while down or dead, or in state `0x00223b70` unless unlock 6/15 or a `0x0041e420` count is set. [switches](#switches). | confirmed (code) |
| `0x00123588` | `Cam_ICamera_StaticInit` | Static init/exit of Cam_ICamera.cpp: clears the object list `0x005d9198` and target list `0x005d91a8` to -1 and the last target. | confirmed (code) |
| `0x00123610` | `Cam_ICamera_StaticInitStub` | Static-init stub of Cam_ICamera.cpp: `0x00123588`(1, 0xffff). | confirmed (code) |

### `Cam_Failed`, the death camera (type 12) {#fn-failed}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00123630` | `CamFailed_Construct` | Cam_Failed (death camera, type 12) constructor, 0x200 bytes, vtable `0x005357d0`: no human (`+0x1e0`), height 5 (`+0x1e8`), turn rate 1 (`+0x1ec`), `+0x1f0`/`+0x1f4` 1, rotating flag `+0x1fe` = 1. | confirmed (code) |
| `0x001236b8` | `CamFailed_Destroy` | Cam_Failed destructor. | confirmed (code) |
| `0x001236e0` | `CamFailed_Activate` | Death camera activation (vtable `+0x144`): looks at the last target `0x005d91b8` if it is a player human, else at whichever player is nearer the previous camera (`+0x1e4`); then places itself (`0x001238a8`). | confirmed (code) |
| `0x00123888` | `CamFailed_SetTarget` | Death camera: sets the human it looks at (`+0x1e0`) and places the camera (`0x001238a8`). | confirmed (code) |
| `0x001238a8` | `CamFailed_Place` | Death camera placement: look-at = the human's position (`+0x180`); height (`+0x1e8`) = an upward ray of near + 5 m (mask 0x200) less the near plane; camera straight above, looking down (pitch about -88°) at a random yaw (Random_Int(36) × 10° - 180°); rotating flag set. | confirmed (code) |
| `0x00123ba0` | `CamFailed_Deactivate` | Death camera vtable `+0x154`: releases itself when it is no longer its player's current camera. | confirmed (code) |
| `0x00123bf0` | `CamFailed_Update` | Death camera update: while rotating, yaws by turn rate × 15°/s and closes in on the body at 0.18 m/s down to 2.5 m; when player 1's colour controller has faded fully (`+0x1d8` >= 1, `+0x1dc` > 0) starts the next fade (`0x0018d058`) and stops rotating. | confirmed (code) |

### `Cam_Fixed` (type 0) {#fn-fixed}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00123ea0` | `CamFixed_Construct` | Cam_Fixed (type 0) constructor, 0x210 bytes, vtable `0x00535a90`: no target (`+0x200`), snap flag (`+0x204`) 0, offset (`+0x1f0`) zero. | confirmed (code) |
| `0x00123f08` | `CamFixed_Destroy` | Cam_Fixed destructor. | confirmed (code) |
| `0x00123f30` | `CamFixed_Activate` | Fixed camera activation (vtable `+0x144`): target = the player's entry of the target list; computes the look-at (CamFixed_ComputeLookAt), finishes an update, then resets (vtable `+0x13c`). | confirmed (code) |
| `0x00123fb8` | `CamFixed_Reset` | Fixed camera reset (vtable `+0x13c`, CameraReset): runs one update of 1/60 s and sets the snap flag `+0x204` so the next update does not smooth. | confirmed (code) |
| `0x00124000` | `CamFixed_ComputeLookAt` | Fixed camera: look-at point = the average position of the listed targets that still count, plus the offset. | confirmed (code) |
| `0x00124248` | `CamFixed_SmoothLookAt` | Fixed camera: damps the new look-at point (`+0x180`) and aim point (`+0x1f0`) against last update's: a rise over 0.5 m halves then quarters, a jump over 1 m quarters, a rise of 0.1-0.5 m eases by 1 - 1.875(dz - 0.1), a move of 0.4-1 m by 1 - 1.25(h - 0.4) (the rail camera's damping, [rail](#rail)). | confirmed (code) |
| `0x00124690` | `CamFixed_Update` | Fixed camera update: with targets, recomputes and (unless snapped) smooths the look-at, shakes, and faces it (`0x00337028`); base finish; clears the snap flag. | confirmed (code) |

### `Cam_Follow` (type 2) {#fn-follow}

`Cam_Follow.cpp`, `0x00124778`-`0x00134b90`; the behaviour is under [Behaviour](#setting-up).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00124778` | `Cam_Follow_Construct` | Cam_Follow constructor (0x480 bytes, vtable `0x00535d50`): distances 4/9/6, pitch 15°, fov 65, offset (0,0,1.4), lag 0.22, pitch limits -20..50°, near 0.1, far `0x005d91c0` (115); clears the latches and the camera-obstacle list. [the follow camera object](#the-follow-camera-object). | confirmed (code) |
| `0x00124be0` | `Cam_Follow_Destroy` | Cam_Follow destructor. | confirmed (code) |
| `0x00124c28` | `Cam_Follow_CopySettings` | Copies another follow camera's distances, pitch, fov, near plane and offset into this one, then applies them (`0x00125888`). Used when a second player joins. | confirmed (code) |
| `0x00124d00` | `Cam_Follow_Reset` | Follow camera reset (vtable `+0x13c`, CameraReset): place behind the target (`0x00124f38`(180,-1)), target pitch and fov back to the configured ones, line-of-sight pull-in. [setting up](#setting-up). | confirmed (code) |
| `0x00124f38` | `Cam_Follow_PlaceBehind` | Places the camera at a heading from the target's facing (180 = behind) at a distance picked from the band presets (min/default/max-0.5) and sets the zoom step. [setting up](#setting-up). | confirmed (code) |
| `0x001250a8` | `Cam_Follow_PlaceAtHeading` | Puts the camera at a heading relative to the target's facing and a distance about the look-at point, pitch to the target pitch; optionally sets the band to that distance (+0.5 deep) and the zoom step from it. | confirmed (code) |
| `0x001254f0` | `Cam_Follow_StepZoom` | Sets the zoom step `+0x400` and the upper pitch limit `+0x3ac` from a distance (50/40/30°). [pitch](#pitch). | confirmed (code) |
| `0x00125588` | `Cam_Follow_GetZoomLevel` | Zoom level from the zoom step `+0x400`: 2 far, 1 default, 0 close (one player); 1 or 2 with two. [update step 7](#update). | confirmed (code) |
| `0x001255f8` | `Cam_Follow_SetHeading` | [`CamSetFollowHeading`](../references/bindings/camera.md#camsetfollowheading) core: snaps the look-at point, turns the camera to the given orientation's horizontal forward, stands it at the band's near edge, sets the wanted and previous positions and runs one 1/60 s update. | confirmed (code) |
| `0x00125870` | `Cam_Follow_Nop` | Empty method called by the follow destructor and deactivation. | confirmed (code) |
| `0x00125888` | `Cam_Follow_ApplyConfig` | Turns the view to the configured pitch at once and eases the fov to its new value over 1 s (`+0x39c`); height-hold ease scale `+0x398` = 0.25. [setting up](#setting-up). | confirmed (code) |
| `0x001259e8` | `Cam_Follow_SetMinDistance` | Sets the minimum distance `+0x300` (clamped against the others). | confirmed (code) |
| `0x00125a40` | `Cam_Follow_SetMaxDistance` | Sets the maximum distance `+0x304` and recomputes the lower pitch limit `+0x3b0`. | confirmed (code) |
| `0x00125ac8` | `Cam_Follow_SetDefaultDistance` | Sets the default distance `+0x308` and the band default .. default + min(0.5, max - min). | confirmed (code) |
| `0x00125b60` | `Cam_Follow_SetTargetPitch` | [`CamSetFollowAngle`](../references/bindings/camera.md#camsetfollowangle) core: target pitch (degrees) clamped to [`+0x3b0`, `+0x3ac`]; with snap, turns the view to it at once and sets the wanted/previous positions; clears the sprint latch `+0x466`. | confirmed (code) |
| `0x00125c50` | `Cam_Follow_SetPosition` | [`CamSetFollowPos`](../references/bindings/camera.md#camsetfollowpos) core: snaps the look-at point, places the camera at a world position and sets its wanted and lag copies to it. | confirmed (code) |
| `0x00125cc0` | `Cam_Follow_Activate` | Follow activation (vtable `+0x144`): recount targets, snap look-at, distance clamped to the band, hard band = band, wanted near edge cleared, recovered distance = near edge. [setting up](#setting-up). | confirmed (code) |
| `0x00125e50` | `Cam_Follow_ActivateStep` | Follow activation step: places the camera at the band and snaps the look-at point; arms the split timers. | confirmed (code) |
| `0x001260e8` | `Cam_Follow_Deactivate` | Follow vtable `+0x154` (another camera taking over): ends a target takeover whose player left, re-lays out split views, clears the close-up flash (`0x00126370`), applies a pending heading target at once. | confirmed (code) |
| `0x00126370` | `Cam_Follow_ClearFlash` | For both players' follow cameras clears the flash alpha `+0x478` and timer `+0x424`, and the colour controllers' alpha byte `+0x1b4`. | confirmed (code) |
| `0x001263e8` | `Cam_Follow_StartShake` | Follow camera shake start (vtable call slot `+0x15c`): in the combat camera shakes are 0.66 as strong and the rumble thresholds lowered. [shake](#shake). | confirmed (code) |
| `0x00126450` | `CameraObstacles_Add` | Adds a world object to the camera-obstacle list `0x00715280` (31 entries, count `0x0050b238`); WorldObject_Init calls it. | confirmed (code) |
| `0x00126488` | `CameraObstacles_Remove` | Removes a world object from the camera-obstacle list `0x00715280`. | confirmed (code) |
| `0x00126520` | `CameraObstacles_Clear` | Clears the camera-obstacle list `0x00715280` (the follow constructor calls it). | confirmed (code) |
| `0x00126558` | `Cam_Follow_TakeOverTarget` | One player: an AI goal (`0x002b8ad8`) hands the follow camera another human as its target, 2.5-30 m from player 1: target swapped in the target list, placed behind, target pitch -5°, fov 70, minimum distance 2 m less, band at the minimum, `+0x465` set; the old values are saved for `0x00126878`. | confirmed (code) |
| `0x00126878` | `Cam_Follow_EndTakeOver` | Ends Cam_Follow_TakeOverTarget: player 1 back in the target list, fov, target pitch, minimum distance and band restored, `+0x465` cleared. | confirmed (code) |
| `0x00126a30` | `Cam_Follow_EnableSprintZoom` | CamEnable(5): the sprint zoom switch `+0x468`; off also clears its state. [sprint zoom](#sprint-zoom). | confirmed (code) |
| `0x00126a68` | `Cam_Follow_ArmSplitTimers` | Sets the two co-op split/merge timers `+0x41c`/`+0x420` to 0.72 s so the next decision acts at once. | confirmed (code) |
| `0x00126a88` | `Cam_Follow_CountdownFlash` | Counts `+0x476` up to `+0x477`; then, on a player's current camera with a player target, a small shake and (unless his colour controller is fading) a flash: alpha 225 (`+0x478`) on the colour controller, timer `+0x424` = 0.2 s. Used when co-op views split or merge. | confirmed (code) |
| `0x00126c40` | `Cam_Follow_DecideSplit` | Co-op (2 players): decides whether the players share one view or split the screen. Merge needs both in view, within 11 m and no colour controller busy; split/merge waits for `+0x41c`/`+0x420` to pass 0.72 s; toggles the follow cameras' views (vtable `+0x294`) and re-lays out (Cameras_LayoutViews). | confirmed (code) |
| `0x001276a0` | `Cam_Follow_OnViewCountChange` | Co-op: when the view count changed, re-snaps the look-at points, re-applies band and zoom step for the new count, resets the target pitch to 25° for a player target and schedules the flash (`0x00126a88`). | confirmed (code) |
| `0x00127ac8` | `Cam_Follow_UpdateSplitScreen` | Co-op step of the follow update: with one listed target merges to player 1's view, else DecideSplit; then OnViewCountChange. | confirmed (code) |
| `0x00127c10` | `Cam_Follow_Prime` | Run when a follow camera is made current (Camera_MakeActive, the view decision, GameState_SyncPlayers): one 1/60 s update (or the view decision the first time), arms the split timers, gathers targets, split-screen step, another update when the player has a base camera. | confirmed (code) |
| `0x00127cd8` | `Cam_Follow_GetFocusPoint` | Follow vtable `+0x24c`: the point it frames: the look-at point `+0x180`, or `+0x2d0` while a watched player (`+0x320`) is kept in view of a non-player target. | confirmed (code) |
| `0x00127d48` | `Cam_Follow_RefreshLookAt` | Gathers the targets (`0x001282a0`) and recomputes the look-at point (`0x00127d88`, with snap). | confirmed (code) |
| `0x00127d88` | `Cam_Follow_LookAt` | Look-at point = target + offset, its move limited by length (20% above 0.8 m); hidden-in-shadow height 1.65 m. [update step 1](#update). | confirmed (code) |
| `0x00128298` | `Cam_Follow_Nop2` | Empty function. | confirmed (code) |
| `0x001282a0` | `Cam_Follow_GatherTargets` | Takes the follow camera's targets from the first two entries of the target list (`+0x314`/`+0x318`, count `+0x444`), falling back to the last target `+0x31c`. [script calls](#script-calls). | confirmed (code) |
| `0x00128678` | `Cam_Follow_AimView` | One player target with `+0x454` set: heading target 15° off the player's per-player aim vector (record `+0x5750`), upper pitch limit 50°, lower limit from (0.075 - offset z) / max distance (at least -20°), and a pitch override; raises the turn rate up to 4x while the view is far off (inferred: an aiming view). | inferred |
| `0x00128b20` | `Cam_Follow_HeadLook` | Turns the player's head toward the camera heading while the right stick turns the camera (within 150° of the facing). [update step 10](#update). | confirmed (code) |
| `0x00128cf0` | `Cam_Follow_SprintZoom` | Sprint zoom: pulls the band in to the minimum and the target pitch to 7° while sprinting, back 250 ms after. [sprint zoom](#sprint-zoom). | confirmed (code) |
| `0x00129050` | `Cam_Follow_RightStick` | Right stick yaw/pitch rates, zoom and centre buttons, look-behind (reverse) button into `+0x458`. [right stick](#right-stick). | confirmed (code) |
| `0x001298c0` | `Cam_Follow_LookBehind` | Look-behind while the reverse button is held (`+0x458` = pad): compares the target's facing with the view; past 75° pulls the band out to 8.5 m (`+0x455`, band saved in `+0x35c`), back within 45° restores it, and swings the view to face the player. | confirmed (code) |
| `0x00129c78` | `Cam_Follow_AutoFollow` | Auto-follow gate: picks the default (`0x0012a400`) or auto-centre (`0x00129f88`) rule. [heading](#heading). | confirmed (code) |
| `0x00129f88` | `Cam_Follow_AutoCentre` | Auto-centre rule: rate from the angle between the facing and the view. [heading](#heading). | confirmed (code) |
| `0x0012a400` | `Cam_Follow_AutoFollowDefault` | Default auto-follow rule (22.5-157.5°). [heading](#heading). | confirmed (code) |
| `0x0012a7d8` | `Cam_Follow_HeightProbe` | Height probe: short ray (flag 0x200) setting the height-hold state `+0x453`. [update step 7](#update). | confirmed (code) |
| `0x0012aae0` | `Cam_Follow_EaseBand` | Eases the band's near edge to the wanted `+0x34c` and steps the zoom. [sprint zoom step 4](#sprint-zoom). | confirmed (code) |
| `0x0012ac58` | `Cam_Follow_EaseRoll` | Eases the roll `+0x408` toward `+0x404` over the timer `+0x40c` (else 60°/s) and rolls the view by it (inferred: roll about the view axis). | inferred |
| `0x0012adf0` | `Cam_Follow_UpdateEntry` | Follow update (vtable `+0x134`): clears the yaw rate `+0x360`, runs the flash countdown when armed, then Cam_Follow_Update when dt > 0. | confirmed (code) |
| `0x0012ae58` | `Cam_Follow_Update` | The follow camera's update: look-at, fov, stick, auto-follow, leash, pitch, height hold, heading target, lag, hard band, collision. [update](#update). | confirmed (code) |
| `0x0012d3b8` | `Cam_Follow_YawAtRate` | Yaws the camera about the look-at point by rate × dt and stores the rate `+0x360`. | confirmed (code) |
| `0x0012d4e8` | `Cam_Follow_Pitch` | Pitch about the look-at point, clamped to [`+0x3b0`, `+0x3ac`]. [pitch](#pitch). | confirmed (code) |
| `0x0012d688` | `Cam_Follow_Yaw` | Yaw about the look-at point. [heading](#heading). | confirmed (code) |
| `0x0012d7a8` | `Cam_Follow_MoveBand` | Moves the distance band and recomputes the lower pitch limit. [pitch](#pitch). | confirmed (code) |
| `0x0012d908` | `Cam_Follow_GetViewPitch` | Current pitch of the view (from the camera to the look-at point). | confirmed (code) |
| `0x0012da20` | `Cam_Follow_ReactToLedge` | Called by human code (`0x0023dc58`) at a ledge: heading target the given heading +-92°, target pitch the upper limit - 5° (old saved in `+0x3d4`), sprint zoom cancelled, over the given time (0.35 s for 0). | confirmed (code) |
| `0x0012de48` | `Cam_Follow_TurnToQuat` | Converts an orientation quaternion to a matrix and calls Cam_Follow_TurnToMatrix (Cam_Mug, `0x00137e08` / `0x00138b90`). | confirmed (code) |
| `0x0012ded8` | `Cam_Follow_TurnToMatrix` | Sets a timed heading target (`+0x350`, the matrix's heading turned 180°) and pitch override (`+0x354`) over the given times, `+0x475` set; optionally merges co-op views. Used when mug and mini-game cameras hand back. | confirmed (code) |
| `0x0012e170` | `Cam_Follow_KeepInView` | Keeps a watched target in view, yaw at most 270°/s. [heading](#heading). | confirmed (code) |
| `0x0012e9a8` | `Cam_Follow_FrameEnemy` | Combat camera: holds the enemy 27° off centre. [combat camera](#combat-camera). | confirmed (code) |
| `0x0012ec50` | `Cam_Follow_TiltOverObstacles` | Against each object of the camera-obstacle list `0x00715280` near the camera, raises a pitch offset `+0x3a8` (eased 90%) so the view clears it; decays 10% per update when clear (inferred). | inferred |
| `0x0012f3e0` | `Cam_Follow_SlopePitch` | From the height ray's ground normal, eases a pitch offset `+0x3a4` 10% per update toward the slope's angle (sets `+0x460`); 0 on flat ground. | confirmed (code) |
| `0x0012f858` | `Cam_Follow_RaiseWhenBlocked` | When the collision step pulls the camera in, raises the wanted position toward a height over the look-at, chooses a clear heading (`0x0012fd20`) for one target, and sphere-pushes the camera. | confirmed (code) |
| `0x0012fd20` | `Cam_Follow_ChooseClearHeading` | Blocked view, one target: scores 9 headings round the target with ray casts (3 each) and stores the best as the swing heading `+0x43c`; latches `+0x462`..`+0x464`. | confirmed (code) |
| `0x001303c8` | `Cam_Follow_SphereTest` | Switch 1: a physics bound sphere (radius = near plane) at the camera tested against physics objects of kinds 0xc-0xe, 0x10 and 0x1c (vehicles, inferred); keeps the camera out of them (`+0x410` timer 0.15 s). | inferred |
| `0x00130990` | `Cam_Follow_Collide` | Follow world collision: rays, side probes, swing away, sphere pushes, next update's lag. [collision](#collision). | confirmed (code) |
| `0x00134ab0` | `Cam_Follow_StaticInit` | Static init of Cam_Follow.cpp: far clip 115 (`0x005d91c0`) and cosines of 75, 45, 53.13 and 10° (`0x005d91c4`..`0x005d91d0`). | confirmed (code) |
| `0x00134b70` | `Cam_Follow_StaticInitStub` | Static-init stub of Cam_Follow.cpp. | confirmed (code) |

### `Cam_Hood` (type 11) {#fn-hood}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00134b90` | `CamHood_Construct` | Cam_Hood (type 11) constructor, 0x220 bytes, vtable `0x00536010`: no vehicle (`+0x1e0`), mount offset `+0x1f0` (0, 0, 1.53), look offset `+0x200` (0, 0.3, 1.7), fov 60 (also the wanted fov `+0x214`), near 0.1, far 115. | confirmed (code) |
| `0x00134c80` | `CamHood_Destroy` | Cam_Hood destructor. | confirmed (code) |
| `0x00134ca8` | `CamHood_CopySettings` | Copies another hood camera's vehicle, mount and look offsets, wanted fov and fov (Cameras_AddPlayer). | confirmed (code) |
| `0x00134d28` | `CamHood_Activate` | Hood camera activation (vtable `+0x144`): placed on its vehicle's mount at once, then a base update. | confirmed (code) |
| `0x00134d78` | `HoodCam_PlaceOnMount` | Hood camera: position = the vehicle's position (an object with class flag 0x40: its transform-table entry) plus the mount offset in the vehicle's frame; orientation from the vehicle. | confirmed (code) |
| `0x00134f08` | `HoodCam_AimAtTargets` | Hood camera: look-at point = the mean position of the counted targets plus the look offset's height. | confirmed (code) |
| `0x00135028` | `CamHood_GatherTargets` | Hood camera: the counted players among the target list's first two entries (count `+0x218`), else the last target kept in `+0x210`. | confirmed (code) |
| `0x00135138` | `HoodCam_Update` | Hood camera update: on the mount, the fov stepped toward `+0x214` by at most dt°rees, aimed at the targets, base finish. | confirmed (code) |

### `Cam_Locked` (type 1) {#fn-locked}

[Locked cameras](#locked-cameras).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001352f0` | `CamLocked_Construct` | Cam_Locked (type 1) constructor, 0x220 bytes, vtable `0x005362d0`: heading, pitch, roll 0 (`+0x200`..`+0x208`), kept-in-view list (`+0x20c`, two handles) cleared. | confirmed (code) |
| `0x00135390` | `CamLocked_Destroy` | Cam_Locked destructor. | confirmed (code) |
| `0x001353b8` | `CamLocked_SetAngles` | Locked camera: heading, pitch and roll in°rees (`+0x200`/`+0x204`/`+0x208`), then the orientation (`0x001353e0`). | confirmed (code) |
| `0x001353e0` | `CamLocked_BuildOrientation` | Locked camera orientation = rotation about z by the heading × about × by the pitch × about y by the roll. | confirmed (code) |
| `0x001355f8` | `CamLocked_Activate` | Locked camera activation (vtable `+0x144`): one base update of 1/60 s. | confirmed (code) |
| `0x00135680` | `CamLocked_Update` | Locked camera update: placed, aimed 3 m ahead, kept out of walls, keeps its listed humans inside the view's sides, shake. [locked cameras](#locked-cameras). | confirmed (code) |
| `0x001358d0` | `CamLocked_SetKeptHuman` | [`CamLockLocked`](../references/bindings/camera.md#camlocklocked) core: adds or removes a human in the locked camera's kept-in-view list (two entries). | confirmed (code) |
| `0x00135960` | `CamLocked_PushHumanIntoView` | Pushes a listed human back inside the view when within 0.3 m of the left or right frustum plane, with a world ray and a ground snap. [locked cameras](#locked-cameras). | confirmed (code) |
| `0x00135ca8` | `LockedCam_KeepHumansInView` | Runs the push for each human of the locked camera's list. [locked cameras](#locked-cameras). | confirmed (code) |

### `Cam_Mini`, the mini-game camera (type 8) {#fn-mini}

`Cam_Mini.cpp`: the shots of the mount and stereo-theft mini-games ([Mini-game and mugging cameras](#mini-mug)).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00136068` | `CamMini_Construct` | Cam_Mini (type 8, Minigame) constructor, 0x260 bytes, vtable `0x00536590`: two humans (`+0x1e0`/`+0x1e4`), the kind (`+0x25c`), the camera to return to (`+0x240`), shot distance `+0x254` (20, set to 3 by the set-ups), done flag `+0x258`. | confirmed (code) |
| `0x00136160` | `CamMini_Destroy` | Cam_Mini destructor. | confirmed (code) |
| `0x00136188` | `CamMini_Deactivate` | Mini-game camera vtable `+0x154`: releases itself. | confirmed (code) |
| `0x001361a8` | `CamMini_Start` | Mini-game camera start (kind 0 from Player_UpdateMounting via `0x0022d030`, kind 2 from StereoTheft_Start): saves the base camera to return to, copies its fov and near plane, places the shot (`0x00137670`); from a follow camera the follow is turned to the shot's orientation for the hand-back (Cam_Follow_TurnToMatrix, 0.15 s). | confirmed (code) |
| `0x001363e8` | `CamMini_PlaceKind0` | Mini-game shot for kind 0: about the humans' midpoint at 3 m, a yaw and a pitch picked at random from five-entry tables (up to 3 tries until `0x001374d8` finds it clear), else a fixed 36° pitch. | confirmed (code) |
| `0x00136a88` | `CamMini_PlaceKind1` | Mini-game shot for kind 1: as kind 0 with other yaw and pitch tables (fallback pitch 32°). No caller passes kind 1. | confirmed (code) |
| `0x00136f68` | `CamMini_PlaceKind2` | Mini-game shot for kind 2 (the stereo): about the stereo object, a random height 3-7° and one of eight yaws, up to 2.3 m, tried with `0x001374d8`. | confirmed (code) |
| `0x001374d8` | `CamMini_TestShot` | Tests a mini-game shot: a sphere sweep from the look-at point toward the camera; true when the clear distance reaches the wanted one (`+0x254`). | confirmed (code) |
| `0x00137670` | `CamMini_PlaceShot` | Mini-game shot set-up: a physics bound sphere (near-plane radius) for the tests, then the kind's placement (`0x001363e8` / `0x00136a88` / `0x00136f68`). | confirmed (code) |
| `0x00137808` | `CamMini_Update` | Mini-game camera update: keeps the humans framed (turns at most 3x the angle error per second); when the done flag `+0x258` is set (`0x0022d1f8`) goes back to the saved camera with a 0.3 s blend. | confirmed (code) |
| `0x00137c68` | `CamMini_StaticInit` | Static init of Cam_Mini.cpp. | confirmed (code) |
| `0x00137cb0` | `CamMini_StaticInitStub` | Static-init stub of Cam_Mini.cpp. | confirmed (code) |

### `Cam_Mug`, the mugging camera (type 7) {#fn-mug}

`Cam_Mug.cpp`, [Mini-game and mugging cameras](#mini-mug).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00137cd0` | `CamMug_Construct` | Cam_Mug (type 7) constructor, 0x250 bytes, vtable `0x00536850`: mugger, victim (`+0x1e0`/`+0x1e4`), intro sound handle `+0x1ec`, look-at offset `+0x200` (0, 0, 1.45), fov ease time `+0x244` 0.33 s. | confirmed (code) |
| `0x00137dc0` | `CamMug_Destroy` | Cam_Mug destructor. | confirmed (code) |
| `0x00137de8` | `CamMug_Deactivate` | Mugging camera vtable `+0x154`: releases itself. | confirmed (code) |
| `0x00137e08` | `CamMug_Start` | Mugging camera start (`0x002729a8`): only from a single-view follow camera; saves it (`+0x230`), copies its fov and near, places the shot (`0x00138078`) and plays vags/misc/mug_intro (2D). | confirmed (code) |
| `0x00138078` | `CamMug_PlaceShot` | Mugging shot: about the pair, a random yaw and pitch from small tables, distance 2.5 m (1.8 m when blocked), tested with a physics sphere against vehicles and the collision mesh. | confirmed (code) |
| `0x00138b90` | `CamMug_Update` | Mugging camera update: eases the fov to 50 over `+0x244`, turns at most 135°/s to keep both humans framed, keeps 3 m from humans (Camera_PushOutOfHumans); when the mugging ends stops the intro, plays vags/misc/mug_outro, turns the follow camera (Cam_Follow_TurnToQuat) and blends back in 0.3 s. | confirmed (code) |
| `0x00139d00` | `CamMug_StaticInit` | Static init of Cam_Mug.cpp: `0x005d91d8` = tan(5°). | confirmed (code) |
| `0x00139d48` | `CamMug_StaticInitStub` | Static-init stub of Cam_Mug.cpp. | confirmed (code) |

### `Cam_Power`, the power-move camera (type 6) {#fn-power}

`Cam_Power.cpp`, [Power-move camera](#power-camera).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00139d68` | `CamPower_Construct` | Cam_Power (type 6) constructor, 0x340 bytes, vtable `0x00536b10`: no human (`+0x320`), shot count `+0x331` = -99 (none), mode `+0x330` = 2. | confirmed (code) |
| `0x00139df8` | `CamPower_Destroy` | Cam_Power destructor. | confirmed (code) |
| `0x00139e20` | `CamPower_Deactivate` | Power camera vtable `+0x154`: releases itself. | confirmed (code) |
| `0x00139e40` | `CamPower_GetOrientation` | Power camera vtable `+0x22c`: the orientation of the camera it came from (`+0x334`), else its own. | confirmed (code) |
| `0x00139e90` | `CamPower_StartShake` | Power camera shake (vtable `+0x15c`): only for a human on the target list. | confirmed (code) |
| `0x00139ef8` | `CamPower_Update` | Power camera update: holds the current shot; ends (`0x0013a3b0`) when the human is gone, down or dead, or has left the power move (and switch 9 is on). | confirmed (code) |
| `0x0013a170` | `CamPower_Begin` | Animation event: begins a power-move shot sequence for a human (or the previous camera's target): saves the current camera (`+0x334`, or its own when already a power camera), copies its near, far and fov, records the human's position and orientation, mode 1 or 2. | confirmed (code) |
| `0x0013a3b0` | `CamPower_End` | Ends the power camera: back to the saved camera at once (no blend) when it is a player's current camera; shots cleared. | confirmed (code) |
| `0x0013a448` | `CamPower_AddShot` | Animation event: stores shot n (of 8) as a position (`+0x1e0`) and orientation (`+0x280`) relative to the human. | confirmed (code) |
| `0x0013a548` | `CamPower_CutToShot` | Animation event (type 0x39 with switch 9): cuts to a stored shot: placed at it relative to the human (with the sphere test `0x0013a818`), made the player's current camera. | confirmed (code) |
| `0x0013a818` | `CamPower_PlaceShot` | Places a power shot: the shot relative to the human's transform, pulled in by a physics bound sphere and ray test so it is not inside walls. | confirmed (code) |

### `Cam_Rail` (type 9) {#fn-rail}

[Rail cameras](#rail).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x0013b118` | `CamRail_Construct` | Cam_Rail (type 9) constructor, 0x3f0 bytes, vtable `0x00536dd0`: no points, mode 0, fov target 60, settings off (-1 / -FLT_MAX), offset `+0x320` zero, `+0x3d0` 1.5, `+0x3d4` 0.5, `+0x3d8` -1. | confirmed (code) |
| `0x0013b290` | `CamRail_Destroy` | Cam_Rail destructor. | confirmed (code) |
| `0x0013b2b8` | `CamRail_Reset` | CamSetupRail's reset: no points, mode 0, settings and ease times off, fov target the current fov, switches 7 and 8 on. [rail](#rail). | confirmed (code) |
| `0x0013b358` | `CamRail_CopySettings` | Copies another rail camera's points, offset, settings, lead, mode and fov (Cameras_AddPlayer). | confirmed (code) |
| `0x0013b508` | `CamRail_Activate` | Rail camera activation (vtable `+0x144`): gathers targets, sets the target point, finishes an update, sets the fov and every current setting to its target at once, then resets (vtable `+0x13c`). | confirmed (code) |
| `0x0013b5d8` | `CamRail_Deactivate` | Rail camera vtable `+0x154`: re-lays out co-op views when needed (`0x0013ccd8`) and clears the rail flash (`0x0013b6f8`). | confirmed (code) |
| `0x0013b698` | `CamRail_Reset` | Rail camera reset (vtable `+0x13c`): arms the split timer, clears the blend `+0x3cc` and, with two or more points, runs one 1/60 s update with `+0x3e8` set (no damping). | confirmed (code) |
| `0x0013b6f8` | `CamRail_ClearFlash` | For both players' rail cameras clears the flash alpha `+0x3eb` and timer `+0x3e0`, and the colour controllers' alpha byte. | confirmed (code) |
| `0x0013b770` | `CamRail_ClearPoints` | Rail point count `+0x350` and current segment `+0x352` to 0. | confirmed (code) |
| `0x0013b780` | `CamRail_AppendPoint` | Appends a rail point (16 bytes at `+0x1e0`, at most 16). [rail](#rail). | confirmed (code) |
| `0x0013b7b8` | `CamRail_GatherTargets` | The rail camera's targets from the target list, filtered by switches 3, 8, 12 and Camera_HumanCounts. [rail](#rail). | confirmed (code) |
| `0x0013ba68` | `CamRail_StartShake` | Rail camera shake (vtable `+0x15c`): only for a human on the target list. | confirmed (code) |
| `0x0013bad0` | `CamRail_UpdateTargetPoint` | The rail camera's target point P and look-at point by mode and settings 3, 4, 9. [rail](#rail). | confirmed (code) |
| `0x0013bfe0` | `CamRail_DampTargetPoint` | Damps P and the look-at point toward last update's (the dz / h thresholds). [rail](#rail). | confirmed (code) |
| `0x0013c590` | `CamRail_ArmSplitTimer` | Co-op split/merge timer `+0x3dc` = 0.5 s. | confirmed (code) |
| `0x0013c5a8` | `CamRail_Flash` | On a player's current rail camera: small shake and the alpha-225 flash on his colour controller (`+0x3eb`, timer `+0x3e0` 0.2 s), as Cam_Follow_CountdownFlash. | confirmed (code) |
| `0x0013c710` | `CamRail_DecideSplit` | Co-op (2 players) with rail cameras: whether the players share one view or split, as Cam_Follow_DecideSplit (players in frame and close, timer `+0x3dc`). | confirmed (code) |
| `0x0013ccd8` | `CamRail_OnViewCountChange` | Co-op: after the view count changed, re-frames and flashes each rail camera (`0x0013c5a8`). | confirmed (code) |
| `0x0013ce78` | `CamRail_Prime` | Run when a rail camera is made current (Camera_MakeActive, the view decision): views decided, split timer armed, targets gathered, split step, one 1/60 s update when it is a player's current camera. | confirmed (code) |
| `0x0013cf20` | `CamRail_UpdateSplitScreen` | Split-screen extras (two player cameras only): DecideSplit and OnViewCountChange; on a merge to one view one more update with `+0x3e9` set. [rail](#rail). | confirmed (code) |
| `0x0013d010` | `CamRail_Update` | Rail camera update: ease settings and lead, targets, target point, damping, placement by mode, split extras. [rail](#rail). | confirmed (code) |
| `0x0013d6b0` | `CamRail_UpdatePosition` | Mode 0 placement: segment choice, projection with setting 0, blend, height ceiling, look-at with setting 8, ends hold, collision. [rail](#rail). | confirmed (code) |
| `0x0013dcc8` | `CamRail_PlaceMode3` | Mode 3 placement (setting 5 on): from the rail point nearest the target, raised by 2 × tan(setting 5), moves along the rail toward the nearest target at most 40% of the way and the frame's speed, holds at the ends (`+0x3e4`), reframes (`0x00140d68`), shakes and rebuilds the frustum. | confirmed (code) |
| `0x0013e708` | `CamRail_PlaceLeading` | Modes 1 and 2 (CamLeadRail): the camera stands the lead ahead of or behind the target's place on the rail. [rail](#rail). | confirmed (code) |
| `0x0013f930` | `CamRail_EaseValue` | Moves one rail setting toward its target by (target - current) × dt / time left. [rail](#rail). | confirmed (code) |
| `0x0013f9b0` | `CamRail_PitchToPoint` | Signed angle of a direction from the horizontal (negative when the point is below `+0x2f8`'s height), used by the rail update's angle settings. | confirmed (code) |
| `0x0013fac8` | `CamRail_SetMode3Angle` | Setting 5: below -360 turns mode 3 off (back to mode 0), else mode 3 with that angle in°rees (`+0x388`). | confirmed (code) |
| `0x0013fb30` | `CamRail_SetFixedPitch` | Setting 8: below -360 off, else the look-at pitch in°rees (0 becomes -0.05) at `+0x394`. | confirmed (code) |
| `0x0013fba0` | `CamRail_ChooseSegment` | Chooses the rail segment for P (nearest point, then the segment P projects inside). [rail](#rail). | confirmed (code) |
| `0x00140308` | `CamRail_ProjectOnSegment` | Projects P on the segment, clamped, and pulls it toward P by setting 0. [rail](#rail). | confirmed (code) |
| `0x00140708` | `CamRail_CollideTwoTargets` | Collision with two targets and switch 8: when the targets are not all in frame (`0x00140ad0`, margin 0.3) keeps the previous position, orientation and look-at, then reframes (`0x00140d68`). | confirmed (code) |
| `0x00140830` | `CamRail_Collide` | Sweeps a 0.3 m sphere from the target's head to 2 m behind the camera and pulls the camera in front of a hit. [rail](#rail). | confirmed (code) |
| `0x00140ad0` | `CamRail_TargetsInFrame` | True when every target, by its radius (humans: scale × 0.5) plus a margin, is inside the view's side planes (vtable `+0x174`); with switch 2 (`+0x3e6`) tested from the camera's view. | confirmed (code) |
| `0x00140d68` | `CamRail_KeepTargetsInFrame` | Pulls the rail camera back and up until every target is inside the frame: the targets' height span (plus the offset's height, at least 1 m) and each target's radius against the side planes; with switch 2 (`+0x3e6`) a target within 2 m of the segment's points also counts (inferred). | inferred |

### `Cam_Scene` (type 4) {#fn-scene}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x001421b8` | `CamScene_Construct` | Cam_Scene (type 4) constructor, 0x1f0 bytes, vtable `0x00537090`: `+0x1e0` 0, `+0x1e4` -1. Made by the scene start, `0x00354558` and the movie player; the scene sets its view each frame. | confirmed (code) |
| `0x00142210` | `CamScene_Destroy` | Cam_Scene destructor. | confirmed (code) |
| `0x00142238` | `CamScene_Update` | Scene camera update (vtable `+0x134`): stores dt and finishes a base update; the scene player moves it. | confirmed (code) |

### `Cam_Spline`, the path camera (type 3) {#fn-spline}

[Path cameras](#path-cameras).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00142260` | `CamSpline_Construct` | Cam_Spline (type 3, the path camera) constructor, 0x350 bytes, vtable `0x00537350`: no source camera or callback, fov 45 (`+0x330`), points cleared (`0x00142308`). | confirmed (code) |
| `0x001422e0` | `CamSpline_Destroy` | Cam_Spline destructor. | confirmed (code) |
| `0x00142308` | `CamSpline_ClearPoints` | Clears the path's 8 points (positions `+0x1e0`, orientations `+0x260`, times `+0x2ec`, callbacks `+0x30c`), the count `+0x334`, the progress `+0x338` and the running flag `+0x33c`. | confirmed (code) |
| `0x00142368` | `CamSpline_Start` | [`CamSetupPoizo`](../references/bindings/camera.md#camsetuppoizo) core: source camera (`+0x2e0`), fov (0 takes the source's), near, far (0 takes the source's), aspect; the end callback (`+0x2e4`) from the script system; progress cleared; the source's view becomes point 0 with the given time; placed there. [path cameras](#path-cameras). | confirmed (code) |
| `0x00142578` | `PoizoCam_Reverse` | [`CamReversePoizo`](../references/bindings/camera.md#camreversepoizo) core: puts the points in reverse order with their times shifted, progress to the start, placed at the new first point, restarted with the new end callback. | confirmed (code) |
| `0x00142690` | `CamSpline_Activate` | Path camera activation (vtable `+0x144`): running flag `+0x33c` set, one base update of 1/60 s. | confirmed (code) |
| `0x001426c0` | `PoizoCam_Update` | Path camera update: while it is player 1's current camera, advances along the Catmull-Rom curve through the points (`0x00142db8`), slerping orientations, calling each point's callback and the end callback. [path cameras](#path-cameras). | confirmed (code) |
| `0x00142a58` | `PoizoCam_AddPoint` | Appends a path point (position, orientation, time, callback); at most 8. | confirmed (code) |
| `0x00142ac8` | `CamSpline_AddPointAngles` | [`CamAddPoizoPoint`](../references/bindings/camera.md#camaddpoizopoint) core: orientation from heading (about z), pitch (about x) and roll (about y) in°rees, then PoizoCam_AddPoint. | confirmed (code) |
| `0x00142d28` | `CamSpline_AddPointFromCamera` | [`CamAddPoizoPointCam`](../references/bindings/camera.md#camaddpoizopointcam) core: a path point from another camera's position (vtable `+0x21c`) and orientation (vtable `+0x224`). | confirmed (code) |
| `0x00142db8` | `CamSpline_EvalCurve` | Catmull-Rom point at t on segment n of the path (the end points repeated at the ends): -1.5t^3 + 2t^2 + 0.5t and the other three weights. | confirmed (code) |

### `Cam_Transition`, the blend camera (type 5) {#fn-blend}

[Blends](#blends).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00142f98` | `CamBlend_Construct` | Cam_Transition (type 5, the blend camera) constructor, 0x240 bytes, vtable `0x00537610`: no source (`+0x210`) or destination (`+0x214`), start orientation identity, fov 60 (`+0x224`, `+0x22c`, `+0x230`), near 0.3 (`+0x228`), `+0x234` 1. | confirmed (code) |
| `0x00143050` | `CamBlend_Destroy` | Cam_Transition destructor. | confirmed (code) |
| `0x00143078` | `CamBlend_Start` | Starts a blend: the duration and the cameras. [blends](#blends). | confirmed (code) |
| `0x00143090` | `CamBlend_GetAspect` | Blend camera vtable `+0x1fc`: the destination's aspect, mixed with the source's by t (`+0x220`) when there is a source. | confirmed (code) |
| `0x00143130` | `CamBlend_GetLens234` | Blend camera vtable `+0x234`: the destination's value mixed with the source's by t. | confirmed (code) |
| `0x001431d0` | `CamBlend_GetLens23c` | Blend camera vtable `+0x23c`: the destination's value mixed with the source's by t. | confirmed (code) |
| `0x00143270` | `CamBlend_GetLens244` | Blend camera vtable `+0x244`: the larger of the source's and destination's values. | confirmed (code) |
| `0x00143300` | `CamBlend_Activate` | Blend activation (vtable `+0x144`): snapshots the source's orientation, position, look-at, fov, near, far and draw distance (`+0x1e0`..`+0x230`) as the start, activates the destination (vtable `+0x144`); `+0x234` stays 1 only when the source is a scene or path camera. | confirmed (code) |
| `0x00143540` | `CamBlend_Deactivate` | Blend camera vtable `+0x154`: releases itself when it is no longer its player's current camera. | confirmed (code) |
| `0x00143590` | `CamBlend_Update` | Blend update: destination (and source) updated, then a linear blend of look-at and position and a slerp of orientation by t, far clip min'd; at t = 1 the destination becomes current. [blends](#blends). | confirmed (code) |

### `Cam_Win` (type 13) {#fn-win}

[Rumble: win camera](rumble.md#win-camera).

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00143918` | `CamWin_Construct` | Cam_Win (type 13) constructor, 0x200 bytes, vtable `0x005378d0`: no target (`+0x1e0`), distance 1 (`+0x1e4`), angle -10° (`+0x1e8`), height 1.4 (`+0x1ec`), speed 15°/s (`+0x1f0`), direction 1 (`+0x1f4`), orbiting (`+0x1f8`). [Rumble: win camera](rumble.md#win-camera). | confirmed (code) |
| `0x001439b0` | `CamWin_Destroy` | Cam_Win destructor. | confirmed (code) |
| `0x001439d8` | `CamWin_Activate` | Win camera start (vtable `+0x144`): look-at above the target, placed on its circle. [Rumble: win camera](rumble.md#win-camera). | confirmed (code) |
| `0x00143c58` | `CamWin_SetDirection` | Win camera: circling direction `+0x1f4` = 1 for a positive argument, else -1. | confirmed (code) |
| `0x00143c78` | `CamWin_Update` | Win camera update: circles the target. [Rumble: win camera](rumble.md#win-camera). | confirmed (code) |

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

### Globals {#globals}

The camera manager's state, confirmed (code) at the functions cited ([The camera manager](#fn-manager)):

| Address | Meaning |
| --- | --- |
| `0x005d9148[2]` | each player's base camera, the one returned to (`0x0011f9c8`) |
| `0x005d9150[2]` | each player's current camera (`0x0011f9b0`) |
| `0x005d9158[2]` / `0x005d9160[2]` / `0x005d9168[2]` | each player's follow / rail / hood camera |
| `0x005d9170[2]` / `0x005d9178[2]` / `0x005d9180[2]` / `0x005d9188[2]` | each player's mug / mini-game / blend / power camera |
| `0x0050b16c` / `0x0050b170` / `0x0050b174` | the one path, death and win camera |
| `0x005d9190[2]`, `0x0050b180` | the camera stack (top index, −1 empty): the camera a scene returns to |
| `0x005d9198[4]` | objects registered with `CamRegisterObject`; message `0x12` goes to each (`0x00123428`) |
| `0x005d91a8[4]` | the shared target list (`CamTarget`); `0x0050b194` its count, refreshed each update |
| `0x005d91b8` | the last target (`CamGetLastTarget`), set by the view decision `0x00121888` |
| `0x0050b198` / `0x0050b19c` | views shown / player cameras, counted by `0x00122ed0` |
| `0x0050b1a0` / `0x0050b1a4` | split mode (`CamSetSplitMode`) / the mode in use (2 with three or more views) |
| `0x0050b1a8` / `0x0050b1aa` | the view grid: columns and rows (mode 0: 2 × 1, 1: 1 × 2, 2: 2 × 2) |
| `0x0050b1e8[2]` | per player: his view may show (`0x00121e30`) |
| `0x0050b200` | field-of-view scale when the screen splits (at least 40°) |
| `0x0050b204` / `0x0050b208` / `0x0050b20c` | game aspect, overlay aspect, overlay scale ([Graphics](graphics.md)) |
| `0x0050b230` / `0x0050b234` | the reverse-camera and zoom buttons |
| `0x00715280[31]`, `0x0050b238` | world objects the follow camera tilts over (`0x0012ec50`); `WorldObject_Init` adds them |

The [switches](../references/cameras.md#switch) sit beside them (`0x0050b1b0`-`0x0050b2b4`).

### The base camera object {#the-base-camera-object}

Every camera class starts with `Cam_ICamera` (constructor `0x00120868`). Confirmed (code) at the constructor and the
cited functions; the lens fields are also on [The streamed world](world.md#player-camera).

| Offset | Meaning | Constructor |
| --- | --- | --- |
| `+0x00` | vtable | `0x00535510` |
| `+0x10` | position (`float[4]`) | 0 |
| `+0x20` | orientation quaternion (x, y, z, w); rotates the camera's local axes into the world: +y is the view direction, +z up, +x right (see below) | identity |
| `+0x30` | name (15 characters) | |
| `+0x40` | the camera's handle (a slot from `0x3c` to `0x6b`) | |
| `+0x44` / `+0x48` | field of view, degrees / the one used (+ `0x0050b178`, halved in split mode 0) | 60 |
| `+0x4c` | aspect | `0x0050b204` |
| `+0x50` / `+0x54` / `+0x58` | near plane / own far clip / draw distance | 0.3 / 60 / 60 |
| `+0x5c`-`+0x68` | view window (`0x00120a98`) | |
| `+0x70`-`+0xc0` | six frustum planes (normal, distance), inward | |
| `+0xd0`-`+0x14c` | near and far corner points | |
| `+0x150` / `+0x17c` | bounding sphere centre / radius | |
| `+0x180` | look-at point | 0 |
| `+0x1a0` | the last update's `dt` | |
| `+0x1cc` | a script reference (`0x001221a0`, inferred: the end callback) | 0 |
| `+0x1d4` / `+0x1d5` | the ground under the view has type bit `0x20` / `0x10` (`0x00121720`) | 0 |
| `+0x1d6` | the view may show | 1 |
| `+0x1d8` | the player it belongs to (−1 none) | −1 |

**Axes.** Confirmed (code). Every camera class places itself with `Mat_LookAt` (`0x00337028`, eye, target), which
calls `Mat_LookAtUp` (`0x00337168`). That writes a row-per-axis matrix:

| Row | Axis | Value |
| --- | --- | --- |
| `+0x00` | local +x, right | normalise(forward × up) |
| `+0x10` | local +y, forward | normalise(target - eye) |
| `+0x20` | local +z, up | right × forward |

The up hint is world +z (`0x00511700`), or (1, 1, 1) normalised when the forward's z is beyond ±0.98; eye and
target within 0.005 m return 0 and leave the camera as it was. The rows' w and the matrix's position are zeroed.
`Mat_ToQuat` (`0x003365c8`) turns that matrix into the quaternion at `+0x20`. It treats the rows as the rotated
axes (x = (m12 - m21) / 4w with `m[row][col]`), so the quaternion maps local +y to the view direction. This is the
same convention as the [scene cameras](scenes.md).

Class fields after `+0x1e0`, confirmed (code) at the constructors and setters:

| Class | Fields |
| --- | --- |
| `Cam_3rdPerson` (`0x230`) | `+0x1e0` target; `+0x1f0` angle quaternion; `+0x200` eased orientation; `+0x210` distance (5.1); `+0x214` height (1.8); `+0x220` look-at offset (0, 0, 1.5); field of view 50, near 0.5, far 75 |
| `Cam_Failed` (`0x200`) | `+0x1e0` the human; `+0x1e4` the camera before it; `+0x1e8` height (5); `+0x1ec` turn rate (1, × 15°/s); `+0x1f0`-`+0x1fc` values `CamUseDeathCamera` saves; `+0x1fe` rotating |
| `Cam_Fixed` (`0x210`) | `+0x1f0` offset added to the look-at point; `+0x200` target; `+0x204` snapped this update |

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
  or the top of something under the look-at), `0x0012f3e0` eases a pitch offset (`+0x3a4`) 10% per update toward
  the slope's angle, 0 on flat ground.
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

`CamSetFollowHeading(degrees)` (`0x0011c2f0`, 14 calls in `level5`, `level80` and `level87`, all after a scene:
170-200, once 0) is **relative to the target's facing, not a world heading**. It snaps player 0's look-at point and
calls `Cam_Follow_PlaceBehind(degrees, −1)` (`0x00124f38`) on every follow camera, the same placement as
`CameraReset` with 180 (above). `Cam_Follow_PlaceAtHeading` (`0x001250a8`) builds the direction `D` = the first
listed target's forward (`Quat_AxisY` of its transform-table quaternion, `0x00714b10` + index × `0x20`; world +y
when there is no target) turned by `degrees` about **−z** (`Quat_FromAxisAngle` `0x00335ea0` uses the axis
(0, 0, −1) at `0x00511710`), so a positive angle turns **clockwise** seen from above, as the characters' headings do.
The camera's orientation is turned so that its backward axis lies along `D`, then the camera is put at the look-at
point + distance × `D` and pitched to the target pitch. So 180 stands it behind the player looking where he faces,
190 behind and 10° round to his left (clockwise from his facing, seen from above), 0 in front looking at his face.
Confirmed (code).

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
test from its position, runs `0x00135ca8` and applies the [shake](#shake). Its orientation comes from three angles
in degrees, by the conventions of [Scripted camera angles](#scripted-angles). `0x00135ca8` does not
track: each human `CamLockLocked` lists (none in `level99`) that comes within 0.3 m of the view's left or right edge
is pushed back inside, an invisible wall at the frame's sides (confirmed (code)). The test point is 1.4 m above the
physics body; the camera's planes 0 and 1 (camera `+0x70`, inward normals; left and right inferred) are tried in
turn, and the first one the point is within 0.3 m of pushes it to exactly 0.3 m inside; the sideways part of the
step is clipped by a world ray, and the feet are snapped to the ground (ray from 2.4 m up, 3.4 m long). Details in
[`CamLockLocked`](../references/bindings/camera.md#camlocklocked). Confirmed (code).

### Scripted camera angles {#scripted-angles}

`CameraCreateLocked(name, {x, y, z}, fov, heading, pitch, roll, near, far)` (binding `0x00365d38`: Lua arguments 4,
5 and 6 go to `+0x200`, `+0x204`, `+0x208`) and `CamAddPoizoPoint({x, y, z}, heading, pitch, roll, time,
callback)` (binding `0x00366e18`) take the same three angles in degrees and turn them into the camera's
orientation quaternion the same way: `CamLocked_BuildOrientation` (`0x001353e0`) and `CamSpline_AddPointAngles`
(`0x00142ac8`) are the same code. Confirmed (code) at both:

1. Three axis quaternions, each `(axis × sin(a/2), cos(a/2))` with `a` = angle × π/180 (`sinf` `0x004b8c40`,
   `cosf` `0x004b8a70`): `qh` about world +z (`0x00511700`) by the heading, `qp` about +x (`0x005116e0`) by the
   pitch, `qr` about +y (`0x005116f0`) by the roll.
2. `q = qh ⊗ qp ⊗ qr`, Hamilton products (the vector-unit sequence at `0x00135568` computes
   `(w1·v2 + w2·v1 + v1 × v2, w1·w2 − v1·v2)` with `qh` on the left, then the result times `qr`). With column
   vectors that is `R = Rz(h) · Rx(p) · Ry(r)`: the pitch turns about the camera's own right axis after the
   heading, and the roll about its own forward axis; the pitch is not about world x.
3. `q` (x, y, z, w) is stored at `+0x20` (and `+0x1f0` for a locked camera, the orientation it keeps). It maps the
   camera's local axes to the world ([Axes](#the-base-camera-object): +x right, +y forward, +z up), as
   `Quat_AxisY` (`0x003363b0`: `(2(xy − wz), 1 − 2(x² + z²), 2(yz + wx))`) reads it.

So, with `h`, `p`, `r` the script's angles:

| Axis | World vector |
| --- | --- |
| forward (+y) | `(−sin h · cos p, cos h · cos p, sin p)` |
| up (+z) | `(cos h · sin r + sin h · sin p · cos r, sin h · sin r − cos h · sin p · cos r, cos p · cos r)` |
| right (+x) | `forward × up`; with `r` = 0 it is `(cos h, sin h, 0)` |

- **Heading** 0 looks along world +y and **grows anticlockwise** seen from above (90 looks along −x). This is the
  opposite way round from the characters' headings and `Vec_FromHeading` (`(sin h, cos h)`, clockwise,
  [Maths](maths.md)): a camera heading `h` looks where a character heading `−h` faces.
- **Pitch positive looks up**, negative looks down: every `level99` locked camera that looks down at the street has
  a negative pitch (−2.7° to −27.7°).
- **Roll positive** tips the camera's top toward its right (+x). No script in the levels read so far passes a roll
  other than 0.
- The locked update (`0x00135680`) then rebuilds the same orientation each frame: the eye is the position
  (`+0x1e0`, also `+0x10`), the target the eye + 3 m × `Quat_AxisY(q)`, the up hint `Quat_AxisZ(q)` (`0x00336458`),
  through `Mat_LookAtUp` (`0x00337168`) and `Mat_ToQuat`, so the roll survives (the default up hint of
  `Mat_LookAt` would drop it). Its look-at point is that target.
- The **view matrix** (world to camera) is the inverse of `[right, forward, up]` placed at the eye: camera-space
  `(x, y, z) = (right · (P − eye), forward · (P − eye), up · (P − eye))`, with +y the depth; the lens is on
  [The streamed world](world.md#player-camera).

**Runtime check** (confirmed (runtime), PCSX2 2026-10-07, a copy of the owner's slot 10 in `level99` checkpoint 3,
every locked camera object read from the handle table `0x006ebd38` slots `0x3c`-`0x6b`). The angles at
`+0x200`-`+0x208` are the script's; `+0x20` equals the formula above to 5 decimals for all eight, and each
camera made current (player 1's current-camera word `0x005d9150` pointed at it) showed its shot:

| Camera | Position | h, p, r | fov / far | Quaternion `+0x20` (x, y, z, w) | Forward |
| --- | --- | --- | --- | --- | --- |
| `VerminCar` | 60.925, 43.425, 2.146 | 151.47183, −11.52631, 0 | 65 / 107.3 | −0.02474, −0.09732, 0.96427, 0.24515 | −0.4680, −0.8609, −0.1998 |
| `PedCam` | 44.246, 39.242, 1.752 | 49.16566, −2.67624, 0 | 50 / 150 | −0.02124, −0.00971, 0.41589, 0.90911 | −0.7558, 0.6532, −0.0467 |
| `VerminWait` | 50.613, 43.172, 1.936 | 21.78949, −6.02318, 0 | 50 / 72.6 | −0.05159, −0.00993, 0.18874, 0.98062 | −0.3691, 0.9234, −0.1049 |
| `FenceCam` | 47.497, 37.941, 3.507 | 177.78334, −11.85450, 0 | 50 / 150 | −0.00200, −0.10325, 0.99447, 0.01924 | −0.0379, −0.9779, −0.2054 |
| `ClimbPoizo` | 64.084, 15.313, 9.375 | 143.57188, −27.69311, 0 | 50 / 150 | −0.07480, −0.22733, 0.92229, 0.30348 | −0.5258, −0.7124, −0.4647 |
| `JumpCam` | 57.213, −1.465, 6.173 | 88.01634, 0.31346, 0 | 50 / 150 | 0.00197, 0.00190, 0.69476, 0.71924 | −0.9994, 0.0346, 0.0055 |
| `VerminFencePoizo` | 25.282, 6.984, 2.012 | −90.95051, −3.46206, 0 | 50 / 150 | −0.02118, 0.02154, −0.71262, 0.70090 | 0.9980, −0.0166, −0.0604 |
| `BreakFencePoizo` | 23.674, 0.423, 1.087 | 178.03990, 4.13726, 0 | 50 / 150 | 0.00062, 0.03609, 0.99920, 0.01709 | −0.0341, −0.9968, 0.0721 |

All eight have near 0.1. In `level80` (checkpoint 1, the "Let's Go" tutorial) the current camera is `StartCam1`
((−194.746, 130.353, 2.699), h −163.123, p −3.893, fov 65, far 150): `+0x20` reads (0.00498, −0.03360, 0.98860,
−0.14667), the formula's quaternion negated (the same rotation; `Mat_ToQuat` picks the sign), and `level2`'s first
camera `StartLock` ((453.389, 203.602, 15.861), h −111.904, p −19.474) reads (−0.09469, 0.14013, −0.81663, 0.55182),
the formula's to 4 decimals; confirmed (runtime).
**Worked example**, `FenceCam` (`level99_lesson2`, checkpoint 3, the shot of Vermin going
over the chain-link fence): `CameraCreateLocked("FenceCam", {47.497, 37.941, 3.507}, 50, 177.78334, −11.85450, 0,
0.1, 150)`. `qh` = (0, 0, sin 88.892°, cos 88.892°) = (0, 0, 0.99981, 0.01934); `qp` = (sin −5.927°, 0, 0, cos
−5.927°) = (−0.10326, 0, 0, 0.99465); `q = qh ⊗ qp` = (−0.00200, −0.10325, 0.99447, 0.01924), as read. Forward =
(−sin 177.78° · cos 11.85°, cos 177.78° · cos 11.85°, sin −11.85°) = (−0.0379, −0.9779, −0.2054): south, 11.9°
down. Up = (−0.0079, −0.2053, 0.9787), right = (−0.9993, 0.0387, 0); look-at point (47.383, 35.007, 2.891), as
read at `+0x180`. View translation (right, forward, up rows times −eye) = (45.994, 39.622, 4.733). The frame
looks down the alley at the fence from above head height; read with the pitch's sign flipped it would look 11.9°
up, over the fence into the buildings.

### Path cameras {#path-cameras}

A path camera (`CamSetupPoizo`, points added with `CamAddPoizoPoint` / `CamAddPoizoPointCam`) holds at most 8 points
and moves along a Catmull-Rom curve through them, slerping between the points' orientations; a point's time is the
time to the **next** point. Its update (`PoizoCam_Update`, `0x001426c0`) runs only while it is player 0's current
camera, so a script makes it active itself. Confirmed (code); details per call on
[Cameras](../references/bindings/camera.md#camsetuppoizo).

### Rail cameras {#rail}

A rail camera (type 9, one per player, `Camera_GetPlayerRail` `0x0011fbb0`) slides along a polyline of up to 16
points while it frames its targets: the chases of `level2`, `level3` and 17 later levels. Confirmed (code) at the
addresses cited; the bindings' arguments are on [Cameras](../references/bindings/camera.md#camsetuprail).

**Set-up.** [`CamSetupRail`](../references/bindings/camera.md#camsetuprail) (`Camera_SetupRail` `0x0011cce8`) names
the camera, adds the human to the camera target list (`CameraTargets_Add(human, 1)`), sets the field of view, near
and far planes (far at most 150) and the look-at offset (`+0x320`, its z also the start of setting 9), then
`CamRail_Reset` (`0x0013b2b8`): no points (`+0x350` count, `+0x352` current segment), mode 0 (`+0x3ed`), the
distance, setting 3 and 4 targets 0, the height target −1 (off), the angle targets off (−FLT_MAX), all ease times 0,
the field-of-view target the current one, and switches 7 and 8 back on. [`CamAddRailPoint`](../references/bindings/camera.md#camaddrailpoint)
appends points (`+0x1e0`, 16 bytes each). The camera does nothing until a script makes it current
(`CameraMakeActive(rail, seconds, nil, player)`).

**Settings.** [`CamModifyRail(setting, value, seconds, player)`](../references/bindings/camera.md#cammodifyrail)
(`Camera_ModifyRail` `0x0011d228`) stores a target and an ease time; every update `CamRail_EaseValue` (`0x0013f930`)
moves each current value toward its target by `(target − current) × dt / time left` and takes the frame's time off,
so the value arrives linearly in `seconds` (0: at once).

| Setting | Target / time / current | Unit | Used by mode 0 as |
| --- | --- | --- | --- |
| 0 | `+0x378` / `+0x3a8` / `+0x3c8` | m, 0 = off (negative stored as 0) | the most the camera stays from its target point in plan |
| 1 | `+0x37c` / `+0x3a4` / `+0x358` | m, negative = off | the most the camera stands above the first target's feet |
| 2 | `+0x374` / `+0x3a0` / the lens | degrees, negative ignored | field of view |
| 3 | `+0x380` / `+0x3ac` / `+0x360` | m | the target point's shift along the rail |
| 4 | `+0x384` / `+0x3b0` / `+0x364` | m | the look-at point's further shift along the rail |
| 5 | `+0x388` / `+0x3b4` / `+0x368` | degrees, below −360 = off | switches the rail to mode 3 (`0x0013fac8`, below) |
| 6, 7 | `+0x38c`, `+0x390` / `+0x3b8`, `+0x3bc` / `+0x3d0`, `+0x3d4` | | not read by modes 0-2; not traced |
| 8 | `+0x394` / `+0x3c0` / `+0x36c` | degrees, below −360 = off | a fixed pitch for the look-at (below) |
| 9 | `+0x398` / `+0x3c4` / `+0x328` | m | the look-at offset's height above the targets |

Settings 0 and 1 start from the camera's present distance and height when they are switched on from off, and a
height set negative eases back to the present height before it switches off. Setting 2 eases the lens itself.

**One update** (`CamRail_Update`, `0x0013d010`, skipped when the frame time is 0), in order:

1. Ease the settings (above) and the lead (`+0x35c` toward `+0x370`, [`CamLeadRail`](../references/bindings/camera.md#camleadrail)).
2. **Targets** (`CamRail_GatherTargets`, `0x0013b7b8`): the humans of the camera target list (two entries, one per
   player), each kept by the [switches](#switches) 3, 8 and 12 and whether the player still counts; with switch 3
   on and two player views, each player's rail camera keeps only its own player. None: the update stops here.
3. **The target point** (`CamRail_UpdateTargetPoint`, `0x0013bad0`) `P` = the targets' mean position (their feet).
   In mode 0, with `d` the current segment's direction flattened to the ground plane and normalised:
   - the look-at point starts as `P`; `P.z` += the offset's z (setting 9);
   - setting 3 non-zero: `P` += `d` × setting 3, and the look-at point = `P` + `d` × setting 4;
   - setting 3 zero, one target: the look-at point += the whole offset `(x, y, z)` when setting 4 is 0, else its z and
     then `d` × setting 4;
   - setting 3 zero, two targets: the look-at point = `P`.

   In modes 1-3 `P` += the offset and the look-at point = `P` (with two targets, modes 1 and 2 first pull `P` back
   toward the camera when the targets are spread wider than the camera's distance to them). With a split screen
   (`0x0050b1a4` = 0 and the view's `+0x44` ≠ `+0x48`) settings 3 and 4 and the offset's x and y are halved.
   In modes 0-2, unless `+0x3e8` is set or the first target has a state flag of `0x1c00000000`, `0x0013bfe0` then
   **damps** `P` and the look-at point toward last frame's values. With `Δ` = new − old, `h` its length in plan and
   `dz` its rise (the rise tests are skipped when the segment has no length in plan):
   `dz` > 0.5 → old + `Δ` × 0.5, then its z keeps only a quarter of its rise; else `h` > 1 → old + `Δ` × 0.25;
   else `dz` > 0.1 → with `f` = 1 − 1.875 (`dz` − 0.1), old + `Δ` × min(2`f`, 1), then its z keeps `f` of its
   rise; else `h` > 0.4 → old + `Δ` × (1 − 1.25 (`h` − 0.4)); else no damping. While `+0x3ec` is 1 the look-at
   point moves only 40% of the way each frame, until it arrives (then `+0x3ec` = 0).
4. **Placement** by mode (`+0x3ed`): 0 `CamRail_UpdatePosition` (`0x0013d6b0`, below), 1 and 2 `CamRail_PlaceLeading`
   (`0x0013e708`, set by `CamLeadRail`: the camera stands the lead ahead of or behind the target's place on the
   rail), 3 `0x0013dcc8` (below).
5. Split-screen extras (`0x0013cf20`, only with two player cameras); a timer at `+0x3e0` fades the co-op flash,
   alpha 225 on both colour controllers' byte `+0x1b4` ([Two players](#two-players)); then the camera's distance to
   `P` is kept at `+0x3d8` and the flags
   `+0x3e5`, `+0x3e7`, `+0x3e8`, `+0x3e9` cleared.

**Mode 0 placement** (`CamRail_UpdatePosition`):

1. Unless the camera is **held** (`+0x3e4`), choose the segment (`CamRail_ChooseSegment`, `0x0013fba0`): the rail
   point nearest `P` (3D) starts the segment, the last point giving the last segment (a tie between two points takes
   the later one only when `P` projects inside its segment); then, when `P` projects inside that segment
   (0 ≤ t ≤ length) it stays, else it moves to the previous or the next segment, whichever `P` projects inside. A
   change of segment sets a blend `+0x3cc` = 0.1.
2. **Project** `P` on the segment (`CamRail_ProjectOnSegment`, `0x00140308`): `Q` = the foot of the perpendicular,
   clamped to the segment's ends. With setting 0 on, when `Q` is farther than it from `P` in plan, `Q` moves toward
   `P` (in plan) until it is exactly that far: the camera leaves the rail to stay within reach.
3. While the blend is on, `Q` = camera + (`Q` − camera) × blend and the blend grows by 5% a frame until it reaches
   1 or the step is under 1 cm: a soft hand-over between segments. `Q` += the vector at `+0x330` (not set by any rail
   binding).
4. **Height**: with setting 1 on, `Q.z` is lowered to the first target's feet + setting 1 when it is higher (a
   ceiling, it never raises the camera). The camera stands at `Q`.
5. **Look-at**: with setting 8 on, the look-at point is replaced by the point 6 m from `Q` toward the targets in plan,
   raised by 6 × tan(setting 8). The camera then faces the look-at point.
6. **Ends**: at the first point with the look-at point behind the rail's start, or at the last point with it beyond
   the end, the camera is held (`+0x3e4` = 1): it keeps the previous frame's position, orientation, `P` and look-at
   point; what clears the hold is not traced.
7. **Collision**: `0x00140830` sweeps a 0.3 m sphere from the target's head (position + offset) to 2 m behind the
   camera and pulls the camera in front of a hit (two targets and switch 8: `0x00140708` instead).

**Mode 3 placement** (`0x0013dcc8`, `CamModifyRail` setting 5 ≥ −360): the camera starts at the rail point nearest
the target point, raised by 2 × tan(setting 5); each update it moves along the rail toward the nearest target, at
most 40% of the way and at most the frame's step, is held at the rail's ends (`+0x3e4`), keeps every target in frame
(`0x00140d68`: pulled back and up until each target, by its radius, is inside the view's side planes) and shakes.
Confirmed (code); the framing test's details are inferred.

**In `level3`'s chase** ([`level3`](scripting.md#level3)) the rail runs straight along y = 349 at z = 29.97, beside the
rooftops the player runs along; the main framing (setting 3 = −8, 4 = 6, 0 = 5.5, 1 = 4, 9 = 2, field of view 78°)
puts the camera 8 m behind the player along the rail, at most 5.5 m from him in plan and 4 m above his feet, looking
at a point 2 m behind him and 2 m up. Inferred from the code above with the script's values.

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

### Death camera {#death-camera}

`CamUseDeathCamera(human, ms, ms2)` (`0x0011daa8`) makes player 1's `Cam_Failed` (type 12) current at once, keeping the
camera it replaced (`+0x1e4`), fades the screen over `ms`, hides the HUD and stops the players. Confirmed (code) at the
cited addresses:

- **Target** (`0x001236e0`): the last target (`0x005d91b8`) when it is a player, else whichever player stands nearer
  the camera it replaced.
- **Placement** (`0x001238a8`): the look-at point is the human's position; a ray straight up from it, near plane + 5 m
  long (mask `0x200`), gives the height: the camera sits that far above the body, less the near plane, looking
  straight down (a pitch of about −88°) at a random yaw (one of 36 steps of 10° from −180°).
- **Each update** (`0x00123bf0`): it turns about the vertical at 15°/s and sinks toward the body at 0.18 m/s, no nearer
  than 2.5 m, until player 1's colour controller has finished its fade; it then starts the next fade and stops moving.

### Mini-game and mugging cameras {#mini-mug}

Both are per-player cameras made on first use; both start only from a single-view follow camera, return to it with a
0.3 s blend, and turn the follow camera to the shot's direction first (`Cam_Follow_TurnToMatrix`, `0x0012ded8`) so the
hand-back does not swing. Confirmed (code) at the cited addresses; the shot tables' purpose (variety) is inferred.

- **The mini-game camera** (`Cam_Mini`, type 8, `0x001361a8`) frames two humans (kind 0, the mount from
  `Player_UpdateMounting`) or a human and the stereo (kind 2, `StereoTheft_Start`); no caller passes kind 1. The shot
  is about the pair's midpoint at 3 m, at a yaw and pitch drawn at random from five-entry tables (kind 2: a height of
  3-7° and one of eight yaws, at most 2.3 m); up to three draws are tested with a sphere sweep (`0x001374d8`), and if
  none is clear a fixed shot (pitch 36°, 32° for kind 1) is used. Each update turns it at up to 3 × its angle error per
  second to keep both in frame. The mini-game's end (`0x0022d1f8`) sets its done flag (`+0x258`), and the next update
  blends back.
- **The mugging camera** (`Cam_Mug`, type 7, `0x00137e08`, from `0x002729a8`) plays `vags/misc/mug_intro` as it cuts
  in. The shot (`0x00138078`) is 2.5 m from the pair (1.8 m when the world is in the way) at a random yaw and pitch
  from small tables. Each update (`0x00138b90`) eases the field of view to 50° over 0.33 s, turns at up to 135°/s to
  keep both humans framed and stays 3 m from other humans (`Camera_PushOutOfHumans`, `0x00122728`). When the mugger or
  the victim leaves the hold it stops the intro, plays `vags/misc/mug_outro` and blends back.

### Power-move camera {#power-camera}

`Cam_Power` (type 6) shows a power move from shots its animation lays down; animation events (`Anim_FireFrameEvents`)
drive it, the cut (event `0x39`) only while switch 9 is on. Confirmed (code) at the cited addresses:

1. **Begin** (`0x0013a170`): remembers the current camera (`+0x334`, or the one it came from when it is already a power
   camera), copies its near plane, far clip and field of view, and records the human's position and orientation.
2. **Shots** (`0x0013a448`): up to 8 shots, each a position and an orientation relative to the human.
3. **Cut** (`0x0013a548`): to a stored shot, placed relative to the human and pulled out of walls by a sphere and ray
   test (`0x0013a818`); it becomes the player's current camera.
4. **Each update** (`0x00139ef8`) holds the shot; when the human is gone, down or dead, or has left the power move, it
   ends (`0x0013a3b0`): the remembered camera becomes current at once, with no blend.

### Hood and third-person cameras {#hood-third}

- **Hood** (`Cam_Hood`, type 11, `CamSetupHood`): each update (`0x00135138`) it sits on its vehicle at the mount offset
  in the vehicle's frame (default (0, 0, 1.53)), looks at the mean position of the players that still count raised by
  the look offset's height (default 1.7), and steps its field of view toward the wanted one by at most 1° per second
  of `dt`. Confirmed (code).
- **Third person** (`Cam_3rdPerson`, type 16, `CameraCreateThird`): each update (`0x001205e8`) the look-at point is the
  target's position plus the offset (default (0, 0, 1.5)); the orientation eases 10% toward the target's; the camera
  stands `distance` (default 5.1) behind along it and `height` (default 1.8) up, swung by `angle` about the look-at
  point. Confirmed (code).

### Two players {#two-players}

With two player cameras (`0x0050b19c` = 2) the follow and rail cameras decide each update whether the players share one
view or split the screen. Confirmed (code) at `0x00126c40` and `0x0013c710`; the purpose of each test is inferred:

- **Merging** needs both players inside the shared view, within 11 m of each other and neither colour controller
  fading; **splitting** follows when that stops holding. Either waits until its timer has run 0.72 s (follow,
  `+0x41c` / `+0x420`) or 0.5 s (rail, `+0x3dc`).
- On a change (`0x001276a0`) the views are re-laid out (`0x00122ed0`: one view, or the grid of the split mode), the
  band and zoom step re-applied for the new count, the target pitch set to 25°, and one or two updates later a short
  shake and a white flash (alpha 225, fading over 0.2 s) on the colour controller (`0x00126a88`).
- When the screen splits, each view's field of view is scaled by `0x0050b200` (at least 40°), and split mode 0 halves
  the view window's width ([The camera manager](#fn-manager)).

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
  A **locked camera** looks along its angles at a point 3 m ahead, its far clip at most 150. While it is current it
  keeps the humans `CamLockLocked` lists inside its sides (`camera::keepInView()`, `repo:src/camera/locked_camera.h`):
  the head point 1.4 m up is pushed to 0.3 m inside the left side, or else the right, the move's part along the side
  clipped by the world 0.3 m short of a wall, the feet snapped to the ground, and after one push every later listed
  human is placed again, as the original's flag carries over. **Coney's readings**: the sides are the planes at half
  the horizontal field of view either side (left first); the move is from the human's feet at the last update; an
  airborne human is not landed (no fall damage);
- the **path camera** ([Path cameras](#path-cameras), `repo:src/camera/path_camera.h`): `CamSetupPoizo` starts it
  from a camera's view, `CamAddPoizoPoint` / `CamAddPoizoPointCam` append up to 8 points, and while it is current it
  flies the Catmull-Rom curve (each end point's neighbour standing in for the missing one), slerping the
  orientations, and gameplay calls each point's function as it is reached and the end function at the last, where
  it stays. **Coney's choices**: a point's angles are read as a locked camera's, and the look-at point is 3 m ahead;
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
  scripts' brains). A scene's camera is theirs too: the play mode's scene stage pushes and pops it
  (`Cameras::beginScene()`, `setSceneView()`, `endScene()`), so play blends back to the follow camera over the
  scene's `BlendCam`. The level streams round the current camera and draws through its lens, the far clip capping the
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
- **Locked cameras' angles**: the heading is read as the original's (0 facing +y, anticlockwise) and the roll
  positive turning the top to the right, but **the pitch positive looking down, the opposite of the original**
  ([Scripted camera angles](#scripted-angles): positive looks up), so every scripted shot that looks down at its
  subject looks up by as much in Coney; path points share the reading. The line of sight test, the blend's sphere
  push and keep-in-view's ray (for a range above 0) are left out.
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
- **The Rumble cameras** (`src/camera/win_camera.*`, [Rumble: win camera](rumble.md#win-camera)): `CameraCreateWin`
  sets up the one win camera on the winner, which starts again when made active (the scripts teleport the winner in
  the same frame) and orbits until replaced; its stop condition is not read. `CamDelete` forgets a locked camera,
  cutting to the follow camera when it was current (**Coney choice**); `CamSetFollowHeading` places the follow camera
  at its distance from the player's last feet along a world heading, where the original turns the player's facing
  clockwise by the angle ([Script calls](#script-calls)), so 180 must put it behind him whichever way he faces.

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

In Coney, `CameraSetClipping` sets a locked camera's near and far clips and the follow camera's own far clip (at most
150; the follow camera keeps its 0.1 near clip), `CameraGetActive` answers the current camera's handle (NilHandle for a
scene's or for player 2), `CamGetPos` its position, and `CamSetFollowPos` puts the follow camera at a point at once.

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
  fractions, and what `+0x10a`-`+0x10c` (side angle history) feed.
- **Slow motion**: which clips carry the events `0x2e` / `0x2f` ([Slow motion](#slow-motion)).
- **Shake**: the view offset's form, and the type of the anim event (`0x00101dd8`) that starts one.
- **Slow motion's game time**: whether the combat timers (stun, ground) count the shorter step or the frames.
- **The combat camera's 0.4 keep-in-view** (`0x0012e170(0.4)` in the update's one-target case): which human it keeps.
- **Rail cameras** ([Rail cameras](#rail)): settings 6 and 7; what clears the end hold `+0x3e4`; the vector at
  `+0x330`.
- **Scenes**: the "a scene is playing" flag at `0x0051489c + 0x410` (see
  [Scenes](#scenes)).
