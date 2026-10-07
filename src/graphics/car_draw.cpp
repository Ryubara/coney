// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/car_draw.h"

namespace coney::graphics {

bool carAtomicDraws(std::size_t atomic, std::uint32_t removedParts, std::span<const float> damage, CarPass pass) {
    const std::size_t part = carPartOf(atomic);
    if (part >= kCarModelParts || ((removedParts >> part) & 1U) != 0) {
        return false;
    }
    // Each part in its own pass.
    if (carPartGlass(part) != (pass == CarPass::Glass)) {
        return false;
    }
    // The wheels have no damaged form: always the undamaged one.
    const bool wheel = part >= kCarFirstWheel && part <= kCarLastWheel;
    const float partDamage = part < damage.size() ? damage[part] : 0.0F;
    const bool damagedForm = atomic >= kCarModelParts;
    if (damagedForm) {
        return !wheel && partDamage >= kCarDamagedForm;
    }
    return wheel || partDamage < kCarDamagedForm;
}

} // namespace coney::graphics
