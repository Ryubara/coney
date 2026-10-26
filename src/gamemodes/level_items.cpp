// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/level_items.h"

#include <algorithm>
#include <cstdint>

#include "ai/brain.h"
#include "ai/brains.h"
#include "animation/anim_math.h"
#include "human/human.h"
#include "world_objects/glass.h"
#include "world_objects/pickups.h"
#include "world_objects/props.h"

namespace coney {

namespace {

// A store flag's zone: the low byte of its group (the rest carries the robbed bit and the robbing gang).
constexpr int kZoneMask = 0xff;
// The kinds Object_IsVandalisable refuses: TYPE_BREAKANDENTER_DOOR, TYPE_EXPLOSIVE, TYPE_FIREBARREL.
constexpr int kKindBreakAndEnterDoor = 30;
constexpr int kKindExplosive = 40;
constexpr int kKindFireBarrel = 42;
// The anim sets of carried and thrown objects, and the one held objects of kind 6 swing with.
constexpr int kCarriedSet = 4;
constexpr int kThrownSet = 5;
constexpr int kSwungSet = 6;
// A type's word +0x5e bit that lets a held breaking object strike it where it stands.
constexpr std::uint16_t kHeldBreaksBit = 4;

// The squared 3D distance.
// @coney-only arithmetic helper
float distanceSquared(anim::Vec3 a, anim::Vec3 b) {
    const anim::Vec3 d = anim::subtract(a, b);
    return anim::dot(d, d);
}

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

bool LevelItems::smashable(const world_objects::ObjectType& type) {
    // A type worth nothing is left alone, but for the masks.
    if (type.value < 1 && type.className != world_objects::kDynMasksClass) {
        return false;
    }
    if (type.objectKind == kKindBreakAndEnterDoor || type.objectKind == kKindExplosive ||
        type.objectKind == kKindFireBarrel) {
        return false;
    }
    return type.animSet == kCarriedSet || type.animSet == kThrownSet ||
           type.className == world_objects::kDynMasksClass || type.className == world_objects::kOverheadWeaponClass ||
           type.className == world_objects::kCashRegisterClass;
}

ai::SmashTarget LevelItems::smashTargetOf(double handle, anim::Vec3 at, const world_objects::ObjectType& type) {
    return ai::SmashTarget{
        .handle = handle, .position = at, .animSet = type.animSet, .heldBreaks = (type.bodyWord & kHeldBreaksBit) != 0};
}

std::vector<ai::SmashTarget> LevelItems::smashTargets(anim::Vec3 centre, float radius) const {
    std::vector<ai::SmashTarget> targets;
    for (const world_objects::SpawnRecord& record : m_records.all()) {
        // In a hand, or in flight (the object flag 0x4000000, bit 26), it is no target.
        if (!record.live || record.removed || record.hidden || m_pickups.inHand(record.handle) ||
            m_pickups.airborne(record.handle) || broken(record.handle)) {
            continue;
        }
        const world_objects::ObjectType* type = m_types.find(record.typeName);
        const anim::Vec3 at{record.position[0], record.position[1], record.position[2]};
        if (type == nullptr || !smashable(*type) || distanceSquared(at, centre) > radius * radius) {
            continue;
        }
        targets.push_back(smashTargetOf(record.handle, at, *type));
    }
    std::ranges::sort(targets, {},
                      [centre](const ai::SmashTarget& target) { return distanceSquared(target.position, centre); });
    // Coney's choice: the cap applies after the smash filter, the original's to the raw list before it.
    if (targets.size() > kSmashListMost) {
        targets.resize(kSmashListMost);
    }
    return targets;
}

std::vector<ai::PaneView> LevelItems::panes(anim::Vec3 centre, float radius) const {
    std::vector<ai::PaneView> views;
    if (m_objects == nullptr) {
        return views;
    }
    for (const world_objects::GlassPane& pane : m_objects->glass.panes()) {
        // Coney's choice: "has its body" (vtable +0xf4) is read as not hidden.
        if (pane.broken || pane.hidden || distanceSquared(pane.centre, centre) > radius * radius) {
            continue;
        }
        views.push_back(ai::PaneView{.handle = pane.handle,
                                     .centre = pane.centre,
                                     .normal = pane.normal,
                                     .height = pane.height,
                                     .alarmed = (pane.alarmBits & 3U) == 3U});
    }
    std::ranges::sort(views, {}, [centre](const ai::PaneView& view) { return distanceSquared(view.centre, centre); });
    // Coney's choice: the cap applies after the broken and hidden filter (the list's own filter is not studied).
    if (views.size() > kPaneListMost) {
        views.resize(kPaneListMost);
    }
    return views;
}

bool LevelItems::broken(double handle) const {
    if (m_objects != nullptr) {
        if (const world_objects::GlassPane* pane = m_objects->glass.find(handle); pane != nullptr) {
            return pane->broken;
        }
        if (const auto counters = m_objects->props.counters(handle); counters) {
            if (counters->first == 0 || counters->second == 0) {
                return true;
            }
        }
    }
    const world_objects::SpawnRecord* record = m_records.find(handle);
    return record == nullptr || record->removed;
}

bool LevelItems::inFlight(double handle) const { return m_pickups.airborne(handle); }

std::optional<anim::Vec3> LevelItems::objectPosition(double handle) const {
    if (m_objects != nullptr) {
        if (const world_objects::GlassPane* pane = m_objects->glass.find(handle); pane != nullptr) {
            return pane->centre;
        }
    }
    return itemPosition(handle);
}

ai::HeldUse LevelItems::heldUse(double handle) const {
    // Coney's choice: a held object with no spawn record counts as Other, where the original goes on as if
    // empty-handed for one that is no world object.
    const world_objects::SpawnRecord* record = m_records.find(handle);
    const world_objects::ObjectType* type = record != nullptr ? m_types.find(record->typeName) : nullptr;
    if (type != nullptr && (type->animSet == kCarriedSet || type->animSet == kThrownSet || type->animSet == kSwungSet ||
                            type->objectKind == world_objects::kObjectKindKnife)) {
        return ai::HeldUse::Swings;
    }
    return ai::HeldUse::Other;
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
