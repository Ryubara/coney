// SPDX-License-Identifier: GPL-3.0-or-later
// The 1,284-byte profile record (docs/research/save.md#record).
#include "warriors/profile_record.h"

#include <bit>
#include <cstdint>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using coney::ErrorCode;
using coney::ProfileRecord;
using coney::SavedProgress;

namespace {

// The little-endian 32-bit value at `at` in a serialised record.
std::uint32_t wordAt(std::span<const std::byte> bytes, std::size_t at) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes[at + i]) << (8 * i);
    }
    return value;
}

// Overwrites the little-endian 32-bit value at `at`.
void putWord(std::span<std::byte> bytes, std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[at + i] = static_cast<std::byte>(value >> (8 * i));
    }
}

} // namespace

TEST_CASE("a new profile's record holds the defaults at the page's offsets", "[profile_record]") {
    ProfileRecord record;
    record.name = "WARRIOR";
    const auto bytes = record.serialise();
    REQUIRE(bytes.size() == 1284);

    CHECK(wordAt(bytes, 0x000) == 0x11);
    CHECK(static_cast<char>(bytes[0x004]) == 'W');
    CHECK(bytes[0x00b] == std::byte{0}); // seven characters, zero-padded
    CHECK(wordAt(bytes, 0x00c) == 0);
    CHECK(wordAt(bytes, 0x010) == 0);       // mission bests zero
    CHECK(bytes[0x178] == std::byte{0xff}); // every unlockable locked
    CHECK(bytes[0x1c7] == std::byte{0xff});
    CHECK(bytes[0x1c8] == std::byte{0}); // no "new" marks
    CHECK(wordAt(bytes, 0x4ac) == 0);    // banked money
    CHECK(wordAt(bytes, 0x4b0) == 0);
    CHECK(wordAt(bytes, 0x4b4) == 40);                         // brightness
    CHECK(std::bit_cast<float>(wordAt(bytes, 0x4b8)) == 0.9F); // SoundFX
    CHECK(std::bit_cast<float>(wordAt(bytes, 0x4bc)) == 0.9F); // Music
    CHECK(wordAt(bytes, 0x4c0) == 0);
    for (std::size_t pad = 0; pad < 2; ++pad) {
        CHECK(wordAt(bytes, 0x4c4 + pad * 12) == 0); // invert off
        CHECK(wordAt(bytes, 0x4c8 + pad * 12) == 1); // auto-adjust on
        CHECK(wordAt(bytes, 0x4cc + pad * 12) == 1); // vibration on
    }
    CHECK(wordAt(bytes, 0x4dc) == 1); // merge on
    CHECK(wordAt(bytes, 0x4e0) == 0); // subtitles off
    CHECK(wordAt(bytes, 0x4e4) == 0); // Pro Logic II off
    CHECK(wordAt(bytes, 0x4e8) == 0); // Normal video
    CHECK(wordAt(bytes, 0x4ec) == 1); // BOPPER
    CHECK(wordAt(bytes, 0x4f0) == 0);
    CHECK(wordAt(bytes, 0x4f4) == 0); // script flags clear
}

TEST_CASE("a record survives a write and a read with every field changed", "[profile_record]") {
    ProfileRecord record;
    record.name = "ABCDEFGH"; // eight characters: no terminator
    record.hardcoreFinished = true;
    record.brightness = 65;
    record.subtitles = true;
    record.difficulty = 3;
    SavedProgress& p = record.progress;
    p.missionBests[4] = {.bestScore = 1234, .total = 99, .grade = 2, .percent = 87, .rest = {5, 6}};
    p.setLocked(0, false);
    p.setLocked(639, false);
    p.newBits[3] = 0x81;
    p.rumbleData[0x253] = 0x42;
    p.rumbleBits[0] = 0x24;
    p.addToBank(300);
    p.addToBank(25);
    p.optionWord0 = 7;
    p.soundFxVolume = 0.5F;
    p.musicVolume = 0.25F;
    p.optionWord10 = 9;
    p.pads[1] = {.invertCamera = true, .autoAdjustCamera = false, .vibration = false};
    p.splitScreenMerge = false;
    p.proLogic2 = true;
    p.wideVideo = true;
    p.stateWord0 = 11;
    p.setScriptFlag(1, true);
    p.setScriptFlag(128, true);

    const auto bytes = record.serialise();
    CHECK(static_cast<char>(bytes[0x00b]) == 'H');
    auto read = ProfileRecord::parse(bytes);
    REQUIRE(read.has_value());
    CHECK(read->name == "ABCDEFGH");
    CHECK(read->hardcoreFinished);
    CHECK(read->brightness == 65);
    CHECK(read->subtitles);
    CHECK(read->difficulty == 3);
    const SavedProgress& q = read->progress;
    CHECK(q.missionBests[4].bestScore == 1234);
    CHECK(q.missionBests[4].total == 99);
    CHECK(q.missionBests[4].grade == 2);
    CHECK(q.missionBests[4].percent == 87);
    CHECK(q.missionBests[4].rest[1] == 6);
    CHECK_FALSE(q.isLocked(0));
    CHECK(q.isLocked(1));
    CHECK_FALSE(q.isLocked(639));
    CHECK(q.isLocked(640)); // out of range reads as locked
    CHECK(q.newBits[3] == 0x81);
    CHECK(q.rumbleData[0x253] == 0x42);
    CHECK(q.rumbleBits[0] == 0x24);
    CHECK(q.bankedMoney == 325);
    CHECK(q.optionWord0 == 7);
    CHECK(q.soundFxVolume == 0.5F);
    CHECK(q.musicVolume == 0.25F);
    CHECK(q.optionWord10 == 9);
    CHECK_FALSE(q.pads[0].invertCamera);
    CHECK(q.pads[1].invertCamera);
    CHECK_FALSE(q.pads[1].autoAdjustCamera);
    CHECK_FALSE(q.pads[1].vibration);
    CHECK_FALSE(q.splitScreenMerge);
    CHECK(q.proLogic2);
    CHECK(q.wideVideo);
    CHECK(q.stateWord0 == 11);
    CHECK(q.scriptFlag(1));
    CHECK_FALSE(q.scriptFlag(2));
    CHECK(q.scriptFlag(128));
    CHECK_FALSE(q.scriptFlag(0));
    CHECK(read->serialise() == bytes);
}

TEST_CASE("the unlock and script-flag bits sit where the page puts them", "[profile_record]") {
    ProfileRecord record;
    record.progress.setLocked(9, false);     // byte 1, bit 1
    record.progress.setScriptFlag(10, true); // flag 10 -> bit 9: byte 1, bit 1
    const auto bytes = record.serialise();
    CHECK(bytes[0x178 + 1] == std::byte{0xfd});
    CHECK(bytes[0x4f4 + 1] == std::byte{0x02});
}

TEST_CASE("a record of another version or size is refused", "[profile_record]") {
    ProfileRecord record;
    record.name = "OLD";
    auto bytes = record.serialise();

    SECTION("another version") {
        putWord(bytes, 0, 0x10);
        auto read = ProfileRecord::parse(bytes);
        REQUIRE_FALSE(read.has_value());
        CHECK(read.error().code == ErrorCode::Invalid);
        // The header's name is still readable, so the profile can be listed as not loadable.
        auto name = ProfileRecord::headerName(bytes);
        REQUIRE(name.has_value());
        CHECK(*name == "OLD");
    }
    SECTION("too short") {
        auto read = ProfileRecord::parse(std::span<const std::byte>(bytes).first(1000));
        REQUIRE_FALSE(read.has_value());
        CHECK(read.error().code == ErrorCode::Truncated);
        CHECK_FALSE(ProfileRecord::headerName(std::span<const std::byte>(bytes).first(15)).has_value());
    }
    SECTION("too long") {
        std::vector<std::byte> longer(bytes.begin(), bytes.end());
        longer.push_back(std::byte{0});
        auto read = ProfileRecord::parse(longer);
        REQUIRE_FALSE(read.has_value());
        CHECK(read.error().code == ErrorCode::Invalid);
    }
}
