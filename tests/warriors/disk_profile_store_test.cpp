// SPDX-License-Identifier: GPL-3.0-or-later
// The disk-backed save system: one file per profile slot (docs/research/save.md#coney).
#include "warriors/disk_profile_store.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/name_hash.h"
#include "support/fixtures.h"
#include "warriors/save_game.h"

using coney::DiskProfileStore;
using coney::GameState;
using coney::Profile;
using coney::ProfileRecord;
using coney::SaveGame;
using coney::test::TempDir;

namespace {

// A profile as the STORY screens hand it over.
Profile chosen(std::string name, int difficulty = 1, int brightness = 40, bool subtitles = false) {
    Profile profile;
    profile.name = std::move(name);
    profile.difficulty = difficulty;
    profile.brightness = brightness;
    profile.subtitles = subtitles;
    return profile;
}

// The bytes of a file in the folder.
std::vector<std::byte> fileBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(chars.size());
    std::ranges::transform(chars, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
    return bytes;
}

} // namespace

TEST_CASE("an empty folder holds no profiles and has room", "[disk_profile_store]") {
    TempDir dir;
    GameState state;
    DiskProfileStore store(dir.path() / "profiles", state);
    CHECK(store.count() == 0);
    CHECK(store.freeSlot() == 0);
    CHECK(store.hasRoom());
    CHECK_FALSE(store.fourthDifficultyUnlocked());
    CHECK_FALSE(store.load(0));
    CHECK(store.save()); // nothing in use: nothing to do
    CHECK_FALSE(std::filesystem::exists(dir.path() / "profiles"));
}

TEST_CASE("a new profile is a fresh game with the screens' three choices, written at once", "[disk_profile_store]") {
    // Expected: save.md#story-screens, confirmed (code) (Profile_Create: the template, then the three choices)
    TempDir dir;
    GameState state;
    state.saved.bankedMoney = 500; // left over from an earlier game
    DiskProfileStore store(dir.path(), state);
    store.setInUse(true);
    REQUIRE(store.create(2, chosen("SWAN", 2, 45, true)));

    CHECK(store.loaded() == 2);
    CHECK(state.profileDifficulty == 2);
    CHECK(state.brightness == 45);
    CHECK(state.subtitles);
    CHECK(state.saved.bankedMoney == 0);
    CHECK(state.saved.isLocked(0));
    REQUIRE(SaveGame::parse(fileBytes(store.slotFile(2))).has_value()); // Coney's own format
    CHECK(store.freeSlot() == 0);
    CHECK(store.nameUsed("SWAN"));
    CHECK_FALSE(store.nameUsed("swan"));

    // A new store over the same folder reads it back.
    GameState other;
    DiskProfileStore reread(dir.path(), other);
    REQUIRE(reread.profile(2) != nullptr);
    CHECK(reread.profile(2)->name == "SWAN");
    CHECK(reread.profile(2)->difficulty == 2);
    CHECK(reread.profile(2)->brightness == 45);
    CHECK(reread.profile(2)->subtitles);
    CHECK_FALSE(reread.loaded().has_value());
    REQUIRE(reread.load(2));
    CHECK(other.profileDifficulty == 2);
    CHECK(other.subtitles);
}

TEST_CASE("an autosave keeps the banked money and the unlocks, not the checkpoint", "[disk_profile_store]") {
    TempDir dir;
    GameState state;
    DiskProfileStore store(dir.path(), state);
    REQUIRE(store.create(0, chosen("CLEON")));

    state.saved.addToBank(120);
    state.saved.setLocked(7, false);
    state.saved.setScriptFlag(3, true);
    state.checkPoint = 4;
    store.setInUse(false);
    REQUIRE(store.save()); // saving disabled: the file keeps the new profile
    {
        GameState fresh;
        DiskProfileStore reread(dir.path(), fresh);
        REQUIRE(reread.load(0));
        CHECK(fresh.saved.bankedMoney == 0);
    }
    store.setInUse(true);
    REQUIRE(store.save());

    GameState fresh;
    DiskProfileStore reread(dir.path(), fresh);
    REQUIRE(reread.load(0));
    CHECK(fresh.saved.bankedMoney == 120);
    CHECK_FALSE(fresh.saved.isLocked(7));
    CHECK(fresh.saved.scriptFlag(3));
    CHECK(fresh.checkPoint == 1); // not saved
}

TEST_CASE("six profiles fill the store; deleting one frees its slot and file", "[disk_profile_store]") {
    TempDir dir;
    GameState state;
    DiskProfileStore store(dir.path(), state);
    for (std::size_t slot = 0; slot < DiskProfileStore::kSlots; ++slot) {
        REQUIRE(store.create(slot, chosen("P" + std::to_string(slot))));
    }
    CHECK(store.count() == 6);
    CHECK_FALSE(store.freeSlot().has_value());
    CHECK_FALSE(store.hasRoom());

    store.remove(3);
    CHECK_FALSE(std::filesystem::exists(store.slotFile(3)));
    CHECK(store.freeSlot() == 3);
    store.remove(5); // the loaded one
    CHECK_FALSE(store.loaded().has_value());
    CHECK(store.count() == 4);
}

TEST_CASE("a file of another version is listed as a damaged profile", "[disk_profile_store]") {
    TempDir dir;
    ProfileRecord old;
    old.name = "AJAX";
    auto bytes = old.serialise();
    bytes[0] = std::byte{0x10};
    dir.write("profile-2.sav", bytes);
    dir.write("profile-4.sav", std::vector<std::byte>(3));

    GameState state;
    DiskProfileStore store(dir.path(), state);
    REQUIRE(store.profile(1) != nullptr);
    CHECK(store.profile(1)->damaged);
    CHECK(store.profile(1)->name == "AJAX");
    CHECK(store.record(1) == nullptr);
    CHECK_FALSE(store.load(1));
    REQUIRE(store.profile(3) != nullptr);
    CHECK(store.profile(3)->damaged);
    CHECK(store.profile(3)->name.empty());
    CHECK(store.freeSlot() == 0);

    store.remove(1);
    CHECK(store.profile(1) == nullptr);
}

TEST_CASE("finishing the story on HARDCORE SOLDIER unlocks the fourth difficulty", "[disk_profile_store]") {
    // Expected: save.md#slots, confirmed (code) (SaveSystem_MarkHardcoreDone, slot +0x28)
    TempDir dir;
    GameState state;
    DiskProfileStore store(dir.path(), state);
    store.setInUse(true);
    REQUIRE(store.create(0, chosen("VERMIN", 1)));
    store.noteStoryFinished();
    CHECK_FALSE(store.fourthDifficultyUnlocked()); // BOPPER does not earn it

    state.profileDifficulty = 2;
    store.noteStoryFinished();
    CHECK(store.fourthDifficultyUnlocked());
    REQUIRE(store.save());

    GameState fresh;
    DiskProfileStore reread(dir.path(), fresh);
    CHECK(reread.fourthDifficultyUnlocked());
}

TEST_CASE("a profile file of an earlier Coney loads and is rewritten in the new format at its next save",
          "[disk_profile_store]") {
    // Expected: unverified, #1179 (Coney's own versions, save.md#save-versions; version 0 is save.md#record)
    TempDir dir;
    ProfileRecord old;
    old.name = "SNOW";
    old.difficulty = 2;
    old.brightness = 55;
    old.progress.addToBank(410);
    old.progress.setLocked(12, false);
    old.progress.setScriptFlag(9, true);
    dir.write("profile-1.sav", old.serialise());

    GameState state;
    DiskProfileStore store(dir.path(), state);
    REQUIRE(store.profile(0) != nullptr);
    CHECK_FALSE(store.profile(0)->damaged);
    CHECK(store.profile(0)->name == "SNOW");
    REQUIRE(store.load(0));
    CHECK(state.profileDifficulty == 2);
    CHECK(state.brightness == 55);
    CHECK(state.saved.bankedMoney == 410);
    CHECK_FALSE(state.saved.isLocked(12));
    CHECK(state.saved.scriptFlag(9));
    // Reading never writes: the file stays the old record until a save.
    CHECK(std::filesystem::file_size(store.slotFile(0)) == ProfileRecord::kSize);

    store.setInUse(true);
    REQUIRE(store.save());
    auto rewritten = SaveGame::parse(fileBytes(store.slotFile(0)));
    REQUIRE(rewritten.has_value());
    CHECK(std::filesystem::file_size(store.slotFile(0)) != ProfileRecord::kSize);
    CHECK(rewritten->profile.serialise() == old.serialise());
}

TEST_CASE("a mod's section survives a save made without that mod", "[disk_profile_store]") {
    // Expected: unverified, #1179 (Coney's own sections, save.md#save-mods; mods.md#saves-replays)
    // The store's folder stands for `modded/`: a modded save loaded with no mod running is written back there.
    TempDir dir;
    SaveGame modded;
    modded.profile.name = "REMBRAND";
    modded.modded.story.push_back({.id = "yard-brawl.alley",
                                   .complete = true,
                                   .ranks = 3,
                                   .bestScore = 800,
                                   .total = 61,
                                   .grade = 2,
                                   .percent = 70});
    coney::ModSection mod{.name = "yard-brawl", .version = "0.3.0", .packageHash = {}, .data = {std::byte{5}}};
    mod.packageHash.fill(0x5a);
    modded.modded.mods.push_back(mod);
    dir.write("profile-3.sav", modded.serialise());

    GameState state;
    DiskProfileStore store(dir.path(), state);
    REQUIRE(store.load(2));
    REQUIRE(store.modded(2) != nullptr);
    CHECK(*store.modded(2) == modded.modded);
    // A session with no mod running plays on and autosaves.
    state.saved.addToBank(25);
    store.setInUse(true);
    REQUIRE(store.save());

    auto reread = SaveGame::parse(fileBytes(store.slotFile(2)));
    REQUIRE(reread.has_value());
    CHECK(reread->profile.progress.bankedMoney == 25);
    CHECK(reread->modded == modded.modded);
}

TEST_CASE("a new profile over a slot that held a modded save starts with nothing modded", "[disk_profile_store]") {
    // Expected: unverified, #1179 (save.md#save-mods: a new profile is a faithful one until a mod writes to it)
    TempDir dir;
    SaveGame modded;
    modded.profile.name = "MASAI";
    modded.modded.story.push_back({.id = "yard-brawl.alley", .complete = true});
    modded.modded.mods.push_back({.name = "yard-brawl", .version = "0.3.0", .packageHash = {}, .data = {}});
    dir.write("profile-2.sav", modded.serialise());

    GameState state;
    DiskProfileStore store(dir.path(), state);
    REQUIRE(store.modded(1) != nullptr);
    REQUIRE_FALSE(store.modded(1)->empty());
    store.setInUse(true);
    REQUIRE(store.create(1, chosen("RUBY")));

    // Neither the file nor the slot keeps the old profile's story entries or mod sections.
    auto written = SaveGame::parse(fileBytes(store.slotFile(1)));
    REQUIRE(written.has_value());
    CHECK(written->profile.name == "RUBY");
    CHECK(written->modded.empty());
    REQUIRE(store.modded(1) != nullptr);
    CHECK(store.modded(1)->empty());
}

TEST_CASE("a save made by a newer Coney is listed as damaged under its name", "[disk_profile_store]") {
    // Expected: unverified, #1179 (Coney's own rule, save.md#save-damaged; the slot +0x24 of save.md#slots)
    TempDir dir;
    SaveGame save;
    save.profile.name = "LUTHER";
    std::vector<std::byte> bytes = save.serialise();
    // Raise the version and seal the file again, as a later Coney would write it.
    bytes[8] = static_cast<std::byte>(SaveGame::kVersion + 1);
    bytes.resize(bytes.size() - 4);
    const std::uint32_t crc = coney::crc32(std::span<const std::byte>(bytes));
    for (int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::byte>(crc >> shift));
    }
    dir.write("profile-5.sav", bytes);

    GameState state;
    DiskProfileStore store(dir.path(), state);
    REQUIRE(store.profile(4) != nullptr);
    CHECK(store.profile(4)->damaged);
    CHECK(store.profile(4)->name == "LUTHER");
    CHECK_FALSE(store.load(4));
    CHECK(store.modded(4) == nullptr);
}
