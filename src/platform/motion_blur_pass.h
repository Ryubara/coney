// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "effects/motion_blur.h"
#include "graphics/frame_description.h"
#include "platform/render_engine.h"

namespace coney::platform {

/// Draws a view's motion blur (effects::MotionBlur) over the 3D frame: the last frame, kept by the renderer, laid over
/// the new one in the blur's colour at its strength, then the result kept for the next frame, so moving things leave
/// fading trails. It records the pass into the frame description (graphics::MotionBlurOverlay) with a copy of its own,
/// which the renderer keeps from frame to frame (platform/gl3_renderer.h).
///
/// **Coney's stand-in** for the original's blur, which draws through a 512 × 256 camera texture and the effects camera
/// (device slot `+0x108`, docs/research/graphics.md#device-object) in a way the page does not describe: the kept frame
/// is the whole view at the window's size (RenderEngine::viewRect(), so in a wide window its sides too, as the
/// original's covers its whole screen, docs/research/graphics.md#motion-blur), and the strength is the overlay's alpha.
/// With the NULL backend the pass is recorded and nothing is drawn.
class MotionBlurPass {
  public:
    MotionBlurPass() = default;
    MotionBlurPass(const MotionBlurPass&) = delete;
    MotionBlurPass& operator=(const MotionBlurPass&) = delete;
    MotionBlurPass(MotionBlurPass&&) = delete;
    MotionBlurPass& operator=(MotionBlurPass&&) = delete;
    /// Gives its copy back to the renderer. Must run before the RenderEngine stops.
    ~MotionBlurPass();

    /// Lays the kept frame over the frame being drawn with `colour` (none while its strength is 0, which also forgets
    /// the kept frame), then keeps the result. Between the 3D pass and the 2D overlays of a frame.
    void apply(RenderEngine& engine, effects::MotionBlur::Colour colour);

  private:
    RenderEngine* m_engine = nullptr;  // the engine its copy belongs to, from the first apply()
    graphics::ScreenCopyId m_copy = 0; // its kept frame, 0 before the first apply()
};

} // namespace coney::platform
