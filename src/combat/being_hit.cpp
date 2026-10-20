// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/being_hit.h"

#include "animation/anim_task.h"

namespace coney::combat {

namespace {

// Clip events count frames at 30 a second.
constexpr float kEventFramesPerSecond = 30.0F;
// The attacks that ignore any hit armour.
constexpr int kFirstArmourBreaker = 617;
constexpr int kLastArmourBreaker = 620;
// The phases of record +0x08 that armour a player: the wind-up and the chain window.
constexpr std::uint32_t kArmouredPhases = 0x3;

} // namespace

std::optional<AttackWarning> warningOf(std::uint16_t type) {
    if (type == kDuckEvent) {
        return AttackWarning::Duck;
    }
    if (type == kEarlyBlockEvent) {
        return AttackWarning::EarlyBlock;
    }
    return std::nullopt;
}

std::optional<AttackWarning> warningBetween(const anim::AnimClip& clip, float fromSeconds, float toSeconds) {
    for (const anim::ClipEvent& event : clip.events) {
        const float at = static_cast<float>(event.frame) / kEventFramesPerSecond;
        if (at > fromSeconds && at <= toSeconds) {
            if (const auto warning = warningOf(event.type); warning.has_value()) {
                return warning;
            }
        }
    }
    return std::nullopt;
}

bool eventBetween(const anim::AnimClip& clip, std::uint16_t type, float fromSeconds, float toSeconds) {
    // The frames the cursor passed, rounded as the task stack fires a clip's events (anim::eventFrame()). The duck's
    // and the rise's 0x25 both reach the human through that one event dispatcher (`Anim_FireFrameEvents`, 0x00101dd8),
    // so its rounding holds for both (docs/research/combat-moves.md#reading,
    // docs/research/combat-reactions.md#ground-escape): at rate 0.75 the rise's frames 11-13 fire on its updates 15, 16
    // and 17, and none on 18, as at runtime.
    const int from = anim::eventFrame(fromSeconds);
    const int to = anim::eventFrame(toSeconds);
    for (const anim::ClipEvent& event : clip.events) {
        if (event.type == type && event.frame > from && event.frame <= to) {
            return true;
        }
    }
    return false;
}

bool asksDuckCounter(CommandId command) {
    return command == command::kSquarePressed || command == 0x11 || command == command::kSquareHeld ||
           command == command::kCrossLongHold || command == command::kCrossPressed || command == command::kCrossHeld;
}

int duckCounterClip(Side side) { return kDuckCounterFront + static_cast<int>(side); }

bool armourBreakingAttack(int animId) { return animId >= kFirstArmourBreaker && animId <= kLastArmourBreaker; }

bool hitArmourHolds(std::uint32_t attackPhase, int attackAnim, bool attackerIgnoresArmour) {
    return (attackPhase & kArmouredPhases) != 0 && !attackerIgnoresArmour && !armourBreakingAttack(attackAnim);
}

bool heldCancelsReaction(std::uint32_t held) { return (held & kReactionCancellingHeld) != 0; }

int flooredDamage(int health, int maximum, int damage, float floorFraction) {
    const auto floor = static_cast<int>(static_cast<float>(maximum) * floorFraction);
    if (health > floor && health - damage < floor) {
        return health - floor;
    }
    return damage;
}

BlockResult blockHit(const ReactionInput& input) {
    const Reaction reaction = hitReaction(input);
    if (!blockHolds(reaction.code.strength, input.attackAnim)) {
        return {};
    }
    return BlockResult{.holds = true, .reaction = blockReaction(reaction.code.height, reaction.code.direction)};
}

int mashCut(int groundMs, CombatRandom& random) {
    const auto divisor = static_cast<int>(((random.next() >> 8U) % 3U) + 1U);
    return groundMs / divisor;
}

} // namespace coney::combat
