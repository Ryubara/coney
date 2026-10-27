// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "core/game_random.h"
#include "warriors/story_state.h"

// The system music: the game's own mood music, which plays a random track of the mood the player's surroundings
// give (calm, fight or hunted) while the scripts leave it on (`SoundEnableSystemMusic`, `SoundSetMusicTrack`).
// Research: docs/references/bindings/sound.md#soundenablesystemmusic, docs/research/sound.md#music-player

namespace coney::script {
class SoundHost;
} // namespace coney::script

namespace coney {

/// One frame of the system music with the player's surroundings in `mood` (0 calm, 1 fight, 2 hunted): while it is
/// on and the mood differs from the one playing (or a new pick is due), a random track of the mood loops on `sound`
/// (null plays nothing), or the music stops when the mood has none. A mood the scripts hold
/// (`SoundSetSystemMusicState`) takes the place of `mood`. The new track cross-fades on the playing track's bar: a
/// cut into the fight, 4 bars back to calm from the fight or the hunt, else 2. **Coney choices**: the pick draws from
/// the game's random index; the fades between tracks are the sound host's.
/// @orig 0x0041a060 GameState_UpdateSystemMusic (unknown)
void stepSystemMusic(StoryState& story, script::SoundHost* sound, GameRandom& random, int mood);

/// Turns the system music on or off: the scripts' hold on the mood ends either way; a change also asks for a new pick
/// and, turning it off, stops the music on `sound` (null stops nothing). **Coney's choice**: the pick is the next
/// step's (stepSystemMusic()), where the original picks at once.
/// @orig 0x0041a008 GameState_SetSystemMusic (unknown)
void setSystemMusic(StoryState& story, script::SoundHost* sound, bool on);

/// Gives the mood `mood` (0-2) the tracks `tracks`, at most kMoodTracks (the rest are dropped). When it is the mood
/// playing, a new pick is due and the system music is turned on (setSystemMusic()).
/// @orig 0x0010e9e0 SystemMusic_SetTracks (unknown)
void setMoodTracks(StoryState& story, script::SoundHost* sound, std::size_t mood,
                   std::span<const std::uint32_t> tracks);

} // namespace coney
