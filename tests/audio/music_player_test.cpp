// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/music_player.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/sound_data.h"
#include "audio_fixtures.h"

using Catch::Approx;
using coney::audio::Mixer;
using coney::audio::MusicPlayer;
using coney::audio::MusicRecord;
using coney::audio::MusicState;
using coney::audio::SoundTables;

// The tracks are constant synthetic ADPCM; no game data.

namespace {

constexpr std::uint32_t kTrackA = 0xa;
constexpr std::uint32_t kTrackB = 0xb;
constexpr std::uint32_t kTrackC = 0xc;
constexpr double kStep = 1000.0 / 30.0;

// A track of `blocks` blocks of 32 bytes per channel at `offset` (2 frames per channel per block).
MusicRecord track(std::uint32_t hash, std::uint32_t offset, std::uint32_t blocks) {
    return MusicRecord{.name = "music/test",
                       .hash = hash,
                       .volume = 1.0F,
                       .sampleRate = 30000,
                       .offset = offset,
                       .channels = 2,
                       .interleave = 32,
                       .blocks = blocks,
                       .lastBlock = 32};
}

// A player over three tracks: A and B long (64 blocks, about 0.12 s), C one block; each one constant.
struct Rig {
    Mixer mixer;
    SoundTables tables{{}, {}, {}, {track(kTrackA, 0, 64), track(kTrackB, 4096, 64), track(kTrackC, 8192, 1)}};
    coney::test::MemorySoundFiles files;
    MusicPlayer player{mixer, tables};
    double now = 0.0;
    std::vector<std::string> ended;

    Rig() {
        files.music = coney::test::constantAdpcm(1, std::size_t{64} * 4, 4);
        files.music.resize(8192 + 64);
        player.setTrackEndCallback([this](std::string_view name) { ended.emplace_back(name); });
    }

    // Runs `count` updates of a step each.
    void steps(int count) {
        for (int i = 0; i < count; ++i) {
            now += kStep;
            player.update(now, &files, {});
        }
    }
};

} // namespace

TEST_CASE("a track starts at once when nothing plays, at the volumes' product", "[audio]") {
    Rig rig;
    rig.player.play(kTrackA, true);
    CHECK(rig.player.state(2) == MusicState::Queued);
    rig.steps(1); // queued, then pre-loaded and started in the same update
    CHECK(rig.player.state(0) == MusicState::Playing);
    CHECK(rig.player.currentTrack() == kTrackA);
    CHECK(rig.player.sentVolume(0) == Approx(0.9F)); // the options' 0.9
    rig.player.setVolume(0.5F);
    rig.player.setScenePlaying(true);
    rig.steps(1);
    CHECK(rig.player.sentVolume(0) == Approx(0.9F * 0.5F * 0.75F));
    rig.player.setSceneDuck(false);
    rig.steps(1);
    CHECK(rig.player.sentVolume(0) == Approx(0.9F * 0.5F));
}

TEST_CASE("a new track waits for the playing one's next bar, then cross-fades over its fade bars", "[audio]") {
    Rig rig;
    rig.player.configure(kTrackA, 1000.0F, 1.0F);
    rig.player.play(kTrackA, true);
    rig.steps(1); // A starts at one step
    const double start = rig.now;
    rig.steps(5);
    rig.player.play(kTrackB, true, {}, 1);
    rig.steps(1);
    CHECK(rig.player.state(1) == MusicState::BarSync);
    // The bar boundary is 1000 ms after A's start; B starts 33 ms before it.
    while (rig.now + kStep < start + 1000.0 - MusicPlayer::kBarLeadMs) {
        rig.steps(1);
        CHECK(rig.player.state(1) == MusicState::BarSync);
    }
    rig.steps(1);
    CHECK(rig.player.state(1) == MusicState::Playing);
    CHECK(rig.player.state(0) == MusicState::FadeOut); // over A's bar (1000 ms) x 1
    rig.steps(15);
    CHECK(rig.player.sentVolume(0) < 0.9F * 0.6F);
    rig.steps(16);
    CHECK(rig.player.state(0) == MusicState::Idle);
    CHECK(rig.player.currentTrack() == kTrackB);
}

TEST_CASE("a track played once tells its callback when it ends", "[audio]") {
    Rig rig;
    rig.player.play(kTrackC, false, "OnTrackDone");
    rig.steps(1);
    CHECK(rig.player.state(0) == MusicState::Playing);
    // One block: 28 frames per channel; mixing them plays it out.
    (void)coney::test::pull(rig.mixer, 200);
    rig.steps(1);
    CHECK(rig.player.state(0) == MusicState::Idle);
    REQUIRE(rig.ended.size() == 1);
    CHECK(rig.ended[0] == "OnTrackDone");
}

TEST_CASE("music that is not allowed or not listed does not play, and stopping fades", "[audio]") {
    Rig rig;
    rig.player.play(0x999, true);
    CHECK(rig.player.state(2) == MusicState::Idle);
    rig.player.setAllowed(false);
    rig.player.play(kTrackA, true);
    CHECK(rig.player.state(2) == MusicState::Idle);
    rig.player.setAllowed(true);
    rig.player.play(kTrackA, true, {}, 2);
    rig.steps(1);
    CHECK(rig.player.state(0) == MusicState::FadeIn); // over 2 bars of the default 2000 ms
    rig.steps(30);
    CHECK(rig.player.sentVolume(0) == Approx(0.9F * 0.25F).margin(0.01F));
    rig.player.stop();
    CHECK(rig.player.state(0) == MusicState::FadeOut);
}

TEST_CASE("the system music plays one of the mood's tracks, cutting into a fight", "[audio]") {
    Rig rig;
    std::vector<int> moods;
    rig.player.setMoodCallback([&moods](int mood) { moods.push_back(mood); });
    rig.player.setSystemTracks(0, {kTrackA, 0, 0});
    rig.player.setSystemTracks(1, {kTrackB, 0, 0});
    rig.player.setSystemMusic(true);
    rig.steps(1);
    CHECK(rig.player.currentTrack() == kTrackA); // mood 0 from none: a 2-bar fade in
    CHECK(rig.player.state(0) == MusicState::FadeIn);
    rig.player.setMood(1);
    rig.steps(1);
    CHECK(rig.player.state(1) == MusicState::Playing); // into a fight: a cut, the fading-in one stopped
    CHECK(rig.player.track(1) == kTrackB);
    CHECK(moods == std::vector<int>{0, 1});
    // A mood with no track stops the music.
    rig.player.setMood(2);
    rig.steps(1);
    CHECK(rig.player.state(1) == MusicState::FadeOut);
}
