// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/player_combat.h"

#include "core/pad.h"

namespace coney::combat {

namespace {

// The attack `animId` as anim set `set` plays it: `S1`, `X1` and the two strikes take the set's clips.
int withAnimSet(int animId, int set) {
    const AnimSetClips clips = animSetClips(set);
    switch (animId) {
    case anim_id::kAttackS1:
        return clips.square;
    case anim_id::kAttackX1:
        return clips.cross;
    case anim_id::kGroundedStrike1:
        return clips.grounded;
    case anim_id::kMountingStrike:
        return clips.mounting;
    default:
        return animId;
    }
}

} // namespace

PlayerCombat::PlayerCombat(const AnimRangeList* ranges, std::uint64_t startMs, std::uint32_t seed)
    : m_ranges(ranges), m_random(seed), m_power(kPlayerPowerMax, kPlayerPowerRefillPerSecond, startMs),
      m_rage(kPlayerRageMax, kPlayerRageGainPercent, startMs) {}

CombatOutput PlayerCombat::update(const CombatInput& input, const CombatTuning& tuning) {
    CombatOutput out;

    // The meters run every update: power drains while holding someone (but not in rage) and refills otherwise; rage
    // drains in rage and decays after a gain's hold.
    const bool holding = m_mode == CombatMode::Grabbing || m_mode == CombatMode::Tackling;
    m_power.update(input.nowMs, holding && !m_rage.raging(), tuning.powerDrainPerSecond);
    m_rage.update(input.nowMs, tuning);
    if (input.helpless) {
        return out;
    }

    // The record's +0x08 as the update starts: the attack's phase and what the fighter's clips carry.
    const std::uint32_t phase = phaseFlags(input);

    // 1. The block. With R1 held as the command nothing else reads the input this update.
    const bool blockOnly = updateBlock(input, out);
    // In the recovery or the run attack the dispatcher returns before the chain and every command.
    const bool dropped = (phase & kDispatchDroppingPhases) != 0;

    // 2. The chain. **Coney choice**: the attack playing still counts the updates to its hit under a held R1 or a
    // dropping phase (the original's clip plays on); only the press is not read.
    const ChainButton press =
        blockOnly || dropped ? ChainButton::None : chainButton(input.command, input.stick, tuning.snapAttacks);
    const ChainStep step = m_chain.update(press, phase, tuning);
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
    if (dropped) {
        // The meters still end a grab whose power ran out (Human_UpdateMeters, not the dispatcher).
        if (m_mode == CombatMode::Grabbing && m_power.value() == 0) {
            updateGrabbing(input, tuning, out);
        }
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
        // Mounted on the victim, once the mount's clips and the last move have given back their bits (square's mask).
        if ((phase & kAttackRefusingPhases) == 0) {
            updateMounting(input, tuning, out);
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
    m_powerMove = anim_id::kNone;
}

void PlayerCombat::interrupt() {
    m_chain.cancel();
    m_blocking = false;
}

void PlayerCombat::startCounter(int animId, const CombatTuning& tuning) {
    m_chain.cancel();
    m_blocking = false;
    m_chain.start(animId, tuning);
}

void PlayerCombat::startHolding() {
    m_chain.cancel();
    m_blocking = false;
    m_mode = CombatMode::Grabbing;
}

bool PlayerCombat::updateBlock(const CombatInput& input, CombatOutput& out) {
    // A block starts only on R1 held on a pad (the record's buttons): an AI's command 4 alone never starts one
    // (docs/research/ai.md#block).
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

void PlayerCombat::startAttack(int animId, const CombatTuning& tuning, CombatOutput& out) {
    out.startAnim = animId;
    // An attack whose hit lands on its start (the power strike) deals it now.
    if (m_chain.start(animId, tuning)) {
        out.hitAnim = animId;
        out.hitDamage = m_ranges != nullptr ? strikeDamage(*m_ranges, animId) : 0;
    }
}

bool PlayerCombat::extendPowerMove(const CombatInput& input, const CombatTuning& tuning, CombatOutput& out) {
    // Square's press or cross's 0x10 while the power move playing has its window open (a press in the wind-up is
    // dropped), at most twice: the next part, at no further cost.
    const bool press = input.command == command::kSquarePressed || input.command == command::kCrossLongHold;
    if (m_powerMove == anim_id::kNone || m_chain.animId() != m_powerMove || !press ||
        (phaseFlags(input) & kPhaseChainWindow) == 0 || m_powerExtensions >= kMaxPowerExtensions) {
        return false;
    }
    ++m_powerExtensions;
    m_powerMove += 2;
    out.grabAction = GrabAction::PowerStrike;
    startAttack(m_powerMove, tuning, out);
    return true;
}

void PlayerCombat::updateGrabbing(const CombatInput& input, const CombatTuning& tuning, CombatOutput& out) {
    // The grab breaks when the power meter runs out.
    if (m_power.value() == 0) {
        m_chain.cancel();
        out.grabAction = GrabAction::LetGo;
        out.startAnim = anim_id::kGrabLetGo;
        out.grabPowerOut = true;
        m_mode = CombatMode::Free;
        return;
    }
    if (extendPowerMove(input, tuning, out)) {
        return;
    }
    // One grab move at a time: a move's clip (a strike, a spin, the mugging's end) and the grab's own intro and
    // connecting clips hold bits square refuses on (update() documents the mask).
    if ((phaseFlags(input) & kAttackRefusingPhases) != 0) {
        return;
    }
    GrabInput grab;
    grab.command = input.command;
    grab.stick = input.stick;
    grab.fromRear = input.fromRear;
    grab.raging = m_rage.raging();
    grab.wallInReach = input.wallInReach;
    grab.victimMuggable = input.victimMuggable;
    grab.victimInPlace = input.victimInPlace;
    const GrabOutcome outcome = updateGrab(grab, m_power, tuning, m_random);
    out.grabAction = outcome.action;
    switch (outcome.action) {
    case GrabAction::PowerStrike:
        m_powerMove = outcome.animId;
        m_powerExtensions = 0;
        startAttack(outcome.animId, tuning, out);
        break;
    case GrabAction::Strike:
    case GrabAction::Throw:
        // The moves with a hit run through the chain for their timing. **Coney choice**: a rear power strike's spin
        // is played by the caller in front of the strike, whose timing starts with it.
        startAttack(outcome.animId, tuning, out);
        break;
    case GrabAction::Spin:
        out.startAnim = outcome.animId;
        break;
    case GrabAction::Mount:
        // Mounted, the player is in the tackle's state: square strikes the victim on the ground.
        out.startAnim = outcome.animId;
        m_mode = CombatMode::Tackling;
        break;
    case GrabAction::Mug:
        m_mugging.emplace(input.nowMs, m_random);
        m_mode = CombatMode::Mugging;
        break;
    case GrabAction::LetGo:
        out.startAnim = outcome.animId;
        m_mode = CombatMode::Free;
        break;
    case GrabAction::Release:
        m_mode = CombatMode::Free;
        break;
    case GrabAction::None:
        break;
    }
    if (outcome.action == GrabAction::Throw) {
        // **Coney choice**: the throw lets go at once; its hit still lands through the attack's timing.
        m_mode = CombatMode::Free;
    }
}

void PlayerCombat::updateMounting(const CombatInput& input, const CombatTuning& tuning, CombatOutput& out) {
    const MountOutcome outcome =
        updateMount(MountInput{.command = input.command, .raging = m_rage.raging()}, m_power, tuning, m_random);
    out.mountAction = outcome.action;
    switch (outcome.action) {
    case MountAction::Strike:
    case MountAction::PowerStrike:
        // The strikes run through the chain for their hit's timing.
        startAttack(outcome.animId, tuning, out);
        break;
    case MountAction::ToHold:
        out.startAnim = outcome.animId;
        m_mode = CombatMode::Grabbing;
        break;
    case MountAction::GetOff:
        out.startAnim = outcome.animId;
        m_mode = CombatMode::Free;
        break;
    case MountAction::None:
        break;
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

void PlayerCombat::grabOrTackle(const CombatInput& input, CombatOutput& out) {
    // Refused while +0x08 has any of 0xfc7eaf7: an attack's phases, and the grab bit 0x10 its own intro and miss hold
    // (docs/research/combat.md#input-return).
    if (!grabAllowed(phaseFlags(input))) {
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
        // The original's own test (a run with +0x08 clear, or a sprint) decides, not whether a clip plays.
        if (runningAttackAllowed(input.gait, phaseFlags(input))) {
            startAttack(input.command == command::kL2Cross ? anim_id::kRunningAttackCharge
                                                           : anim_id::kRunningAttackDive,
                        tuning, out);
        }
        break;
    case command::kSquarePressed:
        // Refused while +0x08 has any of 0x100101f (the attack phases, the grab bit, the duck, the run attack).
        if ((phaseFlags(input) & kAttackRefusingPhases) == 0) {
            SquareInput square;
            square.target = input.target;
            square.stick = input.stick;
            square.gait = input.gait;
            square.snapAttacks = tuning.snapAttacks;
            startAttack(input.target == TargetKind::Breakable ? objectAttack(input.objectHeight)
                                                              : withAnimSet(squareAttack(square), input.animSet),
                        tuning, out);
        }
        break;
    case command::kCrossLongHold:
        if ((phaseFlags(input) & kAttackRefusingPhases) == 0) {
            startAttack(withAnimSet(crossAttack(), input.animSet), tuning, out);
        }
        break;
    case command::kCircleTapped:
    case command::kCircleHeld:
        grabOrTackle(input, out);
        break;
    case command::kCrossSquare:
        special(input, tuning, out);
        break;
    case command::kCircleCross:
        strongGrapple(input, out);
        break;
    default:
        break;
    }
}

void PlayerCombat::special(const CombatInput& input, const CombatTuning& tuning, CombatOutput& out) {
    if ((phaseFlags(input) & kSpecialRefusingPhases) != 0) {
        return;
    }
    // Not paired, it needs and spends the endurance fraction of the meter; rage plays its own and spends nothing.
    const bool raging = m_rage.raging();
    if (!raging) {
        if (m_power.fraction() < tuning.powerEndurance) {
            return;
        }
        m_power.spend(tuning.powerEndurance);
    }
    startAttack(raging ? anim_id::kSpecialRage : anim_id::kSpecial, tuning, out);
}

void PlayerCombat::strongGrapple(const CombatInput& input, CombatOutput& out) {
    // The strike's clip is paired, so with nobody the search may grab nothing plays; the power meter is neither tested
    // nor spent (the hold's drain starts with the grab, as for any).
    if ((phaseFlags(input) & kSpecialRefusingPhases) != 0 || !input.grabTargetInReach) {
        return;
    }
    m_chain.cancel();
    m_mode = CombatMode::Grabbing;
    out.startAnim = anim_id::kGrabPlayerIntro;
    out.grabStarted = true;
    out.grappleAnim = m_rage.raging() ? anim_id::kStrongGrappleRage : anim_id::kStrongGrapple;
}

} // namespace coney::combat
