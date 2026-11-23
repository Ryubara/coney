// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "core/pad.h"
#include "core/pads.h"

namespace coney {

/// Turns the pads' samples, one set per step, into an input script (input_script.h): a line for each change from the
/// step before and none for a step that holds the pad as it was, so playing the text back through ScriptedInput
/// gives the same samples on every step. A bug report's recording is that text, and an agent replays it with
/// `--input-script` to reach the reporter's state (docs/guides/run-log.md#input-recording).
///
/// It starts from the state a script starts from (port 1 plugged in and at rest, port 2 unplugged), so a first sample
/// that differs gets its lines on its own step. A port that is unplugged keeps its buttons and sticks as they were
/// until it is plugged in again, as a script's do; nothing about it is written while it is out. Coney's own debug
/// tooling: the original has nothing like it.
class InputRecorder {
  public:
    InputRecorder();

    /// The script lines that take the pads from the last recorded sample to `samples` on step `frame`, each ending in
    /// a line break; empty when nothing changed. Frames must not go down from one call to the next.
    [[nodiscard]] std::string record(std::uint64_t frame, const PortSamples& samples);

  private:
    /// What the replay's pad holds for one port after the lines so far.
    struct Port {
        PadSample pad;
        /// The pressure bytes a `pressure` line holds; none: the button's state decides.
        std::array<std::optional<std::uint8_t>, pad::kPressureCount> pressure{};
    };

    // Appends the lines for one port's change to `out`.
    void recordPort(std::string& out, std::uint64_t frame, std::size_t port, const PadSample& sample);

    std::array<Port, kPadPorts> m_ports;
};

/// What a recording's header says about the run it starts: the seed the AI's random draws were varied by
/// (`--ai-seed`) and, for a run that started in a level, its name and checkpoint, so whoever replays it can start the
/// same way. Names and numbers only, never a path (logs go into public issues).
struct InputRecordingHeader {
    std::uint32_t aiSeed = 0;      ///< `--ai-seed`, 0 for the usual run.
    std::string level;             ///< `--play-level`'s level; empty for a run from the front end.
    std::optional<int> checkpoint; ///< `--checkpoint`, when given.
};

/// The comment lines a recording starts with, each ending in a line break: `# coney input recording 1`,
/// `# ai-seed N` (which `--input-script` plays back through inputScriptSeed()), and the level and checkpoint when
/// there are some. A level name with anything but letters, digits and `_ : . -` is left out.
[[nodiscard]] std::string inputRecordingHeader(const InputRecordingHeader& header);

/// An input source that records what it hands the game: it asks `inner` for each step, writes the changes since the
/// step before through `sink` (InputRecorder) and gives the samples on. Wrapped around the pads the game reads, it
/// holds what the game saw, whatever drove it (a script, a driver program, a person's gamepad), so the recording
/// replays the run. Port 1's and 2's samples are the local pads' only: what a network guest sends is not in it.
///
/// With `started` it begins at the first step it says yes for and counts that step as 0, as DeferredInput starts a
/// script, so a `--play-level` run's recording replays from the level's first step of play.
class RecordingInput final : public InputSource {
  public:
    /// Where the lines go: each call is whole lines, ending in a line break.
    using Sink = std::function<void(std::string_view)>;

    /// Records `inner` (which must outlive this) into `sink`. Ends with a `# frames N` comment, N the steps recorded.
    RecordingInput(InputSource& inner, Sink sink, std::function<bool()> started = {});
    ~RecordingInput() override;
    RecordingInput(const RecordingInput&) = delete;
    RecordingInput& operator=(const RecordingInput&) = delete;
    RecordingInput(RecordingInput&&) = delete;
    RecordingInput& operator=(RecordingInput&&) = delete;

    /// `inner`'s samples for `frame`, recorded.
    [[nodiscard]] PortSamples sample(std::uint64_t frame) override;

    /// How many steps have been recorded.
    [[nodiscard]] std::uint64_t framesRecorded() const { return m_recorded; }

  private:
    InputSource& m_inner;
    Sink m_sink;
    std::function<bool()> m_started;
    InputRecorder m_recorder;
    std::optional<std::uint64_t> m_origin; // the step recording began on
    std::uint64_t m_recorded = 0;
};

} // namespace coney
