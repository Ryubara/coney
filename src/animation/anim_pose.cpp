// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation/anim_pose.h"

#include <algorithm>

#include "animation/skeleton.h"
#include "core/assert.h"

namespace coney::anim {

namespace {

// The index of the key in force at `frame`: the last whose frame is not after it, or 0 before the first.
template <class Key> std::size_t keyAt(std::span<const Key> channel, float frame) {
    const auto after = std::ranges::upper_bound(channel, frame, std::less<>{},
                                                [](const Key& key) { return static_cast<float>(key.frame); });
    return after == channel.begin() ? 0 : static_cast<std::size_t>(after - channel.begin()) - 1;
}

// How far `frame` is from key `i` towards key `i + 1`, in 0 to 1; the caller has checked there is a next key.
template <class Key> float fraction(std::span<const Key> channel, std::size_t i, float frame) {
    const auto start = static_cast<float>(channel[i].frame);
    const auto span = static_cast<float>(channel[i + 1].frame - channel[i].frame);
    return span > 0.0F ? std::clamp((frame - start) / span, 0.0F, 1.0F) : 0.0F;
}

// The reference pose's rotations, (x, y, z, w) per pose bone, as the research page gives them (rounded to four
// places; normalised once in referenceRotations()).
constexpr std::array<Quat, kPoseBones> kReferenceRotations{{
    {0.0F, 0.0F, 0.0F, 1.0F},                // 0
    {-0.4912F, -0.5087F, 0.4912F, 0.5087F},  // 1
    {-0.5F, 0.5F, -0.5F, 0.5F},              // 2
    {0.4793F, -0.5199F, 0.5199F, -0.4793F},  // 3
    {0.0F, -0.0298F, 0.0F, 0.9996F},         // 4
    {0.0F, -0.1757F, 0.0F, 0.9844F},         // 5
    {0.0F, 0.1172F, 0.0F, 0.9931F},          // 6
    {0.5274F, -0.4710F, 0.4710F, 0.5274F},   // 7
    {0.6590F, 0.0F, 0.7521F, 0.0F},          // 8
    {0.0F, -0.2983F, 0.0F, 0.9545F},         // 9
    {0.6081F, -0.5780F, 0.3635F, 0.4050F},   // 10
    {0.0F, 0.6425F, 0.0F, 0.7663F},          // 11
    {0.4043F, -0.3602F, 0.5801F, 0.6085F},   // 12
    {-0.0036F, -0.6166F, 0.0054F, 0.7873F},  // 13
    {0.7393F, 0.0F, 0.6733F, 0.0F},          // 14
    {0.5247F, -0.4805F, 0.4743F, 0.5186F},   // 15
    {0.6200F, -0.7643F, -0.1281F, 0.1227F},  // 16
    {0.0452F, -0.0282F, 0.1112F, 0.9924F},   // 17
    {0.0F, 0.1174F, 0.0F, 0.9931F},          // 18
    {-0.6951F, -0.0567F, 0.0262F, 0.7162F},  // 19
    {-0.0004F, -0.2858F, 0.0001F, 0.9583F},  // 20
    {-0.0811F, -0.3223F, -0.0030F, 0.9431F}, // 21
    {-0.6200F, -0.7643F, 0.1281F, 0.1227F},  // 22
    {-0.0452F, -0.0282F, -0.1112F, 0.9924F}, // 23
    {0.0F, 0.1174F, 0.0F, 0.9931F},          // 24
    {0.6951F, -0.0567F, -0.0262F, 0.7162F},  // 25
    {0.0004F, -0.2858F, -0.0001F, 0.9583F},  // 26
    {0.0811F, -0.3223F, 0.0030F, 0.9431F},   // 27
    {-0.0066F, -0.1154F, 0.9910F, -0.0678F}, // 28
    {0.0F, 0.0340F, 0.0F, 0.9994F},          // 29
    {-0.0030F, -0.0194F, -0.0700F, 0.9974F}, // 30
    {-0.0066F, 0.1154F, 0.9910F, 0.0678F},   // 31
    {0.0F, 0.0340F, 0.0F, 0.9994F},          // 32
    {0.0030F, -0.0194F, 0.0700F, 0.9974F},   // 33
}};

// Whether `bone` is `root` or below it in the skeleton.
bool inSubtree(std::size_t bone, std::size_t root) {
    for (int b = static_cast<int>(bone); b >= 0; b = poseBoneParent(static_cast<std::size_t>(b))) {
        if (static_cast<std::size_t>(b) == root) {
            return true;
        }
    }
    return false;
}

} // namespace

Vec3 sampleChannel(std::span<const PositionKey> channel, float frame) {
    CONEY_ASSERT(!channel.empty());
    const std::size_t i = keyAt(channel, frame);
    if (i + 1 >= channel.size()) {
        return channel[i].value; // the channel's last key holds
    }
    return lerp(channel[i].value, channel[i + 1].value, fraction(channel, i, frame));
}

Quat sampleChannel(std::span<const RotationKey> channel, float frame) {
    CONEY_ASSERT(!channel.empty());
    const std::size_t i = keyAt(channel, frame);
    if (i + 1 >= channel.size()) {
        return channel[i].value;
    }
    return nlerp(channel[i].value, channel[i + 1].value, fraction(channel, i, frame));
}

std::span<const Quat, kPoseBones> referenceRotations() {
    // Normalised once, so the four-place rounding leaves no quaternion off unit length.
    static const std::array<Quat, kPoseBones> rotations = [] {
        std::array<Quat, kPoseBones> unit{};
        std::ranges::transform(kReferenceRotations, unit.begin(), [](Quat q) { return normalise(q); });
        return unit;
    }();
    return rotations;
}

Pose samplePose(const AnimClip& clip, float seconds, std::span<const Quat, kPoseBones> defaultRotations) {
    const float frame = std::clamp(seconds, 0.0F, clip.duration) * kClipFrameRate;
    Pose pose;
    std::ranges::copy(defaultRotations, pose.rotations.begin());
    pose.defaulted.fill(true);
    if (!clip.rootVelocity.empty()) {
        pose.rootVelocity = sampleChannel(clip.rootVelocity, frame);
        pose.hasRootVelocity = true;
    }
    if (!clip.rootTranslation.empty()) {
        pose.rootTranslation = sampleChannel(clip.rootTranslation, frame);
        pose.hasRootTranslation = true;
    }
    for (std::size_t c = 0; c < clip.rotations.size(); ++c) {
        pose.rotations[clip.rotationBones[c]] = sampleChannel(clip.rotations[c], frame);
        pose.defaulted[clip.rotationBones[c]] = false;
    }
    return pose;
}

Pose blendPoses(const Pose& a, const Pose& b, float weight, std::size_t subtreeBone) {
    Pose result = a;
    const bool whole = subtreeBone >= kPoseBones;
    for (std::size_t bone = 0; bone < kPoseBones; ++bone) {
        if (!whole && !inSubtree(bone, subtreeBone)) {
            continue;
        }
        // Two defaults keep a's. Bone 0 is motion: a default side gives way to the other. Otherwise slerp.
        if (a.defaulted[bone] && b.defaulted[bone]) {
            continue;
        }
        if (bone == 0 && (a.defaulted[bone] || b.defaulted[bone])) {
            result.rotations[bone] = a.defaulted[bone] ? b.rotations[bone] : a.rotations[bone];
            result.defaulted[bone] = false;
            continue;
        }
        result.rotations[bone] = slerp(a.rotations[bone], b.rotations[bone], weight);
        result.defaulted[bone] = false;
    }
    if (whole) {
        // The root velocity is motion too: a side without one gives way to the other.
        if (a.hasRootVelocity && b.hasRootVelocity) {
            result.rootVelocity = lerp(a.rootVelocity, b.rootVelocity, weight);
        } else if (b.hasRootVelocity) {
            result.rootVelocity = b.rootVelocity;
        }
        result.rootTranslation = lerp(a.rootTranslation, b.rootTranslation, weight);
        result.hasRootVelocity = a.hasRootVelocity || b.hasRootVelocity;
        result.hasRootTranslation = a.hasRootTranslation || b.hasRootTranslation;
    }
    return result;
}

float AnimCursor::advance(float seconds, float rate) {
    m_time += seconds * rate;
    if (m_time <= m_clip->duration) {
        return 0.0F;
    }
    const float overshoot = m_time - m_clip->duration;
    m_time = m_clip->duration;
    return overshoot;
}

void AnimCursor::restart(float seconds) { m_time = std::clamp(seconds, 0.0F, m_clip->duration); }

} // namespace coney::anim
