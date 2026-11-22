// SPDX-License-Identifier: GPL-3.0-or-later

// The screenshot path without a game disc (issue #593): `coney --frames N --screenshot out.png` on a run with no disc
// drew the debug menu's text, one flat quad per lit pixel of Coney's bitmap font, in a single call, and librw's 2D
// buffers hold 10,000 vertices and 10,000 indices (STARTVERTICES and STARTINDICES in its gl3immed.cpp). The call raised
// GL_INVALID_VALUE, drew nothing right, and the read-back that came after it reported the leftover error as
// "glReadPixels could not read the frame". With a disc the menu draws in the game's font, a few quads at a time, which
// is why only the run without one failed. These tests run on a hidden window, as the other window tests do, and skip
// without a display and OpenGL 3.3 (the software pictures job of CI has both).
//
// Expected values: ../engine/renderer.md#today holds librw's limits, from the sources the build pins
// (cmake/deps.cmake); a frame of white 2 x 2 quads on black has exactly four pixels for each quad (the logical frame is
// the window's own 640 x 448, one pixel to one logical pixel, and every quad sits on whole pixels); OpenGL's error
// codes are the specification's (GL_INVALID_VALUE 0x0501, GL_INVALID_ENUM 0x0500).

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <rw.h>

#include "graphics/render_device.h"
#include "platform/draw_calls.h"
#include "platform/render_engine.h"
#include "support/fixtures.h"

namespace {

using coney::graphics::LogicalQuad;
using coney::graphics::Rgba;
using coney::platform::RenderEngine;

constexpr int kWidth = 640;
constexpr int kHeight = 448;

// Starts the OpenGL renderer on a hidden window whose frame is the logical screen, or skips the test where there is no
// display or OpenGL 3.3.
std::unique_ptr<RenderEngine> startWindow() {
    coney::platform::WindowDesc desc;
    desc.title = "Coney screenshot check";
    desc.width = kWidth;
    desc.height = kHeight;
    desc.hidden = true;
    desc.activate = false;
    desc.logicalFrame = true;
    auto started = RenderEngine::start(coney::platform::RenderBackend::OpenGl, desc);
    if (!started) {
        SKIP("no OpenGL window here: " << started.error().message);
    }
    return std::move(*started);
}

// `count` white 2 x 2 quads laid out left to right, top to bottom, four pixels apart, from the top left.
std::vector<LogicalQuad> whiteSpecks(std::size_t count) {
    constexpr std::size_t kPerRow = kWidth / 4;
    std::vector<LogicalQuad> quads;
    quads.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t column = i % kPerRow;
        const std::size_t row = i / kPerRow;
        quads.push_back(LogicalQuad{static_cast<float>(column) * 4.0F,
                                    static_cast<float>(row) * 4.0F,
                                    2.0F,
                                    2.0F,
                                    {},
                                    Rgba{255, 255, 255, 255}});
    }
    return quads;
}

} // namespace

TEST_CASE("a screenshot of a frame of thousands of flat quads, as a run without a disc draws a menu, is read back",
          "[render][window][screenshot]") {
    // Expected: ../engine/renderer.md#today, Coney's own limit of librw's buffers: 6,000 quads is more than the 1,666
    // whose indices librw's 2D index buffer holds (10,000 / 6) and the 2,500 whose vertices its vertex buffer holds
    // (10,000 / 4), and fewer than the 7,168 the grid has room for.
    constexpr std::size_t kQuads = 6000;
    auto engine = startWindow();
    const coney::test::TempDir folder;
    const std::string path = (folder.path() / "specks.png").string();
    engine->requestCapture(engine->framesPresented(), path);
    engine->beginFrame(Rgba{0, 0, 0, 255});
    const std::vector<LogicalQuad> quads = whiteSpecks(kQuads);
    engine->drawQuads(nullptr, quads);
    engine->present();

    const auto& captured = engine->capture();
    // Checked by an `if` rather than REQUIRE, which clang-tidy's optional-access check does not read as a guard.
    if (!captured.has_value() || !captured->has_value()) {
        FAIL("the frame was not captured: " << (captured.has_value() ? captured->error().message
                                                                     : std::string("no capture was requested")));
    } else {
        // Every quad was drawn: none cut off with the ones past the buffer's limit.
        CHECK((*captured)->stats.notBackground == kQuads * 4);
    }
}

TEST_CASE("a read of the frame that OpenGL refuses names the error", "[render][window][screenshot]") {
    // Expected: ../engine/renderer.md#today, Coney's own message: a negative width is GL_INVALID_VALUE (0x0501) in
    // glReadPixels' specification, the error the issue's fix list names as the likeliest; the message must say which
    // one it was.
    auto engine = startWindow();
    engine->beginFrame(Rgba{0, 0, 0, 255});
    std::array<std::uint8_t, 16> rgba{};
    const auto read = coney::platform::draw::readFrame(0, 0, -1, 1, rgba.data());
    engine->present();
    REQUIRE_FALSE(read.has_value());
    CHECK_THAT(read.error().message, Catch::Matchers::ContainsSubstring("GL_INVALID_VALUE"));
}

TEST_CASE("an error an earlier OpenGL call left is not blamed on the read of the frame",
          "[render][window][screenshot]") {
    // Expected: ../engine/renderer.md#today, Coney's own rule for the read: GL_INVALID_ENUM (0x0500) is what
    // glEnable(0) raises; glReadPixels itself is valid here, so the read succeeds, and the leftover error is taken off
    // OpenGL's list so the next check does not find it again.
    auto engine = startWindow();
    engine->beginFrame(Rgba{0, 0, 0, 255});
    glEnable(0);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(kWidth) * 4);
    const auto read = coney::platform::draw::readFrame(0, 0, kWidth, 1, rgba.data());
    CHECK(read.has_value());
    CHECK(glGetError() == GL_NO_ERROR);
    engine->present();
}
