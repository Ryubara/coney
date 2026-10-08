// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "effects/motion_blur.h"
#include "platform/render_engine.h"

namespace rw {
struct Raster;
struct Texture;
} // namespace rw

namespace coney::platform {

/// Draws a view's motion blur (effects::MotionBlur) over the 3D frame: the last frame, kept in a texture, laid over the
/// new one in the blur's colour at its strength, then the result kept for the next frame, so moving things leave
/// fading trails.
///
/// **Coney's stand-in** for the original's blur, which draws through a 512 × 256 camera texture and the effects camera
/// (device slot `+0x108`, docs/research/graphics.md#device-object) in a way the page does not describe: the kept frame
/// is the whole view at the window's size (RenderEngine::viewRect(), so in a wide window its sides too, as the
/// original's covers its whole screen, docs/research/graphics.md#motion-blur), and the strength is the overlay's alpha.
/// With the NULL backend nothing is drawn.
class MotionBlurPass {
  public:
    MotionBlurPass() = default;
    MotionBlurPass(const MotionBlurPass&) = delete;
    MotionBlurPass& operator=(const MotionBlurPass&) = delete;
    MotionBlurPass(MotionBlurPass&&) = delete;
    MotionBlurPass& operator=(MotionBlurPass&&) = delete;
    /// Must run before the RenderEngine stops.
    ~MotionBlurPass();

    /// Lays the kept frame over the frame being drawn with `colour` (none while its strength is 0), then keeps the
    /// result. Between the 3D pass and the 2D overlays of a frame.
    void apply(RenderEngine& engine, effects::MotionBlur::Colour colour);

  private:
    // Drops the kept frame's texture.
    void release();

    rw::Raster* m_raster = nullptr;
    rw::Texture* m_texture = nullptr;
    int m_width = 0;
    int m_height = 0;
    bool m_kept = false; // whether the texture holds the last frame
};

} // namespace coney::platform
