// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/reference_render.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/character_model.h"
#include "core/name_hash.h"
#include "support/character_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
namespace characters = coney::characters;

namespace {

// Where `p` lands in the view, as offsets across over depth: both must be within the half-view for it to be seen.
struct Projected {
    float across;
    float up;
    float depth;
};

// Projects `p` through `view` with no lens: its offsets along right and up over its distance along forward.
Projected project(const characters::ReferenceView& view, Vec3 p) {
    const Vec3 offset = coney::anim::subtract(p, view.position);
    const float depth = coney::anim::dot(offset, view.forward);
    return {coney::anim::dot(offset, view.right) / depth, coney::anim::dot(offset, view.up) / depth, depth};
}

} // namespace

TEST_CASE("the bind pose turns every vertex into the pose's axes", "[reference_render]") {
    auto model = characters::decodeCharacterModel(
        coney::test::characterClump(coney::test::testCharacterFields()).span(), coney::test::testBoneOffsets().span());
    REQUIRE(model.has_value());
    std::vector<Vec3> positions(model->vertices.size());
    std::vector<Vec3> normals(model->vertices.size());
    characters::bindPoseVertices(*model, positions, normals);
    // d at (1, 1, 0) in the model's axes is (1, 0, 1) in the pose's; the normal along z turns to -y.
    CHECK(positions[3].x == Approx(1.0F));
    CHECK(positions[3].y == Approx(0.0F).margin(1e-6));
    CHECK(positions[3].z == Approx(1.0F));
    CHECK(normals[0].y == Approx(-1.0F).epsilon(0.02)); // the fixture's normals are quantised
}

TEST_CASE("the reference camera frames every point from the three-quarter front", "[reference_render]") {
    // A box the size of a character: half a metre wide and deep, 1.8 m tall.
    std::vector<Vec3> points;
    for (const float x : {-0.25F, 0.25F}) {
        for (const float y : {-0.2F, 0.2F}) {
            for (const float z : {0.0F, 1.8F}) {
                points.push_back({x, y, z});
            }
        }
    }
    const characters::ReferenceView view = characters::frameReference(points);

    // In front (+y), to the character's right (+x), above, and looking down at it.
    CHECK(view.position.y > 0.0F);
    CHECK(view.position.x > 0.0F);
    CHECK(view.forward.z < 0.0F);
    CHECK(coney::anim::length(view.forward) == Approx(1.0F));
    CHECK(coney::anim::dot(view.forward, view.up) == Approx(0.0F).margin(1e-6));
    CHECK(view.up.z > 0.0F);

    // Every point inside the view and in front of the camera, and at least one at the margin's edge.
    const float limit = characters::kReferenceHalfView * (1.0F - 2.0F * characters::kReferenceMargin);
    float widest = 0.0F;
    for (const Vec3& p : points) {
        const Projected at = project(view, p);
        CHECK(at.depth > 0.0F);
        CHECK(std::abs(at.across) <= limit + 1e-5F);
        CHECK(std::abs(at.up) <= limit + 1e-5F);
        widest = std::max({widest, std::abs(at.across), std::abs(at.up)});
    }
    CHECK(widest == Approx(limit).epsilon(1e-3));

    // The same points give the same view, exactly.
    const characters::ReferenceView again = characters::frameReference(points);
    CHECK(again.position.x == view.position.x);
    CHECK(again.position.y == view.position.y);
    CHECK(again.position.z == view.position.z);
}

TEST_CASE("the reference camera without points stands in front of the origin", "[reference_render]") {
    const characters::ReferenceView view = characters::frameReference({});
    CHECK(view.position.y > 0.0F);
    CHECK(coney::anim::length(view.position) == Approx(3.0F));
}

TEST_CASE("downsampling weights colour by alpha, so a transparent background leaves no fringe", "[reference_render]") {
    // A 2 x 2 image reduced to one pixel: one opaque red, one half-transparent blue, two transparent white.
    const std::vector<std::uint8_t> image{
        255, 0,   0,   255, /**/ 0,   0,   255, 128, //
        255, 255, 255, 0,   /**/ 255, 255, 255, 0,
    };
    const std::vector<std::uint8_t> reduced = characters::downsampleRgba(image, 2, 2);
    REQUIRE(reduced.size() == 4);
    // Red 255*255 / 383 and blue 255*128 / 383, rounded; the white of the transparent pixels does not show.
    CHECK(reduced[0] == 170);
    CHECK(reduced[1] == 0);
    CHECK(reduced[2] == 85);
    CHECK(reduced[3] == 96); // 383 / 4, rounded

    // A fully transparent block stays transparent black.
    const std::vector<std::uint8_t> clear(16, 0);
    CHECK(characters::downsampleRgba(clear, 2, 2) == std::vector<std::uint8_t>(4, 0));

    // A factor of 1 leaves the image as it is.
    CHECK(characters::downsampleRgba(image, 2, 1) ==
          std::vector<std::uint8_t>{255, 0, 0, 255, 0, 0, 255, 128, 0, 0, 0, 0, 0, 0, 0, 0});
}

TEST_CASE("a reference image is named by the model name, or by the name hash", "[reference_render]") {
    CHECK(characters::referenceFileName(0x1234abcdU, "warr_re_cv") == "warr_re_cv.png");
    CHECK(characters::referenceFileName(0x0000abcdU, "") == "0000abcd.png");
}

TEST_CASE("an object's model is stood up and turned to face the reference camera", "[reference_render]") {
    // A model frame that lifts the model 2 m along its own up axis (y), then the turn into the pose's axes.
    coney::anim::Mat34 frame;
    frame.t = {0.0F, 2.0F, 0.0F};
    const coney::anim::Mat34 transform = characters::objectReferenceTransform(frame);
    // The model's up (y) is the pose's up (z), and its front (+z) points at the camera's side (+y).
    const Vec3 top = coney::anim::transformPoint(transform, {0.0F, 1.0F, 0.0F});
    CHECK(top.x == Approx(0.0F).margin(1e-6));
    CHECK(top.y == Approx(0.0F).margin(1e-6));
    CHECK(top.z == Approx(3.0F));
    const Vec3 front = coney::anim::transformDirection(transform, {0.0F, 0.0F, 1.0F});
    CHECK(front.y == Approx(1.0F));
    // A turn, not a mirror: the axes stay right-handed.
    const Vec3 handed = coney::anim::cross(characters::kObjectToPose.x, characters::kObjectToPose.y);
    CHECK(handed == characters::kObjectToPose.z);
    // Framed like a character: the camera stands on the front's side.
    const std::vector<Vec3> points{coney::anim::transformPoint(transform, {-0.3F, 0.0F, -0.2F}),
                                   coney::anim::transformPoint(transform, {0.3F, 1.0F, 0.2F})};
    CHECK(characters::frameReference(points).position.y > 0.0F);
}

TEST_CASE("reference lists go into the folders the references index names", "[reference_render]") {
    CHECK(characters::referenceFolder(characters::ReferenceList::Characters) == "characters");
    CHECK(characters::referenceFolder(characters::ReferenceList::Objects) == "objects");
    CHECK(characters::referenceFileName(coney::crc32("dyn_bat"), "dyn_bat") == "dyn_bat.png");
}

TEST_CASE("an --only request is a 0x name hash or a name hashed in lower case", "[reference_render]") {
    CHECK(characters::referenceRequestHash("0x1234abcd") == 0x1234abcdU);
    CHECK(characters::referenceRequestHash("0X00FF") == 0xffU);
    CHECK(characters::referenceRequestHash("DYN_Bat") == coney::crc32("dyn_bat"));
    // Not hex after the prefix, or nothing after it: the text is a name.
    CHECK(characters::referenceRequestHash("0xzz") == coney::crc32("0xzz"));
    CHECK(characters::referenceRequestHash("0x") == coney::crc32("0x"));
}

TEST_CASE("a name list skips blank lines and comments and trims each name", "[reference_render]") {
    CHECK(characters::parseNameList("warr_re_cv\r\n\n  # a comment\n\tdyn_bat  \r\n#x\ndyn_door") ==
          std::vector<std::string>{"warr_re_cv", "dyn_bat", "dyn_door"});
    CHECK(characters::parseNameList("").empty());
    CHECK(characters::parseNameList("\n \n").empty());
}
