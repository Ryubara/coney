// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/attack_choice.h"

#include <cstddef>
#include <vector>

namespace coney::ai {

namespace {

// The class whose kind-4/6 object keeps it from the specials (16-18).
constexpr int kClassKeepsObjectFromSpecials = 0x77;
// The armed bonus applies to the strikes and their chains, kinds 0-9.
constexpr int kLastStrikeKind = 9;
// A rear-grabbed target's bonus to the grab.
constexpr int kRearGrabbedBonus = 200;
// The kinds the grab and the throw from a grab are (the crowd and busy-target rules).
constexpr int kGrabKind = 22;

// The plain strikes' test (kinds 2-9 and 11): A free, and T not down, out of the fight or arrested.
bool strikeAllowed(const AttackerView& a, const TargetView& t) { return a.free && !t.downOrOut; }

// One candidate of the draw.
struct Candidate {
    int kind = kNoAttackKind;
    int weight = 0;
};

// Whether the draw's running rule keeps `kind`: while running only the charge kinds, and never the charge and the
// dive (19, 20) when not.
bool runningRuleKeeps(int kind, bool running) {
    if (running) {
        return isChargeKind(kind);
    }
    return kind != 19 && kind != 20;
}

// The weight of `kind` after the crowd, busy-target, armed, rear-grab and pattern rules, truncated.
int adjustedWeight(int kind, int weight, const PickContext& c) {
    // A crowd round A favours the snap and the throw from a grab and gives up the tackles and the strike in a grab.
    if (c.ownSlotsTaken > 1 && c.ownSlotsMax > 0) {
        const float share = static_cast<float>(c.ownSlotsTaken) / static_cast<float>(c.ownSlotsMax);
        if (kind == 10 || kind == 25 || kind == 36) {
            weight += static_cast<int>(static_cast<float>(weight) * share);
        } else if (kind == 14 || kind == 21 || kind == 24) {
            weight = static_cast<int>(static_cast<float>(weight) * (1.0F - share));
        }
    }
    // Several attackers on T make the grab likelier.
    if (kind == kGrabKind && c.targetSlotsTaken > 1 && c.targetSlotsMax > 0) {
        weight += static_cast<int>(static_cast<float>(weight) * static_cast<float>(c.targetSlotsTaken) /
                                   static_cast<float>(c.targetSlotsMax));
    }
    if (c.armed && kind <= kLastStrikeKind && weight != 0) {
        weight += c.armedBonus;
    }
    if (c.targetRearGrabbed && kind == kGrabKind && weight != 0) {
        weight += kRearGrabbedBonus;
    }
    // The player's pattern makes the three-hit enders likelier.
    if (c.pattern.has_value() && kind >= 6 && kind <= 9) {
        weight += static_cast<int>(static_cast<float>(weight) * *c.pattern);
    }
    return weight;
}

} // namespace

bool isChargeKind(int kind) { return kind == 0 || (kind >= 19 && kind <= 21); }

bool canUseAttackKind(const AttackerView& a, const TargetView& t, int kind) {
    switch (kind) {
    case 0:
    case 1:
        // A swung world object of type 1-3 needs A free only.
        if (a.heldObjectType >= 1 && a.heldObjectType <= 3) {
            return a.free;
        }
        return strikeAllowed(a, t);
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 11:
        return strikeAllowed(a, t);
    case 10:
        return a.free && a.snapTargetAside;
    case 12:
    case 13:
        return !t.isAttacker && (t.highOrBusy || (!t.state2000 && t.held400000)) && a.free;
    case 14:
        return t.isAttacker || (t.knockedDown && a.free);
    case 15:
        return a.free && (a.holdsSprayPaint || !a.padControlled);
    case 16:
    case 17:
    case 18:
        return a.free && !t.specialRefused && !(a.characterClass == kClassKeepsObjectFromSpecials && a.holdsKind4or6);
    case 19:
    case 20:
        return a.free && t.notDown && a.atRunSpeed;
    case 21:
        return a.free && !t.ungrabbable && t.free && !t.heldFlag40;
    case 22:
        return a.free && a.grabPower && !t.ungrabbable && !t.grabRefused;
    case 23:
        return a.holdsThrowable;
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
        return a.grabbing && a.holdMovesFree && a.holdsTarget;
    case 31:
    case 33:
    case 34:
        return a.grabbed && a.holdMovesFree && a.heldByTarget;
    case 32:
        return a.grabbedFromRear && a.holdMovesFree && !a.heldByTarget;
    case 35:
    case 36:
    case 37:
    case 38:
    case 39:
    case 40:
        return a.tackling && a.holdMovesFree && a.holdsTarget;
    case 41:
        return a.tackling && a.holdMovesFree && a.holdsTarget && a.carriesCuffs;
    case 42:
    case 43:
    case 44:
        if (a.knockedDown && a.groundedGoal) {
            return true;
        }
        return a.tackled && a.holdMovesFree && a.heldByTarget;
    default:
        return false;
    }
}

bool canStartAttack(const StartGuard& guard, const AttackerView& attacker, const TargetView& target, int kind) {
    if ((guard.targetHeldBusy && !guard.targetPinned) || guard.attackerHeldBusy || guard.damagePending ||
        !guard.inReach) {
        return false;
    }
    const AttackerView& a = attacker;
    const TargetView& t = target;
    switch (kind) {
    case 10:
        return a.free;
    case 12:
    case 13:
        return t.isAttacker || (t.highOrBusy && a.free && !a.held48000);
    case 14:
        return a.free && t.knockedDown;
    case 15:
        return a.free && !t.specialRefused && (a.holdsSprayPaint || !a.padControlled);
    case 21:
        return canUseAttackKind(a, t, kind) && a.grabPower;
    case 22:
        return canUseAttackKind(a, t, kind) && !t.heldC12200 && (!t.rearGrabbed || t.attackerInFront);
    case 23:
        return a.free && a.holdsThrowable;
    case 27:
    case 28:
        return a.grabbing && a.holdsTarget;
    case 38:
    case 39:
        return a.tackling && a.holdsTarget;
    default:
        return canUseAttackKind(attacker, target, kind);
    }
}

std::optional<int> pickAttackKind(const AttackWeights& weights, const PickContext& context,
                                  const std::function<bool(int)>& filter, combat::CombatRandom& random) {
    // A target that cannot be approached: an object is swung at him (X1), else nothing.
    if (!context.targetChasable) {
        return context.swingsObject ? std::optional<int>(0) : std::nullopt;
    }
    std::vector<Candidate> candidates;
    int total = 0;
    for (int kind = 0; kind < static_cast<int>(kAttackKinds); ++kind) {
        if (!filter(kind) || !runningRuleKeeps(kind, context.running)) {
            continue;
        }
        const bool movesWhileHeld = (kind >= 31 && kind <= 34) || (kind >= 42 && kind <= 44);
        if (movesWhileHeld && context.hurtAndAttackable) {
            continue;
        }
        const int weight = adjustedWeight(kind, weights[static_cast<std::size_t>(kind)], context);
        candidates.push_back(Candidate{.kind = kind, .weight = weight});
        total += weight;
    }
    if (total <= 0) {
        return std::nullopt;
    }
    // The first candidate whose running sum exceeds the draw.
    const int draw = rollRange(random, 0, total - 1);
    int sum = 0;
    for (const Candidate& candidate : candidates) {
        sum += candidate.weight;
        if (sum > draw) {
            return candidate.kind;
        }
    }
    return std::nullopt;
}

} // namespace coney::ai
