// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/engine_limits.h"

namespace coney {

std::optional<EngineLimits> parseEngineLimitsName(std::string_view /*name*/) { return std::nullopt; }

std::string_view engineLimitsName(EngineLimits /*limits*/) { return {}; }

} // namespace coney
