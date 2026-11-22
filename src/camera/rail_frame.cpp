// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/rail_frame.h"

namespace coney::camera {

std::array<FramePlane, 3> framePlanes(const CameraView& /*view*/) { return {}; }

std::optional<anim::Vec3> keepTargetInFrame(const CameraView& /*view*/, const RailFrameTarget& /*target*/,
                                            const RailFrameRules& /*rules*/, const raycast::CollisionMesh* /*mesh*/) {
    return std::nullopt;
}

} // namespace coney::camera
