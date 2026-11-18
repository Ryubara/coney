// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only Coney's own save container; the field set it carries is Profile_Write's
// (docs/research/save.md#save-format)
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "core/error.h"
#include "warriors/profile_record.h"

namespace coney {

/// One mod mission's progress in the save's story section, keyed by its mission id: what the original keeps for one
/// of its own missions in the unlock bits and the 30 best records, minus nothing and plus nothing (no checkpoint).
///
/// Research: docs/research/save.md#save-mods, docs/guides/studio/mission-mods.md#graph-progress
struct MissionProgress {
    std::string id;              ///< The mission's id, `<mod>.<id>` (`yard-brawl.alley`), 1-255 bytes.
    bool complete = false;       ///< The mission's completion record, `(level, 0, 0)` for an original one.
    std::uint32_t ranks = 0;     ///< Rank record `(level, 0, k)` for k 1-32 is bit k - 1.
    std::uint32_t bestScore = 0; ///< The best record's best score, as MissionBest::bestScore.
    std::uint32_t total = 0;     ///< Its running total, as MissionBest::total.
    std::uint8_t grade = 0;      ///< Its grade byte, as MissionBest::grade.
    std::uint8_t percent = 0;    ///< Its percentage byte, as MissionBest::percent.

    friend bool operator==(const MissionProgress&, const MissionProgress&) = default;
};

/// One mod's section of a save: the mod named as everywhere else (name, version, package hash) and the bytes it keeps
/// with the save.
///
/// Research: docs/research/save.md#save-mods, docs/guides/studio/mods.md#saves-replays
struct ModSection {
    /// The longest mod name: the section name `mod:<name>` is at most 64 bytes.
    static constexpr std::size_t kMaxName = 60;

    std::string name;                           ///< The project's name: lower case, digits and '-', 1-60 bytes.
    std::string version;                        ///< The project's version, 0-255 bytes.
    std::array<std::uint8_t, 32> packageHash{}; ///< The SHA-256 of the packaged manifest.
    std::vector<std::byte> data;                ///< The mod's own bytes, kept as they are.

    friend bool operator==(const ModSection&, const ModSection&) = default;
};

/// What a save holds beyond the original's record: mod missions' progress and the mods' sections. Empty in a save of a
/// session with no mod running; a save keeps what the file held, so a mod that is not running keeps its section.
///
/// Research: docs/research/save.md#save-mods
struct ModdedProgress {
    std::vector<MissionProgress> story; ///< The story section, sorted by id when written.
    std::vector<ModSection> mods;       ///< One section per mod, sorted by name when written.

    /// Whether there is nothing here: the save is a faithful one.
    [[nodiscard]] bool empty() const { return story.empty() && mods.empty(); }

    friend bool operator==(const ModdedProgress&, const ModdedProgress&) = default;
};

/// A profile in Coney's own versioned save format: the original's record field for field (ProfileRecord, the faithful
/// set `Profile_Write` stores and nothing else) and, for modded sessions, ModdedProgress. Little-endian named sections
/// under one format version, a CRC-32 at the end; older versions are migrated step by step when read.
///
/// Research: docs/research/save.md#save-format
struct SaveGame {
    /// The format version this Coney writes. Version 0 is the original's bare record.
    static constexpr std::uint32_t kVersion = 1;

    ProfileRecord profile; ///< The faithful fields.
    ModdedProgress modded; ///< Mod missions and mod sections; empty without mods.

    /// The file's bytes in the current version.
    /// @coney-only Coney's save container
    [[nodiscard]] std::vector<std::byte> serialise() const;
    /// Reads a save of any version up to kVersion, migrating an older one. Fails with ErrorCode::Truncated when it
    /// ends early, and ErrorCode::Invalid for a newer version, a CRC-32 that does not match, or a section missing,
    /// repeated, unknown, of the wrong length or with a value outside its rules (docs/research/save.md#save-damaged).
    /// @coney-only Coney's save container
    [[nodiscard]] static std::expected<SaveGame, Error> parse(std::span<const std::byte> bytes);
    /// The profile name a file holds, read as far as the frame allows even when the file cannot be loaded (a newer
    /// version, a damaged one, a version-0 record of another version): the name a damaged profile is listed under.
    /// Empty when no name can be read.
    /// @coney-only Coney's save container
    [[nodiscard]] static std::string nameOf(std::span<const std::byte> bytes);
};

} // namespace coney
