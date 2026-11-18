// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

// @coney-only the Coney console's terminal model (docs/guides/console.md#terminal); the original has no console.

namespace coney::console {

/// The output lines the terminal keeps; the run log keeps them all.
inline constexpr std::size_t kOutputLinesMax = 2000;
/// The lines of history kept, here and in `console-history.txt`.
inline constexpr std::size_t kHistoryLinesMax = 200;

/// What an output line is, for the front end's colours.
enum class LineKind : std::uint8_t {
    Echo,   ///< The line as entered.
    Result, ///< A command's output.
    Error,  ///< What was wrong, and what to type instead.
};

/// One line of the terminal's output.
struct OutputLine {
    std::string text;
    LineKind kind = LineKind::Result;
    std::string source; ///< The mod whose command printed it; empty for Coney's own.
};

/// The terminal's text, without a user interface: the output above the line and the history of lines entered
/// (docs/guides/console.md#terminal). The front end draws it; reading and writing the history file is the console's,
/// through its HistoryStore.
class Terminal {
  public:
    /// Adds `text` to the output, one line for each line of it (an empty text adds one empty line), dropping the
    /// oldest past kOutputLinesMax.
    void print(std::string_view text, LineKind kind = LineKind::Result, std::string_view source = {});
    /// Empties the output.
    void clear() { m_output.clear(); }
    /// The output, oldest first.
    [[nodiscard]] const std::deque<OutputLine>& output() const { return m_output; }

    /// Adds an entered line to the history, newest last, unless it is blank or repeats the newest; the oldest goes
    /// past kHistoryLinesMax. Ends any walk.
    void remember(std::string_view line);
    /// The history, oldest first.
    [[nodiscard]] const std::deque<std::string>& history() const { return m_history; }
    /// Up: the line before the one shown (the newest, on the first press), or nothing at the oldest.
    [[nodiscard]] std::optional<std::string_view> previous();
    /// Down: the line after the one shown, or nothing past the newest (back to an empty line).
    [[nodiscard]] std::optional<std::string_view> next();

    /// Replaces the history with the lines of `text` (a history file's), the last kHistoryLinesMax of them.
    void loadHistory(std::string_view text);
    /// The history as a history file holds it: one line each, each ended by a line break.
    [[nodiscard]] std::string historyText() const;

  private:
    std::deque<OutputLine> m_output;
    std::deque<std::string> m_history;
    std::size_t m_walk = 0; // how many lines back the walk is; 0 when not walking
};

} // namespace coney::console
