// SPDX-License-Identifier: GPL-3.0-or-later
// The volume boxes (docs/research/scripting.md#triggers): the inside test with and without a turn, each box updated on
// one frame in five, and the trigger update's messages 3 (entered), 5 (still inside, once a period) and 4 (left or
// died), and an occupant whose handle no longer resolves skipped. Synthetic boxes and humans.
#include "world_objects/volume_boxes.h"

#include <algorithm>
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

// One frame of `boxes` over `subjects` at `nowMs`; returns what it sent.
Sent frame(VolumeBoxes& boxes, const std::vector<BoxSubject>& subjects, std::uint64_t nowMs) {
    Sent sent;
    boxes.update(subjects, nowMs,
                 [&](double box, int message, double human) { sent.emplace_back(box, message, human); });
    return sent;
}

// Five frames at `nowMs`, in which every box's turn comes once (one update of each box); returns everything sent.
Sent step(VolumeBoxes& boxes, const std::vector<BoxSubject>& subjects, std::uint64_t nowMs) {
    Sent all;
    for (std::uint32_t i = 0; i < VolumeBoxes::kFramesPerUpdate; ++i) {
        const Sent sent = frame(boxes, subjects, nowMs);
        all.insert(all.end(), sent.begin(), sent.end());
    }
    return all;
}

} // namespace

TEST_CASE("a box holds the points between its corners, turned about its centre by its matrix", "[volume_boxes]") {
    // Expected: scripting.md#triggers, confirmed (code): the inside test 0x00412a18 and RotateVolumeBox's matrix
    VolumeBoxes boxes;
    const VolumeBox& box = *boxes.add(7, "vLong", 0, {0, 0, 0}, {10, 2, 3}, true);
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
    const VolumeBox& back = *boxes.add(8, "vBack", 0, {0, 0, 0}, {-2, -2, 2}, true);
    CHECK(VolumeBoxes::inside(back, {-1, -1, 1}));
    CHECK(boxes.find(9) == nullptr);
}

TEST_CASE("each box updates on one frame in five, its turn set by the order it was added", "[volume_boxes]") {
    // Expected: trigger-updates.md#schedule, confirmed (code): VolumeBoxes_UpdateAll 0x00412ca0 updates the box at
    // handle index i (the first free from 0x26c, so the order added) when i % 5 equals the frame counter 0x00514804,
    // which starts at 0 and steps 0-4 once a frame.
    VolumeBoxes boxes;
    boxes.add(40, "vFirst", 0, {0, 0, 0}, {2, 2, 3}, true);
    boxes.add(41, "vSecond", 0, {0, 0, 0}, {2, 2, 3}, true);
    boxes.add(42, "vThird", 0, {0, 0, 0}, {2, 2, 3}, true);
    std::vector<BoxSubject> humans{{.handle = 100, .position = {1, 1, 1}, .alive = true, .player = false},
                                   {.handle = 101, .position = {5, 5, 0}, .alive = true, .player = false}};

    CHECK(frame(boxes, humans, 0) == Sent{{40, VolumeBoxes::kEntered, 100}});
    CHECK(frame(boxes, humans, 0) == Sent{{41, VolumeBoxes::kEntered, 100}});
    CHECK(frame(boxes, humans, 0) == Sent{{42, VolumeBoxes::kEntered, 100}});
    CHECK(frame(boxes, humans, 0).empty());
    CHECK(frame(boxes, humans, 0).empty());

    // A human who comes into the first box just after its turn is heard on its next turn, five frames on.
    humans[1].position = {1.5F, 1.5F, 0.5F};
    CHECK(frame(boxes, humans, 0) == Sent{{40, VolumeBoxes::kEntered, 101}}); // frame 6: the first box's turn again
    humans.push_back({.handle = 102, .position = {1, 1, 2}, .alive = true, .player = false});
    for (int i = 0; i < 4; ++i) {
        const Sent sent = frame(boxes, humans, 0); // frames 7-10: the other two boxes hear 101 and 102
        CHECK(std::ranges::none_of(sent, [](const auto& message) { return std::get<0>(message) == 40; }));
    }
    CHECK(frame(boxes, humans, 0) == Sent{{40, VolumeBoxes::kEntered, 102}}); // frame 11
}

TEST_CASE("a box past the 128th is never updated", "[volume_boxes]") {
    // Expected: trigger-updates.md#schedule, confirmed (code): VolumeBoxes_UpdateAll 0x00412ca0 walks handle indices
    // 0x26c-0x2eb only, so the 129th box (index 0x2ec) is never updated.
    VolumeBoxes boxes;
    for (int i = 0; i < 129; ++i) {
        boxes.add(1000 + i, "vMany", 0, {0, 0, 0}, {2, 2, 3}, true);
    }
    const std::vector<BoxSubject> inside{{.handle = 100, .position = {1, 1, 1}, .alive = true, .player = false}};
    const Sent sent = step(boxes, inside, 0);
    CHECK(sent.size() == 128);
    CHECK(std::ranges::find(sent, std::tuple<double, int, double>{1127, VolumeBoxes::kEntered, 100}) != sent.end());
    CHECK(std::ranges::find(sent, std::tuple<double, int, double>{1128, VolumeBoxes::kEntered, 100}) == sent.end());
}

TEST_CASE("a trigger box sends 3 on entry, 5 once a period while inside and 4 on leaving or dying", "[volume_boxes]") {
    // Expected: trigger-updates.md#stay-timer, confirmed (code): VolumeBox_Update 0x00415378 sends 5 only once the time
    // is strictly past the stay timer, which it re-arms to now + period on each due update.
    VolumeBoxes boxes;
    boxes.add(40, "vMark", 0, {0, 0, 0}, {2, 2, 3}, true);
    std::vector<BoxSubject> humans{{.handle = 100, .position = {5, 5, 0}, .alive = true, .player = false},
                                   {.handle = 101, .position = {1, 1, 1}, .alive = true, .player = false}};

    CHECK(step(boxes, humans, 0) == Sent{{40, VolumeBoxes::kEntered, 101}});
    CHECK(step(boxes, humans, 500) == Sent{{40, VolumeBoxes::kInside, 101}});
    CHECK(step(boxes, humans, 900).empty());  // within the period
    CHECK(step(boxes, humans, 1500).empty()); // due only once strictly past the timer
    CHECK(step(boxes, humans, 1550) == Sent{{40, VolumeBoxes::kInside, 101}});

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

TEST_CASE("a box's stay timer re-arms when due with nobody inside, so a newcomer's 5 comes on the box's beat",
          "[volume_boxes]") {
    // Expected: trigger-updates.md#stay-timer, confirmed (code): VolumeBox_Update 0x00415378 re-arms +0x1e0 on every
    // due update, whether or not anyone is inside.
    VolumeBoxes boxes;
    boxes.add(40, "vMark", 0, {0, 0, 0}, {2, 2, 3}, true);
    const std::vector<BoxSubject> inside{{.handle = 101, .position = {1, 1, 1}, .alive = true, .player = false}};
    CHECK(step(boxes, {}, 500).empty()); // due: the timer goes to 1500
    CHECK(step(boxes, inside, 600) == Sent{{40, VolumeBoxes::kEntered, 101}});
    CHECK(step(boxes, inside, 1200).empty());
    CHECK(step(boxes, inside, 1501) == Sent{{40, VolumeBoxes::kInside, 101}});
}

TEST_CASE("a box's message-5 period is its handler component's", "[volume_boxes]") {
    // Expected: trigger-updates.md#stay-timer, confirmed (code): VolumeBox_Update 0x00415378 reads the period through
    // the handler component's getter (vtable 0x00544b98 slot +0x4c, +0x74), not a fixed 1000 ms.
    VolumeBoxes boxes;
    boxes.add(40, "vMark", 0, {0, 0, 0}, {2, 2, 3}, true);
    boxes.setStayPeriod([](double box) -> std::uint64_t { return box == 40 ? 250 : 1000; });
    const std::vector<BoxSubject> inside{{.handle = 101, .position = {1, 1, 1}, .alive = true, .player = false}};
    CHECK(step(boxes, inside, 0) == Sent{{40, VolumeBoxes::kEntered, 101}});
    CHECK(step(boxes, inside, 100) == Sent{{40, VolumeBoxes::kInside, 101}}); // the timer goes to 350
    CHECK(step(boxes, inside, 300).empty());
    CHECK(step(boxes, inside, 351) == Sent{{40, VolumeBoxes::kInside, 101}});
}

TEST_CASE("a handler that disables its box stops the box's update", "[volume_boxes]") {
    // Expected: trigger-updates.md#box, confirmed (code): VolumeBox_Update 0x00415378 sends each message at once and
    // tests the box's enable byte before the next human.
    VolumeBoxes boxes;
    boxes.add(40, "vMark", 0, {0, 0, 0}, {2, 2, 3}, true);
    const std::vector<BoxSubject> inside{{.handle = 101, .position = {1, 1, 1}, .alive = true, .player = false},
                                         {.handle = 102, .position = {1, 1, 2}, .alive = true, .player = false}};
    Sent sent;
    boxes.update(inside, 0, [&](double box, int message, double human) {
        sent.emplace_back(box, message, human);
        VolumeBox* disabled = boxes.find(box);
        disabled->enabled = false;
        disabled->occupants.clear();
    });
    CHECK(sent == Sent{{40, VolumeBoxes::kEntered, 101}});
}

TEST_CASE("a players' box re-arms its stay timer when due with nobody inside", "[volume_boxes]") {
    // Expected: trigger-updates.md#player-box, confirmed (code): PlayerBox_Update 0x004134f8 re-arms +0xf8 on every due
    // update, whether or not a player is inside.
    VolumeBoxes boxes;
    boxes.add(2, "vbStartRail", 2, {0, 0, 0}, {2, 2, 3}, true);
    const std::vector<BoxSubject> player{{.handle = 1, .position = {1, 1, 1}, .alive = true, .player = true}};
    CHECK(step(boxes, {}, 500).empty()); // due: the timer goes to 1500
    CHECK(step(boxes, player, 600) == Sent{{2, VolumeBoxes::kEntered, 1}});
    CHECK(step(boxes, player, 1200).empty());
    CHECK(step(boxes, player, 1501) == Sent{{2, VolumeBoxes::kInside, 1}});
}

TEST_CASE("AddVolumeBox's kind 1 and a kind outside 0, 2 and 3 make no box", "[volume_boxes]") {
    // Expected: trigger-updates.md#add, confirmed (code): VolumeBox_Add 0x004125b8 returns NilHandle for kind 1 and has
    // no pool for a kind but 0, 2 and 3 (the original then uses a null box; Coney's choice is to make none).
    VolumeBoxes boxes;
    CHECK(boxes.add(5, "vNo", 1, {0, 0, 0}, {1, 1, 1}, true) == nullptr);
    CHECK(boxes.add(6, "vOdd", 4, {0, 0, 0}, {1, 1, 1}, true) == nullptr);
    CHECK(boxes.all().empty());
    CHECK(boxes.add(7, "vTurf", 3, {0, 0, 0}, {1, 1, 1}, true) != nullptr);
}

TEST_CASE("a disabled box or a turf box sends nothing; a players' box only for a player", "[volume_boxes]") {
    // Expected: trigger-updates.md#player-box, confirmed (code): only enabled kind-0 and kind-2 boxes update, kind 2
    // over the players
    VolumeBoxes boxes;
    boxes.add(1, "vOff", 0, {0, 0, 0}, {2, 2, 3}, false);
    boxes.add(2, "vbStartRail", 2, {0, 0, 0}, {2, 2, 3}, true);
    boxes.add(3, "vTurf", 3, {0, 0, 0}, {2, 2, 3}, true);
    std::vector<BoxSubject> humans{{.handle = 100, .position = {1, 1, 1}, .alive = true, .player = false}};
    CHECK(step(boxes, humans, 0).empty());
    humans.push_back({.handle = 1, .position = {1, 1, 1}, .alive = true, .player = true});
    CHECK(step(boxes, humans, 100) == Sent{{2, VolumeBoxes::kEntered, 1}});
    boxes.clear();
    CHECK(boxes.all().empty());
}

TEST_CASE("an occupant gone from the subjects leaves while its handle resolves, and is skipped and kept once not",
          "[volume_boxes]") {
    // Expected: trigger-updates.md#box, confirmed (code): an occupant that no longer resolves is skipped and kept
    VolumeBoxes boxes;
    boxes.add(40, "vTopTier", 0, {0, 0, 0}, {2, 2, 3}, true);
    bool resolves = true;
    boxes.setResolves([&resolves](double /*handle*/) { return resolves; });
    const std::vector<BoxSubject> inside{{.handle = 101, .position = {1, 1, 1}, .alive = true, .player = false}};
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
    // Expected: scripting.md#triggers, confirmed (code): message 6 tests the human's position (0x00413018)
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

TEST_CASE("a theft reaches every enabled kind-0 box round the object's spot", "[volume_boxes][steal]") {
    // Expected: crimes.md#owned-items, inferred from Item_TakeOwned's inside test (which boxes are asked is Coney's
    // reading)
    // docs/research/crimes.md#owned-items: Item_TakeOwned tells the boxes whose inside test holds for the spot.
    VolumeBoxes boxes;
    boxes.add(70, "vStealTrigger", 0, {-249.4F, 217.9F, 1.1F}, {12.4F, 10.2F, 3.5F}, true);
    boxes.add(71, "vTurf", 3, {-249.4F, 217.9F, 1.1F}, {12.4F, 10.2F, 3.5F}, true);
    boxes.add(72, "vOff", 0, {-249.4F, 217.9F, 1.1F}, {12.4F, 10.2F, 3.5F}, false);
    std::vector<double> heard;
    boxes.sendTheft({-240.0F, 218.6F, 2.5F}, [&heard](double box) { heard.push_back(box); });
    CHECK(heard == std::vector<double>{70});
    heard.clear();
    boxes.sendTheft({-230.0F, 218.6F, 2.5F}, [&heard](double box) { heard.push_back(box); });
    CHECK(heard.empty());
}
