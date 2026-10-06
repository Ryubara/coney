// SPDX-License-Identifier: GPL-3.0-or-later
// The scripts' path camera (docs/research/camera.md#path-cameras): a flight from a camera's view through the points
// added, each point's time the time to the next, the points' functions and the end function fired as reached.
#include "camera/path_camera.h"

#include <cmath>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "camera/camera_view.h"

using coney::anim::Quat;
using coney::anim::Vec3;
using coney::camera::CameraView;
using coney::camera::orientationOf;
using coney::camera::PathCamera;
using coney::camera::PathPoint;

namespace {

// Whether two vectors are the same within a hundredth on every axis.
bool near(Vec3 a, Vec3 b) {
    return std::abs(a.x - b.x) <= 0.01F && std::abs(a.y - b.y) <= 0.01F && std::abs(a.z - b.z) <= 0.01F;
}

// A path from the origin: 1 s to a point 10 m along x ("reached"), 1 s on to a point 20 m along x, "ended" at the end.
PathCamera straightPath() {
    PathCamera path;
    path.setup(CameraView{.position = Vec3{}, .orientation = Quat{}, .lookAt = Vec3{}, .fieldOfView = 50.0F}, 1.0F,
               "ended", 0.0F, 0.0F);
    path.addPoint(
        PathPoint{.position = Vec3{10.0F, 0.0F, 0.0F}, .orientation = Quat{}, .seconds = 1.0F, .onReach = "reached"});
    path.addPoint(
        PathPoint{.position = Vec3{20.0F, 0.0F, 0.0F}, .orientation = Quat{}, .seconds = 0.0F, .onReach = {}});
    return path;
}

} // namespace

TEST_CASE("the path camera stays at its start until it is made active", "[camera][path]") {
    PathCamera path = straightPath();
    std::vector<std::string> fired;
    path.update(0.5F, fired);
    CHECK(near(path.view().position, Vec3{}));
    CHECK(path.view().fieldOfView == 50.0F);
    CHECK(fired.empty());
}

TEST_CASE("the path camera flies its points in their times and fires their functions, then the end's",
          "[camera][path]") {
    PathCamera path = straightPath();
    std::vector<std::string> fired;
    path.activate();
    path.update(0.5F, fired);
    // Halfway in time along the first segment: on the line between the start and the first point (the start standing
    // in for its own missing neighbour, the curve eases out of it: 4.375 m).
    CHECK(near(path.view().position, Vec3{4.375F, 0.0F, 0.0F}));
    CHECK(fired.empty());
    path.update(0.5F, fired);
    CHECK(fired == std::vector<std::string>{"reached"});
    path.update(1.0F, fired);
    CHECK(path.finished());
    CHECK(fired == std::vector<std::string>{"reached", "ended"});
    CHECK(near(path.view().position, Vec3{20.0F, 0.0F, 0.0F}));
    // It stays at the end and fires nothing more; making it active again while it flew does not restart it.
    path.activate();
    path.update(1.0F, fired);
    CHECK(fired.size() == 2);
}

TEST_CASE("the path camera holds eight points, a ninth replacing the last", "[camera][path]") {
    PathCamera path = straightPath();
    for (int i = 0; i < 8; ++i) {
        path.addPoint(PathPoint{.position = Vec3{static_cast<float>(i), 1.0F, 0.0F},
                                .orientation = Quat{},
                                .seconds = -1.0F,
                                .onReach = {}});
    }
    REQUIRE(path.points().size() == PathCamera::kMaxPoints);
    CHECK(near(path.points().back().position, Vec3{7.0F, 1.0F, 0.0F}));
    CHECK(path.points().back().seconds == 0.0F);
}

TEST_CASE("a path point's angles turn the camera as a locked camera's do", "[camera][path]") {
    // Heading 0 faces +y; a quarter turn anticlockwise faces -x.
    const Quat ahead = orientationOf(0.0F, 0.0F, 0.0F);
    const Quat left = orientationOf(90.0F, 0.0F, 0.0F);
    const Vec3 forward{0.0F, 1.0F, 0.0F};
    CHECK(near(coney::anim::transformDirection(coney::anim::matrixFromQuat(ahead), forward), forward));
    CHECK(near(coney::anim::transformDirection(coney::anim::matrixFromQuat(left), forward), Vec3{-1.0F, 0.0F, 0.0F}));
}
