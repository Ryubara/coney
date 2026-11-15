// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/vrr_pacing.h"

namespace coney {

std::optional<std::uint32_t> vrrFpsCap(double refreshHz) {
    (void)refreshHz;
    return std::nullopt;
}

PacingChoice choosePacing(std::uint32_t fpsCap, bool vsync, bool vrr, double refreshHz) {
    (void)vrr;
    (void)refreshHz;
    return {.fpsCap = fpsCap, .vsync = vsync};
}

std::string vrrLogLine(const PacingChoice& choice, double refreshHz) {
    (void)choice;
    (void)refreshHz;
    return {};
}

bool wantsVrr(const Options& options, bool saved) {
    (void)options;
    (void)saved;
    return false;
}

bool& vrrSetting() {
    static bool setting = false; // off until a player saves otherwise
    return setting;
}

} // namespace coney
