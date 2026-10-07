// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/attack_choice.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "ai/attack_places.h"
#include "ai/fight_checks.h"

// The AI's attack choice, its spacing and its two waits, as pure decisions.
// Research: docs/research/ai.md#pick-attack, docs/research/ai.md#attack-places, docs/research/ai.md#check-attack

using coney::ai::AttackerView;
using coney::ai::AttackWeights;
using coney::ai::PickContext;
using coney::ai::TargetView;

namespace {

// Weights of `weight` for each kind in `kinds`, 0 for the rest.
AttackWeights weightsOf(std::initializer_list<int> kinds, std::uint8_t weight = 10) {
    AttackWeights weights{};
    for (const int kind : kinds) {
        weights[static_cast<std::size_t>(kind)] = weight;
    }
    return weights;
}

// How often each kind comes out of `draws` picks.
std::map<int, int> tally(const AttackWeights& weights, const PickContext& context, int draws = 2000) {
    coney::combat::CombatRandom random(7);
    std::map<int, int> counts;
    for (int i = 0; i < draws; ++i) {
        const std::optional<int> kind =
            coney::ai::pickAttackKind(weights, context, [](int /*kind*/) { return true; }, random);
        counts[kind.value_or(coney::ai::kNoAttackKind)]++;
    }
    return counts;
}

} // namespace

TEST_CASE("the per-kind test follows the original's table", "[ai]") {
    AttackerView a;
    TargetView t;
    // The strikes need A free and T standing.
    CHECK(coney::ai::canUseAttackKind(a, t, 2));
    t.downOrOut = true;
    CHECK_FALSE(coney::ai::canUseAttackKind(a, t, 2));
    t.downOrOut = false;
    // The charge and the dive need the run speed.
    CHECK_FALSE(coney::ai::canUseAttackKind(a, t, 19));
    a.atRunSpeed = true;
    CHECK(coney::ai::canUseAttackKind(a, t, 19));
    CHECK(coney::ai::canUseAttackKind(a, t, 20));
    // The grab needs a fifth of the power meter and a grabbable target.
    CHECK_FALSE(coney::ai::canUseAttackKind(a, t, 22));
    a.grabPower = true;
    CHECK(coney::ai::canUseAttackKind(a, t, 22));
    t.ungrabbable = true;
    CHECK_FALSE(coney::ai::canUseAttackKind(a, t, 22));
    CHECK_FALSE(coney::ai::canUseAttackKind(a, t, 21));
    t.ungrabbable = false;
    // The snap needs a slot on a human beside or behind.
    CHECK_FALSE(coney::ai::canUseAttackKind(a, t, 10));
    a.snapTargetAside = true;
    CHECK(coney::ai::canUseAttackKind(a, t, 10));
    // The moves in a grab need A grabbing T.
    CHECK_FALSE(coney::ai::canUseAttackKind(a, t, 24));
    a.grabbing = true;
    a.holdMovesFree = true;
    a.holdsTarget = true;
    CHECK(coney::ai::canUseAttackKind(a, t, 24));
    CHECK(coney::ai::canUseAttackKind(a, t, 25));
    // Kind 42 from the ground needs the Grounded goal; kinds 18 and past 44 never.
    a.knockedDown = true;
    CHECK_FALSE(coney::ai::canUseAttackKind(a, t, 42));
    a.groundedGoal = true;
    CHECK(coney::ai::canUseAttackKind(a, t, 42));
    CHECK_FALSE(coney::ai::canUseAttackKind(a, t, 45));
}

TEST_CASE("the start test adds the guard and the reach to the per-kind test", "[ai]") {
    const AttackerView a;
    const TargetView t;
    coney::ai::StartGuard guard;
    CHECK(coney::ai::canStartAttack(guard, a, t, 0));
    guard.inReach = false;
    CHECK_FALSE(coney::ai::canStartAttack(guard, a, t, 0));
    guard.inReach = true;
    guard.targetHeldBusy = true;
    CHECK_FALSE(coney::ai::canStartAttack(guard, a, t, 0));
    guard.targetPinned = true;
    CHECK(coney::ai::canStartAttack(guard, a, t, 0));
    guard.damagePending = true;
    CHECK_FALSE(coney::ai::canStartAttack(guard, a, t, 0));
}

TEST_CASE("a running AI picks only the charge kinds, a standing one never the charge or dive", "[ai]") {
    const AttackWeights weights = weightsOf({0, 1, 19, 20, 21});
    PickContext context;
    context.running = true;
    for (const auto& [kind, count] : tally(weights, context)) {
        CHECK(coney::ai::isChargeKind(kind));
        CHECK(count > 0);
    }
    context.running = false;
    const std::map<int, int> standing = tally(weights, context);
    CHECK_FALSE(standing.contains(19));
    CHECK_FALSE(standing.contains(20));
    CHECK(standing.contains(1));
}

TEST_CASE("the draw's adjustments", "[ai]") {
    // Two kinds of equal weight: a rear-grabbed target adds 200 to the grab.
    const AttackWeights weights = weightsOf({1, 22});
    PickContext context;
    context.targetRearGrabbed = true;
    const std::map<int, int> rear = tally(weights, context);
    CHECK(rear.at(22) > 10 * rear.at(1));
    // A crowd of four on A takes the tackle's weight to nothing.
    const AttackWeights tackle = weightsOf({1, 21});
    PickContext crowd;
    crowd.ownSlotsTaken = 4;
    crowd.ownSlotsMax = 4;
    CHECK_FALSE(tally(tackle, crowd).contains(21));
    // A target that cannot be chased: X1 with an object to swing, else nothing.
    PickContext unchased;
    unchased.targetChasable = false;
    CHECK(tally(weights, unchased, 10).at(coney::ai::kNoAttackKind) == 10);
    unchased.swingsObject = true;
    CHECK(tally(weights, unchased, 10).at(0) == 10);
    // No weight at all: none.
    CHECK(tally(AttackWeights{}, PickContext{}, 10).at(coney::ai::kNoAttackKind) == 10);
}

TEST_CASE("a target's spacing shares one swing among those allowed to swing", "[ai]") {
    coney::ai::Spacing spacing;
    CHECK(spacing.inUse(false) == 1);
    spacing.raise(2, 3, false);
    CHECK(spacing.inUse(false) == 2);
    CHECK(spacing.inUse(true) == 3);
    // A Warrior target keeps one swinging at a time on his feet.
    coney::ai::Spacing warrior;
    warrior.raise(2, 2, true);
    CHECK(warrior.inUse(false) == 1);
    spacing.reset();
    CHECK(spacing.inUse(true) == 1);
    CHECK(coney::ai::attackableGapMs(600, 2) == 300);
    CHECK(coney::ai::attackableGapMs(600, 0) == 600);
    // The clips that time a kind.
    CHECK(coney::ai::swingClipsOf(0, false).ids[0] == 11);
    CHECK(coney::ai::swingClipsOf(0, true).ids[0] == 194);
    CHECK(coney::ai::swingClipsOf(1, true).count == 0);
    CHECK(coney::ai::swingClipsOf(21, false).sum);
}

TEST_CASE("active-attacker places", "[ai]") {
    coney::ai::ActivePlaces places;
    const int a = 0;
    const int b = 0;
    const int c = 0;
    // Nobody swings and the target may not be attacked yet: no place.
    CHECK_FALSE(places.claim(&a, 2, false, false));
    CHECK(places.claim(&a, 2, true, false));
    // Someone already swings: a second place among the first two even before the time.
    CHECK(places.claim(&b, 2, false, false));
    CHECK_FALSE(places.claim(&c, 2, true, false));
    // His own target displaces the first holder once he may be attacked.
    CHECK(places.claim(&c, 2, true, true));
    CHECK_FALSE(places.holds(&a));
    places.release(&b);
    CHECK(places.held() == 1);
}

TEST_CASE("may A attack now, and the ring he waits in", "[ai]") {
    namespace check = coney::ai::check;
    coney::ai::CheckAttackInput input;
    bool asked = false;
    const auto place = [&asked] {
        asked = true;
        return true;
    };
    input.cooledDown = false;
    CHECK(coney::ai::checkAttack(input, place) == check::kCoolingDown);
    input.cooledDown = true;
    input.policeHaveHim = true;
    CHECK(coney::ai::checkAttack(input, place) == check::kPolice);
    CHECK_FALSE(asked);
    input.policeHaveHim = false;
    CHECK(coney::ai::checkAttack(input, place) == check::kGo);
    CHECK(asked);
    // No place: a grab from behind still goes.
    input.grabKind = true;
    input.behindTarget = true;
    CHECK(coney::ai::checkAttack(input, [] { return false; }) == check::kGo);

    coney::ai::RepositionInput ring;
    ring.reason = check::kNoKind;
    ring.targetRadius = 0.4F;
    const coney::ai::RepositionRing far = coney::ai::repositionRing(ring);
    CHECK(far.inner == 4.0F);
    CHECK(far.outer == 4.75F);
    CHECK(far.limitMs == 2000);
    ring.targetFacesMe = true;
    const coney::ai::RepositionRing close = coney::ai::repositionRing(ring);
    CHECK(close.inner == 0.8F);
    ring.reason = check::kPolice;
    const coney::ai::RepositionRing police = coney::ai::repositionRing(ring);
    CHECK(police.inner == 15.0F);
    CHECK(police.limitMs == 6000);
    // The taunt: not close in, and T far enough.
    ring.reason = check::kNoKind;
    ring.targetFacesMe = false;
    CHECK(coney::ai::repositionTaunts(ring, 3.0F, 0, true, 50));
    CHECK_FALSE(coney::ai::repositionTaunts(ring, 2.0F, 0, true, 50));
}

TEST_CASE("the tackle meter fills on a fleeing target and fires past the gang's threshold", "[ai]") {
    coney::ai::TackleMeter meter;
    for (int i = 0; i < 10; ++i) {
        meter.think(true, 4);
    }
    CHECK(meter.value() == coney::ai::TackleMeter::kCap);
    CHECK_FALSE(meter.ready(1));
    CHECK(meter.ready(2));
    CHECK_FALSE(meter.ready(0));
    meter.think(false, 4);
    CHECK(meter.value() == coney::ai::TackleMeter::kCap - 2);
}
