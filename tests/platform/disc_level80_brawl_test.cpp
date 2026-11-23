// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the Destroyers at `level80` checkpoint 4 fight on their own once Cochise
// and Swan's scene ends (issue 406: they stood lined up on the script's flags until someone hit one), and that the
// attack order turns the fight on the crew. Checkpoint 4 is played in the game session as `coney --play-level level80
// --checkpoint 4` plays it (support/disc_session.h): player 1 is put 1 m outside Cochise and Swan's volume and walked
// into it with the left stick at 70 %, the scene is skipped with cross, and from the scene's end every Destroyer's
// goals, target and place are read once an update. The pad then rests (the player stands still and presses nothing),
// except for the attack order in the second case (R2 held, the right stick up-left at 95 %). It runs only when the
// environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "ai/goal.h"
#include "core/pad.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "human/combatant.h"
#include "human/human.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/driven_pad.h"
#include "world_objects/flags.h"
#include "world_objects/volume_boxes.h"

namespace {

using coney::ai::Brain;
using coney::ai::GoalType;

// How long one step of the set-up may take: 60 s of frames.
constexpr std::uint64_t kWait = 1800;

// The fight's cast at the scene's end, found by the gangs the chapter's Setup makes.
struct Cast {
    std::vector<Brain*> destroyers; // Destroyers1's six, then Destroyers2's
    Brain* cochise = nullptr;       // CochiseSwan's two members, either way round
    Brain* swan = nullptr;
    Brain* player = nullptr;
    const coney::ai::Gang* destroyers1 = nullptr;
};

// The gang named `name` among `brains`' gangs, or null.
coney::ai::Gang* gangNamed(coney::ai::Brains& brains, std::string_view name) {
    for (std::size_t id = 0; id < coney::ai::kGangSlots; ++id) {
        coney::ai::Gang* gang = brains.gangs().find(static_cast<int>(id));
        if (gang != nullptr && gang->name() == name) {
            return gang;
        }
    }
    return nullptr;
}

// Plays checkpoint 4 up to the end of Cochise and Swan's scene: the scene's end is the first update the Destroyers'
// gang is no longer suspended (EndCochiseSwanScene resumes both gangs after the twenty-one teleports,
// docs/research/scripting.md#level80-fight-start). Returns the cast, read at that update.
Cast playToSceneEnd(coney::test::DiscSession& run, coney::test::DrivenPad& pad) {
    coney::platform::GameSession& session = run.session();
    const auto until = [&](const std::function<bool()>& done, std::uint64_t limit) {
        for (std::uint64_t i = 0; i < limit && !done(); ++i) {
            run.run(1);
        }
        return done();
    };
    const auto padded = [&session] { return session.play() != nullptr && session.play()->player().padControlled(); };
    session.startAtLevel("level80", 4);
    REQUIRE(run.runUntilPlay());
    REQUIRE(until(padded, kWait));
    run.run(60);

    // The brains and gangs gameplay owns (the test reads goals through the non-const finders).
    const coney::ai::Brains* owned = session.gameplay().brains();
    REQUIRE(owned != nullptr);
    auto& brains = const_cast<coney::ai::Brains&>(*owned);
    Cast cast;
    coney::ai::Gang* first = gangNamed(brains, "Destroyers1");
    coney::ai::Gang* second = gangNamed(brains, "Destroyers2");
    coney::ai::Gang* pair = gangNamed(brains, "CochiseSwan");
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(pair != nullptr);
    cast.destroyers1 = first;
    REQUIRE(first->suspended());

    // Walked into the meeting's volume from 1 m outside it, as the original's reference runs entered it.
    const std::optional<double> volume =
        session.flow().scripts().vm().global(std::string("vCochiseSwanTrigger")).number();
    REQUIRE(volume.has_value());
    REQUIRE(session.flow().context().boxes != nullptr);
    const coney::world_objects::VolumeBox* box = session.flow().context().boxes->find(volume.value_or(0.0));
    REQUIRE(box != nullptr);
    const coney::anim::Vec3 meeting{(box->low[0] + box->high[0]) / 2.0F, (box->low[1] + box->high[1]) / 2.0F,
                                    box->low[2]};
    const std::optional<double> flagHandle = session.flow().scripts().vm().global(std::string("fCochiseSwan")).number();
    REQUIRE(flagHandle.has_value());
    const coney::world_objects::WorldFlag* flag = session.flow().flags().find(flagHandle.value_or(0.0));
    REQUIRE(flag != nullptr);
    session.play()->teleportPlayer(coney::world_objects::Placement{
        .position = {flag->position[0], std::max(box->low[1], box->high[1]) + 1.0F, flag->position[2]},
        .headingDegrees = 180.0F});
    run.run(5);
    const bool met = until(
        [&] {
            if (!padded()) {
                return true;
            }
            const auto [x, y] = coney::test::stickTowards(*session.play(), meeting, 0.7F);
            pad.leftX = x;
            pad.leftY = y;
            return false;
        },
        kWait);
    pad.leftX = 0.0F;
    pad.leftY = 0.0F;
    REQUIRE(met);

    // The scene skipped with cross, tapped once a second, until the gangs are resumed.
    bool ended = false;
    for (std::uint64_t i = 0; i < kWait * 2 && !ended; ++i) {
        pad.buttons = i % 30 == 0 ? coney::pad::kCross : 0;
        run.run(1);
        ended = !first->suspended();
    }
    pad.buttons = 0;
    REQUIRE(ended);

    for (const coney::ai::Gang* gang : {first, second}) {
        for (Brain* member : gang->members()) {
            cast.destroyers.push_back(member);
        }
    }
    REQUIRE(pair->members().size() == 2);
    cast.cochise = pair->members()[0];
    cast.swan = pair->members()[1];
    for (std::size_t i = 0; i < brains.size(); ++i) {
        if (brains.at(i).type() == coney::ai::BrainType::Player) {
            cast.player = &brains.at(i);
        }
    }
    REQUIRE(cast.player != nullptr);
    return cast;
}

// Whether `brain` has the two goals of a fight pushed (Goal_Melee's FindEnemy and Melee).
bool fighting(Brain& brain) {
    return brain.findGoal(GoalType::FindEnemy) != nullptr && brain.findGoal(GoalType::Melee) != nullptr;
}

// The horizontal distance between two points, metres.
float apart(coney::anim::Vec3 a, coney::anim::Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }

} // namespace

TEST_CASE("the disc's level80 checkpoint 4: the Destroyers break their block and fight Cochise and Swan on their own",
          "[disc][story][level80-brawl]") {
    // Expected: scripting.md#level80-fight-timeline, confirmed (runtime)
    // Expected: scripting.md#level80-fight-start, confirmed (runtime)
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DrivenPad pad;
    coney::test::DiscSession run(*wad, pad);
    const Cast cast = playToSceneEnd(run, pad);
    REQUIRE(cast.destroyers.size() == 12);

    // The flags: where each stands at the scene's end.
    std::vector<coney::anim::Vec3> flags;
    for (const Brain* brain : cast.destroyers) {
        flags.push_back(brain->human().position());
    }
    // Once an update from the scene's end: the first update each has a fight pushed, how far each has gone from his
    // flag, how many fail "targets Cochise or Swan, or watches with a Spectate goal" from update 12 to 30, and any
    // update a Destroyer targets the idle player (15 s, the earliest order of the original's runs).
    constexpr std::uint64_t kWatch = 450;
    std::vector<std::uint64_t> firstFight(cast.destroyers.size(), 0);
    std::vector<float> farthest(cast.destroyers.size(), 0.0F);
    std::size_t failingAt12 = 0;
    std::size_t mostFailing = 0;
    std::size_t movedBy60 = 0;
    int playerTargeted = 0;
    for (std::uint64_t update = 1; update <= kWatch; ++update) {
        run.run(1);
        std::size_t failing = 0;
        for (std::size_t i = 0; i < cast.destroyers.size(); ++i) {
            Brain& brain = *cast.destroyers[i];
            if (firstFight[i] == 0 && fighting(brain)) {
                firstFight[i] = update;
            }
            farthest[i] = std::max(farthest[i], apart(brain.human().position(), flags[i]));
            const bool engaged = brain.target() == cast.cochise || brain.target() == cast.swan ||
                                 brain.findGoal(GoalType::Spectate) != nullptr;
            failing += engaged ? 0 : 1;
            playerTargeted += brain.target() == cast.player ? 1 : 0;
        }
        if (update == 12) {
            failingAt12 = failing;
        }
        if (update >= 12 && update <= 30) {
            mostFailing = std::max(mostFailing, failing);
        }
        if (update == 60) {
            movedBy60 = static_cast<std::size_t>(std::ranges::count_if(farthest, [](float d) { return d > 0.5F; }));
        }
    }
    const std::uint64_t lastFight = std::ranges::max(firstFight);
    const bool allFought = std::ranges::none_of(firstFight, [](std::uint64_t u) { return u == 0; });
    std::printf(
        "  level80 brawl: last Destroyer fighting at update %llu, %zu failing at 12, at most %zu from 12 to 30, "
        "%zu moved by 60, player targeted %d\n",
        static_cast<unsigned long long>(lastFight), failingAt12, mostFailing, movedBy60, playerTargeted);
    // Every Destroyer has FindEnemy and Melee by update 10 (the original: the last at 7 to 9).
    CHECK(allFought);
    CHECK(lastFight <= 10);
    // By update 12 all twelve target Cochise or Swan or watch; to update 30 at most one at a time fails it.
    CHECK(failingAt12 == 0);
    CHECK(mostFailing <= 1);
    // How many have left their flags by more than 0.5 m within 60 updates is printed, not checked: the original's 10
    // (run r-20261123-level80-block-breaks) needs the fight's band-keeping move, which Coney stands in for: its
    // waiting attackers stand inside the ring instead of backing off to it (scripting.md#level80-think-fight);
    // #1663 ports that move and adds the check.
    // Nobody goes for the idle player before the order: the Warriors are not their enemies yet.
    CHECK(playerTargeted == 0);
}

TEST_CASE("the disc's level80 checkpoint 4: after the attack order the Destroyers turn on the crew",
          "[disc][story][level80-brawl]") {
    // Expected: scripting.md#level80-after-order, confirmed (runtime)
    // Expected: scripting.md#level80-fight-timeline, confirmed (runtime)
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DrivenPad pad;
    coney::test::DiscSession run(*wad, pad);
    const Cast cast = playToSceneEnd(run, pad);
    REQUIRE(cast.destroyers.size() == 12);
    const coney::ai::Gang* crew = cast.player->gang();
    REQUIRE(crew != nullptr);

    // 15 s of the brawl, then the order: R2 held, the right stick up-left, R2 let go (the original's runs gave it 15 to
    // 60 s into the brawl).
    run.run(450);
    pad.buttons = coney::pad::kR2;
    run.run(2);
    pad.rightX = -0.67F;
    pad.rightY = 0.67F;
    run.run(6);
    pad.buttons = 0;
    run.run(1);
    pad.rightX = 0.0F;
    pad.rightY = 0.0F;
    // U: the update CommandIssued moves Cochise into the crew (GangAddMember).
    bool joined = false;
    for (std::uint64_t i = 0; i < kWait && !joined; ++i) {
        run.run(1);
        joined = cast.cochise->gang() == crew && cast.swan->gang() == crew;
    }
    REQUIRE(joined);

    // The seven Warriors: the crew without Cochise and Swan.
    std::vector<const Brain*> warriors;
    for (const Brain* member : crew->members()) {
        if (member != cast.cochise && member != cast.swan) {
            warriors.push_back(member);
        }
    }
    CHECK(warriors.size() == 7);
    const auto isWarrior = [&](const Brain* brain) { return std::ranges::find(warriors, brain) != warriors.end(); };
    // The Destroyers holding a slot on Cochise or Swan at U.
    std::vector<const Brain*> holders;
    for (const Brain* holder : cast.cochise->attackSlots()) {
        holders.push_back(holder);
    }
    for (const Brain* holder : cast.swan->attackSlots()) {
        holders.push_back(holder);
    }
    const auto isHolder = [&](const Brain* brain) { return std::ranges::find(holders, brain) != holders.end(); };

    std::uint64_t followAt = 0;
    std::vector<const Brain*> holdersTurned;
    std::vector<const Brain*> crewOnHolders;
    bool pairKnockedOut = false;
    std::size_t listing = 0;
    std::size_t targeting = 0;
    for (std::uint64_t update = 0; update <= 300; ++update) {
        if (update > 0) {
            run.run(1);
        }
        if (followAt == 0 && cast.cochise->findGoal(GoalType::FollowAndAttack) != nullptr &&
            cast.swan->findGoal(GoalType::FollowAndAttack) != nullptr) {
            followAt = update + 1;
        }
        pairKnockedOut = pairKnockedOut || cast.cochise->knockedOut() || cast.swan->knockedOut();
        for (const Brain* brain : cast.destroyers) {
            if (isHolder(brain) && isWarrior(brain->target()) &&
                std::ranges::find(holdersTurned, brain) == holdersTurned.end()) {
                holdersTurned.push_back(brain);
            }
        }
        for (const Brain* warrior : warriors) {
            if (isHolder(warrior->target()) && std::ranges::find(crewOnHolders, warrior) == crewOnHolders.end()) {
                crewOnHolders.push_back(warrior);
            }
        }
        if (update == 300) {
            listing = static_cast<std::size_t>(std::ranges::count_if(
                cast.destroyers, [&](const Brain* brain) { return std::ranges::any_of(brain->enemies(), isWarrior); }));
            targeting = static_cast<std::size_t>(
                std::ranges::count_if(cast.destroyers, [&](const Brain* brain) { return isWarrior(brain->target()); }));
        }
    }
    std::printf("  level80 after the order: FollowAndAttack at U+%llu, at U+300 %zu listing and %zu targeting a "
                "Warrior, %zu of %zu holders turned, %zu crew on holders\n",
                static_cast<unsigned long long>(followAt == 0 ? 0 : followAt - 1), listing, targeting,
                holdersTurned.size(), holders.size(), crewOnHolders.size());
    // Cochise and Swan follow and attack within 2 updates of joining the crew (the original: update 0 or 1).
    CHECK(followAt != 0);
    CHECK(followAt <= 3);
    // By U+300 at least 9 Destroyers list one of the seven and 4 target one (the original's eight runs: 10-12 and 5-9).
    CHECK(listing >= 9);
    CHECK(targeting >= 4);
    // At least 3 holders have turned to a Warrior and 2 crew Warriors have gone for a holder (the original: 4-6, 3-6).
    CHECK(holdersTurned.size() >= 3);
    CHECK(crewOnHolders.size() >= 2);
    // Cochise and Swan are never knocked out.
    CHECK_FALSE(pairKnockedOut);
}
