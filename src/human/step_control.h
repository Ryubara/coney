// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// The step control: an AI's single step (the TakeStep and GiveWay actions) as one clip on a heading. The step has no
// distance of its own: the clip's root motion moves the body, and the human turns over half the clip so that the
// clip's own direction ends along the heading.
// Research: docs/research/characters.md#step-control

namespace coney::human {

/// The clip a step plays and the facing it ends on.
struct StepClip {
    std::uint32_t clip = 0; ///< The anim id.
    float endFacing = 0.0F; ///< Radians, 0 facing +y.
};

/// The step toward `heading` for a human facing `facing` (radians): forward within ±22.5°, backward from 112.5° either
/// way, else to the side the heading lies on; the fight stance's steps in `stance`, otherwise the plain steps, or the
/// dashes with `boost` above 0. The end facing points the clip's own direction along `heading`. **Coney's reading**:
/// the side is the one the heading lies on (the research's table names the right step for a positive angle in the
/// original's sign, which the step's turn negates).
/// @orig 0x00243420 Human_StepControl (unknown)
[[nodiscard]] StepClip stepClipFor(float heading, float facing, bool stance, int boost);

} // namespace coney::human
