// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/reactions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace coney::combat {

namespace {

// The reaction table at `0x00510798`, index strength × 16 + height × 4 + direction (0 back, 1 left, 2 front,
// 3 right); -1 where the table has no entry.
constexpr std::array<int, 64> kReactions{// Strength 0: low (none), mid, high, unused.
                                         -1, -1, -1, -1, 270, 271, 268, 269, 274, 275, 272, 273, -1, -1, -1, -1,
                                         // Strength 1.
                                         286, 287, 284, 285, 278, 279, 276, 277, 282, 283, 280, 281, -1, -1, -1, -1,
                                         // Strength 2.
                                         286, 287, 284, 285, 290, 291, 288, 289, 294, 295, 292, 293, -1, -1, -1, -1,
                                         // Strength 3.
                                         286, 287, 284, 285, 298, 299, 296, 297, 302, 303, 300, 301, -1, -1, -1, -1};

// The block reactions at `0x00510898`: mid, then high, each back, left, front, right.
constexpr std::array<int, 8> kBlockReactions{614, 615, 612, 613, 610, 611, 608, 609};

// Combo ids (the chain's second and third hits) whose strength drops at a fresh victim.
constexpr int kFirstComboId = 13;
constexpr int kLastComboId = 20;
// The attacker clips that break a block.
constexpr int kFirstBlockBreaker = 26;
constexpr int kLastBlockBreaker = 34;

// The height change for an attacker standing `above` metres higher (negative: lower).
int heightStep(float above) {
    const float rise = std::fabs(above);
    int step = 0;
    if (rise >= 0.9F) {
        step = 2; // **Coney choice**: beyond 1.5 m counts as 0.9-1.5 m; the research gives no larger step.
    } else if (rise >= 0.3F) {
        step = 1;
    }
    return above < 0.0F ? -step : step;
}

// The table entry for a modified code, with the fallback.
int tableReaction(const HitCode& code) {
    const std::size_t index = (static_cast<std::size_t>(code.strength) * 16U) +
                              (static_cast<std::size_t>(code.height) * 4U) + static_cast<std::size_t>(code.direction);
    const int id = kReactions.at(index);
    return id < 0 ? kFallbackReaction : id;
}

} // namespace

HitCode decodeHitCode(int code) {
    const auto bits = static_cast<unsigned>(code);
    return HitCode{.direction = static_cast<int>(bits & 3U),
                   .height = static_cast<int>((bits >> 2U) & 3U),
                   .strength = static_cast<int>((bits >> 4U) & 3U)};
}

Side victimSide(const anim::Vec3& victim, float heading, const anim::Vec3& attacker) {
    // The attacker in the victim's frame: ahead along its facing (-sin, cos), right along (cos, sin).
    const float dx = attacker.x - victim.x;
    const float dy = attacker.y - victim.y;
    const float ahead = (-std::sin(heading) * dx) + (std::cos(heading) * dy);
    const float right = (std::cos(heading) * dx) + (std::sin(heading) * dy);
    return sideOf(std::atan2(right, ahead) * 180.0F / std::numbers::pi_v<float>);
}

Reaction hitReaction(const ReactionInput& input) {
    HitCode code = decodeHitCode(input.code);
    // 1. The strength: a combo hit is lighter on a fresh victim.
    const bool combo = input.attackAnim >= kFirstComboId && input.attackAnim <= kLastComboId;
    if (combo && !input.victimFlag400 && !input.victimHurt) {
        code.strength -= 1;
    }
    code.strength = std::clamp(code.strength, 0, 3);
    // 2. The height by where the attacker stands; a low hit is heavy.
    code.height = std::clamp(code.height + heightStep(input.attackerAbove), 0, 2);
    if (code.height == 0) {
        code.strength = 2;
    }
    // 3. The direction turned by the side.
    code.direction = (code.direction + static_cast<int>(input.side)) & 3;
    return Reaction{.animId = tableReaction(code), .code = code};
}

int deathReaction(const ReactionInput& input, bool dieSet) {
    Reaction reaction = hitReaction(input);
    reaction.code.strength = 2;
    return dieSet ? tableReaction(reaction.code) + kDeathOffset : kDeathFallback;
}

int blockReaction(int height, int direction) {
    const std::size_t row = height >= 2 ? 1U : 0U;
    return kBlockReactions.at((row * 4U) + static_cast<std::size_t>(direction & 3));
}

bool blockHolds(int strength, int attackAnim) {
    return strength < 3 && (attackAnim < kFirstBlockBreaker || attackAnim > kLastBlockBreaker);
}

} // namespace coney::combat
