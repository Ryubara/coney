// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/level_pickups.h"

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

#include "scripting/player_bindings.h"
#include "warriors/inventory.h"

namespace coney {

namespace {

// A record's position as a vector.
anim::Vec3 positionOf(const world_objects::SpawnRecord& record) {
    return anim::Vec3{record.position[0], record.position[1], record.position[2]};
}

// The plan distance from `a` to `b`.
float planDistance(anim::Vec3 a, anim::Vec3 b) { return std::hypot(b.x - a.x, b.y - a.y); }

} // namespace

std::optional<PickupChoice> LevelPickups::search(anim::Vec3 feet, anim::Vec3 facing,
                                                 const world_objects::SightBlocked& blocked) const {
    std::vector<world_objects::PickupCandidate> candidates;
    std::vector<int> pickupAnims;
    for (const world_objects::SpawnRecord& record : m_records.all()) {
        if (record.removed || record.hidden || m_inHand.contains(record.handle) ||
            !m_records.zoneEnabled(record.zone)) {
            continue;
        }
        const world_objects::ObjectType* type = m_types.find(record.typeName);
        candidates.push_back(world_objects::PickupCandidate{
            .handle = record.handle,
            .position = positionOf(record),
            .pickable = type != nullptr && world_objects::pickable(type->className, type->modelHash)});
        pickupAnims.push_back(type != nullptr ? type->pickupAnim : 0);
    }
    const std::optional<std::size_t> found = world_objects::searchPickup(feet, facing, candidates, blocked);
    if (!found) {
        return std::nullopt;
    }
    const world_objects::PickupCandidate& chosen = candidates[*found];
    return PickupChoice{.handle = chosen.handle,
                        .position = chosen.position,
                        .clip = world_objects::pickupClip(pickupAnims[*found], chosen.position.z - feet.z)};
}

TriangleOutcome LevelPickups::triangle(double human, anim::Vec3 feet, anim::Vec3 facing, bool holding,
                                       const world_objects::SightBlocked& blocked) {
    // Step 4: the current context record's object, which may take the press.
    if (const std::optional<ActionObject> object = actionObject(feet); object && interact(object->handle, human)) {
        return TriangleOutcome{.result = TriangleResult::Consumed, .choice = {}};
    }
    // Step 5: every object the search gathers hears message 0 first, before its filters.
    std::vector<double> gathered;
    for (const world_objects::SpawnRecord& record : m_records.all()) {
        if (!record.removed && !m_inHand.contains(record.handle) &&
            planDistance(feet, positionOf(record)) <= world_objects::kPickupReach) {
            gathered.push_back(record.handle);
        }
    }
    for (const double object : gathered) {
        if (interact(object, human)) {
            return TriangleOutcome{.result = TriangleResult::Consumed, .choice = {}};
        }
    }
    // Then the search's choice: one needing empty hands is not taken with something held.
    if (const std::optional<PickupChoice> choice = search(feet, facing, blocked)) {
        const world_objects::SpawnRecord* record = m_records.find(choice->handle);
        const world_objects::ObjectType* type = record != nullptr ? m_types.find(record->typeName) : nullptr;
        const int kind = type != nullptr ? type->objectKind : 0;
        if (!holding || kind == world_objects::kObjectKindSpecial || kind == kKindAnyHands) {
            return TriangleOutcome{.result = TriangleResult::PickUp, .choice = *choice};
        }
    }
    return TriangleOutcome{.result = holding ? TriangleResult::Drop : TriangleResult::Nothing, .choice = {}};
}

TakeResult LevelPickups::take(double handle, int player) {
    world_objects::SpawnRecord* record = m_records.find(handle);
    if (record == nullptr || record->removed || m_inHand.contains(handle)) {
        return TakeResult::Gone;
    }
    const world_objects::ObjectType* type = m_types.find(record->typeName);
    if (type != nullptr && type->objectKind == world_objects::kObjectKindSpecial) {
        script::addInventoryItem(m_scripts, m_state, player, item::kStolenLoot, 1, true);
        const auto money = static_cast<int>(std::lround(static_cast<float>(type->value) * kLootMoneyFactor));
        script::addInventoryItem(m_scripts, m_state, player, item::kMoney, money, false);
        m_records.destroy(handle);
        return TakeResult::Loot;
    }
    // Into the hand: the record stays, pinned as the search pinned the winner.
    record->pinned = true;
    m_inHand.insert(handle);
    return TakeResult::InHand;
}

void LevelPickups::drop(double handle, anim::Vec3 at) {
    if (m_inHand.erase(handle) == 0) {
        return;
    }
    if (world_objects::SpawnRecord* record = m_records.find(handle)) {
        record->pinned = false;
        record->position = {at.x, at.y, at.z};
    }
}

std::string LevelPickups::typeOf(double handle) const {
    const world_objects::SpawnRecord* record = m_records.find(handle);
    return record != nullptr ? record->typeName : std::string();
}

int LevelPickups::animSetOf(std::string_view typeName) const {
    const world_objects::ObjectType* type = typeName.empty() ? nullptr : m_types.find(typeName);
    return type != nullptr ? type->animSet : 0;
}

void LevelPickups::placeObject(double handle, anim::Vec3 position, anim::Quat rotation) {
    if (world_objects::SpawnRecord* record = m_records.find(handle); record != nullptr && !m_inHand.contains(handle)) {
        record->position = {position.x, position.y, position.z};
        record->rotation = {rotation.x, rotation.y, rotation.z, rotation.w};
    }
}

std::optional<ActionObject> LevelPickups::actionObject(anim::Vec3 feet) const {
    if (m_messages == nullptr) {
        return std::nullopt;
    }
    std::optional<ActionObject> best;
    float bestDistance = kPromptReach;
    for (const auto& [object, prompt] : m_messages->prompts()) {
        const std::optional<anim::Vec3> at = promptPosition(object);
        if (!at) {
            continue;
        }
        const float distance = planDistance(feet, *at);
        if (distance <= bestDistance && std::fabs(at->z - (feet.z + 1.0F)) <= kPromptHeight) {
            best = ActionObject{.handle = object, .prompt = prompt};
            bestDistance = distance;
        }
    }
    return best;
}

std::optional<anim::Vec3> LevelPickups::promptPosition(double object) const {
    if (const world_objects::SpawnRecord* record = m_records.find(object)) {
        if (record->removed || m_inHand.contains(object)) {
            return std::nullopt;
        }
        return positionOf(*record);
    }
    return m_locate ? m_locate(object) : std::nullopt;
}

void LevelPickups::stereoStolen(int player, double human, double car) {
    script::addInventoryItem(m_scripts, m_state, player, item::kMoney, kStereoMoney, true);
    script::addInventoryItem(m_scripts, m_state, player, item::kCarStereo, 1, true);
    if (!m_state.player.stereoTheftHandler.empty()) {
        const std::array<script::Value, 2> args{script::Value(human), script::Value(car)};
        static_cast<void>(m_scripts.call(m_state.player.stereoTheftHandler, args));
    }
}

std::optional<combat::MuggingParams> LevelPickups::muggingOverride() const {
    const InterrogateOverride& set = m_state.characters.interrogate.front();
    if (!set.active()) {
        return std::nullopt;
    }
    constexpr float kDegrees = 180.0F / std::numbers::pi_v<float>;
    return combat::MuggingParams{.requiredMs = static_cast<int>(set.timesMs[0]),
                                 .periodMs = static_cast<int>(set.timesMs[1]),
                                 .offTargetMs = static_cast<int>(set.timesMs[2]),
                                 .toleranceDegrees = set.anglesRadians[0] * kDegrees,
                                 .gapDegrees = set.anglesRadians[1] * kDegrees};
}

void LevelPickups::mugEnded(int player, double mugger, const std::string& callback, int& victimMoney, bool success) {
    // The victim's money goes to the player.
    if (success && victimMoney > 0) {
        script::addInventoryItem(m_scripts, m_state, player, item::kMoney, victimMoney, true);
        victimMoney = 0;
    }
    // Every end calls the mugger's callback with the mugger and the success (Lua 4: true is 1, false nil).
    if (!callback.empty()) {
        const std::array<script::Value, 2> args{script::Value(mugger), success ? script::Value(1.0) : script::Value()};
        static_cast<void>(m_scripts.call(callback, args));
    }
}

bool LevelPickups::interact(double object, double human) {
    return m_messages != nullptr && m_messages->deliver(m_scripts, object, 0, human, 0.0, 0.0);
}

} // namespace coney
