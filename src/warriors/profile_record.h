// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

#include "core/error.h"

namespace coney {

/// One mission's best record in a profile: 12 bytes, indexed by the level record's mission byte (`+0x0c`).
///
/// Research: docs/research/save.md#record
struct MissionBest {
    std::uint32_t bestScore = 0;        ///< `+0`: the best score.
    std::uint32_t total = 0;            ///< `+4`: a running total.
    std::uint8_t grade = 0;             ///< `+8`: a grade byte.
    std::uint8_t percent = 0;           ///< `+9`: a percentage byte.
    std::array<std::uint8_t, 2> rest{}; ///< `+10`, `+11`: not described; kept as read.
};

/// One pad's options in a profile (P1 is pad 0).
struct PadOptions {
    bool invertCamera = false;    ///< Invert P1 / P2 Cam (`W_GameState + 0x440 + pad × 4`).
    bool autoAdjustCamera = true; ///< Auto Adjust P1 / P2 Cam (`W_GameState + 0x448 + pad × 4`).
    bool vibration = true;        ///< P1 / P2 Controller vibration (byte `W_GameState + 0x56de + pad`).
};

/// The parts of a profile that the game state keeps for the save system and that no other Coney subsystem owns yet:
/// the per-mission bests, the unlockables' bit sets, the Rumble data, the banked money, the options other than
/// difficulty, brightness and subtitles, and the 128 script flags. GameState holds one (GameState::saved);
/// ProfileRecord carries one.
///
/// Research: docs/research/save.md#record
struct SavedProgress {
    /// Missions with a best record (30 × 12 bytes at record `+0x010`).
    static constexpr std::size_t kMissions = 30;
    /// Unlockable records, one bit each in the two 0x50-byte sets.
    static constexpr std::size_t kUnlockables = 640;
    /// Script flags `SetLUASaveDataBool` sets, numbered 1-128.
    static constexpr std::size_t kScriptFlags = 128;
    /// Bytes of the Rumble data (record `+0x218`).
    static constexpr std::size_t kRumbleBytes = 0x254;
    /// Bytes of the packed copy of the Rumble data's first 512 bits (record `+0x46c`).
    static constexpr std::size_t kRumbleBitBytes = 0x40;

    std::array<MissionBest, kMissions> missionBests{}; ///< `+0x010`, stats object `0x006fe490 + 0x300`.
    /// `+0x178`: the unlockables' **locked** bits (`0x006fe8f8`), record *i* at byte i / 8, bit i % 8 (the
    /// little-endian layout of a 32-bit-word set, inferred). A fresh profile has every record locked.
    std::array<std::uint8_t, kUnlockables / 8> lockedBits = allSet();
    /// `+0x1c8`: the second 640-bit set (`0x006fe948`, the "new" marks, inferred), laid out as lockedBits.
    std::array<std::uint8_t, kUnlockables / 8> newBits{};
    std::array<std::uint8_t, kRumbleBytes> rumbleData{};      ///< `+0x218`: the Rumble data (`0x0063ef80`).
    std::array<std::uint8_t, kRumbleBitBytes> rumbleBits{};   ///< `+0x46c`: its first 512 bits again, packed.
    std::uint32_t bankedMoney = 0;                            ///< `+0x4ac`: `W_GameState + 0x480`.
    std::uint32_t optionWord0 = 0;                            ///< `+0x4b0`: the option block's `+0x00`, unidentified.
    float soundFxVolume = 0.9F;                               ///< `+0x4b8`: the SoundFX volume.
    float musicVolume = 0.9F;                                 ///< `+0x4bc`: the Music volume.
    std::uint32_t optionWord10 = 0;                           ///< `+0x4c0`: the option block's `+0x10`, unidentified.
    std::array<PadOptions, 2> pads{};                         ///< `+0x4c4`: P1 then P2.
    bool splitScreenMerge = true;                             ///< `+0x4dc`: 2P Split Screen Merge.
    bool proLogic2 = false;                                   ///< `+0x4e4`: Pro Logic II.
    bool wideVideo = false;                                   ///< `+0x4e8`: the Video setting, Wide when set.
    std::uint32_t stateWord0 = 0;                             ///< `+0x4f0`: `W_GameState + 0x00`, unidentified.
    std::array<std::uint8_t, kScriptFlags / 8> scriptFlags{}; ///< `+0x4f4`: flag n at byte (n-1) / 8, bit (n-1) % 8.

    /// Whether unlockable record `index` (0-639) is locked; out of range reads as locked.
    [[nodiscard]] bool isLocked(std::size_t index) const;
    /// Locks or unlocks unlockable record `index` (0-639); out of range does nothing.
    void setLocked(std::size_t index, bool locked);
    /// Script flag `flag` (1-128, as `GetLUASaveDataBool` numbers them); out of range reads false.
    [[nodiscard]] bool scriptFlag(std::size_t flag) const;
    /// Sets script flag `flag` (1-128, `SetLUASaveDataBool`); out of range does nothing.
    void setScriptFlag(std::size_t flag, bool on);
    /// Adds a player's mission money to the bank, as mode 0xb's `Update` does for each player after a mission (the
    /// add at `0x0041e398`, docs/research/save.md#record).
    void addToBank(std::uint32_t amount) { bankedMoney += amount; }

  private:
    // Every bit set: the locked set of a fresh profile.
    static constexpr std::array<std::uint8_t, kUnlockables / 8> allSet() {
        std::array<std::uint8_t, kUnlockables / 8> bits{};
        bits.fill(0xff);
        return bits;
    }
};

/// A profile as the save system stores it: the 1,284-byte record, version `0x11`, little-endian, fields in the order
/// `Profile_Write` writes them. A default-constructed record is the template a new profile is made from (a fresh game
/// state) with an empty name. Coney keeps one record per file (docs/research/save.md#coney).
///
/// Research: docs/research/save.md#record
struct ProfileRecord {
    /// The record's size, measured by `SaveSystem_Init` (`ss + 0x108`).
    static constexpr std::size_t kSize = 0x504;
    /// The only version the game writes and accepts.
    static constexpr std::uint32_t kVersion = 0x11;
    /// The longest name: 8 bytes, with no terminator when all 8 are used.
    static constexpr std::size_t kNameLength = 8;
    /// The default brightness: `Gamma_Set`'s 40, what PM_Light starts at.
    static constexpr std::uint8_t kDefaultBrightness = 40;

    std::string name;              ///< `+0x004`: at most kNameLength bytes.
    bool hardcoreFinished = false; ///< `+0x00c`: story finished on HARDCORE SOLDIER (the fourth-difficulty unlock).
    std::uint8_t brightness = kDefaultBrightness; ///< `+0x4b4`: the option block's brightness byte, 0-100.
    bool subtitles = false;                       ///< `+0x4e0`: `W_GameState + 0x438`.
    std::uint8_t difficulty = 1;                  ///< `+0x4ec`: `W_GameState + 0x43c`, 0 SUCKER to 3 UNLEASH THE FURY.
    SavedProgress progress;                       ///< Every other field.

    /// The record's bytes, kSize long; the name is cut to kNameLength bytes.
    /// @orig 0x00421708 Profile_Write (W_SaveSystem.cpp)
    /// @orig 0x00421638 ProfileHeader_Write (W_SaveSystem.cpp)
    [[nodiscard]] std::array<std::byte, kSize> serialise() const;
    /// Reads a record. Fails with ErrorCode::Truncated or ErrorCode::Invalid when `bytes` is not kSize long, and with
    /// ErrorCode::Invalid when the version is not kVersion (the "bad version" flag `ss + 0x11c`).
    /// @orig 0x00421ad0 Profile_Read (W_SaveSystem.cpp)
    /// @orig 0x004219d0 ProfileHeader_Read (W_SaveSystem.cpp)
    [[nodiscard]] static std::expected<ProfileRecord, Error> parse(std::span<const std::byte> bytes);
    /// The name in a record's 16-byte header, even when the rest of it is unreadable. Fails with
    /// ErrorCode::Truncated when `bytes` is shorter than the header. The save system lists such a profile as not
    /// loadable (slot `+0x24`).
    [[nodiscard]] static std::expected<std::string, Error> headerName(std::span<const std::byte> bytes);
};

} // namespace coney
