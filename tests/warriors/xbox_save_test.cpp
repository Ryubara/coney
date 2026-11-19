// SPDX-License-Identifier: GPL-3.0-or-later
// The Xbox save importer: the Xbox build's profile record, its save folders and the disk images that hold them, into
// new Coney saves, on synthetic saves and images only (docs/research/save.md#xbox-import).
#include "warriors/xbox_save.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "fileio/fatx_volume.h"
#include "support/fixtures.h"
#include "support/xbox_save_fixtures.h"
#include "warriors/disk_profile_store.h"
#include "warriors/game_state.h"
#include "warriors/save_import.h"

using coney::DiskProfileStore;
using coney::ErrorCode;
using coney::GameState;
using coney::ImportCandidate;
using coney::OriginalSave;
using coney::ProfileRecord;
using coney::SaveSourceKind;
using coney::test::FatxFile;
using coney::test::TempDir;
using coney::test::XboxRumble;

namespace {

// A profile far from a fresh one in every part the issue names: unlocks, mission records, money and options. Its
// Video setting stays Normal and its Rumble bytes empty: the Xbox record has no Video setting, and its Rumble store
// is laid out another way (checked on its own below).
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
    progress.rumbleBits[1] = 3;
    progress.bankedMoney = 4321;
    progress.optionWord0 = 6;
    progress.soundFxVolume = 0.5F;
    progress.musicVolume = 0.25F;
    progress.optionWord10 = 9;
    progress.pads[1].invertCamera = true;
    progress.pads[0].vibration = false;
    progress.pads[1].autoAdjustCamera = false;
    progress.splitScreenMerge = false;
    progress.proLogic2 = true;
    progress.stateWord0 = 77;
    progress.setScriptFlag(5, true);
    progress.setScriptFlag(128, true);
    return record;
}

// The bytes of a record, every field: two records are the same profile when these are equal.
std::vector<std::byte> fields(const ProfileRecord& record) {
    const auto bytes = record.serialise();
    return {bytes.begin(), bytes.end()};
}

// One save folder's files as the game writes them, on a FATX volume or a disk: the data file, the dashboard's
// SaveMeta.xbx and its SaveImage.xbx, under `prefix` (the title folder's path, with a trailing slash).
std::vector<FatxFile> saveFiles(const std::string& prefix, const std::string& folder, const ProfileRecord& record) {
    return {{prefix + folder + "/SaveMeta.xbx", coney::test::saveMeta(record.name)},
            {prefix + folder + "/profile.dat", coney::test::xboxDataFile(coney::test::xboxRecord(record))},
            {prefix + folder + "/SaveImage.xbx", std::vector<std::byte>(4096, std::byte{3})}};
}

// SNAKE and VERMIN, each in its own save folder of the title folder `UDATA/5454009b/`, beside the title's own files.
std::vector<FatxFile> twoSaves() {
    std::vector<FatxFile> files{{"UDATA/5454009b/TitleMeta.xbx", std::vector<std::byte>(50, std::byte{1})}};
    for (const FatxFile& file : saveFiles("UDATA/5454009b/", "0A1B2C3D4E5F", played("SNAKE"))) {
        files.push_back(file);
    }
    for (const FatxFile& file : saveFiles("UDATA/5454009b/", "9F8E7D6C5B4A", played("VERMIN"))) {
        files.push_back(file);
    }
    files.push_back({"UDATA/4d530051/ABCDEF012345/data", std::vector<std::byte>(10)}); // another game's save
    return files;
}

constexpr std::uint64_t kDiskBytes = std::uint64_t{8} << 30; // xemu's 8 GB disk

// Writes `files` (paths from E:'s root) under `root` in `dir`.
void writeFiles(const TempDir& dir, const std::string& root, const std::vector<FatxFile>& files) {
    for (const FatxFile& file : files) {
        dir.write(root + file.path, file.data);
    }
}

// The same two saves in each Xbox container the importer reads from a path; returns that path.
std::filesystem::path writeContainer(const TempDir& dir, SaveSourceKind kind) {
    switch (kind) {
    case SaveSourceKind::XboxSaveFolder:
        writeFiles(dir, "ftp/E/", twoSaves());
        return dir.path() / "ftp" / "E" / "UDATA" / "5454009b";
    case SaveSourceKind::XboxQcow2Image: {
        const auto volume = coney::test::buildFatx(coney::io::kXboxDataPartitionSize, twoSaves());
        return dir.write(
            "xbox_hdd.qcow2",
            coney::test::buildQcow2(kDiskBytes, coney::test::shifted(volume, coney::io::kXboxDataPartitionOffset)));
    }
    case SaveSourceKind::XboxFatxImage:
        return dir.write("memory-unit.bin", coney::test::flatten(coney::test::buildFatx(8 << 20, twoSaves()), 8 << 20));
    default:
        break;
    }
    FAIL("no such container in this test");
    return {};
}

} // namespace

TEST_CASE("the Xbox profile record becomes the PS2 record field for field", "[xbox_save]") {
    // Expected: save.md#xbox-record, confirmed (code) (Profile_Write at Xbox 0x0033bae0, Profile_Read at Xbox
    // 0x0033bcf0: the PS2's fields in the same order, the options one byte each, no Video setting)
    const ProfileRecord record = played("SNAKE");
    const auto parsed = coney::parseXboxRecord(coney::test::xboxRecord(record));
    REQUIRE(parsed.has_value());
    CHECK(parsed->name == "SNAKE");
    CHECK(fields(*parsed) == fields(record));

    const auto eight = coney::parseXboxRecord(coney::test::xboxRecord(played("ABCDEFGH")));
    REQUIRE(eight.has_value());
    CHECK(eight->name == "ABCDEFGH"); // eight characters, no terminator
}

TEST_CASE("the Xbox Rumble store's gangs move to the PS2 store's places", "[xbox_save]") {
    // Expected: save.md#xbox-record, confirmed (code) (the Xbox store's gang functions at Xbox 0x0012c330-0x0012c5c0:
    // 0x30-byte gangs, an in-use byte, the name at +1, the members at +0x0c; current gang +0x220, member +0x224,
    // changed +0x228); frontend.md#rumble-gangs, confirmed (code) (the PS2 store's 0x34-byte gangs)
    XboxRumble rumble;
    rumble.owned[0] = 0x81;
    rumble.owned[63] = 0x40;
    rumble.gangs[0] = {true, "ORPHANS", {1, 2, 3, 4, 5, 6, 7, 8, 9}};
    rumble.gangs[9] = {true, "TENLETTERS", {0x160, 12, 0x160, 0x160, 0x160, 0x160, 0x160, 0x160, 300}};
    rumble.currentGang = 9;
    rumble.currentMember = 1;
    rumble.changed = true;
    const auto parsed = coney::parseXboxRecord(coney::test::xboxRecord(played("SNAKE"), rumble));
    REQUIRE(parsed.has_value());
    const auto& ps2 = parsed->progress.rumbleData;
    // Reads the PS2 store's little-endian word at `offset`.
    auto word = [&ps2](std::size_t offset) {
        return static_cast<std::uint32_t>(ps2[offset]) | (static_cast<std::uint32_t>(ps2[offset + 1]) << 8U) |
               (static_cast<std::uint32_t>(ps2[offset + 2]) << 16U) |
               (static_cast<std::uint32_t>(ps2[offset + 3]) << 24U);
    };
    CHECK(ps2[0] == 0x81);
    CHECK(ps2[63] == 0x40);
    CHECK(word(0x40) == 1); // gang 0 in use
    CHECK(std::string(reinterpret_cast<const char*>(&ps2[0x44]), 7) == "ORPHANS");
    CHECK(ps2[0x44 + 7] == 0);
    for (std::size_t i = 0; i < 9; ++i) {
        CHECK(word(0x50 + 4 * i) == i + 1);
    }
    CHECK(word(0x40 + 0x34) == 0); // gang 1 not in use
    const std::size_t nine = 0x40 + 9 * 0x34;
    CHECK(word(nine) == 1);
    CHECK(std::string(reinterpret_cast<const char*>(&ps2[nine + 4]), 10) == "TENLETTERS");
    CHECK(ps2[nine + 0x0e] == 0);
    CHECK(word(nine + 0x10 + 4) == 12);
    CHECK(word(nine + 0x10 + 32) == 300);
    CHECK(word(0x248) == 9);
    CHECK(word(0x24c) == 1);
    CHECK(word(0x250) == 1);
}

TEST_CASE("an Xbox record of the wrong size or version is refused", "[xbox_save]") {
    // Expected: save.md#xbox-record, confirmed (code) (ProfileHeader_Read at Xbox 0x0033bc60 loads only version 0x11)
    auto bytes = coney::test::xboxRecord(played("SNAKE"));
    bytes[0] = std::byte{0x12};
    const auto version = coney::parseXboxRecord(bytes);
    REQUIRE_FALSE(version.has_value());
    CHECK(version.error().message.find("0x12") != std::string::npos);
    bytes.resize(1000);
    CHECK_FALSE(coney::parseXboxRecord(bytes).has_value());
}

TEST_CASE("a SaveMeta.xbx names its save", "[xbox_save]") {
    // Expected: save.md#xbox-save, confirmed (runtime), run r-20261119-xbox-profile-save-signature (UTF-16 with a
    // byte-order mark, a Name= line)
    CHECK(coney::xboxSaveName(coney::test::saveMeta("SNAKE")) == "SNAKE");
    CHECK_FALSE(coney::xboxSaveName(std::vector<std::byte>(10, std::byte{0})).has_value());
}

TEST_CASE("an Xbox save the game would not load is left out and named", "[xbox_save]") {
    // Expected: save.md#xbox-save, confirmed (code) (the load at Xbox 0x0033efe0 and 0x0033eda0: the data file must be
    // the record and its 20-byte signature; ProfileHeader_Read at Xbox 0x0033bc60: version 0x11, used when the name's
    // first byte is not 0)
    std::vector<coney::XboxSaveFiles> folders;
    folders.push_back(
        {"A", coney::test::xboxDataFile(coney::test::xboxRecord(played("SNAKE"))), coney::test::saveMeta("SNAKE")});
    auto shortFile = coney::test::xboxDataFile(coney::test::xboxRecord(played("AJAX")));
    shortFile.resize(1229);
    folders.push_back({"B", shortFile, coney::test::saveMeta("AJAX")});
    auto otherVersion = coney::test::xboxDataFile(coney::test::xboxRecord(played("COCHISE")));
    otherVersion[0] = std::byte{0x10};
    folders.push_back({"C", otherVersion, coney::test::saveMeta("COCHISE")});
    folders.push_back(
        {"D", coney::test::xboxDataFile(coney::test::xboxRecord(played(""))), coney::test::saveMeta("X")});
    folders.push_back({"E", std::nullopt, coney::test::saveMeta("REMBRANDT")});
    std::vector<std::string> skipped;
    const std::vector<ImportCandidate> profiles = coney::readXboxSaveFolders(folders, skipped);
    REQUIRE(profiles.size() == 1);
    CHECK(profiles[0].record.name == "SNAKE");
    CHECK(profiles[0].slot == 0);
    REQUIRE(skipped.size() == 4);
    CHECK(skipped[0].find("AJAX") != std::string::npos);
    CHECK(skipped[0].find("1,230") != std::string::npos);
    CHECK(skipped[1].find("0x10") != std::string::npos);
    CHECK(skipped[2].find("no name") != std::string::npos);
    CHECK(skipped[3].find("REMBRANDT") != std::string::npos);
}

TEST_CASE("a profile imported from each kind of Xbox save loads with the same unlocks, records, money and options",
          "[xbox_save]") {
    // Expected: save.md#xbox-containers, confirmed (runtime), run r-20261119-xbox-hdd-fatx-layout (xemu's qcow2 disk,
    // the data partition's FATX, UDATA/5454009b); save.md#xbox-save, confirmed (runtime), run
    // r-20261119-xbox-profile-save-signature (one profile.dat of 1,230 bytes per save folder); save.md#xbox-record,
    // confirmed (code) (the fields)
    const SaveSourceKind kind =
        GENERATE(SaveSourceKind::XboxSaveFolder, SaveSourceKind::XboxQcow2Image, SaveSourceKind::XboxFatxImage);
    CAPTURE(coney::describe(kind));
    TempDir dir;
    const auto save = coney::readOriginalSave(writeContainer(dir, kind));
    REQUIRE(save.has_value());
    CHECK(save->kind == kind);
    REQUIRE(save->profiles.size() == 2);
    CHECK(coney::placeOf(*save, save->profiles[0]) == "save folder 0A1B2C3D4E5F");
    CHECK(coney::placeOf(*save, save->profiles[1]) == "save folder 9F8E7D6C5B4A");
    CHECK(save->skipped.empty());

    GameState state;
    {
        DiskProfileStore store(dir.path() / "profiles", state);
        for (const ImportCandidate& candidate : save->profiles) {
            REQUIRE(coney::importProfile(candidate.record, store).has_value());
        }
    }
    DiskProfileStore again(dir.path() / "profiles", state);
    REQUIRE(again.count() == 2);
    REQUIRE(again.record(1) != nullptr);
    CHECK(fields(*again.record(1)) == fields(played("VERMIN")));
    REQUIRE(again.load(1));
    CHECK(state.saved.bankedMoney == 4321);
    CHECK_FALSE(state.saved.isLocked(17));
    CHECK(state.saved.isLocked(18));
    CHECK(state.saved.missionBests[3].bestScore == 1234);
    CHECK(state.saved.musicVolume == 0.25F);
    CHECK(state.saved.pads[1].invertCamera);
    CHECK_FALSE(state.saved.wideVideo);
    CHECK(state.saved.scriptFlag(128));
    CHECK(state.profileDifficulty == 2);
    CHECK(state.brightness == 65);
    CHECK(state.subtitles);
    CHECK(again.fourthDifficultyUnlocked());
}

TEST_CASE("an Xbox save is found in a save folder, the title folder, a copy of E: or as profile.dat alone",
          "[xbox_save]") {
    // Expected: save.md#xbox-containers, confirmed (runtime), run r-20261119-xbox-profile-save-signature (the save
    // folder under UDATA/5454009b holds SaveMeta.xbx and profile.dat)
    TempDir dir;
    writeFiles(dir, "E/", twoSaves());
    for (const std::filesystem::path& path :
         {dir.path() / "E", dir.path() / "E" / "UDATA", dir.path() / "E" / "UDATA" / "5454009b"}) {
        CAPTURE(path.string());
        const auto save = coney::readOriginalSave(path);
        REQUIRE(save.has_value());
        CHECK(save->kind == SaveSourceKind::XboxSaveFolder);
        CHECK(save->profiles.size() == 2);
    }
    const auto one = coney::readOriginalSave(dir.path() / "E" / "UDATA" / "5454009b" / "9F8E7D6C5B4A");
    REQUIRE(one.has_value());
    REQUIRE(one->profiles.size() == 1);
    CHECK(one->profiles[0].record.name == "VERMIN");
    const auto alone =
        coney::readOriginalSave(dir.path() / "E" / "UDATA" / "5454009b" / "9F8E7D6C5B4A" / "profile.dat");
    REQUIRE(alone.has_value());
    CHECK(alone->kind == SaveSourceKind::XboxDataFile);
    REQUIRE(alone->profiles.size() == 1);
    CHECK(alone->profiles[0].record.name == "VERMIN");
}

TEST_CASE("an image of a whole Xbox hard disk is read at its data partition", "[xbox_save]") {
    // Expected: save.md#xbox-containers, confirmed (runtime), run r-20261119-xbox-hdd-fatx-layout (the data partition
    // at 0xabe80000, 0x131f00000 bytes)
    const auto volume = coney::test::buildFatx(coney::io::kXboxDataPartitionSize, twoSaves());
    coney::test::SparseStream disk(coney::test::shifted(volume, coney::io::kXboxDataPartitionOffset), kDiskBytes);
    const auto save = coney::readXboxImage(disk, "hdd.img");
    REQUIRE(save.has_value());
    REQUIRE(save->has_value());
    CHECK((*save)->kind == SaveSourceKind::XboxHddImage);
    CHECK((*save)->profiles.size() == 2);

    std::vector<std::byte> nothing(4096, std::byte{0});
    coney::io::MemoryStream other(nothing);
    CHECK_FALSE(coney::readXboxImage(other, "other.bin").has_value());
}

TEST_CASE("an Xbox image or folder with no save of the game says what it holds", "[xbox_save]") {
    // Expected: save.md#xbox-import (the importer's refusals)
    TempDir dir;
    const auto volume =
        coney::test::buildFatx(8 << 20, {{"UDATA/4d530051/ABCDEF012345/data", std::vector<std::byte>(10)}});
    const auto image = coney::readOriginalSave(dir.write("mu.bin", coney::test::flatten(volume, 8 << 20)));
    REQUIRE_FALSE(image.has_value());
    CHECK(image.error().code == ErrorCode::Invalid);
    CAPTURE(image.error().message);
    CHECK(image.error().message.find("5454009b") != std::string::npos);
    CHECK(image.error().message.find("4d530051") != std::string::npos);

    dir.write("E/UDATA/5454009b/TitleMeta.xbx", std::vector<std::byte>(50, std::byte{1}));
    const auto empty = coney::readOriginalSave(dir.path() / "E");
    REQUIRE_FALSE(empty.has_value());
    CAPTURE(empty.error().message);
    CHECK(empty.error().message.find("no save") != std::string::npos);
}

TEST_CASE("an import folder lists Xbox saves beside PS2 ones", "[xbox_save]") {
    // Expected: save.md#xbox-import (the front end's import folder takes every container the command does)
    TempDir dir;
    writeContainer(dir, SaveSourceKind::XboxQcow2Image);
    writeFiles(dir, "E/", twoSaves());
    const coney::ImportScan scan = coney::scanImportFolder(dir.path());
    REQUIRE(scan.saves.size() == 2);
    CHECK(scan.saves[0].source == "E");
    CHECK(scan.saves[0].kind == SaveSourceKind::XboxSaveFolder);
    CHECK(scan.saves[1].source == "xbox_hdd.qcow2");
    CHECK(scan.profileCount() == 4);
    CHECK(scan.problems.empty());
}
