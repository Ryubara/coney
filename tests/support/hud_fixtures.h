// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "hud/hud_audio.h"

namespace coney::test {

/// A sound output that keeps what the HUD asked for: the interface cues by number (its cue table names cue `n` "cue
/// n") and the other sounds played by name. Every play gets a handle (1, 2, ...) that plays until the HUD stops it or
/// the test ends it (end()). `sound` is what the HUD is given.
struct KeepingAudio {
    std::vector<int> cues;
    std::vector<std::string> sounds;          ///< The sounds played by name, not as cues.
    std::vector<hud::HudSoundHandle> live;    ///< The handles still playing.
    std::vector<hud::HudSoundHandle> stopped; ///< The handles the HUD stopped, in order.
    hud::HudSoundHandle next = 1;
    hud::HudSound sound;

    KeepingAudio() {
        sound.play = [this](std::string_view name) {
            if (!name.starts_with("cue ")) {
                sounds.emplace_back(name);
            }
            live.push_back(next);
            return next++;
        };
        sound.stop = [this](hud::HudSoundHandle handle) {
            if (std::erase(live, handle) > 0) {
                stopped.push_back(handle);
            }
        };
        sound.playing = [this](hud::HudSoundHandle handle) { return std::ranges::find(live, handle) != live.end(); };
        sound.cueName = [this](int cue) {
            cues.push_back(cue);
            return "cue " + std::to_string(cue);
        };
    }
    ~KeepingAudio() = default;
    KeepingAudio(const KeepingAudio&) = delete;
    KeepingAudio& operator=(const KeepingAudio&) = delete;
    KeepingAudio(KeepingAudio&&) = delete;
    KeepingAudio& operator=(KeepingAudio&&) = delete;

    /// Ends `handle`'s sound as if it had played out or been stolen, not by the HUD.
    void end(hud::HudSoundHandle handle) { std::erase(live, handle); }
};

/// No sound.
inline const hud::HudSound kSilent{};

} // namespace coney::test
