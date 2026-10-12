// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include "debug/entity_inspect.h"
#include "world_objects/doors.h"
#include "world_objects/glass.h"
#include "world_objects/spawn_records.h"

// The Entity inspector's view of the dynamic objects (docs/guides/debug-menu.md#entity-inspector): a door or barrier,
// a glass pane and a spawned world object, as watch lines in groups. Read through their public fields only, so
// src/world_objects holds no debug code.

namespace coney::debug {

/// Everything known about `door` (entity `id`): Identity, Transform and Object.
[[nodiscard]] std::vector<WatchGroup> describeDoor(const world_objects::Door& door, EntityId id);

/// Everything known about `pane` (entity `id`): Identity, Transform and Object.
[[nodiscard]] std::vector<WatchGroup> describePane(const world_objects::GlassPane& pane, EntityId id);

/// Everything known about the world object of spawn record `record` (entity `id`): Identity, Transform and Object.
[[nodiscard]] std::vector<WatchGroup> describeSpawnRecord(const world_objects::SpawnRecord& record, EntityId id);

} // namespace coney::debug
