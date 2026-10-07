// SPDX-License-Identifier: GPL-3.0-or-later

// Checks on a real OpenGL window that the passes which copy the frame and lay it back (the motion blur, the line
// blending, the blur pulse) put the copy back the right way up. Each draws a frame whose top half is red and bottom
// half blue, runs a pass, and reads the top and bottom rows back: a pass that flipped its copy shows blue at the top.
// The motion blur once did, laying an upside-down ghost of the last frame over every frame it blurred.
//
// It needs a display and an OpenGL 3.3 context; without them it exits with 77, which ctest reports as skipped.

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include <rw.h>

#include "graphics/render_device.h"
#include "platform/motion_blur_pass.h"
#include "platform/render_engine.h"

namespace {

using coney::graphics::LogicalQuad;
using coney::graphics::Rgba;
using coney::platform::RenderEngine;

constexpr int kWidth = 640;
constexpr int kHeight = 448;
constexpr int kSkipped = 77;

// One pixel of the frame being drawn, `y` counted from the window's top.
Rgba readPixel(int x, int y) {
    std::array<unsigned char, 4> pixel{};
    glReadPixels(x, kHeight - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    return Rgba{pixel[0], pixel[1], pixel[2], pixel[3]};
}

// The test picture: red over blue, split at the middle.
void drawRedOverBlue(RenderEngine& engine) {
    const std::array<LogicalQuad, 2> halves{{
        {0.0F, 0.0F, kWidth, kHeight / 2.0F, {}, Rgba{255, 0, 0, 255}},
        {0.0F, kHeight / 2.0F, kWidth, kHeight / 2.0F, {}, Rgba{0, 0, 255, 255}},
    }};
    engine.drawWindowRects(halves);
}

// True when the top rows read mostly red and the bottom rows mostly blue; prints what it read otherwise.
bool upright(const char* pass, Rgba top, Rgba bottom) {
    const bool ok = top.r > 2 * top.b && bottom.b > 2 * bottom.r;
    if (!ok) {
        std::fprintf(stderr, "%s: top (%d, %d, %d), bottom (%d, %d, %d): the copy is laid back upside down\n", pass,
                     top.r, top.g, top.b, bottom.r, bottom.g, bottom.b);
    }
    return ok;
}

} // namespace

// Runs the three passes in turn and fails when any reads back flipped.
int main() {
    coney::platform::WindowDesc desc;
    desc.title = "Coney frame copy check";
    desc.width = kWidth;
    desc.height = kHeight;
    desc.hidden = true;
    desc.logicalFrame = true;
    auto started = RenderEngine::start(coney::platform::RenderBackend::OpenGl, desc);
    if (!started) {
        std::fprintf(stderr, "skipped: no OpenGL window (%s)\n", started.error().message.c_str());
        return kSkipped;
    }
    RenderEngine& engine = **started;
    engine.setVsync(false);
    engine.setLineBlend(false);
    bool ok = true;

    // 1. Motion blur: keep the picture, then lay it fully over a black frame.
    {
        coney::platform::MotionBlurPass blur;
        const coney::effects::MotionBlur::Colour full{255, 255, 255, 255};
        engine.beginFrame(Rgba{0, 0, 0, 255});
        drawRedOverBlue(engine);
        blur.apply(engine, full);
        engine.present();
        engine.beginFrame(Rgba{0, 0, 0, 255});
        blur.apply(engine, full);
        ok = upright("motion blur", readPixel(kWidth / 2, 4), readPixel(kWidth / 2, kHeight - 5)) && ok;
        engine.present();
    }

    // 2. Line blending: read in the present overlay, which runs after the blend.
    {
        engine.setLineBlend(true);
        Rgba top;
        Rgba bottom;
        engine.setPresentOverlay([&](coney::graphics::RenderDevice&) {
            top = readPixel(kWidth / 2, 4);
            bottom = readPixel(kWidth / 2, kHeight - 5);
        });
        engine.beginFrame(Rgba{0, 0, 0, 255});
        drawRedOverBlue(engine);
        engine.present();
        engine.setPresentOverlay({});
        engine.setLineBlend(false);
        ok = upright("line blend", top, bottom) && ok;
    }

    // 3. The blur pulse's blur, which replaces the screen with its blurred copy.
    {
        engine.beginFrame(Rgba{0, 0, 0, 255});
        drawRedOverBlue(engine);
        engine.blurScreen(2, 0.0F, 0.0F);
        ok = upright("blur pulse", readPixel(kWidth / 2, 16), readPixel(kWidth / 2, kHeight - 17)) && ok;
        engine.present();
    }

    if (ok) {
        std::puts("every frame copy is laid back upright");
    }
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
