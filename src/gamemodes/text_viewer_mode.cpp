// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/text_viewer_mode.h"

#include <utility>

#include "core/game_timer.h"

namespace coney {

TextViewerMode::TextViewerMode(graphics::RenderDevice& device, graphics::Font font,
                               std::optional<graphics::Font> bigFont, std::string text, float scale)
    : m_device(device), m_font(std::move(font)), m_bigFont(std::move(bigFont)), m_text(std::move(text)),
      m_batch(m_font.sheet(), kCapacity, kDepth) {
    if (m_bigFont) {
        m_bigBatch.emplace(m_bigFont->sheet(), kCapacity, kDepth);
    }
    m_style.x = kLeft;
    m_style.y = kTop;
    m_style.scale = scale;
    m_style.shadowAlpha = kShadowAlpha;
}

// One frame: lay the text out at the frame's game time, add its sprites to the batch, draw it in the 2D pass, present.
ModeResult TextViewerMode::update(GameModeStack& /*stack*/, const FrameTime& frame) {
    m_style.timeMs = static_cast<std::uint32_t>(frame.gameTicks / (GameTimer::kTicksPerSecond / 1000));
    // <BIGFONT>'s slot draws with the big font and its batch when there is one; every other slot with the text font.
    const auto big = [this](int slot) { return slot == gui::kBigFontSlot && m_bigFont.has_value(); };
    const gui::FontLookup fonts = [this, &big](int slot) -> const graphics::Font* {
        return big(slot) ? &*m_bigFont : &m_font;
    };
    m_layout = gui::layoutText(m_text, m_style, fonts);
    m_device.beginFrame(kClearColour);
    gui::addTextSprites(m_layout, [this, &big](int slot) { return big(slot) ? &*m_bigBatch : &m_batch; });
    m_pass.queue(m_batch);
    if (m_bigBatch) {
        m_pass.queue(*m_bigBatch);
    }
    m_pass.render(m_device, m_camera);
    m_device.present();
    return ModeResult::Stay;
}

} // namespace coney
