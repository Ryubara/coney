// SPDX-License-Identifier: GPL-3.0-or-later
// The world flags (docs/research/flags.md): the pool's size, the 15-character names, the case-sensitive first-match
// search, a new pool hiding the old names but not the handles, and a parent the position and heading follow.
#include "world_objects/flags.h"

#include <array>
#include <optional>

#include <catch2/catch_test_macros.hpp>

using coney::world_objects::Placement;
using coney::world_objects::WorldFlag;
using coney::world_objects::WorldFlags;

TEST_CASE("a flag pool holds the requested count plus four, and a flag past it is still made", "[flags]") {
    WorldFlags flags;
    flags.createPool(1);
    CHECK(flags.capacity() == 5);
    for (int i = 0; i < 6; ++i) {
        flags.add(10.0 + i, "f", {0.0F, 0.0F, 0.0F}, 0.0F);
    }
    CHECK(flags.poolCount() == 6);
    CHECK(flags.overflows() == 1);
    flags.clear();
    CHECK(flags.all().empty());
    CHECK(flags.capacity() == 0);
    CHECK(flags.find(10.0) == nullptr);
}

TEST_CASE("a flag keeps 15 characters of its name, its place, heading and integers, enabled with no parent",
          "[flags]") {
    WorldFlags flags;
    flags.createPool(4);
    const WorldFlag& made = flags.add(7.0, "fWchiefStart_1_too_long", {-188.6F, 95.0F, -194.3F}, 89.0F, 3, -2);
    CHECK(made.name == "fWchiefStart_1_");
    CHECK(made.position == std::array<float, 3>{-188.6F, 95.0F, -194.3F});
    CHECK(made.headingDegrees == 89.0F);
    CHECK(made.kind == 3);
    CHECK(made.kind2 == -2);
    CHECK(made.enabled);
    CHECK(made.parent == 0.0);
    CHECK(made.user == 0.0);
    CHECK(flags.find(7.0) == &made);
}

TEST_CASE("FindFlag's search is case-sensitive, returns the first match and never matches a name past 15 characters",
          "[flags]") {
    WorldFlags flags;
    flags.createPool(4);
    flags.add(1.0, "fP1_01", {}, 0.0F);
    flags.add(2.0, "fP1_01", {}, 0.0F);
    flags.add(3.0, "a_name_of_twenty_chars", {}, 0.0F);
    CHECK(flags.findByName("fP1_01") == std::optional<double>(1.0));
    CHECK(!flags.findByName("FP1_01").has_value());
    CHECK(flags.findByName("a_name_of_twent") == std::optional<double>(3.0));
    CHECK(!flags.findByName("a_name_of_twenty_chars").has_value());
}

TEST_CASE("a second pool hides the earlier flags from the name search but not from their handles", "[flags]") {
    WorldFlags flags;
    flags.createPool(2);
    flags.add(1.0, "old", {1.0F, 2.0F, 3.0F}, 0.0F);
    flags.createPool(2);
    flags.add(2.0, "new", {}, 0.0F);
    CHECK(!flags.findByName("old").has_value());
    CHECK(flags.findByName("new") == std::optional<double>(2.0));
    REQUIRE(flags.find(1.0) != nullptr);
    CHECK(flags.poolCount() == 1);
}

TEST_CASE("a flag's position and heading follow a live parent, and are its own otherwise", "[flags]") {
    WorldFlags flags;
    flags.createPool(1);
    flags.add(5.0, "follow", {1.0F, 1.0F, 1.0F}, 45.0F);
    WorldFlag* flag = flags.find(5.0);
    REQUIRE(flag != nullptr);
    if (flag == nullptr) {
        return;
    }
    // Object 9 stands at (10, 20, 0) facing 90; any other handle is no live object.
    const coney::world_objects::ObjectLocator locate = [](double handle) -> std::optional<Placement> {
        if (handle == 9.0) {
            return Placement{.position = {10.0F, 20.0F, 0.0F}, .headingDegrees = 90.0F};
        }
        return std::nullopt;
    };
    CHECK(WorldFlags::position(*flag, locate) == std::array<float, 3>{1.0F, 1.0F, 1.0F});
    CHECK(WorldFlags::headingDegrees(*flag, locate) == 45.0F);
    flag->parent = 9.0;
    CHECK(WorldFlags::position(*flag, locate) == std::array<float, 3>{10.0F, 20.0F, 0.0F});
    CHECK(WorldFlags::headingDegrees(*flag, locate) == 90.0F);
    flag->parent = 8.0;
    CHECK(WorldFlags::position(*flag, locate) == std::array<float, 3>{1.0F, 1.0F, 1.0F});
    CHECK(WorldFlags::headingDegrees(*flag, {}) == 45.0F);
}
