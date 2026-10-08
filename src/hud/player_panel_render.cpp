// SPDX-License-Identifier: GPL-3.0-or-later
// The player panel's drawing: banner, rage meter, score, money and counters (docs/research/hud.md#the-player-panel).
#include <algorithm>
#include <format>
#include <string>
#include <string_view>

#include "gui/text_layout.h"
#include "hud/player_panel.h"

namespace coney::hud {

void PlayerPanel::render(const HudCanvas& canvas, int levelNumber) const {
    if (!m_shown) {
        return;
    }
    const float alpha = fade();
    if (alpha <= 0.0F) {
        return;
    }
    // In a level numbered 100 or more, player 1's two rage colours are swapped.
    const bool swapped = m_player == 1 && levelNumber >= kArcadeLevelStart;
    renderBanner(canvas, alpha, swapped);
    if (meterVisible()) {
        renderMeter(canvas, alpha, swapped);
    }
    if (levelNumber < kArcadeLevelStart) {
        renderScore(canvas, alpha);
    }
    renderMoney(canvas, alpha);
    renderCounters(canvas, alpha);
    renderTally(canvas, alpha, swapped, levelNumber);
}

void PlayerPanel::renderBanner(const HudCanvas& canvas, float alpha, bool swapped) const {
    if (!canvas.banner) {
        return;
    }
    graphics::SpriteBatch* banner = canvas.banner(m_banner, false);
    graphics::SpriteBatch* shadows = canvas.banner(m_banner, true);
    if (banner == nullptr || banner->sheet().page.rects.empty()) {
        return;
    }
    // The whole first rectangle, kBannerHeight tall, anchored by its left edge and vertical centre.
    const graphics::UvRect uv = banner->sheet().page.rect(0);
    const float height = kBannerHeight;
    const float width = squareTexelWidth(banner->sheet(), uv, height);
    const GuiPoint base = kPanelBase.at(m_player);
    const GuiPoint offset = kBannerOffset.at(m_player);
    const float x = base.x + offset.x + width / 2.0F;
    const float y = base.y + offset.y;
    // Two black copies under it, then the banner in the rage colour; the alpha multiplies all three.
    if (shadows != nullptr) {
        for (const GuiPoint& shift : kBannerShadowOffsets) {
            shadows->addSprite(guiSprite(x + shift.x, y + shift.y, width, height, uv, faded(graphics::kBlack, alpha)));
        }
    }
    banner->addSprite(guiSprite(x, y, width, height, uv, faded(bannerColour(swapped), alpha)));
}

void PlayerPanel::renderMeter(const HudCanvas& canvas, float alpha, bool swapped) const {
    graphics::SpriteBatch* parts = canvas.parts;
    if (parts == nullptr) {
        return;
    }
    // Left to right at the meter's height: the left cap and the body in the background colour, the capacity and the
    // fill strips over the body's width, the right cap. The point is the left end, on the meter's centre line.
    const GuiPoint base = kPanelBase.at(m_player);
    const GuiPoint offset = kMeterOffset.at(m_player);
    const float left = base.x + offset.x;
    const float y = base.y + offset.y;
    const float height = kMeterSize.height;
    const float leftCap = capWidth(parts, kMeterLeftCapRect, height);
    const float rightCap = capWidth(parts, kMeterRightCapRect, height);
    const float inner = kMeterSize.width - leftCap;
    const float bodyLeft = left + leftCap;
    const graphics::Rgba background = faded(kMeterBackground, alpha);
    addRect(parts, kMeterLeftCapRect, left + leftCap / 2.0F, y, leftCap, height, background);
    addStrip(parts, kMeterBodyRect, bodyLeft, y, inner, height, background);
    addStrip(parts, kMeterStripRect, bodyLeft, y, inner * capacity(), height, faded(kMeterCapacity, alpha));
    addStrip(parts, kMeterStripRect, bodyLeft, y, inner * fill(), height, faded(fillColour(swapped), alpha));
    addRect(parts, kMeterRightCapRect, bodyLeft + inner + rightCap / 2.0F, y, rightCap, height, background);
}

void PlayerPanel::renderScore(const HudCanvas& canvas, float alpha) const {
    // Seven digits, the leading zeros grey and the rest white.
    const GuiPoint at = shifted(kScoreOffset);
    const graphics::FontMetrics metrics = metricsOfSize(kPanelTextSize.width, kPanelTextSize.height);
    const std::string digits = std::format("{:07d}", std::max(0, m_score.shown()));
    const std::size_t firstDigit = std::min(digits.find_first_not_of('0'), digits.size());
    float x = at.x;
    x += drawPlainText(canvas, gui::kTextFontSlot, std::string_view(digits).substr(0, firstDigit), x, at.y, metrics,
                       faded(kScoreZeroGrey, alpha));
    x += drawPlainText(canvas, gui::kTextFontSlot, std::string_view(digits).substr(firstDigit), x, at.y, metrics,
                       faded(graphics::kWhite, alpha));
    // The popup beside it, shrinking over its last 400 ms (Coney's choice of place: just right of the score).
    if (const auto& popup = m_score.popup(); popup) {
        const std::uint64_t age = m_nowMs - popup->startMs;
        float scale = 1.0F;
        if (age + kScorePopupShrinkMs > kScorePopupMs) {
            scale = static_cast<float>(kScorePopupMs - age) / static_cast<float>(kScorePopupShrinkMs);
        }
        const graphics::FontMetrics small = metricsOfSize(metrics.width * scale, metrics.height * scale);
        drawPlainText(canvas, gui::kTextFontSlot, std::format("{:+d}", popup->delta), x + metrics.width / 2.0F, at.y,
                      small, faded(graphics::kWhite, alpha));
    }
}

void PlayerPanel::renderMoney(const HudCanvas& canvas, float alpha) const {
    // Hidden at 0 (inferred). **Coney's stand-in** for the money's icon (not found): a "$" before the amount.
    const GuiPoint at = shifted(kMoneyOffset);
    const graphics::FontMetrics metrics = metricsOfSize(kPanelTextSize.width, kPanelTextSize.height);
    if (m_money.shown() != 0) {
        drawPlainText(canvas, gui::kTextFontSlot, std::format("${}", m_money.shown()), at.x, at.y, metrics,
                      faded(graphics::kWhite, alpha));
    }
    // The floating ±$N line for its second, rising a little (Coney's choice of motion and place).
    if (const auto& popup = m_money.popup(); popup && canvas.text.fonts) {
        const float age = static_cast<float>(m_nowMs - popup->startMs) / static_cast<float>(kMoneyPopupMs);
        gui::TextStyle style;
        style.x = at.x + kMoneyIconOffset.x;
        style.y = at.y + kMoneyIconOffset.y - kPanelTextSize.height * (1.0F + age);
        style.scale = metrics.width * 30.0F;
        style.fade = alpha;
        style.shadowAlpha = 128;
        const std::string text = std::format("{}${}", popup->delta >= 0 ? "<MONEYPLUS>" : "<MONEYMINUS>",
                                             popup->delta >= 0 ? popup->delta : -popup->delta);
        gui::addTextSprites(gui::layoutText(text, style, canvas.text.fonts), canvas.text.textBatch);
    }
}

void PlayerPanel::renderCounters(const HudCanvas& canvas, float alpha) const {
    // Each counter with at least one item takes the next free slot, in kCounterItems order.
    const graphics::FontMetrics metrics = metricsOfSize(kPanelTextSize.width, kPanelTextSize.height);
    std::size_t slot = 0;
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i) < 1) {
            continue;
        }
        const GuiPoint place = shifted(GuiPoint{m_slots.at(slot).x, m_slots.at(slot).y});
        ++slot;
        const graphics::Rgba colour = i < 2 ? kCounterGreen : kCounterBlue;
        float iconWidth = kCounterIconSize;
        if (canvas.parts != nullptr && kCounterIcons.at(i) < canvas.parts->sheet().page.rects.size()) {
            iconWidth = squareTexelWidth(canvas.parts->sheet(), canvas.parts->sheet().page.rect(kCounterIcons.at(i)),
                                         kCounterIconSize);
        }
        addRect(canvas.parts, kCounterIcons.at(i), place.x + iconWidth / 2.0F, place.y, iconWidth, kCounterIconSize,
                faded(colour, alpha));
        drawPlainText(canvas, gui::kTextFontSlot, std::to_string(m_items.at(i)), place.x + kCounterTextOffset, place.y,
                      metrics, faded(graphics::kWhite, alpha));
    }
}

void PlayerPanel::renderTally(const HudCanvas& canvas, float alpha, bool swapped, int levelNumber) const {
    graphics::SpriteBatch* parts = canvas.parts;
    if (!m_tallyOn || parts == nullptr || kTallyBarRect >= parts->sheet().page.rects.size()) {
        return;
    }
    // The living count's marks (the nine sprites there are), turned 3.3 rad, in the rage colour `+0x4148` (player 1's
    // swapped in a Rumble level), at the panel's fade.
    const graphics::Rgba colour = faded(swapped ? kRageGold : kRageRed, alpha);
    const std::size_t marks = std::min<std::size_t>(m_tallyCount, kTallySprites);
    for (std::size_t i = 0; i < marks; ++i) {
        const bool bar = NumIndicator::isBar(i);
        const graphics::UvRect uv = parts->sheet().page.rect(bar ? kTallyBarRect : kTallyStrokeRect);
        const float height = bar ? kPanelTallyBarSize : kPanelTallyStrokeSize;
        const GuiPoint place = tallyMarkPlace(i, levelNumber);
        parts->addSprite(guiSprite(place.x, place.y, squareTexelWidth(parts->sheet(), uv, height), height, uv, colour),
                         kTallyRotation);
    }
}

} // namespace coney::hud
