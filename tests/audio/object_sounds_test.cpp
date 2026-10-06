// SPDX-License-Identifier: GPL-3.0-or-later
// The glass panes' and doors' sounds through the SoundPlayer (docs/research/objects.md#coneys-implementation): a name
// hash plays the sound registered under it on the effects bus, and what Coney cannot play yet is counted.
#include "audio/object_sounds.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "audio/audio_format.h"
#include "audio/mixer.h"
#include "audio/pcm_sound.h"
#include "audio/sound_player.h"

using coney::audio::Bus;
using coney::audio::Mixer;
using coney::audio::ObjectSounds;
using coney::audio::PcmSound;
using coney::audio::SoundPlayer;

TEST_CASE("the objects' sounds play by name hash on the effects bus", "[audio][objects]") {
    Mixer mixer;
    SoundPlayer player(mixer);
    auto sound = PcmSound::create(std::vector<std::int16_t>(64, 400), 1, coney::audio::kOutputRate);
    REQUIRE(sound.has_value());
    const coney::audio::SoundId door = coney::audio::soundIdOf("vags/test/door");
    player.add(door, std::make_shared<const PcmSound>(std::move(*sound)));

    ObjectSounds sounds;
    sounds.playSound(door, {}); // no player yet: nothing
    sounds.setPlayer(&player);
    player.setBusVolume(Bus::Sfx, 0.5F);
    sounds.playSound(door, {});
    sounds.playSound(0x12345678U, {}); // no sound under that id
    std::vector<std::int16_t> out(2);
    mixer.mix(out);
    CHECK(out == std::vector<std::int16_t>{200, 200});
    CHECK(sounds.played() == 3);
    CHECK(player.missing() == 1);

    sounds.playMaterialPair(2, 5, {});
    sounds.lockPickClick(1.0);
    CHECK(sounds.unplayed() == 2);
}
