// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/vrr_pacing.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace coney {

std::optional<std::uint32_t> vrrFpsCap(double refreshHz) {
    // SDL reports 0 for a rate it does not know; NaN fails the comparison too.
    if (!(refreshHz > 0.0)) {
        return std::nullopt;
    }
    // Clamped first: the margin formula peaks at 1800 Hz and would fall again past it.
    const double hz = std::min(refreshHz, static_cast<double>(kMaxFpsCap));
    constexpr double kMarginScale = 3600.0; // the margin under the rate, in frames a second, is hz^2 / 3600
    const double cap = std::floor(hz - (hz * hz / kMarginScale));
    // A rate under about 1 Hz leaves nothing to cap at; such a display does not exist, so treat it as unknown.
    if (cap < 1.0) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(cap);
}

PacingChoice choosePacing(std::uint32_t fpsCap, bool vsync, bool vrr, double refreshHz) {
    if (!vrr) {
        return {.fpsCap = fpsCap, .vsync = vsync};
    }
    // Vsync off with no cap would tear on a display whose range is unknown, so an unknown rate keeps what was given.
    const std::optional<std::uint32_t> cap = vrrFpsCap(refreshHz);
    if (!cap) {
        return {.fpsCap = fpsCap, .vsync = vsync, .refreshUnknown = true};
    }
    return {.fpsCap = *cap, .vsync = false, .vrr = true};
}

std::string vrrLogLine(const PacingChoice& choice, double refreshHz) {
    if (choice.refreshUnknown) {
        return "VRR pacing: the display's refresh rate is unknown, so the frame rate is paced as without it";
    }
    if (!choice.vrr) {
        return {};
    }
    return std::format("VRR pacing: vsync off, at most {} frames a second, under the display's {:g} Hz", choice.fpsCap,
                       refreshHz);
}

bool wantsVrr(const Options& options, bool saved) {
    if (options.vrrPacing) {
        return true;
    }
    // The saved mode is a player's: a test run must not depend on it, and a cap asked for by number overrides it.
    return saved && takesSavedRenderer(options) && !options.fpsCap.has_value();
}

bool& vrrSetting() {
    static bool setting = false; // off until a player saves otherwise
    return setting;
}

} // namespace coney
