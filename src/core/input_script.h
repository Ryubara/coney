// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "core/pads.h"

namespace coney {

/// One line of an input script: from frame `frame` on, do `action` on port `port`.
struct InputEvent {
    /// What the line does.
    enum class Action : std::uint8_t {
        Press,      ///< Hold `buttons` from this frame on.
        Release,    ///< Let `buttons` go from this frame on.
        Tap,        ///< Hold `buttons` for this frame only: pressed this frame, released the next.
        Stick,      ///< Move a stick: `stick` 0 left, 1 right, to `x`, `y`.
        Connect,    ///< Plug the pad in.
        Disconnect, ///< Pull the pad out.
    };

    std::uint64_t frame = 0; ///< The frame (FrameTime::index) the line takes effect on.
    std::size_t port = 0;    ///< 0 for port 1, 1 for port 2.
    Action action = Action::Press;
    std::uint16_t buttons = 0; ///< Press, Release, Tap: pad::kL2 ... pad::kLeft.
    std::uint8_t stick = 0;    ///< Stick: 0 the left stick, 1 the right.
    int x = 0;                 ///< Stick: -100 (full left) to 100 (full right).
    int y = 0;                 ///< Stick: -100 (full down) to 100 (full up).
};

/// Parses an input script: the scripted input of test mode, a Coney format (docs/guides/building.md#input-scripts).
///
/// One command per line, `FRAME [p1|p2] ACTION ARGS...`; `#` starts a comment and blank lines are skipped:
///
///     # FRAME [PORT] ACTION ARGS
///     150 tap start
///     160 press down
///     175 release down
///     180 p2 connect
///     200 stick left 0 100
///
/// ACTION is `press`, `release` or `tap` followed by button names (`cross`, `circle`, `triangle`, `square`, `l1`,
/// `r1`, `l2`, `r2`, `l3`, `r3`, `start`, `select`, `up`, `down`, `left`, `right`), `stick left|right X Y` with X and
/// Y whole numbers from -100 to 100 (y up), `connect` or `disconnect`. The port defaults to p1. Frames must not
/// decrease from one line to the next. Fails with ErrorCode::Invalid naming the line for anything else.
[[nodiscard]] std::expected<std::vector<InputEvent>, Error> parseInputScript(std::string_view text);

/// Reads and parses the input script at `path`. Fails with ErrorCode::NotFound or Io when the file cannot be read,
/// and as parseInputScript does for its content.
[[nodiscard]] std::expected<std::vector<InputEvent>, Error> loadInputScript(const std::string& path);

/// The raw stick byte for a script's deflection, -100 to 100 (CONEY_ASSERT otherwise): 0 at -100, 255 at 100 and
/// pad::kStickCentre at 0, with the steps in between spread over the live range outside the dead zone so that
/// pad::stickValue() gives back about value / 100.
[[nodiscard]] std::uint8_t stickByteFromPercent(int value);

/// An input source that plays an input script: deterministic input for tests and headless runs, with no device and
/// no human. Port 1 starts connected and port 2 disconnected; nothing is held and the sticks are centred. A held
/// button has full pressure (255), as a digital button would.
class ScriptedInput final : public InputSource {
  public:
    /// Plays `events`, which must be in frame order (parseInputScript() guarantees it; CONEY_ASSERT otherwise).
    explicit ScriptedInput(std::vector<InputEvent> events);

    /// Applies every line up to and including `frame` and returns the result. Frames must be asked for in
    /// increasing order (CONEY_ASSERT); a frame skipped still has its lines applied, in order.
    [[nodiscard]] PortSamples sample(std::uint64_t frame) override;

  private:
    // Applies one line to the port state.
    void apply(const InputEvent& event);

    std::vector<InputEvent> m_events;
    std::size_t m_next = 0; // the first line not yet applied
    PortSamples m_state{};  // the ports as the lines so far leave them, pressures not yet filled in
    std::array<std::uint16_t, kPadPorts> m_tapped{}; // buttons to release before the next frame
    std::uint64_t m_nextFrame = 0;                   // the lowest frame the next sample() may ask for
};

} // namespace coney
