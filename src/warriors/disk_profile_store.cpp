// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/disk_profile_store.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "core/assert.h"

namespace coney {

namespace {

// The difficulty PM_Difficulty can choose last (UNLEASH THE FURY) and the one that earns it (HARDCORE SOLDIER).
constexpr int kLastDifficulty = 3;
constexpr int kHardcoreDifficulty = 2;

// Reads a whole file; nothing when it does not exist or cannot be opened.
std::optional<std::vector<std::byte>> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(chars.size());
    std::ranges::transform(chars, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return bytes;
}

} // namespace

void applyProfile(const ProfileRecord& record, GameState& state) {
    state.profileDifficulty = static_cast<double>(std::min<int>(record.difficulty, kLastDifficulty));
    state.brightness = record.brightness;
    state.subtitles = record.subtitles;
    state.saved = record.progress;
}

void captureProfile(const GameState& state, ProfileRecord& record) {
    // Gamma_Set keeps the brightness in a byte, clamped to 255; the difficulty is a byte too.
    record.brightness = static_cast<std::uint8_t>(std::clamp(state.brightness, 0, 255));
    record.difficulty = static_cast<std::uint8_t>(std::clamp(static_cast<int>(state.profileDifficulty), 0, 255));
    record.subtitles = state.subtitles;
    record.progress = state.saved;
}

DiskProfileStore::DiskProfileStore(std::filesystem::path folder, GameState& state)
    : m_folder(std::move(folder)), m_state(state) {
    readFolder();
}

void DiskProfileStore::reload() { readFolder(); }

void DiskProfileStore::readFolder() {
    for (std::size_t slot = 0; slot < kSlots; ++slot) {
        m_slots.at(slot) = readSlot(slot);
    }
    m_loaded.reset();
}

std::filesystem::path DiskProfileStore::slotFile(std::size_t slot) const {
    return m_folder / std::format("profile-{}.sav", slot + 1);
}

std::optional<DiskProfileStore::Slot> DiskProfileStore::readSlot(std::size_t slot) const {
    std::error_code ec;
    const std::filesystem::path path = slotFile(slot);
    if (!std::filesystem::is_regular_file(path, ec)) {
        return std::nullopt;
    }
    const std::optional<std::vector<std::byte>> bytes = readFile(path);
    if (bytes) {
        // Any version SaveGame reads, the bare record of earlier Coney builds included.
        if (auto save = SaveGame::parse(*bytes); save) {
            return Slot{.profile = profileOf(save->profile),
                        .record = std::move(save->profile),
                        .modded = std::move(save->modded)};
        }
    }
    // A file there that cannot be loaded: list it as damaged, under the name it holds when that can be read.
    Slot damaged;
    if (bytes) {
        damaged.profile.name = SaveGame::nameOf(*bytes);
    }
    damaged.profile.damaged = true;
    return damaged;
}

std::expected<void, Error> DiskProfileStore::writeSlot(std::size_t slot, const ProfileRecord& record,
                                                       const ModdedProgress& modded) const {
    std::error_code ec;
    std::filesystem::create_directories(m_folder, ec);
    if (ec) {
        return fail(ErrorCode::Io, std::format("{}: cannot be made ({})", m_folder.string(), ec.message()));
    }
    // Write beside the file, then replace it: a failure part way leaves the old profile as it was.
    const std::filesystem::path path = slotFile(slot);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        const SaveGame save{.profile = record, .modded = modded};
        const std::vector<std::byte> bytes = save.serialise();
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) {
            std::filesystem::remove(temporary, ec);
            return fail(ErrorCode::Io, std::format("{}: cannot be written", temporary.string()));
        }
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return fail(ErrorCode::Io, std::format("{}: cannot be replaced ({})", path.string(), ec.message()));
    }
    return {};
}

Profile DiskProfileStore::profileOf(const ProfileRecord& record) {
    Profile profile;
    profile.name = record.name;
    profile.difficulty = record.difficulty;
    profile.brightness = record.brightness;
    profile.subtitles = record.subtitles;
    return profile;
}

const Profile* DiskProfileStore::profile(std::size_t slot) const {
    if (slot >= kSlots) {
        return nullptr;
    }
    const std::optional<Slot>& held = m_slots.at(slot);
    return held.has_value() ? &held->profile : nullptr;
}

const ProfileRecord* DiskProfileStore::record(std::size_t slot) const {
    if (slot >= kSlots) {
        return nullptr;
    }
    const std::optional<Slot>& held = m_slots.at(slot);
    if (!held.has_value() || !held->record.has_value()) {
        return nullptr;
    }
    return &held->record.value();
}

const ModdedProgress* DiskProfileStore::modded(std::size_t slot) const {
    if (slot >= kSlots) {
        return nullptr;
    }
    const std::optional<Slot>& held = m_slots.at(slot);
    return held.has_value() && held->record.has_value() ? &held->modded : nullptr;
}

bool DiskProfileStore::create(std::size_t slot, const Profile& profile) {
    if (slot >= kSlots) {
        return false;
    }
    // A fresh game state's record with the typed name, put into the game state...
    ProfileRecord record;
    record.name = profile.name.substr(0, ProfileRecord::kNameLength);
    applyProfile(record, m_state);
    // ...then the three choices of the screens put back on top.
    m_state.profileDifficulty = profile.difficulty;
    m_state.brightness = profile.brightness;
    m_state.subtitles = profile.subtitles;
    captureProfile(m_state, record);
    // Coney's choice: it writes the new profile at once, with nothing modded, where the original writes the template
    // record into the card image at the slot's offset, then the slot's header, and marks the slot created since the
    // last save; the original's autosave right after a new game stores the same.
    if (auto written = writeSlot(slot, record, ModdedProgress{}); !written) {
        m_lastError = written.error();
        return false;
    }
    m_lastError.reset();
    m_slots.at(slot) = Slot{.profile = profileOf(record), .record = std::move(record), .modded = {}};
    m_state.modStory.setEntries({});
    m_loaded = slot;
    // Not ported: the original ends by applying the brightness to the display (Gamma_Set, #156).
    return true;
}

bool DiskProfileStore::load(std::size_t slot) {
    // Coney's choice: the slot's file was read with the others (readSlots), so an empty or damaged slot fails here
    // where the original reads its image anyway.
    const ProfileRecord* held = record(slot);
    const ModdedProgress* heldModded = modded(slot);
    if (held == nullptr || heldModded == nullptr) {
        return false;
    }
    applyProfile(*held, m_state);
    // The slot's mod missions too, those of mods the session lacks included: the next save writes them back.
    m_state.modStory.setEntries(heldModded->story);
    m_loaded = slot;
    return true;
}

void DiskProfileStore::remove(std::size_t slot) {
    if (slot >= kSlots) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(slotFile(slot), ec);
    if (ec) {
        m_lastError =
            Error{ErrorCode::Io, std::format("{}: cannot be deleted ({})", slotFile(slot).string(), ec.message())};
    }
    m_slots.at(slot).reset();
    if (m_loaded == slot) {
        m_loaded.reset();
    }
}

bool DiskProfileStore::fourthDifficultyUnlocked() const {
    return std::ranges::any_of(m_slots, [](const std::optional<Slot>& slot) {
        return slot && slot->record && slot->record->hardcoreFinished;
    });
}

bool DiskProfileStore::save() {
    if (!m_inUse || !m_loaded.has_value()) {
        return true;
    }
    const std::size_t index = m_loaded.value();
    std::optional<Slot>& slot = m_slots.at(index);
    // A loaded slot always holds a record: load() and create() set m_loaded only then, remove() clears it.
    CONEY_ASSERT(slot.has_value() && slot->record.has_value());
    ProfileRecord record = slot->record.value();
    captureProfile(m_state, record);
    // Coney's choice: one file per slot, so there are no six headers to write first; the file holds the session's
    // story entries (the loaded ones and those played since) and the mod sections the slot holds.
    ModdedProgress modded = slot->modded;
    modded.story = m_state.modStory.entries();
    if (auto written = writeSlot(index, record, modded); !written) {
        m_lastError = written.error();
        return false;
    }
    m_lastError.reset();
    slot->profile = profileOf(record);
    slot->record = std::move(record);
    slot->modded = std::move(modded);
    return true;
}

void DiskProfileStore::noteStoryFinished() {
    if (!m_inUse || !m_loaded.has_value() || static_cast<int>(m_state.profileDifficulty) != kHardcoreDifficulty) {
        return;
    }
    std::optional<Slot>& slot = m_slots.at(m_loaded.value());
    if (slot.has_value() && slot->record.has_value()) {
        slot->record->hardcoreFinished = true;
    }
}

} // namespace coney
