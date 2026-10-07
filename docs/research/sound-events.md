# Sound events

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`): Ghidra, and the disc's
scripts read with `coney-tools` (the `MATERIAL`, `SA` and `CfgChar` values).

## Purpose

Every place the game itself starts a sound during play, with its trigger, how the sound is chosen, where it plays and
how loud: animation sounds, footsteps, hits and contacts, cars, objects, pickups, the interface, the speech the game
says by itself, and the ambience and music of a level. The sound engine these calls go into (tasks, voices, 3D
volume, the music player, the speech-line rules) is on [Sound](sound.md); the data on [Audio data](formats/audio.md).

## The players {#players}

Gameplay sounds go through a few players over the [sound matrix](sound.md#sound-matrix), filled by the matrix
script's `NewMaterialSound` / `NewAnimSound` calls. The matrix's tables are indexed by the script globals `MATERIAL`
(material ids 1-190, the same ids as the collision triangles' [materials](collision.md#materials): 1 `NONE`, 2
`GLASS`, 5 `CONCRETE`, 8 `SHOE`, 9 `FIST`, 10 `TORSO`, 13 `CAR_HOOD`, 17 `HEAD`, 26 `HUMAN`, ...) and `SA`
(animation sound ids 0-161, [below](#anim-sounds)). A matrix entry has up to three columns, each a sound and a volume;
its alternatives play in turn. Every player below plays **positional, duckable** sounds (the play call's last
argument 1) at volume × the column's volume and pitch 1 unless stated. Confirmed (code):

| Player | Address | What it plays | Evidence |
| --- | --- | --- | --- |
| `Sound_PlayMaterialPair(vol, m1, m2, pos, default)` | `0x00110830` | entry `[m1][m2]`: column 2, then column 1 (column 1 at pitch `Random_Unit() × 0.2` when `m1` is 13 `CAR_HOOD`); no owner | confirmed (code) |
| `Sound_PlayMaterialPairPlain` | `0x00110940` | the same without the `CAR_HOOD` pitch | confirmed (code) |
| `Sound_PlayMaterialHit` | `0x00110790` | column 1 of `[m1][m2]` only | confirmed (code) |
| `Sound_PlayAnimSound(vol, id, pos)` | `0x00110a08` | column 1 of animation entry `id`; no owner | confirmed (code) |
| `Human_PlayAnimSound(h, id, pos)` | `0x0021f548` | animation entry `id` at the human, owned by him: not a player: column 1 at its volume; a player: column 1 and column 2 at **2 ×** volume × the combat factor (column 2 unowned), and column 3 the same while the camera is in combat framing | confirmed (code) |
| `Human_PlayImpactSound(vol, h, owner, victim, m1, m2, default)` | `0x00220ac8` | entry `[m1][m2]` at `h`, owned by `owner`; with a `victim` knocked down, `TORSO` (10) and `HEAD` (17) on either side become 159 `TORSO_PRONE`; owner not a player: column 1; a player: column 1 and column 2 at 2 × vol × the owner's combat factor, and under combat framing also column 3 at a random pitch `1 ± n / 100`, `n` in 0-19 (the audio generator `0x006eb8b0`) | confirmed (code) |
| `Human_PlayFootstep(vol, pitch, h, bodyMaterial)` | `0x0021f290` | [footsteps](#footsteps) | confirmed (code) |
| `Human_SayAnimLine(h, id, overLine, cut)` | `0x0021f410` | animation entry `id`'s column 1 as the human's **speech line** ([vocal ids](#vocal-ids)) | confirmed (code) |

A material lookup whose second material is 0 or 1 uses the caller's `default` (5, `CONCRETE`, at every call site on
this page), and an empty entry falls back to `[m1][default]` ([Sound](sound.md#sound-matrix)).
`AudioManager_VolumeHook` (`0x00111650`) is empty: its factor stays 1.

**The combat factor** (`Human_GetSoundVolumeScale`, `0x0021e3a8`): game state `+0x24c` for a **player** while
`Camera_IsCombatFraming` (`0x00233c50`) holds, else 1. `+0x24c` is the first argument of `CfgBreathingSound`
(`Cfg_SetBreathingSound`, `0x0041d480`); `config_preload2.lua` passes 1, so the factor is 1 in the shipped game.
Combat framing: one player, and player 1 either locked on with a target or in the fight stance
(`Human_FightStanceMove`) with a target while the camera mode `0x00510228` is above 1, and not in a grab
(`+0xc4` empty). Confirmed (code).

**Female voices.** Human `+0x3b8` is copied at creation (`Human_Init`, `0x002184f0`) from the character class's
`+0x14b`, which is `CfgChar`'s 13th argument (`Cfg_SetCharacterClass`, `0x00228af8`). It is 1 for 61 of the 449
types, all of them women (`char_lcgf`, the Lizzies, `hook_01` ...): the code reads it only to pick the `_female`
animation sound for a vocal id. Confirmed (code); "female" is inferred from the types and the ids' names.

## The ground under a human {#ground-material}

Human `+0x1d8` is the material of the level triangle under him: `Human_SnapToGround` (`0x0023eab8`, each update's
1.5 m ray down from 1 m above the feet, [Characters](characters.md)) and the landing test (`Human_LandingTest`,
`0x0023e408`, a floor contact with `n.z` > 0.65) store the hit's material (`WorldManager_RayCast` result `+0x28`,
[Collision](collision.md)). A cast that hits nothing leaves it unchanged; it starts at 0 (`Human_Init`), which the
lookup treats as the default, `CONCRETE`. Confirmed (code).

## Animation sounds {#anim-sounds}

A clip event of type 11 sends the human message `0x8b` with the event's value ([Sound](sound.md#anim-sounds));
`Human_OnAnimSoundEvent` (`0x0021f700`) acts on that `SA` id. "At the human" is the human's position in the transform
table (`0x00714b00 + index × 0x20`). Confirmed (code) for every row.

### Footsteps and body falls {#footsteps}

`Human_PlayFootstep(vol, pitch, h, body)` plays `Sound_PlayMaterialPair` of `[body][ground]`, with `ground` the
[ground material](#ground-material) remapped first (`Sound_RemapFootMaterial`, `0x00110aa8`: 18 `TRAINRAIL` → 106
`GRAVEL`, 6 `ASHPHALT` → 5 `CONCRETE`, 121 `CARPET` → 35 `GRASS` at half volume). The volume is `vol` × the remap's
factor × the human's combat factor × **2 for a player**. For body material 8 (`SHOE`), a human hidden in shadow
(state `0x200000`, `Human_IsHiddenInShadow` `0x00228168`, the sneak) plays at **half** volume and without the
player's doubling; classes `0x77` and `0x78` use the plain pair (no difference for body 8).

| `SA` id | Name | Body material | Volume | Evidence |
| --- | --- | --- | --- | --- |
| 1 | `footstep` | 8 `SHOE` | 1 | confirmed (code) |
| 3 | `footstep_run` | 8 `SHOE` | 1.25 | confirmed (code) |
| 12 | `truekneedrop` | 34 `KNEE` | 1 | confirmed (code) |
| 13 | `hit_ground` | 26 `HUMAN` | 1 | confirmed (code) |
| 18 | `xhit_ground` | 10 `TORSO` | 1 | confirmed (code) |
| 37 | `jump_land` | 164 `FEET_LAND` | 1 | confirmed (code) |
| 40 | `light_hit_ground` | 161 `HUMAN_LIGHT` | 1 | confirmed (code) |
| 43 | `thrown_hit_ground` | 162 `HUMAN_HEAVY` | 1 | confirmed (code) |
| 67 | `light_land` | 163 `FEET_LIGHT` | 1 | confirmed (code) |
| 69 | `land` | 101 `FEET` | 1 | confirmed (code) |

### Fixed material pairs {#anim-pairs}

| `SA` id | Name | Plays | Evidence |
| --- | --- | --- | --- |
| 46 | `kick_to_body` | `Sound_PlayMaterialPair([8 SHOE][159 TORSO_PRONE])` at the human, volume 1, a player 2 | confirmed (code) |
| 65 | `kick_to_head` | `Sound_PlayMaterialPair([8 SHOE][17 HEAD])`, volume 1, a player 2 | confirmed (code) |
| 52 | `bottle_smash` | by the held object's material (its type's `+100`): 102 `BRICK` → `SA` 141 `brick_smash`, 60 `POOLBALL` → 142 `cueball_smash`, 37 `BOTTLE` → 52 itself, through `Human_PlayAnimSound`; when that plays nothing (no entry), the object's material against itself: not a player `Sound_PlayMaterialHit([m][m])` volume 1, a player `Sound_PlayMaterialPair([m][m])` at 2 × the combat factor. Nothing without a held world object | confirmed (code) |
| 86 | `head_wtand` | the held object's material `m` against 17 `HEAD`: not a player `Sound_PlayMaterialHit([m][17])`, a player `Sound_PlayMaterialPair([m][17])` at 2 × the combat factor | confirmed (code) |
| 62 | `stab` | `Human_PlayAnimSound(62)`, or `SA` 140 `bottle_stab` when the held object's `+0xc4` is hash `0xc68a017c` (the broken bottle, inferred) | confirmed (code); the bottle inferred |

### Sounds without a speaker {#anim-plain}

| `SA` ids | Plays | Evidence |
| --- | --- | --- |
| 4 `short_fabric`, 6 `long_fabric`, 15 `fabric_friction` | `Sound_PlayAnimSound` at the human: a player at volume 2; anyone else at 1 **only when his class `+0x11b` is 13** (`Human_IsClass13` `0x00223e20`: the bosses and big fighters, `CfgChar`'s third argument), else nothing | confirmed (code) |
| 5 `swoosh_big`, 8 `swoosh_sml`, 16 `boss_swoosh1`, 23 `swoosh_punch3`, 44 `weapon_swoosh`, 90 `boss_swoosh2`, 91 `boss_swoosh3`, 92 `boss_swoosh` | `Sound_PlayAnimSound`: volume 1, a player 2 | confirmed (code) |
| 50 `zoom_01` | column 1 by `PlaySound3DByHash` at its column volume, no owner, no doubling | confirmed (code) |
| 66 `fence_rattle_big`, 68 `fence_rattle_small` | column 1 owned by the human at its column volume (`AudioManager_PlayOwned`), no doubling: the fence climb | confirmed (code) |
| 83 `sprayloop` | stops the human's sound at `+0x168`, plays column 1 owned by him and keeps it there (`Human_SetSpeechHandle`) | confirmed (code) |
| 60 `uncuff` | `Human_StartGrabSound` (`0x0021b228`): starts the sound prepared in `+0x180` (`Human_PrepareGrabSound` `0x0021b148` prepares `SA` 60's column 1 at the grab partner, from `Human_StateUpdate` and `ContextActions_Pick`), or, when none was prepared, `Human_PlayAnimSound(60)` | confirmed (code) |
| 87 `stealth_hands`, 88 `stealth_baton`, 89 `stealth_knife` | while the human's sound at `+0x17c` is not alive: stop his line and play column 1 as a cutting speech line (`Human_PlaySpeechCutting`, volume 1), then `Player_UpdateHiddenLoopSound` (`0x002301f0`); otherwise `Player_StartPreparedLine` (`0x0021f208`) | confirmed (code) |
| 0 `none` | nothing | confirmed (code) |
| **every other id** | `Human_PlayAnimSound` (the default; listed explicitly for 2, 11, 26, 33, 74, 94, 118-128) | confirmed (code) |

### Vocal ids {#vocal-ids}

`Human_SayAnimLine(h, id, overLine, cut)` (`0x0021f410`) plays the entry's column 1 as the human's speech line
(`Human_PlaySpeech` `0x0021e400`, or `Human_PlaySpeechCutting` `0x0021e698` when `cut`), at the column's volume, ×
2 for a player. Nothing when the human cannot speak (`+0x199` clear) or the entry has no sound. While a line plays
(`+0x178` alive) it plays only when `overLine`, and then stops that line first. As a speech line it follows
[Speech lines](sound.md#speech): nothing during a cinematic. Where a female id is given, a human with
`+0x3b8` = 1 uses it instead.

| `SA` id (female id) | overLine | cut | Notes | Evidence |
| --- | --- | --- | --- | --- |
| 7 `whistle`, 149 `sleep_snore`, 150 `sleep_grunt`, 156 `puke_big`, 161 `puke` | yes | no | | confirmed (code) |
| 9 `grunt_pch` (109) | yes | no | | confirmed (code) |
| 22 `grunt` (108) | yes | no | | confirmed (code) |
| 59 `grunt_pain` → plays 22 `grunt` (108) | yes | no | the pain grunt is the `grunt` entry | confirmed (code) |
| 30 `grunt_land`, 54 `grunt_strain`, 72 `grunt_hit_face` | yes | no | no female variant | confirmed (code) |
| 39 `winded` (111) | yes | no | | confirmed (code) |
| 71 `grab_back_grunt` (112) | yes | no | | confirmed (code) |
| 101 `grab_grunt` (110) | yes | no | | confirmed (code) |
| 58 `die` (114) | yes | **yes** | only while no scene plays (game state `+0x410` clear) | confirmed (code) |
| 115 `expire` (116) | no | **yes** | only when no line is playing | confirmed (code) |
| 28 `grunt_x` (113) | yes | no | only after command 12 `pain` said nothing (below) | confirmed (code) |
| 32 `grunt_lightlift` (145) | no | no | only after command 152 `lightlift` said nothing | confirmed (code) |
| 57 `grunt_heavylift` (144) | no | no | only after command 151 `heavylift` said nothing | confirmed (code) |

### Speech-command ids {#command-ids}

These say a [speech command](sound.md#speech) through `Human_SayCommand(vol, h, command, callback, interrupt,
uncut, target, duckable)` (`0x002205e0`; `uncut` 0 plays it as a cutting line; it returns the line's handle). The
target for a look is the null handle (`0x006ebd30`) in every row. "Gesture" means only when `Ambient_MayGesture`
allows; "1.5 at a player" means volume 1.5 when the human's target is a player, else 1.

| `SA` id | Command (number) | Volume | Interrupt | Duckable | Condition | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| 28 `grunt_x` | `pain` (12) | 1.5 at a player | yes | yes | then the vocal line above when no line came | confirmed (code) |
| 29 `cmd_mount` | `mount` (14) | 1.5 at a player | yes | yes | gesture; never for a player while game state `+0x268` is positive | confirmed (code) |
| 32 `grunt_lightlift` | `lightlift` (152) | 1 | no | yes | then the vocal line | confirmed (code) |
| 35 `cmd_wave`, 97 `cmd_point` | `point` (15) | 1.5 at a player | yes | yes | gesture | confirmed (code) |
| 41 `agony` | `agony` (34) | 1 | no | **no** while the human has interrogation lines (`+0x590`-`+0x59c`, [Crimes](crimes.md)), else yes | | confirmed (code) |
| 56 `cmd_throw` | `throw` (10), or `throw2` (38) when the held object's type (`+0x86`) is 8 `TYPE_MOLOTOV` | 1 (3, below) | yes | yes | only with a target (a human or a flag) and brain `+0x2d4` clear | confirmed (code) |
| 57 `grunt_heavylift` | `heavylift` (151) | 1 | yes | yes | then the vocal line | confirmed (code) |
| 82 `cmd_rage` | `rage` (160) | 1 | yes | yes | | confirmed (code) |
| 95 `cmd_boss` | `roar` (167) | 1 | yes | yes | | confirmed (code) |
| 103 `cmd_kiyap` | `kiyap` (176) | 1.5 at a player | yes | yes | gesture | confirmed (code) |
| 139 `cmd_fire` | `onfire` (107) | 1 | yes | yes | only while the human burns (`+0x19b`) | confirmed (code) |
| 146 `sprayface` | `sprayface` (203) | 1 | yes | yes | | confirmed (code) |

`cmd_throw` in the level whose record `+0x04` is 31 at checkpoint 6, for a gang member (brain `+0x04` = 3): volume 3,
and with a target he cannot see (`Human_HasLineOfSight` `0x00222288`), instead the line
`vags/character/voices/<set>/warn/warn_o_01` of his voice set (`+0x3b0`) through `Human_PlaySpeech` at volume 4.

**`Ambient_MayGesture`** (`0x00291ed0`): true when a camera is within 30 m of the human and one of the first
*n* slots of his brain's reaction kind (two handles per kind at `0x006cddf8`; *n* is the brain's help level) is free:
empty, or its human not speaking (`+0x178` dead). The human then takes that slot. So at most two humans of a
reaction kind make these gesture lines at a time. Confirmed (code).

## Hits and contacts {#hits}

### A strike on a human {#strike-human}

A landed strike (`Strike_Contact`, `0x0021b290`, [Combat](combat.md#moving-strikes)) on a human with health plays its hit
sound in `Hit_ResolveBlock` (`0x00220df0`): `Human_PlayImpactSound(1, attacker, attacker, victim, strike, struck, 5)`,
nothing when the attacker's body `+0x1a0` has `+0x38` set. Confirmed (code):

- **`strike`** by the attacker's striking shape (its bone id `+0x31`, [Combat](combat.md#moving-strikes)) and the hit's
  **strength** `s` (0-3: the reaction's bits 4-5 from `Hit_PickReaction` `0x00266d00`, plus 1 when the attack's
  Anim Range flags (`+0x0e`, `0x00254e20`) have `0x600` or the attacker's anim id is 11, at most 3):
    - head (bones 5-16): 17 `HEAD`;
    - forearms and hands (17-27): `[111 JAB, 9 FIST, 112 BIGPUNCH, 112 BIGPUNCH][s]` (`0x00510190`);
    - shins and feet (28-33): `[8 SHOE, 8 SHOE, 113 BIGKICK, 113 BIGKICK][s]` (`0x005101a0`);
    - anything else: 0 (the default row).
    A boss-class attacker (class `+0x11b` 13) striking with `JAB`, `FIST` or `BIGPUNCH` uses 183 `BOSS_FIST`.
- **`struck`**: 108 `BAG` when the victim's class is 460 (`punchbag`); else 184 `DEAD` when he is down or dead
  (`Human_IsDownOrDead` `0x00227e60`); else 127 `BLOCK` when he blocks or ducks (or is a boss-class human with flag
  `0x10` of `+0xe0`); else 17 `HEAD` for a boss-class victim; else by the shape struck: 17 `HEAD` for the head
  (bones 5-16), 10 `TORSO` otherwise (the spine, or no shape).

Before that, `Strike_Contact` adds `Human_PlayImpactSound(1, attacker, attacker, victim, 26 HUMAN, 26 HUMAN, 5)` when
the attacker holds flag `0x1400000` (a charge or a thrown human, [Combat](combat.md)) or the attacker's body shape
has flag `0x2`. A friendly hit by a player on a gang member (brain `+0x04` = 3) who is idle and may gesture makes
him say `fuck_you` (88; interrupt, duckable) when his health is above 10 %.

### A strike on the level, an object or a car {#strike-object}

`Strike_Contact`, confirmed (code):

- **The level mesh** (no object): `Human_PlayImpactSound(116, h, h, 0, 9 FIST, triangle material, 5)` (the volume
  is clamped to 1 by the engine), unless the attacker holds `0x400800`, is turn-limited
  (`Human_IsTurnLimited4` `0x00227f68`, `Human_IsTurnLimited6` `0x00227f40`), has state `0x4000000`, or his anim
  id is 2, 4 or `0x1b2`. Plus a particle burst.
- **A world object**: `Human_PlayImpactSound(1, h, h, 0, 9 FIST, or 26 HUMAN when holding 0x1400000, the object
  type's material (+100), 5)`, then the object takes the hit (a glass pane breaks: [World
  objects](objects.md#pane-break)).
- **A car**: `Human_PlayImpactSound(v, h, h, 0, 9 FIST, or 26 HUMAN when holding 0x1400000, 13 CAR_HOOD, 5)` when
  the strike reached any part (the car's vtable `+0x104` mask is not 0), with `v` 1, or 0.5 when the attacker is a
  player (whose doubling in the player makes it 1 again). A window it breaks adds its glass sound ([Cars](#cars)).

### The body's contacts {#contacts}

`Human_OnContact` (`0x00219d50`), confirmed (code):

- **A thrown human against the level**: the player's throw (`Player_Throw` `0x0026dd08`) arms the victim's
  `+0x3bd`; his first level contact that comes while he holds `0x400800`, is turn-limited 4, has state `0x4000000`
  or plays anim 2 or 4 (and `+0x5b9` is clear and he is not turn-limited 6) plays
  `Human_PlayImpactSound(v, h, h, 0, 26 HUMAN, triangle material, 5)` with `v` 1 when he holds `0x400000`, else 0.5,
  and disarms it.
- **A world object of type 29** (`TYPE_MOVINGVEHICLE`): kills the human and plays
  `Sound_PlayMaterialPair(1, object material, 26 HUMAN)` at him.
- **Type 34** (`TYPE_CHATTERBOXTRAIN`): a third of his maximum health, once per object, and the same pair.

## Cars {#cars}

Confirmed (code):

- **Part damage** (`Car_OnHit`, `0x0038bea0`): a part among 4, 5, 10-14, 16, 18 and 20 whose damage crosses 0.5
  in this hit plays `Sound_PlayMaterialPair(1, 129 CAR_DAMAGE, 129 CAR_DAMAGE)` at the hit point.
- **Windows**: a part's first break (`Car_BreakWindow`, `0x0038a830`) sends message `0x3f` to the shared car-damage
  particle system ([Cars](cars.md)): kind 4 for windows 15, 17, 19 and 21, kind 6 for parts 6 and 7. Its
  glass shatter (`SubGlass_Update`, `0x003e4cb8`) plays `Sound_PlayMaterialPairAt` (`0x00117280`, volume 1) at the
  burst, before any camera culling of the effect: `[88 GLASS_SMALL][88 GLASS_SMALL]` for a small burst or one whose
  record `+0x0c` is `0x10001` (kind 4, the windows), `[2 GLASS][2 GLASS]` otherwise (kind 6).
- **Explosion**: an exploding hit prepares `vags/fire/car_explodes<n>`, `n` random 1-3, at the car
  (`Sound_PrepareAtByName`, kept at car `+0x12fc`), and `Car_DoExplode` (`0x0038ab50`) starts it.
  `Car_StartExplosionLaunch` (`0x0038b1f8`) plays `vags/vehicles/shocks_squeak_<n>` (`n` 1-3) at the car.
- **Loose parts** (`Car_UpdateLoosePart`, `0x00387f18`): a part's impact above 4 m/s plays
  `Sound_PlayMaterialPair(1 / (n + 1), 119 CAR_PART, triangle material)` (`n` the part's byte `+0x97`) and
  `(…, 120 CAR_PART_STREAM, …)`;
  each that plays nothing is replaced by the same material against 5 `CONCRETE`.
- `Car_PlaySound` (`0x0038d6d8`) is only scene event 71 on a car ([Sound](sound.md#scene-sound)).

## Objects {#objects}

World objects play material pairs of their type's material (`CfgObj`, type record `+100`) through
`Sound_PlayMaterialPair` or `Sound_PlayMaterialPairAt` (`0x00117280`: volume 1, default 5), and fixed sounds by
hash through `Sound_PlayAtDefault` / `Sound_PlayAtDefault2` (`0x00117190`, `0x00117238`: positional, volume and
pitch 1, duckable). Glass panes, doors and barriers are on [World objects](objects.md). Confirmed (code):

- **A dropped or thrown object hitting the level** (`WorldObject_OnImpact`, `0x003939a8`): the triangle's material
  is kept in the object's `+0xf4`; `n` counts its impacts (`+0x13c`). A plain bounce plays only for the first three
  and above 2 m/s: `[object][surface]` at volume `1 / (n + 1)`. A hit that breaks the object (its `+0x10d` or
  `+0x10e` is 0) plays `[object][object]` at 1 (0.1 for material 86 `CHAIR` moving straight down), then
  `[object][surface]` at `0.5 / (n + 1)` with default 116 `BRICKWALL`, or, when that plays nothing, with default 5.
  A held or struck object that does not break plays `[object][surface]` with default 116, falling back to 5. The
  first sound's `far` (30 m without one) is the radius of the AI noise it makes.
- **An object touching another or a car** (`WorldObject_OnContact`, `0x00394050`): `[other][object]` at 1 and the
  object against 13 `CAR_HOOD` and 14 `CAR_HOOD_STREAM` at 0.5 (at 1 when the contact has no other object).
- **A thrown object** (`ThrownObject_OnContact` `0x00393538`, `ThrownObject_HitHumanTest` `0x003928d0`,
  `ThrownObject_HitHuman` `0x00392b88`): against an object `[thrown][struck]` at 1 (0.5 for the second pair);
  against a human `[object][26 HUMAN]` at 0.5 or 1, or through `Human_PlayImpactSound` at the human with 127
  `BLOCK` or 128 `WEAPON_BLOCK` when he blocks (0.25-1).
- **Debris and rubble** (`Rubble_Update` `0x003f4188`, `SubDebris_Update` `0x003c24e8`, `WoodSplinterBit_Update`
  `0x00407ae0`): `[piece][surface]` at 1 when a piece lands. `OverheadWeapon_Break` (`0x003ffe90`): the weapon's
  material against itself.
- **Breakables by class** (`Sound_PlayMaterialPairAt`): `DynCashreg`, `DynCbRadio`, `DynCtrlBox`, the breakable doors
  (`DynDoorBarBani`, `DynDoorBnstr`, `DynDoorFenceO`, `DynDoorParapet`, `DoorFence_OnHit`), `DynLizzies`,
  `DynMasks` (with 88 `GLASS_SMALL` pairs), `DynTable`, `DynWoodbridge`, `DynBreakableLight` and the stained glass
  (`SubStainedGlass_Update` `0x003e5168`, as [car windows](#cars)): their material against itself, or against 5,
  35, 36, 106, 121 by the surface; `WorldObject_HitEffectsByModel` (`0x003a6f68`) against 106 `GRAVEL` and 103
  `CASHREG`.
- **Fixed sounds by hash**: the molotov (`0x1be68950`), the ride cart and train loops (`0x326071de`, `0x26ac304b`,
  `PartTrainSound_Update`'s table), urine spray (`0x1927f7ba`), rotating objects (`0x5adc8f1e`, `0xcd6754af`,
  `0x546e0515`), broken neon and lights (`0xf164dfe0`), the CB radio and walkie-talkie tables (`0x006f3190`,
  `0x005144e0`), lock A (`0xf4bad950`, `0xe108f888`), the fire plume, generator and subway sparks tables
  (`0x00514578`, `0x810e2b6f`, `0x005145d8`, `0x00514620`), the motorbike gang (`0x97116eb7`), car steam
  (`0xc1c45c2d`), `SubPla` (`0x813c858a`), a swinging object (`0x3fc9a583`); by name: blood splatter
  `vags/character/blood_02` (`BloodSplatter_Update` `0x003aeb78`), small fires `vags/fire/fire_small_loop`
  (`SubFadeFlame_Init` `0x003c7de0`), the level-51 bus `vags/cutscenes/l51/bus_idle_02` (`PartTruckSound_Update`
  `0x003fce60`). Swinging and sliding doors play their record's hashes (`Sound_PlayHashAt` `0x003a6ee8`; the store
  door's first hit `0xcf6586b2`).

## Speech the game says {#speech}

Besides the [animation ids](#command-ids) and the scripts' `SoundPlayCommand`, the game's own code says speech
commands through `Human_SayCommand` (`0x002205e0`, [Saying a speech command](sound.md#speech)) from 129 functions,
listed below with the commands (number and name, [Speech](../references/speech.md)), the volume and the flags: **I**
interrupt (cuts the speaker's current line; without it nothing is said while he speaks), **C** played as a cutting
line (`Human_PlaySpeechCutting`), **N** not duckable, **L** the speaker looks at a target. "Gesture" means only when
`Ambient_MayGesture` allows ([above](#command-ids)). Every call goes through the voice table, so a command the
speaker's voice set has no line for says nothing. The triggers are the functions' own (their pages: [AI](ai.md),
[Combat](combat.md), [Crimes](crimes.md)); a "variable" command is a parameter of the goal or action. Confirmed
(code) for every call's arguments.

| Address | Function | Commands (volume, flags) | Evidence |
| --- | --- | --- | --- |
| `0x002b6fa0` | `ArrestedGoal_Process` | within 4 m of a player, every 4 s, gesture: 126 `unarrest_help` from a gang member (brain 3), 72 `scared` from someone of a gang hostile to the arrested (1.5, -) | confirmed (code) |
| `0x002af278` | `AttackTargetGoal_Process` | 17 `cheer2` (1, IC) | confirmed (code) |
| `0x002af0b8` | `AttackTarget_Cleanup` | 88 `fuck_you` (1, ICL) | confirmed (code) |
| `0x002e3630` | `AvoidEnemiesGoal_Process` | variable (var, -) | confirmed (code) |
| `0x002e8858` | `BigBrawlerGoal_OnAttackWarning` | 11 `block` or 14 `mount`, 50 % each (1.5, IC) | confirmed (code) |
| `0x002e8bc8` | `BigBrawlerGoal_OnHit` | 8 `swear` (1, -) | confirmed (code) |
| `0x002e8f78` | `BigBrawlerGoal_Process` | 87 `kingohill` (1.5, IC); 17 `cheer2` (1.5, -C) | confirmed (code) |
| `0x002ee7b0` | `BigLedgeThrowerGoal_Process` | 87 `kingohill` (1.5, IC) | confirmed (code) |
| `0x002ed718` | `BigThrowerGoal_Process` | 17 `cheer2` (1.5, IC) | confirmed (code) |
| `0x0030bc68` | `BossRoofTactic_UpdateBanter` | 144 `cheer3` (1, IC) | confirmed (code) |
| `0x0028ca50` | `Brain_OnFoeDown` | the one who downed him, gesture, not wounded: 143 `whoop` or 15 `point`, 50 % each (1, 2 for a player, IC) | confirmed (code) |
| `0x002af928` | `Brain_Shout` | gesture, alternating: the brain's command (`+0x2c`, default 71 `engage`; 1.5 at a player), then for a cop brain 138 `chase_cop` (1, -) | confirmed (code) |
| `0x0028ac80` | `Brain_SpotPlayers` | 22 `spot` (1.5, ICL) | confirmed (code) |
| `0x002accc8` | `BumLogicGoal_Process` | 182 `bum_hire` (1, IC); 145 `bum_beg` (1, I) | confirmed (code) |
| `0x002ac458` | `Bum_GiveItem` | 148 `kicked` (1, IC) | confirmed (code) |
| `0x002ac5e0` | `Bum_TakeMoney` | 145 `bum_beg` (1, I); 147 `bum_beg_resp` (1, I); 2 `follow` (1, I) | confirmed (code) |
| `0x002acaf8` | `Bum_UpdateRadar` | 170 `bum_beg_deal` (1, I) | confirmed (code) |
| `0x002d6638` | `CallGangGoal_Process` | 169 `surprise` (1.5, IL); 22 `spot` (1.5, IC) | confirmed (code) |
| `0x002a81f8` | `CallPolice_OnCrimeSeen` | 9 `scream` (1, I) | confirmed (code) |
| `0x002a8190` | `CallPolice_SayOnce` | 127 `catch_rat` (1, I) | confirmed (code) |
| `0x002b1860` | `ChaseGoal_Process` | 23 `search` (1, -); 64 `copresistco` (1.5, I); 24 `give_up` (1.5, I) | confirmed (code) |
| `0x002b2410` | `ChaseSupportGoal_Process` | 23 `search` (1, -) | confirmed (code) |
| `0x002ffb30` | `CivilianBrain_OnEvent` | 205 `help_girl` (1.5, IC) | confirmed (code) |
| `0x0030eec0` | `ConfrontTactic_Process` | variable (1, -) | confirmed (code) |
| `0x002c61f8` | `CopInteractGoal_Process` | 192 `shake_ho` (1, I); 193 `shake_ho_resp` (1, I); 190 `shake_bum` (1, I); 191 `shake_bum_resp` (1, I) | confirmed (code) |
| `0x002d4fe8` | `CowerGoal_Process` | 9 `scream` (1, -L) | confirmed (code) |
| `0x002c8af0` | `DealerFleeGoal_Process` | 9 `scream` (1, I); variable (var, -) | confirmed (code) |
| `0x002c89a8` | `DealerFleeGoal_Start` | 9 `scream` (1, -) | confirmed (code) |
| `0x002c7248` | `DealerGoal_Say` | variable (1, ?); 95 `offer` (1, IL) | confirmed (code) |
| `0x002af670` | `EngageEnemyGoal_Start` | 71 `engage` (1, -) | confirmed (code) |
| `0x002678c8` | `Human_PlayGangDeathReaction` | 13 `near` (1, IC) | confirmed (code) |
| `0x00269b80` | `Hit_PlayBegReaction` | 73 `beg` (1, -C) | confirmed (code) |
| `0x00273a68` | `Tag_SayNearbyLine` | a nearby human (of 16, gesture, `+0x19a`) says the tag code's command (1, -) | confirmed (code) |
| `0x00274ee0` | `Revive_Finish` | 75 `revive_thank` (1, I) | confirmed (code) |
| `0x002752a0` | `Revive_Start` | 76 `revive_reasure` (1, I) | confirmed (code) |
| `0x00283a30` | `Player_UpdateMugHold` | 8 `swear` (1, I) | confirmed (code) |
| `0x002cfb90` | `InvestigateGoal_End` | not chasing, gesture: 24 `give_up`, or 184 `give_up_ne` (1, -) | confirmed (code) |
| `0x002d0100` | `InvestigateGoal_Process` | gesture: 183 `ex_alert` or 36 `alert` (1, I); 134 `nosearch` (1, I) | confirmed (code) |
| `0x002d1100` | `Riot_OnAttacked` | 17 `cheer2` (1, IC) | confirmed (code) |
| `0x002d11f8` | `Riot_AfterFight` | 88 `fuck_you` (1, I) | confirmed (code) |
| `0x002d2600` | `HarassGoal_Process` | 88 `fuck_you` (1, -); variable (1, -) | confirmed (code) |
| `0x002d42c0` | `PlaySpecialIdleGoal_Process` | variable (1, ICL); variable (1.5, ICL) | confirmed (code) |
| `0x002d5798` | `Hostile_Attack` | 17 `cheer2` (1.5, I) | confirmed (code) |
| `0x002d58b8` | `HostileGoal_Process` | 175 `hostile` (1.5, I) | confirmed (code) |
| `0x002d8028` | `PathScout_SayLine` | 23 `search` (1, -) | confirmed (code) |
| `0x002db918` | `MoveToUseFlagGoal_End` | 32 `avoid` (1, I) | confirmed (code) |
| `0x002dd210` | `DestroyItemGoal_Process` | variable (1, -) | confirmed (code) |
| `0x002dde50` | `DestroyCarGoal_Process` | variable (1, -) | confirmed (code) |
| `0x002e63e0` | `ShopkeeperGoal_OnPlayerAction` | 197 `collect` (1, I) | confirmed (code) |
| `0x002ea6f0` | `BigFighterGoal_StartBreak` | 34 `agony` (1.5, IC) | confirmed (code) |
| `0x002eabc8` | `BigFighterGoal_Process` | variable (1.5, IC); variable (1, -C); 71 `engage` (1, -); 14 `mount` (1.5, -C); 8 `swear` (1, -); 143 `whoop` (1.5, IC); 8 `swear` (1.5, -C) | confirmed (code) |
| `0x002ec780` | `BigFighterAGoal_Process` | variable (1.5, IC); 14 `mount` (1, -C); variable (1, -C); 71 `engage` (1, -); 8 `swear` (1, -) | confirmed (code) |
| `0x002ee3d0` | `BigLedgeThrowerGoal_PlayTaunt` | variable (1, IC) | confirmed (code) |
| `0x002ee608` | `BigLedgeThrowerGoal_MaybeTaunt` | 11 `block` (1, IC) | confirmed (code) |
| `0x002f2438` | `ShooterGoal_OnHit` | 12 `pain` (2, IC) | confirmed (code) |
| `0x002f28b8` | `StationaryShooterAGoal_Process` | 87 `kingohill` (1.5, IC) | confirmed (code) |
| `0x002f4c68` | `StationaryShooterBGoal_Say` | variable (1.5, -C) | confirmed (code) |
| `0x002f52e8` | `StationaryShooterBGoal_Process` | 87 `kingohill` (1.5, IC); 16 `cheer1` (1, -); 16 `cheer1` (1.5, -C) | confirmed (code) |
| `0x002f5988` | `StationaryShooterBGoal_OnHurt` | 40 `fall` (1, IC) | confirmed (code) |
| `0x002f7348` | `BigBullGoal_Process` | 8 `swear` (1, IC); 11 `block` (1, IC); variable (1, -C) | confirmed (code) |
| `0x002f8468` | `HideAndSeekGoal_Process` | 17 `cheer2` (1, -); 71 `engage` (1, -); 143 `whoop` (1.5, -C); 15 `point` (1, -C) | confirmed (code) |
| `0x002f8e70` | `LeftTurfGoal_Process` | 71 `engage` (1, -L) | confirmed (code) |
| `0x003069f0` | `Tactic_SayAckLine` | variable (1, -) | confirmed (code) |
| `0x0030d3b8` | `BossBirdieTactic_OnEvent` | 34 `agony` (1.5, IC) | confirmed (code) |
| `0x0031b768` | `ShadowTactic_Process` | 135 `shadow` (1, -) | confirmed (code) |
| `0x0031c840` | `TauntTactic_Process` | 17 `cheer2` (1, -) | confirmed (code) |
| `0x0031e770` | `VandalizeTactic_OnObjectBroken` | 143 `whoop` (1, I) | confirmed (code) |
| `0x003200c8` | `WanderTactic_TryPickUp` | 89 `riot` (1, -) | confirmed (code) |
| `0x00322050` | `ChargeSubTactic_Process` | 1 `attack` (1.5, IC) | confirmed (code) |
| `0x00322510` | `GuardSubTactic_Process` | variable (1.5, IC); 1 `attack` (1.5, IC) | confirmed (code) |
| `0x00322a40` | `LeaderSubTactic_Process` | 1 `attack` (1.5, IC) | confirmed (code) |
| `0x00323160` | `ThrowSubTactic_Process` | 10 `throw` (1.5, IC) | confirmed (code) |
| `0x002f9f78` | `FidgetAction_Start` | variable (var, -) | confirmed (code) |
| `0x002b3ab0` | `FightGoal_Process` | 17 `cheer2` (1, -C) | confirmed (code) |
| `0x00416530` | `Flag_ActivitySound` | variable (1, I) | confirmed (code) |
| `0x002bd808` | `FollowAndDefendGoal_Process` | variable (1, -) | confirmed (code) |
| `0x0041c230` | `GameState_AddGuardingCop` | 22 `spot` (1, IC) | confirmed (code) |
| `0x00165d40` | `Gang_SaySpotLine` | variable (1.5, IC) | confirmed (code) |
| `0x002dacf8` | `GoalMoveToExitFlag_Process` | variable (1, I) | confirmed (code) |
| `0x002ab4b8` | `Goal_ProcessMove` | 89 `riot` (1, -); variable (1, IL); 125 `mumble` (1, -) | confirmed (code) |
| `0x002a83f8` | `Goal_ReportCrime` | 33 `rat` (1.5, I); 129 `hurryup` (1, I); 84 `phone_cop` (1.5, I); 9 `scream` (1, I) | confirmed (code) |
| `0x002c4360` | `Goal_ReportCrimeB` | 22 `spot` (1, IL) | confirmed (code) |
| `0x002bb858` | `GrabTargetGoal_Process` | 9 `scream` (1.5, -) | confirmed (code) |
| `0x0022c730` | `Grab_Link` | 8 `swear` (var, -) | confirmed (code) |
| `0x002c8ff8` | `HTLDefenseGoal_Process` | 74 `holdline` (1, -) | confirmed (code) |
| `0x002cc990` | `HideGoal_Process` | 69 `hide` (1, -); 168 `hide_stealth` (1, -); 206 `hide_response` (1, -) | confirmed (code) |
| `0x00265f70` | `Human_ApplyPendingDamage` | the attacker, after a hit that causes no reaction, when his anim is 9 or `0x4c`, gesture: 60 % 17 `cheer2` (11 `block` for anim `0x4c`), else 8 `swear` (1, 1.5 when either is a player, -) | confirmed (code) |
| `0x0022ec18` | `Human_Arrest` | 25 `arrested` (1, I) | confirmed (code) |
| `0x00269f30` | `Human_BlockHit` | 11 `block` (var, -) | confirmed (code) |
| `0x00245920` | `Human_HandleMessage` | 34 `agony` (1, IC) | confirmed (code) |
| `0x0021f700` | `Human_OnAnimSoundEvent` | 12 `pain` (var, I); 152 `lightlift` (1, -); 34 `agony` (1, -N); 151 `heavylift` (1, I); variable (var, ?) | confirmed (code) |
| `0x0021ed28` | `Human_SayCopLine` | variable (1, I) | confirmed (code) |
| `0x00233890` | `Human_SayGoalLine` | variable (1, I) | confirmed (code) |
| `0x002207d0` | `Human_SayObjectLine` | 108 `cb_wear` (1, -); 110 `hat_wear` (1, -) | confirmed (code) |
| `0x00219238` | `Human_StartBurning` | 107 `onfire` (1, IC) | confirmed (code) |
| `0x00238db0` | `Human_Tag` | 37 `nopaint` (2, -) | confirmed (code) |
| `0x002c69b0` | `IssueWarningGoal_Process` | 32 `avoid` (1, IL); 36 `alert` (1, I) | confirmed (code) |
| `0x00412dc8` | `Item_TakeOwned` | variable (1, -) | confirmed (code) |
| `0x002c5160` | `LeaveAreaGoal_Process` | 46 `cop1034f` (1, IL) | confirmed (code) |
| `0x002ae2e8` | `MeleeGoal_ProcessTarget` | 71 `engage` (1, -) | confirmed (code) |
| `0x002da588` | `MoveToFlagGoal_Process` | 125 `mumble` (1, -) | confirmed (code) |
| `0x002c1858` | `PatrolGoal_Process` | 32 `avoid` (1, IL); 125 `mumble` (1, -) | confirmed (code) |
| `0x002a9508` | `PedInteractGoal_Process` | variable (1, I); 179 `question` (1, I); 180 `answer` (1, I); 181 `bye` (1, I) | confirmed (code) |
| `0x002aa760` | `PedReactionGoal_Process` | 9 `scream` (1, -); 9 `scream` (1, I) | confirmed (code) |
| `0x002aa598` | `PedReactionGoal_Start` | 9 `scream` (1, -); variable (1, I) | confirmed (code) |
| `0x002ad6c0` | `PeddlerGoal_Process` | variable (1, I) | confirmed (code) |
| `0x002fb8a0` | `PlaySoundAction_Update` | variable (var, I?) | confirmed (code) |
| `0x00303468` | `PlayerBrain_OnPrompt` | 146 `give_me` (1, -) | confirmed (code) |
| `0x002856b8` | `Player_UpdateMugging` | 39 `mugcop` (1, IC); 19 `mug` (1, IC); 31 `no_item` (1, IC); variable (1, I); variable (1, IC) | confirmed (code) |
| `0x00318130` | `PursueTactic_EndSearch` | 24 `give_up` (1, I) | confirmed (code) |
| `0x001f0420` | `RM_ChooseGangs_Render` | 17 `cheer2` (20, -) | confirmed (code) |
| `0x002d1c38` | `RiotGoal_Process` | 89 `riot` (1, I) | confirmed (code) |
| `0x002d1288` | `RiotGoal_TryPickFight` | 17 `cheer2` (1, IC) | confirmed (code) |
| `0x002e5fa0` | `ShopkeeperGoal_OnDisturbed` | 188 `cower` (1, I) | confirmed (code) |
| `0x002e6668` | `ShopkeeperGoal_Process` | 185 `store_greet` (1, I); 189 `dead_meat` (1, I); 137 `mug_grunt` (1, I); 187 `phone_gang` (1, I); 186 `store_chat` (1, I); variable (1, I) | confirmed (code) |
| `0x001140d8` | `Sound_PlayHumanCommand` | variable (1, ?NL) | confirmed (code) |
| `0x0021b290` | `Strike_Contact` | 88 `fuck_you` (1, I) | confirmed (code) |
| `0x002fb128` | `TauntAction_Start` | variable (var, -) | confirmed (code) |
| `0x002e7f50` | `TiredGoal_Process` | 150 `energy` (1.5, IC); 149 `tired` (1.5, IC); 8 `swear` (1, -) | confirmed (code) |
| `0x002e7c18` | `TiredGoal_StartBreak` | 34 `agony` (1.5, IC) | confirmed (code) |
| `0x002606e8` | `Uncuff_MashSuccess` | 67 `unarrest_thank` (1, I) | confirmed (code) |
| `0x00260ca8` | `Uncuff_Start` | 68 `unarrest_reasure` (1, I) | confirmed (code) |
| `0x002fb738` | `UsePhoneAction_Update` | variable (1, I) | confirmed (code) |
| `0x0041c338` | `WarChief_CheckCrewInRange` | variable (1, -) | confirmed (code) |
| `0x0041cc40` | `WarChief_SayCommand` | variable (1.5, IC); 3 `defend` (1.5, IC); 158 `scatter` (1.5, IC) | confirmed (code) |
| `0x00306040` | `WarriorBrain_OnPrompt` | 146 `give_me` (1, -) | confirmed (code) |
| `0x003052f0` | `WarriorBrain_Think` | 109 `cb_whine` (1.5, ICL) | confirmed (code) |
| `0x002bf118` | `WarriorVandalStealGoal_TryVandalise` | variable (1, -) | confirmed (code) |
| `0x002b52c0` | `WoundedGoal_Process` | 8 `swear` (0.75, I) | confirmed (code) |

Lines played by name or by voice-table hash, not by command, confirmed (code):

- **Gang warning barks** and the spot line: [Sound](sound.md#warning-barks), `Gang_SaySpotLine` (`0x00165d40`).
- **Banter** (overheard conversations of gangs under the HanginOut, Idle, MoveToFlag, TravelPath, UseFlag and Wander
  tactics, [AI](ai.md#tactic-kinds)): `Human_SayStateResponse(h, line, near, far)` (`0x002208f0`) takes the line of
  the speaker's **state-response** voice set (`+0x3b4`) for `statement` (20) or `response` (21), the line number
  being the gang's counter `+0x60c`, moved on after each response by the responder's line count
  (`Gang_CycleSpotLine`). With the listener nearer (human `+0x334`) than that line's `far` − 20 m and no other near
  banter line alive (one game-wide, handle `0x0065ff40`) it plays at volume 1; otherwise, with no far banter line
  alive (`0x0065ff48`), the `statement2` / `response2` line (77, 78), or the near line at 0.5 when there is none,
  plays when the listener is within `far` + 10 m. Both through `Human_PlaySpeech`, after stopping the speaker's line.
- **Bums at a flag** (`PedFlagGoal_Process`, `0x002a68b0`, flag activity 6, at a random interval):
  `vags/character/bums/bum_bum1_00<n>` or `bum_bum2_00<n>`, `n` random 0-5, volume 1.
- **Boss lines**: `BossBirdieTactic_Process` (`0x0030d208`: command 17's line of one of three numbers, volume 4),
  `InfoTactic_OnLeaderAnimStart` (`0x00315820`), `InfoTactic_Process` (`0x00315928`), `BossTactic_OnEvent`
  (`0x00309058`), `BigBrawlerGoal_Process`, `StationaryShooterGoal_Say` (`0x002f0930`) and `ShooterGoal_Say`
  (`0x002f2230`): fixed lines of those fights.
- **Idle dialogue** (`Human_PlayIdleDialogLine`, `0x00220430`, from `WarriorFollowTactic_Process`): the human's next
  line of a name built from his voice set, the level's number, the checkpoint and his counter `+0x196` (up to `+0x197`
  lines, wrapping to 1).
- **Scene and clip events** 12 and 70 (human messages `0x8c`, `0xc6`): a speech line by hash at volume 1; 14 and 71
  (`0x8e`, `0xc7`): a positional sound by hash at the human, replacing the sound in his `+0x168`; 69 (`0xc5`) stops
  that sound (`Human_HandleMessage`, `0x00245920`).

## Pick-ups, items and the interface {#items}

Confirmed (code). 2D sounds are `PlaySound2DByHash` / `ByName` with flags 0, 0 and duckable 1; interface cues are
`Sound_PlayInterfaceCue` (2D, flags `0x12`) by number from the `SoundCfgInterfaceSound` table
([Sound and music](../references/sound.md#interface-sound)).

| Event | Sound | Where | Evidence |
| --- | --- | --- | --- |
| A pick-up gives an inventory item (key, flash, spray can, money, loot, cuffs ...) | the item's pick-up sound (item `+0x24`, [Inventory](player-state.md#inventory)), 2D | `Human_PickUpObject` `0x0023bf00` | confirmed (code) |
| Taking from a weapon pile (types 17-23, 47) | cues 25-31 by pile type (17 `BRICKPILE` 25 ... 23 `POOLBALLPILE` 31; 47 `BEERPILEHEAVY` 27) **at the object**, positional | human message 3, `Human_HandleMessage` | confirmed (code) |
| Taking from a spray pile (type 44) | item 3's pick-up sound, 2D | the same | confirmed (code) |
| Mugging: the money taken, the pocket handed over | item 2's sound; the pocket item's sound, or item 3's or item 1's | `Player_UpdateMugging` `0x002856b8`, `Mug_HandOverPocket` `0x002334d0` ([Crimes](crimes.md)) | confirmed (code) |
| The mugging camera starts, ends | `vags/misc/mug_intro` (handle kept), then `vags/misc/mug_outro` after stopping it | `CamMug_Start` `0x00137e08`, `CamMug_Update` `0x00138b90` | confirmed (code) |
| Buying from a dealer | `vags/interface/powerup` | `DealerGoal_OnBuy` `0x002c74d8` | confirmed (code) |
| Using a flash; a revive completed | cue 23 | `Flash_Use` `0x00284280`, `Revive_Finish` `0x00274ee0` | confirmed (code) |
| Triangle on a context action (lock, key) | cue 24 | `ContextAction_Use` `0x0024d530`, `Player_TriangleAction` `0x002811f0`, `Player_UseItemCommand` `0x002843f8` | confirmed (code) |
| The car-stereo theft's turns, done | cue 34 per turn, cue 35 at the fourth | `Theft_UpdateStereo` `0x0027e908` | confirmed (code) |
| The lock-pick dial | `vags/misc/lock_spins_01` (shown), `vags/misc/click_01` (judged) | `LockPickDial_Show` `0x001b8428`, `LockPickDial_Judge` `0x001b8d38` | confirmed (code) |
| Rage full, rage starts, rage ends | `vags/interface/rage_indicator_02` once when the meter reaches its maximum; `vags/misc/rage_mode_06`; `vags/misc/rage_ends_01` | `PlayerHUD_Update` `0x00214138`, `GameState_PlayRageSound` `0x004194b8`, `RageSound_PlayEnd` `0x00419400` | confirmed (code) |
| Wanted level, money count, objective, announcement, hint | cues 2, 16, 17, 20, 21 | `HUD_SetWanted`, `HudMoney_Update`, `HUD_SetObjective`, `HUD_ShowAnnouncement`, `HintBox_Update` ([HUD](hud.md)) | confirmed (code) |
| A Warrior command chosen on the stick | cue 32 | `WarCommandDisplay_ReadStick` `0x001a7040` | confirmed (code) |
| The stopwatch's last seconds | `vags/interface/menu/menu_enter3`, once a second | `StopWatch_Update` `0x004233f8` | confirmed (code) |
| The "go" sign | `vags/misc/bleep27` while it shows, one at a time, not while paused | `GoSign_Render` `0x0019ed28` | confirmed (code) |
| A counter panel's bonus | `vags/interface/menu/bonuspart_01` | `CounterPanels_SetValue` `0x001c3280` | confirmed (code) |
| The angry breathing | `CfgBreathingSound`'s sound, 2D, its handle kept | `Breathing_Start` `0x00419030` | confirmed (code) |

## World one-shots {#world}

- **Wind gusts** (`WindManager_Update`, `0x00407318`): a gust starts at random (one chance in `0x00514794` × 60 per
  update); during it `vags/ambient/environment/wind_blows_<NN>` (`NN` 01-10 in turn) plays 2D when no player stands
  on [covered ground](#covered), the garbage is on and not paused, the level is not 51 at checkpoint 8, the last gust
  sound has ended and 20 s have passed since it started. Confirmed (code).
- Doors, glass, radios, props and other objects: [World objects](objects.md), [Radios](sound.md#radios).

## Covered ground {#covered}

Human `+0x5b7` is set while a **player** stands on a triangle with flag bit 5 (`0x20`; `Human_SnapToGround` through
`Human_SetCoverFlag5` `0x002195e0`, which also pauses the wind-blown garbage). It decides the ambient emitters'
listener filter (0: only players **not** on covered ground hear the emitter; 1: only players on it), the × 0.25 owner
duck of [Task update](sound.md#three-d) and the wind gusts. Confirmed (code); that bit 5 marks **indoor** ground is
inferred (level95's filter-1 emitters are room sounds such as `tMusic_01`, and the garbage stops).

## level95: music, ambience and conversations {#level95}

From `level95.lua`, `level95_coney.lua` and `level95_clubhouse.lua`, read with `coney-tools` (the bindings:
[Sound bindings](../references/bindings/sound.md)). Confirmed (code) for the scripts' calls:

- **Matrix and bank**: `SndLoadMatrix('sound')` in `Main`; the level bank is `sound` ([Banks](sound.md#banks)).
- **Music outside**: `SetupConeyEnvironment` (run when the player leaves the clubhouse by a door) sets the system
  music's moods (`SoundSetMusicTrack`: 0 `music/ambient_idle1` and `music/2moog1a_loop`, 1 `music/120f_bit_biz_1`,
  2 `music/2nd_trialloop_120b`) and turns it on (`SoundEnableSystemMusic(true)`); `level95_coney.lua` sets the
  same. So outside the music is the [system music](sound.md#music): a random track of the current mood, looping.
- **Music inside**: `SetupClubhouseEnvironment` turns the system music off; the music heard indoors is the
  `tMusic_01` / `tMusic_02` loop emitters (filter 1, name kind `music`, so ducked to 0.5 while a music channel
  plays). Opening the mission or subway menu plays
  `MenuTrack` looping at music volume 0.5 (the ambient track stopped); closing it stops the music and restores
  volume 1.
- **The ambient track outside** (`level95_coney.lua`'s `soundbox`): volume boxes call `soundbox.Enter` (message 3:
  stop the ambient track, `CfgSetOutdoorMode(false)`), `soundbox.Exit` (message 4: play the last outdoor track
  again, outdoor mode on) and, for outdoor boxes, `soundbox.EnterOutdoor` (play that box's track). The defaults are
  `vags/ambient/city/distant_traffic_loop` outside and `vags/ambient/city/inside_room1_loop` in rooms. Its setup
  also turns the effects on (`SoundEnableEffects(true)`, `SetNormalEffects`).
- **Ambient emitters**: `AddAmbientSoundEmitters` (from `RegisterObjects`) adds 66 emitters with
  `AddAmbientSoundEmitter2` (41 of mode 4, one-shots; 24 of mode 3, loops; one of mode 1; listener filter 1, heard
  only on [covered ground](#covered), for `tElectric01`, `tElectric02`, `tMusic_01` and `tMusic_02`, 0 for the rest)
  and `SetAmbientEmitterPositions` for `tDogs01`,
  `tDogs02`, `tENVIRO_02` and `tENVIRO_03`; `t_pinball_machine` (filter 1) is added and switched by
  `EnableAmbientEmitter(events.PinballSound, ...)` as Warriors play pinball. Every emitter follows
  [Ambient emitters](sound.md#ambient): range, plays, delays, mode and filter.
- **The clubhouse conversations** are scripted (`events`): for each cluster of Warriors with enough talkers,
  `events.ChatEvent` runs after a random 1-5 s; it picks a Warrior who says `SoundPlayCommand(h, 20 statement,
  'events.FinishChatEvent', false, ...)` (no interrupt) while the other looks at him, then the other says 21
  `response` with the same callback, which queues the next chat. At the pinball machine a random one of three says
  143 `whoop` or 202 `pinball`; at the workout cluster 201 `workout`. The lines come from the Warriors' voice sets
  (type 193, `warr_sw`, its `CfgChar` voice).
- **Outside**, the street humans' own speech: gang banter (above), the civilians' and cops' goal lines (the table
  above: `PedInteractGoal` 179 `question`, 180 `answer`, 181 `bye`, crime reports, cop shakedowns, bums) and the
  dealers.

## Open questions

- What the attacker's body `+0x38` is (it silences his hits).
