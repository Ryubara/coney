// SPDX-License-Identifier: GPL-3.0-or-later
// Coney's 2D test cards (`--test-card`, docs/guides/running-viewers.md#test-cards), drawn headless: what each card puts
// in the frame description, which both renderers draw from. Coney's own: the expected values are the cards' own
// (src/platform/test_card_mode.h); CI's software pictures compare what the renderers make of them.
#include "platform/test_card_mode.h"

#include <cstddef>
#include <string_view>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "gamemodes/game_mode.h"
#include "graphics/frame_description.h"
#include "platform/render_engine.h"
#include "support/optional_value.h"

using coney::graphics::FrameDescription;
using coney::graphics::Overlay2dPass;
using coney::graphics::Shape2d;
using coney::graphics::TextureAddress;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;
using coney::platform::TestCardMode;

namespace {

// The one overlay pass a card draws; fails the test when the frame holds anything else.
const Overlay2dPass& onlyPass(const FrameDescription& frame) {
    REQUIRE(frame.passes().size() == 1);
    REQUIRE(std::holds_alternative<Overlay2dPass>(frame.passes()[0]));
    return std::get<Overlay2dPass>(frame.passes()[0]);
}

// Draws one frame of card `name` through `engine`.
void drawCard(RenderEngine& engine, std::string_view name) {
    auto card = TestCardMode::create(engine, name);
    REQUIRE(card.has_value());
    (*card)->render(coney::RenderTime{});
}

} // namespace

// Expected: docs/guides/running-viewers.md#test-cards (the shapes card) and src/platform/test_card_mode.cpp.
TEST_CASE("the shapes card draws flat quads in one overlay pass", "[render][test-card]") {
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    drawCard(**engine, "shapes");
    const FrameDescription& frame = (*engine)->frame();
    // The fill, the bars, the blended quads, the faint quad, the wash and the window rectangles: flat colour.
    const Overlay2dPass& pass = onlyPass(frame);
    CHECK(pass.draws.size() == 6);
    for (const auto& draw : pass.draws) {
        CHECK(draw.shape == Shape2d::Quads);
        CHECK(draw.material.texture == coney::graphics::kNoFrameTexture);
    }
    CHECK(frame.textureCount() == 0);
    // Not black: the fill shows the frame was drawn.
    CHECK_FALSE(frame.start().clear == coney::graphics::kBlack);
}

// Expected: docs/guides/running-viewers.md#test-cards (the textures card) and src/platform/test_card_mode.cpp.
TEST_CASE("the textures card draws three textures, one repeated", "[render][test-card]") {
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    drawCard(**engine, "textures");
    const FrameDescription& frame = (*engine)->frame();
    const Overlay2dPass& pass = onlyPass(frame);
    CHECK(frame.textureCount() == 3);
    std::size_t wrapped = 0;
    std::size_t textured = 0;
    for (const auto& draw : pass.draws) {
        wrapped += draw.material.address == TextureAddress::Wrap ? 1U : 0U;
        textured += draw.material.texture != coney::graphics::kNoFrameTexture ? 1U : 0U;
    }
    CHECK(wrapped == 1);
    CHECK(textured == 6);
}

// Expected: docs/guides/running-viewers.md#test-cards (the triangles card) and src/platform/test_card_mode.cpp.
TEST_CASE("the triangles card draws triangles alpha tested at 0 and at a half", "[render][test-card]") {
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    drawCard(**engine, "triangles");
    const Overlay2dPass& pass = onlyPass((*engine)->frame());
    std::size_t keepAll = 0;
    std::size_t half = 0;
    for (const auto& draw : pass.draws) {
        if (draw.shape != Shape2d::Triangles) {
            continue;
        }
        const float alphaRef = coney::test::got(draw.material.alphaRef);
        keepAll += alphaRef == 0.0F ? 1U : 0U;
        half += alphaRef == 0.5F ? 1U : 0U;
    }
    CHECK(keepAll == 3);
    CHECK(half == 1);
}

// Expected: docs/guides/running-viewers.md#test-cards (the cards' names).
TEST_CASE("a test card of no such name is refused with the names", "[render][test-card]") {
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    auto card = TestCardMode::create(**engine, "colour-bars");
    REQUIRE_FALSE(card.has_value());
    CHECK(card.error().code == coney::ErrorCode::NotFound);
    CHECK(card.error().message.find("shapes, textures or triangles") != std::string_view::npos);
}

// Expected: docs/guides/running-viewers.md#test-cards (the effects card) and src/platform/test_card_mode.cpp.
TEST_CASE("the effects card draws the scene card and then an effect pass", "[render][test-card][effects]") {
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    drawCard(**engine, "effects");
    const FrameDescription& frame = (*engine)->frame();
    REQUIRE(!frame.passes().empty());
    // The scene card's pass first, its effects after it: sprites blended and added, a surface in the fog in force, a
    // texture with alpha and lines.
    CHECK(std::holds_alternative<coney::graphics::ScenePass>(frame.passes().front()));
    const auto* effects = std::get_if<coney::graphics::EffectPass>(&frame.passes().back());
    REQUIRE(effects != nullptr);
    CHECK(effects->camera.has_value());
    std::size_t additive = 0;
    std::size_t lines = 0;
    std::size_t textured = 0;
    for (const auto& draw : effects->draws) {
        additive += draw.states.blend == coney::graphics::EffectBlend::Additive ? 1U : 0U;
        lines += draw.shape == coney::graphics::EffectShape::Lines ? 1U : 0U;
        textured += draw.texture != coney::graphics::kNoFrameTexture ? 1U : 0U;
    }
    CHECK(effects->draws.size() == 5);
    CHECK(additive == 1);
    CHECK(lines == 1);
    CHECK(textured == 1);
}
