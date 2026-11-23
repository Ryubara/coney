// SPDX-License-Identifier: GPL-3.0-or-later
// Checks against the player's own disc that play on the GPU renderer records its effects for it to draw
// (docs/engine/gpu-renderer.md#effects): level99's coronas and the player's blob shadow, from the disc's own sprite
// sheets, as textured effect draws. It runs only when the environment variable CONEY_DISC names the disc and a GPU
// device opens here, and skips otherwise; it prints counts only (LEGAL.md).
#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "graphics/frame_description.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "support/disc_play_fixtures.h"
#include "support/gpu_capture.h"
#include "world/sector_budget.h"

TEST_CASE("play on the GPU renderer records its effects with their textures", "[disc][render][renderer][effects]") {
    // Expected: ../engine/gpu-renderer.md#effects: the play mode loads its effects' sheets on every renderer that draws
    // the scene (GL3 and the GPU renderer), converting them for librw on GL3 alone, so level99's coronas and the
    // player's blob shadow (SceneLighting's sprites of the disc's `lighting` and `part_page1`) reach the GPU renderer
    // as textured effect draws, as they reach GL3. Before item 20 they were loaded for GL3 only, and the GPU renderer's
    // play frames held no textured effect.
    if (!coney::test::gpuHere()) {
        SKIP("no display or no GPU device here");
    }
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::platform::WindowDesc desc;
    desc.width = 320;
    desc.height = 240;
    desc.hidden = true;
    desc.activate = false;
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Gpu, desc);
    REQUIRE(engine.has_value());
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    auto mode = coney::platform::PlayLevelMode::create(**engine, *wad, "level99", budget, [](std::string_view) {});
    REQUIRE(mode.has_value());
    coney::GameModeStack stack;
    stack.push(**mode);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    (void)stack.runUntilEmpty(timer, {}, 30);

    std::size_t effectDraws = 0;
    std::size_t textured = 0;
    for (const coney::graphics::FramePass& pass : (*engine)->frame().passes()) {
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
