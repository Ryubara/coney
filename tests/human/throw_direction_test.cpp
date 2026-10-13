// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "human/locomotion.h"
#include "human/pair_placement.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

// Where a throw from a grab sends the body (docs/research/combat-throws.md#throw-direction): from a front hold the pair
// is turned onto the stick over 1/6 s, the thrower so that the side's body axis points along the stick and the victim
// carried round him, so the body flies along the stick whatever its angle within the side's 90 degrees; from a rear
// hold the spin to the front plays first and nothing turns. A grabbed passive target in open ground, the stick at 0.9
// (below the hold's walk at 0.95) pushed on the update circle is pressed.

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::wrapAngle;
using coney::test::Fight;
using coney::test::FightCharacter;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDegrees = kPi / 180.0F;

// The update circle is pressed on, well after the grab has attached its victim.
constexpr std::uint64_t kThrowFrame = 45;

// The synthetic character with every throw clip: the thrower's 147-153 standing still for 0.6 s, the victim's
// 148-154 carrying it backwards (away from the thrower it faces) at 1 m/s for 1.2 s, so where it lands shows the
// direction it was thrown in.
std::vector<coney::test::LocomotionClip> throwClips() {
    std::vector<coney::test::LocomotionClip> clips = coney::test::fightClips();
    std::erase_if(clips, [](const coney::test::LocomotionClip& clip) { return clip.id >= 147 && clip.id <= 154; });
    for (std::uint32_t id = 147; id <= 154; ++id) {
        const bool victim = id % 2 == 0;
        clips.push_back({.id = id,
                         .speed = 0.0F,
                         .duration = victim ? 1.2F : 0.6F,
                         .rootVelocity = victim ? -1.0F : 0.0F,
                         .rangeFlags = 0,
                         .reach = 0.0F,
                         .knockdown = false});
    }
    return clips;
}

// One throw as measured: the stick's heading on the press, the thrower's heading 5 updates on and at the end, and
// where the victim landed in the stick's frame (metres right of and along the stick heading from where the thrower
// stood on the update before the press).
struct Throw {
    float stickHeading = 0.0F;
    float headingBefore = 0.0F;
    float headingAfterTurn = 0.0F;
    float headingAtEnd = 0.0F;
    float victimHeadingAtEnd = 0.0F;
    Vec3 landing;
    std::vector<std::uint32_t> throwerClips;
    std::vector<std::uint32_t> victimClips;
};

// Appends `clip` to `clips` when it differs from the last one.
void addChange(std::vector<std::uint32_t>& clips, std::uint32_t clip) {
    if (clips.empty() || clips.back() != clip) {
        clips.push_back(clip);
    }
}

// Grabs the target (`fromRear`: one facing away, so the grab is a rear hold) and throws it with the stick at 0.9,
// `degrees` clockwise of the facing (the camera looks along the facing).
Throw throwAt(float degrees, bool fromRear = false) {
    const FightCharacter character(throwClips());
    Fight fight = fromRear ? Fight(character, 1.2F, 0.0F, 0.0F) : Fight(character, 1.5F);
    const auto x = static_cast<int>(std::lround(90.0F * std::sin(degrees * kDegrees)));
    const auto y = static_cast<int>(std::lround(90.0F * std::cos(degrees * kDegrees)));
    const std::string script = std::format("5 tap circle\n{0} stick left {1} {2}\n{0} tap circle\n{3} stick left 0 0\n",
                                           kThrowFrame, x, y, kThrowFrame + 9);
    Throw out;
    Vec3 start;
    fight.run(script, kThrowFrame + 70, [&](std::uint64_t frame) {
        const coney::human::Human& human = fight.human();
        if (frame == kThrowFrame - 1) {
            start = human.position();
            out.headingBefore = human.heading();
        }
        if (frame == kThrowFrame) {
            // The camera-turned stick of the press's update; its heading is its angle less 90 degrees.
            out.stickHeading = wrapAngle(human.intent().angle - (kPi / 2.0F));
        }
        if (frame >= kThrowFrame) {
            addChange(out.throwerClips, human.animator().animId());
            addChange(out.victimClips, fight.target().animator().animId());
        }
        if (frame == kThrowFrame + 5) {
            out.headingAfterTurn = human.heading();
        }
    });
    out.headingAtEnd = fight.human().heading();
    out.victimHeadingAtEnd = fight.target().heading();
    out.landing = coney::human::toFrame(start, out.stickHeading, fight.target().position());
    return out;
}

// Whether `a` and `b` are within `degrees` of each other, round the circle.
bool near(float a, float b, float degrees) { return std::fabs(wrapAngle(a - b)) <= degrees * kDegrees; }

} // namespace

TEST_CASE("from a front hold the thrower turns over 5 updates so the side's axis points along the stick",
          "[human][combat][throw]") {
    // The stick's angle clockwise of the facing, and the side it falls on (0 front, 1 right, 2 rear, 3 left).
    struct Case {
        float degrees;
        int side;
    };
    const std::array<Case, 9> cases{{{0.0F, 0},
                                     {20.0F, 0},
                                     {-20.0F, 0},
                                     {40.0F, 0},
                                     {-40.0F, 0},
                                     {50.0F, 1},
                                     {90.0F, 1},
                                     {-160.0F, 2},
                                     {-110.0F, 3}}};
    std::array<std::vector<Vec3>, 4> landings;
    for (const Case& c : cases) {
        CAPTURE(c.degrees);
        const Throw thrown = throwAt(c.degrees);
        // The stick read is the one pushed, give or take the pad's quantisation.
        CHECK(near(thrown.stickHeading, thrown.headingBefore - (c.degrees * kDegrees), 1.0F));
        // The synthetic clips do not turn the thrower: the 1/6 s turn alone takes him to stick + side x 90 degrees,
        // in 5 updates, and he stays there; the victim ends facing him.
        const float wanted = wrapAngle(thrown.stickHeading + (static_cast<float>(c.side) * kPi / 2.0F));
        CHECK(near(thrown.headingAfterTurn, wanted, 0.5F));
        CHECK(near(thrown.headingAtEnd, wanted, 0.5F));
        CHECK(near(thrown.victimHeadingAtEnd, wanted + kPi, 0.5F));
        CHECK(std::ranges::find(thrown.throwerClips, 147U + (2U * static_cast<std::uint32_t>(c.side))) !=
              thrown.throwerClips.end());
        landings[static_cast<std::size_t>(c.side)].push_back(thrown.landing);
    }
    // The landing point in the stick's frame is the same for every angle of one side (within 0.1 m): the turn takes
    // the whole difference between the stick and the side's axis.
    for (const std::vector<Vec3>& side : landings) {
        for (const Vec3& landing : side) {
            CHECK(std::hypot(landing.x - side.front().x, landing.y - side.front().y) <= 0.1F);
        }
    }
    // A front throw: the victim carried round at the hold's offset (0.38 m right, 1.01 m ahead) and flown 1.2 m on
    // along the stick.
    REQUIRE(!landings[0].empty());
    CHECK(landings[0].front().x == Approx(0.38F).margin(0.1F));
    CHECK(landings[0].front().y == Approx(1.01F + 1.2F).margin(0.1F));
}

TEST_CASE("from a rear hold the spin to the front plays first, then the side's throw, with no turn",
          "[human][combat][throw]") {
    // The stick 60 degrees right of the facing (30 short of the right axis): the right throw (149), which goes along
    // the right axis, not the stick. (Item 6 of the page says 30 degrees, which item 2 makes a front throw.)
    const Throw thrown = throwAt(60.0F, true);
    REQUIRE(thrown.throwerClips.size() >= 2);
    CHECK(thrown.throwerClips[0] == 80U);
    CHECK(thrown.throwerClips[1] == 149U);
    REQUIRE(!thrown.victimClips.empty());
    CHECK(thrown.victimClips.front() == 81U);
    CHECK(std::ranges::find(thrown.victimClips, 150U) != thrown.victimClips.end());
    CHECK(near(thrown.headingAfterTurn, thrown.headingBefore, 0.01F));
    CHECK(near(thrown.headingAtEnd, thrown.headingBefore, 0.01F));
}
