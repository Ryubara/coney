// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/human_blood.h"

#include <algorithm>
#include <cmath>

namespace coney::graphics {

BloodLayer bloodLayerFor(float healthPercent, bool noBlood) {
    // The slot index counts up with health: 0 below 30%, 1 from 30%, 2 from 60%; slot 2 is the lightest texture.
    const float band = std::max(0.0F, std::trunc(healthPercent / 30.0F));
    const int index = static_cast<int>(std::min(band, 2.0F));
    BloodLayer layer;
    layer.texture = index == 2 ? BloodTexture::Light : index == 1 ? BloodTexture::Medium : BloodTexture::Heavy;
    layer.shows = healthPercent < kBloodShowsBelowPercent && !noBlood;
    return layer;
}

} // namespace coney::graphics
