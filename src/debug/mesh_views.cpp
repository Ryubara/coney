// SPDX-License-Identifier: GPL-3.0-or-later
// Stubs for the tests' commit: each returns a default or empty result; the next commit implements them.
#include "debug/mesh_views.h"

namespace coney::debug {

bool meshViewsOn(const MeshViewOptions& /*options*/) { return false; }

bool meshPassesOn(const MeshViewOptions& /*options*/) { return false; }

bool inMeshScope(MeshScope /*scope*/, MeshKind /*kind*/, bool /*selected*/) { return false; }

MeshDrawPlan meshPlanFor(const MeshViewOptions& /*options*/, MeshKind /*kind*/, bool /*selected*/) { return {}; }

graphics::Rgba meshWireColour(MeshKind /*kind*/, bool /*selected*/) { return {}; }

graphics::Rgba meshPaletteColour(std::uint32_t /*key*/) { return {}; }

std::uint32_t meshMaterialKey(std::string_view /*texture*/, std::size_t /*index*/) { return 0; }

graphics::Rgba meshLodColour(float /*fade*/) { return {}; }

anim::Mat34 characterPlacement(anim::Vec3 /*feet*/, float /*heading*/, float /*lean*/) { return {}; }

std::size_t addNormals(DebugLines& /*lines*/, std::span<const anim::Vec3> /*positions*/,
                       std::span<const anim::Vec3> /*normals*/, const anim::Mat34& /*placement*/, float /*length*/) {
    return 0;
}

std::size_t addBoneAxes(DebugLines& /*lines*/, std::span<const anim::Mat34> /*bones*/, std::span<const int> /*parents*/,
                        const anim::Mat34& /*placement*/, float /*length*/) {
    return 0;
}

std::expected<MeshViewSpec, Error> parseMeshViewSpec(std::string_view /*text*/) { return MeshViewSpec{}; }

std::string sceneCounterLine(std::span<const NamedCount> /*counters*/) { return {}; }

} // namespace coney::debug
