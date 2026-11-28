// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace coney::ps2 {

// The game's own sine and cosine (fdlibm's float versions on the PS2's floating-point unit), for gameplay whose last
// bit matters: a run's velocity (docs/research/characters-locomotion.md#run-velocity, docs/research/maths.md#trig).

/// The sine of `x` (radians) as the game computes it, to the bit for |x| under 3π/4.
[[nodiscard]] float sinf(float x);

/// The cosine of `x` (radians) as the game computes it, to the bit for |x| under 3π/4.
[[nodiscard]] float cosf(float x);

} // namespace coney::ps2
