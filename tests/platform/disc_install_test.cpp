// SPDX-License-Identifier: GPL-3.0-or-later
// The player's disc against the install's list of known discs: counts and "matched" only, never content (LEGAL.md).

#include <cstddef>
#include <cstdio>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "core/sha1.h"
#include "fileio/disc.h"
#include "install/installer.h"
#include "install/known_discs.h"

TEST_CASE("the player's PS2 disc is a known disc, and its small files match their SHA-1s", "[disc][install]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    const auto files = coney::install::listFiles(*disc);
    const auto known =
        coney::install::identifyDisc(files, coney::install::Platform::Ps2, coney::install::knownDiscs(), discPath);
    REQUIRE(known.has_value());
    CHECK((*known)->id == "ps2-ntsc-u");
    CHECK(files.size() == (*known)->files.size());

    // The files under 8 MB (all but the archive, the sound and the movies) are read whole and hashed; the large ones
    // are checked by `coney --install` (docs/guides/running.md#install).
    std::size_t matched = 0;
    std::size_t hashed = 0;
    for (const coney::install::KnownFile& file : (*known)->files) {
        if (file.size >= (std::uint64_t{8} << 20)) {
            continue;
        }
        auto bytes = disc->readFile(file.name);
        REQUIRE(bytes.has_value());
        coney::Sha1 sha;
        sha.update(std::span<const std::byte>(*bytes));
        ++hashed;
        if (coney::toHex(sha.finish()) == file.sha1) {
            ++matched;
        }
    }
    std::printf("known disc %.*s: %zu files; %zu small files hashed, %zu matched\n",
                static_cast<int>((*known)->id.size()), (*known)->id.data(), files.size(), hashed, matched);
    CHECK(hashed > 0);
    CHECK(matched == hashed);
}
