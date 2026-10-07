// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "world_objects/spawn_records.h"

namespace coney::world_objects {

/// The molotov's type name.
inline constexpr std::string_view kMolotovType = "dyn_molotv";
/// Ticks (60 a second) from the destroy message to the molotov breaking itself: the rest of its 20-tick update
/// schedule, then 2. **Coney's reading**: the page gives "up to about 22" for a bottle spawned on the same frame, as
/// the level80 intro's are; Coney uses 22 for every bottle.
inline constexpr float kMolotovBreakTicks = 22.0F;
/// How far above the bottle its break makes the `sub_explode` flash, metres.
inline constexpr float kMolotovFlashRise = 0.7F;

/// `BreakObjectsInRadius`' search: the destroy message (0x15) to every object of the object manager within `radius` of
/// `centre`, the object `centreObject` included. What a type does with it is its own: a `dyn_molotv` breaks itself
/// kMolotovBreakTicks later (stepSelfBreaks()); other types Coney models ignore it here (the doors take theirs from
/// world_objects::Doors). Counts the records told. **Coney's choice**: the object manager's objects are the records
/// that are live, plus the centre object.
///
/// Research: docs/research/objects.md#break-objects-in-radius, docs/research/script-types.md#molotov
/// @orig 0x003961d0 World_BreakObjectsInRadius (unknown)
/// @orig 0x00404c48 DynMolotov_OnMessage (unknown)
std::size_t sendDestroyInRadius(SpawnRecords& records, anim::Vec3 centre, float radius, double centreObject);

/// One step of `seconds` of the objects breaking themselves: each molotov whose time has come breaks (its record
/// removed for good) and its flash's position is returned, kMolotovFlashRise above it; the caller makes the
/// `sub_explode` there. The bottle's flame is never lit on this path, so no flame, light, smoke or sound follows.
/// @orig 0x00405600 DynMolotov_Update (unknown)
[[nodiscard]] std::vector<anim::Vec3> stepSelfBreaks(SpawnRecords& records, float seconds);

} // namespace coney::world_objects
