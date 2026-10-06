// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/system_music.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "scripting/sound_bindings.h"

namespace coney {

void stepSystemMusic(StoryState& story, script::SoundHost* sound, GameRandom& random, int mood) {
    if (!story.systemMusic || mood < 0 || mood >= static_cast<int>(kMusicMoods) || mood == story.musicMood) {
        return;
    }
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
    sound->playMusic(tracks.at(pick), true, {});
}

} // namespace coney
