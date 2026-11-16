// SPDX-License-Identifier: GPL-3.0-or-later
// A downed player mounted by another human, and getting out from under him (docs/research/combat-reactions.md#ground-
// escape, docs/research/combat-moves.md#mounted-victim): an AI's circle at the player lying in the grounded idle mounts
// him with the grounded mount (237 + the side on the mounter, 241 on him, then the mount's idles 210 / 207); not during
// the knockdown's reaction, not past half of his rise, not from beyond the clip's far range. Mounted, his square
// struggles (250 / 251: the mounter struck and drained), the mounter's power running out lets him escape (246 / 247),
// circle breaks out on the roll (246 / 247) and R1 reverses (242 / 243, then he is on top); free again, his square
// counters at once. Synthetic clips with the runtime's lengths in updates; the player's pad comes from input scripts,
// the other human's commands from the test (its brain's, in play).
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "combat/commands.h"
#include "combat/player_combat.h"
#include "combat/power_class.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/human_flags.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

using coney::anim::Vec3;
using coney::human::Human;
using coney::human::IncomingHit;
using coney::human::TargetState;
using coney::test::FightCharacter;
namespace combat = coney::combat;
namespace flag = coney::human::flag;

namespace {

// The clips of the mount and their lengths at runtime, in updates (combat-reactions.md#ground-escape): the grounded
// mount 237-240 and its reaction 241 (27 updates to the mount's idles), the reversal 242 / 243 (57), the break 246 /
// 247 (48) and the struggle 250 / 251 (16). With no rate flag a clip plays at 0.75, so n updates last n × 0.75 / 30 s.
std::vector<coney::test::LocomotionClip> mountClips() {
    std::vector<coney::test::LocomotionClip> clips = coney::test::fightClips();
    const std::map<std::uint32_t, int> updates{{237, 27}, {238, 27}, {239, 27}, {240, 27}, {241, 27}, {242, 57},
                                               {243, 57}, {246, 48}, {247, 48}, {250, 16}, {251, 16}};
    for (const auto& [id, count] : updates) {
        clips.push_back({.id = id,
                         .speed = 0.0F,
                         .duration = static_cast<float>(count) * 0.75F / 30.0F,
                         .rootVelocity = 0.0F,
                         .rangeFlags = 0,
                         .reach = 0.0F,
                         .knockdown = false});
    }
    return clips;
}

// The character with those clips; the struggle 250 deals the 40 the runtime's mounter lost.
struct MountCharacter {
    MountCharacter() { character.ranges = coney::test::fightRanges({}, {{250, 40}}); }
    FightCharacter character{mountClips()};
};

// The player at (40, 40) facing +y on a floor, driven by a pad, and a human no player controls `left` metres to his
// left (or `ahead` in front), facing him, a civilian with 600 health and 200 power whose commands the test gives. The
// player is stepped first, then the other human, each with the other as its only target.
class MountFight {
  public:
    MountFight(const FightCharacter& character, float left, float ahead = 0.0F)
        : m_mesh(coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F))),
          m_player(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                   &character.ranges),
          m_other(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F,
                  &character.ranges) {
        m_player.spawn(m_mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
        m_other.setFighterProfile(
            coney::human::FighterProfile{.player = false, .powerClass = combat::kCivilianPowerClass, .health = 600});
        m_other.spawn(m_mesh.get(), Vec3{40.0F - left, 40.0F + ahead, 0.0F}, left != 0.0F ? -90.0F : 180.0F);
    }

    // Runs `frames` updates of `script`, calling `each` after every update with its index.
    template <typename Each> void run(std::string_view script, std::uint64_t frames, Each each) {
        for (const coney::test::PadFrame& frame : coney::test::playScript(script, frames)) {
            const combat::CommandId command =
                m_matcher.update(frame.buttons, m_tables, combat::combatTuning().historyHoldSamples);
            m_player.step(coney::human::HumanInput{.stickX = frame.leftX,
                                                   .stickY = frame.leftY,
                                                   .cameraForward = Vec3{0.0F, 1.0F, 0.0F},
                                                   .sprintHeld = false,
                                                   .actionPressed = false,
                                                   .command = command,
                                                   .buttons = frame.buttons,
                                                   .targets = m_otherTarget},
                          m_mesh.get());
            const auto found = otherCommands.find(m_frame);
            m_other.step(coney::human::HumanInput{.stickX = 0.0F,
                                                  .stickY = 0.0F,
                                                  .cameraForward = Vec3{0.0F, 1.0F, 0.0F},
                                                  .sprintHeld = false,
                                                  .actionPressed = false,
                                                  .command = found != otherCommands.end() ? found->second
                                                                                          : combat::command::kNone,
                                                  .buttons = 0,
                                                  .targets = m_playerTarget},
                         m_mesh.get());
            each(m_frame++);
        }
    }
    // Runs `frames` updates of `script`.
    void run(std::string_view script, std::uint64_t frames) {
        run(script, frames, [](std::uint64_t /*frame*/) {});
    }
    // Runs single updates of `script` until `done` holds or `limit` updates have passed; whether it held.
    bool runUntil(const std::function<bool()>& done, std::uint64_t limit) {
        for (std::uint64_t update = 0; update < limit; ++update) {
            if (done()) {
                return true;
            }
            run("", 1);
        }
        return done();
    }

    Human& player() { return m_player; }
    Human& other() { return m_other; }
    // The update the next run starts on.
    [[nodiscard]] std::uint64_t frame() const { return m_frame; }
    // The other human's command on each update, by the update's index.
    std::map<std::uint64_t, combat::CommandId> otherCommands;

  private:
    std::unique_ptr<coney::raycast::CollisionMesh> m_mesh;
    Human m_player;
    Human m_other;
    std::array<coney::human::Combatant*, 1> m_otherTarget{&m_other};
    std::array<coney::human::Combatant*, 1> m_playerTarget{&m_player};
    combat::CommandTables m_tables = combat::CommandTables::street();
    combat::CommandMatcher m_matcher;
    std::uint64_t m_frame = 0;
};

// The crushing special 653 (code 0x39, stunning) from `attacker`, as the runtime's puppet knocked the player down.
IncomingHit crushingFrom(Vec3 attacker) {
    IncomingHit hit;
    hit.damage = 50;
    hit.attackAnim = 653;
    hit.code = 0x39;
    hit.flags = 0x400;
    hit.attacker = attacker;
    return hit;
}

// Knocks the player down with 653 from the other human and runs until he lies in the grounded idle 196.
void knockDown(MountFight& fight) {
    fight.player().takeHit(crushingFrom(fight.other().position()));
    REQUIRE(fight.runUntil([&] { return fight.player().animator().animId() == 196U; }, 60));
}

// The clips each human played, one entry per change.
struct Clips {
    std::vector<std::uint32_t> player;
    std::vector<std::uint32_t> other;
    // Notes the clips the two humans play now.
    void note(const Human& player_, const Human& other_) {
        if (player.empty() || player.back() != player_.animator().animId()) {
            player.push_back(player_.animator().animId());
        }
        if (other.empty() || other.back() != other_.animator().animId()) {
            other.push_back(other_.animator().animId());
        }
    }
};

// Whether `clips` holds `id`.
bool played(const std::vector<std::uint32_t>& clips, std::uint32_t id) {
    return std::ranges::find(clips, id) != clips.end();
}

// The update index on which the other human presses `command` next: the run's next update.
void otherPresses(MountFight& fight, combat::CommandId command, std::uint64_t after = 0) {
    fight.otherCommands[fight.frame() + after] = command;
}

// Knocks the player down and lets the other human's circle held mount him; runs until both sit in the mount's idles.
void mounted(MountFight& fight) {
    knockDown(fight);
    otherPresses(fight, combat::command::kCircleHeld);
    REQUIRE(fight.runUntil(
        [&] { return fight.player().animator().animId() == 207U && fight.other().animator().animId() == 210U; }, 60));
}

} // namespace

TEST_CASE("an AI's circle held during the knockdown's reaction tackles at nobody; at the player lying in 196 it mounts "
          "him: 240 from his left on it, 241 on him, then 210 / 207",
          "[human][combat]") {
    // Expected: combat-reactions.md#ground-escape, confirmed (runtime)
    const MountCharacter mount;
    // 1. During the knockdown's reaction he is not mounted: the circle held finds nobody and its tackle misses.
    {
        MountFight early(mount.character, 0.8F);
        early.player().takeHit(crushingFrom(early.other().position()));
        early.run("", 2);
        REQUIRE(early.player().animator().animId() != 196U);
        otherPresses(early, combat::command::kCircleHeld);
        Clips during;
        early.run("", 30, [&](std::uint64_t /*frame*/) { during.note(early.player(), early.other()); });
        for (std::uint32_t id = 237; id <= 241; ++id) {
            CHECK_FALSE(played(during.other, id));
            CHECK_FALSE(played(during.player, id));
        }
        CHECK(played(during.other, 2U)); // the tackle's miss
        CHECK(early.player().state() != TargetState::Mounted);
    }
    // 2. In a fight of its own, lying in the grounded idle he is mounted from his left.
    MountFight fight(mount.character, 0.8F);
    knockDown(fight);
    otherPresses(fight, combat::command::kCircleHeld);
    int sameUpdate = 0;
    Clips clips;
    fight.run("", 40, [&](std::uint64_t /*frame*/) {
        clips.note(fight.player(), fight.other());
        if (fight.other().animator().animId() == 240U && fight.player().animator().animId() == 241U) {
            ++sameUpdate;
        }
    });
    CHECK(sameUpdate > 0);
    CHECK_FALSE(played(clips.other, 4U)); // not the tackle
    CHECK(fight.other().animator().animId() == 210U);
    CHECK(fight.player().animator().animId() == 207U);
    CHECK(fight.player().state() == TargetState::Mounted);
    CHECK_FALSE(fight.player().fighter().victim().grounded());
    CHECK(fight.other().fighter().combat().mode() == combat::CombatMode::Tackling);
    CHECK(fight.other().fighter().held() == &fight.player());
}

TEST_CASE("a circle tap at the downed player mounts him too, from the front with 237", "[human][combat]") {
    // Expected: combat-reactions.md#ground-escape, confirmed (code)
    const MountCharacter mount;
    MountFight fight(mount.character, 0.0F, 0.8F);
    knockDown(fight);
    otherPresses(fight, combat::command::kCircleTapped);
    Clips clips;
    fight.run("", 40, [&](std::uint64_t /*frame*/) { clips.note(fight.player(), fight.other()); });
    CHECK(played(clips.other, 237U));
    CHECK(played(clips.player, 241U));
    CHECK(fight.player().state() == TargetState::Mounted);
}

TEST_CASE("a downed human beyond the grounded mount's far range is not mounted, and nothing plays", "[human][combat]") {
    // Expected: combat-reactions.md#ground-escape, confirmed (code)
    // 240's far range is the fixture's 1.25 m; an AI's is not scaled by 1.25 (a player's is).
    const MountCharacter mount;
    MountFight fight(mount.character, 2.0F);
    knockDown(fight);
    otherPresses(fight, combat::command::kCircleHeld);
    Clips clips;
    fight.run("", 30, [&](std::uint64_t /*frame*/) { clips.note(fight.player(), fight.other()); });
    CHECK_FALSE(played(clips.other, 240U));
    CHECK_FALSE(played(clips.other, 4U));
    CHECK(fight.player().state() == TargetState::Grounded);
}

TEST_CASE("the rise is cut by the mount below half its playing; past half he is not mounted", "[human][combat]") {
    // Expected: combat-reactions.md#ground-escape, confirmed (code)
    const MountCharacter mount;
    for (const bool early : {true, false}) {
        INFO("early " << early);
        MountFight fight(mount.character, 0.8F);
        knockDown(fight);
        REQUIRE(fight.runUntil([&] { return fight.player().animator().animId() == 199U; }, 150));
        // The synthetic 199 lasts 0.4 s of clip at 0.75: 16 updates; its first update or its 12th.
        if (!early) {
            fight.run("", 11);
        }
        otherPresses(fight, combat::command::kCircleHeld);
        Clips clips;
        fight.run("", 10, [&](std::uint64_t /*frame*/) { clips.note(fight.player(), fight.other()); });
        CHECK(played(clips.other, 240U) == early);
        CHECK(played(clips.player, 241U) == early);
    }
}

TEST_CASE("mounted, square struggles: 250 on him, 251 on the mounter, which loses 40 health and a third of its power",
          "[human][combat]") {
    // Expected: combat-moves.md#mounted-victim, confirmed (runtime)
    const MountCharacter mount;
    MountFight fight(mount.character, 0.8F);
    mounted(fight);
    const int healthBefore = fight.other().fighter().health().value();
    const int powerBefore = fight.other().fighter().combat().power().value();
    Clips clips;
    fight.run("0 tap square\n", 6, [&](std::uint64_t /*frame*/) { clips.note(fight.player(), fight.other()); });
    CHECK(played(clips.player, 250U));
    CHECK(played(clips.other, 251U));
    CHECK(healthBefore - fight.other().fighter().health().value() == 40);
    // The civilian's 200 over the player's divisor 3: 66, and the tackle's own drain on top.
    const int powerLost = powerBefore - fight.other().fighter().combat().power().value();
    CHECK(powerLost >= 66);
    CHECK(powerLost <= 66 + 5);
    CHECK(fight.player().state() == TargetState::Mounted);
    // Both go back to the mount's idles.
    fight.run("", 20);
    CHECK(fight.player().animator().animId() == 207U);
    CHECK(fight.other().animator().animId() == 210U);
}

TEST_CASE("struggling until the mounter's power runs out lets him escape: 246 on him, 247 on it, both free",
          "[human][combat]") {
    // Expected: combat-reactions.md#ground-escape, confirmed (runtime)
    const MountCharacter mount;
    MountFight fight(mount.character, 0.8F);
    mounted(fight);
    Clips clips;
    fight.run("0 tap square\n20 tap square\n40 tap square\n60 tap square\n", 90,
              [&](std::uint64_t /*frame*/) { clips.note(fight.player(), fight.other()); });
    CHECK(played(clips.player, 246U));
    CHECK(played(clips.other, 247U));
    CHECK(fight.player().state() == TargetState::Standing);
    CHECK_FALSE(fight.player().fighter().inPair());
    CHECK(fight.other().fighter().held() == nullptr);
    CHECK(fight.other().fighter().combat().mode() == combat::CombatMode::Free);
}

TEST_CASE("circle breaks out once the mounter is at a quarter of its power: 246 / 247, no damage, then his square "
          "counters at once",
          "[human][combat]") {
    // Expected: combat-reactions.md#ground-escape, confirmed (runtime)
    const MountCharacter mount;
    MountFight fight(mount.character, 0.8F);
    mounted(fight);
    fight.other().fighter().combat().power().set(50);
    // The mounted human reads the mounter's numbers as its last update left them.
    fight.run("", 1);
    const int healthBefore = fight.other().fighter().health().value();
    Clips clips;
    fight.run("0 tap circle\n", 4, [&](std::uint64_t /*frame*/) { clips.note(fight.player(), fight.other()); });
    CHECK(played(clips.player, 246U));
    CHECK(played(clips.other, 247U));
    CHECK(fight.other().fighter().health().value() == healthBefore);
    CHECK(fight.player().state() == TargetState::Standing);
    // 246 holds him for its 48 updates; then square strikes the human who held him down.
    fight.run("", 50);
    const int beforeCounter = fight.other().fighter().health().value();
    fight.run("0 tap square\n", 30, [&](std::uint64_t /*frame*/) { clips.note(fight.player(), fight.other()); });
    CHECK(played(clips.player, 12U));
    CHECK(fight.other().fighter().health().value() < beforeCounter);
}

TEST_CASE("with the no-escape flag circle never breaks out", "[human][combat]") {
    // Expected: combat-moves.md#mounted-victim, confirmed (code)
    const MountCharacter mount;
    MountFight fight(mount.character, 0.8F);
    mounted(fight);
    fight.player().setFlag(flag::kNoEscape, true);
    fight.other().fighter().combat().power().set(50);
    // The mounted human reads the mounter's numbers as its last update left them.
    fight.run("", 1);
    Clips clips;
    fight.run("0 tap circle\n10 tap circle\n", 20,
              [&](std::uint64_t /*frame*/) { clips.note(fight.player(), fight.other()); });
    CHECK_FALSE(played(clips.player, 246U));
    CHECK(fight.player().state() == TargetState::Mounted);
}

TEST_CASE("R1 reverses the mount: 242 on him, 243 on the mounter, then he sits on it and his square strikes it",
          "[human][combat]") {
    // Expected: combat-reactions.md#ground-escape, confirmed (runtime)
    const MountCharacter mount;
    MountFight fight(mount.character, 0.8F);
    mounted(fight);
    fight.other().fighter().combat().power().set(50);
    // The mounted human reads the mounter's numbers as its last update left them.
    fight.run("", 1);
    Clips clips;
    fight.run("0 tap r1\n", 4, [&](std::uint64_t /*frame*/) { clips.note(fight.player(), fight.other()); });
    CHECK(played(clips.player, 242U));
    CHECK(played(clips.other, 243U));
    // 57 updates of the reversal, then the roles have swapped.
    fight.run("", 62);
    CHECK(fight.player().animator().animId() == 210U);
    CHECK(fight.other().animator().animId() == 207U);
    CHECK(fight.other().state() == TargetState::Mounted);
    CHECK(fight.player().fighter().held() == &fight.other());
    CHECK(fight.player().fighter().combat().mode() == combat::CombatMode::Tackling);
    CHECK(fight.player().fighter().mountReversals() == 1);
    const int healthBefore = fight.other().fighter().health().value();
    fight.run("0 tap square\n", 20);
    CHECK(healthBefore - fight.other().fighter().health().value() == 61);
}

TEST_CASE("no reversal of a mounter with flag 0x40, nor for a player with the block-tackle flag", "[human][combat]") {
    // Expected: combat-moves.md#mounted-victim, confirmed (code)
    const MountCharacter mount;
    for (const bool mounterFlag : {true, false}) {
        INFO("the mounter's flag " << mounterFlag);
        MountFight fight(mount.character, 0.8F);
        mounted(fight);
        if (mounterFlag) {
            fight.other().setFlag(flag::kUngrabbable, true);
        } else {
            fight.player().setFlag(flag::kBlockTackle, true);
        }
        fight.other().fighter().combat().power().set(50);
        // The mounted human reads the mounter's numbers as its last update left them.
        fight.run("", 1);
        Clips clips;
        fight.run("0 tap r1\n", 6, [&](std::uint64_t /*frame*/) { clips.note(fight.player(), fight.other()); });
        CHECK_FALSE(played(clips.player, 242U));
        CHECK(fight.player().state() == TargetState::Mounted);
    }
}
