// SPDX-License-Identifier: GPL-3.0-or-later
// The 2D pass's vertices: the front end, the legal screens, the movies and the HUD, drawn by the GPU renderer from the
// frame description (docs/engine/gpu-renderer.md#drawing). The fragment half is overlay2d.frag.hlsl.
//
// A corner comes in window pixels from the top left (graphics::Overlay2dVertex), as librw's Im2D vertices do in the GL3
// renderer, and is mapped onto clip space the way librw's im2d.vert maps them: x * 2 / width - 1 and 1 - y * 2 / height.
// SDL's GPU API names a vertex input by TEXCOORD and its location; a vertex shader's uniform buffers are in space1.

cbuffer Frame : register(b0, space1)
{
    float2 frameSize; // the target's width and height in pixels
    float2 unused;
};

struct Input
{
    float2 position : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 colour : TEXCOORD2; // 8 bits a channel, 0 to 1
};

struct Output
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 colour : TEXCOORD1;
};

Output main(Input input)
{
    Output output;
    output.position = float4(input.position.x * 2.0 / frameSize.x - 1.0, 1.0 - input.position.y * 2.0 / frameSize.y,
                             0.0, 1.0);
    output.uv = input.uv;
    output.colour = input.colour;
    return output;
}
