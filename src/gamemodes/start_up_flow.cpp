// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/start_up_flow.h"

#include <format>
#include <utility>

#include "scripting/hud_bindings.h"
#include "warriors/disk_profile_store.h"

namespace coney {

namespace {

// The save system for `folder`: profiles on disk there, or for the run only when there is no folder (test mode).
std::unique_ptr<ProfileStore> makeProfileStore(const std::optional<std::filesystem::path>& folder, GameState& state) {
    if (folder.has_value()) {
        return std::make_unique<DiskProfileStore>(folder.value(), state);
    }
    return std::make_unique<SessionProfileStore>();
}

} // namespace

StartUpFlow::StartUpFlow(graphics::RenderDevice& device, GameModeStack& stack,
                         const ProfileManagerMode::SheetLoader& loadSheet, gui::GlobalStrings& strings,
                         LegalScreenSettings legal, const std::function<void(std::string_view)>& log,
                         script::ScriptSource scripts, GameplayMode::LevelLoader loadLevel,
                         const std::optional<std::filesystem::path>& profileFolder, std::uint64_t cardCheckingMs)
    : m_stack(stack), m_log(log), m_services(log), m_profiles(makeProfileStore(profileFolder, m_state)),
      m_context{&m_state, &strings,    this,     &m_recorded,      &m_humans,      &m_flags,       &m_rumbleData,
                nullptr,  &m_messages, &m_boxes, &m_animCallbacks, &m_objectTypes, &m_spawnRecords},
      m_hasScripts(static_cast<bool>(scripts)),
      m_scripts(
          std::move(scripts),
          [this](script::ScriptSystem& system, script::LuaVm& vm) { script::installBindings(system, vm, m_context); },
          log),
      m_profileManager(device, loadSheet, strings, m_services, m_fade, m_scripts, m_state, *m_profiles, legal.europe,
                       log),
      m_gameplay(device, m_scripts, m_context, m_state, m_humans, m_flags, m_recorded, std::move(loadLevel), log),
      m_levelFlow(device, stack, m_profileManager, m_services, m_scripts, m_state, log,
                  m_gameplay.loads() ? &m_gameplay : nullptr),
      m_missionComplete(device, stack, m_levelFlow, m_scripts, m_state, log),
      m_pause(
          device, loadSheet, [this](std::uint32_t record) { return loadRecord(record); }, strings, m_services, m_state,
          m_levelFlow, log),
      m_missionFailed(
          device, loadSheet, [this](std::uint32_t record) { return loadRecord(record); }, strings, m_services, m_state,
          m_levelFlow, stack, log),
      m_rumbleMenu(device, loadSheet, stack, m_scripts, m_state, strings, m_rumbleData, m_services, m_fade,
                   legal.europe, log),
      m_rumbleIntro(loadSheet, strings, m_services, m_state, log),
      m_rumbleResult(
          device, loadSheet, strings, m_services, m_state, m_levelFlow, stack,
          [this] { return m_rumbleMenu.fromFrontEnd(); }, log),
      m_memoryCard(device, stack, m_levelFlow, loadSheet, strings, cardCheckingMs, log),
      m_legal(device, loadSheet, legal, log, &m_scripts) {
    m_state.language = legal.language;
    // The HUD the scripts' HUD bindings act on; the scripts start later (start()).
    m_context.hud = &m_hud;
    m_hud.setServices(script::hudServicesOf(m_context));
    m_services.attachScripts(&m_scripts);
    m_gameplay.setMoviePlayer(&m_services);
    m_missionComplete.setProfiles(m_profiles.get());
    m_memoryCard.setProfiles(m_profiles.get());
    m_memoryCard.setProfileManager(&m_profileManager);
    // START pauses a level; the pause and the mission-failed screen draw the level under them.
    m_gameplay.setPause(&m_pause);
    m_pause.setWorld(&m_gameplay);
    m_missionFailed.setWorld(&m_gameplay);
    // A Rumble match: the intro over play, the result screen over the world going on.
    m_gameplay.addOverlay(&m_rumbleIntro);
    m_rumbleResult.setWorld(&m_gameplay);
}

void StartUpFlow::start() {
    // Game_InitializeSubsystems makes the script system before main pushes the first modes.
    if (m_hasScripts) {
        m_scripts.create();
    }
    m_stack.push(m_levelFlow);
    m_memoryCard.setBootCheck();
    m_stack.push(m_memoryCard);
    m_stack.push(m_legal);
    // The original's main plays the start-up movies before it pushes the modes; Coney's movie player is a mode that
    // goes over the top of the stack, so the movies are asked for after the pushes and still play first.
    for (const std::string_view movie : kStartUpMovies) {
        playMovie(movie);
    }
}

void StartUpFlow::showProfileManager(std::string_view onRumble, std::string_view onStartGame) {
    m_profileManager.show(m_stack, std::string(onRumble), std::string(onStartGame));
}

void StartUpFlow::showRumbleModeInterface(std::string_view onCancel, std::string_view onStart, double fromFrontEnd) {
    m_rumbleMenu.show(std::string(onCancel), std::string(onStart), fromFrontEnd != 0.0);
}

void StartUpFlow::menuLoadLevel(std::string_view level) { m_levelFlow.chooseLevel(level); }

void StartUpFlow::playMovie(std::string_view name) {
    m_services.playMovie(name);
    // Movie_Play leaves both screen-effects managers fully black on return: the screen stays black until a fade in.
    m_fade.queue(graphics::ScreenFade::kFadeOut, 0.0, m_scripts.now());
}

void StartUpFlow::playMusic(std::string_view track) { m_services.playMusic(track); }

void StartUpFlow::stopMusic() { m_services.stopMusic(); }

void StartUpFlow::queueScreenEffect(int type, double seconds) { m_fade.queue(type, seconds, m_scripts.now()); }

void StartUpFlow::launchMissionComplete(int kind) { m_missionComplete.launch(kind); }

void StartUpFlow::startLoadSequence() { m_memoryCard.startLoadSequence(); }

void StartUpFlow::startDeleteSequence() { m_memoryCard.startDeleteSequence(); }

void StartUpFlow::launchMissionFailed(std::string_view reason) { m_missionFailed.launch(reason); }

void StartUpFlow::showRumbleModeIntro(std::string_view onDone, std::span<const std::string> names) {
    m_rumbleIntro.show(onDone, names);
}

void StartUpFlow::launchRumbleWin(std::string_view winner, std::string_view reason) {
    m_rumbleResult.launch(winner, reason);
}

void StartUpFlow::setPauseHooks(const PauseHooks& hooks) {
    m_pause.setHooks(hooks);
    m_missionFailed.setHooks(hooks);
}

std::expected<graphics::SpriteSheet, Error> StartUpFlow::loadRecord(std::uint32_t record) const {
    if (!m_loadRecord) {
        return fail(ErrorCode::NotFound, "no sheet-table loader");
    }
    return m_loadRecord(record);
}

} // namespace coney
