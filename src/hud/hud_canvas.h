// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

#include "graphics/font.h"
#include "graphics/overlay_camera.h"
#include "graphics/screen.h"
#include "graphics/sprite_batch.h"
#include "gui/widget.h"

namespace coney::hud {

/// The sprite sheet records the HUD draws from, by their index in the sheet table (chunk 0x4D).
inline constexpr std::uint32_t kPartPage0Record = 0;
inline constexpr std::uint32_t kHudMinigamesRecord = 10;
inline constexpr std::uint32_t kBigFontRecord = 13;

/// Where the HUD's render puts its sprites: the text fonts and their batches (gui::GuiCanvas, slot 2 `part_page0` and
/// slot 6 `big_font`), the batches over the sheets it draws images from, and the banners' batches. Any batch may be
/// null, which leaves out what it would draw.
struct HudCanvas {
    gui::GuiCanvas text;                        ///< The fonts by slot and their batches.
    graphics::SpriteBatch* parts = nullptr;     ///< `part_page0`: the meter, the counters' icons, the radar's icons.
    graphics::SpriteBatch* minigames = nullptr; ///< `hud_minigames`: the instruction arrow.
    graphics::SpriteBatch* flat = nullptr;      ///< Untextured quads below the text: the hint box, panel bars.
    graphics::SpriteBatch* radar = nullptr;     ///< The radar disc (Coney's stand-in sheet, Hud::kRadarDiscRect).
    /// The batch of a banner's sheet (by sheet-table record) at the banner's depth (`shadow` false) or the shadows'
    /// (`shadow` true); null when that sheet is not loaded.
    std::function<graphics::SpriteBatch*(std::uint32_t record, bool shadow)> banner;
};

/// A sprite of a `width` x `height` GUI box centred at GUI (`x`, `y`): the overlay-camera position and size the batches
/// take.
[[nodiscard]] inline graphics::Sprite guiSprite(float x, float y, float width, float height, graphics::UvRect uv,
                                                graphics::Rgba colour) {
    graphics::Sprite sprite;
    sprite.position = graphics::OverlayCamera::guiToOverlay(x, y);
    sprite.width = graphics::OverlayCamera::guiWidthToOverlay(width);
    sprite.height = height;
    sprite.uv = uv;
    sprite.colour = colour;
    return sprite;
}

/// `colour` with its alpha multiplied by `fade` (0 to 1).
[[nodiscard]] inline graphics::Rgba faded(graphics::Rgba colour, float fade) {
    const float alpha = static_cast<float>(colour.a) * fade;
    colour.a = static_cast<std::uint8_t>(alpha < 0.0F ? 0.0F : (alpha > 255.0F ? 255.0F : alpha + 0.5F));
    return colour;
}

/// The GUI width of a sheet rectangle `uv` drawn `height` tall with its texels square on screen: its texel aspect,
/// times the height, times 448 / 640 (a GUI width is stretched by 640 / 448 into the overlay camera's space). 0 when
/// the sheet has no texture.
[[nodiscard]] inline float squareTexelWidth(const graphics::SpriteSheet& sheet, const graphics::UvRect& uv,
                                            float height) {
    if (!sheet.texture || sheet.texture->height() == 0) {
        return 0.0F;
    }
    const float texels = (uv.u1 - uv.u0) * static_cast<float>(sheet.texture->width());
    const float rows = (uv.v1 - uv.v0) * static_cast<float>(sheet.texture->height());
    if (rows <= 0.0F) {
        return 0.0F;
    }
    return height * (texels / rows) * (graphics::kLogicalHeight / graphics::kLogicalWidth);
}

/// Glyph metrics of height `height` (GUI): the width that height gives in graphics::fontMetrics(), the spacing and line
/// gap of that size. The HUD's tables give text sizes as a glyph height.
[[nodiscard]] graphics::FontMetrics metricsOfHeight(float height);

/// The metrics of a glyph `width` × `height` (GUI), as the panel's text size `(0.04, 0.05)` gives it, with the spacing
/// and line gap of graphics::fontMetrics().
[[nodiscard]] graphics::FontMetrics metricsOfSize(float width, float height);

/// Draws plain `text` (no markup) in the font of slot `slot` into that slot's batch: left-aligned from GUI x `x`,
/// the glyphs centred on GUI y `y`, proportional, with a half-strength shadow. Returns the text's width, 0 when the
/// slot has no font or batch.
float drawPlainText(const HudCanvas& canvas, int slot, std::string_view text, float x, float y,
                    const graphics::FontMetrics& metrics, graphics::Rgba colour);

/// Adds rectangle `rect` of `batch`'s sheet as a sprite `width` × `height` (GUI) centred at (`x`, `y`). Does nothing
/// for a null batch or a sheet without that rectangle.
void addRect(graphics::SpriteBatch* batch, std::size_t rect, float x, float y, float width, float height,
             graphics::Rgba colour);

} // namespace coney::hud
