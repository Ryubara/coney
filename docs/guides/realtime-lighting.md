# Realtime lighting design

The design for Coney's lighting beyond the vertex-lit picture of the PS2: realtime shadows, per-pixel lighting and
reflections over the game's own lights, a **lighting lab** in the debug menu that shows how a frame is lit, the
**tests** that hold Coney's lighting to the original's, and **Coney's own sun** for Coney Studio, a light a project
rotates and that casts light and shadows beside the game's lights. It extends the [Enhancements plan](enhancements.md)
and does not repeat it. The [renderer design](../engine/renderer.md) owns the decisions this page builds on: the
backend, the presets, the one shadow source and the order of the work ([Lighting and
shadows](../engine/renderer.md#lighting)); this page owns the lighting model, the lab, the tests and the sun.

**Status: design only; nothing on this page is built.** Enhancements may be built now, as options beside the faithful
look (AGENTS.md, "Faithful first", since 2026-10-25), and what the Xbox port does differently in its lighting and
shadows ([Xbox lighting and shadows](../research/xbox-lighting.md)) is one of its sources. Every enhancement ships
**off** and is forced off in test mode, so tests and reference screenshots show the *PS2* preset, the faithful image
(the name the renderer design gives what this page called *Faithful*).

## What the original gives us

All of it from [Lighting](../research/lighting.md) unless another page is named.

| Fact | Consequence |
| --- | --- |
| The streamed world is **prelit**: a baked colour and a normal per vertex, plus runtime lights on top (the world ambient, world directional lights and up to 8 point lights per sector). With the ambient at black the scenery stays visible from the baked colour alone | The baked colour is part of the final image and already holds lighting. New lights cannot simply be added: surfaces would be lit twice |
| Humans, objects and cars are vertex-lit by 3-6 lights chosen by distance: moonlight, a weak "reflected" directional light from below, an ambient and nearby lamps. Lamps flicker and get a corona sprite. The choice is confirmed in the code and, call for call, in the running game ([Selection](../research/lighting.md#select)) | The light **data** (type, colour, radius, flicker, what it lights) and the choice per atomic are complete and can feed a per-pixel path unchanged, and a trace of the original can check Coney's choice exactly |
| The final colour is `min(1, prelight + light) × material`, point lights falling off as `1 - d / radius`, no overbright ([World lighting](../research/lighting.md#world)) | A CPU reference of that formula gives every test an expected vertex colour |
| Shadows are one faint 1 m **blob sprite** per human. `SetShadowColor`, `SetShadowLightOffset` and `EnableShadow` are empty on the PS2; the Xbox port fills them and draws a shadow map from one per-level shadow light ([Xbox lighting: the shadow light](../research/xbox-lighting.md#shadow-light)). The stealth "hidden in shadow" flag is separate: set by the brain from a ground check ([Stealth](../research/stealth.md#shadow-ground)) | Real shadows are an addition with a source the game already names. They must never feed the stealth flag, or the AI changes |
| There is no reflection code. The "reflected light" is a plain upward directional light. MatFX **environment-map** materials exist (effect 2, world pipeline `0x30087`, [World](../research/world.md#pipelines)) | The only reflective surfaces the data marks are those materials. Wet streets and shop windows have no marker in the data |
| 88 of `level99`'s 90 lamps light only objects and humans, not the world | Lamp shadows on the world would show light that the original's world never received |
| Draw distance 115 m, fog from half of it ([World](../research/world.md#a-frame)) | A shadow map needs to cover the player's surroundings, not the whole view |

## Goals and non-goals

Goals: a lab that shows every light of a frame and what each surface got; tests that prove Coney chooses and applies
the original's lights; contact and cast shadows from the level's shadow light; per-pixel lighting of humans, objects
and vehicles that keeps the original's light choice; believable reflections on the surfaces the game already marks as
shiny, then on a short list of surfaces Coney tags itself; a sun of a project's own, rotatable, with its shadows.

Non-goals: global illumination or path tracing that "relights" the baked world; changing the gameplay-visible
lighting of the game's levels (stealth, the lit/unlit AI rules); shipping any game data or Coney-made replacement of
it; a physically based material rework of the PS2 textures (that is the upscaling and texture-pack work in the plan).

## Architecture

The renderer design moves Coney's drawing from librw's GL3 pipelines onto its own renderer over SDL3's GPU API, and
puts every enhancement on that renderer only ([Order](../engine/renderer.md#order)): the lighting frame first (its
item 19), then, once the GPU renderer has a scene target (item 22), the light caps, shadow maps and per-pixel lights.
The seam is the plan's rule: no renderer call outside the platform renderers. Game code produces a **lighting frame**
(below); a renderer consumes it, and the lab reads it.

### The lighting frame

Per viewport, built by the game side from the `LightManager`'s existing selection, with no new simulation state
([#1211](issue:1211) builds it; this page adds each atomic's lights and the sun to what it holds):

- every light record of the frame (kind, what it lights, colour with brightness and offset applied, position or
  direction, radius, corona, flicker value of this step) and the world's list;
- each atomic's chosen lights, in the order the selection made them, with the inputs it used (the atomic's sphere,
  its squared distance, the caller's flags): the same rows a trace of the original records
  ([Selection](../research/lighting.md#select)), so the lab shows them and the comparison test diffs them;
- the **shadow light**: on a game level the level's own (`SetShadowColor`, `SetShadowLightOffset` from `global.lua`),
  on a Coney level the project's [sun](#sun), or none;
- the project sun's direction, colour and intensity when the level has one;
- the fog and background colours, and the world ambient;
- the camera, interpolated between simulation steps like everything drawn above 30 Hz.

Because the frame is derived from state the faithful path already computes, turning enhancements on or off cannot
change the simulation. A test compares the frame with and without them.

## Lighting model {#lighting-model}

Three layers, drawn in this order:

1. **Faithful layer.** Exactly the original: baked vertex colour plus the vertex-lit runtime lights, in the
   original's colour scale (`0x80` is 1.0). In the *PS2* preset this is the whole image.
2. **Per-pixel lights** (*Enhanced*). The same lights and the same selection, evaluated per pixel instead of per
   vertex, so lamp pools and moonlight follow the surface. Two rules keep the look: the result must equal the
   vertex result on average over a triangle (a test checks this on synthetic meshes), and the clamp comes before
   the material as in [Lighting](../research/lighting.md#world) so highlights do not wash out.
3. **Shadows and reflections** multiply or add on top (below).

**The baked light problem.** The world's baked colour already contains some occlusion and light direction, so a
shadow can only darken it plausibly, not relight it. A receiver's final colour is multiplied by
`1 - s * (1 - visibility)`, where `visibility` comes from the shadow map and `s` is the strength. On a game level `s`
comes from the level's shadow colour, `1 - shadowColour` (0.23 in most levels, the Xbox's multiply,
[Receiving](../research/xbox-lighting.md#shadow-receive)); on a Coney level from the sun's shadow strength.

## Shadows

| Part | Design |
| --- | --- |
| Source | The level's shadow light, one per level ([One shadow source](../engine/renderer.md#lighting)): `global.lua`'s offset and colour on a game level, the project's [sun](#sun) on a Coney level. A level that turns shadows off keeps the blob |
| Casters, receivers, coverage, filtering | Per preset, as the renderer design's [Presets](../engine/renderer.md#presets) table gives them. Never the streamed world as a caster: its shadows are baked |
| Coverage, in any preset | Maps fitted round the player's surroundings, not the whole view, and snapped to texel steps so they do not shimmer while the camera moves |
| Facing away | A surface facing away from the light is never darkened (the Xbox's rule) |
| Blob | Off when real shadows are on, so a person does not get two |
| Lamps | Point-light shadows for fires and lamps in *High* only (renderer item 31): the data says lamps do not light the world |
| Stealth | Untouched. The brain's shadow flag keeps its own ground check; a test makes sure it does not read the renderer |

Cost: one extra depth pass over the casters near the player, and the receiver lookup in Coney's world and object
shaders. Budget on a mid-range 2024 GPU at 1080p: under 1.5 ms (a target, to be measured, not a result).

## Reflections {#reflections}

Three tiers, each usable on its own. The data limits them: only the environment-map materials are marked.

1. **Environment-map materials, done properly.** The materials the world pipeline `0x30087` already marks sample a
   reflection texture through a matrix from the camera. Draw them with a runtime-captured cube map instead of the
   original's texture, and fade it with the Fresnel term. The faithful look stays the default.
2. **Probes.** Low-resolution cube maps captured at level load and refreshed slowly at a few spots in the streamed
   sectors, from the faithful image of the scene, so they hold no game data. Used by tier 1 and for the tagged
   surfaces below. Where probes go, and how they stream with the sectors, is the open question.
3. **Tagged wet and glossy surfaces.** A Coney-owned table, keyed by texture name (never game data), says which
   surfaces are wet road, glass or polished floor and how rough. Those get screen-space reflections from the scene
   target with a probe fallback where the ray leaves the screen. Roughness blurs the result. Rain and wet
   weather are a separate question; the game's own rain, if any, would be the signal.

## Quality ladder {#quality-ladder}

The presets are the renderer design's four, *PS2*, *Xbox*, *Enhanced* and *High*, and their one definition is its
[Presets](../engine/renderer.md#presets) table: per-vertex lights with the PS2's or the Xbox's caps, then per-pixel
lights, the shadow maps above, environment-map materials, then GTAO, probes and tagged reflections. Ray-traced shadows
and AO are in no preset (SDL3's GPU API has no ray tracing). The lab's views and the sun work in every preset; the
sun's shadows need a preset with a shadow map ([The sun](#sun)).

## The lighting lab {#lab}

A **Lighting** page of the [debug menu](debug-menu.md#pages), so an engineer sees exactly how a frame is lit, in Coney
and side by side with a trace of the original. Like every debug feature it is data in `src/debug/` (pages of items:
toggles, choices, watches, logs and actions), drawn by every front end, and it changes no simulation state: its
views are switches on the drawing side of the lighting frame, and its edits are queued between steps like the Entity
inspector's ([Adding a feature](debug-menu.md#adding-a-feature)). In test mode every view is off.

| Group | What it shows or does |
| --- | --- |
| Frame | **Hold lighting**: the flicker, the pulse and every atomic's selection stay as they are on this step while the game is paused, so one frame can be studied; *Step one* on the Time page moves it on by one step. **Light from**: the game camera (the default) or the view camera; the original culls and caps from the player camera, so with the free camera flying the lighting stays the game's. **Dump this frame**: writes the lighting frame, every light and every atomic's selection, as rows in the same columns as the original's trace, to the run log folder, for the comparison tool |
| Counts | Watches: light records in use (of 512), visible point lights (objects, world), coronas drawn, atomics lit, humans by number of lights (3 to 6), selections whose list was full |
| Lights | Every light record, one line each: index, kind (ambient, directional, point, spot, corona only), what it lights (world, objects, both), on or off, base colour and current colour (with brightness and offset), radius, corona (rectangle, height, pull, size), flicker mode and its timer, and its **source**: built in (A, B, the glow), `global.lua`'s `LightData`, the level script and function that called `SetLight`, a chunk `0x4B` glow, or a light task (`sub_fire2_light`). Choosing one marks it in the scene, moves the view camera to it, and offers on, off, colour and radius as debug edits |
| Selection | For the Entity inspector's selection (a human, object or car) or the sector under the view's centre: its lights this step in order, each with its kind, its score, where it came from (the copied list, the glow, the point lists) and whether it replaced another; the cap and the distance that set it; the point lights that overlapped and were dropped. Lines from the atomic's sphere to each chosen light |
| Layers | Toggles, each a switch on the drawing side: prelighting, the world ambient (A), the objects' ambient and directional lights, point lights, flicker (lights held at their base colour), coronas, blob shadows, the brightness term, the sun. A **view** choice: final; prelight only; runtime light only; runtime share (false colour of light ÷ (prelight + light) per vertex); saturation (vertices at the 1.0 clamp in magenta); lights per atomic (0 to 8 as a heat scale) |
| Debug draw | Point lights as spheres of their radius in their colour, corona-only lights as crosses, directional lights as arrows, the cull's reach round the camera, each atomic's selection sphere |
| Shadows | Once the shadow maps exist (renderer items 28 and 30): the map as an inset, the casters' boxes and the light's frustum, the cascades tinted over the scene, the texel size, the multiply on and off, and the sun's direction and time |

The **free camera** is the Camera page's; the lab adds only *Light from*. **Comparing with the original** is two
switches beside the page's own: a dumped frame and a trace of the original from the same place open in
`coney-tools trace diff`, and the *Selection* group can show the original's list for the matched atomic beside
Coney's when a trace is loaded (`--light-trace FILE`). The original's frame comes from a PCSX2 save state, so the lab
never needs the emulator running.

## Testing {#testing}

Every test below says where its expected values come from ([Cite the expected values](testing.md#citations)): the
rule and the runs on [Lighting](../research/lighting.md#select), the formula in [World
lighting](../research/lighting.md#world), the Xbox's rules on [Xbox lighting](../research/xbox-lighting.md).
Pictures and traces of the game stay in the scratch folder; only Coney-made synthetic scenes are in the repository.

### Faithful comparison: the light choice {#test-selection}

| Test | What it checks | Where | Result reported |
| --- | --- | --- | --- |
| Selection rules | Synthetic scenes built for each case of the rule: the caps at 40, 56.6 and 69.3 m, list C's cap, a full list keeping the first lights plus the deepest overlap (the first of equal scores), a copied list already over the cap, the glow added over the cap, a corona-only light inside and outside an atomic's sphere, no point light kept | Unit tests, CI | pass or fail per case |
| Replay of the original | A trace of the original (below) gives each call's inputs: the atomic's sphere, the squared distance, the flags and the manager's lists with each light's position, radius and kind. Coney's `LightManager::select` runs on the same inputs and must give the same lights in the same order, every call | Local, with a trace from PCSX2 (`[trace]`, skipped without one) | calls replayed, calls equal (the run behind the page: 360 of 360 for the script that replayed the rule) |
| Same place, both games | Coney at the trace's place (`--play-level`, `--camera`, `--freeze-world`) dumps its frame; `trace diff` matches atomics by caller and sphere centre (5 cm) and compares each list by kind, position (1 cm), radius and colour (1/255, flickering lights by base colour) | Local, with the disc and a trace | atomics matched, lists equal, the first difference |

**The trace of the original** is the method of run r-20261030-light-selection made into a tool: breakpoints on the
selection's entry and its callers' return addresses, the inputs read at the entry and the upload list at the return,
one CSV row per call with the caller, the inputs and the chosen lights as values (positions, radii, kinds, colours,
never pointers). It runs from a state of the [state library](pcsx2-states.md) and writes a [run
record](research-evidence.md#run-records); the CSV stays in scratch and the record holds its hash.

### Faithful comparison: the colour of a vertex {#test-vertex-colour}

A **CPU reference** of the PS2's light model, `min(1, prelight + Σ light) × material` with the falloff and clamp of
[World lighting](../research/lighting.md#world), gives the expected colour of any vertex in the GS's 0-128 scale. It
is Coney's own code, the one the lab's *runtime share* view uses, and it is the yardstick for both renderers.

| Test | What it checks | Where | Tolerance |
| --- | --- | --- | --- |
| Reference scenes | Synthetic meshes (a grid with normals in every direction, a sphere, a skinned capsule figure) with prelight, materials and lights chosen to hit each term: ambient only, a directional light from behind, a point light at `d / radius` of 0.3, 0.5 and 0.9, the clamp, a dark material under a bright light | Unit tests against the CPU reference, CI | exact in floats; ±1 of 128 after the GS rounding |
| The renderer against the reference | The same scenes drawn by the GL3 and GPU renderers under the software rasteriser, pixel colours at vertices against the reference | CI, the picture check of the [renderer design](../engine/renderer.md#comparing) | ±2 of 255 a channel |
| Against the original | A GS dump of the original at a state (`level99`'s street at night, a human under a lamp) gives each vertex's colour as it reached the GS; the CPU reference, given the lights the trace says each draw chose, must give the same colours | Local, with PCSX2 | ±1 of 128 for 99 % of the vertices, ±2 for all; counts of each |

### Pictures and invariants {#test-pictures}

- **The *PS2* preset does not move.** Named frames (the `level99` street at night, the front end's sky over
  `level100`, a lamp-lit corner, a human under a lamp, a flickering lamp held by *Hold lighting*) are captured with
  `--screenshot` against the base commit at the renderer design's tolerance ([How pictures are
  compared](../engine/renderer.md#comparing)); the draw-stream hash records each draw's chosen lights, so a change in
  the choice shows in CI on the synthetic scenes.
- **Enhancements leave the simulation alone.** The lighting frame is identical with the options on and off; the state
  hash over 600 steps of a level is the same with every option, every lab view and the sun on, off or rotating; the
  stealth flag is not read from the renderer.
- **Enhanced paths keep the faithful light.** A synthetic mesh lit per pixel averages to the per-vertex result; the
  cascades keep a fixed texel size as the camera moves; with the sun off a Coney level's frame equals the sun-less
  frame.

### Shadows on synthetic scenes {#test-shadows}

Headless renders under the software rasteriser of Coney-made scenes, no game data:

| Scene | Expected |
| --- | --- |
| A 1 m box on a plane, the sun at 45° | The shadow reaches `height / tan(elevation)` past the box, within one texel's width |
| A human stand-in (capsules) on a plane | The shadow's centroid lies where the sun's direction projects the figure's centre, within a texel; the figure does not darken its own front |
| A moving sun: 24 steps of 15° round the box | The shadow's direction follows the sun's azimuth plus 180° within 2°; its length follows the elevation |
| A still sun, the camera moved 1 m in 100 steps | The shadow's edge does not move in the world (the texel snap) |
| A wall facing away from the light | Never darkened (the Xbox's rule) |
| A lit quad in shadow | Its colour is the lit colour × the shadow colour (the Xbox's multiply) |
| Humans at 10, 19.9 and 20.1 m in the *Xbox* preset | The first two cast, the third does not ([The shadow map](../research/xbox-lighting.md#shadow-map)) |

Visual quality is judged from screenshots by a person, as for the plan's other options.

## Coney's own sun {#sun}

A **sun** is a light of a Coney level's own: a project sets it in Coney Studio and it lights the level beside whatever
other lights the level has, the way a directional light in a general-purpose engine's scene does. It is content, not
an enhancement: a Coney level is not the original, so its sun is drawn in every preset, and on a game level there is
no sun unless a project adds one ([Questions](#questions)).

**What a sun is.**

| Setting | Values | Notes |
| --- | --- | --- |
| Direction | azimuth and elevation, or a direction vector | The way the light travels, as the sandbox's `sun direction=` |
| Colour, intensity | RGB and a factor | The light added is colour × intensity × `max(0, n · −L)`, the same term as a directional light of the game's |
| Ambient | sky and ground colours | The sandbox's `ambient sky= ground=` |
| Time | fixed, or a time of day and a speed | The direction turns with the level's clock: so many degrees of azimuth per minute of game time (counted in simulation steps), from a start; never the real clock |
| Shadows | on or off, strength, softness | The strength is the multiply's `s` ([Lighting model](#lighting-model)) |

**Beside the game's lights.** The sun is not a light record of the `LightManager`: it never enters the cull, the
lists, the caps or the selection, so nothing the original decides moves when a sun is added. The lighting frame carries
it as its own entry, and each renderer adds its term after the selected lights: per vertex in the *PS2* and *Xbox*
presets, per pixel from *Enhanced*. A Coney level may also hold the game's kinds of light (lamps placed in Studio as
`SetLight` would make them), and those go through the manager as the game's do.

**Its shadows.** On a Coney level the sun *is* the level's shadow light, so the renderer design's one shadow source
holds: the shadow maps take their direction from the sun and their strength from its shadow setting, in the presets
that draw a shadow map. In *PS2* a still sun's shadows are baked into the vertex colours, as the sandbox bakes its
sun's today ([How it is drawn](sandbox.md#how-it-is-drawn)); a turning sun needs a shadow map, which *PS2* does not
draw ([Questions](#questions)).

**State and the network.** The sun's direction comes from one number, the level's sun time, which the game mode owns
as level state: it advances with the simulation's steps, a script may set it, and a client receives it at join and when
a script changes it, then advances it itself ([The networking checklist](network-architecture.md#checklist)). Drawing
reads it from the step snapshot and blends between steps like the camera.

**In Studio's level settings** ([Coney Studio's panels](studio/editor-experience.md#panel-list)): a *Sun* group with
the direction as a gizmo in the Level view (drag to turn it, as a directional light is turned in other editors) and as
numbers, colour, intensity, the time of day and its speed, a scrubber that previews another time without saving it,
and the shadow settings; the *Ambient* and *Fog* groups beside it. A game level's own lights are listed read-only from
the lab's light list. The sandbox layout gains the same in its `sun` statement (`time=`, `speed=`, `intensity=`,
`shadow=`), so a layout and a Studio level say the same thing.

### The sun and sneak zones {#sun-sneak}

Where a human may hide is **collision**: the shadow-ground bit of the triangle under his feet
([Stealth](../research/stealth.md#shadow-ground)), which a project sets on its colliders
([Colliders and primitives](studio/content-and-formats.md#collision)). The sun relates to it in two ways:

- **A bake in the editor.** Studio's *Bake sneak zones from the sun* casts rays from each collider triangle towards
  the sun at a chosen time, marks the triangles mostly in its shadow with the same bit, and shows them for the user to
  edit. The game then reads collision as it always does: no new rule, and the same on every machine and preset.
- **Never from the renderer.** A shadow map differs between presets and GPUs and is not drawn on a server, so it
  never decides gameplay. A sun that moves the sneak zones while the game runs would compute them on the simulation
  side, from collision rays at the sun time, and that is a mechanic of Coney's own, which waits by the rule of
  faithful first unless a maintainer decides otherwise ([Questions](#questions)).

## Work breakdown

The design's steps, and the renderer design's items that carry them ([Order](../engine/renderer.md#order)):

| Step | What | Renderer item |
| --- | --- | --- |
| L0 | The lighting frame and its test; the level's shadow light and the strength | 19 |
| L1 | The scene target and Coney's shaders for world, human and object pipelines | 22, with the GPU renderer's shaders |
| L2 | Shadow maps: the *Xbox* preset's, then *Enhanced* and *High*'s | 28, 30 |
| L3 | Per-pixel lights with the equivalence test | 29 |
| L4 | Environment-map materials with cube maps; the capture path | filed from this page after 22 |
| L5 | Probes, then the tagged-surface table and screen-space reflections | filed from this page after L4 |
| L6 | Point-light shadows for fires and lamps | 31 |
| L7 | Ray-traced shadows and AO | a native ray-tracing path after 21 |

## Build issues {#build-issues}

In order: the lab and the faithful comparison tests first, then the sun. Each is one pull request.

| # | Issue | Depends on | Size |
| --- | --- | --- | --- |
| 1 | `graphics: Add the lighting lab page to the debug menu` (counts, lights with their source, selection, layers and views, *Hold lighting*, *Light from*, debug draw, *Dump this frame*) | none | M |
| 2 | `tools: Add the original's light-selection trace from PCSX2` | none | M |
| 3 | `graphics: Add the light-selection replay against the original's trace` (Coney's frame dump in the trace's columns, the replay test, `trace diff` for light lists, the lab's `--light-trace`) | 1, 2 | M |
| 4 | `graphics: Fix the light selection's caps for copied lists and the glow` ([In Coney](../research/lighting.md#coneys-implementation)) | 3 | S |
| 5 | `graphics: Add the PS2 vertex-light reference and its colour tests` | none | M |
| 6 | `tools: Add vertex colours from a PCSX2 GS dump to the comparison` | 2, 5 | M |
| 7 | `tests: Add PS2-preset picture checks of lit scenes` | renderer items 1, 2 | S |
| 8 | `graphics: Add the sun to the lighting frame and the sandbox layouts` | renderer item 19 ([#1211](issue:1211)) | M |
| 9 | `graphics: Add the shadow test scenes and their invariants` | 8, renderer items 22, 28 | M |
| 10 | `graphics: Add the shadow and cascade views to the lighting lab` | 1, renderer items 28, 30 | S |
| 11 | `tools: Add the sun to Coney Studio's level settings` | 8, Studio's Inspector ([#1054](issue:1054)) | M |
| 12 | `tools: Add Studio's sneak-zone bake from the sun` | 11 | M |

Item 1 also answers the lighting part of [#216](issue:216) (the World page's lights and looks); item 4 sits beside
the light manager's other fidelity fixes of [#654](issue:654), and item 3's replay is how both are proved.

## Risks {#risks}

- **Baked light fights real light.** Mitigated by the multiply rule and the level's shadow strength, but some levels
  may look wrong until tuned. The strength comes from the level's own shadow colour, so tuning does not need code.
- **Streaming.** The world loads in sectors; shadow receivers, probes and the SSR depth must cope with sectors
  appearing and disappearing without a pop.
- **Performance on low-end machines.** The presets exist for this; *Enhanced* has to hold 60 fps on integrated GPUs
  or become opt-in only.
- **Traces drift from frames.** The original and Coney never draw the same frame at the same instant, so the
  same-place comparison matches atomics by position and holds the flicker; the replay test, which needs no matching,
  is the exact one.

## Research this design needs

For the graphics engineers, one line each.

- What the Xbox build's `GraphicsShadowXBox.cpp` draws: done, see [Xbox lighting and
  shadows](../research/xbox-lighting.md#in-coney).
- How each frame's lights are chosen: done, confirmed in the code and at runtime
  ([Selection](../research/lighting.md#select)); what VU1 does with a corona-only light the selection keeps is open
  ([Lighting: open questions](../research/lighting.md#open-questions)).
- How the environment-map pipeline gets its texture and matrix (`0x00428410`, `0x00428260`), and which materials
  and objects use it, as a count per level.
- What the prelit colour holds: whether direction or occlusion can be told apart from the lamps' baked part, from a
  few sectors read at runtime.
- Whether the levels have wet or rain states and how scripts mark them.
- Which levels' scripts turn the shadow light off, and where a level's moonlight direction changes within it.

## Questions for the user {#questions}

1. **A turning sun in the *PS2* preset.** A sun that turns needs a shadow map, which *PS2* does not draw. Should a
   Coney level whose sun turns draw its shadows in every preset (the shadow map counts as the level's content), or
   show the turning light without shadows (the blob) in *PS2*?
2. **Sneak zones that follow the sun.** The design bakes them in the editor at one time of day. Should a project be
   able to have them follow a turning sun while the game runs, a mechanic of Coney's own that the faithful-first rule
   holds back until the game is complete?
3. **Time of day.** Should the sun's time also change its colour, the ambient and the fog (dawn, noon, dusk), or only
   its direction?
4. **A sun over a game level.** May a project put its sun over one of the game's levels (a mod), in place of the
   level's shadow light from *Enhanced* up, or do the game's levels always keep their own?
5. **The lab in Studio.** Should the lighting lab also be a Studio panel, beside the debug menu page?
