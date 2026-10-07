// SPDX-License-Identifier: GPL-3.0-or-later
// The hats humans wear: the hat-fit sets, where a hat sits on the head, and a knocked-off hat's throw and fall
// (docs/research/characters.md#hats).
#include "world_objects/hats.h"

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "scripting/lua_value.h"

using Catch::Matchers::WithinAbs;
using coney::anim::Quat;
using coney::anim::Vec3;
using coney::script::Value;
using coney::world_objects::FallingHats;
using coney::world_objects::hatAttachment;
using coney::world_objects::HatFit;
using coney::world_objects::HatFits;
using coney::world_objects::hatThrow;
using coney::world_objects::WorldPose;

namespace {

// A Lua table of `numbers` at t[1]..t[n].
Value table(std::initializer_list<double> numbers) {
    auto made = std::make_shared<coney::script::Table>();
    double key = 1.0;
    for (const double number : numbers) {
        REQUIRE(made->set(Value(key), Value(number)).has_value());
        key += 1.0;
    }
    return Value(made);
}

} // namespace

TEST_CASE("CfgHat fills a set's slots in order and a Warrior's own set comes before his class's") {
    coney::script::RecordedCalls recorded;
    const std::vector<Value> first{Value(1.0), Value(7.0), Value(std::string("dyn_abe")),
                                   table({0.0681, -0.0019, 0.0175}), table({0.5438, -0.5438, 0.452, -0.452})};
    recorded.add("CfgHat", first);
    const std::vector<Value> second{Value(2.0), Value(64.0), Value(std::string("dyn_abe")), table({0.1, 0.0, 0.0}),
                                    table({0.0, 0.0, 0.0, 1.0})};
    recorded.add("CfgHat", second);
    const HatFits fits = HatFits::fromRecorded(recorded);

    const std::optional<HatFit> own = fits.find(7, 64, "dyn_abe");
    REQUIRE(own.has_value());
    CHECK_THAT(own->offset.x, WithinAbs(0.0681, 1e-5));
    CHECK_THAT(own->rotation.w, WithinAbs(-0.452, 1e-5));
    // A type that owns no set uses its class's.
    const std::optional<HatFit> classes = fits.find(9, 64, "dyn_abe");
    REQUIRE(classes.has_value());
    CHECK_THAT(classes->offset.x, WithinAbs(0.1, 1e-6));
    CHECK_FALSE(fits.find(7, 64, "dyn_other").has_value());
    CHECK_FALSE(fits.find(9, 10, "dyn_abe").has_value());
}

TEST_CASE("A set keeps at most 48 hats") {
    HatFits fits;
    for (int i = 0; i < 50; ++i) {
        fits.add(3, 5, "hat" + std::to_string(i), HatFit{});
    }
    CHECK(fits.find(5, 0, "hat47").has_value());
    CHECK_FALSE(fits.find(5, 0, "hat48").has_value());
}

TEST_CASE("A hat sits on the head bone at its fitting, the offset times the wearer's scale") {
    HatFits fits;
    fits.add(1, 7, "dyn_abe", HatFit{.offset = Vec3{0.1F, 0.0F, 0.02F}, .rotation = Quat{0.0F, 0.0F, 1.0F, 0.0F}});
    const HatFit own{.offset = Vec3{0.2F, 0.0F, 0.0F}, .rotation = Quat{}};

    // A Warrior wears it at his set's fitting.
    const auto warrior = hatAttachment(fits, true, 7, 64, "dyn_abe", own, 2.0F);
    CHECK(warrior.bone == coney::world_objects::kHatBone);
    CHECK_THAT(warrior.position.x, WithinAbs(0.2, 1e-6));
    CHECK_THAT(warrior.position.z, WithinAbs(0.04, 1e-6));
    CHECK_THAT(warrior.rotation.z, WithinAbs(1.0, 1e-6));
    // Anyone else, or a Warrior whose set has no slot for it, wears it at the hat's own pose.
    const auto other = hatAttachment(fits, false, 7, 64, "dyn_abe", own, 1.0F);
    CHECK_THAT(other.position.x, WithinAbs(0.2, 1e-6));
    CHECK_THAT(other.rotation.w, WithinAbs(1.0, 1e-6));
    const auto unfitted = hatAttachment(fits, true, 7, 64, "dyn_new", own, 1.0F);
    CHECK_THAT(unfitted.position.x, WithinAbs(0.2, 1e-6));
}

TEST_CASE("A knocked-off hat is thrown by the side of the blow and spins about the matching axis") {
    CHECK(hatThrow(0).direction == Vec3{0.0F, 1.0F, 0.0F});
    CHECK(hatThrow(0).spinAxis == Vec3{1.0F, 0.0F, 0.0F});
    CHECK(hatThrow(1).direction == Vec3{1.0F, 0.0F, 0.0F});
    CHECK(hatThrow(1).spinAxis == Vec3{0.0F, -1.0F, 0.0F});
    CHECK(hatThrow(2).direction == Vec3{0.0F, -1.0F, 0.0F});
    CHECK(hatThrow(3).spinAxis == Vec3{0.0F, 1.0F, 0.0F});
    CHECK(hatThrow(6).direction == hatThrow(2).direction);
}

TEST_CASE("A knocked-off hat flies at 2 m/s turned by the wearer's heading, falls and stops on the ground") {
    FallingHats hats;
    // Facing +x (heading -90 degrees turns +y onto +x), thrown ahead.
    const float heading = -1.5707963F;
    hats.knockOff(10.0, WorldPose{Vec3{0.0F, 0.0F, 1.7F}, Quat{}}, heading, hatThrow(0));
    CHECK(hats.falling(10.0));
    const auto ground = [](Vec3) { return std::optional<float>(0.0F); };

    const auto first = hats.step(0.1F, ground);
    REQUIRE(first.size() == 1);
    CHECK_THAT(first[0].second.position.x, WithinAbs(0.2, 1e-4));
    CHECK_THAT(first[0].second.position.y, WithinAbs(0.0, 1e-4));
    CHECK(first[0].second.position.z < 1.7F);

    std::optional<WorldPose> last;
    for (int i = 0; i < 100 && hats.falling(10.0); ++i) {
        const auto moved = hats.step(1.0F / 30.0F, ground);
        REQUIRE(moved.size() == 1);
        last = moved[0].second;
    }
    CHECK_FALSE(hats.falling(10.0));
    REQUIRE(last.has_value());
    CHECK_THAT(last->position.z, WithinAbs(0.0, 1e-6));
    CHECK(last->position.x > 1.0F);
    // Once landed it is no longer moved.
    CHECK(hats.step(0.1F, ground).empty());
}
