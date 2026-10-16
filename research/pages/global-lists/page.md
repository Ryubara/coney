# Global lists (warriors.glr)

The game-wide lists on the PS2 disc live in one resource of `WARRIORS.WAD`, `warriors.glr`. This page gives where it
is, which chunks it holds, and the record layouts of three of its lists: the character list (chunk `0x44`), the
object list (chunk `0x46`) and the dependency list (chunk `0x4F`). All values are little-endian. Every count comes
from the PS2 disc (the USA disc image, five languages).

## Finding the file

`WARRIORS.DIR` is a 16-byte header (u32 entry count, 10,701, then zeros) followed by 12-byte entries: u32 offset in
`WARRIORS.WAD`, u32 size, u32 name hash. The name hash is the CRC-32 of the lower-cased path `./ee_files/<name>`.

| Entry | Name | Directory hash | Offset in the WAD | Size | Resource hash |
| ---: | --- | --- | ---: | ---: | --- |
| 10,670 | (not recovered) | `0xE87E18D5` | 1,492,969,472 | 1,041,808 | `0xFA2E10B2` |
| 10,671 | `warriors.glr` | `0xF5C908B1` | 1,494,011,904 | 635,488 | `0xFA2E10B2` |

Both are ordinary resources: a 16-byte header (u32 chunk count, u32 sum of chunk data sizes, u32 0, u32 resource
hash) and chunks with 16-byte headers (u32 type, u32 data size, u32 0, u32 chunk hash). The resource hash
`0xFA2E10B2` is the CRC-32 of `warriors`. The two resources hold the same nine chunks in the same order; only chunk
`0x29` differs in size (812,656 bytes in entry 10,670, 406,336 in entry 10,671). The five list chunks (`0x44`,
`0x46`, `0x4D`, `0x4E`, `0x4F`) are byte-for-byte the same in both.

| Chunk | Data size | Chunk hash | Content |
| --- | ---: | --- | --- |
| `0x31` | 35,888 | `0xFADA6479` | sound table (not covered here) |
| `0x49` | 3,984 | `0xB536E6E3` | sound table (not covered here) |
| `0x48` | 1,232 | `0xB536E6E3` | sound table (not covered here) |
| `0x29` | 406,336 | `0xB536E6E3` | sound data (not covered here) |
| `0x44` | 17,392 | 0 | character list |
| `0x46` | 50,640 | 0 | object list |
| `0x4D` | 4,624 | `0x7CDA7744` | u32 count 576, then 576 records of 8 bytes, then 12 zero bytes |
| `0x4F` | 110,704 | 0 | dependency list |
| `0x4E` | 4,528 | `0x4B96AA98` | u32 count 564, then 564 records of 8 bytes, then 12 zero bytes |

## Hashes in the lists

Every name in the lists is a CRC-32 of a name as the game's data spells it (all names recovered are lower case, with
no path and no extension). The naming patterns seen:

| Kind | Hash of | Example |
| --- | --- | --- |
| model resource | `<name>_geo` | `car_osedan_geo` |
| texture dictionary resource | `<name>_tex` | `car_osedan_tex` |
| object list type | `<name>` | `car_osedan` |
| character list entry | `<name>` | `bopp_bo` |
| dependency list group | a level, scene or model name | `level86`, `objarena`, `dyn_trashcan_geo` |

## Character list (chunk 0x44)

A 16-byte header whose first word is the record count (543) and whose other three words are 0, then 543 records of
32 bytes; 16 + 543 x 32 is exactly the chunk's 17,392 bytes. The records are not sorted.

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | u32 | name hash of the character entry |
| `+0x04` | u32 | hash of the character resource (animations and character data; chunk `0x45` among them) |
| `+0x08` | u32 | hash of the skinned model resource (chunks `0x47` and `0x28`) |
| `+0x0c` | u32 | hash of the texture dictionary resource (chunk `0x2A`) |
| `+0x10` | u32 | size of the character resource as stored, header included |
| `+0x14` | u32 | size of the model resource as stored, header included |
| `+0x18` | u32 | a size slightly above the model's: model size + 479 in 511 records, + 487 in 21, + 478 in 5, + 475 in 3, and + 477, + 486, + 485 once each |
| `+0x1c` | u32 | size of the texture dictionary resource as stored, header included |

| Disc fact | Value |
| --- | ---: |
| records | 543 |
| distinct entry names | 541 (one name hash, `0xD41E7A61`, occurs three times) |
| distinct character resources | 52 (one of them, `0x9DA2E531`, is used by 249 records) |
| distinct models | 128, every one a standalone skinned model resource |
| distinct texture dictionaries | 507, every one a standalone resource |
| entry names recovered | 513 of 541 |

The names group by gang or role prefix: `civl` 190, `warr` 78, `dest` 31, `riff` 17, `cops` 16, `orph` 16, `rogu`
16, `hurr` 14, `hiha` 12, `lizz` 11, `bopp` 10, `elec` 10, and others.

| Entry name | Character resource | Model | Texture dictionary | Sizes |
| --- | --- | --- | --- | --- |
| `bopp_bo` | `0x768FB8E3` | `bopp_bo_geo` | `bopp_bo_tex` | 134,304; 93,568; 94,047; 66,992 |
| `bopp_lt` | `0x9F9C4E0C` | `bopp_lt_geo` | `bopp_lt_tex` | 157,232; 94,400; 94,879; 17,840 |
| `civl_hl_fb1` | `0xA444878B` | `lizz_lt_geo` | `lizz_hl_lt1_tex` | 136,640; 108,752; 109,231; 17,840 |
| `warr_cl_chase_fury` | `0xACDDC681` | `fury_bo_geo` | `fury_bo_tex` | 30,976; 100,432; 100,919; 66,992 |

## Object list (chunk 0x46)

A 16-byte header whose first word is the record count (1,406) and whose other words are 0, then 1,406 records of 36
bytes, then 8 zero bytes to the chunk's 50,640.

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | u32 | name hash of the object type |
| `+0x04` | u32 | 0, or the type-name hash of a related type |
| `+0x08` | u32 | hash of the model resource |
| `+0x0c` | u32 | hash of the texture dictionary resource |
| `+0x10` | u32 | 0, or the hash of a second texture dictionary |
| `+0x14` | u32 | size of the model resource as stored, header included |
| `+0x18` | u32 | a size above the model's: model size + 198 in 1,375 records, + 134 in 25, + 15,334 in 6 |
| `+0x1c` | u32 | size of the texture dictionary resource as stored |
| `+0x20` | u32 | size of the second texture dictionary, 0 when there is none |

| Disc fact | Value |
| --- | ---: |
| records | 1,406 |
| distinct type names | 1,405 (`dyn_wok` occurs twice) |
| type names recovered | 1,365: 1,358 begin `dyn_`, 6 begin `car_` |
| records with a related type at `+0x04` | 136 (129 of them name another record of the list) |
| distinct models | 1,262, every one a standalone model resource |
| distinct texture dictionaries at `+0x0c` | 750, every one a standalone resource |
| records with a second texture dictionary | 33 (7 distinct dictionaries, among them `refmap_a_tex`, `dynremi6_tex` and `dyn_van_colors_wrecked_tex`) |

| Type | `+0x04` | Model | Texture dictionary | Second dictionary | Sizes |
| --- | --- | --- | --- | --- | --- |
| `car_copcar` | 0 | `car_copcar_geo` | `car_copcar_tex` | 0 | 120,784; 136,118; 17,840; 0 |
| `car_coupe` | 0 | `car_coupe_geo` | `car_osedan_tex` | 0 | 90,752; 106,086; 17,840; 0 |
| `dyn_arcade_aa` | `dyn_arcade_ab` | `dyn_arcade_aa_geo` | `dyn_arcade_aa_tex` | 0 | 1,056; 1,254; 2,496; 0 |
| `dyn_cstick_a` | 0 | `dyn_cstick_a_geo` | `dyn_cstick_a_tex` | `refmap_a_tex` | 4,256; 4,390; 1,600; 1,712 |
| `dyn_copcar_wrecked` | 0 | `dyn_copcar_wrecked_geo` | `dyn_copcar_tex` | `dyn_van_colors_wrecked_tex` | 24,400; 24,534; 17,840; 2,496 |

The records with + 15,334 at `+0x18` include `car_copcar`, `car_coupe` and `car_osedan`, whose models are the
47-part vehicles of the model format page.

## Dependency list (chunk 0x4F)

A 16-byte header whose first word is the record count (9,223) and whose other words are 0, then 9,223 records of 12
bytes, then 12 zero bytes to the chunk's 110,704. The records are sorted by their first word, ascending.

| Offset | Type | Meaning |
| --- | --- | --- |
| `+0x00` | u32 | hash of a resource: a model (5,511 records) or a texture dictionary (3,712 records) |
| `+0x04` | u32 | hash of the group the resource belongs to |
| `+0x08` | u32 | 0 in every record on the disc |

A resource belongs to as many groups as it has records. The list names 1,441 distinct resources and 308 distinct
groups; 1,196 records repeat a resource and group pair that an earlier record already gives.

| Group kind | Groups | Records | Examples |
| --- | ---: | ---: | --- |
| a level, `level<N>` | 85 | 6,998 | `level86` (284 records), `level31` (268), `level5` (259), `level1` (12) |
| other named levels and test scenes | 28 | 1,902 | `objarena` (346), `test4special` (169), `test_combat_ben` (154), `buschase` (80) |
| a model resource name | 181 | 249 | `dyn_trashcan_geo` (8), `dyn_jukebox_geo` (5) |
| a texture dictionary name | 10 | 10 | `dyn_dryerdoor_tex` (1) |
| `always` | 1 | 57 | see below |
| not recovered | 3 | 7 | |

The 85 level groups are levels 1, 2, 3, 5, 7, 9, 11, 14, 20, 31, 34, 51, 52, 54, 55, 60 to 74, 80 to 84, 86 to 88,
90 to 134, 136 and 137. The other named groups are `buschase`, `buschase2`, `carchase`, `combatselect`,
`combatselect2`, `combatslow`, `dynarena`, `objarena`, `test4special`, `testcamera`, `testclimb`, `testflags` and
16 groups whose names begin `test_`.

The group hash is the CRC-32 of the group name as written: `always` is `0xA6A43C1B`, `level86` is `0x6AC16E7E`.

| Group | Members on the disc |
| --- | --- |
| `always` (57 records) | pickups and icons such as `dyn_baton_geo`, `dyn_brassknkl_geo`, `dyn_cuffs_geo`, `dyn_key_geo`, `dyn_money_geo`, `dyn_phone_geo`, `dyn_spraycan_geo`, `dyn_walkie_geo` and texture dictionaries such as `newer_icons_tex`, `dyn_w_icons_tex`, `key_tex` |
| `dyn_trashcan_geo` (8 records) | `dyn_trashbit_a_geo` to `dyn_trashbit_d_geo`, `dyn_trashcan_b_geo` (twice), `dyn_rat_brown_geo`, `dyn_rat_black_geo` |
| `dyn_jukebox_geo` (5 records) | `dyn_jukebox_aa_geo` to `dyn_jukebox_ae_geo` |
| `level1` (12 records) | `dyn_pl_ma5_geo`, `dyn_pl_ma6_geo`, `dyn_pl_ma9_geo`, `dyn_tr_ma5_geo`, `dyn_bm_vb4_geo` and their texture dictionaries |

## Behaviour

The lists the game uses come from `warriors.glr` (entry 10,671). The game reads them once at start-up and keeps them
for the whole session.

### Lookups

| Lookup | Outcome |
| --- | --- |
| Character list, by an entry name that no record has | No record |
| Character list, by an entry name in another case (`BOPP_BO` for `bopp_bo`) | No record; the case of the name counts |
| Character list, by the name hash `0xD41E7A61`, which three records share | The earliest of the three in list order |
| Object list, by a type-name hash that no record has | No record |
| Object list, by `dyn_wok`, which two records share | The earlier of the two in list order |
| Dependency list, whether a resource that no record names is wanted | Not wanted, ever |
| Dependency list, wanting a group that no record names | No resource becomes wanted |

### When groups are wanted

A resource is wanted while at least one group it belongs to is wanted.

| Group | Wanted |
| --- | --- |
| `always` | From the moment the lists are read, for the whole session |
| A level's own name, such as `level86` | While that level is loaded; no longer once it is unloaded |
| The name of an object model or texture dictionary | While the resource of that name is loaded |

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x0040d900` | Load the game-wide lists at start-up | Behaviour |
| `0x001780e8` | Find a character list record by entry name | Behaviour |
| `0x001811b0` | Find an object list record by type | Behaviour |
| `0x00178c48` | Tell whether the dependency list wants a resource | Behaviour |
| `0x00178bc8` | Mark a dependency group as wanted or no longer wanted | Behaviour |

## Acceptance checks

- The PS2 disc's `WARRIORS.DIR` lists 10,701 entries; entry 10,671 has the directory hash of `warriors.glr`.
- `warriors.glr` is 635,488 bytes and holds nine chunks; a second, unnamed resource with the same resource hash
  `0xFA2E10B2` is 1,041,808 bytes.
- Chunks `0x44`, `0x46`, `0x4D`, `0x4E` and `0x4F` are identical in both resources.
- The character list has 543 records of 32 bytes; each record's model hash names a standalone skinned model resource
  and its size field at `+0x14` equals that resource's stored size.
- The character list record `bopp_bo` names the model `bopp_bo_geo` and the texture dictionary `bopp_bo_tex`.
- The object list has 1,406 records of 36 bytes; every model and texture dictionary it names is a standalone resource
  whose stored size matches the record.
- The object list record `car_osedan` names `car_osedan_geo` (104,864 bytes) and `car_osedan_tex` (17,840 bytes).
- The dependency list has 9,223 records of 12 bytes, sorted by resource hash, with a zero third word.
- The dependency list's `always` group has 57 records, among them `dyn_baton_geo` and `dyn_phone_geo`.
- The dependency list has 85 groups named `level<N>`; the largest is `level86` with 284 records.

## Open questions

- What the size at `+0x18` of character list and object list records measures (a few hundred bytes above the model's
  size, 15,334 above it for the vehicles).
- What the related type at object list `+0x04` is used for (a damaged or alternative form is a guess from names such
  as `dyn_arcade_aa` and `dyn_arcade_ab`).
- Whether car models, like object models, make the group of their own name wanted while they are loaded.
- Why 1,196 dependency list records repeat an earlier pair.
- The names of 28 character list entries, 40 object list types, 3 dependency list groups and the character
  resources.
- What chunk `0x29` holds that makes entry 10,670 twice the size, and whether the game reads entry 10,670 at all (its
  lists come from `warriors.glr`, see Behaviour).
- Whether the Xbox disc has the same lists; it was not examined.
- The record layouts of chunks `0x4D` and `0x4E` were not examined beyond their counts.
