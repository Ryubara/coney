// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/profile_store.h"

namespace coney {

std::size_t ProfileStore::count() const {
    std::size_t used = 0;
    for (std::size_t slot = 0; slot < kSlots; ++slot) {
        used += profile(slot) != nullptr ? 1 : 0;
    }
    return used;
}

std::optional<std::size_t> ProfileStore::freeSlot() const {
    for (std::size_t slot = 0; slot < kSlots; ++slot) {
        if (profile(slot) == nullptr) {
            return slot;
        }
    }
    return std::nullopt;
}

bool ProfileStore::nameUsed(std::string_view name) const {
    for (std::size_t slot = 0; slot < kSlots; ++slot) {
        if (const Profile* used = profile(slot); used != nullptr && used->name == name) {
            return true;
        }
    }
    return false;
}

const Profile* SessionProfileStore::profile(std::size_t slot) const {
    if (slot >= m_slots.size()) {
        return nullptr;
    }
    const std::optional<Profile>& held = m_slots.at(slot);
    return held.has_value() ? &held.value() : nullptr;
}

bool SessionProfileStore::create(std::size_t slot, const Profile& profile) {
    if (slot >= m_slots.size()) {
        return false;
    }
    m_slots.at(slot) = profile;
    m_loaded = slot;
    return true;
}

bool SessionProfileStore::load(std::size_t slot) {
    if (profile(slot) == nullptr) {
        return false;
    }
    m_loaded = slot;
    return true;
}

void SessionProfileStore::remove(std::size_t slot) {
    if (slot < m_slots.size()) {
        m_slots.at(slot).reset();
        if (m_loaded == slot) {
            m_loaded.reset();
        }
    }
}

} // namespace coney
