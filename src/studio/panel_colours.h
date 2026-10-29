// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <imgui.h>

#include "studio/level_document.h"
#include "studio/theme.h"

// Colours the Studio panels share: the Level view's markers (also the Hierarchy's row colours) and the theme's tokens
// as Dear ImGui's. @coney-only the editor; the original has none.

namespace coney::studio {

// The marker colours of the point entities, by kind (Coney's choice: the Level view design's colours are for gizmos).
inline constexpr ImU32 kSpawnColour = IM_COL32(110, 220, 120, 255);
inline constexpr ImU32 kTargetColour = IM_COL32(240, 170, 60, 255);
inline constexpr ImU32 kFighterColour = IM_COL32(235, 90, 80, 255);
inline constexpr ImU32 kViewColour = IM_COL32(190, 190, 200, 255);

// The marker colour of `kind`.
inline ImU32 markerColour(EntityKind kind) {
    switch (kind) {
    case EntityKind::Spawn:
        return kSpawnColour;
    case EntityKind::Target:
        return kTargetColour;
    case EntityKind::Fighter:
        return kFighterColour;
    case EntityKind::View:
        return kViewColour;
    case EntityKind::Primitive:
        break;
    }
    return IM_COL32_WHITE;
}

// The accent of `tokens` as a packed colour with alpha `a`.
inline ImU32 accentColour(const ThemeTokens& tokens, float a = 1.0F) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(tokens.accent.r, tokens.accent.g, tokens.accent.b, a));
}

// `colour` as Dear ImGui's.
inline ImVec4 imguiColour(ThemeColour colour) { return ImVec4(colour.r, colour.g, colour.b, colour.a); }

} // namespace coney::studio
