// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/line_editor.h"

#include <algorithm>
#include <cstddef>
#include <optional>

namespace coney::console {

namespace {

// How many leading characters every one of `lines` shares.
std::size_t sharedPrefix(const std::vector<std::string>& lines) {
    std::size_t length = lines.front().size();
    for (const std::string& line : lines) {
        const auto [first, ignored] = std::ranges::mismatch(lines.front(), line);
        length = std::min(length, static_cast<std::size_t>(first - lines.front().begin()));
    }
    return length;
}

} // namespace

void LineEditor::setText(std::string_view text) {
    m_text = text;
    // Nothing before the first key: an empty line would list every command.
    m_candidates = m_text.empty() ? std::vector<std::string>() : m_console.complete(m_text);
}

bool LineEditor::complete() {
    const std::vector<std::string> candidates = m_console.complete(m_text);
    if (candidates.empty()) {
        return false;
    }
    std::string completed;
    if (candidates.size() == 1) {
        completed = candidates.front() + " ";
    } else {
        completed = candidates.front().substr(0, sharedPrefix(candidates));
    }
    if (completed.size() <= m_text.size()) {
        return false;
    }
    setText(completed);
    return true;
}

void LineEditor::previous() {
    if (const std::optional<std::string_view> line = m_console.terminal().previous()) {
        setText(*line);
    }
}

void LineEditor::next() {
    const std::optional<std::string_view> line = m_console.terminal().next();
    setText(line.value_or(std::string_view()));
}

Outcome LineEditor::submit(PlayerSlot slot) {
    // The space a completion leaves after the last word is not part of the line entered.
    std::string line = m_text;
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
        line.pop_back();
    }
    setText({});
    return m_console.submit(line, LineSource::Typed, slot);
}

} // namespace coney::console
