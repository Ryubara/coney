// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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
/// (null plays nothing), or the music stops when the mood has none. **Coney choices**: the pick draws from the game's
/// random index; the fades between tracks are the sound host's.
/// @orig 0x0041a060 GameState_UpdateSystemMusic (unknown)
void stepSystemMusic(StoryState& story, script::SoundHost* sound, GameRandom& random, int mood);

} // namespace coney
