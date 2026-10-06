// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "audio/pcm_sound.h"

namespace coney::audio {

/// The shape of a synthesised tone sweep.
struct ToneSweep {
    float lowHz = 220.0F;    ///< Where the sweep starts and ends.
    float highHz = 880.0F;   ///< The top it reaches halfway.
    float seconds = 3.0F;    ///< The whole sweep, up and back down.
    float amplitude = 0.4F;  ///< Peak level, 0 to 1 of full scale.
    int sampleRate = 22'050; ///< Below the output rate on purpose, so playing it exercises the resampler.
};

/// A mono sine that glides from `lowHz` up to `highHz` and back (evenly in pitch), looping over its whole length:
/// Coney's own test signal for hearing the audio path (`--audio-test`, the debug menu's Audio page). Synthesised,
/// never game data. The phase runs on without a jump at the loop, so it loops without a click.
[[nodiscard]] PcmSound makeToneSweep(const ToneSweep& sweep = {});

} // namespace coney::audio
