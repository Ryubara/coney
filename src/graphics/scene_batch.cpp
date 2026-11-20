// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the GPU renderer's reading of the frame description's scene passes (docs/engine/gpu-renderer.md#scene);
// the original draws its meshes through RenderWare's PS2 pipelines
#include "graphics/scene_batch.h"

#include <algorithm>
#include <cmath>

namespace coney::graphics {

namespace {

// `v` made unit; (0, -1, 0) for a zero vector, as platform::placeLight() makes a light's frame.
std::array<float, 3> unit(const std::array<float, 3>& v) {
    const float length = std::sqrt((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));
    if (length <= 0.0F) {
        return {0.0F, -1.0F, 0.0F};
    }
    return {v[0] / length, v[1] / length, v[2] / length};
}

// The dot product of `a` and `b`.
float dot(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]);
}

} // namespace

Matrix4 worldMatrixOf(const Transform3d& where) {
    // Column by column: the axes, then the position.
    return {where.right[0],    where.right[1],    where.right[2],    0.0F,
            where.up[0],       where.up[1],       where.up[2],       0.0F,
            where.at[0],       where.at[1],       where.at[2],       0.0F,
            where.position[0], where.position[1], where.position[2], 1.0F};
}

Matrix4 viewMatrixOf(const SceneCamera& camera) {
    // Each row an axis, the last column minus the position along it: librw inverts the camera's frame (whose right
    // the world pass flipped) and flips x back, which comes to the screen's right as x.
    const Transform3d& f = camera.frame;
    const float x = -dot(f.right, f.position);
    const float y = -dot(f.up, f.position);
    const float z = -dot(f.at, f.position);
    return {f.right[0], f.up[0], f.at[0], 0.0F, f.right[1], f.up[1], f.at[1], 0.0F,
            f.right[2], f.up[2], f.at[2], 0.0F, x,          y,       z,       1.0F};
}

Matrix4 projectionOf(const SceneCamera& camera) {
    // librw's perspective (no view offset): x / half width, y / half height, w the distance; OpenGL's depth row
    // ((far + near) z - 2 far near) / (far - near) taken half way to w, which maps -w..w onto 0..w.
    const float invZ = 1.0F / (camera.farClip - camera.nearClip);
    const float glDepth = (camera.farClip + camera.nearClip) * invZ;
    const float glOffset = -2.0F * camera.nearClip * camera.farClip * invZ;
    Matrix4 m{};
    m[0] = 1.0F / camera.halfWidth;
    m[5] = 1.0F / camera.halfHeight;
    m[10] = 0.5F * (glDepth + 1.0F);
    m[11] = 1.0F;
    m[14] = 0.5F * glOffset;
    return m;
}

Matrix4 multiply(const Matrix4& a, const Matrix4& b) {
    Matrix4 out{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            float sum = 0.0F;
            for (std::size_t k = 0; k < 4; ++k) {
                sum += a[(k * 4) + row] * b[(column * 4) + k];
            }
            out[(column * 4) + row] = sum;
        }
    }
    return out;
}

std::array<float, 4> transformPoint(const Matrix4& matrix, const std::array<float, 3>& point) {
    std::array<float, 4> out{};
    for (std::size_t row = 0; row < 4; ++row) {
        out[row] =
            (matrix[row] * point[0]) + (matrix[4 + row] * point[1]) + (matrix[8 + row] * point[2]) + matrix[12 + row];
    }
    return out;
}

DrawLights lightsFor(std::span<const SceneLight> lights, const MeshData& mesh, const Transform3d& where) {
    DrawLights out;
    // A geometry without RenderWare's LIGHT flag takes no light at all (lightingCB).
    if (!mesh.lit) {
        return out;
    }
    // The mesh's sphere in the world, its radius unscaled (Atomic::getWorldBoundingSphere).
    const auto centre = transformPoint(worldMatrixOf(where), mesh.bounds.centre);
    const std::array<float, 3> sphere{centre[0], centre[1], centre[2]};
    const bool normals = mesh.attributes.normals;
    std::vector<DrawLight> directionals;
    std::vector<DrawLight> locals;
    for (const SceneLight& light : lights) {
        switch (light.type) {
        case LightType::Ambient:
            for (std::size_t c = 0; c < 3; ++c) {
                out.ambient[c] += light.colour[c];
            }
            break;
        case LightType::Directional:
            if (normals && directionals.size() < kMaxDrawLights) {
                directionals.push_back(DrawLight{
                    .type = LightType::Directional, .colour = light.colour, .direction = unit(light.direction)});
            }
            break;
        case LightType::Point:
        case LightType::Spot: {
            // Only a light whose sphere meets the mesh's, as enumerateLights tests them.
            if (!normals || locals.size() >= kMaxDrawLights) {
                break;
            }
            const std::array<float, 3> between{light.position[0] - sphere[0], light.position[1] - sphere[1],
                                               light.position[2] - sphere[2]};
            if (std::sqrt(dot(between, between)) >= mesh.bounds.radius + light.radius) {
                break;
            }
            locals.push_back(
                DrawLight{.type = light.type,
                          .colour = light.colour,
                          .position = light.position,
                          .direction = unit(light.direction),
                          .radius = light.radius,
                          .minusCosAngle = light.type == LightType::Spot ? -std::cos(light.coneAngle) : 0.0F});
            break;
        }
        }
    }
    // The directional lights first, then the rest, eight in all (setLights).
    for (const std::vector<DrawLight>* list : {&directionals, &locals}) {
        for (const DrawLight& light : *list) {
            if (out.count == kMaxDrawLights) {
                return out;
            }
            out.lights[out.count++] = light;
        }
    }
    return out;
}

bool needsSkinning(const FrameDescription& frame, const Draw3d& draw) {
    // A human comes posed by his skinning matrices; the scene shaders have no skinning yet and the converted character
    // meshes no weights, so he would stand in his bind pose (#1206).
    return !frame.bonesOf(draw).empty();
}

DrawColours coloursOf(const MeshData& mesh, const Material3d& material) {
    DrawColours out;
    out.modulate = mesh.modulate;
    out.colours.reserve(mesh.materials.size());
    for (const MeshMaterial& own : mesh.materials) {
        out.colours.push_back(own.colour);
    }
    switch (material.colour) {
    case MaterialColour::Fade:
        for (Rgba& colour : out.colours) {
            colour.a = material.fade;
        }
        break;
    case MaterialColour::Tint:
        if (!out.colours.empty()) {
            out.colours[0] = material.tint;
        }
        out.modulate = out.modulate || material.tint != kWhite;
        break;
    case MaterialColour::Paint:
        std::fill(out.colours.begin(), out.colours.end(), material.tint);
        break;
    case MaterialColour::Shade:
        // The human's colour times the shade (truncated) with the fade as its alpha, in place of the first material's
        // own, the mesh's modulation as it is: platform::MaterialChange's HumanRender_Draw colour.
        if (!out.colours.empty()) {
            const float shade = std::clamp(material.shade, 0.0F, 1.0F);
            const auto scaled = [shade](std::uint8_t channel) {
                return static_cast<std::uint8_t>(static_cast<float>(channel) * shade);
            };
            out.colours[0] =
                Rgba{scaled(material.tint.r), scaled(material.tint.g), scaled(material.tint.b), material.fade};
        }
        break;
    }
    return out;
}

DrawColours layerColoursOf(const MeshData& layer, const DrawColours& base) {
    DrawColours out;
    out.modulate = layer.modulate;
    const std::uint8_t alpha = base.colours.empty() ? 255 : base.colours[0].a;
    for (const MeshMaterial& own : layer.materials) {
        Rgba colour = own.colour;
        colour.a = alpha;
        out.colours.push_back(colour);
    }
    return out;
}

bool vertexAlphaOf(const MeshData& mesh, const SubMesh& sub) {
    if (!mesh.attributes.prelit || sub.indexCount == 0) {
        return false;
    }
    // librw instances a mesh's colours over its vertices from the least index it uses to the most.
    const auto first = mesh.indices.begin() + sub.firstIndex;
    const auto [least, most] = std::minmax_element(first, first + sub.indexCount);
    for (std::size_t v = *least; v <= *most && v < mesh.vertices.size(); ++v) {
        if (mesh.vertices[v].prelit.a != 255) {
            return true;
        }
    }
    return false;
}

bool hasAlpha(const TextureData& texture) {
    if (texture.levels.empty()) {
        return false;
    }
    const std::vector<std::uint8_t>& texels = texture.levels[0].texels;
    for (std::size_t at = 3; at < texels.size(); at += 4) {
        if (texels[at] != 255) {
            return true;
        }
    }
    return false;
}

std::vector<SceneDrawPass> drawPassesOf(const SceneStates& states, bool translucent, bool layer) {
    // An opaque mesh: librw turns blending and the alpha test off.
    if (!translucent) {
        return {SceneDrawPass{false, AlphaRange{}, states.depthWrite}};
    }
    const bool blend = states.alphaBlend;
    // A first pass's GS test (not for a dual layer, drawn with the test off).
    if (!layer && states.alphaTest == AlphaTest::GreaterEqual) {
        if (!states.depthWrite) {
            return {SceneDrawPass{blend, AlphaRange{}, false}};
        }
        return {SceneDrawPass{blend, AlphaRange{kGsAlphaRef, 1000.0F}, true},
                SceneDrawPass{blend, AlphaRange{-1000.0F, kGsAlphaRef}, false}};
    }
    AlphaRange kept;
    if (!layer && states.alphaTest == AlphaTest::Less) {
        kept.high = kStartAlphaRef;
    }
    return {SceneDrawPass{blend, kept, states.depthWrite}};
}

TextureLevels textureLevelsOf(const TextureData* texture, bool distanceLevels) {
    TextureLevels out;
    if (texture == nullptr || !texture->lod) {
        return out; // Coney's own, or none: the GPU chooses
    }
    out.k = texture->lod->k;
    out.scale = std::ldexp(1.0F, texture->lod->l);
    out.byDistance = distanceLevels;
    out.gpuChooses = false;
    return out;
}

float textureLevelAt(const TextureLevels& levels, float depth) {
    if (!levels.byDistance) {
        return 0.0F;
    }
    return std::max((std::log2(std::max(depth, 1.0F / 1024.0F)) * levels.scale) + levels.k, 0.0F);
}

float fogAt(const SceneCamera& camera, bool fog, float depth) {
    const float range = 1.0F / (camera.fogStart - camera.farClip);
    return std::clamp((depth - camera.farClip) * range, fog ? 0.0F : 1.0F, 1.0F);
}

} // namespace coney::graphics
