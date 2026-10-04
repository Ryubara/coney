// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/camera_lens.h"

#include <cmath>
#include <numbers>

namespace coney::camera {

ViewWindow viewWindow(const CameraLens& lens) {
    // The original adds a global (0, inferred to be a field-of-view tweak) to the angle before halving it.
    const float half = lens.fieldOfView * 0.5F * std::numbers::pi_v<float> / 180.0F;
    const float width = std::tan(half);
    return ViewWindow{.halfWidth = width, .halfHeight = width * 0.75F};
}

} // namespace coney::camera
