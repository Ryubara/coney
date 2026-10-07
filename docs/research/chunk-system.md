# Chunk system

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). No runtime claims. The
disc-side checks were run on the NTSC-U disc and are reported as counts only.

## Purpose

Most of the game's data files (levels, characters, objects, texture dictionaries, collision, sounds, string tables)
are **chunk containers**: a count followed by typed, sized chunks. The chunk system reads a container from a stream,
gives each chunk to the code that owns its type, and lets those owners assemble the chunks into game objects. It is
the loader every later milestone goes through; the roadmap's "Boot the engine" step is done when Coney can load any
WAD entry through it.

The original does this with **two global stacks** rather than a tree: raw chunks are pushed as they are read, and a
chunk's "on loaded" handler pops the chunks it needs (which were written before it in the file) and pushes the object
it builds. Handlers are found through a table indexed by chunk type.

## Original structure

`c:/Warriors/Source/Core/ChunkSystem.cpp`. The static-initialiser stub at `0x00144080` ends the translation unit
before it, so the file starts at `0x001440a0` (the stack helpers) and runs at least to `0x001446d0` (inferred). The
CRC helpers at `0x00143f68`-`0x00144050` belong to that earlier unit, the first of `Core/`, whose file name is unknown
([Open questions](#open-questions)). Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00143ea0` | `Crc32_BuildTable(table)` | fills the 256-word reflected CRC-32 table (polynomial `0xedb88320`) | confirmed (code) |
| `0x00143f00` | `Crc32_Buffer(table, data, n)` | CRC-32 of `n` bytes | confirmed (code) |
| `0x00143f68` | `Crc32_Hash(table, name)` | CRC-32 of a string, used by the WAD index (see [Name hashing](name-hash.md)) | confirmed (code) |
| `0x00143fd8` | `Crc32_Lowercase(table, name)` | lowercases a string in place | confirmed (code) |
| `0x00144050` / `0x00144080` | `Crc32_StaticInit` / its stub | builds the table at `0x005d91e0` before `main`; the stub ends the CRC unit | confirmed (code) |
| `0x001440a0` | `ChunkSystem_PopObject()` | pop from the object stack | confirmed (code) |
| `0x001440c8` | `ChunkSystem_PushObject(obj)` | push onto the object stack | confirmed (code) |
| `0x001440f0` | `ChunkSystem_PeekChunkType()` | type of the top chunk, `0x54` when empty | confirmed (code) |
| `0x00144120` | `ChunkSystem_PopChunk(expectedType)` | pop a chunk's data (the type argument is not checked) | confirmed (code) |
| `0x00144148` | `ChunkSystem_PushChunk(data, type)` | push a chunk | confirmed (code) |
| `0x00144180` | `ChunkSystem_LoadContainer(stream)` | read a flat container | confirmed (code) |
| `0x00144348` | `ChunkSystem_SetHandlers(type, onLoaded, readFromStream)` | replace a table record's handlers | confirmed (code) |
| `0x00144370` | `ChunkSystem_RetagAsNullPointer()` | handler: pop the top chunk and push it back as type `0x14` | confirmed (code) |
| `0x00144398` | `ChunkSystem_LoadGroupedContainer(stream)` | read a container of resource groups | confirmed (code) |
| `0x00154440` | `Stream_SkipBytes(stream, n)` | read and discard `n` bytes, 256 at a time | confirmed (code) |

The CRC table object is at `0x005d91e0`.

## Data

All values little-endian, as the EE reads them with plain 32-bit loads.

### The chunk type table {#chunk-type-table}

`0x0050b2e0` in `.data` (labelled `ChunkTypeNameTable` in our Ghidra project), 85 records of 12 bytes, indexed by
chunk type. Confirmed (code) at `0x00144180`:

```c
struct ChunkTypeRecord {
    const char* name;           // +0: debug name, also passed to the allocator as the allocation's name
    void (*onLoaded)(void);     // +4: called after the chunk is read (both paths); may be null
    void (*readFromStream)(Stream* s, const ChunkHeader* h);  // +8: reads the chunk itself; may be null
};
```

Record `0x54` is the terminator: name, `onLoaded` and `readFromStream` are all `0xffffffff`. So there are 84 real
types, `0x00` to `0x53`. Two records are filled in at run time (see
[Handlers registered at run time](#handlers-registered-at-run-time)).

The two handler kinds:

- **`readFromStream`** (the source map's field `b`): the chunk is not read raw; the handler reads it from the stream
  itself (in practice: a RenderWare stream read), builds the object and pushes it with `PushChunk` under a *result*
  type. Afterwards the loader skips whatever part of the chunk the handler did not read.
- **`onLoaded`** (the source map's field `a`): called after the chunk is on the stack, with no arguments. It pops its
  own chunk (and the chunks before it that it needs), fixes up pointers, builds an object, and either stores it in
  a global or pushes it onto the object stack.

The 84 records. "Result" means the type pushed by a `readFromStream` handler; "pops" lists the types an `onLoaded`
handler takes off the chunk stack, in the order it pops them. Evidence: confirmed (code) at the handler address for
the push/pop behaviour; the description of what the object *is* is inferred from the handler's file and the names.

| Type | Name | `onLoaded` (+4) | `readFromStream` (+8) | What happens |
| --- | --- | --- | --- | --- |
| `0x00` | Anim Rot Keyframes | | | raw; popped by `Anim Data` |
| `0x01` | Anim Pos Keyframes | | | raw |
| `0x02` | Anim Data | set at run time: `0x001045e0` | | pops `0x02`, `0x00`; builds an animation object (vtable `0x00534240`, keyframes at `+0x1c`); pushes it as an object |
| `0x03` | Collision Mesh | `0x00350580` | | pops `0x03`, `0x06`, `0x05`, `0x04`, `0x07`, `0x52`; writes the mesh's vtable into the 160-byte header; links index lists `+0x80`, grid `+0x78`, triangles `+0x88`, vertex buffer `+0x8c`, checked bitset `+0x94`; sets the enabled bit in every 10-byte triangle; clears the bitset; pushes the mesh as an object ([Collision](collision.md)) |
| `0x04` | Collision Triangles | | | raw |
| `0x05` | Collision Grid | | | raw |
| `0x06` | Collision Strings | | | raw; despite the name, the grid's triangle index lists (`u16` count, then `u16` indices), not text |
| `0x07` | Collision Vertex Buffer | | | raw |
| `0x08` | Character Data | `0x0016e258` | | pops `0x08`, `0x45`; resolves 722 slot indices at `+0x08` into pointers by popping objects (see [Fix-ups](#fix-ups)); pushes the character as an object |
| `0x09` | Character DFF Data | | `0x0017f2c0` | RenderWare clump read; result `0x0A`, `0x41` or `0x50` (below) |
| `0x0A` | Character Device OID | | | result type of a DFF read |
| `0x0B` | Texture Dictionary TID | | `0x001906e8` | RenderWare texture dictionary read; result `0x0B` |
| `0x0C` | Camera Rail Nodes | | | raw |
| `0x0D` | Camera Rail Header | | | raw |
| `0x0E` | Camera Animation | `0x00144370` | | retagged as `0x14` |
| `0x0F` | English String Table | `0x00386f50` (empty) | | raw; read by `StringTable/` when the level unloads or a language is chosen |
| `0x10` | French String Table | `0x00386f60` (empty) | | raw |
| `0x11` | Italian String Table | `0x00386f70` (empty) | | raw |
| `0x12` | Spanish String Table | `0x00386f80` (empty) | | raw |
| `0x13` | German String Table | `0x00386f90` (empty) | | raw |
| `0x14` | Null Pointer | `0x00144370` | | retagged as `0x14`: an untyped "pointer" result |
| `0x15` | Sector BSP Data | | `0x00197b30` | world sector read through the render device (slot `+0x170`); result `0x42` |
| `0x16` | World Header | `0x004124f8` | | pops `0x16`, `0x42`, `0x0B`; world object (vtable `0x00545bf0`) with sectors at `+4`, textures at `+8`; pushed as an object |
| `0x17` | Level Header | set at run time: `0x0040ce30` | | see [below](#handlers-registered-at-run-time) |
| `0x18` | The One World | | | raw |
| `0x19` | Particle Types | | | raw |
| `0x1A` | Particle Code Memory | | | raw |
| `0x1B` | Particle Vector Const | | | raw |
| `0x1C` | Particle Float Const | | | raw |
| `0x1D` | Particle Int Constants | | | raw |
| `0x1E` | Particle Trigger List | | | raw |
| `0x1F` | Particle Particle Types | | | raw |
| `0x20` | Particle Asm Debug | | | raw |
| `0x21` | Particle Source | | | raw |
| `0x22` | Game Object Instance | | | raw; popped by `Game Object List` |
| `0x23` | Game Object Definition | | | raw; popped by `Game Object List` |
| `0x24` | Game Object List | `0x0039ae48` | | pops `0x24`, `0x23`, `0x22`; object list (vtable `0x00545548`) with definitions `+0x10`, instances `+0x0c`; pushed as an object |
| `0x25` | Dynamic Obj DFF Data | | `0x0017f2c0` | as `0x09` |
| `0x26` | Static Obj DFF Data | | | raw |
| `0x27` | Level Header Obj List | | | raw |
| `0x28` | Chunk Bone Offsets | | | raw |
| `0x29` | Static Sounds | `0x0010f320` | | pops `0x29`; stored at `0x0059867c` and handed to the audio manager (`0x0010f900`) |
| `0x2A` | Renderware Texture Dic | | `0x00190770` | texture dictionary read through the render device (slot `+0x150`), made current and then cleared (slot `+0x160`); result `0x0B`; see [Graphics](graphics.md#loading-textures) |
| `0x2B` | Game Map | | | raw |
| `0x2C` | Path Grid | | | raw |
| `0x2D` | Path Nodes | | | raw |
| `0x2E` | Grid Connections | | | raw |
| `0x2F` | Grid Connections2 | | | raw |
| `0x30` | GBH Script | `0x00144370` | | retagged as `0x14` |
| `0x31` | Music | `0x0010f360` | | pops `0x31`; stored at `0x00598680`, handed to the audio manager (`0x0010f9d8`) |
| `0x32` | Palette Bitmap Raw Data | | | raw |
| `0x33` | Image Palettes | | | raw |
| `0x34` | Image Textures | | | raw |
| `0x35` | Texture Dictionary | | | raw |
| `0x36` | Subway Map | | | raw |
| `0x37` | Fonts | | | raw |
| `0x38` | Scene Data | `0x00353458` (empty) | | raw |
| `0x39` | SceneBip | | | raw |
| `0x3A` | SceneDyn | | | raw |
| `0x3B` | SceneAnimKeyFrames | | | raw |
| `0x3C` | SceneAnimStreamTypes | | | raw |
| `0x3D` | SceneAnimationData | | | raw |
| `0x3E` | MoveKeyFrames | | | raw |
| `0x3F` | MoveData | | | raw |
| `0x40` | PathData | `0x0024e720` | | pops `0x40`; fixes up the path graph in place (counts in its header, offsets turned into pointers) and stores it in globals around `0x00510584` |
| `0x41` | Object Device OID | | | result type of a DFF read |
| `0x42` | Object Device SID | | | result type of a sector read |
| `0x43` | Scene List | `0x00353460` | | pops `0x43`; copies `count` 24-byte records (count is the first word) into a new allocation and frees the chunk |
| `0x44` | Character List | `0x00178098` | | pops `0x44`; resource manager `+0x80` = chunk, `+0x84` = count (word 0), `+0x88` = records at chunk `+0x10` |
| `0x45` | Anim Range List | | | raw; popped by `Character Data`, which keeps word 0 and a pointer to `+4` |
| `0x46` | Object List | `0x00181170` | | as `0x44`, into resource manager `+0x8c`, `+0x90`, `+0x94`. Layout: [WAD contents](formats/wad-contents.md#object-list) |
| `0x47` | Preinstance Object | | `0x0017f2c0` | as `0x09` |
| `0x48` | Sound Command Data | `0x0010f3a0` | | pops `0x48`; stored at `0x00598684`, handed to the audio manager |
| `0x49` | Sound Material Data | `0x0010f3d8` | | pops `0x49`; stored at `0x00598688`; audio manager gets word 0 (count) and the records at `+0x10` |
| `0x4A` | Sound Anim Data | `0x0010f410` | | pops `0x4a`; stored at `0x0059868c` |
| `0x4B` | Light Glow Data | `0x0017e490` | | pops `0x4b`; word 0 is a count of 40-byte light records starting at `+0x10`; each becomes a light in the `LightManager` |
| `0x4C` | Particle Page | `0x00181b20` | | pops `0x4c`, `0x0B`; textures at `+0x10`, `+0x0c` = chunk `+0x14`; pushed back as `0x4c`. Layout: [GUI](gui.md#particle-page) |
| `0x4D` | Particle Page Header | `0x00182820` | | pops `0x4d`; resource manager `+0x9040` = chunk, `+0x9048` = word 0, `+0x9044` = chunk `+4`: the table of sprite sheets ([GUI](gui.md#sprite-sheet-table-chunk-0x4d-particle-page-header)) |
| `0x4E` | Anim List | `0x00178ee8` | | pops `0x4e`; resource manager `+0x98` = chunk, `+0x9c` = word 0, `+0xa0` = chunk `+4` |
| `0x4F` | Dependency List | `0x00178b30` | | pops `0x4f`; resource manager `+0xa4` = chunk, `+0xa8` = word 0, `+0xac` = chunk `+0x10`, `+0xb0` = 1; then loads the dependencies named for the CRC of `always` (`0x00178bc8`) |
| `0x50` | ImportCars | | | raw; also a result type of a DFF read |
| `0x51` | Subtitles | `0x001cab90` | | pops `0x51`; stored at `0x0050ea74` |
| `0x52` | Collision Checked | | | raw; popped by `Collision Mesh` |
| `0x53` | Occluders | | | raw; popped by `Level Header`, points converted to RenderWare axes ([Level loading](level-loading.md#occluders)) |

The DFF reader (`0x0017f2c0`) wraps the chunk stream in a RenderWare custom stream (`0x00197df0`), finds the clump
chunk (`0x10`) and reads a clump. It pushes the clump as `0x50` when it has two or more atomics, otherwise as `0x41`
or `0x0A` depending on a flag in the clump's game data (`0x00198428`, word `+4 == 1` means `0x41`). The texture
dictionary reader (`0x001906e8`) does the same with chunk `0x16` (RenderWare's texture dictionary). Confirmed (code)
for the chunk ids and result types; that the RenderWare calls are `RwStreamOpen`, `RwStreamFindChunk`,
`RpClumpStreamRead`, `RwTexDictionaryStreamRead` and `RwStreamClose` is inferred from their arguments and their place
in the RenderWare block.

### Container layout {#container-layout}

What the loaders read, in order. Confirmed (code) at `0x00144180` and `0x00144398` for the fields the code uses; the
other fields' meanings come from the disc check below and are inferred.

```c
struct ContainerHeader {      // 16 bytes, at the start of the file
    uint32_t count;           // flat: number of chunks; grouped: number of groups. The only field the code reads.
    uint32_t payloadBytes;    // inferred: sum of the chunks' `size` fields
    uint32_t zero;            // inferred: 0
    uint32_t id;              // unknown; often equals the first chunk's `id`
};

struct ChunkHeader {          // 16 bytes, before every chunk
    uint32_t type;            // index into the chunk type table, 0x00..0x53
    uint32_t size;            // bytes of chunk data that follow this header
    uint32_t zero;            // inferred: 0 in nearly all chunks
    uint32_t id;              // unknown; not read by the loaders
};

struct GroupHeader {          // grouped containers only, 16 bytes before each group
    uint32_t chunkCount;      // chunks in this group
    uint32_t unknown1, unknown2;
    uint32_t resourceId;      // the resource this group belongs to; 0 ends the container
};
```

A flat container is `ContainerHeader` then `count` × (`ChunkHeader` + `size` bytes). A grouped container is
`ContainerHeader` then up to `count` × (`GroupHeader` + `chunkCount` × (`ChunkHeader` + `size` bytes)). There is no
alignment or padding between chunks. Chunk data is copied into a 16-byte-aligned allocation, so data that needs
alignment gets it from the allocator, not from the file.

**Disc check (corroboration), NTSC-U, 10,701 WAD entries:** 7,590 entries start with a count between 1 and 5,000.
6,184 of them parse as flat containers with every chunk type `<= 0x53` and the chunks ending inside the entry. In
4,246 of those `payloadBytes` is exactly the sum of the chunk sizes and the container ends exactly at the end of the
entry: these are real chunk containers. The other 1,938 only look like containers: they are a 16-byte header
`{1, 0, 0, id}` followed directly by a **RenderWare stream** whose first chunk id is `0x16` (RenderWare's texture
dictionary) or `0x01` (RenderWare's struct), with the RenderWare version stamp `0x1C02000A` where a chunk header's
third word would be. They happen to parse as one chunk of type `0x16` or `0x01`. 1,911 of them are the streamed
world's sector-atomics files (`%s_ms%i.sec`), which the world loader reads directly as RenderWare streams, skipping
the 16-byte header (`0x004114d8`/`0x004110c0`, `World/ps2/WorldPS2.cpp`; see
[Graphics](graphics.md#loading-textures)); a stricter check finds exactly 1,911 such entries. Of the 1,406 entries
that do not parse flat, 1,259 parse as grouped containers. The other 3,111 entries are not containers (Lua bytecode,
text and the other kinds on [WARRIORS.DIR / .WAD](formats/wad-dir.md)).

**One classifier (2026-10-04)** settles these counts against [WAD contents](formats/wad-contents.md#kinds-of-entry).
Each entry was first given its kind from its name or its structure (streamed-world files by their now-known names,
packs by the marker `0xDE686795` plus a grouped parse, resources by an exact flat parse), and the loose parses above
were then run over every kind:

| Kind | Entries | Also parses flat (loosely) | Also parses grouped |
| --- | ---: | ---: | ---: |
| standalone resource (exact flat container) | 4,246 | 4,246 | 56 |
| pack (`.pak`) | 881 | 1 | 881 |
| world part (`_ms<i>.sec`) | 1,911 | 1,911 | 0 |
| world stream (`_sec.wld`) | 159 | 26 | 26 |
| world manifest (`_sec.mem`) | 159 | 0 | 0 |
| everything else | 3,345 | 0 | 379 |

So the 6,184 flat parses are 4,246 resources, 1,911 world parts, 26 world streams and 1 pack; the 1,938 that are not
exact are the last three groups (the "other 27" are 26 world streams, whose leading part count looks like a chunk
count, and 1 pack). The 1,259 grouped parses that are not flat are 880 packs and 379 entries of other kinds that
parse by chance. Only the marker, or the caller, tells a pack from a flat container reliably.

### The two stacks {#stacks}

| Stack | Data | Top index | Capacity | Evidence |
| --- | --- | --- | --- | --- |
| chunk stack | data pointers at `0x005da5f0`, types at `0x005dc5f0` (parallel arrays of 4-byte entries) | `0x0050b6d4`, initially -1 | 2,048 by the arrays' spacing; no bound check | confirmed (code); capacity inferred |
| object stack | object pointers at `0x005d95f0` | `0x0050b6d0`, initially -1 | 1,024 by spacing; no bound check | confirmed (code); capacity inferred |

Push pre-increments the index, pop post-decrements it. `PopChunk` takes the expected type as an argument but does
not check it (the check was compiled out; inferred), so a container whose chunks are out of order corrupts objects
silently. `PeekChunkType` returns `0x54` (the terminator's index) when the stack is empty.

## Behaviour

### Loading a flat container

`ChunkSystem_LoadContainer(stream)` (`0x00144180`). The stream is any object with the file interface on
[File I/O](file-io.md#file-interface): `Read(buf, n)` (slot `+0x50`) and `Tell()` (slot `+0x68`). Confirmed (code):

```text
read ContainerHeader (16 bytes)
repeat header.count times:
    read ChunkHeader h (16 bytes)
    rec = table[h.type]
    if rec.readFromStream is null:
        data = allocate(h.size, align 16, tag "u8", name rec.name)   # ChunkSystem.cpp line 0xfe
        read h.size bytes into data
        PushChunk(data, h.type)
    else:
        start = Tell()
        rec.readFromStream(stream, &h)
        if h.size - (Tell() - start) > 0: skip the rest
    if rec.onLoaded is not null: rec.onLoaded()
```

The allocation goes to the memory manager's current heap; callers choose the heap by pushing one before loading (for
example a level loads into its `World Level Pool`). A size of 0 still allocates (0 bytes) on this path.

After the loop, the caller pops what it wants. For example `WorldLevel` loading (`0x0040c688`) loads `<name>.lev` and
then pops the level object with `PopObject`; the `WorldManager` constructor loads `warriors.glr` the same way.

### Loading a grouped container

`ChunkSystem_LoadGroupedContainer(stream)` (`0x00144398`), used by the `ResourceManager` (`0x00187c38`) for resource
packs. Confirmed (code):

```text
read ContainerHeader
for each group (at most header.count):
    read GroupHeader g; if g.resourceId == 0: stop
    info = ResourceManager.Find(g.resourceId)                 # 0x00186110
    skip = not found, or already resident, or no room for info.size + 16 KB within 200 ms (0x001888f0)
    if skip or info.size == 0:
        for each chunk: read its header and discard its data, 4 bytes at a time
    else:
        ResourceManager.BeginGroup(info)   # 0x00186e88: a new heap of info.size bytes named after the resource, made current
        load g.chunkCount chunks exactly as above (a zero-size raw chunk pushes null instead of allocating)
        ResourceManager.EndGroup(info)     # 0x00186ff0: restore the heap, register the resource
```

The resource manager itself (what `Find` returns, how resources are freed) belongs on a graphics or resource page.

### Fix-ups {#fix-ups}

There is no general relocation table. Each `onLoaded` handler fixes up its own data. The patterns, all confirmed
(code) at the handlers above:

- **Links between chunks** come from stack order: a handler pops the chunks written before it in the file and stores
  their pointers in its object (`Collision Mesh`, `World Header`, `Game Object List`, `Level Header`).
- **Counted arrays**: many chunk payloads start with a 16-byte sub-header whose first word is a record count, with
  the records at `+0x10` (`Character List`, `Object List`, `Sound Material Data`, `Light Glow Data`,
  `Dependency List`); some use `+4` instead (`Anim List`, `Anim Range List`).
- **In-place pointer fix-up**: `PathData` turns offsets and indices inside its own chunk into pointers.
- **Index to object**: `Character Data` holds 722 (`0x2d2`) four-byte slots at `+0x08`. A slot holding
  `0xffffffff` is filled from the resource manager's defaults; a slot holding a small index `n` (`<= 0x2d1`) is
  replaced, together with every other slot holding the same `n`, by the next object popped from the object stack, in
  increasing `n` (`0x0016e258`). So the objects a character refers to are loaded before it, and pushed in index order.
- **Vtables**: handlers that turn a raw chunk into a C++ object write the vtable pointer into the chunk's first word
  (the file stores a placeholder there; inferred). Coney will build its own objects instead.

### Handlers registered at run time {#handlers-registered-at-run-time}

`ChunkSystem_SetHandlers` (`0x00144348`) overwrites a record's two handlers. It is called twice, both during
initialisation (confirmed (code)):

- `AnimationSystem` creation (`0x00104818`): type `0x02` `Anim Data` gets `onLoaded = 0x001045e0`.
- `0x0040cf18`: type `0x17` `Level Header` gets `onLoaded = 0x0040ce30`. That handler pops the `Level Header`, builds
  the level object (`0x0040cf40`), takes one object from the object stack (`+4`), then pops in order `0x42` (`+0x0c`),
  `0x0B` (`+0x08`), and three (`0x41`, `0x0B`) pairs into `+0x24`/`+0x20`, `+0x1c`/`+0x18`, `+0x14`/`+0x10`, then
  `0x53` occluders; links the three pairs and pushes the level as an object. The object is built in place on the
  48-byte `Level Header` chunk ([Level loading](level-loading.md#the-level-object)).

## Coney's implementation

Written from this page, in `src/core/`:

- `chunk_types.h`: the header layouts and the 84 type names (`chunkTypeName`), used for diagnostics only.
- `chunk_stacks.h` (`ChunkStacks`): the two stacks, one pair per load rather than globals. `popChunk(type)` and
  `popObject<T>()` check what they pop and fail with `coney::Error` (leaving the stack as it was) instead of
  corrupting memory; `peekChunkType` returns nothing on an empty stack where the original returns `0x54`.
- `chunk_system.h`: the handler table (`ChunkHandlerTable`, `setHandlers`), `loadContainer` and
  `loadGroupedContainer`. Every count and size is checked against what the stream holds before anything is
  allocated, a type at or above `0x54` is an error, and a `readFromStream` handler gets a window of exactly its chunk
  (`io::SubStream`), so it cannot read into the next one; what it leaves unread is skipped.
- Handlers implemented: the retag handler (`retagAsNullPointer`) on `0x0E`, `0x14` and `0x30`, which needs no
  other subsystem, and the two texture dictionary stream readers, `0x0B` (`0x001906e8`) and `0x2A` (`0x00190770`),
  in `src/platform/texture_dictionary.h` because they read through librw
  (`addTextureDictionaryHandlers`). Each finds the RenderWare section `0x16` in its chunk, checks the stream
  (`graphics::inspectTexDictionary`, `src/graphics/rw_stream.h`), reads it with librw and pushes the dictionary as an
  object under type `0x0B`. The original's `0x2A` reader also makes the dictionary current and clears it again; Coney
  keeps no current dictionary, so that step has no counterpart. The `0x4C` (Particle Page) `onLoaded` handler
  (`0x00181b20`, `src/platform/sprite_sheets.h`) pops the page and the dictionary before it and pushes a sprite sheet
  back under `0x4C`, and the `0x4D` (Particle Page Header) handler (`0x00182820`) pushes the sheet table back under
  `0x4D` ([GUI](gui.md#coneys-implementation)). Every other type stays on the chunk stack as a raw byte
  block until its subsystem is written. `ChunkStacks::takeChunks(type)` (Coney's own) lets a tool collect every
  result of one type once a load is over.
- The grouped loader loads every group: Coney has no resource manager yet to ask whether a group is resident.
- `coney --disc <disc> --load <entry>` loads an entry with these and prints a summary
  ([Building and testing](../guides/building.md#run-coney)). It reads an entry whose container header ends with
  `0xDE686795` (`crc32("package")`) as grouped, any other as flat, and if the flat parse fails, as grouped.

Run over every entry of the NTSC-U disc (2026-10-04; counts only): 6,183 load as flat containers, 881 as packs
(grouped, with the marker) and 2 as grouped containers without the marker; the other 3,635 are not chunk containers.
1,939 of the flat loads leave bytes after the container, close to the 1,938 header-plus-RenderWare-stream entries
described above (which parse as one chunk of type `0x16` or `0x01`); the disc check above counts 6,184 flat
containers. Neither difference of one has been looked into.

TODO for the analysts, found while implementing:

- What the original does with a `readFromStream` handler that reads past its chunk (`h.size - consumed` is then
  negative); Coney fails the load.
- Which caller decides that an entry is grouped (answered): the caller, by the kind of file. The resource manager
  reads `.pak` files grouped (`0x00187c38`); `WorldLevel_Load` (`0x0040c688`, `.lev`) and the `WorldManager`
  constructor (`0x0040d900`, `warriors.glr`) read flat. The world files are not chunk containers at all
  ([The streamed world](world.md)). Coney's choice by the package marker finds the same 881 packs (see the
  classifier above).
- Names for the two texture chunk readers (answered): no string names them, so the names Coney's `@orig` tags use,
  `ChunkReader_TextureDictionaryTid` (`0x001906e8`) and `ChunkReader_RenderwareTextureDic` (`0x00190770`), after
  their chunk types, are the research names too (the local Ghidra project carries them). Their file: they are the
  last two functions before `Graphics/WaterEffect.cpp` (`0x00190810`), after `Graphics/Texture.cpp`'s anchors, with
  no static-initialiser stub between; `Graphics/WarTexture.cpp`, whose path string sits between those two files'
  in `.rodata` and has no code reference, is the likeliest unit (speculative). Cite them as `(Graphics/unknown)`.

## Open questions

- What the container header's fourth word and the chunk header's fourth word (`id`) mean; neither loader reads them.
- What the sector-atomics header's `id` is (answered): the CRC-32 of the file's own name, `<world>_ms<i>.sec`; the 27
  other entries are 26 world streams and a pack ([above](#container-layout)).
- Why 147 container-like entries parse neither flat nor grouped.
- The `GroupHeader`'s second and third words.
- Which file holds the CRC helpers (partly answered): they are a unit of their own, `0x00143ea0`-`0x001440a0`: a
  table builder (`0x00143ea0`), a buffer CRC (`0x00143f00`), the two string hashes, and a static initialiser
  (`0x00144050`) that fills the one global table at `0x005d91e0` before `main`; the stub `0x00144080` ends it. It
  follows the last camera code and precedes `Core/ChunkSystem.cpp`, and is no camera code, so it is the first unit of
  `Core/` (inferred). No string names the file; cite it as `(Core/unknown)`.
