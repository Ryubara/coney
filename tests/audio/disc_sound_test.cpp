// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: the sound tables of warriors.glr parse with the counts on
// docs/research/formats/audio.md, the `sound` and `menu` banks decode whole, and a streamed sound and a music track
// stream from IOP/BFW.SND and IOP/MUSIC.SND through the engine. It runs only when the environment variable CONEY_DISC
// names the disc and skips otherwise, so CI never needs the game. It prints counts only, never data (LEGAL.md).

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "audio/mixer.h"
#include "audio/sound_bank.h"
#include "audio/sound_data.h"
#include "audio/sound_engine.h"
#include "audio/sound_stream.h"
#include "fileio/disc.h"
#include "fileio/wad.h"

TEST_CASE("the disc's sound tables, banks and streams load and play", "[disc][audio]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());

    auto tables = coney::audio::loadSoundTables(*wad);
    if (!tables) {
        FAIL(tables.error().message);
    }
    CHECK(tables->sounds().size() == 25'395);
    CHECK(tables->classes().size() == 246);
    CHECK(tables->stereo().size() == 248);
    CHECK(tables->music().size() == 345);
    for (const auto& layout : tables->stereo()) {
        const auto* record = tables->find(layout.hash);
        REQUIRE(record != nullptr);
        CHECK(tables->classOf(*record)->stereo());
    }

    auto sound = coney::audio::loadSoundBank(*wad, "sound", *tables);
    REQUIRE(sound.has_value());
    CHECK(sound->size() == 319);
    CHECK(sound->skipped() == 0);
    auto menu = coney::audio::loadSoundBank(*wad, "menu", *tables);
    REQUIRE(menu.has_value());
    CHECK(menu->size() == 14);
    std::printf("sound tables: %zu sounds, %zu classes, %zu stereo, %zu tracks; banks sound %zu, menu %zu\n",
                tables->sounds().size(), tables->classes().size(), tables->stereo().size(), tables->music().size(),
                sound->size(), menu->size());

    // Stream the first streamed mono sound and the first music track through the engine for a second.
    auto files = coney::audio::DiscSoundFiles::open(wad->disc());
    REQUIRE(files.has_value());
    std::uint32_t streamed = 0;
    for (const auto& record : tables->sounds()) {
        const auto* cls = tables->classOf(record);
        if (cls != nullptr && cls->streamed() && !cls->stereo() && !cls->positional() && record.size > 64'000) {
            streamed = record.hash;
            break;
        }
    }
    REQUIRE(streamed != 0);
    const std::uint32_t track = tables->music().front().hash;
    coney::audio::Mixer mixer;
    coney::audio::SoundEngine engine(mixer, std::move(*tables), std::move(*files), {}, {});
    const auto handle = engine.play(streamed);
    REQUIRE(handle.valid());
    CHECK_FALSE(engine.isVirtual(handle));
    engine.music().play(track, true);
    std::array<std::int16_t, std::size_t{1600} * 2> out{};
    int peak = 0;
    for (int step = 0; step < 30; ++step) {
        engine.update(1000.0F / 30.0F, {});
        mixer.mix(out);
        for (const std::int16_t sample : out) {
            peak = std::max(peak, std::abs(static_cast<int>(sample)));
        }
    }
    CHECK(engine.music().currentTrack() == track);
    CHECK(engine.stats().readErrors == 0);
    CHECK(peak > 0);
    std::printf("streamed for 1 s: peak %d\n", peak);
}
