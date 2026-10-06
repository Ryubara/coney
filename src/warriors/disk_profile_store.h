// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>

#include "core/error.h"
#include "warriors/game_state.h"
#include "warriors/profile_record.h"
#include "warriors/profile_store.h"

namespace coney {

/// Puts a profile into the game state, as loading a slot does: the difficulty, the brightness, the subtitles and
/// every other saved field (GameState::saved). The checkpoint, the inventories and the script numbers are not saved,
/// so they are left alone.
/// @orig 0x00421ad0 Profile_Read (W_SaveSystem.cpp)
void applyProfile(const ProfileRecord& record, GameState& state);

/// Writes the game state's saved fields into `record`, keeping its name and fourth-difficulty unlock: what an
/// autosave stores.
/// @orig 0x00421708 Profile_Write (W_SaveSystem.cpp)
void captureProfile(const GameState& state, ProfileRecord& record);

/// The save system with Coney's storage in place of the memory card: each of the six profile slots is one file,
/// `profile-<slot + 1>.sav`, holding the 1,284-byte record, in a folder the caller names (the platform's per-user data
/// folder in the game, a temporary folder in tests). There are no card dialogs: no format, space or icon checks, and
/// every write happens at once (docs/research/save.md#coney).
///
/// A file that cannot be read as a record (another size, another version) is listed as a damaged profile under the
/// name in its header, the original's "not loadable" slot (`+0x24`), which PM_Load sends to PM_Delete.
///
/// Research: docs/research/save.md
class DiskProfileStore final : public ProfileStore {
  public:
    /// Reads the profiles in `folder` (which need not exist yet; it is made on the first write) and applies loaded
    /// profiles to `state`, which must outlive the store.
    DiskProfileStore(std::filesystem::path folder, GameState& state);

    [[nodiscard]] const Profile* profile(std::size_t slot) const override;
    /// Coney has no space check: there is room while a slot is free.
    [[nodiscard]] bool hasRoom() const override { return freeSlot().has_value(); }
    /// Makes a new profile from the template (a fresh game state), puts it into the game state with the
    /// difficulty, brightness and subtitles chosen on the screens, and writes its file at once.
    /// @orig 0x00421e68 Profile_Create (W_SaveSystem.cpp)
    bool create(std::size_t slot, const Profile& profile) override;
    /// Puts the profile in `slot` into the game state (save-system `+0xcc`). Fails for an empty or damaged slot.
    bool load(std::size_t slot) override;
    /// Deletes the profile's file and empties the slot; a deleted profile cannot be brought back.
    /// @orig 0x00421f98 Profile_Delete (W_SaveSystem.cpp)
    void remove(std::size_t slot) override;
    /// Turns saving on or off (save-system `+0x124`, "saving enabled").
    void setInUse(bool inUse) override { m_inUse = inUse; }
    [[nodiscard]] bool inUse() const override { return m_inUse; }
    [[nodiscard]] std::optional<std::size_t> loaded() const override { return m_loaded; }
    /// Whether any profile has finished the story on HARDCORE SOLDIER (slot `+0x28` of any slot, save-system `+0xac`).
    [[nodiscard]] bool fourthDifficultyUnlocked() const override;
    /// The autosave: when saving is enabled and a profile is loaded, writes the game state into its record and file.
    /// Does nothing (and succeeds) otherwise; returns false when the file cannot be written.
    /// @orig 0x00421d98 SaveSystem_WriteCurrent (W_SaveSystem.cpp)
    bool save() override;

    /// Notes that the story was finished: when saving is enabled and the game state's difficulty is HARDCORE SOLDIER
    /// (2), the loaded profile earns the fourth difficulty (save-system `+0xb4`). The original also requires every
    /// story level (mission bytes 1-23) unlocked; the caller checks that, as Coney's unlockables have no records yet.
    void noteStoryFinished();
    /// Reads the folder again, forgetting the loaded profile (mode 6's load: at boot and RELOAD PROFILES).
    void reload() override;

    /// The record in `slot`, or null for an empty or damaged slot.
    [[nodiscard]] const ProfileRecord* record(std::size_t slot) const;
    /// The file `slot` is kept in.
    [[nodiscard]] std::filesystem::path slotFile(std::size_t slot) const;
    /// The last write or delete that failed, kept for the log; nothing after a success.
    [[nodiscard]] const std::optional<Error>& lastError() const { return m_lastError; }

  private:
    // One used slot: what the screens see and, unless damaged, the record behind it.
    struct Slot {
        Profile profile;
        std::optional<ProfileRecord> record;
    };

    // Reads every slot's file and forgets the loaded profile (the constructor's read and reload(), kept non-virtual so
    // the constructor makes no virtual call).
    void readFolder();
    // Reads one slot's file: nothing when there is none, a damaged slot when it is not a record.
    [[nodiscard]] std::optional<Slot> readSlot(std::size_t slot) const;
    // Writes `record` as `slot`'s file through a temporary file, so a failed write never leaves half a record.
    [[nodiscard]] std::expected<void, Error> writeSlot(std::size_t slot, const ProfileRecord& record) const;
    // The screens' view of a record.
    [[nodiscard]] static Profile profileOf(const ProfileRecord& record);

    std::filesystem::path m_folder;
    GameState& m_state;
    std::array<std::optional<Slot>, kSlots> m_slots;
    std::optional<std::size_t> m_loaded;
    bool m_inUse = false;
    std::optional<Error> m_lastError;
};

} // namespace coney
