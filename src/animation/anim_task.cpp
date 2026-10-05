// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation/anim_task.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <utility>
#include <vector>

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

// A clip whose time is this close to its length has ended: a clip of a whole number of updates at its rate ends on
// the update its time reaches its length, as XX2's did at runtime (30 updates for 24 frames at 0.8), not one later
// when the floats' sum falls a hair short.
constexpr float kClipEndSlack = 1e-4F;

// Rounding slack for eventFrame(): a time that sums to a whole and a half frame in floats counts as the tie it is.
constexpr float kEventFrameSlack = 1e-3F;

} // namespace

void applyHeldFlagEvent(std::uint16_t type, AnimTask& task, std::uint32_t& flags) {
    const std::uint32_t held = task.heldFlags();
    switch (type) {
    case kEventChainWindow:
        // The chain window opens: the wind-up ends.
        if ((held & kFlagChainWindow) != 0) {
            flags = (flags & ~kFlagWindUp) | kFlagChainWindow;
        }
        break;
    case kEventAttackEnd:
        // The attack's end phase: its wind-up and window are over.
        if ((held & kFlagAttackEnd) != 0) {
            flags = (flags & ~kFlagAttackPhases) | kFlagAttackEnd;
        }
        break;
    case kEventRecovery:
        // The recovery replaces whatever the task holds, on the record and in the task.
        if (held != 0) {
            flags = (flags & ~held) | kFlagRecovery;
            task.replaceHeldFlags(kFlagRecovery);
        }
        break;
    default:
        break;
    }
}

int eventFrame(float seconds) {
    return static_cast<int>(std::ceil((seconds * kClipFrameRate) - 0.5F - kEventFrameSlack));
}

float AnimTask::normalisedTime() const {
    const float total = duration();
    return total > 0.0F ? std::clamp(time() / total, 0.0F, 1.0F) : 0.0F;
}

Pose AnimTask::applyFlags(Pose pose, std::span<const Quat, kPoseBones> defaultRotations) const {
    // The sampler puts section A times the task's rate in the pose, unless the task asks for no root velocity.
    if ((m_flags & kTaskNoRootVelocity) != 0 || !pose.hasRootVelocity) {
        pose.rootVelocity = Vec3{};
        pose.hasRootVelocity = false;
    } else {
        pose.rootVelocity = scale(pose.rootVelocity, m_rate);
    }
    if ((m_flags & kTaskNoRootTurn) != 0) {
        pose.rotations[0] = defaultRotations[0];
        pose.defaulted[0] = true;
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

Pose LoopTask::sample(std::span<const Quat, kPoseBones> defaultRotations) const {
    return applyFlags(samplePose(m_cursor.clip(), m_cursor.time(), defaultRotations), defaultRotations);
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
    const float overshoot = m_cursor.advance(seconds, rate());
    if (overshoot > 0.0F || m_cursor.clip().duration - m_cursor.time() <= kClipEndSlack) {
        // The clip has ended (its time has reached its length, give or take the floats' rounding): the next task
        // starts with the time the clip did not use (overshoot is in the clip's time, so it is turned back into game
        // time first).
        m_finished = true;
        if (m_next) {
            m_next->advance(rate() > 0.0F ? overshoot / rate() : 0.0F);
        }
    } else if (m_handOverEarly && m_cursor.clip().duration - m_cursor.time() < seconds * rate()) {
        // Less than one more advance of the clip left: the next task takes over now, from its start.
        m_finished = true;
    }
}

Pose ClipThenNextTask::sample(std::span<const Quat, kPoseBones> defaultRotations) const {
    return applyFlags(samplePose(m_cursor.clip(), m_cursor.time(), defaultRotations), defaultRotations);
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

Pose GaitBlendTask::sample(std::span<const Quat, kPoseBones> defaultRotations) const {
    const std::size_t lower = lowerIndex();
    const float fraction = m_value - static_cast<float>(lower);
    const AnimClip& low = *m_clips[lower].clip;
    const AnimClip& high = *m_clips[lower + 1].clip;
    if (fraction < kLowerAlone) {
        return applyFlags(samplePose(low, timeAt(low, m_phase), defaultRotations), defaultRotations);
    }
    if (fraction > kUpperAlone) {
        return applyFlags(samplePose(high, timeAt(high, m_phase), defaultRotations), defaultRotations);
    }
    const Pose a = samplePose(low, timeAt(low, m_phase), defaultRotations);
    const Pose b = samplePose(high, timeAt(high, m_phase), defaultRotations);
    return applyFlags(blendPoses(a, b, fraction), defaultRotations);
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

void AnimTaskStack::startTask(const AnimTask& task) { m_flags = (m_flags & ~task.heldFlags()) | task.startFlags(); }

void AnimTaskStack::releaseTask(const AnimTask& task, std::size_t except) {
    std::uint32_t kept = 0;
    for (std::size_t i = 0; i < m_layers.size(); ++i) {
        if (i != except && m_layers[i].task != nullptr && m_layers[i].task.get() != &task) {
            kept |= m_layers[i].task->heldFlags();
        }
    }
    m_flags &= ~(task.heldFlags() & ~kept);
}

void AnimTaskStack::fireEvents(AnimTask& task, const AnimClip& clip, float before, float after, bool wrapped) {
    const int from = eventFrame(before);
    const int to = eventFrame(after);
    for (const ClipEvent& event : clip.events) {
        const int frame = event.frame;
        const bool passed = wrapped ? (frame > from || frame <= to) : (frame > from && frame <= to);
        if (passed) {
            applyHeldFlagEvent(event.type, task, m_flags);
        }
    }
}

void AnimTaskStack::dropOlderThan(std::size_t index) {
    if (index + 1 < m_layers.size()) {
        // The dropped tasks are cut off: each gives back what no task left in the stack holds.
        std::vector<Layer> dropped;
        dropped.reserve(m_layers.size() - index - 1);
        for (std::size_t i = index + 1; i < m_layers.size(); ++i) {
            dropped.push_back(std::move(m_layers[i]));
        }
        m_layers.erase(m_layers.begin() + static_cast<std::ptrdiff_t>(index) + 1, m_layers.end());
        for (const Layer& layer : dropped) {
            releaseTask(*layer.task, m_layers.size());
        }
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
    startTask(*task);
    m_layers.insert(m_layers.begin(), Layer{.task = std::move(task), .fade = fadeSeconds, .elapsed = 0.0F});
    // A fade of no length is over at once: the older tasks go now.
    if (fadeSeconds <= 0.0F) {
        dropOlderThan(0);
    }
}

void AnimTaskStack::advance(float seconds) {
    // Every task advances, the outgoing ones too; the newest task's clip fires its events as their frames pass; a
    // finished clip-then-next task gives back its bits and gives way to its next task, which starts.
    for (std::size_t i = 0; i < m_layers.size(); ++i) {
        Layer& layer = m_layers[i];
        const AnimClip* clip = i == 0 ? layer.task->eventClip() : nullptr;
        const float before = layer.task->time();
        layer.task->advance(seconds);
        if (clip != nullptr) {
            const float after = layer.task->eventClip() == clip ? layer.task->time() : clip->duration;
            fireEvents(*layer.task, *clip, before, after, after < before);
        }
        if (std::unique_ptr<AnimTask> next = layer.task->takeReplacement(); next) {
            std::unique_ptr<AnimTask> ended = std::move(layer.task);
            layer.task = std::move(next);
            releaseTask(*ended, i);
            startTask(*layer.task);
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

Pose AnimTaskStack::sample(std::span<const Quat, kPoseBones> defaultRotations) const {
    if (m_layers.empty()) {
        Pose rest;
        std::ranges::copy(defaultRotations, rest.rotations.begin());
        rest.defaulted.fill(true);
        return rest;
    }
    // From the oldest up: each newer task over what is under it, the old weighing (1 + cos(π t / d)) / 2.
    Pose pose = m_layers.back().task->sample(defaultRotations);
    for (std::size_t i = m_layers.size() - 1; i-- > 0;) {
        const Layer& layer = m_layers[i];
        const Pose incoming = layer.task->sample(defaultRotations);
        pose = blendPoses(incoming, pose, outgoingWeight(layer.elapsed, layer.fade));
    }
    return pose;
}

RootMotion rootMotionOf(const Pose& pose) {
    RootMotion motion;
    if (pose.hasRootVelocity) {
        motion.velocity = pose.rootVelocity;
    }
    // Bone 0's rotation is the root's turn per frame: angle 2 acos(w) about z, its sign from z; none when defaulted.
    if (pose.defaulted[0]) {
        return motion;
    }
    const Quat q = pose.rotations[0];
    const float w = std::clamp(q.w, -1.0F, 1.0F);
    const float angle = 2.0F * std::acos(w);
    motion.turn = q.z < 0.0F ? -angle : angle;
    return motion;
}

} // namespace coney::anim
