// SPDX-License-Identifier: GPL-3.0-or-later

// Checks on a real OpenGL window that the passes which copy the frame and lay it back (the motion blur, the line
// blending, the blur pulse) put the copy back the right way up. Each draws a frame whose top half is red and bottom
// half blue, runs a pass, and reads the top and bottom rows back: a pass that flipped its copy shows blue at the top.
// The motion blur once did, laying an upside-down ghost of the last frame over every frame it blurred.
//
// It runs once where the window system puts the window and once on each display (`--window-display`): a window moved
// to a display with another scale must keep the frame size it asked for, or every copy and capture is off.
//
// It needs a display and an OpenGL 3.3 context; without them it exits with 77, which ctest reports as skipped.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string_view>

#include <SDL3/SDL.h>
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
bool upright(const char* where, const char* pass, Rgba top, Rgba bottom) {
    const bool ok = top.r > 2 * top.b && bottom.b > 2 * bottom.r;
    if (!ok) {
        std::fprintf(stderr, "%s, %s: top (%d, %d, %d), bottom (%d, %d, %d): the copy is laid back upside down\n",
                     where, pass, top.r, top.g, top.b, bottom.r, bottom.g, bottom.b);
    }
    return ok;
}

// What one placement of the window gave: run and passed, run and failed, or no OpenGL window to run on.
enum class Outcome : std::uint8_t { Passed, Failed, Skipped };

// Opens the check's window on `display` (a `--window-display` value; empty for the window system's own place), checks
// that its frame is the size asked for, and runs the three passes in turn; fails when any reads back flipped. `where`
// names the placement in what it prints.
Outcome checkOn(std::string_view display, const char* where) {
    coney::platform::WindowDesc desc;
    desc.title = "Coney frame copy check";
    desc.width = kWidth;
    desc.height = kHeight;
    desc.hidden = true;
    desc.logicalFrame = true;
    desc.display = display;
    auto started = RenderEngine::start(coney::platform::RenderBackend::OpenGl, desc);
    if (!started) {
        std::fprintf(stderr, "skipped: no OpenGL window (%s)\n", started.error().message.c_str());
        return Outcome::Skipped;
    }
    RenderEngine& engine = **started;
    engine.setVsync(false);
    engine.setLineBlend(false);
    // The reads below are placed for a frame of the size asked for. A window moved to a display with another scale
    // once came out a few pixels larger, which letterboxed the picture and read the bars as a flipped copy.
    if (const coney::graphics::Extent size = engine.frameSize(); size.width != kWidth || size.height != kHeight) {
        std::fprintf(stderr, "%s: the frame is %dx%d, not the %dx%d asked for\n", where, size.width, size.height,
                     kWidth, kHeight);
        return Outcome::Failed;
    }
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
        ok = upright(where, "motion blur", readPixel(kWidth / 2, 4), readPixel(kWidth / 2, kHeight - 5)) && ok;
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
        ok = upright(where, "line blend", top, bottom) && ok;
    }

    // 3. The blur pulse's blur, which replaces the screen with its blurred copy.
    {
        engine.beginFrame(Rgba{0, 0, 0, 255});
        drawRedOverBlue(engine);
        engine.blurScreen(2, 0.0F, 0.0F);
        ok = upright(where, "blur pulse", readPixel(kWidth / 2, 16), readPixel(kWidth / 2, kHeight - 17)) && ok;
        engine.present();
    }

    if (ok) {
        std::printf("%s: every frame copy is laid back upright\n", where);
    }
    return ok ? Outcome::Passed : Outcome::Failed;
}

// The number of displays SDL lists; 0 when its video cannot start (no display).
int displayCount() {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return 0;
    }
    int count = 0;
    SDL_free(SDL_GetDisplays(&count));
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return count;
}

} // namespace

// Checks the window where the window system puts it, then on each display in turn (`--window-display 1`, 2, ...),
// which covers a display with another scale than the primary's. CONEY_WINDOW_DISPLAY is cleared first: the check
// chooses its displays itself, whatever the person or agent running it has set.
int main() {
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "CONEY_WINDOW_DISPLAY");
    const Outcome first = checkOn({}, "default place");
    if (first == Outcome::Skipped) {
        return kSkipped;
    }
    bool ok = first == Outcome::Passed;
    const int displays = displayCount();
    for (int i = 1; i <= displays; ++i) {
        // Fixed buffers rather than std::string, whose allocations could throw out of main.
        std::array<char, 16> index{};
        std::array<char, 32> where{};
        std::snprintf(index.data(), index.size(), "%d", i);
        std::snprintf(where.data(), where.size(), "display %d", i);
        ok = checkOn(index.data(), where.data()) == Outcome::Passed && ok;
    }
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
