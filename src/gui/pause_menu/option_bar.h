// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>

#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"

namespace coney::gui {

/// Where and how large an Options bar (`StatBarItem`: a volume bar or the brightness bar) is drawn, as its first
/// update leaves it (docs/research/pause.md#options).
struct OptionBarPlace {
    float left = 0.0F;   ///< GUI x of its left end.
    float y = 0.0F;      ///< GUI y of its centre line.
    float width = 0.0F;  ///< GUI width, caps included.
    float height = 0.0F; ///< Overlay-camera height.
};

/// The bars' sprites as the original sets them up (`StatBarItem_Setup`, `0x001d6ef8`): two `HudBar`s of sprite word
/// `0x30036` (part_page0, the batch of font slot 3; body rectangle 54, strip 55, caps 56 and 57), and a sizing sprite
/// of sheet 12's rectangle 0 that is never drawn.
namespace option_bar {
/// The width the item is set up with (`0x0050eda8`), GUI units.
inline constexpr float kWidth = 0.54F;
/// The bars' x after set-up and first update: `0x0050ed28` (-0.004), then `0x0050ed38` (+0.003).
inline constexpr float kSetupShiftX = -0.004F;
inline constexpr float kUpdateShiftX = 0.003F;
/// Their y: +0.004 at set-up, then `0x0050ed34` (-0.004).
inline constexpr float kSetupShiftY = 0.004F;
inline constexpr float kUpdateShiftY = -0.004F;
/// The sizing sprite's rectangle of sheet 12 (sprite word `0xc0000`).
inline constexpr std::size_t kSizingRect = 0;
/// part_page0's strip and left cap rectangles (the sprite word's 54, + 1, + 2).
inline constexpr std::size_t kStripRect = 55;
inline constexpr std::size_t kLeftCapRect = 56;
/// The back bar's colour (`0xff151515`), drawn at full alpha whatever the menu's fade.
inline constexpr graphics::Rgba kBackColour{21, 21, 21, 255};
} // namespace option_bar

/// Where the bar of an item at (`barX`, `barY`) is drawn: its set-up place moved by the first update; the width it is
/// set up with, and the height its first update gives it, the sizing sprite's height (the width in overlay units times
/// rectangle 0's shape, from `sizingSheet`) less `heightCut` (`0x0050ed30`, by video mode). None without a sheet or
/// rectangle to measure, or when nothing is left of the height.
/// @orig 0x001d7270 StatBarItem_Update (OptionMenu.cpp)
[[nodiscard]] std::optional<OptionBarPlace> optionBarPlace(const graphics::SpriteBatch* sizingSheet, float barX,
                                                           float barY, float heightCut);

/// Draws a bar at `place` into `parts` (part_page0's batch; null draws nothing): the back bar's strip over the width
/// left after its left cap, in kBackColour, then the fill bar's over `fill` (0-1) of it in `fillColour`, the item's
/// label colour with the menu's alpha. The two bars' caps and bodies are set up transparent, so only the strips show.
/// @orig 0x001d73b0 StatBarItem_RenderColoured (OptionMenu.cpp)
void drawOptionBar(graphics::SpriteBatch* parts, const OptionBarPlace& place, float fill, graphics::Rgba fillColour);

} // namespace coney::gui
