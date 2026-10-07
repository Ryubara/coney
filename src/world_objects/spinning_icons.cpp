// SPDX-License-Identifier: GPL-3.0-or-later
// The spinning icons over humans (docs/research/ai.md#dealer-icon).
#include "world_objects/spinning_icons.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace coney::world_objects {

namespace {

// The heights over the feet: the dealers' icons, every other icon.
constexpr float kDealerIconHeight = 2.5F;
constexpr float kIconHeight = 2.25F;
// The turn, radians a second (angular velocity (0, 0, π)), and the period it repeats in, ms.
constexpr float kTurnRate = std::numbers::pi_v<float>;
constexpr std::uint64_t kTurnPeriodMs = 2000;

// The icons that do not turn.
constexpr std::array<std::string_view, 5> kStill{"dyn_play_one", "dyn_play_two", "dyn_play_one_euro",
                                                 "dyn_play_two_euro", "dyn_lizziestarget"};

} // namespace

std::string_view dealerIcon(int type) {
    switch (type) {
    case 0:
        return kFlashDealerIcon;
    case 1:
        return kWeaponDealerIcon;
    case 2:
        return kSprayDealerIcon;
    default:
        return {};
    }
}

IconPose spinningIconPose(std::string_view name, anim::Vec3 feet, std::uint64_t ms) {
    const bool dealer = name == kFlashDealerIcon || name == kWeaponDealerIcon || name == kSprayDealerIcon;
    const float height = dealer ? kDealerIconHeight : kIconHeight;
    IconPose pose;
    pose.position = anim::Vec3{feet.x, feet.y, feet.z + height};
    if (std::ranges::find(kStill, name) == kStill.end()) {
        // The angle within one turn (kept small so the float stays exact over a long game), about +z.
        const float angle = kTurnRate * static_cast<float>(ms % kTurnPeriodMs) / 1000.0F;
        pose.rotation = anim::Quat{0.0F, 0.0F, std::sin(angle / 2.0F), std::cos(angle / 2.0F)};
    }
    return pose;
}

} // namespace coney::world_objects
