// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/level_pickups.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "core/name_hash.h"
#include "scripting/player_bindings.h"
#include "warriors/inventory.h"

namespace coney {

namespace {

// The message WorldObject_Remove sends an object's handlers (docs/research/objects.md#barriers).
constexpr int kRemovedMessage = 2;

// A record's position as a vector.
anim::Vec3 positionOf(const world_objects::SpawnRecord& record) {
    return anim::Vec3{record.position[0], record.position[1], record.position[2]};
}

// The plan distance from `a` to `b`.
float planDistance(anim::Vec3 a, anim::Vec3 b) { return std::hypot(b.x - a.x, b.y - a.y); }

} // namespace

std::optional<PickupChoice> LevelPickups::search(anim::Vec3 feet, anim::Vec3 facing,
                                                 const world_objects::SightBlocked& blocked,
                                                 std::span<const double> refused) const {
    std::vector<world_objects::PickupCandidate> candidates;
    std::vector<int> pickupAnims;
    for (const world_objects::SpawnRecord& record : m_records.all()) {
        if (record.removed || record.hidden || m_inHand.contains(record.handle) ||
            !m_records.zoneEnabled(record.zone) || std::ranges::find(refused, record.handle) != refused.end()) {
            continue;
        }
        const world_objects::ObjectType* type = m_types.find(record.typeName);
        candidates.push_back(world_objects::PickupCandidate{
            .handle = record.handle,
            .position = positionOf(record),
            .pickable = type != nullptr && world_objects::pickable(type->className, type->modelHash) &&
                        !(m_spent && m_spent(record.handle))});
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
    std::vector<double> refused;
    for (const double object : gathered) {
        if (interact(object, human)) {
            return TriangleOutcome{.result = TriangleResult::Consumed, .choice = {}};
        }
        // A class's own answer: a cash register asks to be lifted (message 0x14), or refuses once broken.
        const NativeUse use = m_native ? m_native(object, human) : NativeUse::Ignore;
        const world_objects::SpawnRecord* record = m_records.find(object);
        const world_objects::ObjectType* type = record != nullptr ? m_types.find(record->typeName) : nullptr;
        if (use == NativeUse::Lift && !holding && record != nullptr && type != nullptr) {
            const anim::Vec3 at = positionOf(*record);
            return TriangleOutcome{
                .result = TriangleResult::PickUp,
                .choice = PickupChoice{.handle = object,
                                       .position = at,
                                       .clip = world_objects::pickupClip(type->pickupAnim, at.z - feet.z)}};
        }
        if (use != NativeUse::Ignore) {
            refused.push_back(object);
        }
    }
    // Then the search's choice: one needing empty hands is not taken with something held.
    if (const std::optional<PickupChoice> choice = search(feet, facing, blocked, refused)) {
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
        giveLoot(player, *type);
        pickupSound(player, item::kStolenLoot);
        m_records.destroy(handle);
        return TakeResult::Loot;
    }
    // Into the hand: the record stays, pinned as the search pinned the winner.
    record->pinned = true;
    m_inHand.insert(handle);
    return TakeResult::InHand;
}

void LevelPickups::giveLoot(int player, const world_objects::ObjectType& type) {
    // Item 10 with notify, then the type's value in money without.
    script::addInventoryItem(m_scripts, m_state, player, item::kStolenLoot, 1, true);
    const auto money = static_cast<int>(std::lround(static_cast<float>(type.value) * kLootMoneyFactor));
    script::addInventoryItem(m_scripts, m_state, player, item::kMoney, money, false);
}

std::vector<LevelPickups::WalkedOver> LevelPickups::walkOver(int player, anim::Vec3 feet, bool fullHealth,
                                                             const world_objects::SightBlocked& blocked) {
    const Inventory& inventory = m_state.player.inventory;
    // Whether the player has room for one more of `id`, at most `most`.
    const auto room = [&inventory, player](int id, int most) {
        return inventory.count(player, id) < std::min(inventory.limit(id), most);
    };
    // The power-ups he touches (Human_OnContact passes through them) and what each would give.
    std::vector<std::pair<WalkedOver, const world_objects::ObjectType*>> touched;
    for (const world_objects::SpawnRecord& record : m_records.all()) {
        const anim::Vec3 at = positionOf(record);
        if (record.removed || record.hidden || m_inHand.contains(record.handle) ||
            !m_records.zoneEnabled(record.zone) || planDistance(feet, at) > kWalkOverReach ||
            at.z < feet.z - kWalkOverBelow || at.z > feet.z + kWalkOverAbove) {
            continue;
        }
        const world_objects::ObjectType* type = m_types.find(record.typeName);
        if (type == nullptr || type->className != world_objects::kPowerupItemClass) {
            continue;
        }
        // A flash is passed by at full health while the switch is off; anything out of sight is not touched.
        if (type->objectKind == world_objects::kObjectKindRevival && fullHealth && !m_state.hub.powerupPickup) {
            continue;
        }
        if (!world_objects::inSight(feet, at, blocked)) {
            continue;
        }
        // Human_PickUpObject by the type: a refused one stays.
        WalkedOver gift{.handle = record.handle};
        switch (type->objectKind) {
        case world_objects::kObjectKindMoney:
            gift.item = item::kMoney;
            gift.amount = static_cast<int>(record.money);
            break;
        case world_objects::kObjectKindKey:
            gift.item = item::kHandcuffKey;
            gift.amount = room(item::kHandcuffKey, Inventory::kNoLimit) ? 1 : 0;
            break;
        case world_objects::kObjectKindRevival:
            gift.item = item::kRevive;
            gift.amount = room(item::kRevive, Inventory::kNoLimit) ? 1 : 0;
            break;
        case world_objects::kObjectKindSpraycan:
            gift.item = item::kSprayPaint;
            gift.amount = room(item::kSprayPaint, kSprayPaintMost) ? 1 : 0;
            break;
        case world_objects::kObjectKindSpecial:
            gift.item = item::kStolenLoot;
            gift.amount = 1;
            break;
        default:
            break;
        }
        if (gift.amount > 0 || (gift.item == item::kMoney && type->objectKind == world_objects::kObjectKindMoney)) {
            touched.emplace_back(gift, type);
        }
    }
    // Each gift notifies and plays its item's pick-up sound, and the object goes for good.
    std::vector<WalkedOver> taken;
    for (const auto& [gift, type] : touched) {
        if (type->objectKind == world_objects::kObjectKindSpecial) {
            giveLoot(player, *type);
        } else {
            script::addInventoryItem(m_scripts, m_state, player, gift.item, gift.amount, true);
        }
        pickupSound(player, gift.item);
        m_records.destroy(gift.handle);
        taken.push_back(gift);
    }
    return taken;
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
            best =
                ActionObject{.handle = object, .prompt = prompt, .hint = std::string(m_messages->promptHint(object))};
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

void LevelPickups::objectRemoved(double object) {
    if (m_messages != nullptr) {
        static_cast<void>(m_messages->deliver(m_scripts, object, kRemovedMessage, object, 0.0, 0.0));
    }
    static_cast<void>(m_records.destroy(object));
}

int LevelPickups::carried(int player, int item) const { return m_state.player.inventory.count(player, item); }

int LevelPickups::itemLimit(int item) const { return m_state.player.inventory.limit(item); }

int LevelPickups::score(int player) const { return static_cast<int>(m_state.player.stats.score(player)); }

void LevelPickups::spendItem(int player, int item) { m_state.player.inventory.give(player, item, -1); }

void LevelPickups::dealerSold(int player, int item, int amount, int price) {
    script::addInventoryItem(m_scripts, m_state, player, item, amount, false);
    if (amount > 0) {
        pickupSound(player, item);
    }
    script::addInventoryItem(m_scripts, m_state, player, item::kMoney, -price, false);
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

void LevelPickups::mugPaid(int player, int& victimMoney) {
    // All the victim's money goes to the player in one add.
    if (victimMoney > 0) {
        script::addInventoryItem(m_scripts, m_state, player, item::kMoney, victimMoney, true);
        victimMoney = 0;
        pickupSound(player, item::kMoney);
    }
}

void LevelPickups::pickupSound(int player, int item) const {
    const InventoryItem* slot = m_state.player.inventory.slot(player, item);
    if (!m_play2D || slot == nullptr || slot->sound.empty() || slot->sound == "none") {
        return;
    }
    m_play2D(crc32(slot->sound));
}

void LevelPickups::mugEnded(double mugger, const std::string& callback, bool success) {
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
