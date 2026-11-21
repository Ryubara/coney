// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "animation/anim_math.h"
#include "characters/character_model.h"
#include "graphics/asset_store.h"
#include "platform/character_skin.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Atomic;
struct Texture;
} // namespace rw

namespace coney::platform {

class SceneAssets;

/// Where a character's mesh is skinned.
enum class Skinning : std::uint8_t {
    Cpu, ///< On the CPU, every vertex written into librw's geometry each frame (the viewers, reference images).
    Gpu, ///< In the vertex shader (platform/character_skin.h) when it runs, else on the CPU (play and scenes).
};

/// The placement of a character's space (game axes, the feet at the origin, facing +y) in the world in RenderWare's
/// axes: leaned by `lean` radians into the turn about the forward axis at the feet (**Coney's choice** of axis and
/// pivot: the research gives the lean's angle, not how the body takes it), turned by `heading` radians about z, moved
/// to `feet`, and turned into RenderWare's axes ((x, y, z) to (x, z, -y)).
[[nodiscard]] anim::Mat34 characterPlacement(anim::Vec3 feet, float heading, float lean);

/// A box: the vertices one skin node moves (with any weight), in the model's space; or the posed body's, in the world.
struct BoneBox {
    anim::Vec3 min;
    anim::Vec3 max;
    bool empty = true;
};

/// Each skin node's box of the vertices it moves.
[[nodiscard]] std::vector<BoneBox> boneBoxes(const characters::CharacterModel& model);

/// The box round every node's box as its matrix in `skinning` moves it, and round the origin (the feet). Every skinned
/// vertex lies inside: a vertex is a blend of points each inside one of the moved boxes, by weights that sum to 1 or
/// a hair below (characters.md#character-geometry), so it lies between them and the origin. Empty with no boxes.
[[nodiscard]] BoneBox posedBox(std::span<const BoneBox> boxes, std::span<const anim::Mat34> skinning);

/// Whether a mesh asking for `skinning`, of a model with `nodes` skin nodes, is skinned by the vertex shader: only with
/// the skinned pipeline running (`pipelineOn`, characterSkinOn()) and at most kMaxSkinBones nodes, the shader's
/// palette; any other mesh is skinned on the CPU.
[[nodiscard]] bool skinsOnGpu(Skinning skinning, bool pipelineOn, std::size_t nodes);

/// Each vertex's weights and bone indices as the skinned pipeline's vertex buffer takes them: four of each, a weight
/// of 0 and bone 0 in an unused slot.
[[nodiscard]] SkinVertexData packSkinVertices(std::span<const characters::SkinVertex> vertices);

/// Writes the skinning matrices into the shader's palette, each a column-major 4 x 4 matrix; matrices past
/// kMaxSkinBones are left out and the palette's other matrices left as they were.
void fillBonePalette(std::span<const anim::Mat34> skinning, BonePalette& palette);

/// A character model as plain librw geometry: the model's vertices and triangles in one librw atomic with one material
/// per model material, textured with the character's texture. Skinned on the GPU (Skinning::Gpu with the skinned
/// pipeline running), the vertices are uploaded once and each frame sets only the bone matrices; skinned on the CPU,
/// each frame's positions and normals are written in and librw uploads them again. Owns the atomic, its
/// geometry and frame, and a reference to the texture. Needs a running RenderEngine (either backend) and must be
/// destroyed before it stops. Not copyable or movable: librw keeps pointers to it.
///
/// A second atomic holds the same triangles with the model's second texture-coordinate set: the material's MatFX dual
/// layer, which the original draws right after the first pass with a blood texture (bloodAtomic()). librw's GL3
/// pipeline draws one texture per atomic, so the layer is an atomic of its own, posed with the first.
///
/// Research: docs/research/characters.md#coneys-implementation, docs/research/rendering.md#characters
class CharacterMesh {
  public:
    /// Builds the geometry of `model` in its unskinned (bind) positions. `texture` (may be null: untextured) is used by
    /// every material, as the models' materials name no texture of their own (characters.md); the mesh keeps a
    /// reference to it. Lit by the world's lights with the vertex normals, modulated by the material colour. With
    /// `assets` (which must outlive the mesh: the RenderEngine's), both atomics are held there by id for the frame
    /// description's draws (meshId(), bloodMeshId(); docs/engine/renderer.md#frame-description) until the mesh goes,
    /// with each vertex's bones and weights and the texture, which a renderer that skins them needs; and where those
    /// assets are for a renderer that skins in its shaders (SceneAssets::skinsInShaders()), the mesh is posed by its
    /// bone palette alone, as on GL3's skinned pipeline (docs/engine/gpu-renderer.md#skinning).
    CharacterMesh(const characters::CharacterModel& model, rw::Texture* texture, Skinning skinning = Skinning::Cpu,
                  SceneAssets* assets = nullptr);
    ~CharacterMesh();
    CharacterMesh(const CharacterMesh&) = delete;
    CharacterMesh& operator=(const CharacterMesh&) = delete;
    CharacterMesh(CharacterMesh&&) = delete;
    CharacterMesh& operator=(CharacterMesh&&) = delete;

    /// Replaces every vertex's position and normal, in both atomics; both spans hold one entry per model vertex
    /// (checked by CONEY_ASSERT). They are in the atomics' frames, which stay at the origin unless pose() moved them.
    /// Only on a mesh skinned on the CPU, and not mixed with pose().
    void update(std::span<const anim::Vec3> positions, std::span<const anim::Vec3> normals);

    /// Poses the mesh: `skinning` holds the model's skinning matrices (characters::skinningMatrices(), one per skin
    /// node) and `placement` puts the character's space into the world (characterPlacement()). The blend is done in
    /// the character's space and the placement is both atomics' frame, so it moves the blended vertex whole, as it must
    /// with weights that sum a hair below 1. On the GPU only the bone matrices change, and the bounding sphere is the
    /// box round each bone's vertices as the bone moved them (posedBox()); on the CPU every vertex is blended by its
    /// bones (as characters::skinVertices()) straight into librw's geometry, and the sphere is the vertices' own.
    /// The same as preparePose() then commitPose().
    void pose(std::span<const anim::Mat34> skinning, const anim::Mat34& placement);

    /// The half of pose() that calls no librw and touches only this mesh, so a worker thread may run it while the main
    /// thread waits (docs/engine/renderer-performance.md#posing-on-workers): keeps the skinning matrices, and on the
    /// GPU fills the shader's palette and works out the posed box (posedBox()), on the CPU blends every vertex into the
    /// mesh's own buffers. Each mesh on one thread at a time; commitPose() follows on the main thread.
    void preparePose(std::span<const anim::Mat34> skinning);
    /// The librw half of pose(), on the main thread after preparePose(): places both atomics' frames by `placement`
    /// and gives them the prepared pose's bounding sphere and, on the CPU, its vertices.
    void commitPose(const anim::Mat34& placement);

    /// Whether the vertex shader skins this mesh.
    [[nodiscard]] bool gpuSkinned() const { return m_gpu; }
    /// The skinning matrices of the last pose(), one per skin node in the character's space: the pose a frame
    /// description's draw of the mesh carries. Empty before the first pose() and for a mesh posed by update().
    [[nodiscard]] std::span<const anim::Mat34> skinning() const { return m_skinning; }

    /// The first pass's atomic in the scene assets the mesh was given; invalid without them, or when it did not
    /// convert.
    [[nodiscard]] graphics::MeshId meshId() const { return m_meshId; }
    /// The blood layer's atomic in the scene assets, as meshId().
    [[nodiscard]] graphics::MeshId bloodMeshId() const { return m_bloodMeshId; }

    /// The librw atomic of the first pass, for drawing. Valid as long as this object.
    [[nodiscard]] rw::Atomic* atomic() const { return m_atomic; }
    /// The librw atomic of the dual (blood) layer, untextured until setBloodTexture(). Valid as long as this object.
    [[nodiscard]] rw::Atomic* bloodAtomic() const { return m_blood; }
    /// Sets the dual layer's texture (null: none); its materials hold their own reference.
    void setBloodTexture(rw::Texture* texture);

    /// How many passes the last frame drew of it: 0 (too far, not drawn), 1 (the texture) or 2 (with the blood layer),
    /// as PlayLighting::drawHumanPasses() records it; the Entity inspector's Mesh group counts its draw calls from it.
    [[nodiscard]] std::size_t drawnPasses() const { return m_drawnPasses; }
    /// Records the passes drawn this frame.
    void setDrawnPasses(std::size_t passes) { m_drawnPasses = passes; }

  private:
    // The CPU pose's librw half: the prepared vertices into both atomics' geometry, in the character's space.
    void commitOnCpu();
    // The GPU pose's librw half: the sphere from the prepared box, in the character's space.
    void commitOnGpu();

    rw::Atomic* m_atomic = nullptr;                 // owned, with its geometry and frame
    rw::Atomic* m_blood = nullptr;                  // owned, with its geometry and frame
    rw::Texture* m_texture = nullptr;               // one reference held
    rw::Texture* m_bloodTexture = nullptr;          // the dual layer's, as last set (its materials hold the references)
    std::vector<characters::SkinVertex> m_vertices; // the model's, for skinning on the CPU (empty on the GPU)
    std::vector<anim::Vec3> m_positions;            // the CPU pose's blended positions and normals, kept between poses
    std::vector<anim::Vec3> m_normals;
    bool m_gpu = false;
    SkinVertexData m_skin;                  // the shader's weights and bones (GPU)
    std::unique_ptr<BonePalette> m_palette; // this frame's bone matrices (GPU)
    std::vector<BoneBox> m_boxes;           // per skin node (GPU)
    BoneBox m_posedBox;                     // the prepared pose's box (GPU)
    std::size_t m_drawnPasses = 0;          // setDrawnPasses()
    std::vector<anim::Mat34> m_skinning;    // skinning()
    SceneAssets* m_assets = nullptr;        // where meshId() and bloodMeshId() are held; null: nowhere
    graphics::TextureId m_textureId;        // the texture's hold there, for the meshes' materials
    graphics::MeshId m_meshId;
    graphics::MeshId m_bloodMeshId;
};

} // namespace coney::platform
