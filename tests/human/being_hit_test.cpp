// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/anim_set.h"
#include "combat/being_hit.h"
#include "combat/combat_tuning.h"
#include "combat/grabbed.h"
#include "combat/player_combat.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/pair_placement.h"
#include "human/target_human.h"
#include "human/victim.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

// The player as a victim, driven by synthetic hits through the human's entry points (nothing in Coney attacks the
// player yet) and input scripts at partial stick deflections: the reaction and the damage taken as it is, the 200 ms
// stun, the 2750 ms on the ground and the mash, the hit armour, the block and the duck, the grab on the player (the
// counter, the struggle, the strike back, the escape, the reversal); then lock-on and the combat walk, turning a grab,
// the class damage per human, and a target moved by its reaction's root motion
// (docs/research/combat.md#being-hit-runtime).

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::GrabbedReport;
using coney::human::IncomingHit;
using coney::test::Fight;
using coney::test::FightCharacter;
namespace combat = coney::combat;

namespace {

// Where an attacker in front of the player (at (40, 40) facing +y) stands.
constexpr Vec3 kInFront{40.0F, 41.0F, 0.0F};

// A hit of attack `animId` with `damage`, `code` and `flags`, from `attacker`.
IncomingHit hitOf(int animId, int damage, int code, std::uint16_t flags = 0, Vec3 attacker = kInFront) {
    return IncomingHit{.damage = damage, .attackAnim = animId, .code = code, .flags = flags, .attacker = attacker};
}

// The anim ids the player played, each once per run of it.
class ClipLog {
  public:
    // Notes the clip playing now.
    void note(std::uint32_t clip) {
        if (m_clips.empty() || m_clips.back() != clip) {
            m_clips.push_back(clip);
        }
    }
    [[nodiscard]] const std::vector<std::uint32_t>& clips() const { return m_clips; }
    // Whether `clip` was played.
    [[nodiscard]] bool has(std::uint32_t clip) const { return std::ranges::find(m_clips, clip) != m_clips.end(); }

  private:
    std::vector<std::uint32_t> m_clips;
};

// Sets the combat tuning for one test and puts the defaults back after it.
class TuningScope {
  public:
    TuningScope() = default;
    TuningScope(const TuningScope&) = delete;
    TuningScope& operator=(const TuningScope&) = delete;
    ~TuningScope() { combat::combatTuning() = combat::CombatTuning{}; }
    [[nodiscard]] static combat::CombatTuning& tuning() { return combat::combatTuning(); }
};

// The puppet civilian of the runtime as a grabber: `power` of 200, class 2.
combat::GrabberState grabber(int power, bool hurt = false) {
    return combat::GrabberState{.power = power, .powerMax = 200, .hurt = hurt, .struggleDivisor = 4};
}

} // namespace

TEST_CASE("a hit plays the table's reaction and takes the attacker's damage as it is, with no rage",
          "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 30.0F);
    // The civilian's S1 (14) from in front: 272.
    fight.human().takeHit(hitOf(12, 14, 0x0a, 0x800));
    fight.run("", 1);
    CHECK(fight.human().fighter().health().value() == 886);
    CHECK(fight.human().animator().animId() == 272);
    CHECK(fight.human().fighter().helpless(fight.human().animator()));
    // X1 (23, 0x09) from the player's left: 274.
    fight.run("", 20);
    fight.human().takeHit(hitOf(11, 23, 0x09, 0x800, Vec3{39.0F, 40.0F, 0.0F}));
    fight.run("", 1);
    CHECK(fight.human().animator().animId() == 274);
    CHECK(fight.human().fighter().health().value() == 886 - 23);
    // Being hit gives no rage.
    CHECK(fight.human().fighter().combat().rage().value() == 0);
}

TEST_CASE("the player's 200 ms stun goes straight to 357 when the reaction ends, with no 356", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 30.0F);
    ClipLog log;
    // SX2 (0x1a, stun) from in front: 280.
    fight.human().takeHit(hitOf(15, 37, 0x1a, 0xc00));
    std::uint64_t stunOver = 0;
    fight.run("", 40, [&](std::uint64_t frame) {
        log.note(fight.human().animator().animId());
        if (stunOver == 0 && !fight.human().fighter().victim().stunned()) {
            stunOver = frame;
        }
    });
    CHECK(log.has(280));
    CHECK(log.has(357));
    CHECK_FALSE(log.has(356));
    // 200 ms is 6 updates.
    CHECK(stunOver >= 5);
    CHECK(stunOver <= 7);
    // Free again after 357.
    CHECK_FALSE(fight.human().fighter().helpless(fight.human().animator()));
}

TEST_CASE("knocked down the player lies 2750 ms in 196, then rises with 199", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 30.0F);
    // XX2 (0x26, heavy) from in front: 288, which knocks down.
    fight.human().takeHit(hitOf(13, 46, 0x26));
    std::uint64_t rose = 0;
    ClipLog log;
    fight.run("", 110, [&](std::uint64_t frame) {
        log.note(fight.human().animator().animId());
        if (rose == 0 && fight.human().animator().animId() == 199) {
            rose = frame;
        }
    });
    CHECK(log.has(288));
    CHECK(log.has(196));
    // 2750 ms is 82.5 updates (83 at runtime).
    CHECK(rose >= 81);
    CHECK(rose <= 84);
    CHECK(fight.human().fighter().health().value() == 900 - 46);
}

TEST_CASE("653's stun outlasts the rise: 199, then 357", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 30.0F);
    // The crushing special (0x39) with its stun: 303, down, stunned until the rise + 200 ms.
    fight.human().takeHit(hitOf(653, 50, 0x39, 0x400));
    ClipLog log;
    bool stunnedAtRise = false;
    fight.run("", 120, [&](std::uint64_t) {
        const std::uint32_t clip = fight.human().animator().animId();
        if (clip == 199 && !log.has(199)) {
            stunnedAtRise = fight.human().fighter().victim().stunned();
        }
        log.note(clip);
    });
    CHECK(log.has(303));
    CHECK(stunnedAtRise);
    const auto rise = std::ranges::find(log.clips(), 199U);
    REQUIRE(rise != log.clips().end());
    CHECK(std::find(rise, log.clips().end(), 357U) != log.clips().end());
}

TEST_CASE("presses during the knockdown's reaction do nothing; lying down each one cuts the time", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 30.0F);
    fight.human().takeHit(hitOf(13, 46, 0x26));
    std::uint64_t riseBefore = 0;
    std::uint64_t riseDuring = 0;
    std::uint64_t riseAfter = 0;
    std::uint64_t rose = 0;
    // A press at 3 (in the 288 reaction), one at 40 (lying in 196).
    fight.run("3 tap square\n40 tap square\n", 110, [&](std::uint64_t frame) {
        const std::uint64_t riseAt = fight.human().fighter().victim().riseAtMs();
        if (frame == 1) {
            riseBefore = riseAt;
        }
        if (frame == 6) {
            riseDuring = riseAt;
        }
        if (frame == 42) {
            riseAfter = riseAt;
        }
        if (rose == 0 && fight.human().animator().animId() == 199) {
            rose = frame;
        }
    });
    CHECK(riseDuring == riseBefore);
    // 2750 ms over 1, 2 or 3: at least 916 ms off.
    CHECK(riseBefore - riseAfter >= 916);
    CHECK(rose < 81);
}

TEST_CASE("hit armour: in the wind-up the damage lands with no reaction; in the end phase the attack is lost",
          "[human][combat]") {
    const FightCharacter character;
    {
        Fight fight(character, 30.0F);
        bool queued = false;
        fight.run("10 tap square\n", 14, [&](std::uint64_t) {
            if (!queued && fight.human().fighter().combat().chain().phaseFlags() == 0x1) {
                fight.human().takeHit(hitOf(12, 14, 0x0a, 0x800));
                queued = true;
            }
        });
        CHECK(fight.human().fighter().hitsArmoured() == 1);
        CHECK(fight.human().fighter().health().value() == 886);
        CHECK(fight.human().fighter().combat().chain().active());
        CHECK(fight.human().animator().animId() == 12);
    }
    {
        Fight fight(character, 30.0F);
        bool queued = false;
        fight.run("10 tap square\n", 30, [&](std::uint64_t) {
            if (!queued && fight.human().fighter().combat().chain().phaseFlags() == 0x4) {
                fight.human().takeHit(hitOf(12, 14, 0x0a, 0x800));
                queued = true;
            }
        });
        REQUIRE(queued);
        CHECK(fight.human().fighter().hitsArmoured() == 0);
        CHECK(fight.human().fighter().victim().lastReaction() == 272);
        CHECK_FALSE(fight.human().fighter().combat().chain().active());
    }
}

TEST_CASE("a held block takes no damage; a warned duck lets the hit pass; strength 3 breaks it", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 30.0F);
    fight.run("0 press r1\n", 5);
    REQUIRE(fight.human().fighter().blocking());
    // S1 from in front: 608, then the sustain again.
    fight.human().takeHit(hitOf(12, 14, 0x0a, 0x800));
    fight.run("0 press r1\n", 1);
    CHECK(fight.human().fighter().health().value() == 900);
    CHECK(fight.human().animator().animId() == 608);
    CHECK(fight.human().fighter().hitsBlocked() == 1);
    fight.run("0 press r1\n", 15);
    CHECK(fight.human().animator().animId() == 606);
    // SSX3's clip warns (event 0x24): the duck 616, and its hit misses.
    fight.human().warn(coney::human::AttackNotice{
        .warning = combat::AttackWarning::Duck, .attackAnim = 17, .code = 0x2b, .attacker = kInFront});
    fight.run("0 press r1\n", 1);
    CHECK(fight.human().animator().animId() == static_cast<std::uint32_t>(combat::kBlockDodge));
    fight.human().takeHit(hitOf(17, 51, 0x2b));
    fight.run("0 press r1\n", 1);
    CHECK(fight.human().fighter().hitsDucked() == 1);
    CHECK(fight.human().fighter().health().value() == 900);
    // An early block reaction (event 0x26) for S1: 608 before the hit.
    fight.run("0 press r1\n", 15);
    fight.human().warn(coney::human::AttackNotice{
        .warning = combat::AttackWarning::EarlyBlock, .attackAnim = 12, .code = 0x0a, .attacker = kInFront});
    fight.run("0 press r1\n", 1);
    CHECK(fight.human().animator().animId() == 608);
    // The crushing special breaks the block: its damage and its reaction land.
    fight.run("0 press r1\n", 15);
    fight.human().takeHit(hitOf(653, 50, 0x39));
    fight.run("0 press r1\n", 1);
    CHECK(fight.human().fighter().health().value() == 850);
    CHECK(fight.human().animator().animId() == 303);
}

TEST_CASE("caught in a grab: square struggles, cross strikes back, circle escapes on a winning roll",
          "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 30.0F);
    fight.human().catchInGrab(
        coney::human::GrabCatch{.grabberAnims = &character.anims, .fromRear = false, .grabber = grabber(200)});
    fight.run("", 2);
    REQUIRE(fight.human().fighter().grabbed());
    // Square: 96 and a third of the grabber's 200 (the player's divisor 3).
    GrabbedReport struggle;
    fight.run("10 tap square\n", 30, [&](std::uint64_t) {
        if (fight.human().fighter().grabbedReport().action != combat::GrabbedAction::None) {
            struggle = fight.human().fighter().grabbedReport();
        }
    });
    CHECK(struggle.action == combat::GrabbedAction::Struggle);
    CHECK(struggle.grabberPowerCost == 67);
    CHECK(struggle.grabberClip == 97);
    // Cross: 104, 20 damage to the grabber and 1 rage.
    fight.human().updateGrabber(grabber(200));
    GrabbedReport strike;
    fight.run("10 tap cross\n", 30, [&](std::uint64_t) {
        if (fight.human().fighter().grabbedReport().action != combat::GrabbedAction::None) {
            strike = fight.human().fighter().grabbedReport();
        }
    });
    CHECK(strike.action == combat::GrabbedAction::StrikeBack);
    CHECK(strike.grabberDamage == 20);
    CHECK(fight.human().fighter().combat().rage().value() == 1);
    // Circle at a grabber down to a quarter: the escape 100; the escapee takes its 20, the grabber goes down.
    fight.human().updateGrabber(grabber(40));
    GrabbedReport escape;
    fight.run("10 tap circle\n", 20, [&](std::uint64_t) {
        if (fight.human().fighter().grabbedReport().action == combat::GrabbedAction::Escape) {
            escape = fight.human().fighter().grabbedReport();
        }
    });
    CHECK(escape.ended);
    CHECK(escape.grabberClip == 101);
    CHECK(escape.grabberKnockedDown);
    CHECK_FALSE(fight.human().fighter().grabbed());
    CHECK(fight.human().fighter().health().value() == 880);
}

TEST_CASE("R1 at the catch counters with 76; R1 in the hold reverses it to the rear hold", "[human][combat]") {
    const FightCharacter character;
    {
        Fight fight(character, 30.0F);
        fight.human().catchInGrab(
            coney::human::GrabCatch{.grabberAnims = &character.anims, .fromRear = false, .grabber = grabber(200)});
        fight.run("0 tap r1\n", 1);
        const GrabbedReport& report = fight.human().fighter().grabbedReport();
        CHECK(report.countered);
        CHECK(report.grabberClip == 77);
        CHECK(report.grabberPowerCost == 50);
        CHECK(report.grabberStunned);
        CHECK(fight.human().animator().animId() == 76);
        CHECK_FALSE(fight.human().fighter().grabbed());
        // 100 of the player's 400 power, and 10 rage.
        // (the meter refills as the update runs on: 300 to 302).
        CHECK(fight.human().fighter().combat().power().value() >= 300);
        CHECK(fight.human().fighter().combat().power().value() <= 302);
        CHECK(fight.human().fighter().combat().rage().value() == 10);
    }
    {
        Fight fight(character, 30.0F);
        fight.human().catchInGrab(
            coney::human::GrabCatch{.grabberAnims = &character.anims, .fromRear = false, .grabber = grabber(40)});
        ClipLog log;
        fight.run("20 tap r1\n", 50, [&](std::uint64_t) { log.note(fight.human().animator().animId()); });
        CHECK(log.has(90));
        CHECK(fight.human().animator().animId() == 84);
        CHECK(fight.human().fighter().combat().mode() == combat::CombatMode::Grabbing);
        CHECK(fight.human().fighter().fromRear());
        CHECK_FALSE(fight.human().fighter().grabbed());
    }
    {
        // A grabber out of power lets go: 94.
        Fight fight(character, 30.0F);
        fight.human().catchInGrab(
            coney::human::GrabCatch{.grabberAnims = &character.anims, .fromRear = true, .grabber = grabber(0)});
        fight.run("", 3);
        CHECK(fight.human().animator().animId() == 94);
        CHECK_FALSE(fight.human().fighter().grabbed());
    }
}

TEST_CASE("a hit on a grabbing player costs it 0.6 of its power", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 1.5F);
    fight.run("5 tap circle\n", 30);
    REQUIRE(fight.human().fighter().combat().mode() == combat::CombatMode::Grabbing);
    const int before = fight.human().fighter().combat().power().value();
    fight.human().takeHit(hitOf(12, 14, 0x0a, 0x800, Vec3{39.0F, 40.0F, 0.0F}));
    fight.run("", 1);
    CHECK(before - fight.human().fighter().combat().power().value() >= 239);
    CHECK(fight.human().fighter().combat().mode() == combat::CombatMode::Grabbing);
}

TEST_CASE("out of power while hurt, the held target escapes and the player goes down", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 1.5F);
    fight.run("5 tap circle\n", 40);
    REQUIRE(fight.human().fighter().pairStage() == coney::human::PairStage::Attached);
    // Hurt (below 30 % of 900) and out of power.
    fight.human().fighter().health().apply(800);
    fight.human().fighter().combat().power().set(0);
    fight.run("", 2);
    CHECK(fight.human().fighter().held() == nullptr);
    CHECK(fight.target().state() == coney::human::TargetState::Standing);
    CHECK(fight.human().fighter().victim().grounded());
    CHECK(fight.human().fighter().victim().stunned());
}

TEST_CASE("locked onto the target the stick at 0.6 walks the player at 3.429 m/s facing it", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 2.0F);
    Vec3 start;
    std::uint32_t walkClip = 0;
    fight.run("0 tap l1\n3 stick left 60 0\n13 stick left 0 0\n", 14, [&](std::uint64_t frame) {
        if (frame == 2) {
            start = fight.human().position();
        }
        if (frame == 8) {
            walkClip = fight.human().animator().animId();
        }
    });
    REQUIRE(fight.human().fighter().lockTarget() != nullptr);
    // Ten updates to the right at 3.429 m/s, without turning away from the target.
    CHECK(fight.human().position().x - start.x == Approx(10.0F * 3.429F / 30.0F).margin(0.15F));
    CHECK(fight.human().position().y == Approx(start.y).margin(0.05F));
    const Vec3 to = coney::anim::subtract(fight.target().position(), fight.human().position());
    CHECK(fight.human().heading() == Approx(coney::human::headingOf(to)).margin(0.05F));
    // The stick 90° clockwise from the facing at first: the right walk.
    CHECK((walkClip == 382 || walkClip == 381));
}

TEST_CASE("with CfgLockOn off and no auto-lock, L1 picks a target but the stick still turns the player",
          "[human][combat]") {
    const TuningScope scope;
    TuningScope::tuning().autoLockAndCombat = false;
    const FightCharacter character;
    Fight fight(character, 2.0F);
    fight.run("0 press l1\n3 stick left 60 0\n", 20);
    CHECK(fight.human().fighter().target() != nullptr);
    CHECK(fight.human().fighter().lockTarget() == nullptr);
    // Turned towards the stick's right, as a free player does.
    CHECK(fight.human().heading() < -0.5F);
}

TEST_CASE("a target farther than 2.5 m is dropped and the lock ends", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 2.0F);
    fight.run("0 tap l1\n", 3);
    REQUIRE(fight.human().fighter().target() != nullptr);
    // Walk back, away from it, at 0.6.
    fight.run("0 stick left 0 -60\n", 20);
    CHECK(fight.human().fighter().target() == nullptr);
}

TEST_CASE("in a standing grab the full stick turns the pair and walks it backward; 0.8 does nothing",
          "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 1.5F);
    fight.run("5 tap circle\n", 40);
    REQUIRE(fight.human().fighter().pairStage() == coney::human::PairStage::Attached);
    const float heading = fight.human().heading();
    const Vec3 position = fight.human().position();
    fight.run("0 stick left 80 0\n", 10);
    CHECK(fight.human().heading() == Approx(heading).margin(1e-4F));
    // Full right: the back turns to +x, so the grabber turns left towards +90° and walks to +x.
    fight.run("0 stick left 100 0\n", 20);
    CHECK(fight.human().heading() > heading + 0.5F);
    CHECK(fight.human().position().x > position.x + 0.3F);
    // The victim keeps the front hold's offset.
    const Vec3 local =
        coney::human::toFrame(fight.human().position(), fight.human().heading(), fight.target().position());
    CHECK(local.x == Approx(0.38F).margin(0.01F));
    CHECK(local.y == Approx(1.01F).margin(0.01F));
}

TEST_CASE("the tackled victim sits at clip 210's pair event", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 2.0F);
    fight.run("5 press circle\n15 release circle\n", 50);
    REQUIRE(fight.target().state() == coney::human::TargetState::Mounted);
    const Vec3 local =
        coney::human::toFrame(fight.human().position(), fight.human().heading(), fight.target().position());
    CHECK(local.x == Approx(-0.120F).margin(0.005F));
    CHECK(local.y == Approx(0.032F).margin(0.005F));
}

TEST_CASE("each human deals its own class's damage from its own copy of the list", "[human][combat]") {
    const FightCharacter character;
    // A class with S1 at 30, scaled for a player at 115 %: 34. The shared list keeps its 17.
    std::array<std::int16_t, coney::combat::kClassDamageEntries> values{};
    values[1] = 30;
    const coney::human::Human classed(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(),
                                      1.0F, &character.ranges, values, 115);
    REQUIRE(classed.ranges() != nullptr);
    CHECK(classed.ranges()->damage(12) == 34);
    CHECK(character.ranges.damage(12) == 17);
    const coney::human::Human plain(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(),
                                    1.0F, &character.ranges);
    CHECK(plain.ranges() == &character.ranges);
}

TEST_CASE("a target's reaction moves it by the clip's root motion", "[human][combat]") {
    // The fight's clips with 272 pushing its human back at 1.5 m/s.
    std::vector<coney::test::LocomotionClip> clips = coney::test::locomotionClips();
    for (coney::test::LocomotionClip clip : coney::test::combatClips()) {
        if (clip.id == 272) {
            clip.rootVelocity = -1.5F;
        }
        clips.push_back(clip);
    }
    const coney::characters::CharacterData data = coney::test::locomotionData(clips);
    const coney::characters::AnimSet anims{data, nullptr};
    coney::human::TargetHuman target(anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 600,
                                     Vec3{40.0F, 41.0F, 0.0F}, std::numbers::pi_v<float>);
    target.hit(hitOf(12, 17, 0x0a, 0x800, Vec3{40.0F, 40.0F, 0.0F}));
    for (int update = 0; update < 12; ++update) {
        target.step();
    }
    CHECK(target.lastReaction() == 272);
    // Pushed away from the attacker, along +y, about 0.4 s × 1.5 m/s.
    CHECK(target.position().y > 41.4F);
    CHECK(target.position().x == Approx(40.0F).margin(0.01F));
}

TEST_CASE("square in the duck's window counters with 617 at an attacker in front", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 30.0F);
    fight.run("0 press r1\n", 5);
    fight.human().warn(coney::human::AttackNotice{
        .warning = combat::AttackWarning::Duck, .attackAnim = 17, .code = 0x2b, .attacker = kInFront});
    ClipLog log;
    // The duck plays at 0.75: its frames 6-13 fall 8 to 17 updates in; square pressed at 10.
    fight.run("0 press r1\n10 tap square\n", 20, [&](std::uint64_t) { log.note(fight.human().animator().animId()); });
    CHECK(log.has(static_cast<std::uint32_t>(combat::kBlockDodge)));
    CHECK(log.has(617));
    CHECK(fight.human().fighter().duckCounters() == 1);
    // Square outside the window (just after the duck starts) does not counter.
    fight.run("0 press r1\n", 20);
    fight.human().warn(coney::human::AttackNotice{
        .warning = combat::AttackWarning::Duck, .attackAnim = 17, .code = 0x2b, .attacker = kInFront});
    fight.run("0 press r1\n2 tap square\n", 25);
    CHECK(fight.human().fighter().duckCounters() == 1);
}

TEST_CASE("six X1 hits in a row on the target halve the seventh's rage", "[human][combat]") {
    const FightCharacter character;
    Fight fight(character, 1.0F);
    // Cross every 40 updates (1.3 s), far apart enough that each is a new chain.
    std::vector<int> gains;
    int last = 0;
    fight.run("10 tap cross\n50 tap cross\n90 tap cross\n130 tap cross\n170 tap cross\n210 tap cross\n"
              "250 tap cross\n",
              290, [&](std::uint64_t) {
                  const int now = fight.human().fighter().combat().rage().value();
                  if (now != last) {
                      gains.push_back(now - last);
                      last = now;
                  }
              });
    REQUIRE(gains.size() >= 7);
    CHECK(gains.at(0) == 5);
    CHECK(gains.at(5) == 5);
    CHECK(gains.at(6) == 2);
}
