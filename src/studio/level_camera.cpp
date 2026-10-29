// SPDX-License-Identifier: GPL-3.0-or-later
// Stub for the tests-first commit: the tests compile against it and fail.
#include "studio/level_camera.h"

namespace coney::studio {

std::optional<PanelPoint> project(const LevelView&, anim::Vec3) { return std::nullopt; }

Ray rayThrough(const LevelView&, PanelPoint) { return {}; }

std::optional<float> hitTriangle(const Ray&, anim::Vec3, anim::Vec3, anim::Vec3) { return std::nullopt; }

std::optional<float> hitBounds(const Ray&, const Bounds&) { return std::nullopt; }

std::optional<EntityId> pickEntity(const LevelDocument&, const LevelView&, PanelPoint) { return std::nullopt; }

FlyCamera::FlyCamera(anim::Vec3, float, float) {}

FlyCamera FlyCamera::at(const sandbox::Viewpoint&) { return FlyCamera({}, 0.0F, 0.0F); }

// Stubs for the fly camera feedback: the old fixed turn per pixel and no acceleration yet.
float FlyCamera::lookRadiansPerPixel(float /*displayScale*/, float /*sensitivity*/) { return 0.004F; }

float FlyCamera::accelerationAfter(float /*movingSeconds*/) { return 1.0F; }

void FlyCamera::step(const FlyInput&, float) {}

void FlyCamera::frame(const Bounds&, float) {}

ViewPose FlyCamera::pose() const { return {}; }

float FlyCamera::yawDegrees() const { return 0.0F; }

float FlyCamera::pitchDegrees() const { return 0.0F; }

LevelView levelView(const ViewPose&, float, float, float, float) { return {}; }

} // namespace coney::studio
