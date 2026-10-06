// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/inventory.h"

#include <algorithm>

namespace coney {

bool Inventory::valid(int player, int id) { return player >= 0 && player < kPlayers && id >= 0 && id < kItems; }

int Inventory::limit(int id) const {
    if (id == item::kRevive) {
        return m_reviveUpgrade ? kRevivesUpgraded : kRevives;
    }
    return kNoLimit;
}

int Inventory::clamp(int id, long long value) const {
    return static_cast<int>(std::clamp<long long>(value, 0, limit(id)));
}

void Inventory::configure(int id, const std::string& objectName, int count, const std::string& sound, int durationMs) {
    if (!valid(0, id)) {
        return;
    }
    for (auto& player : m_items) {
        InventoryItem& slot = player.at(static_cast<std::size_t>(id));
        slot.objectName = objectName;
        slot.count = count;
        slot.sound = sound;
        slot.durationMs = durationMs;
    }
}

int Inventory::count(int player, int id) const {
    const InventoryItem* item = slot(player, id);
    return item != nullptr ? item->count : 0;
}

int Inventory::give(int player, int id, int amount) {
    if (!valid(player, id)) {
        return 0;
    }
    InventoryItem& slot = m_items.at(static_cast<std::size_t>(player)).at(static_cast<std::size_t>(id));
    const int before = slot.count;
    slot.count = clamp(id, static_cast<long long>(before) + amount);
    return slot.count - before;
}

void Inventory::set(int player, int id, int value) {
    if (!valid(player, id)) {
        return;
    }
    m_items.at(static_cast<std::size_t>(player)).at(static_cast<std::size_t>(id)).count = clamp(id, value);
}

void Inventory::setMoney(int player, int value) {
    if (!valid(player, item::kMoney)) {
        return;
    }
    set(player, item::kMoney, value);
    ++m_moneySets.at(static_cast<std::size_t>(player));
}

std::uint32_t Inventory::moneySets(int player) const {
    return player >= 0 && player < kPlayers ? m_moneySets.at(static_cast<std::size_t>(player)) : 0;
}

void Inventory::setSprayPaint(int player, int value) {
    if (!valid(player, item::kSprayPaint)) {
        return;
    }
    set(player, item::kSprayPaint, value);
    // The hint flag is raised only by the first positive amount of the game state's life.
    if (value > 0 && !m_sprayHintGiven) {
        m_sprayHintGiven = true;
        m_sprayHintPending = true;
    }
}

const InventoryItem* Inventory::slot(int player, int id) const {
    if (!valid(player, id)) {
        return nullptr;
    }
    return &m_items.at(static_cast<std::size_t>(player)).at(static_cast<std::size_t>(id));
}

void Inventory::clearCounts() {
    for (auto& player : m_items) {
        for (InventoryItem& slot : player) {
            slot.count = 0;
        }
    }
}

} // namespace coney
