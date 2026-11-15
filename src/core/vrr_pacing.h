// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The VRR frame-pacing mode (docs/engine/renderer.md#frame-rate-gaps): for a G-Sync or FreeSync display, which follows
// each frame as it comes, vsync goes off and the frame pacer caps the rate just under the display's refresh rate, so
// every frame lands inside the display's range and none tears. Render-side only: the fixed 1/30 s step never sees it.
// No SDL type appears here, so the choice can be tested; src/platform/window.cpp reads the refresh rate from SDL.
//
// @coney-only enhancement: VRR frame pacing; the original presents at the PS2's field rate

#include <cstdint>
#include <optional>
#include <string>

#include "core/options.h"

namespace coney {

/// The frame cap VRR pacing holds for a display refreshing `refreshHz` times a second: just under it,
/// `refresh - refresh^2 / 3600` rounded down (59 at 60 Hz, 138 at 144 Hz, 224 at 240 Hz), so the margin grows with the
/// rate as a frame's timing jitter becomes a larger share of a shorter period. A rate above kMaxFpsCap counts as
/// kMaxFpsCap. Nothing when the rate is unknown (SDL reports 0) or not a positive number.
/// @coney-only enhancement: VRR frame pacing
[[nodiscard]] std::optional<std::uint32_t> vrrFpsCap(double refreshHz);

/// How a run paces its frames: the cap the frame pacer holds and whether presents wait for the vertical blank.
struct PacingChoice {
    std::uint32_t fpsCap = 0; ///< frames a second at most, 0 for no cap
    bool vsync = true;        ///< presents wait for the vertical blank
    bool vrr = false;         ///< the VRR mode chose the two above
    /// VRR was asked for but the display's refresh rate is unknown, so the cap and vsync stayed as given; the run log
    /// says so.
    bool refreshUnknown = false;
};

/// The pacing for `fpsCap` (`--fps-cap N`, 0 for none) and `vsync` (`--vsync`), or, when `vrr`, the VRR mode's: vsync
/// off and vrrFpsCap() of `refreshHz`, the refresh rate of the display the window is on. VRR with an unknown refresh
/// rate keeps `fpsCap` and `vsync`, and sets PacingChoice::refreshUnknown.
/// @coney-only enhancement: VRR frame pacing
[[nodiscard]] PacingChoice choosePacing(std::uint32_t fpsCap, bool vsync, bool vrr, double refreshHz);

/// The run log's line for `choice`, chosen for a display refreshing `refreshHz` times a second: the cap VRR pacing
/// holds, or that the refresh rate is unknown; empty when VRR was not asked for.
/// @coney-only enhancement: VRR frame pacing
[[nodiscard]] std::string vrrLogLine(const PacingChoice& choice, double refreshHz);

/// Whether a run paces in the VRR mode: `--fps-cap vrr`; else, in a run a person plays (takesSavedRenderer(), so never
/// in test mode) with no `--fps-cap N`, `saved`, the overrides file's `Display/VRR`.
/// @coney-only enhancement: VRR frame pacing
[[nodiscard]] bool wantsVrr(const Options& options, bool saved);

/// The live value of the `Display/VRR` setting, which the Tunables page edits and the overrides file saves (off by
/// default). It is read at the start (debug::savedVrr()); changing it live waits for the Graphics page (#1239).
[[nodiscard]] bool& vrrSetting();

} // namespace coney
