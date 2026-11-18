// SPDX-License-Identifier: GPL-3.0-or-later
// The memory-card importer: a profile from each kind of PS2 save into a new Coney save, on synthetic cards and
// profiles only (docs/research/save.md#import).
#include "warriors/save_import.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "fileio/ps2_memory_card.h"
#include "support/fixtures.h"
#include "support/optional_value.h"
#include "support/ps2_card_fixtures.h"
#include "warriors/disk_profile_store.h"
#include "warriors/game_state.h"

using coney::DiskProfileStore;
using coney::ErrorCode;
using coney::GameState;
using coney::ImportCandidate;
using coney::OriginalSave;
using coney::ProfileRecord;
using coney::SaveSourceKind;
using coney::SessionProfileStore;
using coney::test::Bytes;
using coney::test::CardFolder;
using coney::test::CardLayout;
using coney::test::TempDir;

namespace {

// A profile far from a fresh one in every part the issue names: unlocks, mission records, money and options.
ProfileRecord played(std::string name) {
    ProfileRecord record;
    record.name = std::move(name);
    record.hardcoreFinished = true;
    record.brightness = 65;
    record.subtitles = true;
    record.difficulty = 2;
    coney::SavedProgress& progress = record.progress;
    progress.missionBests[3] = {.bestScore = 1234, .total = 567, .grade = 2, .percent = 80, .rest = {0, 0}};
    progress.missionBests[29].bestScore = 99;
    progress.setLocked(0, false);
    progress.setLocked(17, false);
    progress.setLocked(639, false);
    progress.newBits[2] = 0x10;
    progress.rumbleData[5] = 7;
    progress.rumbleBits[1] = 3;
    progress.bankedMoney = 4321;
    progress.soundFxVolume = 0.5F;
    progress.musicVolume = 0.25F;
    progress.pads[1].invertCamera = true;
    progress.pads[0].vibration = false;
    progress.pads[1].autoAdjustCamera = false;
    progress.splitScreenMerge = false;
    progress.proLogic2 = true;
    progress.wideVideo = true;
    progress.setScriptFlag(5, true);
    progress.setScriptFlag(128, true);
    return record;
}

// The game's data file: six records, the slots given first, an empty slot being a fresh record with no name
// (docs/research/save.md#card-files).
std::vector<std::byte> dataFile(const std::vector<std::optional<ProfileRecord>>& slots) {
    Bytes bytes;
    for (std::size_t slot = 0; slot < coney::ProfileStore::kSlots; ++slot) {
        bytes.append((slot < slots.size() ? slots[slot] : std::nullopt).value_or(ProfileRecord{}).serialise());
    }
    return bytes.data();
}

// A data file with SNAKE in slot 0 and VERMIN in slot 3.
std::vector<std::byte> twoProfiles() {
    return dataFile({played("SNAKE"), std::nullopt, std::nullopt, played("VERMIN")});
}

// The save folder as the game writes it, its icons stood in by bytes of the right sizes.
CardFolder saveFolder(const std::vector<std::byte>& data) {
    return {std::string(coney::kPs2SaveName),
            {{"BASLUS-21215", data},
             {"WARR.ICO", std::vector<std::byte>(79128, std::byte{1})},
             {"icon.sys", std::vector<std::byte>(964, std::byte{2})}}};
}

// Writes `folder`'s files under `parent/<folder name>/`.
void writeFolder(const TempDir& dir, const std::string& parent, const CardFolder& folder) {
    for (const coney::test::CardFile& file : folder.files) {
        dir.write(parent + "/" + folder.name + "/" + file.name, file.data);
    }
}

// The same SNAKE and VERMIN save in each container the importer reads; returns its path.
std::filesystem::path writeContainer(const TempDir& dir, SaveSourceKind kind) {
    const std::vector<std::byte> data = twoProfiles();
    switch (kind) {
    case SaveSourceKind::Ps2CardImage:
        return dir.write("Mcd001.ps2", coney::test::buildPs2Card({{"BADATA-SYSTEM", {}}, saveFolder(data)}));
    case SaveSourceKind::Ps2FolderCard:
        dir.write("Mcd002.ps2/_pcsx2_superblock", std::vector<std::byte>(16, std::byte{0}));
        writeFolder(dir, "Mcd002.ps2", saveFolder(data));
        return dir.path() / "Mcd002.ps2";
    case SaveSourceKind::Ps2SaveFolder:
        writeFolder(dir, "usb", saveFolder(data));
        return dir.path() / "usb" / "BASLUS-21215";
    case SaveSourceKind::Ps2DataFile:
        return dir.write("BASLUS-21215", data);
    case SaveSourceKind::Ps2Psu:
        return dir.write("BASLUS-21215.psu", coney::test::buildPsu(saveFolder(data)));
    }
    return {};
}

// The bytes of a record, every field: two records are the same profile when these are equal.
std::vector<std::byte> fields(const ProfileRecord& record) {
    const auto bytes = record.serialise();
    return {bytes.begin(), bytes.end()};
}

} // namespace

TEST_CASE("a profile imported from each kind of PS2 save loads with the same unlocks, records, money and options",
          "[save_import]") {
    // Expected: save.md#import-containers, inferred (the containers); save.md#record, confirmed (code) (the fields);
    // save.md#import, the importer writes the record as a Coney save as the version-0 migration does
    const SaveSourceKind kind =
        GENERATE(SaveSourceKind::Ps2CardImage, SaveSourceKind::Ps2FolderCard, SaveSourceKind::Ps2SaveFolder,
                 SaveSourceKind::Ps2DataFile, SaveSourceKind::Ps2Psu);
    CAPTURE(coney::describe(kind));
    TempDir dir;
    const auto save = coney::readOriginalSave(writeContainer(dir, kind));
    REQUIRE(save.has_value());
    CHECK(save->kind == kind);
    REQUIRE(save->profiles.size() == 2);
    CHECK(save->profiles[0].slot == 0);
    CHECK(save->profiles[1].slot == 3);
    CHECK(save->skipped.empty());

    GameState state;
    {
        DiskProfileStore store(dir.path() / "profiles", state);
        for (const ImportCandidate& candidate : save->profiles) {
            REQUIRE(coney::importProfile(candidate.record, store).has_value());
        }
        CHECK_FALSE(store.loaded().has_value()); // imported, not loaded
    }
    // A later run reads the imported files back as Coney saves and loads one into the game.
    DiskProfileStore again(dir.path() / "profiles", state);
    REQUIRE(again.count() == 2);
    REQUIRE(again.record(1) != nullptr);
    CHECK(again.record(1)->name == "VERMIN");
    CHECK(fields(*again.record(1)) == fields(played("VERMIN")));
    REQUIRE(again.load(1));
    CHECK(state.saved.bankedMoney == 4321);
    CHECK_FALSE(state.saved.isLocked(17));
    CHECK(state.saved.isLocked(18));
    CHECK(state.saved.missionBests[3].bestScore == 1234);
    CHECK(state.saved.musicVolume == 0.25F);
    CHECK(state.saved.pads[1].invertCamera);
    CHECK(state.saved.scriptFlag(128));
    CHECK(state.profileDifficulty == 2);
    CHECK(state.brightness == 65);
    CHECK(state.subtitles);
    CHECK(again.fourthDifficultyUnlocked());
    REQUIRE(again.modded(1) != nullptr);
    CHECK(again.modded(1)->empty()); // nothing modded, as the version-0 migration makes it
}

TEST_CASE("the data file's empty slots are skipped and a slot of another version is named", "[save_import]") {
    // Expected: save.md#import, confirmed (code) (ProfileHeader_Read at 0x004219d0: a slot is used when its name's
    // first byte is not 0, and a version other than 0x11 is not loaded)
    auto bytes = dataFile({std::nullopt, played("SNAKE"), played("COCHISE")});
    bytes[2 * ProfileRecord::kSize] = std::byte{0x10}; // slot 2's version
    std::vector<std::string> skipped;
    const auto profiles = coney::readPs2DataFile(bytes, skipped);
    REQUIRE(profiles.has_value());
    REQUIRE(profiles->size() == 1);
    CHECK(profiles->front().slot == 1);
    CHECK(profiles->front().record.name == "SNAKE");
    REQUIRE(skipped.size() == 1);
    CHECK(skipped.front().find("slot 3") != std::string::npos);
    CHECK(skipped.front().find("0x10") != std::string::npos);
}

TEST_CASE("a damaged or foreign save is refused with a message naming the problem", "[save_import]") {
    // Expected: save.md#import, confirmed (code) (PS2Save_CheckFileSizes at 0x0041fcc8: any other data file size is
    // corrupt); save.md#import-containers, inferred (the containers)
    std::optional<std::vector<std::byte>> bytes;
    std::string problem;
    SECTION("a data file of the wrong size") {
        auto data = twoProfiles();
        data.resize(7000);
        bytes = data;
        problem = "7,704";
    }
    SECTION("no profile in the save") {
        bytes = dataFile({});
        problem = "no profile";
    }
    SECTION("every profile of another version") {
        auto data = dataFile({played("SNAKE")});
        data[0] = std::byte{0x12};
        bytes = data;
        problem = "0x12";
    }
    SECTION("a card with another game's save only") {
        bytes = coney::test::buildPs2Card({{"BESLES-00000", {{"DATA", std::vector<std::byte>(10)}}}});
        problem = "BESLES-00000";
    }
    SECTION("a card with a damaged chain") {
        CardLayout layout;
        layout.spares = false;
        auto card = coney::test::buildPs2Card({saveFolder(twoProfiles())}, layout);
        // The data file's second cluster (allocatable 3: the root takes 0-1, the file starts at 2) marked free.
        for (std::size_t i = 0; i < 4; ++i) {
            card[2048 + 3 * 4 + i] = i == 3 ? std::byte{0x7f} : std::byte{0xff};
        }
        bytes = card;
        problem = "ends after";
    }
    SECTION("a PSU export of another game") {
        bytes = coney::test::buildPsu({"BESLES-00000", {{"DATA", std::vector<std::byte>(10)}}});
        problem = "BESLES-00000";
    }
    SECTION("an unformatted card, as PCSX2 makes a new card file") {
        bytes = std::vector<std::byte>(8650752, std::byte{0xff});
        problem = "not formatted";
    }
    SECTION("something else") {
        bytes = std::vector<std::byte>(4096, std::byte{0x33});
        problem = "not a PS2 save";
    }
    REQUIRE(bytes.has_value());
    const auto save = coney::readOriginalSave(coney::test::got(bytes), "card");
    REQUIRE_FALSE(save.has_value());
    CHECK(save.error().code == ErrorCode::Invalid);
    CAPTURE(save.error().message);
    CHECK(save.error().message.find(problem) != std::string::npos);
}

TEST_CASE("a folder with no save of the game, or a missing path, says so", "[save_import]") {
    // Expected: save.md#import-containers, inferred
    TempDir dir;
    dir.write("Mcd003.ps2/_pcsx2_superblock", std::vector<std::byte>(16));
    dir.write("Mcd003.ps2/BESLES-00000/DATA", std::vector<std::byte>(10));
    const auto folder = coney::readOriginalSave(dir.path() / "Mcd003.ps2");
    REQUIRE_FALSE(folder.has_value());
    CHECK(folder.error().code == ErrorCode::NotFound);
    CHECK(folder.error().message.find("BASLUS-21215") != std::string::npos);
    const auto missing = coney::readOriginalSave(dir.path() / "nothing.ps2");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::NotFound);
}

TEST_CASE("an import never gives two profiles one name and stops when every slot is used", "[save_import]") {
    // Expected: frontend.md#pm-screens, confirmed (code) (PM_Create refuses a name a slot holds, case-sensitively);
    // save.md#import (the importer keeps the rule)
    TempDir dir;
    GameState state;
    DiskProfileStore store(dir.path(), state);
    REQUIRE(coney::importProfile(played("SNAKE"), store) == 0);
    const auto again = coney::importProfile(played("SNAKE"), store);
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error().code == ErrorCode::Invalid);
    CHECK(again.error().message.find("SNAKE") != std::string::npos);
    CHECK(coney::importProfile(played("snake"), store) == 1); // strcmp: another name
    for (const char* name : {"A", "B", "C", "D"}) {
        REQUIRE(coney::importProfile(played(name), store).has_value());
    }
    const auto full = coney::importProfile(played("E"), store);
    REQUIRE_FALSE(full.has_value());
    CHECK(full.error().code == ErrorCode::NotFound);
}

TEST_CASE("the run's own profile store takes an imported profile too", "[save_import]") {
    // Expected: save.md#import
    SessionProfileStore store;
    REQUIRE(coney::importProfile(played("SNAKE"), store) == 0);
    REQUIRE(store.profile(0) != nullptr);
    CHECK(store.profile(0)->name == "SNAKE");
    CHECK(store.profile(0)->difficulty == 2);
    CHECK_FALSE(store.importFolder().has_value());
}

TEST_CASE("an import folder lists the saves it holds and why the rest cannot be read", "[save_import]") {
    // Expected: save.md#import
    TempDir dir;
    writeContainer(dir, SaveSourceKind::Ps2CardImage);
    writeContainer(dir, SaveSourceKind::Ps2Psu);
    dir.write("notes.txt", std::vector<std::byte>(20, std::byte{'x'}));
    const coney::ImportScan scan = coney::scanImportFolder(dir.path());
    REQUIRE(scan.saves.size() == 2);
    CHECK(scan.saves[0].source == "BASLUS-21215.psu"); // name order
    CHECK(scan.saves[1].source == "Mcd001.ps2");
    CHECK(scan.profileCount() == 4);
    REQUIRE(scan.problems.size() == 1);
    CHECK(scan.problems.front().find("notes.txt") != std::string::npos);
    CHECK(coney::scanImportFolder(dir.path() / "absent").saves.empty());
}

TEST_CASE("the disk store offers an import folder beside the profiles", "[save_import]") {
    // Expected: save.md#import
    TempDir dir;
    GameState state;
    DiskProfileStore store(dir.path(), state);
    REQUIRE(store.importFolder().has_value());
    CHECK(store.importFolder() == dir.path() / "import");
}

TEST_CASE("a PSU export whose last file's padding is left out is read", "[save_import]") {
    // Expected: save.md#import-containers, inferred (some tools leave the last file's padding out)
    auto psu = coney::test::buildPsu(saveFolder(twoProfiles()));
    psu.resize(psu.size() - (1024 - 964)); // icon.sys, 964 bytes, is the last file
    CHECK(coney::io::looksLikePsu(psu));
    const auto save = coney::readOriginalSave(psu, "short.psu");
    REQUIRE(save.has_value());
    CHECK(save->kind == SaveSourceKind::Ps2Psu);
    CHECK(save->profiles.size() == 2);
}

TEST_CASE("a file larger than any PS2 save is refused without being read, in the import folder too", "[save_import]") {
    // Expected: save.md#import (Coney's limit: a 64 MB PCSX2 card with its pages' spares, the largest card PCSX2 makes)
    CHECK(coney::kLargestOriginalSave == 69'206'016);
    TempDir dir;
    const std::filesystem::path big = dir.write("disc.iso", std::vector<std::byte>(16, std::byte{0}));
    std::filesystem::resize_file(big, coney::kLargestOriginalSave + 1);
    const auto save = coney::readOriginalSave(big);
    REQUIRE_FALSE(save.has_value());
    CHECK(save.error().message.starts_with("disc.iso: too large to be a PS2 save"));
    const coney::ImportScan scan = coney::scanImportFolder(dir.path());
    CHECK(scan.saves.empty());
    REQUIRE(scan.problems.size() == 1);
    CHECK(scan.problems.front().starts_with("disc.iso: too large to be a PS2 save"));
}
