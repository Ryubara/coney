// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "combat/anim_ids.h"
#include "combat/meters.h"
#include "human/combatant.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/victim.h"
#include "support/fight_fixtures.h"

// The target searches' passes over the humans, the cars, the world objects and the glass panes, in the original's
// order: `Player_PickTarget` 0x0027a6c0, `Player_FindAttackTarget` 0x0027a4b0, `Attack_FindNearestInReach` 0x0027b058,
// and the two object filters `Target_ObjectFilter` 0x0027a120 and `Target_GlassFilter` 0x0027a070
// (docs/research/combat-targets.md#targets). Issue #1011. The searcher stands at the origin facing +y (heading 0) with
// the stick at rest, so every search runs along +y; +x is his right.

using coney::anim::Vec3;
using coney::human::Combatant;
using coney::human::Fighter;
using coney::human::FighterInput;
using coney::human::ObjectKind;
using coney::human::ObjectTarget;
using coney::human::TargetPick;
using coney::human::TargetState;
using coney::test::Fight;
using coney::test::FightCharacter;
namespace id = coney::combat::anim_id;

namespace {

// A world object's body flags with the strike-target bit 0x8 (`Object_IsStrikeTarget` tests 0x38).
constexpr std::uint32_t kStrikeBody = 0x80000508U;

// A human to search for: where he stands and how he lies, with full health.
class Dummy final : public Combatant {
  public:
    explicit Dummy(Vec3 at, TargetState state = TargetState::Standing) : m_at(at), m_state(state) {}

    [[nodiscard]] Vec3 position() const override { return m_at; }
    [[nodiscard]] float heading() const override { return 0.0F; }
    [[nodiscard]] TargetState state() const override { return m_state; }
    [[nodiscard]] const coney::combat::Health& health() const override { return m_health; }
    void hit(const coney::human::IncomingHit& /*hit*/) override {}
    void warn(const coney::human::AttackNotice& /*notice*/) override {}

  private:
    Vec3 m_at;
    TargetState m_state;
    coney::combat::Health m_health{600};
};

// An object of `kind` at `point` named `handle`; a world object's body makes it a strike target.
ObjectTarget object(double handle, Vec3 point, ObjectKind kind) {
    return ObjectTarget{.handle = handle,
                        .point = point,
                        .kind = kind,
                        .bodyFlags = kind == ObjectKind::WorldObject ? kStrikeBody : 0U,
                        .hasBody = true};
}

// The search's input: the searcher at the origin facing +y, the humans `targets` and the objects `objects`.
FighterInput searchInput(const std::vector<Combatant*>& targets, const std::vector<ObjectTarget>& objects) {
    FighterInput input;
    input.position = Vec3{0.0F, 0.0F, 0.0F};
    input.heading = 0.0F;
    input.targets = targets;
    input.objects = objects;
    return input;
}

// The handle of the object a search found, or nothing.
std::optional<double> handleOf(const TargetPick& pick) {
    return pick.object != nullptr ? std::optional<double>{pick.object->handle} : std::nullopt;
}

} // namespace

TEST_CASE("the pick takes a world object ahead before a downed human beside the player", "[human][combat][targeting]") {
    // Expected: combat-targets.md#targets, confirmed (code)
    // The objects' passes run before the 135° pass that takes the downed: a world object 1.5 m ahead wins over a
    // human down 1.0 m to the right, whom the wide pass (2.0 × 0.7 = 1.4 m) alone would find.
    Dummy downed(Vec3{1.0F, 0.0F, 0.0F}, TargetState::Grounded);
    const std::vector<Combatant*> targets{&downed};
    const std::vector<ObjectTarget> objects{object(7, Vec3{0.0F, 1.5F, 0.5F}, ObjectKind::WorldObject)};
    const TargetPick pick = Fighter::pickTargetOrObject(searchInput(targets, objects), 2.0F);
    CHECK(pick.human == nullptr);
    CHECK(handleOf(pick) == 7.0);
    // Square's pick seen as a human (`Human_GetTarget` of an object) is no one.
    CHECK(Fighter::pickTarget(searchInput(targets, objects), 2.0F) == nullptr);
}

TEST_CASE("the pick's car pass comes after the enemies ahead and ends the search before the objects and panes",
          "[human][combat][targeting]") {
    // Expected: combat-targets.md#targets, confirmed (code)
    const std::vector<ObjectTarget> objects{object(1, Vec3{0.0F, 0.6F, 1.0F}, ObjectKind::Glass),
                                            object(2, Vec3{0.0F, 0.8F, 0.5F}, ObjectKind::WorldObject),
                                            object(3, Vec3{0.0F, 1.2F, 0.5F}, ObjectKind::Car)};
    const std::vector<Combatant*> none;
    CHECK(handleOf(Fighter::pickTargetOrObject(searchInput(none, objects), 2.0F)) == 3.0);
    // An enemy within range × 1.1 (2.2 m) and 54° comes first.
    Dummy enemy(Vec3{0.0F, 2.1F, 0.0F});
    const std::vector<Combatant*> targets{&enemy};
    const TargetPick pick = Fighter::pickTargetOrObject(searchInput(targets, objects), 2.0F);
    CHECK(pick.human == &enemy);
    CHECK(pick.object == nullptr);
}

TEST_CASE("the pick searches the world objects, then the panes, then the humans at any angle",
          "[human][combat][targeting]") {
    // Expected: combat-targets.md#targets, confirmed (code)
    const std::vector<Combatant*> none;
    // A world object 1.9 m ahead wins over a pane 1.0 m ahead: its pass ends the search.
    const std::vector<ObjectTarget> both{object(4, Vec3{0.0F, 1.0F, 1.0F}, ObjectKind::Glass),
                                         object(5, Vec3{0.0F, 1.9F, 0.5F}, ObjectKind::WorldObject)};
    CHECK(handleOf(Fighter::pickTargetOrObject(searchInput(none, both), 2.0F)) == 5.0);
    // A pane 1.0 m ahead wins over an enemy 1.5 m to the right, whom the any-angle pass (1.8 m) would find.
    Dummy beside(Vec3{1.5F, 0.0F, 0.0F});
    const std::vector<Combatant*> targets{&beside};
    const std::vector<ObjectTarget> pane{object(4, Vec3{0.0F, 1.0F, 1.0F}, ObjectKind::Glass)};
    CHECK(handleOf(Fighter::pickTargetOrObject(searchInput(targets, pane), 2.0F)) == 4.0);
    // With no object in the cone the any-angle pass finds him.
    const std::vector<ObjectTarget> behind{object(4, Vec3{0.0F, -1.0F, 1.0F}, ObjectKind::Glass)};
    CHECK(Fighter::pickTargetOrObject(searchInput(targets, behind), 2.0F).human == &beside);
}

TEST_CASE("the pick measures an object's distance in three dimensions", "[human][combat][targeting]") {
    // Expected: combat-targets.md#targets, confirmed (runtime)
    // As in the original's yard: a world object 0.70 m above the feet is picked 1.85 m ahead (1.978 m away) and not
    // 1.90 m ahead (2.025 m away); at the feet's height it is picked 1.95 m ahead.
    const std::vector<Combatant*> none;
    const std::vector<ObjectTarget> near{object(1, Vec3{0.0F, 1.85F, 0.70F}, ObjectKind::WorldObject)};
    CHECK(handleOf(Fighter::pickTargetOrObject(searchInput(none, near), 2.0F)) == 1.0);
    const std::vector<ObjectTarget> past{object(1, Vec3{0.0F, 1.90F, 0.70F}, ObjectKind::WorldObject)};
    CHECK_FALSE(handleOf(Fighter::pickTargetOrObject(searchInput(none, past), 2.0F)));
    const std::vector<ObjectTarget> level{object(1, Vec3{0.0F, 1.95F, 0.0F}, ObjectKind::WorldObject)};
    CHECK(handleOf(Fighter::pickTargetOrObject(searchInput(none, level), 2.0F)) == 1.0);
}

TEST_CASE("the pick's 135 degree pass takes a world object within 0.8 x the range before a human at any angle",
          "[human][combat][targeting]") {
    // Expected: combat-targets.md#targets, confirmed (code)
    // A current target far behind keeps the any-angle pass out; a world object 1.5 m off to the right and a little
    // behind (100° off the aim) is inside 135° and 1.6 m, so it wins over an enemy 1.3 m away at 120° whom the wide
    // pass (1.4 m) would take.
    Dummy current(Vec3{0.0F, -9.0F, 0.0F});
    Dummy enemy(Vec3{1.126F, -0.65F, 0.0F});
    const std::vector<Combatant*> targets{&current, &enemy};
    const std::vector<ObjectTarget> objects{object(6, Vec3{1.477F, -0.26F, 0.5F}, ObjectKind::WorldObject)};
    CHECK(handleOf(Fighter::pickTargetOrObject(searchInput(targets, objects), 2.0F, &current)) == 6.0);
    // Beyond 1.6 m, or a pane (the 135° pass searches only the world objects), the wide pass takes the enemy.
    const std::vector<ObjectTarget> far{object(6, Vec3{1.674F, -0.295F, 0.5F}, ObjectKind::WorldObject)};
    CHECK(Fighter::pickTargetOrObject(searchInput(targets, far), 2.0F, &current).human == &enemy);
    const std::vector<ObjectTarget> pane{object(6, Vec3{1.477F, -0.26F, 0.5F}, ObjectKind::Glass)};
    CHECK(Fighter::pickTargetOrObject(searchInput(targets, pane), 2.0F, &current).human == &enemy);
}

TEST_CASE("the pick keeps the current target within range when nothing is found, downed or not",
          "[human][combat][targeting]") {
    // Expected: combat-targets.md#targets, confirmed (code)
    // Down 1.8 m behind: beyond the wide pass's 1.4 m, within the range.
    Dummy downed(Vec3{0.0F, -1.8F, 0.0F}, TargetState::Grounded);
    const std::vector<Combatant*> targets{&downed};
    const std::vector<ObjectTarget> none;
    CHECK(Fighter::pickTargetOrObject(searchInput(targets, none), 2.0F, &downed).human == &downed);
    CHECK(Fighter::pickTarget(searchInput(targets, none), 2.0F, &downed) == &downed);
    // Beyond the range he is not kept.
    Dummy away(Vec3{0.0F, -2.5F, 0.0F}, TargetState::Grounded);
    const std::vector<Combatant*> far{&away};
    CHECK(Fighter::pickTarget(searchInput(far, none), 2.0F, &away) == nullptr);
}

TEST_CASE("the pick keeps an object target the object filter still takes, before the enemies ahead",
          "[human][combat][targeting]") {
    // Expected: combat-targets.md#targets, confirmed (code)
    Dummy enemy(Vec3{0.0F, 0.8F, 0.0F});
    const std::vector<Combatant*> targets{&enemy};
    const std::vector<ObjectTarget> objects{object(8, Vec3{0.0F, 1.0F, 0.5F}, ObjectKind::WorldObject),
                                            object(9, Vec3{0.0F, 1.0F, 1.0F}, ObjectKind::Glass)};
    const TargetPick kept = Fighter::pickTargetOrObject(searchInput(targets, objects), 2.0F, nullptr, objects.data());
    CHECK(kept.human == nullptr);
    CHECK(handleOf(kept) == 8.0);
    // The object filter takes only world objects: a pane as the target is searched for again.
    CHECK(Fighter::pickTargetOrObject(searchInput(targets, objects), 2.0F, nullptr, &objects[1]).human == &enemy);
}

TEST_CASE("the object filter takes a world object with a strike-target body within the cone and 2 m in height",
          "[human][combat][targeting]") {
    // Expected: combat-targets.md#targets, confirmed (code)
    const Vec3 feet{0.0F, 0.0F, 0.0F};
    const ObjectTarget ahead = object(1, Vec3{0.0F, 1.0F, 0.5F}, ObjectKind::WorldObject);
    CHECK(coney::human::objectTargetable(ahead, feet, 0.0F, 54.0F));
    ObjectTarget plain = ahead;
    plain.bodyFlags = 0x80000500U;
    CHECK_FALSE(coney::human::objectTargetable(plain, feet, 0.0F, 54.0F));
    ObjectTarget broken = ahead;
    broken.hasBody = false;
    CHECK_FALSE(coney::human::objectTargetable(broken, feet, 0.0F, 54.0F));
    CHECK_FALSE(
        coney::human::objectTargetable(object(1, Vec3{0.0F, 1.0F, 2.1F}, ObjectKind::WorldObject), feet, 0.0F, 54.0F));
    // 60° off the aim: outside 54°, inside 135°.
    const ObjectTarget side = object(1, Vec3{0.866F, 0.5F, 0.5F}, ObjectKind::WorldObject);
    CHECK_FALSE(coney::human::objectTargetable(side, feet, 0.0F, 54.0F));
    CHECK(coney::human::objectTargetable(side, feet, 0.0F, 135.0F));
    CHECK_FALSE(
        coney::human::objectTargetable(object(1, Vec3{0.0F, 1.0F, 0.5F}, ObjectKind::Glass), feet, 0.0F, 54.0F));
}

TEST_CASE("the glass filter takes a pane with a body within the cone and 2 m in height", "[human][combat][targeting]") {
    // Expected: combat-targets.md#targets, confirmed (code)
    const Vec3 feet{0.0F, 0.0F, 0.0F};
    const ObjectTarget pane = object(1, Vec3{0.0F, 1.0F, 1.5F}, ObjectKind::Glass);
    CHECK(coney::human::glassTargetable(pane, feet, 0.0F, 54.0F));
    ObjectTarget broken = pane;
    broken.hasBody = false;
    CHECK_FALSE(coney::human::glassTargetable(broken, feet, 0.0F, 54.0F));
    CHECK_FALSE(coney::human::glassTargetable(object(1, Vec3{0.0F, 1.0F, 2.1F}, ObjectKind::Glass), feet, 0.0F, 54.0F));
    CHECK_FALSE(
        coney::human::glassTargetable(object(1, Vec3{0.866F, 0.5F, 1.5F}, ObjectKind::Glass), feet, 0.0F, 54.0F));
}

TEST_CASE("the attack search's object and pane passes come before its any-angle and wide passes",
          "[human][combat][targeting]") {
    // Expected: combat-targets.md#targeting, confirmed (code)
    Dummy downed(Vec3{1.0F, 0.0F, 0.0F}, TargetState::Grounded);
    const std::vector<Combatant*> targets{&downed};
    const std::vector<ObjectTarget> objects{object(2, Vec3{0.0F, 1.0F, 0.5F}, ObjectKind::WorldObject)};
    const TargetPick found = Fighter::attackSearch(searchInput(targets, objects), 2.0F, 0.0F, nullptr);
    CHECK(found.human == nullptr);
    CHECK(handleOf(found) == 2.0);
    const std::vector<ObjectTarget> pane{object(3, Vec3{0.0F, 1.5F, 1.0F}, ObjectKind::Glass)};
    CHECK(handleOf(Fighter::attackSearch(searchInput(targets, pane), 2.0F, 0.0F, nullptr)) == 3.0);
    // With neither in the cone, the wide pass finds the downed human.
    const std::vector<ObjectTarget> none;
    CHECK(Fighter::attackSearch(searchInput(targets, none), 2.0F, 0.0F, nullptr).human == &downed);
}

TEST_CASE("the attack search keeps the current target as it is when nothing is found", "[human][combat][targeting]") {
    // Expected: combat-targets.md#targeting, confirmed (code)
    // A human target 6 m behind, beyond the 2 m far range and down, is kept.
    Dummy current(Vec3{0.0F, -6.0F, 0.0F}, TargetState::Grounded);
    const std::vector<Combatant*> targets{&current};
    const std::vector<ObjectTarget> none;
    CHECK(Fighter::attackSearch(searchInput(targets, none), 2.0F, 0.0F, &current).human == &current);
    // An object target is kept when it is no car and has a body, wherever it stands.
    const std::vector<Combatant*> nobody;
    const std::vector<ObjectTarget> objects{object(4, Vec3{0.0F, -5.0F, 0.5F}, ObjectKind::WorldObject),
                                            object(5, Vec3{0.0F, -5.0F, 0.5F}, ObjectKind::Car)};
    CHECK(handleOf(Fighter::attackSearch(searchInput(nobody, objects), 2.0F, 0.0F, nullptr, objects.data())) == 4.0);
    CHECK_FALSE(handleOf(Fighter::attackSearch(searchInput(nobody, objects), 2.0F, 0.0F, nullptr, &objects[1])));
}

TEST_CASE("the charge and dive search falls back on world objects, then panes, between the reach and the far range",
          "[human][combat][targeting]") {
    // Expected: combat/index.md#charge-aim, confirmed (code)
    const std::vector<Combatant*> none;
    // The dive's band: 2.22 m to 3.0 m.
    const std::vector<ObjectTarget> pane{object(1, Vec3{0.0F, 2.5F, 1.0F}, ObjectKind::Glass)};
    CHECK(handleOf(Fighter::reachSearch(searchInput(none, pane), 2.22F, 3.0F)) == 1.0);
    const std::vector<ObjectTarget> near{object(1, Vec3{0.0F, 1.0F, 1.0F}, ObjectKind::Glass)};
    CHECK_FALSE(handleOf(Fighter::reachSearch(searchInput(none, near), 2.22F, 3.0F)));
    // A world object comes before a nearer pane, and a standing enemy before both.
    const std::vector<ObjectTarget> both{object(1, Vec3{0.0F, 2.5F, 1.0F}, ObjectKind::Glass),
                                         object(2, Vec3{0.0F, 2.8F, 0.5F}, ObjectKind::WorldObject)};
    CHECK(handleOf(Fighter::reachSearch(searchInput(none, both), 2.22F, 3.0F)) == 2.0);
    Dummy enemy(Vec3{0.0F, 2.9F, 0.0F});
    const std::vector<Combatant*> targets{&enemy};
    const TargetPick found = Fighter::reachSearch(searchInput(targets, both), 2.22F, 3.0F);
    CHECK(found.human == &enemy);
    CHECK(found.object == nullptr);
}

TEST_CASE("square strikes a pane 1.8 m ahead, within square's 2 m pick", "[human][combat][targeting]") {
    // Expected: combat-targets.md#targets, confirmed (code)
    // The pick's object passes search square's 2.0 m, not the object attack's far range (1.25 m in these ranges).
    const FightCharacter character;
    Fight fight(character, 30.0F);
    fight.human().setObjectTargets({object(92, Vec3{40.0F, 41.8F, 1.59F}, ObjectKind::Glass)});
    std::vector<std::uint32_t> played;
    std::vector<double> hits;
    fight.run("10 tap square\n", 40, [&](std::uint64_t) {
        played.push_back(fight.human().animator().animId());
        if (const std::optional<double> hit = fight.human().fighter().objectHit()) {
            hits.push_back(*hit);
        }
    });
    CHECK(std::ranges::find(played, static_cast<std::uint32_t>(id::kBreakObjectMid)) != played.end());
    CHECK(hits == std::vector<double>{92});
}
