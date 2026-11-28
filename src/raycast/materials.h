// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string_view>

// The names the game gives its collision materials: a reference list of names only, by `MATERIAL_*` id
// (docs/research/physics-world.md#u16-names). The same ids are the collision triangles' material byte and the sound
// matrix's material side.

namespace coney::raycast {

/// The number of material ids the game names, 0 to 190.
inline constexpr std::size_t kMaterialCount = 191;

/// The name the game gives material `id`: `undefined` for 0, `MATERIAL_NONE` for 1, `MATERIAL_` and a word for 2 to
/// 190, and `unknown` for any other id, negative ones included (the original compares unsigned).
/// @orig 0x0034fc08 CollisionMaterial_GetName (unknown)
[[nodiscard]] std::string_view materialName(int id);

} // namespace coney::raycast
