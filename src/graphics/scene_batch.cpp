// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the GPU renderer's reading of the frame description's scene passes (docs/engine/gpu-renderer.md#scene);
// the original draws its meshes through RenderWare's PS2 pipelines
#include "graphics/scene_batch.h"

namespace coney::graphics {

// Stubs for the test commit: each returns a default.

Matrix4 worldMatrixOf(const Transform3d& /*where*/) { return {}; }

Matrix4 viewMatrixOf(const SceneCamera& /*camera*/) { return {}; }

Matrix4 projectionOf(const SceneCamera& /*camera*/) { return {}; }

Matrix4 multiply(const Matrix4& /*a*/, const Matrix4& /*b*/) { return {}; }

std::array<float, 4> transformPoint(const Matrix4& /*matrix*/, const std::array<float, 3>& /*point*/) { return {}; }

DrawLights lightsFor(std::span<const SceneLight> /*lights*/, const MeshData& /*mesh*/, const Transform3d& /*where*/) {
    return {};
}

DrawColours coloursOf(const MeshData& /*mesh*/, const Material3d& /*material*/) { return {}; }

DrawColours layerColoursOf(const MeshData& /*layer*/, const DrawColours& /*base*/) { return {}; }

bool vertexAlphaOf(const MeshData& /*mesh*/, const SubMesh& /*sub*/) { return false; }

bool hasAlpha(const TextureData& /*texture*/) { return false; }

std::vector<SceneDrawPass> drawPassesOf(const SceneStates& /*states*/, bool /*translucent*/, bool /*layer*/) {
    return {};
}

TextureLevels textureLevelsOf(const TextureData* /*texture*/, bool /*distanceLevels*/) { return {}; }

float textureLevelAt(const TextureLevels& /*levels*/, float /*depth*/) { return 0.0F; }

float fogAt(const SceneCamera& /*camera*/, bool /*fog*/, float /*depth*/) { return 0.0F; }

} // namespace coney::graphics
