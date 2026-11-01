// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/terminal.h"

#include <algorithm>

namespace coney::console {

namespace {

// Calls `each` with every line of `text`, a line break ending each (a `\r` before it dropped); the text after the
// last break is a line when it is not empty, or when the text is empty altogether.
template <typename Each> void forEachLine(std::string_view text, Each each) {
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            if (start < text.size() || text.empty()) {
                each(text.substr(start));
            }
            break;
        }
        std::string_view line = text.substr(start, end - start);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        each(line);
        start = end + 1;
    }
}

// Whether `line` holds nothing but spaces and tabs.
bool isBlank(std::string_view line) {
    return std::ranges::all_of(line, [](char c) { return c == ' ' || c == '\t'; });
}

} // namespace

void Terminal::print(std::string_view text, LineKind kind, std::string_view source) {
    forEachLine(text, [&](std::string_view line) {
        m_output.push_back(OutputLine{std::string(line), kind, std::string(source)});
        if (m_output.size() > kOutputLinesMax) {
            m_output.pop_front();
        }
    });
}

void Terminal::remember(std::string_view line) {
    m_walk = 0;
    if (isBlank(line) || (!m_history.empty() && m_history.back() == line)) {
        return;
    }
    m_history.emplace_back(line);
    if (m_history.size() > kHistoryLinesMax) {
        m_history.pop_front();
    }
}

std::optional<std::string_view> Terminal::previous() {
    if (m_walk >= m_history.size()) {
        return std::nullopt;
    }
    ++m_walk;
    return m_history[m_history.size() - m_walk];
}

std::optional<std::string_view> Terminal::next() {
    if (m_walk <= 1) {
        m_walk = 0;
        return std::nullopt;
    }
    --m_walk;
    return m_history[m_history.size() - m_walk];
}

void Terminal::loadHistory(std::string_view text) {
    m_history.clear();
    m_walk = 0;
    forEachLine(text, [this](std::string_view line) {
        if (!isBlank(line)) {
            m_history.emplace_back(line);
        }
    });
    while (m_history.size() > kHistoryLinesMax) {
        m_history.pop_front();
    }
}

std::string Terminal::historyText() const {
    std::string text;
    for (const std::string& line : m_history) {
        text += line;
        text += '\n';
    }
    return text;
}

} // namespace coney::console
