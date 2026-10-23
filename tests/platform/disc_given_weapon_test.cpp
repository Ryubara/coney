// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that a weapon given to a Warrior is held, hangs from his hand, is drawn and is
// fought with (issue #78, docs/research/objects.md#given-weapon): `level80` checkpoint 3 (the crew free of the cuffs)
// played in the game session as `coney --play-level level80 --checkpoint 3` plays it (support/disc_session.h), its
// opening scene skipped with the pad. Player 1 is given a bat; stood 1 m in front of a Warrior of his crew, he hands
// it over with triangle, the "Give Weapon" swap (docs/research/ai.md#warrior-swap). The Warrior then holds
// it, it hangs from his right hand (bone 25, the pick-up clip's take event), it is among the objects drawn in a hand,
// his square and cross choose the bat's swings, and it stays in his hand once he moves on. It runs only when the
// environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "combat/attacks.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_pickups.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "human/place.h"
#include "human/player.h"
#include "human/script_state.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/live_pad.h"
#include "world_objects/spawn_records.h"

namespace {

// How long anything here may take: 60 s of frames.
constexpr std::uint64_t kLimit = 1800;
// The weapon handed over: the bat the original's check used (its anim set, 3, gives the swings 34, 36 and 37;
// docs/research/objects.md#given-weapon, step 2).
constexpr const char* kBat = "dyn_bat";
constexpr int kBatSet = 3;
// The right hand, the bone of the bat's pick-up clip's take event (docs/research/combat-throws.md#bat).
constexpr std::size_t kRightHand = 25;
// How far from his feet a held bat may hang, metres: in plan within his arm's reach, in height above the ground and
// below his head.
constexpr float kReach = 1.2F;
constexpr float kLowest = 0.2F;
constexpr float kHighest = 1.9F;

// A pad script that taps cross every second for the first 20 s of play: it skips the level's opening scene.
constexpr const char* kSkipScene = "30 tap cross\n60 tap cross\n90 tap cross\n120 tap cross\n150 tap cross\n"
                                   "180 tap cross\n210 tap cross\n240 tap cross\n270 tap cross\n300 tap cross\n"
                                   "330 tap cross\n360 tap cross\n390 tap cross\n420 tap cross\n450 tap cross\n"
                                   "480 tap cross\n510 tap cross\n540 tap cross\n570 tap cross\n600 tap cross\n";

// The draw of the object in `holder`'s hand, if any.
std::optional<coney::platform::PlayLevelMode::HeldDraw> drawnIn(const coney::platform::PlayLevelMode& play,
                                                                const coney::human::Human& holder) {
    for (const coney::platform::PlayLevelMode::HeldDraw& draw : play.heldDraws()) {
        if (draw.holder == &holder) {
            return draw;
        }
    }
    return std::nullopt;
}

// Whether `draw` hangs where a hand can hold it: within reach of `holder`'s feet, between the ground and his head.
bool inReach(const coney::platform::PlayLevelMode::HeldDraw& draw, const coney::human::Human& holder) {
    const coney::anim::Vec3 feet = holder.position();
    const float plan = std::hypot(draw.pose.position.x - feet.x, draw.pose.position.y - feet.y);
    const float up = draw.pose.position.z - feet.z;
    return plan <= kReach && up >= kLowest && up <= kHighest;
}

} // namespace

TEST_CASE("the disc's level80 checkpoint 3: a bat given to a Warrior is held, drawn in his hand and swung as a bat",
          "[disc][story][pickup]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::LivePad pad;
    pad.play(kSkipScene);
    coney::test::DiscSession run(*wad, pad);
    run.session().startAtLevel("level80", 3);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runUntil(
        [&run] { return run.session().play() != nullptr && run.session().play()->player().padControlled(); }, kLimit));
    run.run(30);
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    coney::human::Human& player = const_cast<coney::human::Player&>(play->player()).human();

    // Player 1 and a Warrior of his crew (the test steers both, so it writes to brains gameplay owns).
    const coney::ai::Brains* found = run.session().gameplay().brains();
    REQUIRE(found != nullptr);
    auto& brains = const_cast<coney::ai::Brains&>(*found);
    coney::ai::Brain* chief = nullptr;
    for (std::size_t i = 0; i < brains.size(); ++i) {
        if (brains.at(i).type() == coney::ai::BrainType::Player) {
            chief = &brains.at(i);
        }
    }
    REQUIRE(chief != nullptr);
    coney::ai::Brain* warrior = nullptr;
    for (std::size_t i = 0; i < brains.size() && warrior == nullptr; ++i) {
        coney::ai::Brain& brain = brains.at(i);
        if (brain.type() == coney::ai::BrainType::Warrior && brain.gang() != nullptr && brain.gang() == chief->gang()) {
            warrior = &brain;
        }
    }
    REQUIRE(warrior != nullptr);
    coney::human::Human& member = warrior->human();
    REQUIRE(member.script().heldObject == 0.0);

    // A bat put in player 1's hand as a pick-up leaves it (**the test's stand-in** for the pick-up itself, which other
    // tests cover): a record of the type, taken into the hand, then held by him; his next step gives him its anim set.
    coney::world_objects::SpawnRecord made;
    made.handle = run.session().flow().scripts().nextObjectHandle();
    REQUIRE(made.handle != 0.0);
    made.typeName = kBat;
    const coney::anim::Vec3 feet = player.position();
    made.position = {feet.x, feet.y, feet.z};
    made.live = true;
    REQUIRE(run.session().flow().spawnRecords().add(made) != nullptr);
    const double bat = made.handle;
    REQUIRE(run.session().gameplay().pickups() != nullptr);
    auto& pickups = const_cast<coney::LevelPickups&>(*run.session().gameplay().pickups());
    REQUIRE(pickups.take(bat, 0) == coney::TakeResult::InHand);
    player.script().heldObject = bat;
    player.script().heldObjectName = kBat;
    run.run(2);
    REQUIRE(player.fighter().animSet() == kBatSet);
    REQUIRE(drawnIn(*play, player).has_value());

    // Stood 1 m east of the Warrior, facing him (heading 90 degrees faces -x), once he stands still; his goals are
    // held (the brain suspended, as BrSuspend does; his thinks still run the prompt's test), or his follow goal would
    // run him back to his formation slot, out of the swap's reach. Still means standing at no speed for a think
    // period (his brain thinks one step in five).
    constexpr int kThinkPeriod = 6;
    constexpr float kStill = 1e-3F;
    const auto standsStill = [&member] {
        return [&member, standing = 0]() mutable {
            const coney::anim::Vec3 v = member.velocity();
            const bool still = member.gait() == coney::human::Gait::Standing && std::hypot(v.x, v.y) < kStill;
            standing = still ? standing + 1 : 0;
            return standing >= kThinkPeriod;
        };
    };
    REQUIRE(run.runUntil(standsStill(), kLimit));
    warrior->setSuspended(true);
    const coney::anim::Vec3 at = member.position();
    play->teleport(coney::human::Place{
        .name = "give", .feet = coney::anim::Vec3{at.x + 1.0F, at.y, at.z}, .headingDegrees = 90.0F});
    REQUIRE(run.runUntil(standsStill(), kLimit));

    // Triangle gives it: it leaves player 1's hand and is in the Warrior's at once.
    const std::uint64_t start = pad.nextFrame() + 2;
    pad.play(std::format("{} tap triangle\n", start));
    REQUIRE(run.runUntil([&member, bat] { return member.script().heldObject == bat; }, kLimit));
    CHECK(player.script().heldObject == 0.0);
    CHECK(member.script().heldObjectName == kBat);
    CHECK(pickups.inHand(bat));
    CHECK(member.fighter().animSet() == kBatSet);

    // It hangs from his right hand and is drawn there, and not in player 1's.
    run.run(1);
    std::optional<coney::platform::PlayLevelMode::HeldDraw> held = drawnIn(*play, member);
    REQUIRE(held.has_value());
    // The guard an `if`: clang-tidy does not follow REQUIRE.
    if (held.has_value()) {
        CHECK(held->handle == bat);
        CHECK(held->bone == kRightHand);
        CHECK(inReach(*held, member));
        std::printf("  level80 given bat: held %.0f on bone %zu, %.2f m above his feet\n", held->handle, held->bone,
                    held->pose.position.z - member.position().z);
    }
    CHECK_FALSE(drawnIn(*play, player).has_value());

    // His attacks are the bat's: an AI attacks through the player's dispatcher
    // (docs/research/combat-targets.md#ai-attacks), whose square and cross take the held set's swings (34 and 36 for a
    // bat, 37 at a grounded target; docs/research/combat-throws.md#armed-moves), from the anim set the swap gave him.
    coney::combat::SquareInput press;
    press.heldSet = member.fighter().animSet();
    press.fightStance = true;
    CHECK(coney::combat::squareAttack(press) == 34);
    CHECK(coney::combat::crossAttack(press) == 36);
    press.target = coney::combat::TargetKind::Grounded;
    CHECK(coney::combat::squareAttack(press) == 37);

    // Free again, he keeps it in his hand as he goes on.
    warrior->setSuspended(false);
    run.run(90);
    held = drawnIn(*play, member);
    REQUIRE(held.has_value());
    if (held.has_value()) {
        CHECK(held->handle == bat);
        CHECK(inReach(*held, member));
    }
}
