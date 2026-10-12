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
/// at him, with the name of a script callback to run when it ends. A line is cut in on by the rule of
/// docs/research/sound.md#speech-cut-in: the human keeps a "may be cut" flag (the original's `+0x194`) and his
/// callback, so a stop never drops the callback and an unforced stop leaves a line that may not be cut alone.
///
/// **Coney's stand-ins:** a line follows its speaker each update (the original plays it at the human; how it follows
/// him is not traced); the humans' speech update runs in the order the humans first spoke.
class Speech {
  public:
    /// A line that ended this update: the callback to run, with its argument (none: called with no argument).
    struct Ended {
        std::string callback;
        std::optional<double> arg;
    };

    /// The two ways a line starts (docs/research/sound.md#speech-cut-in).
    enum class Start : std::uint8_t {
        /// `HuSpeak`, the vocal ids that do not cut, commands said uncut: plays only when no line is alive and the
        /// human's line may be cut.
        Waiting,
        /// `HuSpeakNI`, `die`, `expire`, commands said cutting: plays unless a line that may not be cut is alive;
        /// stops the line (unforced) and from then on the human's line may not be cut.
        Cutting,
    };

    /// Plays the line `hash` at `at` for `human` at `volume` (`duckable` false: a line that ducks the others), started
    /// as `start` says. The line's handle, invalid when nothing plays: then the human's line, callback and flag are as
    /// they were (the caller runs its own callback at once). A line that plays writes its callback (or none) over the
    /// human's.
    /// @orig 0x0021e400 Human_PlaySpeech (unknown)
    /// @orig 0x0021e698 Human_PlaySpeechCutting (unknown)
    SoundHandle say(SoundEngine& engine, double human, std::uint32_t hash, const SpeakerPlace& at, Start start,
                    std::string callback, std::optional<double> arg, float volume = 1.0F, bool duckable = true);
    /// Whether `human` is saying a line.
    [[nodiscard]] bool speaking(const SoundEngine& engine, double human) const;
    /// Stops the line `human` is saying: nothing when none is alive, nor, without `force`, while the line may not be
    /// cut. A stop ends the stream and lets the human's line be cut again, but never touches his callback, which the
    /// next update runs.
    /// @orig 0x0021ec38 Human_StopSpeech (unknown)
    void shutUp(SoundEngine& engine, double human, bool force);
    /// Moves each live line to its speaker (found with `locate`) and returns the callbacks of the humans whose line is
    /// gone (ended or stopped), forgetting them. A human with no line and no callback may be cut again.
    /// @orig 0x0021e940 Human_UpdateSpeech (unknown)
    std::vector<Ended> update(SoundEngine& engine, const SpeakerLocator& locate);
    /// Stops every line (when there is an engine) and forgets them, callbacks and all.
    void clear(SoundEngine* engine);
    /// Lines being said.
    [[nodiscard]] std::size_t lines() const;
    /// Lines started so far, for the test mode's summary.
    [[nodiscard]] std::uint64_t said() const { return m_said; }

  private:
    // One human's speech: his current line (if any), the flag and the callback that outlive a line.
    struct Voice {
        double human = 0.0;
        SoundHandle sound;         // the line; invalid once it has ended or been stopped
        bool mayCut = true;        // the original's +0x194: a stop or a cutting start may end the line
        std::string callback;      // the original's +0x188: run when the line is gone, once
        std::optional<double> arg; // the original's +0x190
    };

    // The human's voice, null when he has none.
    [[nodiscard]] Voice* find(double human);
    [[nodiscard]] const Voice* find(double human) const;
    // Whether the voice's line is alive.
    [[nodiscard]] static bool alive(const SoundEngine& engine, const Voice& voice);
    // The stop of shutUp(), for a voice whose line is alive.
    static void stop(SoundEngine& engine, Voice& voice);

    std::vector<Voice> m_voices;
    std::uint64_t m_said = 0; // said()
};

} // namespace coney::audio
