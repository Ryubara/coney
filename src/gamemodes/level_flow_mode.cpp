// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/level_flow_mode.h"

#include <format>
#include <optional>
#include <utility>

#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"

namespace coney {

LevelFlowMode::LevelFlowMode(graphics::RenderDevice& device, GameModeStack& stack, ProfileManagerMode& profileManager,
                             FrontEndServices& services, script::ScriptSystem& scripts, GameState& state,
                             std::function<void(std::string_view)> log)
    : m_device(device), m_stack(stack), m_profileManager(profileManager), m_services(services), m_scripts(scripts),
      m_state(state), m_log(std::move(log)) {}

void LevelFlowMode::enter() {
    m_loadFrontEndOnResume = true;
    resume();
}

void LevelFlowMode::resume() {
    if (m_loadFrontEndOnResume && m_chosenLevel == kNoLevel) {
        startFrontEnd();
    }
}

ModeResult LevelFlowMode::update(GameModeStack& /*stack*/, const FrameTime& frame) {
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    m_scripts.setTime(nowMs);

    // A level is chosen: the original finishes the front end, selects the level and pushes gameplay (mode 1). Mode 1
    // is not written yet, so Coney stops at the request and brings the front end back.
    if (m_chosenLevel != kNoLevel) {
        const LevelRecord* record = m_state.levels.at(static_cast<std::size_t>(m_chosenLevel));
        const std::string name = record != nullptr ? record->name : std::string();
        finishFrontEnd();
        m_log(std::format("level flow: level start requested: {} (level index {}); gameplay is not written yet, back "
                          "to the front end\n",
                          name, m_chosenLevel));
        m_chosenLevel = kNoLevel;
        startFrontEnd();
    }

    // The front-end world's frame: no world yet, so the black background the front end sets; then the scripts.
    m_device.beginFrame(graphics::kBlack);
    m_scripts.update(nowMs, frame.seconds);
    m_device.present();
    return ModeResult::Stay;
}

void LevelFlowMode::chooseLevel(std::string_view name) {
    const std::optional<std::size_t> index = m_state.levels.find(name);
    m_levelRequests.emplace_back(name);
    if (!index) {
        m_log(std::format("level flow: MenuLoadLevel({}): no such level in the level table; ignored\n", name));
        return;
    }
    m_chosenLevel = static_cast<int>(*index);
}

void LevelFlowMode::startFrontEnd() {
    // Level index 0 is the front end, `level100` (its name from the level table when the preloads filled it).
    m_state.currentLevel = 0;
    const LevelRecord* record = m_state.levels.at(0);
    m_currentLevel = record != nullptr ? record->name : std::string(kFrontEndLevel);

    // InitLevel's script step: global.lua, then the level's own script. Nothing else of the level loads yet.
    m_log(std::format("level flow: front end {} (scripts only: no level loader yet)\n", m_currentLevel));
    m_scripts.enterLevel(m_currentLevel);
    m_services.playMusic(ProfileManagerMode::kMusic);

    // Menu.onStart shows the menus. Without scripts, or when it did not, Coney shows them itself with the callbacks the
    // script passes.
    m_scripts.call("Menu.onStart");
    if (m_stack.topId() != ProfileManagerMode::kId) {
        m_log("level flow: Menu.onStart did not show the menus; showing them with its callbacks\n");
        m_profileManager.show(m_stack, std::string(kOnRumble), std::string(kOnStartGame));
    }
    m_frontEndLoaded = true;
}

void LevelFlowMode::finishFrontEnd() {
    m_scripts.call("Menu.onFinish");
    // UnloadLevel destroys the script system and makes it again: the next level starts from the bindings alone.
    if (m_scripts.exists()) {
        m_scripts.create();
    }
    m_frontEndLoaded = false;
}

} // namespace coney
