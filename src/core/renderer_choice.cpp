// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/renderer_choice.h"

namespace coney {

std::optional<RendererKind> parseRendererName(std::string_view name) {
    if (name == "gl3") {
        return RendererKind::Gl3;
    }
    if (name == "gpu") {
        return RendererKind::Gpu;
    }
    return std::nullopt;
}

std::string_view rendererName(RendererKind kind) { return kind == RendererKind::Gpu ? "gpu" : "gl3"; }

RendererKind rendererOfSetting(int value) { return value == 1 ? RendererKind::Gpu : RendererKind::Gl3; }

RendererChoice chooseRenderer(std::optional<RendererKind> option, RendererKind saved, bool ignoreSaved,
                              bool gpuDrawsMenus) {
    // The option is asked for by name, so it wins even in test mode; the saved setting is a player's, which a test run
    // must not depend on.
    if (option) {
        return {.kind = *option};
    }
    if (ignoreSaved) {
        return {.kind = RendererKind::Gl3};
    }
    // A saved GPU renderer that cannot draw the menus would come up blank with no way back from inside the game
    // (issue #1267), so it starts GL3 instead and the run log says why.
    if (saved == RendererKind::Gpu && !gpuDrawsMenus) {
        return {.kind = RendererKind::Gl3, .savedGpuRefused = true};
    }
    return {.kind = saved};
}

std::string_view savedGpuRefusedWarning() {
    return "the overrides file saves Display/Renderer = 1, but the GPU renderer draws no menus yet, so this run draws "
           "with GL3; start it with --renderer gpu, or set Display/Renderer back to 0 "
           "(docs/engine/gpu-renderer.md#way-back)";
}

int& rendererSetting() {
    static int setting = 0; // GL3 until a player saves otherwise
    return setting;
}

} // namespace coney
