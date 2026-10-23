// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Which display Coney's window opens on (`--window-display`, `CONEY_WINDOW_DISPLAY`), and where on it: Coney's own
// debug tooling, so agents' runs stay off the display a person works on
// (docs/guides/running-options.md#window-display). The choosing and fitting live here, free of SDL, so they can be
// tested; src/platform/render_engine.cpp asks SDL for the displays and moves the window.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace coney {

/// A rectangle on the desktop, in the window system's coordinates (they may be negative left of or above the primary
/// display).
struct DisplayRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool operator==(const DisplayRect&) const = default;
};

/// One display as the window system lists it.
struct DisplayInfo {
    std::string name;   ///< Its name, such as the monitor's model.
    DisplayRect bounds; ///< The whole display.
    DisplayRect usable; ///< What windows may use: the display less a taskbar or menu bar.
};

/// A display asked for by a value of `--window-display` or `CONEY_WINDOW_DISPLAY`.
struct WindowDisplayChoice {
    /// How the value names the display.
    enum class Kind : std::uint8_t {
        Index,    ///< Digits: a 1-based position in the window system's list.
        Name,     ///< Any other text: part of the display's name, case ignored.
        Portrait, ///< `portrait`: the first display taller than wide.
    };
    Kind kind = Kind::Index;
    std::size_t index = 0; ///< Kind::Index: the 1-based position (0, or too large to hold, matches nothing).
    std::string name;      ///< Kind::Name: the text to look for.
};

/// Reads a value of `--window-display` or `CONEY_WINDOW_DISPLAY`, spaces round it ignored: all digits is an index,
/// `portrait` (any case) the first portrait display, anything else a name to look for. Nothing for an empty value,
/// which keeps the window system's own placement.
[[nodiscard]] std::optional<WindowDisplayChoice> parseWindowDisplay(std::string_view text);

/// The position in `displays` of the display `choice` names, or nothing when none matches (the caller warns and
/// keeps the default placement).
[[nodiscard]] std::optional<std::size_t> pickDisplay(const WindowDisplayChoice& choice,
                                                     std::span<const DisplayInfo> displays);

/// Where a `width` x `height` window goes in `area`: centred, at its own size when it fits, else scaled down to the
/// largest size of the same shape that does (each side rounded down, never under one pixel). An empty area leaves
/// the size alone and puts the window at the area's corner. `width` and `height` must be positive (CONEY_ASSERT).
[[nodiscard]] DisplayRect placeWindow(const DisplayRect& area, int width, int height);

} // namespace coney
