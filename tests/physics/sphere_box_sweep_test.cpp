// SPDX-License-Identifier: GPL-3.0-or-later
// The sphere-box sweep (physics/sphere_box_sweep.h): the worked cases of docs/research/physics.md's Unit 5 test
// ideas, a hand sphere (r 0.09) against a car-like box, with the values computed from the page's spec.
#include "physics/sphere_box_sweep.h"

#include <array>
#include <cmath>
#include <optional>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "physics/contact.h"
#include "physics/shapes.h"

using Catch::Approx;
using coney::anim::Vec3;
using namespace coney::physics;

namespace {

// The axes of an unturned box.
constexpr std::array<Vec3, 3> kUnturned{{{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}}};

constexpr float kHand = 0.09F;
constexpr double kEps = 1e-4;

// Box B of the page: centre (0, 0, 0.5), axes identity, half (2, 1, 0.5): faces x = ±2, y = ±1, z = 0 and 1.
Box boxB() { return Box{.half = {2.0F, 1.0F, 0.5F}, .axes = kUnturned, .centre = {0.0F, 0.0F, 0.5F}}; }

// A contact filled with a marker, so the tests see which fields a sweep writes.
Contact marked() {
    Contact contact;
    const Vec4 junk{7.0F, 7.0F, 7.0F, 7.0F};
    contact.halves = {ContactHalf{nullptr, false, junk, junk, 7, 7}, ContactHalf{nullptr, false, junk, junk, 7, 7}};
    contact.fraction = 7.0F;
    contact.depth = 7.0F;
    contact.point = junk;
    contact.slide = junk;
    contact.flags = 7;
    return contact;
}

// Checks xyz and w of `v` against the expected values to 1e-4.
void checkVec(const Vec4& v, float x, float y, float z, float w) {
    CHECK(v.x == Approx(x).margin(kEps));
    CHECK(v.y == Approx(y).margin(kEps));
    CHECK(v.z == Approx(z).margin(kEps));
    CHECK(v.w == Approx(w).margin(kEps));
}

// Checks xyz of `v`.
void checkXyz(const Vec4& v, float x, float y, float z) {
    CHECK(v.x == Approx(x).margin(kEps));
    CHECK(v.y == Approx(y).margin(kEps));
    CHECK(v.z == Approx(z).margin(kEps));
}

// Sweeps a hand sphere from `prev` by `move` against `box` into a marked contact.
std::optional<Contact> strike(Vec3 prev, Vec3 move, const Box& box = boxB(), float radius = kHand) {
    Contact contact = marked();
    if (!sweepSphereBox(contact, Sphere{prev, radius}, box, move)) {
        CHECK(contact == marked());
        return std::nullopt;
    }
    return contact;
}

} // namespace

TEST_CASE("A sphere moving into a face hits it with the face's normal and point", "[physics][sphere-box]") {
    const auto found = strike({0.0F, -1.3F, 0.5F}, {0.0F, 0.4F, 0.0F});
    REQUIRE(found.has_value());
    const Contact c = found.value_or(Contact{});
    CHECK(c.fraction == Approx(0.525).margin(kEps));
    checkXyz(c.halves[1].localPoint, 0.0F, -1.0F, 0.0F);
    CHECK(c.halves[1].localPoint.w == 7.0F); // w of +0x60 is not written
    checkVec(c.point, 0.0F, -1.0F, 0.5F, 1.0F);
    checkVec(c.halves[1].normal, 0.0F, -1.0F, 0.0F, 0.0F);
    checkVec(c.halves[0].normal, 0.0F, 1.0F, 0.0F, 1.0F);
    CHECK(c.halves[1].featureType == kFeatureFace);
    CHECK(c.halves[1].featureIndex == 2);
    CHECK(c.flags == 0U);
    // Nothing else is written.
    CHECK(c.depth == 7.0F);
    CHECK(c.halves[0].featureType == 7);
    CHECK(c.halves[0].localPoint == Vec4{7.0F, 7.0F, 7.0F, 7.0F});
    CHECK(c.slide == Vec4{7.0F, 7.0F, 7.0F, 7.0F});
}

TEST_CASE("A sphere ending 0.1 m short of the box does not hit it", "[physics][sphere-box]") {
    CHECK_FALSE(strike({0.0F, -1.3F, 0.5F}, {0.0F, 0.11F, 0.0F}).has_value());
}

TEST_CASE("A face-region sphere clipping the face's edge keeps the face normal", "[physics][sphere-box]") {
    const auto found = strike({1.98F, -1.3F, 0.5F}, {0.1F, 0.4F, 0.0F});
    REQUIRE(found.has_value());
    const Contact c = found.value_or(Contact{});
    CHECK(c.fraction == Approx(0.5419).margin(kEps));
    checkXyz(c.halves[1].localPoint, 2.0F, -1.0F, 0.0F);
    checkVec(c.point, 2.0F, -1.0F, 0.5F, 1.0F);
    checkVec(c.halves[1].normal, 0.0F, -1.0F, 0.0F, 0.0F);
    checkVec(c.halves[0].normal, -0.8359F, 0.5488F, 0.0F, 1.0F);
    CHECK(c.halves[1].featureType == kFeatureFace);
    CHECK(c.halves[1].featureIndex == 2);
}

TEST_CASE("A path passing an edge farther than the radius misses", "[physics][sphere-box]") {
    CHECK_FALSE(strike({1.98F, -1.3F, 0.5F}, {0.2F, 0.4F, 0.0F}).has_value());
}

TEST_CASE("A sphere starting by a vertical edge meets the edge's cylinder", "[physics][sphere-box]") {
    const auto found = strike({2.3F, -1.3F, 0.5F}, {-0.4F, 0.4F, 0.0F});
    REQUIRE(found.has_value());
    const Contact c = found.value_or(Contact{});
    CHECK(c.fraction == Approx(0.5909).margin(kEps));
    checkXyz(c.halves[1].localPoint, 2.0F, -1.0F, 0.0F);
    checkVec(c.point, 2.0F, -1.0F, 0.5F, 1.0F);
    checkVec(c.halves[1].normal, 0.7071F, -0.7071F, 0.0F, 0.0F);
    checkVec(c.halves[0].normal, -0.8706F, 0.4921F, 0.0F, 1.0F);
    CHECK(c.halves[1].featureType == kFeatureEdge);
    CHECK(c.halves[1].featureIndex == 2);
}

TEST_CASE("An edge along y gets the original's wrong table normal", "[physics][sphere-box]") {
    const auto found = strike({2.3F, 0.0F, 1.3F}, {-0.4F, 0.0F, -0.4F});
    REQUIRE(found.has_value());
    const Contact c = found.value_or(Contact{});
    CHECK(c.fraction == Approx(0.5909).margin(kEps));
    checkXyz(c.halves[1].localPoint, 2.0F, 0.0F, 0.5F);
    checkVec(c.point, 2.0F, 0.0F, 1.0F, 1.0F);
    CHECK(c.halves[1].featureType == kFeatureEdge);
    CHECK(c.halves[1].featureIndex == 7);
    checkVec(c.halves[1].normal, -0.7071F, 0.0F, 0.7071F, 0.0F); // the true outward normal is (0.7071, 0, 0.7071)
    checkVec(c.halves[0].normal, -1.0F, 0.0F, 0.0F, 1.0F);
}

TEST_CASE("A corner-region sphere's point is its own centre at contact", "[physics][sphere-box]") {
    const auto found = strike({2.3F, -1.3F, 1.3F}, {-0.4F, 0.4F, -0.4F});
    REQUIRE(found.has_value());
    const Contact c = found.value_or(Contact{});
    CHECK(c.fraction == Approx(0.6201).margin(kEps));
    checkXyz(c.halves[1].localPoint, 2.0520F, -1.0520F, 0.5520F);
    checkVec(c.point, 2.0520F, -1.0520F, 1.0520F, 1.0F);
    CHECK(c.halves[1].featureType == kFeatureCorner);
    CHECK(c.halves[1].featureIndex == 5);
    checkVec(c.halves[1].normal, -0.5774F, -0.5774F, -0.5774F, 0.0F); // true: (0.5774, -0.5774, 0.5774)
    checkVec(c.halves[0].normal, -0.8706F, 0.4921F, 0.0F, 1.0F);
}

TEST_CASE("A centre starting inside the box hits at fraction 0 with the world z", "[physics][sphere-box]") {
    for (const Vec3 move : {Vec3{0.3F, -0.2F, 0.1F}, Vec3{}}) {
        const auto found = strike({0.5F, 0.2F, 0.6F}, move);
        REQUIRE(found.has_value());
        const Contact c = found.value_or(Contact{});
        CHECK(c.fraction == 0.0F);
        CHECK(c.halves[1].featureType == kFeatureInside);
        CHECK(c.halves[1].featureIndex == 0);
        checkVec(c.point, 0.5F, 0.2F, 0.6F, 1.0F);
        checkXyz(c.halves[1].localPoint, 0.5F, 0.2F, 0.6F); // the local z would be 0.1
        checkVec(c.halves[1].normal, 0.9285F, 0.3714F, 0.0F, 1.0F);
        checkVec(c.halves[0].normal, -0.9285F, -0.3714F, 0.0F, 1.0F);
    }
}

TEST_CASE("A sphere already touching with its centre outside does not hit until its centre is in",
          "[physics][sphere-box]") {
    CHECK_FALSE(strike({0.0F, -1.05F, 0.5F}, {0.0F, 0.1F, 0.0F}).has_value());
    const auto found = strike({0.0F, -0.95F, 0.5F}, {0.0F, 0.1F, 0.0F});
    REQUIRE(found.has_value());
    const Contact next = found.value_or(Contact{});
    CHECK(next.fraction == 0.0F);
    CHECK(next.halves[1].featureType == kFeatureInside);
}

TEST_CASE("A zero move hits only with the centre inside", "[physics][sphere-box]") {
    CHECK_FALSE(strike({0.0F, -1.3F, 0.5F}, {}).has_value());
    CHECK(strike({0.0F, 0.0F, 0.5F}, {}).has_value());
}

TEST_CASE("A sweep reaching the box at exactly the end of the move hits", "[physics][sphere-box]") {
    const auto found = strike({0.0F, -1.5F, 0.5F}, {0.0F, 0.25F, 0.0F}, boxB(), 0.25F);
    REQUIRE(found.has_value());
    const Contact c = found.value_or(Contact{});
    CHECK(c.fraction == 1.0F);
    CHECK_FALSE(strike({0.0F, -1.5F, 0.5F}, {0.0F, 0.125F, 0.0F}, boxB(), 0.25F).has_value());
}

TEST_CASE("The local point is in the struck box's own frame from its centre", "[physics][sphere-box]") {
    const Box rotated{.half = {2.0F, 1.0F, 0.5F},
                      .axes = {Vec3{0.0F, 1.0F, 0.0F}, Vec3{-1.0F, 0.0F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F}},
                      .centre = {10.0F, 5.0F, 0.5F}};
    const auto found = strike({11.3F, 5.0F, 0.5F}, {-0.4F, 0.0F, 0.0F}, rotated);
    REQUIRE(found.has_value());
    const Contact c = found.value_or(Contact{});
    CHECK(c.fraction == Approx(0.525).margin(kEps));
    checkXyz(c.halves[1].localPoint, 0.0F, -1.0F, 0.0F);
    checkVec(c.point, 11.0F, 5.0F, 0.5F, 1.0F);
    checkVec(c.halves[1].normal, 1.0F, 0.0F, 0.0F, 0.0F);
    checkVec(c.halves[0].normal, -1.0F, 0.0F, 0.0F, 1.0F);
}

TEST_CASE("A moving box against a resting sphere fills the mirrored halves", "[physics][sphere-box]") {
    Contact c = marked();
    REQUIRE(sweepBoxSphere(c, boxB(), Sphere{{0.0F, -1.3F, 0.5F}, kHand}, {0.0F, -0.4F, 0.0F}));
    CHECK(c.fraction == Approx(0.525).margin(kEps));
    CHECK(c.halves[0].featureType == kFeatureFace);
    CHECK(c.halves[0].featureIndex == 2);
    checkXyz(c.halves[0].localPoint, 0.0F, -1.0F, 0.0F);
    checkVec(c.point, 0.0F, -1.0F, 0.5F, 1.0F); // on the box's start pose: the true contact is at y -1.21
    checkVec(c.halves[0].normal, 0.0F, -1.0F, 0.0F, 0.0F);
    checkVec(c.halves[1].normal, 0.0F, 1.0F, 0.0F, 1.0F);
    CHECK(c.flags == 0U);
    CHECK(c.halves[1].featureType == 7);

    Contact missed = marked();
    CHECK_FALSE(sweepBoxSphere(missed, boxB(), Sphere{{0.0F, -1.3F, 0.5F}, kHand}, {0.0F, -0.1F, 0.0F}));
    CHECK(missed == marked());
}

TEST_CASE("The six wrong region normals are the original's table entries", "[physics][sphere-box]") {
    // Each start region of a unit cube at the origin, swept straight at the centre; the table entry the original
    // writes for the four edges along y and the odd corners.
    const Box cube{.half = {1.0F, 1.0F, 1.0F}, .axes = kUnturned, .centre = {}};
    struct Case {
        Vec3 start;
        int type;
        int index;
        Vec3 normal;
    };
    constexpr float a = 0.70710677F;
    constexpr float b = 0.57735026F;
    const Case cases[] = {
        {{2.0F, 0.0F, 2.0F}, kFeatureEdge, 7, {-a, 0.0F, a}},
        {{2.0F, 0.0F, -2.0F}, kFeatureEdge, 6, {-a, 0.0F, -a}},
        {{-2.0F, 0.0F, 2.0F}, kFeatureEdge, 5, {a, 0.0F, -a}},
        {{-2.0F, 0.0F, -2.0F}, kFeatureEdge, 4, {a, 0.0F, a}},
        {{2.0F, 2.0F, 2.0F}, kFeatureCorner, 1, {-b, b, -b}},
        {{-2.0F, 2.0F, -2.0F}, kFeatureCorner, 3, {b, b, b}},
        {{2.0F, -2.0F, 2.0F}, kFeatureCorner, 5, {-b, -b, -b}},
        {{-2.0F, -2.0F, -2.0F}, kFeatureCorner, 7, {b, -b, b}},
        {{-2.0F, 2.0F, 2.0F}, kFeatureCorner, 0, {-b, b, b}},
        {{2.0F, 2.0F, -2.0F}, kFeatureCorner, 2, {b, b, -b}},
        {{2.0F, 2.0F, 0.0F}, kFeatureEdge, 1, {a, a, 0.0F}},
        {{0.0F, 2.0F, 2.0F}, kFeatureEdge, 8, {0.0F, a, a}},
        {{0.0F, 0.0F, -2.0F}, kFeatureFace, 5, {0.0F, 0.0F, -1.0F}},
        {{-2.0F, 0.0F, 0.0F}, kFeatureFace, 3, {-1.0F, 0.0F, 0.0F}},
    };
    for (const Case& k : cases) {
        CAPTURE(k.start.x, k.start.y, k.start.z);
        const auto found = sweepSphereOrientedBox(0.1F, 1.0F, cube, {}, k.start, coney::anim::scale(k.start, -1.0F));
        REQUIRE(found.has_value());
        const SphereBoxHit hit = found.value_or(SphereBoxHit{});
        CHECK(hit.featureType == k.type);
        CHECK(hit.featureIndex == k.index);
        checkVec(hit.featureNormal, k.normal.x, k.normal.y, k.normal.z, 0.0F);
    }
}

TEST_CASE("The ray roots give the smaller root", "[physics][sphere-box]") {
    // |(-3, 0, 0) + t (1, 0, 0)|² = 1 at t = 2 and 4.
    CHECK(raySphereRoot({-3.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, 1.0F) == Approx(2.0F));
    CHECK(rayCircleRoot(0.0F, -5.0F, 0.0F, 2.0F, 4.0F, 4.0F) == Approx(1.5F));
}

TEST_CASE("A box posed from an object's frame has the frame's axes and position", "[physics][sphere-box]") {
    coney::anim::Mat34 pose;
    pose.x = {0.0F, 1.0F, 0.0F};
    pose.y = {-1.0F, 0.0F, 0.0F};
    pose.t = {10.0F, 5.0F, 0.5F};
    const Box box = boxAt(pose, {2.0F, 1.0F, 0.5F});
    CHECK(box.half[0] == 2.0F);
    CHECK(box.axes[1] == Vec3{-1.0F, 0.0F, 0.0F});
    CHECK(box.centre == Vec3{10.0F, 5.0F, 0.5F});
}
