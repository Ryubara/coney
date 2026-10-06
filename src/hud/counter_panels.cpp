// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/counter_panels.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>
#include <vector>

#include "gui/text_layout.h"

namespace coney::hud {

namespace {

// A panel value as a whole number.
long long whole(float value) { return std::llround(std::trunc(value)); }

// The height of a bar panel's bar (Coney's).
constexpr float kBarHeight = 0.02F;
// The bar's background.
constexpr graphics::Rgba kBarBackground{80, 80, 80, 255};

} // namespace

int CounterPanels::take(int kind, std::string label, float barWidth) {
    for (std::size_t i = 0; i < m_panels.size(); ++i) {
        CounterPanel& panel = m_panels.at(i);
        if (!panel.used) {
            panel = CounterPanel{};
            panel.used = true;
            panel.kind = static_cast<CounterKind>(std::clamp(kind, 0, 3));
            panel.label = std::move(label);
            panel.barWidth = barWidth;
            return static_cast<int>(i);
        }
    }
    return -1;
}

void CounterPanels::release(int panel) {
    if (panel < 0 || static_cast<std::size_t>(panel) >= m_panels.size()) {
        return;
    }
    m_panels.at(static_cast<std::size_t>(panel)) = CounterPanel{};
}

void CounterPanels::setValue(int panel, int field, float value, std::uint32_t ms, bool sound, std::uint64_t nowMs,
                             const HudSound& audio) {
    if (panel < 0 || static_cast<std::size_t>(panel) >= m_panels.size()) {
        return;
    }
    CounterPanel& target = m_panels.at(static_cast<std::size_t>(panel));
    if (field == 2) {
        target.target = value;
        return;
    }
    if (field != 1) {
        return;
    }
    target.value = value;
    target.visible = true;
    target.hideAtMs = ms == 0 ? std::nullopt : std::optional<std::uint64_t>(nowMs + ms);
    if (sound) {
        audio.playSound(kCounterPanelSound);
    }
}

void CounterPanels::update(std::uint64_t nowMs) {
    for (CounterPanel& panel : m_panels) {
        if (panel.visible && panel.hideAtMs && nowMs >= *panel.hideAtMs) {
            panel.visible = false;
        }
    }
}

std::string CounterPanels::textOf(const CounterPanel& panel) {
    switch (panel.kind) {
    case CounterKind::Suffixed:
        return std::format("{} {}", whole(panel.value), panel.label);
    case CounterKind::Single:
        return std::format("{} {}", panel.label, whole(panel.value));
    case CounterKind::GotNeeded:
        return std::format("{} {}/{}", panel.label, whole(panel.value), whole(panel.target));
    case CounterKind::Bar:
        return panel.label;
    }
    return panel.label;
}

void CounterPanels::render(const HudCanvas& canvas) const {
    std::size_t row = 0;
    for (const CounterPanel& panel : m_panels) {
        if (!panel.used || !panel.visible) {
            continue;
        }
        const float y = kCounterPanelTop + kCounterPanelRow * static_cast<float>(row);
        ++row;
        if (canvas.text.fonts) {
            gui::TextStyle style;
            style.x = kCounterPanelX;
            style.y = y;
            style.scale = metricsOfHeight(kMessageTextHeight).width * 30.0F;
            style.shadowAlpha = 128;
            // Measured first, then moved left by its width so that it ends on the column.
            const std::string text = textOf(panel);
            style.x = kCounterPanelX - gui::layoutText(text, style, canvas.text.fonts).width;
            gui::addTextSprites(gui::layoutText(text, style, canvas.text.fonts), canvas.text.textBatch);
        }
        if (panel.kind == CounterKind::Bar && canvas.flat != nullptr) {
            const float barY = y + kMessageTextHeight * 0.75F;
            const float left = kCounterPanelX - panel.barWidth;
            canvas.flat->addSprite(guiSprite(left + panel.barWidth / 2.0F, barY, panel.barWidth, kBarHeight,
                                             graphics::UvRect{}, kBarBackground));
            const float filled = panel.barWidth * std::clamp(panel.value, 0.0F, 1.0F);
            canvas.flat->addSprite(
                guiSprite(left + filled / 2.0F, barY, filled, kBarHeight, graphics::UvRect{}, graphics::kWhite));
        }
    }
}

} // namespace coney::hud
