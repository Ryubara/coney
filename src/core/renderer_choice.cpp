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
                              bool /*gpuDrawsMenus*/) {
    // The option is asked for by name, so it wins even in test mode; the saved setting is a player's, which a test run
    // must not depend on.
    if (option) {
        return {.kind = *option};
    }
    return {.kind = ignoreSaved ? RendererKind::Gl3 : saved};
}

std::string_view savedGpuRefusedWarning() { return {}; }

int& rendererSetting() {
    static int setting = 0; // GL3 until a player saves otherwise
    return setting;
}

} // namespace coney
