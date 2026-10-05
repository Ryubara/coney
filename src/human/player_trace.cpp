// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/player_trace.h"

#include <cmath>
#include <format>
#include <numbers>

namespace coney::human {

namespace {

constexpr float kDegrees = 180.0F / std::numbers::pi_v<float>;

} // namespace

std::string_view traceHeader() {
    return "step,x,y,z,heading,speed,vz,gait,clip,traversal,stamina,sprinting,cam_x,cam_y,cam_z,look_x,look_y,look_z,"
           "wanted_x,wanted_y,wanted_z,cam_distance,cam_pitch,cam_yaw,band_near,target_pitch,auto_turn\n";
}

std::string traceLine(std::uint64_t step, const Player& player) {
    const Human& human = player.human();
    const camera::FollowCamera& camera = player.camera();
    const anim::Vec3 feet = human.position();
    const anim::Vec3 eye = camera.position();
    const anim::Vec3 look = camera.lookAt();
    const anim::Vec3 wanted = camera.wanted();
    // The camera's angles from its offset to the look-at point: pitch above it, yaw the way its view faces.
    const anim::Vec3 offset = anim::subtract(eye, look);
    const float pitch = std::atan2(offset.z, std::hypot(offset.x, offset.y)) * kDegrees;
    const float yaw = headingOf(anim::scale(offset, -1.0F)) * kDegrees;
    return std::format("{},{:.4f},{:.4f},{:.4f},{:.3f},{:.4f},{:.4f},{},{},{},{},{},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},"
                       "{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.3f},{:.3f},{:.4f},{:.3f},{:.3f}\n",
                       step, feet.x, feet.y, feet.z, human.heading() * kDegrees, human.speed(), human.velocity().z,
                       static_cast<int>(human.gait()), human.animator().animId(), traversalName(human.traversal()),
                       human.stamina().value(), human.sprinting() ? 1 : 0, eye.x, eye.y, eye.z, look.x, look.y, look.z,
                       wanted.x, wanted.y, wanted.z, anim::length(offset), pitch, yaw, camera.bandNear(),
                       camera.targetPitch() * kDegrees, camera.lastAutoTurn() * kDegrees);
}

} // namespace coney::human
