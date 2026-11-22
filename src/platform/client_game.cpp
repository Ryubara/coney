// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/client_game.h"

#include <format>
#include <string>
#include <utility>

#include "net/net_status.h"
#include "platform/net_play.h"

namespace coney::platform {

namespace {

// What a frame with no level shows: black, as between two of the game's screens.
constexpr graphics::Rgba kNoLevelColour{0, 0, 0, 255};

} // namespace

ClientGame::ClientGame(NetPlay& net, ClientSceneLoader load, std::function<bool(std::string_view)> levelExists,
                       std::function<void(std::string_view)> print, graphics::RenderDevice* device)
    : m_net(net), m_load(std::move(load)), m_levelExists(std::move(levelExists)), m_print(std::move(print)),
      m_device(device) {}

ModeResult ClientGame::update(GameModeStack& /*stack*/, const FrameTime& frame) {
    // A session that ended takes the level with it: nothing more will come for it.
    if (m_net.role() != net::NetRole::Client && m_level) {
        m_leavePending = true;
        m_pending.reset();
    }
    if (m_leavePending) {
        m_leavePending = false;
        dropLevel();
    }
    // The host's level, loaded between two steps; in play at once, so NetLevels tells the host at its next look.
    if (m_pending) {
        const LevelAt at = std::move(*m_pending);
        m_pending.reset();
        dropLevel();
        ++m_starts;
        m_level = at;
        auto scene = m_load(at);
        if (scene) {
            m_scene = std::move(*scene);
            m_world = std::make_unique<net::ClientWorld>();
            // The receiver still keeps the last level's snapshots; this level's world takes none of them.
            if (const net::SnapshotReceiver* snapshots = m_net.clientSnapshots(); snapshots != nullptr) {
                m_world->skipDecoded(*snapshots);
            }
            m_print(
                std::format("client game: drawing the host's level {} at checkpoint {}\n", at.level, at.checkpoint));
        } else {
            m_print(
                std::format("client game: the host's level {} did not load: {}\n", at.level, scene.error().message));
        }
    }
    // The world takes what the session decoded and moves on a step; the scene streams round it.
    if (m_world && m_scene) {
        if (const net::SnapshotReceiver* snapshots = m_net.clientSnapshots(); snapshots != nullptr) {
            m_world->receive(*snapshots);
        }
        m_world->step();
        m_scene->step(*m_world, frame);
    }
    // No copy of the host's movie plays here: each is over as soon as it is known, so the host waits for no copy.
    if (const std::optional<net::HostMovie> movie = m_net.hostMovie(); movie && movie->step != m_movieAnswered) {
        m_net.movieDone(movie->step, false);
        m_movieAnswered = movie->step;
    }
    return ModeResult::Stay;
}

void ClientGame::render(const RenderTime& time) {
    if (m_scene && m_world) {
        m_scene->render(*m_world, time);
        return;
    }
    if (m_device != nullptr) {
        m_device->beginFrame(kNoLevelColour);
        m_device->present();
    }
}

bool ClientGame::inPlay() const { return m_scene != nullptr && !m_pending && !m_leavePending; }

bool ClientGame::jumpToLevel(std::string_view level, int checkpoint) {
    // NetLevels asks again each step and notes the refusal once.
    if (!m_levelExists(level)) {
        return false;
    }
    m_pending = LevelAt{.level = std::string(level), .checkpoint = checkpoint};
    m_leavePending = false;
    return true;
}

bool ClientGame::leaveLevel() {
    if (!m_level && !m_pending) {
        return false;
    }
    m_pending.reset();
    m_leavePending = true;
    return true;
}

void ClientGame::dropLevel() {
    m_scene.reset();
    m_world.reset();
    m_level.reset();
}

} // namespace coney::platform
