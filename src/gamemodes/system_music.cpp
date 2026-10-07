// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/system_music.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "scripting/sound_bindings.h"

namespace coney {

namespace {

// The system music's fades in bars (docs/research/sound.md#music): into the fight, back to calm, otherwise.
constexpr int kFadeIntoFight = 0;
constexpr int kFadeIntoCalm = 4;
constexpr int kFadeOther = 2;

} // namespace

void stepSystemMusic(StoryState& story, script::SoundHost* sound, GameRandom& random, int mood) {
    // The scripts' held mood (SoundSetSystemMusicState) overrides the game's.
    if (story.musicHold >= 0) {
        mood = story.musicHold;
    }
    if (!story.systemMusic || mood < 0 || mood >= static_cast<int>(kMusicMoods) || mood == story.musicMood) {
        return;
    }
    const int from = story.musicMood;
    story.musicMood = mood;
    if (sound == nullptr) {
        return;
    }
    const std::vector<std::uint32_t>& tracks = story.moodTracks.at(static_cast<std::size_t>(mood));
    if (tracks.empty()) {
        sound->stopMusic();
        return;
    }
    const auto pick = static_cast<std::size_t>(random.range(0, static_cast<std::int32_t>(tracks.size()) - 1));
    // The fade (SystemMusic_Update): a cut at the bar into the fight, 4 bars back to calm from the fight or the hunt,
    // else 2.
    int fadeBars = kFadeOther;
    if (mood == 1) {
        fadeBars = kFadeIntoFight;
    } else if (mood == 0 && (from == 1 || from == 2)) {
        fadeBars = kFadeIntoCalm;
    }
    sound->playSystemMusic(tracks.at(pick), fadeBars);
}

} // namespace coney
