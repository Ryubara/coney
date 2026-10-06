// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace coney {

/// One player profile as the profile manager's screens see it: its name and the choices a new game made on the way
/// in (PM_Difficulty, PM_Light, PM_Subtitles). The original's profile record and its save format are being researched
/// (save.md); these fields are what the screens read and write, **Coney's stand-in** until that page lands.
struct Profile {
    /// The longest name PM_Create accepts (`0x0063f1d8`).
    static constexpr std::size_t kNameLength = 8;

    std::string name;       ///< The name typed on PM_Create, at most kNameLength characters.
    int difficulty = 1;     ///< PM_Difficulty's index (`W_GameState + 0x43c`).
    int brightness = 40;    ///< PM_Light's value, 0-100 (`W_GameState + 0x57a4`).
    bool subtitles = false; ///< PM_Subtitles' choice (`W_GameState + 0x438`).
    bool damaged = false;   ///< The save system reports the profile damaged (save-system `+0x7c`).
};

/// The save system as the profile manager asks it about profiles: which of the six slots hold one, whether there is
/// room for another, and creating, loading and deleting them. Each call names the save-system slot of the original it
/// stands for (docs/research/frontend.md#profile-manager); what those slots do on a memory card is save.md's subject.
///
/// Research: docs/research/frontend.md#profile-manager
class ProfileStore {
  public:
    /// How many profiles the save system holds (PM_Create's duplicate check walks 6 slots; PM_Profile offers "create"
    /// while the count is not 6).
    static constexpr std::size_t kSlots = 6;

    virtual ~ProfileStore() = default;
    ProfileStore() = default;
    ProfileStore(const ProfileStore&) = delete;
    ProfileStore& operator=(const ProfileStore&) = delete;
    ProfileStore(ProfileStore&&) = delete;
    ProfileStore& operator=(ProfileStore&&) = delete;

    /// The profile in `slot`, or null when the slot is empty or out of range.
    [[nodiscard]] virtual const Profile* profile(std::size_t slot) const = 0;
    /// Profiles held (save-system `+0x9c`).
    [[nodiscard]] std::size_t count() const;
    /// The first empty slot, where a new profile goes (save-system `+0xa4`); nothing when all are used.
    [[nodiscard]] std::optional<std::size_t> freeSlot() const;
    /// Whether the medium has room for another profile (save-system `+0x144`).
    [[nodiscard]] virtual bool hasRoom() const = 0;
    /// Whether some slot already holds a profile named `name` (PM_Create's check, exact bytes).
    [[nodiscard]] bool nameUsed(std::string_view name) const;

    /// Creates `profile` in `slot` (save-system `+0x4c(slot, name)`, called by the profile manager's exit). Returns
    /// false when the slot is out of range or the medium refuses it.
    virtual bool create(std::size_t slot, const Profile& profile) = 0;
    /// Loads the profile in `slot` as the current one (save-system `+0xcc(slot)`). Returns false for an empty slot.
    virtual bool load(std::size_t slot) = 0;
    /// Deletes the profile in `slot` (save-system `+0x54(slot)` and `+0x18c(slot)`).
    virtual void remove(std::size_t slot) = 0;
    /// Marks a profile in use for this session, or none (save-system `+0x124`: set by PM_Light's and PM_Load's
    /// accept, cleared when the profile manager starts; its meaning is inferred from those uses).
    virtual void setInUse(bool inUse) = 0;
    /// Whether a profile is in use (above).
    [[nodiscard]] virtual bool inUse() const = 0;
    /// The slot loaded last, or nothing.
    [[nodiscard]] virtual std::optional<std::size_t> loaded() const = 0;
    /// Whether PM_Difficulty offers its fourth item (a save-system query not yet identified).
    [[nodiscard]] virtual bool fourthDifficultyUnlocked() const = 0;
    /// Saves the game state into the loaded profile when one is in use (the autosave the mission-complete mode asks
    /// for). Returns false when the medium refuses it.
    virtual bool save() = 0;
    /// Reads the medium again, forgetting the loaded profile: the load kind of the memory-card mode (6), at boot and
    /// for RELOAD PROFILES (`SSMC_StartLoadSequence`). The session store has nothing to read.
    virtual void reload() {}
};

/// **Coney's stand-in** for the save system: profiles live in memory for the session only. There is no memory card in
/// Coney and its save files are not designed yet (save.md), so every run starts with no profile, which is what a new
/// player sees: PM_Profile offers CREATE NEW PROFILE and RELOAD PROFILES. The fourth difficulty is locked.
class SessionProfileStore final : public ProfileStore {
  public:
    [[nodiscard]] const Profile* profile(std::size_t slot) const override;
    [[nodiscard]] bool hasRoom() const override { return freeSlot().has_value(); }
    bool create(std::size_t slot, const Profile& profile) override;
    bool load(std::size_t slot) override;
    void remove(std::size_t slot) override;
    void setInUse(bool inUse) override { m_inUse = inUse; }
    [[nodiscard]] bool inUse() const override { return m_inUse; }
    [[nodiscard]] std::optional<std::size_t> loaded() const override { return m_loaded; }
    [[nodiscard]] bool fourthDifficultyUnlocked() const override { return false; }
    /// Nothing to write: the profiles live in memory.
    bool save() override { return true; }

  private:
    std::array<std::optional<Profile>, kSlots> m_slots;
    std::optional<std::size_t> m_loaded;
    bool m_inUse = false;
};

} // namespace coney
