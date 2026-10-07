// SPDX-License-Identifier: GPL-3.0-or-later
// The volume boxes (docs/research/scripting.md#triggers): the inside test with and without a turn, and the trigger
// update's messages 3 (entered), 5 (still inside, once a period) and 4 (left or died), and an occupant whose handle
// no longer resolves skipped. Synthetic boxes and humans.
#include "world_objects/volume_boxes.h"

#include <array>
#include <cstdint>
#include <tuple>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using coney::world_objects::BoxSubject;
using coney::world_objects::VolumeBox;
using coney::world_objects::VolumeBoxes;

namespace {

// The messages a box update sent: (box, message, human).
using Sent = std::vector<std::tuple<double, int, double>>;

// One update of `boxes` over `subjects` at `nowMs`; returns what it sent.
Sent step(VolumeBoxes& boxes, const std::vector<BoxSubject>& subjects, std::uint64_t nowMs) {
    Sent sent;
    boxes.update(subjects, nowMs,
                 [&](double box, int message, double human) { sent.emplace_back(box, message, human); });
    return sent;
}

} // namespace

TEST_CASE("a box holds the points between its corners, turned about its centre by its matrix", "[volume_boxes]") {
    VolumeBoxes boxes;
    const VolumeBox& box = boxes.add(7, "vLong", 0, {0, 0, 0}, {10, 2, 3}, true);
    CHECK(box.handle == 7);
    CHECK(VolumeBoxes::inside(box, {9, 1, 1}));
    CHECK_FALSE(VolumeBoxes::inside(box, {5, 4, 1}));
    CHECK_FALSE(VolumeBoxes::inside(box, {5, 1, 3.5F})); // above it
    CHECK_FALSE(VolumeBoxes::inside(box, {5, 1, -0.5F}));

    // A quarter turn about the centre (5, 1): the long side now runs along y.
    boxes.rotate(7, {0, -1, 1, 0});
    const VolumeBox& turned = *boxes.find(7);
    CHECK_FALSE(VolumeBoxes::inside(turned, {9, 1, 1}));
    CHECK(VolumeBoxes::inside(turned, {5, 5, 1}));
    CHECK(VolumeBoxes::inside(turned, {5.5F, -3, 1}));

    // A negative size still makes a box; an unknown handle is not found.
    const VolumeBox& back = boxes.add(8, "vBack", 0, {0, 0, 0}, {-2, -2, 2}, true);
    CHECK(VolumeBoxes::inside(back, {-1, -1, 1}));
    CHECK(boxes.find(9) == nullptr);
}

TEST_CASE("a trigger box sends 3 on entry, 5 once a period while inside and 4 on leaving or dying", "[volume_boxes]") {
    VolumeBoxes boxes;
    boxes.add(40, "vMark", 0, {0, 0, 0}, {2, 2, 3}, true);
    std::vector<BoxSubject> humans{{.handle = 100, .position = {5, 5, 0}, .alive = true},
                                   {.handle = 101, .position = {1, 1, 1}, .alive = true}};

    CHECK(step(boxes, humans, 0) == Sent{{40, VolumeBoxes::kEntered, 101}});
    CHECK(step(boxes, humans, 500) == Sent{{40, VolumeBoxes::kInside, 101}});
    CHECK(step(boxes, humans, 900).empty()); // within the period
    CHECK(step(boxes, humans, 1500) == Sent{{40, VolumeBoxes::kInside, 101}});

    humans[0].position = {1.5F, 1.5F, 0.5F};
    CHECK(step(boxes, humans, 1600) == Sent{{40, VolumeBoxes::kEntered, 100}});

    humans[1].position = {3, 1, 1};
    CHECK(step(boxes, humans, 1700) == Sent{{40, VolumeBoxes::kLeft, 101}});

    humans[0].alive = false;
    CHECK(step(boxes, humans, 1800) == Sent{{40, VolumeBoxes::kLeft, 100}});
    CHECK(boxes.find(40)->occupants.empty());

    // A dead human does not enter; a gone one leaves.
    CHECK(step(boxes, humans, 1900).empty());
    humans[1].position = {1, 1, 1};
    CHECK(step(boxes, humans, 2000) == Sent{{40, VolumeBoxes::kEntered, 101}});
    CHECK(step(boxes, {}, 2100) == Sent{{40, VolumeBoxes::kLeft, 101}});
}

TEST_CASE("a disabled box or one of another kind sends nothing", "[volume_boxes]") {
    VolumeBoxes boxes;
    boxes.add(1, "vOff", 0, {0, 0, 0}, {2, 2, 3}, false);
    boxes.add(2, "vPlayer", 2, {0, 0, 0}, {2, 2, 3}, true);
    const std::vector<BoxSubject> humans{{.handle = 100, .position = {1, 1, 1}, .alive = true}};
    CHECK(step(boxes, humans, 0).empty());
    boxes.clear();
    CHECK(boxes.all().empty());
}

TEST_CASE("an occupant gone from the subjects leaves while its handle resolves, and is skipped and kept once not",
          "[volume_boxes]") {
    VolumeBoxes boxes;
    boxes.add(40, "vTopTier", 0, {0, 0, 0}, {2, 2, 3}, true);
    bool resolves = true;
    boxes.setResolves([&resolves](double /*handle*/) { return resolves; });
    const std::vector<BoxSubject> inside{{.handle = 101, .position = {1, 1, 1}, .alive = true}};
    CHECK(step(boxes, inside, 0) == Sent{{40, VolumeBoxes::kEntered, 101}});

    // Deleted but still resolving: it leaves as anyone does.
    CHECK(step(boxes, {}, 100) == Sent{{40, VolumeBoxes::kLeft, 101}});
    CHECK(boxes.find(40)->occupants.empty());

    // No longer resolving: no message, and its entry stays.
    CHECK(step(boxes, inside, 200) == Sent{{40, VolumeBoxes::kEntered, 101}});
    resolves = false;
    CHECK(step(boxes, {}, 300).empty());
    CHECK(boxes.find(40)->occupants.size() == 1);
}

TEST_CASE("damage done from inside reaches every enabled kind-0 box the human stands in", "[volume_boxes]") {
    // docs/research/scripting.md#triggers: VolumeBoxes_SendDamageMessage tests the human's position, not the object's.
    VolumeBoxes boxes;
    boxes.add(57, "vLevelVandal", 0, {-10, -10, -1}, {20, 20, 30}, true);
    boxes.add(59, "vTier2Turf", 3, {0, 0, -1}, {5, 5, 10}, true);
    boxes.add(60, "vOff", 0, {0, 0, -1}, {5, 5, 10}, false);
    boxes.add(61, "vElsewhere", 0, {50, 50, -1}, {5, 5, 10}, true);
    std::vector<double> heard;
    boxes.sendDamage({1, 1, 0}, [&heard](double box) { heard.push_back(box); });
    CHECK(heard == std::vector<double>{57}); // not the turf box (kind 3)
    // Below the boxes' floor: none.
    heard.clear();
    boxes.sendDamage({1, 1, -5}, [&heard](double box) { heard.push_back(box); });
    CHECK(heard.empty());
}
