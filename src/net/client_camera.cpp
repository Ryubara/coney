// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/client_camera.h"

// The stubs the tests compile against: they do what the code did before this change, which is nothing (the client had
// no camera of its own). The real bodies follow in the next commit.

namespace coney::net {

std::optional<NetId> followedHuman(const ClientWorld::Humans& /*humans*/, std::uint8_t /*slot*/) {
    return std::nullopt;
}

camera::FollowTarget followTargetOf(const HumanState& /*now*/, const HumanState* /*before*/,
                                    const human::Speeds& /*speeds*/, float /*seconds*/) {
    return camera::FollowTarget{};
}

ClientCamera::ClientCamera(const camera::FollowSettings& settings) : m_settings(settings) {}

ClientCamera::Placed ClientCamera::placed() const { return Placed{}; }

void ClientCamera::step(const ClientWorld& /*world*/, std::optional<NetId> /*followed*/,
                        const ClientCameraInput& /*input*/) {}

std::optional<camera::CameraView> ClientCamera::view(float /*alpha*/) const { return std::nullopt; }

} // namespace coney::net
