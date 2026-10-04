// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/character_mesh.h"

#include <cstddef>

#include <rw.h>

#include "core/assert.h"

namespace coney::platform {

namespace {

// librw's vector from ours.
rw::V3d toRw(anim::Vec3 v) { return rw::V3d{v.x, v.y, v.z}; }

} // namespace

CharacterMesh::CharacterMesh(const characters::CharacterModel& model, rw::Texture* texture) : m_texture(texture) {
    if (m_texture != nullptr) {
        m_texture->addRef();
    }
    // The geometry: positions, normals, one texture-coordinate set, lit and modulated by the material colour.
    const auto vertexCount = static_cast<rw::int32>(model.vertices.size());
    const auto triangleCount = static_cast<rw::int32>(model.triangles.size());
    rw::Geometry* geometry =
        rw::Geometry::create(vertexCount, triangleCount,
                             rw::Geometry::POSITIONS | rw::Geometry::NORMALS | rw::Geometry::LIGHT |
                                 rw::Geometry::MODULATE | rw::Geometry::TEXTURED);
    for (const characters::ClumpMaterial& source : model.clump.materials) {
        rw::Material* material = rw::Material::create();
        material->color = rw::makeRGBA(source.colour[0], source.colour[1], source.colour[2], source.colour[3]);
        material->setTexture(m_texture);
        geometry->matList.appendMaterial(material);
        material->destroy(); // the list holds its own reference
    }
    rw::MorphTarget& target = geometry->morphTargets[0];
    for (std::size_t i = 0; i < model.vertices.size(); ++i) {
        const characters::SkinVertex& vertex = model.vertices[i];
        target.vertices[i] = toRw(vertex.position);
        target.normals[i] = toRw(vertex.normal);
        geometry->texCoords[0][i] = rw::TexCoords{vertex.texCoords[0], vertex.texCoords[1]};
    }
    for (std::size_t i = 0; i < model.triangles.size(); ++i) {
        const characters::ModelTriangle& triangle = model.triangles[i];
        geometry->triangles[i].v[0] = triangle.vertices[0];
        geometry->triangles[i].v[1] = triangle.vertices[1];
        geometry->triangles[i].v[2] = triangle.vertices[2];
        geometry->triangles[i].matId = triangle.material;
    }
    geometry->buildMeshes();
    geometry->calculateBoundingSphere();

    // The atomic, on a frame of its own at the origin.
    m_atomic = rw::Atomic::create();
    m_atomic->setGeometry(geometry, 0);
    geometry->destroy(); // the atomic holds its own reference
    m_atomic->setFrame(rw::Frame::create());
}

CharacterMesh::~CharacterMesh() {
    rw::Frame* frame = m_atomic->getFrame();
    m_atomic->destroy();
    if (frame != nullptr) {
        frame->destroy();
    }
    if (m_texture != nullptr) {
        m_texture->destroy(); // drops this mesh's reference
    }
}

void CharacterMesh::update(std::span<const anim::Vec3> positions, std::span<const anim::Vec3> normals) {
    rw::Geometry* geometry = m_atomic->geometry;
    CONEY_ASSERT(positions.size() == static_cast<std::size_t>(geometry->numVertices) &&
                 normals.size() == positions.size());
    geometry->lock(rw::Geometry::LOCKVERTICES | rw::Geometry::LOCKNORMALS);
    rw::MorphTarget& target = geometry->morphTargets[0];
    for (std::size_t i = 0; i < positions.size(); ++i) {
        target.vertices[i] = toRw(positions[i]);
        target.normals[i] = toRw(normals[i]);
    }
    // The bounding sphere follows the pose, so librw's frustum test never culls a stretched limb.
    geometry->calculateBoundingSphere();
    geometry->unlock();
}

} // namespace coney::platform
