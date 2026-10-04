// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "gamemodes/game_mode.h"
#include "graphics/overlay_camera.h"
#include "graphics/particle_page.h"
#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"

namespace coney {

/// A game mode that shows every rectangle of a sprite sheet as a sprite, laid out in a grid over the logical screen
/// (graphics::layoutGrid()), each at its own shape. It draws through a sprite batch and the 2D pass, so what it shows
/// is what the game's sprites go through. It backs `coney --view-sheet` (docs/guides/building.md#run-coney) and stays
/// until the window closes or the frame limit is hit.
///
/// Coney's own tool; nothing in the original corresponds.
class SheetViewerMode final : public GameMode {
  public:
    /// The mode's id, outside the original's range of ids.
    static constexpr std::uint32_t kId = 0x103;
    /// The background: the texture viewer's grey, against which dark and light texels and transparency show.
    static constexpr graphics::Rgba kClearColour{72, 72, 80, 255};
    /// Logical pixels around and between the grid's cells.
    static constexpr int kMargin = 4;

    /// Shows the rectangles of `sheet` through `device`, which must outlive the mode.
    SheetViewerMode(graphics::RenderDevice& device, const graphics::SpriteSheet& sheet);

    [[nodiscard]] std::uint32_t id() const override { return kId; }
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// The sprites one frame adds, in the sheet's order: one per rectangle.
    [[nodiscard]] const std::vector<graphics::Sprite>& layout() const { return m_layout; }

  private:
    graphics::RenderDevice& m_device;
    graphics::OverlayCamera m_camera;
    graphics::SpriteBatch m_batch;
    std::vector<graphics::Sprite> m_layout;
    graphics::OverlayPass m_pass;
};

} // namespace coney
