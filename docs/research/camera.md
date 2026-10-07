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
| `0x0011daa8` | `Camera_UseDeathCamera` | [`CamUseDeathCamera`](../references/bindings/camera.md#camusedeathcamera): makes player 1's Cam_Failed (`0x001200e0`) current with no blend on a human; saves the previous camera (`+0x1e4`), sets flags `0x0050b1e8`=1/`0x0050b1ec`=0, fades the screen over ms (`0x0018c988`/`0x0018d058`), hides the HUD (`0x001b1f38`) and removes the overhead icons of every player and the human (`Human_RemoveOverheadIcon`, `0x00227388`); it stops no one. [Death camera](#death-camera). | confirmed (code) |
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
| `0x00123bf0` | `CamFailed_Update` | Death camera update: while rotating, yaws by turn rate × 15°/s and closes in on the body at 0.18 m/s down to 2.5 m; when player 1's screen-effects manager's fade out has fully covered the screen (fade level `+0x1d8` >= 1, rate `+0x1dc` > 0) ends the blur pulse at once (`0x0018d058`, 0 s, reverse) and stops rotating ([Graphics](graphics.md#fade)). | confirmed (code) |

### `Cam_Fixed` (type 0) {#fn-fixed}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00123ea0` | `CamFixed_Construct` | Cam_Fixed (type 0) constructor, 0x210 bytes, vtable `0x00535a90`: no target (`+0x200`), snap flag (`+0x204`) 0, aim point (`+0x1f0`) zero; the offset `+0x1e0` is set only by `CameraCreateFixed` (`0x0011c6b8`). | confirmed (code) |
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
| `0x00125588` | `Cam_Follow_GetZoomLevel` | Zoom level from the zoom step `+0x400`: 2 far, 1 default, 0 close (one player); 1 or 2 with two. [update step 12](#update). | confirmed (code) |
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
| `0x00126450` | `CameraObstacles_Add` | Adds a world object to the camera-obstacle list `0x00715280` (31 entries, count `0x0050b238`; a full list ignores the add); `WorldObject_Init` calls it only for kind 29 `TYPE_MOVINGVEHICLE` while player 1's follow camera exists. [tilt](#collision-helpers). | confirmed (code) |
| `0x00126488` | `CameraObstacles_Remove` | Removes a world object from the camera-obstacle list `0x00715280` (the last entry fills its slot); `WorldObject_Delete` calls it. | confirmed (code) |
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
| `0x00127d88` | `Cam_Follow_LookAt` | Look-at point = target + offset, its move limited by length (20% above 0.8 m); hidden-in-shadow height 1.65 m. [update step 5](#update). | confirmed (code) |
| `0x00128298` | `Cam_Follow_Nop2` | Empty function. | confirmed (code) |
| `0x001282a0` | `Cam_Follow_GatherTargets` | Takes the follow camera's targets from the first two entries of the target list (`+0x314`/`+0x318`, count `+0x444`), falling back to the last target `+0x31c`. [script calls](#script-calls). | confirmed (code) |
| `0x00128678` | `Cam_Follow_AimView` | One player target with `+0x454` set: heading target 15° off the player's per-player aim vector (record `+0x5750`), upper pitch limit 50°, lower limit from (0.075 - offset z) / max distance (at least -20°), and a pitch override; raises the turn rate up to 4x while the view is far off (inferred: an aiming view). | inferred |
| `0x00128b20` | `Cam_Follow_HeadLook` | Turns the player's head toward the camera heading while the right stick turns the camera (within 150° of the facing). [Head look](#head-look). | confirmed (code) |
| `0x00128cf0` | `Cam_Follow_SprintZoom` | Sprint zoom: pulls the band in to the minimum and the target pitch to 7° while sprinting, back 250 ms after. [sprint zoom](#sprint-zoom). | confirmed (code) |
| `0x00129050` | `Cam_Follow_RightStick` | Right stick yaw/pitch rates, zoom and centre buttons, look-behind (reverse) button into `+0x458`. [right stick](#right-stick). | confirmed (code) |
| `0x001298c0` | `Cam_Follow_LookBehind` | Look-behind while the reverse button is held (`+0x458` = pad): compares the target's facing with the view; past 75° pulls the band out to 8.5 m (`+0x455`, band saved in `+0x35c`), back within 45° restores it, and swings the view to face the player. | confirmed (code) |
| `0x00129c78` | `Cam_Follow_AutoFollow` | Auto-follow gate: picks the default (`0x0012a400`) or auto-centre (`0x00129f88`) rule. [heading](#heading). | confirmed (code) |
| `0x00129f88` | `Cam_Follow_AutoCentre` | Auto-centre rule: rate from the angle between the facing and the view. [heading](#heading). | confirmed (code) |
| `0x0012a400` | `Cam_Follow_AutoFollowDefault` | Default auto-follow rule (22.5-157.5°). [heading](#heading). | confirmed (code) |
| `0x0012a7d8` | `Cam_Follow_HeightProbe` | Height probe: short ray (flag 0x200) setting the height-hold state `+0x453`. [height hold](#height-hold). | confirmed (code) |
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
| `0x0012ded8` | `Cam_Follow_TurnToMatrix` | Sets a timed heading target (`+0x350`, the matrix's heading turned 180°) and pitch override (`+0x354`) over the given times, `+0x475` set; optionally merges co-op views; with a camera passed, queues it as the hand-over (`+0x47c`, blend `+0x3c0`) and sets the pitch override. Used when mug and mini-game cameras cut in and hand back. | confirmed (code) |
| `0x0012e170` | `Cam_Follow_KeepInView` | Keeps a watched target in view, yaw at most 270°/s. [heading](#heading). | confirmed (code) |
| `0x0012e9a8` | `Cam_Follow_FrameEnemy` | Combat camera: holds the enemy 27° off centre. [combat camera](#combat-camera). | confirmed (code) |
| `0x0012ec50` | `Cam_Follow_TiltOverObstacles` | Sweeps a sphere from the look-at point to the camera against the camera-obstacle boxes (`0x00715280`); tilts the view over a box it looks along (`+0x3a8`, eased 90%, decays 10%) or pulls the camera in front of it. [Helpers](#collision-helpers). | confirmed (code) |
| `0x0012f3e0` | `Cam_Follow_SlopePitch` | From the height ray's ground normal, eases a pitch offset `+0x3a4` 10% per update toward the slope's angle (sets `+0x460`); 0 on flat ground. [Helpers](#collision-helpers). | confirmed (code) |
| `0x0012f858` | `Cam_Follow_RaiseWhenBlocked` | When the collision step pulls the camera in, raises the wanted position toward a height over the look-at, chooses a clear heading (`0x0012fd20`) for one target, and sphere-pushes the aim point (1.0 m) again. [Helpers](#collision-helpers). | confirmed (code) |
| `0x0012fd20` | `Cam_Follow_ChooseClearHeading` | Blocked view, one target: casts 11 rays round the face's normal (0, ±30°, ±45°, ±60°, ±77°, ±82°), scores 9 windows of 3 and stores the best as the swing heading `+0x43c`; latches `+0x462`..`+0x464`. [Helpers](#collision-helpers). | confirmed (code) |
| `0x001303c8` | `Cam_Follow_SphereTest` | Switch 1: a physics bound sphere (radius = near plane) at the camera collided with the physics world, ignoring object kinds 0xc-0xe, 0x10 and 0x1c; keeps the camera off what it hits (`+0x410` timer 0.15 s). [Helpers](#collision-helpers). | confirmed (code) |
| `0x00130990` | `Cam_Follow_Collide` | Follow world collision: rays, side probes, swing away, sphere pushes, next update's lag. [collision](#collision). | confirmed (code) |
| `0x00134ab0` | `Cam_Follow_StaticInit` | Static init of Cam_Follow.cpp: far clip 115 (`0x005d91c0`) and cosines of 75, 45, 53.13 and 10° (`0x005d91c4`..`0x005d91d0`). | confirmed (code) |
| `0x00134b70` | `Cam_Follow_StaticInitStub` | Static-init stub of Cam_Follow.cpp. | confirmed (code) |

### `Cam_Hood` (type 11) {#fn-hood}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00134b90` | `CamHood_Construct` | Cam_Hood (type 11) constructor, 0x220 bytes, vtable `0x00536010`: no vehicle (`+0x1e0`), look offset `+0x1f0` (0, 0, 1.53), mount offset `+0x200` (0, 0.3, 1.7), fov 60 (also the wanted fov `+0x214`), near 0.1, far 115. | confirmed (code) |
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
| `0x001363e8` | `CamMini_PlaceKind0` | Mini-game shot for kind 0: 3 m from the mounting player's feet + 0.5 m on his clearer side, a yaw and a pitch picked at random from five-entry tables ([shot tables](#mini-mug-shots)) (up to 3 tries until `0x001374d8` finds it clear), else a fixed 36° pitch. | confirmed (code) |
| `0x00136a88` | `CamMini_PlaceKind1` | Mini-game shot for kind 1: as kind 0 with other yaw and pitch tables (fallback pitch 32°). No caller passes kind 1. | confirmed (code) |
| `0x00136f68` | `CamMini_PlaceKind2` | Mini-game shot for kind 2 (the stereo): 3 m from the stereo + 0.3 m, one of eight yaws and three pitches, tried with `0x001374d8` (clear for 2.3 m) ([shot tables](#mini-mug-shots)). | confirmed (code) |
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
| `0x0013dcc8` | `CamRail_PlaceMode3` | Mode 3 placement (setting 5 on, the Armies levels 60-64): pushes the targets into frame, then the look-at point chases a goal 2 m in plan from the hindmost target's rail point, forward only, by min(40% of the gap, (his forward speed + 1 m/s) × `dt`); the camera is its projection on the rail; the look-at point 2 m ahead at the pitch setting 5; holds at the ends (`+0x3e4`). [mode 3](#rail-mode3). | confirmed (code) |
| `0x0013e708` | `CamRail_PlaceLeading` | Modes 1 and 2 (CamLeadRail): the camera stands the lead ahead of or behind the target's place on the rail, or with switch 7 on off the rail beside it, with a ray back toward the rail and a 0.5 m sphere push of the look-at point. [leading](#rail-leading). | confirmed (code) |
| `0x0013f930` | `CamRail_EaseValue` | Moves one rail setting toward its target by (target - current) × dt / time left. [rail](#rail). | confirmed (code) |
| `0x0013f9b0` | `CamRail_PitchToPoint` | Signed angle of a direction from the horizontal (negative when the point is below `+0x2f8`'s height), used by the rail update's angle settings. | confirmed (code) |
| `0x0013fac8` | `CamRail_SetMode3Angle` | Setting 5: below -360 turns mode 3 off (back to mode 0), else mode 3 with that angle in°rees (`+0x388`). | confirmed (code) |
| `0x0013fb30` | `CamRail_SetFixedPitch` | Setting 8: below -360 off, else the look-at pitch in°rees (0 becomes -0.05) at `+0x394`. | confirmed (code) |
| `0x0013fba0` | `CamRail_ChooseSegment` | Chooses the rail segment for P (nearest point, then the segment P projects inside). [rail](#rail). | confirmed (code) |
| `0x00140308` | `CamRail_ProjectOnSegment` | Projects P on the segment, clamped, and pulls it toward P by setting 0. [rail](#rail). | confirmed (code) |
| `0x00140708` | `CamRail_CollideTwoTargets` | Collision with two targets and switch 8, or while held: when the targets are not all in frame (`0x00140ad0`, by their radii) and not held, puts back the previous position, orientation and look-at; then pushes the targets into frame (`0x00140d68`). [frame](#rail-frame). | confirmed (code) |
| `0x00140830` | `CamRail_Collide` | Mode 0's last step for the first target, with no world test: with switch 2 off moves the camera along its segment to keep it `max(2, 0.4 × height above P)` from `P` in plan; with switch 2 on keeps the player 2 m from the rail's vertical planes. [collision](#rail-collide). | confirmed (code) |
| `0x00140ad0` | `CamRail_TargetsInFrame` | True when every target, by its radius (player: scale × 0.5; other: capsule radius; or the given margin when the last argument is 0), is inside the left, right and bottom frustum planes (vtable `+0x174`), and with switch 2 (`+0x3e6`) beyond the plane 2 m in front of the camera. [frame](#rail-frame). | confirmed (code) |
| `0x00140d68` | `CamRail_KeepTargetsInFrame` | Moves each target (players and others) back inside the left or right and the bottom frustum planes, out of walls toward the camera and onto the ground below (`Human_SetTransform`); the camera does not move. [frame](#rail-frame). | confirmed (code) |

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
  argument (forced to 0 while the player is tagging: [The right stick](#right-stick)). With it 0 the reverse-camera button
  (`0x0050b230`) never turns the view round, and a press on a button shared with the zoom counts as a zoom tap.
  `Human_SetWheelchairControl` clears it.
- **Power camera** (switch 9, `0x0050b1d4`): an animation event of type `0x39` (`0x00101dd8`) switches a player whose
  current camera is the follow, rail, fixed or power camera to the power camera (type 6) with the event's shot id;
  with the switch off the event is ignored. `level99_lesson2.lua` turns it off while the flash dealer respawns.
- Switches 2, 7 and 8 are read only by the rail camera (type 9), which `level99` never makes; switch 7 lets the
  leading modes stand the camera off the rail, the lead from the target in plan ([leading](#rail-leading),
  confirmed (code)).
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
| `0x00715280[31]`, `0x0050b238` | the moving vehicles (kind 29) the follow camera tilts over (`0x0012ec50`); `WorldObject_Init` adds them |

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
| `Cam_Fixed` (`0x210`) | `+0x1e0` offset added to the look-at point (`CameraCreateFixed`'s offset, w = 1); `+0x1f0` the **aim point**: `CamFixed_ComputeLookAt` (`0x00124000`) copies the new look-at point there, `CamFixed_SmoothLookAt` (`0x00124248`) damps it against last update's on its own, the shake moves it, and the camera faces it (`0x00124690`); `+0x200` target; `+0x204` snapped this update (no damping) |

### The follow camera object {#the-follow-camera-object}

0x480 bytes. Fields written by the constructor and `CfgFollowCamera`, confirmed (code); "runtime" values are from
`level99` with the player standing, after the tutorial script's own calls.

| Offset | Meaning | Constructor | `CfgFollowCamera` in `level99` (runtime) |
| --- | --- | --- | --- |
| `+0x180` | look-at point (target position + offset) | | player position + (0, 0, 1.4) |
| `+0x200` | the target's offset (set by `CamSetupFollow`) | | |
| `+0x1e0` / `+0x1f0` | the camera's position and orientation at the end of the last update (copied from `+0x10` / `+0x20`) | | |
| `+0x240` | the [aim point](#aim-point) the view faces | | look-at + 0.36 m along the facing, at the body's height |
| `+0x250` | the wanted position (after the leash and the rules that turn it) | | |
| `+0x260` | the held position: the wanted position after the lag and the hard band, before collision | | |
| `+0x270` | last update's look-at point | | |
| `+0x2b0` | the position the collision step returned on the last update ("allowed") | | |
| `+0x2e0` | the eased copy of the target's rotation the aim point leads along (35% per update) | | |
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
| `+0x354` | pitch override (−FLT_MAX for none), set only by aiming (`0x00128678`) and `TurnToMatrix` ([update step 16](#update)) | | |
| `+0x370` | hold timer of a reverse button shared with the zoom button (one per pad) ([buttons](#buttons)) | | |
| `+0x47c` / `+0x3c0` | queued hand-over: the camera to make current when the timed move `+0x40c` ends, and the blend time (s); set by `TurnToMatrix` ([mini-game and mugging cameras](#mini-mug)) | 0 / 0 | |
| `+0x3d4` | target pitch saved by [ReactToLedge](#ledge) (−FLT_MAX for none) | | |
| `+0x35c` | band near edge saved while [looking behind](#reverse) | | |
| `+0x455` / `+0x458` / `+0x459` | view pulled out behind / pad holding the reverse button (−1 for none) / drop the reverse hold ([looking behind](#reverse)) | | |
| `+0x453`, `+0x220` / `+0x230`, `+0x324` | [height hold](#height-hold): on, the stored face's normal and point, the held height | | |
| `+0x464` / `+0x471` / `+0x479` | collision: wall push allowed (set with `+0x462` / `+0x463` by `0x0012fd20`) / a given-up turn found one side open / full-block turn latch (0, 1, 2, −1) ([collision](#collision)) | | |
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
| `+0x434` / `+0x438` | side factors capping the stick yaw: 1, or −1 on the side of a wall push ([right stick](#right-stick)) | | |
| `+0x444` | number of targets | | |
| `+0x418` | the [aim point](#aim-point)'s lag, worked out each update from 0.06 | 0.06 | 0.061 standing, 0.14-0.16 running |
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
stays where it is until the player drags it, and a few separate rules turn it. The steps below are **in the order the
code runs them** (`0x0012ae58`-`0x0012d3a0`); confirmed (code) at the cited addresses unless marked. Angles are given
in degrees, the code holds radians. "One target" means one human in the camera's target list (`+0x444` = 1), the
single-player case.

1. **Targets and states.** The target list is gathered (`0x001282a0`); with one target the update reads, for the
   steps below, whether the player is idle, walking, running or sprinting (the run timer `+0x36c` grows by `dt` while
   he runs or sprints), whether he is tagging, in a fight stance, or hidden in shadow, and sets the **aiming** flag
   `+0x454` while his control function is the throw-aim one (`Human_MoveThrowAim`) with his `+0x256` set and none of
   state flags `0x1c00000000`. Two factors passed to the collision step are 2.0 and 0.25 (1.9 and 0.3 × scale with a
   target: a probe height and a sphere-radius factor, inferred).
2. **Right stick and buttons** (`0x00129050`, [below](#right-stick)): yaw rate `+0x358`, pitch rate `+0x3b8`, the
   zoom, centre and reverse buttons, and the head-look angles of step 17.
3. **Bookkeeping.** `+0x46e` = the player is in a fight stance that counts for the camera; the sprint-zoom latches
   ([Sprint zoom](#sprint-zoom)); and the **saved pitch** `+0x3d4` (kept by [ReactToLedge](#ledge)) is put back into the
   target pitch `+0x3b4` once the player no longer has state flag `0x1800000000` (falling or a long fall,
   [Characters](characters.md)): after he lands.
4. **Previous look-at** (`+0x180` copied to `+0x270`), then the band eases toward a wanted near edge (`0x0012aae0`,
   the [sprint zoom](#sprint-zoom)) and the stick hold timer `+0x368` counts down by `dt`.
5. **Look-at point** (`+0x180`, `0x00127d88`, called with 0 from the update at `0x0012b740`): the target's position
   plus the offset (runtime: feet + 1.4 m). The offset itself eases toward its wanted value by 15% per update
   (`+0x200` toward `+0x210`). Unless the target is jumping or falling (`0x00227f90`), the look-at point's move this
   update is **limited by its length** `d` (in 3D, from the previous look-at point `+0x270`): above 0.8 m it moves
   20% of the way; from 0.4 to 0.8 m it moves `1 − 2 × (d − 0.4)` of the way (100% at 0.4 m, 20% at 0.8 m); below
   0.4 m all of it. So a climb's rise of 1.3-2.6 m is followed at 20% an update until 0.8 m is left, then in two more
   updates, and a jump is followed directly. **While the player is hidden in shadow** (record flag `0x200000`, tested
   by `0x00228168`, set by `Brain_SetHiddenInShadow` `0x0028ee88` → `0x0022ff88`) and not running or sprinting, with
   one target, the look-at height is instead set straight to feet + 1.75 − 0.1 = **1.65 m** (no ease;
   `0x0012b3fc`-`0x0012b7e0`). While aiming the look-at point is the player's aim origin (player record `+0x5750`).
   `level99` has no hiding spot on its path (inferred).
6. **Field of view** eases toward `+0x394` at `+0x39c` degrees per second, at most 7.5, or over the timed move's
   remaining time `+0x40c` when one runs (then `|fov − wanted| / +0x40c` per second).
7. **Reset when lost** (only with one player camera, `0x0050b19c` = 1): when the held position `+0x260` is more than
   **1.3 × the maximum distance** `+0x304` from the look-at point, or the camera is more than **30 m** above or below
   it, `0x00124d00` puts the camera behind the player as `CameraReset` does.
8. **Look-behind** (`0x001298c0`, [below](#reverse)) while the reverse button is held, and on the update it is let go.
9. **Framing rules**, only while the stick has been still for 0.334 s (`+0x368` = 0) and the view is not pulled out
   behind (`+0x455` = 0), or while a hand-over is queued (`+0x47c`): the [combat camera](#combat-camera) (player 1
   with the lock-on button, `0x0012e9a8`), else [keep in view](#combat-camera) of the fight target (0.4) or of a
   secondary target (`+0x320`, 0.25); a queued **heading from `+0x43c`** (`+0x462`: the heading target `+0x350` =
   `+0x43c` with a 0.1 s timer, and the band moved to the default distance when it was at the minimum); then
   **auto-follow** (`0x00129c78`, [Heading](#heading)) when nothing above framed the player.
10. **Leash** to the distance band `+0x32c`-`+0x330`: when the distance from the wanted position to the look-at point
    leaves the band, the wanted position is moved along that line back to the nearer edge. `level99`'s band is
    3.0-3.5 m at checkpoint 1 and 4.8-5.3 m in the street.
11. **Pitch toward its target** `+0x3b4` (`0x0012d4e8`, a rotation about the look-at point), skipped in the height
    hold; see [Pitch](#pitch).
12. **Height hold** `+0x453` ([below](#height-hold)): the probe `0x0012a7d8` enters or leaves it; while it is set the
    wanted position's height moves 30% (close zoom), 40.5% (default) or 48% (far) of the way per update toward the
    look-at height plus the height it held (`+0x378` + `+0x324`), times `+0x398` (1, or 0.25 after `0x00125888`).
    `0x00125588` reads the zoom level from the zoom step `+0x400` (with one player camera: 2 far when `+0x400` ≤ the
    minimum, 1 default when `+0x400` > the default, else 0 close). Confirmed (code) at `0x0012c0c0`-`0x0012c14c`.
13. **Aiming** ([below](#aiming)): while `+0x454` is set, `0x00128678` sets the heading target, the pitch limits and a
    pitch override every update. On the first update after aiming ends the wanted near edge `+0x34c` goes back to the
    distance saved in `+0x3cc`, the lower pitch limit to max(−20°, an angle of (1 − offset z) / maximum distance,
    `0x004b8d30`, inferred an arcsine), the target pitch into the limits, and the timer `+0x40c` to at least 0.35 s.
14. **Placed heading** `+0x3f0` (inferred: a scene's hand-back): with one target, no stick turn this update and
    `+0x3f0` set (with `+0x465`, only while the player runs), `0x001250a8` puts the camera at that heading round the
    look-at point at the far edge of the band, every update.
15. **Heading target** `+0x350` (the centre button, [ReactToLedge](#ledge), aiming, `TurnToMatrix`, `+0x43c`): when
    set, the camera yaws about the look-at point by the angle `a` between its horizontal forward and the target
    heading, or by `a × dt / +0x40c` when the timer runs and that is smaller, toward it; the target (and `+0x43c`) is
    cleared once `a` is within **0.1°** (`0x3ae4c389` is 0.1° in radians, passed to the cosine). **Without a heading
    target** the right stick's yaw rate turns the camera by `+0x358 × dt` (`0x0012d3b8`) and the rate is cleared. So
    a heading target overrides the right stick.
16. **Pitch** ([Pitch](#pitch)): the sprint zoom (`0x00128cf0`) when its latch is due; then, **with a stick pitch
    rate**, the target pitch moves by `+0x3b8 × dt` within the limits (with the slope flag `+0x460` = 1, also kept on
    the side of `+0x3a4` that its sign gives) and the camera pitches by `rate × dt`; **without one**, a **pitch
    override** `+0x354` (when above −FLT_MAX) is reached: with the timer `+0x40c` = 0 at once (when not aiming) or at
    **10°/s** (when aiming, cleared within 0.001°), with the timer by `(override − pitch) × dt / +0x40c`; the target
    pitch follows the camera's pitch meanwhile. With neither, outside the height hold, the pitch returns to its
    target at most **85°/s** (`1.4835` rad/s).
17. **Head look** (`0x00128b20`, [below](#head-look)) while the stick gave a head angle, with one target, the player
    idle and not in a fight stance.
18. **Position lag**, only when there is no heading target and `+0x46b` (set by `PlaceBehind`) is clear, and not
    while the reverse button is held unless the view is pulled out: `wanted = held + (wanted − held) × +0x388`, with
    `held` the position `+0x260` of the last update. `+0x388` is 0.22 normally (0.27 after a blocked collision,
    chosen by the [collision step](#collision) of the previous update), **0.8 while aiming**, and **0.05** while the
    player is in a task of type 11 (inferred: a grapple) with no other input. When the move is under 10⁻⁵ m but the
    stick pitches the camera, only the height moves, by max(`+0x388`, 0.35).
19. **Hard band**: the wanted position is then kept between `+0x33c` and `+0x340` from the look-at point: the leash
    band widened by max(5% of the near edge, 0.2 m) and max(6% of the far edge, 0.35 m); when they shrink they ease
    back by 1% per update. In a task of type 11 the band is the leash band × 0.85 and × 1.2; on the first update the
    player has state flag `0x1000000000` (a long fall) the far edge becomes min(2 × far, 1.1 × the maximum
    distance) and the near edge max(itself, 0.85 × near). The result is the **held position** `+0x260`.
20. **Tilt over camera obstacles** (`0x0012ec50`), then **world collision** (`0x00130990`, [below](#collision)), which
    also moves the [aim point](#aim-point) `+0x240`; the camera is placed (vtable slot `+0x1bc`) at the position it
    returns, that position and the orientation are copied to `+0x1e0` / `+0x1f0` (the camera of the last update,
    which the next update's collision step reads), the shake is applied (`0x00121298`), and the camera is turned to
    face the **aim point** `+0x240` with `Mat_LookAt` (`0x00337028`), not the look-at point. `Mat_LookAt` fails only
    when the eye and the aim point are within 0.005 m; the position is then put back and the orientation kept. There
    is no line-of-sight test here. Then the roll eases (`0x0012ac58`) and the view frustum is rebuilt.
21. **Timers**: `+0x40c` counts down by `dt`; at 0 the timed move ends (`+0x475` and `+0x43c` cleared) and a queued
    **hand-over** runs: when `+0x47c` holds a camera, `Camera_MakeActive` (`0x0011ee08`) makes it current for this
    camera's player, blending over `+0x3c0` seconds, and both are cleared (`0x0012d07c`-`0x0012d0c4`, confirmed
    (code); see [Mini-game and mugging cameras](#mini-mug)). `+0x410` and `+0x414` count down. A fade timer
    `+0x424` sets the alpha byte `+0x1b4` of the two objects at `0x005fdeb8` and `0x005fdebc` to
    `min(10 × t, 1) × 225` (inferred: the player models fading when the camera is close). `+0x46b` and `+0x469` are
    cleared.

### The aim point {#aim-point}

The view does not face the look-at point: it faces an **aim point** `+0x240` that leads the player a little along his
facing and rises and falls with his body, eased twice. The collision step moves it each update (`0x00130990`, at
`0x00133320`-`0x00134290`); confirmed (code) there and confirmed (runtime) through the `fence_climb` scenario (below).
With one target that is a player (with any other target, only a 1.0 m sphere push of the old aim point, when switch
`0x0050b1c0` is 0):

1. **Lead.** `+0x2e0`, a copy of the target's rotation, is slerped 35% of the way to it each update (snapped while the
   target does not count for the cameras, while the camera aims, `+0x454`, or while the target plays clip 1). Its
   forward `f`, the view's direction `v` (from the camera of the last update `+0x1e0` to the look-at point) and `L =
   max(0.75, 0.15 × band near edge)`: `L` is scaled by allowed ÷ wanted distance when the world holds the camera in, and
   by 0.4 while the target has state flag `0x40` or `0x80` (mask `0xc0`: he is grabbing someone from the front or the
   rear, [Combat](combat.md#state-flags)). A world ray (`WorldManager_RayCast` `0x0040db88`, at `0x00133604`)
   from the look-at point along `f`, `L − 0.5 × L × |f · v|` long, gives the lead point `P` = look-at + `f` × (the free
   length − 0.01). So running away from the camera the aim point is about 0.36 m ahead of the look-at point (0.375 −
   0.01 at the default band; runtime 0.363 standing, 0.365 running).
2. **Height.** With `d` = the human's body point height `+0x4e8` − 1.0 (the [body point](#body-point) `+0x4e0` is the
   hips' position in the body's frame, runtime 1.035 m standing, 0.93-1.0 running, 2.37 at the top of a fence vault,
   0.62 landing). **While the target runs or sprints** (below) this step is replaced. Otherwise: nothing (`P` stays at
   the look-at height) while `|d|` ≤ 0.27, unless the human has any of the state flags `0xe2c00`: `0x400` tackling (on
   top of the victim), `0x800` being tackled, `0x2000` (not named; paired attacks read it), `0x20000` arrested,
   `0x40000` knocked out, `0x80000` knocked down ([Combat](combat.md#state-flags),
   [Characters](characters.md#the-record), `Human_EndDownState` `0x0025e250`), so on the ground or in a tackle the
   height follows the body with no dead band. Otherwise `d` is scaled by `max(0, (0.27 − |d|) × 0.9 / 0.27 + 0.1)` when
   `|d|` ≤ 0.27, by `min(1, (|d| − 0.27) × 0.9 / 0.27 + 0.1)` up to 0.54, and kept beyond; a negative `d` is lowered by
   `(offset z − 1) × 0.6` more when the look-at offset is above 1.25 m (0.24 in `level99`). The height goal `zt` =
   look-at height + `d`; the new height is the old aim point's height moved 35% toward `zt`, but where that does not lie
   between the look-at height and `zt` it is `zt` itself; the result is stored as the look-at point's height `+0x188`
   (`0x001339c0`). (With `+0x465`, the target takeover of `0x00126558`, no dead band and 75%.)

   **Running and sprinting** (`0x00133674`-`0x001337d0`), confirmed (code): `+0x450` is a switch the constructor sets
   to 1 (`0x00124940`, its only writer), so it is always on. The update (`0x0012b310`-`0x0012b3f4`) passes the
   collision step two flags of this update: **sprinting** (gait `+0x1a8` 5 with no blocking record flags,
   `0x00223a98`) and **running or sprinting** (that, or gait 4, `0x00223a60`); both are 0 under the takeover `+0x465`.
   While either is set the height step above is skipped and the old aim point is moved instead:
   - **running** (gait 4): by 0.22 × (`+0x4e0` − (0, 0, 1)), the body point's offset as it is, not turned by the
     human's heading; the sway `+0x3f8` is set to 0. So the aim point bobs with the hips: 0.22 × (0.93-1.0 − 1), a
     little below its old height each update;
   - **sprinting**: a sideways **sway**. The sway `+0x3f8` moves by `+0x3f4` × 0.028 × a random number in 0.125-0.25
     (`0x00335420`), 0.0035-0.007 m an update; past ±0.028 it is held at `+0x3f4` × 0.028 and the direction `+0x3f4`
     (1.0 at construction) flips. The aim point moves sideways by the sway along the view's horizontal side axis (the
     unit cross product made at `0x00130b68`) and up by (2 × |sway| − 0.028) × 0.6, so between −0.017 and +0.017 m.
3. **Push.** Unless the camera aims, `P` is pushed out of the world by a sphere of radius 1.0 (0.95 with two targets ×
   the target's scale; `CollisionMesh_SpherePush`).
4. **Lag** `+0x418`, worked out afresh each update from 0.06: × (1 + (1 − `c`) × 0.5) where `c` = `|unit(velocity) ·
   camera forward|` once the target is faster than his walk speed (else `c` = 1); + `s` × it × ((1 − `c`) × 0.5625 +
   0.1875) with `s` = (horizontal speed − walk speed) ÷ sprint speed (≥ 0; `s` is scaled by (0.75 − `+0x38c`) × 4/3
   while the timer `+0x38c` runs down, set to 0.375 s after a blocked view clears and 0.75 s after a sphere push ends);
   × 2.5 when the target is faster than walk + 18% of (run − walk) and either the stick is not his (`+0x1b` of the
   per-player record 0) or the new aim would overshoot `P`; × 1.25 when slower and the latches `+0x45b`/`+0x45d` are set
   on a slope (`+0x460`). Then `+0x418` = last update's value + 3% of the difference; 0.6 while the human has state flag
   `0x1000000000` (a long fall); at least 0.8 when the main ray is blocked while aiming; at least 0.3 during a takeover;
   halved while the target does not count or the game mode is not 1. Runtime: 0.061 standing, 0.14-0.16 running.
5. **Move**: `+0x240` = old + (`P` − old) × `+0x418` (when the main ray is blocked by a wall whose normal is nearly
   level, by `min(1, (3.5 − the feet's distance in front of it) × 0.4) × 0.3` instead); then, if that left it farther
   from the look-at point than `P` is, it is pulled back in plan to `P`'s distance.

**Steps 4 and 5 in detail** (confirmed (code), `0x00133b40`-`0x00134180`; `old` is the aim point `+0x240` before
this update, `P` the pushed lead point):

- **The tests in player terms.** "The target does not count for the cameras" is `Camera_HumanCounts`
  (`0x00123500`) returning 0: he is down or dead (`Human_IsDownOrDead` `0x00227e60`: dying, dead, knocked out or
  tackled, state `0x180050000`; not knocked down), or he is **cuffed** (`Human_IsCuffed` `0x00223b70`, state
  `0x20000`) and cannot free himself (he still counts with upgrade (6, 15) unlocked and a key, item 6, carried,
  [Crimes](crimes.md)). "The game mode is not 1" is `GameModeStack_TopId` (`0x0015e748`) ≠ 1: another mode is on top
  of gameplay ([Boot](boot.md)). **The long fall** (state flag `0x1000000000`, read only for a player, human `+0x1b0`
  ≠ −1) is set by `Human_BuildFallClip` (`0x0025d020`) when a fall (anim state 26) starts while he is knocked down,
  knocked out or cuffed (state `0xe0000`), dying or dead, or already falling (held flag `0x400000`): he goes over an
  edge limp, as when knocked or thrown off a ledge, and plays the long-fall cycles 422 / 425.
- **The timer `+0x38c`.** Set to **0.375 s** when the main ray hit a face this update (its hit plane is not zero) but
  the hit does not hold the camera (`+0x45b` clear) while the sticky copy `+0x45c` is still set; set to **0.75 s**
  when the main ray hit a face this update and the step-3 push of `P` (the 1.0 m `CollisionMesh_SpherePush`, which
  returns whether it touched a wall) touched nothing this update but touched (or did not run, while aiming) on the
  last (`+0x45a`, this update's result kept at the end of the step). With any other target the push in question is
  the 1.0 m push of the old aim point. It **counts down by `dt` only while `s` > 0** (the target faster than his
  walk), so it waits while he stands.
- **The overshoot test** (the ×2.5): with `new` = `old` + (`P` − `old`) × the lag being built, it compares squared
  **3D** distances: (|`P` − `old`|² > 10⁻⁵ or the slope latch `+0x460`) and |`P` − `old`|² < |`P` − `new`|². That
  holds only for a lag outside 0-2, which the build never reaches (inferred), so in play the ×2.5 comes from the
  other case: the target not pad-controlled (per-player record `+0x1b` = 0). The ×1.25 needs the target at or below
  walk + 18% of (run − walk), the latch bytes `+0x45b` or `+0x45d` set (`+0x458` & `0xff00ff000000`) and the slope
  latch `+0x460`.
- **The blocked-wall share** replaces the lag in step 5 when the main ray's hit holds the camera (`+0x45b`), the
  camera does not aim, and the hit face is steep: |normal `z`| < **0.6** (`0x005d91cc`, steeper than 53.13°). Its
  "feet's distance in front" is the target's transform position (his feet) against the **hit face's plane**:
  `n · feet − d`, with `n` and `d` the plane of the main ray's hit (the ray from the look-at point toward the
  camera). The share is `min(1, (3.5 − that distance) × 0.4) × 0.3`: 0.3 within 1 m of the wall, falling to 0 at
  3.5 m. (Aiming, the lag is at least 0.8 instead.)

The camera's yaw and pitch therefore lag the look-at point: the trace's `cam_pitch` (camera to look-at) is not the
pitch of the view; the view's pitch is camera to `+0x240`.

#### The body point {#body-point}

A human's `+0x4e0` (`+0x4e8` its height) is **pose bone 2** of his skeleton (HAnim node 0, the hips: the parent of the
spine and of both legs, [Animation](formats/animation.md)), its position in the model's frame: z up, the model facing
`+y`, relative to the feet and **not turned by the human's heading**, times the human's scale (`+0x65c`, 0.97 for
Rembrandt). Confirmed (code): each update `Human_StateUpdate` (`0x0023fea8`, vtable `+0x13c`, at `0x002400d4`) calls the
human's bone transform (vtable `+0xbc`, `Human_GetBoneTransform` `0x0023bde8`) with bone 2 and the destination
`+0x4e0` (the 32-byte entry: position at `+0x4e0`, rotation at `+0x4f0`); in the same call it fetches bone 0 (the root,
for the root motion), 3 into `+0x540`, 5 into `+0x520` and 6 into `+0x500` (the head look's origin). The bone comes from
the per-update bone cache ([Combat](combat.md#grab-posing)), built from the playing pose and already composed through
the parent table, so it is the animated hips, not the clip's root track. With no model instance (`+0xd8` 0) it is not
written and keeps its old value. Its position is the pelvis channel's: the idle's pelvis height 1.067 × 0.97 = 1.035,
the runtime value standing (inferred from that match).

**When**: in the human's state update, after the state function and the head look and before the root motion and the
move. The cameras update at the end of the same 30 Hz step ([Coney](#coneys-implementation)), so the camera reads this
update's value; whether the clip time has already been advanced in this step when the cache is built is not traced.

### Climbs {#climbs}

The follow camera has no climbing mode: the climb start (`Climb_TryStart` `0x002826f0`,
[Characters](characters.md#climb)) calls no camera function (confirmed (code) for its direct callees), and the only
traversal hooks are the drop ([Ledges](#ledge)), the landing shake and the long-fall far edge (update step 19). There
are no ladders or drainpipes: the executable holds no such names, and climbing is the fence and wall climb (inferred
from the string search and the climb code). A climb is followed by the ordinary rules: the look-at point's distance
limit (step 5) and the aim point.

Does the original's camera jump when the player climbs? **No**, the climb itself moves it smoothly; one level spot
pulls it in once. Confirmed (runtime), PCSX2 2.9.94, slot 7 (`level99` checkpoint 3, the street fence), scenario
`fence_climb` (left stick 100 % straight ahead, triangle every 2 updates from 4.6 m before the fence), every update:

| Updates | Body | Camera |
| --- | --- | --- |
| run (410) | 7.80 m/s, body point 0.93-1.0 m | moves 0.260 m per update with the player, 5.65 m from the look-at point (the hard band's far edge), pitch 10.8° |
| 440, the 2 snap updates | feet 1.11 m, then 1.17 m forward in one update each | look-at point moves 0.22 m, then 0.41 m (20% of the gap, [step 5](#update)); the camera 0.22 m, then 0.41 m: **no jump** |
| 440, to the fence | the clip carries the body 0.18 m per update | camera 0.37, 0.33, 0.30, … 0.22 m per update as the look-at point catches up; its distance stays 5.65 m |
| 440's last update | the body point rises to 1.49 m | the camera moves **0.91 m in one update** (5.65 → 4.93 m from the look-at point) |
| 441, over the fence | body point 1.59 → 2.37 → 1.79 m; feet stay at ground height | aim point height 1.67 → 2.23 m (eased: 0.03-0.05 m per update); camera moves 0.07-0.11 m per update and creeps back out (4.93 → 5.15 m) |
| 442, landing | body point 2.37 → 0.62 → 0.98 m | aim point height eases back down to 1.60 m over 10 updates |

The one 0.91 m move is the street's **disabled panel** ([In the street](#street)): the camera, held 5.65 m behind, is
then 0.27 m behind the 2.65 m panel at `y` = 31.17 and comes to rest 0.25 m in front of it; from the next update
the main ray hits the panel (`+0x290` read its plane, `y` = 31.17), because the camera of the last update is now in
front of it ([World collision](#collision)), and the camera stays held 4.9-5.4 m away, creeping out at about 3% of the
gap per update with the 0.27 lag. It comes from the level's data at that spot: in the same run without the climb
(stopping against the fence) the camera crossed the panel at 0.26 m per update with no pull, then moved 0.34 m
once, pulled in to 5.47 m as the player stopped. Which test pulled it in on 440's last update (the main ray was
clear on that update, `+0x45b` 0, while the blocked-view latch `+0x45d` was set) is not traced. What does move with
a climb is the **aim point**, which rises 0.6 m with the body during the vault and comes
back down over a third of a second, and the look-at point's 20% follow of the snap. A Coney camera that jumps on
every climb is missing these two eases, or pulls in on faces the original ignores.

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
  200°/s to 60°/s, unless the call's fourth argument is set, which keeps 200°/s up to 157.5°. That argument is the
  update's local at `sp + 0x1d4`, set at `0x0012b2ac`: 1 when there is one target, he is in a fight stance
  (`Human_IsInFightStance` `0x00228340`, state flags `0x3`) and he still counts for the cameras (`0x00123500`), else 0;
  confirmed (code). So a player squaring up gets the fast swing over the whole range. Beyond 157.5° the rule turns only
  when the target's record has state flag 4 (`0x002265f0(target, 4)`, "has a target" in
  [Combat](combat.md#state-flags), inferred there; the same flag can stand in for "moving").
  `a` is the angle between the target's facing (its rotation in the transform table) and the camera's own horizontal
  forward: vtable slot `+0x224` returns the orientation `+0x20`, whose y axis (`Quat_AxisY` `0x003363b0`), flattened and
  normalised, is the view as placed at the end of the previous update, toward the [aim point](#aim-point) `+0x240`, not
  the look-at point. The facing's y axis is used flattened but not normalised. The angle is `π/2 − atan(c / √(1 − c²))`
  of the cosine `c`, with an atan (`0x003356a0`, a range-reduced rational form) good to about 10⁻⁵° (inferred: one of
  its constants sits in memory Ghidra cannot read). Confirmed (code); confirmed (runtime) in [In the street](#street).
  Each update turns by `min(a, rate × dt)` (`dt` = `+0x1a0`) toward the facing. The cosine of each threshold is computed
  with `0x004b8a70` (cosine, inferred from the thresholds' use).
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
  position is moved out to the allowed distance plus 3% of the gap and the latches stay. Here "allowed distance" is the
  distance from the last look-at point `+0x270` to `+0x2b0`, the point the step returned on the last update, and the 3%
  step is capped at the distance the step started from. While the view is blocked (`+0x45b` or the local set) the wanted
  position is instead drawn to the allowed distance plus **4%** of the gap, and the hold timer `+0x414` starts at 1.5 s
  if it is not running; on the clearing path it starts at 1.5 s (3 s while `+0x460` is set, 0.1 s with `+0x463` and a
  timed move). This is the final form of the rule (`0x00132f5c`-`0x00133250`, read in full). The **recovered distance**
  `+0x380` moves 10% of the way to the allowed distance at the end of every step (`0x00134a18`). Nothing else reads it:
  the constructor zeroes it and the activation sets it to the band's near edge, so it can be left out. Confirmed (code);
  the meaning of the locals is inferred. While the player runs, the leash keeps the wanted position at the band's far
  edge beyond what the walls allow, so the latches hold until he stops (runtime). So **one blocked update keeps
  auto-follow off until the player stops** ([Runtime checks](#runtime-checks)). Confirmed (runtime), slot 1, stick 100 %
  sideways: the rule turned 110-129°/s every update at gait 4; with `+0x45b` written to 1 before each of 21 updates it
  turned 0 on each of them, and 129°/s again on the next. The gaits explain what [In the street](#street) saw: no turn
  in the walk and run start clips (gait 0-1 while the walk start moves at 0.76 m/s, 3 in the run start's middle) or the
  landing clip (4.23 m/s, gait 3), and a turn during the run start's first five updates and the run stop's slower
  updates (gait 2).
- **Keep the target in view** (`0x0012e170(factor, range)`, called with 0.25 and `+0x3fc` for the human
  `CamSetSecondary` gives, `+0x320`, and with 0.4 in one fight case): when the target's direction from the camera is
  more than `fov × factor` from the view's, the camera yaws 35% of the excess per update, at most 270°/s
  (`4.712` rad/s), toward it. With a range above 0 it acts only while the target is within the range and a ray
  (mask `0x200`) from the look-at point reaches it; `level99` passes 0, so no range and no ray. It runs only when
  nothing else turned the camera this update, `+0x463` and `+0x454` are clear, and it replaces auto-follow, which
  needs `+0x320` to be NilHandle. Confirmed (code).
- **The right stick** (rate `+0x358`) and the **heading target** (`+0x350`, step 15 above).

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
- Without input the pitch is driven back to the target at most 85°/s (step 16).

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

- Inversion flags per pad swap the signs: `0x0050b1f0` for yaw (it also swaps the two side factors before they are
  read), `0x0050b1f8` for pitch.
- Any turn sets `+0x368` to 0.334 s, which holds off the framing and auto-follow rules (update step 9).
- **Side factors.** A left turn's rate is capped at `+0x438 × 150°/s` and happens only while `+0x438` > 0 (a right
  turn: `+0x434`); a turn sets the other side's factor back to 1. The [collision step](#collision) sets both to 1
  every update except after its wall push, which sets the side the camera is pressed against to −1, so the stick
  cannot turn the camera into that wall on the next update. Confirmed (code) in `0x00129050` and at the end of
  `0x00130990`.
- The stick also gives two **head-look angles** for [Head look](#head-look): a yaw (only tested for being non-zero)
  of 60° at full deflection to
  1.8° at the dead-zone edge (`60° − (x / 64) × 58.2°`, mirrored to the right) and, only while the target pitch is
  inside its limits, a pitch of 22.5° to 1.8° up (`22.5° − (y / 8) × 20.7°`) or −1.8° to −22.5° down.
- The stick, the centre button and the reverse button act only when the pad's options allow it (`0x0050b1b0`,
  switch 0 at `0x0050b1b8`), on a player's own camera; the stick and the centre button also need `+0x452` (set every
  update with one target) and the call's seventh argument clear. Confirmed (code) in `0x0012ae58`
  (`0x0012af5c`-`0x0012b0e0`, passed at `0x0012b4c8`):
    - **Seventh argument (right-stick strafing)**: set only when the top game mode is 1 (`GameModeStack_TopId`
      `0x0015e748`), both strafe options are on (`CfgStrafeRStick` `0x005104d0` and `CfgStrafe` `0x005104a8`) and the
      first target's control function (`+0x1bc`) is the lock-on movement `Human_FightStanceMove` (`0x00241b90`, the
      member pointer at `0x00548a00`). Both options are 0 in the image and only those two bindings write them, which
      no script calls, so in the shipped game the argument is always 0 (inferred): the stick always turns the camera.
      When set, it also skips the pitch.
    - **Eighth argument (look-behind)**: switch 11, but 0 while the first target is tagging (`0x002238c0`, state
      `0x2000000`) and either there is one target or the second is tagging too (`s5`, `0x0012b2b0`-`0x0012b2d8`).
    - **Ninth argument (pitch allowed)**: the stick's pitch acts only when the views shown equal the player cameras
      (`0x0050b198` = `0x0050b19c`) or the camera has one target.
    - The whole call is skipped when the camera has no target (`+0x444` = 0).

#### Buttons {#buttons}

The three camera buttons are pad masks (bits as in [the pad record](frontend.md#pad-record)); their defaults, read
from the data at `0x0050b22c`-`0x0050b234`, confirmed (code):

| Global | Button | Default |
| --- | --- | --- |
| `0x0050b22c` | centre | `0x200`, **L3** (the left stick pressed) |
| `0x0050b230` | reverse (look behind) | `0x8`, **R1** |
| `0x0050b234` | zoom | `0x400`, **R3** (the right stick pressed) |

`CamAssignRevCamButton` changes the reverse button; the only script call is `wchair.lua` (`StartIt`), which sets it
to 0 (none) for the wheelchair mini-game.

- **Zoom** (`0x00129050`, then `0x0012d7a8` and `0x001254f0`): a press moves the band by `+0x400 − near` and steps
  `+0x400` to the next preset (see the field table). When the reverse and zoom buttons are **the same button**, a
  hold timer (`+0x370` for the first pad) grows by `dt` while it is held: a press zooms at once (the timer has only
  just started) and resets the timer; holding it past **0.17 s** also counts as the reverse button. A press that
  follows a hold longer than 0.17 s only resets the timer, unless the look-behind switch (11) is off, when every press
  zooms. With separate buttons a zoom is any new press.
- **Centre** (`0x00129050` at `0x00129338`-`0x001293a0`): with one target, no clear heading latched (`+0x463` = 0,
  [helpers](#collision-helpers)), not in a mini-game (`0x00233c50`) and not player 1 in the [combat
  camera](#combat-camera), a new press sets the heading target `+0x350` to the player's facing (the heading of his
  rotation, `0x00335f48`) and, when no timer runs, `+0x40c` = **0.2 s**; the stick does nothing that update. So the
  camera swings behind the player over 0.2 s ([update step 15](#update)).
- **Reverse**: see [Looking behind](#reverse).

#### Looking behind {#reverse}

Only while the game mode on top is 1 (play, inferred) and switch 11 is on, holding the reverse button stores the
pad in `+0x458`; letting go clears it (`0x00129050`). `0x001298c0`, confirmed (code), with the 75° and 45° cosines
set by `Cam_Follow_StaticInit` (`0x00134ab0`):

1. **First held update**: the band's near edge is saved in `+0x35c`. When the angle between the player's facing and
   the camera's reversed forward is more than **75°** (the usual case: the camera behind the player), the band is
   moved out to **8.5 m** (`0x0012d7a8` with `8.5 − near`), `+0x455` set, and the camera yaws about the look-at
   point by that whole angle at once (`0x0012d688`): it stands in front of the player looking back at him. The
   wanted and held positions (`+0x250`, `+0x260`) are set to the result, so there is no lag. Within 75° nothing
   happens.
2. **Held**: nothing more while the angle stays within **45°**; if the player turns further, the look ends: the band
   goes back to the saved near edge, `+0x455` is cleared and `+0x459` set (which drops the held button at the next
   stick read).
3. **Let go**: the band goes back to the saved near edge, `+0x455` is cleared and the camera is turned back behind
   the player at once, again with no lag.

While the button is held the framing rules and auto-follow are off (update step 9) and the lag is skipped until the
view is pulled out (step 18).

### Ledges {#ledge}

When the player walks off an edge, the camera turns to show the drop. Confirmed (code):

- **Who calls it.** `Human_StartFall` (`0x0023dc58`) calls `ReactToLedge` (`0x0012da20`) with the heading of the
  player's velocity (his facing when he is not moving), −1 and time 0, on his follow camera, only when: he is a
  player (`+0x1b0` ≠ −1), not jumping (state `0x400000000`), the number of players equals the number of views
  (`0x0050b198`), he is on the camera's target list, the current camera is the follow camera (type 2), and a world
  ray straight down from his body point (vtable `+0x9c`) raised by his height (vtable `+0x5c`) and moved **0.75 m**
  along that heading, as long as the height + 1.5 m, hits nothing: there is a drop ahead.
- **`ReactToLedge(heading, −1, time, camera)`** does nothing unless the camera has a target, the target has state flag
  `0x800000000` (falling), and the stick has been still for 0.334 s (`+0x368` = 0). Then:
    1. The heading target `+0x350` = the heading. When the target does not hold flag 6 and moves at least
       `walk + 0.18 × (run − walk)` speed, that is all: the camera simply turns to face the way he is going.
    2. Otherwise (walking, or slower) it looks for the open side: two rays along ± the heading's sideways direction,
       **9.5 m** long, from the target's x, y at the height `+0x208`, and again from a point 1 m along the heading
       dropped by a 5 m ray to its hit − 0.3 m. If the room to the left is smaller than to the right the heading is
       turned by **−92°**, else by **+92°** (`0x3fcd87ad` = 92° in radians), and wrapped to 0-2π: the camera swings
       round to look along the edge from the side with more room.
    3. The target pitch is saved in `+0x3d4` (if not already saved) and set to the **upper pitch limit − 5°**, so the
       camera looks down over the edge; `+0x448` = 0, `+0x3e4` = −1.7 × 10³⁸.
    4. The timer `+0x40c` = time, **0.35 s** when 0 (always from `Human_StartFall`).
- **Afterwards**: the turn runs as any heading target (update step 15) and the saved pitch is put back when the
  player no longer has state flag `0x1800000000` (step 3), so after he lands.
- **The second argument** −1 is read only as the heading target's timer fallback: unused here (inferred).

**Landing** (`Human_Land` `0x0023e090`, confirmed (code)): when a player lands with a vertical speed below **−1.5
m/s**, his player camera's shake slot `+0x15c` is called with (human, level, 1): level **1** above −14.9 m/s (no
damage), **2** from −14.9 to −20.5 m/s (fall damage), **3** below −20.5 m/s (a fatal fall). For the follow camera
that is `0x001263e8`, the follow camera's [shake](#shake) (only for a human on its target list), so the levels are
the shake's levels 1-3; the last argument 1 (`+0x1d7`) lets the shake move the view, not only the rumble
([Shake](#shake)).

### Height hold {#height-hold}

The height hold drops a raised camera under an overhang (a beam, an awning's edge, a bridge's underside) and keeps
it there instead of following the leash's height. Confirmed (code) in `0x0012ae58` (`0x0012bf14`-`0x0012c078`) and
`0x0012a7d8`; the player-terms reading is inferred. Below, `feet` = `+0x378` − the offset's z (`+0x208`), where
`+0x378` is the look-at height, refreshed every update while not holding and frozen while holding; `H` is the probe
height of update step 1 (2.0 m; with a target 1.9 m × his scale, changed in some animations, not traced); the near
plane is the camera's vtable `+0x5c`.

- **Probe** (update step 12): runs while the main ray did not block last update (`+0x45b` = 0) and the held position
  `+0x260` is higher than `feet` + `H` − 0.5 m (1.4 m for a normal-sized player): a ray with mask `0x200` from the
  wanted position's x, y at the height `feet` + `H` + 0.75 m + the near plane, level toward the look-at point
  (`0x00336da8`), as long as their distance in plan (`0x00336d68`).
- **Entering** (`0x0012a7d8`, called with `feet`, `feet` + `H` and a table of two heights at `0x00548ad0`): needs a
  hit more than 10⁻⁵ along the ray, on a steep face (its normal's |z| under cos 53.13° = 0.6, `0x005d91cc`), while not
  holding. The face's plane goes to `+0x220` and the hit point to `+0x230`. The same level ray is then cast lower,
  at `feet` + **2.395 m** and then `feet` + **2.148 m**: forward, and when that is clear, backward from its far end.
  The first height clear both ways is taken. If both hit, a last try at `feet` + `H`; if that hits either way there
  is no hold. Otherwise the hold starts: `+0x453` = 1 and `+0x324` = the height − the near plane − `+0x378`, so
  step 12 eases the wanted position's height to that height less the near plane.
- **In player terms**: the camera stands higher than the player's head, something steep sits between it and the
  look-at point about 0.75 m above head height, and there is clear space 2.0-2.4 m above his feet: the camera comes
  down into that space and stays there.
- **Leaving** (`0x0012c048`): when the slope flag `+0x460` is set, the probe did not run or hit nothing, or (holding)
  the wanted position is no longer in front of the stored plane (its distance in plan under 10⁻⁵): `+0x453` = 0 and
  the plane is cleared. So the hold also needs the probe ray to keep hitting something every update.
- **While holding**: update step 12 eases the height and step 11 (the pitch toward its target) and the 85°/s pitch
  return of step 16 are skipped.

### Aiming {#aiming}

While the player aims a throw (`+0x454`, update step 1), `Cam_Follow_AimView` (`0x00128678`) runs every update,
confirmed (code):

- On the first update the wanted near edge `+0x34c` is saved in `+0x3cc` and set to **0.8 m**, so the camera comes
  in close; the upper pitch limit becomes **50°** and the lower one max(−20°, the angle of (0.075 − offset z) /
  maximum distance), and the old look-at point is copied to `+0x240` and `+0x270`.
- The look-at point is the aim origin (player record `+0x5750`) moved 0.05 m toward the aim target (`+0x5760`).
- **Heading target**: the heading from the player's body point raised by the look-at offset to the aim target,
  **−15°**, with no timer, so the camera turns there at once.
- **Pitch override**: when the view's pitch is more than `fov × 0.1875°` from the pitch toward the aim target, the
  override `+0x354` = that pitch clamped to the limits (reached at 10°/s, update step 16), and the yaw rate is raised
  by up to 3 × its value (`× (1 + 3 × min(1, (off − fov × 0.1875°) / (fov × 0.3375° − fov × 0.1875°)))`).
- The lag is 0.8 (update step 18). When aiming ends see update step 13.

### Head look {#head-look}

With the stick turning the camera and the player idle (update step 17), `0x00128b20` turns his head toward where
the camera looks, confirmed (code):

- **Yaw**: the signed angle between the camera's horizontal forward and the player's facing; **0 beyond 150°** (so he
  does not twist to look behind). **Pitch**: the stick's head pitch (above).
- `HeadLook_AtAngles` (`0x00299f98`) stores them in the human's head-look state (`+0x284`) with weight **0.75**, flags
  `0x20 | 4`, for `(dt × 1000 & 0xff) + 100` ms (133 ms at 30 Hz), so the look lapses about 4 updates after the
  stick is let go.
- **The turn** (`Human_UpdateHeadLook` `0x00248428`, each human update): the yaw is clamped to **±55°** (±90° with
  flag `0x40`, not set here) and the pitch to **±45°**; each update the head's angles step toward them by **weight ×
  12°** (9° at 0.75; `Angle_StepToward`, last argument 30°, not traced); flag `0x20` keeps the look from relaxing when
  the yaw passes 60°. `CharacterInstance_SetHeadLook` (`0x00175c58`) then clamps the pitch to −30°..22.5° for the
  bones ([graphics](graphics.md)).

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
  and not `0x800`) and either the camera of the last update (`+0x1e0`, the position copied at the end of each update) is
  not in front of its plane (by less than 0.001 × (1 + the plane normal · the direction from the last look-at point
  `+0x270` to it)) or the look-at point is less than **0.5 m** in front of it, the ray is cast again without mask bit 0,
  so the disabled triangle is ignored; a hit sets `+0x45b` (which latches auto-follow off until the player stops,
  `+0x45d`, [Heading](#heading)) and its distance becomes the limit the rest of the step works from. When that ray hits
  something that is not a ceiling (normal `z` above cos 150°), a second ray (`0x001314d0`) from the camera of the last
  update to the look-at point checks whether the obstacle is between them; if so (and the latch `+0x479` is not −1), the
  main ray is cast again from a point moved along the view by `0.8 / tan(3 × probe angle)` and that distance is added to
  its hit. Confirmed (code) for the masks, the 0.5 m and the recast; the meaning of the second ray inferred.
- **Side probes** (casts from `0x00131744`): the main ray turned about the vertical through the look-at point by **3, 2
  and 1 × the probe angle** to each side (the loop counts down from 2), each as long as the main ray, with the same mask
  and the same disabled-triangle recast. The probe angle is 7° at the near edge of the distance band down to 4° at the
  far one (`7° − 3° × t`, `t` the position in the band). The free angle found on each side is limited to 3 × the probe
  angle. A table of fractions 1.0, 0.7, 0.5, 0.3 and 0.15 (`0x00548b70`) is copied to the stack beside them
  (`0x0013106c`) but never read: no other instruction in `0x00130990` touches that copy (confirmed (code); a leftover).
  After the swing the step keeps the two free angles in `+0x42c` and `+0x430` (0 for a side with nothing in the way,
  `0x00132be4`-`0x00132c04`), for the next update's centring test below.
- **Swinging away**: when one side is clearly freer (the two differ by more than 7.5°), the camera yaws toward it by
  20% of the needed angle per update. When the view is fully blocked it turns toward the target direction by
  the angle between them, at most **dt × 480°/s** (`8.377581` rad/s); the latch `+0x479` records the first
  direction (1 for a positive turn, 2 for a negative one) and, when the needed turn reverses, becomes −1: the turn is
  given up (0), the player's position is kept in `+0x2c0`, and the camera pulls in to the hit instead. `+0x479` is
  cleared when the view is not fully blocked (inferred). Confirmed (code) before `0x001325cc`.
- **Centring in a gap** (`0x001320fc`-`0x00132198`, confirmed (code)): when both sides are blocked and the probe hits
  are uneven (the farthest hit lies beyond the nearest and short of the full ray) the 7.5° rule is not used. Instead,
  while the innermost probe on one side or the other hits short of the full ray and **both** free angles have changed
  by more than 0.1° since the last update (`+0x42c`, `+0x430`), the swing for this update is
  `+0x428 × 0.75 + (a − b) / 2 × 0.25` (`a` the `+0x430` side's free angle, `b` the `+0x42c` side's), stored back
  in `+0x428`: an eased turn toward the middle of the opening (inferred: a player walking down an alley). When only one
  side changed, or neither, it does not turn. `+0x428` is written only here and zeroed by the constructor.
- **While a hand-over is queued** (`+0x47c` set, [update step 21](#update)) the main ray, the side probes and the
  swing away are skipped (`0x001313b0` jumps to `0x00132060`), so the short turn before a mini-game or mugging shot is
  not held back by walls. Confirmed (code).
- **What moves the camera, in order** (confirmed (code) at the addresses cited; the step works on a copy of the
  wanted position `C`, from the look-at point `L` along the unit direction `u` = (`C` − `L`) / |`C` − `L`|):
    1. The swing away (above) turns `C` about `L`.
    2. **Pull-in** (`0x001327f0`-`0x00132a20`), when the main ray or the side probes are blocked: `C` = `L` + `u` ×
       the **nearest hit distance among the main ray and the six side probes** (the probes are as long as the main
       ray, so a face met at a slant by a probe can be nearer than the main ray's hit). With only side probes
       blocked and `+0x471` clear, the distance is instead the farthest probe hit, or, when that is nearer than last
       update's allowed distance, last update's distance moved toward it by 2 × `+0x1a0` (the time step, inferred) of
       the gap. `C` is then moved off the hit's plane, and off a second
       probe plane if there is one, until it is `m` in front of it, with `m` = min(near plane × (1 − `n` · (−`u`)),
       **0.01 m**): almost nothing for a face met head-on. No margin like Coney's 0.2 m.
    3. **Creep out** (`0x00132f5c`-`0x00133250`, [Heading](#heading)): when `C` is now farther from `L` than the
       allowed distance of the last update, it is put at that distance plus 4% of the gap (blocked) or 3% (clearing).
       A pull-in nearer than last update's distance stands at once.
    4. `Camera_PushOutOfHumans` (`0x00122728`) when `C` is more than min(`+0x300`, `+0x32c`) or 1 m from `L`.
    5. The aim point `P` work, including both radius-1.0 sphere pushes ([The aim point](#aim-point) step 3, and again
       in `0x0012f858` after it raises `P`): those pushes move `P` (or `+0x240` with two targets), **never `C`**.
    6. **Raise when blocked** (helpers, below) lifts `C`'s height only; its plan distance is unchanged.
    7. The **wall push** (below), only while `+0x464` is set: the one sphere push of the camera itself, radius the
       near plane + 10⁻⁵.

  So the distance a blocked view leaves is the nearest of the seven hits, less up to 1 cm; the near plane is not kept
  clear of the face unless the wall push runs. In the street run above the camera came to rest 0.25 m in front of the
  disabled panel; a slanted side probe meeting the panel nearer than the main ray fits that (inferred). (The sway
  `+0x3f8` is the sprint's, at most 0.028 m: [The aim point](#aim-point) step 2.)
  The [aim point](#aim-point) gets its own 1.0 m push. **Wall push** (only while `+0x464` is set, set by [choose a clear
  heading](#collision-helpers); one view per player; the final position's distance in front of a near-vertical face,
  `|n.z|` < cos 10°, under 1.05 × the near-plane radius, or the main ray blocked): a sphere push of the near-plane
  radius, then the camera is moved off the plane to that radius, the wanted and held positions are reset to the band's
  near edge on the line from the look-at point, `+0x45b` is cleared, the side factor on the side the camera is turning
  toward (inferred) is set to −1 and the other to 1 ([right stick](#right-stick)), and the lag becomes max(lag, 1): the
  next update takes the position at once. Confirmed (code) from `0x0013464c` to `0x00134a18`.
- **The lag it chooses for the next update** (`+0x388`), in this order, confirmed (code) from `0x00132a24`:
    1. **0.27** when the main ray was blocked (`+0x45b`), or when the full-block turn was given up (`+0x479` = −1)
       and `+0x471` is clear; **0.27 − 0.05 × s** when `+0x471` is set (set when a given-up turn finds one side
       open), `s` being the nearer side probe's free distance as a share of the probe length (0 to 1); else
       **0.22**.
    2. Back to **0.22** when the main hit is a ceiling (normal `z` below cos 150°) and a slope pitch `+0x3a4` is set.
    3. When neither `+0x45b` nor `+0x471` is set, the new value is reached only by **0.5% per update** from the old
       one (`old + (new − old) × 0.005`), so after a block the lag drifts back from 0.27 to 0.22 over a few seconds.
       Otherwise, when the camera moved and no timed heading target runs, it is scaled by
       `min(1, d × 0.2222) × 0.7 + 0.3`, `d` the free distance in front (so 30% of it at 0 m, all of it from 4.5 m).
- **Side factors** `+0x434` / `+0x438` are set to 1 at the end of every collision step except after the wall push
  above. They are not eased.
- **Recovered distance** `+0x380` eases toward the distance the probes allow by 10% per update; nothing reads it
  (inferred from the field's readers), so it can be left out.
- **No final line-of-sight test**: the position the step returns is used as it is (update step 20).

#### The step's helpers {#collision-helpers}

Confirmed (code) at each function; where the reading of a direction or a gate is not certain it says so.

- **Slope pitch** (`0x0012f3e0`, called at `0x00130c8c`). The height ray (down from the look-at point, the offset +
  0.5 m long) must hit a slope between **15° and 53.13°** (normal `z` from cos 15° down to 0.6, `0x005d91cc`). With
  `h` the horizontal direction from the look-at point to the camera, `d` the 3D one, and `n` the slope's normal:
    - **Camera uphill** (`h · n` < −cos 87°): a ray from the look-at point's x, y at feet + max(offset / 4, 0.25 m),
      along −`n`, as long as max(the camera's distance in plan, 6 m), mask `0x200`. If it hits the same plane (normals
      within 0.5°), `+0x460` = 1 and, when `d · n` < 0 (the camera below the slope's plane), the target is
      `asin(−d · n)`; `+0x3a4` moves 10% of the way toward it but never past it (`min`); otherwise `+0x3a4` = 0.
    - **Camera downhill** (`h · n` > cos 87°): the same ray from feet + 1.45 m; on the same plane, `+0x460` = 1 and,
      when `d · n` > 0, the target is `−asin(d · n) × (d · n) / 2`, eased 10% (`max`); otherwise 0.
    - **Use** (`0x00130c94`-`0x00130ef4`): while `|+0x3a4|` > 10⁻⁵ the wanted, held and last-wanted positions are
      turned about the look-at point by `+0x3a4` round the camera's horizontal side axis (a pitch), and the turned
      height is kept in `+0x3a0`; when `+0x460` was not set this update the offset first decays 10%. So on a slope
      the camera is raised off the uphill ground and lowered a little toward downhill ground (the sign inferred from
      the two cases). `+0x460` also ends the height hold, bounds the stick's target pitch (update step 16) and picks
      the 0.22 lag under a ceiling.
- **Tilt over camera obstacles** (`0x0012ec50`, before the collision step). The objects listed at `0x00715280`
  are the **moving vehicles**: `WorldObject_Init` (`0x00391adc`) adds an object only when its type's kind byte `+0x86`
  is 29 (`TYPE_MOVINGVEHICLE`) and player 1's follow camera already exists (`Cam_GetFollow(0, 0)`); no other flag is
  tested. On the disc that is six object types (`dyn_coney_car_a`, `dyn_hearse`, `dyn_ridecart`, `dyn_rogues_car`,
  `dyn_s_train_kill_p`, `dyn_turnbul_bus`, [Objects](../references/objects.md)). `WorldObject_Delete` removes it, the
  follow camera's constructor clears the list, and a 32nd vehicle is not listed.
  They are boxes: their type's size (`+0x78`, `+0x7c`, `+0x80`) halved,
  at their position and rotation. A sphere of radius max(the near plane, the player's capsule radius) is swept from
  the look-at point to the wanted position against each (`Sweep_SphereOrientedBox`); the first hit `t` < 1 is used:
    - when `t` = 0 or the view runs within 10° of the object's −y axis (`cos 170°`), the camera **tilts over** it:
      with `D` the camera's distance, `h` its height over the look-at point and `top` the box top + 1.0 m over the
      look-at point, if `h` < `top` the target is `asin(h / D) − asin(top / D)` and `+0x3a8` eases 90% toward it
      (`max`), `+0x461` set;
    - otherwise the camera is **pulled in** to `t × D − near` along the line, and if that moves it away from the last
      camera position `+0x1e0`, only 30% of the way from there.
    - While `|+0x3a8|` > 10⁻⁵ the wanted position is turned by it about the look-at point round the view's side axis;
      without a tilt this update it decays 10% per update.
- **Raise when blocked** (`0x0012f858`, when the step pulls the camera in). `s` = clamp((m − dist) / (m − 1), 0, 1),
  `m` = max(minimum distance, 0.6 × near edge), `dist` the pulled-in distance: 0 at `m`, 1 at 1 m. For a wall hit
  (normal `|z|` < 0.6), not aiming, the target a player: the camera's height moves `s` of the way up toward feet +
  2.0 + 0.085 m (feet + 0.5 m or the look-at height in states `0xe0c00` / `0x2000`, feet + 0.145 × scale with the
  last argument set; inferred: crouching), never down. Then with one target, the main ray blocked, no clear heading
  latched (`+0x463`) and no timed move (`+0x475`), **choose a clear heading** (below); then the height moves `s` of
  the way to that target − 0.05 m once more, applied to the aim point `P` (one target; `+0x240` with two), and `P`
  is sphere-pushed again with radius half of 2.0 (1.0 m; 0.5 m on one sway path), the same push as
  [The aim point](#aim-point) step 3. Its result goes to the step's push flag (`+0x45a`). The camera is not pushed
  here.
- **Choose a clear heading** (`0x0012fd20`). Only when the player is less than **1.8 m** in front of the blocking
  face, not aiming, not holding someone in front, and the allowed distance is under 9 m (or he is in a fight stance
  that counts, or has one of state flags `0x3800f0c00`). It sets `+0x462`, `+0x463` and `+0x464` (the wall push), and
  casts 11 rays (mask `0x201`) from the look-at point: along the face's normal, and turned about the vertical by ±30°,
  ±45°, ±60°, ±77° and ±82°, each `L / cos(angle)` long (`L` the allowed distance); each scores its free length, at
  most `L`. The best average of three neighbouring rays wins (ties go to the straight one), and `+0x43c` = that ray's
  heading − 180°: update step 9 then turns the camera there over 0.1 s. The three latches clear when neither the
  camera nor the look-at point is within **1.65 m** in front of the blocking face (end of `0x00130990`).
- **Sphere test** (`0x001303c8`, switch 1, off by default). A physics sphere of the near-plane radius at the camera
  is collided with the physics world; hits on object kinds 0xc-0xe, 0x10 and 0x1c are ignored (what they are is not
  traced). Any other hit puts the camera at the contact point plus the near plane along the contact normal, except
  on a box body, where the camera is moved along the view line over a timer `+0x410` (0.15 s when unset) to stay
  two near planes off the box's nearer face (inferred).

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
  the side makes the player run in a circle. **Which camera turns the stick** (confirmed (code)): `Pads_Update`
  (`0x001454a8`, step 2 of [`Humans_Update`](tasks.md#humans-update), before the locomotion; the cameras update
  last) builds the matrix from the player camera's orientation (vtable slot `+0x224`, the `+0x20` that faces the
  aim point) as left by the previous update, stores it at `0x005ddc90 + player × 0x10`, and `Pad_Update`
  (`0x00144fb0`) turns the stick `(x, y)` by its x and y axes and restores the stick's length (reversed while
  `+0x455` is set). The stored quaternion is reused instead only while the follow camera's `+0x473` is 0; the
  constructor, `Cam_Follow_Deactivate` and a stick at rest set it to 1, and only the co-op view change
  (`0x001276a0`, its third argument) can clear it. So in one-player play the stick's frame is exactly the orientation
  the auto-centre rule measures `a` from. Inferred check, with the locomotion's turn ([Characters](characters.md#locomotion),
  18° limit, ease to 2.0 rad, carry 0.8; steady step = 5 × 18° × ease(`e`)): at the run's circle the original turns
  6.37° an update, so `e` = 90° − 76.6° + 6.37° = 19.8° before the step, and the rule gives 6.47° (194°/s).
  **The circling rate** (re-measured 2026-10-05, slot 1, stick held 90°
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
  rule's share alone: the original circles at **about 190°/s** at a run. The `a` in that table is measured to the
  look-at point, which is why the rule's turn came out 3-10°/s above the formula. The rule measures `a` against the
  camera's orientation, which faces the aim point. Re-recorded 2026-10-07 (the `run_circle` scenario, with the
  orientation `+0x20`, the aim point and the turn rate `+0x360` that `0x0012d688` writes): with `a` taken from the
  previous update's orientation and this update's facing, the formula gives the written rate to 0.1°/s on every
  update except the three on the 90° boundary itself. At the run's steady circle that `a` is 76.6° (122.3°/s), while
  the view to the look-at point gives 74.8°: the aim point sits 0.17-0.40 m off the look-at point. Confirmed
  (runtime).
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
   stack (`CameraStack_Pop` then `CameraStack_Push`), so the script changes what the scene returns to. The test in
   `Camera_MakeActive` (`0x0011ee08`) is the byte `0x0051489c + 0x410` non-zero, player index 0, and player 0's
   current camera of type 4. That byte is the **cinematic scene state** of [Scenes](scenes.md) (start step 4: 0 → 1 →
   2 → 3, back to 0 at the end), so only a cinematic scene, not an in-game one, redirects the call. Confirmed (code).
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

In detail, confirmed (code):

1. Player 0's follow camera: the look-at point is snapped (`0x00127d48`) and copied to the previous look-at points
   `+0x240` and `+0x270`.
2. For each player's follow camera, `Cam_Follow_PlaceBehind(degrees, −1)` (`0x00124f38`), the same call as
   `CameraReset`'s:
    - **Distance**: the current one (`+0x3e0`, the sprint zoom's saved edge, when it is set; else the camera's distance
      from the look-at point, `+0x260` to `+0x180`) clamped to the band `[+0x32c, +0x330]`, then rounded to a preset: at
      most `min + (default − min) / 2` → the minimum (zoom step the default); at most `default + (max − default) / 2` →
      the default (zoom step the maximum); else the maximum − 0.5 (zoom step the minimum). The **band** is moved to
      that distance .. distance + min(0.5, max − min), and the lower pitch limit follows (`0x001250a8` with its last
      argument 1).
    - **Heading**: `degrees` is **relative to the target's facing**, not a world heading: the target's forward (the
      y axis of its transform-table rotation, `0x00714b10`) is turned by `degrees` about **−z** (`Quat_FromAxisAngle`
      `0x00335ea0` uses the axis at `0x00511710`, (0, 0, −1)), so a positive angle turns clockwise seen from above;
      the camera is put on that side of the look-at point: 180 is behind, 90 on the target's right, −90 on his left.
      With no target in the list the world's +y is turned instead.
    - **Pitch**: the view is turned to the **current target pitch** `+0x3b4` (the target pitch is not reset).
    - The camera is placed at the look-at point − forward × distance (the held position `+0x260`), and the wanted
      `+0x250`, last-update `+0x1e0` and allowed `+0x2b0` positions are all set to it (no lag, no collision memory);
      `+0x46b` is set for one update (it keeps the next update's lag from running).

`Cam_Follow_SetHeading` (`0x001255f8`, which stands the camera at the band's near edge and runs a 1/60 s update) is
not this binding's: only the activation step and the co-op view change call it.

### Script calls in `level95` {#level95-calls}

Every camera call of `level95.lua`, `level95_clubhouse.lua`, `level95_coney.lua` and `level95_workout.lua` (read from
the scripts' bytecode, as for `level99`). The hub plays the same follow camera at every checkpoint. Confirmed (code):

| Call (where) | Effect |
| --- | --- |
| `CameraCreateFollow("follow", player)`, `CameraMakeActive(follow, 0)`, `CameraReset(follow)` (`AddCameras`, from `RunLevel`) | as `level99`: `CfgFollowCamera(3, 6.6, 4.8, 13, 65, 0.1, {0, 0, 1.4}, 0.2)`, band **3.0-3.5 m**, zoom step 4.8, upper pitch limit 30°, behind the player |
| `CamSetFollowZoom(0)` (`SetupClubhouseEnvironment` from `StartLevel`, and `SetupConeyEnvironment`) | band stays at the minimum, 3.0-3.5 m: unlike `level99` from checkpoint 2 (4.8-5.3 m) |
| `CamSetFollowAngle(10)` (`StartTheWalk`, after `StartDoorWalk`; `FinalStartLevel`, the opening walk's callback, run by `WalkFinish` after `ExitDoorCam`) | target pitch **10°**, reached at once; any later `CameraReset` puts back the configured 13° (`ExitDoorCam` is also the door trigger's message-4 handler) |
| `CameraCreateLocked("Door1EnterCam" … "Door5EnterCam", …)` (`SetupClubhouseEnvironment`) | stored as `env.Doors[1..5] = {door, cam}`; the previous `env.Doors` cams are `CamDelete`d first; field of view 50, far 50 |
| `CameraCreateLocked("Door1LeaveCam" … "Door5LeaveCam", …)` (`SetupConeyEnvironment`) | the same table for the outside; far 90. Only `env.FinishLeaveClubhouse` calls this set-up, so no `*LeaveCam` exists until the player first leaves the clubhouse |
| `StartDoorWalk`: `CameraMakeActive(WalkTable.cam, 0)` when `WalkTable.cam` is set, else `CameraReset(MainCam)` | a cut to the door's locked camera, or the follow camera put behind the player at the start flag |
| `StartDoorWalk`, leaving only, with a second player: `CamEnable(3, false)`, `CameraReset(MainCam)` | switch 3 is the two-player view rule ([switches](#switches)): nothing with one player |
| `ExitDoorCam(flag, human)`: `CamEnable(3, true)`, `CameraMakeActive(MainCam, 0.75)`, `CameraReset(MainCam)` | back to the follow camera, behind the player, with a **0.75 s** blend; ignored when `human` is given and is not a player; with a flag it also clears the flag's message-4 handler and turns its sphere off |
| `CameraSetClipping(MainCam, 0.1, 50 / 90 / 350)` | far clip 50 in the clubhouse, 90 on Coney Island, 350 in one scene set-up |
| `CamEnable(0, true / false)` (`ActivateWarchief` / `DeactivateWarchief`) | right stick and zoom buttons on / off around the warchief menu |
| the clubhouse menus' locked cameras (`MenuCam`, `name`, `options`, `replay`, `rumble`, `exit1`, `Radio`, `Cleon`, `Ajax`, `Vermin`, the workout's three) | cuts at fields of view 50-70, far 50 (299.6 for one workout shot) |

So in the hub the follow camera stands **3.0-3.5 m** from the look-at point (the hard band 2.80-3.85 m) at a 10° target
pitch after the opening walk (13° after a later reset), with the upper pitch limit 30°.

#### The door walks {#door-walks}

A door walk is one scripted walk of the player through a clubhouse door, with an optional locked camera. Confirmed
(code), from the bytecode of `level95.lua` (functions `PrepareDoorWalk`, `StartDoorWalk`, `WalkFinish`, `SkipWalk`,
`SkipWalkFinish`, `ExitDoorCam`, and the anonymous `env.FinishEnterClubhouse` / `env.FinishLeaveClubhouse`):

1. **`PrepareDoorWalk(enter, loc, callback, cam)`** only fills the global `WalkTable`: `loc`, `enter`, `start` and
   `term` (`fWchiefStart[loc]` and `fWchiefEnd[loc]` when `enter` is true, else `fRoamStart[loc]` and
   `fRoamEnd[loc]`), `door = env.Doors[loc].door`, `callback`, and `cam` (the fourth argument, unchanged).
2. **Who calls it**, three places:

    | Caller | `enter` | `loc` | `callback` | `cam` |
    | --- | --- | --- | --- | --- |
    | `StartLevel` (the level's opening, when unlock 6/3 is not set and `LoadLight` is false; otherwise `DoQuickMap` and no walk) | 2 | `WCLoc` (5 on a first visit, while unlock 6/4 is not set) | `FinalStartLevel` | **nil** |
    | `env.FinishEnterClubhouse` (30 ms, or 5 s while `env.BuildCac`, after `env.EnterClubhouse`) | true | `env.flagIndex` | nil | `env.Doors[env.flagIndex].cam`, a `DoorNEnterCam` |
    | `env.FinishLeaveClubhouse` (30 ms after `env.LeaveClubhouse`, the message-0 handler of each `fClubexit_N` flag, the door's action prompt) | false | `env.flagIndex` | `coney.CheckTutorial` | a `DoorNLeaveCam` |

    The two `env` callers first run `SetupClubhouseEnvironment` / `SetupConeyEnvironment`, which make the cams, so
    the cam handed over is the fresh one. `StartLevel` passes **no camera**: the opening walk has no door shot.
3. **`StartDoorWalk`** (from `StartTheWalk` 100 ms after `StartLevel`, or straight after `PrepareDoorWalk` in the `env`
   callers unless `LoadLight` is set): teleports the player to `start`, cuts to `cam` (or, with no cam,
   `CameraReset(MainCam)`), registers `SkipWalk` as the skip callback, opens `door` to −90°, and gives the player
   `GoalMoveToFlag(player, term, 2, 0, 0, 0.5, 0, true)` (a walk, gait 2, to within 0.5 m, then facing the flag). It
   sets `SetMsgHandler(term, 8, "WalkFinish")`. On the opening walk `StartTheWalk` then sets `CamSetFollowAngle(10)`.
4. **What ends it**: the flag's message 8, which the move-to-flag goal sends **once** when the player is inside its
   radius (0.5 m) and turned within 15° of the flag's heading ([ai.md](ai.md#move-to-flag) End, `0x002da4a0`);
   the goal plans again each time a move ends short of the radius, which is not a new walk. Or the skip button:
   `SkipWalk` teleports to `term`, resets the follow camera and 100 ms later (`SkipWalkFinish`) calls `WalkFinish`.
5. **`WalkFinish(flag, human)`** first clears the message-8 handler and the skip callback, so it runs once; at the
   end it sets `WalkTable = nil`. Its camera step, only when `WalkTable.cam` is set:
    - **entering**: `ExitDoorCam(flag, human)` at once: the 0.75 s blend back to the follow camera, reset behind
      the player.
    - **leaving**: the `DoorNLeaveCam` stays on. `TriggerSphereCfg(term, true, r, 0, 200)` with r = 1 m (with a
      second player: 3, 2, 2.5, 1.5, 2 m for doors 1-5) and `SetMsgHandler(term, 4, "ExitDoorCam")`: the blend
      back starts when the player walks out of that sphere (message 4 is "left or died",
      [TriggerSphereCfg](../references/bindings/world.md#triggerspherecfg)).

    Then it closes the door, gives control back (`ActivateWarchief`, a 0.5 s fade effect 3) and calls `callback`
    (`FinalStartLevel` on the opening walk: `CamSetFollowAngle(10)` again).

So the opening walk of `level95` is played on the **follow camera**: no `DoorNEnterCam`, no blend, no switch change;
the camera is reset behind the player at the start flag, pitched to 10° and follows the walk. The walk happens once.
In Coney a walk that repeats three times means `StartDoorWalk` runs three times or the walk does not end on the first
message 8; the script has one `StartTheWalk` (scheduled once, and it sets itself to nil after running), so
speculative causes are Coney sending message 8 before the turn to the flag's heading, the `env` callers firing at the
level's opening as well, or the scheduled function running more than once.

#### At runtime in the clubhouse {#level95-runtime}

Reference numbers to compare Coney with. Confirmed (runtime), PCSX2 2.9.94, 2026-10-07: from a `level99` state
(slot 7, only read) with the `scripted-pad` and `right-stick` patches, `level95` was loaded by writing its level
index (29 in the level table at `W_GameState + 0x14e8`) to mode 8's `+0x20` (`0x005e5db0`) and 3 to
`W_GameState + 0x14c`, the two writes `MenuLoadLevel` makes ([Front end](frontend.md)). The level opened in the
clubhouse with the opening walk done. The camera object (`*0x005d9158`) was read over PINE about every 0.1 s, so the
times are wall-clock and approximate:

| Moment | Camera |
| --- | --- |
| standing after the opening walk, feet at (−184.92, 95.46, −194.38) | 3.50 m from the look-at point (feet + 1.4 m), pitch **10.0°**, band 3.0-3.5 m, hard band 2.80-3.85 m, zoom step 4.8, upper pitch limit 30°, lower −6.58°, lag 0.22 |
| walking, left stick 50 % straight up (raw y 47), about 1.6 m/s | the distance grows to **3.69 m** and the pitch drops to 9.5° while he walks; back to 3.50 m and 10.0° about 0.5 s after he stops |
| right stick 50 % right (raw x 208) for 1.0 s, player still | the view turns clockwise at about **93-96°/s** (the formula gives 96.5°/s), about 100° in all; during the turn the distance dips to **3.42 m** and the pitch rises to 10.24°, both back within 0.5 s of letting go |
| a run, left stick 100 % up, into a wall after 0.5 s | while he pressed into the wall the camera stood **3.85 m** away (the hard band's far edge) and stayed there after he stopped, although the wanted and held positions (`+0x250`, `+0x260`) were 3.50 m away; the cause is not traced |

#### The first visit's opening, at runtime {#level95-first-visit}

The same load (the profile of slot 7 has not done the hub tutorial, unlockable `(6, 4)` locked, so `WCLoc` is 5),
recorded from the moment the level starts. Headings are the game's (`Quat_Heading`, clockwise from +y,
[Maths](maths.md)); a recorder heading (2 atan2(q.z, q.w), anticlockwise) is 360° less this. Times are game time from
the level's start (`*(0x0050b734) + 0x48`, which the load sets back to 0) and updates at 30 a second; the player was
read every one or two updates, so each moment is good to about 2 updates. Confirmed (runtime), PCSX2 2.9.94, 2026-10-07;
what each step is comes from the scripts (`level95.lua`: `StartLevel`, `FinalStartLevel`, and the `tutorial` table's
`RunTutorial`, `ShowANewItem`, `StopWalk` and `Setup`), confirmed (code) for the bytecode:

| Game time (update) | What happens |
| --- | --- |
| 0 | the player stands at `fWchiefStart_1` (−188.6, 95, −194.38), where `AddWarchief` makes him |
| 0.10 s (3) | teleported to `fWchiefStart_5` (−185.2, 112.7, −193.79), facing the flag's 182°; brain "dead" (`+0x09` = 1) |
| 0.43-0.70 s (13-21) | turns in place towards the end flag |
| 0.73 s (22) | **walk 1** starts (gait 1, then 2 from 1.23 s), straight down −y at about 1.6 m/s |
| 4.07 s (122) | stops at (−185.48, 107.63): **5.07 m** walked, 0.43 m short of **`fWchiefEnd_5` (−185.5, 107.2, −193.8), heading 182°**, inside the goal's 0.5 m radius |
| 4.17 s (125) | turned to 177.4° (recorded 182.6°; within the goal's 15° of the flag's 182°): message 8, so **`WalkFinish`** runs between 4.10 and 4.27 s (updates 123-128), then `FinalStartLevel` |
| 4.27 s (128) | **walk 2** starts at once: `FinalStartLevel` sees the tutorial locked, puts the player back under script control (`DeactivateWarchief`) and gives him `GoalMoveToFlag(player, fWchiefEnd_1, walk, 0.5 m)` |
| about 7.6 s (229) | he enters the volume box `vbTutorialStart` (centre (−187.8, 98, −194.1), size (6, 4, 4); entered at y ≈ 102, so the size is a half-extent, inferred): `tutorial.RunTutorial` gives the **same** `GoalMoveToFlag` again (its Resume clears the running move; the new move action waits a random 0-250 ms first, the brain asking for speed 0 meanwhile, [AI](ai.md#move-action)); the positions, read every 1-2 updates, show no stop here, so the walk goes on, and starts the tutorial text and shots |
| 12.21 s (366) | walk 2 ends at (−184.92, 95.47), 0.47 m from `fWchiefEnd_1` (−184.9, 95, −194.3), inside the goal's 0.5 m radius: **12.2 m** in 7.9 s, facing 178° (the game's clockwise heading; the recorder's 182° is anticlockwise) |
| 12.24-12.44 s (367-373) | the goal's face-the-flag turn (`faceFlag` true; facing 178°, he is 89° off the flag's 89°): a turn action at the standing 12° per update, which ends within 15°, at **97.6°** (recorded as 262.4°); then the goal arrives, its End sends the flag message 8, and `tutorial.StopWalk` (the flag's message-8 handler, installed by `RunTutorial`) flushes the player's goals and removes the handler; he stands |
| **25.05 s (751)** | brain "dead" back to 0: **control returns** (`ActivateWarchief`, at the end of the tutorial's last item) |

The tutorial (`ShowANewItem`, rescheduled after each item's time) is seven items, one skipped: item 1, 4.5 s, the
text `LEVEL95.ACT[11]` on the follow camera; items 2-5, 2.5 s each, a cut (blend 0) to the locked cameras `name`,
`options`, `replay` and `rumble` with the flags `fVermin` and `fMenu` flashing; item 6 is not defined and is
skipped; item 7, 2.5 s, the `exit1` camera on `fClubexit_4`. That is **17.0 s**; then a cut back to `MainCam` (no blend
and no reset) and `ActivateWarchief` (`BrDead(player, false)`, right stick on). From the box at about 7.6 s that
gives about 24.6 s, as seen (25.05 s).

So in the original the opening has **two walks, not three**: the door walk (5.1 m, 4.1 s) and one walk to `fWchiefEnd_1`
(12.2 m), which the tutorial's second `GoalMoveToFlag` continues rather than restarts, and which ends as walk 1 does:
arrival inside the radius, the turn to the flag's heading (to within 15°), message 8. Control comes back **about 25 s**
after the level starts (21 s after `WalkFinish`). A walk 1 that ends at y 108.1 after 3.7 m is 0.9 m from
`fWchiefEnd_5`, outside the 0.5 m radius, so the original would not have sent message 8 there.

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
- **Roll positive** tips the camera's top toward its right (+x). Of the 362 `CameraCreateLocked` and
  `CamAddPoizoPoint` calls in the disc's scripts (constant arguments), **7 pass a roll**, all locked cameras and all
  small: `level87`'s tag cameras `one` −0.1°, `ten` **−4.6°**, `eleven` 0.9°, `thirteen` 0.4°; `level95`'s two
  new-item shots 0.4°; `level84_final`'s `MercyCam` 0.4°. At 0.4° the horizon tips about 2 pixels across half the 640
  width (320 × tan 0.4°), so only `ten` shows clearly. The other rolls in the game: the **mugging shot's Dutch tilt**
  of 15° or 11° ([Mini-game and mugging cameras](#mini-mug)), and the follow camera's eased roll
  (`Cam_Follow_EaseRoll` `0x0012ac58`: `q` = orientation ⊗ (0, sin(r/2), 0, cos(r/2)), a turn about the camera's
  own +y, its view axis), whose target `+0x404` only the constructor writes (0), so it stays 0.
- **What the view needs for a roll**: nothing beyond the camera's own axes. The original draws through the current
  camera's orientation quaternion (`+0x20`): the view matrix below takes **up = `Quat_AxisZ(q)`** with right and
  forward from the same `q`, so a roll tips the picture with no separate step. A port that rebuilds the view from
  the eye, the look-at point and the world's +z drops it; one that carries `Quat_AxisZ(q)` (or `q` itself) to the
  renderer keeps it. To see it, use `level87`'s camera `ten`.
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

**Set-up.** [`CamSetupRail`](../references/bindings/camera.md#camsetuprail) (`Camera_SetupRail` `0x0011cce8`) names the
camera, adds the human to the camera target list (`CameraTargets_Add(human, 1)`), sets the field of view, near and far
planes (far at most 150) and the look-at offset (`+0x320`, its z also the start of setting 9), then `CamRail_Reset`
(`0x0013b2b8`): no points (`+0x350` count, `+0x352` current segment), mode 0 (`+0x3ed`), the distance, setting 3 and 4
targets 0, the height target −1 (off), the angle targets off (−FLT_MAX), all ease times 0, the field-of-view target the
current one, and switches 7 and 8 back on. [`CamAddRailPoint`](../references/bindings/camera.md#camaddrailpoint) appends
points (`+0x1e0`, 16 bytes each). The camera does nothing until a script makes it current (`CameraMakeActive(rail,
seconds, nil, player)`).

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
| 5 | `+0x388` / `+0x3b4` / `+0x368` | degrees, below −360 = off | switches the rail to mode 3 (`0x0013fac8`) and is that mode's view pitch ([mode 3](#rail-mode3)) |
| 6 | `+0x38c` / `+0x3b8` / `+0x3d0` | | eased (`0x0013d35c`) but never read: nothing else touches `+0x3d0` after the constructor |
| 7 | `+0x390` / `+0x3bc` / `+0x3d4` | m | not read by modes 0-2; in mode 3 the margin the keep-in-frame step (`0x00140d68`, at `0x00141814` and `0x001418b8`) keeps each target inside the view's two side planes, in place of the target's radius (a player's scale × 0.5) |
| 8 | `+0x394` / `+0x3c0` / `+0x36c` | degrees, below −360 = off | a fixed pitch for the look-at (below) |
| 9 | `+0x398` / `+0x3c4` / `+0x328` | m | the look-at offset's height above the targets |

Settings 0 and 1 start from the camera's present distance and height when they are switched on from off, and a
height set negative eases back to the present height before it switches off. Setting 2 eases the lens itself.

**One update** (`CamRail_Update`, `0x0013d010`, skipped when the frame time is 0), in order:

1. Ease the settings (above) and the lead (`+0x35c` toward `+0x370`,
   [`CamLeadRail`](../references/bindings/camera.md#camleadrail)).
2. **Targets** (`CamRail_GatherTargets`, `0x0013b7b8`): only entries 0 and 1 of the shared target list
   (`0x005d91a8`, up to 4) are read, players or not, in order (`+0x340`, `+0x344`, count `+0x348`). A resolved entry
   is kept at once when switch 3 is on, or when switch 12 is on, the rail is in mode 0 and switch 8 is on (both
   default on); otherwise it must pass `Camera_HumanCounts` (`0x00123500`: not down or dead, and not cuffed unless
   unlock 6/15 and an item count say so), which has no player test, so a standing non-player counts. With switch 3
   on, unless player 0's current camera is type 5 or (one player camera, or as many views as cameras) and both view
   flags are set, each rail keeps only the entry of its own player index (none when the list is shorter). When the
   count grows, `+0x3e5` and (not in mode 3) `+0x3e7` are set and the split timer is armed. None: the update stops
   here. So a one-player list [player 1, Diego] with switch 3 off gives two targets, the player first.
3. **The target point** (`CamRail_UpdateTargetPoint`, `0x0013bad0`) `P` = the plain mean of the targets'
   transform-table positions (their feet), each weighted equally.
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
   rail, [below](#rail-leading)), 3 `0x0013dcc8` ([below](#rail-mode3)).
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
   1 or the step is under 1 cm: a soft hand-over between segments. `Q` += the vector at `+0x330`, which is always zero:
   only the constructor (zero, `0x0013b25c`) and `CamRail_CopySettings` (`0x0013b3d0`) write it (mode 3 also
   subtracts it, `0x0013e4a8`).
4. **Height**: with setting 1 on, `Q.z` is lowered to the first target's feet + setting 1 when it is higher (a
   ceiling, it never raises the camera). The first target is entry 0 of the gathered list (`+0x340`, read at
   `0x0013d848`): the first kept entry of the shared list, so player 1 on a [player, Diego] rail. The camera stands at `Q`.
5. **Look-at**: with setting 8 on, the look-at point is replaced by the point 6 m from `Q` toward the targets in plan,
   raised by 6 × tan(setting 8). The camera then faces the look-at point.
6. **Ends**: when `Q` is the rail's first point and the look-at point lies behind the rail's start, or `Q` is the
   last point and the look-at point lies beyond the end, the hold `+0x3e4` is set (`0x0013dafc`) and the camera is
   put back as it was at the start of the update (position, orientation, `P`, look-at point), then faces the
   look-at point again. This hold lasts one update: step 7 clears it. So the camera stands still while the look-at
   point is past an end and follows again on the first update it is not.
7. **Collision** (`0x0013dbbc`-`0x0013dc1c`): when the camera is held (by step 6 or by `CamLockRail`), or with
   switch 3 off, switch 8 on and two or more targets, `CamRail_CollideTwoTargets` (`0x00140708`) runs and the hold
   is then set back to its value at the start of placement. Otherwise `CamRail_Collide` (`0x00140830`) runs for the
   first target ([below](#rail-collide)); it does not touch the hold.

**The hold** (`+0x3e4`, confirmed (code) at the addresses cited):

- **Mode 0 held** at the start of placement (the value reaches `CamRail_UpdatePosition` as its fifth argument, `t0` =
  `+0x3e4` ≠ 0 at `0x0013d498`): steps 1-6 are skipped. The camera's position, orientation, `P` and look-at point are
  put back to their values at the start of the update (last update's), the camera faces the look-at point and the
  frustum is rebuilt. Step 7 then runs `CamRail_CollideTwoTargets`: when a target is out of frame (`0x00140ad0`) it
  pushes the targets back into the frame (`0x00140d68`, [below](#rail-frame)); a held camera is not put back, it stays.
  The hold is written back as 1, so a `CamLockRail(true)` lock lasts until `CamLockRail(false)`.
- **Every writer**: the constructor (0, `0x0013b1fc`); `Camera_LockRail` (`0x0011d180`, the script's value);
  `CamRail_CopySettings` (`0x0013b498`, copies player 1's, so a joining player inherits his lock); the mode 0 ends
  test (1) and step 7 (the start value); mode 3 (1, below). `CamRail_Reset` (`0x0013b2b8`), `CamRail_Activate`
  (`0x0013b508`), `CamSetupRail` and switching modes never write it: a lock survives `CamSetupRail` and
  `CameraMakeActive`.
- **Modes 1 and 2** (`CamRail_PlaceLeading`, `0x0013e708`) never read it: a lock does not stop them.
- **Mode 3** (`0x0013dcc8`) sets it when the camera reaches the rail's first or last point (squared distance under
  10⁻¹⁰, `0x0013e260`, `0x0013e2ec`) and, while it is set, no longer moves along the rail toward the target: the
  camera keeps its position and its look-at point, still reframing (`0x00140d68`) and shaking. Mode 3 never clears it,
  so a mode-3 camera that reaches an end stays there until `CamLockRail(false)` (inferred from the list of writers).

<span id="rail-collide"></span>**Collision** (`CamRail_Collide`, `0x00140830`, confirmed (code))
does no world or physics test.
Its one caller is mode 0 (`0x0013dc18`). Let `A` be the first target's position in the transform table
(`0x00714b00` + index × `0x20`) plus the look-at offset `+0x320`, and `B` his position as copied at the start of
his own update (human `+0x10`, vtable `+0x9c`) plus the same offset. The helper `0x004dbb68` then acts by switch 2
(`+0x3e6`):

- **Switch 2 off** (the default): the camera keeps its distance from `P`. With `m` = max(2, 0.4 × (camera z − `P`.z)),
  when the camera is nearer than `m` to `P` in plan, it moves along the current segment's direction by `m` minus
  that distance: toward the segment's end, or toward its start when it is within `m` of the end (segment length −
  `m` ≤ its distance from the segment's first point). One step, with no easing or timer; it can leave the camera
  still nearer than `m`. The player is not moved. The camera then faces the look-at point and the frustum is rebuilt.
- **Switch 2 on** (`level83_c5_apartment.lua`): the **player** is kept at least 2 m in plan from the vertical planes
  through the previous, current and next segments. Each plane is unbounded, built from the segment and world up,
  with its positive side the one `B` is on. In turn, when `A` is nearer than 2 m to a plane on that side, `A` is
  pushed along the plane's normal to 2 m (plus 10⁻⁵). If any plane pushed, the player is placed at `A` minus the
  offset with his current rotation (`Human_SetTransform` `0x0023d440`, his vtable `+0x6c`). When he is jumping or in a
  long fall (state flags `0x400000000` or `0x1000000000`) he is landed first (`Human_Land` `0x0023e090`). The camera
  is neither moved nor turned.
- `CamRail_Collide` also builds a vertical plane 2 m in front of the camera along its level forward and passes it
  with a 0.3 m margin. The helper uses that plane only for a rail in modes 1-3 (it would push `A` to 0.3 m beyond
  it), which never call `CamRail_Collide`: the 0.3 m and 2 m have no effect.

<span id="rail-frame"></span>**Keeping the targets in frame** (confirmed (code) at the addresses cited). Two helpers,
reached only through `CamRail_CollideTwoTargets` (mode 0, step 7) and mode 3's placement (`0x0013e088`). Planes are the
camera's frustum planes from `Camera_BuildFrustum` (`0x00120c50`), read through vtable `+0x174(i)`, normals inward:
0 left, 1 right, 2 near, 3 bottom, 4 top, 5 far. For each target, `A` is his transform-table position (feet) plus the
look-at offset `+0x320`, `B` his start-of-update position (human `+0x10`) plus the same offset, and `r` his radius:
a player's scale × 0.5, or for a non-player (`+0x1b0` = −1) his capsule radius (`Human_GetCapsuleRadius`).

- **`CamRail_TargetsInFrame`** (`0x00140ad0`) is a test, one pass over the targets: false as soon as one has
  `n · A − d < r` for plane 0, 1 or 3 (left, right, bottom; never the top), or, with switch 2 (`+0x3e6`), for the
  vertical plane 2 m in front of the camera along its forward axis. The 0.3 its caller passes is the radius only when
  its last argument is 0; `CamRail_CollideTwoTargets` passes 1, so the targets' own radii are used and the 0.3 is
  dead.
- **`CamRail_CollideTwoTargets`** (`0x00140708`): when that test fails and the camera is not held, the camera's
  position, orientation, target point `P` (`+0x180`) and look-at point (`+0x2f0`) go back to their values at the start
  of the update and it faces the look-at point again (frustum rebuilt); held or not, it then calls the keeper. The
  camera is never pulled back or up.
- **`CamRail_KeepTargetsInFrame`** (`0x00140d68`) **moves the targets, not the camera**: one pass, one correction of
  each kind per target, no loop. Before it, `zmax` / `zmin` are the highest and lowest of all the targets' feet and
  start-of-update heights, and `zmax` += max(offset z `+0x328`, 1 m). Per target, in order:
    1. With switch 2 on: in mode 0, the same 2 m push off the previous, current and next segments' vertical planes as
       `CamRail_Collide` ([above](#rail-collide)); in modes 1-3, `A` is pushed beyond the plane 2 m in front of the
       camera by `r`.
    2. **Sides**: if `A` is within the margin of the left plane (else the right; only one per update), its move
       from `B` is split into the part along the plane's normal and the rest; the normal part becomes the margin
       (+ 10⁻⁵) inside the plane, and the rest is cut short by a ray from `B` (mask `0x200`, as long as the rest + the
       margin) so it stops the margin before a wall. The margin is `r`, or setting 7 (`+0x3d4`) in mode 3.
    3. **Bottom** (both callers pass this on): when `A` is within `r` of the bottom plane it is moved across it by the
       shortfall, keeping its distance from the camera (inferred from the vector steps), again cut short by a ray.
    4. **Line of sight**, when any step moved him (or in mode 3 on player 0's current camera while `+0x3e5` is
       set): a ray (mask `0x200`, plus bit 0 in mode 3) from the point 2 m in front of the camera, at `A`'s height
       (the camera's in mode 3), toward `A`, as long as min(that distance, 0.7 × the far plane) + 3`r`; on a hit `A`
       is put `r` (along the face's normal) before the face.
    5. When a step moved him or the ray hit: `A` − offset is his new place; his height is found by a ray straight down
       from `zmax`, `zmax − zmin + 1` m long, and set to the hit + 0.01 m (kept when nothing is hit). If he is jumping
       or in a long fall (state flags `0x1400000000`) he is landed (`Human_Land(1, 1)`) and his height moves only 30%
       of the way. He is placed there with his current rotation (`Human_SetTransform`, vtable `+0x6c`).

  So on a two-target rail every target, **non-players included** (`level5`'s Diego), is pushed back inside the left,
  right and bottom edges of the view and kept out of walls between him and the camera; the camera only stops.

<span id="rail-mode3"></span>**Mode 3 placement** (`CamRail_PlaceMode3`, `0x0013dcc8`; confirmed (code) at the
addresses cited unless marked). `CamModifyRail(5, angle)` at or above −360 sets the mode (`+0x3ed` = 3) and the angle
(`0x0013fac8`); below −360 it goes back to mode 0. Only the five Armies of the Night levels (`level60`-`level64`) use
it, each with `CamSetupRail("rail", player, 22, {0, −0.25, 1.25}, 0.5, 90)`, then `CamModifyRail(5, −7, 0)` and
`CamModifyRail(7, 0.5)`; their gang scripts (`level60_mothers`, `level61_hihats`, `level63_orphans`,
`level64_riffs`) later set setting 7 to −10 or −15 (a survey of the disc's scripts, values only). The mode's pieces:

- **The angle is a pitch.** The look-at point `+0x2f0` is always put 2 m from the camera in plan and
  2 × tan(angle) above it, so the camera looks at a fixed pitch: −7° in those levels, looking down. The camera
  itself stays on the rail (setting 1 aside, below); nothing raises it.
- **Start**, once, on the first update after `CamRail_Activate` (`0x0013b544` sets `+0x3e7`; the update clears it at
  its end, `0x0013d678`, and `CamRail_GatherTargets` sets it again only outside mode 3, `0x0013b9a0`): the base
  speed `+0x1a4` = 1 m/s; `P` = the targets' mean (`CamRail_UpdateTargetPoint`), segment chosen and `P` projected
  (`CamRail_ChooseSegment`, `CamRail_ProjectOnSegment`); the camera is placed at that point `Q`; the look-at point at
  2 m from `Q` toward `P` in plan, at the pitch; the goal `+0x310` = the look-at point. `+0x3ea` = 1 when the first
  target stands to the left of the segment's direction (z of the cross product > 0). Switching setting 5 on later
  does not restart: the next start is the next activation.
- **Projection in mode 3** (`0x00140308`): along the segment **in plan** (the plan distance divided by the plan
  length of the segment's unit vector, so the height follows the rail), and clamped only before the rail's first
  point and past its last; on an inner segment the point may run past the segment's end. Setting 0's reach is
  applied in mode 0 only.
- **Each later update**, with at least one target (none: the camera keeps its place, still shakes and faces the
  look-at point):
    1. **Which target.** With two or more, the one **farthest back** along the current segment's direction (smallest
       signed distance of his feet from the segment's start along it); with one, entry 0.
    2. `CamRail_KeepTargetsInFrame` (`0x00140d68`, [above](#rail-frame)) moves the targets back into frame, with the
       margin setting 7 and the line of sight from the 2 m point while `+0x3e5` is set (the update a target joined).
       Mode 3 passes `+0x3ea` and its negation in `a2` / `a3`, but the keeper overwrites both registers before
       reading them (`0x00140dd0`-`0x00140dd4`): `+0x3ea` is dead.
    3. `P` (`+0x180`) = that target's feet + (0, 0, offset z `+0x328`): only the offset's height; in mode 3 the
       mean and the damping of update step 3 are replaced.
    4. **Goal.** Segment chosen for `P`, `P` projected to `Q'`; the goal = `Q'` + 2 m toward `P` in plan, at the
       pitch. It is taken (`+0x310` = goal) only when it lies **ahead** of the look-at point along the segment's
       direction (plan dot product > 0): the camera never goes back along the rail.
    5. **Ends.** On segment 0, a camera within 10⁻⁵ m of the first point, or on the last segment within 10⁻⁵ m of
       the last point, sets the hold `+0x3e4` ([the hold](#rail)). Inferred: a camera that starts clamped at the
       first point holds at once, so these levels must start the player ahead of it.
    6. **Move**, when the goal was taken, the hold was off at step 4 and is still off: with `v` = the target's
       **stored velocity** (`Task_GetVelocity` `0x003a1f00`: the task's `+0x30`, zero while its flags `+0x54` have
       `0x600`; for a human what the locomotion writes, so 0 while a clip alone moves the body, and not the move
       made by a push) along the segment's direction in plan, the look-at point moves toward the goal by
       min(0.4 × the gap, (max(`v`, 0) + 1 m/s) × `dt`). So it moves 1 m/s faster than the target goes forward,
       never more than 40% of the gap in one update. The segment is then chosen again for the moved look-at point
       (so it **crosses joints** on its own) and the camera = the look-at point projected on the rail.
    7. **Segment blend.** While `+0x3cc` > 0 (set to 0.1 by a change of segment): the camera =
       old position − `+0x330` + `+0x3cc` × (new − old), and `+0x3cc` × 1.15 each update, until it reaches 1 or the
       step is under 0.3 m in plan (then the new position is taken at once). `+0x330` is always zero (only the
       constructor and `CamRail_CopySettings` write it), so the subtraction does nothing.
    8. The look-at point is put again 2 m from the camera in plan, toward where it was, at the pitch.
    9. Without a move (no goal ahead, or held) the goal is set to the look-at point and the camera keeps its place.
- **Finish**: `+0x2e0` = the position; with setting 1 above 0 the height is capped at the first target's feet +
  setting 1; the camera is placed; the shake (`0x00121298`, given the look-at point `+0x2f0`) moves the look-at
  point, so it turns the view; the camera faces the look-at point (`Mat_LookAt`) and the frustum is rebuilt.

So the Armies levels' camera trails the hindmost fighter along a one-way rail at a fixed downward pitch, catching up
at his forward speed + 1 m/s, and pushes everyone back inside the view (0.5 m inside the sides; with −10 or −15
during the gang fights, inferred: up to that far outside, so the side push stops).

<span id="rail-leading"></span>**Leading placement, modes 1 and 2** (`CamRail_PlaceLeading`, `0x0013e708`, set by
[`CamLeadRail(lead, seconds, ahead)`](../references/bindings/camera.md#camleadrail); confirmed (code) at the cited
addresses unless marked). `s` = +1 in mode 1 (ahead), −1 in mode 2 (behind); the lead is `+0x35c`. The target point
`P` comes from update step 3 as in mode 0 (mean, offset, damping); the hold is never read.

1. **Segment.** `CamRail_ChooseSegment(P)` as in mode 0, then, when `P`'s plan distance from the segment's start is
   more than the segment's plan length, step forward while `P` lies beyond the next point along the segment (plan
   dot product > 0, up to the last segment); then step back while `P` lies before the segment's start.
2. **Foot and lead point.** `u` = the segment's direction in plan; `F` = start + `u` × (`P`'s plan distance along
   it), at the start point's height; `B` = `F` + `s` × lead × `u`. With two or more targets, `B` moves on by `s` ×
   the farthest any target's feet are ahead of `P` along `u` (positive values only; in mode 2 the first pass never
   adds one, as decompiled, so trailing adds nothing there).
3. **Lead point's segment.** The same forward and back steps for `B`. If they change the segment, `F` and `B` are
   worked out again on the new one (the farthest-ahead target added again, in either mode) and `B` is the mean of
   the old and new `B`: the camera does not jump at a joint.
4. **Rail point.** `t` = `B`'s plan distance from the segment's start ÷ the segment's plan length, a **length**
   (the plan length of `B` − start, never negative) and not clamped on an inner segment. So a lead point
   before the start gives the same `t` as one that far after it: in mode 2 with `P` less than the lead past the
   first point, the camera stands (lead − `P`'s distance) **past** the start, mirrored, not before it; and with `P`
   up to the lead behind the start, lead + that distance past it. Ends: on segment 0 with `P` more than the lead
   behind the start, `t` = 0 (the first point); on
   the last segment, unless `P` is still before the last point and `F` is at least the lead from it, the camera
   takes the last point (`t` = 1). `C` = the 3D lerp of the segment's points by `t`, so the height follows the rail.
5. **Switch 7** (`0x0050b2ac`, default 1, set to 1 again by every `CamSetupRail`, `0x0013b2b8`): when on, the
   camera leaves the rail: `X` = `P` + (lead + the farthest-ahead extra) × the plan direction from `F` to `C`, at
   `C`'s height. So it stands exactly the lead ahead of (or behind) the target in plan, beside the rail point. Off,
   `X` = `C`, on the rail. `level54_park` turns it off (`CamEnable(7, nil, 0)`).
6. **Collision**, only when `X` ≠ `C`: a world ray (`CollisionMesh_RayCast`, mask `0x200`) from `C` toward `X`; on
   a hit `X` is pulled back toward `C` to stop short of the face (by max(0.5, 2 × the value of vtable slot `+0x5c`)
   plus that value; inferred: the near plane), and a second ray back from `X` can shorten it again. Then `P` is pushed
   out of the world by a 0.5 m sphere (`CollisionMesh_SpherePush`) and the look-at point `+0x2f0` = `P`. With the
   switch off the look-at point is update step 3's.
7. **Finish**: `+0x2e0` = `X`; with setting 1 above 0, `X`'s height **is set** to the first target's feet +
   setting 1 (a fixed height here, not mode 0's ceiling); `Camera_PushOutOfHumans` (`0x00122728`, 3 m); placed;
   faces the look-at point; with `+0x3e8` set, the position and orientation are copied to the caller; frustum; with
   two or more targets `CamRail_CollideTwoTargets` (`0x00140708`, [frame](#rail-frame)). While a shake runs the
   view is faced again after it and `+0x3ec` = 2; on the update it stops `+0x3ec` goes 2 → 1, so the look-at point
   eases back at 40% an update (update step 3).

Levels calling `CamLeadRail` (survey of the disc's scripts, values only): `level2` (3 m), `level9` (4 m),
`level51` chapters 8 and 9 (12 m, ahead), `level54` and its `alley` / `park` scripts (7 and 8 m, `ahead` false,
and the chase's own `chase.CUR_LEAD_DIST`), `level81_chase` (14 and 15 m), `level84_chase` (12 m, ahead).

**In `level3`'s chase** ([`level3`](scripting.md#level3)) the rail runs straight along y = 349 at z = 29.97, beside the
rooftops the player runs along; the main framing (setting 3 = −8, 4 = 6, 0 = 5.5, 1 = 4, 9 = 2, field of view 78°)
puts the camera 8 m behind the player along the rail, at most 5.5 m from him in plan and 4 m above his feet, looking
at a point 2 m behind him and 2 m up. Inferred from the code above with the script's values.

### Fixed and third-person cameras {#fixed-third}

- **The fixed camera** (type 0, [`CameraCreateFixed`](../references/bindings/camera.md#cameracreatefixed)) stands at
  one point and looks at the average of the humans on the shared target list plus an offset; its target is put on
  that list. Confirmed (code) at `0x00124690`.
- **The third-person camera** (type 16, [`CameraCreateThird`](../references/bindings/camera.md#cameracreatethird))
  sits a distance behind its target's facing, eased 10% an update toward it, a height above the look-at point and
  swung by an angle. Confirmed (code) at `0x001205e8`.

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
- **Each update while on**: `0x0012e9a8` takes the enemy's point (its position plus half its [body point](#body-point)
  `+0x4e0`, the hips in the body's frame, not turned by his heading, so about 0.5 m up; an object's position + 0.5 m)
  and the angle at the look-at point between the camera's horizontal view and the direction to the enemy; outside
  25°-29° it yaws by `(angle − 27°) × 0.455` toward 27°; the turn is capped at 640°/s (`11.17` rad/s) when the enemy is
  more than 29° off, not when it is under 25°. So **the enemy is held 27° off the view's centre**, beside the player.
  This counts as this update's turn: auto-follow and keep-in-view do not run.
- **On exit**: the wanted near edge is set back to the saved `+0x3cc`, which is cleared; the band eases back the same
  way (runtime: 2.4 → 2.76, 3.066, …, 4.8). **The target pitch stays 15°** until the next `CameraReset` or
  `CfgFollowCamera` (runtime: 15° 150 updates later).
- Shakes started while it is on are 0.66 as strong, and the pad's rumble thresholds are lowered by a quarter
  ([Shake](#shake)).

### Death camera {#death-camera}

`CamUseDeathCamera(human, ms, ms2)` (`0x0011daa8`) makes player 1's `Cam_Failed` (type 12) current at once, keeping the
camera it replaced (`+0x1e4`), tints the screen over `ms`, starts a blur after `ms2`, hides the HUD and removes the
overhead icons. It is the game-over shot's start (below) with the two times as arguments: the game-over shot is
`(6500, 1500)`, Rumble Survival passes `(7000, 100000)`. Confirmed (code) at the cited addresses, in order:

1. Nothing happens unless `human` resolves and the failed camera can be made (`Cam_GetFailed(1)`).
2. **View flags**: `0x0050b1e8` = 1 and `0x0050b1ec` = 0, player 1's and player 2's "view may show" flags
   (`0x00121e30`): player 1's view on, player 2's off. `Cameras_DecideViews` (`0x00121888`) sets both again on its
   next run (both 1 with one player; worked out from who still counts in co-op), so the write matters only until then
   (inferred: in co-op it drops player 2's half of the screen at once).
3. Player 0's current camera goes to `+0x1e4`, and `Camera_MakeActive(0, failed, 0)` cuts to the shot. Its activation
   (`0x001236e0`) picks a target (the last target `0x005d91b8` when it is a player, else whichever player stands
   nearer the camera it replaced) and places itself.
4. It saves the screen tint (`+0x1f8`, the manager's target tint `+0x1c4`), the HUD state (`+0x1fc`), the ambience
   level (`+0x1f0`) and the music volume (`+0x1f4`).
5. **Target**: `CamFailed_SetTarget(failed, human)` (`0x00123888`) then sets `+0x1e0` = `human` and places the camera
   again (a new random heading). So `human` is the one the shot looks down on; the activation's pick is overwritten.
6. **Tint**: `ScreenFx_BlendTintTo(ms / 1000, 0x005fdeb8, 0xd0000014)` blends player 1's screen-effects manager
   (`0x005fdeb8`, the colour controller the death camera watches) from its current tint to `0xd0000014`, the same dark
   red at 81% as the game-over shot ([Rendering](rendering.md#tint)), over `ms` milliseconds.
7. **Blur pulse**: `ScreenFx_StartBlurPulse(ms / 1000, 0x005fdeb8, 0, ms2)` arms the same manager's blur
   ([Graphics](graphics.md#screen-effects)): its level stays 0 until `ms2` milliseconds have passed (`+0x204`, tested
   by `0x0018d1d0` against the start time `+0x208`), then rises to full over `ms` / 1000 s and holds. With Rumble's
   100000 the blur waits 100 s, so in practice the arena's death has the tint and no blur (inferred).
8. `HUD_Hide(0x600840)`, then `Human_RemoveOverheadIcon` (`0x00227388`, message `0x15` to the icon at `+0x360`, [Crimes](crimes.md))
   for every player and for `human`. **Nothing is stopped**: no brain, pad or velocity is touched here.

**What undoes it.** Nothing in the camera code returns to `+0x1e4`: activation is its only reader. The shot stays
current until a script makes another camera active (`CameraMakeActive`); its deactivation (`0x00123ba0`) then releases
it. But `Camera_MakeActive` (`0x0011ee08`) **refuses** any switch while the player's current camera is type 12 and the
game state's `+0x14c` is 1 (a failure): during a mission failure the death camera cannot be replaced, only after the
state changes (the retry sets it to 0). Confirmed (code). The HUD and tint come back only in `MissionFailed_Toggle`
(`0x00155408`) on a retry from a checkpoint, and only when player 0's current camera is still type 12: the tint is set
back at once to the saved `+0x1f8`, the blur pulse is ended (`0x0018d058` with 0 s, reverse), and the HUD is restored
(`HUD_Restore`) when `+0x1fc` says it was shown. The saved ambience and music volume are not read back there (not traced
elsewhere).

The parts of the shot itself:

- **Target** (`0x001236e0`): the last target (`0x005d91b8`) when it is a player, else whichever player stands nearer
  the camera it replaced; `CamUseDeathCamera` then overrides it with its `human` (step 5).
- **Placement** (`0x001238a8`): the look-at point is the human's position; a ray straight up from it, near plane + 5 m
  long (mask `0x200`), gives the height: the camera sits that far above the body, less the near plane, looking
  straight down (a pitch of about −88°) at a random yaw (one of 36 steps of 10° from −180°).
- **Each update** (`0x00123bf0`): it turns about the vertical at 15°/s and sinks toward the body at 0.18 m/s, no nearer
  than 2.5 m, until player 1's screen-effects manager has faded out fully ([Fade](graphics.md#fade)); it then ends
  the blur pulse at once and stops moving.

**The game-over shot** (`Cam_GetFailed(1)` in the gameplay mode's update, `0x00158728`), confirmed (code) at the
cited addresses:

1. **When.** `W_GameState + 0x14c` is 1 (a failure: the players are out or busted, [Combat](combat.md#defeat)), the
   level is not an Armies level, the game-state flags `+0x152` do not have bit 2 (with it the countdown goes to 0 at
   once and the menu comes with no shot), and player 0's camera is not already type 12. In player terms:
   - **Armies levels** (`GameState_IsArmiesLevel`, levels 60-64) never get the shot: their failure counts down
     at most 90 updates (3 s, the same cap as a level completed) with the current camera still running, then
     pushes the Armies game-over (`ANGameOver_Toggle`) instead of `MissionFailed_Toggle`.
   - **`+0x152` bit 2 means a player fell out of the world.** Its only writer besides the level reset (0,
     `GameState_ResetForLevel`) is `Human_StateUpdate` at `0x002403ec`. When player 1's or player 2's human
     (handles `+0x228`, `+0x22c`) is more than 20 m below the collision mesh's lowest vertex
     ([Characters](characters.md#ground)), it sets the failure reason to global string `0x13`
     (`MissionFailed_SetReason`, `0x001d1fb8`), `+0x118` = 0, `+0x14c` = 1 and `+0x152` = 2. It does this only while
     game-state `+0x150` has bit 2, which `GameState_ResetForLevel` sets (7) and nothing else writes: always, in
     practice (inferred). That same update the countdown goes to 0 and the mission-failed menu is pushed: no shot,
     no tint, no wait.
   - The shot starts only while the failed camera does not yet exist (`Cam_GetFailed(0)` = 0); later updates of the
     same failure find it and skip the start. The slot `0x0050b170` is cleared again by `Cameras_ClearSlots`
     (`0x0011ea68`) and `Camera_Release` (`0x0011e7e0`), so each new level's failure gets a fresh shot (inferred).
2. **Start**, once: `Cam_GetFailed(1)` makes the camera on first use (`0x0050b170`); player 0's current camera is kept
   in `+0x1e4`, and `Camera_MakeActive(0, failed, 0)` makes the shot current with **no blend** (a cut), which runs the
   activation above (target, then placement). It saves the screen tint (`+0x1f8`), the HUD state (`+0x1fc`), the
   ambience level (`+0x1f0`) and the music volume (`+0x1f4`), starts a **6.5 s** tint blend to `0xd0000014` (bytes red
   0x14, green 0, blue 0, alpha 0xd0: a dark red at 104/128, 81%, [Rendering](rendering.md#tint)) and a blur pulse (1500
   ms), hides the HUD and removes the overhead icons.
3. **Placement** (`0x001238a8`): the look-at point is the human's transform position (the feet); a ray straight up
   from it, near plane + 5 m long, gives the height (hit distance − near plane: **5 m** with nothing above); the
   camera is put that far above the feet, pitched **−88.28°** about its x axis (a half-angle of −0.7704 rad), then
   turned about its own forward axis, which points almost straight down, by a random multiple of 10° from −180°
   (`Random_Int(36)`): to the eye, a top-down shot at a random heading.
4. **Motion**, each update while rotating (`+0x1fe` = 1): it turns about its own forward (view) axis at turn rate
   (`+0x1ec`, 1) × **15°/s**, and its distance to the look-at point shrinks by **0.18 m/s** × the step, never below
   **2.5 m**; the camera stays on the view axis, so it sinks straight down toward the body while the picture turns.
   It stops (and starts the next screen pulse) once the screen effect's blend has finished (`+0x1d8` ≥ 1 and
   `+0x1dc` > 0 on `0x005fdeb8`).
5. **Length.** The level-end countdown is **180 updates** (6 s); cross with more than 10 left cuts it to 10 and
   speeds the tint blend up to 1/3 s. Each update the ambience level is lowered toward 0.7 (by 0.7 / (0.9 × the
   updates left)). At 0 the tint blend is finished, the ambience and music volumes are put back and the
   mission-failed menu (mode `0xc`) is pushed; leaving it for a retry puts the saved tint back and restores the HUD.
   Over a full 6 s the shot turns **90°** and sinks from 5 m to about **3.9 m**; the 6.5 s tint blend has not
   finished by then, so the stop in step 4 is not reached before the menu (inferred from the two lengths; the menu
   stops the gameplay update, so the shot then holds).

In Coney the death camera is `repo:src/camera/failed_camera.h`, and `Cameras::startFailed()` cuts to it once per
level. It keeps the base camera's lens (60°, near 0.3, far 60) because its constructor sets none of its own
(inferred). Other choices:

- **Coney's reading**: it stays straight above the feet as it sinks.
- **Coney choice**: the screen-effect stop is not read.
- The tint, blur, HUD, ambience and music parts of the start are not built yet.
- With one player, the target is always player 1.

### Mini-game and mugging cameras {#mini-mug}

Both are per-player cameras made on first use; both start only from a single-view follow camera, return to it with a
0.3 s blend, and turn the follow camera to the shot's direction first (`Cam_Follow_TurnToMatrix`, `0x0012ded8`) so the
hand-back does not swing. The cut in is not immediate: the start queues the new camera as the follow camera's
hand-over (`+0x47c`, blend `+0x3c0`), the follow camera turns for **0.15 s** (its collision probes off), and only
then does the new camera become current, blending over **0.08 s** for a mini-game shot (0 for kind 1) or **0.15 s**
for a mugging (`CamMini_Start` `0x001363a4`, `CamMug_Start` `0x0013804c`). On the way back the mugging camera turns
the follow camera again (0.35 s, nothing queued; `CamMug_Update` `0x00138ec8`). Confirmed (code) at the cited addresses;
the shot tables' purpose (variety) is inferred.

- **The mini-game camera** (`Cam_Mini`, type 8, `0x001361a8`) frames two humans (kind 0, the mount from
  `Player_UpdateMounting`) or a human and the stereo (kind 2, `StereoTheft_Start`); no caller passes kind 1. The shot
  is 3 m from a point on the first human (the mounting player's feet + 0.5 m, or the stereo + 0.3 m), at a yaw and
  pitch drawn at random from small tables; up to three draws are tested with a sphere sweep (`0x001374d8`, clear for
  at least 2.3 m), then a fixed shot (pitch 36°; 30° and yaw 25° for the stereo; 32° for kind 1)
  ([shot tables](#mini-mug-shots)). Each update turns it at up to 3 × its angle error per
  second to keep both in frame. The mini-game's end (`0x0022d1f8`) sets its done flag (`+0x258`), and the next update
  blends back.
- **The mugging camera** (`Cam_Mug`, type 7, `0x00137e08`, from `0x002729a8`) plays `vags/misc/mug_intro` as it cuts
  in. The shot (`0x00138078`) is 2.5 m from the pair (1.8 m when the world is in the way) at a random yaw and pitch
  from small tables. Each update (`0x00138b90`) eases the field of view to 50° over 0.33 s, turns at up to 135°/s to
  keep both humans framed and stays 3 m from other humans (`Camera_PushOutOfHumans`, `0x00122728`). When the mugger or
  the victim leaves the hold it stops the intro, plays `vags/misc/mug_outro` and blends back.

#### Shot tables {#mini-mug-shots}

The random draws use `Random_IntRange(low, high)` (`0x003353f0`, both ends included). Angles in degrees; "yaw" is a
turn about the vertical, "pitch" about the shot's side axis. Confirmed (code) at each function.

**Mount** (`CamMini_PlaceKind0`, `0x001363e8`; `+0x1e0` the mounting player):

1. The point is his feet + min(max(offset z, 0), 0.5) m (+0.5 m with the usual offset), the distance 3 m
   (`+0x254`).
2. The base direction is his side axis: both sides are tested at 3 m (`CamMini_TestShot` `0x001374d8`, a sphere
   sweep from the point that must stay clear for at least **2.3 m**); the clear side wins, a frame-counter bit
   breaks a tie, and nothing is placed when neither is clear.
3. Up to three draws: pitch one of **10, 16.7, 27, 0, 22.5**, yaw one of **−22, 21, 0, 12, −14**. A draw of 0 and
   0 is placed without a test; any other is turned (pitch, then yaw) and tested. The fourth try is **pitch 36°, yaw
   0**; if that fails too the shot is not placed.

**Stereo theft** (`CamMini_PlaceKind2`, `0x00136f68`; `+0x1e0` the stereo, whose car gives the axes):

1. The point is the stereo's position + 0.3 m, the distance 3 m (tested for 2.3 m).
2. One roll of the dice per shot (`Random_Int(100)`): under 80 the yaw comes from all eight entries **10, −15, 90,
   30, 115, 60, 75, 48**, else only from the first two.
3. Each try also draws `+0x244` = 3-7° (a float range; its sign flipped on an odd frame counter; its use is not
   traced). The pitch is 0 for yaws under 21.5° or over 91.5°, else one of **17.1, 21.8, 28**.
4. Three tries, then a fixed **pitch 30°, yaw 25°**.

**Mugging** (`CamMug_PlaceShot`, `0x00138078`; `+0x1e0` the mugger, `+0x1e4` the victim):

1. The point is the mugger's position + the offset `+0x200`, whose height starts at **1.45 m**; the base
   orientation is the victim's turned 90° about the vertical (so the camera looks across the pair).
2. Pitch `+0x23c`: one of **0, 19, −5, 12.8, 7, −3** (draw 0-5); 19, 12.8 and 7 also raise the point by 0.14,
   0.11 and 0.075 m. Yaw `+0x238`: one of **−4.5, 4.5, 0, 8, −8, −13.5, 13.5** (draw 0-6), only the first five
   when the pitch is 15° or more.
3. Test: a physics sphere of the near-plane radius pushed **2.5 m** + near along the shot (object kinds 0xc-0xe,
   0x10 and 0x1c ignored) and a world ray (materials 30 and 122 skipped) must both stay clear for at least
   **1.8 m**; the camera goes to the ray's hit − the near plane, with the point raised by 2.5 × tan 5°.
4. Two tries: the drawn shot, then the plain one (victim turned 90°, no pitch or yaw); with neither the camera is
   not placed (`+0x24c` = 1).
5. A placed shot is **rolled** (a Dutch tilt), confirmed (code) at `0x00138078` and `0x00138b90`:
   - **The angle** (`+0x234`, radians): **15°** (0.2618), or **11°** (0.1920) when bit 0 of the game time in ms is
     set; then **negated** when bit 1 is set. The game time is the `GameTimer`'s `+0x48` (`0x0050b734`, read
     through its vtable `+0x34`, `0x004dc9a8`, which returns `+0x48`), so the four cases (+15°, +11°, −15°, −11°)
     come from the shot's start time, about equally often.
   - **How it is applied**: the orientation is multiplied on the right by a rotation about the camera's own **+y**
     (its view axis; the constant `0x00511730` = (0, 1, 0, 1)), `q' = q ⊗ (0, sin(r/2), 0, cos(r/2))`, as the
     scripted roll is ([Scripted camera angles](#scripted-angles)): a positive angle tips the top of the picture
     toward its right.
   - **Held for the whole shot**: each update (`CamMug_Update`) rebuilds the orientation by looking from the eye at
     the aim point `+0x210` with the world's up (`Mat_LookAt` `0x00337028`, then `Mat_ToQuat` into `+0x20`),
     keeps that un-rolled orientation in `+0x220`, and applies the same roll again; the angle never eases. The
     shot keeps the tilt until the blend back to the follow camera (0.3 s). When the follow camera is in its
     state 2 (its vtable `+0x1ec`), it is first turned (`Cam_Follow_TurnToQuat`) to the shot's base orientation
     with **no roll** (the victim's facing turned 90°, then the shot's yaw `+0x238` and pitch `+0x23c`); the tilt
     goes away through the blend.
   - To draw it, a port needs only the camera's up from the rolled quaternion (`Quat_AxisZ(q)`), as for the
     scripted rolls.

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

- **Hood** (`Cam_Hood`, type 11, `CamSetupHood`): each update (`0x00135138`) it sits on its vehicle at the **mount
  offset `+0x200`** in the vehicle's frame (`0x00134d78`: x along the vehicle's x axis, y along its y axis, z added to
  the height; default **(0, 0.3, 1.7)**), looks at the mean position of the players that still count raised by the
  **look offset `+0x1f0`**'s z (`0x00134f08`, which reads `+0x1f8` only; default **(0, 0, 1.53)**), and steps its
  field of view toward the wanted one `+0x214` by at most `dt` degrees per update, so **1° per second**. Confirmed
  (code). `+0x214` is 60, set by the constructor and copied by `CamHood_CopySettings`; `CamSetupHood` sets the fov
  itself, not `+0x214`, so a script's 65° drifts back to 60° over 5 s (confirmed (code); the arithmetic, not seen at
  runtime).
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
the base) as `(human, level, view)`, confirmed (code). The last argument goes to `+0x1d7`: 1 lets the shake move the
view, 0 makes it a rumble only. The callers:

- a hit reaction (`Human_PlayReaction`, `0x0026a6d0`), with the attack's strength bits `(flags & 0x30) >> 4` as the
  level: on the attacker's player camera with view 1 (`0x0026a8fc` / `0x0026a924`), the level raised by one (to at most
  3) while the attacker has bit `0x80000` in his word `+0xe0` (raging, inferred); and, from strength 2, on the victim's
  camera with view 0 (`0x0026a988`): **the one hit only feels it in the pad**;
- rage start (`Human_StartRageMode`, `0x00236e90`): level 1, view 1;
- a landing (`Human_Land`, `0x0023e090`, under the follow camera above): view 1;
- the animation event of **type `0x20`** (`Anim_FireFrameEvents` `0x00101dd8`, at `0x0010354c`): level = the
  event's value, view 1, on a player's camera. The human is the clip's own unless he has a state of `0x82830`, when it
  is his foe or his grab partner (or the human of handle `+0xc4`); a target whose `+0xcc` kind is one of `0x77`,
  `0x78`, `0x6a`, `0x80`, `0xa0`, `0xa1`, `0xb2`, `0xb3`, `0x4d`-`0x4f`, `0xed` gives the shake back to the clip's
  human (the kinds' meaning not traced).

Levels: **1**: amplitude 0.5, 0.10 s, rumble base 0; **2**: 0.75, 0.15 s, `0x30`;
**3**: 1.0, 0.18 s, `0x60`; 0 stops it. The update (`0x00121298`, every camera kind): the amplitude eases 65% per
update toward the level's while its time lasts; the time counts down by `dt × min(1, 54 × step)` (so slower in [slow
motion](#slow-motion)); the pad's rumble byte (`+0x41` of the pad record) gets `255 × current / level amplitude` when
that exceeds `(base >> 2) + 0x28`, capped at `base + 0x60`.

**Slots and the view offset** (`0x00121298`, confirmed (code)). A camera keeps three shakes, slot `player + 1` (0 for
a shake with no human): level amplitude `+0x1a8`, time `+0x1b4`, current amplitude `+0x1c0`, rumble base `+0x1d0`.
While a slot's time is positive its current eases 65% toward the level; once the time is negative it decays × 0.6 per
update and is zeroed under 0.001; it is never above the level. When a slot's current reaches 0 the slot, the offset
and the pad's rumble bytes (`+0x40`, `+0x41`) are cleared. Each update, only in game mode 1, with `A` = min(the sum of
the currents, the largest level), the offset `o` (`+0x190`) is, per axis,

`o = A × (0.5 × o_last + 0.5 × (u − 0.5))`, with `u` a fresh uniform draw in [0, 1) (`Random_Unit`, `0x003354e0`, three
draws for x, y, z),

so each axis stays within ±`A` / 2 m and changes smoothly (half the last offset carried over). It is added only when
`A` > 0, `+0x1d7` is set and switch 6 (`0x0050b1d0`) is on. Each caller passes the point it moves: the follow camera
passes its **aim point** `+0x240` (`0x0012cf94`, just before `Mat_LookAt`), so its shake turns the view and does not
move the camera; with no point given the base adds it to the look-at point `+0x180`.

### Slow motion {#slow-motion}

`0x005148a0` (`CfgFollowCamera`'s last argument, 0.2 in `global.lua`) scales **the characters' step**: an animation
event of type `0x2e` on a player's clip (`0x00101dd8`, one player only) calls `0x0041ab30`, which sets the step
`0x005102cc` to `0x005148a0 / 30` (1/150 s) and marks the player; type `0x2f` (`0x0041ab60`) unmarks it and, when no
player is marked, sets the step back to 1/30 s. Every character update then advances `dt` = 1/150 s, so humans,
animation and the cameras' character-step logic run at 20% speed while frames keep their rate. Confirmed (code).

**Which clips** (a survey of the disc's animation clips, counts only): 52 clip files (44 distinct names) carry the
pair, always one `0x2e` and one `0x2f`, 1 to 8 clip frames apart (4.4 on average), so a slow-motion moment is short.
They are all **finishing moves**: the grab power moves (`gen_grab_front_power*`, 18 files, plus 2 reactions) and the
mount power moves (`gen_mount_power*`, 16), the tandem attacks' ends (`gen_tandem*` and `samo_bo_tandem*`, 13 files,
one begin), the bottle smash (`bottle_smash_attacker`) and one boss counter (`bopp_bo_grab_counter_victim`, 2).
No rage, running or ordinary attack clip has one. Since the event fires only on a player's clip, a non-player's
finisher does not slow time. Inferred: counted from the clips' frame events, not read from the code.

The other writers of the step, confirmed (code): `HuSetSlowMo(fraction)` (`Human_SetSlowMo`, `0x0023b778`), step =
fraction / 30 for 0 < fraction < 1, else 1/30; and Luther's boss tactic: `BossLutherTactic_StartSlowMotion`
(`0x0030c8c8`) sets 1/60 s for 1 s of game time, then `BossLutherTactic_UpdateSlowMotion` (`0x0030c8f8`) 1/120 s for
4 s, then 1/30 s again.

**Game time does not slow down.** `GameTimer_Update` (`0x00145a10`) advances game time (`+0x40`, milliseconds `+0x48`)
by its own fixed step (`+0x4c`, 1/30 s) × its scale (`+0x50`, 1.0 from the reset; no other writer among the timer's
functions) every update and never reads `0x005102cc`. So everything timed in game milliseconds (the combat timers such
as stun and ground; the missions' and Luther's own timers above) keeps counting frames at 30 a second while the
characters move at 20%: a stun of 1 s ends after 30 frames, in which a slowed character has lived only 0.2 s (inferred
from the code; not seen at runtime). The cameras are not slowed either: `Humans_Update` runs `Cameras_Update`
(`0x0011e878`) with max(step, 1/30) (`0x00249b50`), so 1/30 s.

## Coney's implementation

`src/camera/follow_camera.*` is the follow camera of `Cam_Follow_Update` (`0x0012ae58`), stepped after the human
by `src/human/player.*` and drawn by `--play-level` ([Building](../guides/building.md#playing-a-level));
`src/camera/follow_collision.*` holds its rays against the collision mesh:

- the look-at point is the feet + 1.4 m, its move each update limited by its length `d` as in [step 5](#update):
  all of it within 0.4 m, `1 − 2 × (d − 0.4)` of it to 0.8 m, 20 % beyond; in the air (a jump or a fall) it
  follows the feet directly. A climb's rise of 2.64 m is followed at 20 % for 6 updates, then in 3 (the last one a
  few millimetres);
- the view faces the **[aim point](#aim-point)** (`FollowCamera::aimPoint()`, the trace's `aim_x`-`aim_lag`), stepped
  after the world collision: the lead along the target's facing eased 35% an update (0.365 m ahead standing or walking
  away, as at runtime), the height from the body point (`Human::bodyPoint()`, the hips' bone 2 times the scale) with
  its dead band, the running bob and the sprint sway (Coney's own seeded random numbers), the 1.0 m sphere push, the
  lag from 0.06 and the pull back in plan. **Coney's reading** of the state flags: grabbing (`0xc0`) is holding a
  victim in a grab's pair; down (`0xe2c00`) is holding one with no pair (a tackle), mounted, grounded, arrested or
  knocked out; the target counts for the cameras unless dying, dead, knocked out, mounted or arrested (the upgrade
  and key exception is not built), and a long fall is airborne while not counting or grounded. The long fall's 0.6 and
  the halving apply to the update's move, not the eased lag (**Coney's reading**); the blocked-wall share uses the main
  ray's hit face. **Inferred**: the lead's world ray is a mesh ray with no type mask, skipping the see-through
  materials, and the lead's view direction is taken across the ground (runtime 0.363 m, not the 0.375 m a pitched
  view gives). Not built: the slope case, the 0.75 s timer after the aim push, the takeover, aiming, other game modes
  and two targets;
- the wanted position stays put unless its distance leaves the leash band, 4.8-5.3 m (the default distance and 0.5 m
  more, as in the street), then moves along that line to the band; the camera moves 22% of the way to it each step, held
  inside the hard band (4.56-5.65 m), which widens at once and shrinks by 1% of the difference an update;
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
- the **height hold** (step 12): `holdHeight()` eases the wanted position's height 30 % × `+0x398` of the way an
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
  A cut (`Cameras::cuts()` counts them) is drawn as a jump: a frame drawn between two steps takes the new camera
  whole instead of sweeping from the old one, as the original draws only whole steps.
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
  it stays. `CamReversePoizo` flies it back: the points reversed, each segment's time moved with it, the new end
  function, the flight started again (**Coney's reading**: each point keeps its own function). A point's angles turn
  it as a locked camera's do (`camera::scriptedOrientation()`, the same code in the original); **Coney's choice**: the
  look-at point is 3 m ahead;
- the **combat camera** ([Combat camera](#combat-camera)): with L1 held while the player has a fight target
  (`Fighter::target()`) the band's wanted near edge goes to 2.4 m (4.8, 4.44, 4.134, ... at 4.5/s) and the target pitch
  to 15°, the enemy's point (position + half its velocity) is turned toward 27° off the view's centre (0.455 of the
  excess, at most 640°/s beyond 29°), and on release the saved band comes back while the pitch stays;
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
- **Locked cameras**: their angles and path points' are turned into a view as the original does
  (`camera::scriptedOrientation()`: `q = qz(heading) × qx(pitch) × qy(roll)`, heading anticlockwise from +y, a positive
  pitch looking up, [Scripted camera angles](#scripted-angles)), and the play mode draws through the camera's own up,
  so a roll shows. The line of sight test, the blend's sphere push and keep-in-view's ray (for a range above 0) are
  left out.
- **A follow camera that is not current** is not updated; it only notes where the player is, so a reset or the
  activation places it on him.
- **The shake**: one shake on the manager, applied to whichever camera is current; the view offset is a random share in
  [-1, 1] of the amplitude × 0.05 m on each axis (the original's form: [Shake](#shake)); after its time the amplitude
  eases back to 0 at the same 65 %. Coney has one player camera, so a player's hit shakes player 1's. The animation
  event that starts a shake (type `0x20`, [Shake](#shake)) is not wired yet.
- **Slow motion**: the combat timers (stun, ground and game time, `nowMs`) still count whole updates of 1/30 s, and the
  cameras update by the frame's 1/30 s, as in the original ([Slow motion](#slow-motion)).
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
- **The height hold** is never entered by the player yet: the probe and its lower heights are now on the page
  ([Height hold](#height-hold)) but not modelled, nor the two special modes' 40.5 % and 48 %.
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
  cutting to the follow camera when it was current (**Coney choice**).
- **`CamSetFollowHeading(degrees)`** (`FollowCamera::placeBehind()`, the placement `CameraReset` uses): the camera at
  once along the player's forward turned clockwise by `degrees` (180 behind him), looking back at him at the target
  pitch, at the preset distance nearest its own (the sprint zoom's saved edge while one is held) clamped to the band;
  the band moves to that preset and the zoom step to the one after it.
- **The rail, fixed and third-person cameras** (`src/camera/rail_camera.*`, `fixed_camera.*`, `third_camera.*`):
  the rail camera follows [Rail cameras](#rail) for player 1 (one target, the set-up's): the settings and their
  easing, the target and look-at points, the damping, mode 0's segment choice, foot, reach, hand-over, ceiling, fixed
  pitch and end hold, and the leading modes. **Coney choices and stand-ins**: the end hold lasts only while its
  condition does (what clears it is not traced); the tie between two nearest rail points, the 40% look-at catch-up
  (`+0x3ec`), the `+0x330` vector, the split-screen halving and extras, mode 3 (settings 5, 6, 7) and the push out of
  world collision are not built; a rail with no points stands 3 m behind and 1 m above the look-at point. The fixed
  camera always looks at the average (the original's single-entry case is not traced). `CamDelete` forgets a fixed
  camera as it does a locked one and keeps third-person cameras.

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
  player moves, with the angle measured along the camera's view at the start of the update, which faces the aim point,
  not the look-at point (about 2° more at a run's circle; [In the street](#street)); with the stick held sideways the
  player runs in a circle. At `level99`'s checkpoint 1 it was not seen. Implement it, with the leash and the right
  stick. The player's stick is turned by the camera's heading before it reaches the character
  ([Characters](characters.md#input)), so the camera must ease, never snap. Gate it on the gait (walk, run or sprint;
  not idle, sneak speed or jog), not on which clip plays, and hold it off from a blocked main ray until the player stops
  (the `+0x45d` latch; this is why checkpoint 1 has none). With the stick held sideways the original circles at about
  190°/s at a run and 143°/s at a walk, the rule's turn plus the leash's drag.
- **Sprint zoom** ([Sprint zoom](#sprint-zoom)): on the first update at the sprint gait save the band, zoom and
  target pitch and start a 0.5 s timer; the band's near edge moves by `d × |d| / T × dt` toward the minimum distance
  (`d` what is left, `T` the timer, after it has counted down once), the target pitch in a straight line to 7°;
  250 ms after the sprint gait ends, the same back to the saved values.
- **Look-at point**: limit its move per update by the distance rule of [step 5](#update) (20% above 0.8 m, a
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

In Coney, `CamCanSee` asks whether player 1's current view sees the point 1 m above a human or 0.3 m above an
object (`camera::canSeePoint()`: within the range and the far clip, inside the 4:3 view window beyond the near clip,
no collision triangle on the line, **Coney's reading**: the default ray mask), and `CamGhostDoor` sets type bit
`0x200` on a door's two triangles, which the follow camera's rays skip. `CameraSetClipping` sets a locked camera's
near and far clips and the follow camera's own far clip (at most 150; the follow camera keeps its 0.1 near clip),
`CameraGetActive` answers the current camera's handle (NilHandle for a scene's or for player 2), `CamGetPos` its
position, and `CamSetFollowPos` puts the follow camera at a point at once.

## Open questions

- **The circling rate** (answered): the original circles at about 190°/s at a run and 143°/s at a walk; the 122°/s
  and 127°/s first given were the rule's share ([In the street](#street)). The rule's `a` is measured from the
  camera's orientation, which faces the aim point. Measured that way, the formula matches the turn exactly.
- **The sprint zoom** (answered, [Sprint zoom](#sprint-zoom), with its gates: no enemies or the nearest within 12 m,
  and `0x0050b19c` the number of player cameras).
- **The collision step** (answered, [World collision](#collision)): the table of fractions is never read, and the side
  angle history (`+0x428`-`+0x430`) drives the centring in a gap.
- **Slow motion's clips** (answered, [Slow motion](#slow-motion)): grab and mount power moves, tandem ends, the
  bottle smash and one boss counter.
- **The combat camera's 0.4 keep-in-view** (answered): `0x0012e170(0.4)` keeps the player's brain target (brain
  `+0x124`) in view while he is in a fight stance that counts for the camera and no
  secondary target is watched ([update step 9](#update)).
- **Teleports** (answered): `Teleport` / `TeleportToFlag` do not touch the cameras. Confirmed (code): neither
  `Object_Teleport` (`0x00385bb8`) nor `Object_TeleportToFlag` (`0x00385db0`) calls the camera code. Neither do the
  human's set-transform slot (`Human_SetTransform` `0x0023d440`: pair break, ground ray, brain heading, "moved" marks
  `+0x1b3` and `+0x54` bit `0x40000000`) or slot `+0x14c` (`Human_ResetMotion` `0x0023f158`: velocities and turn
  zeroed, transform re-applied), and the only reader of `+0x1b3` is `Human_GetNavPolygon`. So the follow camera sees
  only the target's new position. Its look-at point follows a move of more than 0.8 m at 20% of the gap per update
  ([update step 5](#update)), and once the held position is more than 1.3 × the maximum distance from the look-at
  point (or 30 m above or below it), step 7 puts it behind the player as `CameraReset` does, with one player camera
  only. A script that wants a cut calls `CameraReset` itself (inferred: the camera's path for a long move follows from
  those two steps; not seen at runtime).
