// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/human_animator.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#include "core/assert.h"
#include "human/locomotion_gate.h"

namespace coney::human {

namespace {

// The run start is the walk start's id plus one (414 after 413).
constexpr std::uint32_t kRunStartOffset = 1;
// The gait blend's value when a move starts: the walk, or the run for a run start.
constexpr float kWalkValue = 0.0F;
constexpr float kRunValue = 2.0F;
// The gait blend a running landing hands over to: the jog (the runtime landing went "from a jog back to the run";
// **Coney's choice** of the exact value).
constexpr float kJogValue = 1.0F;
// The share of the walk start that may have played for a run to replace it.
constexpr float kRunSwapLimit = 0.5F;
// Below this share of the walk speed a human that is not asked to move counts as standing (Coney's reading of "a
// quarter of the speed 0x00221580 returns", which getter that is being open).
constexpr float kIdleSpeedShare = 0.25F;

// The bits the locomotion's clips hold on the record +0x08 (docs/research/characters.md#the-record): a move's start
// clip, a jump's landing, the run stop and the climbs, and 389 after a move.
constexpr HeldFlags kStartClipHeld{.held = kFlagStartClip, .set = kFlagStartClip};
constexpr HeldFlags kLandingHeld{.held = kFlagLanding, .set = kFlagLanding};
constexpr HeldFlags kRunStopHeld{.held = kFlagRunStop, .set = kFlagRunStop};
constexpr HeldFlags kNormalFromFightHeld{.held = kFlagNormalFromFight, .set = kFlagNormalFromFight};

// Whether `state` is an action whose clips play out before the controller chooses again.
bool isAction(AnimState state) {
    return state == AnimState::Land || state == AnimState::RunStop || state == AnimState::Climb ||
           state == AnimState::Attack;
}

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
    slots.ids[kSlotJumpLoop] = 434;
    slots.ids[kSlotRunStop] = 417;
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
    for (const std::size_t slot : {kSlotIdle, kSlotWalk, kSlotJog, kSlotRun, kSlotSprint, kSlotWalkStart,
                                   kSlotDropCycle, kSlotJumpLoop, kSlotRunStop}) {
        missing += anims.clip(slots.ids[slot]) == nullptr ? 1 : 0;
    }
    missing += anims.clip(slots.ids[kSlotWalkStart] + kRunStartOffset) == nullptr ? 1 : 0;
    for (const std::uint32_t id : {kAnimJumpEnd, kAnimJumpEndRunning}) {
        missing += anims.clip(id) == nullptr ? 1 : 0;
    }
    for (std::uint32_t id = kAnimFirstClimb; id <= kAnimLastClimb; ++id) {
        missing += anims.clip(id) == nullptr ? 1 : 0;
    }
    return missing;
}

HumanAnimator::HumanAnimator(const characters::AnimSet& anims, const AnimSlots& slots)
    : m_anims(&anims), m_slots(slots), m_speeds(speedsOf(anims, slots)) {
    CONEY_ASSERT(clipsMissing(anims, slots) == 0);
    // A human is made standing: its idle, with nothing to fade from.
    m_tasks.change(idleLoop(), 0.0F);
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

std::unique_ptr<anim::AnimTask> HumanAnimator::idleLoop() const {
    const anim::GaitClip idle = slotClip(kSlotIdle);
    return std::make_unique<anim::LoopTask>(*idle.clip, idle.animId, m_anims->rate(idle.animId), 0U);
}

std::unique_ptr<anim::AnimTask> HumanAnimator::clipThen(std::uint32_t id, std::unique_ptr<anim::AnimTask> next,
                                                        const characters::AnimSet* from, HeldFlags held) const {
    // Single clips have no task flags, so their root motion moves the body. A paired clip is the attacker's, at the
    // attacker's rate (the original's type 6 task); it plays on this human like any other.
    const characters::AnimSet& set = from != nullptr ? *from : *m_anims;
    auto task = std::make_unique<anim::ClipThenNextTask>(*set.clip(id), id, set.rate(id), 0U, std::move(next));
    task->holdFlags(held.held, held.set);
    return task;
}

bool HumanAnimator::drivingClipPlaying() const {
    const anim::AnimTask* top = m_tasks.top();
    return top != nullptr && top->type() == anim::AnimTaskType::ClipThenNext;
}

bool HumanAnimator::gaitBlendPlaying() const {
    const anim::AnimTask* top = m_tasks.top();
    return top != nullptr && top->type() == anim::AnimTaskType::GaitBlend;
}

bool HumanAnimator::startClipPlaying() const { return m_state == AnimState::Move && drivingClipPlaying(); }

bool HumanAnimator::actionPlaying() const { return isAction(m_state) && drivingClipPlaying(); }

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
    // The fade holds 0x10000000 while it runs, which gates the stick's velocity: after a walk stop or a block the
    // stick turns the human on the spot for those updates and the move starts only once the fade is over
    // (docs/research/tasks.md#locomotion-gate).
    m_tasks.change(idleLoop(), fade, kFlagStartClip);
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
    // From standing: the walk start (or the run start) at once, handing over to a gait blend at the walk (or run) when
    // less than an update of it is left (13 updates at runtime).
    const std::uint32_t startId = m_slots.ids[kSlotWalkStart] + (run ? kRunStartOffset : 0U);
    auto start = std::make_unique<anim::ClipThenNextTask>(*m_anims->clip(startId), startId, m_anims->rate(startId), 0U,
                                                          gaitBlend(run ? kRunValue : kWalkValue, 0.0F), 0.0F, true);
    start->holdFlags(kStartClipHeld.held, kStartClipHeld.set);
    m_tasks.change(std::move(start), 0.0F);
}

void HumanAnimator::buildFall() {
    const anim::GaitClip drop = slotClip(kSlotDropCycle);
    m_tasks.change(std::make_unique<anim::LoopTask>(*drop.clip, drop.animId, m_anims->rate(drop.animId), 0U),
                   kIdleFade);
}

void HumanAnimator::startJump() {
    const anim::GaitClip loop = slotClip(kSlotJumpLoop);
    m_tasks.change(std::make_unique<anim::LoopTask>(*loop.clip, loop.animId, m_anims->rate(loop.animId), 0U),
                   kJumpFade);
    m_state = AnimState::Jump;
}

void HumanAnimator::startLanding(bool movingOn) {
    if (movingOn) {
        m_tasks.change(clipThen(kAnimJumpEndRunning, gaitBlend(kJogValue, 0.0F), nullptr, kLandingHeld), kLandFade);
    } else {
        m_tasks.change(clipThen(kAnimJumpEnd, idleLoop(), nullptr, kLandingHeld), kLandFade);
    }
    m_state = AnimState::Land;
}

void HumanAnimator::startRunStop() {
    m_tasks.change(clipThen(m_slots.ids[kSlotRunStop], idleLoop(), nullptr, kRunStopHeld), kMoveFadeMoving);
    m_state = AnimState::RunStop;
}

void HumanAnimator::startClimb(std::uint32_t firstId, bool running) {
    // The three clips in turn, then the run carries on (from a run) or the idle.
    std::unique_ptr<anim::AnimTask> after =
        running ? std::unique_ptr<anim::AnimTask>(gaitBlend(kRunValue, 0.0F)) : idleLoop();
    std::unique_ptr<anim::AnimTask> chain = clipThen(firstId + 2, std::move(after), nullptr, kRunStopHeld);
    chain = clipThen(firstId + 1, std::move(chain), nullptr, kRunStopHeld);
    chain = clipThen(firstId, std::move(chain), nullptr, kRunStopHeld);
    // No fade-in: the first clip moves the body at its full root speed from its first update, as at runtime
    // (docs/research/characters.md#climb).
    m_tasks.change(std::move(chain), 0.0F);
    m_state = AnimState::Climb;
}

void HumanAnimator::settleToIdle() {
    buildIdle();
    m_state = AnimState::Idle;
}

bool HumanAnimator::idleFading() const { return m_state == AnimState::Idle && (flags() & kFlagStartClip) != 0; }

void HumanAnimator::stopToIdle() {
    m_tasks.change(idleLoop(), kIdleFade);
    m_state = AnimState::Idle;
}

void HumanAnimator::playCombat(std::span<const std::uint32_t> clips, std::uint32_t loop, AnimState state, float fade,
                               HeldFlags held) {
    playChain(clips, nullptr, loop, state, fade, held);
}

void HumanAnimator::playPaired(std::span<const std::uint32_t> clips, const characters::AnimSet& attacker,
                               std::uint32_t loop, AnimState state, float fade) {
    playChain(clips, &attacker, loop, state, fade, HeldFlags{});
}

void HumanAnimator::playChain(std::span<const std::uint32_t> clips, const characters::AnimSet* from, std::uint32_t loop,
                              AnimState state, float fade, HeldFlags held) {
    // The loop last (this human's own), then each clip handing over to what follows it, built from the end.
    const characters::AnimSet& set = from != nullptr ? *from : *m_anims;
    std::unique_ptr<anim::AnimTask> chain;
    if (const anim::AnimClip* clip = m_anims->clip(loop); clip != nullptr) {
        chain = std::make_unique<anim::LoopTask>(*clip, loop, m_anims->rate(loop), 0U);
    } else {
        chain = idleLoop();
    }
    for (auto it = clips.rbegin(); it != clips.rend(); ++it) {
        if (set.clip(*it) != nullptr) {
            chain = clipThen(*it, std::move(chain), from, *it == kAnimNormalFromFight ? kNormalFromFightHeld : held);
        }
    }
    m_tasks.change(std::move(chain), fade);
    m_state = state;
}

void HumanAnimator::playCombatThenRun(std::span<const std::uint32_t> clips, float fade, HeldFlags held) {
    std::unique_ptr<anim::AnimTask> chain = gaitBlend(kRunValue, 0.0F);
    for (auto it = clips.rbegin(); it != clips.rend(); ++it) {
        if (hasClip(*it)) {
            chain = clipThen(*it, std::move(chain), nullptr, held);
        }
    }
    m_tasks.change(std::move(chain), fade);
    m_state = AnimState::Attack;
}

void HumanAnimator::playCombatWalk(std::uint32_t clip) {
    std::uint32_t id = clip;
    if (m_anims->clip(id) == nullptr) {
        id = m_anims->clip(kAnimFightIdle) != nullptr ? kAnimFightIdle : m_slots.ids[kSlotIdle];
    }
    if (m_state == AnimState::CombatWalk && animId() == id) {
        return;
    }
    // The velocity is the human's, so the clip's own root velocity is not sampled.
    m_tasks.change(
        std::make_unique<anim::LoopTask>(*m_anims->clip(id), id, m_anims->rate(id), anim::kTaskNoRootVelocity),
        kCombatFade);
    m_state = AnimState::CombatWalk;
}

void HumanAnimator::leaveCombatWalk() {
    if (m_state == AnimState::CombatWalk) {
        buildIdle();
        m_state = AnimState::Idle;
    }
}

void HumanAnimator::endHold() {
    if (m_state == AnimState::Hold) {
        m_state = AnimState::Idle;
    }
}

bool HumanAnimator::settling() const {
    return m_state == AnimState::Attack && drivingClipPlaying() && animId() == kAnimNormalFromFight;
}

void HumanAnimator::choose(const AnimInputs& inputs) {
    // A held combat pose stays until combat plays something else.
    if (m_state == AnimState::Hold || m_state == AnimState::CombatWalk) {
        return;
    }
    // An action's clips play out (but a move's closing 389 gives way to a move asked for); then the state is whatever
    // they handed over to.
    if (isAction(m_state)) {
        if (drivingClipPlaying() && !(settling() && inputs.wantsMove)) {
            return;
        }
        const anim::AnimTask* top = m_tasks.top();
        m_state = top != nullptr && top->type() == anim::AnimTaskType::GaitBlend ? AnimState::Move : AnimState::Idle;
    }
    // The jump loop plays until the human lands (which starts the landing).
    if (m_state == AnimState::Jump && inputs.airborne) {
        return;
    }
    // The state: falling, moving (asked to, or still going faster than a quarter of the walk), or idle.
    AnimState next = AnimState::Idle;
    if (inputs.airborne) {
        next = AnimState::Fall;
    } else if (inputs.wantsMove || inputs.speed >= kIdleSpeedShare * m_speeds.walk) {
        next = AnimState::Move;
    }
    // While the idle's fade holds 0x10000000 the move waits: the walk start begins the update the fade ends (at
    // runtime 388 for 5 updates after a block or a walk stop, then 413). **Coney's reading** of how the original holds
    // it back (the locomotion's state code 5 while the velocity is gated is not modelled).
    if (next == AnimState::Move && idleFading()) {
        return;
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
        case AnimState::Jump:
        case AnimState::Land:
        case AnimState::RunStop:
        case AnimState::Climb:
        case AnimState::Attack:
        case AnimState::Hold:
        case AnimState::CombatWalk:
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
                                                                gaitBlend(kRunValue, 0.0F), startAt, true),
                       fade);
        return;
    }
    // While moving, the gait blend's target follows the speed every update.
    if (auto* blend = dynamic_cast<anim::GaitBlendTask*>(m_tasks.top()); blend != nullptr) {
        blend->setTarget(gaitBlendForSpeed(inputs.speed, m_speeds));
    }
}

} // namespace coney::human
