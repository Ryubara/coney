// SPDX-License-Identifier: GPL-3.0-or-later
// Stub for the tests-first commit: the tests compile against it and fail.
#include "studio/level_templates.h"

namespace coney::studio {

std::span<const std::string_view> levelTemplates() { return {}; }

std::string basicLevelText() { return {}; }

std::string rehomeTextures(std::string_view, std::string_view) { return {}; }

} // namespace coney::studio
