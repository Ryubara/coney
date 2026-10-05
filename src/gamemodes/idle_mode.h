// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "gamemodes/game_mode.h"
#include "graphics/render_device.h"

namespace coney {

/// The bottom of the stack while Coney has no game to run: it stays forever, so the main loop runs until the window
/// is closed or the frame limit is reached. It stands where the original's mode 8 will (docs/research/boot.md#main).
/// Each frame it clears the screen to kClearColour and presents it, so an open window shows a steady colour.
class IdleMode final : public GameMode {
  public:
    /// The mode's id, outside the original's range of ids.
    static constexpr std::uint32_t kId = 0x101;

    /// The colour the idle screen is cleared to: a dark slate, distinct from black so a working present is visible.
    static constexpr graphics::Rgba kClearColour{24, 28, 40, 255};

    /// Draws through `device`, which must outlive the mode; null draws nothing.
    explicit IdleMode(graphics::RenderDevice* device = nullptr) : m_device(device) {}

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    // Nothing to simulate: it only stays.
    ModeResult update(GameModeStack& /*stack*/, const FrameTime& /*frame*/) override { return ModeResult::Stay; }

    // One frame: clear and present when there is a device to draw on.
    void render(const RenderTime& /*time*/) override {
        if (m_device != nullptr) {
            m_device->beginFrame(kClearColour);
            m_device->present();
        }
    }

  private:
    graphics::RenderDevice* m_device;
};

} // namespace coney
