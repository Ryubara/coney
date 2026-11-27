// SPDX-License-Identifier: GPL-3.0-or-later
// The player grabbing and tackling another human, not a sandbox target (human/holdable.h,
// docs/research/combat-grabs.md#grab, docs/research/combat-grabs.md#grab-posing): the grab holds the human, the
// reaction goal its brain gets follows its state, the hold's moves and the throw land on it, a human with flag 0x40
// cannot be grabbed, the grab-block flags stop a grab of their side, the power-out escape follows the victim's flags,
// a power move plays out with no power left and gives slow motion back, and a human gone from the level or freed by a
// script ends the hold. Synthetic clips; the player's stick and buttons come from input scripts.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ai/goal.h"
#include "ai/reaction_goals.h"
#include "camera/slow_motion.h"
#include "combat/anim_ids.h"
#include "combat/commands.h"
#include "combat/player_combat.h"
#include "combat/power_class.h"
#include "human/fighter.h"
#include "human/fighter_clips.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/human_flags.h"
#include "human/pair_placement.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::Human;
using coney::human::TargetState;
using coney::test::FightCharacter;
namespace combat = coney::combat;
namespace flag = coney::human::flag;

namespace {

// The player at (40, 40) facing +y on a floor, driven by a pad, and a human no player controls `ahead` metres in front
// of it, facing it, with 600 health. Each update steps the player (with the other human as its target, until it is
// taken out of the level), then the other human.
class HumanFight {
  public:
    HumanFight(const FightCharacter& character, float ahead, const std::vector<coney::test::Tri>& walls = {})
        : m_mesh(coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F), walls))),
          m_player(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                   &character.ranges),
          m_other(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                  &character.ranges) {
        m_player.spawn(m_mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
        m_other.setFighterProfile(
            coney::human::FighterProfile{.player = false, .powerClass = combat::kCivilianPowerClass, .health = 600});
        m_other.spawn(m_mesh.get(), Vec3{40.0F, 40.0F + ahead, 0.0F}, 180.0F);
        m_targets.push_back(&m_other);
    }

    // Runs `frames` updates of `script`, calling `each` after every update with its index.
    template <typename Each> void run(std::string_view script, std::uint64_t frames, Each each) {
        for (const coney::test::PadFrame& frame : coney::test::playScript(script, frames)) {
            const combat::CommandId command =
                m_matcher.update(frame.buttons, m_tables, combat::combatTuning().historyHoldSamples);
            const std::span<coney::human::Combatant* const> targets =
                m_removed ? std::span<coney::human::Combatant* const>{} : std::span{m_targets};
            if (!m_playerStopped) {
                m_player.step(coney::human::HumanInput{.stickX = frame.leftX,
                                                       .stickY = frame.leftY,
                                                       .cameraForward = Vec3{0.0F, 1.0F, 0.0F},
                                                       .sprintHeld = false,
                                                       .actionPressed = false,
                                                       .command = command,
                                                       .buttons = frame.buttons,
                                                       .targets = targets},
                              m_mesh.get());
            }
            if (!m_removed) {
                const std::span<coney::human::Combatant* const> otherTargets =
                    otherFightsPlayer ? std::span{m_playerTarget} : std::span<coney::human::Combatant* const>{};
                const std::pair<float, float> stick = otherStick ? otherStick(m_frame) : std::pair{0.0F, 0.0F};
                m_other.step(
                    coney::human::HumanInput{.stickX = stick.first,
                                             .stickY = stick.second,
                                             .cameraForward = Vec3{0.0F, 1.0F, 0.0F},
                                             .sprintHeld = false,
                                             .actionPressed = false,
                                             .command = otherCommand ? otherCommand(m_frame) : combat::command::kNone,
                                             .buttons = 0,
                                             .targets = otherTargets},
                    m_mesh.get());
            }
            each(m_frame++);
        }
    }
    // Runs `frames` updates of `script`.
    void run(std::string_view script, std::uint64_t frames) {
        run(script, frames, [](std::uint64_t /*frame*/) {});
    }
    // Takes the other human out of the level: no longer a target, no longer stepped.
    void removeOther() { m_removed = true; }
    // The player is no longer stepped (taken out of the update); the other human goes on.
    void stopPlayer() { m_playerStopped = true; }
    // Places `human` afresh at `feet`, as a teleport does (Human::spawn()).
    void place(Human& human, Vec3 feet) { human.spawn(m_mesh.get(), feet, 0.0F); }

    Human& player() { return m_player; }
    Human& other() { return m_other; }
    // The other human's command on each update (its brain's, in play): none when empty.
    std::function<combat::CommandId(std::uint64_t)> otherCommand;
    // The other human's stick on each update (x, y in the world, as its brain writes it): at rest when empty.
    std::function<std::pair<float, float>(std::uint64_t)> otherStick;
    // The other human has the player as its target (an AI's attacks land on him).
    bool otherFightsPlayer = false;
    // The damage the other human has taken.
    [[nodiscard]] int damageTaken() const {
        return m_other.fighter().health().maximum() - m_other.fighter().health().value();
    }

  private:
    std::unique_ptr<coney::raycast::CollisionMesh> m_mesh;
    Human m_player;
    Human m_other;
    std::vector<coney::human::Combatant*> m_targets;
    std::array<coney::human::Combatant*, 1> m_playerTarget{&m_player};
    combat::CommandTables m_tables = combat::CommandTables::street();
    combat::CommandMatcher m_matcher;
    bool m_removed = false;
    bool m_playerStopped = false;
    std::uint64_t m_frame = 0;
};

// Whether `human`'s state calls for a reaction goal of `type` first (reactionGoalFor()).
bool reactsWith(const Human& human, coney::ai::GoalType type) {
    const auto goal = coney::ai::reactionGoalFor(human);
    return goal != nullptr && goal->type() == type;
}

} // namespace

TEST_CASE("the player grabs another human, strikes it in the hold and throws it", "[human][combat]") {
    // Expected: combat-grabs.md#grabbing, confirmed (runtime)
    const FightCharacter character;
    HumanFight fight(character, 1.5F);
    TargetState held = TargetState::Standing;
    bool grabbedReaction = false;
    int damageAfterStrike = 0;
    bool attached = false;
    Vec3 local;
    fight.run("5 tap circle\n30 tap square\n60 stick left 0 70\n61 tap circle\n63 stick left 0 0\n", 80,
              [&](std::uint64_t frame) {
                  if (frame == 20) {
                      held = fight.other().state();
                      grabbedReaction = reactsWith(fight.other(), coney::ai::GoalType::ReactGrabbed);
                  }
                  if (!attached && fight.other().attached()) {
                      attached = true;
                      local = coney::human::toFrame(fight.player().position(), fight.player().heading(),
                                                    fight.other().position());
                  }
                  if (frame == 50) {
                      damageAfterStrike = fight.damageTaken();
                  }
              });
    CHECK(held == TargetState::Held);
    CHECK(grabbedReaction);
    REQUIRE(attached);
    // Attached at the front hold's offset (0.380, 1.012) in the grabber's frame.
    CHECK(local.x == Approx(0.38F).margin(0.01F));
    CHECK(local.y == Approx(1.01F).margin(0.01F));
    CHECK(damageAfterStrike == 57);
    // The throw (147) puts it on the ground, 66 more, and the hold is over.
    CHECK(fight.other().state() == TargetState::Grounded);
    CHECK(fight.damageTaken() == 57 + 66);
    CHECK_FALSE(fight.other().fighter().holdState().has_value());
    CHECK(fight.player().fighter().held() == nullptr);
    CHECK(fight.player().fighter().combat().mode() == combat::CombatMode::Free);
    // After its time on the ground it gets up.
    fight.run("", 90);
    CHECK(fight.other().state() == TargetState::Standing);
}

TEST_CASE("the full stick drags a held human with the grabber, at the hold's offset", "[human][combat]") {
    // Expected: combat-grabs.md#hold-walk, confirmed (code)
    const FightCharacter character;
    HumanFight fight(character, 1.5F);
    fight.run("5 tap circle\n", 40);
    REQUIRE(fight.player().fighter().pairStage() == coney::human::PairStage::Attached);
    REQUIRE(fight.other().attached());
    // Full right: the grabber turns its back to +x and walks there, the held human swinging round with it.
    const Vec3 before = fight.player().position();
    const Vec3 otherBefore = fight.other().position();
    fight.run("0 stick left 100 0\n", 20);
    CHECK(fight.player().position().x > before.x + 0.3F);
    CHECK(std::hypot(fight.other().position().x - otherBefore.x, fight.other().position().y - otherBefore.y) > 0.5F);
    const Vec3 local =
        coney::human::toFrame(fight.player().position(), fight.player().heading(), fight.other().position());
    CHECK(local.x == Approx(0.38F).margin(0.01F));
    CHECK(local.y == Approx(1.01F).margin(0.01F));
}

TEST_CASE("a held human whose hold point lies through a wall is swept against it, and the grabber gives way",
          "[human][combat]") {
    // Expected: combat-grabs.md#grab-posing, confirmed (code)
    // Human_MoveAttached (0x00244e78) sweeps the held body to the hold's point against the level; what it falls short
    // by moves the grabber back. A wall facing -x at x = 40.5, 0.5 m to the right of both bodies: the front hold's
    // point is 0.38 m to the grabber's right, so the held human (a 0.35 m sphere) stops at the wall's face, short of
    // the point, and the grabber at x = 40 gives way until the offset holds again.
    const FightCharacter character;
    constexpr float kWallX = 40.5F;
    constexpr float kHeldRadius = 0.35F;
    HumanFight fight(character, 1.5F, coney::test::wallFacingMinusX(kWallX, 30.0F, 50.0F, 0.0F, 3.0F));
    float farthest = 0.0F;
    bool attached = false;
    fight.run("5 tap circle\n", 40, [&](std::uint64_t /*frame*/) {
        attached = attached || fight.other().attached();
        farthest = std::max(farthest, fight.other().position().x);
    });
    REQUIRE(attached);
    REQUIRE(fight.player().fighter().pairStage() == coney::human::PairStage::Attached);
    // Never into the wall: the sphere's centre stays its radius from the face.
    CHECK(farthest <= kWallX - kHeldRadius + 0.01F);
    // The grabber has moved away from the wall, so the held human is at the hold's offset again.
    CHECK(fight.player().position().x < 40.0F - 0.2F);
    const Vec3 local =
        coney::human::toFrame(fight.player().position(), fight.player().heading(), fight.other().position());
    CHECK(local.x == Approx(0.38F).margin(0.03F));
    CHECK(local.y == Approx(1.01F).margin(0.03F));
}

TEST_CASE("a held human put back after a fall out of the world is let go and stands on the ground", "[human][combat]") {
    // Expected: combat-grabs.md#pair-break, confirmed (code)
    // The put-back is Coney's (teleports.md#out-of-world); it places the human, and every placement breaks the pair
    // (Human_BreakPair from Human_SetTransform), so no hold outlives it, and he stands on the ground.
    const FightCharacter character;
    HumanFight fight(character, 1.5F);
    fight.run("5 tap circle\n", 40);
    REQUIRE(fight.player().fighter().held() == &fight.other());
    fight.other().putBack(nullptr, Vec3{30.0F, 30.0F, 0.0F}, 0.0F);
    CHECK_FALSE(fight.other().fighter().holdState().has_value());
    CHECK_FALSE(fight.other().attached());
    CHECK_FALSE(fight.other().airborne());
    fight.run("", 1);
    CHECK(fight.player().fighter().held() == nullptr);
    CHECK_FALSE(fight.other().fighter().inPair());
    CHECK_FALSE(fight.other().airborne());
    CHECK(fight.other().position().x == Approx(30.0F).margin(0.05F));
}

TEST_CASE("circle held tackles another human: it is mounted and its reaction goal is the tackled one",
          "[human][combat]") {
    // Expected: combat-grabs.md#grab, confirmed (code) (the tackled victim's reaction goal: ai/index.md#reaction-goals)
    const FightCharacter character;
    HumanFight fight(character, 2.0F);
    fight.run("5 press circle\n15 release circle\n50 tap square\n", 70);
    CHECK(fight.other().state() == TargetState::Mounted);
    CHECK(reactsWith(fight.other(), coney::ai::GoalType::ReactTackled));
    CHECK(fight.damageTaken() == 61); // the mount's square, 219 or 221
}

TEST_CASE("a tackled player keeps all his health and is never down by state, under the tackle or breaking free",
          "[human][combat]") {
    // Expected: combat-reactions.md#down-health. A tackled human holds state 0x800, which is not among
    // Human_IsDownOrDead's (0x00227e60) states 0x180050000, and the tackle writes no health. Confirmed (runtime), run
    // r-20261122-tackle-escape: tackled from update 30 (state 0x805) to 150, breaking free with circle (246 from 103),
    // at 900 health and never down by state.
    const FightCharacter character;
    HumanFight fight(character, 2.0F);
    fight.otherFightsPlayer = true;
    fight.otherCommand = [](std::uint64_t frame) {
        return frame == 2 ? combat::command::kCircleHeld : combat::command::kNone;
    };
    const combat::Health& health = fight.player().fighter().health();
    const int full = health.value();
    int mountedUpdates = 0;
    int downUpdates = 0;
    // The AI's tackle, then the mount with no input.
    fight.run("", 80, [&](std::uint64_t /*frame*/) {
        mountedUpdates += fight.player().state() == TargetState::Mounted ? 1 : 0;
        downUpdates += fight.player().downOrDead() ? 1 : 0;
    });
    REQUIRE(fight.player().state() == TargetState::Mounted);
    REQUIRE(mountedUpdates > 0);
    // At a quarter of its power or less the mounter lets circle break the tackle at once (Grab_CanEscape, 0x00225830).
    combat::PowerMeter& power = fight.other().fighter().combat().power();
    power.set(power.maximum() / 8);
    fight.run("0 tap circle\n", 120,
              [&](std::uint64_t /*frame*/) { downUpdates += fight.player().downOrDead() ? 1 : 0; });
    CHECK(fight.player().state() == TargetState::Standing);
    CHECK(downUpdates == 0);
    CHECK(health.value() == full);
    CHECK_FALSE(health.depleted());
}

TEST_CASE("a human with flag 0x40 cannot be grabbed: the grab misses", "[human][combat]") {
    // Expected: combat-reactions.md#busy-target, confirmed (code)
    const FightCharacter character;
    HumanFight fight(character, 1.5F);
    fight.other().setFlag(flag::kUngrabbable, true);
    bool missed = false;
    fight.run("5 tap circle\n", 40, [&](std::uint64_t /*frame*/) {
        CHECK(fight.other().state() == TargetState::Standing);
        missed = missed || fight.player().animator().animId() == combat::anim_id::kGrabMiss;
    });
    CHECK(missed);
    CHECK(fight.player().fighter().held() == nullptr);
}

TEST_CASE("a held human taken out of the level, or freed by a script, ends the hold", "[human][combat]") {
    // Expected: combat-grabs.md#pair-break, confirmed (code)
    const FightCharacter character;
    SECTION("gone from the targets") {
        HumanFight fight(character, 1.5F);
        fight.run("5 tap circle\n", 40);
        REQUIRE(fight.player().fighter().held() == &fight.other());
        fight.removeOther();
        fight.run("", 2);
        CHECK(fight.player().fighter().held() == nullptr);
        CHECK(fight.player().fighter().combat().mode() == combat::CombatMode::Free);
    }
    SECTION("HuSetNormalMode on the victim") {
        HumanFight fight(character, 1.5F);
        fight.run("5 tap circle\n", 40);
        REQUIRE(fight.other().state() == TargetState::Held);
        fight.other().setNormalMode(true);
        CHECK(fight.other().state() == TargetState::Standing);
        CHECK_FALSE(fight.other().attached());
        fight.run("", 2);
        CHECK(fight.player().fighter().held() == nullptr);
        // Free, the player no longer places it.
        const Vec3 at = fight.other().position();
        fight.run("0 stick left 100 0\n", 10);
        CHECK(fight.other().position().x == Approx(at.x).margin(1e-4F));
    }
    SECTION("a scene bind's reset of the victim lets both go (docs/research/scenes.md#join-reset)") {
        HumanFight fight(character, 1.5F);
        fight.run("5 tap circle\n", 40);
        REQUIRE(fight.other().state() == TargetState::Held);
        fight.other().resetForJoin();
        CHECK_FALSE(fight.other().fighter().inPair());
        CHECK_FALSE(fight.other().attached());
        fight.run("", 2);
        CHECK(fight.player().fighter().held() == nullptr);
        CHECK_FALSE(fight.player().fighter().inPair());
    }
    SECTION("a scene bind's reset of the grabber lets the victim go") {
        HumanFight fight(character, 1.5F);
        fight.run("5 tap circle\n", 40);
        REQUIRE(fight.player().fighter().held() == &fight.other());
        fight.player().resetForJoin();
        CHECK(fight.player().fighter().held() == nullptr);
        CHECK_FALSE(fight.player().fighter().inPair());
        fight.run("", 2);
        CHECK_FALSE(fight.other().fighter().inPair());
        CHECK(fight.other().state() != TargetState::Held);
    }
}

TEST_CASE("circle + cross grabs with the strike 657, deals its 60 as the hold starts and keeps holding",
          "[human][combat]") {
    // Expected: combat-grabs.md#strong-grapple, confirmed (code)
    const FightCharacter character;
    HumanFight fight(character, 1.0F);
    bool struck = false;
    bool strongStrike = false;
    std::vector<int> struckIds;
    int damageAtStrike = 0;
    fight.run("5 press circle cross\n9 release circle cross\n", 150, [&](std::uint64_t /*frame*/) {
        const std::uint32_t clip = fight.player().animator().animId();
        strongStrike = strongStrike || clip == static_cast<std::uint32_t>(combat::anim_id::kStrongGrapple);
        const std::vector<int>& strikes = fight.player().fighter().strikes();
        if (!strikes.empty() && !struck) {
            struck = true;
            struckIds = strikes;
            damageAtStrike = fight.damageTaken();
        }
    });
    CHECK(strongStrike);
    REQUIRE(struck);
    // The tutorial is told the hold's id, not the strike's.
    CHECK(struckIds == std::vector<int>{82});
    CHECK(damageAtStrike == 60);
    CHECK(fight.damageTaken() == 60);
    // It does not let go.
    CHECK(fight.player().fighter().combat().mode() == combat::CombatMode::Grabbing);
    CHECK(fight.other().state() == TargetState::Held);
    CHECK(fight.player().animator().animId() == 82U);
}

namespace {

// What a player's strong grapple (circle + cross, 657) left once it had played: the damage the victim took, the
// player's rage and whether he still holds the victim.
struct Grapple {
    bool played = false;
    bool heldAfter = false;
    int damage = 0;
    int rage = 0;
};

// The player, who may rage, grabs with the strong grapple; with `outOfReach` the victim is carried 3 m off while 657
// plays, far past the front hold's reach, so its end finds him out of reach.
Grapple strongGrappleRun(bool outOfReach) {
    const FightCharacter character;
    HumanFight fight(character, 1.0F);
    fight.player().setFlag(flag::kRageAllowed, true);
    const auto strong = static_cast<std::uint32_t>(combat::anim_id::kStrongGrapple);
    Grapple result;
    fight.run("5 press circle cross\n9 release circle cross\n", 150, [&](std::uint64_t /*frame*/) {
        if (fight.player().animator().animId() != strong) {
            return;
        }
        result.played = true;
        if (coney::human::Holdable* held = fight.other().holdable(); outOfReach && held != nullptr) {
            const Vec3 at = fight.player().position();
            held->place(Vec3{at.x + 3.0F, at.y, at.z}, fight.other().heading());
        }
    });
    result.heldAfter = fight.player().fighter().held() != nullptr;
    result.damage = fight.damageTaken();
    result.rage = fight.player().fighter().combat().rage().value();
    return result;
}

} // namespace

TEST_CASE("a strong grapple whose victim is out of reach at its end deals its damage, scores no rage and lets go",
          "[human][combat]") {
    // Expected: combat-grabs.md#strong-grapple, confirmed (code) (Grab_ConnectEnd 0x0026bad8 deals the clip's damage
    // before the reach test, and scores the move with Grab_ScoreMove 0x00264fa0 only after the snap)
    const Grapple inReach = strongGrappleRun(false);
    REQUIRE(inReach.played);
    CHECK(inReach.heldAfter);
    CHECK(inReach.damage == 60);
    CHECK(inReach.rage > 0);
    const Grapple away = strongGrappleRun(true);
    REQUIRE(away.played);
    CHECK_FALSE(away.heldAfter);
    CHECK(away.damage == 60);
    CHECK(away.rage == 0);
}

TEST_CASE("a strong grapple's mount and pick-up score the strong grapple again at the pick-up's end",
          "[human][combat]") {
    // Expected: combat-grabs.md#mount, confirmed (code) (Grab_ConnectEnd 0x0026bad8, hung on 248's end by
    // Mount_ReturnToFrontGrab 0x00271808, scores Grab_ScoreMove 0x00264fa0 with the grab's base id, record +0xb4,
    // which Grab_Start 0x0026c548 set to the strong grapple's id and the mount does not clear)
    const FightCharacter character;
    HumanFight fight(character, 1.0F);
    fight.player().setFlag(flag::kRageAllowed, true);
    const auto pickUp = static_cast<std::uint32_t>(combat::anim_id::kMountPickup);
    // The strong grapple, then circle taps: in the hold one mounts, in the mount one picks the victim up.
    std::string script = "5 press circle cross\n9 release circle cross\n";
    for (int frame = 100; frame < 200; frame += 20) {
        script += std::to_string(frame) + " tap circle\n";
    }
    bool picked = false;
    std::optional<int> rageInPickUp;
    std::optional<int> rageAfter;
    std::optional<bool> heldAfter;
    // Only the first pick-up counts: the taps go on and mount the victim again from the hold that follows it.
    fight.run(script, 200, [&](std::uint64_t /*frame*/) {
        const int rage = fight.player().fighter().combat().rage().value();
        if (rageAfter.has_value()) {
            return;
        }
        if (fight.player().animator().animId() == pickUp) {
            picked = true;
            rageInPickUp = rage;
        } else if (picked && !rageAfter.has_value()) {
            rageAfter = rage;
            heldAfter = fight.player().fighter().held() != nullptr;
        }
    });
    REQUIRE(picked);
    REQUIRE(rageInPickUp.has_value());
    REQUIRE(rageAfter.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIREs above end the test when empty, which clang-tidy
    // does not know; heldAfter is set together with rageAfter.
    CHECK(*heldAfter);
    CHECK(*rageAfter > *rageInPickUp);
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST_CASE("the power strike ends the grab with the victim down, unless square in its window extends it to 59",
          "[human][combat]") {
    // Expected: combat-grabs.md#grabbing, confirmed (runtime)
    const FightCharacter character;
    // Without the extension: 57 lands its 57, then the victim falls and the player is free.
    HumanFight plain(character, 1.0F);
    plain.run("5 tap circle\n30 press cross\n32 tap square\n34 release cross\n", 60);
    CHECK(plain.damageTaken() == 57);
    CHECK(plain.other().state() == TargetState::Grounded);
    CHECK(plain.player().fighter().held() == nullptr);
    CHECK(plain.player().fighter().combat().mode() == combat::CombatMode::Free);

    // A square press in the wind-up is dropped; one in the window plays 59 for 79 more, then the grab ends alike.
    HumanFight extended(character, 1.0F);
    std::vector<int> struck;
    extended.run("5 tap circle\n30 press cross\n32 tap square\n34 release cross\n35 tap square\n40 tap square\n", 80,
                 [&](std::uint64_t /*frame*/) {
                     const std::vector<int>& strikes = extended.player().fighter().strikes();
                     struck.insert(struck.end(), strikes.begin(), strikes.end());
                 });
    CHECK(struck == std::vector<int>{57, 59});
    CHECK(extended.damageTaken() == 57 + 79);
    CHECK(extended.other().state() == TargetState::Grounded);
    CHECK(extended.player().fighter().combat().mode() == combat::CombatMode::Free);
}

TEST_CASE("placing the grabber breaks the pair: the victim plays its reaction and stands free", "[human][combat]") {
    // Expected: combat-grabs.md#pair-break, confirmed (code)
    // docs/research/combat-grabs.md#pair-break: the human placed plays nothing and is free; a front grab's victim plays
    // 145.
    const FightCharacter character;
    HumanFight fight(character, 1.5F);
    fight.run("5 tap circle\n", 40);
    REQUIRE(fight.other().state() == TargetState::Held);
    fight.place(fight.player(), Vec3{20.0F, 20.0F, 0.0F});
    CHECK(fight.player().fighter().held() == nullptr);
    CHECK_FALSE(fight.player().fighter().inPair());
    CHECK(fight.other().state() == TargetState::Standing);
    CHECK_FALSE(fight.other().attached());
    CHECK(fight.other().animator().animId() == 145U);
    fight.run("", 60);
    CHECK(fight.other().state() == TargetState::Standing);
    CHECK_FALSE(fight.other().fighter().inPair());
    CHECK(fight.other().animator().animId() != 83U);
}

TEST_CASE("placing the victim breaks the pair: the grabber plays its reaction on its next update", "[human][combat]") {
    // Expected: combat-grabs.md#pair-break, confirmed (code)
    const FightCharacter character;
    HumanFight fight(character, 1.5F);
    fight.run("5 tap circle\n", 40);
    REQUIRE(fight.player().fighter().held() == &fight.other());
    fight.place(fight.other(), Vec3{20.0F, 20.0F, 0.0F});
    CHECK(fight.other().state() == TargetState::Standing);
    fight.run("", 1);
    CHECK(fight.player().fighter().held() == nullptr);
    CHECK(fight.player().fighter().combat().mode() == combat::CombatMode::Free);
    CHECK(fight.player().animator().animId() == 138U);
    // The other human stays where it was put.
    CHECK(fight.other().position().x == Approx(20.0F).margin(0.05F));
}

TEST_CASE("placing the mounter breaks the mount: the human below plays 245, then gets up", "[human][combat]") {
    // Expected: combat-grabs.md#pair-break, confirmed (code)
    const FightCharacter character;
    HumanFight fight(character, 2.0F);
    fight.run("5 press circle\n15 release circle\n", 60);
    REQUIRE(fight.other().state() == TargetState::Mounted);
    fight.place(fight.player(), Vec3{20.0F, 20.0F, 0.0F});
    CHECK(fight.other().state() == TargetState::Standing);
    CHECK(fight.other().animator().animId() == 245U);
    bool rose = false;
    fight.run("", 90, [&](std::uint64_t /*frame*/) { rose = rose || fight.other().animator().animId() == 199U; });
    CHECK(rose);
    CHECK_FALSE(fight.other().fighter().inPair());
}

TEST_CASE("a held human whose grabber stops updating frees itself", "[human][combat]") {
    // Expected: unverified, #684 (Coney's choice: no hold outlives the grabber that drives it)
    // Coney's choice (Fighter::freeFromLostGrabber()): no hold outlives the grabber that drives it.
    const FightCharacter character;
    HumanFight fight(character, 1.5F);
    fight.run("5 tap circle\n", 40);
    REQUIRE(fight.other().state() == TargetState::Held);
    fight.stopPlayer();
    // The victim acts after its grabber here, so its first update without the grabber is its second since the keep.
    fight.run("", 1);
    CHECK(fight.other().state() == TargetState::Held);
    fight.run("", 1);
    CHECK(fight.other().state() == TargetState::Standing);
    CHECK(fight.other().animator().animId() == 145U);
}

TEST_CASE("a held human stays held while its grabber goes on updating", "[human][combat]") {
    // Expected: combat-grabs.md#grabbed, confirmed (code)
    // A live grabber keeps the hold every update, so the victim never frees itself.
    const FightCharacter character;
    HumanFight fight(character, 1.5F);
    fight.run("5 tap circle\n", 40);
    REQUIRE(fight.other().state() == TargetState::Held);
    fight.run("", 200, [&](std::uint64_t /*frame*/) { REQUIRE(fight.other().state() == TargetState::Held); });
}

TEST_CASE("a held human struggles out of a grab: square costs the grabber power, circle then escapes",
          "[human][combat]") {
    // Expected: combat-grabs.md#grabbed, confirmed (runtime)
    const FightCharacter character;
    HumanFight fight(character, 1.5F);
    // The held human presses square three times, then circle (docs/research/combat-grabs.md#grabbed).
    fight.otherCommand = [](std::uint64_t frame) {
        if (frame == 30 || frame == 60 || frame == 90) {
            return combat::command::kSquarePressed;
        }
        return frame == 120 ? combat::command::kCirclePressed : combat::command::kNone;
    };
    std::vector<std::uint32_t> grabberClips;
    std::vector<std::uint32_t> heldClips;
    int powerAfterStruggles = 0;
    fight.run("5 tap circle\n", 160, [&](std::uint64_t frame) {
        const std::uint32_t grabber = fight.player().animator().animId();
        const std::uint32_t held = fight.other().animator().animId();
        if (grabberClips.empty() || grabberClips.back() != grabber) {
            grabberClips.push_back(grabber);
        }
        if (heldClips.empty() || heldClips.back() != held) {
            heldClips.push_back(held);
        }
        if (frame == 110) {
            powerAfterStruggles = fight.player().fighter().combat().power().value();
        }
    });
    // Each square played 96 on the held human and 97 on the grabber, and cost a quarter of the grabber's 400 (the
    // civilian's divisor 4), on top of the grab's drain of 15 a second.
    CHECK(std::ranges::find(heldClips, 96U) != heldClips.end());
    CHECK(std::ranges::find(grabberClips, 97U) != grabberClips.end());
    CHECK(powerAfterStruggles < 400 - 3 * 100 + 5);
    CHECK(powerAfterStruggles >= 400 - 3 * 100 - 15 * 105 / 30);
    // With the grabber's power at a quarter or less, circle always escapes: 100 on the held human, 101 on the grabber,
    // who goes down; the hold is over.
    CHECK(std::ranges::find(heldClips, 100U) != heldClips.end());
    CHECK(std::ranges::find(grabberClips, 101U) != grabberClips.end());
    CHECK_FALSE(fight.other().fighter().holdState().has_value());
    CHECK(fight.player().fighter().held() == nullptr);
    CHECK(fight.player().state() == TargetState::Grounded);
}

TEST_CASE("a throw toward a wall within the side's reach is the wall throw", "[human][combat]") {
    // Expected: combat-throws.md#throws, confirmed (runtime)
    const FightCharacter character;
    // Circle with the stick ahead throws ahead: with a wall 2 m ahead (within 155's far range of 2.25 m) the wall
    // throw 155 / 156 plays; with none, the plain throw 147 / 148 (docs/research/combat-throws.md#throws).
    const auto throwClips = [&character](const std::vector<coney::test::Tri>& walls) {
        HumanFight fight(character, 1.5F, walls);
        std::vector<std::uint32_t> clips;
        fight.run("5 tap circle\n40 stick left 0 70\n41 tap circle\n43 stick left 0 0\n", 60, [&](std::uint64_t) {
            for (const std::uint32_t clip : {fight.player().animator().animId(), fight.other().animator().animId()}) {
                if (std::ranges::find(clips, clip) == clips.end()) {
                    clips.push_back(clip);
                }
            }
        });
        return clips;
    };
    const std::vector<std::uint32_t> wall = throwClips(coney::test::wallFacingMinusY(42.0F, 30.0F, 50.0F, 0.0F, 3.0F));
    CHECK(std::ranges::find(wall, 155U) != wall.end());
    CHECK(std::ranges::find(wall, 156U) != wall.end());
    CHECK(std::ranges::find(wall, 147U) == wall.end());
    const std::vector<std::uint32_t> open = throwClips({});
    CHECK(std::ranges::find(open, 147U) != open.end());
    CHECK(std::ranges::find(open, 155U) == open.end());
}

TEST_CASE("a grab-block flag on either human stops a grab of its side at the intro's end; the miss plays on",
          "[human][combat]") {
    // Expected: combat/index.md#human-flags, confirmed (code)
    const FightCharacter character;
    // Whether the player's circle tap at the other human ends in a hold.
    const auto grabs = [&](bool fromRear, std::uint64_t playerFlags, std::uint64_t otherFlags) {
        HumanFight fight(character, 1.5F);
        if (fromRear) {
            // Facing away from the player: the grab comes from behind.
            fight.place(fight.other(), Vec3{40.0F, 41.5F, 0.0F});
        }
        fight.player().setFlag(playerFlags, true);
        fight.other().setFlag(otherFlags, true);
        bool missed = false;
        fight.run("5 tap circle\n", 40, [&](std::uint64_t /*frame*/) {
            missed = missed || fight.player().animator().animId() == combat::anim_id::kGrabMiss;
        });
        const bool held = fight.player().fighter().held() != nullptr && fight.other().state() == TargetState::Held;
        CHECK(missed != held);
        return held;
    };
    CHECK(grabs(false, 0, 0));
    CHECK(grabs(true, 0, 0));
    // The front bit stops a front grab from either side of it, and leaves the rear grab alone.
    CHECK_FALSE(grabs(false, 0, flag::kBlockFrontGrab));
    CHECK_FALSE(grabs(false, flag::kBlockFrontGrab, 0));
    CHECK(grabs(true, 0, flag::kBlockFrontGrab));
    // The rear bit the same for rear grabs.
    CHECK_FALSE(grabs(true, 0, flag::kBlockRearGrab));
    CHECK_FALSE(grabs(true, flag::kBlockRearGrab, 0));
    CHECK(grabs(false, 0, flag::kBlockRearGrab));
}

TEST_CASE("a player with both grab-block flags starts no grab; his tackle still goes", "[human][combat]") {
    // Expected: combat/index.md#human-flags, confirmed (code)
    const FightCharacter character;
    {
        HumanFight fight(character, 1.5F);
        fight.player().setFlag(flag::kBlockFrontGrab | flag::kBlockRearGrab, true);
        bool intro = false;
        fight.run("5 tap circle\n", 40, [&](std::uint64_t /*frame*/) {
            intro = intro || fight.player().animator().animId() == combat::anim_id::kGrabPlayerIntro;
        });
        CHECK_FALSE(intro);
        CHECK(fight.player().fighter().held() == nullptr);
        CHECK(fight.player().fighter().combat().mode() == combat::CombatMode::Free);
    }
    {
        HumanFight fight(character, 2.0F);
        fight.player().setFlag(flag::kBlockFrontGrab | flag::kBlockRearGrab, true);
        fight.run("5 press circle\n15 release circle\n", 50);
        CHECK(fight.other().state() == TargetState::Mounted);
    }
}

TEST_CASE("out of power, a victim with double damage or auto-escape escapes; with no-escape it is let go whatever "
          "else it has",
          "[human][combat]") {
    // Expected: combat-grabs.md#grabbed, confirmed (code). Human_UpdateMeters 0x002562d0 tests 0x2000000000 first:
    // with it the grab is released (Grab_Release), whether the victim has 0x20000 or 0x4000 or the grabber is hurt.
    const FightCharacter character;
    // Grabs the other human with `otherFlags`, hurts the player when `hurt`, empties the power; whether the victim
    // escaped (the player down) rather than being let go.
    const auto escapes = [&](std::uint64_t otherFlags, bool hurt) {
        HumanFight fight(character, 1.5F);
        fight.other().setFlag(otherFlags, true);
        fight.run("5 tap circle\n", 40);
        REQUIRE(fight.player().fighter().pairStage() == coney::human::PairStage::Attached);
        if (hurt) {
            fight.player().fighter().health().apply(800);
        }
        fight.player().fighter().combat().power().set(0);
        fight.run("", 3);
        CHECK(fight.player().fighter().held() == nullptr);
        CHECK(fight.other().state() == TargetState::Standing);
        return fight.player().fighter().victim().grounded();
    };
    CHECK_FALSE(escapes(0, false));
    CHECK(escapes(flag::kDoubleDamage, false));
    CHECK(escapes(flag::kAutoEscape, false));
    CHECK(escapes(0, true));
    CHECK_FALSE(escapes(flag::kNoEscape, true));
    // No-escape wins over the escape flags.
    CHECK_FALSE(escapes(flag::kDoubleDamage | flag::kNoEscape, false));
    CHECK_FALSE(escapes(flag::kAutoEscape | flag::kNoEscape, true));
}

TEST_CASE("an AI grabbing the player throws him on its circle tap with the stick, and the grab ends",
          "[human][combat]") {
    // Expected: ai/index.md#fight-reactions, confirmed (runtime) (the AI's throw in the hold is command 0xd with the
    // stick; its throw lets the player go, on his back); combat-grabs.md#grabbing, confirmed (code)
    const FightCharacter character;
    HumanFight fight(character, 1.0F);
    fight.otherFightsPlayer = true;
    // The AI grabs at update 2 (0xd), then, two updates into the hold, taps circle again with the stick full ahead of
    // it (it faces the world's -y), as its brain's throw does.
    std::optional<std::uint64_t> heldAt;
    fight.otherCommand = [&](std::uint64_t frame) {
        if (frame == 2 || (heldAt.has_value() && frame == *heldAt + 2)) {
            return combat::command::kCircleTapped;
        }
        return combat::command::kNone;
    };
    fight.otherStick = [&](std::uint64_t frame) {
        return heldAt.has_value() && frame == *heldAt + 2 ? std::pair{0.0F, -1.0F} : std::pair{0.0F, 0.0F};
    };
    bool thrownDown = false;
    fight.run("", 120, [&](std::uint64_t frame) {
        if (!heldAt.has_value() && fight.other().fighter().pairStage() == coney::human::PairStage::Attached) {
            heldAt = frame;
        }
        thrownDown = thrownDown || fight.player().state() == TargetState::Grounded;
    });
    REQUIRE(heldAt.has_value());
    // The front throw (147) put him on the ground with its 66, and the AI holds no one.
    CHECK(thrownDown);
    const auto& health = fight.player().fighter().health();
    CHECK(health.maximum() - health.value() == 66);
    CHECK(fight.other().fighter().held() == nullptr);
    CHECK_FALSE(fight.player().fighter().holdState().has_value());
}

TEST_CASE("an AI that picks the tackled player up as the mount seats him strikes him in the hold that follows",
          "[human][combat]") {
    // Expected: ai/index.md#ai-grab-runtime, confirmed (runtime) (circle two updates into the mount, 210 at 127 and
    // the pick-up 248 at 129, then the front hold); combat-grabs.md#grabbing, confirmed (runtime) (square 51 or 53, 57)
    const FightCharacter character;
    HumanFight fight(character, 2.0F);
    fight.otherFightsPlayer = true;
    // The AI tackles at update 2 (0xe); two updates into the mount it taps circle (0xd), still in the mount's 0.1 s
    // seat, and two updates into the hold that follows it squares.
    std::optional<std::uint64_t> mountedAt;
    std::optional<std::uint64_t> holdAt;
    fight.otherCommand = [&](std::uint64_t frame) {
        if (frame == 2) {
            return combat::command::kCircleHeld;
        }
        if (mountedAt.has_value() && frame == *mountedAt + 2) {
            return combat::command::kCircleTapped;
        }
        if (holdAt.has_value() && frame == *holdAt + 2) {
            return combat::command::kSquarePressed;
        }
        return combat::command::kNone;
    };
    bool struck = false;
    fight.run("", 200, [&](std::uint64_t frame) {
        const std::uint32_t clip = fight.other().animator().animId();
        if (!mountedAt.has_value() && clip == coney::human::clips::kMountingIdle) {
            mountedAt = frame;
        }
        if (mountedAt.has_value() && !holdAt.has_value() && clip == coney::human::clips::kGrabHold) {
            holdAt = frame;
        }
        struck = struck || clip == static_cast<std::uint32_t>(combat::anim_id::kGrabComboStrike1) ||
                 clip == static_cast<std::uint32_t>(combat::anim_id::kGrabComboStrike2);
    });
    REQUIRE(mountedAt.has_value());
    REQUIRE(holdAt.has_value());
    // The strike played on the held player and took its 57.
    CHECK(struck);
    const auto& health = fight.player().fighter().health();
    CHECK(health.maximum() - health.value() == 57);
}

namespace {

// What an AI's pick-up of the tackled player left: the damage he took and whether the AI still holds him once the
// pick-up (248) has ended.
struct PickUp {
    bool picked = false;
    bool heldAfter = false;
    int damage = 0;
};

// The AI tackles the player at update 2 and taps circle two updates into the mount; `during` runs after each update
// in which the AI plays the pick-up (248). The run stops 10 updates after 248 ends, before any move in the hold.
template <typename During> PickUp pickUpRun(const FightCharacter& character, During during) {
    HumanFight fight(character, 2.0F);
    fight.otherFightsPlayer = true;
    std::optional<std::uint64_t> mountedAt;
    fight.otherCommand = [&](std::uint64_t frame) {
        if (frame == 2) {
            return combat::command::kCircleHeld;
        }
        if (mountedAt.has_value() && frame == *mountedAt + 2) {
            return combat::command::kCircleTapped;
        }
        return combat::command::kNone;
    };
    const auto pickUp = static_cast<std::uint32_t>(combat::anim_id::kMountPickup);
    PickUp result;
    std::optional<std::uint64_t> endedAt;
    fight.run("", 200, [&](std::uint64_t frame) {
        const std::uint32_t clip = fight.other().animator().animId();
        if (!mountedAt.has_value() && clip == coney::human::clips::kMountingIdle) {
            mountedAt = frame;
        }
        if (clip == pickUp) {
            result.picked = true;
            during(fight);
        } else if (result.picked && !endedAt.has_value()) {
            endedAt = frame;
        }
        if (endedAt.has_value() && frame == *endedAt + 10) {
            result.heldAfter = fight.other().fighter().held() != nullptr;
            const auto& health = fight.player().fighter().health();
            result.damage = health.maximum() - health.value();
        }
    });
    return result;
}

} // namespace

TEST_CASE("an AI's pick-up from the mount deals the pick-up clip's damage as it hands over to the hold",
          "[human][combat]") {
    // Expected: combat-grabs.md#mount, confirmed (code) (Mount_ReturnToFrontGrab 0x00271808 hangs Grab_ConnectEnd
    // 0x0026bad8 on 248's end: the clip's damage-table value while the grabber has power, then the hold)
    FightCharacter character;
    REQUIRE(character.ranges.setDamage(static_cast<std::size_t>(combat::anim_id::kMountPickup), 25));
    const PickUp result = pickUpRun(character, [](HumanFight& /*fight*/) {});
    REQUIRE(result.picked);
    CHECK(result.heldAfter);
    CHECK(result.damage == 25);
}

TEST_CASE("an AI's pick-up from the mount lets the player go when he is out of reach at its end", "[human][combat]") {
    // Expected: combat-grabs.md#mount, confirmed (code) (Grab_ConnectEnd 0x0026bad8: the damage first, then the
    // release past the larger of the hold's reach + 0.2 m and its reach x 1.2)
    FightCharacter character;
    REQUIRE(character.ranges.setDamage(static_cast<std::size_t>(combat::anim_id::kMountPickup), 25));
    // The player is carried 3 m off while the pick-up plays: far past the front hold's 1.081 m reach.
    const PickUp result = pickUpRun(character, [](HumanFight& fight) {
        const Vec3 at = fight.other().position();
        if (coney::human::Holdable* held = fight.player().holdable(); held != nullptr) {
            held->place(Vec3{at.x + 3.0F, at.y, at.z}, fight.player().heading());
        }
    });
    REQUIRE(result.picked);
    CHECK_FALSE(result.heldAfter);
    CHECK(result.damage == 25);
}

TEST_CASE("an AI attacker's double damage gets through the player's hit armour", "[human][combat]") {
    // Expected: combat/index.md#human-flags, confirmed (code)
    const FightCharacter character;
    // The other human squares the player at update 5; the player squares at update `wind`. The player's armoured hit
    // count and the damage he took.
    struct Result {
        int armoured = 0;
        int damage = 0;
    };
    const auto exchange = [&](std::uint64_t wind, bool doubled) {
        HumanFight fight(character, 1.2F);
        fight.otherFightsPlayer = true;
        fight.other().setFlag(flag::kDoubleDamage, doubled);
        fight.otherCommand = [](std::uint64_t frame) {
            return frame == 5 ? combat::command::kSquarePressed : combat::command::kNone;
        };
        fight.run(std::format("{} tap square\n", wind), 40);
        const auto& health = fight.player().fighter().health();
        return Result{.armoured = fight.player().fighter().hitsArmoured(), .damage = health.maximum() - health.value()};
    };
    // The first timing at which the plain hit is armoured: the player is winding up as it lands.
    std::uint64_t wind = 0;
    for (; wind < 20; ++wind) {
        if (exchange(wind, false).armoured == 1) {
            break;
        }
    }
    REQUIRE(wind < 20);
    const Result plain = exchange(wind, false);
    const Result doubled = exchange(wind, true);
    CHECK(plain.damage > 0);
    CHECK(doubled.armoured == 0);
    CHECK(doubled.damage == 2 * plain.damage);
}

namespace {

// The fight's clips with the power strike 57 and its extension 59 as long as the disc's slow-motion power moves (24
// frames) and carrying their pair, slow motion on at frame 10 and off at 15, as Vermin's 59 has them
// (docs/research/slow-motion.md#non-lethal).
std::vector<coney::test::LocomotionClip> slowMotionPowerClips() {
    std::vector<coney::test::LocomotionClip> clips = coney::test::fightClips();
    const auto strike = static_cast<std::uint32_t>(combat::anim_id::kGrabPower1Strike1);
    for (coney::test::LocomotionClip& clip : clips) {
        if (clip.id == strike || clip.id == strike + 2) {
            clip.duration = 24.0F / 30.0F;
            clip.markers.push_back({10, coney::camera::SlowMotion::kEventOn});
            clip.markers.push_back({15, coney::camera::SlowMotion::kEventOff});
        }
    }
    return clips;
}

// What a power move left behind: the characters' step at the end, whether slow motion was ever on, and whether the
// grab was let go (95) while the move played.
struct SlowMotionRun {
    float step = 0.0F;
    bool slowed = false;
    bool letGo = false;
};

// Plays `script` for 300 updates with the player's power at `power` as the hold stands (update 29), each update's
// slow-motion event of the player's clip switching slow motion as the player does (human::Player), its step given to
// both humans for the next update.
SlowMotionRun runSlowMotion(const FightCharacter& character, std::string_view script, int power) {
    HumanFight fight(character, 1.0F);
    coney::camera::SlowMotion slow;
    SlowMotionRun run;
    fight.run(script, 300, [&](std::uint64_t frame) {
        if (frame == 29) {
            fight.player().fighter().combat().power().set(power);
        }
        if (const std::optional<std::uint16_t> event = fight.player().slowMotionEvent()) {
            slow.event(*event, 0);
        }
        run.slowed = run.slowed || slow.active();
        run.letGo =
            run.letGo || fight.player().animator().animId() == static_cast<std::uint32_t>(combat::anim_id::kGrabLetGo);
        fight.player().setStepSeconds(slow.stepSeconds());
        fight.other().setStepSeconds(slow.stepSeconds());
    });
    run.step = slow.stepSeconds();
    return run;
}

} // namespace

TEST_CASE("a power move that does not kill plays out with no power left and gives slow motion back",
          "[human][combat]") {
    // Expected: docs/research/slow-motion.md#power-move, confirmed (code). Once the power strike starts the player is
    // no longer grabbing (`Grab_BreakOnHit` ends the grab and makes the pair a paired move), so the meter neither
    // drains nor lets go when it is empty (`Human_UpdateMeters`): the move plays to its slow-motion off event. 110
    // power leaves 10 after the strike's quarter of 400, which the hold's drain of 15 a second would empty within the
    // move.
    const FightCharacter character(slowMotionPowerClips());
    constexpr std::string_view kPowerStrike = "5 tap circle\n30 press cross\n32 tap square\n34 release cross\n";
    // Without the extension: 57 slows time and gives it back.
    const SlowMotionRun plain = runSlowMotion(character, kPowerStrike, 110);
    CHECK(plain.slowed);
    CHECK_FALSE(plain.letGo);
    CHECK(plain.step == coney::camera::SlowMotion::kNormalStep);
    // With the extension: square in 57's window (before its slow motion) plays 59, which slows time and gives it back.
    const SlowMotionRun extended =
        runSlowMotion(character, std::string(kPowerStrike) + "35 tap square\n40 tap square\n", 110);
    CHECK(extended.slowed);
    CHECK_FALSE(extended.letGo);
    CHECK(extended.step == coney::camera::SlowMotion::kNormalStep);
    // With full power the same move never ran short.
    CHECK(runSlowMotion(character, kPowerStrike, 400).step == coney::camera::SlowMotion::kNormalStep);
}

TEST_CASE("a power move holds the power meter: it neither drains nor refills while the move plays", "[human][combat]") {
    // Expected: docs/research/slow-motion.md#power-move, confirmed (code) at 0x00256a60 and confirmed (runtime) in run
    // r-20261025-power-move-meter (110 before the strike, 9 after it and 30 frames into 57): the power strike makes the
    // attacker a paired move's (state 0x1000), the refill adds nothing in that state, and the drain runs only in a
    // grab, so what the strike leaves of 110 (its quarter of 400 and the hold's drain before it) stays as it is until
    // the move (here in slow motion, five times as long) has played out.
    const FightCharacter character(slowMotionPowerClips());
    constexpr std::string_view kPowerStrike = "5 tap circle\n30 press cross\n32 tap square\n34 release cross\n";
    HumanFight fight(character, 1.0F);
    coney::camera::SlowMotion slow;
    const auto strike = static_cast<std::uint32_t>(combat::anim_id::kGrabPower1Strike1);
    std::vector<int> during;
    int after = -1;
    fight.run(kPowerStrike, 300, [&](std::uint64_t frame) {
        if (frame == 29) {
            fight.player().fighter().combat().power().set(110);
        }
        if (const std::optional<std::uint16_t> event = fight.player().slowMotionEvent()) {
            slow.event(*event, 0);
        }
        fight.player().setStepSeconds(slow.stepSeconds());
        fight.other().setStepSeconds(slow.stepSeconds());
        const int power = fight.player().fighter().combat().power().value();
        if (fight.player().animator().animId() == strike) {
            during.push_back(power);
        } else if (!during.empty()) {
            after = power;
        }
    });
    REQUIRE(during.size() > 30);
    const int left = during.front();
    CHECK(left <= 10);
    CHECK(*std::ranges::min_element(during) == left);
    CHECK(*std::ranges::max_element(during) == left);
    // Once the move has ended the meter refills.
    CHECK(after > left);
}

// The player's fight stance around a throw, a special and a hidden L1: after a throw with L1 held the lock-on walk
// comes back, the special 653 enters no stance, and hidden, L1 held keeps a target knocked over once its bit is set
// (`Player_UpdateSprint` 0x0027ce90, `Player_EnterStance` 0x00280068, `Player_OnL1Held` 0x0027dc00,
// docs/research/combat-targets.md#fight-stance). Issue #82.

using coney::test::Fight;
using coney::test::throwAhead;
using coney::test::throwOver;

namespace {

// The stance cases' character, built once.
const FightCharacter& stanceCharacter() {
    static const FightCharacter built;
    return built;
}

} // namespace

TEST_CASE("after a throw with L1 held the lock-on walk comes back on the target", "[human][combat][targeting]") {
    // Expected: combat-targets.md#fight-stance, confirmed (code). L1's bit (0x8), which no handler clears while he
    // holds a victim, and the lock (0x4) the throw keeps make Human_IsLocked 0x00227c88 true, so Player_UpdateSprint
    // 0x0027ce90's step 3 enters again off the throw's free movement and Player_LockOn 0x00280158 puts the lock-on
    // movement back.
    Fight fight(stanceCharacter(), 1.5F);
    const auto [line, thrown] = throwAhead(fight, true);
    REQUIRE(thrown > 0);
    REQUIRE(line[static_cast<std::size_t>(thrown) - 1].target == &fight.target());
    const auto over = throwOver(line, thrown);
    REQUIRE(over != line.end());
    // From the update after the throw's clip is over the lock-on walk drives him again, on the thrown target.
    for (auto it = over + 1; it != line.end(); ++it) {
        INFO("update " << (it - line.begin()));
        CHECK(it->target == &fight.target());
        CHECK(it->lockWalk);
    }
}

TEST_CASE("the special 653 does not put the player in the fight stance", "[human][combat][targeting]") {
    // Expected: combat-targets.md#fight-stance, confirmed (code). Player_Special 0x00287730 and Player_SpecialAttack
    // 0x00263c90 start 653 (Attack_StartSolo) without Player_EnterStance 0x00280068, which only Square, Cross, the
    // dive, the grab or tackle, the ground strike and the stance updates call. No one stands within 2 m to enter it.
    Fight fight(stanceCharacter(), 6.0F);
    bool played = false;
    fight.run("5 tap cross square\n", 60, [&](std::uint64_t update) {
        played = played || fight.human().animator().animId() == static_cast<std::uint32_t>(combat::anim_id::kSpecial);
        INFO("update " << update);
        CHECK_FALSE(fight.human().fighter().inFightStance());
    });
    CHECK(played);
}

TEST_CASE("hidden, L1 held keeps a target knocked over once its bit is set", "[human][combat][targeting]") {
    // Expected: combat-targets.md#fight-stance, confirmed (code). Player_OnL1Held 0x0027dc00 drops a hidden player's
    // target in state 0x3800f0000 only while state 0x8 is clear; the hold sets it (0x20000008), so a target knocked
    // over after that stays.
    Fight fight(stanceCharacter(), 1.5F);
    fight.makeCalm(fight.target());
    fight.human().enterHiding();
    fight.run("0 press l1\n", 10);
    REQUIRE(fight.human().fighter().target() == &fight.target());
    static constexpr std::array<std::uint32_t, 1> kDown{coney::human::clips::kIdle};
    fight.target().play(kDown, coney::human::clips::kIdle, coney::human::AnimState::Hold,
                        coney::human::TargetState::Grounded);
    REQUIRE(fight.target().state() == coney::human::TargetState::Grounded);
    fight.run("0 press l1\n", 10, [&](std::uint64_t update) {
        INFO("update " << update);
        CHECK(fight.human().fighter().target() == &fight.target());
    });
}
