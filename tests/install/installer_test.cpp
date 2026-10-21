// SPDX-License-Identifier: GPL-3.0-or-later
#include "install/installer.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "fileio/disc.h"
#include "install/install_manifest.h"
#include "support/fixtures.h"
#include "support/install_fixtures.h"

using coney::ErrorCode;
using coney::install::InstallProgress;
using coney::install::InstallRequest;
using coney::install::InstallStep;
using coney::install::Platform;
using coney::install::SourceFile;
using coney::io::Disc;
using coney::test::FixtureRelease;
using coney::test::MemoryInstallFolder;
using coney::test::TempDir;

namespace {

// `size` bytes counting up from `seed`.
std::vector<std::byte> payload(std::size_t size, std::uint8_t seed) {
    std::vector<std::byte> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<std::byte>(static_cast<std::uint8_t>(seed + (i * 7)));
    }
    return bytes;
}

// A small release laid out as the game's disc is: a key file, the archive, and a file in a folder. The folder file is
// larger than one copy piece (8 MiB), so a cancel can land inside it.
std::vector<std::pair<std::string, std::vector<std::byte>>> releaseFiles() {
    return {{"SLUS_212.15", payload(3000, 1)},
            {"WARRIORS.DIR", payload(100, 2)},
            {"WARRIORS.WAD", payload(5000, 3)},
            {"IOP/BFW.SND", payload((9U << 20) + 5, 4)}};
}

// Writes the release's files into a folder of `dir`, as a mounted disc holds them, and opens it.
Disc discFolder(const TempDir& dir, const FixtureRelease& release, std::string_view folder = "disc") {
    for (const auto& [name, data] : release.contents()) {
        dir.write(std::string(folder) + "/" + name, data);
    }
    auto disc = Disc::open(dir.path() / folder);
    REQUIRE(disc.has_value());
    return std::move(*disc);
}

// The installed bytes of `name`, which must be there.
const std::vector<std::byte>& installed(const MemoryInstallFolder& folder, const std::string& name) {
    const auto it = folder.files.find("game/ps2/" + name);
    REQUIRE(it != folder.files.end());
    return it->second;
}

// The request for installing `disc` against the fixture release's list.
InstallRequest requestFor(const Disc& disc, const FixtureRelease& release) {
    InstallRequest request;
    request.ps2 = &disc;
    request.known = release.known();
    return request;
}

} // namespace

TEST_CASE("identifyDisc names the known disc and refuses others with a reason", "[install]") {
    const FixtureRelease release(releaseFiles());
    std::vector<SourceFile> files;
    for (const auto& [name, data] : release.contents()) {
        files.push_back({name, data.size()});
    }
    SECTION("the disc itself, in any letter case") {
        files[1].name = "warriors.dir";
        const auto disc = coney::install::identifyDisc(files, Platform::Ps2, release.known(), "D:/");
        REQUIRE(disc.has_value());
        CHECK((*disc)->id == "ps2-test");
    }
    SECTION("another game") {
        const std::vector<SourceFile> other{{"SLES_123.45", 10}, {"DATA.BIN", 20}};
        const auto disc = coney::install::identifyDisc(other, Platform::Ps2, release.known(), "D:/");
        REQUIRE_FALSE(disc.has_value());
        CHECK(disc.error().code == ErrorCode::Invalid);
        CHECK(disc.error().message.find("not a disc of The Warriors") != std::string::npos);
        CHECK(disc.error().message.find("A test disc") != std::string::npos);
    }
    SECTION("the game's archive without the boot file Coney knows") {
        files.erase(files.begin());
        const auto disc = coney::install::identifyDisc(files, Platform::Ps2, release.known(), "D:/");
        REQUIRE_FALSE(disc.has_value());
        CHECK(disc.error().message.find("another release") != std::string::npos);
    }
    SECTION("a boot file of another size") {
        files[0].size = 2999;
        const auto disc = coney::install::identifyDisc(files, Platform::Ps2, release.known(), "D:/");
        REQUIRE_FALSE(disc.has_value());
        CHECK(disc.error().message.find("SLUS_212.15 is 2999 bytes, not the 3000") != std::string::npos);
    }
    SECTION("a missing file") {
        files.pop_back();
        const auto disc = coney::install::identifyDisc(files, Platform::Ps2, release.known(), "D:/");
        REQUIRE_FALSE(disc.has_value());
        CHECK(disc.error().message.find("IOP/BFW.SND is missing") != std::string::npos);
    }
    SECTION("a file of another size") {
        files[2].size = 4096;
        const auto disc = coney::install::identifyDisc(files, Platform::Ps2, release.known(), "D:/");
        REQUIRE_FALSE(disc.has_value());
        CHECK(disc.error().message.find("WARRIORS.WAD is 4096 bytes, not 5000") != std::string::npos);
    }
}

TEST_CASE("an install copies every file as it is, makes the layout and writes the manifest last", "[install]") {
    TempDir dir;
    const FixtureRelease release(releaseFiles());
    const Disc disc = discFolder(dir, release);
    MemoryInstallFolder folder;
    InstallRequest withVersion = requestFor(disc, release);
    withVersion.version = "9.9.9";
    std::vector<InstallStep> steps;
    std::uint64_t lastDone = 0;
    bool monotonic = true;
    const auto progress = [&](const InstallProgress& p) {
        if (steps.empty() || steps.back() != p.step) {
            steps.push_back(p.step);
        }
        monotonic = monotonic && p.done >= lastDone && p.done <= p.total;
        lastDone = p.done;
        return true;
    };
    const auto report = coney::install::install(folder, withVersion, progress);
    REQUIRE(report.has_value());
    CHECK_FALSE(report->cancelled);
    CHECK(report->copied == 4);
    CHECK(report->kept == 0);
    CHECK(report->ps2Disc == "ps2-test");
    for (const auto& [name, data] : release.contents()) {
        CHECK(installed(folder, name) == data);
    }
    CHECK(folder.endingWith(".part").empty());
    for (const std::string_view overrides : coney::install::kOverrideFolders) {
        CHECK(folder.folders.contains(overrides));
    }
    REQUIRE(folder.files.contains("index/install.json"));
    CHECK(report->manifest.find("\"coney_version\": \"9.9.9\"") != std::string::npos);
    CHECK(report->manifest.find("\"disc\": \"ps2-test\"") != std::string::npos);
    CHECK(report->manifest.find("\"game/ps2/IOP/BFW.SND\"") != std::string::npos);
    // No program files were given, so the Program step reports nothing.
    CHECK(steps == std::vector<InstallStep>{InstallStep::Checking, InstallStep::Copying, InstallStep::Index});
    CHECK(monotonic);
    CHECK(lastDone == 3000 + 100 + 5000 + (9U << 20) + 5);
}

TEST_CASE("an ISO image installs the same files and the same manifest but for its source", "[install]") {
    TempDir dir;
    // buildIso puts a folder's files in SUBDIR, so this release names its sound file that way.
    auto files = releaseFiles();
    files.back().first = "SUBDIR/BFW.SND";
    const FixtureRelease release(files);
    const auto image = coney::test::buildIso({{"SLUS_212.15;1", files[0].second},
                                              {"WARRIORS.DIR;1", files[1].second},
                                              {"WARRIORS.WAD;1", files[2].second},
                                              {"SUBDIR/BFW.SND;1", files[3].second}});
    auto disc = Disc::open(dir.write("game.iso", image));
    REQUIRE(disc.has_value());
    MemoryInstallFolder folder;
    const auto report = coney::install::install(folder, requestFor(*disc, release), {});
    REQUIRE(report.has_value());
    CHECK(report->copied == 4);
    CHECK(installed(folder, "SUBDIR/BFW.SND") == files[3].second);
    CHECK(report->manifest.find("\"kind\": \"image\"") != std::string::npos);
}

TEST_CASE("running the install again keeps what is there and writes the same manifest", "[install]") {
    TempDir dir;
    const FixtureRelease release(releaseFiles());
    const Disc disc = discFolder(dir, release);
    MemoryInstallFolder folder;
    const InstallRequest request = requestFor(disc, release);
    const auto first = coney::install::install(folder, request, {});
    REQUIRE(first.has_value());
    const auto second = coney::install::install(folder, request, {});
    REQUIRE(second.has_value());
    CHECK(second->copied == 0);
    CHECK(second->kept == 4);
    CHECK(second->manifest == first->manifest);
}

TEST_CASE("a cancelled install keeps only finished files, and the next run finishes it", "[install]") {
    TempDir dir;
    const FixtureRelease release(releaseFiles());
    const Disc disc = discFolder(dir, release);
    MemoryInstallFolder folder;
    const InstallRequest request = requestFor(disc, release);
    // Cancel once the large file is part copied.
    const auto cancelInSound = [](const InstallProgress& p) {
        return !(p.file == "game/ps2/IOP/BFW.SND" && p.fileDone > 0 && p.fileDone < p.fileSize);
    };
    const auto cancelled = coney::install::install(folder, request, cancelInSound);
    REQUIRE(cancelled.has_value());
    CHECK(cancelled->cancelled);
    CHECK(cancelled->manifest.empty());
    CHECK_FALSE(folder.files.contains("index/install.json"));
    CHECK(folder.endingWith(".part").empty());
    CHECK_FALSE(folder.files.contains("game/ps2/IOP/BFW.SND"));
    CHECK(folder.files.contains("game/ps2/SLUS_212.15")); // the files before it are finished

    const auto resumed = coney::install::install(folder, request, {});
    REQUIRE(resumed.has_value());
    CHECK(resumed->kept == 3);
    CHECK(resumed->copied == 1);
    CHECK(folder.files.contains("index/install.json"));
}

TEST_CASE("a file whose SHA-1 is not the known one is not kept", "[install]") {
    TempDir dir;
    FixtureRelease release(releaseFiles());
    const Disc disc = discFolder(dir, release);
    release.setSha1(2, "0000000000000000000000000000000000000000");
    MemoryInstallFolder folder;
    const auto report = coney::install::install(folder, requestFor(disc, release), {});
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().code == ErrorCode::Invalid);
    CHECK(report.error().message.find("game/ps2/WARRIORS.WAD: SHA-1") != std::string::npos);
    CHECK_FALSE(folder.files.contains("game/ps2/WARRIORS.WAD"));
    CHECK(folder.endingWith(".part").empty());
    CHECK_FALSE(folder.files.contains("index/install.json"));
}

TEST_CASE("verifying an install copies again a file that changed since", "[install]") {
    TempDir dir;
    const FixtureRelease release(releaseFiles());
    const Disc disc = discFolder(dir, release);
    MemoryInstallFolder folder;
    REQUIRE(coney::install::install(folder, requestFor(disc, release), {}).has_value());
    folder.files["game/ps2/WARRIORS.WAD"][10] ^= std::byte{0xFF}; // same size, other bytes

    const auto quick = coney::install::install(folder, requestFor(disc, release), {});
    REQUIRE(quick.has_value());
    CHECK(quick->kept == 4); // without verifying, the size is all that is checked

    InstallRequest verify = requestFor(disc, release);
    verify.verifyInstalled = true;
    const auto verified = coney::install::install(folder, verify, {});
    REQUIRE(verified.has_value());
    CHECK(verified->copied == 1);
    CHECK(verified->kept == 3);
    CHECK(installed(folder, "WARRIORS.WAD") == release.contents()[2].second);
}

TEST_CASE("an install refuses a folder without the space for the disc", "[install]") {
    TempDir dir;
    const FixtureRelease release(releaseFiles());
    const Disc disc = discFolder(dir, release);
    MemoryInstallFolder folder;
    folder.free = 1U << 20;
    const auto report = coney::install::install(folder, requestFor(disc, release), {});
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().code == ErrorCode::Io);
    CHECK(report.error().message.find("free") != std::string::npos);
    CHECK(folder.files.empty());
}

TEST_CASE("a drive that fills up mid-copy leaves no part file", "[install]") {
    TempDir dir;
    const FixtureRelease release(releaseFiles());
    const Disc disc = discFolder(dir, release);
    MemoryInstallFolder folder;
    folder.writeLimit = 1U << 20;
    const auto report = coney::install::install(folder, requestFor(disc, release), {});
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().code == ErrorCode::Io);
    CHECK(folder.endingWith(".part").empty());
    CHECK_FALSE(folder.files.contains("game/ps2/IOP/BFW.SND"));
}

TEST_CASE("Coney's own files are copied into the root and listed with their SHA-1", "[install]") {
    TempDir dir;
    const FixtureRelease release(releaseFiles());
    const Disc disc = discFolder(dir, release);
    const auto program = dir.write("bin/coney.exe", payload(64, 9));
    MemoryInstallFolder folder;
    InstallRequest withProgram = requestFor(disc, release);
    withProgram.programFiles = {program};
    const auto report = coney::install::install(folder, withProgram, {});
    REQUIRE(report.has_value());
    CHECK(folder.files.at("coney.exe") == payload(64, 9));
    coney::Sha1 sha;
    sha.update(std::span<const std::byte>(payload(64, 9)));
    CHECK(report->manifest.find("\"path\": \"coney.exe\", \"size\": 64, \"sha1\": \"" + coney::toHex(sha.finish())) !=
          std::string::npos);
}

TEST_CASE("an install without a PS2 disc is refused", "[install]") {
    MemoryInstallFolder folder;
    const auto report = coney::install::install(folder, {}, {});
    REQUIRE_FALSE(report.has_value());
    CHECK(report.error().code == ErrorCode::InvalidArgument);
}
