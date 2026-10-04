// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/level_flow_mode.h"

#include <format>
#include <string>
#include <utility>

#include "gamemodes/game_mode_stack.h"

namespace coney {

LevelFlowMode::LevelFlowMode(graphics::RenderDevice& device, GameModeStack& stack, ProfileManagerMode& profileManager,
                             FrontEndServices& services, std::function<void(std::string_view)> log)
    : m_device(device), m_stack(stack), m_profileManager(profileManager), m_services(services), m_log(std::move(log)) {}

void LevelFlowMode::enter() {
    m_loadFrontEndOnResume = true;
    resume();
}

void LevelFlowMode::resume() {
    if (m_loadFrontEndOnResume && m_chosenLevel == kNoLevel) {
        startFrontEnd();
    }
}

ModeResult LevelFlowMode::update(GameModeStack& /*stack*/, const FrameTime& /*frame*/) {
    // The front-end world's frame: no world yet, so the black background the front end sets.
    // TODO(docs/research/frontend.md#mode-flow): with a chosen level, Menu.onFinish, UnloadLevel and push mode 1.
    m_device.beginFrame(graphics::kBlack);
    m_device.present();
    return ModeResult::Stay;
}

void LevelFlowMode::startFrontEnd() {
    // Level index 0 is the front end. Coney selects it by name: no level table, and InitLevel is not written yet.
    m_currentLevel = kFrontEndLevel;
    m_log(std::format("level flow: front end {} (not loaded: no level loader yet)\n", m_currentLevel));
    m_services.playMusic(ProfileManagerMode::kMusic);
    // Menu.onStart's stand-in: the call that shows the menus.
    m_profileManager.show(m_stack, std::string(kOnRumble), std::string(kOnStartGame));
    m_frontEndLoaded = true;
}

} // namespace coney
