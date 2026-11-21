// SPDX-License-Identifier: GPL-3.0-or-later
// The scene passes' vertices: the streamed world's sectors, the world objects, the parked cars and the humans, drawn by
// the GPU renderer from the frame description (docs/engine/gpu-renderer.md#scene). The fragment half is
// scene3d.frag.hlsl.
//
// A posed draw (a human) is skinned first, as the GL3 renderer's skinning shader skins him (platform/character_skin.cpp,
// docs/engine/gpu-renderer.md#skinning): each slot with a weight moves the position and turns the normal by its bone of
// the frame's palette, times the weight, summed, the normal then made unit; the draw's world matrix then places him.
// graphics::skinVertex() is the same formula on the CPU, for the tests.
//
// It lights each vertex as librw's GL3 device does (its default.vert and header.vert, Coney's level-of-detail step in
// platform/texture_lod.cpp), so both renderers draw the same picture: the prelighting (black for a mesh without it),
// plus the ambient light times the material's ambient, plus the other lights times its diffuse, clamped to 0 to 1 and
// multiplied by the material's colour; the fog factor from the distance in front of the camera (w). The matrices
// arrive column by column (graphics::Matrix4), which is HLSL's own packing for a cbuffer.

#define MAX_LIGHTS 8

cbuffer Draw : register(b0, space1)
{
    float4x4 world;                    // the mesh's frame (graphics::worldMatrixOf())
    float4x4 viewProjection;           // the camera's projection times its view (graphics::projectionOf())
    float4 ambient;                    // the ambient lights' sum; a unused
    float4 lightParams[MAX_LIGHTS];    // type (0 none, 1 directional, 2 point, 3 spot), radius, -cos angle, hard spot
    float4 lightPosition[MAX_LIGHTS];  // point and spot lights
    float4 lightDirection[MAX_LIGHTS]; // directional and spot lights: the way they travel, unit
    float4 lightColour[MAX_LIGHTS];
    float4 materialColour;             // the material's colour, or white for a mesh that does not modulate
    float4 surface;                    // the material's ambient, specular (unused), diffuse; 1 for a prelit mesh
    float4 fogData;                    // the fog's start, its end (the far plane), 1 / (start - end), 1 for no fog
    uint4 skin;                        // the draw's first bone in Bones, 1 when the draw is posed; zw unused
};

// The frame's bone palette: every posed draw's skinning matrices, each four columns (graphics::bonePaletteOf()).
StructuredBuffer<float4> Bones : register(t0, space0);

struct Input
{
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 prelit : TEXCOORD2; // 8 bits a channel, 0 to 1
    float2 uv : TEXCOORD3;
    uint4 bones : TEXCOORD4;    // into the draw's part of the palette
    float4 weights : TEXCOORD5; // 0 for an unused slot, and for every slot of a mesh that is not skinned
};

struct Output
{
    float4 position : SV_Position;
    float4 colour : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float fog : TEXCOORD2;
    float depth : TEXCOORD3; // the distance in front of the camera, for the texture's level
};

// The lights' colour at `at` with normal `normal`, as librw's DoDynamicLight with every kind of light compiled in,
// including its spot light, which returns its own colour alone and ends the loop (so the pictures match librw's while
// it is the reference: docs/engine/gpu-renderer.md#scene).
float3 dynamicLight(float3 at, float3 normal)
{
    float3 colour = float3(0.0, 0.0, 0.0);
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        const float type = lightParams[i].x;
        if (type == 0.0)
        {
            break;
        }
        if (type == 1.0)
        {
            colour += max(0.0, dot(normal, -lightDirection[i].xyz)) * lightColour[i].rgb;
        }
        else if (type == 2.0)
        {
            const float3 toward = at - lightPosition[i].xyz;
            const float distance = length(toward);
            const float fall = max(0.0, 1.0 - distance / lightParams[i].y);
            colour += max(0.0, dot(normal, -normalize(toward))) * lightColour[i].rgb * fall;
        }
        else if (type == 3.0)
        {
            float3 toward = at - lightPosition[i].xyz;
            const float distance = length(toward);
            const float fall = max(0.0, 1.0 - distance / lightParams[i].y);
            toward /= distance;
            float lit = max(0.0, dot(normal, -toward));
            const float pointCos = dot(toward, lightDirection[i].xyz);
            const float coneCos = -lightParams[i].z;
            const float edge = (pointCos - coneCos) / (1.0 - coneCos);
            if (edge < 0.0)
            {
                lit = 0.0;
            }
            lit *= max(edge, lightParams[i].w);
            return lit * lightColour[i].rgb * fall;
        }
    }
    return colour;
}

Output main(Input input)
{
    Output output;
    float3 position = input.position;
    float3 normal = input.normal;
    if (skin.y != 0)
    {
        float3 skinned = float3(0.0, 0.0, 0.0);
        float3 skinnedNormal = float3(0.0, 0.0, 0.0);
        for (int i = 0; i < 4; i++)
        {
            const float weight = input.weights[i];
            if (weight != 0.0)
            {
                const uint first = (skin.x + input.bones[i]) * 4;
                const float3 x = Bones[first].xyz;
                const float3 y = Bones[first + 1].xyz;
                const float3 z = Bones[first + 2].xyz;
                const float3 t = Bones[first + 3].xyz;
                skinned += (x * position.x + y * position.y + z * position.z + t) * weight;
                skinnedNormal += (x * normal.x + y * normal.y + z * normal.z) * weight;
            }
        }
        position = skinned;
        normal = normalize(skinnedNormal);
    }
    const float4 at = mul(world, float4(position, 1.0));
    output.position = mul(viewProjection, at);
    normal = mul((float3x3)world, normal);
    float4 colour = surface.w > 0.5 ? input.prelit : float4(0.0, 0.0, 0.0, 1.0);
    colour.rgb += ambient.rgb * surface.x;
    colour.rgb += dynamicLight(at.xyz, normal) * surface.z;
    output.colour = saturate(colour) * materialColour;
    output.uv = input.uv;
    const float w = output.position.w;
    output.fog = clamp((w - fogData.y) * fogData.z, fogData.w, 1.0);
    output.depth = w;
    return output;
}
