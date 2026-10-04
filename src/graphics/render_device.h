// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace coney::graphics {

/// A colour with 8 bits per channel, alpha 255 for opaque.
struct Rgba {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    friend bool operator==(const Rgba&, const Rgba&) = default;
};

/// The part of the renderer a game mode drives each frame: start a frame cleared to a colour, then show it.
///
/// It stands for the original's render device, which each game mode's update calls to begin the frame and, at the
/// end, to present it (docs/research/boot.md#one-frame, steps 6 and 10). The platform layer implements it with librw
/// (src/platform/render_engine.h); the headless renderer implements both calls as no-ops, so a mode never needs to
/// know whether anything is on screen.
class RenderDevice {
  public:
    virtual ~RenderDevice() = default;
    RenderDevice() = default;
    RenderDevice(const RenderDevice&) = delete;
    RenderDevice& operator=(const RenderDevice&) = delete;
    RenderDevice(RenderDevice&&) = delete;
    RenderDevice& operator=(RenderDevice&&) = delete;

    /// Starts a frame with the whole screen cleared to `clear`. Drawing happens between this and present().
    virtual void beginFrame(Rgba clear) = 0;

    /// Ends the frame and shows it. With a display this waits for the vertical blank, which paces the game as the
    /// original's present does; the headless renderer returns at once.
    virtual void present() = 0;
};

} // namespace coney::graphics
