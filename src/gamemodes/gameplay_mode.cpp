// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/gameplay_mode.h"

#include <array>
#include <format>
#include <optional>
#include <utility>
#include <vector>

#include "animation/anim_math.h"
#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/pause_mode.h"
#include "gamemodes/player_frame.h"
#include "scripting/anim_callbacks.h"
#include "scripting/object_bindings.h"

namespace coney {

GameplayMode::GameplayMode(graphics::RenderDevice& device, script::ScriptSystem& scripts,
                           script::BindingContext& context, GameState& state, CreatedHumans& humans,
                           world_objects::WorldFlags& flags, const script::RecordedCalls& recorded, LevelLoader loader,
                           std::function<void(std::string_view)> log)
    : m_device(device), m_scripts(scripts), m_context(context), m_state(state), m_humans(humans), m_flags(flags),
      m_recorded(recorded), m_loader(std::move(loader)), m_log(std::move(log)),
      m_objectServices(scripts, flags, nullptr) {
    m_objectServices.setPlayers(&state, &humans);
}

GameplayMode::~GameplayMode() {
    endLevel();
    if (m_context.objects == &m_objects) {
        m_context.objects = nullptr;
    }
}

void GameplayMode::endLevel() {
    // The level first: its humans are what the brains refer to, its player what the cameras follow. Then the cameras,
    // whose locator reads the scripts' hold, then the hold, then the brains it holds.
    m_level.reset();
    if (m_context.cameras == m_cameras.get()) {
        m_context.cameras = nullptr;
    }
    m_cameras.reset();
    // The level's objects go with it, and with them the level's collision mesh and path data they pointed at.
    m_objects.clear();
    m_objects.world = world_objects::ObjectWorld{.services = &m_objectServices, .random = &m_state.random};
    if (m_context.effects == m_effects.get()) {
        m_context.effects = nullptr;
    }
    m_objectServices.setParticles(nullptr, {});
    m_effects.reset();
    if (m_context.ai == m_scripted.get()) {
        m_context.ai = nullptr;
    }
    m_scripted.reset();
    m_brains.reset();
    if (m_scenes && m_context.scenes == m_scenes.get()) {
        m_context.scenes = m_scenesBefore; // the front end's, say, again
    }
    m_scenes.reset();
    if (m_context.lighting == m_lighting.get()) {
        m_context.lighting = nullptr;
    }
    m_lighting.reset();
}

void GameplayMode::enter() {
    // The level's brains and gangs (InitLevel's AI reset), which its script's bindings drive. The calls on the humans
    // the script creates wait until the level has loaded its characters and made them.
    endLevel();
    m_brains = std::make_unique<ai::Brains>();
    m_scripted = std::make_unique<ai::ScriptedBrains>(*m_brains, m_flags,
                                                      [this](double handle) { return m_humans.placement(handle); });
    m_scripted->setScripts(&m_scripts);
    m_scripted->setMessages(m_context.messages);
    if (m_context.animCallbacks != nullptr) {
        m_context.animCallbacks->clear();
    }
    m_scripted->setAnimCallbacks(m_context.animCallbacks);
    m_scripted->hold();
    // The level's scenes, which the script preloads and plays; their end functions and preload callbacks call it.
    if (m_sceneMaker) {
        m_scenes = m_sceneMaker();
    }
    if (m_scenes) {
        m_scenes->setScriptCall([this](std::string_view function, std::span<const double> args) {
            std::vector<script::Value> values(args.begin(), args.end());
            m_scripts.call(function, values);
        });
        m_scenesBefore = m_context.scenes;
        m_context.scenes = m_scenes.get();
    }
    // The characters' rules the configuration set (the rage handlers, the formations' default slots).
    if (m_context.state != nullptr) {
        m_scripted->humanHost().applyRules(m_context.state->characters);
    }
    // The last level's objects are gone, and their handlers and boxes with them.
    if (m_context.messages != nullptr) {
        m_context.messages->clear();
    }
    if (m_context.boxes != nullptr) {
        m_context.boxes->clear();
    }
    m_context.ai = m_scripted.get();
    // Player 1's cameras, which the script sets up before the level makes him; CamSetSecondary finds its human live.
    m_cameras = std::make_unique<camera::Cameras>();
    m_cameras->setLocator([scripted = m_scripted.get()](double handle) -> std::optional<anim::Vec3> {
        const std::optional<world_objects::Placement> placement = scripted->humanPlacement(handle);
        if (!placement) {
            return std::nullopt;
        }
        return anim::Vec3{placement->position[0], placement->position[1], placement->position[2]};
    });
    m_context.cameras = m_cameras.get();
    // The level script spawns the level's panes and doors into gameplay's objects, typed by what the boot scripts'
    // `CfgSetGlassProperties` calls recorded.
    script::applyRecordedGlassTypes(m_recorded, m_objects.glass);
    m_context.objects = &m_objects;
    // A fresh light manager and fog for the level, which its scripts' SetLight and SetFogColor fill.
    m_lighting = std::make_unique<graphics::LevelLighting>();
    m_context.lighting = m_lighting.get();
    // The level's particles and motion blur; an attached particle system follows a human the scripts made.
    m_effects = std::make_unique<effects::LevelEffects>();
    m_effects->particles.setLocator([scripted = m_scripted.get()](double handle) -> std::optional<anim::Vec3> {
        const std::optional<world_objects::Placement> placement = scripted->humanPlacement(handle);
        if (!placement) {
            return std::nullopt;
        }
        return anim::Vec3{placement->position[0], placement->position[1], placement->position[2]};
    });
    m_context.effects = m_effects.get();
    // The panes' shards and the objects' dust go to the level's particles, culled round player 1.
    m_objectServices.setParticles(&m_effects->particles, [this]() -> std::optional<anim::Vec3> {
        const HumanCreation* player = m_humans.player(1);
        const std::optional<world_objects::Placement> placement =
            player != nullptr && m_scripted ? m_scripted->humanPlacement(player->handle) : std::nullopt;
        if (!placement) {
            return std::nullopt;
        }
        return anim::Vec3{placement->position[0], placement->position[1], placement->position[2]};
    });

    // With a loading screen the level loads once it has faded in (updateLoadingScreen()); begun by the first update,
    // which knows the time.
    m_screenStartMs.reset();
    if (m_loadingScreen != nullptr) {
        m_phase = Phase::FadeIn;
        return;
    }
    m_phase = Phase::Playing;
    loadLevel();
    startPlay();
}

void GameplayMode::loadLevel() {
    startPlayerLevel(m_state);
    // InitLevel's script step: the level script creates player 1 at the checkpoint's start, before anything streams.
    const LevelStart& start = m_start.emplace(runLevelScript(m_scripts, m_state, m_humans, m_flags, m_levelName));
    const HumanCreation* player = start.player ? &*start.player : nullptr;
    m_playerTeleports = player != nullptr ? player->teleports : 0;
    if (player != nullptr) {
        const std::array<float, 3> p =
            player->teleported ? player->teleported->position : player->position.value_or(std::array<float, 3>{});
        const float heading = player->teleported ? player->teleported->headingDegrees : player->headingDegrees;
        m_log(std::format("gameplay: {} checkpoint {}: player 1 {} (type {}, model {}) at ({:.2f}, {:.2f}, {:.2f}) "
                          "heading {:.0f}, {} the level script; {} humans, {} flags\n",
                          start.level, start.checkpoint, player->name, player->type,
                          player->model.empty() ? "unknown" : player->model, p[0], p[1], p[2], heading,
                          player->teleported ? "teleported to a flag by" : "created by", m_humans.all().size(),
                          m_flags.all().size()));
    } else {
        m_log(std::format("gameplay: {} checkpoint {}: the level script made no player 1 with a position\n",
                          start.level, start.checkpoint));
    }

    // The level itself, with the player at that start; entering it preloads the world around him.
    std::expected<std::unique_ptr<GameMode>, Error> level = fail(ErrorCode::NotFound, "no level loader");
    if (m_loader) {
        level = m_loader(start, ScriptedCast{.humans = &m_humans,
                                             .recorded = &m_recorded,
                                             .brains = m_brains.get(),
                                             .scripted = m_scripted.get(),
                                             .cameras = m_cameras.get(),
                                             .scenes = m_scenes.get(),
                                             .objects = &m_objects,
                                             .lighting = m_lighting.get(),
                                             .effects = m_effects.get()});
    }
    if (!level) {
        m_log(std::format("gameplay: {}: {}\n", start.level, level.error().message));
        return;
    }
    m_level = std::move(*level);
    m_level->enter();
}

void GameplayMode::startPlay() {
    // A level that failed to load asks for no movie.
    if (!m_level) {
        return;
    }
    // InitLevel step 12, after the preload: the intro movie (`L99_IN` for level99 at checkpoint 1).
    if (const LevelRecord* record = m_state.levels.at(m_state.currentLevel);
        record != nullptr && m_moviePlayer != nullptr) {
        if (const std::optional<std::string> movie = levelIntroMovie(*record, m_state.checkPoint)) {
            m_moviePlayer->playMovie(*movie);
        }
    }
}

bool GameplayMode::updateLoadingScreen(const FrameTime& frame) {
    LoadingScreen& screen = *m_loadingScreen;
    const std::uint64_t msPerTick = GameTimer::kTicksPerSecond / 1000;
    const std::uint64_t nowMs = frame.gameTicks / msPerTick;
    if (!m_screenStartMs) {
        // LoadScreen_Begin (InitLevel step 2), at the moment the mode was entered: just before this first step.
        m_screenStartMs = (frame.gameTicks - frame.stepTicks) / msPerTick;
        const LevelRecord* record = m_state.levels.at(m_state.currentLevel);
        const int number = record != nullptr ? static_cast<int>(record->number) : 0;
        const int gameType = m_state.rumble.values.at(RumbleSetup::kGameType);
        screen.begin(m_levelName, number, gameType, *m_screenStartMs);
    }
    if (m_phase == Phase::FadeIn && nowMs >= *m_screenStartMs + LoadScreenTimeline::kFadeMilliseconds) {
        // Faded in: the load-screen sounds (step 3), then the whole load in this one step (steps 4-10).
        screen.startSounds();
        loadLevel();
        m_phase = Phase::Loading;
    }
    if (m_phase == Phase::Loading && nowMs >= *m_screenStartMs + kLoadScreenHoldMilliseconds) {
        // Step 11: the sounds stop, then LoadScreen_End's finish fades the screen out.
        screen.stopSounds();
        screen.finish(nowMs);
        m_phase = Phase::FadeOut;
    }
    if (m_phase == Phase::FadeOut && screen.finished(nowMs)) {
        // The fade out is over: the next frame cuts to the movie and then play.
        screen.end();
        m_phase = Phase::Playing;
        startPlay();
        return true;
    }
    return false;
}

ModeResult GameplayMode::update(GameModeStack& stack, const FrameTime& frame) {
    // Behind the loading screen nothing plays and the pads are not read.
    if (m_phase != Phase::Playing && m_loadingScreen != nullptr && !updateLoadingScreen(frame)) {
        return ModeResult::Stay;
    }
    // The level's step (the characters, the cameras, the streaming), then the scripts' frame, as a frame of play
    // orders them.
    ModeResult result = ModeResult::Stay;
    if (m_level) {
        result = m_level->update(stack, frame);
    }
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    m_scripts.setTime(nowMs);
    if (m_scripted) {
        m_scripted->runAnimCallbacks();
        m_scripted->humanHost().runRageHandlers();
    }
    updateBoxes(nowMs);
    runPlayerFrame(m_state, m_scripts, stack.pads(), nowMs, &m_objectServices.crimeServices());
    m_scripts.update(nowMs, frame.seconds);
    if (m_effects) {
        m_effects->step(static_cast<float>(frame.seconds));
    }

    // A script that teleported player 1 during the frame (the hub's door walk) moves him in the level.
    if (const HumanCreation* player = m_humans.player(1);
        player != nullptr && player->teleported && player->teleports != m_playerTeleports) {
        m_playerTeleports = player->teleports;
        const world_objects::Placement& to = *player->teleported;
        m_log(std::format("gameplay: player 1 teleported to ({:.2f}, {:.2f}, {:.2f}) heading {:.0f}\n", to.position[0],
                          to.position[1], to.position[2], to.headingDegrees));
        if (auto* scripted = dynamic_cast<ScriptedPlayer*>(m_level.get())) {
            scripted->teleportPlayer(to);
        }
    }
    // START pauses the game (PauseMenu_Toggle), last in the frame of play.
    if (m_pause != nullptr) {
        m_pause->playFrame(stack, stack.pads());
    }
    return result;
}

void GameplayMode::updateBoxes(std::uint64_t nowMs) {
    if (m_context.boxes == nullptr || m_context.messages == nullptr || !m_scripted) {
        return;
    }
    const std::vector<world_objects::BoxSubject> subjects = m_scripted->boxSubjects();
    m_context.boxes->update(subjects, nowMs, [this](double box, int message, double human) {
        m_context.messages->deliver(m_scripts, box, message, human, 0.0, 0.0);
    });
}

void GameplayMode::render(const RenderTime& time) {
    // The loading screen's tick, on game time.
    if (m_phase != Phase::Playing && m_loadingScreen != nullptr) {
        m_loadingScreen->render(time.gameTicks / (GameTimer::kTicksPerSecond / 1000));
        return;
    }
    if (m_level) {
        m_level->render(time);
        return;
    }
    m_device.beginFrame(graphics::kBlack);
    m_device.present();
}

void GameplayMode::renderWithOverlay(const RenderTime& time,
                                     const std::function<void(graphics::RenderDevice&)>& overlay) {
    if (auto* overlaid = dynamic_cast<OverlaidLevel*>(m_level.get())) {
        overlaid->renderWithOverlay(time, overlay);
        return;
    }
    if (m_level) {
        m_level->render(time);
        return;
    }
    m_device.beginFrame(graphics::kBlack);
    overlay(m_device);
    m_device.present();
}

void GameplayMode::exit() {
    // Left behind the loading screen: its sounds stop and it ends.
    if (m_loadingScreen != nullptr && m_phase != Phase::Playing) {
        m_loadingScreen->stopSounds();
        m_loadingScreen->end();
    }
    m_phase = Phase::Playing;
    if (m_level) {
        m_level->exit();
    }
    endLevel();
    m_humans.clear();
    m_flags.clear();
    if (m_context.messages != nullptr) {
        m_context.messages->clear();
    }
    if (m_context.boxes != nullptr) {
        m_context.boxes->clear();
    }
    // UnloadLevel destroys the script system and makes it again: the next level starts from the bindings alone.
    if (m_scripts.exists()) {
        m_scripts.create();
    }
}

void GameplayMode::suspend() {
    if (m_level) {
        m_level->suspend();
    }
}

void GameplayMode::resume() {
    if (m_level) {
        m_level->resume();
    }
}

} // namespace coney
