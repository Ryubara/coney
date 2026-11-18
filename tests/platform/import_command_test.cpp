// SPDX-License-Identifier: GPL-3.0-or-later
// `coney --import-save`: the importer's command, on synthetic saves (docs/guides/running.md#import-save).
#include "platform/import_command.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/options.h"
#include "support/fixtures.h"
#include "support/ps2_card_fixtures.h"
#include "warriors/disk_profile_store.h"
#include "warriors/game_state.h"
#include "warriors/profile_record.h"

using coney::ProfileRecord;
using coney::test::Bytes;
using coney::test::TempDir;

namespace {

// A temporary file for the command's output (tmpfile_s on MSVC, which deprecates std::tmpfile).
std::FILE* scratchFile() {
#ifdef _MSC_VER
    std::FILE* file = nullptr;
    return tmpfile_s(&file) == 0 ? file : nullptr;
#else
    return std::tmpfile();
#endif
}

// Runs the command with `options`; returns its exit code and what it printed.
std::pair<int, std::string> run(const coney::Options& options) {
    std::FILE* out = scratchFile();
    REQUIRE(out != nullptr);
    const int code = coney::platform::runImportCommand(options, out);
    REQUIRE(std::fseek(out, 0, SEEK_SET) == 0);
    std::string text;
    for (int c = std::fgetc(out); c != EOF; c = std::fgetc(out)) {
        text.push_back(static_cast<char>(c));
    }
    std::fclose(out);
    return {code, text};
}

// A data file with the named profiles in slots 0, 1, ... and the rest empty.
std::vector<std::byte> dataFile(const std::vector<std::string>& names) {
    Bytes bytes;
    for (std::size_t slot = 0; slot < 6; ++slot) {
        ProfileRecord record;
        if (slot < names.size()) {
            record.name = names[slot];
            record.progress.bankedMoney = 100 + static_cast<std::uint32_t>(slot);
        }
        bytes.append(record.serialise());
    }
    return bytes.data();
}

} // namespace

TEST_CASE("coney --import-save imports every profile of a card into the profiles folder", "[import_command]") {
    // Expected: save.md#import
    TempDir dir;
    const auto card = coney::test::buildPs2Card({{"BASLUS-21215", {{"BASLUS-21215", dataFile({"SNAKE", "AJAX"})}}}});
    coney::Options options;
    options.importSave = dir.write("Mcd001.ps2", card).string();
    options.profilesDir = (dir.path() / "profiles").string();
    const auto [code, text] = run(options);
    CAPTURE(text);
    CHECK(code == 0);
    CHECK(text.find("imported SNAKE (card slot 1) as profile 1") != std::string::npos);
    CHECK(text.find("imported AJAX (card slot 2) as profile 2") != std::string::npos);
    CHECK(text.find("2 of 2 profile(s) imported") != std::string::npos);

    coney::GameState state;
    coney::DiskProfileStore store(dir.path() / "profiles", state);
    REQUIRE(store.record(1) != nullptr);
    CHECK(store.record(1)->progress.bankedMoney == 101);

    // A second import of the same card finds both names taken and imports nothing.
    const auto [again, againText] = run(options);
    CHECK(again == 1);
    CHECK(againText.find("a profile named SNAKE exists already") != std::string::npos);
    CHECK(againText.find("0 of 2 profile(s) imported") != std::string::npos);
}

TEST_CASE("coney --import-save names the problem with a file that is not a save", "[import_command]") {
    // Expected: save.md#import
    TempDir dir;
    coney::Options options;
    options.importSave = dir.write("photo.png", std::vector<std::byte>(300, std::byte{7})).string();
    options.profilesDir = (dir.path() / "profiles").string();
    const auto [code, text] = run(options);
    CHECK(code == 1);
    CHECK(text.find("photo.png: not a PS2 save") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(dir.path() / "profiles"));
}

TEST_CASE("the import option takes only --profiles beside it", "[import_command]") {
    // Expected: save.md#import
    const std::vector<std::string_view> alone{"--import-save", "card.ps2", "--profiles", "dir"};
    const auto parsed = coney::parseOptions(alone);
    REQUIRE(parsed.has_value());
    CHECK(parsed->importSave == "card.ps2");
    const std::vector<std::string_view> withLevel{"--import-save", "card.ps2", "--play-level", "level99"};
    CHECK_FALSE(coney::parseOptions(withLevel).has_value());
    const auto player = coney::parseOptions(alone, coney::OptionSet::Player);
    CHECK(player.has_value());
}
