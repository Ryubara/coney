// SPDX-License-Identifier: GPL-3.0-or-later
// The developer overlay on both renderers (docs/guides/debug-menu.md#the-developer-overlay, gpu-renderer.md#overlay):
// in a scripted run (a hidden window, every frame stepped by the test, nothing real-time) it starts, opens, draws into
// the frame, closes, and leaves the frame as it found it, on librw's GL3 device and on the SDL3 GPU renderer. A
// renderer that cannot start here (no display, no OpenGL 3.3, no GPU device) skips its own case, as the other window
// tests do.
#include <cstdint>
#include <memory>
#include <string>

#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>

#include "debug/debug_session.h"
#include "graphics/frame_stats.h"
#include "graphics/render_device.h"
#include "platform/imgui_overlay.h"
#include "platform/render_engine.h"
#include "support/fixtures.h"

using coney::debug::DebugServices;
using coney::debug::DebugSession;
using coney::debug::TunableRegistry;
using coney::graphics::FrameStats;
using coney::platform::ImGuiOverlay;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;
using coney::platform::WindowDesc;

namespace {

constexpr coney::graphics::Rgba kBackground{20, 40, 60, 255};

// Presents one frame cleared whole to the background colour, with the engine's present overlay, and returns what the
// frame held (summarised against the background), read back before it was shown.
FrameStats presentCaptured(RenderEngine& engine, const std::string& path) {
    engine.requestCapture(engine.framesPresented(), path);
    engine.beginWindowFrame(kBackground);
    engine.present();
    const auto& captured = engine.capture();
    // Checked by an `if` rather than REQUIRE, which clang-tidy's optional-access check does not read as a guard; the
    // failed check names the missing capture and the empty stats fail the test's comparisons after it.
    if (!captured.has_value() || !captured->has_value()) {
        FAIL_CHECK("the frame was not captured: " << (captured.has_value() ? captured->error().message
                                                                           : std::string("no capture was requested")));
        return {};
    }
    return (*captured)->stats;
}

// Runs one renderer through the overlay's whole life in a hidden window, as a scripted run would: closed, opened,
// closed again.
void runOverlayOn(RenderBackend backend) {
    WindowDesc desc;
    desc.activate = false;
    desc.hidden = true;
    desc.width = 640;
    desc.height = 480;
    auto started = RenderEngine::start(backend, desc);
    if (!started) {
        SKIP("this renderer does not start here: " << started.error().message);
    }
    RenderEngine& engine = **started;
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    // No settings file: nothing a player saved can change the layout.
    auto overlay = ImGuiOverlay::start(engine, session, std::string());
    REQUIRE(overlay.has_value());
    ImGuiOverlay& developer = **overlay;
    CHECK_FALSE(developer.visible());
    engine.setPresentOverlay([&developer](coney::graphics::RenderDevice&) { developer.draw(); });
    // A folder of its own: runs at once never write the same file (docs/guides/testing.md#capture-paths).
    const coney::test::TempDir folder;
    const std::string path = (folder.path() / "capture.png").string();

    // Closed it draws nothing: the frame is the background alone.
    const FrameStats closed = presentCaptured(engine, path);
    CHECK(closed.notBackground == 0);

    // Open, it draws its windows over the frame.
    developer.setVisible(true);
    CHECK(developer.visible());
    const FrameStats open = presentCaptured(engine, path);
    CHECK(open.notBackground > 0);
    CHECK(open.hash != closed.hash);

    // Closed again it leaves the frame as it found it, byte for byte.
    developer.setVisible(false);
    CHECK_FALSE(developer.visible());
    const FrameStats closedAgain = presentCaptured(engine, path);
    CHECK(closedAgain.hash == closed.hash);

    // `--dev-overlay N`: shown for N frames, then hidden by itself.
    developer.showForFrames(2);
    for (int frame = 0; frame < 2; ++frame) {
        CHECK(developer.visible());
        engine.beginFrame(kBackground);
        engine.present();
    }
    CHECK_FALSE(developer.visible());

    // The overlay goes before the renderer it draws through.
    engine.setPresentOverlay({});
}

} // namespace

TEST_CASE("the developer overlay opens and closes on GL3 in a scripted run", "[render][renderer][window][debug]") {
    // Expected: ../guides/debug-menu.md#the-developer-overlay, Coney's own overlay: hidden it draws nothing, shown it
    // draws, and closed again the frame is as it was (the check `--dev-overlay N` with `--screenshot` makes).
    runOverlayOn(RenderBackend::OpenGl);
}

TEST_CASE("the developer overlay opens and closes on the GPU renderer in a scripted run",
          "[render][renderer][window][debug]") {
    // Expected: ../engine/gpu-renderer.md#overlay, Coney's own: Dear ImGui's SDL GPU backend draws the overlay into the
    // GPU renderer's frame, so the frame holds it (a capture reads it back) while it is open and not once it is closed.
    runOverlayOn(RenderBackend::Gpu);
}

TEST_CASE("two overlays on the GPU renderer each draw, and one going away leaves the other drawing",
          "[render][renderer][window][debug]") {
    // Expected: ../engine/gpu-renderer.md#overlay, Coney's own: the renderer's overlay drawing is not one slot, so a
    // second Dear ImGui overlay (the console bar beside F1) neither replaces the first nor, going away, takes it down.
    WindowDesc desc;
    desc.activate = false;
    desc.hidden = true;
    desc.width = 640;
    desc.height = 480;
    auto started = RenderEngine::start(RenderBackend::Gpu, desc);
    if (!started) {
        SKIP("this renderer does not start here: " << started.error().message);
    }
    RenderEngine& engine = **started;
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    auto first = ImGuiOverlay::start(engine, session, std::string());
    REQUIRE(first.has_value());
    // The second starts hidden and draws nothing: the first must still draw with it present.
    auto second = ImGuiOverlay::start(engine, session, std::string());
    REQUIRE(second.has_value());
    engine.setPresentOverlay([&first, &second](coney::graphics::RenderDevice&) {
        if (*first) {
            (*first)->draw();
        }
        if (*second) {
            (*second)->draw();
        }
    });
    // A folder of its own: runs at once never write the same file (docs/guides/testing.md#capture-paths).
    const coney::test::TempDir folder;
    const std::string path = (folder.path() / "capture.png").string();

    CHECK(presentCaptured(engine, path).notBackground == 0);
    (*first)->setVisible(true);
    CHECK(presentCaptured(engine, path).notBackground > 0);

    // The second going away (its context was the last one made) leaves the first drawing.
    second->reset();
    CHECK(presentCaptured(engine, path).notBackground > 0);

    // Another second, and now the first goes away: the second draws on its own.
    second = ImGuiOverlay::start(engine, session, std::string());
    REQUIRE(second.has_value());
    (*second)->setVisible(true);
    first->reset();
    CHECK(presentCaptured(engine, path).notBackground > 0);

    engine.setPresentOverlay({});
    second->reset();
}
