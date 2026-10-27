// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/system_music.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
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

void setSystemMusic(StoryState& story, script::SoundHost* sound, bool on) {
    // Either way the scripts' hold on the mood ends (the hold flag cleared, the held mood handed back to the game).
    story.musicHold = -1;
    if (on == story.systemMusic) {
        return;
    }
    story.systemMusic = on;
    // Coney's choice: a new pick is due at the next step (stepSystemMusic), where the original picks at once; the
    // mood is the same, a step later.
    story.musicMood = -1;
    if (!on && sound != nullptr) {
        sound->stopMusic();
    }
}

void setMoodTracks(StoryState& story, script::SoundHost* sound, std::size_t mood,
                   std::span<const std::uint32_t> tracks) {
    std::vector<std::uint32_t>& kept = story.moodTracks.at(mood);
    kept.assign(tracks.begin(), tracks.begin() + static_cast<std::ptrdiff_t>(std::min(tracks.size(), kMoodTracks)));
    // The mood playing changes at the next step, and the system music is on for it.
    if (std::cmp_equal(story.musicMood, mood)) {
        story.musicMood = -1;
        setSystemMusic(story, sound, true);
    }
}

} // namespace coney
