// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ps2_trig.h"

#include "core/ps2_float.h"

#include <cmath>

namespace coney::ps2 {

float sinf(float x) { return towardZero(std::sin(static_cast<double>(x))); }

float cosf(float x) { return towardZero(std::cos(static_cast<double>(x))); }

} // namespace coney::ps2
