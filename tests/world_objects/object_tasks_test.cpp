// SPDX-License-Identifier: GPL-3.0-or-later
// The world objects' step (world_objects/object_tasks.h): streaming round the camera, the objective marker's turn,
// fade, column colour and destroy, the drawing rules and a held object's pose. Synthetic records and types.
#include "world_objects/object_tasks.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_clip.h"
#include "animation/anim_math.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::world_objects::ObjectDraw;
using coney::world_objects::ObjectiveMarker;
using coney::world_objects::ObjectTasks;
using coney::world_objects::SpawnRecord;

namespace {

// A level with a gold marker (handle 1) and a crate (handle 2) near the origin, and a crate 100 m away (handle 3).
struct Level {
    coney::world_objects::SpawnRecords records;
    coney::world_objects::ObjectTypes types;
    ObjectTasks tasks;

    Level() {
        static_cast<void>(types.add("dyn_w_mission", "dyn_objective", 0));
        static_cast<void>(types.add("dyn_s_crate", "simple_object", 0));
        static_cast<void>(records.add(SpawnRecord{.handle = 1, .typeName = "dyn_w_mission", .position = {5, 0, 0.3F}}));
        static_cast<void>(records.add(SpawnRecord{.handle = 2, .typeName = "dyn_s_crate", .position = {0, 5, 0}}));
        static_cast<void>(records.add(SpawnRecord{.handle = 3, .typeName = "dyn_s_crate", .position = {100, 0, 0}}));
    }

    // One step with the camera at the origin and a 60 m draw distance.
    void step() { tasks.step(records, types, ObjectTasks::StepView{.camera = Vec3{}, .drawDistance = 60.0F}); }

    // This step's draw of `handle` (the column when `column`); null when it is not drawn.
    [[nodiscard]] const ObjectDraw* draw(double handle, bool column = false) const {
        for (const ObjectDraw& d : tasks.draws()) {
            if (d.handle == handle && d.column == column) {
                return &d;
            }
        }
        return nullptr;
    }
};

} // namespace

TEST_CASE("the column's colour follows the disc's type", "[object_tasks]") {
    using coney::world_objects::columnColourFor;
    CHECK(columnColourFor(0x27af4fe0U) == 0xC1A04700U); // dyn_w_mission: gold
    CHECK(columnColourFor(0xa83a74daU) == 0x99121300U); // dyn_w_goto: red
    CHECK(columnColourFor(0x14dc9db8U) == 0x5F447000U); // dyn_w_bonus: purple
    CHECK(columnColourFor(0x12345678U) == 0xFFFFFF00U); // any other: white
}

TEST_CASE("a marker turns 90 degrees a second and fades by 8 an update, drawn one update late", "[object_tasks]") {
    ObjectiveMarker marker;
    CHECK(marker.alpha() == 0);
    marker.setShown(true);
    for (int i = 0; i < 30; ++i) {
        CHECK_FALSE(marker.update());
    }
    // A second of updates: a quarter turn, alpha 240, the drawn alpha the update before's.
    CHECK(marker.angle() == Approx(std::numbers::pi_v<float> / 2.0F).margin(1e-4));
    CHECK(marker.alpha() == 240);
    CHECK(marker.drawnAlpha() == 232);
    for (int i = 0; i < 2; ++i) {
        static_cast<void>(marker.update());
    }
    CHECK(marker.alpha() == 255); // 32 updates, about 1.07 s
    // Dying: it fades out and the update reports it gone at alpha 0.
    marker.setDying();
    int updates = 0;
    while (!marker.update()) {
        ++updates;
    }
    CHECK(updates == 31);
    CHECK(marker.alpha() == 0);
}

TEST_CASE("the step brings in what is within 70 m and draws a marker's disc and column", "[object_tasks]") {
    Level level;
    level.step();
    CHECK(level.tasks.count() == 2); // the far crate stays out
    REQUIRE(level.draw(1) != nullptr);
    REQUIRE(level.draw(1, true) != nullptr);
    CHECK(level.draw(3) == nullptr);
    // The disc is exempt from the size cull; the column is coloured gold, both hidden (alpha 0) until ObjShow.
    CHECK(level.draw(1)->sizeCullExempt);
    CHECK(level.draw(1, true)->column);
    CHECK(level.draw(1, true)->tint == 0xC1A04700U);
    CHECK(level.draw(1, true)->modelHash == coney::world_objects::kColumnModelHash);

    level.records.find(1)->shownMessage = true;
    for (int i = 0; i < 40; ++i) {
        level.step();
    }
    CHECK((level.draw(1)->tint & 0xFFU) == 255);
    CHECK(level.draw(1, true)->tint == 0xC1A047FFU);
    // The column turns back as the disc turns, so it keeps the record's rotation.
    const coney::anim::Quat column = level.draw(1, true)->rotation;
    CHECK(column.w == Approx(1.0F).margin(1e-4));
    CHECK(level.draw(1)->rotation.w < 0.99F);
}

TEST_CASE("a plain object fades in over its first second and ObjHide hides it", "[object_tasks]") {
    Level level;
    level.step();
    CHECK((level.draw(2)->tint & 0xFFU) < 20);
    for (int i = 0; i < 30; ++i) {
        level.step();
    }
    CHECK((level.draw(2)->tint & 0xFFU) == 255);
    level.records.find(2)->hidden = true;
    level.step();
    CHECK(level.draw(2) == nullptr);
}

TEST_CASE("a dying marker fades out and its record is removed; ObjDestroy removes it at once", "[object_tasks]") {
    Level level;
    level.records.find(1)->shownMessage = true;
    for (int i = 0; i < 40; ++i) {
        level.step();
    }
    level.records.find(1)->dying = true;
    for (int i = 0; i < 31; ++i) {
        level.step();
        CHECK_FALSE(level.records.find(1)->removed);
    }
    level.step();
    CHECK(level.records.find(1)->removed);
    CHECK(level.draw(1) == nullptr);
    // Destroyed at once: gone from the next step's draws.
    static_cast<void>(level.records.destroy(2));
    level.step();
    CHECK(level.draw(2) == nullptr);
}

TEST_CASE("objects in a hand are left to their holder", "[object_tasks]") {
    Level level;
    level.tasks.step(
        level.records, level.types,
        ObjectTasks::StepView{.camera = Vec3{}, .drawDistance = 60.0F, .elapsedMs = 33, .inHand = [](double handle) {
                                  return handle == 2.0;
                              }});
    CHECK(level.draw(2) == nullptr);
    CHECK(level.draw(1) != nullptr);
}

TEST_CASE("the size cull, the ObjShow distance and the alpha floor", "[object_tasks]") {
    using coney::world_objects::showDistanceFade;
    using coney::world_objects::sizeFade;
    // Radius 1 m: 0.0005 at 2,000 m², 0.0004 at 2,500 m².
    CHECK(sizeFade(1.0F, 1000.0F) == 1.0F);
    CHECK(sizeFade(1.0F, 3000.0F) == 0.0F);
    CHECK(sizeFade(1.0F, 1.0F / 0.00045F) == Approx(0.5F).margin(1e-3));
    CHECK(showDistanceFade(0.0F, 500.0F) == 1.0F);
    CHECK(showDistanceFade(20.0F, 17.0F) == 1.0F);
    CHECK(showDistanceFade(20.0F, 19.0F) == Approx(0.5F));
    CHECK(showDistanceFade(20.0F, 25.0F) == 0.0F);
    CHECK(coney::world_objects::scaleTintAlpha(0xC1A047FFU, 0.5F) == 0xC1A04780U);
    CHECK(coney::world_objects::kMinDrawnAlpha == 10);
}

TEST_CASE("a held object hangs from its bone at the take event's offset, slid to its grip", "[object_tasks]") {
    // A take event on bone 25 at (0.1, 0, 0), no turn; a grip of 0.39 m along the object's y.
    coney::anim::AnimClip clip;
    clip.events.push_back(coney::anim::ClipEvent{
        .frame = 7, .type = 9, .value = 0, .word = 25, .position = Vec3{0.1F, 0, 0}, .rotation = coney::anim::Quat{}});
    const std::optional<coney::world_objects::HeldAttachment> held =
        coney::world_objects::heldAttachment(clip, 0.5F, 0.39F);
    REQUIRE(held.has_value());
    CHECK(held->bone == 25);
    CHECK(held->position.x == Approx(0.05F));
    CHECK(held->position.y == Approx(0.39F));
    // The bone 1 m up in the character's space, the human at (10, 0, 0) turned a quarter turn: the object's local
    // (0.05, 0.39, 0) turns to (-0.39, 0.05, 0).
    coney::anim::Mat34 bone;
    bone.t = Vec3{0, 0, 1};
    const coney::world_objects::WorldPose pose =
        coney::world_objects::heldWorldPose(Vec3{10, 0, 0}, std::numbers::pi_v<float> / 2.0F, bone, 1.0F, *held);
    CHECK(pose.position.x == Approx(10.0F - 0.39F).margin(1e-4));
    CHECK(pose.position.y == Approx(0.05F).margin(1e-4));
    CHECK(pose.position.z == Approx(1.0F).margin(1e-4));
    CHECK_FALSE(coney::world_objects::heldAttachment(coney::anim::AnimClip{}, 1.0F, 0.0F).has_value());
}
