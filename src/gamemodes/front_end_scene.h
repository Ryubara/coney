// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string_view>

#include "core/error.h"
#include "gamemodes/game_mode.h"

namespace coney {

/// The front-end level's 3D world behind the menus (docs/research/frontend.md#background): `level100`'s world seen
/// through the Wonder Wheel scene's camera. The level flow loads it with the front end (`InitLevel` with record 0) and
/// drops it when the front end finishes; the menus run its step and draw it under their 2D pass.
///
/// The platform layer implements it (src/platform/front_end_scene.h) with the world renderer.
class FrontEndScene {
  public:
    virtual ~FrontEndScene() = default;
    FrontEndScene() = default;
    FrontEndScene(const FrontEndScene&) = delete;
    FrontEndScene& operator=(const FrontEndScene&) = delete;
    FrontEndScene(FrontEndScene&&) = delete;
    FrontEndScene& operator=(FrontEndScene&&) = delete;

    /// One fixed step at game time `nowMs`: streaming, the scene's camera and objects.
    virtual void update(std::uint64_t nowMs) = 0;

    /// Begins a frame, draws the world through the scene's camera, then `overlay` (the menus' 2D pass and the fade),
    /// and presents.
    virtual void render(const RenderTime& time, const std::function<void()>& overlay) = 0;
};

/// Loads the front-end scene of the level named `level` (`level100`); fails as the level's files do.
using FrontEndSceneLoader = std::function<std::expected<std::unique_ptr<FrontEndScene>, Error>(std::string_view level)>;

} // namespace coney
