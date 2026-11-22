// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/client_level_scene.h"

namespace coney::platform {

std::expected<std::unique_ptr<ClientLevelScene>, Error>
ClientLevelScene::load(RenderEngine& /*engine*/, const io::Wad& /*wad*/, const LevelAt& /*level*/,
                       world::SectorBudget& /*budget*/, std::function<void(std::string_view)> /*print*/) {
    return std::unexpected(Error{ErrorCode::NotFound, "not built"});
}

ClientLevelScene::~ClientLevelScene() = default;

void ClientLevelScene::step(const net::ClientWorld& /*world*/, const FrameTime& /*frame*/) {}

void ClientLevelScene::render(const net::ClientWorld& /*world*/, const RenderTime& /*time*/) {}

} // namespace coney::platform
