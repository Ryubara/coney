// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/engine_limits.h"

namespace coney {

std::optional<EngineLimits> parseEngineLimitsName(std::string_view name) {
    if (name == "coney") {
        return EngineLimits::Coney;
    }
    if (name == "original") {
        return EngineLimits::Original;
    }
    return std::nullopt;
}

std::string_view engineLimitsName(EngineLimits limits) {
    return limits == EngineLimits::Original ? "original" : "coney";
}

} // namespace coney
