// SPDX-License-Identifier: GPL-3.0-or-later
// Stub for the tests-first commit: the tests compile against it and fail.
#include "studio/theme.h"

namespace coney::studio {

ThemeColour hexColour(std::uint32_t) { return {}; }

ThemeColour textColourFor(ThemeColour) { return {}; }

ThemeColour shade(ThemeColour colour, float) { return colour; }

ThemePalette derivePalette(const ThemeTokens&) { return {}; }

void applyTheme(const ThemeTokens&, float, ImGuiStyle&) {}

} // namespace coney::studio
