// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/renderer_choice.h"

namespace coney {

// Stub for the test commit: nothing is recognised yet.
std::optional<RendererKind> parseRendererName(std::string_view /*name*/) { return std::nullopt; }

std::string_view rendererName(RendererKind /*kind*/) { return {}; }

RendererKind rendererOfSetting(int /*value*/) { return RendererKind::Gl3; }

RendererKind chooseRenderer(std::optional<RendererKind> /*option*/, RendererKind /*saved*/, bool /*testMode*/) {
    return RendererKind::Gl3;
}

int& rendererSetting() {
    static int setting = 0;
    return setting;
}

} // namespace coney
