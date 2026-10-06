// SPDX-License-Identifier: GPL-3.0-or-later
// The tactics a Rumble match runs (docs/research/ai.md#tactic-kinds, docs/research/rumble.md#match-end):
// TacticConfront's ranges and events, TacticAttack's melee and codes, and a tactic replaced from inside its own update.
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/goal.h"
#include "ai/tactic.h"
#include "ai/tactic_attack.h"
#include "ai/tactic_confront.h"
#include "support/ai_fixtures.h"

using coney::ai::Brain;
using coney::ai::BrainEvent;
using coney::test::AiScene;

namespace {

// A gang of kind `kind` named `name` with an AI member at each of `places`.
int makeGang(AiScene& scene, int kind, const std::string& name, const std::vector<coney::anim::Vec3>& places,
             std::vector<Brain*>& members) {
    const int gang = scene.brains.gangs().create(kind, name);
    for (const coney::anim::Vec3& feet : places) {
        Brain& member = scene.add(feet, 0.0F);
        scene.brains.gangs().addMember(gang, member);
        members.push_back(&member);
    }
    return gang;
}

// The codes `function` was called with for `gang`, oldest first.
std::vector<int> codes(const AiScene& scene, const std::string& function, int gang) {
    std::vector<int> found;
    for (const coney::test::ScriptCall& call : scene.services.calls) {
        if (call.function == function && call.args.size() == 2 && call.args[0] == gang) {
            found.push_back(static_cast<int>(call.args[1]));
        }
    }
    return found;
}

// A tactic that replaces itself with `next` on its first update, then counts on (touching itself after the swap).
class SelfReplacingTactic final : public coney::ai::Tactic {
  public:
    SelfReplacingTactic(coney::ai::Gangs& gangs, int gang, std::unique_ptr<coney::ai::Tactic> next)
        : Tactic(0x21, ""), m_gangs(&gangs), m_gang(gang), m_next(std::move(next)) {}
    void start(coney::ai::Gang& /*gang*/) override {}
    [[nodiscard]] int update(coney::ai::Gang& /*gang*/) override {
        if (m_next) {
            m_gangs->setTactic(m_gang, std::move(m_next));
        }
        ++updates;
        return 0;
    }
    int updates = 0;

  private:
    coney::ai::Gangs* m_gangs;
    int m_gang;
    std::unique_ptr<coney::ai::Tactic> m_next;
};

} // namespace

TEST_CASE("TacticConfront closes a gang on the other's leader and reports approach, then critical range",
          "[ai][tactics][rumble]") {
    AiScene scene;
    std::vector<Brain*> ours;
    std::vector<Brain*> theirs;
    const int gang = makeGang(scene, 19, "Furies", {{44.0F, 40.0F, 0.0F}, {44.0F, 42.0F, 0.0F}}, ours);
    const int other = makeGang(scene, 21, "Orphans", {{52.0F, 40.0F, 0.0F}}, theirs);
    auto owned = std::make_unique<coney::ai::TacticConfront>(
        coney::ai::ConfrontSettings{.targetGang = other, .approachRange = 10.0F, .criticalRange = 2.0F}, "Confront");
    coney::ai::TacticConfront& tactic = *owned;
    scene.brains.gangs().setTactic(gang, std::move(owned));
    scene.run(1);
    CHECK(tactic.targetGang() == other);
    for (const Brain* member : ours) {
        REQUIRE(member->topGoal() != nullptr);
        CHECK(member->topGoal()->type() == coney::ai::kConfrontGoal);
        CHECK(member->target() == theirs[0]);
    }
    CHECK(coney::ai::gangLeader(*scene.brains.gangs().find(gang)) == ours[0]);

    // The leaders start 8 m apart, inside the approach range; the members close in to the critical range.
    const auto critical = [&scene, gang] {
        const std::vector<int> seen = codes(scene, "Confront", gang);
        return std::ranges::find(seen, coney::ai::kTacInCriticalRange) != seen.end();
    };
    for (int k = 0; k < 10 * 30 && !critical(); ++k) {
        scene.run(1);
    }
    const std::vector<int> seen = codes(scene, "Confront", gang);
    REQUIRE_FALSE(seen.empty());
    CHECK(seen.front() == coney::ai::kTacInRange);
    CHECK(std::ranges::find(seen, coney::ai::kTacInCriticalRange) != seen.end());

    // Damage fires 5, an attack warning 6.
    scene.services.calls.clear();
    coney::ai::Gang& ourGang = *scene.brains.gangs().find(gang);
    (void)tactic.event(ourGang, *ours[0], BrainEvent{.id = coney::ai::kEventDamaged});
    (void)tactic.event(ourGang, *ours[0], BrainEvent{.id = coney::ai::kConfrontAttackedEvent});
    CHECK(codes(scene, "Confront", gang) == std::vector<int>{coney::ai::kTacDamage, coney::ai::kTacAttacked});
}

TEST_CASE("TacticConfront with no target gang does nothing", "[ai][tactics][rumble]") {
    AiScene scene;
    std::vector<Brain*> ours;
    const int gang = makeGang(scene, 19, "Furies", {{44.0F, 40.0F, 0.0F}}, ours);
    auto owned = std::make_unique<coney::ai::TacticConfront>(coney::ai::ConfrontSettings{}, "Confront");
    coney::ai::TacticConfront& tactic = *owned;
    scene.brains.gangs().setTactic(gang, std::move(owned));
    scene.run(30);
    CHECK(tactic.targetGang() == -1);
    CHECK(codes(scene, "Confront", gang).empty());
}

TEST_CASE("TacticAttack sends its members at the nearest enemy, and reports when none has an enemy",
          "[ai][tactics][rumble]") {
    AiScene scene;
    std::vector<Brain*> ours;
    std::vector<Brain*> theirs;
    const int gang = makeGang(scene, 19, "Furies", {{44.0F, 40.0F, 0.0F}}, ours);
    const int other = makeGang(scene, 21, "Orphans", {{60.0F, 40.0F, 0.0F}, {50.0F, 40.0F, 0.0F}}, theirs);
    scene.brains.gangs().makeEnemies(gang, other);
    CHECK(coney::ai::nearestGangEnemy(*ours[0], scene.brains.gangs()) == theirs[1]);
    scene.brains.gangs().setTactic(gang, std::make_unique<coney::ai::TacticAttack>("Attack"));
    scene.run(1);
    REQUIRE(ours[0]->topGoal() != nullptr);
    CHECK(ours[0]->topGoal()->type() == coney::ai::kMeleeGoal);
    CHECK(ours[0]->threatResponse() == 2);
    scene.run(1);
    CHECK(ours[0]->target() == theirs[1]);

    // A gang with no enemies hears 9 within the check's period.
    AiScene lonely;
    std::vector<Brain*> alone;
    const int solo = makeGang(lonely, 19, "Furies", {{44.0F, 40.0F, 0.0F}}, alone);
    lonely.brains.gangs().setTactic(solo, std::make_unique<coney::ai::TacticAttack>("Attack"));
    lonely.run(40);
    const std::vector<int> seen = codes(lonely, "Attack", solo);
    REQUIRE_FALSE(seen.empty());
    CHECK(seen.front() == coney::ai::kTacNoEnemies);
}

TEST_CASE("a tactic that replaces itself from its own update is freed only after the gangs' update",
          "[ai][tactics][rumble]") {
    AiScene scene;
    std::vector<Brain*> members;
    const int gang = makeGang(scene, 19, "Furies", {{44.0F, 40.0F, 0.0F}}, members);
    auto next = std::make_unique<coney::ai::TacticAttack>("");
    const coney::ai::Tactic* nextTactic = next.get();
    scene.brains.gangs().setTactic(gang,
                                   std::make_unique<SelfReplacingTactic>(scene.brains.gangs(), gang, std::move(next)));
    scene.run(3);
    CHECK(scene.brains.gangs().find(gang)->tactic() == nextTactic);
    CHECK(nextTactic->started());
}
