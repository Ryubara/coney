// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/start_up_flow.h"

#include <format>
#include <utility>

namespace coney {

StartUpFlow::StartUpFlow(graphics::RenderDevice& device, GameModeStack& stack,
                         const ProfileManagerMode::SheetLoader& loadSheet, gui::GlobalStrings& strings,
                         LegalScreenSettings legal, const std::function<void(std::string_view)>& log,
                         script::ScriptSource scripts, GameplayMode::LevelLoader loadLevel)
    : m_stack(stack), m_log(log), m_services(log), m_context{&m_state, &strings, this, &m_recorded, &m_humans},
      m_hasScripts(static_cast<bool>(scripts)),
      m_scripts(
          std::move(scripts),
          [this](script::ScriptSystem& system, script::LuaVm& vm) { script::installBindings(system, vm, m_context); },
          log),
      m_profileManager(device, loadSheet, strings, m_services, m_fade, m_scripts, legal.europe, log),
      m_gameplay(device, m_scripts, m_state, m_humans, std::move(loadLevel), log),
      m_levelFlow(device, stack, m_profileManager, m_services, m_scripts, m_state, log,
                  m_gameplay.loads() ? &m_gameplay : nullptr),
      m_missionComplete(device, stack, m_levelFlow, m_scripts, m_state, log), m_memoryCard(device, stack, m_levelFlow),
      m_legal(device, loadSheet, legal, log, &m_scripts) {
    m_state.language = legal.language;
    m_services.attachScripts(&m_scripts);
}

void StartUpFlow::start() {
    // Game_InitializeSubsystems makes the script system before main pushes the first modes.
    if (m_hasScripts) {
        m_scripts.create();
    }
    for (const std::string_view movie : kStartUpMovies) {
        m_services.playMovie(movie);
    }
    m_stack.push(m_levelFlow);
    m_memoryCard.setBootCheck();
    m_stack.push(m_memoryCard);
    m_stack.push(m_legal);
}

void StartUpFlow::showProfileManager(std::string_view onRumble, std::string_view onStartGame) {
    m_profileManager.show(m_stack, std::string(onRumble), std::string(onStartGame));
}

void StartUpFlow::showRumbleModeInterface(std::string_view onCancel, std::string_view /*onStart*/, double /*players*/) {
    // The Rumble mode's screens are not researched yet: cancel as if the player backed out, on the next frame, so the
    // script's cancel callback brings the main menu back.
    m_log("front end: the Rumble mode menus are not written yet; cancelled\n");
    m_scripts.schedule(std::string(onCancel), 0);
}

void StartUpFlow::menuLoadLevel(std::string_view level) { m_levelFlow.chooseLevel(level); }

void StartUpFlow::playMovie(std::string_view name) { m_services.playMovie(name); }

void StartUpFlow::playMusic(std::string_view track) { m_services.playMusic(track); }

void StartUpFlow::stopMusic() { m_services.stopMusic(); }

void StartUpFlow::queueScreenEffect(int type, double seconds) { m_fade.queue(type, seconds, m_scripts.now()); }

void StartUpFlow::launchMissionComplete(int kind) { m_missionComplete.launch(kind); }

} // namespace coney
