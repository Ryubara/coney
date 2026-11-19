// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <span>

#include "combat/combat_tuning.h"
#include "combat/commands.h"
#include "combat/grab.h"
#include "combat/reactions.h"
#include "combat/stick.h"
#include "combat/tandem.h"
#include "core/log.h"
#include "human/body.h"
#include "human/fighter.h"
#include "human/fighter_clips.h"
#include "human/human_contacts.h"
#include "human/pair_placement.h"
#include "human/way_clear.h"

// The stealth kill: a hidden player holding L1 behind a human who faces away presses square, cross or circle, and the
// pair plays, his kill clip and the victim's side from his set; the victim takes 3000, knocked out and never wounded.
// The attacker's side runs in the player's update; the victim's (takePairedMove()) is told by the attacker, through the
// victim's Holdable view, as the original's Attack_StartPaired plays both humans' tasks at once.
// Research: docs/research/stealth.md#stealth-kill, docs/research/combat-moves.md#stealth

namespace coney::human {

namespace {

// The kill's clip bare-handed, with a set-1 weapon (the knife) and with set 2 or 3 (a baton or a club); the victim
// plays the next id (docs/research/combat-moves.md#stealth).
constexpr std::uint32_t kStealthKill = 637;
constexpr std::uint32_t kStealthKnifeKill = 639;
constexpr std::uint32_t kStealthBatonKill = 641;
// The bits the victim's side holds and sets: the paired victim's (`Player_StartStealthKill` passes 0x400000).
constexpr std::uint32_t kStealthVictimHeld = 0x400000;
// The attacker's chained clip holds 7 and the record then takes 1 (`Attack_StartPaired`'s last argument 1, which the
// chained task turns into 7).
constexpr HeldFlags kPairedAttackerHeld{.held = 0x7, .set = 0x1};
// A target whose clip holds any of these bits may not be grabbed (`Human_CanBeGrabbedBy` `0x00225520`'s held test).
constexpr std::uint32_t kGrabRefusedHeld = 0x8fc57a60;
// The pair's alignment turns both bodies over 1/60 s (`Pair_AlignForMove` `0x00277b20`).
constexpr float kPairedAlignSeconds = 1.0F / 60.0F;
// Where the victim stands in the attacker's frame for a clip with no range data: straight ahead at 637's reach (0.74
// m, docs/research/combat-moves.md#stealth).
constexpr anim::Vec3 kStealthFallbackPoint{0.0F, 0.74F, 0.0F};

} // namespace

bool Fighter::tryStealthKill(const FighterInput& input, HumanAnimator& animator, float& heading) {
    // A hidden player's buttons (state 0x200000 sends Player_UpdateActions here). The busy gates are the L1
    // handlers' (updateL1()), which the original runs on the same branch first: the block, the dispatcher's busy
    // phases and a hold. **Coney's choice**: Player_BufferChain's chain press and the armed special of a throwable
    // held object (Player_ArmedSpecial) come before the buttons in the original; Coney's dispatcher takes both after.
    if (!m_player || !m_lockBlocked || blocking() || (animator.flags() & kStanceGate) != 0 || m_held != nullptr) {
        return false;
    }
    // Square pressed (0xf), cross (0x10), circle held (0xe) or circle tapped (0xd); any other press goes on.
    const combat::CommandId command = input.command;
    if (command != combat::command::kSquarePressed && command != combat::command::kCrossLongHold &&
        command != combat::command::kCircleHeld && command != combat::command::kCircleTapped) {
        return false;
    }
    // A weapon of set 4, 5 or 6 takes the press to its own cross (0x002880d8), which the dispatcher plays; without
    // L1's bit, or for a human no pad drives (the record's +0x1b), the press is the normal strike, kick or grab.
    // **Coney stand-in**: the set-5 cross's own stealth kill while hidden (0x002880d8 calls Player_StartStealthKill)
    // is not built.
    if (m_animSet == 4 || m_animSet == 5 || m_animSet == 6 || !input.padControlled || !m_l1Lock) {
        return false;
    }
    // From here the press runs only the kill test: no target, or one it fails, and nothing plays.
    Combatant* target = m_target;
    if (target == nullptr) {
        return true;
    }
    // The player in the target's rear quarter (side 2) and the target in his front one (side 0).
    // **Coney's reading**: both sides are taken by the reaction's test (combat::victimSide(), `Hit_GetSideFromPoint`
    // with the mode 0 these calls pass).
    if (combat::victimSide(target->position(), target->heading(), input.position) != combat::Side::Rear ||
        combat::victimSide(input.position, heading, target->position()) != combat::Side::Front) {
        return true;
    }
    Holdable* victim = grabbableNow(*target, input);
    if (victim == nullptr) {
        return true;
    }
    // The way to him clear (`Human_IsWayClearTo`). **Coney stand-in**: the other humans are not swept, and his
    // capsule is the 2 m upright one of the body's radius.
    const UprightCapsule goal{.feet = target->position(), .top = 2.0F, .radius = bodyTuning().radius};
    if (!wayClear(input.mesh, input.position, goal.top, goal, std::span<const UprightCapsule>{})) {
        return true;
    }
    startStealthKill(*victim, input, animator, heading);
    return true;
}

Holdable* Fighter::grabbableNow(Combatant& target, const FighterInput& input) {
    // A human with flag 0x40 offers no hold (holdable() is null). Not down or dead, within the height, no refusing
    // held bit. **Coney stand-in**: the scene state and the state word's mask are read as standing with health left,
    // which is what Coney's targets report; the knocked-down branch (a rise more than half played) cannot pass then.
    Holdable* holdable = target.holdable();
    if (holdable == nullptr || target.health().depleted() || target.state() != TargetState::Standing ||
        std::fabs(target.position().z - input.position.z) > combat::kGrabbableHeight ||
        (target.heldFlags() & kGrabRefusedHeld) != 0) {
        return nullptr;
    }
    return holdable;
}

void Fighter::startStealthKill(Holdable& victim, const FighterInput& input, HumanAnimator& animator, float& heading) {
    // The clip by the weapon held. **Coney stand-in**: a held object of another set is dropped first in the original
    // (637 is then played); only the set-5 cross reaches here with one, and Coney does not build that path.
    std::uint32_t clip = kStealthKill;
    if (m_animSet == 1) {
        clip = kStealthKnifeKill;
    } else if (m_animSet == 3 || m_animSet == 2) {
        clip = kStealthBatonKill;
    }
    // A pad player, still in the target's rear quarter, with a paired clip: the victim's flag 0x8 is cleared (knocked
    // out, not wounded) and the pair starts. **Coney's choice**: the original pops move style 0xf first; Coney's
    // player keeps no move style stack.
    const bool behind = combat::victimSide(victim.position(), victim.heading(), input.position) == combat::Side::Rear;
    if (!input.padControlled || !behind || animator.clip(clip) == nullptr) {
        return;
    }
    const PairedMove move{.attackAnim = static_cast<int>(clip),
                          .attackerAnims = &animator.anims(),
                          .attacker = input.position,
                          .attackerIsPlayer = m_player,
                          .notWoundable = true,
                          .victimHeld = kStealthVictimHeld};
    if (startPairedMove(victim, clip, move, input, animator, heading)) {
        m_stealthKillClip = clip;
        logging::info(logging::Category::Combat, std::format("stealth kill: {} with anim {}", logName(), clip));
    }
}

bool Fighter::startPairedMove(Holdable& victim, std::uint32_t clip, const PairedMove& move, const FighterInput& input,
                              HumanAnimator& animator, float& heading) {
    const combat::CombatTuning& tuning = combat::combatTuning();
    // The alignment (`Pair_AlignForMove`): refused beyond the clip's far range (× 1.25 for a player); the attacker
    // turns to face the victim and slides to where the victim stands at the clip's pair point, and the victim turns
    // away from him (from behind; to face him from the front), both over 1/60 s. **Coney's choice**: the turns and
    // the slide take at least one step, as Coney's steer cannot act within one; the shared collision group is not
    // set (Coney's pair never collides with itself).
    const float listed = m_ranges != nullptr ? m_ranges->farRange(clip) : 0.0F;
    const float far = m_player ? listed * kPlayerFarScale : listed;
    const anim::Vec3 to = anim::subtract(victim.position(), input.position);
    if (std::hypot(to.x, to.y) > far) {
        return false;
    }
    const bool front = combat::victimSide(victim.position(), victim.heading(), input.position) == combat::Side::Front;
    const float towards = std::hypot(to.x, to.y) > 1e-4F ? headingOf(to) : heading;
    const float victimHeading = front ? wrapAngle(towards + std::numbers::pi_v<float>) : towards;
    const anim::Vec3 point = pairPoint(m_ranges, clip, kStealthFallbackPoint);
    const anim::Vec3 along = anim::subtract(fromFrame(input.position, towards, point), input.position);
    const anim::Vec3 feet{victim.position().x - along.x, victim.position().y - along.y, input.position.z};
    const float seconds = std::max(input.stepSeconds, kPairedAlignSeconds);
    m_steer.clear();
    m_steer.turnToOver(heading, towards, seconds);
    m_steer.moveToOver(input.position, feet, seconds);
    victim.carryOver(victim.position(), victimHeading, seconds);
    // The damage: the clip's, doubled by flag 0x4000. **Coney stand-in**: with a weapon held the original scales it
    // by the first number of the attacker's class record, which Coney does not read (the kill defeats either way).
    int damage = m_ranges != nullptr ? m_ranges->damage(clip) : 0;
    if (hasFlag(flag::kDoubleDamage)) {
        damage *= 2;
    }
    // A move that deals damage needs more than the power endurance of the meter and spends it. **Coney's choice**:
    // the overlay and strikes it ends and a grab of the victim it breaks (`Human_EndOverlayAndStrikes`,
    // `Grab_BreakOnHit`) have nothing to end here: the hidden buttons start no strike, and a held victim fails the
    // grabbable test; the throwing branch (a move from a throw) is not this move's.
    if (damage != 0) {
        // **Coney's choice**: the meter is first brought to this update's time, as the original's meters
        // (`Human_UpdateMeters`) run before the actions; Coney's dispatcher refills it after them, which would hand
        // back this update's refill over the spend (302 where the original reads 300).
        m_combat.power().update(input.nowMs, false, tuning.powerDrainPerSecond);
        if (m_combat.power().fraction() <= tuning.powerEndurance) {
            return false;
        }
        m_combat.power().spend(tuning.powerEndurance);
    }
    // The victim's side: the damage, then its clip or its death chain.
    PairedMove sent = move;
    sent.damage = quarteredFor(victim, damage);
    if (!victim.takePairedMove(sent)) {
        return false;
    }
    // This human's clip, then the fight idle (state 0xb). **Coney's choice**: the push sphere the original turns off
    // for the pair is not modelled (Coney's pair never pushes itself apart).
    animator.playCombat(clips::one(clip), kAnimFightIdle, AnimState::Attack, clips::kPairFade, kPairedAttackerHeld);
    m_target = &victim;
    ++m_hitsLanded;
    m_damageDealt += sent.damage;
    return true;
}

bool Fighter::takePairedMove(const PairedMove& move, const FighterInput& input, HumanAnimator& animator) {
    if (m_health.depleted() || move.attackerAnims == nullptr) {
        return false;
    }
    if (move.notWoundable) {
        setFlag(flag::kWoundable, false);
    }
    const auto side = static_cast<std::uint32_t>(move.attackAnim + 1);
    const IncomingHit hit{.damage = move.damage,
                          .attackAnim = move.attackAnim,
                          .attacker = move.attacker,
                          .react = false,
                          .ignoresArmour = move.attackerIsPlayer,
                          .attackerIsPlayer = move.attackerIsPlayer};
    // A defeating damage takes all its health and plays the death chain with its side as the dying clip
    // (`Human_PlayDeathReaction` with the attacker's set). **Coney's choice**: the foe it sets and the speech it
    // stops (`Human_SetFoe`, `Human_StopSpeech`) are the brain's, which hears of the hit through its health; the
    // push sphere it turns off is not modelled; and a side with no knockdown event, which the original pairs with a
    // random dying clip of the victim's own (308-311), is taken as the dying clip all the same
    // (docs/research/stealth.md#q-stealth-side).
    const bool defeats = combat::damageDefeats(combat::DefeatTest{.health = m_health.value(),
                                                                  .damage = move.damage,
                                                                  .cuffed = input.cuffed,
                                                                  .god = hasFlag(flag::kGod),
                                                                  .demiGod = hasFlag(flag::kDemiGod),
                                                                  .knockedOut = input.knockedOut});
    if (defeats) {
        m_health.apply(std::max(move.damage, m_health.value()));
        logging::info(logging::Category::Combat,
                      std::format("damage: {} took anim {} for all its health", logName(), move.attackAnim));
        const NamedDying named{.clip = side, .from = move.attackerAnims};
        dieNow(hit, input, animator, &named);
        return true;
    }
    // Any other damage is taken at once (`Human_DealDamage`) and its side plays, then the ground. **Coney stand-in**:
    // the original's loop after the side (the stunned or the down slot) and the fall it may chain are not read; the
    // side holds and sets the move's bits and lies down after it.
    m_health.apply(move.damage);
    m_combat.interrupt();
    m_steer.clear();
    const HeldFlags held{.held = move.victimHeld, .set = move.victimHeld};
    animator.playPaired(clips::one(side), *move.attackerAnims, clips::kGroundedIdle, AnimState::Hold, clips::kPairFade,
                        held);
    enterHold(TargetState::Grounded, animator, input.nowMs);
    m_reacting = true;
    return true;
}

void Fighter::followStealthKill(const HumanAnimator& animator) {
    // The kill's clip over (the pair's end, Human_BreakPair): the hiding ends and he stands in the fight stance, as
    // the fight idle after 637 shows at runtime.
    if (m_stealthKillClip.has_value() && animator.animId() != *m_stealthKillClip) {
        m_stealthKillClip.reset();
        enterStance();
    }
}

} // namespace coney::human
