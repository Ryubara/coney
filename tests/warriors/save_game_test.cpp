// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's own versioned save format (docs/research/save.md#save-format).
#include "warriors/save_game.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/name_hash.h"

using coney::ErrorCode;
using coney::MissionProgress;
using coney::ModSection;
using coney::ProfileRecord;
using coney::SavedProgress;
using coney::SaveGame;

namespace {

// One section of a hand-built file: its name and payload.
struct RawSection {
    std::string name;
    std::vector<std::byte> payload;
};

// Appends a little-endian 32-bit value.
void putU32(std::vector<std::byte>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::byte>(value >> shift));
    }
}

// The little-endian 32-bit value at `at`.
std::uint32_t u32At(std::span<const std::byte> bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes[at + i]) << (8 * i);
    }
    return value;
}

// A file in the format's frame, written by the test itself: the magic, `version`, the sections, then the CRC-32.
// It stands for what any version of Coney writes, since the frame never changes (save.md#save-format).
std::vector<std::byte> frame(std::uint32_t version, const std::vector<RawSection>& sections) {
    std::vector<std::byte> out;
    for (const char c : std::string_view("CONEYSAV")) {
        out.push_back(static_cast<std::byte>(c));
    }
    putU32(out, version);
    putU32(out, static_cast<std::uint32_t>(sections.size()));
    for (const RawSection& section : sections) {
        out.push_back(static_cast<std::byte>(section.name.size()));
        for (const char c : section.name) {
            out.push_back(static_cast<std::byte>(c));
        }
        putU32(out, static_cast<std::uint32_t>(section.payload.size()));
        out.insert(out.end(), section.payload.begin(), section.payload.end());
    }
    putU32(out, coney::crc32(std::span<const std::byte>(out)));
    return out;
}

// The sections of a file this Coney wrote, read back with the test's own reading of the frame.
std::vector<RawSection> sectionsOf(std::span<const std::byte> bytes) {
    std::vector<RawSection> sections;
    if (bytes.size() < 16) {
        return sections;
    }
    std::size_t at = 16;
    const std::uint32_t count = u32At(bytes, 12);
    for (std::uint32_t i = 0; i < count; ++i) {
        RawSection section;
        const auto nameLength = static_cast<std::size_t>(bytes[at++]);
        for (std::size_t c = 0; c < nameLength; ++c) {
            section.name.push_back(static_cast<char>(bytes[at++]));
        }
        const std::uint32_t length = u32At(bytes, at);
        at += 4;
        section.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                               bytes.begin() + static_cast<std::ptrdiff_t>(at + length));
        at += length;
        sections.push_back(std::move(section));
    }
    return sections;
}

// A record with every field away from its default, so a field lost or swapped on the way shows.
ProfileRecord everyFieldChanged() {
    ProfileRecord record;
    record.name = "ABCDEFGH";
    record.hardcoreFinished = true;
    record.brightness = 65;
    record.subtitles = true;
    record.difficulty = 3;
    SavedProgress& p = record.progress;
    for (std::size_t i = 0; i < SavedProgress::kMissions; ++i) {
        const auto n = static_cast<std::uint32_t>(i);
        p.missionBests.at(i) = {.bestScore = 1000 + n,
                                .total = 50 + n,
                                .grade = static_cast<std::uint8_t>(n % 5),
                                .percent = static_cast<std::uint8_t>(60 + n),
                                .rest = {static_cast<std::uint8_t>(n), static_cast<std::uint8_t>(n * 3)}};
    }
    p.setLocked(0, false);
    p.setLocked(639, false);
    p.newBits[3] = 0x81;
    p.newBits[79] = 0x7e;
    p.rumbleData[0] = 0x11;
    p.rumbleData[0x253] = 0x42;
    p.rumbleBits[0] = 0x24;
    p.rumbleBits[0x3f] = 0x99;
    p.bankedMoney = 325;
    p.optionWord0 = 7;
    p.soundFxVolume = 0.5F;
    p.musicVolume = 0.25F;
    p.optionWord10 = 9;
    p.pads[0] = {.invertCamera = true, .autoAdjustCamera = true, .vibration = false};
    p.pads[1] = {.invertCamera = false, .autoAdjustCamera = false, .vibration = true};
    p.splitScreenMerge = false;
    p.proLogic2 = true;
    p.wideVideo = true;
    p.stateWord0 = 11;
    p.setScriptFlag(1, true);
    p.setScriptFlag(77, true);
    p.setScriptFlag(128, true);
    return record;
}

// The names of the sections, in file order.
std::vector<std::string> namesOf(const std::vector<RawSection>& sections) {
    std::vector<std::string> names;
    for (const RawSection& section : sections) {
        names.push_back(section.name);
    }
    return names;
}

// The faithful sections, in the order the writer puts them (save.md#save-mapping).
const std::vector<std::string> kFaithful{"profile", "missions", "unlockables", "rumble",
                                         "bank",    "options",  "script-flags"};

// A save's file with one section's payload replaced (or the section dropped when `payload` is empty and `drop`).
std::vector<std::byte> withSection(std::string_view name, std::vector<std::byte> payload, bool drop = false) {
    SaveGame save;
    save.profile.name = "AJAX";
    std::vector<RawSection> sections = sectionsOf(save.serialise());
    for (auto it = sections.begin(); it != sections.end(); ++it) {
        if (it->name == name) {
            if (drop) {
                sections.erase(it);
            } else {
                it->payload = std::move(payload);
            }
            break;
        }
    }
    return frame(SaveGame::kVersion, sections);
}

// The payload of section `name` in a default save, so a test can change one byte of it.
std::vector<std::byte> payloadOf(std::string_view name) {
    SaveGame save;
    save.profile.name = "AJAX";
    for (RawSection& section : sectionsOf(save.serialise())) {
        if (section.name == name) {
            return std::move(section.payload);
        }
    }
    return {};
}

// One fixture per older version: the bytes a file of that version holds, and the record they stand for.
struct OlderVersion {
    std::uint32_t version;
    std::vector<std::byte> bytes;
};

// Every version before SaveGame::kVersion, written as that version wrote it. A new version adds the fixture of the one
// before it here, which the static_assert below asks for.
std::array<OlderVersion, SaveGame::kVersion> olderVersions(const ProfileRecord& record) {
    const auto v0 = record.serialise();
    return {OlderVersion{.version = 0, .bytes = std::vector<std::byte>(v0.begin(), v0.end())}};
}

} // namespace

TEST_CASE("a save keeps every faithful field through a write and a read", "[save_game]") {
    // Expected: save.md#record, confirmed (code): the field set Profile_Write stores; the container is #1179's
    SaveGame save;
    save.profile = everyFieldChanged();
    const std::vector<std::byte> bytes = save.serialise();
    auto read = SaveGame::parse(bytes);
    REQUIRE(read.has_value());

    // Every field of the record: two records with the same bytes have the same fields.
    CHECK(read->profile.serialise() == save.profile.serialise());
    CHECK(read->profile.name == "ABCDEFGH");
    CHECK(read->profile.hardcoreFinished);
    CHECK(read->profile.difficulty == 3);
    CHECK(read->profile.brightness == 65);
    CHECK(read->profile.subtitles);
    CHECK(read->profile.progress.missionBests[29].rest[1] == 87);
    CHECK(read->profile.progress.soundFxVolume == 0.5F);
    CHECK(read->profile.progress.pads[0].invertCamera);
    CHECK_FALSE(read->profile.progress.pads[0].vibration);
    CHECK(read->profile.progress.scriptFlag(77));
    CHECK(read->modded.empty());
    // Written again, the same bytes: the format has one way to write a save.
    CHECK(read->serialise() == bytes);
}

TEST_CASE("a save without mods holds the faithful sections and nothing else", "[save_game]") {
    // Expected: save.md#save-mapping, confirmed (code) for the field sizes (save.md#record); the layout is #1179's
    SaveGame save;
    save.profile.name = "SWAN";
    const std::vector<std::byte> bytes = save.serialise();
    REQUIRE(bytes.size() > 20);
    CHECK(std::string(reinterpret_cast<const char*>(bytes.data()), 8) == "CONEYSAV");
    CHECK(u32At(bytes, 8) == SaveGame::kVersion);
    const std::vector<RawSection> sections = sectionsOf(bytes);
    CHECK(namesOf(sections) == kFaithful);

    // Each section's size is its fields' sizes, so nothing beyond the record's fields is stored.
    const std::vector<std::size_t> sizes{1 + 4 + 1 + 1 + 4, 1 + 30 * 12, 80 + 80, 0x254 + 0x40, 4, 27, 16};
    std::vector<std::size_t> found;
    for (const RawSection& section : sections) {
        found.push_back(section.payload.size());
    }
    CHECK(found == sizes);
    // The trailer is the CRC-32 of everything before it.
    const std::span<const std::byte> body(bytes.data(), bytes.size() - 4);
    CHECK(u32At(bytes, bytes.size() - 4) == coney::crc32(body));
}

TEST_CASE("a save of each older version migrates with every field", "[save_game]") {
    // Expected: unverified, #1179 (Coney's own versions, save.md#save-versions; version 0 is save.md#record)
    const ProfileRecord record = everyFieldChanged();
    const auto older = olderVersions(record);
    static_assert(std::tuple_size_v<decltype(older)> == SaveGame::kVersion, "a fixture per older version");
    for (const OlderVersion& file : older) {
        INFO("version " << file.version);
        auto read = SaveGame::parse(file.bytes);
        REQUIRE(read.has_value());
        CHECK(read->profile.serialise() == record.serialise());
        CHECK(read->modded.empty());
    }
}

TEST_CASE("story entries and mod sections survive a write and a read, in order", "[save_game]") {
    // Expected: unverified, #1179 (Coney's own sections, save.md#save-mods)
    SaveGame save;
    save.profile.name = "RUMBLE";
    save.modded.story.push_back(
        {.id = "zeta.b", .complete = false, .ranks = 0, .bestScore = 9, .total = 2, .grade = 1, .percent = 3});
    save.modded.story.push_back({.id = "yard-brawl.alley",
                                 .complete = true,
                                 .ranks = 0x80000005U,
                                 .bestScore = 4321,
                                 .total = 99,
                                 .grade = 4,
                                 .percent = 100});
    ModSection zeta{.name = "zeta", .version = "1.0.0", .packageHash = {}, .data = {}};
    zeta.packageHash.fill(0xab);
    ModSection brawl{.name = "yard-brawl", .version = "0.3.0", .packageHash = {}, .data = {std::byte{1}, std::byte{2}}};
    brawl.packageHash[31] = 0x01;
    save.modded.mods = {zeta, brawl};

    const std::vector<std::byte> bytes = save.serialise();
    std::vector<std::string> expected = kFaithful;
    expected.insert(expected.end(), {"story", "mod:yard-brawl", "mod:zeta"});
    CHECK(namesOf(sectionsOf(bytes)) == expected);

    auto read = SaveGame::parse(bytes);
    REQUIRE(read.has_value());
    REQUIRE(read->modded.story.size() == 2);
    CHECK(read->modded.story[0] == save.modded.story[1]); // sorted by id
    CHECK(read->modded.story[1] == save.modded.story[0]);
    REQUIRE(read->modded.mods.size() == 2);
    CHECK(read->modded.mods[0] == brawl); // sorted by name
    CHECK(read->modded.mods[1] == zeta);
    CHECK(read->profile.serialise() == save.profile.serialise());
}

TEST_CASE("a save of a newer version is not loadable but keeps its name", "[save_game]") {
    // Expected: unverified, #1179 (Coney's own rule, save.md#save-damaged: the frame never changes)
    SaveGame save;
    save.profile.name = "VERMIN";
    std::vector<RawSection> sections = sectionsOf(save.serialise());
    sections.push_back({.name = "a-later-section", .payload = {std::byte{7}}});
    const std::vector<std::byte> newer = frame(SaveGame::kVersion + 1, sections);

    auto read = SaveGame::parse(newer);
    REQUIRE_FALSE(read.has_value());
    CHECK(read.error().code == ErrorCode::Invalid);
    CHECK(read.error().message.find("newer") != std::string::npos);
    CHECK(SaveGame::nameOf(newer) == "VERMIN");
}

TEST_CASE("a damaged save is not loadable", "[save_game]") {
    // Expected: unverified, #1179 (Coney's own checks, save.md#save-damaged; the record has none)
    SaveGame save;
    save.profile.name = "AJAX";
    const std::vector<std::byte> good = save.serialise();
    REQUIRE(SaveGame::parse(good).has_value());

    SECTION("a changed byte fails the CRC-32 but the name is still read") {
        std::vector<std::byte> bytes = good;
        bytes[bytes.size() / 2] ^= std::byte{0x10};
        CHECK_FALSE(SaveGame::parse(bytes).has_value());
        CHECK(SaveGame::nameOf(bytes) == "AJAX");
    }
    SECTION("a file cut short") {
        const std::span<const std::byte> cut(good.data(), good.size() - 9);
        auto read = SaveGame::parse(cut);
        REQUIRE_FALSE(read.has_value());
        CHECK(read.error().code == ErrorCode::Truncated);
    }
    SECTION("bytes after the trailer") {
        std::vector<std::byte> bytes = good;
        bytes.push_back(std::byte{0});
        CHECK_FALSE(SaveGame::parse(bytes).has_value());
    }
    SECTION("neither the magic nor a record") {
        CHECK_FALSE(SaveGame::parse(std::vector<std::byte>(40)).has_value());
        CHECK_FALSE(SaveGame::parse(std::vector<std::byte>{}).has_value());
        CHECK(SaveGame::nameOf(std::vector<std::byte>(3)).empty());
    }
    SECTION("a record of another version keeps its header's name") {
        ProfileRecord record;
        record.name = "CLEON";
        auto bytes = record.serialise();
        bytes[0] = std::byte{0x10};
        CHECK_FALSE(SaveGame::parse(bytes).has_value());
        CHECK(SaveGame::nameOf(bytes) == "CLEON");
    }
    SECTION("a missing section") {
        CHECK_FALSE(SaveGame::parse(withSection("bank", {}, true)).has_value());
        CHECK(SaveGame::nameOf(withSection("bank", {}, true)) == "AJAX");
    }
    SECTION("a section twice") {
        std::vector<RawSection> sections = sectionsOf(good);
        sections.push_back(sections[4]);
        CHECK_FALSE(SaveGame::parse(frame(SaveGame::kVersion, sections)).has_value());
    }
    SECTION("an unknown section in this version") {
        std::vector<RawSection> sections = sectionsOf(good);
        sections.push_back({.name = "checkpoint", .payload = {std::byte{2}}});
        CHECK_FALSE(SaveGame::parse(frame(SaveGame::kVersion, sections)).has_value());
    }
    SECTION("a section of the wrong length") {
        std::vector<std::byte> bank = payloadOf("bank");
        bank.push_back(std::byte{0});
        CHECK_FALSE(SaveGame::parse(withSection("bank", bank)).has_value());
    }
    SECTION("a flag that is not 0 or 1") {
        std::vector<std::byte> options = payloadOf("options");
        REQUIRE(options.size() == 27);
        options[17] = std::byte{2}; // P1's invert camera
        CHECK_FALSE(SaveGame::parse(withSection("options", options)).has_value());
    }
    SECTION("a name longer than eight bytes") {
        std::vector<std::byte> profile = payloadOf("profile");
        profile[0] = std::byte{9};
        CHECK_FALSE(SaveGame::parse(withSection("profile", profile)).has_value());
    }
    SECTION("a mod section whose name breaks the rules") {
        std::vector<RawSection> sections = sectionsOf(good);
        std::vector<std::byte> mod{std::byte{0}};
        mod.resize(1 + 32 + 4);
        sections.push_back({.name = "mod:Yard", .payload = mod});
        CHECK_FALSE(SaveGame::parse(frame(SaveGame::kVersion, sections)).has_value());
        sections.back().name = "mod:yard";
        CHECK(SaveGame::parse(frame(SaveGame::kVersion, sections)).has_value());
    }
}
