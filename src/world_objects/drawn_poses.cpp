// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/drawn_poses.h"

#include "core/interpolation.h"

namespace coney::world_objects {

WorldPose blendPose(const WorldPose& from, const WorldPose& to, float alpha) {
    // The ends exactly: slerp does not return its ends bit for bit.
    if (alpha >= 1.0F) {
        return to;
    }
    if (alpha <= 0.0F) {
        return from;
    }
    return WorldPose{.position = anim::Vec3{lerp(from.position.x, to.position.x, alpha),
                                            lerp(from.position.y, to.position.y, alpha),
                                            lerp(from.position.z, to.position.z, alpha)},
                     .rotation = anim::slerp(from.rotation, to.rotation, alpha)};
}

} // namespace coney::world_objects
