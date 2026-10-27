// SPDX-License-Identifier: GPL-3.0-or-later
// The Entity inspector's Mesh group (docs/guides/debug-menu.md#mesh-group). Coney's own debug tooling.
#include "debug/mesh_stats.h"

#include <algorithm>
#include <format>
#include <set>
#include <string>
#include <utility>

namespace coney::debug {

namespace {

// A material's texture as the group prints it: "texture skin 128x64", "texture skin" with no size, "untextured".
std::string textureText(const MaterialStats& material) {
    if (material.texture.empty()) {
        return "untextured";
    }
    if (material.textureWidth <= 0 || material.textureHeight <= 0) {
        return "texture " + material.texture;
    }
    return std::format("texture {} {}x{}", material.texture, material.textureWidth, material.textureHeight);
}

} // namespace

MeshStats characterMeshStats(const characters::CharacterModel& model, const TextureInfo& texture) {
    MeshStats stats;
    // platform::CharacterMesh: one atomic, one geometry of the distinct vertices, one material per clump material.
    stats.atomics = 1;
    stats.geometries = 1;
    stats.triangles = model.triangles.size();
    stats.vertices = model.vertices.size();
    stats.materials.resize(model.clump.materials.size());
    for (std::size_t i = 0; i < model.clump.materials.size(); ++i) {
        MaterialStats& material = stats.materials[i];
        material.colour = model.clump.materials[i].colour;
        // The models' materials name no texture of their own: the mesh draws every one with the character's.
        material.texture = texture.name;
        material.textureWidth = texture.width;
        material.textureHeight = texture.height;
    }
    for (const characters::ModelTriangle& triangle : model.triangles) {
        if (triangle.material < stats.materials.size()) {
            ++stats.materials[triangle.material].triangles;
        }
    }
    std::set<std::string> textures;
    for (const MaterialStats& material : stats.materials) {
        if (!material.texture.empty()) {
            textures.insert(material.texture);
        }
    }
    stats.textures = textures.size();
    stats.hierarchyBones = model.clump.hierarchy.size();
    stats.skinBones = model.clump.skin.boneCount;
    stats.usedBones = model.clump.skin.usedBones.size();
    stats.declaredWeights = model.clump.skin.maxWeights;
    for (const characters::SkinVertex& vertex : model.vertices) {
        const auto weights =
            static_cast<std::size_t>(std::ranges::count_if(vertex.weights, [](float weight) { return weight > 0.0F; }));
        ++stats.verticesByWeights.at(weights);
    }
    // librw's buildMeshes() makes a mesh for every material, an empty one too, and its GL3 pipeline draws each.
    stats.drawsPerPass = stats.materials.size();
    return stats;
}

std::size_t drawCallsOf(const MeshStats& stats, const MeshDrawn& drawn) { return stats.drawsPerPass * drawn.passes; }

double meanWeights(const MeshStats& stats) {
    std::size_t vertices = 0;
    std::size_t weights = 0;
    for (std::size_t count = 0; count < stats.verticesByWeights.size(); ++count) {
        vertices += stats.verticesByWeights[count];
        weights += stats.verticesByWeights[count] * count;
    }
    return vertices == 0 ? 0.0 : static_cast<double>(weights) / static_cast<double>(vertices);
}

WatchGroup describeMesh(const MeshStats& stats, const MeshDrawn& drawn) {
    WatchGroup group{.name = "Mesh", .lines = {}};
    const auto add = [&group](std::string name, std::string value) {
        group.lines.push_back(WatchLine{.name = std::move(name), .value = std::move(value)});
    };
    add("Atomics", std::to_string(stats.atomics));
    add("Geometries", std::to_string(stats.geometries));
    add("Triangles", std::to_string(stats.triangles));
    add("Vertices", std::to_string(stats.vertices));
    add("Materials", std::to_string(stats.materials.size()));
    for (std::size_t i = 0; i < stats.materials.size(); ++i) {
        const MaterialStats& material = stats.materials[i];
        add(std::format("Material {}", i),
            std::format("{} triangles, colour ({}, {}, {}, {}), {}", material.triangles, material.colour[0],
                        material.colour[1], material.colour[2], material.colour[3], textureText(material)));
    }
    add("Textures", std::to_string(stats.textures));
    add("Bones", std::format("{} in the hierarchy, {} skinned, {} used", stats.hierarchyBones, stats.skinBones,
                             stats.usedBones));
    // The most weights any vertex carries, then the vertices by how many they carry (0 left out when none has 0).
    std::size_t most = 0;
    std::string spread;
    for (std::size_t count = 0; count < stats.verticesByWeights.size(); ++count) {
        if (stats.verticesByWeights[count] == 0) {
            continue;
        }
        most = count;
        spread += std::format("{}{}: {}", spread.empty() ? "" : ", ", count, stats.verticesByWeights[count]);
    }
    add("Weights per vertex", std::format("most {} (skin allows {}), mean {:.2f}; {}", most, stats.declaredWeights,
                                          meanWeights(stats), spread.empty() ? "-" : spread));
    // "1 material x 1 pass": a count of one is singular.
    add("Draw calls last frame",
        std::format("{} ({} material{} x {} pass{})", drawCallsOf(stats, drawn), stats.drawsPerPass,
                    stats.drawsPerPass == 1 ? "" : "s", drawn.passes, drawn.passes == 1 ? "" : "es"));
    // Every model the game draws has one level of detail (docs/research/graphics.md#lod): only the distance fade
    // varies.
    add("LOD in use", std::format("0 of 1 (one model), fade {:.2f}", drawn.fade));
    add("Skinned on", drawn.skinning);
    return group;
}

} // namespace coney::debug
