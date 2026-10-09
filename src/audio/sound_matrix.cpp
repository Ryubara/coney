// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/sound_matrix.h"

#include <algorithm>

namespace coney::audio {

namespace {

// A second material of 0 or 1 (none) stands for the caller's default.
constexpr std::uint32_t kNoMaterial = 1;

} // namespace

SoundMatrix::SoundMatrix() : m_name("sound"), m_materials(std::size_t{kMaterials} * kMaterials, nullptr) {}

bool SoundMatrix::load(std::string_view name) {
    if (name == m_name) {
        return false;
    }
    clear();
    m_name = name;
    return true;
}

void SoundMatrix::clear() {
    std::ranges::fill(m_materials, nullptr);
    m_anims.fill(nullptr);
    m_entries.clear();
}

SoundMatrix::Entry* SoundMatrix::makeEntry(std::uint32_t count, std::uint32_t columns,
                                           const std::array<float, 3>& volumes) {
    Entry& entry = m_entries.emplace_back();
    entry.used = count;
    entry.columns = std::clamp(columns, 1U, kColumns);
    entry.alternatives.assign(count, std::array<std::uint32_t, 3>{});
    entry.volumes = volumes;
    return &entry;
}

SoundMatrix::Entry** SoundMatrix::cell(std::uint32_t m1, std::uint32_t m2) {
    if (m1 >= kMaterials || m2 >= kMaterials) {
        return nullptr;
    }
    return &m_materials[(std::size_t{m1} * kMaterials) + m2];
}

void SoundMatrix::newMaterialSlots(std::uint32_t m1, std::uint32_t m2, std::uint32_t count, std::uint32_t columns,
                                   const std::array<float, 3>& volumes) {
    if (Entry** at = cell(m1, m2); at != nullptr) {
        *at = makeEntry(count, columns, volumes);
    }
}

void SoundMatrix::setSounds(Entry* entry, std::uint32_t index,
                            const std::array<std::optional<std::uint32_t>, 3>& sounds) {
    if (entry == nullptr || index >= entry->alternatives.size()) {
        return;
    }
    // Only the columns the entry was made with hold sounds.
    for (std::uint32_t column = 0; column < entry->columns; ++column) {
        if (const auto& sound = sounds.at(column)) {
            entry->alternatives[index].at(column) = *sound;
        }
    }
}

void SoundMatrix::newMaterialSound(std::uint32_t index, std::uint32_t m1, std::uint32_t m2,
                                   const std::array<std::optional<std::uint32_t>, 3>& sounds) {
    if (Entry** at = cell(m1, m2); at != nullptr) {
        setSounds(*at, index, sounds);
    }
}

void SoundMatrix::setMaterialSlotCount(std::uint32_t m1, std::uint32_t m2, std::uint32_t count) {
    if (Entry** at = cell(m1, m2); at != nullptr && *at != nullptr) {
        Entry& entry = **at;
        entry.used = std::min(count, static_cast<std::uint32_t>(entry.alternatives.size()));
        entry.turn = 0;
    }
}

void SoundMatrix::duplicateMaterials(std::uint32_t a, std::uint32_t b) {
    Entry** from = cell(a, b);
    Entry** to = cell(b, a);
    if (a != b && from != nullptr && to != nullptr) {
        *to = *from;
    }
}

void SoundMatrix::newAnimSlots(std::uint32_t event, std::uint32_t count, std::uint32_t columns,
                               const std::array<float, 3>& volumes) {
    if (event < kAnimSounds) {
        m_anims.at(event) = makeEntry(count, columns, volumes);
    }
}

void SoundMatrix::newAnimSound(std::uint32_t index, std::uint32_t event,
                               const std::array<std::optional<std::uint32_t>, 3>& sounds) {
    if (event < kAnimSounds) {
        setSounds(m_anims.at(event), index, sounds);
    }
}

std::optional<MatrixSounds> SoundMatrix::next(Entry* entry) {
    if (entry == nullptr || entry->used == 0) {
        return std::nullopt;
    }
    if (entry->turn >= entry->used) {
        entry->turn = 0;
    }
    MatrixSounds out{.sounds = entry->alternatives[entry->turn], .volumes = entry->volumes};
    entry->turn = (entry->turn + 1) % entry->used;
    return out;
}

std::optional<MatrixSounds> SoundMatrix::nextMaterialSounds(std::uint32_t m1, std::uint32_t m2,
                                                            std::uint32_t fallback) {
    if (m2 <= kNoMaterial) {
        m2 = fallback;
    }
    Entry** at = cell(m1, m2);
    Entry* entry = at != nullptr ? *at : nullptr;
    // An empty pair sounds like the first material on the default.
    if (entry == nullptr || entry->used == 0) {
        Entry** byDefault = cell(m1, fallback);
        entry = byDefault != nullptr ? *byDefault : nullptr;
    }
    return next(entry);
}

std::optional<MatrixSounds> SoundMatrix::nextAnimSounds(std::uint32_t event) {
    return event < kAnimSounds ? next(m_anims.at(event)) : std::nullopt;
}

std::size_t SoundMatrix::materialEntries() const {
    return static_cast<std::size_t>(std::ranges::count_if(m_materials, [](const Entry* e) { return e != nullptr; }));
}

std::size_t SoundMatrix::animEntries() const {
    return static_cast<std::size_t>(std::ranges::count_if(m_anims, [](const Entry* e) { return e != nullptr; }));
}

} // namespace coney::audio
