// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/tag_hud.h"

#include <algorithm>
#include <array>

#include "graphics/overlay_camera.h"

namespace coney::hud {

namespace {

// The players' origins (GUI), default video mode.
constexpr std::array<GuiPoint, kPlayers> kOrigins{GuiPoint{0.01F, 0.70F}, GuiPoint{0.76F, 0.70F}};

// A square sprite `size` overlay units wide of `rect` at overlay offset (`dx`, `dy`) from `origin` (y up).
graphics::Sprite at(graphics::OverlayPoint origin, float dx, float dy, float width, float height,
                    const graphics::UvRect& rect, graphics::Rgba colour) {
    graphics::Sprite sprite;
    sprite.position = graphics::OverlayPoint{origin.x + dx, origin.y + dy, origin.z};
    sprite.width = width;
    sprite.height = height;
    sprite.uv = rect;
    sprite.colour = colour;
    return sprite;
}

} // namespace

graphics::OverlayPoint TagHud::origin(std::size_t player) {
    const GuiPoint gui = kOrigins.at(std::min(player, kPlayers - 1));
    return graphics::OverlayCamera::guiToOverlay(gui.x, gui.y);
}

graphics::Rgba TagHud::cursorColour(std::uint64_t nowMs, bool paused) {
    if (paused) {
        return graphics::kWhite;
    }
    // A triangle wave over 500 ms: white at 0, black at 250, white again at 500.
    const std::uint64_t phase = nowMs % (2 * kTagCursorFadeMs);
    const std::uint64_t toBlack = phase < kTagCursorFadeMs ? phase : (2 * kTagCursorFadeMs) - phase;
    const auto grey = static_cast<std::uint8_t>(255 - ((255 * toBlack) / kTagCursorFadeMs));
    return graphics::Rgba{grey, grey, grey, 255};
}

void TagHud::render(const HudCanvas& canvas, std::size_t player, std::uint64_t nowMs) const {
    graphics::SpriteBatch* parts = canvas.parts;
    if (!m_state || parts == nullptr) {
        return;
    }
    const TagPanelState& state = *m_state;
    const graphics::SpriteSheet& sheet = parts->sheet();
    const std::size_t rects = sheet.page.rects.size();
    const graphics::OverlayPoint base = origin(player);
    const auto cell = [](int c) { return static_cast<float>(c) * kTagCellStep; };

    // 1. The path, all but its last 3 points, grey from 100 at the start to 255 at the end.
    if (kTagPathRect < rects && state.path.size() > kTagPathHiddenEnd) {
        const std::size_t n = state.path.size();
        for (std::size_t i = 0; i + kTagPathHiddenEnd < n; ++i) {
            const auto grey = static_cast<std::uint8_t>(100 + ((155 * i) / n));
            parts->addSprite(at(base, cell(state.path[i].x), cell(state.path[i].y), kTagPathSize, kTagPathSize,
                                sheet.page.rect(kTagPathRect), graphics::Rgba{grey, grey, grey, 255}));
        }
    }
    // 2. The painted cells, in the paint's colour at alpha 205.
    if (kTagCellRect < rects) {
        graphics::Rgba paint = state.colour;
        paint.a = kTagCellAlpha;
        for (const TagPanelCell& painted : state.painted) {
            parts->addSprite(at(base, cell(painted.x), cell(painted.y), kTagCellSize, kTagCellSize,
                                sheet.page.rect(kTagCellRect), paint));
        }
    }
    // 3. The cursor, between cells as it moves.
    if (kTagCursorRect < rects) {
        parts->addSprite(at(base, state.cursorX * kTagCellStep, state.cursorY * kTagCellStep, kTagCursorSize,
                            kTagCursorSize, sheet.page.rect(kTagCursorRect), cursorColour(nowMs, state.paused)));
    }
    // 4. The charge bar: its bottom fixed, its centre half its height above, so it shrinks down as the paint goes.
    if (kTagBarRect < rects) {
        const float height = kTagBarHeight * std::clamp(state.chargeLeft, 0.0F, 1.0F);
        graphics::Rgba paint = state.colour;
        paint.a = 255;
        parts->addSprite(at(base, kTagBarX, kTagBarBottom + (height / 2.0F), kTagBarWidth, height,
                            sheet.page.rect(kTagBarRect), paint));
    }
    // 5. The frame (depth 11,000 in the original: over the panel's other sprites, so added last).
    if (kTagFrameRect < rects) {
        parts->addSprite(at(base, kTagFrameX, kTagFrameY, kTagFrameSize, kTagFrameSize, sheet.page.rect(kTagFrameRect),
                            kTagFrameColour));
    }
}

} // namespace coney::hud
