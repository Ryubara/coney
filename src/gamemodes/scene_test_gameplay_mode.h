// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>
#include <utility>

#include "gamemodes/gameplay_mode.h"

namespace coney {

/// Keeps a debug scene request until the loading screen reaches the script-loading phase.
/// @coney-only Scene-test entry uses the ordinary gameplay mode's identity and loading lifecycle.
class SceneTestGameplayMode final : public GameplayMode {
  public:
    using GameplayMode::GameplayMode;

    /// Selects the scene for the next load only; null cancels a request before a transition.
    void requestSceneTest(std::optional<std::string> scene) { m_scene = std::move(scene); }

  private:
    // Consume before loading so a later ordinary level cannot inherit the debug request.
    // @coney-only Selects the debug script entry at the existing level-loading phase.
    void loadLevel() override {
        if (const auto scene = std::exchange(m_scene, std::nullopt)) {
            loadSceneTestLevel(*scene);
        } else {
            GameplayMode::loadLevel();
        }
    }

    std::optional<std::string> m_scene;
};

} // namespace coney
