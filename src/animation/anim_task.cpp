// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation/anim_task.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "core/assert.h"

namespace coney::anim {

namespace {

// The steps of floor(value) the gait blend uses, so that a value a hair under a whole number already counts as it.
constexpr std::array<float, 3> kPairSteps{0.995F, 1.995F, 2.995F};
// Below this fraction the lower clip plays alone, above kUpperAlone the upper one.
constexpr float kLowerAlone = 0.01F;
constexpr float kUpperAlone = 0.995F;
// Above this fraction the upper clip leads.
constexpr float kUpperLeadsFraction = 0.875F;
// How far above its target the value must be to count as falling.
constexpr float kFallingTolerance = 1e-3F;

// Seconds `normalised` of the way through `clip`.
float timeAt(const AnimClip& clip, float normalised) { return normalised * clip.duration; }

} // namespace

float AnimTask::normalisedTime() const {
    const float total = duration();
    return total > 0.0F ? std::clamp(time() / total, 0.0F, 1.0F) : 0.0F;
}

Pose AnimTask::applyFlags(Pose pose, std::span<const Quat, kPoseBones> bindRotations) const {
    // The sampler puts section A times the task's rate in the pose, unless the task asks for no root velocity.
    if ((m_flags & kTaskNoRootVelocity) != 0 || !pose.hasRootVelocity) {
        pose.rootVelocity = Vec3{};
        pose.hasRootVelocity = false;
    } else {
        pose.rootVelocity = scale(pose.rootVelocity, m_rate);
    }
    if ((m_flags & kTaskNoRootTurn) != 0) {
        pose.rotations[0] = bindRotations[0];
    }
    return pose;
}

LoopTask::LoopTask(const AnimClip& clip, std::uint32_t animId, float rate, std::uint32_t flags, float startSeconds)
    : AnimTask(rate, flags), m_cursor(clip), m_animId(animId) {
    m_cursor.restart(startSeconds);
}

void LoopTask::advance(float seconds) {
    if (const float overshoot = m_cursor.advance(seconds, rate()); overshoot > 0.0F) {
        // Past the end: wrap, keeping the overshoot.
        const float length = m_cursor.clip().duration;
        m_cursor.restart(length > 0.0F ? std::fmod(overshoot, length) : 0.0F);
    }
}

Pose LoopTask::sample(std::span<const Quat, kPoseBones> bindRotations) const {
    return applyFlags(samplePose(m_cursor.clip(), m_cursor.time(), bindRotations), bindRotations);
}

ClipThenNextTask::ClipThenNextTask(const AnimClip& clip, std::uint32_t animId, float rate, std::uint32_t flags,
                                   std::unique_ptr<AnimTask> next, float startSeconds, bool handOverEarly)
    : AnimTask(rate, flags), m_cursor(clip), m_animId(animId), m_next(std::move(next)), m_handOverEarly(handOverEarly) {
    CONEY_ASSERT(m_next != nullptr);
    m_cursor.restart(startSeconds);
}

void ClipThenNextTask::advance(float seconds) {
    if (m_finished) {
        return;
    }
    if (const float overshoot = m_cursor.advance(seconds, rate()); overshoot > 0.0F) {
        // The clip has ended: the next task starts with the time the clip did not use (overshoot is in the clip's
        // time, so it is turned back into game time first).
        m_finished = true;
        if (m_next) {
            m_next->advance(rate() > 0.0F ? overshoot / rate() : 0.0F);
        }
    } else if (m_handOverEarly && m_cursor.clip().duration - m_cursor.time() < seconds * rate()) {
        // Less than one more advance of the clip left: the next task takes over now, from its start.
        m_finished = true;
    }
}

Pose ClipThenNextTask::sample(std::span<const Quat, kPoseBones> bindRotations) const {
    return applyFlags(samplePose(m_cursor.clip(), m_cursor.time(), bindRotations), bindRotations);
}

std::unique_ptr<AnimTask> ClipThenNextTask::takeReplacement() { return m_finished ? std::move(m_next) : nullptr; }

GaitBlendTask::GaitBlendTask(const std::array<GaitClip, 5>& clips, float value, float valueSpeed, float rate,
                             std::uint32_t flags, float phase)
    : AnimTask(rate, flags), m_clips(clips), m_value(std::clamp(value, 0.0F, kMaxValue)), m_target(m_value),
      m_valueSpeed(valueSpeed), m_phase(std::clamp(phase, 0.0F, 1.0F)) {
    for (const GaitClip& clip : m_clips) {
        CONEY_ASSERT(clip.clip != nullptr);
    }
}

std::size_t GaitBlendTask::lowerIndex() const {
    std::size_t index = 0;
    while (index < kPairSteps.size() && m_value >= kPairSteps[index]) {
        ++index;
    }
    return index;
}

bool GaitBlendTask::upperLeads() const {
    const float fraction = m_value - static_cast<float>(lowerIndex());
    // "Falling" needs the value to be above the target by more than rounding: a speed a hair over the walk speed
    // otherwise flips the leader each update (a Coney guard; the original's floats may never meet this case).
    return m_value > m_target + kFallingTolerance || fraction > kUpperLeadsFraction;
}

void GaitBlendTask::advance(float seconds) {
    // 1. The value toward the target, by at most rate × dt × speed.
    const float step = rate() * seconds * m_valueSpeed;
    m_value = m_value < m_target ? std::min(m_target, m_value + step) : std::max(m_target, m_value - step);
    // 2-3. The leading clip advances by rate × dt; the other follows at the same normalised time. Coney keeps the one
    // shared normalised time, which is what "set to the same normalised time" leaves, and which a clip coming into the
    // pair also starts at.
    const std::size_t lower = lowerIndex();
    const AnimClip& leader = *m_clips[upperLeads() ? lower + 1 : lower].clip;
    if (leader.duration > 0.0F) {
        m_phase += rate() * seconds / leader.duration;
        m_phase -= std::floor(m_phase); // the leading clip wraps
    }
}

Pose GaitBlendTask::sample(std::span<const Quat, kPoseBones> bindRotations) const {
    const std::size_t lower = lowerIndex();
    const float fraction = m_value - static_cast<float>(lower);
    const AnimClip& low = *m_clips[lower].clip;
    const AnimClip& high = *m_clips[lower + 1].clip;
    if (fraction < kLowerAlone) {
        return applyFlags(samplePose(low, timeAt(low, m_phase), bindRotations), bindRotations);
    }
    if (fraction > kUpperAlone) {
        return applyFlags(samplePose(high, timeAt(high, m_phase), bindRotations), bindRotations);
    }
    const Pose a = samplePose(low, timeAt(low, m_phase), bindRotations);
    const Pose b = samplePose(high, timeAt(high, m_phase), bindRotations);
    return applyFlags(blendPoses(a, b, fraction), bindRotations);
}

float GaitBlendTask::time() const { return m_phase * duration(); }

float GaitBlendTask::duration() const {
    const std::size_t lower = lowerIndex();
    return m_clips[upperLeads() ? lower + 1 : lower].clip->duration;
}

std::uint32_t GaitBlendTask::animId() const {
    const std::size_t lower = lowerIndex();
    return m_clips[upperLeads() ? lower + 1 : lower].animId;
}

void GaitBlendTask::setTarget(float target) {
    const float clamped = std::clamp(target, 0.0F, kMaxValue);
    m_target = (flags() & kTaskContinuous) != 0 ? clamped : std::round(clamped);
}

void GaitBlendTask::setValue(float value) {
    m_value = std::clamp(value, 0.0F, kMaxValue);
    m_target = m_value;
}

float AnimTaskStack::outgoingWeight(float elapsed, float duration) {
    if (duration <= 0.0F || elapsed >= duration) {
        return 0.0F;
    }
    return (1.0F + std::cos(std::numbers::pi_v<float> * std::max(elapsed, 0.0F) / duration)) * 0.5F;
}

void AnimTaskStack::dropOlderThan(std::size_t index) {
    if (index + 1 < m_layers.size()) {
        m_layers.erase(m_layers.begin() + static_cast<std::ptrdiff_t>(index) + 1, m_layers.end());
    }
    m_layers[index].fade = 0.0F;
    m_layers[index].elapsed = 0.0F;
}

void AnimTaskStack::change(std::unique_ptr<AnimTask> task, float fadeSeconds) {
    CONEY_ASSERT(task != nullptr);
    // With more than six tasks held, the newest fade is finished early first.
    if (taskCount() > kEarlyFinishTasks) {
        dropOlderThan(0);
    }
    m_layers.insert(m_layers.begin(), Layer{.task = std::move(task), .fade = fadeSeconds, .elapsed = 0.0F});
    // A fade of no length is over at once: the older tasks go now.
    if (fadeSeconds <= 0.0F) {
        dropOlderThan(0);
    }
}

void AnimTaskStack::advance(float seconds) {
    // Every task advances, the outgoing ones too; a finished clip-then-next task gives way to its next task.
    for (Layer& layer : m_layers) {
        layer.task->advance(seconds);
        if (std::unique_ptr<AnimTask> next = layer.task->takeReplacement(); next) {
            layer.task = std::move(next);
        }
        layer.elapsed += seconds;
    }
    // The newest fade that has run its time (or every fade, with more than twelve tasks) removes what it faded out.
    for (std::size_t i = 0; i + 1 < m_layers.size(); ++i) {
        if (m_layers[i].elapsed >= m_layers[i].fade || taskCount() > kMaxTasks) {
            dropOlderThan(i);
            break;
        }
    }
}

Pose AnimTaskStack::sample(std::span<const Quat, kPoseBones> bindRotations) const {
    if (m_layers.empty()) {
        Pose rest;
        std::ranges::copy(bindRotations, rest.rotations.begin());
        return rest;
    }
    // From the oldest up: each newer task over what is under it, the old weighing (1 + cos(π t / d)) / 2.
    Pose pose = m_layers.back().task->sample(bindRotations);
    for (std::size_t i = m_layers.size() - 1; i-- > 0;) {
        const Layer& layer = m_layers[i];
        const Pose incoming = layer.task->sample(bindRotations);
        pose = blendPoses(incoming, pose, outgoingWeight(layer.elapsed, layer.fade));
    }
    return pose;
}

RootMotion rootMotionOf(const Pose& pose) {
    RootMotion motion;
    if (pose.hasRootVelocity) {
        motion.velocity = pose.rootVelocity;
    }
    // Bone 0's rotation is the root's turn per frame: angle 2 acos(w) about z, its sign from z.
    const Quat q = pose.rotations[0];
    const float w = std::clamp(q.w, -1.0F, 1.0F);
    const float angle = 2.0F * std::acos(w);
    motion.turn = q.z < 0.0F ? -angle : angle;
    return motion;
}

} // namespace coney::anim
