// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/face.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "animation/anim_clip.h"
#include "animation/anim_task.h"
#include "characters/anim_set.h"

namespace coney::human {

namespace {

// The rows of the face's sample that reach the default pose: the head (6) to the eyelid (14).
constexpr std::size_t kFirstFaceDefaultBone = 6;
constexpr std::size_t kLastFaceDefaultBone = 14;

// The whole frame `seconds` falls on as the cursor counts it: `uint(t × 30 + 0.5)`.
// @orig 0x001044a0 AnimCursor_Advance (unknown)
std::uint32_t roundedFrame(float seconds) {
    return static_cast<std::uint32_t>(std::max(0.0F, seconds * anim::kClipFrameRate + 0.5F));
}

} // namespace

void FaceState::setMood(FaceMood mood) {
    // While a line plays the state stays a speaking one: the mood's odd state.
    const int even = static_cast<int>(mood);
    m_value = talking() ? even + 1 : even;
}

void FaceState::beginTalk() {
    if (!talking()) {
        m_value += 1;
    }
}

void FaceState::endTalk() {
    if (talking()) {
        m_value -= 1;
    }
}

void FaceLayer::update(const FaceState& state, const characters::AnimSet& anims, bool shown) {
    const std::size_t first = state.firstClip();
    if (m_current == first || !shown) {
        return;
    }
    // The new face task starts on its first id, at the rate that id's flags give it (FaceTask_Construct).
    const Loop next{.first = first, .last = state.lastClip(), .id = first, .time = 0.0F, .rate = anims.rate(first)};
    if (!m_current) {
        m_loop = next;
        m_current = first;
        return;
    }
    // A change: the old loop fades out under the new one over 1/30 s, at once into a speaking state, and never longer
    // than the new first clip takes to play (its length over its rate, Anim_GetPlayTime).
    float fade = state.talking() ? 0.0F : kFaceFadeSeconds;
    if (const anim::AnimClip* clip = anims.clip(first); clip != nullptr && next.rate > 0.0F) {
        fade = std::min(fade, clip->duration / next.rate);
    }
    // A fade of no length gives the old pose no weight: the original's fade task goes at its first update.
    m_old = fade > 0.0F ? m_loop : std::nullopt;
    m_loop = next;
    m_fadeSeconds = fade;
    m_fadeElapsed = 0.0F;
    m_current = first;
}

void FaceLayer::advanceLoop(Loop& loop, float seconds, const characters::AnimSet& anims, const Draw& draw) {
    const anim::AnimClip* clip = anims.clip(loop.id);
    const float next = loop.time + seconds * loop.rate;
    // The cursor's end test: the step ends the clip when the frame it reaches, rounded, is the clip's frame count or
    // more (AnimCursor_Advance); then the time does not move.
    if (clip == nullptr || roundedFrame(next) < roundedFrame(clip->duration)) {
        loop.time = next;
        return;
    }
    // At the clip's end a random id of the range takes its place, from its start; the rate stays the first id's.
    const int low = static_cast<int>(loop.first);
    const int high = static_cast<int>(loop.last);
    loop.id = static_cast<std::size_t>(draw ? std::clamp(draw(low, high), low, high) : low);
    loop.time = 0.0F;
}

void FaceLayer::advance(float seconds, const characters::AnimSet& anims, const Draw& draw) {
    if (m_loop) {
        advanceLoop(*m_loop, seconds, anims, draw);
    }
    if (m_old) {
        // The outgoing loop keeps playing through the fade; past the fade it is given back.
        m_fadeElapsed += seconds;
        if (m_fadeElapsed > m_fadeSeconds) {
            m_old.reset();
        } else {
            advanceLoop(*m_old, seconds, anims, draw);
        }
    }
}

std::optional<std::size_t> FaceLayer::playing() const {
    if (!m_loop) {
        return std::nullopt;
    }
    return m_loop->id;
}

std::array<anim::Quat, anim::kPoseBones> FaceLayer::defaults(std::span<const anim::Quat, anim::kPoseBones> reference,
                                                             const characters::AnimSet& anims) const {
    std::array<anim::Quat, anim::kPoseBones> out{};
    std::ranges::copy(reference, out.begin());
    const auto sample = [&](const Loop& loop) -> std::optional<anim::Pose> {
        const anim::AnimClip* clip = anims.clip(loop.id);
        if (clip == nullptr) {
            return std::nullopt;
        }
        return anim::samplePose(*clip, loop.time, reference);
    };
    if (!m_loop) {
        return out;
    }
    std::optional<anim::Pose> pose = sample(*m_loop);
    if (!pose) {
        return out;
    }
    if (m_old) {
        if (const std::optional<anim::Pose> old = sample(*m_old)) {
            // The new pose blended from the old one, the old weighing as a type-9 fade's (FaceBlendOut's sample).
            pose = anim::blendPoses(*pose, *old, anim::AnimTaskStack::outgoingWeight(m_fadeElapsed, m_fadeSeconds));
        }
    }
    // Only the head's and eight face bones' rows reach the default pose: the sample's rotations of bones 6-14 are
    // copied over the reference (Instance_CopyRootSlots), so the upper lip (15) and the rest stay the reference's
    // whatever the face clip holds. **Coney's choice**: the original copies into one buffer all humans share, so a
    // bone of 6-14 a face clip leaves out keeps the last human's value; Coney gives it the reference.
    std::copy(pose->rotations.begin() + kFirstFaceDefaultBone, pose->rotations.begin() + kLastFaceDefaultBone + 1,
              out.begin() + kFirstFaceDefaultBone);
    return out;
}

} // namespace coney::human
