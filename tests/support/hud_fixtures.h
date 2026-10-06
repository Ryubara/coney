// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "hud/hud_audio.h"

namespace coney::test {

/// A sound output that keeps what the HUD asked for: the interface cues by number (its cue table names none, so a cue
/// plays nothing further) and the sounds played by name. `sound` is what the HUD is given.
struct KeepingAudio {
    std::vector<int> cues;
    std::vector<std::string> sounds;
    hud::HudSound sound;

    KeepingAudio() {
        sound.play = [this](std::string_view name) { sounds.emplace_back(name); };
        sound.cueName = [this](int cue) {
            cues.push_back(cue);
            return std::string();
        };
    }
    ~KeepingAudio() = default;
    KeepingAudio(const KeepingAudio&) = delete;
    KeepingAudio& operator=(const KeepingAudio&) = delete;
    KeepingAudio(KeepingAudio&&) = delete;
    KeepingAudio& operator=(KeepingAudio&&) = delete;
};

/// No sound.
inline const hud::HudSound kSilent{};

} // namespace coney::test
