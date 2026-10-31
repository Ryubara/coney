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
    // Expected: ../engine/gpu-renderer.md#choice, Coney's own option. The saved GPU renderer is taken only once it
    // draws its own menus (gpuDrawsMenus true here); until then the next test case holds.
    constexpr bool drawsMenus = true;
    // Nothing given, nothing saved: GL3, the default until the switch (renderer.md#order, item 21).
    CHECK(chooseRenderer(std::nullopt, RendererKind::Gl3, false, drawsMenus).kind == RendererKind::Gl3);
    // The saved setting, at the next start.
    CHECK(chooseRenderer(std::nullopt, RendererKind::Gpu, false, drawsMenus).kind == RendererKind::Gpu);
    // Test mode keeps GL3 whatever a player saved.
    CHECK(chooseRenderer(std::nullopt, RendererKind::Gpu, true, drawsMenus).kind == RendererKind::Gl3);
    // The option wins over the setting, in test mode too (CI's smoke run of the GPU renderer).
    CHECK(chooseRenderer(RendererKind::Gpu, RendererKind::Gl3, true, drawsMenus).kind == RendererKind::Gpu);
    CHECK(chooseRenderer(RendererKind::Gl3, RendererKind::Gpu, false, drawsMenus).kind == RendererKind::Gl3);
    CHECK_FALSE(chooseRenderer(std::nullopt, RendererKind::Gpu, false, drawsMenus).savedGpuRefused);
}

TEST_CASE("renderer: a saved GPU renderer starts GL3 with a warning while it draws no menus", "[core][renderer]") {
    // Expected: ../engine/gpu-renderer.md#way-back, Coney's own guard (issue #1267). The GPU renderer only clears and
    // presents until the 2D layers (#1203) and the developer overlay (#1204), so a saved 1 would come up blank with
    // the menus that set it back unseen. When those land, kGpuRendererDrawsMenus turns true, this check fails, and
    // the guard and this test case are removed.
    STATIC_REQUIRE_FALSE(coney::kGpuRendererDrawsMenus);
    // A windowed start with a saved 1 and no option: GL3, and the run log is told why.
    const coney::RendererChoice saved = chooseRenderer(std::nullopt, RendererKind::Gpu, false);
    CHECK(saved.kind == RendererKind::Gl3);
    CHECK(saved.savedGpuRefused);
    CHECK(coney::savedGpuRefusedWarning().find("--renderer gpu") != std::string_view::npos);
    CHECK(coney::savedGpuRefusedWarning().find("Display/Renderer") != std::string_view::npos);
    // `--renderer gpu`, asked for by name, still starts it: developers keep it.
    const coney::RendererChoice option = chooseRenderer(RendererKind::Gpu, RendererKind::Gl3, false);
    CHECK(option.kind == RendererKind::Gpu);
    CHECK_FALSE(option.savedGpuRefused);
    CHECK_FALSE(chooseRenderer(RendererKind::Gpu, RendererKind::Gpu, false).savedGpuRefused);
    // Nothing to warn about where the saved one is not taken anyway (test mode) or names GL3.
    CHECK_FALSE(chooseRenderer(std::nullopt, RendererKind::Gpu, true).savedGpuRefused);
    CHECK_FALSE(chooseRenderer(std::nullopt, RendererKind::Gl3, false).savedGpuRefused);
}
