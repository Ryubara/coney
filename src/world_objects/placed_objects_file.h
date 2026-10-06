// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "world_objects/spawn_records.h"

// A level's placed objects, `<level>_objs.txt`, which InitLevel reads after the level script's main chunk: a count,
// then one line per object with the arguments `ObjSpawn` takes.
// Research: docs/research/objects.md#objs-file

namespace coney::world_objects {

/// One line of the file.
struct PlacedObject {
    std::string name;                          ///< The object type, or a particle emitter's name (`part...`).
    std::array<float, 3> position{};           ///< `{x, y, z}`.
    std::array<float, 4> rotation{0, 0, 0, 1}; ///< `{qx, qy, qz, qw}`.
    std::uint32_t zone = 0;                    ///< The object zone.
    std::uint32_t flags = 0;                   ///< `ObjSpawn`'s flags.
    std::uint32_t tint = 0xFFFFFFFFU;          ///< The tint word, written in hexadecimal.
    std::string flagName;                      ///< The linked flag; empty for `nil`.

    /// Whether the line makes a particle emitter (a name starting `part`) rather than a spawn record.
    [[nodiscard]] bool emitter() const { return name.starts_with("part"); }
};

/// Reads the file's text: the count on the first line, then that many lines `{name {x, y, z}, {qx, qy, qz, qw}, -1,
/// zone, flags, tint, flagName )`. Fails with ErrorCode::Invalid for a missing count or a line that does not read
/// (its number given). **Coney's choice**: the braces, commas and parenthesis are taken as separators, so the
/// spacing between fields does not matter (the original's `sscanf` format at `0x005811f0` is not quoted).
/// @orig 0x00398598 ObjectList_LoadPlaced (unknown)
[[nodiscard]] std::expected<std::vector<PlacedObject>, Error> parsePlacedObjects(std::string_view text);

/// Adds a spawn record to `records` for every line that is not an emitter, each with a handle from `nextHandle`, as
/// `ObjRecord_Add` takes them (no unlockable check). Returns how many were added. **Coney's choice**: the emitters
/// are skipped; Coney makes no particle tasks from the file yet.
std::size_t addPlacedObjects(std::span<const PlacedObject> objects, SpawnRecords& records,
                             const std::function<double()>& nextHandle);

} // namespace coney::world_objects
