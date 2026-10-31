// SPDX-License-Identifier: GPL-3.0-or-later
// A triangle that covers the whole screen, made from the vertex index alone: draw three vertices with no vertex buffer.
// The first shader of the GPU renderer (docs/engine/renderer.md#order), and the vertex half of the full-screen passes
// the 2D layers and the post chain will be: it proves the shader build from HLSL to every backend.
//
// Vertex 0 is the top left corner of the screen, vertex 1 runs off the right edge and vertex 2 below the bottom edge;
// the picture is the part of the triangle inside the screen. The texture coordinate (0, 0) is the top left, as SDL's
// GPU API has it on every backend.

struct Output
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Output main(uint vertexId : SV_VertexID)
{
    Output output;
    // (0, 0), (2, 0), (0, 2) for vertices 0, 1, 2.
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    // Clip space has y up, the texture coordinate has y down.
    output.position = float4(output.uv.x * 2.0 - 1.0, 1.0 - output.uv.y * 2.0, 0.0, 1.0);
    return output;
}
