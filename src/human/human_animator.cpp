// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/human_animator.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include "core/assert.h"

namespace coney::human {

namespace {

// The run start is the walk start's id plus one (414 after 413).
constexpr std::uint32_t kRunStartOffset = 1;
// The gait blend's value when a move starts: the walk, or the run for a run start.
constexpr float kWalkValue = 0.0F;
constexpr float kRunValue = 2.0F;
// The share of the walk start that may have played for a run to replace it.
constexpr float kRunSwapLimit = 0.5F;
// Below this share of the walk speed a human that is not asked to move counts as standing (Coney's reading of "a
// quarter of the speed 0x00221580 returns", which getter that is being open).
constexpr float kIdleSpeedShare = 0.25F;

} // namespace

AnimSlots AnimSlots::player() {
    AnimSlots slots;
    slots.ids[kSlotIdle] = 388;
    slots.ids[kSlotSneak] = 407;
    slots.ids[kSlotWalk] = 408;
    slots.ids[kSlotJog] = 409;
    slots.ids[kSlotRun] = 410;
    slots.ids[kSlotSprint] = 411;
    slots.ids[kSlotWalkStart] = 413;
    slots.ids[kSlotCombatWalk] = 380;
    slots.ids[kSlotDropCycle] = 428;
    return slots;
}

Speeds speedsOf(const characters::AnimSet& anims, const AnimSlots& slots) {
    return Speeds{.base = anims.speed(slots.ids[kSlotCombatWalk]),
                  .sneak = anims.speed(slots.ids[kSlotSneak]),
                  .walk = anims.speed(slots.ids[kSlotWalk]),
                  .jog = anims.speed(slots.ids[kSlotJog]),
                  .run = anims.speed(slots.ids[kSlotRun]),
                  .sprint = anims.speed(slots.ids[kSlotSprint])};
}

std::size_t HumanAnimator::clipsMissing(const characters::AnimSet& anims, const AnimSlots& slots) {
    std::size_t missing = 0;
    for (const std::size_t slot :
         {kSlotIdle, kSlotWalk, kSlotJog, kSlotRun, kSlotSprint, kSlotWalkStart, kSlotDropCycle}) {
        missing += anims.clip(slots.ids[slot]) == nullptr ? 1 : 0;
    }
    missing += anims.clip(slots.ids[kSlotWalkStart] + kRunStartOffset) == nullptr ? 1 : 0;
    return missing;
}

HumanAnimator::HumanAnimator(const characters::AnimSet& anims, const AnimSlots& slots)
    : m_anims(&anims), m_slots(slots), m_speeds(speedsOf(anims, slots)) {
    CONEY_ASSERT(clipsMissing(anims, slots) == 0);
    // A human is made standing: its idle, with nothing to fade from.
    const anim::GaitClip idle = slotClip(kSlotIdle);
    m_tasks.change(std::make_unique<anim::LoopTask>(*idle.clip, idle.animId, m_anims->rate(idle.animId), 0U), 0.0F);
    m_state = AnimState::Idle;
}

anim::GaitClip HumanAnimator::slotClip(std::size_t slot) const {
    const std::uint32_t id = m_slots.ids[slot];
    return anim::GaitClip{.clip = m_anims->clip(id), .animId = id};
}

std::unique_ptr<anim::GaitBlendTask> HumanAnimator::gaitBlend(float value, float phase) const {
    const std::array<anim::GaitClip, 5> clips{slotClip(kSlotWalk), slotClip(kSlotJog), slotClip(kSlotRun),
                                              slotClip(kSlotSprint), slotClip(kSlotSprint)};
    // The locomotion blends play at the human's speed multiplier (+0x3a4), 1.0 in play.
    return std::make_unique<anim::GaitBlendTask>(clips, value, kGaitValueSpeed, 1.0F, anim::kGaitBlendFlags, phase);
}

bool HumanAnimator::startClipPlaying() const {
    const anim::AnimTask* top = m_tasks.top();
    return top != nullptr && top->type() == anim::AnimTaskType::ClipThenNext;
}

std::uint32_t HumanAnimator::animId() const {
    const anim::AnimTask* top = m_tasks.top();
    return top != nullptr ? top->animId() : 0;
}

float HumanAnimator::gaitValue() const {
    const auto* blend = dynamic_cast<const anim::GaitBlendTask*>(m_tasks.top());
    return blend != nullptr ? blend->value() : -1.0F;
}

void HumanAnimator::advance(float seconds) { m_tasks.advance(seconds); }

void HumanAnimator::buildIdle() {
    // Over a start clip the fade depends on how much of it has played.
    float fade = kIdleFade;
    if (startClipPlaying()) {
        const anim::AnimTask& start = *m_tasks.top();
        fade = start.time() / start.rate() < kStartClipEarly ? kIdleFadeEarlyStart : kIdleFadeLateStart;
    }
    const anim::GaitClip idle = slotClip(kSlotIdle);
    m_tasks.change(std::make_unique<anim::LoopTask>(*idle.clip, idle.animId, m_anims->rate(idle.animId), 0U), fade);
}

void HumanAnimator::buildMove(bool run) {
    const anim::AnimTask* top = m_tasks.top();
    // Already moving (the newest task is a gait blend): a new blend that carries on the old one's value, rounded down,
    // and its cycle.
    if (top != nullptr && (top->flags() & anim::kTaskGaitBlendMark) != 0) {
        const auto& old = dynamic_cast<const anim::GaitBlendTask&>(*top);
        m_tasks.change(gaitBlend(std::floor(std::min(old.value(), 3.0F)), old.phase()), kMoveFadeMoving);
        return;
    }
    // From standing: the walk start (or the run start) at once, handing over to a gait blend at the walk (or run).
    const std::uint32_t startId = m_slots.ids[kSlotWalkStart] + (run ? kRunStartOffset : 0U);
    const anim::AnimClip* start = m_anims->clip(startId);
    m_tasks.change(std::make_unique<anim::ClipThenNextTask>(*start, startId, m_anims->rate(startId), 0U,
                                                            gaitBlend(run ? kRunValue : kWalkValue, 0.0F)),
                   0.0F);
}

void HumanAnimator::buildFall() {
    const anim::GaitClip drop = slotClip(kSlotDropCycle);
    m_tasks.change(std::make_unique<anim::LoopTask>(*drop.clip, drop.animId, m_anims->rate(drop.animId), 0U),
                   kIdleFade);
}

void HumanAnimator::choose(const AnimInputs& inputs) {
    // The state: falling, moving (asked to, or still going faster than a quarter of the walk), or idle.
    AnimState next = AnimState::Idle;
    if (inputs.airborne) {
        next = AnimState::Fall;
    } else if (inputs.wantsMove || inputs.speed >= kIdleSpeedShare * m_speeds.walk) {
        next = AnimState::Move;
    }
    // A change of state calls its builder.
    if (next != m_state) {
        switch (next) {
        case AnimState::Idle:
            buildIdle();
            break;
        case AnimState::Move:
            buildMove(inputs.wantsRun);
            break;
        case AnimState::Fall:
            buildFall();
            break;
        case AnimState::None:
            break;
        }
        m_state = next;
        return;
    }
    if (m_state != AnimState::Move) {
        return;
    }
    // A walk start that has played less than half its length becomes a run start when a run is asked for.
    const anim::AnimTask* top = m_tasks.top();
    if (top != nullptr && top->type() == anim::AnimTaskType::ClipThenNext &&
        top->animId() == m_slots.ids[kSlotWalkStart] && inputs.wantsRun && top->normalisedTime() < kRunSwapLimit) {
        const std::uint32_t runId = m_slots.ids[kSlotWalkStart] + kRunStartOffset;
        const anim::AnimClip* runStart = m_anims->clip(runId);
        const float startAt = top->normalisedTime() * runStart->duration;
        const float fade = std::min(kMoveFadeMoving, top->duration());
        m_tasks.change(std::make_unique<anim::ClipThenNextTask>(*runStart, runId, m_anims->rate(runId), 0U,
                                                                gaitBlend(kRunValue, 0.0F), startAt),
                       fade);
        return;
    }
    // While moving, the gait blend's target follows the speed every update.
    if (auto* blend = dynamic_cast<anim::GaitBlendTask*>(m_tasks.top()); blend != nullptr) {
        blend->setTarget(gaitBlendForSpeed(inputs.speed, m_speeds));
    }
}

} // namespace coney::human
