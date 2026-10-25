// SPDX-License-Identifier: GPL-3.0-or-later
#include "launcher/release_manifest.h"

#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "support/optional_value.h"

using coney::launcher::parseRelease;
using coney::launcher::parseVersion;
using coney::launcher::Version;

namespace {

// A manifest with one Windows file, `path` and `extra` spliced in to vary it.
std::string manifest(std::string_view path = "coney.exe", std::string_view extra = "") {
    return std::string(R"({"format": 1, "version": "0.2.0", "channel": "stable", "min_launcher": "0.1.0",)") +
           R"("notes": ["Line one", "Line two"], "changelog_url": "https://example.invalid/notes",)" +
           R"("platforms": {"windows-x64": [{"path": ")" + std::string(path) + R"(", "size": 4096, "sha256": ")" +
           std::string(64, 'a') + R"(", "asset": "coney-windows.exe"})" + std::string(extra) + "]}}";
}

} // namespace

TEST_CASE("versions parse and sort as Semantic Versioning orders them", "[launcher]") {
    const Version v = coney::test::got(parseVersion("0.3.0-rc.1"));
    CHECK(v.minor == 3);
    CHECK(v.prerelease == "rc.1");
    CHECK(v.text() == "0.3.0-rc.1");
    for (const char* bad : {"v0.1.0", "0.1", "0.01.0", "1.2.3-", "1.2.3-r c", "1.2.x", ""}) {
        INFO(bad);
        CHECK_FALSE(parseVersion(bad).has_value());
    }
    // The ordering example of Semantic Versioning 2.0, section 11.
    const char* ordered[] = {"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta",
                             "1.0.0-beta",  "1.0.0-beta.2",  "1.0.0-beta.11",
                             "1.0.0-rc.1",  "1.0.0",         "1.0.1",
                             "1.1.0",       "2.0.0"};
    for (std::size_t i = 0; i + 1 < std::size(ordered); ++i) {
        INFO(ordered[i] << " < " << ordered[i + 1]);
        CHECK(coney::test::got(parseVersion(ordered[i])) < coney::test::got(parseVersion(ordered[i + 1])));
    }
    CHECK(coney::test::got(parseVersion("1.0.0")) == coney::test::got(parseVersion("1.0.0")));
}

TEST_CASE("a release manifest is read with its files per platform", "[launcher]") {
    const auto release = parseRelease(manifest());
    REQUIRE(release.has_value());
    CHECK(release->version.text() == "0.2.0");
    CHECK(release->channel == "stable");
    CHECK(release->minLauncher == coney::test::got(parseVersion("0.1.0")));
    CHECK(release->notes.size() == 2);
    const auto files = release->platforms.find("windows-x64");
    REQUIRE(files != release->platforms.end());
    REQUIRE(files->second.size() == 1);
    CHECK(files->second[0].path == "coney.exe");
    CHECK(files->second[0].size == 4096U);
    CHECK(files->second[0].asset == "coney-windows.exe");
}

TEST_CASE("a release manifest that could write outside the install is refused", "[launcher]") {
    for (const char* path :
         {"../coney.exe", "/etc/coney", "C:/coney.exe", "a\\\\b", "a//b", "a/", "./a", "a/../b", "a/./b", "a/..", ""}) {
        INFO(path);
        const auto release = parseRelease(manifest(path));
        REQUIRE_FALSE(release.has_value());
        CHECK(release.error().message.find("path") != std::string::npos);
    }
    CHECK(parseRelease(manifest("licences/SDL3.txt")).has_value());
    // Only printable ASCII, which keeps the case-aside comparison of paths complete.
    CHECK_FALSE(parseRelease(manifest("caf\xc3\xa9.txt")).has_value());
}

TEST_CASE("a release manifest path names one file on every platform", "[launcher]") {
    // Windows strips a trailing `.` or space (`coney.exe.` is `coney.exe`), forbids some characters, and keeps device
    // names whatever their case or extension.
    for (const char* path : {"coney.exe.",       "coney.exe ", "a./b", "a /b", "a*b",      "a?b",
                             "a\\\"b",           "a<b",        "a>b",  "a|b",  "CON",      "con.txt",
                             "licences/Nul.txt", "aux",        "PRN",  "com1", "LPT9.log", "conin$",
                             "CONOUT$.x",        "con .txt"}) {
        INFO(path);
        const auto release = parseRelease(manifest(path));
        REQUIRE_FALSE(release.has_value());
        CHECK(release.error().message.find("path") != std::string::npos);
    }
    for (const char* path : {"console.exe", "com10", "lpt.txt", "a.b/c", ".hidden"}) {
        INFO(path);
        CHECK(parseRelease(manifest(path)).has_value());
    }
}

TEST_CASE("a release manifest path that is also another's folder is refused", "[launcher]") {
    // `a` cannot be a file and a folder at once, whatever the case or the order.
    for (const auto& [first, second] : {std::pair{"a", "a/b"}, std::pair{"A/b/c", "a/B"}, std::pair{"x/y/z", "X"}}) {
        INFO(first << " and " << second);
        const auto release =
            parseRelease(manifest(first, std::string(R"(, {"path": ")") + second + R"(", "size": 1, "sha256": ")" +
                                             std::string(64, 'b') + R"(", "asset": "x"})"));
        REQUIRE_FALSE(release.has_value());
        CHECK(release.error().message.find("as a file and as the folder of") != std::string::npos);
    }
    CHECK(parseRelease(manifest("ab", R"(, {"path": "a/b", "size": 1, "sha256": ")" + std::string(64, 'b') +
                                          R"(", "asset": "x"})"))
              .has_value());
}

TEST_CASE("a release manifest asset name is a plain file name", "[launcher]") {
    // The asset is downloaded under its name, so `.` and `..` would name the download folder or its parent.
    for (const char* asset :
         {".", "..", "...", ". ", "", "a/b", "../coney.exe", "a\\\\b", "C:coney.exe", "x.exe.", "nul", "a*b"}) {
        INFO(asset);
        std::string text = manifest();
        text.replace(text.find("coney-windows.exe"), 17, asset);
        const auto release = parseRelease(text);
        REQUIRE_FALSE(release.has_value());
        CHECK(release.error().message.find("asset name") != std::string::npos);
    }
}

TEST_CASE("a release manifest path repeated in another case is refused", "[launcher]") {
    // Windows and macOS would write both to one file.
    const auto twice = parseRelease(manifest("Coney.exe", R"(, {"path": "coney.EXE", "size": 1, "sha256": ")" +
                                                              std::string(64, 'b') + R"(", "asset": "x"})"));
    REQUIRE_FALSE(twice.has_value());
    CHECK(twice.error().message.find("repeats the path coney.EXE") != std::string::npos);
    CHECK(coney::launcher::pathKey("Licences/SDL3.TXT") == "licences/sdl3.txt");
}

TEST_CASE("a release manifest with a bad field names it", "[launcher]") {
    const auto twice = parseRelease(manifest("coney.exe", R"(, {"path": "coney.exe", "size": 1, "sha256": ")" +
                                                              std::string(64, 'b') + R"(", "asset": "x"})"));
    REQUIRE_FALSE(twice.has_value());
    CHECK(twice.error().message.find("repeats") != std::string::npos);
    std::string text = manifest();
    text.replace(text.find("\"format\": 1"), 11, "\"format\": 2");
    const auto newer = parseRelease(text);
    REQUIRE_FALSE(newer.has_value());
    CHECK(newer.error().message.find("newer launcher") != std::string::npos);
    std::string badSha = manifest();
    badSha.replace(badSha.find(std::string(64, 'a')), 64, std::string(63, 'a'));
    CHECK_FALSE(parseRelease(badSha).has_value());
    CHECK_FALSE(parseRelease("{not json").has_value());
}
