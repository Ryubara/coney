// SPDX-License-Identifier: GPL-3.0-or-later
// The player grabbing and tackling another human, not a sandbox target (human/holdable.h,
// docs/research/combat.md#grab, docs/research/combat.md#grab-posing): the grab holds the human, the reaction goal
// its brain gets follows its state, the hold's moves and the throw land on it, a human with flag 0x40 cannot be
// grabbed, and a human gone from the level or freed by a script ends the hold. Synthetic clips; the player's stick and
// buttons come from input scripts.
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ai/goal.h"
#include "ai/reaction_goals.h"
#include "combat/anim_ids.h"
#include "combat/commands.h"
#include "combat/player_combat.h"
#include "combat/power_class.h"
#include "human/human.h"
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
    HumanFight(const FightCharacter& character, float ahead)
        : m_mesh(coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F))),
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
            m_player.step(coney::human::HumanInput{.stickX = frame.leftX,
                                                   .stickY = frame.leftY,
                                                   .cameraForward = Vec3{0.0F, 1.0F, 0.0F},
                                                   .sprintHeld = false,
                                                   .actionPressed = false,
                                                   .command = command,
                                                   .buttons = frame.buttons,
                                                   .targets = targets},
                          m_mesh.get());
            if (!m_removed) {
                m_other.step(coney::human::HumanInput{.stickX = 0.0F,
                                                      .stickY = 0.0F,
                                                      .cameraForward = Vec3{0.0F, 1.0F, 0.0F},
                                                      .sprintHeld = false,
                                                      .actionPressed = false,
                                                      .command = combat::command::kNone,
                                                      .buttons = 0,
                                                      .targets = {}},
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

    Human& player() { return m_player; }
    Human& other() { return m_other; }
    // The damage the other human has taken.
    [[nodiscard]] int damageTaken() const {
        return m_other.fighter().health().maximum() - m_other.fighter().health().value();
    }

  private:
    std::unique_ptr<coney::raycast::CollisionMesh> m_mesh;
    Human m_player;
    Human m_other;
    std::vector<coney::human::Combatant*> m_targets;
    combat::CommandTables m_tables = combat::CommandTables::street();
    combat::CommandMatcher m_matcher;
    bool m_removed = false;
    std::uint64_t m_frame = 0;
};

// Whether `human`'s state calls for a reaction goal of `type` first (reactionGoalFor()).
bool reactsWith(const Human& human, coney::ai::GoalType type) {
    const auto goal = coney::ai::reactionGoalFor(human);
    return goal != nullptr && goal->type() == type;
}

} // namespace

TEST_CASE("the player grabs another human, strikes it in the hold and throws it", "[human][combat]") {
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

TEST_CASE("circle held tackles another human: it is mounted and its reaction goal is the tackled one",
          "[human][combat]") {
    const FightCharacter character;
    HumanFight fight(character, 2.0F);
    fight.run("5 press circle\n15 release circle\n50 tap square\n", 70);
    CHECK(fight.other().state() == TargetState::Mounted);
    CHECK(reactsWith(fight.other(), coney::ai::GoalType::ReactTackled));
    CHECK(fight.damageTaken() == 61); // the mount's square, 219 or 221
}

TEST_CASE("a human with flag 0x40 cannot be grabbed: the grab misses", "[human][combat]") {
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
}

TEST_CASE("circle + cross grabs with the strike 657, deals its 60 as the hold starts and keeps holding",
          "[human][combat]") {
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
