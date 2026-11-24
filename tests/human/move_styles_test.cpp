// SPDX-License-Identifier: GPL-3.0-or-later
// A human's move styles (human/move_styles.h) and how a held two-handed object is carried with them
// (docs/research/objects.md#two-handed): the style stack, style 4's slots, and a human lifting, carrying and dropping a
// two-handed object. Synthetic clips and floors only.
#include "human/move_styles.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/anim_set.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "raycast/collision_mesh.h"
#include "support/collision_fixtures.h"
#include "support/human_fixtures.h"

using coney::anim::Vec3;
using coney::human::AnimSlots;
using coney::human::Human;
using coney::human::HumanInput;
using coney::human::MoveStyles;

namespace {

constexpr Vec3 kAlongY{0.0F, 1.0F, 0.0F};

// The two-handed clips: the floor pick-up (503, 1 s, still, its take event at 0.5 s), the barrel idle (509), walk start
// (519, 0.3 s, root velocity 1.0) and run start (520), walk 1.4 (515), jog, run and sprint (516-518), run stop (523),
// fight idle (527), turn (529), fight steps (535, 537) and fight walk (541).
std::vector<coney::test::LocomotionClip> carryClips() {
    std::vector<coney::test::LocomotionClip> clips = coney::test::locomotionClips();
    const auto add = [&clips](std::uint32_t id, float speed, float duration, float root, bool loop) {
        clips.push_back({.id = id,
                         .speed = speed,
                         .duration = duration,
                         .rootVelocity = root,
                         .rangeFlags = static_cast<std::uint16_t>(loop ? 0x1000 : 0)});
    };
    add(503, 0.0F, 1.0F, 0.0F, false);
    clips.back().markers = {{15, 9}}; // the take event, half way through
    add(509, 0.0F, 2.0F, 0.0F, false);
    add(519, 0.3F, 0.3F, 1.0F, false);
    add(520, 1.2F, 0.3F, 4.0F, false);
    add(515, 1.4F, 1.0F, 0.0F, true);
    add(516, 3.0F, 0.5F, 0.0F, true);
    add(517, 3.0F, 0.5F, 0.0F, true);
    add(518, 3.0F, 0.5F, 0.0F, true);
    add(523, 1.0F, 0.6F, 2.0F, false);
    for (const std::uint32_t id : {527U, 529U, 535U, 537U, 541U}) {
        add(id, 0.0F, 1.0F, 0.0F, false);
    }
    return clips;
}

// The synthetic character with the two-handed clips, and its anim set, held together for a test.
struct CarryCharacter {
    coney::characters::CharacterData data = coney::test::locomotionData(carryClips());
    coney::characters::AnimSet anims{data, nullptr};
};

// A stick at `x`, `y` seen from a camera looking along +y.
HumanInput stick(float x, float y) {
    return HumanInput{.stickX = x, .stickY = y, .cameraForward = kAlongY, .sprintHeld = false, .targets = {}};
}

} // namespace

TEST_CASE("move style 4 writes the barrel clips over the default slots, and popping it gives the defaults back",
          "[human][move_styles]") {
    // Expected: objects.md#two-handed, confirmed (code) at 0x00253688 (case 4) and 0x00253f28
    const AnimSlots defaults = AnimSlots::player();
    const AnimSlots carry = coney::human::styledSlots(defaults, coney::human::kStyleCarry, true);
    const std::vector<std::pair<std::size_t, std::uint32_t>> expected{
        {0, 509}, {1, 535},  {2, 537},  {3, 515},  {4, 515},  {5, 516},  {6, 517},  {7, 518}, {8, 529},
        {9, 519}, {10, 519}, {11, 527}, {12, 535}, {13, 537}, {14, 541}, {15, 529}, {33, 523}};
    for (std::size_t slot = 0; slot < AnimSlots::kCount; ++slot) {
        std::uint32_t want = defaults.ids.at(slot);
        for (const auto& [s, id] : expected) {
            if (s == slot) {
                want = id;
            }
        }
        CHECK(carry.ids.at(slot) == want);
    }
    // The ghetto blasters' style has the same shape: idle 555, walk 561, run 563, run stop 569.
    const AnimSlots ghetto = coney::human::styledSlots(defaults, coney::human::kStyleGhetto, true);
    CHECK(ghetto.ids.at(0) == 555);
    CHECK(ghetto.ids.at(4) == 561);
    CHECK(ghetto.ids.at(6) == 563);
    CHECK(ghetto.ids.at(33) == 569);
    // A style the switch does not name keeps the defaults.
    CHECK(coney::human::styledSlots(defaults, 5, true).ids == defaults.ids);

    MoveStyles styles;
    CHECK(styles.push(coney::human::kStyleCarry) == coney::human::kStyleCarry);
    CHECK(styles.top() == coney::human::kStyleCarry);
    CHECK(styles.pop() == coney::human::kStyleNone);
    CHECK(styles.depth == 0);
}

TEST_CASE("a carried set replaces the hidden style, goes under any other, and its removal from under the top changes "
          "nothing on top",
          "[human][move_styles]") {
    // Expected: objects.md#two-handed, confirmed (code) at 0x00221ed0, 0x00253e68 and 0x00253f78
    MoveStyles styles;
    // Nothing for set 0; pushed on an empty stack.
    CHECK(styles.carry(coney::human::kStyleNone) == -1);
    CHECK(styles.depth == 0);
    CHECK(styles.carry(coney::human::kStyleCarry) == coney::human::kStyleCarry);
    CHECK(styles.depth == 1);
    CHECK(styles.remove(coney::human::kStyleCarry) == coney::human::kStyleNone);

    // Hidden, set 4 takes the hidden style's place; set 3 goes under it.
    static_cast<void>(styles.push(coney::human::kStyleHidden));
    CHECK(styles.carry(coney::human::kStyleCarry) == coney::human::kStyleCarry);
    CHECK(styles.depth == 1);
    CHECK(styles.top() == coney::human::kStyleCarry);
    static_cast<void>(styles.pop());
    static_cast<void>(styles.push(coney::human::kStyleHidden));
    CHECK(styles.carry(3) == coney::human::kStyleHidden);
    CHECK(styles.depth == 2);
    CHECK(styles.entries.at(1) == 3);
    CHECK(styles.top() == coney::human::kStyleHidden);
    // The bat's set removed from under the hidden style: the hidden style stays on top, nothing to apply.
    CHECK(styles.remove(3) == -1);
    CHECK(styles.depth == 1);
    CHECK(styles.top() == coney::human::kStyleHidden);
    // A style not on the stack changes nothing.
    CHECK(styles.remove(coney::human::kStyleGhetto) == -1);
    CHECK(styles.depth == 1);
    // Replacing the top of an empty stack with style 0 applies style 0 and pushes nothing.
    MoveStyles empty;
    CHECK(empty.replaceTop(coney::human::kStyleNone) == coney::human::kStyleNone);
    CHECK(empty.depth == 0);
}

TEST_CASE("a push on a full move style stack loses the lowest pushed style", "[human][move_styles]") {
    // Expected: objects.md#two-handed, confirmed (code) at 0x00253ed0
    MoveStyles styles;
    for (const int style : {1, 2, 3, 4}) {
        static_cast<void>(styles.push(static_cast<std::uint8_t>(style)));
    }
    CHECK(styles.depth == MoveStyles::kMaxDepth);
    CHECK(styles.entries.at(1) == 2);
    CHECK(styles.entries.at(2) == 3);
    CHECK(styles.entries.at(3) == 4);
}

TEST_CASE("a two-handed pick-up ends in the barrel idle, walks with the barrel clips, and a drop brings back the "
          "plain idle",
          "[human][move_styles]") {
    // Expected: objects.md#two-handed, confirmed (runtime): after 504 the idle is 509, the stick plays 519 then 515,
    // and the drop goes back to 388; confirmed (code) at 0x0025e5a8, 0x00221ed0 and 0x00257f38
    const CarryCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human(character.anims, AnimSlots::player(), coney::test::identityBind());
    human.spawn(mesh.get(), Vec3{20.0F, 20.0F, 0.0F}, 0.0F);
    human.step(stick(0.0F, 0.0F), mesh.get());
    // Lift a set-4 object just ahead with the floor clip; the take's message 3 at the clip's end.
    REQUIRE(human.startPickUp(7.0, Vec3{20.0F, 20.8F, 0.0F}, 503, coney::human::kStyleCarry));
    CHECK(human.animator().moveStyle() == coney::human::kStyleNone);
    bool taken = false;
    for (int k = 0; k < 90; ++k) {
        human.step(stick(0.0F, 0.0F), mesh.get());
        if (human.takePickedUp()) {
            human.takeHeldStyle(coney::human::kStyleCarry);
            taken = true;
        }
    }
    REQUIRE(taken);
    CHECK(human.animator().moveStyle() == coney::human::kStyleCarry);
    CHECK(human.animator().slots().ids.at(coney::human::kSlotIdle) == 509);
    CHECK(human.animator().animId() == 509);
    // The stick: the barrel walk start, then the barrel walk at its own speed.
    human.step(stick(0.0F, 0.5F), mesh.get());
    CHECK(human.animator().animId() == 519);
    for (int k = 0; k < 40; ++k) {
        human.step(stick(0.0F, 0.5F), mesh.get());
    }
    CHECK(human.animator().animId() == 515);
    CHECK(human.speeds().walk == Catch::Approx(1.4F));
    for (int k = 0; k < 60; ++k) {
        human.step(stick(0.0F, 0.0F), mesh.get());
    }
    CHECK(human.animator().animId() == 509);
    // The drop: the style comes off and the plain idle plays at once.
    human.releaseHeldStyle(coney::human::kStyleCarry, false);
    CHECK(human.animator().moveStyle() == coney::human::kStyleNone);
    CHECK(human.animator().animId() == 388);
    CHECK(human.speeds().walk == Catch::Approx(1.5F));
}
