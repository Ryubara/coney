// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/install_command.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

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
    std::rewind(out);
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
