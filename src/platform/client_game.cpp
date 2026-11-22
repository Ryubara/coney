// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/client_game.h"

#include <utility>

#include "platform/net_play.h"

namespace coney::platform {

ClientGame::ClientGame(NetPlay& net, ClientSceneLoader load, std::function<bool(std::string_view)> levelExists,
                       std::function<void(std::string_view)> print, graphics::RenderDevice* device)
    : m_net(net), m_load(std::move(load)), m_levelExists(std::move(levelExists)), m_print(std::move(print)),
      m_device(device) {}

ModeResult ClientGame::update(GameModeStack& /*stack*/, const FrameTime& /*frame*/) { return ModeResult::Stay; }

void ClientGame::render(const RenderTime& /*time*/) {}

bool ClientGame::inPlay() const { return false; }

bool ClientGame::jumpToLevel(std::string_view /*level*/, int /*checkpoint*/) { return false; }

bool ClientGame::leaveLevel() { return false; }

void ClientGame::dropLevel() {}

} // namespace coney::platform
