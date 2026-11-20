# Anti-aliasing, scaling and HDR

How the [renderer design](renderer.md) smooths edges, scales the picture and shows it on an HDR display: the part of
that page about the window's output, split out to keep it short. The items named here are in the renderer's
[Order](renderer.md#order), and every option is render-side only, off in the *PS2* and *Xbox* presets
([Presets](renderer.md#presets)).

## Anti-aliasing and scaling {#aa}

All of these draw into the scene target at the internal resolution and resolve or upscale into the window
([Enhancements: foundation F2](../guides/enhancements.md#foundations)).

| Technique | Where it fits | Licence |
| --- | --- | --- |
| MSAA 2×, 4×, 8× | geometry edges, which this game's long straight scenery edges are full of; with alpha-to-coverage for fences and foliage. On the SDL3 GPU renderer only (item 26): the GL3 path draws none | none (API feature) |
| FXAA 3.11 | cheapest post AA, slight blur | NVIDIA's FXAA licence, permissive; check at adoption |
| SMAA 1×, T2× | better edges than FXAA, cheap; the default post AA | MIT |
| TAA (Coney's own) | shimmer on thin wires and fences; uses the blended steps' previous poses for motion vectors and a jittered projection | Coney's code |
| Render scale 0.5-2× | above 1 is supersampling; a *640 × 448* mode with nearest filtering for the PS2 look | Coney's code |
| Upscalers | from a render scale below 1, in tiers ([below](#upscalers)) | per tier |

### Upscalers {#upscalers}

Listed as options, in tiers, each off in *PS2* and *Xbox*. Each licence is dealt with when its item comes: FSR's MIT
needs no decision, and a proprietary SDK or binary is a maintainer decision then. The versions are the current ones
as this design was written; the item checks them again.

| Tier | Upscaler | Runs on | Licence (as its project states it; not confirmed, checked at adoption) | Reach from SDL3's GPU API | Item |
| --- | --- | --- | --- | --- | --- |
| Fallback | AMD FSR 1 (EASU and RCAS): spatial upscale and sharpen | any GPU; the one tier CI can test, on its software drivers | MIT | plain shaders of Coney's | 33 |
| Main, portable | AMD FSR 3.1 (FidelityFX SDK): temporal, from TAA's motion vectors | any vendor | MIT | the SDK's upscaler shaders ported as Coney's compute passes; its own backends want native handles | 34 |
| Vendor | NVIDIA DLSS 4 and DLAA (Streamline or NGX SDK) | NVIDIA RTX | NVIDIA's proprietary SDK licence; redistributable binaries | native handles and a vendor binary beside the executable; a separate opt-in plug-in, as the [Enhancements plan](../guides/enhancements.md#decided) records for proprietary SDKs | 35 |
| Vendor | AMD FSR 4 ("Redstone"): machine-learned | AMD RX 9000 and 7000; 4.1 adds RX 6000 and some APUs; never NVIDIA. Shader Model 6.6, so Direct3D 12 only until AMD's announced Vulkan version ships | the FidelityFX SDK's terms for its prebuilt ML upscaler | as DLSS | 36 |
| Optional | Intel XeSS | Intel Arc best; other GPUs through its fallback | Intel's XeSS SDK licence; redistributable binaries | as DLSS | filed if a maintainer asks for it |

The vendor tiers need the device and command buffer underneath SDL's GPU API: a small patch to Coney's pinned SDL (or
an upstream extension) that exposes them, used only by the plug-in. Whether to carry that change to SDL is decided
with item 35, the first that needs it; FSR 3.1 is ported so that it does not.

## HDR output {#hdr}

The game's content is SDR on the GS scale, so HDR output is a presentation of it, not new range
([Enhancements: HDR](../guides/enhancements.md#lighting-and-post-processing)). On the GPU renderer, an HDR10 or scRGB
swapchain (SDL3's GPU API has both) takes an inverse tone map of the SDR picture, with highlights from what the
lighting work adds (coronas, glows, bloom). Paper white and peak brightness are display values; the HUD and the front
end draw at paper white. It is a live toggle on the debug menu's Display page and a command-line flag, greyed out with
the reason when the display or its driver has no HDR. The PS2 look stays the default and test mode forces SDR. Its
pull request shows a picture check for each output mode (item 23).
