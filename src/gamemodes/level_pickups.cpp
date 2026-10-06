// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/level_pickups.h"

#include <cmath>

#include "scripting/player_bindings.h"
#include "warriors/inventory.h"

namespace coney {

std::optional<PickupChoice> LevelPickups::search(anim::Vec3 feet, anim::Vec3 facing,
                                                 const world_objects::SightBlocked& blocked) const {
    std::vector<world_objects::PickupCandidate> candidates;
    for (const world_objects::SpawnRecord& record : m_records.all()) {
        if (record.removed || record.hidden || !m_records.zoneEnabled(record.zone)) {
            continue;
        }
        const world_objects::ObjectType* type = m_types.find(record.typeName);
        candidates.push_back(world_objects::PickupCandidate{
            .handle = record.handle,
            .position = anim::Vec3{record.position[0], record.position[1], record.position[2]},
            .pickable = type != nullptr && world_objects::pickableClass(type->className)});
    }
    const std::optional<std::size_t> found = world_objects::searchPickup(feet, facing, candidates, blocked);
    if (!found) {
        return std::nullopt;
    }
    const world_objects::PickupCandidate& chosen = candidates[*found];
    return PickupChoice{.handle = chosen.handle,
                        .position = chosen.position,
                        .clip = world_objects::pickupClip(chosen.position.z - feet.z)};
}

bool LevelPickups::take(double handle, int player) {
    const world_objects::SpawnRecord* record = m_records.find(handle);
    if (record == nullptr || record->removed) {
        return false;
    }
    if (const world_objects::ObjectType* type = m_types.find(record->typeName);
        type != nullptr && type->objectKind == world_objects::kObjectKindSpecial) {
        script::addInventoryItem(m_scripts, m_state, player, item::kStolenLoot, 1, true);
        const auto money = static_cast<int>(std::lround(static_cast<float>(type->value) * kLootMoneyFactor));
        script::addInventoryItem(m_scripts, m_state, player, item::kMoney, money, false);
    }
    m_records.destroy(handle);
    return true;
}

} // namespace coney
