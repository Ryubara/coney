# Coney Studio: project formats

**Status: design.** Nothing on this page is built. The files a Coney Studio project saves in formats of Coney's own:
the level file with its entity tree and prefabs, characters, missions, overrides and import sidecars, and the `.cmod`
package a finished project is exported as. What each kind of
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
| On every entity | `id=` (stable, written by Studio), `name=`, `tags=a,b`, `parent=ID` | Its identity and its place in the level's tree; what scripts find it by ([Names and tags](#names-tags)) |
| Transform | `at=x,y,z`, `rot=x,y,z,w` (a quaternion) or `yaw= pitch= roll=`, `scale=x,y,z` | Every entity's full transform, relative to its parent; `heading=` and `yaw=` keep their meaning, so version 1 layouts read unchanged |
| `group` | an entity with a transform and nothing else | Unity's empty object: a parent to arrange and move children by |
| `instance` | `prefab=project:/prefabs/<name>.prefab`, the entity keys | A placed prefab ([Prefabs](#prefabs)) |
| `set`, `drop` | `INSTANCE/ID key=value ...`; `INSTANCE/ID` | A per-instance override of a prefab's entity, or its removal from this instance |
| `base PATH` | `game:/levels/<level>` | Plays the project level on top of a disc level ([How a level starts](content-and-formats.md#level-start)) |
| `edit TARGET` | `objs:<line>`, `call:<function>:<n>` or `nav:<area>`, then `move at= rot=` or `remove` | An override of a disc level's thing ([How a level starts](content-and-formats.md#level-start)) |
| `map PATH` | `project:/maps/<file>` | A whole map imported from Blender, its markers written as statements beside it |
| `mesh` | `model=project:/...`, `collide=none\|mesh\|hull`, collision settings | An imported static mesh |
| `faces` | `mesh=ID` (or `mesh=base`, the base level's collision) `faces=LIST` and `material= climb= sneak= twosided= area=`, or `remove` | Hand edits to an imported mesh's or the base level's collision, by triangle index ([What stays editable](content-and-formats.md#editable)) |
| `collider` | `shape=box\|sphere\|capsule\|hull\|mesh`, its sizes or `model=`, `material= climb= sneak= twosided= area=` | Collision only ([Colliders](content-and-formats.md#collision)) |
| Collision names | `material=GRASS`, `climb=player`, `sneak=yes` on shapes and colliders | Names in place of `flags=` and a numeric `material=`, which stay for the bits not named |
| `navarea` | `points=x,y;x,y;...`, `hole=` lines after it, `area=`, `flags=` | A walkable area of the path map |
| `navlink` | `from= to= kind=walk\|jump\|climb\|door` | A link between areas |
| Entities | `flag`, `path`, `flaglink`, `volume`, `object`, `emitter`, `door`, `glass`, `car`, `light`, `character`, `spawner`, `trigger` with their natives' arguments by name | The entities of [Level objects](content-and-formats.md#entities) |
| `spawn NAME` | `character=` a type number or `project:/characters/...` | The spawn point's character |

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
character Rival id=h1 tags=rivals type=32 gang=2 at=18,12,0 heading=180

instance id=p1 name=AmbushNorth tags=ambush prefab=project:/prefabs/ambush.prefab at=30,5,0 yaw=90
instance id=p2 name=AmbushSouth tags=ambush prefab=project:/prefabs/ambush.prefab at=40,-8,0
set p2/h3 at=0,2,0 type=33
drop p2/c2
```

### The entity tree {#entity-tree}

A level is a tree of **entities**, as Unity's Hierarchy is (the maintainer's choice; [#1055](issue:1055) draws it): any
entity may be the parent of any other, a child's transform is relative to its parent's and follows it, and the tree has
no folders by kind. Every statement that places something is an entity: primitives, colliders, meshes, walkable areas,
every entity kind of [Level objects](content-and-formats.md#entities), groups and prefab instances. Scripts are not
entities and are never attached to one: they belong to the project ([Names and tags](#names-tags)).

- **Full transforms on every entity**: position, rotation and scale, kept relative to the parent; the file stores
  them as the user set them, and Studio composes them down the tree.
- **At Play, the tree is flattened**: Coney composes each entity's world transform from its parents, then builds
  geometry and calls natives with the world values. A kind whose native takes no rotation or no scale uses the world
  position and, where it takes a heading, the world rotation's turn about z ([Level
  objects](content-and-formats.md#entities)); a scaled parent scales its children's positions and the geometry, never a
  human or an object of the game, which have no scale. Studio shows such a child's scale as unused rather than refusing
  the parent.
- **Order** is the file's: a child's line comes after its parent's, and Coney makes entities in file order, so a parent
  exists before its children.
- **The base level beside the tree.** A loaded game level ([How a level starts](content-and-formats.md#level-start)) is
  a read-only branch at the bottom of the tree, as the editor experience design draws it ([#1055](issue:1055)): its
  parts are shown, selected and inspected, never stored in the file and never changed in place. The project's entities
  sit above it and may be parented to one of its entities (they then follow it, as a crate on a disc car). Choosing
  *Override* on one of its entities writes an `edit` statement, which shows among the project's entities.
- **What the base branch lists.** Without playing: the streamed world, the level file's parts (collision, path data,
  sky, occluders) and the placed objects of `<level>_objs.txt`, which Coney's own parsers read. Everything the level
  script makes (flags, paths, boxes, doors, glass, lights, cars, spawners, characters) only from the set-up trace of
  [Levels and worlds](content-and-formats.md#levels), which runs the level's set-up headless on the player's machine.

### Names and tags {#names-tags}

Studio attaches no script to an entity (the maintainer's choice: Studio is not a general engine). A project's code is
the manifest's `[scripts]`, and a level has no script of its own ([Projects and the manifest](lua-sdk.md#manifest),
[Play, step by step](lua-sdk.md#play)). A script finds the entities it needs when a level starts, in its `levelStart`
handler, as the original's level scripts find flags by name ([Flags](../../research/flags.md#findflag)), and handles one
that is missing. So every entity carries what a script can look it up by:

- **An id**, `id=`, written by Studio, unique in the level and never changed by a rename or a move; inside a prefab
  instance it is `INSTANCE/LOCAL` (`p2/h3`), nested as deep as the instances are.
- **A name**, `name=` (or the statement's own name, as `flag fYardGate`), set by the user and shown in the Hierarchy;
  Studio warns when two entities share one, since a lookup by name then finds the first in file order.
- **Tags**, `tags=ambush,guards`, any number of short words. An instance's tags belong to the instance; the entities
  inside it keep the prefab's tags, and `set p2/h3 tags=...` overrides one instance's.

At Play, Coney keeps a table from each id, name and tag to the handles its natives returned, and the Lua SDK's natives
read it: one entity by id or name, every entity with a tag (an instance's members found from it), each answer the
handles the game natives take ([Assets by path](lua-sdk.md#assets) for the native family; the names are that page's).
"Every entity tagged ambush" is one call, and a script that finds none gets an empty list, never an error.

### Prefabs {#prefabs}

A **prefab** is a file of entities placed many times: any mix (five fighters, a spawn point, crates and colliders for an
ambush), with its own tree. A prefab holds no script: the project's scripts find its instances by name or tag
([Names and tags](#names-tags)).

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
| `.mission` | `level PATH`, `checkpoint N spawn=ID setup=FUNCTION objective="..."` (a function of the manifest's scripts) | `checkpoint 1 spawn=s1 setup=Start objective="Reach the gate"` |
| `overrides.layout` | `override GAME_PATH PROJECT_PATH` | `override game:/textures/warr_re_cv/warr_re_cv project:/textures/re.png` |
| `.import` sidecar | `importer`, `id`, `scale`, `bone SOURCE POSE_BONE`, `clip NAME keep=`, `node NAME collide=`, `loop START END`, `derived GAME_PATH` | `bone mixamorig:Hips 1` |

## What is never in a project {#never}

A file copied or converted from the disc (the export folder is outside the project,
[Export](content-and-formats.md#export)); a script listing or trace of a disc level; the game's text; anything under
`.studio/` in a shared copy. A derived file (an edited export, [Provenance](content-and-formats.md#import)) may sit in a
project on the player's machine and is left out of a package.

## The `.cmod` package {#cmod}

A finished project is exported as one file, a **`.cmod`** (the working name): a zip of the project folder with its
manifest at the root. This page owns what goes into one; how Coney loads it, by hand or at start as its manifest says,
and how mods depend on each other, are the Lua SDK design's ([The mods folder](lua-sdk.md#mods-folder)), whose loader
reads a project as a folder today: reading a `.cmod` there in place of its unpacked folder is the change that page
takes in. What goes in, from the formats on this page:

| In the project | In the `.cmod` |
| --- | --- |
| `coney-project.toml` | At the root, with a list of every file the package holds and its SHA-256, written by the export |
| `levels/`, `prefabs/`, `missions/`, `characters/`, `overrides.layout` | As they are: text files of the statement format, holding `project:/` and `game:/` paths only |
| `scripts/` | As they are |
| `models/`, `anims/`, `textures/`, `sounds/` with their sidecars | The engine forms the import made (`imported/`, from `.studio/imported/`, with the import table), which is what Coney loads; the sources and sidecars too unless the export is told to leave them out, so another user can open the project in Studio |
| `.studio/` (cache, traces, the user's layouts) | Nothing but the engine forms above |
| A derived file (an edited export of a disc asset, [Provenance](content-and-formats.md#import)) | Never: the export leaves it out and lists it |

**Nothing from the disc goes into a `.cmod`.** A package refers to the game's assets only by `game:/` path, resolved on
each player's machine against their own disc ([Paths](content-and-formats.md#paths)). The export enforces it: it
refuses a file marked derived, a file whose bytes match a disc asset it can resolve (a texture, sound or model hash
compared with the asset store's), and a trace or listing of a disc level, and names each. A `.cmod` with no `levels/`
is a scripts-only mod, one with no `scripts/` a level-only mod; both load.
