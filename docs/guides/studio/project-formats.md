# Coney Studio: project formats

**Status: design.** Nothing on this page is built. The files a Coney Studio project saves in formats of Coney's own:
the level file with its entity tree and prefabs, characters, missions, overrides and import sidecars. What each kind of
content is, and what Studio shows, edits and imports, is on [Content and formats](content-and-formats.md), which this
page completes; the project folder's layout is the editor experience design's ([#1055](issue:1055)) and the manifest
the [Lua SDK design](lua-sdk.md#manifest)'s.

## The project folder {#formats}

The project folder of [#1055](issue:1055) (`coney-project.toml`, `levels/`, `textures/`, `scripts/`, `.studio/`),
with the folders this page adds. Everything in it is the user's own; paths inside files are `project:/` or `game:/`,
never absolute.

```text
my-project/
  coney-project.toml        the manifest (#1055, lua-sdk.md#manifest), plus: the disc it was made against
  overrides.layout          texture and sound overrides of disc assets
  levels/<name>.layout      levels
  prefabs/<name>.prefab     prefabs: groups of entities placed many times
  missions/<name>.mission   missions
  characters/<name>.character   custom characters
  models/                   imported glTF and FBX files, each with its .import sidecar
  anims/                    clip files (glTF, FBX), with sidecars
  textures/                 PNG, with sidecars
  sounds/                   WAV, with sidecars
  scripts/                  Lua
  .studio/                  cache: imported engine forms, the import table, traces; never shared
```

## One statement format {#statement-format}

Levels, prefabs, missions, characters, overrides and sidecars share **one text format**: the sandbox layout's, one
statement per line, a keyword then `key=value` arguments, `#` comments ([The layout
format](../sandbox.md#the-layout-format)). Coney parses it today, a person can write it by hand, git diffs and merges it
line by line, and Studio writes it back keeping comments and the order of lines, so an edit and its undo give the same
file byte for byte ([#1055](issue:1055)). Each file starts with `format <kind> <version>`; a parser refuses a newer
version than it knows. JSON was the alternative (Coney has a reader, [`json.h`](repo:src/core/json.h)) and stays the
format of what machines exchange (`extract`'s indexes, the install manifest); for files people read and merge, the line
format wins.

## The level file {#level-file}

A sandbox layout is a level file of version 1; version 2 adds the statements below. Every statement keeps the layout's
units and axes: metres and degrees, the game's axes, z up.

| Addition | Arguments | Does |
| --- | --- | --- |
| On every entity | `id=` (stable, written by Studio), `parent=ID`, `name=` | Its place in the level's tree ([The entity tree](#entity-tree)); an id scripts and other entities refer to |
| Transform | `at=x,y,z`, `rot=x,y,z,w` (a quaternion) or `yaw= pitch= roll=`, `scale=x,y,z` | Every entity's full transform, relative to its parent; `heading=` and `yaw=` keep their meaning, so version 1 layouts read unchanged |
| `group` | an entity with a transform and nothing else | Unity's empty object: a parent to arrange and move children by |
| `instance` | `prefab=project:/prefabs/<name>.prefab`, the entity keys | A placed prefab ([Prefabs](#prefabs)) |
| `set`, `drop` | `INSTANCE/ID key=value ...`; `INSTANCE/ID` | A per-instance override of a prefab's entity, or its removal from this instance |
| `base PATH` | `game:/levels/<level>` | Plays the project level on top of a disc level ([How a level starts](content-and-formats.md#level-start)) |
| `edit TARGET` | `objs:<line>`, `call:<function>:<n>` or `nav:<area>`, then `move at= rot=` or `remove` | An override of a disc level's thing ([How a level starts](content-and-formats.md#level-start)) |
| `map PATH` | `project:/maps/<file>` | A whole map imported from Blender, its markers written as statements beside it |
| `mesh` | `model=project:/...`, `collide=none\|mesh\|hull`, collision settings | An imported static mesh |
| `faces` | `mesh=ID faces=LIST` and `material= climb= sneak= twosided= area=`, or `remove` | Hand edits to an imported mesh's collision, by triangle index ([What stays editable](content-and-formats.md#editable)) |
| `collider` | `shape=box\|sphere\|capsule\|hull\|mesh`, its sizes or `model=`, `material= climb= sneak= twosided= area=` | Collision only ([Colliders](content-and-formats.md#collision)) |
| Collision names | `material=GRASS`, `climb=player`, `sneak=yes` on shapes and colliders | Names in place of `flags=` and a numeric `material=`, which stay for the bits not named |
| `navarea` | `points=x,y;x,y;...`, `hole=` lines after it, `area=`, `flags=` | A walkable area of the path map |
| `navlink` | `from= to= kind=walk\|jump\|climb\|door` | A link between areas |
| Entities | `flag`, `path`, `flaglink`, `volume`, `object`, `emitter`, `door`, `glass`, `car`, `light`, `character`, `spawner`, `trigger` with their natives' arguments by name | The entities of [Level objects](content-and-formats.md#entities) |
| `spawn NAME` | `character=` a type number or `project:/characters/...` | The spawn point's character |
| `script` | `handler=NAME` and the entity keys | A script entity: a function the project's scripts define, called with its parent entity ([Script entities](#script-entities)) |

An example, in version 2:

```text
format level 2
title Yard fight
texture floor dark_grid.png

base game:/levels/level99
spawn start id=s1 at=12,4,0 heading=90 character=project:/characters/cj.character

mesh id=m1 model=project:/models/bench.glb at=14,6,0 yaw=30 collide=hull
collider id=c1 shape=box at=10,10,0 size=4,0.2,1.1 material=LOW_FENCE climb=anyone
box id=b1 at=20,0,0 size=6,6,0.1 rot=0,0.1305,0,0.9914 sneak=yes texture=floor

group id=g1 name=Gate at=16,12,0
flag fYardGate id=f1 parent=g1 at=0,0,0 heading=0
object id=o1 parent=g1 type=dyn_trashcan at=-1,-1,0 zone=0
character Rival id=h1 type=32 gang=2 at=18,12,0 heading=180
script id=sc1 parent=h1 handler=Yard.OnRival

instance id=p1 prefab=project:/prefabs/ambush.prefab at=30,5,0 yaw=90
instance id=p2 prefab=project:/prefabs/ambush.prefab at=40,-8,0
set p2/h3 at=0,2,0 type=33
drop p2/c2
```

### The entity tree {#entity-tree}

A level is a tree of **entities**, as Unity's Hierarchy is (the maintainer's choice; [#1055](issue:1055) draws it): any
entity may be the parent of any other, a child's transform is relative to its parent's and follows it, and the tree has
no folders by kind. Every statement that places something is an entity: primitives, colliders, meshes, walkable areas,
every entity kind of [Level objects](content-and-formats.md#entities), groups, prefab instances and scripts.

- **Full transforms on every entity**: position, rotation and scale, kept relative to the parent; the file stores
  them as the user set them, and Studio composes them down the tree.
- **At Play, the tree is flattened**: Coney composes each entity's world transform from its parents, then builds
  geometry and calls natives with the world values. A kind whose native takes no rotation or no scale uses the world
  position and, where it takes a heading, the world rotation's turn about z ([Level
  objects](content-and-formats.md#entities)); a scaled parent scales its children's positions and the geometry, never a
  human or an object of the game, which have no scale. Studio shows such a child's scale as unused rather than refusing
  the parent.
- **Order** is the file's: a child's line comes after its parent's, and Coney makes entities in file order, so a parent
  exists before its children. Script entities run later, once every entity is made ([Script
  entities](#script-entities)).
- **The base level beside the tree.** A loaded game level ([How a level starts](content-and-formats.md#level-start)) is
  a read-only branch at the bottom of the tree, as the editor experience design draws it ([#1055](issue:1055)): its
  parts are shown, selected and inspected, never stored in the file and never changed in place. The project's entities
  sit above it and may be parented to one of its entities (they then follow it, as a crate on a disc car). Choosing
  *Override* on one of its entities writes an `edit` statement, which shows among the project's entities.
- **What the base branch lists.** Without playing: the streamed world, the level file's parts (collision, path data,
  sky, occluders) and the placed objects of `<level>_objs.txt`, which Coney's own parsers read. Everything the level
  script makes (flags, paths, boxes, doors, glass, lights, cars, spawners, characters) only from the set-up trace of
  [Levels and worlds](content-and-formats.md#levels), which runs the level's set-up headless on the player's machine.

### Script entities {#script-entities}

A project's code is loaded from the manifest's `[scripts]`, and a level has no level script of its own
([Projects and the manifest](lua-sdk.md#manifest), [Play, step by step](lua-sdk.md#play)). A **script entity** joins
that code to a place in the tree: `script id=sc1 parent=h1 handler=Yard.OnRival` names a function the project's
scripts define. In step 5 of Play, after the project's own `levelStart` handlers, Coney calls each script entity's
function in file order with the handle of its parent entity, the level's name and the checkpoint, in the project's
state and with its instruction budget; a handler that does not exist is an error in the Console with the level line,
and Play goes on. A prefab's script entity is called once per instance, with that instance's entity, which is how an
ambush prefab's script finds its own fighters. This call is an addition to the Lua SDK's Play order, which that page's
entry points take in when it is built.

### Prefabs {#prefabs}

A **prefab** is a file of entities placed many times: any mix (five fighters, a spawn point, crates, colliders and a
script for an ambush), with its own tree.

- **The prefab file** is a level file's entity statements under `format prefab 1`, with ids local to the prefab and one
  root entity whose transform is the instance's origin. It holds no `base`, no `map` and no level settings (sky, fog,
  title).
- **An instance** is one `instance` line: the prefab's path and the instance's own id, parent and transform. Its
  entities' ids are `INSTANCE/LOCAL` (`p2/f3`), which scripts and the Hierarchy use.
- **Per-instance overrides** are `set INSTANCE/LOCAL key=value ...`, only the keys that differ from the prefab, and
  `drop INSTANCE/LOCAL` for an entity this instance leaves out; entities added to one instance are ordinary entities
  parented to one of its entities. Editing the prefab file changes every instance at once, except the keys an instance
  overrides.
- **Nesting.** A prefab may hold instances of other prefabs; an override path names each level (`p1/inner/h2`). Studio
  refuses a prefab that contains itself, directly or through another.
- **A stale override**, one whose entity the prefab no longer has, is listed when the level opens and skipped at Play,
  as an `edit` of a disc level is.
- **At Play** instances are expanded into plain entities, the overrides applied, then the tree is flattened as above.
  Names that must be unique in the game (a flag's name for `FindFlag`, a path's) get the instance id as a suffix unless
  the prefab marks them shared, and Studio warns where the 15 characters a flag keeps make two equal.

## The other files {#other-files}

| File | Statements | Example |
| --- | --- | --- |
| `.character` | `base TYPE`, `model PATH`, `texture PATH`, `slot ANIM_ID PATH#CLIP` (an anim set override), `scale=` | `base 30` then `model project:/models/cj.glb` |
| `.mission` | `level PATH`, `script PATH`, `checkpoint N spawn=ID setup=FUNCTION objective="..."` | `checkpoint 1 spawn=s1 setup=Start objective="Reach the gate"` |
| `overrides.layout` | `override GAME_PATH PROJECT_PATH` | `override game:/textures/warr_re_cv/warr_re_cv project:/textures/re.png` |
| `.import` sidecar | `importer`, `id`, `scale`, `bone SOURCE POSE_BONE`, `clip NAME keep=`, `node NAME collide=`, `loop START END`, `derived GAME_PATH` | `bone mixamorig:Hips 1` |

## What is never in a project {#never}

A file copied or converted from the disc (the export folder is outside the project,
[Export](content-and-formats.md#export)); a script listing or trace of a disc level; the game's text; anything under
`.studio/` in a shared copy. A derived file (an edited export, [Provenance](content-and-formats.md#import)) may sit in a
project on the player's machine and is left out of a package.
