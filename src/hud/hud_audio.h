// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace coney::hud {

/// A sound the HUD started and may stop: the sound output's own handle, 0 for none (nothing played).
using HudSoundHandle = std::uint32_t;

/// How the HUD plays sounds: `play` plays a sound by name and returns its handle (the platform connects it to
/// audio::SoundPlayer::play; the HUD lives in coney_core, which coney_audio builds on, so it takes the calls rather
/// than the player), `stop` stops a handle's sound, `playing` says whether it still plays, and `cueName` names
/// interface cue `n` (the table `SoundCfgInterfaceSound(n, name)` fills). Any may be empty: no sound, or (for
/// `playing`) a started sound counts as playing, so a looping cue is never started twice.
struct HudSound {
    std::function<HudSoundHandle(std::string_view name)> play;
    std::function<void(HudSoundHandle sound)> stop;
    std::function<bool(HudSoundHandle sound)> playing;
    std::function<std::string(int cue)> cueName;

    /// Plays interface cue `cue` once (0x11 an objective, 0x14 an announcement, 0x15 a hint), when the table names it.
    void playCue(int cue) const { static_cast<void>(startCue(cue)); }
    /// Plays interface cue `cue` and returns its handle, to stop it later (the money count's looping 0x10); 0 when the
    /// table names none or nothing played.
    [[nodiscard]] HudSoundHandle startCue(int cue) const {
        if (play && cueName) {
            if (const std::string name = cueName(cue); !name.empty()) {
                return play(name);
            }
        }
        return 0;
    }
    /// Plays the sound named `name` once (`vags/interface/rage_indicator_02`).
    void playSound(std::string_view name) const {
        if (play) {
            static_cast<void>(play(name));
        }
    }
    /// Stops `sound`; nothing for 0 or without a stop.
    void stopSound(HudSoundHandle sound) const {
        if (sound != 0 && stop) {
            stop(sound);
        }
    }
    /// Whether `sound` still plays: false for 0, true for any other handle when the output cannot tell.
    [[nodiscard]] bool isPlaying(HudSoundHandle sound) const {
        if (sound == 0) {
            return false;
        }
        return !playing || playing(sound);
    }
};

} // namespace coney::hud
