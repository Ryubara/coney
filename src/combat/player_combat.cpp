// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/player_combat.h"

#include "core/pad.h"

namespace coney::combat {

PlayerCombat::PlayerCombat(const AnimRangeList* ranges, std::uint64_t startMs, std::uint32_t seed)
    : m_ranges(ranges), m_random(seed), m_power(kPlayerPowerMax, kPlayerPowerRefillPerSecond, startMs),
      m_rage(kPlayerRageMax, kPlayerRageGainPercent, startMs) {}

CombatOutput PlayerCombat::update(const CombatInput& input, const CombatTuning& tuning) {
    CombatOutput out;

    // The meters run every update: power drains while holding someone and refills otherwise; rage drains in rage.
    const bool holding = m_mode == CombatMode::Grabbing || m_mode == CombatMode::Tackling;
    m_power.update(input.nowMs, holding, tuning.powerDrainPerSecond);
    m_rage.update(input.nowMs, tuning.rageDrainPerSecond);

    // 1. The block. With R1 held as the command nothing else reads the input this update.
    const bool blockOnly = updateBlock(input, out);

    // 2. The chain. **Coney choice**: the attack playing still counts its updates under a held R1 (the original's
    // clip plays on); only the press is not read.
    const ChainButton press =
        blockOnly ? ChainButton::None : chainButton(input.command, input.stick, tuning.snapAttacks);
    const ChainStep step = m_chain.update(press, tuning);
    if (step.started != anim_id::kNone) {
        out.startAnim = step.started;
    }
    if (step.hit != anim_id::kNone) {
        out.hitAnim = step.hit;
        out.hitDamage = m_ranges != nullptr ? strikeDamage(*m_ranges, step.hit) : 0;
    }
    if (blockOnly) {
        return out;
    }

    // 3. The state routes, then 4. the commands of a free player.
    switch (m_mode) {
    case CombatMode::Mugging: {
        if (!m_mugging) {
            m_mode = CombatMode::Grabbing;
            break;
        }
        const GameResult result = m_mugging->update(input.nowMs, input.padStick, tuning, m_random);
        if (result != GameResult::Running) {
            out.game = result;
            m_mugging.reset();
            m_mode = CombatMode::Grabbing;
        }
        break;
    }
    case CombatMode::Theft:
        updateTheft(input, tuning, out);
        break;
    case CombatMode::Grabbing:
        updateGrabbing(input, tuning, out);
        break;
    case CombatMode::Tackling:
        // Mounted on the victim, square strikes it.
        if (input.command == command::kSquarePressed && !m_chain.active()) {
            startAttack(anim_id::kMountingStrike, out);
        }
        break;
    case CombatMode::Free:
        updateCommands(input, tuning, out);
        break;
    }
    return out;
}

void PlayerCombat::startTheft(TheftKind kind, std::uint64_t nowMs, float stageTurns, float mashFactor) {
    m_chain.cancel();
    m_mode = CombatMode::Theft;
    if (kind == TheftKind::Rotate) {
        m_theft.emplace(nowMs, stageTurns);
        m_mash.reset();
    } else {
        m_mash.emplace();
        m_theft.reset();
        m_mashFactor = mashFactor;
    }
}

void PlayerCombat::release() {
    m_mode = CombatMode::Free;
    m_mugging.reset();
}

bool PlayerCombat::updateBlock(const CombatInput& input, CombatOutput& out) {
    const bool r1 = m_mode == CombatMode::Free && input.inFight && (input.buttons & pad::kR1) != 0;
    const bool keep =
        m_blocking && ((input.command == command::kL1R1 && !m_rage.full()) || input.command == command::kL1Released);
    if (!r1 && !keep) {
        m_blocking = false;
        return false;
    }
    m_blocking = true;
    out.blocking = true;
    // L1 + R1 with a full meter starts rage from the block.
    if (input.command == command::kL1R1 && m_rage.start(input.nowMs)) {
        out.rageStarted = true;
        out.startAnim = anim_id::kRageStart;
    }
    return input.command == command::kR1Held;
}

void PlayerCombat::startAttack(int animId, CombatOutput& out) {
    m_chain.start(animId);
    out.startAnim = animId;
}

void PlayerCombat::updateGrabbing(const CombatInput& input, const CombatTuning& tuning, CombatOutput& out) {
    // **Coney choice**: one grab move at a time; a move plays out (its hit included) before the next is read.
    if (m_chain.active()) {
        return;
    }
    GrabInput grab;
    grab.command = input.command;
    grab.stick = input.stick;
    grab.fromRear = input.fromRear;
    grab.raging = m_rage.raging();
    grab.wallInReach = input.wallInReach;
    grab.victimMuggable = input.victimMuggable;
    const GrabOutcome outcome = updateGrab(grab, m_power, tuning, m_random);
    out.grabAction = outcome.action;
    if (outcome.animId != anim_id::kNone) {
        startAttack(outcome.animId, out);
    }
    if (outcome.action == GrabAction::Mug) {
        m_mugging.emplace(input.nowMs, m_random);
        m_mode = CombatMode::Mugging;
    } else if (outcome.action == GrabAction::Throw) {
        // **Coney choice**: the throw lets go at once; its hit still lands through the attack's timing.
        m_mode = CombatMode::Free;
    }
}

void PlayerCombat::updateTheft(const CombatInput& input, const CombatTuning& tuning, CombatOutput& out) {
    GameResult result = GameResult::Running;
    if (m_theft) {
        result = m_theft->update(input.nowMs, input.command, input.padStick, tuning);
        if (result != GameResult::Running) {
            out.startAnim = result == GameResult::Succeeded ? anim_id::kStereoStealEnd : anim_id::kStereoStealFail;
        }
    } else if (m_mash) {
        result = m_mash->update(input.command, m_mashFactor, tuning);
    }
    if (result != GameResult::Running) {
        out.game = result;
        m_theft.reset();
        m_mash.reset();
        m_mode = CombatMode::Free;
    }
}

void PlayerCombat::grabOrTackle(const CombatInput& input, const CombatTuning& tuning, CombatOutput& out) {
    if (!grabAllowed(m_chain.phaseFlags(tuning))) {
        return;
    }
    const bool tackle = input.command == command::kCircleHeld;
    m_chain.cancel();
    out.startAnim = tackle ? anim_id::kTacklePlayerIntro : anim_id::kGrabPlayerIntro;
    if (!input.grabTargetInReach) {
        out.grabMissed = true;
        return;
    }
    m_mode = tackle ? CombatMode::Tackling : CombatMode::Grabbing;
    out.grabStarted = !tackle;
    out.tackleStarted = tackle;
}

void PlayerCombat::updateCommands(const CombatInput& input, const CombatTuning& tuning, CombatOutput& out) {
    switch (input.command) {
    case command::kL2Cross:
    case command::kL2Square:
        if (runningAttackAllowed(input.gait, m_chain.phaseFlags(tuning))) {
            startAttack(
                input.command == command::kL2Cross ? anim_id::kRunningAttackCharge : anim_id::kRunningAttackDive, out);
        }
        break;
    case command::kSquarePressed:
        if (!m_chain.active()) {
            SquareInput square;
            square.target = input.target;
            square.stick = input.stick;
            square.gait = input.gait;
            square.snapAttacks = tuning.snapAttacks;
            startAttack(input.target == TargetKind::Breakable ? objectAttack(input.objectHeight) : squareAttack(square),
                        out);
        }
        break;
    case command::kCrossLongHold:
        if (!m_chain.active()) {
            startAttack(crossAttack(), out);
        }
        break;
    case command::kCircleTapped:
    case command::kCircleHeld:
        grabOrTackle(input, tuning, out);
        break;
    default:
        break;
    }
}

} // namespace coney::combat
