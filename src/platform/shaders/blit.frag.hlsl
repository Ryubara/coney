// SPDX-License-Identifier: GPL-3.0-or-later
// Copies a texture to the screen, one sample per pixel: the fragment half of fullscreen.vert.hlsl. The first shader of
// the GPU renderer (docs/engine/renderer.md#order).
//
// SDL's GPU API fixes where a fragment shader's resources go, the same on every backend: sampled textures and their
// samplers in space2, uniform buffers in space3 (a vertex shader's are in space0 and space1).

Texture2D<float4> Source : register(t0, space2);
SamplerState SourceSampler : register(s0, space2);

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    return Source.Sample(SourceSampler, uv);
}
