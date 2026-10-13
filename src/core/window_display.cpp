// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/window_display.h"

#include <algorithm>
#include <cstdint>

#include "core/assert.h"

namespace coney {

namespace {

// ASCII lower case; display names and the setting are compared without case.
char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// Whether `text` holds `part`, case ignored.
bool containsIgnoringCase(std::string_view text, std::string_view part) {
    const auto same = [](char a, char b) { return lower(a) == lower(b); };
    return !std::ranges::search(text, part, same).empty();
}

// `text` without the spaces and tabs round it.
std::string_view trimmed(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t") - first + 1);
}

} // namespace

std::optional<WindowDisplayChoice> parseWindowDisplay(std::string_view text) {
    text = trimmed(text);
    if (text.empty()) {
        return std::nullopt;
    }
    WindowDisplayChoice choice;
    if (std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; })) {
        // A number too long to hold names no display: 0 matches nothing.
        std::size_t index = 0;
        for (const char c : text) {
            index = (index * 10) + static_cast<std::size_t>(c - '0');
            if (index > 0xFFFF) {
                index = 0;
                break;
            }
        }
        choice.kind = WindowDisplayChoice::Kind::Index;
        choice.index = index;
        return choice;
    }
    if (std::ranges::equal(text, std::string_view("portrait"), [](char a, char b) { return lower(a) == b; })) {
        choice.kind = WindowDisplayChoice::Kind::Portrait;
        return choice;
    }
    choice.kind = WindowDisplayChoice::Kind::Name;
    choice.name = std::string(text);
    return choice;
}

std::optional<std::size_t> pickDisplay(const WindowDisplayChoice& choice, std::span<const DisplayInfo> displays) {
    switch (choice.kind) {
    case WindowDisplayChoice::Kind::Index:
        if (choice.index >= 1 && choice.index <= displays.size()) {
            return choice.index - 1;
        }
        return std::nullopt;
    case WindowDisplayChoice::Kind::Portrait:
        for (std::size_t i = 0; i < displays.size(); ++i) {
            if (displays[i].bounds.height > displays[i].bounds.width) {
                return i;
            }
        }
        return std::nullopt;
    case WindowDisplayChoice::Kind::Name:
        for (std::size_t i = 0; i < displays.size(); ++i) {
            if (containsIgnoringCase(displays[i].name, choice.name)) {
                return i;
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

DisplayRect placeWindow(const DisplayRect& area, int width, int height) {
    CONEY_ASSERT(width > 0 && height > 0);
    if (area.width <= 0 || area.height <= 0) {
        return {area.x, area.y, width, height};
    }
    // Scale down only, to whichever side of the area runs out first; 64-bit products, so no size can overflow.
    const std::int64_t w = width;
    const std::int64_t h = height;
    const std::int64_t aw = area.width;
    const std::int64_t ah = area.height;
    std::int64_t fitW = w;
    std::int64_t fitH = h;
    if (w > aw || h > ah) {
        if (w * ah <= aw * h) { // the height runs out first
            fitH = ah;
            fitW = std::max<std::int64_t>(1, w * ah / h);
        } else {
            fitW = aw;
            fitH = std::max<std::int64_t>(1, h * aw / w);
        }
    }
    return {area.x + static_cast<int>((aw - fitW) / 2), area.y + static_cast<int>((ah - fitH) / 2),
            static_cast<int>(fitW), static_cast<int>(fitH)};
}

} // namespace coney
