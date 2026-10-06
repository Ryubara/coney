// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc (docs/research/movies.md): LOGO decoded through FFmpeg to its last frame and
// sound sample, and L99_IN's captions found in level99's Subtitles chunk and its caption scene. It runs only when the
// environment variable CONEY_DISC names the disc, and skips otherwise. It prints counts and a hash only (LEGAL.md).

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "core/language.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "movies/disc_captions.h"
#include "platform/ffmpeg_movie_decoder.h"

namespace {

// FNV-1a over `bytes`, folded into `hash`.
std::uint64_t fnv1a(std::uint64_t hash, const std::vector<std::uint8_t>& bytes) {
    for (const std::uint8_t byte : bytes) {
        hash = (hash ^ byte) * 0x100000001b3ULL;
    }
    return hash;
}

} // namespace

TEST_CASE("LOGO decodes to its frame count and its sound", "[disc][movies]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto decoder = coney::platform::discMovieOpener(*disc)("LOGO");
    REQUIRE(decoder.has_value());
    const coney::movies::MovieInfo& info = (*decoder)->info();
    CHECK(info.width == 640);
    CHECK(info.height == 448);
    CHECK(info.rateNumerator == 2997);
    CHECK(info.rateDenominator == 100);
    CHECK(info.frameCount == 115);
    CHECK(info.hasAudio);
    CHECK(info.sampleRate == 48'000);
    CHECK(info.channels == 2);

    std::vector<std::uint8_t> rgba(std::size_t{640} * 448 * 4);
    std::vector<std::int16_t> pcm;
    std::uint32_t frames = 0;
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    while (true) {
        auto decoded = (*decoder)->decodeFrame(rgba);
        REQUIRE(decoded.has_value());
        if (!*decoded) {
            break;
        }
        ++frames;
        hash = fnv1a(hash, rgba);
    }
    (*decoder)->takeAudio(pcm);
    CHECK(frames == 115);
    CHECK(pcm.size() == std::size_t{184'320} * 2); // 3.84 s of stereo
    std::printf("LOGO: %u frames, %zu sound samples, picture hash %016llx\n", frames, pcm.size(),
                static_cast<unsigned long long>(hash));
}

TEST_CASE("L99_IN's captions come from level99 and l99_in_sub", "[disc][movies]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    const coney::movies::CaptionSource source =
        coney::movies::wadCaptionSource(*wad, [] { return coney::Language::English; });
    auto captions = source("L99_IN");
    REQUIRE(captions.has_value());
    // Six captions, each shown by an event and hidden by another; walk the whole movie.
    int shows = 0;
    for (double t = 0.0; t < 100.0; t += 1.0 / 30.0) {
        for (const int command : captions->timeline.advance(t)) {
            captions->captions.command(command);
            if (command == 0 && captions->captions.visible(true) != nullptr) {
                ++shows;
            }
        }
    }
    CHECK(shows == 6);
    CHECK_FALSE(source("LOGO").has_value());
    std::printf("L99_IN: %zu caption events, %d captions shown\n", captions->timeline.size(), shows);
}
