// SPDX-License-Identifier: GPL-3.0-or-later
// The radios (docs/research/sound.md#radios): SetupRadio's start states, a track played at the radio while the player
// is within 40 m, the DJ link and next track after it, the announcement when the player walks up, the scene's half
// volume, the retune and the switch, and the tables' names. A fake sound stands in for the game's.
#include "world_objects/radios.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/game_random.h"
#include "core/name_hash.h"

using coney::GameRandom;
using coney::world_objects::Radio;
using coney::world_objects::Radios;
using coney::world_objects::RadioWorld;

namespace {

// Sounds that play until ended() is called for them.
class FakeSound final : public coney::world_objects::RadioSound {
  public:
    double play(std::uint32_t hash, const std::array<float, 3>& /*position*/) override {
        played.push_back(hash);
        if (hash == 0) {
            return 0.0;
        }
        live[++next] = hash;
        return next;
    }
    [[nodiscard]] bool playing(double handle) const override { return live.contains(handle); }
    void stop(double handle) override { live.erase(handle); }
    void follow(double /*handle*/, const std::array<float, 3>& /*position*/, float volume) override {
        lastVolume = volume;
    }
    // Every sound playing ends.
    void endAll() { live.clear(); }

    std::vector<std::uint32_t> played;
    std::map<double, std::uint32_t> live;
    double next = 0;
    float lastVolume = 0.0F;
};

// The radio's object, at the origin.
constexpr double kBox = 9.0;
std::optional<std::array<float, 3>> locate(double object) {
    return object == kBox ? std::optional(std::array<float, 3>{0, 0, 0}) : std::nullopt;
}

// A world with the player `metres` from the radio and no level complete.
RadioWorld playerAt(float metres) {
    RadioWorld world;
    world.player = std::array<float, 3>{metres, 0, 0};
    world.playerHandle = 1;
    world.levelComplete = [](int) { return false; };
    return world;
}

} // namespace

TEST_CASE("the radio tables are vags/music names, with the hashes the page gives for the others", "[radios]") {
    CHECK(Radios::tracks().at(0) == coney::crc32("vags/music/echoes_in_my_mind"));
    CHECK(Radios::tracks().at(5) == 0x60b5c0c3U);
    CHECK(Radios::tracks().at(20) == coney::crc32("vags/music/tna_funk"));
    CHECK(Radios::clip(3, 0) == Radios::tracks().at(13));
    CHECK(Radios::clip(3, 12) == coney::crc32("vags/music/djlady_12"));
    CHECK(Radios::clip(2, 6) == coney::crc32("vags/music/dj_rumble_08"));
    CHECK(Radios::clip(2, 8) == coney::crc32("vags/music/dj_rumble_07"));
    CHECK(Radios::clip(1, 0) == 0); // not named
}

TEST_CASE("SetupRadio starts a radio playing, or off with no track", "[radios]") {
    Radios radios;
    radios.setup(kBox, "onPick", 3, "onSegment", 0);
    radios.setup(kBox + 1, "", 0, "", 0);
    REQUIRE(radios.find(kBox) != nullptr);
    CHECK(radios.find(kBox)->state == Radios::kStart);
    CHECK(radios.find(kBox + 1)->state == Radios::kSwitchOff);
    radios.setup(kBox + 2, "", 2, "", 4);
    CHECK(radios.find(kBox + 2)->announcementArmed);
    CHECK(radios.find(kBox + 2)->next == 4);
}

TEST_CASE("a radio plays its track within 40 m, then a DJ link, then the next track", "[radios]") {
    Radios radios;
    FakeSound sound;
    GameRandom random;
    std::vector<std::string> calls;
    const auto call = [&calls](const std::string& function) { calls.push_back(function); };
    radios.setup(kBox, "", 3, "segment", 0);
    // Too far: nothing.
    radios.update(locate, playerAt(50.0F), sound, random, call);
    CHECK(sound.played.empty());
    // Within 40 m: the track plays and follows the radio, at full volume and half in a scene.
    radios.update(locate, playerAt(20.0F), sound, random, call);
    REQUIRE(sound.played.size() == 1);
    CHECK(sound.played[0] == Radios::tracks().at(3));
    CHECK(radios.find(kBox)->state == Radios::kTrack);
    RadioWorld scene = playerAt(20.0F);
    scene.scene = true;
    radios.update(locate, scene, sound, random, call);
    CHECK(sound.lastVolume == Radios::kSceneVolume);
    // The track ends: a DJ link is picked and the next track drawn (one of 12), the clip plays and the callback runs.
    sound.endAll();
    radios.update(locate, playerAt(20.0F), sound, random, call);
    const Radio& radio = *radios.find(kBox);
    CHECK(radio.state == Radios::kClip);
    CHECK(radio.next >= 0);
    CHECK(radio.next < 12);
    radios.update(locate, playerAt(20.0F), sound, random, call);
    CHECK(radio.state == Radios::kClipPlaying);
    CHECK(calls == std::vector<std::string>{"segment"});
    // The clip ends: the next track becomes current and plays.
    sound.endAll();
    radios.update(locate, playerAt(20.0F), sound, random, call);
    CHECK(radio.state == Radios::kNextTrack);
    const int next = radio.next;
    radios.update(locate, playerAt(20.0F), sound, random, call);
    CHECK(radio.track == next);
    CHECK(calls.size() == 2);
    radios.update(locate, playerAt(20.0F), sound, random, call);
    CHECK(radio.state == Radios::kTrack);
    CHECK(sound.played.back() == Radios::tracks().at(static_cast<std::size_t>(next)));
}

TEST_CASE("an armed announcement plays when the player comes within 5 m", "[radios]") {
    Radios radios;
    FakeSound sound;
    GameRandom random;
    radios.setup(kBox, "", 2, "", 4);
    radios.update(locate, playerAt(20.0F), sound, random, {});
    CHECK(sound.played.back() == Radios::tracks().at(2));
    radios.update(locate, playerAt(4.0F), sound, random, {});
    const Radio& radio = *radios.find(kBox);
    CHECK_FALSE(radio.announcementArmed);
    CHECK(radio.state == Radios::kClipPlaying);
    CHECK(sound.played.back() == coney::crc32("vags/music/djlady_04"));
    CHECK(sound.live.size() == 1); // the track was stopped
}

TEST_CASE("Radio_SetMode retunes a playing radio and switches it off", "[radios]") {
    Radios radios;
    FakeSound sound;
    GameRandom random;
    radios.setup(kBox, "", 3, "", 0);
    radios.update(locate, playerAt(10.0F), sound, random, {});
    radios.setMode(kBox, 1, sound);
    CHECK(sound.live.empty());
    radios.update(locate, playerAt(10.0F), sound, random, {});
    CHECK(sound.played.back() == 0x09a4be6aU); // the retune sound
    sound.endAll();
    radios.update(locate, playerAt(10.0F), sound, random, {});
    radios.update(locate, playerAt(10.0F), sound, random, {});
    CHECK(radios.find(kBox)->state == Radios::kStart);
    radios.setMode(kBox, 0, sound);
    radios.update(locate, playerAt(10.0F), sound, random, {});
    CHECK(sound.played.back() == 0x5b601235U); // the switch click
    CHECK(radios.find(kBox)->track == -1);
    CHECK(radios.find(kBox)->state == Radios::kOff);
}
