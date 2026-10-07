// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/character_mesh.h"

#include <array>
#include <cstddef>

#include <rw.h>

#include "core/assert.h"

namespace coney::platform {

namespace {

// librw's vector from ours.
rw::V3d toRw(anim::Vec3 v) { return rw::V3d{v.x, v.y, v.z}; }

// An atomic, on a frame of its own at the origin, holding `model`'s triangles textured with `texture` through the
// texture coordinates `set` picks (each vertex's first or second set).
rw::Atomic* makeAtomic(const characters::CharacterModel& model, rw::Texture* texture,
                       std::array<float, 2> characters::SkinVertex::* set) {
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
        material->setTexture(texture);
        geometry->matList.appendMaterial(material);
        material->destroy(); // the list holds its own reference
    }
    rw::MorphTarget& target = geometry->morphTargets[0];
    for (std::size_t i = 0; i < model.vertices.size(); ++i) {
        const characters::SkinVertex& vertex = model.vertices[i];
        target.vertices[i] = toRw(vertex.position);
        target.normals[i] = toRw(vertex.normal);
        const std::array<float, 2>& uv = vertex.*set;
        geometry->texCoords[0][i] = rw::TexCoords{uv[0], uv[1]};
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

    rw::Atomic* atomic = rw::Atomic::create();
    atomic->setGeometry(geometry, 0);
    geometry->destroy(); // the atomic holds its own reference
    atomic->setFrame(rw::Frame::create());
    return atomic;
}

// Destroys an atomic made by makeAtomic(), with its frame (which Atomic::destroy leaves alone).
void destroyAtomic(rw::Atomic* atomic) {
    rw::Frame* frame = atomic->getFrame();
    atomic->destroy();
    if (frame != nullptr) {
        frame->destroy();
    }
}

// Writes the posed positions and normals into an atomic's geometry.
void writePose(rw::Atomic* atomic, std::span<const anim::Vec3> positions, std::span<const anim::Vec3> normals) {
    rw::Geometry* geometry = atomic->geometry;
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
    // The atomic keeps its own copy of the sphere (taken when the geometry was set) and a world copy it only redoes
    // when marked dirty: both follow the pose, so the lights are chosen where the human stands, not at the bind pose.
    atomic->boundingSphere = geometry->morphTargets[0].boundingSphere;
    atomic->object.object.privateFlags |= rw::Atomic::WORLDBOUNDDIRTY;
}

} // namespace

CharacterMesh::CharacterMesh(const characters::CharacterModel& model, rw::Texture* texture) : m_texture(texture) {
    if (m_texture != nullptr) {
        m_texture->addRef();
    }
    m_atomic = makeAtomic(model, m_texture, &characters::SkinVertex::texCoords);
    m_blood = makeAtomic(model, nullptr, &characters::SkinVertex::secondTexCoords);
}

CharacterMesh::~CharacterMesh() {
    destroyAtomic(m_blood); // its materials drop their blood texture references
    destroyAtomic(m_atomic);
    if (m_texture != nullptr) {
        m_texture->destroy(); // drops this mesh's reference
    }
}

void CharacterMesh::update(std::span<const anim::Vec3> positions, std::span<const anim::Vec3> normals) {
    writePose(m_atomic, positions, normals);
    writePose(m_blood, positions, normals);
}

void CharacterMesh::setBloodTexture(rw::Texture* texture) {
    if (texture == m_bloodTexture) {
        return;
    }
    m_bloodTexture = texture;
    rw::Geometry* geometry = m_blood->geometry;
    for (rw::int32 i = 0; i < geometry->matList.numMaterials; ++i) {
        geometry->matList.materials[i]->setTexture(texture);
    }
}

} // namespace coney::platform
