// SPDX-License-Identifier: GPL-3.0-or-later
// The scene records and the scene list decode as docs/research/scenes.md#data lays them out, on synthetic records.

#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "scenes/scene_list.h"
#include "scenes/scene_record.h"
#include "support/scene_fixtures.h"

using Catch::Approx;
using coney::test::Bytes;
namespace scenes = coney::scenes;

using coney::test::segmentPart;
using coney::test::twoPartSpec;

TEST_CASE("a header record decodes its definitions and tracks", "[scenes]") {
    const Bytes record = coney::test::sceneHeaderRecord(twoPartSpec());
    REQUIRE(scenes::isSceneHeader(record.span()));
    auto header = scenes::parseSceneHeader(record.span());
    REQUIRE(header.has_value());
    CHECK(header->name == "tst_c1");
    CHECK(header->firstSegment == "aa");
    CHECK(header->hasSegments());
    CHECK(header->label == "tst_c1");
    CHECK(header->frames == 45);
    REQUIRE(header->roles.size() == 2);
    CHECK(header->roles[0].name == "warrtest");
    CHECK(header->roles[0].start.position == coney::anim::Vec3{10.0F, 20.0F, 0.0F});
    CHECK(scenes::headingOf(header->roles[0].start.rotation) == Approx(0.5F));
    CHECK(header->roles[0].end.position == coney::anim::Vec3{12.0F, 21.0F, 0.0F});
    CHECK(scenes::headingOf(header->roles[0].end.rotation) == Approx(1.0F));
    REQUIRE(header->objects.size() == 1);
    REQUIRE(header->camera.has_value());
    if (const std::optional<scenes::SceneCameraDef>& camera = header->camera) {
        CHECK(camera->nearClip == 0.5F);
        CHECK(camera->farClip == 75.0F);
        CHECK(camera->preloadRadius == 500.0F);
        CHECK(camera->fieldOfView == 60.0F);
    }
    REQUIRE(header->lights.size() == 1);
    CHECK(header->lights[0].kind == 1);
    CHECK(header->lights[0].colour == std::array{1.0F, 1.0F, 1.0F});
    CHECK(header->lights[0].coneDegrees == 40.0F);
    CHECK(header->lights[0].range == 0.0F);

    // The tracks: a clip per role with its events as stored, the keyed tracks with their keys and events.
    const scenes::SceneTracks& tracks = header->tracks;
    REQUIRE(tracks.clips.size() == 2);
    CHECK(tracks.clips[0].clip.duration == 1.0F);
    REQUIRE(tracks.clips[0].events.size() == 3);
    CHECK(tracks.clips[0].events[0].type == 21);
    CHECK(tracks.clips[0].events[0].f32At(8) == 10.0F);
    CHECK(tracks.clips[0].events[0].f32At(0xc) == 20.0F);
    CHECK(tracks.clips[0].events[1].f32At(8) == 0.5F);
    CHECK(tracks.clips[0].events[2].frame == 40);
    REQUIRE(tracks.objects.size() == 1);
    CHECK(tracks.objects[0].positions.size() == 2);
    CHECK(tracks.objects[0].events.size() == 1);
    REQUIRE(tracks.camera.has_value());
    if (const std::optional<scenes::KeyTrack>& camera = tracks.camera) {
        REQUIRE(camera->rotations.size() == 1);
        CHECK(camera->rotations[0].value.w == 1.0F);
        REQUIRE(camera->events.size() == 5);
        CHECK(camera->events[3].u32At(8) == 0x1234);
    }
    CHECK(tracks.lights.size() == 1);
    CHECK(tracks.duration() == 1.0F);
}

TEST_CASE("a segment record decodes and is named after its scene", "[scenes]") {
    const Bytes record = coney::test::sceneSegmentRecord("tst_c1aa", "", segmentPart());
    CHECK_FALSE(scenes::isSceneHeader(record.span()));
    auto segment = scenes::parseSceneSegment(record.span());
    REQUIRE(segment.has_value());
    CHECK(segment->name == "tst_c1aa");
    CHECK(segment->nextSegment.empty());
    CHECK(segment->tracks.clips.size() == 2);
    CHECK(segment->tracks.duration() == 0.5F);
    CHECK(scenes::segmentName("tst_c1", "aa") == "tst_c1aa");
    CHECK(scenes::segmentName("a_very_long_scene_name", "abcd") == "a_very_long_sceabc");
}

TEST_CASE("a keyed track interpolates between its keys and holds the ends", "[scenes]") {
    scenes::KeyTrack track;
    track.positions = {{10, {0.0F, 0.0F, 0.0F}}, {20, {10.0F, 0.0F, 0.0F}}};
    const float s = std::sqrt(0.5F);
    track.rotations = {{0, {0.0F, 0.0F, 0.0F, 1.0F}}, {10, {0.0F, 0.0F, s, s}}};
    CHECK(track.positionAt(0.0F).x == 0.0F);
    CHECK(track.positionAt(15.0F).x == Approx(5.0F));
    CHECK(track.positionAt(25.0F).x == 10.0F);
    CHECK(scenes::headingOf(track.rotationAt(5.0F)) == Approx(std::numbers::pi_v<float> / 4.0F));
    CHECK(track.rotationAt(50.0F).z == Approx(s));
    CHECK(scenes::KeyTrack{}.positionAt(3.0F) == coney::anim::Vec3{});
}

TEST_CASE("damaged scene records fail to decode", "[scenes]") {
    Bytes record = coney::test::sceneHeaderRecord(twoPartSpec());
    // A size word that is not the record's size.
    Bytes wrongSize = record;
    wrongSize.patchU32(0, 12);
    CHECK(scenes::parseSceneHeader(wrongSize.span()).error().code == coney::ErrorCode::Invalid);
    // A table offset past the end.
    Bytes badTable = record;
    badTable.patchU32(0xa4, 0x7fffff00);
    CHECK(scenes::parseSceneHeader(badTable.span()).error().code == coney::ErrorCode::Truncated);
    // Too short for the fixed part.
    const std::vector<std::byte> stub(32);
    CHECK_FALSE(scenes::parseSceneHeader(stub).has_value());
    CHECK_FALSE(scenes::parseSceneSegment(std::span(stub).first(8)).has_value());
}

TEST_CASE("the scene list finds ids by substring, exact name and id", "[scenes]") {
    const std::vector<std::string> names{"l11_c2", "l99_c1", "l99_c1aa", "l99_c5"};
    const Bytes chunk = coney::test::sceneListChunk(names);
    auto list = scenes::SceneList::parse(chunk.span());
    REQUIRE(list.has_value());
    CHECK(list->size() == 4);
    CHECK(list->findContaining("l99_c1") == 1U);
    CHECK(list->findContaining("c5") == 3U);
    CHECK_FALSE(list->findContaining("nothing").has_value());
    CHECK(list->findExact("l99_c1aa") == 2U);
    CHECK_FALSE(list->findExact("l99_c").has_value());
    REQUIRE(list->entry(3) != nullptr);
    CHECK(list->entry(3)->name == "l99_c5");
    CHECK(list->entry(9) == nullptr);
    Bytes truncated = chunk;
    truncated.patchU32(0, 99);
    CHECK_FALSE(scenes::SceneList::parse(truncated.span()).has_value());
}
