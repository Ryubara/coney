# Entities

Every kind of thing *The Warriors* has at run time that a script can make, find or change: what the game calls it,
how a script refers to it, how many there can be, when it comes and goes, and which [reference lists](index.md) and
[script bindings](bindings/index.md) cover it. It is the map a script or mod author starts from, the part FiveM's
entity, ped, object and vehicle pages play for GTA V. The evidence for each fact is on the research page it links;
families of ids that have no list yet are under [Still to list](index.md#still-to-list).

## Kinds {#kinds}

*Refer by* is what a binding takes; *limit* is the most there can be at once. *Image* says how a list illustrates
the kind: **model render** (Coney renders the model, as for [characters](characters.md)), **icon render** (one 2D
icon of at most 64 × 64 that Coney renders from the disc), **swatch** (a colour drawn by the docs) or **none**.

| Kind | What it is | Made with | Refer by | Limit | Lists | Bindings | Image |
| --- | --- | --- | --- | --- | --- | --- | --- |
| <span id="human"></span>Human | Every person, the player included (GTA's ped) | `HuCreate`, a gang's spawner (`GangAddSpawner`, [Spawner states](spawner-states.md)) | handle | 60 | [Characters](characters.md), [Character models](character-models.md), [Speed classes](speed-classes.md), [Power classes](power-classes.md), [Warrior classes](warrior-classes.md), [Attack kinds and tables](attacks.md), [Hat fittings](hats.md), [Anim ids](anim-ids.md), [Speech](speech.md) | [Characters](bindings/character.md) | model render (done) |
| <span id="brain"></span>Brain | A human's AI: goal stack, action queue, target | with its human | the human's handle | 60 | [AI goal types](goal-types.md) | [AI](bindings/ai.md) | none |
| <span id="gang"></span>Gang | A group of humans with relations to other gangs | `GangCreate` | gang id, the slot 0-31 | 32 | [Gangs](gangs.md), [Spawner states](spawner-states.md), [Commands](commands.md#warrior-command), [Script events](script-events.md), [Rumble roster](rumble.md) | [Gangs](bindings/gang.md) | none |
| <span id="object"></span>World object | Props, weapons, hats, pick-ups, doors, icons: one class, its kind a `CfgObj` type | `ObjSpawn`, `SpawnDoor`; placed by the level | handle; its type by name or `ObjGetIndex` | 384 live | [Objects and weapons](objects.md), [Object groups](object-groups.md), [Doors](doors.md), [Object tints](tints.md), [Script enums](enums.md) | [World and objects](bindings/world.md) | model render (coming) |
| <span id="car"></span>Car | A parked car that can be damaged or wrecked | `CarSpawn` | handle | 18 | [Cars](cars.md) | [World and objects](bindings/world.md) | model render: Coney must load the `<type>_geo` clump (chunk `0x47`, no skin) and its dictionary through the Object List ([Cars](../research/cars.md#model)) |
| <span id="glass"></span>Glass pane | A breakable window pane | `SpawnBreakableGlass` | handle | 100 | [Glass types](glass-types.md) | [World and objects](bindings/world.md) | none |
| <span id="particle"></span>Particle system | Fire, smoke, sparks, strobes | `SpawnParticle` | handle | 1,400 | [Particle effects](particles.md) | [Effects and lighting](bindings/effects.md) | icon render: cut the type's rectangle from its sheet record and fit it to 64 × 64 (sprite traced for 58 types; [Particles](../research/particles.md#sprite-words)) |
| <span id="light"></span>Dynamic light | A point, spot, directional or ambient light | `SetLight` | its own light handle | not traced | [Lights](lights.md) | [Effects and lighting](bindings/effects.md) | swatch (done) |
| <span id="crime"></span>Crime | A reported offence the police answer: responders from a gang's dispatch spawner | `CrimeIsHappening`, `SpawnCustomCrime`; the game itself | crime type | one crime scene | [Crime types](crime-types.md) | [Levels and game state](bindings/level.md), [Configuration](bindings/config.md) | none |
| <span id="flag"></span>World flag | A named point with a heading: spawn point, waypoint, objective | `AddFlag` | handle; `FindFlag` by name | per level | [World flags](flags.md) | [World and objects](bindings/world.md) | none |
| <span id="path"></span>Path | A route of points AI humans follow | `AddPath` | a userdata | 32 | | [World and objects](bindings/world.md), [AI](bindings/ai.md) | none |
| <span id="box"></span>Volume, turf and player boxes | Trigger boxes; a gang's turf | `AddVolumeBox`, `GangAddTurfBox` | handle | per level | [Volume boxes](boxes.md) | [World and objects](bindings/world.md), [Gangs](bindings/gang.md) | none |
| <span id="trigger"></span>Trigger sphere | A radius around an object that reports entries | `TriggerSphereCfg` | its object's handle | one per object | | [World and objects](bindings/world.md) | none |
| <span id="zone"></span>Object zone | A group of placed objects switched on and off together | the level's placements | zone number | 255 | [Object zones](zones.md) | [World and objects](bindings/world.md), [Script flow](bindings/script.md) | none |
| <span id="camera"></span>Camera | Follow, locked, fixed, rail and scene cameras | `CameraCreateLocked`, `CameraCreateFixed`, `CameraCreateThird`, ... | handle | one of each shared kind per player; fixed, locked, third-person and scene cameras not traced | [Camera types and switches](cameras.md) | [Cameras](bindings/camera.md) | none |
| <span id="scene"></span>Scene | An in-engine cutscene or animation set | `ScenePreload` | scene id (index in the scene list `scene_list.cnk`) | 12 loaded | [Scenes and movies](scenes.md) | [Scenes and movies](bindings/scene.md) | none |
| <span id="movie"></span>Movie | A full-motion video | `PlayMovie` | name | one at a time | [Scenes and movies](scenes.md#movie) | [Scenes and movies](bindings/scene.md) | none |
| <span id="sound"></span>Sound and emitter | A one-shot or looping sound; an ambient emitter | `SoundPlay`, `SoundPreLoad`, `AddAmbientSoundEmitter2` | sound handle; emitter id | not traced | [Sound and music](sound.md), [Speech](speech.md) | [Sound and music](bindings/sound.md) | none |
| <span id="blip"></span>Radar blip | A mark on the radar for a human, object or flag | `HUDAddRadarMissionObjective`, `HUDAddRadarHuman` | the marked thing's handle | 128 per radar | [Radar icons and blips](radar-icons.md) | [HUD and menus](bindings/hud.md) | icon render: rectangle *n* of `part_page0`, fitted to 64 × 64 ([GUI](../research/gui.md#radar-icons)) |
| <span id="icon"></span>Spinning icon | The marker over a target: a world object of class `dyn_icon` | `HuAttachSpinningIcon`, `GangAttachSpinningIcon` | through its human or gang | one per human | [Objects](objects.md) (class `dyn_icon`) | [Characters](bindings/character.md), [Gangs](bindings/gang.md) | model render |
| <span id="weather"></span>Weather and screen effects | Rain, fog, film grain, fades | `StartRain`, `StartFog`, `ScreenQueueEffect` | none (global) | one manager per view | [Screen effects](screen-effects.md) | [Effects and lighting](bindings/effects.md) | none |

Level scripts keep the handles they get in tables of their own, such as `Objects.<name>` and `Lights.<name>`;
those are the scripts' names, not the game's.

## Handles {#handles}

A handle is a number: the low 16 bits a serial, the high 16 an index into one table of 2,816 entries that humans,
cameras, flags, boxes, cars, glass, world objects and particle systems share
([Tasks: handles](../research/tasks.md#handles)). `NilHandle` is the global for no handle. When a thing is removed
and its entry reused, the old handle stops resolving: bindings then do nothing or return their "none" value, so test
with `HuIsAlive` or `ObjIsAlive` before using a handle kept across frames. How bindings read a handle from Lua is in
the [bindings' conventions](bindings/index.md#conventions).

Not handles: gang ids, scene ids, zone numbers, light and sound handles, emitter ids and path userdata, each
private to its own bindings (table above). A brain is reached through its human.

## Classes {#classes}

Everything the game schedules shares one head (position, rotation, velocities, flags, update interval;
[the task object](../research/tasks.md#task-object)) and has its own class:

```text
task head (Task_Init 0x003a15c0)
├── human            vtable 0x0053f088   stepped by Humans_Update at 30 Hz
├── world object     vtable 0x005453a0   on the wheel
├── particle system  vtable 0x00545660   on the wheel
├── scene            vtable 0x005458c8   on the wheel
├── car              vtable 0x00544c08   on the wheel
├── glass pane       vtable 0x00544ed0
└── light task       vtable 0x00545138
not scheduled: world flag (0x00545e68), box (0x00545c48: volume, turf, player), camera (0x00535510)
```

The four "on the wheel" are the classes seen updating at run time; doors, weapons, hats and pick-ups are world
objects whose behaviour comes from their `CfgObj` class and type, not from subclasses
([Tasks: task classes and pools](../research/tasks.md#classes)).

## Pools and limits {#pools}

| Thing | Limit | Set by |
| --- | --- | --- |
| humans (and brains, per-player records, physics bodies) | 60 | fixed ([Characters](../research/characters.md#the-human-object)) |
| world objects alive | 384 | fixed |
| placed objects (spawn records) | the level's count + 500 | `CfgSetDatabaseSizes` |
| world flags | the level's count + 4 | `CfgSetDatabaseSizes` ([World flags](../research/flags.md#pool)) |
| volume, turf and player boxes | the level's counts | `CfgSetDatabaseSizes` |
| particle systems | 1,400 in play, 100 in the pause menu | fixed |
| cars / glass panes / scenes / light tasks | 18 / 100 / 12 / 28 | fixed |
| gangs / formations / paths | 32 / 41 / 32 | fixed ([AI: gangs](../research/ai.md#gang-record)) |
| AI goals / actions, all brains together | 170 / 100 | fixed ([AI](../research/ai.md#goals)) |
| spawned characters | 3-70 in the scripts | `SetSpawnMax` |
| handles | 2,816 | fixed |

## Lifetime {#lifetime}

- **Humans.** `HuCreate` takes a free slot of the 60; when none is free the game removes corpses (`CullCorpses`)
  and tries once more, else returns `NilHandle` ([Characters: creation](../research/characters.md#creation)).
  `HuDelete` frees the slot at once; `CullCorpses` clears the dead.
- **Placed objects** are spawn records. Each frame the object manager spawns the objects of enabled zones within
  **70 m** of a player and removes others ([Tasks: the play tick](../research/tasks.md#tick); the removal rule is
  not traced). `ObjSpawn` adds a record, `ObjDestroy` removes an object, `ObjEnableZone` switches a zone.
- **Level pools** (flags, boxes, spawn records) are made by `CfgSetDatabaseSizes`, the first call of every level
  script, and freed when the level unloads; nothing frees a single flag.
- **Everything else** lasts until its remove binding (`CarDestroy`, `KillParticle`, `CamDelete`, `SceneUnload`,
  `GangDelete`, ...) or the level's end.

## FiveM to Warriors {#fivem}

| FiveM | The Warriors | Lists | Bindings |
| --- | --- | --- | --- |
| Entity | any handle; the task object behind it ([Classes](#classes)) | | [Utilities](bindings/util.md) |
| Ped | [human](#human) | [Characters](characters.md) | [Characters](bindings/character.md) |
| Ped model | character type (`CfgChar` id) and its model | [Characters](characters.md), [Character models](character-models.md) | `SetCharacterModel` |
| Player | a human with a pad (player 1 or 2) | [Controls](controls.md), [Commands](commands.md) | [Characters](bindings/character.md), [Pad input](bindings/input.md) |
| Ped task, scenario | the [brain](#brain)'s goals and actions; a gang's tactic; world flags the AI uses | | [AI](bindings/ai.md) |
| Relationship group | [gang](#gang) (`GangMakeEnemies`, `GangMakeFriends`, `GangSetNeutral`) | [Gangs](gangs.md) | [Gangs](bindings/gang.md) |
| Object | [world object](#object) | [Objects and weapons](objects.md) | [World and objects](bindings/world.md) |
| Vehicle | [car](#car), parked; trains and moving vehicles are world objects (`ObjStartTrain`) | [Cars](cars.md) | [World and objects](bindings/world.md) |
| Weapon | a world object of a weapon class, held (`HuPlaceItemInHand`, `HuGiveWeapon`) | [Objects and weapons](objects.md) | [Characters](bindings/character.md) |
| Pickup | a world object of class `pickup_item` or `powerup_item`; inventory items | [Objects and weapons](objects.md), [Inventory items](inventory.md) | [Levels and game state](bindings/level.md) |
| Door | a world object (`SpawnDoor`, `DoorOpen`) | [Doors](doors.md), [Objects and weapons](objects.md) | [World and objects](bindings/world.md) |
| Blip | [radar blip](#blip) | [Radar icons and blips](radar-icons.md) | [HUD and menus](bindings/hud.md) |
| Marker | [spinning icon](#icon); the HUD's tutorial arrow (`HUDEnableInstArrow`) | [Objects and weapons](objects.md) | [Characters](bindings/character.md), [HUD and menus](bindings/hud.md) |
| Checkpoint (race) | none; a [trigger sphere](#trigger) or [volume box](#box) does the job. A Warriors checkpoint is a restart point (`SetCheckPoint`) | [Level starts](level-starts.md) | [Levels and game state](bindings/level.md) |
| Ptfx | [particle system](#particle) | [Particle effects](particles.md) | [Effects and lighting](bindings/effects.md) |
| Zone | [object zone](#zone), [turf box](#box) | [Object zones](zones.md), [Volume boxes](boxes.md) | [World and objects](bindings/world.md) |
| Interior | none: a level or one of its streamed sections | [Levels](levels.md) | [Levels and game state](bindings/level.md) |
| Camera | [camera](#camera) | | [Cameras](bindings/camera.md) |
| Cutscene | [scene](#scene), [movie](#movie) | [Scenes and movies](scenes.md) | [Scenes and movies](bindings/scene.md) |
| Sound | [sound and emitter](#sound) | [Sound and music](sound.md), [Speech](speech.md) | [Sound and music](bindings/sound.md) |
| Weather | [weather and screen effects](#weather) | | [Effects and lighting](bindings/effects.md) |
| HUD colour | the `CL` colours | [HUD colours](hud-colours.md) | [HUD and menus](bindings/hud.md) |
| Ped combat attributes | a character type's power class, Warrior class and attack, damage and range tables | [Power classes](power-classes.md), [Warrior classes](warrior-classes.md), [Attack kinds and tables](attacks.md) | [Configuration (Cfg)](bindings/config.md), [AI](bindings/ai.md) |
| Stat | the mission score's categories and events; the unlockable records progress sets | [Statistics](statistics.md), [Unlockables](unlockables.md) | [Levels and game state](bindings/level.md) |
| Text label | a string table key | [Text labels](text-labels.md) | [HUD and menus](bindings/hud.md) |
| Animation dictionary, clip | anim ids and clips | [Anim ids](anim-ids.md), [Animation clips](animations.md) | [Characters](bindings/character.md) |
| Wanted level, dispatch | [crime](#crime): police responders from dispatch spawners | [Crime types](crime-types.md), [Spawner states](spawner-states.md) | [Levels and game state](bindings/level.md) |
| Network, multiplayer | none: two players share one screen | | |

## Open questions {#open-questions}

- What spawns the light tasks, and the camera pool's size.
- The radius at which a placed object is removed again.
- Which bindings hand out serial-0 (spawn record) handles.
