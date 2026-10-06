// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/level_flow_mode.h"

#include <cstddef>
#include <format>
#include <optional>
#include <utility>

#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"

namespace coney {

LevelFlowMode::LevelFlowMode(graphics::RenderDevice& device, GameModeStack& stack, ProfileManagerMode& profileManager,
                             FrontEndServices& services, script::ScriptSystem& scripts, GameState& state,
                             std::function<void(std::string_view)> log, GameplayMode* gameplay)
    : m_device(device), m_stack(stack), m_profileManager(profileManager), m_services(services), m_scripts(scripts),
      m_state(state), m_log(std::move(log)), m_gameplay(gameplay) {}

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

    // A level is chosen: finish the front end if it is loaded, select the level and push gameplay (mode 1), whose
    // enter loads it. Without gameplay, Coney stops at the request and brings the front end back.
    if (m_chosenLevel != kNoLevel) {
        const auto index = static_cast<std::size_t>(m_chosenLevel);
        const LevelRecord* record = m_state.levels.at(index);
        const std::string name = record != nullptr ? record->name : std::string();
        if (m_frontEndLoaded) {
            finishFrontEnd();
        }
        m_chosenLevel = kNoLevel;
        if (m_gameplay != nullptr) {
            m_state.currentLevel = index;
            m_currentLevel = name;
            m_log(std::format("level flow: starting {} (level index {}, checkpoint {})\n", name, index,
                              m_state.checkPoint));
            m_gameplay->setLevel(name);
            m_stack.push(*m_gameplay);
        } else {
            m_log(std::format("level flow: level start requested: {} (level index {}); no gameplay to start it, back "
                              "to the front end\n",
                              name, index));
            startFrontEnd();
        }
    }

    // The scripts' step. The original draws the front-end world's frame around it; Coney's render() does.
    m_scripts.update(nowMs, frame.seconds);
    return ModeResult::Stay;
}

void LevelFlowMode::render(const RenderTime& time) {
    // The front-end world when it is loaded, else the black background the front end sets.
    if (m_scene) {
        m_scene->render(time, {});
        return;
    }
    m_device.beginFrame(graphics::kBlack);
    m_device.present();
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

void LevelFlowMode::chooseLevelIndex(std::size_t index) {
    const LevelRecord* record = m_state.levels.at(index);
    if (record == nullptr) {
        m_log(std::format("level flow: level index {}: no such record in the level table; ignored\n", index));
        return;
    }
    m_levelRequests.push_back(record->name);
    m_chosenLevel = static_cast<int>(index);
}

void LevelFlowMode::startFrontEnd() {
    // Level index 0 is the front end, `level100` (its name from the level table when the preloads filled it).
    m_state.currentLevel = 0;
    const LevelRecord* record = m_state.levels.at(0);
    m_currentLevel = record != nullptr ? record->name : std::string(kFrontEndLevel);

    // InitLevel: the level's world (the scene behind the menus), then its script step: global.lua, then the level's own
    // script.
    loadScene();
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
    // UnloadLevel(0): the front-end world goes with the level.
    m_profileManager.setScene(nullptr);
    m_scene.reset();
    // UnloadLevel destroys the script system and makes it again: the next level starts from the bindings alone.
    if (m_scripts.exists()) {
        m_scripts.create();
    }
    m_frontEndLoaded = false;
}

} // namespace coney

namespace coney {

void LevelFlowMode::loadScene() {
    m_scene.reset();
    if (!m_loadScene) {
        m_log(std::format("level flow: front end {} (scripts only: no scene loader)\n", m_currentLevel));
        m_profileManager.setScene(nullptr);
        return;
    }
    auto scene = m_loadScene(m_currentLevel);
    if (!scene) {
        m_log(std::format("level flow: front end {}: no scene: {}\n", m_currentLevel, scene.error().message));
    } else {
        m_scene = std::move(*scene);
        m_log(std::format("level flow: front end {} with its scene\n", m_currentLevel));
    }
    m_profileManager.setScene(m_scene.get());
}

} // namespace coney
