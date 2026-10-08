// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/level_flow_mode.h"

#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/legal_screen_mode.h"
#include "scripting/lua_value.h"

namespace coney {

LevelFlowMode::LevelFlowMode(graphics::RenderDevice& device, GameModeStack& stack, ProfileManagerMode& profileManager,
                             FrontEndServices& services, script::ScriptSystem& scripts, GameState& state,
                             std::function<void(std::string_view)> log, GameplayMode* gameplay)
    : m_device(device), m_stack(stack), m_profileManager(profileManager), m_services(services), m_scripts(scripts),
      m_state(state), m_log(std::move(log)), m_gameplay(gameplay) {}

void LevelFlowMode::enter() {
    m_loadFrontEndOnResume = true;
    // Coney's direct start: what the legal screen and the front end run before a story level, then the level chosen,
    // so the resume below starts no front end.
    if (m_directStart) {
        DirectStart start = std::move(*m_directStart);
        m_directStart.reset();
        m_log(std::format("level flow: direct start of {} at checkpoint {}\n", start.level, start.checkpoint));
        if (m_scripts.exists()) {
            runPreloadScripts(m_scripts);
        }
        // What the menus would have done in the front end's state (the Rumble menu's set-up of an arena).
        if (start.ready) {
            start.ready();
        }
        // The front end's unload (finishFrontEnd()) makes a fresh state: the level starts from the bindings alone.
        if (m_scripts.exists()) {
            m_scripts.create();
        }
        m_state.checkPoint = start.checkpoint;
        chooseLevel(start.level);
    }
    resume();
}

void LevelFlowMode::startAtLevel(std::string level, int checkpoint, std::function<void()> ready) {
    m_directStart = DirectStart{.level = std::move(level), .checkpoint = checkpoint, .ready = std::move(ready)};
}

void LevelFlowMode::resume() {
    if (m_loadFrontEndOnResume && m_chosenLevel == kNoLevel && !m_cardLoad) {
        // The start-up load behind the memory-card screen, timed by the updates; any later one at once. **Coney's
        // reading**: the flag is cleared by this first use (the original's PM_Greet START, which follows it).
        if (m_loadingScreen != nullptr && m_loadScreenFlag) {
            m_loadScreenFlag = false;
            m_cardLoad = CardLoad{};
            return;
        }
        startFrontEnd();
    }
}

ModeResult LevelFlowMode::update(GameModeStack& /*stack*/, const FrameTime& frame) {
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    m_scripts.setTime(nowMs);
    // Behind the memory-card screen nothing else runs.
    if (m_cardLoad) {
        stepMemoryCardScreen(nowMs, frame.stepTicks / (GameTimer::kTicksPerSecond / 1000));
        return ModeResult::Stay;
    }

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
    if (m_cardLoad && m_loadingScreen != nullptr) {
        m_loadingScreen->render(time.gameTicks / (GameTimer::kTicksPerSecond / 1000));
        return;
    }
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
    loadFrontEnd();
    showMenus();
}

void LevelFlowMode::stepMemoryCardScreen(std::uint64_t nowMs, std::uint64_t stepMs) {
    LoadingScreen& screen = *m_loadingScreen;
    CardLoad& load = *m_cardLoad;
    if (!load.startMs) {
        // LoadScreen_Begin, at the resume: just before this first step.
        load.startMs = nowMs > stepMs ? nowMs - stepMs : 0;
        screen.beginMemoryCard(*load.startMs);
    }
    const std::uint64_t start = *load.startMs;
    if (load.phase == CardLoad::Phase::FadeIn && nowMs >= start + LoadScreenTimeline::kFadeMilliseconds) {
        // Faded in: the whole load in this one step.
        loadFrontEnd();
        load.phase = CardLoad::Phase::Loading;
    }
    if (load.phase == CardLoad::Phase::Loading && nowMs >= start + kMemoryCardHoldMilliseconds) {
        screen.finish(nowMs);
        load.phase = CardLoad::Phase::FadeOut;
    }
    if (load.phase == CardLoad::Phase::FadeOut && screen.finished(nowMs)) {
        screen.end();
        m_cardLoad.reset();
        showMenus();
    }
}

void LevelFlowMode::loadFrontEnd() {
    // Level index 0 is the front end, `level100` (its name from the level table when the preloads filled it).
    m_state.currentLevel = 0;
    const LevelRecord* record = m_state.levels.at(0);
    m_currentLevel = record != nullptr ? record->name : std::string(kFrontEndLevel);

    // InitLevel: the level's world (the scene behind the menus), then its script step: global.lua, then the level's own
    // script.
    loadScene();
    makeScenes();
    m_scripts.enterLevel(m_currentLevel);
    m_services.loadBank(ProfileManagerMode::kSoundBank);
}

void LevelFlowMode::showMenus() {
    // Menu.onStart shows the menus. Without scripts, or when it did not, Coney shows them itself with the callbacks the
    // script passes.
    m_scripts.call("Menu.onStart");
    if (m_stack.topId() != ProfileManagerMode::kId) {
        m_log("level flow: Menu.onStart did not show the menus; showing them with its callbacks\n");
        m_profileManager.show(m_stack, std::string(kOnRumble), std::string(kOnStartGame));
        // ... and fades in over 1.5 s as `Menu.onStart` does, after the movies left the screen black.
        m_profileManager.queueFade(graphics::ScreenFade::kFadeIn, kOnStartFadeSeconds);
    }
    m_frontEndLoaded = true;
}

void LevelFlowMode::finishFrontEnd() {
    m_scripts.call("Menu.onFinish");
    // UnloadLevel(0): the front-end world and its scenes go with the level.
    dropScenes();
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
    dropScenes();
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

namespace coney {

void LevelFlowMode::makeScenes() {
    dropScenes();
    // The world's lights and tint reach the bindings with or without a scene system.
    if (m_context != nullptr) {
        m_context->lighting = m_scene ? m_scene->lighting() : nullptr;
        m_context->tint = m_scene ? m_scene->tint() : nullptr;
    }
    if (!m_sceneMaker) {
        return;
    }
    m_scenes = m_sceneMaker();
    if (!m_scenes) {
        return;
    }
    // A preload's callback and an end function call into the front end's scripts.
    m_scenes->setScriptCall([this](std::string_view function, std::span<const double> args) {
        std::vector<script::Value> values(args.begin(), args.end());
        m_scripts.call(function, values);
    });
    m_scenes->setHost(m_scene ? m_scene->sceneHost() : nullptr);
    if (m_context != nullptr) {
        m_context->scenes = m_scenes.get();
    }
    m_profileManager.setScenes(m_scenes.get());
}

void LevelFlowMode::dropScenes() {
    if (m_context != nullptr) {
        if (m_scenes && m_context->scenes == m_scenes.get()) {
            m_context->scenes = nullptr;
        }
        if (m_scene && m_context->lighting != nullptr && m_context->lighting == m_scene->lighting()) {
            m_context->lighting = nullptr;
        }
        if (m_scene && m_context->tint != nullptr && m_context->tint == m_scene->tint()) {
            m_context->tint = nullptr;
        }
    }
    m_profileManager.setScenes(nullptr);
    if (m_scenes) {
        m_scenes->setHost(nullptr);
    }
    m_scenes.reset();
}

} // namespace coney
