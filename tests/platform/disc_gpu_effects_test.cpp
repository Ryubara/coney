// SPDX-License-Identifier: GPL-3.0-or-later
// Checks against the player's own disc that play on the GPU renderer records its effects for it to draw
// (docs/engine/gpu-renderer.md#effects): the blob shadows under the humans, from the disc's own sprite sheet, as
// textured effect draws. It runs only when the environment variable CONEY_DISC names the disc and a GPU device opens
// here, and skips otherwise; it prints counts only (LEGAL.md).
#include <cstddef>
#include <cstdio>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "graphics/frame_description.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "support/disc_mesh_views_fixtures.h"
#include "support/gpu_capture.h"

using coney::test::mesh_views::Opened;
using coney::test::mesh_views::openYard;
using coney::test::mesh_views::Yard;

TEST_CASE("play on the GPU renderer records its effects with their textures", "[disc][render][renderer][effects]") {
    // Expected: ../engine/gpu-renderer.md#effects: the play mode loads its effects' sheets on every renderer that draws
    // the scene (GL3 and the GPU renderer), converting them for librw on GL3 alone, so the humans' blob shadows
    // (SceneLighting's sprites of the disc's `part_page1`) reach the GPU renderer as textured effect draws, as they
    // reach GL3. Before item 20 they were loaded for GL3 only, and the GPU renderer's frames held no effect.
    if (!coney::test::gpuHere()) {
        SKIP("no display or no GPU device here");
    }
    Yard yard;
    const Opened opened = openYard(yard, true, coney::platform::RenderBackend::Gpu);
    if (opened == Opened::NoDisc) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    REQUIRE(opened == Opened::Window);
    yard.run(3);

    std::size_t effectDraws = 0;
    std::size_t textured = 0;
    for (const coney::graphics::FramePass& pass : yard.engine->frame().passes()) {
        if (const auto* effects = std::get_if<coney::graphics::EffectPass>(&pass); effects != nullptr) {
            effectDraws += effects->draws.size();
            for (const coney::graphics::EffectDraw& draw : effects->draws) {
                textured += draw.texture != coney::graphics::kNoFrameTexture ? 1U : 0U;
            }
        }
    }
    std::printf("GPU play frame: %zu effect draws, %zu textured\n", effectDraws, textured);
    CHECK(textured > 0);
}
