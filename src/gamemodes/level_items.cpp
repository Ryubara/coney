// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/level_items.h"

#include <cstdint>

#include "ai/brain.h"
#include "ai/brains.h"
#include "human/human.h"
#include "world_objects/pickups.h"

namespace coney {

namespace {

// A store flag's zone: the low byte of its group (the rest carries the robbed bit and the robbing gang).
constexpr int kZoneMask = 0xff;

} // namespace

std::vector<ai::StoreFlag> LevelItems::stores() const {
    std::vector<ai::StoreFlag> stores;
    for (const world_objects::WorldFlag& flag : m_flags.all()) {
        if (flag.kind == ai::kStoreFlagActivity && flag.enabled) {
            stores.push_back(ai::StoreFlag{.handle = flag.handle,
                                           .position = {flag.position[0], flag.position[1], flag.position[2]},
                                           .zone = flag.kind2 & kZoneMask});
        }
    }
    return stores;
}

std::vector<ai::LooseItem> LevelItems::ownedItems(int zone) const {
    std::vector<ai::LooseItem> items;
    if (zone < 0 || !m_records.zoneEnabled(static_cast<std::uint32_t>(zone))) {
        return items;
    }
    for (const world_objects::SpawnRecord& record : m_records.all()) {
        if (items.size() >= kZoneListMost) {
            break;
        }
        if (record.zone == static_cast<std::uint32_t>(zone) && record.owned && !record.removed && !record.hidden &&
            !m_pickups.inHand(record.handle)) {
            items.push_back(ai::LooseItem{.handle = record.handle,
                                          .position = {record.position[0], record.position[1], record.position[2]}});
        }
    }
    return items;
}

std::optional<anim::Vec3> LevelItems::itemPosition(double handle) const {
    const world_objects::SpawnRecord* record = m_records.find(handle);
    if (record == nullptr || record->removed) {
        return std::nullopt;
    }
    return anim::Vec3{record->position[0], record->position[1], record->position[2]};
}

std::optional<double> LevelItems::holder(double handle) const { return m_pickups.holderOf(handle); }

bool LevelItems::startPickUp(ai::Brain& brain, double handle) {
    const world_objects::SpawnRecord* record = m_records.find(handle);
    if (record == nullptr || record->removed || m_pickups.inHand(handle)) {
        return false;
    }
    const world_objects::ObjectType* type = m_types.find(record->typeName);
    const anim::Vec3 at{record->position[0], record->position[1], record->position[2]};
    human::Human& human = brain.human();
    const int clip = world_objects::pickupClip(type != nullptr ? type->pickupAnim : 0, at.z - human.position().z);
    return human.startPickUp(handle, at, static_cast<std::uint32_t>(clip));
}

void LevelItems::collectTakes(ai::Brains& brains) {
    for (std::size_t i = 0; i < brains.size(); ++i) {
        ai::Brain& brain = brains.at(i);
        if (brain.type() == ai::BrainType::Player) {
            continue;
        }
        const std::optional<double> taken = brain.human().takePickedUp();
        if (!taken) {
            continue;
        }
        if (m_pickups.takeByAi(*taken, brain.handle()) == TakeResult::InHand) {
            brain.human().script().heldObject = *taken;
            brain.human().script().heldObjectName = m_pickups.typeOf(*taken);
        }
    }
}

} // namespace coney
