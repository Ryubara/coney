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

TEST_CASE("the GPU renderer needs a window and reads no frame back", "[options][renderer]") {
    // Expected: ../engine/gpu-renderer.md#choice, Coney's own option
    // It opens a window of its own, so not headless; it only clears and presents so far, so nothing reads its frames.
    CHECK_FALSE(parse(std::array<std::string_view, 3>{"--renderer", "gpu", "--headless"}).has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 6>{"--renderer", "gpu", "--frames", "1", "--screenshot", "a.png"})
                    .has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 4>{"--renderer", "gpu", "--record", "scratch/task/clips/a.mp4"})
                    .has_value());
    CHECK_FALSE(parse(std::array<std::string_view, 6>{"--renderer", "gpu", "--disc", "x", "--render-references", "out"})
                    .has_value());
    // GL3 goes with all of them, as before.
    CHECK(parse(std::array<std::string_view, 3>{"--renderer", "gl3", "--headless"}).has_value());
    auto refused = parse(std::array<std::string_view, 3>{"--renderer", "gpu", "--headless"});
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("--renderer gpu") != std::string::npos);
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
