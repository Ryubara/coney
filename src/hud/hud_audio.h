// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace coney::hud {

/// How the HUD plays sounds: `play` plays a sound by name (the platform connects it to audio::SoundPlayer::play; the
/// HUD lives in coney_core, which coney_audio builds on, so it takes the call rather than the player), and `cueName`
/// names interface cue `n` (the table `SoundCfgInterfaceSound(n, name)` fills). Either may be empty: no sound.
struct HudSound {
    std::function<void(std::string_view name)> play;
    std::function<std::string(int cue)> cueName;

    /// Plays interface cue `cue` once (0x10 money counting, 0x11 an objective, 0x14 an announcement, 0x15 a hint),
    /// when the table names it.
    void playCue(int cue) const {
        if (play && cueName) {
            if (const std::string name = cueName(cue); !name.empty()) {
                play(name);
            }
        }
    }
    /// Plays the sound named `name` once (`vags/interface/rage_indicator_02`).
    void playSound(std::string_view name) const {
        if (play) {
            play(name);
        }
    }
};

} // namespace coney::hud
