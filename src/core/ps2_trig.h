// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace coney::ps2 {

/// The sine of `x` (radians) as the game computes it.
[[nodiscard]] float sinf(float x);

/// The cosine of `x` (radians) as the game computes it.
[[nodiscard]] float cosf(float x);

} // namespace coney::ps2
