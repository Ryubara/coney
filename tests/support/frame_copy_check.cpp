// SPDX-License-Identifier: GPL-3.0-or-later

// Checks on a real OpenGL window that the passes which copy the frame and lay it back (the motion blur, the line
// blending, the blur pulse) put the copy back the right way up. Each draws a frame whose top half is red and bottom
// half blue, runs a pass, and reads the top and bottom rows back: a pass that flipped its copy shows blue at the top.
// The motion blur once did, laying an upside-down ghost of the last frame over every frame it blurred.
//
// It runs once where the window system puts the window and once on each display (`--window-display`): a window moved
// to a display with another scale must keep the frame size it asked for, or every copy and capture is off.
//
// Two more things are checked, both about starting and stopping the renderer more than once in one process, which the
// loop over the displays does:
//  - A renderer that stops leaves no pointer to a shader it destroyed behind in librw (`rw::gl3::currentShader`). The
//    next renderer's first shader reads through that pointer, and the read of freed memory crashed this test now and
//    then (issue #757), most often when the machine was busy.
//  - A renderer that cannot start reports it as an error and leaves the process able to start another. A video driver
//    with no OpenGL (SDL's "dummy" one) stands in for a machine that cannot make the window or the context.
//
// The window checks need a display and an OpenGL 3.3 context; without them the test exits with 77, which ctest reports
// as skipped.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

#include <SDL3/SDL.h>
#include <rw.h>

#include "core/error.h"
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

// Checks the frame's size and runs the three passes on a started `engine`; fails when the frame is not the size asked
// for or any pass reads back flipped. `where` names the placement in what it prints.
Outcome checkPasses(RenderEngine& engine, const char* where) {
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
    return ok ? Outcome::Passed : Outcome::Failed;
}

// Opens the check's window on `display` (a `--window-display` value; empty for the window system's own place), runs the
// passes on it, stops the renderer and checks that it left no shader pointer behind. `where` names the placement in
// what it prints.
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
    Outcome outcome = checkPasses(**started, where);

    // The passes draw, so librw has a shader in use; the check below means nothing if it has none.
    if (rw::gl3::currentShader == nullptr) {
        std::fprintf(stderr, "%s: librw has no shader in use after drawing; this check no longer tests anything\n",
                     where);
        outcome = Outcome::Failed;
    }
    // Stopping the renderer destroys the shaders. librw keeps "the shader in use" in a global it never clears, and the
    // next renderer's first shader reads through it before it sets it: the read of freed memory crashed this test now
    // and then. The renderer clears the global as it stops.
    started->reset();
    if (rw::gl3::currentShader != nullptr) {
        std::fprintf(stderr, "%s: the stopped renderer left librw pointing at a shader it destroyed\n", where);
        outcome = Outcome::Failed;
    }
    if (outcome == Outcome::Passed) {
        std::printf("%s: every frame copy is laid back upright\n", where);
    }
    return outcome;
}

// Starts the renderer on SDL's "dummy" video driver, which has a display and no OpenGL, so the window cannot be made:
// the start must come back as an error that says what failed, and the process must be able to go on. Returns true when
// it does. The check needs no display of its own.
bool checkStartFailureIsReported() {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    coney::platform::WindowDesc desc;
    desc.title = "Coney start failure check";
    desc.width = kWidth;
    desc.height = kHeight;
    desc.hidden = true;
    auto started = RenderEngine::start(coney::platform::RenderBackend::OpenGl, desc);
    bool ok = true;
    if (started) {
        std::fprintf(stderr, "start failure: the renderer started on a video driver with no OpenGL\n");
        ok = false;
    } else if (started.error().message.find("could not start the renderer") == std::string::npos) {
        std::fprintf(stderr, "start failure: the error does not say the renderer could not start: %s\n",
                     started.error().message.c_str());
        ok = false;
    } else {
        std::printf("start failure: reported as an error (%s)\n", started.error().message.c_str());
    }
    // A renderer that did start is stopped here, before the hint goes back.
    started = std::unexpected(coney::Error{coney::ErrorCode::Invalid, "released"});
    SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
    return ok;
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

// Checks that a start which cannot happen is reported, then the window where the window system puts it, then on each
// display in turn (`--window-display 1`, 2, ...), which covers a display with another scale than the primary's.
// CONEY_WINDOW_DISPLAY is cleared first: the check chooses its displays itself, whatever the person or agent running
// it has set.
int main() {
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "CONEY_WINDOW_DISPLAY");
    // First, so that every window check after it also shows the renderer can start again after a start that failed.
    bool ok = checkStartFailureIsReported();
    const Outcome first = checkOn({}, "default place");
    if (first == Outcome::Skipped) {
        // Nothing to draw on, but a failed start-failure check is still a failure.
        return ok ? kSkipped : EXIT_FAILURE;
    }
    ok = first == Outcome::Passed && ok;
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
