// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "human/locomotion.h"

// The player's jump: triangle at a run or a sprint leaves the ground at 5.5 m/s up, at the run or sprint speed
// forward, and steers in the air. Pure rules; the human (src/human/human.*) applies them.
// Research: docs/research/characters.md#jump

namespace coney::human {

/// The jump's values the debug menus may edit while the game runs (docs/guides/debug-menu.md#tunables). Each defaults
/// to the researched constant; read through jumpTuning().
struct JumpTuning {
    float minSpeed = 3.3F;       ///< At or below this speed (`+0x1ac`) no jump (`0x0051018c`), m/s.
    float upSpeed = 5.5F;        ///< The vertical speed at launch (`0x00510188`), m/s.
    float climbableCheck = 5.5F; ///< A climbable wall this close ahead (from 1.7 m up) refuses the jump, m.
    float airTurnDegrees = 4.0F; ///< The air turn's limit an update; see airTurnLimit().
};

/// The one JumpTuning the game uses; at its defaults unless a debug menu changed it.
[[nodiscard]] JumpTuning& jumpTuning();

/// Whether a jump may start (`Player_TryJump` and `Human_BeginJump`): faster than the minimum speed, at a speed whose
/// gait is a jog or more, and no climbable wall ahead. The stick (above the run threshold) and the human's state are
/// the caller's checks; a start clip does not stop it. **Coney's reading**: the gait is the stored one (gaitOfSpeed(),
/// `+0x1a8`, the nearest gait), as a tap 7 updates into the run start jumped at 3.35 m/s at runtime, where the
/// "reached" gait (gaitForSpeed()) the page reads in the code would still be a walk (docs/research/feel.md).
/// @orig 0x002829e8 Player_TryJump (unknown)
/// @orig 0x0023db48 Human_BeginJump (unknown)
[[nodiscard]] bool jumpAllowed(float speed, const Speeds& speeds, bool climbableAhead);

/// The horizontal speed a jump leaves at, by the gait at take-off (the stored gait, as jumpAllowed() reads it): the run
/// speed for a jog or a run, the sprint speed for a sprint, the jog speed below.
/// @orig 0x002217f0 Human_LaunchJump (unknown)
[[nodiscard]] float launchSpeed(Gait takeOffGait, const Speeds& speeds);

/// The air turn's limit an update, radians. **Coney's choice**: the research does not trace which gait
/// `Human_AirControl` passes to `Human_MaxTurn`; a sprint jump was seen turning about 4° an update (the run's limit,
/// not the sprint's 2.5°), so every jump turns at jumpTuning().airTurnDegrees.
/// @orig 0x00240898 Human_AirControl (unknown)
[[nodiscard]] float airTurnLimit();

} // namespace coney::human
