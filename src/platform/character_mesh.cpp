// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/character_mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
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

// Puts an atomic's frame at `placement`: the character's space in the world (characterPlacement()).
void placeFrame(rw::Atomic* atomic, const anim::Mat34& placement) {
    rw::Matrix matrix;
    matrix.setIdentity();
    matrix.right = toRw(placement.x);
    matrix.up = toRw(placement.y);
    matrix.at = toRw(placement.z);
    matrix.pos = toRw(placement.t);
    matrix.update();
    atomic->getFrame()->transform(&matrix, rw::COMBINEREPLACE);
}

} // namespace

namespace {

// `box` grown to hold `p`.
void include(BoneBox& box, anim::Vec3 p) {
    box.min = box.empty ? p : anim::Vec3{std::min(box.min.x, p.x), std::min(box.min.y, p.y), std::min(box.min.z, p.z)};
    box.max = box.empty ? p : anim::Vec3{std::max(box.max.x, p.x), std::max(box.max.y, p.y), std::max(box.max.z, p.z)};
    box.empty = false;
}

} // namespace

std::vector<BoneBox> boneBoxes(const characters::CharacterModel& model) {
    std::vector<BoneBox> boxes(model.clump.skin.inverseBind.size());
    for (const characters::SkinVertex& vertex : model.vertices) {
        for (std::size_t w = 0; w < characters::kVertexWeights; ++w) {
            if (vertex.weights[w] != 0.0F && vertex.bones[w] < boxes.size()) {
                include(boxes[vertex.bones[w]], vertex.position);
            }
        }
    }
    return boxes;
}

BoneBox posedBox(std::span<const BoneBox> boxes, std::span<const anim::Mat34> skinning) {
    BoneBox posed;
    for (std::size_t bone = 0; bone < boxes.size() && bone < skinning.size(); ++bone) {
        const BoneBox& box = boxes[bone];
        if (box.empty) {
            continue;
        }
        for (unsigned corner = 0; corner < 8; ++corner) {
            const anim::Vec3 p{(corner & 1U) != 0 ? box.max.x : box.min.x, (corner & 2U) != 0 ? box.max.y : box.min.y,
                               (corner & 4U) != 0 ? box.max.z : box.min.z};
            include(posed, anim::transformPoint(skinning[bone], p));
        }
    }
    // The origin too: weights that sum below 1 pull a vertex towards it.
    if (!posed.empty) {
        include(posed, anim::Vec3{});
    }
    return posed;
}

bool skinsOnGpu(Skinning skinning, bool pipelineOn, std::size_t nodes) {
    return skinning == Skinning::Gpu && pipelineOn && nodes <= kMaxSkinBones;
}

SkinVertexData packSkinVertices(std::span<const characters::SkinVertex> vertices) {
    static_assert(characters::kVertexWeights == 4, "the shader blends four bones a vertex");
    SkinVertexData data;
    data.weights.reserve(vertices.size());
    data.indices.reserve(vertices.size());
    for (const characters::SkinVertex& vertex : vertices) {
        data.weights.push_back(vertex.weights);
        data.indices.push_back(vertex.bones);
    }
    return data;
}

void fillBonePalette(std::span<const anim::Mat34> skinning, BonePalette& palette) {
    for (std::size_t i = 0; i < skinning.size() && i < kMaxSkinBones; ++i) {
        const anim::Mat34& m = skinning[i];
        const std::array<float, kBoneFloats> column{m.x.x, m.x.y, m.x.z, 0.0F, m.y.x, m.y.y, m.y.z, 0.0F,
                                                    m.z.x, m.z.y, m.z.z, 0.0F, m.t.x, m.t.y, m.t.z, 1.0F};
        std::ranges::copy(column, palette.begin() + static_cast<std::ptrdiff_t>(i * kBoneFloats));
    }
}

anim::Mat34 characterPlacement(anim::Vec3 feet, float heading, float lean) {
    // The lean's roll about the forward (y) axis, then the heading about z, as columns: where each axis goes.
    const float lc = std::cos(-lean);
    const float ls = std::sin(-lean);
    const float c = std::cos(heading);
    const float s = std::sin(heading);
    const anim::Mat34 roll{{lc, 0.0F, -ls}, {0.0F, 1.0F, 0.0F}, {ls, 0.0F, lc}, {}};
    const anim::Mat34 turn{{c, s, 0.0F}, {-s, c, 0.0F}, {0.0F, 0.0F, 1.0F}, feet};
    // Game axes into RenderWare's: (x, y, z) to (x, z, -y).
    const anim::Mat34 toRenderWare{{1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F}, {}};
    return anim::multiply(toRenderWare, anim::multiply(turn, roll));
}

CharacterMesh::CharacterMesh(const characters::CharacterModel& model, rw::Texture* texture, Skinning skinning)
    : m_texture(texture) {
    if (m_texture != nullptr) {
        m_texture->addRef();
    }
    m_atomic = makeAtomic(model, m_texture, &characters::SkinVertex::texCoords);
    m_blood = makeAtomic(model, nullptr, &characters::SkinVertex::secondTexCoords);
    m_gpu = skinsOnGpu(skinning, characterSkinOn(), model.clump.skin.inverseBind.size());
    if (!m_gpu) {
        m_vertices = model.vertices; // blended on the CPU at each pose
        return;
    }
    // The shader's weights and bones, and each bone's box of the vertices it moves.
    m_skin = packSkinVertices(model.vertices);
    m_boxes = boneBoxes(model);
    m_palette = std::make_unique<BonePalette>();
    m_palette->fill(0.0F);
    useCharacterSkin(m_atomic, m_skin, *m_palette);
    useCharacterSkin(m_blood, m_skin, *m_palette);
}

// Both atomics with their geometry and frames, then the mesh's texture reference.
CharacterMesh::~CharacterMesh() {
    destroyAtomic(m_blood); // its materials drop their blood texture references
    destroyAtomic(m_atomic);
    if (m_texture != nullptr) {
        m_texture->destroy(); // drops this mesh's reference
    }
}

void CharacterMesh::update(std::span<const anim::Vec3> positions, std::span<const anim::Vec3> normals) {
    CONEY_ASSERT(!m_gpu);
    writePose(m_atomic, positions, normals);
    writePose(m_blood, positions, normals);
}

void CharacterMesh::pose(std::span<const anim::Mat34> skinning, const anim::Mat34& placement) {
    // The blend stays in the character's space and the frame places its result, as the per-vertex placement did:
    // folded into each bone's matrix, the placement would move a vertex by its weights' sum, which the stored weights
    // keep a hair below 1 (characters.md#character-geometry).
    placeFrame(m_atomic, placement);
    placeFrame(m_blood, placement);
    if (m_gpu) {
        poseOnGpu(skinning);
    } else {
        poseOnCpu(skinning);
    }
}

void CharacterMesh::poseOnCpu(std::span<const anim::Mat34> skinning) {
    rw::Geometry* geometry = m_atomic->geometry;
    CONEY_ASSERT(m_vertices.size() == static_cast<std::size_t>(geometry->numVertices));
    geometry->lock(rw::Geometry::LOCKVERTICES | rw::Geometry::LOCKNORMALS);
    rw::MorphTarget& target = geometry->morphTargets[0];
    // Each vertex by its bones' matrices, in the character's space; the normal blended, then normalised.
    for (std::size_t i = 0; i < m_vertices.size(); ++i) {
        const characters::SkinVertex& vertex = m_vertices[i];
        anim::Vec3 position;
        anim::Vec3 normal;
        for (std::size_t w = 0; w < characters::kVertexWeights; ++w) {
            if (vertex.weights[w] == 0.0F) {
                continue;
            }
            const anim::Mat34& matrix = skinning[vertex.bones[w]];
            position =
                anim::add(position, anim::scale(anim::transformPoint(matrix, vertex.position), vertex.weights[w]));
            normal = anim::add(normal, anim::scale(anim::transformDirection(matrix, vertex.normal), vertex.weights[w]));
        }
        target.vertices[i] = toRw(position);
        target.normals[i] = toRw(anim::normalise(normal));
    }
    geometry->calculateBoundingSphere();
    geometry->unlock();
    m_atomic->boundingSphere = target.boundingSphere;
    m_atomic->object.object.privateFlags |= rw::Atomic::WORLDBOUNDDIRTY;
    // The blood layer takes the same vertices.
    rw::Geometry* layer = m_blood->geometry;
    layer->lock(rw::Geometry::LOCKVERTICES | rw::Geometry::LOCKNORMALS);
    rw::MorphTarget& layerTarget = layer->morphTargets[0];
    std::copy_n(target.vertices, geometry->numVertices, layerTarget.vertices);
    std::copy_n(target.normals, geometry->numVertices, layerTarget.normals);
    layerTarget.boundingSphere = target.boundingSphere;
    layer->unlock();
    m_blood->boundingSphere = target.boundingSphere;
    m_blood->object.object.privateFlags |= rw::Atomic::WORLDBOUNDDIRTY;
}

void CharacterMesh::poseOnGpu(std::span<const anim::Mat34> skinning) {
    fillBonePalette(skinning, *m_palette);
    // The character's box round every bone's box as its matrix moves it (posedBox()); librw moves the sphere by the
    // frame. The sphere is the box's, as librw makes a sphere from the vertices' box: **Coney's choice**, a box a
    // little larger than the vertices' own.
    const BoneBox box = posedBox(m_boxes, skinning);
    const anim::Vec3 low = box.min;
    const anim::Vec3 high = box.max;
    rw::Sphere sphere{};
    sphere.center = rw::V3d{(low.x + high.x) * 0.5F, (low.y + high.y) * 0.5F, (low.z + high.z) * 0.5F};
    const anim::Vec3 half{(high.x - low.x) * 0.5F, (high.y - low.y) * 0.5F, (high.z - low.z) * 0.5F};
    sphere.radius = std::sqrt((half.x * half.x) + (half.y * half.y) + (half.z * half.z));
    for (rw::Atomic* atomic : {m_atomic, m_blood}) {
        atomic->geometry->morphTargets[0].boundingSphere = sphere;
        atomic->boundingSphere = sphere;
        atomic->object.object.privateFlags |= rw::Atomic::WORLDBOUNDDIRTY;
    }
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
