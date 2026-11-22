// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/victim.h"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "animation/anim_clip.h"
#include "combat/attacks.h"
#include "combat/being_hit.h"

namespace coney::human {

namespace {

// The ground clips (docs/references/anim-ids.md): lying on the back and getting up.
constexpr std::uint32_t kGroundedIdle = 196;
constexpr std::uint32_t kGroundedRise = 199;
// The clip event type that knocks a victim down (docs/research/combat-reactions.md#reactions).
constexpr std::uint16_t kKnockdownEvent = 7;
// The rise holds 0x8000 on the record +0x08 (`Human_PlayRise`), which its recovery event (0x48, frame 14 on the disc)
// replaces with 0x40000: from then on the rise's counter is refused (docs/research/combat-reactions.md#ground-escape).
constexpr HeldFlags kRiseHolds{.held = combat::kPhaseRise, .set = combat::kPhaseRise};

// The death chain's clips (docs/research/combat-reactions.md#knockout-timing): its loops (349, 200 after 205, 323
// cuffed), 205, the falls (308 for a dying clip with no knockdown event, 198 for one whose event's value is 0) and the
// dying clip of a woundable human knocked down.
constexpr std::uint32_t kDeathLoop = 349;
constexpr std::uint32_t kDeathLoopWoundable = 200;
constexpr std::uint32_t kDeathLoopCuffed = 323;
constexpr std::uint32_t kWoundedDown = 205;
constexpr std::uint32_t kFallUnmarked = 308;
constexpr std::uint32_t kFallMarked = 198;
constexpr std::uint32_t kDyingKnockedDown = 195;
// The blends out of the dying clip and out of the fall into the loop, seconds.
constexpr float kDyingBlend = 0.2F;
constexpr float kFallBlend = 0.1F;
// The held flag the chain keeps until its loop (Human_PlayGangDeathReaction passes 0x2000).
constexpr HeldFlags kDyingHolds{.held = 0x2000, .set = 0x2000};
// A state bit and the dying clip it picks.
struct StateClip {
    std::uint64_t bits;
    std::uint32_t clip;
};
// The special dying clips tested before the knocked-down woundable human's 195, in the original's order.
constexpr std::array<StateClip, 9> kHeldDying{{{human_state::kCuffed, 322},
                                               {human_state::kState8000000, 264},
                                               {human_state::kWounded, 204},
                                               {human_state::kGrabbingFront, 146},
                                               {human_state::kGrabbingRear, 132},
                                               {human_state::kGrabbedFront, 139},
                                               {human_state::kGrabbedRear, 127},
                                               {human_state::kTackled, 208},
                                               {human_state::kTackling, 211}}};
// The special dying clips tested after it.
constexpr std::array<StateClip, 2> kDownDying{{{human_state::kDown, 348}, {human_state::kOnFire, 319}}};

// The one clip `clip` as an array.
std::array<std::uint32_t, 1> one(std::uint32_t clip) { return {clip}; }

} // namespace

Victim::Victim(const combat::PowerClass& powerClass, std::uint32_t seed) : m_class(powerClass), m_random(seed) {}

void Victim::hit(const IncomingHit& hit) {
    if (!m_hasPending || hit.damage > m_pending.damage) {
        m_pending = hit;
        m_hasPending = true;
    }
}

IncomingHit Victim::takePending() {
    m_hasPending = false;
    return m_pending;
}

combat::ReactionInput Victim::reactionInput(const IncomingHit& hit, const VictimFrame& frame) {
    combat::ReactionInput input;
    input.attackAnim = hit.attackAnim;
    input.code = hit.code;
    input.side = combat::victimSide(frame.position, frame.heading, hit.attacker);
    input.attackerAbove = hit.attacker.z - frame.position.z;
    input.victimFlag400 = frame.flag400;
    input.victimFlag200 = frame.flag200;
    input.victimFlag80 = frame.flag80;
    input.victimHurt = frame.hurt;
    input.attackerFlag200000 = hit.attackerFlag200000;
    return input;
}

ReactionKind Victim::react(const IncomingHit& hit, const VictimFrame& frame, HumanAnimator& animator,
                           std::uint32_t idle, std::uint64_t nowMs) {
    const combat::Reaction reaction = combat::hitReaction(reactionInput(hit, frame));
    const auto clip = static_cast<std::uint32_t>(reaction.animId);
    m_lastReaction = reaction.animId;
    ++m_reactions;
    // A victim with flag 0x100 is never stunned.
    const bool stuns = !frame.unstunnable && ((hit.flags & combat::kRangeFlagStun) != 0 || stunned());
    // A hit during 199 cuts the rise. The cut drops 199's end callback (`AnimTaskChained_CutOff` clears it before the
    // release), so `Human_GetUp` never runs: the original's human is still down (state 0x80000), its ground time past.
    const bool cutsRise = m_rising;
    m_rising = false;
    // A reaction with a knockdown event lays it down; a stunned one stays stunned until after the rise.
    if (knocksDown(animator, reaction.animId)) {
        animator.playCombat(one(clip), kGroundedIdle, AnimState::Hold);
        // **Stand-in**: `Human_PlayReaction` stuns at a knockdown when the reaction's id is above 0x127 (the crushing
        // 296-303), not by the attack's stun flag; an earlier stun is `Human_SetDownTimers`' (#685).
        knockDown(nowMs, stuns);
        ++m_knockdowns;
        return ReactionKind::Knockdown;
    }
    // Down again under the standing reaction (Coney's ground ended as 199 began): step() rises with 199 once the
    // reaction ends, and that rise's end restarts a stun.
    if (cutsRise) {
        m_grounded = true;
    }
    if (stuns) {
        animator.playCombat(one(clip), static_cast<std::uint32_t>(combat::kStunLoop), AnimState::Hold);
        stun(nowMs);
        ++m_stuns;
        return ReactionKind::Stun;
    }
    animator.playCombat(one(clip), idle, AnimState::Attack);
    return ReactionKind::Plain;
}

void Victim::die(const IncomingHit& hit, const VictimFrame& frame, const DyingState& dying, combat::Health& health,
                 HumanAnimator& animator, const NamedDying* named) {
    const auto has = [&dying](std::uint64_t bits) { return (dying.state & bits) != 0; };
    const bool cuffed = has(human_state::kCuffed);
    const bool wounded = has(human_state::kWounded);
    // 1. The loop it ends in: a woundable human not yet wounded passes through 205 into 200; a cuffed one loops 323.
    std::uint32_t loop = kDeathLoop;
    bool woundedDown = false;
    if (cuffed) {
        loop = kDeathLoopCuffed;
    } else if (dying.woundable && !wounded) {
        loop = kDeathLoopWoundable;
        woundedDown = true;
    }
    // 2. The dying clip: the one the caller names (a paired move's victim side, from the attacker's set), else the
    // first of the special states that holds, in the original's order, else the hit's own (the coin is drawn only then,
    // as Hit_PickDyingReaction draws it).
    const std::uint32_t dyingClip = [&] {
        if (named != nullptr) {
            return named->clip;
        }
        for (const StateClip& special : kHeldDying) {
            if (has(special.bits)) {
                return special.clip;
            }
        }
        if (dying.woundable && !wounded && has(human_state::kKnockedDown)) {
            return kDyingKnockedDown;
        }
        for (const StateClip& special : kDownDying) {
            if (has(special.bits)) {
                return special.clip;
            }
        }
        return static_cast<std::uint32_t>(combat::deathReaction(reactionInput(hit, frame), m_random.coin()));
    }();
    // 3. The fall, by the dying clip's first knockdown event (type 7), read from the set the clip comes from: none
    // found, 308 unless it is down (cuffed, knocked out or down); found with a value of 0 (the u32 at +4), 198; else
    // none.
    std::optional<std::uint32_t> fall;
    const characters::AnimSet* from = named != nullptr ? named->from : nullptr;
    const anim::AnimClip* clip = from != nullptr ? from->clip(dyingClip) : animator.clip(dyingClip);
    const anim::ClipEvent* knockdown = nullptr;
    if (clip != nullptr) {
        const auto found = std::ranges::find_if(
            clip->events, [](const anim::ClipEvent& event) { return event.type == kKnockdownEvent; });
        knockdown = found != clip->events.end() ? &*found : nullptr;
    }
    if (knockdown == nullptr) {
        if (!has(human_state::kDown)) {
            fall = kFallUnmarked;
        }
    } else if (knockdown->value == 0 && knockdown->word == 0) {
        fall = kFallMarked;
    }
    // 4. Each clip's blend into the next: the dying clip's 0.2 unless a fall without 205 follows it, the fall's 0.1
    // into the loop, none into or out of 205.
    std::vector<DeathStep> steps{{.clip = dyingClip, .blend = woundedDown || !fall ? kDyingBlend : 0.0F}};
    if (fall) {
        steps.push_back({.clip = *fall, .blend = woundedDown ? 0.0F : kFallBlend});
    }
    if (woundedDown) {
        steps.push_back({.clip = kWoundedDown, .blend = 0.0F});
    }
    // 5. Played as one chain after Human_PlayGangDeathReaction's 0.1 s blend-out (kCombatFade), holding 0x2000 until
    // the loop; the owner knocks it out once no chained clip plays (Fighter::update), where the original's last clip
    // before the loop ends in Human_OnHealthOut.
    std::vector<std::uint32_t> clips;
    clips.reserve(steps.size());
    for (const DeathStep& step : steps) {
        clips.push_back(step.clip);
    }
    // stand-in: the original sets 0x2000 as it builds the chain and gives it to the clip before the loop alone; every
    // clip here holds and sets it, which keeps it set over the same updates. The blends between the clips are kept in
    // m_deathChain, not built (Coney's chained clips hand over at once).
    if (from != nullptr) {
        animator.playPairedThenOwn(clips.front(), *from, std::span(clips).subspan(1), loop, AnimState::Hold,
                                   kCombatFade, kDyingHolds);
    } else {
        animator.playCombat(clips, loop, AnimState::Hold, kCombatFade, kDyingHolds);
    }
    m_deathChain = DeathChain{.steps = std::move(steps), .loop = loop};
    // 6. The hit waiting is dropped (the pending damage +0x118), and the health, run out, reads 1 while he stays down.
    m_hasPending = false;
    health.keepOneWhileOut();
    m_lastReaction = static_cast<int>(dyingClip);
    // stand-in: the ground and the health's down state (combat::Health::depleted()) stand for the dying state
    // 0x80000000, and the animator's held state for the record's +0x18 = 0.
    m_grounded = true;
    m_stunUntilMs = 0;
    m_stunTimeOver = false;
    m_rising = false;
}

void Victim::knockDown(std::uint64_t nowMs, bool stunned) {
    // **Gap**: a victim with flag 0x100 that is already stunned keeps the stun here in the original (state 0x100000
    // kept, its end at the rise - 1 ms); Coney's react() passes no stun for it, so the stun is lost (#1351).
    m_grounded = true;
    m_riseAtMs = nowMs + static_cast<std::uint64_t>(m_class.groundMs);
    m_stunUntilMs = stunned ? m_riseAtMs + static_cast<std::uint64_t>(m_class.stunMs) : 0;
    m_stunEntered = false;
    m_stunTimeOver = false;
    m_rising = false;
}

void Victim::stun(std::uint64_t nowMs) {
    m_stunUntilMs = nowMs + static_cast<std::uint64_t>(m_class.stunMs);
    m_stunEntered = false;
    m_stunTimeOver = false;
}

void Victim::clear() {
    m_grounded = false;
    m_stunUntilMs = 0;
    m_stunTimeOver = false;
    m_rising = false;
}

bool Victim::step(HumanAnimator& animator, std::uint32_t idle, std::uint64_t nowMs, bool canRise) {
    // 0. The rise 199 over (`Human_GetUp`, its end callback): a stunned human's stun time starts again from now
    // (`Human_SetStunTimer`), in the loop 356 the rise handed over to. A hit that cuts 199 drops the callback
    // (react()).
    if (m_rising && !animator.drivingClipPlaying()) {
        m_rising = false;
        if (stunned() && !m_stunTimeOver) {
            m_stunUntilMs = nowMs + static_cast<std::uint64_t>(m_class.stunMs);
            m_stunEntered = true;
        }
    }
    // 1. Standing stunned with no clip driving the human (`Human_UpdateDown` tests the stun only with no held flag: a
    // reaction, the rise and 355 hold one, the loop 356 none). Its time passed, the stun ends and its state clears as
    // 357 starts (`Human_EndStun`, then `Human_BuildIdleTasks`): at once when the clip that held it ends late enough.
    // Its time not passed, the clip that ended returns to 355, then the loop 356 (`Human_BuildIdleTasks`).
    if (stunned() && !m_grounded && !animator.drivingClipPlaying()) {
        // Past, not on it: the original tests the time minus now below zero. A stun ended from outside is over now.
        if (m_stunTimeOver || nowMs > m_stunUntilMs) {
            m_stunUntilMs = 0;
            m_stunTimeOver = false;
            animator.playCombat(one(static_cast<std::uint32_t>(combat::kStunEnd)), idle, AnimState::Attack);
            // **Stand-in**: with the global flag 0x0051021c set (no writer found), `Human_EndStun` also sets the
            // human's speed multiplier (+0x3a4) to 0.65 unless it holds state 0x80000; Coney keeps the speed as it
            // is (#1346).
        } else if (!m_stunEntered) {
            m_stunEntered = true;
            animator.playCombat(one(static_cast<std::uint32_t>(combat::kStunEnter)),
                                static_cast<std::uint32_t>(combat::kStunLoop), AnimState::Hold);
        }
    }
    // 2. Down long enough (past its time, as the stun) with no clip driving the human (`Human_UpdateDown` tests the
    // ground only with no held flag), it gets up: into the stun's loop while stunned (its time restarted as 199 ends,
    // step 0), else the idle.
    if (m_grounded && canRise && nowMs > m_riseAtMs && !animator.drivingClipPlaying()) {
        // **Stand-in**: the original keeps the knocked-down state (0x80000) until 199 ends (`Human_GetUp`); Coney's
        // ground ends as 199 starts, the rise clip holding the human instead.
        m_grounded = false;
        m_rising = true;
        if (stunned()) {
            animator.playCombat(one(kGroundedRise), static_cast<std::uint32_t>(combat::kStunLoop), AnimState::Hold,
                                kCombatFade, kRiseHolds);
        } else {
            animator.playCombat(one(kGroundedRise), idle, AnimState::Attack, kCombatFade, kRiseHolds);
        }
        return true;
    }
    return false;
}

bool Victim::mash(const HumanAnimator& animator, std::uint64_t nowMs) {
    // Only lying in the ground's loop: a press during the knockdown's reaction does nothing. Down until the rise's time
    // is past (step()), so a press on the update that lands on it still cuts.
    if (!m_grounded || animator.drivingClipPlaying() || nowMs > m_riseAtMs) {
        return false;
    }
    // The cut may take the rise's time below now, as the original's does: step() then rises this update. A stun after
    // the rise is not cut: its time starts again as 199 ends.
    const auto cut = static_cast<std::uint64_t>(combat::mashCut(m_class.groundMs, m_random));
    m_riseAtMs -= std::min(cut, m_riseAtMs);
    return true;
}

bool Victim::knocksDown(const HumanAnimator& animator, int id) {
    const anim::AnimClip* clip = id >= 0 ? animator.clip(static_cast<std::uint32_t>(id)) : nullptr;
    return clip != nullptr && std::ranges::any_of(clip->events, [](const anim::ClipEvent& event) {
               return event.type == kKnockdownEvent;
           });
}

} // namespace coney::human
