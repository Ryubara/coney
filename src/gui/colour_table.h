// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>

#include "graphics/render_device.h"

namespace coney::gui {

/// The menus' colour table: the `RwRGBA` entries the original fills once at start-up (`0x0017ae38`) at `0x005fd260`,
/// 8 bytes apart, and every menu widget takes its colours from. Index `i` is the entry at `0x005fd260 + 8 × i`.
/// Colours are RenderWare's 0-255 with 255 opaque (docs/research/gui.md#sprite-colours).
///
/// Research: docs/research/gui.md#colour-table
/// @orig 0x0017ae38 ColourTable_Init (unknown)
inline constexpr std::array<graphics::Rgba, 26> kColourTable{{
    {0, 0, 0, 255},       // 0x005fd260 black
    {255, 255, 255, 255}, // 0x005fd268 white
    {127, 127, 127, 255}, // 0x005fd270
    {255, 0, 0, 255},     // 0x005fd278 red
    {0, 255, 0, 255},     // 0x005fd280 green
    {0, 0, 255, 255},     // 0x005fd288 blue
    {0, 255, 255, 255},   // 0x005fd290 cyan
    {255, 0, 255, 255},   // 0x005fd298 magenta
    {255, 255, 0, 255},   // 0x005fd2a0 yellow
    {255, 128, 0, 255},   // 0x005fd2a8
    {128, 0, 0, 255},     // 0x005fd2b0
    {0, 0, 128, 255},     // 0x005fd2b8
    {0, 128, 0, 255},     // 0x005fd2c0
    {192, 96, 0, 255},    // 0x005fd2c8
    {0, 128, 128, 128},   // 0x005fd2d0
    {128, 128, 0, 255},   // 0x005fd2d8
    {32, 0, 0, 48},       // 0x005fd2e0
    {4, 4, 4, 140},       // 0x005fd2e8
    {0, 0, 32, 64},       // 0x005fd2f0
    {10, 5, 40, 60},      // 0x005fd2f8
    {0, 0, 0, 0},         // 0x005fd300
    {36, 75, 130, 255},   // 0x005fd308
    {178, 178, 178, 255}, // 0x005fd310 the menus' grey
    {178, 178, 178, 255}, // 0x005fd318 the selected grid item
    {80, 80, 80, 255},    // 0x005fd320 dim items
    {170, 43, 43, 255},   // 0x005fd328 the front end's red
}};

/// The entry at `address` (`0x005fd260` to `0x005fd328`, 8 bytes apart). Evaluated at compile time, so an address past
/// the table does not compile.
[[nodiscard]] consteval graphics::Rgba colourAt(std::size_t address) {
    return kColourTable.at((address - 0x005fd260) / 8);
}

/// `0x005fd310`, grey (178, 178, 178): usage lines, the Rumble screens' titles and arrows, the message box's text, and
/// the alpha every grid item is drawn in.
inline constexpr graphics::Rgba kMenuGrey = colourAt(0x005fd310);
/// `0x005fd318`, grey (178, 178, 178): a grid item that is selected (enabled and focused).
inline constexpr graphics::Rgba kSelectedGrey = colourAt(0x005fd318);
/// `0x005fd320`, dark grey (80, 80, 80): the message box's and the Rumble Game Type screen's unselected items.
inline constexpr graphics::Rgba kDimGrey = colourAt(0x005fd320);
/// `0x005fd328`, red (170, 43, 43): the front end's titles, items, separators, prompts, the logo's tint, bar fills.
inline constexpr graphics::Rgba kMenuRed = colourAt(0x005fd328);

} // namespace coney::gui
