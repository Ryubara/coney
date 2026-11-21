// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "core/error.h"
#include "core/pad.h"

// A host's session record (`--net-record FILE`) and its replay (`--net-replay FILE`): each step the host played, the
// pad on the remote player's port, and the session's events as notes. The remote pad is the only thing a session
// changes in the host's game, so a headless run with the same input script and the recorded pads replayed on that port
// gives the host's digests again. Coney's own; the original has no network play (docs/guides/multiplayer.md#replays).

namespace coney::net {

/// One line of a session record.
/// @coney-only network play; the original has none
struct RecordLine {
    /// What the line holds.
    enum class Kind : std::uint8_t {
        Pad,   ///< the remote port's pad the step played
        Event, ///< a note of what happened in the session (a join, a leave); a replay ignores it
        Hold,  ///< the host held the step for the loading barrier: it read its pads but ran nothing
    };
    Kind kind = Kind::Pad;
    std::uint32_t step = 0; ///< the host's step, from 0
    PadSample pad;          ///< Pad: the pad
    std::string text;       ///< Event: what happened
};

/// `pad STEP 0` for an unplugged pad, else `pad STEP 1 BUTTONS STICKS PRESSURES`: the buttons as 4 hex digits, the four
/// stick bytes as 8 and the 12 pressure bytes as 24, all lowercase.
/// @coney-only network play; the original has none
[[nodiscard]] std::string formatPadLine(std::uint32_t step, const PadSample& pad);

/// `event STEP TEXT`; `text` is one line, not empty (CONEY_ASSERT).
/// @coney-only network play; the original has none
[[nodiscard]] std::string formatEventLine(std::uint32_t step, std::string_view text);

/// `hold STEP`: the host's game held step `step` (its pads read, nothing run) while a guest loaded its level
/// (docs/guides/multiplayer.md#levels), so a replay holds it too.
/// @coney-only network play; the original has none
[[nodiscard]] std::string formatHoldLine(std::uint32_t step);

/// Reads a formatPadLine(), formatEventLine() or formatHoldLine() line, strictly: Invalid for anything else.
/// @coney-only network play; the original has none
[[nodiscard]] std::expected<RecordLine, Error> parseRecordLine(std::string_view text);

/// A session record read back for a replay: the remote pad of each step it holds, and the steps the host held.
/// @coney-only network play; the original has none
class SessionReplay {
  public:
    /// Reads a record's text, a line each (a last line without its newline counts; empty lines are skipped). Invalid,
    /// naming the line, for a malformed line, a step's pad recorded twice or a step held twice.
    [[nodiscard]] static std::expected<SessionReplay, Error> parse(std::string_view text);

    /// The pad step `step` played on the remote port, when the record holds that step.
    [[nodiscard]] std::optional<PadSample> pad(std::uint32_t step) const;
    /// Whether the host held step `step` for the loading barrier, so the replay holds it too.
    [[nodiscard]] bool held(std::uint32_t step) const { return m_held.contains(step); }
    /// How many steps the record holds.
    [[nodiscard]] std::size_t steps() const { return m_pads.size(); }

  private:
    std::map<std::uint32_t, PadSample> m_pads;
    std::set<std::uint32_t> m_held;
};

} // namespace coney::net
