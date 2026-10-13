// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "combat/anim_ids.h"
#include "combat/reactions.h"
#include "combat/tandem.h"
#include "human/fighter.h"
#include "human/fighter_clips.h"
#include "human/pair_placement.h"

// The tandem: a three-person move in which an attacker strikes a victim another human holds from behind. The attacker's
// fighter starts it and puts itself in place; the victim plays his side from the attacker's set and passes the set to
// his grabber, whose fighter joins on its next update; the victim takes the damage when his intro ends; the grabber
// lets go into the fight stance after its end clip and the victim lies on the ground (or dies) after his.
// Research: docs/research/combat-moves.md#tandem, docs/research/combat-reactions.md#protected-moves

namespace coney::human {

namespace {

namespace id = combat::anim_id;

// Where the victim stands in the attacker's frame without range data for the attacker's intro: straight ahead at the
// first set's reach (1.72 m, docs/research/combat-moves.md#frame-data).
constexpr anim::Vec3 kTandemFallbackPoint{0.0F, 1.72F, 0.0F};
// A pair point shorter than this has no direction to align along.
constexpr float kMinPairPoint = 0.01F;

// The horizontal distance between two points.
float flatDistance(const anim::Vec3& a, const anim::Vec3& b) { return std::hypot(b.x - a.x, b.y - a.y); }

} // namespace

Holdable* Fighter::tandemTarget(const FighterInput& input, float range) const {
    // A target the tandem may start at: held from the rear, the attacker in its front cone, none of 0xc12200 held,
    // with health left, within `range` and the search's height.
    const auto allows = [&input, range](Combatant& target) -> Holdable* {
        if (target.health().depleted() || flatDistance(input.position, target.position()) > range ||
            std::fabs(target.position().z - input.position.z) > kPickHeight) {
            return nullptr;
        }
        const combat::TandemCheck check{.victimRearHeld = target.heldFromRear(),
                                        .attackerSide =
                                            combat::victimSide(target.position(), target.heading(), input.position),
                                        .victimHeld = target.heldFlags()};
        return combat::tandemCanStart(check) ? target.holdable() : nullptr;
    };
    // The current target first, then the nearest.
    Holdable* best = nullptr;
    float bestDistance = range;
    for (Combatant* target : input.targets) {
        Holdable* victim = allows(*target);
        if (victim == nullptr) {
            continue;
        }
        if (target == m_target) {
            return victim;
        }
        const float distance = flatDistance(input.position, target->position());
        if (distance <= bestDistance) {
            best = victim;
            bestDistance = distance;
        }
    }
    return best;
}

void Fighter::startTandem(Holdable& victim, int base, const FighterInput& input, HumanAnimator& animator,
                          float heading) {
    const combat::TandemClips set = combat::tandemClips(base);
    const int damage = m_ranges != nullptr ? combat::tandemDamage(*m_ranges, base) : 0;
    // The victim first: it may refuse (a passive target plays no tandem), and then nothing starts.
    if (!victim.startTandem(TandemStart{.base = base,
                                        .attackerAnims = &animator.anims(),
                                        .damage = damage,
                                        .attacker = input.position,
                                        .attackerIsPlayer = m_player})) {
        return;
    }
    // The start announces the attack to the humans around, as any attack's does.
    for (Combatant* other : input.targets) {
        other->announceAttack(input.position);
    }
    alignTandem(victim, base, input, animator, heading);
    const std::array<std::uint32_t, 2> clips{static_cast<std::uint32_t>(set.attackerIntro),
                                             static_cast<std::uint32_t>(set.attackerEnd)};
    animator.playCombat(clips, kAnimFightIdle, AnimState::Attack, clips::kPairFade,
                        HeldFlags{.held = combat::kTandemAttackerHeld, .set = combat::kTandemAttackerHeld});
    m_target = &victim;
    m_tandemRole = TandemRole::Attacker;
    m_tandemBase = base;
}

void Fighter::alignTandem(const Holdable& victim, int base, const FighterInput& input, const HumanAnimator& animator,
                          float heading) {
    const auto intro = static_cast<std::uint32_t>(combat::tandemClips(base).attackerIntro);
    anim::Vec3 point = pairPoint(m_ranges, intro, kTandemFallbackPoint);
    if (std::hypot(point.x, point.y) < kMinPairPoint) {
        point = kTandemFallbackPoint;
    }
    // The heading that puts the victim along the point's direction, then the feet that put him at its length.
    const anim::Vec3 to = anim::subtract(victim.position(), input.position);
    const float towards = std::hypot(to.x, to.y) > 1e-4F ? headingOf(to) : heading;
    const float wanted = wrapAngle(towards - std::atan2(-point.x, point.y));
    const anim::Vec3 along = anim::subtract(fromFrame(input.position, wanted, point), input.position);
    const anim::Vec3 feet{victim.position().x - along.x, victim.position().y - along.y, input.position.z};
    const anim::AnimClip* clip = animator.clip(intro);
    const float seconds =
        std::max(input.stepSeconds, clip != nullptr ? alignSeconds(*clip, animator.anims().rate(intro)) : 0.0F);
    m_steer.clear();
    m_steer.turnToOver(heading, wanted, seconds);
    m_steer.moveToOver(input.position, feet, seconds);
}

bool Fighter::startTandemVictim(const TandemStart& start, HumanAnimator& animator, bool cuffed, bool knockedOut) {
    if (!heldFromRear() || m_tandemRole != TandemRole::None || start.attackerAnims == nullptr) {
        return false;
    }
    const combat::TandemClips set = combat::tandemClips(start.base);
    const std::array<std::uint32_t, 2> clips{static_cast<std::uint32_t>(set.victimIntro),
                                             static_cast<std::uint32_t>(set.victimEnd)};
    // Its side from the attacker's set, both clips holding 0x400000, then the ground ("KD 0" on the end clip).
    animator.playPaired(clips, *start.attackerAnims, clips::kGroundedIdle, AnimState::Hold, clips::kPairFade,
                        HeldFlags{.held = combat::kTandemVictimHeld, .set = combat::kTandemVictimHeld});
    m_tandemRole = TandemRole::Victim;
    m_tandemBase = start.base;
    m_tandemDamage = start.damage;
    m_tandemIntroOver = false;
    m_tandemCuffed = cuffed;
    m_tandemKnockedOut = knockedOut;
    m_tandemByPlayer = start.attackerIsPlayer;
    m_tandemAttacker = start.attacker;
    m_tandemRequest = start.base;
    // No longer in the rear hold's state: no second tandem starts on him.
    m_rearHeld = false;
    return true;
}

void Fighter::joinTandem(int base, HumanAnimator& animator) {
    // The grab is over for the dispatcher: no power drain, no moves in the hold. The victim stays attached where the
    // hold put him, placed each update (step 7 of the update), until the grabber lets go.
    m_combat.release();
    const combat::TandemClips set = combat::tandemClips(base);
    const std::array<std::uint32_t, 2> clips{static_cast<std::uint32_t>(set.grabberIntro),
                                             static_cast<std::uint32_t>(set.grabberEnd)};
    animator.playCombat(clips, kAnimFightIdle, AnimState::Attack, clips::kPairFade,
                        HeldFlags{.held = combat::kTandemAttackerHeld, .set = combat::kTandemAttackerHeld});
    m_tandemRole = TandemRole::Grabber;
    m_tandemBase = base;
}

void Fighter::followTandem(const FighterInput& input, HumanAnimator& animator) {
    const combat::TandemClips set = combat::tandemClips(m_tandemBase);
    const auto clip = static_cast<int>(animator.animId());
    switch (m_tandemRole) {
    case TandemRole::Attacker:
        // His intro and end over: the fight idle plays, the tandem is done for him.
        if (clip != set.attackerIntro && clip != set.attackerEnd) {
            m_tandemRole = TandemRole::None;
        }
        break;
    case TandemRole::Grabber:
        // His end over: he lets go of the victim into the fight stance. **Coney's reading**: at the end of his own end
        // clip (the page's 48 updates after the damage is not explained by the clips' lengths).
        if (clip != set.grabberIntro && clip != set.grabberEnd) {
            if (m_held != nullptr) {
                m_held->setAttached(false);
            }
            m_held = nullptr;
            m_pair = PairStage::None;
            m_rear = false;
            m_tandemRole = TandemRole::None;
        }
        break;
    case TandemRole::Victim:
        // The intro has handed over to the end: the damage (Tandem_OnVictimIntroEnd).
        if (!m_tandemIntroOver && clip != set.victimIntro) {
            m_tandemIntroOver = true;
            tandemVictimIntroEnd(input);
        }
        // The end is over: on the ground, the hold done; a defeated victim's death plays now (takePending()).
        if (m_tandemIntroOver && clip != set.victimEnd) {
            m_tandemRole = TandemRole::None;
            m_tandemRequest.reset();
            if (!m_health.depleted()) {
                enterHold(TargetState::Grounded, animator, input.nowMs);
            }
        }
        break;
    case TandemRole::None:
        break;
    }
}

void Fighter::tandemVictimIntroEnd(const FighterInput& /*input*/) {
    // Human_IsDamageDefeating decides: a defeating damage takes all his health (it counts three times over on a cuffed
    // victim, who may have more health than the damage).
    const bool defeats = combat::damageDefeats(combat::DefeatTest{.health = m_health.value(),
                                                                  .damage = m_tandemDamage,
                                                                  .cuffed = m_tandemCuffed,
                                                                  .god = hasFlag(flag::kGod),
                                                                  .demiGod = hasFlag(flag::kDemiGod),
                                                                  .knockedOut = m_tandemKnockedOut});
    // The hit is the attacker's end id, with no reaction of its own: the victim's clips are the tandem's.
    const int end = combat::tandemClips(m_tandemBase).attackerEnd;
    m_victim.hit(IncomingHit{.damage = defeats ? std::max(m_tandemDamage, m_health.value()) : m_tandemDamage,
                             .attackAnim = end,
                             .attacker = m_tandemAttacker,
                             .react = false,
                             .ignoresArmour = true,
                             .attackerIsPlayer = m_tandemByPlayer});
}

} // namespace coney::human
