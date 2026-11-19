// SPDX-License-Identifier: GPL-3.0-or-later
// The 2D pass's fragments (docs/engine/gpu-renderer.md#drawing), the half of overlay2d.vert.hlsl that colours them: the
// texel (a white texel for flat colour) times the corner colours, kept when its alpha is in the draw's range
// (graphics::AlphaRange), as librw's simple.frag and DoAlphaTest do in the GL3 renderer. The 2D pass has no fog.
// A fragment shader's sampled textures are in space2 and its uniform buffers in space3.

Texture2D<float4> Source : register(t0, space2);
SamplerState SourceSampler : register(s0, space2);

cbuffer AlphaTest : register(b0, space3)
{
    float alphaLow;  // kept from here up
    float alphaHigh; // and below this
    float2 unused;
};

float4 main(float2 uv : TEXCOORD0, float4 colour : TEXCOORD1) : SV_Target0
{
    float4 result = colour * Source.Sample(SourceSampler, uv);
    if (result.a < alphaLow || result.a >= alphaHigh)
    {
        discard;
    }
    return result;
}
