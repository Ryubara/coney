// SPDX-License-Identifier: GPL-3.0-or-later
// Which renderer a run starts with (docs/engine/renderer.md#migration): `--renderer`, the saved Display/Renderer
// setting outside test mode, else GL3.
#include "core/renderer_choice.h"

#include <optional>

#include <catch2/catch_test_macros.hpp>

using coney::chooseRenderer;
using coney::parseRendererName;
using coney::RendererKind;

TEST_CASE("renderer: gl3 and gpu are the names the renderer option takes", "[core][renderer]") {
    // Expected: ../engine/gpu-renderer.md#choice, Coney's own option
    CHECK(parseRendererName("gl3") == std::optional<RendererKind>(RendererKind::Gl3));
    CHECK(parseRendererName("gpu") == std::optional<RendererKind>(RendererKind::Gpu));
    CHECK_FALSE(parseRendererName("GPU").has_value());
    CHECK_FALSE(parseRendererName("vulkan").has_value());
    CHECK_FALSE(parseRendererName("").has_value());
    CHECK(coney::rendererName(RendererKind::Gl3) == "gl3");
    CHECK(coney::rendererName(RendererKind::Gpu) == "gpu");
    // The setting's values: 1 the GPU renderer, anything else GL3 (a hand-edited file cannot pick a third).
    CHECK(coney::rendererOfSetting(1) == RendererKind::Gpu);
    CHECK(coney::rendererOfSetting(0) == RendererKind::Gl3);
    CHECK(coney::rendererOfSetting(7) == RendererKind::Gl3);
}

TEST_CASE("renderer: the option wins, then the saved setting outside test mode, else GL3", "[core][renderer]") {
    // Expected: ../engine/gpu-renderer.md#choice, Coney's own option
    // Nothing given, nothing saved: GL3, the default until the switch (renderer.md#order, item 21).
    CHECK(chooseRenderer(std::nullopt, RendererKind::Gl3, false) == RendererKind::Gl3);
    // The saved setting, at the next start.
    CHECK(chooseRenderer(std::nullopt, RendererKind::Gpu, false) == RendererKind::Gpu);
    // Test mode keeps GL3 whatever a player saved.
    CHECK(chooseRenderer(std::nullopt, RendererKind::Gpu, true) == RendererKind::Gl3);
    // The option wins over the setting, in test mode too (CI's smoke run of the GPU renderer).
    CHECK(chooseRenderer(RendererKind::Gpu, RendererKind::Gl3, true) == RendererKind::Gpu);
    CHECK(chooseRenderer(RendererKind::Gl3, RendererKind::Gpu, false) == RendererKind::Gl3);
}
