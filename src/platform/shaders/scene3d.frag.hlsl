// SPDX-License-Identifier: GPL-3.0-or-later
// The scene passes' fragments (docs/engine/gpu-renderer.md#scene), the half of scene3d.vert.hlsl that colours them, as
// Coney's level-of-detail step does in the GL3 renderer (platform/texture_lod.cpp): the texel at the level the GS
// would pick by distance (log2 of the depth times 2^L plus K, never below 0) or at level 0, or as the GPU chooses for a
// texture of Coney's own; times the vertex colour; mixed toward the fog's colour; kept when its alpha is in the pass's
// range (graphics::AlphaRange), as librw's DoAlphaTest keeps it.

Texture2D<float4> Source : register(t0, space2);
SamplerState SourceSampler : register(s0, space2);

cbuffer Fragment : register(b0, space3)
{
    float alphaLow;  // kept from here up
    float alphaHigh; // and below this
    float2 unused;
    float4 levels;    // K, 2^L, 1 to pick the level by distance, 1 when the GPU chooses (graphics::TextureLevels)
    float4 fogColour; // rgb
};

float4 main(float4 colour : TEXCOORD0, float2 uv : TEXCOORD1, float fog : TEXCOORD2, float depth : TEXCOORD3)
    : SV_Target0
{
    float level = 0.0;
    if (levels.z > 0.5)
    {
        level = max(log2(max(depth, 1.0 / 1024.0)) * levels.y + levels.x, 0.0);
    }
    // Both read, then one taken: the GPU's own choice needs its derivatives outside any branch.
    const float4 chosen = Source.Sample(SourceSampler, uv);
    const float4 picked = Source.SampleLevel(SourceSampler, uv, level);
    const float4 texel = levels.w > 0.5 ? chosen : picked;
    float4 result = colour * texel;
    result.rgb = lerp(fogColour.rgb, result.rgb, fog);
    if (result.a < alphaLow || result.a >= alphaHigh)
    {
        discard;
    }
    return result;
}
