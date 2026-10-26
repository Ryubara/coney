// SPDX-License-Identifier: GPL-3.0-or-later
// The revive's "way clear" test (human/way_clear.h, docs/research/combat-reactions.md#revive): a 0.2 m capsule as tall
// as the reviver swept from his feet to the downed human's passes when it meets nothing, or meets the downed human
// first. Synthetic walls and capsules.
#include <array>

#include <catch2/catch_test_macros.hpp>

#include "human/human_contacts.h"
#include "human/way_clear.h"
#include "support/collision_fixtures.h"

using coney::anim::Vec3;
using coney::human::UprightCapsule;
using coney::human::wayClear;

namespace {

// A human's 2 m capsule of radius 0.35 standing at (x, y) on the floor at 0.
UprightCapsule standing(float x, float y) {
    return UprightCapsule{.feet = Vec3{x, y, 0.0F}, .top = 2.0F, .radius = 0.35F};
}

constexpr Vec3 kFrom{40.0F, 40.0F, 0.0F};
constexpr float kHeight = 2.0F;

} // namespace

TEST_CASE("the way to a downed human is clear when the capsule meets nothing else first", "[human][revive]") {
    const auto floor = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    const UprightCapsule downed = standing(42.0F, 40.0F);

    // Nobody else, the open floor: clear.
    CHECK(wayClear(floor.get(), kFrom, kHeight, downed, {}));
    // Someone beside the line, not in the 0.2 m capsule's way: clear.
    const std::array<UprightCapsule, 1> aside{standing(41.0F, 41.0F)};
    CHECK(wayClear(floor.get(), kFrom, kHeight, downed, aside));
    // Someone beyond the downed human on the same line: the downed is met first, clear.
    const std::array<UprightCapsule, 1> beyond{standing(43.5F, 40.0F)};
    CHECK(wayClear(floor.get(), kFrom, kHeight, downed, beyond));
    // Someone between them: not clear.
    const std::array<UprightCapsule, 1> between{standing(41.0F, 40.1F)};
    CHECK_FALSE(wayClear(floor.get(), kFrom, kHeight, downed, between));
    // No mesh: only the humans count.
    CHECK(wayClear(nullptr, kFrom, kHeight, downed, {}));
}

TEST_CASE("a wall between the reviver and the downed human blocks the way; one behind him does not",
          "[human][revive]") {
    const UprightCapsule downed = standing(42.0F, 40.0F);
    // A 3 m wall across the line at x = 41, facing the reviver.
    const auto walled =
        coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F),
                                                coney::test::wallFacingMinusX(41.0F, 35.0F, 45.0F, 0.0F, 3.0F)));
    CHECK_FALSE(wayClear(walled.get(), kFrom, kHeight, downed, {}));
    // The same wall behind the downed human (x = 43): clear.
    const auto behind =
        coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F),
                                                coney::test::wallFacingMinusX(43.0F, 35.0F, 45.0F, 0.0F, 3.0F)));
    CHECK(wayClear(behind.get(), kFrom, kHeight, downed, {}));
    // A wall right behind the reviver, facing away from the move: clear.
    const auto atBack =
        coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F),
                                                coney::test::wallFacingMinusX(39.9F, 35.0F, 45.0F, 0.0F, 3.0F)));
    CHECK(wayClear(atBack.get(), kFrom, kHeight, downed, {}));
}
