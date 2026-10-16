# Chunk container

The chunk container is the common layout of most entries of the PS2 archive `WARRIORS.WAD` (directory
`WARRIORS.DIR`) on the NTSC-U disc (`SLUS_212.15`). It holds typed blocks of data, the chunks. Every fact below was
read from the disc image and counted over the whole archive. All values are little-endian.

## Where it is used

Of the archive's 10,701 entries, 5,127 are chunk containers. They come in two shapes.

| Shape | Entries | Bytes | Holds |
| --- | ---: | ---: | --- |
| Flat container (one resource) | 4,246 | 194,058,208 | One resource header followed by its chunks |
| Grouped container (a pack, `.pak`) | 881 | 671,648,576 | A pack header followed by 2 to 351 resources, each laid out like a flat container |

The other 5,574 entries (scene records, Lua scripts, world streams, sector files, text, sound banks) use other
layouts. They are listed on the archive contents page.

Each container fills its entry exactly: the last chunk ends at the entry's last byte. No trailing bytes are present.

## Layout

### Resource header

A flat container starts with one resource header. Inside a pack, each resource starts with the same header.

| Offset | Size | Type | Meaning |
| ---: | ---: | --- | --- |
| 0x00 | 4 | u32 | Number of chunks that follow (1 to 1,190 on the disc) |
| 0x04 | 4 | u32 | Sum of the payload sizes of those chunks; the 16-byte chunk headers are not counted |
| 0x08 | 4 | u32 | Always 0 (all 43,143 resources) |
| 0x0C | 4 | u32 | Resource hash: the CRC-32 of the resource's name, not lower-cased, with no path or prefix |

### Chunk record

The chunks follow the resource header back to back. Each chunk is a 16-byte header followed by its payload.

| Offset | Size | Type | Meaning |
| ---: | ---: | --- | --- |
| 0x00 | 4 | u32 | Chunk type id, 0x00 to 0x53 (see the type table) |
| 0x04 | 4 | u32 | Payload size in bytes, not counting this header; always a multiple of 16 |
| 0x08 | 4 | u32 | Always 0 (all 109,094 chunks) |
| 0x0C | 4 | u32 | Chunk hash (see Hashes) |
| 0x10 | size | bytes | Payload |

The next chunk header starts at the payload start plus the payload size. The size field always locates the next
header, whatever the payload holds.

### Pack header

A pack (grouped container) starts with a 16-byte header, followed by its resources back to back.

| Offset | Size | Type | Meaning |
| ---: | ---: | --- | --- |
| 0x00 | 4 | u32 | Number of resources (2 to 351; 38,897 over all 881 packs) |
| 0x04 | 4 | u32 | Entry size minus 16 (the bytes after this header) |
| 0x08 | 4 | u32 | Always 0 |
| 0x0C | 4 | u32 | Always `0xDE686795`, the CRC-32 of the text `package` |

### Examples from the disc

| Entry | Size | Header | Contents |
| --- | ---: | --- | --- |
| `scene_list.cnk` | 66,400 | 1 chunk, payload sum 66,368 | One chunk of type 0x43, 66,368 bytes |
| `warriors.glr` | 635,488 | 9 chunks, hash `0xFA2E10B2` = CRC-32 of `warriors` | Types 0x31, 0x49, 0x48, 0x29, 0x44, 0x46, 0x4D, 0x4F, 0x4E in that order |
| `level1.lev` | 46,992 | 18 chunks, hash `0x07F844A5` = CRC-32 of `level1` | The level chunk sequence below |
| `accoustic.anm` | 7,856 | 2 chunks, hash `0xDA27C16D` = CRC-32 of `accoustic.anm` | 0x00 (7,728 bytes) and 0x02 (80 bytes) |
| `global.pak` | 599,968 | Pack of 11 resources | Texture dictionaries (one chunk 0x2A each) |
| `l11_c1.pak` | 27,264 | Pack of 3 resources | One texture dictionary and two animations |
| Entry 0 (unnamed) | 17,840 | 1 chunk, hash `0x3BB55F1C` | One texture dictionary chunk 0x2A of 17,808 bytes |

## Alignment

- Every archive entry starts at a multiple of 2,048 bytes in `WARRIORS.WAD`.
- Headers are 16 bytes and every payload size is a multiple of 16. So every header and every payload begins at a
  16-byte boundary from the start of its entry.
- Payloads that hold one RenderWare section (types 0x15, 0x2A and 0x47) are padded after the section with fewer
  than 16 zero bytes. The padding is zero in all 39,764 such chunks.

## Nesting

There are three levels: a pack holds resources, a resource holds chunks, and a chunk holds a payload. A flat
container is one resource on its own. Pack resources carry no extra framing between them. The same resource
(same hash) often appears in several packs.

| Fact | Value |
| --- | ---: |
| Resources in flat containers | 4,246 |
| Resources inside packs | 38,897 |
| Distinct resource hashes over both | 4,211 |
| Hashes that occur more than once | 2,300 |
| Most copies of one hash | 183 |
| Hashes whose copies differ in their chunk type lists | 34 |

## Chunk types

Type ids run from 0x00 to 0x53. Only 29 of these 84 ids occur on the PS2 disc. What each type holds is given in the
last column and under payload kinds below.

| Id | Chunks | Payload bytes | Payload size | Carried by |
| --- | ---: | ---: | --- | --- |
| 0x00 | 31,274 | 135,986,000 | 32 to 70,672 | `.anm`, character resources, packs |
| 0x02 | 31,274 | 2,501,920 | always 80 | `.anm`, character resources, packs |
| 0x03 | 64 | 10,240 | always 160 | `.lev` |
| 0x04 | 64 | 1,506,080 | 144 to 90,512 | `.lev` |
| 0x05 | 64 | 1,144,560 | 16 to 229,152 | `.lev` |
| 0x06 | 64 | 847,408 | 48 to 54,592 | `.lev` |
| 0x07 | 64 | 1,508,704 | 160 to 86,416 | `.lev` |
| 0x08 | 1,209 | 3,520,608 | always 2,912 | character resources (53 flat, 1,156 in packs) |
| 0x15 | 64 | 645,104 | 1,072 to 28,928 | `.lev` |
| 0x17 | 64 | 3,072 | always 48 | `.lev` |
| 0x28 | 2,370 | 1,289,280 | always 544 | skinned models (153 flat, 2,217 in packs) |
| 0x29 | 2 | 1,218,992 | 406,336 and 812,656 | the two global resources |
| 0x2A | 18,244 | 330,158,928 | 560 to 263,584 | texture dictionaries, `.lev`, packs |
| 0x31 | 2 | 71,776 | always 35,888 | the two global resources |
| 0x40 | 64 | 5,482,576 | 208 to 373,248 | `.lev` |
| 0x43 | 1 | 66,368 | 66,368 | `scene_list.cnk` |
| 0x44 | 2 | 34,784 | always 17,392 | the two global resources |
| 0x45 | 1,209 | 13,985,712 | always 11,568 | character resources |
| 0x46 | 2 | 101,280 | always 50,640 | the two global resources |
| 0x47 | 21,456 | 362,373,184 | 960 to 339,840 | models, `.lev`, packs |
| 0x48 | 2 | 2,464 | always 1,232 | the two global resources |
| 0x49 | 2 | 7,968 | always 3,984 | the two global resources |
| 0x4C | 1,335 | 135,600 | 48 to 5,968 | texture dictionaries with a sprite page |
| 0x4D | 2 | 9,248 | always 4,624 | the two global resources |
| 0x4E | 2 | 9,056 | always 4,528 | the two global resources |
| 0x4F | 2 | 221,408 | always 110,704 | the two global resources |
| 0x51 | 64 | 386,784 | 16 to 30,128 | `.lev` |
| 0x52 | 64 | 19,376 | 16 to 1,136 | `.lev` |
| 0x53 | 64 | 8,416 | 16 to 1,136 | `.lev` |

The ids that never occur on the PS2 disc are 0x01, 0x09 to 0x14, 0x16, 0x18 to 0x27, 0x2B to 0x30, 0x32 to 0x3F,
0x41, 0x42, 0x4A, 0x4B and 0x50.

### Payload kinds

| Ids | Payload on the disc |
| --- | --- |
| 0x2A | One RenderWare texture dictionary section (type 0x16, stamp `0x1C02000A`) in all 18,244 |
| 0x47 | One RenderWare clump section (type 0x10, stamp `0x1C02000A`) in all 21,456 |
| 0x15 | One RenderWare world section (type 0x0B, stamp `0x1C02000A`) in all 64 |
| All others | Game-specific records; their layouts belong to the pages for animation, collision, characters, levels, global lists and scenes |

### Resource shapes

The set of chunk types in a resource says what kind of resource it is.

| Chunk types | Flat | In packs | What it is |
| --- | ---: | ---: | --- |
| 0x47 | 1,324 | 17,570 | Model |
| 0x2A | 1,509 | 15,144 | Texture dictionary |
| 0x00, 0x02 | 564 | 2,051 | One animation (one pair) or a set of animations (several pairs) |
| 0x28, 0x47 | 153 | 2,217 | Skinned model with bone offsets |
| 0x2A, 0x4C | 576 | 759 | Texture dictionary with a sprite page |
| 0x00, 0x02, 0x08, 0x45 | 53 | 1,156 | Character: pairs of 0x00 and 0x02, then 0x45 and 0x08 last |
| 0x03 to 0x07, 0x15, 0x17, 0x2A, 0x40, 0x47, 0x51 to 0x53 | 64 | 0 | Level (`.lev`) |
| 0x29, 0x31, 0x44, 0x46, 0x48, 0x49, 0x4D, 0x4E, 0x4F | 2 | 0 | Global lists (`warriors.glr` and one unnamed copy) |
| 0x43 | 1 | 0 | Scene list (`scene_list.cnk`) |

All 64 level files hold the same 18 chunks in the same order: 0x53, then 0x2A and 0x47 three times in turn, then
0x2A, 0x15, 0x40, 0x52, 0x07, 0x04, 0x05, 0x06, 0x03, 0x17, 0x51.

The largest resource has 1,190 chunks: an unnamed character resource of 2,126,704 bytes with 594 animation pairs.

## Hashes

All hashes here are plain CRC-32 of a name. Unlike the archive directory, the name is not lower-cased and has no
`./ee_files/` prefix.

| Where | Hash of | Observed |
| --- | --- | --- |
| Level resource header | `level<N>` | all 64 `.lev` files |
| Global resource header | `warriors` | both global resources (`0xFA2E10B2`) |
| Animation resource header | the `.anm` file name as the tools spelled it | 440 of the 561 named `.anm` files match their lower-case archive name exactly |
| Pack header | `package` | all 881 packs |
| Chunk 0x00 in a one-animation resource | the same as the resource hash | 2,615 chunks |
| Chunk 0x00 in a multi-animation resource | `<anim>.anm.b.cnk`, the name of an intermediate file | 16,337 matched |
| Chunks 0x2A, 0x47, 0x4C, 0x43 | usually equal to the resource hash | 0x2A: 17,988 of 18,244; 0x47: 18,894 of 21,456; 0x4C: all 1,335; 0x43: 1 of 1 |
| Level chunks 0x03 to 0x07, 0x51, 0x52 | always 0 | all 64 levels |

## Fix-ups

The container carries no relocation or fix-up table. The 0x08 field of every header is zero, and nothing in the
container points into a payload. Pointer slots sit inside some payload types at fixed offsets. They hold a
placeholder on the disc and receive a real address once loaded.

| Type | Offset in payload | Value on the disc | Filled with once loaded |
| --- | --- | --- | --- |
| 0x4C (particle page) | 0x00 | `0x00421798` in all 1,335 | (not established) |
| 0x4C (particle page) | 0x0C | 0 in all 1,335 | the address of the payload's own offset 0x14 |
| 0x4C (particle page) | 0x10 | 0 in all 1,335 | the texture dictionary from the same resource |
| 0x08 (character data) | 0x00 | `0x00436b10` in all 1,209 | a fixed address in the executable |
| 0x08 (character data) | 0x08, 722 slots of 4 bytes | `0xFFFFFFFF` (832,598 slots) or a value from 0 to 0x2D1 (40,300 slots) | references to loaded objects, or a default |
| 0x02 (clip descriptor) | 0x00 | `0x0045de40` in all 31,274 | (not established) |

The constant leading words of 0x02, 0x08 and 0x4C are the same in every chunk of their type. Each lies within the
address range that the executable `SLUS_212.15` occupies in memory, so none of them carries data of its own.

The 0x45 (anim range list) payload starts with the count 722 (0x2D2) in all 1,209 chunks.

## Behaviour

| Situation | Outcome |
| --- | --- |
| A pack holds a resource whose hash is already loaded | The resource is not loaded a second time. The copy already in memory stays in use, unchanged, and the pack's copy is passed over. |
| Several copies of one resource hash differ (34 such hashes on the disc) | The copy in use is the one loaded earliest; a later pack holding another copy does not replace it while it stays loaded. |
| A pack resource is passed over | The pack's other resources load as usual. The passed-over copy takes no memory. |

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x00144180` | Read one resource's chunks from a flat container | Layout |
| `0x00187c38` | Load a pack from the archive by its name | Behaviour |

## Acceptance checks

- The PS2 archive holds 4,246 flat containers and 881 packs. Every one of them parses with the layout above and
  ends exactly at its entry's last byte.
- The packs hold 38,897 resources in all, from 2 to 351 per pack. Every pack header ends with `0xDE686795`.
- The headers' 0x08 field is zero in all 881 pack headers, 43,143 resource headers and 109,094 chunk headers.
- Every chunk payload size is a multiple of 16.
- In every resource, the header's size field equals the sum of its chunks' payload sizes.
- Only the 29 chunk type ids in the type table occur, with the counts given there.
- Every `.lev` file is one resource of 18 chunks in the order given. Its resource hash is the CRC-32 of its name
  without the extension.
- `scene_list.cnk` is 66,400 bytes and holds one chunk of type 0x43.
- `warriors.glr` is 635,488 bytes and holds 9 chunks, including a 0x29 chunk of 406,336 bytes.
- Every 0x2A payload is one RenderWare texture dictionary section, and every 0x47 payload one clump section, both
  stamped `0x1C02000A`.

## Open questions

- The second global resource (an unnamed entry of 1,041,808 bytes with the same hash and chunk list as
  `warriors.glr`, but a 0x29 chunk twice as large) has no recovered name. When the game uses it instead of
  `warriors.glr` is not established.
- The chunk hash of 0x02 (clip descriptor), and the rest of the 0x2A and 0x47 chunk hashes that differ from their resource
  hash, are not matched to names.
- What the leading words of 0x02 and 0x4C payloads become in memory is not established.
- How the values 0 to 0x2D1 in the 0x08 slots map to particular loaded objects is not confirmed.
- The scene list resource hash `0x38648458` is not matched to a name.
- The Xbox disc was not examined for this page. Whether its archive uses the same container is open.
- The entry points for the jobs of the other chunk types (string tables, global lists, collision, subtitles, scene
  list) belong to their own pages. They were not checked here.
