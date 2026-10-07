// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/human_sounds.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace coney::human {

namespace {

// The hand's and the foot's materials by strength (0x00510190, 0x005101a0).
constexpr std::array<std::uint32_t, 4> kHandMaterials{material::kJab, material::kFist, material::kBigPunch,
                                                      material::kBigPunch};
constexpr std::array<std::uint32_t, 4> kFootMaterials{material::kShoe, material::kShoe, material::kBigKick,
                                                      material::kBigKick};

} // namespace

std::uint32_t strikeMaterial(StrikeLimb limb, int strength, bool bossClass) {
    const auto s = static_cast<std::size_t>(std::clamp(strength, 0, 3));
    switch (limb) {
    case StrikeLimb::Head:
        return material::kHead;
    case StrikeLimb::Hand:
        // A boss-class attacker's punches all sound as its fist.
        return bossClass ? material::kBossFist : kHandMaterials.at(s);
    case StrikeLimb::Foot:
        return kFootMaterials.at(s);
    case StrikeLimb::Other:
        break;
    }
    return 0;
}

StrikeLimb strikeLimbOfClip(std::string_view clipName) {
    std::string name(clipName);
    std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto has = [&name](std::string_view part) { return name.find(part) != std::string::npos; };
    if (has("headbutt") || has("head_butt")) {
        return StrikeLimb::Head;
    }
    if (has("kick") || has("stomp") || has("knee")) {
        return StrikeLimb::Foot;
    }
    return StrikeLimb::Hand;
}

} // namespace coney::human
