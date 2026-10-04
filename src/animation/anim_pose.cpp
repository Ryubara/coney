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

Pose samplePose(const AnimClip& clip, float seconds, std::span<const Quat, kPoseBones> bindRotations) {
    const float frame = std::clamp(seconds, 0.0F, clip.duration) * kClipFrameRate;
    Pose pose;
    std::ranges::copy(bindRotations, pose.rotations.begin());
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
    }
    return pose;
}

Pose blendPoses(const Pose& a, const Pose& b, float weight, std::size_t subtreeBone) {
    Pose result = a;
    const bool whole = subtreeBone >= kPoseBones;
    for (std::size_t bone = 0; bone < kPoseBones; ++bone) {
        if (whole || inSubtree(bone, subtreeBone)) {
            result.rotations[bone] = slerp(a.rotations[bone], b.rotations[bone], weight);
        }
    }
    if (whole) {
        result.rootVelocity = lerp(a.rootVelocity, b.rootVelocity, weight);
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
