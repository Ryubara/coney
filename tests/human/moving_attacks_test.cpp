// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "combat/anim_ids.h"
#include "combat/combat_tuning.h"
#include "combat/commands.h"
#include "core/pad.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/humans.h"
#include "human/locomotion.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

// Every attack button pressed while the player walks, runs or sprints, driven as a player drives him: an analog left
// stick and the pad's buttons through the street's command tables (docs/research/combat.md#run-attacks). Each case
// runs through human::Humans, the characters' step the play mode uses, and through Human::step, which the other fight
// tests use: the dispatcher's gait tests must see the gait the last update left on both. Synthetic clips only.

using coney::human::Gait;
using coney::human::Human;
using coney::human::Humans;
namespace combat = coney::combat;
namespace id = combat::anim_id;

namespace {

constexpr std::uint32_t kRun = 410;
constexpr std::uint32_t kSprint = 411;
constexpr std::uint32_t kWalk = 408;
constexpr std::uint32_t kWalkStart = 413;

// The fight's clips with the moving attacks' lengths as on the disc (charge 0, dive 1, walk attack 23, run attack 24),
// the bat's square and cross (34, 36) and the armed run attack 501. The synthetic clips play at 0.75: a clip of
// `updates` lasts updates / 40 s.
std::vector<coney::test::LocomotionClip> movingClips() {
    std::vector<coney::test::LocomotionClip> clips = coney::test::fightClips();
    const auto timed = [](std::uint32_t clip, float updates, float velocity) {
        return coney::test::LocomotionClip{.id = clip,
                                           .speed = 0.0F,
                                           .duration = updates * 0.75F / 30.0F,
                                           .rootVelocity = velocity,
                                           .rangeFlags = 0,
                                           .reach = 0.0F,
                                           .knockdown = false};
    };
    for (const coney::test::LocomotionClip& clip :
         {timed(0, 26.67F, 7.45F), timed(1, 61.33F, 0.0F), timed(23, 24.0F, 0.0F), timed(24, 21.33F, 7.09F),
          timed(34, 20.0F, 0.0F), timed(36, 30.0F, 0.0F), timed(501, 21.0F, 7.09F)}) {
        const auto same = std::ranges::find(clips, clip.id, &coney::test::LocomotionClip::id);
        if (same != clips.end()) {
            *same = clip;
        } else {
            clips.push_back(clip);
        }
    }
    return clips;
}

// The character, built once.
const coney::test::FightCharacter& movingCharacter() {
    static const coney::test::FightCharacter character(movingClips());
    return character;
}

// What one update left: the clip playing, the stored gait and the speed.
struct Sample {
    std::uint32_t clip = 0;
    Gait gait = Gait::Standing;
    float speed = 0.0F;
};

// The player alone on a floor, driven by an input script through the command tables, stepped by human::Humans (the
// play mode's path) or by Human::step.
class Runner {
  public:
    explicit Runner(bool throughHumans) : m_throughHumans(throughHumans) {
        m_human.spawn(m_mesh.get(), coney::anim::Vec3{40.0F, 10.0F, 0.0F}, 0.0F);
        m_humans.add(m_human, true);
    }

    // Runs `frames` updates of `script` and returns what each left.
    std::vector<Sample> run(std::string_view script, std::uint64_t frames) {
        std::vector<Sample> samples;
        for (const coney::test::PadFrame& frame : coney::test::playScript(script, frames)) {
            const combat::CommandId command =
                m_matcher.update(frame.buttons, m_tables, combat::combatTuning().historyHoldSamples);
            const coney::human::HumanInput input{.stickX = frame.leftX,
                                                 .stickY = frame.leftY,
                                                 .cameraForward = coney::test::kAlongY,
                                                 .sprintHeld = (frame.buttons & coney::pad::kL2) != 0,
                                                 .actionPressed = false,
                                                 .command = command,
                                                 .buttons = frame.buttons,
                                                 .targets = {}};
            if (m_throughHumans) {
                // As human::Player writes the pad into the record before the characters' step.
                m_human.record() = coney::human::recordOf(input);
                m_humans.update(m_mesh.get());
            } else {
                m_human.step(input, m_mesh.get());
            }
            samples.push_back(Sample{.clip = m_human.animator().animId(), .gait = m_human.gait(), .speed = speed()});
        }
        return samples;
    }

    Human& human() { return m_human; }

  private:
    // The speed across the ground.
    [[nodiscard]] float speed() const { return std::hypot(m_human.velocity().x, m_human.velocity().y); }

    bool m_throughHumans;
    std::unique_ptr<coney::raycast::CollisionMesh> m_mesh =
        coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 120.0F));
    Human m_human{movingCharacter().anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                  &movingCharacter().ranges};
    Humans m_humans;
    combat::CommandTables m_tables = combat::CommandTables::street();
    combat::CommandMatcher m_matcher;
};

// The first update at or after `from` playing `clip`, or -1.
int firstOf(const std::vector<Sample>& samples, std::uint32_t clip, int from = 0) {
    for (auto k = static_cast<std::size_t>(from); k < samples.size(); ++k) {
        if (samples[k].clip == clip) {
            return static_cast<int>(k);
        }
    }
    return -1;
}

// The first update after `from` that no longer plays `clip`, or -1.
int endOf(const std::vector<Sample>& samples, std::uint32_t clip, int from) {
    for (auto k = static_cast<std::size_t>(from); k < samples.size(); ++k) {
        if (samples[k].clip != clip) {
            return static_cast<int>(k);
        }
    }
    return -1;
}

// The press frame used throughout: well into a steady walk, run or sprint.
constexpr int kPress = 60;

// The script: the stick at (`x`, `y`) % from update 5, L2 held from 30 when `l2`, and `button` tapped at kPress.
std::string script(int x, int y, std::string_view button, bool l2 = false) {
    std::string text = "5 stick left " + std::to_string(x) + " " + std::to_string(y) + "\n";
    if (l2) {
        text += "30 press l2\n";
    }
    text += std::to_string(kPress) + " tap " + std::string(button) + "\n";
    return text;
}

// The update an attack by `button` pressed at kPress starts: square on its press, cross on its release (0x10).
int startOf(std::string_view button) { return button == "cross" ? kPress + 1 : kPress; }

} // namespace

TEST_CASE("square or cross at a run plays the run attack 24 and the run resumes", "[human][combat][moving]") {
    for (const bool throughHumans : {true, false}) {
        for (const std::string_view button : {"square", "cross"}) {
            INFO((throughHumans ? "through Humans" : "through Human::step") << ", " << button);
            // Full ahead, and a full diagonal (the squared-off stick's corner, longer than 1 and clamped).
            for (const auto& [x, y] : {std::pair{0, 100}, std::pair{71, 71}, std::pair{-100, 0}}) {
                INFO("stick " << x << ", " << y);
                Runner runner(throughHumans);
                const std::vector<Sample> line = runner.run(script(x, y, button), 130);
                const int start = startOf(button);
                REQUIRE(line[start - 1].gait == Gait::Run);
                CHECK(firstOf(line, id::kAttackS1) == -1);
                CHECK(firstOf(line, id::kAttackX1) == -1);
                REQUIRE(firstOf(line, id::kAttackFromRun) == start);
                // The clip carries him on by its root (7.09 m/s at its own rate, 0.75 here); it does not stop him.
                CHECK(line[start + 5].speed > 5.0F);
                // With the stick still full the run's clips take over at its end, at the run's speed.
                const int over = endOf(line, id::kAttackFromRun, start);
                REQUIRE(over > start);
                CHECK((line[over].clip == kRun || line[over].clip == kSprint));
                CHECK(line[over + 5].gait == Gait::Run);
            }
        }
    }
}

TEST_CASE("square or cross at a walk plays the walk attack 23, then he walks on", "[human][combat][moving]") {
    for (const bool throughHumans : {true, false}) {
        for (const std::string_view button : {"square", "cross"}) {
            INFO((throughHumans ? "through Humans" : "through Human::step") << ", " << button);
            // 40 % and 90 %: both walk (the run needs more than 0.95).
            for (const int deflection : {40, 90}) {
                INFO("stick " << deflection << " %");
                Runner runner(throughHumans);
                const std::vector<Sample> line = runner.run(script(0, deflection, button), 120);
                const int start = startOf(button);
                REQUIRE(line[start - 1].gait == Gait::Walk);
                CHECK(firstOf(line, id::kAttackS1) == -1);
                CHECK(firstOf(line, id::kAttackX1) == -1);
                REQUIRE(firstOf(line, id::kAttackFromWalk) == start);
                const int over = endOf(line, id::kAttackFromWalk, start);
                REQUIRE(over > start);
                // The stick moves him again as the clip ends: the walk start, then the walk.
                CHECK(firstOf(line, kWalkStart, over) == over);
                CHECK(firstOf(line, kWalk, over) > over);
            }
        }
    }
}

TEST_CASE("L2 + cross at a run charges and L2 + square dives; the run resumes after them", "[human][combat][moving]") {
    struct Case {
        std::string_view button;
        int clip;
    };
    for (const bool throughHumans : {true, false}) {
        // Bare hands and a bat: the charge and the dive are the same (combat.md#armed-moves).
        for (const int set : {0, 3}) {
            for (const Case& c : {Case{"cross", id::kRunningAttackCharge}, Case{"square", id::kRunningAttackDive}}) {
                INFO((throughHumans ? "through Humans" : "through Human::step")
                     << ", set " << set << ", L2 + " << c.button);
                Runner runner(throughHumans);
                runner.human().fighter().setAnimSet(set);
                const std::vector<Sample> line = runner.run(script(0, 100, c.button, true), 160);
                // L2 held from 30 sprints (gait 5), where the charge and the dive start too.
                REQUIRE(line[kPress - 1].gait == Gait::Sprint);
                for (const int other : {id::kAttackS1, id::kAttackX1, id::kAttackFromRun, id::kArmedAttackFromRun}) {
                    CHECK(firstOf(line, static_cast<std::uint32_t>(other)) == -1);
                }
                REQUIRE(firstOf(line, static_cast<std::uint32_t>(c.clip)) == kPress);
                const int over = endOf(line, static_cast<std::uint32_t>(c.clip), kPress);
                REQUIRE(over > kPress);
                CHECK((line[over].clip == kRun || line[over].clip == kSprint));
            }
        }
    }
}

TEST_CASE("L2 + cross or square at a walk neither charges nor dives", "[human][combat][moving]") {
    for (const bool throughHumans : {true, false}) {
        for (const std::string_view button : {"cross", "square"}) {
            INFO((throughHumans ? "through Humans" : "through Human::step") << ", L2 + " << button);
            Runner runner(throughHumans);
            // The stick at 60 % walks even with L2 held.
            const std::vector<Sample> line = runner.run(script(0, 60, button, true), 100);
            REQUIRE(line[kPress - 1].gait == Gait::Walk);
            for (const int clip : {id::kRunningAttackCharge, id::kRunningAttackDive, id::kAttackS1, id::kAttackX1}) {
                CHECK(firstOf(line, static_cast<std::uint32_t>(clip)) == -1);
            }
            // The combination overwrites the press, but cross's release still gives its 0x10, which starts cross's
            // walk attack as any cross tap at a walk does (combat.md#commands); square has nothing on its release.
            CHECK(firstOf(line, id::kAttackFromWalk) == (button == "cross" ? kPress + 1 : -1));
        }
    }
}

TEST_CASE("at a sprint square has no moving attack (S1) and cross plays the run attack", "[human][combat][moving]") {
    for (const bool throughHumans : {true, false}) {
        for (const std::string_view button : {"square", "cross"}) {
            INFO((throughHumans ? "through Humans" : "through Human::step") << ", " << button);
            // Square's run test is gait 4 exactly; cross's is gait 4 or 5 (combat.md#armed-moves). L2 is let go just
            // before the press so the command is the button's own, not L2's combination.
            Runner runner(throughHumans);
            const std::string text = "5 stick left 0 100\n30 press l2\n" + std::to_string(kPress - 1) +
                                     " release l2\n" + std::to_string(kPress) + " tap " + std::string(button) + "\n";
            const std::vector<Sample> line = runner.run(text, 100);
            REQUIRE(line[kPress - 1].gait == Gait::Sprint);
            if (button == "square") {
                CHECK(firstOf(line, id::kAttackFromRun) == -1);
                CHECK(firstOf(line, id::kAttackS1) == kPress);
            } else {
                CHECK(firstOf(line, id::kAttackX1) == -1);
                CHECK(firstOf(line, id::kAttackFromRun) == kPress + 1);
            }
        }
    }
}

TEST_CASE("with a bat, square or cross at a run plays 501 and the run resumes", "[human][combat][moving]") {
    for (const bool throughHumans : {true, false}) {
        for (const std::string_view button : {"square", "cross"}) {
            INFO((throughHumans ? "through Humans" : "through Human::step") << ", " << button);
            Runner runner(throughHumans);
            runner.human().fighter().setAnimSet(3);
            const std::vector<Sample> line = runner.run(script(0, 100, button), 120);
            const int start = startOf(button);
            REQUIRE(line[start - 1].gait == Gait::Run);
            for (const int other : {34, 36, id::kAttackFromRun}) {
                CHECK(firstOf(line, static_cast<std::uint32_t>(other)) == -1);
            }
            REQUIRE(firstOf(line, id::kArmedAttackFromRun) == start);
            CHECK(line[start + 5].speed > 5.0F);
            const int over = endOf(line, id::kArmedAttackFromRun, start);
            REQUIRE(over > start);
            CHECK((line[over].clip == kRun || line[over].clip == kSprint));
        }
    }
}

TEST_CASE("with a bat, square or cross at a walk or standing swings where he is", "[human][combat][moving]") {
    struct Case {
        std::string_view button;
        std::uint32_t swing;
    };
    for (const bool throughHumans : {true, false}) {
        for (const Case& c : {Case{"square", 34}, Case{"cross", 36}}) {
            // No walk attack with a bat in hand: the standing swing, and he stops.
            for (const int deflection : {0, 60}) {
                INFO((throughHumans ? "through Humans" : "through Human::step")
                     << ", " << c.button << ", stick " << deflection << " %");
                Runner runner(throughHumans);
                runner.human().fighter().setAnimSet(3);
                const std::vector<Sample> line = runner.run(script(0, deflection, c.button), 120);
                const int start = startOf(c.button);
                if (deflection > 0) {
                    REQUIRE(line[start - 1].gait == Gait::Walk);
                }
                CHECK(firstOf(line, id::kAttackFromWalk) == -1);
                REQUIRE(firstOf(line, c.swing) == start);
                CHECK(line[start + 3].speed < 0.5F);
            }
        }
    }
}
