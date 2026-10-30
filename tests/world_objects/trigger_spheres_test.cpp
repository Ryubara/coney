// SPDX-License-Identifier: GPL-3.0-or-later
// The trigger spheres (docs/research/scripting.md#triggers): the pool of 100, each sphere checked every fifth frame,
// messages 3, 5 and 4 to the sphere's object, and the clear-line modes. Synthetic objects and humans.
#include "world_objects/trigger_spheres.h"

#include <array>
#include <cstdint>
#include <optional>
#include <tuple>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using coney::world_objects::BoxSubject;
using coney::world_objects::TriggerSpheres;
using coney::world_objects::VolumeBoxes;

namespace {

// The messages an update sent: (object, message, human).
using Sent = std::vector<std::tuple<double, int, double>>;

// One frame of `spheres` over `subjects` at `nowMs`; returns what it sent.
Sent step(TriggerSpheres& spheres, const std::vector<BoxSubject>& subjects, std::uint64_t nowMs) {
    Sent sent;
    spheres.update(subjects, nowMs,
                   [&](double object, int message, double human) { sent.emplace_back(object, message, human); });
    return sent;
}

// Five frames (one check of every sphere) at `nowMs`; returns everything sent.
Sent checkAll(TriggerSpheres& spheres, const std::vector<BoxSubject>& subjects, std::uint64_t nowMs) {
    Sent all;
    for (std::uint32_t i = 0; i < TriggerSpheres::kFramesPerCheck; ++i) {
        const Sent sent = step(spheres, subjects, nowMs);
        all.insert(all.end(), sent.begin(), sent.end());
    }
    return all;
}

// Objects at fixed places: 7 (a dealer) at the origin; anything else is gone.
std::optional<std::array<float, 3>> locate(double object) {
    if (object == 7) {
        return std::array<float, 3>{0, 0, 0};
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("a sphere sends its object 3 on entry, 5 once a period while inside and 4 on leaving", "[trigger_spheres]") {
    // Expected: trigger-updates.md#stay-timer, confirmed (code): TriggerSphere_Update 0x004146e0 sends 5 only once the
    // time is strictly past the stay timer (+0x178), which it re-arms to now + period on each due check.
    TriggerSpheres spheres;
    spheres.setLocate(locate);
    REQUIRE(spheres.configure(7, true, 4.0F, 0, 500));
    // The object itself, standing at the centre, is never an occupant; the human 3 m away enters.
    std::vector<BoxSubject> humans{{.handle = 7, .position = {0, 0, 0}, .alive = true, .player = false},
                                   {.handle = 100, .position = {3, 0, 0}, .alive = true, .player = false}};
    CHECK(checkAll(spheres, humans, 0) == Sent{{7, VolumeBoxes::kEntered, 100}});
    // Still inside: 5 at the next check, then once the sphere's own period (1000 ms) is due again.
    CHECK(checkAll(spheres, humans, 600) == Sent{{7, VolumeBoxes::kInside, 100}});
    CHECK(checkAll(spheres, humans, 1500).empty());
    CHECK(checkAll(spheres, humans, 1600).empty()); // due only once strictly past the timer
    CHECK(checkAll(spheres, humans, 1700) == Sent{{7, VolumeBoxes::kInside, 100}});
    // Out of the radius: 4; dead inside counts as leaving too.
    humans[1].position = {5, 0, 0};
    CHECK(checkAll(spheres, humans, 2000) == Sent{{7, VolumeBoxes::kLeft, 100}});
    humans[1].position = {1, 0, 0};
    CHECK(checkAll(spheres, humans, 2100) == Sent{{7, VolumeBoxes::kEntered, 100}});
    humans[1].alive = false;
    CHECK(checkAll(spheres, humans, 2200) == Sent{{7, VolumeBoxes::kLeft, 100}});
}

TEST_CASE("a sphere's stay timer re-arms when due with nobody inside, so a newcomer's 5 comes on the shared beat",
          "[trigger_spheres]") {
    // Expected: trigger-updates.md#stay-timer, confirmed (code): TriggerSphere_Update 0x004146e0 re-arms +0x178 on
    // every due check, whether or not anyone is inside.
    TriggerSpheres spheres;
    spheres.setLocate(locate);
    REQUIRE(spheres.configure(7, true, 4.0F, 0, 500));
    const std::vector<BoxSubject> inside{{.handle = 100, .position = {3, 0, 0}, .alive = true, .player = false}};
    CHECK(checkAll(spheres, {}, 600).empty()); // due: the timer goes to 1600
    CHECK(checkAll(spheres, inside, 700) == Sent{{7, VolumeBoxes::kEntered, 100}});
    CHECK(checkAll(spheres, inside, 1500).empty());
    CHECK(checkAll(spheres, inside, 1601) == Sent{{7, VolumeBoxes::kInside, 100}});
}

TEST_CASE("a handler that disarms its sphere stops the sphere's check", "[trigger_spheres]") {
    // Expected: trigger-updates.md#sphere, confirmed (code): TriggerSphere_Update 0x004146e0 sends each message at once
    // and stops the check once the sphere is disarmed (+0x180).
    TriggerSpheres spheres;
    spheres.setLocate(locate);
    REQUIRE(spheres.configure(7, true, 4.0F, 0, 500));
    const std::vector<BoxSubject> inside{{.handle = 100, .position = {1, 0, 0}, .alive = true, .player = false},
                                         {.handle = 101, .position = {2, 0, 0}, .alive = true, .player = false}};
    Sent sent;
    spheres.update(inside, 0, [&](double object, int message, double human) {
        sent.emplace_back(object, message, human);
        spheres.arm(object, false);
    });
    CHECK(sent == Sent{{7, VolumeBoxes::kEntered, 100}});
}

TEST_CASE("a handler that switches its sphere off with TriggerSphereCfg ends the found humans, not the occupants' pass",
          "[trigger_spheres]") {
    // Expected: trigger-updates.md#sphere, confirmed (code): in TriggerSphere_Update 0x004146e0 a sphere found disarmed
    // (+0x180) after a message only leaves the found loop (0x004148ec); the occupants' pass still runs unless the
    // sphere was freed. TriggerSphereCfg off (0x00414bc0) keeps the occupants, so one who left still gets 4.
    TriggerSpheres spheres;
    spheres.setLocate(locate);
    REQUIRE(spheres.configure(7, true, 4.0F, 0, 500));
    std::vector<BoxSubject> humans{{.handle = 101, .position = {5, 0, 0}, .alive = true, .player = false},
                                   {.handle = 100, .position = {1, 0, 0}, .alive = true, .player = false}};
    CHECK(checkAll(spheres, humans, 0) == Sent{{7, VolumeBoxes::kEntered, 100}});
    // 100 walks out as 101 walks in; 101's message 3 makes the handler switch the sphere off.
    humans[0].position = {1, 0, 0};
    humans[1].position = {5, 0, 0};
    Sent sent;
    for (std::uint32_t i = 0; i < TriggerSpheres::kFramesPerCheck; ++i) {
        spheres.update(humans, 100, [&](double object, int message, double human) {
            sent.emplace_back(object, message, human);
            if (message == VolumeBoxes::kEntered) {
                REQUIRE(spheres.configure(object, false, 4.0F, 0, 500));
            }
        });
    }
    CHECK(sent == Sent{{7, VolumeBoxes::kEntered, 101}, {7, VolumeBoxes::kLeft, 100}});
    CHECK(spheres.find(7)->occupants == std::vector<double>{101});
}

TEST_CASE("a sphere's occupant whose handle no longer resolves is kept without message 4", "[trigger_spheres]") {
    // Expected: trigger-updates.md#sphere, confirmed (code): TriggerSphere_Update 0x004146e0 skips an occupant whose
    // handle does not resolve (0x00390268), sending nothing and keeping its entry.
    TriggerSpheres spheres;
    spheres.setLocate(locate);
    REQUIRE(spheres.configure(7, true, 4.0F, 0, 500));
    const std::vector<BoxSubject> inside{{.handle = 100, .position = {1, 0, 0}, .alive = true, .player = false}};
    CHECK(checkAll(spheres, inside, 0) == Sent{{7, VolumeBoxes::kEntered, 100}});
    CHECK(checkAll(spheres, {}, 100).empty());
    CHECK(spheres.find(7)->occupants.size() == 1);
}

TEST_CASE("each sphere is checked on one frame in five", "[trigger_spheres]") {
    // Expected: scripting.md#triggers, confirmed (code): the pool is checked round-robin, each sphere every fifth frame
    // (0x00414398)
    TriggerSpheres spheres;
    spheres.setLocate(locate);
    REQUIRE(spheres.configure(7, true, 4.0F, 0, 1));
    const std::vector<BoxSubject> humans{{.handle = 100, .position = {1, 0, 0}, .alive = true, .player = false}};
    // Sphere 0 is checked on frames 0, 5, 10, ...
    CHECK(step(spheres, humans, 0).size() == 1);
    for (int frame = 1; frame < 5; ++frame) {
        CHECK(step(spheres, humans, 0).empty());
    }
}

TEST_CASE("an unarmed sphere, or one round a gone object, sends nothing; the pool holds 100", "[trigger_spheres]") {
    // Expected: scripting.md#triggers, confirmed (code): a pool of 100 spheres, checked only while armed (+0x180)
    TriggerSpheres spheres;
    spheres.setLocate(locate);
    const std::vector<BoxSubject> humans{{.handle = 100, .position = {1, 0, 0}, .alive = true, .player = false}};
    REQUIRE(spheres.configure(7, false, 4.0F, 0, 1));
    CHECK(checkAll(spheres, humans, 0).empty());
    // Arming it again reconfigures the same sphere.
    REQUIRE(spheres.configure(7, true, 4.0F, 0, 1));
    CHECK(spheres.all().size() == 1);
    CHECK(checkAll(spheres, humans, 0).size() == 1);

    TriggerSpheres full;
    full.setLocate(locate);
    for (int i = 0; i < 100; ++i) {
        REQUIRE(full.configure(1000 + i, true, 1.0F, 0, 1));
    }
    CHECK_FALSE(full.configure(7, true, 1.0F, 0, 1));
    CHECK(checkAll(full, humans, 0).empty()); // none of their objects is found
}

TEST_CASE("modes 1 and 2 also need a clear line, mode 2 from a raised centre", "[trigger_spheres]") {
    // Expected: scripting.md#triggers, inferred from the line tests 0x0024dee8 and 0x0024df40 (the raised ends are
    // Coney's choice)
    TriggerSpheres spheres;
    spheres.setLocate(locate);
    std::vector<std::array<float, 3>> starts;
    // A wall the line through x = 1 crosses below 0.5 m.
    spheres.setClearLine([&](const std::array<float, 3>& from, const std::array<float, 3>& to) {
        starts.push_back(from);
        return !(from[0] < 1 && to[0] > 1 && from[2] < 0.5F);
    });
    const std::vector<BoxSubject> humans{{.handle = 100, .position = {2, 0, 0}, .alive = true, .player = false}};
    REQUIRE(spheres.configure(7, true, 4.0F, 1, 1));
    CHECK(checkAll(spheres, humans, 0).empty());
    REQUIRE(spheres.configure(7, true, 4.0F, 2, 1));
    CHECK(checkAll(spheres, humans, 0) == Sent{{7, VolumeBoxes::kEntered, 100}});
    REQUIRE(starts.size() >= 2);
    CHECK(starts[0][2] == 0.0F);
    CHECK(starts.back()[2] == TriggerSpheres::kLineRaise);
}

TEST_CASE("a sphere of a mode other than 1 and 2 accepts on the radius alone", "[trigger_spheres]") {
    // Expected: trigger-updates.md#accepts, confirmed (code): TriggerSphere_Accepts 0x00414548 runs a line test only
    // for modes 1 and 2; any other mode accepts once the radius test passes.
    TriggerSpheres spheres;
    spheres.setLocate(locate);
    int lineTests = 0;
    spheres.setClearLine([&](const std::array<float, 3>&, const std::array<float, 3>&) {
        ++lineTests;
        return false;
    });
    const std::vector<BoxSubject> humans{{.handle = 100, .position = {2, 0, 0}, .alive = true, .player = false}};
    REQUIRE(spheres.configure(7, true, 4.0F, 3, 1));
    CHECK(checkAll(spheres, humans, 0) == Sent{{7, VolumeBoxes::kEntered, 100}});
    CHECK(lineTests == 0);
}

TEST_CASE("TriggerSphereEnable arms a sphere, making one of radius 0 and mode 1; off forgets who is inside",
          "[trigger_spheres]") {
    // Expected: scripting.md#triggers, confirmed (code): the defaults of 0x00414480 and the disarm of 0x004144d0
    TriggerSpheres spheres;
    spheres.setLocate(locate);
    // A new sphere of radius 0 accepts nobody.
    REQUIRE(spheres.arm(7, true));
    REQUIRE(spheres.find(7) != nullptr);
    CHECK(spheres.find(7)->radius == 0.0F);
    CHECK(spheres.find(7)->mode == 1);
    CHECK(spheres.find(7)->stayPeriodMs == 1000);
    std::vector<BoxSubject> humans{{.handle = 100, .position = {1, 0, 0}, .alive = true, .player = false}};
    CHECK(checkAll(spheres, humans, 0).empty());
    // Configured, a human inside enters; disarmed, he is forgotten without message 4 and the sphere stays.
    REQUIRE(spheres.configure(7, true, 4.0F, 0, 500));
    CHECK(checkAll(spheres, humans, 100) == Sent{{7, VolumeBoxes::kEntered, 100}});
    REQUIRE(spheres.arm(7, false));
    CHECK(checkAll(spheres, humans, 200).empty());
    REQUIRE(spheres.find(7) != nullptr);
    CHECK(spheres.find(7)->occupants.empty());
    CHECK(spheres.find(7)->radius == 4.0F);
    // Armed again, he gets a fresh message 3.
    REQUIRE(spheres.arm(7, true));
    CHECK(checkAll(spheres, humans, 300) == Sent{{7, VolumeBoxes::kEntered, 100}});
    // Off for an object with no sphere does nothing.
    CHECK(spheres.arm(8, false));
    CHECK(spheres.find(8) == nullptr);
}
