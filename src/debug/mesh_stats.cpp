// SPDX-License-Identifier: GPL-3.0-or-later
// Stubs for the tests' commit: each returns an empty result; the next commit implements them.
#include "debug/mesh_stats.h"

namespace coney::debug {

MeshStats characterMeshStats(const characters::CharacterModel& /*model*/, const TextureInfo& /*texture*/) { return {}; }

std::size_t drawCallsOf(const MeshStats& /*stats*/, const MeshDrawn& /*drawn*/) { return 0; }

double meanWeights(const MeshStats& /*stats*/) { return 0.0; }

WatchGroup describeMesh(const MeshStats& /*stats*/, const MeshDrawn& /*drawn*/) { return {}; }

} // namespace coney::debug
