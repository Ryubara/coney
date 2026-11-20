// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "console/console.h"

// @coney-only the Coney console's terminal line (docs/guides/console.md#front-end); the original has no console.

namespace coney::console {

/// The terminal's line as the keyboard edits it, without a user interface: the text, the candidates shown above it
/// while typing, Tab's completion, Up and Down through the history, and Enter (docs/guides/studio/console.md#terminal).
/// The F8 bar draws it and feeds it the keys; the logic lives here so it is tested without Dear ImGui.
class LineEditor {
  public:
    /// An empty line over `console`, which must outlive it.
    explicit LineEditor(Console& console) : m_console(console) {}

    /// The line as typed so far.
    [[nodiscard]] const std::string& text() const { return m_text; }
    /// Replaces the line (a key typed, a paste) and lists its candidates again.
    void setText(std::string_view text);
    /// The completions of the word being typed, each as the whole line would read with it (Console::complete()); empty
    /// for an empty line, so nothing shows before the first key.
    [[nodiscard]] const std::vector<std::string>& candidates() const { return m_candidates; }

    /// Tab: one candidate takes its place, followed by a space for the next word; several extend the line to what they
    /// all begin with. Returns whether the line changed.
    bool complete();
    /// Up: the line entered before the one shown; nothing changes at the oldest.
    void previous();
    /// Down: the line entered after the one shown, or an empty line past the newest.
    void next();
    /// Enter: hands the line to the console as typed by `slot`'s player and empties it.
    Outcome submit(PlayerSlot slot = 0);

  private:
    Console& m_console;
    std::string m_text;
    std::vector<std::string> m_candidates;
};

} // namespace coney::console
