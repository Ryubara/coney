# Characters (humans)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Runtime claims were made in
PCSX2 2.9.94 (2026-10-04) by reading memory over PINE in `level99`, checkpoint 1, and say so; those about sprint,
jumps and climbs (2026-10-05) in a street ("the street"), from save states made at test spots, with pad
input patched in through a copy of the state; the street is in `level99`'s world (its spots, ground and objects are
where `level99`'s collision mesh has them, [Feel comparison](feel.md)). The disc survey read the
NTSC-U disc's WAD with throwaway scripts outside the repository and reports counts and hashes only.

## Purpose

Every person in the game, the player included, is a **human**: one object of a single class with a skinned model,
an animation player and a brain. The player is a human whose controller is a pad instead of the AI. This page covers
what the first playable milestone needs: how a level script creates the player, which files make a character, the
object's layout as far as it is known, how pad input becomes movement, how a speed picks idle, walk or run, and the
player's sprint, stamina, jump and climbs. The animation format is on [Animation](formats/animation.md); the camera
that follows the player on [Camera](camera.md).

In one paragraph: a level script calls `HuCreate(name, type, position, heading, ..., player, gang)`. The game takes a
free slot among **60 static humans**, remaps the type to a behaviour class, finds the character's model by name in
the **Character List** of `warriors.glr`, snaps the position to the ground with a short ray cast and, for player 1,
binds pad 0 and the camera. The character's files are three resources named by CRC: the model (an RW clump with a
32-bone HAnim skeleton and a PS2 skin), its texture dictionary, and its **character data** (its animations and an
anim id → animation table). Each update (30 per second) the pad's stick, already turned into camera space, gives a
direction and a magnitude; the magnitude chooses walk (from 0.12) or run (above 0.95), the speed ramps towards it at
24 m/s², and the heading turns towards the stick at a limited rate. Holding L2 at a run sprints while stamina lasts;
Triangle climbs a fence or wall in front of the player or, at a run, jumps.

## Original structure

`Human/` (`0x002176b8`-`0x00323298`, [Source map](source-map.md)) holds the human; the model side
is in `Graphics/Character.cpp`, `CharacterModel.cpp` and `Animations.cpp`. Names are ours unless a path or class
string gives them.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00358428` | `HuCreate` binding | reads the Lua arguments, calls `Human_Create` | confirmed (code) |
| `0x00233d60` | `Human_Create` | heading quaternion, slot, init, name, player; returns the handle | confirmed (code) |
| `0x00217f08` / `0x00217ec8` | `Human_AllocSlot` / `Human_FreeSlot` | a free index below 60 in the object table | confirmed (code) |
| `0x00218008` | `Human_Init` | type remap, model, ground snap, defaults | confirmed (code) |
| `0x0021cda8` | `Human_SetName` | 15 characters at `+0x80` | confirmed (code) |
| `0x00229c40` | `Human_MakePlayer` | for player 1: pad, HUD, camera target | confirmed (code) for the call; roles inferred |
| `0x00228730` / `0x0021cb78` | static initialiser / constructor | builds the 60 humans at start-up | confirmed (code) |
| `0x00228800` | `CharClass_Get(id)` | `0x00684620 + id × 0x1ac` | confirmed (code) |
| `0x00383f38` → `0x00228af8` | `CfgChar` | fills a character class | confirmed (code) |
| `0x003842c0` → `0x00228888` | `CfgSpeedClass` | fills a speed class | confirmed (code) |
| `0x00221710`, `0x002215d0`, `0x00221620`, `0x00221670`, `0x002216c0` | speed getters | base, walk, jog, run, sprint | confirmed (code) |
| `0x00221760` | `Human_GaitForSpeed` | a speed to gait 0, 2, 3, 4 or 5 | confirmed (code) |
| `0x002213d8` | `Human_MaxTurn` | the turn limit per update | confirmed (code) |
| `0x00146078` | `PlayerRecord_Update` | pad → stick angle and magnitude | confirmed (code) |
| `0x00240e38` | `Human_PlayerLocomotion` | stick → velocity and heading | confirmed (code) |
| `0x00248df0` | `Human_Lean` | a lean from the turn rate | confirmed (code) |
| `0x00259578` | `Human_ChooseAnimState` | picks the anim state, calls its builder from `0x005106e0` | confirmed (code) |
| `0x0025b200`, `0x0025b9f8`, `0x0025f770` | move, run-start and idle task builders | see [Clip selection](#clip-selection) | confirmed (code) |
| `0x0025ec28` | `Gait_BlendForSpeed` | speed → gait blend target 0-3 | confirmed (code) |
| `0x0023fea8` | `Human_StateUpdate` (vtable `+0x13c`) | per update: state function, gravity, out-of-world, move | confirmed (code) |
| `0x0023d8c8` | `Human_Move` | slope factor, physics sweep, ground snap | confirmed (code) |
| `0x0023eab8` | `Human_SnapToGround` | 1.5 m ray down from 1 m above the feet | confirmed (code) |
| `0x0023dc58` | `Human_StartFall` (vtable `+0x154`) | airborne flag, fall target, camera ledge hint | confirmed (code) |
| `0x0023e408` | `Human_LandingTest` | the airborne sweep's floor test (`n.z` > 0.65) | confirmed (code) |
| `0x0023e090` | `Human_Land` | clears airborne, fall damage, `vz` = 0 | confirmed (code) |
| `0x003a2158` / `0x003a21c0` | `Object_SetAirborne` / `Object_SetGrounded` | flag `0x4000000` / `0x2000000` in `+0x54` | confirmed (code) |
| `0x0021b0b8` | `Object_SetPosition` | writes the transform table `0x00714b00` | confirmed (code) |
| `0x0033e278` | `PhysicsBody_Sweep` | moves a body by `v × dt`, up to three sliding passes | confirmed (code) for the steps listed |
| `0x00347c08` | `PhysicsMesh_SweepCapsule` | the walking body's sphere swept against the mesh's walls, with the 0.25 m step rule | confirmed (code) |
| `0x003477c0` | `PhysicsBody_PushOutOfWalls` | sphere against the collision mesh's walls (`n.z` within ±0.65) | confirmed (code) |
| `0x0021a490` | `Human_PushOutInAir` | the push-out sphere (or a bone segment) while airborne and in some states | confirmed (code) |
| `0x00219d50` | `Human_OnContact` | the body's contact handler: landings, fences while climbing, objects | confirmed (code) for the parts cited |
| `0x0027c120` | `Player_UpdateActions` | the player's commands each update: climb, jump, block, sprint | confirmed (code) for the parts cited |
| `0x0027ce90` | `Player_UpdateSprint` | clears the sprint flag and sets it again while L2 is held | confirmed (code), runtime |
| `0x00223188` | `Human_StaminaMax` | stamina maximum from the power class | confirmed (code) |
| `0x002562d0` | `Human_DrainMeters` | stamina drain while sprinting (and a second meter) | confirmed (code) |
| `0x00256a60` | `Human_RefillMeters` | stamina refill (and the second meter) | confirmed (code) |
| `0x002829e8` / `0x0023db48` | `Player_TryJump` / `Human_BeginJump` | the jump's checks; take-off gait and height | confirmed (code) |
| `0x002217f0` | `Human_LaunchJump` | the jump's velocity, then airborne | confirmed (code) |
| `0x00240898` | `Human_AirControl` | the jump's state function: steering in the air | confirmed (code) |
| `0x002826f0` | `Climb_TryStart` | the two forward probes; fence, wall and their short forms | confirmed (code) |
| `0x00282370` | `Climb_ProbeTop` | finds the obstacle's top and picks fence or wall | confirmed (code) |
| `0x00281c20` | `Climb_Start` | reach window, start point, the three-clip chain | confirmed (code) |
| `0x00281450` / `0x00281838` | climb callbacks | end of the first clip, from a run / from standing | confirmed (code) |
| `0x00249108` | `Humans_Update` | the characters' update, every second task-manager tick | confirmed (code) |
| `0x00249b98` | `Humans_MarkSkeletons` | marks every skeleton for an update, from mode 1 | confirmed (code) |
| `0x001783d0`, `0x0018e9e0`, `0x0016e8f0` | resource loaders | model (type 3), textures (type 4), character data (type 5) | confirmed (code) |
| `0x0016e258` | chunk `0x08` handler | the character data's anim table | confirmed (code) |
| `0x00177b80` / `0x00174d00` | `CharacterInstance` create / constructor | the model instance (0x330 bytes, vtable `0x00538a78`) | confirmed (code) |
| `0x00217a98` | `Human_AttachInstance` | gives the human its instance (`+0xd8`) | confirmed (code) |
| `0x00175080` | `CharacterInstance_GetAnim(id)` | an anim id to an animation | confirmed (code) |
| `0x0023ac50` | `AnimCallback_Dispatch(human, id)` | runs the script's anim callback ([Animation callbacks](#anim-callbacks)) | confirmed (code) |
| `0x001897a8` | resource manager update | loads missing characters and dynamic animations later | confirmed (code) |

## Data

### The human object (0x6d0 bytes) {#the-human-object}

Sixty humans live in one static array at `0x00640c80` (human `i` at `0x00640c80 + i × 0x6d0`), built at start-up by
`0x00228730` with the constructor `0x0021cb78`. A human in use has an entry in the handle table
(`0x006ebd38`, 0xb00 entries of `{pointer, u16 serial}`; a handle is `serial | index << 16`, resolved by
`0x00390180` / `0x00390268`). Vtables: `0x0053f088` at `+0x00`, `0x0053f020` at `+0x70`, `0x00544b98` at `+0xe8`.
Confirmed (code); offsets with "runtime" were checked on Rembrandt.

| Offset | Type | Meaning | Evidence |
| --- | --- | --- | --- |
| `+0x10` | vec4 | position, game axes (`z` up), metres | confirmed (code), runtime |
| `+0x20` | quat | rotation `(x, y, z, w)`; a heading is a rotation about `z` | confirmed (code), runtime |
| `+0x80` | char[16] | name (`Rembrandt`) | confirmed (code), runtime |
| `+0x90` / `+0x92` | u16 | own handle's serial / index | confirmed (code) |
| `+0xcc` | int | behaviour class (`0x1e` for Rembrandt): selects the class record for speeds | confirmed (code), runtime |
| `+0xd0` | int | the type passed to `HuCreate` (`0x20`) | confirmed (code), runtime |
| `+0x54` | u32 | object flags: `0x4000000` airborne, `0x2000000` on the ground ([Ground](#ground)) | confirmed (code) |
| `+0xd4` | pointer | the human's 0x180-byte record (`0x0065a540 + i × 0x180`, [below](#the-record)) | confirmed (code), runtime |
| `+0xe0` | u64 | human flags. `Human_MakePlayer` sets `0x2` (may start a climb from a run), `0x4` and `0x2000000` and clears `0x8`; `0x4000000` keeps stamina full; `0x10000000` forbids a jump; `0x20000000` forces the anim state and forbids a climb. Rembrandt: `0x24004440407` | confirmed (code), runtime |
| `+0xd8` | pointer | the `CharacterInstance` (model, skeleton, animation) | confirmed (code) |
| `+0x1a0` | pointer | the physics body (0 for none) | confirmed (code) |
| `+0x1a8` | int | the gait for the current speed (0, 1-5; `0x0022aeb0`), used by the lean | confirmed (code) |
| `+0x1ac` | float | current speed (length of the velocity, written with it) | confirmed (code) |
| `+0x1b0` | s8 | player index, -1 for none | confirmed (code) |
| `+0x1b8` / `+0x1b9` | u8 | [power class](#power-classes) of a non-player / of a player (64 for Rembrandt) | confirmed (code), runtime |
| `+0x1d8` | int | material of the ground under the feet (5 when none) | confirmed (code) |
| `+0x230` | vec4 | ground normal from the last snap (`+0x238` its `z`) | confirmed (code) |
| `+0x280` | int | an attachment: −1 normally; otherwise the human moves without the physics sweep | confirmed (code) for the tests |
| `+0x298` / `+0x29c` | float | turn this update / smoothed lean (radians) | confirmed (code) |
| `+0x2c0` | vec4 | where a fall will end (`Human_StartFall`); a climb's start point (`Climb_Start`) | confirmed (code) |
| `+0x2f0` | vec4 | external push velocity, added to the velocity when moving | confirmed (code) |
| `+0x37c` (`+0xdf` as a word index) | int | model index in the Character List | confirmed (code) |
| `+0x384` | int | airborne updates so far | confirmed (code) |
| `+0x390` | vec4 | last ground position | confirmed (code) |
| `+0x3a0` | float | vertical velocity, kept by the locomotion, integrated by gravity while airborne, 0 on landing | confirmed (code) |
| `+0x3a4` | float | speed multiplier, set to 1.0 by `Human_Init` (`0x002180c8`); Rembrandt's speeds match his clips exactly, so 1.0 at runtime | confirmed (code); runtime inferred |
| `+0x3c0` | int | the gait at a jump's take-off (picks the launch speed) | confirmed (code) |
| `+0x3c8` | 7 × 0x28 | dynamic animation slots | confirmed (code) |
| `+0x560` | float | the height (`z`) at a jump's take-off | confirmed (code) |
| `+0x5b9` | u8 | 1 while a climb clip (437-460) plays, set by `Human_StateUpdate` | confirmed (code) |
| `+0x5d8` / `+0x5dc` | float | last stick angle / magnitude | confirmed (code) |
| `+0x5e0` / `+0x5e4` | float | last turn step / last heading error (turn smoothing) | confirmed (code) |
| `+0x65c` | float | body scale: 1.0, then set by `Human_Init` through `0x00219608` to `1 − 0.01 × n` (`n` from a division not traced; at least 0.99 in one case); read by `0x0021d020`. 0.97 for Rembrandt | confirmed (code), runtime |

**Per-player record** (`0x00660f50 + i × 0x2c`, 60 of them, one per human index), confirmed (code) at `0x00146078`:

| Offset | Meaning |
| --- | --- |
| `+0x00` | the pad's buttons (8 bytes copied from the pad record's `+0x08`, [Front end](frontend.md#pad-record)) |
| `+0x08`, `+0x0c` | stick angle, two buffers (radians, `atan2` of the camera-turned stick) |
| `+0x10`, `+0x14` | stick magnitude, two buffers, clamped to 1 |
| `+0x18` | which buffer is current (toggles 0/1 each update; the other is the previous update's) |
| `+0x19` | pad index, -1 for none (0 for the player at runtime) |
| `+0x1b` | 1 while the human is pad-controlled; 0 hands it to the AI |
| `+0x1e` | when not 0, `Player_UpdateActions` is skipped (`0x00254e78` via `0x001480e0`); 0 for every human in the street, so AI humans act on `+0x20` too (confirmed (runtime)) |
| `+0x1f` | input locked: angle π/2, magnitude 0 |
| `+0x20` | this update's command id ([Buttons](#buttons)), read by `0x00147ef8`; cleared each update for a human without a pad (`0x00146000`) |
| `+0x24` | a pending command (from table entries whose mask is `0xfe`) |

A second per-human record of 0x2f0 bytes at `0x006d53f0 + i × 0x2f0` is the human's **brain** ([AI](ai.md#brain)).

### The 0x180 record {#the-record}

Each human's record (`+0xd4`), as far as this page uses it. Confirmed (code) at the accessors named; runtime values
are Rembrandt's.

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | u64 | **state flags**: test `0x002265f0`, set `0x002265d0`, clear `0x00226620`; bits below |
| `+0x08` | u32 | more flags: test `0x00226660`, set `0x00226640`, clear `0x00226688`. `0x10` forbids a sprint, `0x40` is set while climbing over, `0x400000` slows a body in the air (velocity × 0.95 per update); any of `0x84240` refuses a climb and any of `0x41a420` stops the stamina refill. Seen at runtime: `0x10000000` while a start clip plays, `0x80000` during a run stop and the first and last clips of a climb, `0x1000000` while landing from a jump |
| `+0x14` | int | state code (setter `0x002266a8`) |
| `+0x18` | int | anim state ([Clip selection](#clip-selection)) |
| `+0x20` | int | anim id playing (getter `0x002266b8`) |
| `+0x28`-`+0xb3` | int[35] | [anim slots](#anim-slots) |
| `+0x144` / `+0x146` | s16 | **health** / its maximum ([Combat](combat.md#damage)) |
| `+0x148` | s16 | the **power meter** that grabs spend, refilled at the power class's `+0x2a` per second to the maximum `0x00223068` returns (`+0x28`, [Combat](combat.md#power-meter)) |
| `+0x14a` | s16 | **stamina** ([Sprint](#sprint)) |
| `+0x14c` / `+0x154` | u32 | game time (ms, `*(0x0050b734) + 0x48`) of the last update of the second meter / of stamina |
| `+0x150` / `+0x158` | float | the fractions carried between updates of the two meters |
| `+0x164`-`+0x17c` | float | the speeds ([Speed classes](#speed-classes)) |

State flag bits used on this page:

| Bit | Meaning |
| --- | --- |
| `0x1000000` | sprint asked for (L2 held, [Sprint](#sprint)) |
| `0x1`-`0x8000`, `0x4000000` | fight stance, grabs, mugging, tackles, throwing, blocking, theft ([Combat](combat.md#state-flags)) |
| `0x200000000` | dead |
| `0x400000000` | jumping (set at launch) |
| `0x800000000` | falling (a drop) |
| `0x1000000000` | a long fall |
| `0x2000000000` | landing |

The three airborne bits (`0x1c00000000`) are cleared on landing.

### Power classes {#power-classes}

`CfgPowerClass` fills records of 0x44 bytes at `0x006619a0 + class × 0x44`. A human's class is byte `+0x1b9` for a
player and `+0x1b8` otherwise (`0x00222b78`). Confirmed (code) for the reads at the cited addresses; Rembrandt's
values confirmed (runtime). A street civilian (class 2) had a 0.35 hurt threshold, stun 750 ms, ground time 2000 ms,
power 200, byte `+0x36` 4 (confirmed (runtime)). The AI's fields `+0x08` (block chance), `+0x0c` (block chance while
hurt), `+0x24` (counter chance) and `+0x37` (the pattern-reading threshold) are on [AI](ai.md#block); `+0x32` and
the bytes after `+0x37` are not traced. The sparring Warriors' class 40 is on [AI](ai.md#level99):

| Field | Rembrandt (class 64) | Use |
| --- | --- | --- |
| `+0x00` | 1.3 | as the attacker, scales the victim's stun for weapon hits of 20-49 damage (`0x0022f658`) |
| `+0x04` | 0.3 | the **hurt** threshold: below this fraction of maximum health the human is hurt (`0x00222ff8`) |
| `+0x10` / `+0x14` | 1.0 / 1.0 | the stun time's / the ground time's factor while hurt ([Combat](combat.md#reactions)) |
| `+0x1c` / `+0x20` | 3.0 / 3.0 | AI only: the factor on `CfgAttackDelay`, the second when the target is down (`0x00223800`) |
| `+0x28` | 400 | the **power meter's maximum** ([Combat](combat.md#power-meter)) |
| `+0x2a` | 60 | the power meter's refill per second |
| `+0x2c` | 135 | **stamina maximum** |
| `+0x2e` | 40 | **stamina refill per second** |
| `+0x30` | 200 | a **stun**'s length, ms (`0x0022f658`) |
| `+0x34` | 2750 | the time a knocked-down human stays **down**, ms (`0x0022f100`) |
| `+0x36` | 3 | in a grab struggle, the grabbed human's strike costs the grabber 1 / this of its power (`0x0027fd68`) |

`Human_StaminaMax` (`0x00223188`) returns `+0x2c`; for a player, when unlockable `(6, 12)` is unlocked, it returns
`+0x2c × (1 + b × 0.01)` rounded, where `b` is byte 3 of `CfgWarriorUpgrade`'s record (`0x006b6650`). The power
meter's maximum (`0x00223068`) is `+0x28`, likewise raised by byte 2 with `(6, 11)`, then times `+0x18` while the
human is hurt. Confirmed (code); the unlocks: [Unlockables](player-state.md#unlockables).

**Which class a human has** (`Human_Init`, `0x00218008`; confirmed (code)): `+0x1b8` and `+0x1b9` both take the
type's `CfgChar` byte `+0x11d`; for a type of category 14 (the Warriors) `+0x1b9`, the class it plays with as a
player, is then replaced by `0x00222ba8`'s by type: 58 Ajax (types 11-14), 59 Cleon (1-4, 189), 60 Cochise (15-17),
61 Cowboy (18-20, 188), 62 Fox (21-25), 63 Vermin (26-29, 191), 64 Rembrandt and Ash (30-32, 38-40), 65 Snow (33-37,
190), 66 Swan (5-10), 7 for any other. The **Warrior class** (`+0x1ba`, `0x00222c20`) follows the same groups: 0
Ajax, 1 Cleon, 2 Cochise, 3 Cowboy, 4 Fox, 5 Vermin, 6 Rembrandt and Ash, 7 Snow, 8 Swan, 9 anyone else. Each
difficulty script sets every class again and re-runs `CfgChar` for 63 types. Every value:
[Power classes](../references/power-classes.md), [Warrior classes](../references/warrior-classes.md).

### Character classes {#classes}

`CfgChar` (in `config_preload2.lua`) fills a 0x1ac-byte record per type at `0x00684620 + id × 0x1ac`: 45 floats from
`+0x00`, 16-bit values from `+0xb8`, the model index by name at `+0x112` and by `"<name>_a"` at `+0x114` (used in
levels 60-64), the speed class byte at `+0x11c`, and four strings (32 bytes each) from `+0x14c`. Rembrandt's call is
`CfgChar(32, ..., 1800, DamageFox, Att_Warrior, 1, "warr_re_cv", "none", 7, 0, RangeNormal, "none", 0, "none")`; type
40 (Ash) uses `warr_ty_cv`. Confirmed (code) for the layout; the arguments inferred from the disassembly.

`Human_Init` remaps the type: 32 becomes behaviour class 30 (`0x1e`) with a variant flag (confirmed (code) at
`0x00218008`, runtime `+0xcc` = `0x1e`, `+0xd0` = `0x20`).

### Hats {#hats}

`CfgHat(set, type, hat, {x, y, z}, {i, j, k, r})` (`Cfg_AddHatFit`, `0x00228d70`) fills a **hat-fit set**: a record of
0xa90 bytes at `0x00662b70 + set × 0xa90` with 48 slots of a 32-byte transform (the offset with w = 1, then the
rotation) from `+0x00`, the 48 hat names (24 bytes each) from `+0x600`, and the owning character type at `+0xa80`; a
hat goes into the first slot still named `none`. Placing a worn hat (`0x003a3ba0`): for a human whose class has brain
kind 3 (`+0x11a`, the Warriors), the set owned by the type it was created as (`+0xd0`) if there is one, else by its
class (`+0xcc`), and in it the slot named as the hat's object type (`0x00228f40` for the offset, `0x00229028` for the
rotation); a hat in no slot of the set gets a default transform (`0x005116c0`). Every other human uses the hat
model's own attach point (`0x00391828`, `0x00391880`). Confirmed (code). The 908 fittings:
[Hat fittings](../references/hats.md).

### From a type to a model {#type-to-model}

How `HuCreate`'s type picks what is drawn and animated, confirmed (code) at `0x00218008` (`Human_Init`),
`0x00228af8` (`Cfg_SetCharacterClass`, `CfgChar`'s writer) and `0x001780e8`:

1. **`CfgChar` resolves the model name once**, when `config_preload2.lua` runs: its model-name argument (`warr_re_cv`
   for type 32) is hashed (CRC-32 of the lower-case name, [Name hashing](name-hash.md)) and looked up in the
   **Character List** (the resource manager's `+0x88`, the chunk `0x44` records of [Files](#files)); the record's
   **index** is stored at `+0x112` (`0xffff` when the name is not in the list). The same is done for `"<name>_a"`
   into `+0x114`.
2. **`Human_Init` maps the type to a class** with a fixed switch. Types not in it are their own class. A type marked
   *variant* below sets a flag; the others are plain aliases of their class:

    | Class | Plain aliases | Variants |
    | --- | --- | --- |
    | 1 | 2 | 3, 4 |
    | 5 | 6 | 7, 8, 9, 10 |
    | 11 (`0xb`) | 12 | 13, 14 |
    | 15 (`0xf`) | 16 | |
    | 18 (`0x12`) | 19 | 20 |
    | 21 (`0x15`) | 22 | 25 |
    | 26 (`0x1a`) | 27 | 28, 29 |
    | 30 (`0x1e`) | 31 | 32 |
    | 33 (`0x21`) | 34 | 35, 36, 37 |
    | 38 (`0x26`) | | 39, 40 |
    | 41 (`0x29`) | | 42 |
    | 43 (`0x2b`) | | 44 |
    | 45 (`0x2d`) | | 46, 47, 48 |
    | 221 (`0xdd`) | | 222 |

3. **The model index** (kept at `+0x37c`): the **type's own** record when the human is not a player (player index
   below 1) or the type is a variant; otherwise (a player created as a plain alias) the **class's** record. Of that
   record it reads `+0x114` (the `_a` model) when the current level's number is 60-64 (the Armies of the Night bonus
   levels), else `+0x112`. So a player made as type 2 (`warr_cl_gen`) is drawn as type 1's `warr_cl`, while type 32
   keeps `warr_re_cv`.
4. **The rest of `Human_Init` reads the class record**: `+0xcc` holds the class from here on, and the fields read
   after the remap (`+0x118`, `+0x11a`, `+0x14b`, `+0xb4`, the strings at `+0x14c` and `+0x18c`) come from it; only
   `+0x11b` and `+0x11d` are first read from the type's own record. `+0xd0` keeps the type as given. Whether the
   later systems (combat, health) read the class or the type is not traced here.
5. **The files**: the Character List record at the model index names the three resources (character data with the
   animations, model, texture dictionary, [Files](#files)); `Human_Init` makes the instance at once when they are
   resident (`0x001774d0`, `0x00177b80`, then `Human_AttachInstance` `0x00217a98`), else the resource manager loads
   them later ([Creation](#creation)). The anim set is that character data over the generic defaults (its 722 slots,
   [Files](#files)) and the [anim slots](#anim-slots), the same for every type until a movement style changes them.

For an implementer with Coney's recorded `CfgChar` calls and its `CharacterList`: find the type's call, apply the
alias rule for a player, take the model-name argument (or `<name>_a` in levels 60-64) and look it up by name. The
[character reference](../references/characters.md) lists every type's model name.

### Speed classes {#speed-classes}

`CfgSpeedClass(class, ...)` writes six floats at `0x006b6548 + class × 0x18`. The getters read entry `+0x00` (base),
`+0x08` (walk), `+0x0c` (jog), `+0x10` (run) and `+0x14` (sprint); `+0x04` has no getter found. Values from
`config_preload2.lua` (inferred from the disassembly):

| Class | +0x00 | +0x04 | walk | jog | run | sprint |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 3 | 0.8 | 1.6 | 4.1 | 6.5 | 8 |
| 1 | 3 | 0.8 | 1.6 | 4.1 | 7.5 | 9 |
| 2 | 3 | 0.8 | 1.6 | 1.6 | 1.6 | 9.75 |
| 3 | 3 | 0.8 | 1.6 | 4.1 | 7.5 | 7.5 |
| 4 | 3 | 0.964 | 0.964 | 0.965 | 2.723 | 2.723 |

**But these are not used in play.** When `0x005101e0` is set (it is 1 at runtime), each getter returns the human's
own value from its 0x180-byte record times `+0x3a4` instead (confirmed (code) at `0x00221710`, `0x002215d0`): `+0x164`
for the base, `+0x170` for walk, `+0x174`, `+0x178`, `+0x17c` for jog, run and sprint.

**The speeds come from the clips.** `0x00254078` (called when the character is attached, `Human_AttachInstance`
`0x00217a98`, and after a movement-style change, `0x00243848`, `0x00253688`) computes each one from the clip in an
[anim slot](#anim-slots) as the clip's horizontal root displacement over its playing time:
`sqrt(dx² + dy²) / (duration / rate)`, with the displacement from the clip's descriptor (`0x00101950`), the duration
from `0x00101a00` and the rate from `0x00104a38`. Confirmed (code). It writes, in the 0x180 record:

| Record field | Speed | From slot | Rembrandt (runtime) |
| --- | --- | --- | --- |
| `+0x164` | base (combat walk) | 14 | 3.429 (clip 380) |
| `+0x16c` | sneak walk | 3 | 1.585 (407) |
| `+0x170` | walk | 4 | 1.629 (408) |
| `+0x174` | jog | 5 | 4.857 (409) |
| `+0x178` | run | 6 | 7.801 (410) |
| `+0x17c` | sprint | 7 | 10.245 (411) |

Each runtime value equals the clip's displacement divided by its duration as read from the disc, confirmed (runtime).
The speed class table is therefore only a fallback.

**Gait from speed** (`0x0022aeb0`, the gait stored with the velocity): below 0.5 m/s gait 0; otherwise the gait 1-5
whose speed (`+0x16c`, `+0x170`, `+0x174`, `+0x178`, `+0x17c`) is nearest. Confirmed (code); a jump table at
`0x0055bec0`.

### Anim slots {#anim-slots}

The 0x180 record holds the human's **anim slots**: 35 anim ids at `+0x28`-`+0xb3`, copied from the default table at
`0x005105d8` (`0x00253608`) and changed one by one by `0x002535f0(human, slot, id)`. A player gets slot 14 = 380
(player combat walk). `0x00253688(human, style)` swaps in a movement style's ids (cases 1-20, a jump table) and then
recomputes the speeds. Record `+0x20` is the anim id playing (getter `0x002266b8`). Confirmed (code); Rembrandt's ids
were read at runtime and named from the clips on the disc (confirmed (runtime)).

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

### Files of a character {#files}

A character is three resources, each a file named by the decimal CRC of a hash (`"%u"`, `0x00551ed8`) and loaded by
the resource manager ([Chunk system](chunk-system.md)):

- the **model** (resource type 3, loader `0x001783d0`): chunk `0x47` with the RenderWare clump, then chunk `0x28`
  with the bones' bind offsets (34 × float4, 544 bytes);
- the **texture dictionary** (type 4, `0x0018e9e0`): one chunk `0x2a`;
- the **character data** (type 5, `0x0016e8f0`): the animations (a `0x00` keyframe chunk and a `0x02` descriptor
  for each, [Animation](formats/animation.md)), one **Anim Range List** (`0x45`) and one **Character Data** chunk
  (`0x08`).

The **Character List** (chunk `0x44` in `warriors.glr`, 543 records of 32 bytes) finds them by model name:

| Offset | Meaning |
| --- | --- |
| `+0x00` | CRC-32 of the model name, lower case, no path prefix ([Name hashing](name-hash.md)) |
| `+0x04` | hash naming the character data resource |
| `+0x08` | hash naming the model resource |
| `+0x0c` | hash naming the texture dictionary |
| `+0x10` | the character data's size |
| `+0x14` | the model's size |
| `+0x18` | a size close to the model's (`+0x14` plus `0x1df` in the cases checked; meaning not traced) |
| `+0x1c` | the texture dictionary's size |

Confirmed (code) for `+0x00`-`+0x0c` (the loaders' lookups); the sizes are inferred from matching the WAD entries.

**The clump**, the same for every skinned character on the disc: one atomic, 33 frames, an HAnim hierarchy (`0x11E`)
of 32 bones with key size 36 and flags 0, geometry in PS2 native format (`0x010200f3`, two texture coordinate sets)
with the extensions `0x50e`, `0x510` (native data) and `0x116` (Skin, PS2 platform 4). Plugins:
[Graphics](graphics.md). Corroboration (disc survey).

**Character Data** (`0x08`, 2,912 bytes for Rembrandt; handler `0x0016e258`, vtable `0x005389a0`): a placeholder vtable
word, then **722 slots** at `+0x08`, one per anim id, then 16 bytes. A slot of `0xffffffff` means "use the default",
taken from the table at resource manager `+0x70`; a slot value `n` picks the `n`-th animation loaded with this
resource, counted on the stack of objects the chunk system pops last-in first-out. `CharacterInstance_GetAnim`
(`0x00175080`) answers an id with this table. For Rembrandt, id 408 is walk and 413 walk-start; ids 11 and 12 are
combo attacks (inferred from the clip names). The handler keeps the Anim Range List at `+0xb50` / `+0xb54`. Confirmed
(code) for the slot rule; the id meanings inferred.

**Anim Range List** (`0x45`, 11,568 bytes): a count word, then 722 records of 16 bytes, one per anim id, of the form
`(s16, s16 = 1000, f32, s16, s16, s16, u16)`: offsets, reach, ranges and rate flags ([Animation](formats/animation.md#anim-range-list)).

**Example, Rembrandt (`warr_re_cv`)**: character data `0xe72f9fb5` (200,640 bytes, shared with `warr_re`: 36
animations, one `0x45`, one `0x08`), model `0xdb1cdf36`, textures `0x46af47d8`. Corroboration (disc).

**Disc survey (counts only):** 153 distinct skinned models, all with 33 frames, 32 HAnim bones, key size 36, a PS2
skin and a 544-byte bone offset chunk; 1,209 Character Data and Anim Range List chunks.

### Character geometry {#character-geometry}

What the model's bytes hold beyond the clump's sections, found by Coney's decoder and its disc test over the 128
models the Character List names (no code was read for this subsection, so it is corroboration (disc) unless marked).

- **The native geometry** is the world's PS2 layout ([Graphics](graphics.md)) with a fifth slot: `STCYCL 5,1` and
  five `UNPACK`s per batch, slot 0 positions (`V4_16`), slot 1 texture coordinates (`V4_16`, two sets), slot 2
  colours (`V4_8` unsigned, all zero in the characters checked), slot 3 normals (`V4_8`) and slot 4 the **bone
  weights** (`V4_32`, format `0x6c`). Vertex counts match the mesh plugin's in every model.
- **A weight** is a float whose low 10 bits are replaced by `(node index + 1) << 2`, or 0 for an unused slot; the
  weight is the float with those bits cleared. The same encoding as librw's PS2 skin reader.
- **One mesh, one material**, colour `0x969696ff`, and the material is **not textured** (no texture section): every
  character's texture dictionary holds exactly one texture (507 dictionaries), named after the character in 427 of
  them. How the game binds that texture to the material is open.
- **The skin's inverse bind matrices** are the inverses of the HAnim frames' world matrices (checked to float
  precision for every model).
- **Frames to pose bones**: frame 0 is the clump's root, frame 1 carries HAnim id 0 and the hierarchy; node index
  equals id on the disc, and the pose bone of HAnim id `n` is `n + 2` ([Animation](formats/animation.md)), so pose
  bones 0 and 1 have no frame. Inferred, from the next point.
- **Pose axes are the clump's turned +90° about x**, `(x, y, z) → (x, −z, y)`: the bone offset chunk's entries are
  the frames' translations turned so (bone `n + 2` against frame of id `n`), and of the 24 axis rotations this one
  gives the smallest mismatch between clip-posed joints and the bind skeleton (0.022 m for Rembrandt's walk, 0.054 m
  for the next best). In pose space z is up and a character faces +y (the walk's root velocity is +y). Inferred.
- **The parent table differs from the frames in one place**: pose bone 3's parent is 1 in `Skeleton_InitParents`
  (`0x00101120`), while its frame's parent is HAnim id 0 (pose bone 2). Inferred (Coney follows the table).
- The float at `+4` of the bone offset chunk (entry 0's `y`) is 0 in every model.

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

The values "in play" were read in the street save's RAM and match every measured turn (confirmed (runtime),
[Feel comparison](feel.md#details-behind-the-table)); the setters are confirmed (code) on
[CfgSetTurnRates](../references/bindings/config.md#cfgsetturnrates).

## Behaviour

### Creation {#creation}

`HuCreate(name, type, {x, y, z}, headingDegrees, str, playerIndex, gang, flag)` (`0x00358428` → `0x00233d60`),
confirmed (code):

1. Build the rotation about `z` from the heading (degrees × 0.0174533, half-angle quaternion with the axis at
   `0x00511740`).
2. Take a free slot (`0x00217f08`); on failure free it again (`0x00217ec8`) and return `NilHandle` (`0x006ebd30`).
3. `Human_Init` (`0x00218008`): remap the type to the behaviour class; take the model index from the class
   record (`+0x112`, or `+0x114` in levels 60-64); load or find the character; **snap to the ground**: cast a ray from
   the position plus (0, 0, 1) straight down for 2.5 m through `WorldManager_RayCast`
   ([Collision](collision.md)) and, on a hit, put the human on it 0.01 above (skipped in game modes `0xb` and `0x11`).
4. Name (`+0x80`), player index (`+0x1b0`); for player 1, `Human_MakePlayer` (`0x00229c40`).
5. Return the handle (`+0x2c` of the slot) and write the snapped position back into the Lua table.

The fifth argument (`"warr_sw"` in `level99.lua`) is not read by `0x00233d60`. **At runtime** Rembrandt was created at
`(-284.4, 120.4, 0.3)` and stood at `(-289.03, 120.29, 0.25)` once the intro scene ended (confirmed (runtime)).

**Loading the files.** When the character's resources are already resident (the level's dependency list or the
section's pack holds them), the instance is made at once (`0x00177b80`) and attached (`0x00217a98`). Otherwise the
resource manager's update (`0x001897a8`) loads them later and attaches them then; the dynamic animations a script asks
for (`SetDynamicAnimation`) go the same way into the slots at `+0x3c8`. Confirmed (code) for both paths.

### Where a level puts the player {#level-starts}

**No data file holds a player start.** The `.lev` file's 18 chunks ([Level loading](level-loading.md#the-level-file))
and the level record ([the record](level-loading.md#the-level-record)) carry none, and nothing in `InitLevel` places
a player (confirmed (code) for those readers). The **level script** creates player 1 with `HuCreate` while
`InitLevel` runs it, before the preload ([Level loading](level-loading.md#initlevel)); the values are in the
[Level starts](../references/level-starts.md) reference list, one entry per checkpoint (inferred from the
disassembly of the 29 level scripts that create a player; the list's values are read from the disc).

**The checkpoint** is `W_GameState + 0x33a` (16 bits), confirmed (code):

- the game state's constructor (`0x00418588`) sets it to **1**;
- `SetCheckPoint(n)` (`0x0037b760` → `0x0041abc0` → `0x0041ce98`) stores `n` and tells the inventory, the stats and
  the world objects (`0x0041e0b8`, `0x00422c60`, `0x00397e88`); scripts call it before `MenuLoadLevel` to enter a level
  at a checkpoint, and during a mission to mark progress, so a retry restarts there;
- `0x0041cef0(gameState, 1)` puts it back to 1 without the bookkeeping: on the paths that restart a mission or
  leave it (the failure menu `0x00155408`, `0x00155648`, `0x001557f8`, the mission-complete mode's `Update`
  `0x0015d160`, and `0x00160d38`, which also selects level index 0, the front end);
- `GetCheckPoint` (`0x0037b798` → `0x0041abe8`) reads it. `InitLevel` uses it as the section: the pack
  `<level>_<checkpoint>.pak` and the intro movie only at 1 ([Level loading](level-loading.md#the-level-record)).

So a level loaded without a `SetCheckPoint` starts at checkpoint 1, or at whatever the last one left (inferred).

**The pattern of a story level's script** (inferred from the disassembly; 28 story levels follow it):

1. The main chunk defines the helpers, the flags (`AddFlagsBoxesPaths`), the objects, then runs `Main`.
2. `Main` reads `GetCheckPoint()` into a global (`checkpoint`, `CHAPTER` or `mState`; `level84` turns 0 into 1) and
   indexes a list with it. The list holds either the **creator** functions themselves (`PlayerGang = {AddWarriors1,
   AddWarriors2, ...}`, sometimes a local) or rows `{creator, start function, chapter script}` (`tMission`, as in
   `level99`, [Scripts](scripting.md#level99)).
3. The creator makes the Warriors' gang (`GangCreate(0, "Warriors...", 0, 0)`) and its humans, **player 1** among
   them: `HuCreate(name, type, {x, y, z}, heading, model, 1, gang)`, the position and heading literal numbers. It
   returns the table of humans and the gang; `Main` then sets `player = Warriors.<name>` (the name differs by
   checkpoint: `level3` plays Rembrandt for checkpoints 1-2 and Snow for 3-5).
4. `preLoadFile(chapter script, start function)` loads the checkpoint's own script; when it arrives the game runs it and
   calls the start function by name (`0x00356d00`, confirmed (code)). A start function may move the player again: in
   five checkpoints it, or a function it calls, teleports the player (`Teleport` or `TeleportToFlag`, noted in the
   list).

`Teleport(object, {x, y, z}, heading)` (`0x00385bb8`) sets the transform; a heading of −1 keeps the rotation. Its
`TeleportToFlag(object, flag, heading)` sibling (`0x00385db0`) takes the flag's position and, with −1, the flag's
heading (`0x00416258`), and for a human also calls its vtable slot `+0x14c`. Confirmed (code). Neither snaps to the
ground; `HuCreate` does (above).

**Interiors are below the street.** Starts with `z` near −195 to −215 (`level5` checkpoint 2, `level11` 1 and 2,
`level20` 1 and 3, the hub ...) are rooms placed about 200 m under the city in the same world (inferred from the values
and the hub's clubhouse at `z` −194.3).

**The hub** (`level95`, [Scripts](scripting.md#the-hub)): `AddWarchief` creates the player at the flag
`fWchiefStart_1` (−188.6, 95, −194.3), heading 222, as the type the chapter script's `WarchiefTable` names (Cleon,
Rembrandt, Ajax, Cochise, Cowboy or Swan by chapter). `StartLevel` then either opens the quick map (when unlockable
`(6, 3)` is unlocked, or `LoadLight` is set) or makes the **door walk**: `TeleportToFlag(player, fWchiefStart_<n>)`
and a walk to `fWchiefEnd_<n>`, where `n` = `WCLoc`, `random(1, 5)` from the main chunk (1 to 5 inclusive, from the
game's table generator), or 5 while unlockable `(6, 4)` (the tutorial) is still locked, in any chapter. The five
start flags: 1 (−188.6, 95, −194.3) 89°; 2 (−188.6, 102.7, −197.5) 89°; 3 (−163.7, 80.7, −197.5) 358°; 4 (−174.4,
80.5, −194.3) 358°; 5 (−185.2, 112.7, −193.7) 182°. Inferred from the disassembly; how flags work, the generator
and the order of `StartLevel`'s steps are on [World flags](flags.md#player-starts).

**A Rumble arena** (`level101`-`level137`): the level script runs `doFile("level" .. Level .. "_" ..
RumbleInfo[Rumble.gameType] .. "_init")`, which adds the mode's flags, among them the list `fP1` (player 1's gang)
and `fP2`. `AddRumbleGang1` creates `P11` with `HuCreate("P11", Rumble.gang1[1], FlagPos(fP1[1]), 270, nil, 1,
gang, true)` and teleports it to `fP1[1]` with heading −1, so it stands on the first flag facing the flag's heading
(inferred from the disassembly). The gang, and so the type, comes from the Rumble menu (`GetRumbleModeData`); the
steps are on [World flags](flags.md#player-starts).

**No player**: `level100` (the front end), `level1` (its script only calls `MenuLoadLevel("menu")`), and the levels
without a `.lev` file.

### The characters' update {#update}

The task manager's play tick (`0x003a3148` → `0x003a2ea0`, called first in mode 1's frame,
[Level loading](level-loading.md#a-frame-of-play)) calls `Humans_Update` (`0x00249108`) on each 60 Hz tick, once
0x4b0000 ticks (about 16.7 ms) have passed, and runs two ticks when it fell behind. `Humans_Update` does its work on
every **second** call, so the characters step at 30 Hz with dt = 1/30. In order, confirmed (code), each step behind a
debug switch that is on in play (`0x005e5350`-`0x005e5368`); the full list is on [Tasks](tasks.md#humans-update):

1. Three animation managers (`0x00170c88`, `0x00171d38`, `0x00184568`); `Pads_Update`; the formations; the 60 player
   records (`0x00146078`, below).
2. The gangs, then the **brains** (`0x00293b28`, [AI](ai.md#update)), which write an AI human's command into its
   player record as a pad would.
3. For every human with an instance, `0x0023bd78` (the instance's animation step, `0x00175610`), then
   `0x00105570`.
4. Per human: a position 1.3 above the human handed to `0x0019c3f0` for humans with flag `0x4000` (an effect or
   sound, inferred), and checks against the player's gang.
5. Per human: vtable slot `+0x13c` (its state update: the locomotion) or a flag when it is idle.
6. The actions, alternating the order (0 → 59, then 59 → 0) on each update: `Human_UpdateActions` (`0x00254e78`),
   which runs the command dispatcher (`0x0027c120`) for player and AI humans alike; a dead or knocked-out human runs
   `0x00221108`, `0x00256f28` and `0x00265f70` instead. An earlier reading of this page called this step the
   brains.
7. The cameras' update (`0x0011e878`) with dt, at least 1/30.

### From pad to intent {#input}

`PlayerRecord_Update` (`0x00146078`), each update, for a record with a pad and control on: flip the buffer; copy the
buttons; take the pad record's **camera-turned** left stick (`+0x00`, `+0x04`, [Front end](frontend.md#pad-record)),
store its angle (`atan2`, `0x003357a8`) and its length clamped to 1. With `+0x1f` set the angle is π/2 and the length
0. Confirmed (code). The stick is thus already relative to the camera: pushing up moves away from it, which is what
the game does at runtime (confirmed (runtime)).

### Locomotion {#locomotion}

`Human_PlayerLocomotion` (`0x00240e38`) for a pad-controlled human; others run their state's function instead.
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
   state choice (`0x00259578`) picks state 0 for code 9. Confirmed (code); the code 9 and its caller confirmed
   (runtime).
4. **Turn.** Target heading = stick angle − π/2; current heading = the facing from the rotation. The error is
   wrapped to (−π, π]. The limit per update comes from `Human_MaxTurn` (`0x002213d8`) through the gait lookup
   `0x002212d0`, which reads the table's player word for a pad-controlled human: in play **20° walking or standing,
   18° jogging and running, 16° sprinting** (600, 540 and 480 degrees per second); 4° and 6° in two special states
   (`0x00227f68`, `0x00227f40`), 11° in two others, 24° in a combat stance ([Movement
   constants](#movement-constants)). With smoothing on, the step is the limit times an ease
   `(1 − cos(π · error / E)) / 2` with **E = 2.0 rad** in play (full from 2.0 rad), plus 0.8 times the previous step
   (−0.5 times it when the error changed sign), clamped to [0, limit]. When the error is smaller than the step, the
   heading snaps to the target. The new heading becomes the rotation (unless `+0x3bb` locks it) and the velocity's
   direction. Confirmed (code) and confirmed (runtime): a 90° turn at a run went 16.0°, then 18° per update (the
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
not pick a clip: one gait blend covers walk to sprint ([Clip selection](#clip-selection)), and what happens at runtime
is below.

**Gaits at runtime** (PCSX2 2.9.94, `level99` checkpoint 1, Rembrandt standing; the left stick held straight up at
the magnitude given, by the stick-table method in [Driving PCSX2](../guides/research-workflow.md#driving-pcsx2);
speed is `+0x1ac` sampled every 5 ms, the clip is record `+0x20`). Confirmed (runtime):

| Stick magnitude | What happens |
| --- | --- |
| 0.10 | nothing (inside the 0.12 dead zone) |
| 0.13, 0.5, 0.94 | **walk start** (413) at a steady 0.76 m/s for about 0.45 s, then **walk** (408) at 1.63 m/s, gait 2: the walk speed does not depend on how far the stick is pushed |
| 0.96, 1.0 | **run start** (414) for about 0.36 s, the speed following the clip (2.77, 2.49, 2.56, 2.63, 2.84, 3.35, 3.90, 4.54, 5.11, 5.40, 5.61, 5.69 m/s, one value per update), then **run** (410), gaining 0.8 m/s per update up to 7.80 |
| released from a walk | speed 0 at once and **idle** (388) |
| released from a sprint (10.245 m/s) | **run stop** (417) for 24 updates (0.8 s), moved by the clip 1.63 m with the facing held, then the idle ([Sprint](#sprint)) |
| released from a run (7.80 m/s) | the run stop only when the speed that update is not below the run speed by a rounding error, about one release in ten; otherwise the idle (388) at once, at 0 m/s ([The run stop](#run-stop)) |
| reversed (180°) at a run | one turn step (18°), then the same skid and run stop, sliding 1.63 m the old way; then the walk or run start toward the stick |

So the start clips drive the speed while they play (their root motion, [Animation](formats/animation.md#root-motion)),
and the gait clip's speed is the target afterwards. The walk start's "about 0.45 s" is its 0.333 s played at rate 0.75
(0.444 s). The run start's shorter 0.36 s is not explained; it may begin part-way through, as the walk-start-to-run-start
swap does (speculative). A jog was not reached from the stick alone (it needs a carried object, see the locomotion steps
above).

**Per update** (the feel pass, slot 1, read every update; confirmed (runtime)): on the update the stick is first
seen the start clip begins and the body does not move; the walk start then moves it at a steady **0.762 m/s** and the
run start at 2.77, 2.49, 2.56, 2.63, 2.84, 3.35, 3.90, 4.54, 5.11, 5.40, 5.61, 5.69 m/s; both last **13 updates**,
and the gait blend then gains 0.8 m/s per update. The walk start's clip speed from the disc is 0.786 m/s: the body
moves at **the clip's speed × the body scale** (0.97), as does the landing clip 436 (4.230 m/s against the clip's
4.361) and, inferred, every clip that moves the body. The gait speeds (walk, run, sprint) are not scaled.

The first updates in detail (the second feel pass, 2026-10-05, slot 1; speed `+0x1ac` and the position's change
agree to 0.003 m/s; the stored gait `+0x1a8` in brackets). Confirmed (runtime):

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
Confirmed (runtime), scenario `run_stop` (slot 1, a straight run released, then a run circling behind the camera
released, as `run_circle`), with hooks on the state-code setter and on the skid test logging the gait, both stick
buffers, the run speed and the speed register `f28` as raw words:

- The run speed is `0x40f9a3ad` (7.8012300). The speed the locomotion measures at a steady run (the length of the
  velocity, `0x0023fea8`'s vtable `+0x94`) jitters between `0x40f9a3aa` and `0x40f9a3ae`: of 93 updates at the run
  in one recording, 10 were at or above the run speed and 83 one to three units below it.
- On every release seen the gait was 4, the previous stick 1.0 and the stick 0. Where the speed on the release update
  was below (`0x40f9a3ab`, `0x40f9a3ac`, straight and circling alike, three recordings) no code 9 was set and the
  idle 388 began on the next update at 0 m/s, holding `0x10000000` for 5 updates; where it was not (one circling
  release), the state code went to 9 from `0x00241538`, the idle builder set it back to 0 (`0x0025fa2c`) and the
  run stop 417 played for 24 updates with `0x80000`, at the speeds in the table above.
- So **turning has nothing to do with it**: the circling run of `run_circle` (step 192) and a straight run both go
  either way. After a sprint (10.245 m/s) the speed is far above the run speed, so a sprint always ends in the run
  stop; a jog or walk (gait 3 or less) never does.

In the idle that follows a release without the run stop, the stick's last angle and magnitude (`+0x5d8`, `+0x5dc`)
keep turning the body: while the idle's fade holds `0x10000000`, the locomotion turns toward the last stick angle with
a magnitude × 0.8 each update (`0x002411cc` onward), which is the 0.7°, 1.1°, 1.4°, 1.5° seen after `run_circle`'s
release.

### Buttons {#buttons}

The pad's buttons become **command ids** through tables of 12-byte entries `{u16 mask, u32 command, u16 buttons,
u16 extra}` that `AddCommand` fills. Each update `0x00147940` matches them and stores the command in the per-player
record's `+0x20`, or in the pending `+0x24` when the entry's mask is `0xfe` (`0xff` enables it for pad 0). The buttons
are the pad word's bits ([Pad record](frontend.md#pad-record)). The nine tables, what each trigger means, the
order in which they are matched and every command they make are on [Combat, Commands](combat.md#commands)
(confirmed (code) and (runtime)). An earlier reading here took square (`0x80`) for d-pad left and cross (`0x40`) for
down: the combinations are L2 + square (`0x21`), L2 + cross (`0x20`), cross + square (`0x22`) and circle + triangle
(`0x24`), and the d-pad combinations `0x29`-`0x2c` and L3's 9 need select held as well.

Traversal uses two buttons: **triangle pressed** (command 10) starts a climb, a context action or a jump
([Jumping](#jump), [Climbing](#climb)), and **L2** sprints. The sprint reads the button itself
(`0x00147f98(record, 1)`, "is the button of mask 1 held") rather than command 5. Confirmed (code) at `0x0027c120` and
`0x0027ce90`.

### Sprint and stamina {#sprint}

**Sprint is held, not toggled.** Every update `Player_UpdateSprint` (`0x0027ce90`, called from `0x0027d5a0`, which
`Player_UpdateActions` calls) first **clears** state flag `0x1000000`, then sets it again (and calls `0x00230140`,
and clears state flags `0x8008`) only when all of these hold. Confirmed (code):

1. L2 is held;
2. stamina (record `+0x14a`) is not 0;
3. record `+0x08` bit `0x10` is clear.

So the sprint lasts exactly as long as L2 is held and stamina lasts: letting go of L2 ends it on the next update, and
nothing latches it. It also ends when stamina reaches 0 (below) and when `Player_UpdateActions` takes its block branch
(`0x0027c6b0`): **R1 held** in a fight, or L1 released or L1 + R1 while already blocking
([Combat, Dispatcher](combat.md#dispatch)), which clears the flag and sets state flags `0x8001`. **At runtime**
(`level99` and a street, stick 1.0 straight up and L2 held
through patched pad input, [Driving PCSX2](../guides/research-workflow.md#driving-pcsx2)) the flag was set every
update, the command was 5, and the speed rose by 0.8 m/s per update from 7.80 to **10.245 m/s** (gait 5).
Confirmed (runtime).

**The stick still decides.** The flag only asks for a sprint: the locomotion's sprint test (`0x00225dc0`) also needs
the stick above **0.95** and stamina, as for a run ([Locomotion](#locomotion)). With `0x00510258` set
(`CfgPlayerRunButton`), L2's pressure decides instead (pressure byte 10 above 100 sprints, otherwise a run); it is 0 in
play. Confirmed (code).

**Stamina** is the s16 at record `+0x14a`. Its maximum is `Human_StaminaMax` ([Power classes](#power-classes)): 135 for
Rembrandt (confirmed (runtime)). Confirmed (code) unless marked:

- **Drain** (`0x002562d0`, called from the brain `0x00254e78`): while the gait stored with the velocity (`+0x1a8`) is 5
  and record `+0x08` is 0 (`0x00223a98`), stamina loses **20 per second** of game time (`0x005101f4`, `CfgBurnRates`
  rate 5; 0 when `0x005102a8` is set, which it is not in play), with the fraction carried at `+0x158` and the time at
  `+0x154`. When it reaches 0 or below it is set to 0 and the sprint flag is cleared. Gait 5 is the speed nearest the
  sprint speed, above 9.02 m/s for Rembrandt, so the drain starts on the third update of the speed-up.
- **Refill** (`0x00256a60`, every update): stamina gains the power class's `+0x2e`, **40 per second** for Rembrandt,
  up to the maximum. It gains nothing (and the time is not carried over) while any of these holds: gait 5
  (`0x00223a98`); state flags `0x1c18003ff0` (the airborne bits among them); a jump or fall (`0x00227f90`, flags
  `0x1c00000000`); **gait 4 (run) with L2 held** (`0x00223a60` and the button); record `+0x08` flags `0x41a420`. Human
  flag `0x4000000` fills stamina and the second meter every update. In three states (`0x00223b98`, `0x00223bc0`,
  `0x00223b70`) the function returns before taking the time, so the time spent in them is refilled at once afterwards.
- **No delay and no threshold**: the refill starts on the first update the blocks are gone, and the sprint test is
  only "not 0", so one point of stamina sprints again.

**At runtime** (the street, Rembrandt, stamina 135 of 135; stick and L2 through patched pad
input; stamina, speed and flags read every update). Confirmed (runtime):

- From a standstill with stick 1.0 and L2 held, stamina **still refilled** through the run start (gait 2-3), stood
  still at gait 4 (the "run with L2" block), and fell by 1 every 1.5 updates (**20 per second**) from the update the
  speed reached 9.686 m/s (gait 5, the third update of the speed-up from 6.486).
- When it reached 0 the sprint flag cleared and the speed dropped from 10.245 to **7.801 m/s in one update**; with
  L2 still held for 1.5 s more, stamina **stayed at 0**.
- With L2 let go and the stick still at 1.0 (a run), stamina refilled at 40 per second (0 to 39 in about 1 s); L2
  pressed again sprinted at once.
- Standing, it refilled by 4 every 3 updates (**40 per second**) to 135.
- Letting go of the stick in a sprint played the **run stop** (417) for about 0.8 s with record `+0x08` = `0x80000`,
  the body moved by the clip (about 1.6 m) while stamina refilled, then the idle: the skid of
  [Locomotion](#locomotion) step 3.
- Stamina also drains **in a jump** from a run (135 to 132): the gait is taken from the whole velocity, and the jump's
  vertical speed puts it above 9.02 m/s ([Jumping](#jump)).

**What a player sees**, from the code and the runs above:

| Stick | L2 | Result |
| --- | --- | --- |
| 0.5 or 0.8 | held | walk at 1.63 m/s; the flag is set but there is no sprint and no drain; stamina refills (inferred) |
| 1.0, or a full diagonal | held | run, then sprint at 10.245 m/s after 4 updates; stamina drains from the third, 135 lasts **6.75 s** |
| 1.0 | released | run at 7.80 m/s; stamina refills at 40 per second, **3.4 s** from empty to full |
| 1.0 | held at 0 stamina | run at 7.80 m/s, and stamina **stays at 0** until L2 is let go |

In an input script: `stick left 0 100` and `press l2` to sprint, `release l2` to refill.

**Measured** (PCSX2 2.9.94, the stick held fully forward from a standstill by the W key, magnitude 1.0, positions
read over PINE): 4.10 m after a
0.75 s hold, 10.08 m after 1.5 s: 5.98 m in the second 0.75 s, **8.0 m/s**, close to Rembrandt's run speed of
7.80 m/s (confirmed (runtime); the hold times are those of the key presses, so about ±1 frame).

### Clip selection {#clip-selection}

Each update the human's animation controller `Human_ChooseAnimState` (`0x00259578`, called from the characters'
update at `0x00255510` and `0x00256c34`) picks an **anim state** and, when it differs from the one in record `+0x18`
(or human flag `0x20000000` forces it), calls that state's builder from the table at `0x005106e0` (one function per
state; most entries are an empty stub `0x0025f4c8`). The builders make [animation tasks](formats/animation.md#animation-tasks)
and change animation by "fade over *d*, new task at the bottom" ([Task stack](formats/animation.md#task-stack)).
Confirmed (code) at the cited addresses unless marked.

**Choosing the state** (the plain locomotion case; grabs, combat, carried objects and scripted moves test their own
flags first and are not described here):

| State | When | Builder |
| --- | --- | --- |
| 0, idle | speed below a quarter of the speed `0x00221580` returns | `0x0025f770` |
| 4, move | speed at or above that, or record `+0x14` is 5, 6 or 8 | `0x0025f4d0` |
| 11 | a combat stance (`0x00228340`) at speed 0.01 or less | `0x0025fa48` |
| 14 | a combat stance while moving | `0x0025f4d0` |
| 21, 24 | human flag `0x8000`: 24 when the pending turn `+0x5e4` exceeds `Human_MaxTurn`, else 21 (turns in place, inferred) | `0x0025fc30`, `0x0025fc70` |

**Move (state 4).** `0x0025f4d0` calls `Human_BuildMoveTasks` (`0x0025b200`) with a fade time of 1/15 s (1/6 s when
record `+0x14` is 18 or human flag `0x40000000` is set), which builds one of:

- **From standing** (the top task is not a gait blend, task flag `0x80` clear): a [gait blend](formats/animation.md#gait-blend)
  of slots 4, 5, 6, 7, 7 (walk, jog, run, sprint, sprint) at value 0, flags `0x2c1`; then a clip-then-next task
  (type 3) that plays **slot 10, the walk start** (413), with a blend time of 0, and hands over to the gait blend.
  The fade before it has a duration of **0**: the start clip replaces the idle at once. Human flag `0x10000000` is set
  while the start clip plays (it is what stops the locomotion setting a velocity, [Root motion](formats/animation.md#root-motion)).
- **Run start** (`0x0025b9f8`, when the stick asks for a run and record `+0x14` is 6, 7 or 8, or a carried object of
  class 4 or 6 and state flag `0x1000000`): the same, but the gait blend starts at 2 (run) and the start clip is
  **slot 10's id + 1** (414, run start).
- **Already moving** (the top task is a gait blend): a fade of 0.1333 s and a new gait blend that starts at the old
  one's value (`0x0025ff50` rounds it down to 0, 1, 2 or 3) and the old one's normalised time, so the cycle carries on.
- Other cases: a looping single clip of slot 14 (combat walk) when the global `0x0051031c` is set; a four-clip task of
  slot 0 and slot 4 under `0x00227d98`; a two-clip mix of ids 633 and 636 when carrying (`0x00228188`).

**Walk start to run start.** While the walk start plays (flag `0x10000000`), if the stick asks for a run or sprint
(`0x00225c10`, `0x00225dc0`) above the magnitude at `0x005102e8`, and less than half of the clip has played, the
controller swaps it for the run start: a fade of min(0.1333 s, the clip's duration), a gait blend at 2, and the run
start begun at the walk start's normalised time (`0x00259578`).

**Gait value from the speed.** In states 4 and 14, every update, `0x0025eee0` sets the top gait blend's target to
`Gait_BlendForSpeed` (`0x0025ec28`), a piecewise-linear map through the [speed getters](#speed-classes):

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
357, 394, 634). With the state code 9 (the [skid](#locomotion)) it builds the **run stop** instead (`0x0025c0e8`, slot
33, 417); otherwise releasing the stick fades straight to the idle, which matches the runtime samples below. This
corrects the earlier reading that slot 33 is not used.

**At runtime** (PCSX2 2.9.94, `level99` checkpoint 1, Rembrandt; the task stack read from the instance every 50 ms).
Confirmed (runtime):

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
  for that human (inferred from the instance used).

### Moving, standing on the ground and falling {#ground}

The locomotion above only sets a velocity. Moving the human, keeping it on the ground and making it fall happen in
the human's state update (vtable slot `+0x13c`, `0x0023fea8`) and its move step (`0x0023d8c8`), with the physics
world (`0x00597198`, `Physics/`) doing the sweep against walls. Confirmed (code) unless marked.

**Where the transform lives.** The authoritative position and rotation of every task-manager object are in a table at
`0x00714b00`, 32 bytes per object (`position` vec4, then `rotation` quat), indexed by the object's handle index
(`+0x92`). `SetPosition` (`0x0021b0b8`) writes the table; the human copies it into `+0x10` / `+0x20` at the start of
each update (vtable `+0xac`, `0x004ed828`). The velocity is at `+0x30` (vec4), read through `0x003a1f00` (zero while
flag `0x600` is set in `+0x54`); writing it (vtable `+0x74`, `0x0023cf00`) also stores its length at `+0x1ac` and the
gait for that speed (`0x0022aeb0`) at `+0x1a8`. So `+0x1ac` (used by the lean) is the current speed.

**Airborne flag.** Bit `0x4000000` of the object flags at `+0x54` means "in the air". `0x003a2158` sets it (and clears
`0x2000000`, "on the ground"); `0x003a21c0` does the reverse.

**One update, in order** (the parts that move the body; the human's other duties are left out):

1. Copy the transform from the table; run the current state's function (for the player, the locomotion,
   [above](#locomotion)), which sets the velocity.
2. **Speed sanity check:** a velocity longer than 50 × the human's scale (`+0x65c`, 1.0 normally; `0x0021d020`) is
   zeroed.
3. **Gravity**, only while airborne: from the second airborne update on (the counter `+0x384` counts airborne
   updates), `vz -= 15.68 × dt` (0.5227 m/s per 1/30 s update; 15.68 m/s² is 1.6 g), clamped at **−50 m/s**. The
   result is written back with the velocity. On the ground `+0x384` is reset to 0.
4. **Fell out of the world:** if the position is more than **20 m below the collision mesh's lowest vertex** (mesh
   header `+0x98`, [Collision](collision.md#header)), the human is flagged dead (`0x200000000`), its velocity zeroed,
   and for a player in a mission whose game-state flags `+0x150` have bit 1, `W_GameState + 0x14c` is set to 1 and
   `+0x152` to 2 (a mission failure, [Level loading](level-loading.md#a-frame-of-play)).
5. **Stuck in the air:** after 60 airborne updates, if the physics body's "could not move" counter (body `+0x70`) is
   also above 59, the human is put back at its last ground position (`+0x390`) and landed (`0x0023e090`).
6. **Move** (`0x0023d8c8`, below).

**The move.** With velocity `v` (and an external push `+0x2f0` added, which knock-backs use):

- A human without a physics body (`+0x1a0` = 0), riding something (`+0x280` ≠ −1) or in state `0x40` simply moves:
  `position += (v + push) × dt`.
- Otherwise, on the ground, **`v.z` is set to 0** first: walking never climbs by velocity, only by the ground snap
  below. On a slope (ground normal `z`, `+0x238`, below 0.95) and outside some states (`0x00227f68`), the velocity
  is scaled by `clamp(0.6 + 0.3 × (n.z − 0.5), 0.5, 1.0)`: 0.735 just below the threshold, 0.71 on a 30° slope
  (`n.z` = 0.87), 0.5 from `n.z` ≤ 0.17. (The code also computes the facing just before, but the scale
  does not depend on it, so it slows uphill and downhill alike; and there is a step from 1.0 to 0.735 at
  `n.z` = 0.95.)
- The physics world then **sweeps the body** (`0x0033e278(dt, world, body, &v, &out)`): the body's pending push-out
  (body `+0x50`, divided by dt) is added to the displacement `v × dt`, which is swept up to three times, sliding
  along what it hits. If it is still blocked after three passes, the body stays where it is, its horizontal velocity is
  zeroed and its "could not move" counter (`+0x70`) goes up; otherwise the counter is reset. Walls come from the
  level's [collision mesh](collision.md) through the body's shape: a **sphere swept along the move**, below
  ([Walls and steps](#walls)).
- **Ground snap**, on the ground only (and not in game modes 8, `0xb` or `0x11`, `0x00221950`), `0x0023eab8`: cast
  a ray **straight down from 1.0 m above the feet, 1.5 m long** (the 1.0 is vtable `+0x5c`, `0x004ed818`, a constant).
    - **Hit:** put the feet **exactly on the hit point** (no gap), remember it as the last ground position (`+0x390`
      before the snap, `+0x240` after), store the ground normal at `+0x230` (its `z` at `+0x238`, used by the slope
      rule) and the material at `+0x1d8`. The triangle's flag bits 4 and 5 are passed on (bit 4 to a per-player
      "under cover" state, `0x0028ef00`; bit 5 to `0x002195e0`; meanings inferred from what the callees touch).
    - **Miss** (nothing within 0.5 m below the feet): the human **starts to fall** through vtable `+0x154`
      (`0x0023dc58`) unless it is in state `0x800`.
  So a step or kerb up to **1.0 m** high is climbed in one update when the sweep lets the body over it, and a drop of
  up to **0.5 m** is followed without falling; anything deeper is a fall. The sweep lets the body over a step only
  when the step's wall triangles are under 0.25 m tall ([Walls and steps](#walls)).

#### Walls and steps {#walls}

The physics body of a human holds a capsule shape (type 3, shape `+0x30`; radius 0.35
at shape `+0x40`, 1.886 at `+0x44`; read at runtime), and the physics world handles a shape through a table of
functions by type (world `+0xe0`, `0x0033d2d8`, which passes −0.65). For type 3 that is
`PhysicsMesh_SweepCapsule` (`0x00347c08`), which treats the body as **one sphere** for walls. Confirmed (code):

1. **The sphere**: radius `r` = shape `+0x40` × body `+0x60` × the human's scale (`0x0021d020`: `+0x65c`),
   centre **`r + 0.05` above the feet**. For the player, body `+0x60` (and `+0x64`) is **1.4286** (0.5 / 0.35)
   and the shape's `+0x40` reads 0.3395 (0.35 × 0.97), so `r` = 0.3395 × 1.4286 = **0.485 m** (0.470 m if the scale
   were applied a second time; the stops below fit 0.485) and the sphere spans 0.05 to 1.02 m above the feet.
   Confirmed (runtime): the fields read in the street save, and the player stopped 0.480 m from three faces (a wall
   head-on, a wall at an angle and the fence of slot 7). Who sets `+0x60`, and its value for other humans (1.0
   would give them 0.34 m), are not traced.
2. For each enabled triangle of the grid cells it covers that is a wall (`|n.z| ≤ 0.65`) and that the sphere is in
   front of (a two-sided triangle is turned to face it), and that the move goes into (`n · move < −0.001`):
3. **Skip low and thin triangles**: take the edge whose unit vector is steepest; if its two ends differ in height by
   less than **0.25 m**, skip the triangle. Then take the longest edge; unless it is nearly vertical (`|unit z|` ≥
   0.8), skip the triangle when the third corner lies less than 0.25 m from that edge's line within the triangle's
   plane.
4. Sweep the sphere along the move against the triangle (`0x0034ee60`); a hit at a fraction from 0 to 1 is a
   contact. The contacts go to the body's contact handler (`Human_OnContact`, `0x00219d50`), which for the level's
   triangles slides (code `0x20001`), lands on a floor contact (flag `0x80`), and during a climb (record `+0x08`
   `0x40`) **ignores** triangles of materials 30 (`LOW_FENCE`), 31 (`OPAQUE_FENCE`) and 122 (`RAILING`), so the body
   passes through the fence it climbs.

So on the ground the original has **no step height of its own**: a wall face shorter than 0.25 m is not a wall, and
the ground snap (1.0 m up, above) then lifts the feet onto it; any face 0.25 m or taller that reaches into the sphere
(0.05-1.02 m above the feet for the player) stops the body. A kerb of 0.2 m is walked onto; a ledge of 0.5 m or
0.75 m is a wall to walk into and needs a climb (a short wall from 0.7 m, [Climbing](#climb)) or a jump.

**The step at runtime** (confirmed (runtime), PCSX2 2.9.94, a copy of slot 1 in `level99`'s world, 2026-10-05). The
street there has no kerb (no wall face under 0.35 m rises from the street level), so a box of the level's collision
mesh (material 41, 2.62 × 0.62 m, against a wall; its eight vertices are used by its own ten triangles only) was
made into a step by writing its vertices' heights in RAM: bottom at the floor (0.213), top 0.10 to 0.30 m higher.
The player was put 1.26 m in front of it, facing it, and walked in with the stick at 35 %, 50 % or 100 %, turned by
the camera so that the move pointed straight at the face:

| Step height | Stick | Result |
| --- | --- | --- |
| 0.100, 0.200, 0.240 m | 35 %, 50 % (walk 1.63 m/s) | walked on: the feet rose by the step's height **in one update**, on the update the body's centre passed over the edge, with no change of speed or clip |
| 0.245 m | 100 % (run start, 5.6 m/s) | walked on, the same way |
| 0.255 m | 100 % | stopped 0.395 m short of the face, idle with the stick held |
| 0.260, 0.300 m | 50 % | stopped 0.399 m and 0.423 m short of the face |

So the 0.25 m test is exact, and the stopping distances are those of the 0.485 m sphere centred 0.535 m above the feet
touching the face's top edge: `sqrt(0.485² − (0.535 − h)²)` gives 0.396, 0.400 and 0.424 m for `h` = 0.255, 0.26
and 0.30. Against a full-height wall the same walk stopped 0.476 m from it.

**Sliding along a wall** (confirmed (runtime), slot 1): the velocity the sweep leaves after sliding is the human's
velocity, so the next update's current speed is its length and the 0.8 m/s gain starts from there. Running into a
wall 20° off its normal, the speed fell from 7.80 to 4.00, 1.55, 0.86, 0.65 and 0.56 m/s (every second update) and
settled near
`0.8 k / (1 − k)` (`k` = sin 20°: 0.42 m/s measured, 0.416 predicted), until it fell under the idle threshold
(a quarter of the sneak-walk speed, 0.396 m/s) and the idle played with the stick still pushed. A walk does the
same. So the player brakes against a wall met at a steep angle instead of sliding along it at speed.

The push-out `PhysicsBody_PushOutOfWalls` (`0x003477c0`, a sphere out of the nearest wall by `n × (r − distance)`)
is not the walking case: its only caller `Human_PushOutInAir` (`0x0021a490`) runs while the human is airborne
(object flag `0x4000000`), in a few states (`0x00227ef8`, `0x00223b48`, record `+0x08` `0x400000`, anim id 2 or 4),
and not in states `0x00223920` / `0x00223980`. Its sphere has radius 0.35, or **0.5 for a player**, times the scale,
centred `r + 0.05` above the feet (at bone 2 in states `0x00227ef8` / `0x00223b48`). In a long fall (state flag
`0x1000000000`) and some states it instead pushes out a 0.2 m capsule between bones 6 and 3 (`0x0033e7f0`, the
average of the contacts). Confirmed (code).

#### Falling and landing {#falling}

**Starting to fall** (`0x0023dc58`): march a ray along the velocity (`Collision_MarchRay`, steps of 0.1 m, up to 5 m)
to find where the fall will end (kept at `+0x2c0`; the current position if nothing is hit), set the airborne flag
(`0x003a2158`) and switch the body to its falling mode (body slot `+0x2c` with 2). For a player whose follow camera is
in its normal mode, a ray 2.5 m down from 0.75 m ahead of the feet checks for a drop and, if there is no ground,
tells the camera (`0x0012da20` with the heading, −1.0): the camera reacts to a ledge.

**Landing.** While airborne, the sweep tests the segment from the body's upper point (feet + `+0x4e8` − 0.16 × scale)
to the moved feet against the collision mesh (`0x0023e408`, through `WorldManager_RayCast`); a hit on a triangle with
`n.z` > **0.65** is a landing contact. The contact handler (`0x00219d50`) then calls `Land(human, 1, 1)`
(`0x0023e090`): clear the airborne flag, apply **fall damage** by the vertical speed at impact
(`vz` ≥ −14.9 m/s: none; between −14.9 and −20.5: a share of the maximum health, `(vz + 14.9) / −5.6`, at least the
class's minimum `+0x118`; below −20.5 m/s: the maximum health, so a fall that fast kills), tell a player's camera
when `vz` < −1.5 (camera slot `+0x15c` with the landing kind 1-3), set `+0x3a0` (the vertical velocity) to 0 and
write the velocity back. The thresholds are `0x00510674` (−14.9) and `0x00510678` (−20.5). Free fall from rest
reaches 14.9 m/s after about 7.1 m and 20.5 m/s after 13.4 m (inferred from the gravity above).

**When the landing happens** (the feel pass's second runtime run, 2026-10-05, PCSX2 2.9.94, slot 1, a run jump with
stick 100 % and triangle tapped 35 updates in; the arc's phase shifted by adding 0.015 to 0.15 m, or taking 0.05 or
0.12 m, to the feet's height at the 7th update in the air; ground at 0.2231). Confirmed (runtime):

| Feet at the start of an airborne update, below the ground | That update |
| --- | --- |
| above it, or 0.041, 0.091, 0.111, 0.141 or 0.161 m below | still airborne: the body falls a whole step (0.20-0.22 m) more |
| 0.176 or 0.191 m below (and 0.241-0.379 m) | lands: `Land`, the feet snapped up onto the ground, full horizontal speed |

So the feet are **not** stopped at the ground on the update they pass it: the jump lands on the first update that
**starts** with the feet about 0.17 m (between 0.161 and 0.176 m) or more below the ground, and until then the body
keeps falling through it, up to 0.38 m below in one phase. The unshifted jump (the street's arc) crosses with the feet
0.009 m above the ground, ends that update 0.191 m below and lands on the next: the 0.19 m dip of the per-update
trace is this, not a sampling artefact. The code path is the sweep's landing contact (`0x0023e408`, run from
`0x0033d340` when body flag `0x40` is set; segment from feet + `+0x4e8` − 0.16 × scale, 0.880 m for the player,
to the moved feet), which records the contact's fraction as `(hit − 1.0) / (length − 1.0)`, or 0 when the hit is
nearer than 1.0 m (the human's vtable `+0x5c`, the constant 1.0 at `0x004ed818`); why the hit is not taken until the
feet are about 0.17 m under is not traced. What the code shows (confirmed (code)):

- the segment's upper point is `0x00226a20`: the feet + `+0x4e8` − 0.16 × the scale;
- only a hit with a normal `z` above 0.65 records a landing contact;
- `0x0033d340` marks the contact (`+0xb0` = `0x80`) and its handler `0x00219d50` calls `Land(1, 1)`;
- contacts are kept sorted by fraction (`0x0033d718`), and the resolution `0x0033d9d8` acts on the first contact
  whose handler returns a code that is not 0, then returns;
- the body's move loop `0x0033e278` runs the sweep up to 3 times.

Read alone, this lands on the update the moved feet pass the ground, which the runtime contradicts; the 0.17 m does
not follow from the 0.88 / 1.0 m geometry either. A log of the contacts (a code patch that copies each contact's
fraction and handler code to free memory) would settle it.

**A fall at runtime** (PCSX2 2.9.94, `level99`, no stick input; Rembrandt raised 10 m by writing the transform table's
`z`). Confirmed (runtime):

- `vz` changes by −0.5227 m/s per update from the second airborne update, and `z(n+1) = z(n) + vz(n+1) × dt`: the
  velocity is updated before the move.
- Clip 428 (drop cycle) plays while falling.
- Landing after 34 updates at `vz` = −17.25 m/s, inside the damage band; then 429 (drop land), 294 (a hit reaction)
  and 198 (a ground roll).

**Anim states in the air.** `Human_ChooseAnimState` (`0x00259578`) checks the state flags first: `0x2000000000` →
anim state **27** (landing), `0x800000000` or `0x1000000000` → **26** (falling), `0x400000000` → **25** (jump).
Confirmed (code). The builders, confirmed (code):

- 25 (`0x0025fba0` → `0x0025cf30`): the **jump loop** (434) after a 0.1 s fade, then the launch ([Jumping](#jump)).
- 26 (`0x0025d020`): the **drop cycle** (428), or the long-fall cycles 422 / 425 in a long fall
  (`0x400000`, `0x00223b48`, `0x00227e60`). After a jump the jump loop keeps playing (runtime).
- 27 (`0x0025d390`): after a jump, **436** (jump end running) handing over to a gait blend when the stick is above
  0.12, else **435** (jump end) and then the idle; after a drop, **429** (drop land); after a long fall
  (`0x1000000000`), the clip after the current one, or 198 (ground roll).

**Walking off an edge at runtime** (the roof of a car, 1.48 m up; stick 0.5). Confirmed (runtime): state 26 with 428
and record `+0x08` `0x400000` for the first two updates, gravity as above, then state 27 with **429** for about
0.5 s and the idle.

### Jumping {#jump}

**The button.** On command 10 (triangle pressed), `Player_UpdateActions` (`0x0027c120`) tries, in order, and stops at
the first that succeeds. Confirmed (code):

1. a **climb** (`Climb_TryStart`, [below](#climb)), if human flag `0x20000000` is clear and the stick is above 0.12;
2. a **context action** (`0x002811f0`), if L2 is not held;
3. a **jump** (`Player_TryJump`, `0x002829e8`), if human flag `0x10000000` is clear and the stick is above **0.95**;
4. an object action (`0x00226ff0`, `0x00257f38`), if L2 is not held.

Nothing happens while state flags `0x7bf9e9f7ff0` or record `+0x08` flags `0x5cfeafb` are set.

**Checks** (`Player_TryJump`, then `Human_BeginJump` `0x0023db48`). Confirmed (code). The jump is refused when the human
carries an object of class 4 or 6 (`0x00224000`), is in a combat stance (`0x00228340`), moves at 3.3 m/s or less
(`0x0051018c`, speed `+0x1ac`), is not a player, or a ray from 1.7 m above the feet, 5.5 m long in the stick's
direction (`0x0021d228`), hits a **climbable** triangle (the rule of [Climbing](#climb)): near a climbable wall the
button climbs or does nothing. `Human_BeginJump` then needs the gait for the speed (`Human_GaitForSpeed`) to be 3 or
more and `|v|` at least 3.3; it keeps the gait at `+0x3c0` and the height at `+0x560` and sets state flag
`0x400000000`.

**Launch** (`Human_LaunchJump`, `0x002217f0`, called by the state 25 builder). Confirmed (code):

- horizontal velocity: the current direction times the **run speed** for a take-off gait of 3 or 4, the **sprint
  speed** for gait 5 (the jog speed below 3);
- vertical velocity **5.5 m/s** (`0x00510188`);
- `Human_StartFall` (vtable `+0x154`), then the state function `Human_AirControl` (`0x00240898`, set through
  `0x00227c28`) and anim state 26.

**In the air** (`Human_AirControl`). The horizontal speed is kept. With the stick above 0.12 the heading turns toward
it at the `Human_MaxTurn` limit with the same ease as on the ground, and the velocity turns with it; with the stick
centred nothing changes. Gravity, the airborne counter and the landing are those of a fall. Confirmed (code).

**At runtime** (the street, Rembrandt on flat ground, triangle tapped for one update). Confirmed (runtime):

| Case | What happened |
| --- | --- |
| stick 0.8 (walk at 1.63 m/s), triangle | nothing |
| stick 1.0, run at 7.80 m/s, triangle | state 26 and clip 434 at once, state flags `0xc00000000`; `vz` 5.5 then −0.5227 per update; apex **1.06 m** above the take-off; horizontal speed 7.80 throughout; landed after about 25 updates (0.83 s), **6.4 m** further |
| stick 1.0 and L2, sprint at 10.245 m/s, triangle | the same arc at 10.24 m/s horizontal, about 23 updates in the air |
| the same, stick turned 90° right just after take-off | the heading turned by about **4° per update** toward the stick, the horizontal speed staying 10.24; that is the run's limit, not the sprint's 2.5° (which gait the air turn uses is not traced) |
| landing | state 27, **436** for about 0.4 s at its own 4.23 m/s, record `+0x08` `0x1000000`, then the gait blend from a jog back to the run |
| per update (the feel pass, slot 1) | **24 updates** in the air from a run: the last airborne update leaves the feet 0.19 m below the ground and the next one lands them ([the landing rule](#falling)); that landing update still moves at the full 7.80 m/s, then 436 holds 4.23 m/s for 10 more updates (11 in all) and the gait blend gains 0.8 m/s per update from there; take-off to landing **6.24 m** |
| triangle during the run start (414), at 3.35 m/s | a jump at once: the run start does not block it, only the 3.3 m/s and gait tests do (slot 7) |

Clips 427 (slot 25) and 430-433 (jump from idle, walk or either foot) did not play in the player's jump; 430-433
belong to the AI's jump (`0x0029ade0`, inferred from its callees).

**In an input script**: `stick left 0 100`, wait until the run start is over and the speed is above 3.3 m/s, then
`tap triangle`; add `press l2` beforehand for a sprint jump.

### Climbing {#climb}

`Climb_TryStart` (`0x002826f0`) runs on triangle with the stick above 0.12 ([Jumping](#jump)). Confirmed (code):

1. Refused while record `+0x08` has any of `0x84240`.
2. Direction `d`: the stick's direction when its magnitude is above 0.01, else the facing (`0x0021d228`).
3. Two horizontal rays along `d` from **1.7 m** and **0.69 m** above the feet, **1.5 m** long, or **4.5 m** when human
   flag `0x2` is set (players) and the gait (`+0x1a8`) is 4 or 5.
4. If both hit and their distances differ by less than 0.1, the obstacle is **tall** (the high hit is used);
   otherwise it is **short** (the low hit is used; a hit by the high ray alone is not a climb).
5. The hit must be **climbable**: material 30 (`MATERIAL_LOW_FENCE`), or triangle flag `0x4` for a player, or
   triangle flag `0x80` ([Collision](collision.md#triangles)).
6. `Climb_ProbeTop` (`0x00282370`) with, for a tall obstacle, `H` = 3.0, a window of 1.7-2.91 m and the fence ids
   437 / 440; for a short one `H` = 1.8, a window of 0.7-1.7 m and the short-fence ids 443 / 446.

`Climb_ProbeTop`, confirmed (code):

1. The face must look at the player: `n · d < −0.7` (within about 45°).
2. A ray straight down from `H` above the point 0.4 m beyond the face (`pos + d·t − 0.4·n`), `H + 0.5` long, finds
   what lies just behind the face; its **top** is `H` minus the hit distance, a height above the feet.
3. A top inside the window is a **wall climb**: the ids + 12 (449 / 452 wall, 455 / 458 short wall).
4. Otherwise a top of 0.25 m or more refuses the climb.
5. Otherwise (no floor just behind the face, or one near the feet' height: a **fence**), for a tall obstacle a ray
   along `d` from 2.5 m above the feet must miss (the fence is lower than 2.5 m), and the fence ids are kept.

`Climb_Start` (`0x00281c20`), confirmed (code):

- The first id is the **standing** clip; the **running** one (+3) is used when human flag `0x2` is set and the gait
  is 4 or 5. Each clip's reach is the length of its type-8 event vector (`0x00101558`, through the Anim Range List,
  `0x002544a0`). A running climb needs the face between `(r1 + r2) × 0.4` and `r2 × 2.2` away (the reaches of the
  first and second clips); a standing one at most `(r1 + r2) × 0.4`. `CfgClimbWithGhetto` (`0x00510250`) is also
  read here.
- The start point is the hit at the feet' height plus `n × 0.9 × reach`, kept at `+0x2c0`; the human is turned to
  face the wall and moved there over 1/60 s or 1/15 s.
- The three clips play in turn (P1, P2 = id + 1, P3 = id + 2). At P1's end (`0x00281450` running, `0x00281838`
  standing) the forward probe is made again from 0.69 m (2.5 m long running, 1.5 m standing). On success the body
  is **moved at once** by P2's root displacement turned to the facing, body `+0x40` gets `0x80000000 | 0x4000`,
  and record `+0x08` gets `0x40` (the fences stop blocking, [Walls and steps](#walls)); on failure the climb ends
  in the idle or the combat idle. A running climb ends in a gait blend, a standing one in the idle.
- While clips 437-460 play, `+0x5b9` is 1 (`Human_StateUpdate`).
- **The first clip runs during the move.** The same call sets the move to the start point (`0x0023d2b8`: over 1/60 s,
  or 1/15 s when the turn `0x0023cf88` takes 1/30 s) and installs the three clip tasks (`0x00105990`, `0x001754e8`),
  so P1's clock starts on the climb's first update, not after the move. Confirmed (code). At runtime (the slot 7
  fence from a run, [Feel comparison](feel.md)) 440 lasted 11 updates: the start update still at run speed, the two
  snap updates (1.11 and 1.17 m), then 8 updates of the clip's root motion at full speed from the first (5.79 m/s
  falling to 0.36 m/s), with no fade-in. The first update of 441 and of 442 moved the body 0 m. Confirmed (runtime).

**What gets climbed**, from the rules above (heights above the feet; inferred from the code, the three rows marked
runtime were seen):

| The obstacle | Just behind it (0.4 m past the face) | Climb |
| --- | --- | --- |
| reaches 0.69 m but not 1.7 m | ground lower than 0.25 m | short fence (443-448) |
| reaches 0.69 m but not 1.7 m | a top at 0.7-1.7 m | short wall (455-460); runtime: a trash can (1.28 m) and a car (1.48 m) |
| reaches 1.7 m | a top at 1.7-2.91 m | wall (449-454); runtime: a roof 2.64 m above the trash can |
| reaches 1.7 m, lower than 2.5 m | ground lower than 0.25 m | fence (437-442); runtime: a street fence, from a run |
| lower than 0.69 m, or a top outside the windows, or a fence of 2.5 m or more | | no climb |

**At runtime** (the street; stick and triangle through patched pad input). Confirmed (runtime):

| Climb | Input | What happened |
| --- | --- | --- |
| fence from a run (440, 441, 442) | stick 1.0, triangle tapped every 0.1 s | the tap 4.9 m from the face did nothing (no climb, and no jump: the 5.5 m climbable check); the one about 4.4 m away started 440. The body was **snapped** 2.5 m forward in two updates (37-39 m/s), then 440 moved it at 5.4 to 4.5 m/s to the start point (0.34 m before the face, 0.3 s); 441 (0.47 s, record `+0x08` `0x40`) carried it **through the fence** at 2.4-3.4 m/s with the feet at ground height; 442 (0.43 s, `0x80000`) and the run went on; 1.27 s in all |
| the same, per update (the feel pass, slot 7, the tap 4.5 m from the face) | stick 1.0, triangle every 2 updates | 440 for **11 updates**: the snap moved 1.11 m and 1.17 m, then the clip carried the body until it **stopped against the fence, 0.48 m from its face** (its walking sphere); 441 for 15 updates through the fence at 2.4-3.6 m/s; 442 for 13; the run's gait blend 39 updates after the tap |
| short wall and wall, per update (slot 8) | stick 0.5, triangle | the rise at 456's start took **2 updates** (1.12 m, then 0.17 m by the ground snap); at 450's start 2.30 m, then 0.097 m per update for 4 updates to the roof (2.64 m in all) |
| short wall, standing (455, 456, 457) | stick 0.5, triangle | 455 for 0.3 s (`0x80000`); at 456's start the feet **jumped** 1.06 m forward and **1.29 m up** in one update onto the trash can; 456 for 0.6 s (`0x40`), 457 for about 0.6 s, then the walk |
| wall, standing (449, 450, 451) | stick 0.5, triangle, on the trash can | 449 for 0.37 s; at 450's start the feet jumped 0.98 m forward and 2.4 m up, and the ground snap settled them on the roof (2.64 m above the start) over the next updates; 450 for 1.4 s, 451 for 0.37 s |
| short wall onto a car | stick 0.5, triangle | as the trash can, 1.48 m up |

The vertical part of a climb is the single move at P2's start: the feet do not rise during P1 or P2.

**In an input script**: standing, `stick left 0 50` toward the obstacle and `tap triangle` within reach; from a run,
`stick left 0 100` and `tap triangle` when the face is 4.5 m away or less.

## Coney's implementation

Coney loads a character's three resources, plays its clips, and plays Rembrandt as player 1 with the human's
locomotion and the follow camera ([Camera](camera.md#coneys-implementation)):

- `src/characters/character_list.*` reads the Character List from `warriors.glr` and finds a record by model name;
  `character_assets.*` loads the three resources by the records' hashes (`"%u"` file names).
- `src/characters/clump_reader.*` and `character_model.*` decode the clump (frames, HAnim, skin, material) and the
  native geometry, with the shared PS2 decoder in `src/graphics/ps2_world_mesh.*`, into merged skinned vertices and
  triangles; no librw outside `src/platform/`.
- `src/characters/character_data.*` resolves the 722 anim ids to clips ([Animation](formats/animation.md)): the
  distinct slot values, smallest first, each take the next clip popped off the object stack, which gives Rembrandt's
  408 walk and 413 walk-start as above.
- `src/characters/character_rig.*` builds the skeleton and the skinning matrices (pose bone `n + 2` · the pose turn
  · the inverse bind matrix) and skins on the CPU.
- `--view-character [NAME] [--anim CLIP]` ([Building](../guides/building.md#the-character-viewer)) shows a skinned
  character playing a clip in place on the fixed 30 Hz step, with a pad-driven orbit camera.
- `--render-references DIR` ([Building](../guides/building.md#reference-images)) writes a 256x256
  transparent PNG of every character, posed at its default clip's first frame (and of every object,
  [Level loading](level-loading.md#the-object-list)), from a fixed three-quarter camera
  (`src/characters/reference_render.*` frames and reduces; `src/platform/reference_renderer.*` draws offscreen).
- `src/characters/anim_set.*` answers an anim id from the character's own data, then from the generic character data
  `0x9da2e531` (594 `gen_*` clips) in place of the default table, with the clip's rate from the Anim Range List flags
  ([CfgAnimSpeeds](formats/animation.md)) and its speed (root displacement × rate / duration).
- `src/human/locomotion.*` is the pure maths: the camera-relative stick, dead zone and run threshold
  (`PlayerRecord_Update`), the target speed and gait, the per-gait turn limit with its ease and carry, acceleration,
  the skid rule, the slope factor and the gait blend's target (`0x0025ec28`); the sprint's target speed; and the
  body's lean (`Human_Lean`, `0x00248df0`). The turn limits and ease are the values in play (20°, 18°, 18°, 16°, 24°
  in a combat stance, full at 2.0 rad): the defaults, and for a level `--play-level` runs, what the preload's
  `CfgSetTurnRates` and `CfgTurnRate` calls recorded.
- `src/human/stamina.*` is the stamina meter ([Sprint and stamina](#sprint)): the drain at the sprint gait, the
  refill and the gait, airborne and "run with L2" blocks, and `Player_UpdateSprint`'s rule (L2 held and stamina not
  0, asked again every update).
- `src/human/body.*` is the walking body of [Walls and steps](#walls): the sphere of 0.35 × scale (× 1.4286 for
  the player: 0.485 m) centred 0.05 above its radius, wall triangles `|n.z|` ≤ 0.65 that the move goes into, the
  0.25 m low-and-thin rule, the fence materials passed through while climbing over, and the airborne push-out sphere
  (0.5 × scale for a player). The walking sphere is stopped where it touches a triangle, so a face lower than its
  centre stops it where it meets the face's top edge, `sqrt(0.485² − (0.535 − h)²)` from the face (0.396, 0.400 and
  0.424 m for 0.255, 0.26 and 0.30 m faces, as at runtime); a face under 0.25 m is walked onto in one update with no
  change of speed or clip. The human keeps the move the walls left as its velocity, so a wall met at a steep angle
  brakes the player.
- `src/human/jump.*` is the jump's checks and launch ([Jumping](#jump)): faster than 3.3 m/s and the stored gait at
  jog or above, refused when a climbable face lies within 5.5 m; the run or sprint speed forward and 5.5 m/s up.
  A start clip does not stop it.
- `src/human/climb.*` is the climb choice ([Climbing](#climb)): the two forward rays, the climbable test, the probe
  0.4 m behind the face with its windows, the 2.5 m fence ray, the clip ids, each clip's reach (its type-8 event
  vector) and the reach window.
- `src/human/human_animator.*` is the anim state and its builders: idle, move, the jump loop (25), the drop cycle
  (26), the landing (27: 436 handing over to a gait blend, or 435 and the idle), the run stop and the climb's three
  clips (Coney's states 101 and 102); the walk or run start played at once and handed to the five-clip gait blend,
  the 0.1333 s fade between moves, the idle's fade by how much of a start clip has played, and the walk start swapped
  for the run start in its first half ([Clip selection](#clip-selection)). Its clips hold their record `+0x08` bits
  while they play ([Tasks](tasks.md#held-flags)): a start clip `0x10000000`, the landing `0x1000000`, the run stop and
  the climb's first and last clips `0x80000`, the combat moves theirs ([Combat](combat.md#coneys-implementation)).
- `src/human/locomotion_gate.*` is the locomotion gate ([Tasks](tasks.md#locomotion-gate)): `stickBusy()`
  (`Human_IsBusy`'s mask `0xaeebf7ff`, in the air or held: no stick step) and `stickVelocityGated()` (`0x110c0880`, or
  state code 5 or 6: the stick turns the human but sets no velocity).
- `src/human/human.*` is the human: spawn (a 2.5 m ray from 1 m above, feet 0.01 above the hit), its per-player
  record (the stick, the camera's forward, the command and the buttons, written by the pad or a brain) and its update
  in three passes ([Tasks](tasks.md#humans-update)): `animate()` (the animation), `updateState()` (root motion of the
  clips that move the body, locomotion through the gate, air control or a climb's move, gravity, move or fall, the
  meters, the lean) and `updateActions()` (the dispatcher from the record, triangle's actions, then the anim state);
  the ground snap, the fall and landing, the walking body, the airborne push-out, and the climb from its probe to its
  last clip (the move to the start point, the re-probe at the first clip's end, the rise onto a wall).
- `src/human/humans.*` is the characters' step (`Humans_Update`): every human's record (the commands of those no pad
  drives cleared), the brains' hook (empty until the AI lands), then the animation of all, the locomotion of all and
  the actions of all, walked forward and backward on alternate steps. It runs on Coney's fixed 1/30 s step.
- `src/human/player.*` is player 1 (the character at scale 0.97, the human, the follow camera; L2 held sprints,
  triangle pressed acts): it writes the pad into its human's record and steps the characters, and keeps the snapshot
  drawing reads: the previous and current feet, heading, lean, pose (each bone slerped) and camera, interpolated for a
  renderer that draws between steps.
- The debug menu's tunables ([Debug menu](../guides/debug-menu.md#tunables)) expose the body, sprint, jump, climb
  and lean values above, each defaulting to the original's.
- `--play-level NAME` ([Building](../guides/building.md#playing-a-level)) plays it: `src/platform/play_level_mode.*`
  steps the player, streams the level and runs the visibility pass in its update, and draws from the snapshots of the
  last two steps, blended by the frame's alpha, in its render
  ([Update and render](../guides/conventions.md#update-and-render)). A disc test (`[frame_rate]`) plays both scripts
  at 30, 60, 144, 240 and 1000 frames a second and with irregular frames and checks the player, the camera and the
  streaming come out bit for bit as in lockstep.

**Disc test** (`[characters]`, counts only): all 543 records load (128 models, 52 character data resources, 507
dictionaries); 150,509 vertices and 155,493 triangles; 1,692 clips and 2,843 resolved ids; the joint mismatch of a
clip's first pose over the [reference pose](formats/animation.md#reference-pose) averages 0.045 m (worst 0.098 m); the
skinned heights run from -0.07 to 4.06 m (2.29 m at most over the bind rotations; which first clips reach higher is
not looked at yet); every model's bind rotation for
bone 2 is 120° from the reference pose's, and 292 of the bones 2-33 over the models are more than 10° from it.

**Disc test** (`[player]`, counts only): Rembrandt's speeds from his clips are the runtime values (walk 1.629, jog
4.857, run 7.801, sprint 10.245 m/s); at level99's start he lands at z 0.25, idles in 388, takes the walk start 413
at a 30 % stick (not moving on its first update, then 0.762 m/s: the clip's root motion × 0.97), walks in 408 at
1.629 m/s, gains 0.8 m/s per update to the run (410), skids into the run stop (417) on release and then idles, and
never leaves the ground or passes through the scenery he is run into; the same script gives the same path twice.
On the default sandbox's ledges he walks onto the 10 and 25 cm blocks (the 25 cm block's 3 m wide face is two
slivers the 0.25 m rule skips) and stops at the 50 cm one, 0.484 m short of its face, where the sphere meets its
top edge.

**Disc test** (`[traversal]`, positions and hashes only; Rembrandt on the sandbox's
[parkour course](../guides/sandbox.md#the-shipped-layouts)), each run against the runtime values above:

- **Sprint** (L2 held, stick 0.8 then 0.96): a walk at 1.629 m/s and no drain at 0.8; 10.245 m/s at 0.96 for about
  6.75 s until stamina is empty, then 7.801 m/s in one update; stamina held at 0 while L2 stays down and refilled at
  40 per second once it is let go; letting go of the stick at the run plays the run stop (417) for about 24 updates.
- **Jumps**: a run jump at 7.801 m/s and a sprint jump at 10.245 m/s, both 5.5 m/s up, rising 1.06 m and landing in
  436; the sprint jump carries about 8.5 m (24 updates in the air and the landing update); letting go of the stick in
  the sprint plays the run stop (417). A run jump clears a 5 m gap between two 2 m platforms, the feet ending one
  update below the far platform's top before they land on it.
- **Fences** of material 30, from a run: the 1 m short fence (446) and the 2 m fence (440) are climbed with the feet at
  ground height, through the fence; the 2.6 m fence is neither climbed nor jumped, and stops the body at its face.
- **Walls and blocks** with flag `0x80`, standing: the 0.75 and 1.0 m walls are short-wall climbs (455) onto their
  tops; the 2.0 m block is a wall climb (449) onto its top; the 3.0 m block is not climbed.
- **Kerbs and ledges**: a 20 cm kerb is walked onto; 30, 50 and 65 cm ledges stop the body at their face.

**Coney choices** where the research is silent: the material takes its dictionary's only texture; bones 0 and 1
rest at the identity; weights are used as stored, not renormalised; the viewer's lights, camera and clip keys, and the
reference images' pose, camera and lights, are Coney's own. For the human:

- **The default anim table**: slots set to the default (`0xffffffff`) are answered by the generic character data
  `0x9da2e531`, whose clip speeds match the runtime-confirmed ones exactly (380, 407, 409, 410, 411); the table at
  resource manager `+0x70` is not decoded.
- **Standing**: a human not asked to move counts as moving while faster than a quarter of the walk speed. The
  original's getter `0x00221580` is the **sneak-walk** speed (record `+0x16c` × `+0x3a4`, 1.585 for Rembrandt;
  confirmed (code)), so the original's threshold is 0.396 m/s against Coney's 0.407.
- **Falling**: state 26's builder loops the drop cycle (slot 26, 428) with the idle's 0.15 s fade; a drop lands
  straight into the idle or the move, without the drop land (429), the long-fall cycles or the ground roll.
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
- **The jump's gait test** reads the stored gait (`+0x1a8`, the nearest) where the code reading names the "reached"
  gait: the runtime jump 7 updates into the run start, at 3.35 m/s, passes only with the stored gait.
- **The body** is the original's walking sphere ([Walls and steps](#walls)), but pushed out of the nearest wall
  triangle by the distance to the triangle's closest point rather than swept along the move; the push tries 3
  passes, then stops. The low-and-thin rule takes the triangle as given, so a 0.25 m face exactly (a sliver) does
  not stop the body, and a face from 0.26 m does.
- **Landing** probes from 1.0 m above the feet, as the ground snap does. A floor found on that segment is landed on
  by the first update that starts with the feet 0.17 m or more below it ([When the landing happens](#falling)); until
  then the body falls on through it (0.19 m after a run jump, as at runtime). The landing is at that update's position
  across the ground, so the landing update keeps the full horizontal speed.
- **Stamina** carries one fraction of a point between updates, started again when the meter changes direction.
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
- **The landing** has no fade, so 436 moves the body at its 4.23 m/s from its first update as at runtime, and hands
  over to a gait blend at the jog (1.0).
- **The air turn** is limited to 4° an update, what a sprint jump turned at runtime; which gait's limit the original
  uses is not traced.
- **Climb reach**: `r1` and `r2` are read as the reaches of the standing and the running first clips (for the fence,
  437 and 440). Rembrandt's type-8 vectors are 0.77 / 2.31 m (fence), 0.64 / 1.39 m (short fence), 0.65 / 2.30 m
  (wall) and 0.78 / 1.55 m (short wall), which give a running fence climb from 1.23 to 5.09 m: the runtime tap at
  about 4.4 m started one and the one at 4.9 m did not (the 4.5 m ray). The chain's first two clips would give
  about 1.2 m and no running climb from 4.4 m.
- **The climb's move** to its start point takes 2 updates from a run and 1 standing; the first clip's clock runs
  through it, as the original installs the clips with the move, but its root motion moves the body only once the
  move is done. On the sandbox's 2 m fence 440 lasts 11 updates from the tap's, as at runtime.
- **The rise**: walls and short walls move at once at the second clip's start, by its root displacement (450 and
  456 carry none in section A; their whole displacement, 0.98 and 1.06 m, as at runtime) and up to the top the probe
  found; fences go by the second clip's own root motion, as the runtime's 441 carried the body through.
- **Climbing over** lasts the second clip: from the re-probe at the first clip's end to the third clip's start (at
  runtime 441 had record `+0x08` `0x40` and 442 did not). Meanwhile the fence materials (30, 31, 122) are not walls,
  and not ground for the snap and the landing, so a low fence's top does not lift the feet.
- **The re-probe** at the first clip's end accepts any triangle, not only a climbable one.
- **The climb's fade**: none; the first clip moves the body at its full root speed from its first update after the
  move (5.79 m/s on the 2 m fence, as at runtime). The update the chain goes on to its second or third clip moves
  nothing, as the first update of 441 and of 442 did at runtime; why is not traced.
- **The lean**: the four limits go with walk (and standing), jog, run and sprint; the drawing rolls the body by the
  lean about its forward axis through the feet.
- **Context and object actions** (triangle's second and fourth tries) are hooks that never succeed yet, so triangle
  goes on from a refused climb to the jump.
- **Camera through a fence**: the follow camera's ray passes through material 30 ([Camera](camera.md#coneys-implementation)),
  so it keeps its distance through a fence climb as the original's does.
- **Out of the world** (20 m below the mesh's lowest point): the human is put back at the start instead of failing
  the mission.
- **The gait blend's leading clip** uses a tolerance of 0.001 when it compares the value with its target.
- **Level starts**: the start comes from the level's own script at run time: its `HuCreate` for player 1 at the
  checkpoint (`GetCheckPoint`), kept by Coney's `HuCreate` binding ([Level
  loading](level-loading.md#coneys-implementation)); the story's way in and `--play-level NAME [--checkpoint N]` both
  run it. A level that places player 1 at a flag (the hub, the Rumble arenas) starts him on the flag ([World
  flags](flags.md#coneys-implementation)). The character's lights (ambient 0.45, one directional 0.7) stand in for the
  LightManager, and he is drawn between the level's two worlds.
- **The model** (`src/characters/character_class.h`, from [From a type to a model](#type-to-model)):
  `characterClassOf` is `Human_Init`'s switch, `modelRecordType` the player's plain-alias rule (a player made as a
  plain alias is drawn as the class's own type; a variant keeps its own), and `modelNameFor` takes the model name from
  that type's recorded `CfgChar` call (its tenth argument), with `_a` added in levels 60 to 64; `HuCreate` keeps it
  and the play mode loads it from the Character List. Disc check: `level2` checkpoint 3 is drawn as `warr_cl` (Cleon),
  `level3` checkpoint 4 as `warr_sn` (Snow), `level99` checkpoint 1 as `warr_re_cv`. Coney's choice: a type with no
  `CfgChar` call or a model the Character List lacks is drawn as Rembrandt, with a log line.
- **Names**: `@orig` names for addresses the research describes but does not name (such as `Human_SnapToGround`,
  `GaitBlend_Advance`, `PhysicsBody_PushOutOfWalls`) are Coney's.

## Notes for implementers

- **Make the player first.** For `level99`, checkpoint 1: one human named `Rembrandt`, model `warr_re_cv` (found
  through the Character List), at `(-284.4, 120.4, 0.3)` heading 0°, snapped to the ground with a 2.5 m ray from 1 m
  above; pad 0; the follow camera targeting it ([Camera](camera.md)). Any other level: its entry for the checkpoint
  in [Level starts](../references/level-starts.md) (checkpoint 1 by default, the first mode's flag in a Rumble arena,
  `fWchiefStart_1` in the hub), with that entry's character type.
- **Step at 30 Hz.** Movement, turning and the animation step all use dt = 1/30 and per-update limits; a PC build
  that runs faster should keep a fixed 30 Hz step (or scale every limit) so speeds and turn rates match.
- **Speeds per human** come from the clips: for each locomotion slot, the clip's horizontal root displacement over
  its duration ([Speed classes](#speed-classes)). For Rembrandt walk 1.63, jog 4.86, run 7.80, sprint 10.25 m/s.
- **Gaits**: a walk up to 0.95 stick magnitude (any amount above 0.12 gives the same walk), a run above it; the start
  clips (413, 414) first, then the gait clip; idle at once on release.
- **Ground**: no vertical velocity on the ground; snap the feet with a ray from 1.0 m above, 1.5 m long, each update;
  a miss starts a fall with gravity 15.68 m/s² (from the second airborne update), capped at 50 m/s; land on floors
  with `n.z` > 0.65; fall damage from 14.9 m/s, a kill from 20.5 m/s ([above](#ground)). A fall or jump lands on
  the first update that starts with the feet about 0.17 m or more below the floor: keep moving the full step until
  then (feet up to 0.38 m under), then land at that update's position with the horizontal speed kept
  ([When the landing happens](#falling)).
- **Clips that move the body** move it at the clip's root speed × the body scale from their **first** moving update,
  with no fade-in of that speed: the walk start 0.762 m/s on every one of its 12 moving updates, the run start 2.77,
  2.49, 2.56, … and the run stop 2.24, 5.00, 5.10, … ([Per update](#locomotion)). A first update slower by 0.75 (the
  start clips' playback rate) or a stop that ramps from 0.31 m/s is a difference to remove.
- **Walls**: a sphere of radius 0.35 × scale × body `+0x60` (1.4286 for the player: 0.485 m), centred that radius
  plus 0.05 above the feet, swept along the move; the slid velocity is kept, so a steep wall brakes the player;
  wall triangles less than 0.25 m tall are not walls, which is all the step-up there is ([Walls and steps](#walls)).
- **Sprint** is L2 **held** with the stick above 0.95: cleared every update and set again while L2 is down and
  stamina is not 0. Stamina drains 20 per second at the sprint gait and refills 40 per second (Rembrandt: 135), not
  while running with L2 held ([Sprint](#sprint)).
- **Triangle** tries a climb (stick above 0.12), then a context action, then a jump (stick above 0.95, faster than
  3.3 m/s): 5.5 m/s up, the run or sprint speed forward, steering in the air ([Jumping](#jump)).
- **Climbs** are chosen by two forward rays at 0.69 and 1.7 m and a downward probe 0.4 m behind the face; the feet
  rise in one move at the second clip's start ([Climbing](#climb)).
- **The stick is camera-relative** before the human sees it: turn the stick by the camera's heading first, then
  apply the dead zone (0.12) and the run threshold (0.95) to its length.
- **Turn with a limit and an ease**, not instantly: in play 20° per update walking, 18° jogging and running, 16°
  sprinting, eased below 2.0 rad (the values `config_preload2.lua` sets, [Locomotion](#locomotion)).
- **Characters are skinned with 32 bones** (34 pose entries, [Animation](formats/animation.md)); every character on
  the disc has the same skeleton layout, so one bone mapping serves all.
- **A disc test**: every Character List record resolves to three resources that load, and every model is the clump
  described above (counts only).

## Open questions

- **Clip selection** (answered): [Clip selection](#clip-selection), with the task system on
  [Animation](formats/animation.md#animation-tasks); the playback rate is never scaled with the speed. Still open:
  the anim states other than idle and move (11, 21, 24 and the combat ones) and the special idle ids.
- **Jog**: when a pad-controlled human jogs other than when carrying (a movement style, a script).
- **What slots 16-24 and 28-34 are used for** (16-24 are the attacks and the block, [Combat](combat.md#attacks)) (their
default ids are known, [Anim slots](#anim-slots)), and the
  movement styles of `0x00253688` beyond the ids they write.
- **The `+0x65c` scale's source**: what the division in `Human_Init` takes.
- **How the texture reaches the material**, which names none ([Character geometry](#character-geometry)).
- **The rest rotations of pose bones 0-2** and why bone 3's parent in the table differs from its frame's.
- **The vertex colour slot** (zeros in every character checked) and **the second texture coordinate set**: what
  the renderer does with them.
- **The default anim table** at resource manager `+0x70`, which answers the slots set to `0xffffffff`; Coney uses the
  generic data `0x9da2e531`, which matches every speed checked, but whether the table is that resource is open.
- **The idle threshold's getter** (answered): the sneak-walk speed ([Coney's implementation](#coneys-implementation)).
- **The body's shape** (answered): a capsule shape whose walls are a swept sphere ([Walls and steps](#walls)). Still
  open: what the capsule's 1.886 (shape `+0x44`) and the human's `+0x4e8` are used for.
- **The airborne anim state** (answered): states 25-27 ([Falling and landing](#falling)); air control
  ([Jumping](#jump)). Whether locomotion turns the human while a start clip plays (answered: it does,
  [Tasks](tasks.md#locomotion-gate)).
- **Step height at runtime** (answered): steps up to 0.245 m are walked onto in one update, from 0.255 m they stop
  the body ([Walls and steps](#walls)), measured on a test step made in the mesh. Still open: a real kerb in a later
  level, as corroboration.
- **The context action** (`0x002811f0`) and the object action (`0x00226ff0`) that triangle also starts: doors,
  pick-ups, which objects.
- **The air turn's gait** (partly answered): 4° per update from a run and a sprint alike, which is the non-player
  column's run value at `0x005101c4` (inferred: `Human_AirControl` reads the other column); not read in the code.
- **Climb reaches** (partly answered): Coney reads Rembrandt's type-8 vectors ([Coney's implementation](#coneys-implementation));
  still open: which two clips `r1` and `r2` belong to (Coney's reading: the standing and running first clips), and
  `CfgClimbWithGhetto`'s effect.
- **The landing's threshold**: why the landing contact is taken only once the feet start an update about 0.17 m
  below the floor ([When the landing happens](#falling)). The contact path is traced (the resolution `0x0033d9d8`
  acts on the first contact by fraction) and does not explain it; a runtime log of the contacts is needed.
- **The landing's fade** and the gait blend value 436 hands over to; the drop land (429) and the long falls are not
  built in Coney.
- **The climb's details** (the first clip's timing answered: it runs during the move to the start point, with no
  fade, [Climbing](#climb)): when record `+0x08` `0x40` is cleared; whether the re-probe needs a climbable triangle;
  how a fence climb's second clip moves the body (Coney: its root motion).
- **The camera during a fence climb** (answered: the camera's rays exclude materials 30, 122 and 107,
  [Camera](camera.md#collision)).
- **Triangle flags `0x4` and `0x80`**: why two climbable flags (one for players only), and which surfaces carry them.
- **The sprint's other clear** (answered: the block, [Combat](combat.md#dispatch)). Still open: what `0x00230140`,
  called when the sprint is set, does, and the fight test `0x00224f28`.
- **The rest of the human**: the 0x180 record (the 0x2f0 record is the brain, [AI](ai.md#brain)), the state flags
  tested by `0x002265f0` / `0x00226660`, and `Human_MakePlayer`'s steps.
- **Level starts at runtime**: the list is read from the scripts; a runtime check would confirm a few. For each of
  `level2` checkpoint 1, `level95` chapter 1 and `level102` brawl, read player 1's transform (the table at
  `0x00714b00`, index `+0x92` of the human whose `+0x1b0` is 1) on the first frame of play, and again after the
  checkpoint script's start function has run (the hub's door walk, the arena's teleport).
- **Start functions that move the player later**: the list notes only teleports made directly by a checkpoint's start
  function (or one function down). Scene callbacks and chapter steps teleport the player too (99 `Teleport` /
  `TeleportToFlag` calls on `player` in the story scripts in all); which of them run before the first frame the player
  controls is not traced.
- **Four Rumble arenas without a known mode script**: their `level<N>_<mode>_init.lua` names are not recovered, so
  they have no entry ([Levels](../references/levels.md) lists the modes found per arena).
- **A steady run's velocity in floats**: the original's run measures at or above the run speed on 10 of 93 updates,
  Coney's on about 6 %; how its velocity is built (the order of the multiplies) is not traced.
- **The run start's speed on its 7th and 11th updates** in `run_circle` (3.35 and 5.40 m/s against Coney's 3.25 and
  5.54): which key the original samples there.
- **The trace's clip id in the gait blend** (record `+0x20`): 410 where Coney reports the blend's leading clip (411,
  409) in `run_circle`'s steps 54-56; which clip of the blend the original stores.
- **The circling run's turn**: on `run_circle`'s step 75 Coney turns 16.92° against the original's 16.44°, which the
  camera's auto-follow then grows to 6.6°; whether the stick is turned by the camera of this update or the last.
