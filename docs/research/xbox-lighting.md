---
kind: feature
---
# Xbox lighting and shadows

Verified against: `SLUS_212.15` (NTSC-U) and the NTSC-U Xbox `default.xbe` (SHA1 `fe246a74501c...`).

## Summary

What Rockstar's Xbox port draws differently from the PS2 build in lighting, shadows, fog and the final image, so that
Coney can offer it. Follows: the Xbox port (presentation only). The PS2 stays the reference for behaviour; nothing on
this page changes what the simulation does.

On the Xbox, characters cast **real shadows**: a dark copy of each nearby person's shape falls on the street, the
walls and the props, all in one direction per level, the same in every frame. Lamps and fires light the same
surfaces, with the same colours and the same flicker, as on the PS2. The whole picture also passes through a
per-level contrast curve that crushes the darkest tones and brightens the rest, and the fog thickens per pixel
rather than per vertex.

How it works, in one paragraph. Above the graphics device the Xbox runs the PS2's game code unchanged: the
[light manager](lighting.md) and its per-atomic light selection, the light tasks that effects spawn (the burning
barrel's `sub_fire2_light`, [Fire lights](script-types.md#fire-lights)), the level scripts and their `LightData`. The
device underneath is the port's own Direct3D 8 code ([Xbox assets](xbox-assets.md#the-executable)). It lights every
vertex in **vertex shaders** that a script compiles at boot, with the PS2's light model (ambient, directional and
point lights with a linear falloff), so lighting stays per vertex. It adds one **shadow map**: each frame the
characters within 20 m, and the props they hold, are drawn into a 1024 × 512 depth texture from a fixed
"shadow light" placed by the level's script, and the world and the world objects darken where that texture says they
are hidden. There are no specular, environment or bump maps, no light other than that one casts a shadow, and
characters never receive one. On the PS2 the same script calls (`SetShadowColor`, `SetShadowLightOffset`,
`EnableShadow`) are empty, and a person has only a faint blob sprite ([Humans](lighting.md#humans)).

**The fire barrel in the reference videos.** Its flickering orange light on the brick wall, the ground and the men is
the PS2's own light, computed the same way on both builds ([The fire light](#fire-light)). The silhouettes on the
ground come from the shadow light, not from the fire: the fire casts no shadow on either build.

## Rules

What a player sees, Xbox against PS2. Each row links to the behaviour that holds its addresses and evidence.

| Element | Xbox | PS2 | Behaviour |
| --- | --- | --- | --- |
| Lights: sources, colours, flicker | the PS2's game code | the same | [The same game code](#same-code) |
| Light model | per vertex in vertex shaders; ambient + directional + point, point falloff `1 - d / r` | per vertex in VU1 microcode; the same terms | [Vertex lighting](#vertex-lighting) |
| Lights per surface | world: 1 directional + 0, 3 or 7 point; objects and people: 2 directional + 3 point; ambients summed | world up to 8 lights; objects and people 3 to 6 by distance (4 to 6 from the pulse list) | [Lights per surface](#light-slots) |
| Character shadows | a shadow map, 1024 × 512, one light per level, humans within 20 m and up to 20 held props | a blob sprite per human | [The shadow map](#shadow-map) |
| Who receives | the streamed world and world objects | the ground under the blob | [Receiving](#shadow-receive) |
| Shadow colour and direction | per level from `global.lua`: colour (0.77, 0.77, 0.79), offset (-500, -500, 850) in most levels | the calls do nothing | [The shadow light](#shadow-light) |
| Fog | linear, per pixel | linear, per vertex | [Fog](#fog) |
| Final image | a per-level gamma ramp | none (`SetGammaRamp` is empty) | [The gamma ramp](#gamma-ramp) |
| Brightness option | default 48 | default 40 | [Brightness](#brightness) |
| Specular, environment, bump maps, per-pixel lighting | none | none (environment-map materials only, [World](world.md#pipelines)) | [What the Xbox does not add](#not-added) |

## Map

Xbox addresses are virtual addresses in `default.xbe` (base `0x00010000`), written `Xbox 0x...`; plain addresses are
in `SLUS_212.15`. The Xbox has no symbols: names are ours, and the source file is the path the function loads where it
loads one. Source files: `\Warriors\Source\Graphics\Devices\XBox\GraphicsShadowXBox.cpp` and
`GraphicsDeviceXBox.cpp`, `\Warriors\Source\Graphics\LightManager.cpp` (shared with the PS2), and the script
`xbox_shaders.lua`.

| Behaviour | Xbox function | PS2 counterpart | Section |
| --- | --- | --- | --- |
| Frame: shadow map, then the passes | Xbox `0x0032a470` viewport render | the viewport pass ([Rendering](rendering.md#passes)) | [A frame](#frame) |
| Shadow object: construct, defaults, map | Xbox `0x000be6a0`, Xbox `0x000bda20`, Xbox `0x000bd810` | none | [The shadow map](#shadow-map) |
| Shadow map: casters and depth pass | Xbox `0x000bdaf0` | none | [The shadow map](#shadow-map) |
| Shadow colour, light offset, caster flag | Xbox `0x0009ce50`, Xbox `0x0009ced0`, Xbox `0x0009cdd0` | `0x0017f260`, `0x0017f268`, `0x0017f270` (empty) | [The shadow light](#shadow-light) |
| World sector draw | Xbox `0x0032dd40` | `World_RenderSectorAtomic` `0x00411990` | [Receiving](#shadow-receive) |
| World object draw | Xbox `0x0009d740` | `ObjectRender_Draw` `0x0017fd78` | [Receiving](#shadow-receive) |
| Character draw | Xbox `0x0008fc70`, Xbox `0x000bedf0` | `HumanRender_Draw` `0x00174320` | [Vertex lighting](#vertex-lighting) |
| Light selection per atomic | Xbox `0x0009ae10` | `LightManager_SelectLights` `0x0017de10` | [Lights per surface](#light-slots) |
| Light upload: world, objects | Xbox `0x000990e0`, Xbox `0x00098f50` | `LightManager_UploadSelected` `0x0017e810` | [Lights per surface](#light-slots) |
| Vertex shaders from the script | binding Xbox `0x0028bb20` → Xbox `0x000b93e0` | none | [Vertex lighting](#vertex-lighting) |
| Pixel shaders | Xbox `0x000ba550` | none (fixed GS blending) | [Vertex lighting](#vertex-lighting) |
| Device frame set-up: fog, gamma ramp | Xbox `0x000bb860`, Xbox `0x000ba800` | `RwDevice_SetUpCameras` `0x00195400` | [Fog](#fog), [The gamma ramp](#gamma-ramp) |
| Fire light flicker | Xbox `0x002dda80` | `SubFire2Light_Update` `0x003c71a8` | [The fire light](#fire-light) |
| Brightness | Xbox `0x000ddd50`, defaults Xbox `0x003386b0` | `Gamma_Set` `0x001b4838` | [Brightness](#brightness) |

## Behaviour

### The same game code {#same-code}

Inferred from the shared source files ([Xbox assets](xbox-assets.md#the-executable)) and confirmed (code) for the
functions this section names.

Everything that decides which lights exist, where, in which colour and when they flicker is the PS2's code on both
builds. The light manager's selection on the Xbox, Xbox `0x0009ae10`, takes the same arguments as
`LightManager_SelectLights` (`0x0017de10`): the world's list or the objects' list (B or C with the pulse), the glow
for a human whose byte above 10 asks for it, and the point lights whose sphere meets the atomic's, replacing the
entry with the smallest score when the list is full, the same quirk as on the PS2 ([Selection](lighting.md#select)).
Both builds run the same scripts: all 467 PS2 scripts are byte-identical on the Xbox
([Kinds of entry](xbox-assets.md#kinds-of-entry)), so `global.lua`'s per-level `LightData` and every level's
`SetLight` calls are the same. One difference in this layer: the Xbox selection never uses the distance it is given,
so objects and people far from the camera keep their lights (the PS2 cuts from 6 to 3 between 40 and 69 m, to 4
for the pulse list). That is
confirmed (code) at Xbox `0x0009ae10`, where the cap is 8 records whatever the distance.

### The fire light {#fire-light}

Confirmed (code) at the addresses named.

A burning barrel is a `part_firebarrel` (or `part_firebarrel_ns`) that a breakable prop adds by model
([dyn_masks](script-types.md#dyn-masks-blaster)). On the PS2 its set-up, `FireParticle_SetUpByName` (`0x003c8b28`),
makes the flames (`sub_fire2` of size 0.4), heat distortion and, unless game-state flag word 0 has bit 3 set, one
`sub_fire2_light` with size 0.55: a point light of radius 7.5 × 0.55 = 4.1 m that lights the world, objects and
humans ([Fire lights](script-types.md#fire-lights), [How a light type lights](script-types.md#light-type-lights)).
Every 5 to 15 ticks `SubFire2Light_Update` (`0x003c71a8`) sets the next of four colours from `0x005144a8`, which
alternate between orange (244, 92, 28) and yellow (244, 186, 31), and 90 to 100 % of the radius; the light task
cross-fades linearly to each new colour over the interval (`LightTask_Update`, `0x003906b8`). The low byte of the
colour word is set to 64 + `rand(192)` each time; it is the colour's alpha, which no lighting path reads.

The Xbox's update, Xbox `0x002dda80`, does the same: the same 10 + 5 interval, the same 0.9-1.0 radius factor, the
same alpha byte, and its own copy of the four colours at Xbox `0x00460b70`, byte for byte the PS2's. So the barrel's
light, its colours, its radius and its rhythm are identical on both builds; what the reference videos show on the
wall and the men is the light model below, the same on both. The fire casts no shadow on either build: the Xbox's only
shadow comes from [the shadow light](#shadow-light).

### Vertex lighting {#vertex-lighting}

Confirmed (code) in the script `xbox_shaders.lua` and at the addresses named.

The Xbox compiles its 13 vertex shaders at boot from text in the script `xbox_shaders.lua`: the binding
`XBoxCompileVertexShader(slot, name, source)` (Xbox `0x0028bb20`) assembles each one and stores it by slot, with the
vertex layout the slot needs (Xbox `0x000b93e0`). The seven pixel shaders are assembly text in the executable
(Xbox `0x00459950`), assembled at boot by Xbox `0x000ba550`: `NoTexture`, `SingleTexture`, `SingleTexturePreLit`,
two `DualTexture`, `ShadowBuffer` and `ShadowBufferDual`. Every pixel shader takes the lit colour from the vertex
colour register and multiplies it by the texture (and, in the shadow ones, by the shadow factor). So **lighting is
per vertex on the Xbox too**: a large wall with few vertices shows the fire's light as a smooth gradient between its
corners, as on the PS2.

| Slots | Shader | Lights it evaluates | Colour it outputs |
| --- | --- | --- | --- |
| 1 | `vsCharacter` (3 bones a vertex) | 2 directional, 3 point | (lights) × material + ambient |
| 2, 13 | `vsCharacterShadowCast`, `vsObjectShadowCast` | none | position only, for the shadow map |
| 3-5 | `vsWorldShadowL0`, `L3`, `L7` | 1 directional, 0, 3 or 7 point | (lights) × material + ambient + the vertex's prelight |
| 6-8 | the same, `2Pass` | the same, two UV sets | the same |
| 9, 10 | `vsObjectShadow`, `2Pass` | 2 directional, 3 point | (lights) × material + prelight + ambient |
| 11, 12 | `vsObjectShadowMat`, `2Pass` | 2 directional, 3 point | (lights) × material + an emissive colour |

Each term is the PS2's ([World lighting](lighting.md#world)): a directional light adds its colour × `max(0, n · −L)`;
a point light adds its colour × `max(0, n · L̂)` × `(r − d) / r` when `d < r` and nothing beyond, from an attenuation
constant `(r, 1 / r)` that the uploads write (Xbox `0x000990e0`, Xbox `0x00098f50`). The hardware clamps the vertex
colour to 0..1 before the pixel shader, so there is no overbright, as on the PS2. Two small differences in the sum:
the material multiplies only the runtime lights, not the prelight or the ambient (the PS2 clamps prelight + light
and then multiplies by the material, though the streamed world's materials are white); and characters get no
prelight on either build.

**A quirk of the character shader.** It skins the position and normal by up to three bones, but measures each point
light's direction and distance from the vertex's **unskinned** position (the bind pose), while the light positions are
moved into the character's model space (Xbox `0x000bedf0`). So a lamp lights an arm or a leg as if it were in the
bind pose. Confirmed (code) in `g_vsCharacter`; how far that shows on screen is inferred from the maths (a limb a
metre from its bind-pose place, under a 4 m light, is lit about a quarter more or less than it should be).

### Lights per surface {#light-slots}

Confirmed (code) at Xbox `0x000990e0` (world), Xbox `0x00098f50` (objects) and Xbox `0x000bedf0` (characters).

The upload walks the selected records by the Xbox record's kind (1 point, 3 directional, 4 ambient):

- **World sector**: the first directional light, the first 7 point lights (position, colour, attenuation), and the
  sum of every ambient. The vertex shader is chosen by the point count rounded up: 0 → `L0`, 1-3 → `L3`, 4-7 → `L7`,
  the unused slots written black (Xbox `0x0032dd40` picks slot 3, 4 or 5, or 6, 7, 8 for two UV sets). The PS2 world
  takes up to 8 records of any kind; the world's list holds the world ambient and the world's directional lights,
  so the counts meet in practice (inferred from [Level 99's values](lighting.md#level99), where no directional light
  lights the world).
- **World object**: the first 2 directional lights, the first 3 point lights, the ambients summed.
- **Character**: the same 2 + 3, each point light's position moved into model space and its colour multiplied by a
  colour the draw passes in (the human's `HuColor` with its dimming, inferred from the PS2's
  [Humans](lighting.md#humans)).

Point lights beyond the third (objects, people) or the seventh (world) are dropped in list order, which is the order
the cull found them in, not by distance; the PS2's selection has the same order.

### A frame {#frame}

Confirmed (code) at the addresses named unless a line says otherwise; the order is the Xbox viewport render's.

Per viewport (Xbox `0x0032a470`), after the camera and the visibility lists are built (up to 60 humans, the cars, the
world objects):

1. **The shadow map** (Xbox `0x000bdaf0`), when the shadow object exists.
2. The humans (Xbox `0x0008fc70` each, through Xbox `0x000bedf0`), lit and blood-layered as on the PS2
   ([Drawing a human](graphics.md#human-draw)); they do not sample the shadow map.
3. A second list of up to 18 a viewport (the cars, inferred from its source), then the world objects with shadow
   receiving on (Xbox `0x000bd430` before, Xbox `0x000bd4a0` after).
4. The streamed world's sector lists, opaque then sorted (Xbox `0x0032d160`, Xbox `0x0032d240`), each sector lit by the
   world selection and drawn with the shadow map (Xbox `0x0032dd40`).
5. The world objects whose kind is `0x20`, kept in a list of their own and receiving as well, then the second list's
   second pass and the rest.

### The shadow map {#shadow-map}

Confirmed (code) at the addresses named.

The shadow object (`GraphicsShadowXBox`, 0x19c bytes, held in one global) is made with the device (Xbox
`0x0032b650` → Xbox `0x000be6a0`, defaults at Xbox `0x000bda20`). Xbox `0x000bd810` creates its map: a **1024 × 512
depth texture**, format `D3DFMT_LIN_D16` (the object's format index `+0x128` is 0 in a table that also lists
`LIN_D24S8`, `LIN_F24S8` and `LIN_F16`), placed in a compressed depth tile, with a surface header for the render
target. The depth range is 65,535 for 16 bits.

Each frame, Xbox `0x000bdaf0`:

1. **Casters.** Every human in the viewport's draw list whose caster flag (human `+0x18b`) is on and who stands within
   **20 m** of the camera (squared distance below 400, from the camera's position reported through its `+0x108`
   virtual; which point it is, the eye or the target, is inferred). The flag is set to 1 when a human is set up (Xbox
   `0x0014bb80`), so everyone casts unless a script calls `EnableShadow(object, false)` (Xbox `0x0009cdd0`; 14 level
   scripts call it or `HuShadow`). For each caster, up to two props he carries (the object behind his handle at
   `+0x324` and the one a second getter returns, kept when the object's type has flag 8) join a list of at most
   **20** props; that they are the held items is inferred from the handles. World geometry, cars and props on the
   ground do not cast.
2. **Bounds.** A box grown by 2 m around each caster's position, and the casters' mean position.
3. **The light's view.** The eye is the box's centre plus the **shadow light offset** (`+0x15c`); it looks at the mean
   position. The projection is a perspective one whose angle just covers the box's radius at that distance, with the
   near plane at the distance minus the radius (at least 0.01) and the far plane at that near plane plus 100 m. With the
   usual offset of about 1,100 m this is close to a parallel projection: one direction for every shadow. The texture
   matrix that receivers use maps it to texels with a half-texel offset (512.5, 256.5) and the depth to the 16-bit
   range.
4. **The pass.** Into the depth texture, cleared to 1.0, with colour writes off and a depth bias (slope 4.0, constant
   2.0; their render states are inferred from the values the object stores at `+0x12c`, `+0x130`): every caster with
   `vsCharacterShadowCast`, then every prop with `vsObjectShadowCast`.
5. With no caster the matrices are reset and the pass is skipped.

So the Xbox has one shadow map per viewport, fitted tightly around the people near the camera, not around the view:
its resolution is spent where the people are, and shadows end with them (no shadow beyond 20 m, and none from a
building).

### The shadow light {#shadow-light}

Confirmed (code) at the addresses named; the per-level values are read from `global.lua`.

`SetShadowColor(r, g, b)` (Xbox `0x0009ce50`) stores the **shadow colour** at the shadow object's `+0x14c` and a copy
at `+0x17c`; `SetShadowLightOffset(x, y, z)` (Xbox `0x0009ced0`) stores the **offset** at `+0x15c` and a copy at
`+0x18c`. The defaults (Xbox `0x000bda20`) are colour 0.7 grey and offset (-500, -500, 850); `+0x13c` is the lit colour,
white. On the PS2 the three bindings are the empty functions `0x0017f260`, `0x0017f268` and `0x0017f270`
([Code index](lighting.md#code-index)).

`global.lua` calls both once per level, after its lights, with the level's row of its light table (`LM[level]`, the
art level's or level 1's when the level has none): the fields `xshadowcolor` and `xshadowdir` sit beside the moonlight
and ambient fields that both builds use (`global.lua`, the `LightData` set-up). Over the table's 50 rows: colour
(0.77, 0.77, 0.79) in 48, (0.64, 0.64, 0.75) and (0.77, 0.77, 0.84) once each; offset (-500, -500, 850) in 45, and
(-500, 500, 550), (500, -500, 2000), (-500, -500, 700), (500, 500, 850), (500, 1400, 2900) once each. The offset is in
the game's axes (z up), so the default light stands high in the -x, -y quarter; it is unrelated to the level's
moonlight direction, which the same rows give separately.

### Receiving {#shadow-receive}

Confirmed (code) at the addresses named and in `xbox_shaders.lua`.

The streamed world (Xbox `0x0032dd40`) and the world objects (Xbox `0x0009d740`) bind the shadow map as texture stage 1
(stage 2 for two UV sets) with border addressing, upload the shadow direction (the offset, negated and normalised,
`+0x16c`) as a vertex constant (Xbox `0x000bd430`, Xbox `0x0032cfd0`) and draw with `ShadowBuffer` or
`ShadowBufferDual`, whose constants are the lit colour and the shadow colour (Xbox `0x0032dd40` sets both). Per vertex
the shader writes the position projected by the shadow matrix as a projective texture coordinate (its `w` kept from
going negative), and a flag that is 1 when the surface faces away from the shadow light. Per pixel the hardware compares
the depth (the shadow-buffer compare is on in the device's state, its states inferred from Xbox `0x000bd430`), and the
shader picks the **lit colour** when the test passes or the surface faces away, else the **shadow colour**, and
multiplies the texture × vertex colour by it.

So:

- A shadow **multiplies** the whole lit colour by the shadow colour, 0.77 in most levels: 23 % darker, faintly blue.
  It does not remove the light of one source; a wall lit by the fire is darkened by the same share as one lit by
  nothing.
- The edge is a **hard** binary choice; any softness comes from the hardware's own filtering of the shadow buffer,
  which was not read (an open question).
- A surface facing away from the light is **never** darkened, so the back of a wall gets no shadow acne and no second
  darkening.
- **Characters never receive**: `vsCharacter` has no shadow coordinate and the humans draw with `SingleTexture` or
  `DualTexture`. No self-shadowing, no shadow of one man on another.
- Cars are drawn outside the receiving block; whether they receive was not traced.

### Fog {#fog}

Confirmed (code) at Xbox `0x000bb860` for the states.

The Xbox device sets the fog colour from the device's own colour bytes (the same fog colour the PS2's
`SetFogColor` sets, through the device's `+0x20` and `+0x24` slots at Xbox `0x00328470`, Xbox `0x003284c0`), the fog
**table mode to linear** (render state value 3) with a start and an end from the device. Table fog is evaluated per
pixel from the depth, where the PS2 writes a fog value per vertex and interpolates it
([Fog](ps2-render.md#fog)). The curve is the same kind (linear from a start to the far end); that the start is the
draw distance × the fog start, as on the PS2, is inferred from the device sharing the PS2's fog fields. The vertex
shaders also write a fog coordinate from the position's `w`, the characters with the opposite sign to the world's
(an open question).

### The gamma ramp {#gamma-ramp}

Confirmed (code) at Xbox `0x000bb860` and Xbox `0x000ba800` for the steps; the ramp's exact arithmetic is inferred from
the loop's shape, whose conversions the decompiler does not show.

At the end of its frame set-up the Xbox device picks one record of a table at Xbox `0x00459288`: 33 records of 40 bytes,
indexed by the game state's word `+0x56b4` (values above 29 share records 30 or 32; record 31 when Xbox `0x0007a470`
does not return 1). That the word is the level number is inferred from its use as a table index beside the level's other
settings ([Open questions](#open-questions)). Each record holds up to four points `(x, y)` of a curve on a 0-256 scale,
a value and a flag. Xbox `0x000ba800` builds three 256-entry ramps from the points, linear between them, clamped to
0-255, with the light manager's brightness plus colour offset (Xbox `0x00099300`, the twin of `LightManager_AddOffsets`
`0x0017ecc8`) folded in, and `D3DDevice_SetGammaRamp` applies them to the whole output, the HUD included. The records'
curves: 16 rise straight from (0, 0), 15 of them to (256, 285), a gain of about 1.11, and one to (256, 340); the other
17 stay black up to an input of 2 to 10 and then rise to between (256, 285) and (256, 390), a gain of up to 1.52 that
crushes the darkest tones. When the record's value changes, Xbox `0x0009a620` adds it × 0.07 to two fields of every
point and spot light's descriptor, clamped to 0..1; which fields they are is open ([Open questions](#open-questions)).
The PS2 has no such step: `SetGammaRamp` is empty ([Brightness](lighting.md#brightness)).

### Brightness {#brightness}

Confirmed (code) at the addresses named.

The brightness option works as on the PS2: Xbox `0x000ddd50` stores the value (0-255) and gives the light manager
value / 255 (Xbox `0x0009ab70`), which every ambient and directional light gets added
([The brightness option](lighting.md#brightness)). The profile's default (Xbox `0x003386b0`, the store at Xbox
`0x003387e8`) is **48**, 0.188, against the PS2's 40, 0.157. On the Xbox the same term also enters the gamma ramp.

### What the Xbox does not add {#not-added}

Confirmed (code) for the shader set: the 13 vertex shaders and 7 pixel shaders above are the whole set the device
creates (Xbox `0x000b93e0`, Xbox `0x000ba550`). None reads a normal map, a cube map or a specular term, and none
lights per pixel; the names of the Xbox's bump, cube-map and specular texture modes occur only among the shader
assembler's own strings, which the XDK links in. Inferred from the shader set and the shadow pass: no light but the
shadow light casts a shadow, and the PS2's corona sprites, flicker and glow are drawn by the shared code.

## In Coney {#in-coney}

**Follows: the Xbox port (presentation).** The maintainer decided on 2026-10-25 that the Xbox port's lighting and
shadows count as faithful and Coney will offer them as options, off by default; the PS2 stays the reference for
behaviour and for the tests. Nothing below is built. The renderer that will host it is designed on
[Realtime lighting design](../guides/realtime-lighting.md) and in issue #883; this section maps each Xbox technique
onto it. Every option is off by default and forced off in test mode, so the faithful tests keep comparing with the
PS2.

**What to keep exactly** (it is data or behaviour, the same on both builds): which lights exist and where, their
kinds, colours, radii and what they light; the fire light's four colours, its 5-15 tick interval and cross-fade; the
selection order and caps where they change the look; the per-level shadow colour and offset from `global.lua`; who
casts (humans with the flag, within 20 m, and their held props) and who receives (world, world objects); the rule
that a surface facing away from the shadow light is never darkened; the gamma curves.

**What is free to improve** (presentation only): per-pixel evaluation; more lights per surface; the shadow map's
size, filtering and coverage; characters receiving shadows; shadows from point lights such as the fires, which the
Xbox never had. These belong to the *Enhanced* and higher presets of the design page.

**Where the options sit.** The design page's [Quality ladder](../guides/realtime-lighting.md#quality-ladder) has four
presets, *Faithful*, *Enhanced*, *High* and *Ultra*, and no Xbox preset. Because the maintainer counts the Xbox look
as faithful, its options (1, 2 with 5, and 4 below) are toggles under *Faithful*, each off by default, so *Faithful*
still adds nothing unless the player turns one on. *Enhanced* and up keep the gamma ramp and the per-pixel fog as the
same toggles, and replace the Xbox shadow map with the design's own sun shadow map (step L2); whether L2 takes the
Xbox's per-level shadow colour, offset and back-face rule as its inputs is for the renderer design (#883) to decide.
Per-pixel lights (3) are not an Xbox technique: they are *Enhanced*'s (step L3), listed here because the Xbox's light
data and caps are what they evaluate.

The options, ordered by how much they change the picture for the effort:

| # | Option | Preset | Xbox technique | Modern form | Keep | Effort | Tests |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | Character shadows | *Faithful* toggle; replaced by L2 from *Enhanced* | one 1024 × 512 D16 map, fitted to the casters, hard compare, × shadow colour | the same fitted map, with the stand-in filter below; larger maps, PCF or PCSS softness belong to L2 | casters, receivers, 20 m, colour, offset, back-face rule | M (needs the design's scene target and Coney shaders, L1) | the light view and caster list from a synthetic scene (unit); a receiver facing away is never darkened; shadow colour × lit colour on a synthetic quad (headless render, pixel values); stealth flag not read from the renderer |
| 2 | Gamma ramp | toggle in every preset | hardware gamma ramp per level, points from a table in the executable | a 1D LUT in the final pass, after the HUD as on the Xbox | the curve points per level index | S | the LUT built from synthetic points matches the piecewise-linear rule; identity with the toggle off |
| 3 | Per-pixel lights | *Enhanced* and up (L3) | per vertex in vertex shaders | the same lights, terms and `1 − d / r` falloff evaluated per pixel; clustered lists if the caps are lifted | light data, order, clamp before the pixel's texture | M (L3 of the design) | the per-pixel result averages to the per-vertex one on a synthetic mesh (the design's test) |
| 4 | Per-pixel fog | toggle in every preset | linear table fog | per-pixel linear fog in Coney's shaders | colour, start, end | S | fog factor at synthetic depths equals the linear formula |
| 5 | Brightness default 48 | with option 2 | profile default | 48 when a profile is made with the gamma ramp on, else the PS2's 40 | the 0-255 scale | S | a fresh profile reads 48 with option 2 on and 40 with it off |
| 6 | Character shader quirk | none | point lights from the bind pose | not reproduced: Coney lights the skinned position | none | none | none |

The data each needs is on the PS2 disc already, except the gamma table and the pixel shaders, which live in the
Xbox executable (see [How this was checked](#how-this-was-checked)): the shadow colour and offset are in `global.lua`
on both discs, and the light data is the PS2's. The gamma table's 33 records are numbers Coney may hold as its own
table of values (curated reference values, [LEGAL.md](repo:LEGAL.md)), or read from the player's Xbox disc at install
when the maintainer prefers that.

**The stand-in until runtime confirmation.** The [open questions](#open-questions) that a running Xbox would settle
(the shadow buffer's filtering, the blob sprite, the ramp's exact arithmetic) are marked stand-ins in the first
implementation: PCF with four taps for the filtering, the blob off when shadow maps are on, and the ramp as
described.

## Open questions

Each says how to answer it: a further read of the Xbox executable in Ghidra, or a look at the running Xbox game in
xemu over its GDB stub ([Runtime access](xbox-executable.md#runtime)), which Coney has no set-up or tooling for
yet, so a runtime answer waits for one. The last is a PCSX2 check.

- **The shadow buffer's filtering**{#q-shadow-filter}: whether the Xbox's shadow-buffer compare samples one texel or
  filters four (the NV2A can filter shadow buffers), which decides how soft the edges are. Find out: a frame in xemu,
  or the texture stage's filter state at the receivers' draw (Xbox `0x0032dd40` calls the stage-state setter for
  addressing only; the filter was not traced).
- **The blob sprite on the Xbox**{#q-xbox-blob}: whether the PS2's blob shadow (HuShadow, render-instance `+0x2a4`
  on the Xbox) is still drawn beside the shadow map. No reader of the flag was found in the analysed code (a quarter of
  the Xbox's code is outside any function). Find out: a frame in xemu, or a scan of the unanalysed code.
- **The ramp's value**{#q-ramp-value}: which two fields of a point or spot light's descriptor Xbox `0x0009a620` adds
  the record's value × 0.07 to, and what the record's last word (0 or -1) does (stored at the device's `+0xc90`).
  Find out: in Ghidra, follow the descriptor's two offsets from Xbox `0x0009a620` to the light upload (Xbox
  `0x000990e0`, `0x00098f50`) and the readers of the device's `+0xc90`; or in xemu, read a point light's descriptor
  before and after a level change.
- **The gamma ramp's index**{#q-ramp-index}: what the game state's `+0x56b4` holds (the level number is inferred) and
  what Xbox `0x0007a470` tests for record 31. Find out: in Ghidra, the writers of `+0x56b4` and the body of Xbox
  `0x0007a470`; or in xemu, read `+0x56b4` and the device's three ramps (`+0xc94` to `+0xf94`) in two or three
  levels.
- **The camera point for the 20 m test**{#q-caster-centre}: the eye or the target, from the camera's `+0x108`
  virtual. Find out: in Ghidra, the camera class's virtual table entry at `+0x108` (found through the call in Xbox
  `0x000bdaf0`); or in xemu, a breakpoint after that call in Xbox `0x000bdaf0`, comparing the vector it returns with
  the camera's eye and target.
- **Cars**{#q-cars}: whether cars receive the shadow map; they are drawn outside the receiving block. Find out: in
  Ghidra, whether the car draw binds the shadow texture stage or a `ShadowBuffer` pixel shader; or in xemu, a frame
  of a person's shadow falling across a parked car.
- **The fog coordinate's sign**{#q-fog-sign}: why the characters' vertex shader writes `+w` and the world's `−w`.
  Find out: in Ghidra, the fog render states each draw sets before its shader (Xbox `0x000bb860` sets table mode for
  the frame); or in xemu, a frame with a person and a wall at the same depth inside the fog.
- **The PS2's fire light at runtime**{#q-ps2-fire}: the code says the PS2 lights the wall with the same light; a PCSX2
  frame of a burning barrel would show it beside the Xbox video (needs a save state near one).

## How this was checked

Code only: no Xbox emulator is set up on this machine, so no claim on this page is runtime. The Xbox executable is the
NTSC-U `default.xbe` with SHA1 `fe246a74501cd10d2ce89ca5dbda5080d2b2edcc`. The Xbox executable was read in a private
copy of the evaluation's Ghidra project ([Method](xbox-executable.md#method)), served by a second ghidra-mcp server on
another port, never in the shared PS2 project; the PS2 side in the shared project. Read on 2026-10-25.

Data, counts and hashes only:

- `xbox_shaders.lua`: on the Xbox disc as entry hash `0x828115c7`, 21,913 bytes, SHA-256
  `82b799a11c0d85debbc5bf58d6cbf945c1f0f9d326a7f60bf96a4dad9e9acfbd`; 13 `XBoxCompileVertexShader` calls. **The PS2
  disc holds the same script** (name hash `0x8617e7c0`, `./ee_files/xbox_shaders.lua`); its decompile differs only in
  the decompiler's trailer line. The PS2 has no binding of that name, so it never runs there. Decompiled with
  `coney-tools lua decompile` into the scratch folder; described here, not copied.
- `global.lua`'s light table: 50 rows, each with `xshadowcolor` and `xshadowdir` (counts above); the script is
  byte-identical on both discs.
- The pixel shaders (7) and the gamma table (33 records of 40 bytes): in the Xbox executable only.
- The two reference videos the maintainer supplied (someone else's capture of a PC build of the Xbox port, at 240
  frames a second) were compared by eye: the fire's flicker between orange and yellow on the wall, the ground and two
  men, and character-shaped shadows falling the same way whatever the fire does, as this page describes. They stay in
  the task's scratch folder and are not part of the repository.
