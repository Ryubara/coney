// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/mesh_data.h"

namespace coney::graphics {

// Stubs so the tests compile and fail first; the next commit implements them.

std::expected<void, Error> validateMesh(const MeshData&) { return {}; }

std::uint64_t hashMesh(const MeshData&) { return 0; }

} // namespace coney::graphics
