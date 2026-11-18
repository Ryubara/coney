// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/tokenizer.h"

#include <format>

namespace coney::console {

namespace {

// Whether `c` separates words.
bool isSeparator(char c) { return c == ' ' || c == '\t'; }

} // namespace

SplitLine splitForCompletion(std::string_view line) {
    SplitLine split;
    std::string word;
    bool inWord = false;  // a word has started, even an empty quoted one
    bool inQuote = false; // inside double quotes
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        // Inside quotes: `\"` is a quote, `"` closes, anything else is text.
        if (inQuote) {
            if (c == '\\' && i + 1 < line.size() && line[i + 1] == '"') {
                word += '"';
                ++i;
            } else if (c == '"') {
                inQuote = false;
            } else {
                word += c;
            }
            continue;
        }
        // Outside: a separator ends the word, a quote opens one, anything else is text.
        if (isSeparator(c)) {
            if (inWord) {
                split.words.push_back(std::move(word));
                word.clear();
                inWord = false;
            }
        } else if (c == '"') {
            inQuote = true;
            inWord = true;
        } else {
            word += c;
            inWord = true;
        }
    }
    if (inWord) {
        split.words.push_back(std::move(word));
    }
    split.openQuote = inQuote;
    split.trailingSpace = !inQuote && !line.empty() && isSeparator(line.back());
    return split;
}

std::expected<std::vector<std::string>, Error> tokenize(std::string_view line) {
    if (line.size() > kMaxLineBytes) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("the line is {} bytes long; a line is at most {}", line.size(), kMaxLineBytes));
    }
    SplitLine split = splitForCompletion(line);
    if (split.openQuote) {
        return fail(ErrorCode::InvalidArgument, "a quote is not closed: end the argument with \"");
    }
    return std::move(split.words);
}

std::string quoteWord(std::string_view word) {
    const bool plain = !word.empty() && word.find_first_of(" \t\"") == std::string_view::npos;
    if (plain) {
        return std::string(word);
    }
    // A backslash only escapes a quote, so one before the closing quote would escape it: the word's trailing
    // backslashes go after the closing quote, where they are plain text of the same word.
    const std::size_t kept = word.find_last_not_of('\\') + 1;
    std::string quoted = "\"";
    for (const char c : word.substr(0, kept)) {
        if (c == '"') {
            quoted += '\\';
        }
        quoted += c;
    }
    quoted += '"';
    quoted.append(word.substr(kept));
    return quoted;
}

} // namespace coney::console
