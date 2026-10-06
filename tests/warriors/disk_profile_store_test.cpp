// SPDX-License-Identifier: GPL-3.0-or-later
// The disk-backed save system: one file per profile slot (docs/research/save.md#coney).
#include "warriors/disk_profile_store.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "support/fixtures.h"

using coney::DiskProfileStore;
using coney::GameState;
using coney::Profile;
using coney::ProfileRecord;
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
    REQUIRE(std::filesystem::file_size(store.slotFile(2)) == ProfileRecord::kSize);
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
