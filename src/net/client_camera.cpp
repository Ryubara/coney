// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/client_camera.h"

#include "camera/camera_lens.h"
#include "core/assert.h"
#include "world_objects/drawn_poses.h"

namespace coney::net {

namespace {

// Whether `from` to `to` is one move of a human walking or running, not a jump the snapshots show (a teleport, a
// respawn): no velocity is read across a jump, as DrawnPoses does not blend one.
bool isJump(anim::Vec3 from, anim::Vec3 to) {
    return anim::distance(from, to) > world_objects::DrawnPoses<NetId>::kMostBlendedMetres;
}

} // namespace

std::optional<NetId> followedHuman(const ClientWorld::Humans& humans, std::uint8_t /*slot*/) {
    // The ids are walked in order, so the first is the lowest; the host's player 1 (**stand-in**, see the header).
    if (humans.empty()) {
        return std::nullopt;
    }
    return humans.begin()->first;
}

camera::FollowTarget followTargetOf(const HumanState& now, const HumanState* before, const human::Speeds& speeds,
                                    float seconds) {
    CONEY_ASSERT(seconds > 0.0F);
    // The move between the two steps over the step's length: the camera's aim point lags by it, and the gait is its
    // size's nearest (Human::gait() reads the velocity's length the same way).
    anim::Vec3 velocity;
    if (before != nullptr && !isJump(before->position, now.position)) {
        velocity = anim::scale(anim::subtract(now.position, before->position), 1.0F / seconds);
    }
    const human::Gait gait = human::gaitOfSpeed(anim::length(velocity), speeds);
    // **Coney's reading**: a knocked-out or wounded human, or one with no health left, does not count for the cameras
    // and lies down, so the aim point's height follows his body with no dead band.
    const bool counts = !now.knockedOut && now.health > 0;
    return camera::FollowTarget{.feet = now.position,
                                .heading = now.heading,
                                .gait = static_cast<std::uint8_t>(gait),
                                .airborne = !now.grounded,
                                .stickBack = false,
                                .nearestEnemy = std::nullopt,
                                .lockOn = false,
                                .lockHeld = false,
                                .enemy = std::nullopt,
                                .secondary = std::nullopt,
                                .secondaryRange = 0.0F,
                                .bodyPoint = std::nullopt,
                                .grabbing = false,
                                .down = !counts,
                                .velocity = velocity,
                                .walkSpeed = speeds.walk,
                                .runSpeed = speeds.run,
                                .sprintSpeed = speeds.sprint,
                                .stickOwned = true,
                                .counts = counts,
                                .longFall = false};
}

ClientCamera::ClientCamera(const camera::FollowSettings& settings) : m_settings(settings) {}

ClientCamera::Placed ClientCamera::placedBy(const camera::FollowCamera& follow) {
    // The play mode's own reading of its follow camera (Player::capture()): the eye, the aim point as the target.
    return Placed{.eye = follow.position(),
                  .target = follow.aimPoint(),
                  .fieldOfView = follow.fieldOfView(),
                  .nearClip = follow.nearClip(),
                  .farClip = camera::kPlayerCameraLens.farClip};
}

void ClientCamera::step(const ClientWorld& world, std::optional<NetId> followed, const ClientCameraInput& input) {
    const ClientWorld::Humans& humans = world.humans();
    const auto found = followed ? humans.find(*followed) : humans.end();
    // Nobody to follow: no view, and the next human gets a camera behind him.
    if (found == humans.end()) {
        m_camera.reset();
        m_followed.reset();
        return;
    }
    const HumanState& now = found->second;
    const auto earlier = world.previousHumans().find(found->first);
    const HumanState* before = earlier != world.previousHumans().end() ? &earlier->second : nullptr;
    // A human not followed until now: the camera is made behind him, and drawn there from this step on (a cut).
    bool cut = false;
    if (!m_camera || m_followed != followed) {
        m_camera.emplace(now.position, now.heading, m_settings);
        m_followed = followed;
        ++m_cuts;
        cut = true;
    }
    camera::FollowCamera& follow = *m_camera;
    follow.update(followTargetOf(now, before, input.speeds, input.seconds), input.rawRightX, input.rawRightY,
                  input.mesh, input.seconds);
    m_previous = cut ? placedBy(follow) : m_current;
    m_current = placedBy(follow);
}

std::optional<camera::CameraView> ClientCamera::view(float alpha) const {
    if (!m_camera) {
        return std::nullopt;
    }
    // The ends are exact: alpha 1 draws the last step's view, alpha 0 the step before's.
    const bool newest = alpha >= 1.0F;
    const bool oldest = alpha <= 0.0F;
    const anim::Vec3 eye = newest   ? m_current.eye
                           : oldest ? m_previous.eye
                                    : anim::lerp(m_previous.eye, m_current.eye, alpha);
    const anim::Vec3 target = newest   ? m_current.target
                              : oldest ? m_previous.target
                                       : anim::lerp(m_previous.target, m_current.target, alpha);
    return camera::viewLookingAt(eye, target, m_current.fieldOfView, m_current.nearClip, m_current.farClip);
}

} // namespace coney::net
