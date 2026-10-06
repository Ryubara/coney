// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "audio/sound_engine.h"

namespace coney::audio {

/// Where a speaking human is: his position and the way he faces (a voice line is directional).
struct SpeakerPlace {
    SoundVec position{};
    SoundVec facing{0.0F, 1.0F, 0.0F};
};

/// Finds a human by his script handle; nothing when the handle names no human.
using SpeakerLocator = std::function<std::optional<SpeakerPlace>(double human)>;

/// The humans' lines (docs/research/sound.md#speech): each human says one line at a time, positional and directional
/// at him, owned by him (so a new line of his can cut it off), with the name of a script callback to run when it ends.
///
/// **Coney's stand-ins:** a line follows its speaker each update (the original plays it at the human; how it follows
/// him is not traced); a line stopped by HuShutUp or cut off by a new one drops its callback.
class Speech {
  public:
    /// A line that ended this update: the callback to run, with its argument (none: called with no argument).
    struct Ended {
        std::string callback;
        std::optional<double> arg;
    };

    /// Plays the line `hash` at `at` for `human`. While he says another line, `interrupt` cuts it off (its callback
    /// dropped); without it nothing plays. The line's handle, invalid when nothing plays.
    /// @orig 0x0021e400 Human_PlaySpeech (unknown)
    /// @orig 0x0021e698 Human_PlaySpeechInterrupt (unknown)
    SoundHandle say(SoundEngine& engine, double human, std::uint32_t hash, const SpeakerPlace& at, bool interrupt,
                    std::string callback, std::optional<double> arg);
    /// Whether `human` is saying a line.
    [[nodiscard]] bool speaking(const SoundEngine& engine, double human) const;
    /// Stops the line `human` is saying (its callback dropped).
    /// @orig 0x0021ec38 Human_StopSpeech (unknown)
    void shutUp(SoundEngine& engine, double human);
    /// Moves each line to its speaker (found with `locate`) and returns the lines that ended, oldest first, forgetting
    /// them.
    std::vector<Ended> update(SoundEngine& engine, const SpeakerLocator& locate);
    /// Stops every line (when there is an engine) and forgets them, callbacks and all.
    void clear(SoundEngine* engine);
    /// Lines being said.
    [[nodiscard]] std::size_t lines() const { return m_lines.size(); }

  private:
    // One human's line.
    struct Line {
        double human = 0.0;
        SoundHandle sound;
        std::string callback;
        std::optional<double> arg;
    };

    std::vector<Line> m_lines;
};

} // namespace coney::audio
