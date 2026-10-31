// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/mesh_data.h"

#include <format>

#include "graphics/asset_hash.h"

namespace coney::graphics {

std::expected<void, Error> validateMesh(const MeshData& mesh) {
    if (mesh.attributes.texCoordSets > kMeshTexCoordSets) {
        return fail(ErrorCode::Invalid, std::format("a mesh with {} texture-coordinate sets (at most {})",
                                                    mesh.attributes.texCoordSets, kMeshTexCoordSets));
    }
    // Each sub-mesh within the index list and naming a material that exists.
    for (std::size_t s = 0; s < mesh.subMeshes.size(); ++s) {
        const SubMesh& sub = mesh.subMeshes[s];
        if (std::size_t{sub.firstIndex} + sub.indexCount > mesh.indices.size()) {
            return fail(ErrorCode::Invalid,
                        std::format("sub-mesh {} runs past the {} indices", s, mesh.indices.size()));
        }
        if (sub.material >= mesh.materials.size()) {
            return fail(ErrorCode::Invalid,
                        std::format("sub-mesh {} names material {} of {}", s, sub.material, mesh.materials.size()));
        }
    }
    // Every index naming a vertex.
    for (std::size_t i = 0; i < mesh.indices.size(); ++i) {
        if (mesh.indices[i] >= mesh.vertices.size()) {
            return fail(ErrorCode::Invalid,
                        std::format("index {} names vertex {} of {}", i, mesh.indices[i], mesh.vertices.size()));
        }
    }
    return {};
}

std::uint64_t hashMesh(const MeshData& mesh) {
    AssetHash hash;
    // The vertices, field by field, so padding in the struct never reaches the hash.
    hash.add(mesh.vertices.size());
    for (const MeshVertex& v : mesh.vertices) {
        hash.add(v.position).add(v.normal);
        hash.add(v.prelit.r).add(v.prelit.g).add(v.prelit.b).add(v.prelit.a);
        for (const auto& set : v.texCoords) {
            hash.add(set);
        }
        hash.add(v.bones).add(v.weights);
    }
    hash.add(mesh.indices.size());
    for (const std::uint16_t index : mesh.indices) {
        hash.add(index);
    }
    hash.add(mesh.subMeshes.size());
    for (const SubMesh& sub : mesh.subMeshes) {
        hash.add(sub.firstIndex).add(sub.indexCount).add(sub.material);
    }
    hash.add(mesh.materials.size());
    for (const MeshMaterial& material : mesh.materials) {
        hash.add(material.colour.r).add(material.colour.g).add(material.colour.b).add(material.colour.a);
        hash.add(material.texture);
    }
    hash.add(mesh.attributes.normals).add(mesh.attributes.prelit).add(mesh.attributes.texCoordSets);
    hash.add(mesh.attributes.skinned).add(static_cast<std::uint8_t>(mesh.topology));
    hash.add(mesh.bounds.centre).add(mesh.bounds.radius);
    return hash.value();
}

} // namespace coney::graphics
