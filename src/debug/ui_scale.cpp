// SPDX-License-Identifier: GPL-3.0-or-later
// The developer overlay's size rule (docs/guides/debug-menu.md#ui-scale). Coney's own tool: nothing in the original
// corresponds.
#include "debug/ui_scale.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace coney::debug {

namespace {

// The fixed scales the Display page offers after Auto.
constexpr std::array<float, 7> kFixedScales{1.0F, 1.25F, 1.5F, 2.0F, 2.5F, 3.0F, 4.0F};

} // namespace

float autoUiScale(float displayHeight) {
    if (!(displayHeight > 0.0F)) {
        return kUiScaleMin;
    }
    const float stepped = std::round(displayHeight / kUiScaleReferenceHeight / kUiScaleStep) * kUiScaleStep;
    return std::clamp(stepped, kUiScaleMin, kUiScaleMax);
}

std::string uiScaleText(float scale) {
    // {:g} drops trailing zeros: 1.5 -> "1.5", 2 -> "2".
    return std::format("{:g}x", scale);
}

const std::vector<std::string>& uiScaleLabels() {
    static const std::vector<std::string> labels = [] {
        std::vector<std::string> out{"Auto"};
        for (const float scale : kFixedScales) {
            out.push_back(uiScaleText(scale));
        }
        return out;
    }();
    return labels;
}

float uiScaleFor(std::size_t choice, float displayHeight) {
    if (choice == 0 || choice > kFixedScales.size()) {
        return autoUiScale(displayHeight);
    }
    return kFixedScales[choice - 1];
}

} // namespace coney::debug
