# Global lists (`warriors.glr`)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`) and the NTSC-U disc's
`WARRIORS.WAD`. Disc checks made with `coney-tools extract` (2026-10-07), counts only.

## Purpose

The global resource `warriors.glr` holds the tables the resource manager keeps for the whole game: which resources
make a character or an object, which clips and sprite sheets can be loaded on demand, and which resources are always
resident. This page gathers their layouts in one place for an extractor; what the game does with each is on the
pages linked from each section. The resource's sound tables (chunks `0x29`, `0x31`, `0x48`, `0x49`) are on
[Audio data](audio.md). The WAD holds two different resources with this hash (the second has a smaller Static Sounds
chunk, otherwise the same lists).

## Original structure

Each list is a chunk whose handler pops it and keeps pointers in the resource manager (`0x0050cd4c`)
([Chunk system](../chunk-system.md#chunk-type-table)). Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00178098` | Character List handler | chunk `0x44`: manager `+0x80` chunk, `+0x84` count, `+0x88` records | confirmed (code) |
| `0x00181170` | Object List handler | chunk `0x46`: manager `+0x8c` chunk, `+0x90` count, `+0x94` records | confirmed (code) |
| `0x00178ee8` | `AnimList_OnLoaded` | chunk `0x4E`: manager `+0x98` chunk, `+0x9c` count, `+0xa0` records at chunk `+4` | confirmed (code) |
| `0x00178b30` | `DependencyList_OnLoaded` | chunk `0x4F`: manager `+0xa4` chunk, `+0xa8` count, `+0xac` records at chunk `+0x10`; then wants the `always` group | confirmed (code) |
| `0x00178bc8` | `DependencyList_SetGroup(hash, state)` | sets the state of every record whose group or resource is the hash | confirmed (code) |
| `0x00182820` | Particle Page Header handler | chunk `0x4D`: the sprite sheet table | confirmed (code) |

## Data

All values little-endian; a hash is a CRC-32 of a lower-case name ([Name hashing](../name-hash.md)).

### Character List (chunk `0x44`)

A 16-byte header whose first word is the count, then 32-byte records `{model name, character data, model, texture
dictionary, four sizes}`. Layout and uses: [Characters](../characters.md#files).

### Object List (chunk `0x46`)

A 16-byte header whose first word is the count, then 36-byte records `{type name, damaged model's record, model,
texture dictionary, second dictionary, four sizes}`. Layout and uses:
[WAD contents](wad-contents.md#object-list).

### Anim List (chunk `0x4E`)

A count word, then `{u32 hash, u32 size}` per loadable clip resource. Confirmed (code) for the layout at
`0x00178ee8`; that it lists the clips `SetDynamicAnimation` can load is inferred ([Animation](animation.md)).

### Dependency List (chunk `0x4F`)

A 16-byte header whose first word is the count, then 12-byte records:

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | u32 | the **group** hash: what wants the resource (`always` at load; on the disc mostly a model or dictionary name, so a resource lists its own dependencies) |
| `+0x04` | u32 | the **resource** hash |
| `+0x08` | u16 | state: written by `DependencyList_SetGroup` (1 = wanted) |
| `+0x0a` | u16 | loaded |

Confirmed (code) at `0x00178bc8` (the record size, the two hashes and the two half-words), `0x00178c48` (a binary
search by resource) and `0x00178d48` (the next wanted, unloaded record becomes a load request named `"%u"` of its
hash) ([Graphics](../graphics.md)). **Disc check (corroboration):** 9,223 records under 1,441 groups, padded to a
multiple of 16 bytes; every state and loaded word is 0 on the disc. Of the group hashes, the recovered names are
model (`dyn_beerbottle_geo`, 98 records) and dictionary (`dyn_crate_b_tex`, 71) names; 1,419 records' groups are not
named yet.

### Sprite sheet table (chunk `0x4D`)

A count word, then `{u32 size, u32 name hash}` per sprite sheet: [GUI](../gui.md#sprite-sheet-table-chunk-0x4d-particle-page-header).

## Coney's implementation

`coney-tools extract` (type `data`) writes each list as `data/global/<list>.json` with every hash in hex and its
recovered name ([python/src/coney_tools/extract_data.py](repo:python/src/coney_tools/extract_data.py)).

## Open questions

- What groups other than `always` the game asks the Dependency List for, and when.
