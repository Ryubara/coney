# Lighting

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Runtime values were read
over PINE in PCSX2 2.9.94 on 2026-10-06 from the owner's quick-save slot 1 (`level99`, checkpoint 3, loaded read-only
from a copy), counts and values only.

## Purpose

How the original lights a level: the light manager and its lights, how lights are chosen for the world, for objects
and for humans, what the level scripts set (lights, fog, world ambient, colour offset), what the brightness option
changes, the coronas, flicker and the humans' blob shadows, and the values `level99` runs with, so that Coney can be
checked against them. Where the world's lights end up in the frame is on [The streamed world](world.md#a-frame) and
[Level loading](level-loading.md#render-order); the fog's render states on [Graphics](graphics.md#device-object);
screen tints and looks on [Graphics](graphics.md#screen-effects); scene lights on [Scenes](scenes.md#camera).

In one paragraph: every light is a RenderWare light owned by the `LightManager`. The constructor makes three: the
**world ambient** (lights only the world), a **pulsing ambient** for some objects and a **glow** point light for
characters. Level scripts add the rest with `SetLight`: `global.lua` a moonlight, a reflected light and an object
ambient from the level's `LightData`, the level script its lamps (point lights, many with a **corona** sprite). Every
ambient and directional light gets the **brightness** (the profile's light setting, 40/255 by default) and the
script's colour offset added to its colour. Each frame and viewport the manager culls the point lights against the
camera and sorts the rest into lists by what they light; each atomic then gets up to 8 (world) or 3-6 (objects,
humans, by distance) lights, the nearest-overlapping point lights last, and RenderWare's PS2 pipeline adds the lit
colour to the prelighting. In `level99` the world is lit only by the 0.227 grey world ambient and two flickering
lamps; humans get a cold moonlight, a weak warm light from below, a 0.22 ambient and up to 3 lamps.

## Original structure

`c:/Warriors/Source/Graphics/LightManager.cpp` (string at `0x005522b0`; [Source map](source-map.md)). Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x0017d640` | `LightManager` constructor | 0xc0 bytes at `0x0050cce4`; the three built-in lights; brightness 40/255 | confirmed (code) |
| `0x0017d338` | `LightManager_Init` | a pool of 512 light records (0x50 bytes), the corona sprite batches | confirmed (code) |
| `0x0017d510` / `0x0017d578` | `LightManager_AddLight` | takes the next free record and builds it from a descriptor | confirmed (code) |
| `0x0017d598` | `LightManager_RemoveLight` | returns a record | inferred |
| `0x0017c508` | `Light_InitFromDescriptor` | record from descriptor; RenderWare light; light bugs | confirmed (code) |
| `0x0017c700` / `0x0017c840` | `Light_GetDescriptor` / `Light_ApplyDescriptor` | read / write a light through a descriptor; the colour offsets | confirmed (code) |
| `0x0017ef20` | `Light_SetFromScript` | `SetLight`'s long form | confirmed (code) |
| `0x0017d0b0` | `Light_SetFlickerTiming` | `SetLightFlicker`'s worker | confirmed (code) |
| `0x0017ea60` | `LightManager_BeginViewport` | the two ambients' colours, then the cull | confirmed (code) |
| `0x0017d880` | `LightManager_CullForViewport` | sorts the lights into the lists below | confirmed (code) |
| `0x0017caa8` | `Light_UpdateFlickerAndCorona` | the flicker step and the corona sprite of a visible light | confirmed (code) |
| `0x0017de10` | `LightManager_SelectLights` | the lights of one atomic | confirmed (code) |
| `0x0017e810` | `LightManager_UploadSelected` | hands the selection to RenderWare's PS2 light upload | confirmed (code) |
| `0x00476cc8` | `RwPS2_QueueLightForVU1` | one light's colour and vectors into the VU1 light block | confirmed (code) |
| `0x0017ed60` | `LightManager_SetColourOffsets` | sets brightness `+0x90` and offset `+0xa0`, re-applying them to every ambient and directional light | confirmed (code) |
| `0x0017ec38` / `0x0017ec80` | set brightness / set colour offset | callers of `0x0017ed60` | confirmed (code) |
| `0x0017f218` | `LightManager_SetWorldAmbient` | `SetWorldAmbient` | confirmed (code) |
| `0x001b4838` / `0x001b48d0` | `Gamma_Set` / `Gamma_Get` | the brightness option | confirmed (code) |
| `0x00411990` | `World_RenderSectorAtomic` | lights a streamed sector | confirmed (code) |
| `0x0017fd78` | `ObjectRender_Draw` | lights a world object | confirmed (code) |
| `0x00174320` | `HumanRender_Draw` | lights a human, its colour and its blob shadow | confirmed (code) |
| `0x0028ee88` | `Brain_SetHiddenInShadow` | the brain's `+0x2d4` flag that dims a human | confirmed (code), meaning inferred |

## The light manager {#manager}

### Fields {#manager-fields}

Confirmed (code) at the constructor and `0x0017d338`; values confirmed (runtime) in `level99`.

| Offset | What |
| --- | --- |
| `+0x00` | the record pool (512 × 0x50 bytes) |
| `+0x04` / `+0x08` / `+0x0c` | the record pointer array, its capacity (512) and the number in use (`level99`: 96) |
| `+0x10`, `+0x12` | resource instance numbers 7 and 8 |
| `+0x1c` | set to 1 after a cull |
| `+0x20`, `+0x30` | per-viewport lists of visible point lights: every one, and those that light the world |
| `+0x40` | light A: the **world ambient** (ambient, lights the world, on) |
| `+0x44` | light B: the **pulsing ambient** (ambient, lights objects, off) |
| `+0x50` | the world ambient colour (0 until `SetWorldAmbient`) |
| `+0x60`, `+0x70`, `+0x80` | light B's two colours (black, 0.25 grey) and its half period, 300 ms |
| `+0x84` | the **glow** (point light, radius 0.4, white, lights objects, off) |
| `+0x90` | the **brightness** (RGB, 40/255 = 0.157 by default; [Brightness](#brightness)) |
| `+0xa0` | the **colour offset** (RGB, 0 by default; `SetGammaOffset`) |
| `+0xb0`, `+0xb4` | the corona sprite batches of viewports 0 and 1: resource instances 7 and 8, over the `lighting` sheet ([GUI](gui.md#resource-instances); confirmed (runtime): `+0xb0` is instance 7) |

### The light record (0x50 bytes) {#record}

Confirmed (code) at `0x0017c508` and `0x0017c700`; the meanings of the flicker bytes at `0x0017caa8` and `0x0017d0b0`.

| Offset | What | From `SetLight` |
| --- | --- | --- |
| `+0x00` | 1 = in use | |
| `+0x04` | the RenderWare light (`RpLight`): type byte `+0x01`, frame `+0x04`, radius `+0x14`, current colour `+0x18` (RGBA floats), cone `+0x28` | type, pos, dir, colour, radius, cone |
| `+0x08` | **effects** word: bit 0 light bugs, bits 1-4 the flicker mode ([Flicker](#flicker)) | 12th, `priority` |
| `+0x0c` | the corona's **height** above the light, metres | 8th, `p` |
| `+0x10` | how far the corona is **pulled towards the camera**, metres | 9th, `flickerA` |
| `+0x14` | the corona's **size**, metres (also the cull radius of a light with radius 0) | 10th, `flickerB` |
| `+0x18`-`+0x28` | flicker counters and timings ([Flicker](#flicker)) | `SetLightFlicker` |
| `+0x2a` | corona drawn this frame (-1 = no corona) | |
| `+0x2c` | the **corona**: rectangle 0-5 of the `lighting` sheet, -1 none | 13th, `flicker` (1-6 → 0-5, 0 → -1) |
| `+0x2e` | **what it lights**: bit 0 objects (and humans), bit 1 the world | 11th, `lights` |
| `+0x30` | on | 14th, `state` |
| `+0x34` | flicker timer, ms | |
| `+0x40` | the **base colour** (what the script set; flicker scales it into the RpLight) | 5th, `colour` |

So the four `SetLight` arguments the [Lights](../references/lights.md) list did not explain are corona parameters
(`p`, `flickerA`, `flickerB`) and an effects word (`priority`); the 13th is the corona, not a flicker. Confirmed (code),
and confirmed (runtime): in `level99` the lights the list shows with "flicker" 4 hold corona 3, and the corona
parameters are one of six sets, (height, pull, size) = (0, 0.5, 2.2), (0, 0.5, 3.2), (0, 0.25, 1.5), (-1.15, 0.6, 4.0),
(-0.8, 0.5, 2.5), (-0.4, 0.1, 1.0).

**Axes.** A script gives positions and directions in the game's axes (z up); `0x0017c840` stores them in RenderWare's
as `(x, z, -y)`. Confirmed (code), and confirmed (runtime): the lamp the script puts at (-286.13, 122.79, 5.49) has its
frame at (-286.13, 5.49, -122.79).

### Light kinds {#kinds}

`SetLight`'s type 0-3 becomes RenderWare type `0x80` point, `0x81` spot, 1 directional, 2 ambient (`0x0017ef20`,
confirmed (code)). A point light lights within its radius; a light with radius 0 lights nothing and exists for its
corona (41 of `level99`'s 90 lamps). No script light in `level99` is a spot light.

**Disc check (corroboration, a scan of the compiled Lua files for the binding names):** `SetLight` in 60 files of 52
levels (5,283 long-form calls, listed in [Lights](../references/lights.md)), `SetLightFlicker` in 9 levels,
`SetWorldAmbient` in 3 files of 2 levels, `SetGammaOffset` in 1 file.

### Colour: brightness and offset {#colour}

When an **ambient or directional** light's colour is set (`0x0017c840`), the manager adds
`max(0, brightness + offset)` (component-wise, `0x0017ecc8`) to the script's colour; reading it back subtracts it
again (`0x0017c700`). Changing either value re-applies every ambient and directional light (`0x0017ed60`). Point and
spot lights are never offset. Confirmed (code); confirmed (runtime): `level99`'s moonlight, base (0.07, 0.10, 0.18),
has the RpLight colour (0.227, 0.257, 0.337).

- **World ambient (light A)**, each viewport (`0x0017ea60`): colour = `+0x50` + offset terms. `SetWorldAmbient(r, g, b)`
  sets `+0x50 = (r, g, b) + 0.07` (`0x0017f218`), so with brightness 40 the world ambient is `rgb + 0.227`.
- **Pulsing ambient (light B)**, each viewport: `lerp(+0x60, +0x70)` by `(t mod 300) / 300` of the game clock in ms,
  the ends swapping every 300 ms: a triangle wave between black and 0.25 grey with a 600 ms period, plus the offsets.
  It stays off, but [object selection](#select) uses it for some objects.

## Choosing lights {#choosing}

### Per viewport: the cull {#cull}

`LightManager_BeginViewport` (`0x0017ea60`) sets the two ambients' colours, clears the lists (`0x0017e7e8`) and, once
per viewport, `0x0017d880` walks every record. Confirmed (code):

1. **Ambient and directional lights** (RenderWare type 1 or 2): with flag 2 and on, into list A (`0x00715324`, the
   world's); with flag 1 and on, into list B (`0x00715348`) and list C (`0x0071536c`); the first light with flag 1 that
   is off (light B, the pulse) into list C alone.
2. **Point and spot lights that are on**: the light's radius (or `+0x14` when it is 0) is `r`; skip it when
   `|pos - camera|² ≥ (far × 0.75)² + r²`, `far` being the camera's draw distance (115 for the player camera), or when
   the sphere lies wholly outside either of the camera's first two frustum planes.
3. A visible light with radius 0 only updates its flicker and draws its corona (if it has one). A visible light with a
   radius joins the viewport's list `+0x20` and, with flag 2, list `+0x30`; then its flicker and corona.

### Per atomic: the selection {#select}

`LightManager_SelectLights(distSq, manager, sphere, flags, pointLights, pulse, human)` (`0x0017de10`) fills the
upload list (`0x00715300`, at most 8). Confirmed (code):

1. Nothing when the global `0x005e5374` is 0.
2. **World** (`flags` bit 0 clear): copy list A; the cap is 8.
   **Objects** (bit 0 set): copy list B (`pulse` = 0) or list C (`pulse` = 1); the cap is
   `6 - min(int(distSq / 1600), 3)` (list B) or `6 - min(int(distSq / 1600), 2)` (list C). With `distSq` the squared
   distance to the camera, that is 6 lights within 40 m, 5 to 56.6 m, 4 to 69.3 m, then 3 (list C keeps 4).
3. If `pointLights`: the **glow** first, for a `human` whose byte `+0x647` is above 10 ([Glow](#glow)). Then every point
   light of `+0x30` (world) or `+0x20` (objects) whose sphere meets the atomic's: score
   `|light - centre|² - (sphereRadius + lightRadius)²`, kept when ≤ 0. While there is room it is appended; when full,
   it replaces the kept point light with the **smallest** score if its own is smaller still. As written, a full list
   is therefore not the N nearest: it keeps the first ones found plus the deepest overlap (the comparison is against
   the minimum, read twice; confirmed (code)).

`LightManager_UploadSelected` (`0x0017e810`) then queues each chosen light with `RwPS2_QueueLightForVU1`
(`0x00476cc8`): a block header with two scales of 1.0, then per light its colour (RpLight `+0x18`) and type, plus the
direction (directional), position and radius (point) or position, radius, direction and cone (spot); at most `0x7c`
quadwords. Confirmed (code).

**Who calls it with what**, confirmed (code):

| Caller | `distSq` | flags | point lights | pulse | human |
| --- | --- | --- | --- | --- | --- |
| a streamed sector (`0x00411990`) | 0 | 2 (world) | yes | no | |
| the background (`0x0040d0a8`, [Level loading](level-loading.md#render-order)) | 0 | 2 | no | no | |
| a world object (`0x0017fd78`) | its squared distance | 1 | unless its radius < 0.25 (and not flag `0x10`) | object flag `0x80`, outside some states | |
| a human (`0x00174320`) | squared distance `+0x334` | 1 | unless hidden ([Humans](#humans)) | no | yes |
| `0x00172c70` (the resource manager's list `+0xc98`, [The streamed world](world.md#a-frame)) | 0 | 1 | yes | no | |

## World lighting {#world}

**Prelit and lit.** The streamed world's sectors carry prelighting colours and normals
([The streamed world](world.md#pipelines)) and are also lit at runtime with the world's list (light A, world
directional lights, world point lights). Confirmed (runtime) in `level99`: with `+0x50` set so that light A is black,
the scenery stays visible from its prelighting alone; with the normal 0.227 a lit wall's screen brightness doubles
(mean about 29 → 58 of 255); with 0.927 it reaches about 117, while the road, brighter in its prelighting, moves
only from 40 to 53 (saturating). The humans and the sky do not change.

**The maths** (inferred: RenderWare 3.7's PS2 lighting, whose light block `0x00476cc8` fills; the VU1 microcode was
not read): per vertex, `colour = prelight + material × (Σ ambient + Σ directional × max(0, n · -L) + Σ point ×
max(0, n · -L) × (1 - d / radius))`, clamped, on the GS's scale where 0x80 is 1.0; the GS then modulates the texel by
it ([Vertex colour range](world.md#pipelines): textured material colours are halved to that scale). The runtime check
above fits an additive, saturating sum.

**In `level99`** no directional light lights the world (all three of `global.lua`'s have flag 1), so the world is its
prelighting plus 0.227 grey, plus two lamps that light both (flags 3, radius 3 and 6 m, flicker mode 1). Confirmed
(runtime).

**Fog and background.** One colour: the device's `+0x440` ([Fog](world.md#fog)); fog starts at the device's `+0x444`
× the draw distance. `level99`: `SetFogColor(0.05, 0.05, 0.02)` gives (12, 12, 5, 255); fog start 0.5. Confirmed
(runtime).

**Time of day.** None: each level's lights are set once by its scripts (`LightData` in `global.lua`, then the level's
lamps); nothing in the manager changes them over time except flicker and light B. Inferred (no timer-driven caller of
the colour setters found).

## Coronas and flicker {#coronas}

### Coronas {#corona}

For every visible light with a corona, `0x0017caa8` adds one sprite to the viewport's batch (`+0xb0`), confirmed
(code):

- **Where**: the light's position raised by `+0x0c` metres, then moved `+0x10` metres towards the camera.
- **Size**: `+0x14` metres, square. **Rectangle**: `+0x2c` of the `lighting` sheet (6 rectangles).
- **Colour**: the light's current colour × 255 (so it flickers with the light), alpha from the colour's alpha; within
  8 m of the camera the alpha is capped at `31.87 × distance` (0 at the camera, 255 at 8 m). Below 11 it is not drawn.
- A per-rectangle switch (`0x00552308`: rectangles 1-4 on, 0 and 5 off) passes `distSq / 640` with the sprite,
  otherwise 0 (what the batch does with it is not traced).

Coronas are separate from the **level world's glows**, the fixed `propglow` halos in the `.lev`
([Level loading](level-loading.md#the-level-object)).

### Flicker {#flicker}

The effects word's bits 1-4 (`+0x08 & 0x1e`) select a mode; the step runs only while the light is visible (from the
cull). Confirmed (code); `rand(n)` is the game's random 0 to n-1 (`0x003353b8`):

| Mode | Set by | Each time its timer runs out |
| --- | --- | --- |
| 2 | `SetLight`'s effects word | timer = `rand(200)` ms; colour = base × `rand(100)` / 100 (alpha kept) |
| 4 | `SetLight`'s effects word | timer = `rand(42)` ms; the base becomes the current colour × `(72 + rand(28)) / 100` (the RpLight is not set here) |
| 6 | `SetLightFlicker` with `p3` = 0 | a burst: while the burst count `+0x18` lasts, timer = `rand(p9)` and colour = base × `rand(p10)` / 100; then a pause of `p5 + rand(p6)` ms at the base colour and a new count `p7 + rand(p8)` |
| 8 | `SetLightFlicker` with `p3` ≠ 0 | blink: on (base) for `p1 + rand(p2)` ms, then dimmed (base × `p10` / 100) for `p3 + rand(p4)` ms; starts on, or after `p6` ms when `p6` ≠ 0 |

`SetLightFlicker(light, p1 … p10)` stores `p1`→`+0x24`, `p2`→`+0x20`, `p3`→`+0x26`, `p4`→`+0x22`, `p5`→`+0x28`,
`p6`→`+0x1c`, `p7`→`+0x1a`, `p8`→`+0x1b`, `p9`→`+0x1e`, `p10`→`+0x19` (`0x0017d0b0`). For a light with radius 0 it
instead spawns the particle `sub_flashing_light` with the timing, the corona and the colour, and removes the light.
Effects bit 0 spawns `part_light_bugs` at the light when it is made (`0x0017c508`; 2 lamps in `level99`).

## Humans {#humans}

`HumanRender_Draw` (`0x00174320`), confirmed (code):

- **Lights**: objects' lists (`flags` 1, no pulse) with the glow; humans are **not** lit by the world ambient (light A)
  nor by world-only lights. In `level99` that is the moonlight, the reflected light and the object ambient (3 lights),
  then up to 3 point lamps within 40 m (fewer further away).
- **Model colour**: the render instance's colour word `+0x28` (`HuColor`, `0x00239100`) × a dimming factor; while the
  brain's `+0x2d4` is set the factor falls from 1.0 to 0.5 over 250 ms (else it is 1.0), and **point lights are
  skipped**. `+0x2d4` is set by `Brain_SetHiddenInShadow` (`0x0028ee88`) from the ground check `0x0023eab8` when the
  triangle under the feet has flag bit 4 ([Collision](collision.md#triangles)): the player hiding in a shadow
  (inferred; [AI](ai.md#block) shows the same flag shrinking how far an attack is noticed to 2 m).
- **Alpha**: faded beyond the distance `0x0050cc64` and over the first second after `+0x30`.
- **Shadow**: when the human's shadow is on (`HuShadow`, `+0x2b4`), in viewport 0 or 1 and not in some states, one
  **blob** sprite: rectangle 40 of `part_page1` (resource instances 9 and 10), colour (10, 10, 10, 128), on the ground
  found by a ray from 0.25 m above the human, 4 m down (`WorldManager_RayCast`), turned to the ground's normal and
  scaled by the human's `+0x580`-`+0x588`, 0.05 m above it. No projected or light-dependent shadows; `SetShadowColor`,
  `SetShadowLightOffset` and `EnableShadow` are empty ([bindings](../references/bindings/effects.md)). The sprite goes
  through `Instance_AddSprite`, so its colour is on RenderWare's 0-255 scale ([GUI](gui.md#sprite-colours)): alpha
  128 is **half transparent**, and the shadow is faint (confirmed (runtime), PCSX2 2.9.94, `level99`: a soft, barely
  darker patch under Rembrandt's feet). Rembrandt's `+0x580`-`+0x588` read 1.195, 1.097, 1.0 (confirmed (runtime)).
  The reticule update turns a player's shadow on every update and the Rumble team disc turns it off
  ([HUD: the health rings](hud.md#the-health-rings)), which are drawn after it, over it.

### The glow {#glow}

When a human's byte `+0x647` is above 10, the manager's glow light (`+0x84`) is placed at the human (its bone
`+0x92`'s position, raised by half the model's scale) with colour = the human's bytes `+0x644`-`+0x647` / 255, and is
the human's first light (`0x0017de10`). Confirmed (code). What sets `+0x644` (written in `Human_HandleMessage` at
`0x002475a8`, cleared at `0x0022eb40` and `0x002325e0`) is not traced.

## The brightness option {#brightness}

`PM_Light` ([Front end](frontend.md)) and the options call `Gamma_Set(v)` (`0x001b4838`) with v from 0 to 100 in
steps of 5, 40 by default: `W_GameState + 0x57a4` = v and the manager's brightness `+0x90` = v / 255 in R, G and B.
So the option **adds v/255 to every ambient and directional light** (the world ambient, the objects' and humans'
ambient and directional lights): 0 to 0.39, 0.157 by default. It does not touch point lights, the prelighting, the sky,
the fog or the screen. `SetGammaRamp` is empty. Confirmed (code); `level99` reads 40 and `+0x90` = 0.157 (confirmed
(runtime)). The setting's storage is on the save page.

## Level 99's values {#level99}

Read at runtime at checkpoint 3 (confirmed (runtime)); brightness 40, offset 0.

| Light | Kind, lights | Script colour | RpLight colour | Direction (game axes) |
| --- | --- | --- | --- | --- |
| A, world ambient | ambient, world | `+0x50` = 0.07 | 0.227 grey | |
| B, pulse | ambient, objects (off) | | 0.157-0.407 | |
| glow | point 0.4 m, objects (off) | white | | |
| moonlight | directional, objects | (0.07, 0.10, 0.18) | (0.227, 0.257, 0.337) | (0.391, -0.474, -0.789) |
| reflected | directional, objects | (0.03, 0.02, 0.01) | (0.187, 0.177, 0.167) | (0.496, 0.552, 0.670), upwards |
| object ambient | ambient, objects | (0.055, 0.065, 0.075) | (0.212, 0.222, 0.232) | |
| 90 lamps | 49 point lights (7 with a corona), 41 coronas alone | warm and cold whites, 0.78-1.0 | | [Lights: level99](../references/lights/level99.md) |

Of the lamps, 88 light objects only and 2 both objects and the world (flicker mode 2); 2 have light bugs. Coronas by
rectangle: 0 ×20, 2 ×7, 3 ×10, 4 ×11. Fog colour (12, 12, 5), fog start 0.5, draw distance 115.

A reference screenshot at this spot (window capture, kept outside the repository) shows a dark blue night: a black-blue
sky, walls lit flat grey-brown, the player lit cold from above.

## Coney's implementation {#coneys-implementation}

`repo:src/graphics/light_manager.h` is the manager (pool, built-in lights, offsets, cull, selection, flicker, coronas);
the lighting bindings (`repo:src/scripting/lighting_bindings.h`) fill a level's lights and fog, kept by gameplay;
`repo:src/platform/scene_lighting.h` hands each atomic's selection to librw, whose GL3 lighting adds ambient,
directional and point light to the prelighting and clamps. Sectors, the background and humans are lit as the callers
above say; humans also get the shadow dimming and a blob shadow (`repo:src/graphics/human_lighting.h`). A disc test
checks `level99`'s values against the table above (all match). Coney's choices:

- The flicker steps with the simulation (game time per step) for the lights the cull would keep, drawing from the
  manager's own random sequence, not the game's; a burst starts with its pause.
- Sprite colours are on the GS's scale (0x80 = 1.0) and doubled for librw, as the prelighting is; coronas and shadows
  are drawn with Z test and no Z write after the world.
- The blob shadow is 1 m square (`+0x580` is not researched); the shadow-ground check uses the shadow's ray.
  **Known gap**: its colour goes through the doubling above, so its alpha 128 becomes 255 and the shadow draws as an
  opaque black shade; the original's is half transparent ([Humans](#humans)). This shade is what shows under Coney's
  humans; the health rings are not built ([HUD](hud.md#the-health-rings)).
- A light with radius 0 given `SetLightFlicker` keeps its corona and flickers (no `sub_flashing_light` particle);
  light bugs are not spawned. No object uses the pulse or the glow yet.
- A sandbox (no level scripts) gets a stand-in ambient and directional light; the front end's background draws with the
  manager as it starts and black fog, without running `level100.lua`'s lights; the character viewer and the reference
  renders keep their fixed lights (`repo:src/platform/character_lights.h`).

## Open questions

- The VU1 microcode's lighting: the exact point-light falloff, clamping and how prelighting and material combine.
- What sets a human's glow colour `+0x644` (a rage or power-up effect?).
- What object flag `0x80` (the pulse, light B) marks.
- Mode 4's flicker: the RpLight's colour is not updated by it; whether another step applies the base.
- The value passed with rectangles 1-4 of the coronas (`distSq / 640`).
- The blob shadow's size: the sprite's base size and what sets the human's `+0x580`-`+0x588` (Rembrandt 1.195, 1.097,
  1.0).
