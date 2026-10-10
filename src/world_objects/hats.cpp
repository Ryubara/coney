// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/hats.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace coney::world_objects {

namespace {

// The quaternion product a·b: b's rotation, then a's.
anim::Quat multiply(anim::Quat a, anim::Quat b) {
    return anim::Quat{a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// The rotation by `angle` radians about the unit axis `axis`.
anim::Quat aboutAxis(anim::Vec3 axis, float angle) {
    const float s = std::sin(angle / 2.0F);
    return anim::Quat{axis.x * s, axis.y * s, axis.z * s, std::cos(angle / 2.0F)};
}

// `v` turned by `heading` radians about z.
anim::Vec3 turned(anim::Vec3 v, float heading) {
    const float c = std::cos(heading);
    const float s = std::sin(heading);
    return anim::Vec3{v.x * c - v.y * s, v.x * s + v.y * c, v.z};
}

// Argument `i` of a recorded call as a number; 0 when it is none.
double numberOf(const std::vector<script::Value>& call, std::size_t i) {
    return i < call.size() ? call[i].number().value_or(0.0) : 0.0;
}

// Entries 1..N of the table at argument `i`, as tolua reads them: a missing entry or a missing table is 0.
template <std::size_t N> std::array<float, N> tableOf(const std::vector<script::Value>& call, std::size_t i) {
    std::array<float, N> values{};
    if (i >= call.size() || call[i].table() == nullptr) {
        return values;
    }
    for (std::size_t c = 0; c < N; ++c) {
        values.at(c) =
            static_cast<float>(call[i].table()->get(script::Value(static_cast<double>(c + 1))).number().value_or(0.0));
    }
    return values;
}

} // namespace

HatFits HatFits::fromRecorded(const script::RecordedCalls& recorded) {
    HatFits fits;
    for (const std::vector<script::Value>& call : recorded.calls("CfgHat")) {
        const auto whole = [&call](std::size_t i) { return static_cast<int>(std::trunc(numberOf(call, i))); };
        const std::optional<std::string_view> hat = call.size() > 2 ? call[2].string() : std::nullopt;
        const std::array<float, 3> offset = tableOf<3>(call, 3);
        const std::array<float, 4> rotation = tableOf<4>(call, 4);
        fits.add(whole(0), whole(1), hat.value_or(""),
                 HatFit{.offset = anim::Vec3{offset[0], offset[1], offset[2]},
                        .rotation = anim::Quat{rotation[0], rotation[1], rotation[2], rotation[3]}});
    }
    return fits;
}

void HatFits::add(int set, int owner, std::string_view hat, HatFit fit) {
    auto at = std::ranges::lower_bound(m_sets, set, {}, &Set::index);
    if (at == m_sets.end() || at->index != set) {
        at = m_sets.insert(at, Set{.index = set, .slots = {}});
    }
    at->owner = owner;
    if (at->slots.size() < kHatSlots) {
        at->slots.emplace_back(std::string(hat), fit);
    }
}

std::optional<HatFit> HatFits::find(int type, int classType, std::string_view hat) const {
    // The type's own set first, else its class's.
    const auto owned = [this](int owner) -> const Set* {
        const auto at = std::ranges::find(m_sets, owner, &Set::owner);
        return at != m_sets.end() ? &*at : nullptr;
    };
    const Set* set = owned(type);
    if (set == nullptr) {
        set = owned(classType);
    }
    if (set == nullptr) {
        return std::nullopt;
    }
    for (const auto& [name, fit] : set->slots) {
        if (name == hat) {
            return fit;
        }
    }
    return std::nullopt;
}

HeldAttachment hatAttachment(const HatFits& fits, bool warrior, int type, int classType, std::string_view hatType,
                             HatFit own, float scale) {
    const HatFit fit = warrior ? fits.find(type, classType, hatType).value_or(own) : own;
    return HeldAttachment{.bone = kHatBone, .position = anim::scale(fit.offset, scale), .rotation = fit.rotation};
}

HatThrow hatThrow(int side) {
    static constexpr std::array<HatThrow, 4> kThrows{{
        {anim::Vec3{0.0F, 1.0F, 0.0F}, anim::Vec3{1.0F, 0.0F, 0.0F}},
        {anim::Vec3{1.0F, 0.0F, 0.0F}, anim::Vec3{0.0F, -1.0F, 0.0F}},
        {anim::Vec3{0.0F, -1.0F, 0.0F}, anim::Vec3{-1.0F, 0.0F, 0.0F}},
        {anim::Vec3{-1.0F, 0.0F, 0.0F}, anim::Vec3{0.0F, 1.0F, 0.0F}},
    }};
    return kThrows.at(static_cast<std::size_t>(side & 3));
}

void FallingHats::knockOff(double hat, WorldPose pose, float heading, HatThrow how) {
    forget(hat);
    m_falls.push_back(Fall{.hat = hat,
                           .pose = pose,
                           .velocity = anim::scale(turned(how.direction, heading), kHatThrowSpeed),
                           .spinAxis = turned(how.spinAxis, heading)});
}

std::vector<std::pair<double, WorldPose>> FallingHats::step(float seconds, const GroundBelow& ground) {
    std::vector<std::pair<double, WorldPose>> moved;
    std::erase_if(m_falls, [&](Fall& fall) {
        fall.velocity.z -= kGravity * seconds;
        fall.pose.position = anim::add(fall.pose.position, anim::scale(fall.velocity, seconds));
        fall.pose.rotation =
            anim::normalise(multiply(aboutAxis(fall.spinAxis, kHatSpin * seconds), fall.pose.rotation));
        // Landed once it is at or below the ground under it.
        const std::optional<float> floor = ground ? ground(fall.pose.position) : std::nullopt;
        const bool landed = floor && fall.pose.position.z <= *floor;
        if (landed) {
            fall.pose.position.z = *floor;
        }
        moved.emplace_back(fall.hat, fall.pose);
        return landed;
    });
    return moved;
}

bool FallingHats::falling(double hat) const { return std::ranges::contains(m_falls, hat, &Fall::hat); }

void FallingHats::forget(double hat) {
    std::erase_if(m_falls, [hat](const Fall& fall) { return fall.hat == hat; });
}

} // namespace coney::world_objects
