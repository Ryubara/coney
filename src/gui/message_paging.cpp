// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/message_paging.h"

#include <algorithm>
#include <optional>

namespace coney::gui {

namespace {

// The break the word wrap inserts and the page clip splits at.
constexpr std::string_view kWrapBreak = "<CRM>";

} // namespace

WrappedMessage wrapMessage(std::string_view text, float wrapWidth, const LineMeasure& measure) {
    WrappedMessage wrapped;
    std::string line; // the current line's characters outside tags, as measured
    bool inTag = false;
    // The words between the spaces, runs of spaces as one (the original's strtok on " ").
    std::size_t start = text.find_first_not_of(' ');
    while (start != std::string_view::npos) {
        const std::size_t end = std::min(text.find(' ', start), text.size());
        const std::string_view word = text.substr(start, end - start);
        const bool lineWasEmpty = line.empty();
        // A word with a `>` in it starts inside a tag (the rest of a tag a space split).
        if (word.contains('>')) {
            inTag = true;
        }
        // Each `<` of a word holding a `<CR>` or `<CR3` is a break of the text's own: counted, and a new line.
        const bool textBreak = word.contains("<CR>") || word.contains("<CR3");
        for (const char c : word) {
            if (c == '<') {
                inTag = true;
                if (textBreak) {
                    ++wrapped.breaks;
                    line.clear();
                }
            } else if (c == '>') {
                inTag = false;
            }
            // Coney's choice: a word or a line may pass 255 characters, where the original's byte counters wrap.
            if (!inTag && c != '>') {
                line += c;
            }
        }
        // A word that added nothing leaves the space after the last one at the end: it is not measured.
        if (line.ends_with(' ')) {
            line.pop_back();
        }
        // Coney's choice: a word too wide for a line of its own stays on it, where the original breaks before it
        // again and again without end.
        if (measure(line) <= wrapWidth || lineWasEmpty) {
            wrapped.text += word;
            wrapped.text += ' ';
            line += ' ';
            start = text.find_first_not_of(' ', end);
            continue;
        }
        // Too wide: the break goes before the last space or `<` before the trailing space, carrying a tag (or a
        // `<CRM>`) found there to the new line, and the same word is measured again on it. Coney's choice: with
        // neither found (a first line of one word), nothing is carried, where the original reads on before its buffer.
        std::string carried;
        const std::size_t at = wrapped.text.find_last_of(" <", wrapped.text.size() - 2);
        if (at != std::string::npos && wrapped.text[at] == '<') {
            carried = wrapped.text.substr(at);
            wrapped.text.erase(at);
        }
        wrapped.text += kWrapBreak;
        wrapped.text += carried;
        line.clear();
        ++wrapped.breaks;
    }
    return wrapped;
}

std::string clipToPage(std::string_view wrapped, const MessagePage& page) {
    // The page starts at the text's start for line 0, else after the break that starts its first line; it ends after
    // the break that ends its last line, else at the text's end.
    std::optional<std::size_t> start;
    if (page.firstLine == 0) {
        start = 0;
    }
    std::size_t end = wrapped.size();
    std::size_t line = 0;
    for (std::size_t at = wrapped.find(kWrapBreak); at != std::string_view::npos;
         at = wrapped.find(kWrapBreak, at + kWrapBreak.size())) {
        const std::size_t after = at + kWrapBreak.size();
        ++line;
        if (line == page.firstLine) {
            start = after;
        } else if (line - 1 == page.lastLine) {
            end = after;
            break;
        }
    }
    // Coney's choice: a page past the text's lines is empty (the original copies from no text).
    if (!start || *start > end) {
        return {};
    }
    return std::string(wrapped.substr(*start, end - *start));
}

bool pageUp(MessagePage& page) {
    if (page.firstLine == 0) {
        return false;
    }
    --page.firstLine;
    --page.lastLine;
    return true;
}

bool pageDown(MessagePage& page, std::size_t breaks) {
    if (page.lastLine >= breaks) {
        return false;
    }
    ++page.lastLine;
    ++page.firstLine;
    return true;
}

LineMeasure lineMeasure(const TextStyle& style, const FontLookup& fonts) {
    TextStyle oneLine = style;
    oneLine.wrapWidth = 0.0F;
    oneLine.boxWidth = 0.0F;
    return [oneLine, fonts](std::string_view line) { return layoutText(line, oneLine, fonts).width; };
}

} // namespace coney::gui
