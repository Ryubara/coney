// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "gamemodes/game_mode.h"
#include "graphics/font.h"
#include "graphics/overlay_camera.h"
#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"
#include "gui/text_layout.h"

namespace coney {

/// A game mode that lays out one marked-up text in a font and draws it every frame through a sprite batch and the 2D
/// pass, as a text widget would: the text's game time advances with the frames, so `<PULSE>` and `<DISPLAYTIME>`
/// play. It backs `coney --view-text` (docs/guides/building.md#run-coney) and stays until the window
/// closes or the frame limit is hit.
///
/// Coney's own tool; nothing in the original corresponds.
class TextViewerMode final : public GameMode {
  public:
    /// The mode's id, outside the original's range of ids.
    static constexpr std::uint32_t kId = 0x104;
    /// The background: a dark blue-grey against which white text and its shadow both show.
    static constexpr graphics::Rgba kClearColour{40, 44, 60, 255};
    /// The GUI x of the text's left edge.
    static constexpr float kLeft = 0.04F;
    /// The GUI y of the first line's centre.
    static constexpr float kTop = 0.06F;
    /// The drop shadow's strength: half.
    static constexpr std::uint8_t kShadowAlpha = 128;
    /// The most sprites a frame holds.
    static constexpr std::size_t kCapacity = 4096;
    /// The depth of the font batch, as big_font's text (docs/research/gui.md#draw-order).
    static constexpr float kDepth = 9000.0F;

    /// Shows `text` through `device`, which must outlive the mode, starting in `font` (the text font slot, and every
    /// slot but `<BIGFONT>`'s) and switching to `bigFont` on `<BIGFONT>` (to `font` when there is none).
    TextViewerMode(graphics::RenderDevice& device, graphics::Font font, std::optional<graphics::Font> bigFont,
                   std::string text, float scale = 1.0F);

    [[nodiscard]] std::uint32_t id() const override { return kId; }
    /// One step: lays the text out at the step's game time and lists its sprites for the 2D pass.
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;
    /// Clears, draws the listed sprites in the 2D pass and presents. The text's effects step at 30 a second, as the
    /// original's do.
    void render(const RenderTime& time) override;

    /// The last frame's layout.
    [[nodiscard]] const gui::TextLayout& layout() const { return m_layout; }

  private:
    graphics::RenderDevice& m_device;
    graphics::Font m_font;
    std::optional<graphics::Font> m_bigFont;
    std::string m_text;
    gui::TextStyle m_style;
    graphics::OverlayCamera m_camera;
    graphics::SpriteBatch m_batch;
    std::optional<graphics::SpriteBatch> m_bigBatch;
    graphics::OverlayPass m_pass;
    gui::TextLayout m_layout;
};

} // namespace coney
