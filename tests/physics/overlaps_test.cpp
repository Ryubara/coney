// SPDX-License-Identifier: GPL-3.0-or-later
// The static shape-pair overlaps (physics/overlaps.h) and their kernels (physics/segment_kernels.h): the cases of
// docs/research/physics.md's Unit 4 test ideas, one per quirk the original has, on synthetic shapes.
#include "physics/overlaps.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "physics/contact.h"
#include "physics/segment_kernels.h"
#include "physics/shapes.h"

using Catch::Approx;
using coney::anim::Vec3;
using namespace coney::physics;

namespace {

constexpr double kEps = 1e-4;
const Vec4 kJunk{7.0F, 7.0F, 7.0F, 7.0F};

// A contact filled with a marker, so the tests see which fields a test writes.
Contact marked() {
    Contact contact;
    contact.halves = {ContactHalf{kJunk, kJunk, 7, 7}, ContactHalf{kJunk, kJunk, 7, 7}};
    contact.fraction = 7.0F;
    contact.depth = 7.0F;
    contact.point = kJunk;
    contact.slide = kJunk;
    contact.flags = 7;
    return contact;
}

// Runs the table entry for (own, other) on a marked contact; a test that writes nothing must leave it unchanged.
bool overlaps(const Shape& own, const Shape& other) {
    Contact contact = marked();
    const bool hit = overlapShapes(contact, own, other);
    CHECK(contact == marked());
    return hit;
}

// Checks xyz and w of `v`.
void checkVec(const Vec4& v, float x, float y, float z, float w) {
    CHECK(v.x == Approx(x).margin(kEps));
    CHECK(v.y == Approx(y).margin(kEps));
    CHECK(v.z == Approx(z).margin(kEps));
    CHECK(v.w == Approx(w).margin(kEps));
}

// A capsule of the human's size (r 0.35, 2 m) at `base`.
Capsule human(Vec3 base) { return Capsule{.base = base, .radius = 0.35F, .height = 2.0F}; }

// A car-like box: centre (0, 0, 0.5), half (2, 1, 0.5), axes identity.
Box carBox() { return Box{.half = {2.0F, 1.0F, 0.5F}, .centre = {0.0F, 0.0F, 0.5F}}; }

// A cube of half 1 at `centre`.
Box cube(Vec3 centre) { return Box{.half = {1.0F, 1.0F, 1.0F}, .centre = centre}; }

} // namespace

TEST_CASE("Segment kernels clamp to the ends and keep the original's quirks", "[physics][overlap]") {
    CHECK(segmentClosestParam({}, {2.0F, 0.0F, 0.0F}, {1.0F, 5.0F, 0.0F}) == Approx(0.5F));
    CHECK(segmentClosestParam({}, {2.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}) == 0.0F);
    CHECK(segmentClosestParam({}, {2.0F, 0.0F, 0.0F}, {3.0F, 0.0F, 0.0F}) == 1.0F);
    CHECK(segmentClosestParam({}, {}, {3.0F, 0.0F, 0.0F}) == 0.0F);
    CHECK(segmentClosestPoint({1.0F, 1.0F, 1.0F}, {2.0F, 0.0F, 0.0F}, {2.0F, 5.0F, 1.0F}) == Vec3{2.0F, 1.0F, 1.0F});

    // Parallel segments: s = 0, t from the first one's start.
    const SegmentParams parallel = segmentClosestPoints({1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {-0.5F, -0.1F, 0.0F});
    CHECK(parallel.s == 0.5F);
    CHECK(parallel.t == 0.0F);
    // A zero-length second segment leaves s at 0.
    const SegmentParams point = segmentClosestPoints({1.0F, 0.0F, 0.0F}, {}, {-0.9F, -0.05F, 0.0F});
    CHECK(point.s == 0.0F);
    CHECK(point.t == 0.0F);

    float depth = 0.0F;
    CHECK(capsuleBoxCornerCircle(-2.2F, -1.2F, 0.35F, 2.0F, 1.0F, depth));
    CHECK(depth == Approx(0.0425F).margin(kEps));
    CHECK_FALSE(capsuleBoxCornerCircle(-2.3F, -1.3F, 0.35F, 2.0F, 1.0F, depth));
    CHECK(capsuleBoxSideInterval(-2.2F, 0.35F, 2.0F, depth));
    CHECK(depth == Approx(0.15F).margin(kEps));
    CHECK_FALSE(capsuleBoxSideInterval(-2.36F, 0.35F, 2.0F, depth));
}

TEST_CASE("Two spheres touching overlap and write nothing", "[physics][overlap]") {
    CHECK(overlaps(Sphere{{}, 0.5F}, Sphere{{1.0F, 0.0F, 0.0F}, 0.5F}));
    CHECK_FALSE(overlaps(Sphere{{}, 0.5F}, Sphere{{1.001F, 0.0F, 0.0F}, 0.5F}));
}

TEST_CASE("Capsules overlap as flat cylinders, strictly side by side", "[physics][overlap]") {
    Contact c = marked();
    REQUIRE(overlapShapes(c, human({}), human({0.5F, 0.0F, 1.0F})));
    checkVec(c.halves[0].normal, 1.0F, 0.0F, 0.0F, 1.0F);
    checkVec(c.halves[1].normal, -1.0F, 0.0F, 0.0F, 1.0F);
    CHECK(c.depth == Approx(0.2F).margin(kEps));
    CHECK(c.fraction == 7.0F);
    CHECK(c.point == kJunk);

    c = marked();
    REQUIRE(overlapShapes(c, human({}), human({0.0F, 0.0F, 1.5F})));
    checkVec(c.halves[0].normal, 0.0F, 1.0F, 0.0F, 1.0F);
    checkVec(c.halves[1].normal, 0.0F, -1.0F, 0.0F, 1.0F);
    CHECK(c.depth == Approx(0.7F).margin(kEps));

    c = marked();
    CHECK(overlapShapes(c, human({}), human({0.5F, 0.0F, 2.0F})));
    c = marked();
    CHECK_FALSE(overlapShapes(c, human({}), human({0.7F, 0.0F, 0.0F})));
    CHECK(c == marked());
    CHECK_FALSE(overlapShapes(c, human({}), human({0.5F, 0.0F, 2.01F})));
    CHECK(c == marked());
}

TEST_CASE("Segments overlap within the sum of the radii, a point segment measured from the start",
          "[physics][overlap]") {
    const Segment own{{}, {1.0F, 0.0F, 0.0F}, 0.1F};
    CHECK(overlaps(own, Segment{{0.5F, -1.0F, 0.15F}, {0.5F, 1.0F, 0.15F}, 0.1F}));
    CHECK(overlaps(own, Segment{{0.5F, -1.0F, 0.2F}, {0.5F, 1.0F, 0.2F}, 0.1F}));
    CHECK_FALSE(overlaps(own, Segment{{0.5F, -1.0F, 0.21F}, {0.5F, 1.0F, 0.21F}, 0.1F}));
    CHECK(overlaps(own, Segment{{0.5F, 0.1F, 0.0F}, {1.5F, 0.1F, 0.0F}, 0.1F}));
    CHECK_FALSE(overlaps(own, Segment{{2.0F, 0.1F, 0.0F}, {3.0F, 0.1F, 0.0F}, 0.1F}));
    // The true distance is 0.05, but the zero-length segment is measured from own's start: 0.8125 m².
    CHECK_FALSE(overlaps(own, Segment{{0.9F, 0.05F, 0.0F}, {0.9F, 0.05F, 0.0F}, 0.1F}));
}

TEST_CASE("A box and a sphere overlap as a true 3D box, in either order", "[physics][overlap]") {
    const Box box = cube({});
    for (const bool boxFirst : {true, false}) {
        CAPTURE(boxFirst);
        const auto test = [&](const Box& b, const Sphere& s) { return boxFirst ? overlaps(b, s) : overlaps(s, b); };
        CHECK(test(box, Sphere{{1.5F, 0.0F, 0.0F}, 0.5F}));
        CHECK_FALSE(test(box, Sphere{{1.51F, 0.0F, 0.0F}, 0.5F}));
        CHECK(test(box, Sphere{{0.2F, 0.3F, 0.0F}, 0.01F}));
        CHECK_FALSE(test(box, Sphere{{1.5F, 1.5F, 0.0F}, 0.7F}));
        CHECK(test(box, Sphere{{1.5F, 1.5F, 0.0F}, 0.75F}));
        const Box turned{.half = {1.0F, 0.5F, 0.5F},
                         .axes = {Vec3{0.70710677F, 0.70710677F, 0.0F}, Vec3{-0.70710677F, 0.70710677F, 0.0F},
                                  Vec3{0.0F, 0.0F, 1.0F}}};
        CHECK(test(turned, Sphere{{0.0F, 1.0F, 0.0F}, 0.4F}));
        CHECK_FALSE(test(turned, Sphere{{0.0F, 1.0F, 0.0F}, 0.2F}));
    }
}

TEST_CASE("A capsule against a box is a circle against its footprint", "[physics][overlap]") {
    struct Case {
        Vec3 base;
        Vec3 normal;
        float depth;
    };
    const Case hits[] = {
        {{0.0F, -1.2F, 0.0F}, {0.0F, -1.0F, 0.0F}, 0.15F},
        {{2.2F, 1.2F, 0.0F}, {0.70710677F, 0.70710677F, 0.0F}, 0.0425F},
        {{-2.2F, 1.2F, 0.0F}, {-0.70710677F, 0.70710677F, 0.0F}, 0.0425F},
        {{2.2F, -1.2F, 0.0F}, {0.70710677F, -0.70710677F, 0.0F}, 0.0425F},
        // The (−, −) corner's normal comes from the opposite corner: normalise(−4.2, −2.2).
        {{-2.2F, -1.2F, 0.0F}, {-0.8858F, -0.4640F, 0.0F}, 0.0425F},
        // Over the box, the face by x < y: +A0 here, +A1 for (−1.5, 0.1) though −x is nearest.
        {{0.5F, 0.2F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1.85F},
        {{-1.5F, 0.1F, 0.0F}, {0.0F, 1.0F, 0.0F}, 1.25F},
        {{2.2F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 0.15F},
        // Heights are not read: the pair test hits 50 m up.
        {{0.0F, -1.2F, 50.0F}, {0.0F, -1.0F, 0.0F}, 0.15F},
    };
    for (const Case& k : hits) {
        CAPTURE(k.base.x, k.base.y, k.base.z);
        Contact c = marked();
        REQUIRE(overlapShapes(c, human(k.base), carBox()));
        CHECK(c.halves[1].normal.x == Approx(k.normal.x).margin(kEps));
        CHECK(c.halves[1].normal.y == Approx(k.normal.y).margin(kEps));
        CHECK(c.halves[1].normal.z == Approx(k.normal.z).margin(kEps));
        CHECK(c.depth == Approx(k.depth).margin(kEps));
        CHECK(c.halves[0] == marked().halves[0]);
        CHECK(c.fraction == 7.0F);
    }
    // Face normals have w 1; a corner's w is not written.
    Contact face = marked();
    REQUIRE(overlapShapes(face, human({0.0F, -1.2F, 0.0F}), carBox()));
    CHECK(face.halves[1].normal.w == 1.0F);
    Contact corner = marked();
    REQUIRE(overlapShapes(corner, human({2.2F, 1.2F, 0.0F}), carBox()));
    CHECK(corner.halves[1].normal.w == 7.0F);

    CHECK_FALSE(overlaps(human({0.0F, -1.36F, 0.0F}), carBox()));
    CHECK_FALSE(overlaps(human({2.3F, 1.3F, 0.0F}), carBox()));
}

TEST_CASE("A box querying a capsule swaps the halves", "[physics][overlap]") {
    Contact c = marked();
    REQUIRE(overlapShapes(c, carBox(), human({0.0F, -1.2F, 0.0F})));
    checkVec(c.halves[0].normal, 0.0F, -1.0F, 0.0F, 1.0F);
    checkVec(c.halves[1].normal, 0.0F, 1.0F, 0.0F, 1.0F); // Coney's choice; the original leaves it undefined
    CHECK(c.depth == Approx(0.15F).margin(kEps));
    CHECK(c.halves[1].localPoint == kJunk);
    CHECK_FALSE(overlaps(carBox(), human({0.0F, -1.36F, 0.0F})));
}

TEST_CASE("A segment and a sphere overlap within the sum of the radii", "[physics][overlap]") {
    const Segment segment{{}, {0.4F, 0.0F, 0.0F}, 0.1F};
    CHECK(overlaps(segment, Sphere{{0.5F, 0.0F, 0.0F}, 0.15F}));
    CHECK_FALSE(overlaps(segment, Sphere{{0.2F, 0.3F, 0.0F}, 0.15F}));
    CHECK(overlaps(segment, Sphere{{0.2F, 0.3F, 0.0F}, 0.25F}));
    CHECK(overlaps(Sphere{{0.2F, 0.3F, 0.0F}, 0.25F}, segment));
    CHECK(overlaps(Segment{{}, {}, 0.1F}, Sphere{{0.2F, 0.0F, 0.0F}, 0.1F}));
}

TEST_CASE("A capsule has rounded ends against a sphere and a segment", "[physics][overlap]") {
    const Capsule capsule = human({});
    CHECK(overlaps(capsule, Sphere{{0.0F, 0.0F, 2.3F}, 0.1F}));
    CHECK(overlaps(capsule, Sphere{{0.0F, 0.0F, -0.3F}, 0.1F}));
    CHECK_FALSE(overlaps(capsule, Sphere{{0.46F, 0.0F, 1.0F}, 0.1F}));
    // The push sphere against a human.
    CHECK(overlaps(Sphere{{1.5F, 0.0F, 1.0F}, 2.0F}, capsule));

    const Segment over{{0.3F, -0.5F, 2.2F}, {0.3F, 0.5F, 2.2F}, 0.07F};
    CHECK(overlaps(over, capsule));
    CHECK(overlaps(capsule, over));
    const Segment higher{{0.3F, -0.5F, 2.4F}, {0.3F, 0.5F, 2.4F}, 0.07F};
    CHECK_FALSE(overlaps(higher, capsule));
    CHECK_FALSE(overlaps(capsule, higher));
}

TEST_CASE("Box-box overlap keeps the original's matrix", "[physics][overlap]") {
    CHECK(overlaps(cube({}), cube({1.9F, 0.0F, 0.0F})));
    CHECK(overlaps(cube({}), cube({2.0F, 0.0F, 0.0F})));
    CHECK_FALSE(overlaps(cube({}), cube({2.1F, 0.0F, 0.0F})));
    // These overlap by 0.5 m, but the centres in the original's matrix separate them.
    CHECK_FALSE(overlaps(cube({100.0F, 0.0F, 0.0F}), cube({101.5F, 0.0F, 0.0F})));
}

TEST_CASE("A segment never overlaps a box", "[physics][overlap]") {
    const Segment through{{-3.0F, 0.0F, 0.5F}, {3.0F, 0.0F, 0.5F}, 0.1F};
    CHECK_FALSE(overlaps(through, carBox()));
    CHECK_FALSE(overlaps(carBox(), through));
    CHECK(overlaps(Sphere{{1.9F, 0.0F, 0.5F}, 0.09F}, carBox()));
}
