// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/client_world.h"

#include <cstddef>
#include <map>
#include <span>

namespace coney::net {

HumanState humanState(const HumanWords& /*words*/) { return {}; }

HumanWords humanWords(const HumanState& /*human*/) { return {}; }

std::size_t playingLayers(const HumanState& /*human*/) { return 0; }

HumanState blendHumans(const HumanState& /*from*/, const HumanState& to, float /*alpha*/) { return to; }

world_objects::WorldPose humanWorldPose(const HumanState& /*human*/) { return {}; }

anim::Pose humanPose(const HumanState& /*human*/, const ClipSource& /*clips*/,
                     std::span<const anim::Quat, anim::kPoseBones> /*defaultRotations*/) {
    return {};
}

ClientWorld::ClientWorld(JitterConfig config) : m_buffer(config) {}

void ClientWorld::receive(const SnapshotReceiver& /*snapshots*/) {}

void ClientWorld::step() {}

std::map<NetId, DrawnHuman> ClientWorld::drawn(float /*alpha*/) const { return {}; }

} // namespace coney::net
