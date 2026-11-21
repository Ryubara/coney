// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the GPU renderer's reading of the frame description's scene passes (docs/engine/gpu-renderer.md#scene);
// the original draws its meshes through RenderWare's PS2 pipelines
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "graphics/frame_description.h"
#include "graphics/mesh_data.h"
#include "graphics/overlay_batch.h"
#include "graphics/texture_data.h"

namespace coney::graphics {

/// A 4 x 4 matrix as the shaders take it, column by column: element `[column * 4 + row]`, and a point is the matrix
/// times the column (x, y, z, 1).
using Matrix4 = std::array<float, 16>;

/// The matrix that places a mesh at `where`: its axes and position, as librw's GL3 device sets an atomic's world
/// matrix from its frame.
[[nodiscard]] Matrix4 worldMatrixOf(const Transform3d& where);

/// The view matrix of `camera`: a point in the world to the camera's space, x along the screen's right, y along its up
/// and z the distance in front, as librw's GL3 device makes it from a camera placed by the world pass (which flips the
/// frame's x axis and the device flips it back).
[[nodiscard]] Matrix4 viewMatrixOf(const SceneCamera& camera);

/// The projection of `camera`: the camera's space to clip space with librw's perspective (the view window's half width
/// and height at a distance of 1, the near and far planes) and w the distance in front, the depth taken from OpenGL's
/// -w to w onto SDL's GPU API's 0 to w, so both renderers' depth buffers hold the same values: 0 at the near plane, 1
/// at the far one.
[[nodiscard]] Matrix4 projectionOf(const SceneCamera& camera);

/// `a` times `b`.
[[nodiscard]] Matrix4 multiply(const Matrix4& a, const Matrix4& b);

/// `point` (x, y, z, 1) through `matrix`.
[[nodiscard]] std::array<float, 4> transformPoint(const Matrix4& matrix, const std::array<float, 3>& point);

/// The most lights a mesh is drawn with besides the ambient ones: librw's GL3 shaders' MAX_LIGHTS.
inline constexpr std::size_t kMaxDrawLights = 8;

/// One light a mesh is drawn with, as librw's GL3 device hands it to its shaders.
struct DrawLight {
    LightType type = LightType::Directional; ///< Directional, Point or Spot.
    std::array<float, 3> colour{};
    std::array<float, 3> position{};  ///< Point and spot lights.
    std::array<float, 3> direction{}; ///< Directional and spot lights: the way the light travels, made unit.
    float radius = 0.0F;              ///< Point and spot lights.
    float minusCosAngle = 0.0F;       ///< Spot lights: -cos of the cone's angle.

    friend bool operator==(const DrawLight&, const DrawLight&) = default;
};

/// What a mesh is lit by: the sum of the ambient lights, and up to kMaxDrawLights others in the order librw's GL3
/// device uploads them (the directional lights, then the point and spot lights).
struct DrawLights {
    std::array<float, 3> ambient{};
    std::array<DrawLight, kMaxDrawLights> lights{};
    std::size_t count = 0;
};

/// The lights `mesh` at `where` is drawn with, of the frame's `lights` for the draw, as librw's GL3 device lights an
/// atomic from a world that holds them (World::enumerateLights, then setLights): nothing at all for a mesh that is not
/// lit (MeshData::lit); the ambient lights summed; the directional lights only for a mesh with normals; the point and
/// spot lights only for a mesh with normals and only those whose sphere meets the mesh's (its bounds placed at
/// `where`, the radius unscaled), at most kMaxDrawLights of them; then the directional lights first and the rest after,
/// kMaxDrawLights in all, each in the order given.
[[nodiscard]] DrawLights lightsFor(std::span<const SceneLight> lights, const MeshData& mesh, const Transform3d& where);
/// lightsFor() with the mesh's sphere taken as `bounds` (its own space) instead of MeshData::bounds: a posed mesh's,
/// boundsOf().
[[nodiscard]] DrawLights lightsFor(std::span<const SceneLight> lights, const MeshData& mesh, const Transform3d& where,
                                   const MeshBounds& bounds);

/// The sphere `draw` of `mesh` is lit by: the posed sphere the draw carries (Draw3d::bounds, a human's as his pose
/// moved it, the sphere librw's GL3 device tests his atomic's lights against), else the mesh's own.
[[nodiscard]] MeshBounds boundsOf(const Draw3d& draw, const MeshData& mesh);

/// The colours a mesh's materials are drawn with this draw, in MeshData::materials' order, and whether the mesh is
/// modulated by them (else the shaders take white): the mesh's own colours changed as `material` says, as the GL3
/// renderer changes the librw materials (platform::MaterialChange): under MaterialColour::Fade every alpha is the fade;
/// under Tint the first colour is the tint and the mesh is modulated while the tint is not opaque white; under Paint
/// every colour is the tint; under Shade the first colour is the tint's red, green and blue times the shade (held to 0
/// to 1, truncated) with the fade as its alpha. A mesh is otherwise modulated when MeshData::modulate says so.
struct DrawColours {
    std::vector<Rgba> colours;
    bool modulate = false;
};

/// How a 3D draw's mesh is placed by its pose.
enum class Posing : std::uint8_t {
    None,    ///< No pose: the mesh drawn as it is.
    Skinned, ///< Posed, and the mesh has the bones and weights to be skinned by it.
    LeftOut, ///< Posed, but the mesh cannot be skinned by it: drawn in its bind pose it would be wrong, so not drawn.
};

/// How `draw`, a 3D draw of `frame` of `mesh`, is posed: not at all without a pose (FrameDescription::bonesOf());
/// skinned when `mesh` carries bones and weights (MeshAttributes::skinned) and the pose holds every bone its vertices
/// name (`bonesNeeded`, bonesNeededBy()); left out otherwise, never drawn in its bind pose.
[[nodiscard]] Posing posingOf(const FrameDescription& frame, const Draw3d& draw, const MeshData& mesh,
                              std::size_t bonesNeeded);

/// How many bones the vertices of `mesh` name with a weight: the highest such index plus one, 0 for a mesh that is not
/// skinned.
[[nodiscard]] std::size_t bonesNeededBy(const MeshData& mesh);

/// The bone palette of a pose: each of its skinning matrices as a Matrix4, column by column with (0, 0, 0, 1) as the
/// last row, the layout of the GL3 renderer's palette (platform::fillBonePalette(), docs/guides/performance.md#gpu-
/// skinning), which the GPU renderer's skinning shader reads.
[[nodiscard]] std::vector<Matrix4> bonePaletteOf(std::span<const Transform3d> bones);

/// A vertex as the skinning shader places it, in the mesh's own space before the draw's transform.
struct SkinnedVertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
};

/// `vertex` skinned by `palette` as the GPU renderer's shader (`scene3dskin.vert.hlsl`) and the GL3 renderer's
/// (platform/character_skin.cpp) skin it, and as characters::skinVertices() did on the CPU: the position and the normal
/// each bone of a slot with a weight moves them to, times the weight, summed (weights that sum a hair below 1 are kept
/// as they are), the normal then made unit. A slot with no weight is skipped; its bone need not be in the palette.
[[nodiscard]] SkinnedVertex skinVertex(const MeshVertex& vertex, std::span<const Matrix4> palette);

/// The colours of `mesh` as `material` changes them (DrawColours).
[[nodiscard]] DrawColours coloursOf(const MeshData& mesh, const Material3d& material);

/// The colours of a dual layer `layer` drawn over a mesh whose colours are `base`: its own, each at the alpha of the
/// base's first material (255 when the base has none), as the GL3 renderer writes them before the layer's draw
/// (platform::drawLitAtomic()); modulated as its own MeshData::modulate says.
[[nodiscard]] DrawColours layerColoursOf(const MeshData& layer, const DrawColours& base);

/// The colours of `draw`'s dual layer `layer` drawn over a mesh coloured `base`: a human's blood layer
/// (DualPass::Blood) in his own colours, `layer`'s as the draw changes them (coloursOf()), as the GL3 renderer changes
/// the blood atomic's (platform::Gl3Renderer::drawHuman()); any other layer as layerColoursOf() says.
[[nodiscard]] DrawColours dualColoursOf(const Draw3d& draw, const MeshData& layer, const DrawColours& base);

/// The states `draw`'s dual layer is drawn in: a human's blood layer (GS context 2, docs/research/rendering.md#
/// characters) blended by source alpha with the alpha test off, its depth and culling the draw's; any other layer the
/// draw's own states.
[[nodiscard]] SceneStates dualStatesOf(const Draw3d& draw);

/// Whether any vertex `sub` of `mesh` draws has prelighting that is not opaque (librw's instancing marks such a mesh's
/// draw as having vertex alpha): only a prelit mesh can, over the vertices from the least index `sub` uses to the most.
[[nodiscard]] bool vertexAlphaOf(const MeshData& mesh, const SubMesh& sub);

/// Whether `texture` has a texel that is not opaque in its level 0, as librw reads an image when it makes a GL3 raster
/// from it (Image::hasAlpha): such a texture turns blending and the alpha test on.
[[nodiscard]] bool hasAlpha(const TextureData& texture);

/// librw's GS alpha test reference for a first pass (`GSALPHATESTREF`, platform::kFirstPassAlphaRef): 128 of 255.
inline constexpr float kGsAlphaRef = 128.0F / 255.0F;

/// One pass of a sub-mesh's draw: how it blends, which fragments it keeps and whether it writes depth.
struct SceneDrawPass {
    bool blend = false; ///< blended by source alpha over what is below, else replacing it
    AlphaRange alpha;   ///< the fragments kept
    bool depthWrite = true;

    friend bool operator==(const SceneDrawPass&, const SceneDrawPass&) = default;
};

/// The passes a sub-mesh is drawn in, as librw's GL3 device draws one mesh of an atomic (gl3render.cpp's drawInst):
///
/// - not `translucent` (no vertex alpha, its material colour opaque and its texture with no alpha): one pass with no
///   blending and no alpha test, writing depth as `states` says;
/// - translucent, with a first pass's alpha test (`states.alphaTest` GreaterEqual, the GS test of
///   docs/research/rendering.md#shared-state) and depth written: the fragments at or above kGsAlphaRef with depth
///   written, then those below it without, both blended as `states` says; without depth written, one blended pass
///   keeping every fragment;
/// - translucent otherwise: one pass blended as `states` says, keeping the fragments `states.alphaTest` keeps against
///   librw's alpha reference after its start (kStartAlphaRef).
///
/// A `layer` (a dual layer, docs/research/rendering.md#dual) is drawn with the alpha test off and no GS test, as
/// platform::drawLitAtomic() draws it.
[[nodiscard]] std::vector<SceneDrawPass> drawPassesOf(const SceneStates& states, bool translucent, bool layer);

/// The texture-level choice the scene's fragment shader makes for a texture (docs/research/rendering.md#world): the
/// GS's K, 2^L, whether the level is picked by the distance this draw, and whether the texture is Coney's own (no GS
/// level of detail: sampled as the GPU chooses), as the GL3 renderer hands them to its shader (`u_coneyLod`,
/// platform/texture_lod.cpp).
struct TextureLevels {
    float k = 0.0F;
    float scale = 1.0F;
    bool byDistance = false;
    bool gpuChooses = true;

    friend bool operator==(const TextureLevels&, const TextureLevels&) = default;
};

/// The texture-level choice for `texture` (null for none: the GPU chooses) in a draw that picks levels by distance or
/// not.
[[nodiscard]] TextureLevels textureLevelsOf(const TextureData* texture, bool distanceLevels);

/// The level the scene's fragment shader samples at `depth` metres for `levels`, when it picks one: log2 of the depth
/// (at least 1/1024) times the scale plus K while picked by distance, else 0, and never below 0.
[[nodiscard]] float textureLevelAt(const TextureLevels& levels, float depth);

/// librw's linear fog factor at `depth` in front of `camera`: 1 at or before the fog's start, 0 at or past the far
/// plane, and 1 everywhere when the draw has no fog.
[[nodiscard]] float fogAt(const SceneCamera& camera, bool fog, float depth);

} // namespace coney::graphics
