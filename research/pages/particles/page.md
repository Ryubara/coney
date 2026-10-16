# Particle effects

## Scope and sources

This page covers the game's particle effects: where their types are listed, the sprite pages they draw from, the
values that set their look (lifetime, speed, size, colour, count) and what the common effects look like in play.

| Source | What it gives |
| --- | --- |
| `SLUS_212.15` (PS2 executable, 4,903,976 bytes) | The script type table and the fixed values each effect uses |
| `WARRIORS.WAD` on the PS2 disc (1,495,371,776 bytes, indexed by `WARRIORS.DIR`) | Sprite pages, the sheet table and the Lua scripts |
| PCSX2 savestates `SLUS-21215 (B99A75DE).01` to `.10` | The sprite batches as they stand in memory during play |

The containers, the texture dictionaries and the particle page chunk `0x4C` are described on the published pages
chunk-container and texture-dictionary. This page only says which of those pages the effects use.

Distances are in world units, with the third coordinate pointing up. Times are in ticks, the unit of an effect's
update interval. Colours are 32-bit words with red in the top byte, then green, then blue, and alpha in the low byte.

## Script type table

The executable holds one table of every object type a level or script can create by name. Particle effects share it
with object behaviours, lights and one glass type.

| Property | Value |
| --- | --- |
| Address | `0x00512f28` |
| Record size | 20 bytes |
| Records | 270, followed by an all-zero record at `0x00514440` |

Each record names one type and gives its category in the 32-bit word at offset 0x10.

| Category value | Meaning | Records |
| --- | --- | --- |
| 16 | Particle effect | 183 |
| 8 | Object behaviour | 64 |
| 4 | Light | 22 |
| 1024 | Glass | 1 |

A type's index is its record number in this table, counting from 0.

## Particle pages on the disc

Every sprite page is a WAD entry holding exactly two chunks: a texture dictionary (`0x2A`) and a particle page
(`0x4C`) whose rectangles cut the texture into sprites. Both chunks carry the same 32-bit id. The PS2 WAD holds 576
such entries: 555 pages hold one rectangle, 21 hold more. Most are menu, loading-screen and HUD art. The pages the
particle effects use are these:

| Texture name | Page id | Sheet slot | Image | Rectangles | What it holds |
| --- | --- | --- | --- | --- | --- |
| `part_page0` | `0x5cd2a2bc` | 0 | 512 x 256, 8-bit | 371 | Small HUD and effect sprites |
| `part_page1` | `0x2bd5922a` | 1 | 512 x 256, 8-bit | 67 | The main effect sprites: puffs, drops, sparks, glints, bugs, litter |
| `part_stained` | `0xa26397e9` | 2 | 128 x 128, 8-bit | 1 | Stained-glass shard |
| `part_tv` | `0x403656b8` | 4 | 64 x 64, 8-bit | 4 | Television screen frames |
| `part_fire` | `0x22767742` | 7 | 256 x 256, 8-bit | 36 | A 36-frame flame animation |
| `lighting` | `0xcb081573` | 8 | 256 x 256, 8-bit | 6 | Light glows and flares |
| `part_smokestack` | `0x39640d4c` | 9 | 128 x 64, 8-bit | 20 | A narrow fire plume |
| `part_wall_fire` | `0x5e5e43c6` | 27 | 512 x 256, 8-bit | 72 | Two 36-frame wall-flame animations |
| `part_subway_light_00` | `0xcf4c87d3` | 409 | 128 x 64, 8-bit | 1 | Subway lens flare |
| `part_fog_00` | `0x7513cd85` | 530 | 64 x 64, 8-bit | 1 | Fog wisp |
| `part_fog_01` | `0x0214fd13` | 531 | 64 x 64, 8-bit | 1 | Fog wisp |
| `part_fog_02` | `0x9b1daca9` | 532 | 64 x 64, 8-bit | 1 | Fog wisp |

The rectangles of `part_page1` that the effects on this page use, in pixels on the 512 x 256 image:

| Rectangle | X | Y | Width | Height | Used by |
| --- | --- | --- | --- | --- | --- |
| 2 | 353 | 171 | 32 | 32 | Ground blood drop |
| 3 | 320 | 171 | 32 | 32 | Ground blood drop |
| 4 | 477 | 180 | 32 | 40 | Ground blood drop |
| 8 to 11 | 75, 54, 425, 503 | 235, 235, 226, 226 | 20, 20, 10, 8 | 20, 20, 20, 20 | Litter, kind 2 |
| 18 | 33 | 235 | 20 | 20 | Litter, kind 3 |
| 19 | 96 | 237 | 16 | 16 | Garbage fly |
| 29 | 369 | 0 | 64 | 64 | Power-up glow |
| 30 to 32 | 66, 210, 436 | 171, 130, 221 | 32, 32, 32 | 32, 36, 32 | Litter, kind 1 |
| 41 | 33 | 204 | 30 | 30 | Glint and spark |
| 42 | 174 | 0 | 64 | 64 | Steam puff |
| 43 | 109 | 0 | 64 | 64 | Steam puff |
| 44 | 44 | 0 | 64 | 64 | Steam puff |
| 52 | 0 | 237 | 16 | 16 | Blood drop and blood spray |
| 54 to 58 | 309, 390, 276, 243, 470 | 130, 226, 130, 130, 221 | 32, 34, 32, 32, 32 | 32, 29, 32, 32, 32 | Litter, kind 0 |

## Sheet table

Two WAD entries, with directory hashes `0xe87e18d5` and `0xf5c908b1`, each hold one chunk `0x4D` of 4,624 bytes. The
two copies are byte-identical.

| Offset | Size | Type | Meaning |
| --- | --- | --- | --- |
| 0x00 | 4 | u32 | Slot count: 576 |
| 0x04 + 8 x n | 4 | u32 | Size in bytes of the WAD entry that holds the page in slot n |
| 0x08 + 8 x n | 4 | u32 | Page id of slot n, the id of its `0x2A` and `0x4C` chunks |

All 576 slots name a page present on the disc, one slot per page entry. The sheet slot column above gives each
particle page's slot.

## Sprite batches

Particles are drawn in sprite batches. Each batch is bound to one page and one drawing style. The PCSX2 savestates
show the same batches 0 to 12 in all ten states:

| Batch | Page | Drawing style |
| --- | --- | --- |
| 0 | `part_page1` | 2 |
| 1 | `part_page1` | 0 |
| 2 | `part_page0` | 1 |
| 3 | `part_page0` | 0 |
| 4 | `part_page1` | 1 |
| 5 | `part_page0` | 0 |
| 6 | `big_font` | 0 |
| 7 | `lighting` | 1 |
| 8 | `lighting` | 1 |
| 9 | `part_page1` | 2 |
| 10 | `part_page1` | 2 |
| 11 | `hud_minigames` | 1 |
| 12 | `part_fire` | 0 |

Batches 17 to 62 in the same states belong to the HUD and to level-specific pages.

## Sprite words

Every particle carries a 32-bit sprite word that picks what it draws.

| Bits | Meaning |
| --- | --- |
| 0 to 15 | Rectangle index on the page |
| 16 to 31 | Sprite batch for ordinary particles; sheet slot for fog wisps and wall flames |

| Sprite word | Batch or slot | Rectangle | Seen on |
| --- | --- | --- | --- |
| `0x0001002a` to `0x0001002c` | Batch 1, `part_page1` | 42 to 44 | Steam puffs |
| `0x0004002a` to `0x0004002c` | Batch 4, `part_page1` | 42 to 44 | Spinning steam puffs |
| `0x00040029` | Batch 4, `part_page1` | 41 | Glints |
| `0x0004001d` | Batch 4, `part_page1` | 29 | Power-up glow |
| `0x00010034` | Batch 1, `part_page1` | 52 | Blood drops and sprays |
| `0x00010002` to `0x00010004` | Batch 1, `part_page1` | 2 to 4 | Ground blood drops |
| `0x02120000` | Slot 530, `part_fog_00` | 0 | Fog, 13 of the 15 fog starts in the scripts |
| `0x02130000` | Slot 531, `part_fog_01` | 0 | Fog, 2 of the 15 fog starts in the scripts |

The two fog values appear in the Lua scripts as the numbers 34734080 and 34799616.

## Colour and size

Each particle has a colour word and a size. Both blend smoothly from the value of the previous update to the value of
the current one, so an effect that sets a new colour or size fades or grows into it over one update interval rather
than jumping. A particle whose alpha is 0 is invisible.

| Colour word | Look |
| --- | --- |
| `0xffffff40` | White at alpha 64 (steam) |
| `0xffffff54` | White at alpha 84 (large steam) |
| `0xffffff80` | White at alpha 128 (glint, lit) |
| `0x7a0015ff` | Dark red, opaque (blood drop) |
| `0x9a1616ff` | Red, opaque (blood spray) |
| `0x28060680` | Very dark red at alpha 128 (ground blood drop) |
| `0x55c9ffff` | Light blue, opaque (blood in the alternative colour) |
| `0xffff88ff` | Pale yellow, opaque (spark) |
| `0xcacaffff` | Pale blue-white, opaque (larger spark) |
| `0x808080ff` | Mid grey, opaque (garbage flies) |
| `0xf8e44300` | Yellow-orange (power-up glow) |

## Particle pool

| Property | Value |
| --- | --- |
| Main particle pool | 1,400 particles of 240 bytes each |
| Second pool | 100 particles of 240 bytes each |
| Free particles an emitter needs before it adds more | More than 512 in the main pool |

When the main pool has 512 or fewer free particles, steam vents, fires, spark bursts and blood stop adding new
particles, and sparks and ground blood drops end early. Emitter types such as the steam vents, the fog emitter and the
garbage-fly spot draw nothing themselves; only the particles they release are visible.

## Types named by the scripts

The PS2 WAD holds 467 compiled Lua 4.0 scripts. 35 of them spawn particle effects by name, 592 uses of
`SpawnParticle` in all, naming 33 different types. Each spawn passes the type name, a position, a rotation and an
optional object to attach to.

| Name in the scripts | Type index | Uses | Scripts | What it is |
| --- | --- | --- | --- | --- |
| `part_spray_tag` | 124 | 230 | 19 | A graffiti tag spot laid on the wall it faces |
| `part_fear` | 81 | 71 | 6 | An invisible marker; draws nothing |
| `part_narrowflame` | 107 | 56 | 2 | Narrow wall flame |
| `part_squareflame_lrg` | 125 | 54 | 1 | Large square wall flame |
| `part_ominous_smoke` | 108 | 36 | 8 | Smoke column, set up further by `CfgSteam` |
| `part_squareflame_med` | 126 | 27 | 2 | Medium square wall flame |
| `part_steam` | 128 | 18 | 4 | Steam vent |
| `part_steam_large` | 130 | 17 | 1 | Large steam vent |
| `part_train_sound` | 138 | 16 | 14 | Train sound and camera shake; draws nothing itself, now and then releases a spark burst |
| `part_strobe` | 131 | 8 | 3 | Strobe light |
| `part_fire` | 82 | 7 | 1 | Fire with sound |
| `part_small_light` | 122 | 6 | 2 | Small light |
| `part_fire_light_medium` | 86 | 6 | 1 | Fire light |
| `part_ominoussmoke` | none | 6 | 1 | Not in the type table |
| `part_plaster_drop` | 113 | 4 | 1 | Falling plaster |
| `part_explosion` | 79 | 4 | 1 | Explosion |
| `part_multi_strobe` | 106 | 3 | 2 | Multiple strobe light |
| `part_torch_flame` | 134 | 3 | 1 | Torch flame with sound |
| `part_fire_large` | 83 | 2 | 1 | Large fire with sound |
| `part_fire_large_ns` | 84 | 2 | 1 | Large fire without sound |
| `part_fire_ns` | 87 | 2 | 1 | Fire without sound |
| `part_strobe_red` | 132 | 2 | 1 | Red strobe light |
| `part_explosion_screen_shake` | 80 | 2 | 1 | Explosion screen shake |
| `part_level2_subway` | 103 | 1 | 1 | Subway effect for one level |
| `part_truck_sound` | 136 | 1 | 1 | Not traced |
| `part_truck_humans` | 137 | 1 | 1 | Not traced |
| `part_steam_huge` | 129 | 1 | 1 | Huge steam vent |
| `part_firetruck_lights` | 92 | 1 | 1 | Fire-truck lights |
| `part_squareflame_sml` | 127 | 1 | 1 | Small square wall flame |
| `part_fire_plume` | 88 | 1 | 1 | Narrow fire plume |
| `part_plaster_drop_ns` | 114 | 1 | 1 | Falling plaster without sound |
| `part_fire_light_large` | 85 | 1 | 1 | Fire light |
| (fireball emitter) | 202 | 1 | 1 | Fireball emitter |

Every name the scripts spawn is a category 16 type, except `part_ominoussmoke`, which no table record carries. The
suffix `_ns` marks the variant that plays no sound.

## Spawning from scripts

Besides `SpawnParticle`, the scripts use these particle-related functions:

| Function | Uses | Scripts | Notes |
| --- | --- | --- | --- |
| `KillParticle` | 296 | 11 | |
| `StartParticle` | 44 | 20 | |
| `EndParticle` | 35 | 16 | |
| `CfgSteam` | 32 | 13 | Sets up smoke and steam objects placed in the level |
| `End3DFog` | 40 | 40 | |
| `Start3DFog` | 15 | 15 | See Fog |
| `MaxFogParticles` | 7 | 7 | Values 10 and 15 |
| `StartGarbage` | 2 | 2 | Always with the value 0 |
| `EndGarbage` | 7 | 7 | |

The script with directory hash `0xc8a31ba8` (55,979 bytes) spawns the most effects: 161 uses of `SpawnParticle`. Each
spawn returns a handle to the new effect.

## Steam

A steam vent releases one puff at a time from its position. Each puff is a white sprite (`part_page1` rectangle 42,
43 or 44, picked at random) that rises, grows and fades out.

| Value | `part_steam` | `part_steam_large` | `part_steam_huge` |
| --- | --- | --- | --- |
| A new puff every | 17 ticks | 6 ticks | 7 ticks |
| Puff update interval | 10 ticks | 10 ticks | 30 ticks |
| Puff lifetime | 11 updates (110 ticks) | 11 updates (110 ticks) | 4 updates (120 ticks) |
| Starting alpha | 64 | 84 | 64 |
| Starting size | 0.25 | 0.75 | 2.5 |
| Growth per update | 0.125 | 0.02 | 0.32 |
| Upward speed | 0.25 | 0.45 | 0.75 |
| Vent works while a camera is within | 30 units | 35 units | 45 units |

| Behaviour | Outcome |
| --- | --- |
| Puff size and upward speed | Each varies at random between 0.8 and 1.2 times the listed value |
| Fading | Alpha falls in equal steps from the starting value to 0 over the puff's lifetime |
| Vent off screen or out of range | No puffs; the vent looks again every 60 ticks |
| Main pool nearly full | No new puffs |

## Fog

`Start3DFog` creates one fog emitter for each of the two player views. Each emitter fills its view with drifting fog
wisps, single sprites from `part_fog_00` or `part_fog_01`.

| `Start3DFog` argument | Meaning |
| --- | --- |
| 1 | Sprite word of the wisps (34734080 or 34799616 on the disc) |
| 2 | Wisp colour as a four-entry table: red, green, blue, alpha |
| 3 | Drift speed |
| 4 | Fade-in pace |
| 5 | Fade-in step multiplier |

| Arguments 3, 4 and 5 | Fog starts using them |
| --- | --- |
| 0.4, 0.3, 7 | 4 |
| 2, 0.3, 6 | 3 |
| 4.6, 0.4, 5 | 2 (with `part_fog_01`) |
| 0.5, 1, 1 | 2 |
| 0.4, 0.5, 7 | 2 |
| 2, 0.5, 6 | 1 |
| 1.5, 0.7, 6 | 1 |

| Behaviour | Outcome |
| --- | --- |
| Wisp limit | 20 wisps per player view when the fog starts |
| New wisps | Up to 10 per emitter update; the emitter updates every 5 ticks |
| Single-player | Only the first view's emitter adds wisps |
| Where wisps appear | Near the view's camera, 0.5 to 2 units above the camera's height |
| Wisp size | Random, 1 to 4 |
| Wisp movement | Drifts roughly toward the camera at the drift speed times 1.75 to 2.25 |
| Fade-in | Alpha rises each update from 0 toward the colour's alpha |
| Hidden wisps | Alpha 0 while more than 20 units from the camera, off screen, or within 4 units of it |

## Garbage flies

A garbage-fly spot shows three small swarms of flies. Each swarm is one 16 x 16 fly sprite (`part_page1` rectangle
19), mid grey and opaque, size 0.02, circling the spot at a radius of 1.5.

| Behaviour | Outcome |
| --- | --- |
| Spot in view | Three swarms are present; the spot looks again every 60 ticks |
| Spot out of view | The swarms are removed; the spot looks again every 120 ticks |
| Fly motion | Each fly circles on two angles that advance by 0.628 and 1.257 radians per update |
| Fly update interval | 20 ticks, now and then 10 |
| Sound | A looping fly sound at the spot; the `_ns` variant (type 95) has none |

## Blowing litter

`StartGarbage` fills the area round player one's camera with litter that blows in the wind. The scripts start it in
two places, both times with kind 0.

| Property | Value |
| --- | --- |
| Pieces | 64, on an 8 x 8 grid round the camera, 7 units apart |
| Height | The camera's height, from 1 unit lower to 5 units higher |
| Wind needed to move a piece | Random per piece, 0.5 to 10 |

| Kind | Sprites on `part_page1` |
| --- | --- |
| 0 | Rectangles 54 to 58 |
| 1 | Rectangles 30 to 32 |
| 2 | Rectangles 8 to 11 |
| 3 | Rectangle 18 only |

## Glints

A glint is a small white star (`part_page1` rectangle 41, 30 x 30 pixels) that twinkles on pickups such as power-ups.

| Behaviour | Outcome |
| --- | --- |
| Size | 0.15 times the size it is given |
| Loose glint | Lit for 20 to 30 ticks, dark for the next 20 to 30 ticks, in turn |
| Glint attached to an object | Lit all the time, following the object |
| Lit and dark | White at alpha 128, and alpha 0; each change blends over one update |

A power-up also carries a glow: `part_page1` rectangle 29 (64 x 64), size 0.2, coloured yellow-orange, attached to
the power-up.

## Blood

Blood effects release drops and sprays. All use the 16 x 16 drop sprite (`part_page1` rectangle 52), drawn as a short
streak along the direction of travel.

| Value | Blood drop | Blood spray | Ground blood drop |
| --- | --- | --- | --- |
| Colour | Dark red, opaque | Red, opaque | Very dark red, alpha 128 |
| Size | Grows from 0 to 0.06 | Grows from 0 to 0.05 | 0.1 to 0.15 at the start |
| Streak | Up to 3 copies | Up to 2 copies | None |
| Update interval | 2 ticks | 2 ticks | 10 ticks |
| Fade | Alpha falls by 4 each update | Alpha falls by 4 each update | Fades out over its last 180 ticks |
| Lifetime | At most 20 updates (40 ticks) | At most 20 updates (40 ticks) | About 400 ticks |

| Behaviour | Outcome |
| --- | --- |
| Drop spread | Each drop's sideways speed varies at random by up to 0.25 |
| Spray speed | 1 to 2 times the speed it is given |
| Faint drops | A drop or spray ends once its alpha falls to 15 or less |
| Ground drop growth | 0.225 after 40 ticks, then grows to 0.45 while fading |
| Alternative colour | With one game setting, all blood is light blue instead (see Open questions) |

## Sparks

A spark burst throws off small bright sparks (`part_page1` rectangle 41) with a crackle sound and a short flash of
light. It appears when certain breakable objects are hit, and now and then near a passing subway train.

| Value | Spark burst |
| --- | --- |
| Bursts | 1 or 2, chosen at random |
| Time between bursts | 10 to 30 ticks |
| Sparks per burst | 10 to 20 |
| Light flash | One per burst |
| Sound | One spark sound per burst, picked at random |
| Burst off screen | Sound only, no sparks |

| Value | One spark |
| --- | --- |
| Size | Random, 0.01 to 0.03 |
| Colour | Pale yellow; sparks larger than 0.015 turn pale blue-white |
| Update interval | 2 ticks |
| Lifetime | At most 90 updates (180 ticks) |

## Wall flames and fires

Wall flames draw from `part_wall_fire`. The square flames use its rectangles 0 to 35, the narrow flame 36 to 71.
Each flame starts on a random one of its 36 frames, updates every 2 ticks and is white at alpha 240 to 255.

| Type | Size |
| --- | --- |
| `part_squareflame_lrg` | 27 |
| `part_squareflame_med` | 12 |
| `part_squareflame_sml` | 7.5 |
| `part_narrowflame` | 9 |

Fires are built from flame pieces. Each piece plays the 36-frame animation of `part_fire` from a random frame,
changes frame every 2 or 3 ticks, wobbles in size by up to a tenth, and now and then throws off embers and smoke.
A piece's size is 5.5 times its scale.

| Type | Flame pieces (scale) | Light value | Sound |
| --- | --- | --- | --- |
| `part_fire` | 2 at the centre (1.0), 6 round it 0.4 out and 0.5 lower (0.5) | 1.45 | Yes |
| `part_fire_ns` | As `part_fire` | 1.45 | No |
| `part_fire_large` | 2 at the centre (1.3), 6 round it 0.6 out and 0.2 lower (1.0), 4 further out and 0.5 lower (0.5) | 2.3 | Yes |
| `part_fire_large_ns` | As `part_fire_large` | 2.3 | No |
| `part_torch_flame` | 1 (0.15) | 0.4 | Yes |
| Fire barrel (type 90) | 1 (0.4) | 0.55 | Yes |
| Tiki torch (type 89) | 1 (0.07) | None | Yes |

The tiki torch (type 89) has no light. Every other fire in the table adds one of the listed light value, unless one
game setting switches fire lights off (see Open questions).

## Landing

Some particles test for contact with the level's surfaces. What happens on contact depends on the effect.

| Effect | On contact |
| --- | --- |
| Spark | Bounces: it leaves the surface at 0.35 of the speed it hit it with, and its speed along the surface halves |
| Spark, fifth contact | Stops dead, lies on the surface, turns pale yellow and fades out over 60 ticks |
| Blood splatter blob | Stops, plays a splat sound and lies flat on the surface at alpha 128 |
| Blood splatter on a floor | Leaves 17 ground blood drops scattered round the spot |
| Blood splatter on a wall or slope | Leaves 10 ground blood drops in a line along its direction of travel, 0.1 apart |
| End of a blood splatter blob | Fades out over 180 ticks and grows to size 0.45, then disappears |

A surface counts as a floor when the up component of its facing is 0.9 or more.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x00378958` | Spawn a named particle effect for a script | Spawning from scripts |
| `0x0039b020` | Draw one particle's sprite | Sprite words |
| `0x0018e148` | Start the drifting fog in both player views | Fog |
| `0x00170330` | Start the blowing litter round the camera | Blowing litter |
| `0x003f6bf0` | Release puffs from a small steam vent | Steam |
| `0x003e1250` | Move a spark and make it bounce | Landing |

## Acceptance checks

| Outcome |
| --- |
| The executable's script type table at `0x00512f28` has 270 records of 20 bytes, then an all-zero record |
| The category field counts 183 particle types, 64 object behaviours, 22 lights and 1 glass type |
| The PS2 WAD has 576 entries made of one texture dictionary and one particle page each |
| The page `part_page1` decodes to a 512 x 256 image cut into 67 rectangles; rectangle 42 is a 64 x 64 soft puff |
| The page `part_wall_fire` has 72 rectangles and `part_fire` has 36 |
| The sheet table holds 576 slots; slot 1 is `part_page1` and slot 530 is `part_fog_00` |
| Every sheet-table slot's size equals the size of the WAD entry holding that page |
| The WAD holds 467 compiled Lua 4.0 scripts; 35 of them use `SpawnParticle`, 592 times, naming 33 types |
| `part_spray_tag` is the most spawned name, 230 times in 19 scripts |
| A small steam vent in view releases a new white puff about every 17 ticks; each puff rises, grows and is gone in about 110 ticks |
| A steam vent stops puffing when the camera is more than 30 units away |
| In fog, wisps near the camera vanish when it comes within 4 units of them |
| A garbage-fly spot shows three fly swarms while it is on screen and none after it leaves the screen |
| A glint on a power-up stays lit; a loose glint blinks on and off every 20 to 30 ticks |
| A blood drop is dark red and gone within 40 ticks |
| A spark burst on screen gives one or two crackling bursts of 10 to 20 sparks each |
| A spark bounces up to five times, then lies still and fades out |
| Blood that splats on a floor leaves 17 small dark drops round the splat; on a wall it leaves 10 in a line |
| The main particle pool holds 1,400 particles |

## Open questions

- The length of a tick in seconds is not confirmed here; a tick is the unit of an effect's update interval.
- The game setting that turns all blood light blue was not identified.
- The game setting that switches off the fire lights was not identified.
- What drawing styles 0, 1 and 2 of the sprite batches look like on screen was not confirmed.
- Why the fog wisps also hide within 10 units of the camera in some cases was not confirmed.
- `MaxFogParticles` is believed to replace the default limit of 20 wisps per view; not confirmed.
- What happens when a script spawns `part_ominoussmoke`, a name with no type record, was not observed.
- The Xbox version's particle data and scripts were not compared with the PS2 version.
- The exact distance at which fog wisps appear in front of the camera was not confirmed.
- The particles' speed and gravity values, other than those listed, were not traced.
