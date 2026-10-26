// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/pause_menu/option_bar.h"

#include "graphics/overlay_camera.h"
#include "graphics/screen.h"

namespace coney::gui {

namespace {

// Rectangle `rect`'s texel shape on `sheet`'s texture: its height over its width in texels (`across` false), or its
// width over its height (`across` true). None without a texture or rectangle, or for an empty one.
// @coney-only Coney's sheet arithmetic; the original reads the rectangle in BaseWidget_UpdateSize and HudBar_Draw
std::optional<float> texelShape(const graphics::SpriteSheet& sheet, std::size_t rect, bool across) {
    if (!sheet.texture || rect >= sheet.page.rects.size()) {
        return std::nullopt;
    }
    const graphics::UvRect& uv = sheet.page.rect(rect);
    const float texels = (uv.u1 - uv.u0) * static_cast<float>(sheet.texture->width());
    const float rows = (uv.v1 - uv.v0) * static_cast<float>(sheet.texture->height());
    if (texels <= 0.0F || rows <= 0.0F) {
        return std::nullopt;
    }
    return across ? texels / rows : rows / texels;
}

} // namespace

std::optional<OptionBarPlace> optionBarPlace(const graphics::SpriteBatch* sizingSheet, float barX, float barY,
                                             float heightCut) {
    if (sizingSheet == nullptr) {
        return std::nullopt;
    }
    // The sizing sprite: the item's width in overlay units (BaseWidget_SetSize's conversion), its height from its
    // rectangle's shape (BaseWidget_UpdateSize, mode 0).
    const std::optional<float> shape = texelShape(sizingSheet->sheet(), option_bar::kSizingRect, false);
    if (!shape) {
        return std::nullopt;
    }
    const float height = (graphics::OverlayCamera::guiWidthToOverlay(option_bar::kWidth) * *shape) - heightCut;
    if (height <= 0.0F) {
        return std::nullopt;
    }
    // The first update takes the bars to that height less the cut and moves them; the width stays the item's.
    return OptionBarPlace{.left = barX + option_bar::kSetupShiftX + option_bar::kUpdateShiftX,
                          .y = barY + option_bar::kSetupShiftY + option_bar::kUpdateShiftY,
                          .width = option_bar::kWidth,
                          .height = height};
}

void drawOptionBar(graphics::SpriteBatch* parts, const OptionBarPlace& place, float fill, graphics::Rgba fillColour) {
    if (parts == nullptr || option_bar::kStripRect >= parts->sheet().page.rects.size()) {
        return;
    }
    const graphics::SpriteSheet& sheet = parts->sheet();
    // Where the strips start. HudBar_Draw takes the left cap's width as its rectangle's UV width, centres the back on
    // the bar and makes it that much narrower, so the strips start half a cap's width in. Stand-in (Coney's choice,
    // shared with the rage meter, docs/research/hud.md#the-rage-meter): the cap is as wide as the bar's height makes
    // its texels square, and the strips start at its end; for the option bars it lands within a pixel of the
    // original's (pause.md#options).
    const float cap = place.height * texelShape(sheet, option_bar::kLeftCapRect, true).value_or(0.0F) *
                      (graphics::kLogicalHeight / graphics::kLogicalWidth);
    const float start = place.left + cap;
    const float inner = place.width - cap;
    // HudBar_Draw moves the stretched strip in by a texel across, so that filtering stays inside it.
    graphics::UvRect uv = sheet.page.rect(option_bar::kStripRect);
    if (sheet.texture && sheet.texture->width() > 0) {
        const float texel = 1.0F / static_cast<float>(sheet.texture->width());
        if (uv.u1 - uv.u0 > 2.0F * texel) {
            uv.u0 += texel;
            uv.u1 -= texel;
        }
    }
    // A strip `width` GUI units long from the start, centred on its own middle.
    const auto strip = [&](float width, graphics::Rgba colour) {
        if (width <= 0.0F) {
            return;
        }
        parts->addSprite(graphics::Sprite{graphics::OverlayCamera::guiToOverlay(start + (width / 2.0F), place.y),
                                          graphics::OverlayCamera::guiWidthToOverlay(width), place.height, uv, colour});
    };
    // The back bar (full), then the fill bar.
    strip(inner, option_bar::kBackColour);
    strip(inner * fill, fillColour);
}

} // namespace coney::gui
