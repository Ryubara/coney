// SPDX-License-Identifier: GPL-3.0-or-later
// `--renderer gl3|gpu` on the command line (docs/engine/gpu-renderer.md#choice): what it takes, what it refuses, and
// that a player's release build takes it too.
#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/options.h"

using coney::ErrorCode;
using coney::OptionSet;
using coney::parseOptions;

namespace {
// Parses a fixed list of arguments as the build with the debug tools does.
template <std::size_t N> auto parse(const std::array<std::string_view, N>& args) {
    return parseOptions(std::span<const std::string_view>(args));
}
} // namespace

// No test name starts with "-": ctest would hand a leading "--" to Catch2 as one of its own options.

TEST_CASE("the renderer option takes gl3 or gpu and refuses anything else", "[options][renderer]") {
    // Expected: ../engine/gpu-renderer.md#choice, Coney's own option
    auto plain = parseOptions({});
    REQUIRE(plain.has_value());
    CHECK_FALSE(plain->renderer.has_value());
    auto gpu = parse(std::array<std::string_view, 2>{"--renderer", "gpu"});
    REQUIRE(gpu.has_value());
    CHECK(gpu->renderer == std::optional<coney::RendererKind>(coney::RendererKind::Gpu));
    auto gl3 = parse(std::array<std::string_view, 2>{"--renderer", "gl3"});
    REQUIRE(gl3.has_value());
    CHECK(gl3->renderer == std::optional<coney::RendererKind>(coney::RendererKind::Gl3));
    // CI's smoke run: the GPU renderer in test mode, for a few frames.
    auto smoke = parse(std::array<std::string_view, 4>{"--renderer", "gpu", "--frames", "3"});
    REQUIRE(smoke.has_value());
    CHECK(smoke->renderer == std::optional<coney::RendererKind>(coney::RendererKind::Gpu));
    auto bad = parse(std::array<std::string_view, 2>{"--renderer", "vulkan"});
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == ErrorCode::InvalidArgument);
    CHECK(bad.error().message.find("gl3 or gpu") != std::string::npos);
    CHECK_FALSE(parse(std::array<std::string_view, 1>{"--renderer"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 4>{"--renderer", "gpu", "--renderer", "gl3"}).has_value());
    CHECK(coney::usageText().find("--renderer gl3|gpu") != std::string_view::npos);
}

TEST_CASE("the GPU renderer needs a window and draws no reference render", "[options][renderer]") {
    // Expected: ../engine/gpu-renderer.md#choice, Coney's own option
    // It opens a window of its own, so not headless; it draws no reference renders, which are GL3's offscreen.
    CHECK_FALSE(parse(std::array<std::string_view, 3>{"--renderer", "gpu", "--headless"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 6>{"--renderer", "gpu", "--disc", "x", "--render-references", "out"})
                    .has_value());
    // GL3 goes with all of them, as before.
    CHECK(parse(std::array<std::string_view, 3>{"--renderer", "gl3", "--headless"}).has_value());
    auto refused = parse(std::array<std::string_view, 3>{"--renderer", "gpu", "--headless"});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("--renderer gpu") != std::string::npos);
}

TEST_CASE("the GPU renderer records a clip", "[options][renderer]") {
    // Expected: ../engine/gpu-renderer.md#capture, Coney's own option: it hands each frame to the clip recorder as
    // GL3 does (../guides/running-options.md#record), so a pull request on the GPU renderer shows a clip of it.
    auto clip = parse(std::array<std::string_view, 4>{"--renderer", "gpu", "--record", "scratch/task/clips/a.mp4"});
    REQUIRE(clip.has_value());
    CHECK(clip->renderer == std::optional<coney::RendererKind>(coney::RendererKind::Gpu));
    CHECK(clip->recordPath == std::optional<std::string>("scratch/task/clips/a.mp4"));
}

TEST_CASE("the GPU renderer takes a screenshot of the last frame", "[options][renderer]") {
    // Expected: ../engine/gpu-renderer.md#capture, Coney's own option: it reads its frames back since it draws the 2D
    // layers, so the software pictures draw the test cards on both renderers.
    auto shot = parse(std::array<std::string_view, 6>{"--renderer", "gpu", "--frames", "3", "--screenshot", "a.png"});
    REQUIRE(shot.has_value());
    CHECK(shot->renderer == std::optional<coney::RendererKind>(coney::RendererKind::Gpu));
    CHECK(shot->screenshotPath == std::optional<std::string>("a.png"));
}

TEST_CASE("the test card option takes a card's name and needs no disc", "[options][renderer][test-card]") {
    // Expected: ../guides/running-viewers.md#test-cards, Coney's own option: the cards are Coney's own drawing.
    auto card = parse(std::array<std::string_view, 6>{"--test-card", "shapes", "--frames", "3", "--renderer", "gpu"});
    REQUIRE(card.has_value());
    CHECK(card->testCard == std::optional<std::string>("shapes"));
    CHECK_FALSE(parse(std::array<std::string_view, 1>{"--test-card"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 4>{"--test-card", "shapes", "--test-card", "textures"}).has_value());
    // One picture at a time: not with another mode.
    CHECK_FALSE(parse(std::array<std::string_view, 3>{"--test-card", "shapes", "--sandbox"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 6>{"--test-card", "shapes", "--disc", "x", "--play-level", "level1"})
                    .has_value());
    // A debug tool: not in a player's build.
    const std::array<std::string_view, 2> args{"--test-card", "shapes"};
    CHECK_FALSE(parseOptions(std::span<const std::string_view>(args), OptionSet::Player).has_value());
    CHECK(coney::usageText().find("--test-card NAME") != std::string_view::npos);
}

TEST_CASE("the release build takes the renderer option", "[options][renderer]") {
    // Expected: ../engine/gpu-renderer.md#choice, Coney's own option
    // A player tries the GPU renderer from their own build (docs/engine/renderer.md#migration).
    const std::array<std::string_view, 2> args{"--renderer", "gpu"};
    auto options = parseOptions(std::span<const std::string_view>(args), OptionSet::Player);
    REQUIRE(options.has_value());
    CHECK(options->renderer == std::optional<coney::RendererKind>(coney::RendererKind::Gpu));
    CHECK(coney::usageText(OptionSet::Player).find("--renderer gl3|gpu") != std::string::npos);
}

TEST_CASE("only a run a person plays takes the saved renderer", "[options][renderer]") {
    // Expected: ../engine/gpu-renderer.md#choice, Coney's own option
    auto plain = parseOptions({});
    REQUIRE(plain.has_value());
    CHECK(coney::takesSavedRenderer(*plain));
    // Test mode draws the same whatever a player saved.
    auto frames = parse(std::array<std::string_view, 2>{"--frames", "3"});
    REQUIRE(frames.has_value());
    CHECK_FALSE(coney::takesSavedRenderer(*frames));
    // Reference renders draw offscreen through GL3, which `--renderer gpu` cannot be combined with either.
    auto references = parse(std::array<std::string_view, 4>{"--disc", "x", "--render-references", "out"});
    REQUIRE(references.has_value());
    CHECK_FALSE(coney::takesSavedRenderer(*references));
}
