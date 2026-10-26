# Realtime lighting design

The design for realtime shadows, per-pixel lighting and reflections in Coney's own rendering pipeline, an
option of the [Enhancements](../roadmap.md#enhancements) milestone. It extends the
[Enhancements plan](enhancements.md) (foundations F2, F3 and F5, the shadow, AO and ray-tracing rows) and does not
repeat it. **Status: design only. Nothing here is built, and nothing is built before the faithful game is complete**
(AGENTS.md, "Faithful first"). Every option ships **off** by default and is forced off in test mode, so tests and
reference screenshots always show the faithful image.

## What the original gives us

All of it from [Lighting](../research/lighting.md) unless another page is named.

| Fact | Consequence |
| --- | --- |
| The streamed world is **prelit**: a baked colour and a normal per vertex, plus runtime lights on top (the world ambient, world directional lights and up to 8 point lights per sector). With the ambient at black the scenery stays visible from the baked colour alone | The baked colour is part of the final image and already holds lighting. New lights cannot simply be added: surfaces would be lit twice |
| Humans and objects are vertex-lit by 3-6 lights chosen by distance: moonlight, a weak "reflected" directional light from below, an ambient and nearby lamps. Lamps flicker and get a corona sprite | The light **data** (type, colour, radius, flicker, what it lights) is complete and can feed a per-pixel path unchanged |
| Shadows are one faint 1 m **blob sprite** per human. `SetShadowColor`, `SetShadowLightOffset` and `EnableShadow` are empty. The stealth "hidden in shadow" flag is separate: set by the brain from a ground check | Real shadows are an addition. They must never feed the stealth flag, or the AI changes |
| There is no reflection code. The "reflected light" is a plain upward directional light. MatFX **environment-map** materials exist (effect 2, world pipeline `0x30087`, [World](../research/world.md#pipelines)) | The only reflective surfaces the data marks are those materials. Wet streets and shop windows have no marker in the data |
| 88 of `level99`'s 90 lamps light only objects and humans, not the world | Lamp shadows on the world would show light that the original's world never received |
| Draw distance 115 m, fog from half of it ([World](../research/world.md#a-frame)) | A shadow map needs to cover the player's surroundings, not the whole view |

## Goals and non-goals

Goals: contact and cast shadows from the level's main directional light; per-pixel lighting of humans, objects and
vehicles that keeps the original's light choice; believable reflections on the surfaces the game already marks as
shiny, then on a short list of surfaces Coney tags itself; a quality ladder from *Faithful* to *High*.

Non-goals: global illumination or path tracing that "relights" the baked world; changing the gameplay-visible
lighting (stealth, the lit/unlit AI rules); shipping any game data or Coney-made replacement of it; a physically
based material rework of the PS2 textures (that is the upscaling and texture-pack work in the plan).

## Architecture: a pipeline of our own

Coney draws through librw on OpenGL 3.3 today. "Our own pipeline" is reached in two steps, each useful alone:

1. **Coney shaders on librw (foundations F2, F3).** Render callbacks of Coney's own pipelines, as `world_atomic`
   already registers one, draw into an offscreen colour + depth target at the internal resolution. This is all
   shadows and per-pixel lighting need, and it keeps the faithful path untouched. Cost: M each for F2 and F3.
2. **A Coney renderer over SDL3's GPU API (F5).** Vulkan, D3D12 and Metal, with librw kept only to read and decode
   the game's streams. Needed for ray-traced shadows and HDR, and for macOS long term. Cost: XL, after the game.

The seam is the rule from the plan's choices: no librw call outside the platform renderers. Game code only produces
a **lighting frame** (below); a renderer consumes it. The faithful renderer ignores the parts it cannot use.

### The lighting frame

Per viewport, built by the game side from the `LightManager`'s existing selection, with no new simulation state:

- the world's list and each atomic's chosen lights (type, colour with brightness and offset applied, position or
  direction, radius, flicker value of this frame);
- the **sun**: the level's object directional light (`level99`: the moonlight), or none;
- the fog and background colours, and the world ambient;
- the camera, interpolated between simulation steps like everything drawn above 30 Hz.

Because the frame is derived from state the faithful path already computes, turning enhancements on or off cannot
change the simulation. A test compares the frame with and without them.

## Lighting model

Three layers, drawn in this order:

1. **Faithful layer.** Exactly the original: baked vertex colour plus the vertex-lit runtime lights, in the
   original's colour scale (`0x80` is 1.0). With enhancements off this is the whole image.
2. **Per-pixel lights** (*Enhanced*). The same lights and the same selection, evaluated per pixel instead of per
   vertex, so lamp pools and moonlight follow the surface. Two rules keep the look: the result must equal the
   vertex result on average over a triangle (a test checks this on synthetic meshes), and the clamp comes before
   the material as in [Lighting](../research/lighting.md#world) so highlights do not wash out.
3. **Shadows and reflections** multiply or add on top (below).

**The baked light problem.** The world's baked colour already contains some occlusion and light direction, so a
shadow can only darken it plausibly, not relight it. The design: a receiver's final colour is multiplied by
`1 - s * (1 - visibility)`, where `visibility` comes from the shadow map and `s` is a per-level strength. `s` starts
at the share of the final colour the sun contributes (the sun's colour over baked + ambient + sun at that surface)
and is then tuned by eye per level, stored in a Coney-owned table that holds numbers only. This is the plan's
"darkens baked light, a plausible fake", with a rule for how much.

## Shadows

| Part | Design |
| --- | --- |
| Source | One shadow map from the sun. Levels with no object directional light get none (the blob stays) |
| Casters | Humans, held objects, thrown objects, vehicles and world objects. Not the streamed world: its shadows are baked |
| Receivers | The world, objects, vehicles and humans (self-shadowing on) |
| Coverage | Two cascades around the camera, about 12 m and 50 m, fitted to the camera frustum and snapped to texel steps so they do not shimmer while the camera moves |
| Filtering | PCF with a few taps; contact hardening is optional |
| Blob | Off when real shadows are on, so a person does not get two |
| Lamps | Not in the first version (the data says lamps do not light the world). Later: the nearest one or two lamps that light objects, as small spot or cube maps, humans and vehicles only |
| Stealth | Untouched. The brain's shadow flag keeps its own ground check; a test makes sure it does not read the renderer |

Cost: one extra depth pass over the casters near the player, and the receiver lookup in Coney's world and object
shaders. Budget on a mid-range 2024 GPU at 1080p: under 1.5 ms (a target, to be measured, not a result).

## Reflections

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

## Quality ladder

| Preset | Contents | Needs |
| --- | --- | --- |
| *Faithful* | Nothing added. The default, and what tests use. The Xbox port's shadow map, gamma ramp and per-pixel fog are toggles here, off by default ([Xbox lighting](../research/xbox-lighting.md#in-coney)) | none |
| *Enhanced* | Sun shadow map, per-pixel lights, environment-map materials with cube maps | F2, F3 |
| *High* | Adds GTAO ([plan](enhancements.md#lighting-and-post-processing)), probes, tagged reflections, lamp shadows for objects | F2, F3 |
| *Ultra* | Adds ray-traced shadows and AO for dynamic objects over the baked light | F5 and a Vulkan ray-query path |

## Testing

- Reference screenshots and the disc tests render *Faithful* only; they must not change when this work lands.
- Enhanced paths are tested by invariants in headless mode: the lighting frame is identical with the options on and
  off; a synthetic mesh lit per pixel averages to the per-vertex result; the cascades keep a fixed texel size as the
  camera moves; the stealth flag is not read from the renderer.
- Visual quality is judged from screenshots by a person, as for the plan's other options.

## Work breakdown

| Step | What | Depends on | Cost |
| --- | --- | --- | --- |
| L0 | The lighting frame and its test; the sun and the shadow strength table | the faithful `LightManager` work | S |
| L1 | Scene target and Coney shaders for world, human and object pipelines | F2, F3 | M |
| L2 | Sun shadow map, two cascades, blob switch | L0, L1 | M |
| L3 | Per-pixel lights with the equivalence test | L0, L1 | M |
| L4 | Environment-map materials with cube maps; the capture path | L1 | M |
| L5 | Probes, then the tagged-surface table and screen-space reflections | L4 | L |
| L6 | Lamp shadows for objects and humans | L2 | M |
| L7 | Ray-traced shadows and AO | F5 | XL |

## Risks

- **Baked light fights real light.** Mitigated by the multiply rule and per-level strength, but some levels may look
  wrong until tuned. The strength table is data, so tuning does not need code.
- **Streaming.** The world loads in sectors; shadow receivers, probes and the SSR depth must cope with sectors
  appearing and disappearing without a pop.
- **macOS.** OpenGL stops at 4.1 there. Steps L1-L6 fit in 3.3 plus the features above; L7 and F5 need the new
  backend.
- **Performance on low-end machines.** The ladder exists for this; *Enhanced* has to hold 60 fps on integrated GPUs
  or become opt-in only.

## Research this design needs

For the graphics engineers, one line each. The first is done; the others are not started.

- What the Xbox build's `GraphicsShadowXBox.cpp` draws: done, see [Xbox lighting and
  shadows](../research/xbox-lighting.md#in-coney) for its findings and how they map onto these presets.
- How the environment-map pipeline gets its texture and matrix (`0x00428410`, `0x00428260`), and which materials
  and objects use it, as a count per level.
- What the prelit colour holds: whether direction or occlusion can be told apart from the lamps' baked part, from a
  few sectors read at runtime.
- Whether the levels have wet or rain states and how scripts mark them.
- Which levels have no object directional light, and where the sun's direction changes within a level.
