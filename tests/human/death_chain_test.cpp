// SPDX-License-Identifier: GPL-3.0-or-later
// The death chain a human beaten to 0 health plays (Victim::die()): the dying reaction picked by his state, the fall,
// 205 for a woundable human, then the loop; the blends between them; the held flag 0x2000 until the loop; and the
// knock-out when the clip before the loop ends (docs/research/combat-reactions.md#knockout-timing). Synthetic clips:
// every clip the chain can pick, the knockdown events each test asks for.
#include <array>
#include <cstdint>
#include <map>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/anim_set.h"
#include "combat/power_class.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/human_flags.h"
#include "human/victim.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

using Catch::Approx;
using coney::human::DeathChain;
using coney::human::DyingState;
using coney::human::HumanAnimator;
using coney::human::IncomingHit;
using coney::human::Victim;
using coney::human::VictimFrame;
namespace state = coney::human::human_state;

namespace {

// The dying clips of the special cases (the hit's own, 292 and the DIE set's 304-315, are added to them).
constexpr std::array<std::uint32_t, 12> kSpecialDying{127, 132, 139, 146, 195, 204, 208, 211, 264, 319, 322, 348};

// A victim's anim set: the locomotion clips and every clip of the chain, 0.4 s each (the loops 1 s, looping). A clip
// in `knockdowns` has a knockdown event (type 7) whose value is the one given.
class ChainCharacter {
  public:
    explicit ChainCharacter(const std::map<std::uint32_t, std::uint32_t>& knockdowns = {})
        : m_data(coney::test::locomotionData(clips(knockdowns))), anims(m_data, nullptr) {}

  private:
    // The clip list for knockdowns `knockdowns`.
    static std::vector<coney::test::LocomotionClip> clips(const std::map<std::uint32_t, std::uint32_t>& knockdowns) {
        std::vector<coney::test::LocomotionClip> all = coney::test::locomotionClips();
        std::vector<std::uint32_t> ids(kSpecialDying.begin(), kSpecialDying.end());
        for (std::uint32_t id = 292; id <= 315; ++id) {
            ids.push_back(id);
        }
        // 308 is one of the DIE set's: the falls' other clips.
        for (const std::uint32_t id : {198U, 205U}) {
            ids.push_back(id);
        }
        for (const std::uint32_t id : ids) {
            const auto knockdown = knockdowns.find(id);
            all.push_back({.id = id,
                           .duration = 0.4F,
                           .knockdown = knockdown != knockdowns.end(),
                           .knockdownValue = knockdown != knockdowns.end() ? knockdown->second : 0U});
        }
        for (const std::uint32_t id : {200U, 323U, 349U}) {
            all.push_back({.id = id, .duration = 1.0F, .rangeFlags = 0x1000});
        }
        return all;
    }

    coney::characters::CharacterData m_data;

  public:
    coney::characters::AnimSet anims;
};

// A punch from in front (attack 12, code 0x0a) that kills.
IncomingHit lethalPunch() { return IncomingHit{.damage = 900, .attackAnim = 12, .code = 0x0a, .attacker = {0, 1, 0}}; }

// The chain a victim in `dying` plays over `character`'s clips.
DeathChain chainOf(const DyingState& dying, const ChainCharacter& character = ChainCharacter{}) {
    HumanAnimator animator(character.anims, coney::human::AnimSlots::player());
    Victim victim(coney::combat::kPlayerPowerClass);
    victim.die(lethalPunch(), VictimFrame{}, dying, animator);
    return victim.deathChain();
}

// The clips of `chain` before its loop.
std::vector<std::uint32_t> clipsOf(const DeathChain& chain) {
    std::vector<std::uint32_t> clips;
    clips.reserve(chain.steps.size());
    for (const coney::human::DeathStep& step : chain.steps) {
        clips.push_back(step.clip);
    }
    return clips;
}

} // namespace

TEST_CASE("a dying human's special state picks his dying clip before the hit's own, in the original's order",
          "[human][combat]") {
    // Expected: combat-reactions.md#knockout-timing, confirmed (code): Human_PlayDeathReaction (0x00267a00) tests, in
    // turn, cuffed 0x142, state 0x8000000 0x108, wounded 0xcc, grabbing from the front 0x92 or the rear 0x84, grabbed
    // from the front 0x8b or the rear 0x7f, tackled 0xd0, tackling 0xd3, knocked down with flag 0x8 0xc3, state
    // 0xe0000 0x15c, burning 0x13f, else Hit_PickDyingReaction.
    struct Case {
        std::uint64_t state;
        bool woundable;
        std::uint32_t dying;
    };
    const std::vector<Case> cases{
        {state::kCuffed, false, 322},
        {state::kState8000000, false, 264},
        {state::kWounded, false, 204},
        {state::kGrabbingFront, false, 146},
        {state::kGrabbingRear, false, 132},
        {state::kGrabbedFront, false, 139},
        {state::kGrabbedRear, false, 127},
        {state::kTackled, false, 208},
        {state::kTackling, false, 211},
        {state::kKnockedDown, true, 195},
        // Knocked down without flag 0x8 is state 0xe0000's.
        {state::kKnockedDown, false, 348},
        {state::kKnockedOut, false, 348},
        {state::kOnFire, false, 319},
        // The first that holds wins: cuffed before burning, wounded before grabbed, tackling before knocked down.
        {state::kCuffed | state::kOnFire, false, 322},
        {state::kWounded | state::kGrabbedRear, false, 204},
        {state::kTackling | state::kKnockedDown, true, 211},
    };
    for (const Case& c : cases) {
        CAPTURE(c.state, c.woundable);
        const DeathChain chain = chainOf(DyingState{.state = c.state, .woundable = c.woundable});
        REQUIRE(!chain.steps.empty());
        CHECK(chain.steps.front().clip == c.dying);
    }
}

TEST_CASE("with no special state the dying clip is the hit's own: 292 or the DIE set's", "[human][combat]") {
    // Expected: combat-reactions.md#knockout-timing, confirmed (code): else Hit_PickDyingReaction (0x00266fd8).
    const DeathChain chain = chainOf(DyingState{});
    REQUIRE(!chain.steps.empty());
    const std::uint32_t dying = chain.steps.front().clip;
    CHECK((dying == 292 || (dying >= 304 && dying <= 315)));
}

TEST_CASE("the fall after the dying clip: 308 with no knockdown event, 198 when its value is 0, none otherwise",
          "[human][combat]") {
    // Expected: combat-reactions.md#knockout-timing, confirmed (code): Anim_GetKnockdownValue (0x00101a60) on the dying
    // clip; none found, 308 (0x134) unless state 0xe0000 is set; found with value 0, 198 (0xc6); else no fall.
    SECTION("no knockdown event: 308") {
        CHECK(clipsOf(chainOf(DyingState{.state = state::kOnFire})) == std::vector<std::uint32_t>{319, 308});
    }
    SECTION("a knockdown event of value 0: 198") {
        const ChainCharacter character({{319, 0}});
        CHECK(clipsOf(chainOf(DyingState{.state = state::kOnFire}, character)) == std::vector<std::uint32_t>{319, 198});
    }
    SECTION("a knockdown event of another value: no fall") {
        const ChainCharacter character({{319, 0x10000}});
        CHECK(clipsOf(chainOf(DyingState{.state = state::kOnFire}, character)) == std::vector<std::uint32_t>{319});
    }
    SECTION("state 0xe0000 with no knockdown event: no fall") {
        CHECK(clipsOf(chainOf(DyingState{.state = state::kKnockedOut})) == std::vector<std::uint32_t>{348});
    }
    SECTION("state 0xe0000 with a knockdown event of value 0: 198") {
        const ChainCharacter character({{348, 0}});
        CHECK(clipsOf(chainOf(DyingState{.state = state::kKnockedOut}, character)) ==
              std::vector<std::uint32_t>{348, 198});
    }
}

TEST_CASE("the death chain's loop is 349, 200 after 205 for a woundable human not yet wounded, 323 cuffed",
          "[human][combat]") {
    // Expected: combat-reactions.md#knockout-timing, confirmed (code): the loop 0x15d; with flag 0x8 and not wounded
    // (not cuffed) 205 (0xcd) before the loop 200; cuffed the loop 0x143.
    const DeathChain plain = chainOf(DyingState{.state = state::kOnFire});
    CHECK(plain.loop == 349);
    const DeathChain woundable = chainOf(DyingState{.state = state::kOnFire, .woundable = true});
    CHECK(clipsOf(woundable) == std::vector<std::uint32_t>{319, 308, 205});
    CHECK(woundable.loop == 200);
    // Wounded already: no 205, the loop 349.
    const DeathChain wounded = chainOf(DyingState{.state = state::kWounded, .woundable = true});
    CHECK(clipsOf(wounded) == std::vector<std::uint32_t>{204, 308});
    CHECK(wounded.loop == 349);
    // Cuffed: 323, and no 205 even with flag 0x8; no fall either, cuffed being one of state 0xe0000's bits.
    const DeathChain cuffed = chainOf(DyingState{.state = state::kCuffed, .woundable = true});
    CHECK(clipsOf(cuffed) == std::vector<std::uint32_t>{322});
    CHECK(cuffed.loop == 323);
}

TEST_CASE("the death chain's blends: 0.2 out of the dying clip unless a fall without 205 follows it, 0.1 out of a fall "
          "into the loop",
          "[human][combat]") {
    // Expected: combat-reactions.md#knockout-timing, confirmed (code): the fall's blend is 0.1 (0x3dcccccd) when the
    // loop follows it, else 0; the dying clip's 0.2 (0x3e4cccce) when the loop or 205 is built, else 0; 205's is 0.
    SECTION("dying, fall, loop") {
        const DeathChain chain = chainOf(DyingState{.state = state::kOnFire});
        REQUIRE(chain.steps.size() == 2);
        CHECK(chain.steps[0].blend == Approx(0.0F));
        CHECK(chain.steps[1].blend == Approx(0.1F));
    }
    SECTION("dying, loop") {
        const DeathChain chain = chainOf(DyingState{.state = state::kKnockedOut});
        REQUIRE(chain.steps.size() == 1);
        CHECK(chain.steps[0].blend == Approx(0.2F));
    }
    SECTION("dying, fall, 205, loop") {
        const DeathChain chain = chainOf(DyingState{.state = state::kOnFire, .woundable = true});
        REQUIRE(chain.steps.size() == 3);
        CHECK(chain.steps[0].blend == Approx(0.2F));
        CHECK(chain.steps[1].blend == Approx(0.0F));
        CHECK(chain.steps[2].blend == Approx(0.0F));
    }
    SECTION("dying, 205, loop") {
        const ChainCharacter character({{319, 1}});
        const DeathChain chain = chainOf(DyingState{.state = state::kOnFire, .woundable = true}, character);
        REQUIRE(chain.steps.size() == 2);
        CHECK(chain.steps[0].blend == Approx(0.2F));
        CHECK(chain.steps[1].blend == Approx(0.0F));
    }
}

TEST_CASE("the death chain plays its clips in turn then its loop, holding 0x2000 until the loop starts",
          "[human][combat]") {
    // Expected: combat-reactions.md#knockout-timing, confirmed (code) and confirmed (runtime):
    // Human_SetHeldFlag(0x2000) as the chain is built, the clip before the loop holding it (knockout_timing: held
    // 0x2000 through 292, 0 from the update 349 starts).
    const ChainCharacter character;
    HumanAnimator animator(character.anims, coney::human::AnimSlots::player());
    Victim victim(coney::combat::kPlayerPowerClass);
    victim.hit(lethalPunch());
    victim.die(lethalPunch(), VictimFrame{}, DyingState{.state = state::kOnFire, .woundable = true}, animator);
    // The pending hit is gone with it (pending damage +0x118 = 0).
    CHECK_FALSE(victim.pending());
    std::vector<std::uint32_t> played;
    int heldBeforeLoop = 0;
    int heldInLoop = 0;
    for (int update = 0; update < 90; ++update) {
        const std::uint32_t now = animator.animId();
        if (played.empty() || played.back() != now) {
            played.push_back(now);
        }
        const bool held = (animator.flags() & 0x2000U) != 0;
        if (now == 200) {
            heldInLoop += held ? 1 : 0;
        } else {
            heldBeforeLoop += held ? 0 : 1;
        }
        animator.advance(1.0F / 30.0F);
    }
    CHECK(played == std::vector<std::uint32_t>{319, 308, 205, 200});
    CHECK(heldBeforeLoop == 0);
    CHECK(heldInLoop == 0);
}

TEST_CASE("a woundable human beaten to 0 health is knocked out when 205 ends and 200 begins", "[human][combat]") {
    // Expected: combat-reactions.md#knockout-timing, confirmed (code): Human_OnHealthOut (0x002674c0) is the end
    // callback of the chain's last clip before the loop, 205 for a human with flag 0x8 not yet wounded.
    const coney::test::FightCharacter character;
    coney::test::Fight fight(character, 30.0F);
    coney::human::Human& human = fight.human();
    human.setFlag(coney::human::flag::kDemiGod, false);
    human.setFlag(coney::human::flag::kWoundable, true);
    human.takeHit(
        IncomingHit{.damage = 900, .attackAnim = 12, .code = 0x0a, .flags = 0x800, .attacker = {40.0F, 41.0F, 0.0F}});
    fight.run("", 1);
    REQUIRE(human.fighter().health().depleted());
    std::vector<std::uint32_t> played;
    int knockedOutBefore200 = 0;
    for (int update = 0; update < 300 && human.animator().animId() != 200; ++update) {
        const std::uint32_t now = human.animator().animId();
        if (played.empty() || played.back() != now) {
            played.push_back(now);
        }
        knockedOutBefore200 += human.knockedOut() ? 1 : 0;
        fight.run("", 1);
    }
    REQUIRE(played.size() >= 2);
    CHECK(played.back() == 205);
    CHECK(knockedOutBefore200 == 0);
    CHECK(human.animator().animId() == 200);
    CHECK(human.knockedOut());
}

TEST_CASE("a human beaten to 0 health weighs 1e9 against other humans' pushes", "[human][combat]") {
    // Expected: combat-reactions.md#knockout-timing, inferred: Human_PlayDeathReaction calls the human's vtable +0xe4
    // (Human_SetAttribute, 0x002212b0, attribute 7: the push weight) with 1e9.
    const coney::test::FightCharacter character;
    coney::test::Fight fight(character, 30.0F);
    coney::human::Human& human = fight.human();
    human.setFlag(coney::human::flag::kDemiGod, false);
    CHECK(human.pushWeight() == Approx(1.0F));
    human.takeHit(
        IncomingHit{.damage = 900, .attackAnim = 12, .code = 0x0a, .flags = 0x800, .attacker = {40.0F, 41.0F, 0.0F}});
    fight.run("", 1);
    REQUIRE(human.fighter().health().depleted());
    CHECK(human.pushWeight() == Approx(1e9F));
}
