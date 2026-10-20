// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/taunt_action.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brain_events.h"
#include "ai/fight_goal.h"
#include "ai/gangs.h"
#include "ai/turn_action.h"
#include "human/human.h"
#include "support/ai_fixtures.h"

// The taunt action and the gloating over a man put on the ground (event 0x15): who taunts, who cheers, who is left
// out, and the event sent as a knocked-down man comes to lie there.
// Research: docs/research/ai-reimpl.md#taunt-action, docs/research/gang-fights.md#waiting

using coney::ai::ActionStatus;
using coney::ai::Brain;
using coney::ai::BrainType;
using coney::ai::TauntAction;
using coney::test::AiScene;

namespace {

// The times `brain` said speech command `line`.
int saidBy(const AiScene& scene, const Brain& brain, std::uint32_t line) {
    return static_cast<int>(std::ranges::count_if(
        scene.services.said, [&](const auto& said) { return said.first == &brain && said.second == line; }));
}

// The clips by anim id `anim` played on `brain`, by name or the character's own.
int playedOn(const AiScene& scene, const Brain& brain, int anim) {
    const auto named = std::ranges::count_if(scene.services.namedClips, [&](const auto& clip) {
        return std::get<0>(clip) == &brain && std::get<1>(clip) == anim;
    });
    return static_cast<int>(named) + static_cast<int>(std::ranges::count(scene.services.clips, anim));
}

} // namespace

TEST_CASE("a taunt plays its kind's anim, says its line, and a cop never taunts", "[ai]") {
    AiScene scene;
    Brain& soldier = scene.add({44.0F, 40.0F, 0.0F}, 0.0F);
    Brain& cop = scene.add({46.0F, 40.0F, 0.0F}, 0.0F, BrainType::Cop);
    for (Brain* brain : {&soldier, &cop}) {
        brain->setServices(&scene.services);
    }
    const int gang = scene.brains.gangs().create(19, "Furies");
    scene.brains.gangs().addMember(gang, soldier);
    scene.brains.gangs().find(gang)->setAlertState(coney::ai::kGangFighting);

    TauntAction cheer(coney::ai::kCheerTaunt, 0x10);
    CHECK(cheer.start(soldier) == ActionStatus::Running);
    CHECK(playedOn(scene, soldier, coney::ai::tauntAnim(coney::ai::kCheerTaunt)) == 1);
    CHECK(saidBy(scene, soldier, 0x10) == 1);
    // The gang's cheers are gen_tnt_cheer1-3 when one of them is drawn.
    for (const auto& [human, anim, clip] : scene.services.namedClips) {
        CHECK(clip.starts_with("gen_tnt_cheer"));
    }
    // Kind 8 says the line and plays nothing.
    TauntAction line(coney::ai::kNoClipTaunt, 0x8f);
    CHECK(line.start(soldier) == ActionStatus::Running);
    CHECK(line.update(soldier) == ActionStatus::Done);
    CHECK(saidBy(scene, soldier, 0x8f) == 1);

    TauntAction copTaunt(coney::ai::kCheerTaunt, 0x10);
    CHECK(copTaunt.start(cop) == ActionStatus::Done);
    CHECK(saidBy(scene, cop, 0x10) == 0);
}

TEST_CASE("over a man put down, the one he fought taunts, the far ones cheer, the near and the pack do not", "[ai]") {
    AiScene scene;
    scene.services.clipSeconds = 1.5F;
    Brain& player = scene.player();
    coney::ai::FightSettings pack;
    pack.gangFight[7].downSpacing = 4; // Turnbulls pile on a downed man
    Brain& fought = scene.add({40.0F, 41.0F, 0.0F}, 180.0F);
    Brain& far = scene.add({40.0F, 34.0F, 0.0F}, 0.0F);
    Brain& near = scene.add({41.0F, 40.0F, 0.0F}, 270.0F);
    Brain& turnbull = scene.add({34.0F, 40.0F, 0.0F}, 90.0F, BrainType::Gang, pack);
    scene.brains.gangs().addMember(scene.brains.gangs().create(7, "Turnbulls"), turnbull);
    for (Brain* brain : {&fought, &far, &near, &turnbull}) {
        brain->setServices(&scene.services);
        brain->setTarget(&player);
        REQUIRE(brain->pushGoal(std::make_unique<coney::ai::FightGoal>()));
    }
    REQUIRE(player.attackSlots().size() == 4);
    player.setTarget(&fought);
    const std::uint64_t before = far.nextAttackMs();

    coney::ai::gloatOver(player);
    // The one he fought and the far one turn to him, then taunt.
    for (Brain* brain : {&fought, &far}) {
        CHECK(brain->actionCount() == 2);
        CHECK(dynamic_cast<const coney::ai::TurnAction*>(brain->frontAction()) != nullptr);
    }
    CHECK(near.actionCount() == 0);
    CHECK(turnbull.actionCount() == 0);
    // Only the cheering one puts off his next attack, by twice the cheer.
    CHECK(far.nextAttackMs() >= std::max(before, far.nowMs()) + 3000);
    CHECK(fought.nextAttackMs() < far.nextAttackMs());

    scene.run(40);
    CHECK(playedOn(scene, fought, coney::ai::tauntAnim(coney::ai::kTauntAtTaunt)) == 1);
    CHECK(playedOn(scene, far, coney::ai::tauntAnim(coney::ai::kCheerTaunt)) == 1);
    CHECK(saidBy(scene, far, 0x10) + saidBy(scene, far, 0x8f) == 1);
    CHECK(saidBy(scene, fought, 0x10) + saidBy(scene, fought, 0x8f) == 0);
}

TEST_CASE("a man knocked down hears that he is down once, as he comes to lie there", "[ai]") {
    AiScene scene;
    Brain& downed = scene.add({40.0F, 41.0F, 0.0F}, 180.0F);
    downed.setServices(&scene.services);
    scene.run(2);
    // Hurt (below 35 % of 1400), a heavy reaction (code 0x26) from the front knocks him down.
    downed.human().fighter().health().apply(1000);
    coney::human::IncomingHit hit;
    hit.damage = 46;
    hit.attackAnim = 13;
    hit.code = 0x26;
    hit.attacker = coney::anim::Vec3{40.0F, 40.0F, 0.0F};
    downed.human().hit(hit);
    const auto putDowns = [&] {
        return std::ranges::count_if(scene.services.humanEvents, [&](const auto& event) {
            return event.first == &downed && event.second.id == coney::ai::kEventPutDown;
        });
    };
    // Sent after he went down (once the knockdown's reaction is over), not as he falls.
    int groundedAt = -1;
    int sentAt = -1;
    for (int k = 0; k < 90; ++k) {
        scene.run(1);
        if (groundedAt < 0 && downed.human().fighter().victim().grounded()) {
            groundedAt = k;
        }
        if (sentAt < 0 && putDowns() > 0) {
            sentAt = k;
        }
    }
    REQUIRE(groundedAt >= 0);
    CHECK(putDowns() == 1);
    CHECK(sentAt > groundedAt);
}
