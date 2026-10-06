// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/hud_canvas.h"

#include <vector>

namespace coney::hud {

namespace {

// The shadow the HUD's text draws with: half strength, as the text widgets'.
constexpr std::uint8_t kTextShadow = 128;

} // namespace

graphics::FontMetrics metricsOfHeight(float height) {
    // fontMetrics() is linear in the scale: scale it so that its height is `height`.
    const graphics::FontMetrics unit = graphics::fontMetrics(1.0F);
    return graphics::fontMetrics(height / unit.height);
}

graphics::FontMetrics metricsOfSize(float width, float height) {
    graphics::FontMetrics metrics = metricsOfHeight(height);
    metrics.width = width;
    return metrics;
}

float drawPlainText(const HudCanvas& canvas, int slot, std::string_view text, float x, float y,
                    const graphics::FontMetrics& metrics, graphics::Rgba colour) {
    const graphics::Font* font = canvas.text.fonts ? canvas.text.fonts(slot) : nullptr;
    graphics::SpriteBatch* batch = canvas.text.textBatch ? canvas.text.textBatch(slot) : nullptr;
    if (font == nullptr || batch == nullptr) {
        return 0.0F;
    }
    std::vector<graphics::Sprite> sprites;
    font->draw(sprites, text, x, y, metrics, graphics::kFontProportional, colour, kTextShadow);
    for (const graphics::Sprite& sprite : sprites) {
        batch->addSprite(sprite);
    }
    return font->measure(text, metrics, graphics::kFontProportional);
}

void addRect(graphics::SpriteBatch* batch, std::size_t rect, float x, float y, float width, float height,
             graphics::Rgba colour) {
    if (batch == nullptr || rect >= batch->sheet().page.rects.size()) {
        return;
    }
    batch->addSprite(guiSprite(x, y, width, height, batch->sheet().page.rect(rect), colour));
}

} // namespace coney::hud
