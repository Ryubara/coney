// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/install_command.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/options.h"
#include "platform/game_run.h"
#include "support/fixtures.h"
#include "support/install_fixtures.h"

using coney::test::FixtureRelease;
using coney::test::TempDir;

namespace {

// A temporary file for the command's output. MSVC deprecates std::tmpfile (C4996, an error in the ci preset), so it
// gets tmpfile_s, which opens the same kind of file.
std::FILE* scratchFile() {
#ifdef _MSC_VER
    std::FILE* file = nullptr;
    return tmpfile_s(&file) == 0 ? file : nullptr;
#else
    return std::tmpfile();
#endif
}

// Runs the command with `options` against the fixture release; returns its exit code and what it printed.
std::pair<int, std::string> run(const coney::Options& options, const FixtureRelease& release,
                                const std::vector<std::filesystem::path>& program = {}) {
    std::FILE* out = scratchFile();
    REQUIRE(out != nullptr);
    const int code = coney::platform::runInstallCommand(options, release.known(), program, out);
    REQUIRE(std::fseek(out, 0, SEEK_SET) == 0);
    std::string text;
    for (int c = std::fgetc(out); c != EOF; c = std::fgetc(out)) {
        text.push_back(static_cast<char>(c));
    }
    std::fclose(out);
    return {code, text};
}

// A release of two small files, the first the key file.
FixtureRelease smallRelease() {
    return FixtureRelease({{"SLUS_212.15", std::vector<std::byte>(40, std::byte{1})},
                           {"PSS/LOGO.BIK", std::vector<std::byte>(70, std::byte{2})}});
}

} // namespace

TEST_CASE("coney --install copies the disc into the folder and prints counts and the digest", "[install]") {
    // Expected: discs.md#digests, Coney's own install record
    TempDir dir;
    const FixtureRelease release = smallRelease();
    for (const auto& [name, data] : release.contents()) {
        dir.write("disc/" + name, data);
    }
    const auto program = dir.write("bin/coney.exe", std::vector<std::byte>(16, std::byte{3}));
    coney::Options options;
    options.installFrom = (dir.path() / "disc").string();
    options.dataPath = (dir.path() / "Coney Install").string();
    const auto [code, text] = run(options, release, {program});
    CHECK(code == 0);
    CHECK(text.find("[1/2] copying game/ps2/SLUS_212.15") != std::string::npos);
    CHECK(text.find("100%") != std::string::npos);
    CHECK(text.find("installed A test disc into") != std::string::npos);
    CHECK(text.find("2 files (copied 2, kept 0)") != std::string::npos);
    const auto root = dir.path() / "Coney Install";
    CHECK(std::filesystem::file_size(root / "game" / "ps2" / "PSS" / "LOGO.BIK") == 70U);
    CHECK(std::filesystem::file_size(root / "coney.exe") == 16U);
    CHECK(std::filesystem::is_regular_file(root / "index" / "install.json"));
    CHECK(std::filesystem::is_directory(root / "overrides" / "audio" / "music"));

    // The same command again keeps everything.
    const auto [again, againText] = run(options, release, {program});
    CHECK(again == 0);
    CHECK(againText.find("2 files (copied 0, kept 2)") != std::string::npos);
}

TEST_CASE("coney --install refuses a folder that is not the game's disc", "[install]") {
    // Expected: discs.md#ps2-ntsc-u, Coney's own check of the known releases
    TempDir dir;
    const FixtureRelease release = smallRelease();
    dir.write("other/SLES_000.00", std::vector<std::byte>(5));
    coney::Options options;
    options.installFrom = (dir.path() / "other").string();
    options.dataPath = (dir.path() / "Coney").string();
    const auto [code, text] = run(options, release);
    CHECK(code == 1);
    CHECK(text.find("not a disc of The Warriors") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(dir.path() / "Coney" / "index" / "install.json"));
}

TEST_CASE("only a finished install is one to play from", "[install]") {
    // Expected: discs.md#digests, Coney's own install record (written last)
    TempDir dir;
    const FixtureRelease release = smallRelease();
    for (const auto& [name, data] : release.contents()) {
        dir.write("disc/" + name, data);
    }
    const auto root = dir.path() / "Coney";
    const auto unfinished = coney::platform::checkInstallFolder(root);
    REQUIRE_FALSE(unfinished.has_value());
    CHECK(unfinished.error().message.find("not a finished Coney install") != std::string::npos);
    coney::Options options;
    options.installFrom = (dir.path() / "disc").string();
    options.dataPath = root.string();
    REQUIRE(run(options, release).first == 0);
    CHECK(coney::platform::checkInstallFolder(root).has_value());
}

TEST_CASE("a game played from an install reads its overrides/files in place of the WAD's entries", "[install][wad]") {
    // Expected: docs/guides/asset-and-script-architecture.md#file-overrides, Coney's own behaviour
    // An install's game/ps2 holds the disc's files; overrides/files/<wad name> replaces one entry whole
    // (docs/guides/asset-and-script-architecture.md#file-overrides).
    TempDir dir;
    dir.write("Coney/game/ps2/WARRIORS.DIR", coney::test::twoEntryDir().span());
    coney::test::Bytes wadData;
    wadData.text("hello").padTo(2048).u32(1).u32(2).u32(3);
    dir.write("Coney/game/ps2/WARRIORS.WAD", wadData.span());
    coney::test::Bytes edited; // a data entry's replacement, longer than the entry; never a script
    edited.u32(4).u32(5).u32(6).u32(7);
    dir.write("Coney/overrides/files/level1.lev", edited.span());
    coney::Options options;
    options.dataPath = (dir.path() / "Coney").string();
    options.discPath = coney::installGameFolder(*options.dataPath);

    auto wad = coney::platform::openGameDisc(options);
    REQUIRE(wad.has_value());
    REQUIRE(wad->has_value());
    CHECK((*wad)->modded());
    auto level1 = (*wad)->lookup("level1.lev");
    REQUIRE(level1.has_value());
    auto stream = (*wad)->openEntry(**level1);
    REQUIRE(stream.has_value());
    CHECK(stream->size() == 16);

    // The same files given as a disc (`--disc`) are the unmodded game: overrides belong to an install.
    coney::Options disc;
    disc.discPath = options.discPath;
    auto plain = coney::platform::openGameDisc(disc);
    REQUIRE(plain.has_value());
    REQUIRE(plain->has_value());
    CHECK_FALSE((*plain)->modded());
}
