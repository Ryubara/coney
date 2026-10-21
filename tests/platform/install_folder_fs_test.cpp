// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/install_folder_fs.h"

#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "support/fixtures.h"

using coney::ErrorCode;
using coney::platform::HostInstallFolder;
using coney::test::TempDir;

namespace {

// Writes `text` to `relative` in `folder` through the interface.
void writeFile(HostInstallFolder& folder, std::string_view relative, std::string_view text) {
    auto out = folder.create(relative);
    REQUIRE(out.has_value());
    REQUIRE((*out)->write(std::as_bytes(std::span(text.data(), text.size()))).has_value());
    REQUIRE((*out)->close().has_value());
}

} // namespace

TEST_CASE("the host install folder writes, renames over and removes files", "[install]") {
    TempDir dir;
    auto folder = HostInstallFolder::open(dir.path() / "Coney");
    REQUIRE(folder.has_value());
    auto& f = **folder;
    REQUIRE(f.makeFolders("game/ps2/IOP").has_value());
    writeFile(f, "game/ps2/IOP/BFW.SND.part", "new");
    writeFile(f, "game/ps2/IOP/BFW.SND", "old!");
    CHECK(f.fileSize("game/ps2/IOP/BFW.SND") == 4U);
    REQUIRE(f.rename("game/ps2/IOP/BFW.SND.part", "game/ps2/IOP/BFW.SND").has_value());
    CHECK(f.fileSize("game/ps2/IOP/BFW.SND") == 3U);
    CHECK_FALSE(f.fileSize("game/ps2/IOP/BFW.SND.part").has_value());
    auto read = f.open("game/ps2/IOP/BFW.SND");
    REQUIRE(read.has_value());
    std::vector<std::byte> bytes(3);
    REQUIRE((*read)->read(bytes).has_value());
    CHECK(bytes[0] == std::byte{'n'});
    read->reset(); // close it, so the file can be removed on Windows
    REQUIRE(f.remove("game/ps2/IOP/BFW.SND").has_value());
    CHECK_FALSE(f.fileSize("game/ps2/IOP/BFW.SND").has_value());
    CHECK(f.remove("not/there").has_value());
    CHECK(f.freeBytes().has_value());
    CHECK(std::filesystem::is_directory(dir.path() / "Coney" / "game" / "ps2"));
}

TEST_CASE("the host install folder refuses paths that leave it", "[install]") {
    TempDir dir;
    auto folder = HostInstallFolder::open(dir.path());
    REQUIRE(folder.has_value());
    const auto climbing = (*folder)->create("../outside.txt");
    REQUIRE_FALSE(climbing.has_value());
    CHECK(climbing.error().code == ErrorCode::InvalidArgument);
    CHECK_FALSE((*folder)->makeFolders("game/../../x").has_value());
    CHECK_FALSE((*folder)->create((dir.path() / "abs.txt").generic_string()).has_value());
}

TEST_CASE("a file cannot be an install folder", "[install]") {
    TempDir dir;
    const auto file = dir.write("taken", std::vector<std::byte>(3));
    const auto folder = HostInstallFolder::open(file);
    REQUIRE_FALSE(folder.has_value());
    CHECK(folder.error().code == ErrorCode::InvalidArgument);
}
