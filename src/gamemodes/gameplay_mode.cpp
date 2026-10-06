// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/gameplay_mode.h"

#include <array>
#include <format>
#include <utility>
#include <vector>

#include "core/game_timer.h"
#include "scripting/anim_callbacks.h"

namespace coney {

GameplayMode::GameplayMode(graphics::RenderDevice& device, script::ScriptSystem& scripts,
                           script::BindingContext& context, GameState& state, CreatedHumans& humans,
                           world_objects::WorldFlags& flags, const script::RecordedCalls& recorded, LevelLoader loader,
                           std::function<void(std::string_view)> log)
    : m_device(device), m_scripts(scripts), m_context(context), m_state(state), m_humans(humans), m_flags(flags),
      m_recorded(recorded), m_loader(std::move(loader)), m_log(std::move(log)) {}

GameplayMode::~GameplayMode() { endLevel(); }

void GameplayMode::endLevel() {
    // The level first: its humans are what the brains refer to. Then the scripts' hold, then the brains it holds.
    m_level.reset();
    if (m_context.ai == m_scripted.get()) {
        m_context.ai = nullptr;
    }
    m_scripted.reset();
    m_brains.reset();
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
    // The last level's objects are gone, and their handlers and boxes with them.
    if (m_context.messages != nullptr) {
        m_context.messages->clear();
    }
    if (m_context.boxes != nullptr) {
        m_context.boxes->clear();
    }
    m_context.ai = m_scripted.get();

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
                                             .scripted = m_scripted.get()});
    }
    if (!level) {
        m_log(std::format("gameplay: {}: {}\n", start.level, level.error().message));
        return;
    }
    m_level = std::move(*level);
    m_level->enter();

    // InitLevel step 12, after the preload: the intro movie (`L99_IN` for level99 at checkpoint 1).
    if (const LevelRecord* record = m_state.levels.at(m_state.currentLevel);
        record != nullptr && m_moviePlayer != nullptr) {
        if (const std::optional<std::string> movie = levelIntroMovie(*record, m_state.checkPoint)) {
            m_moviePlayer->playMovie(*movie);
        }
    }
}

ModeResult GameplayMode::update(GameModeStack& stack, const FrameTime& frame) {
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
    }
    updateBoxes(nowMs);
    m_scripts.update(nowMs, frame.seconds);

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
    if (m_level) {
        m_level->render(time);
        return;
    }
    m_device.beginFrame(graphics::kBlack);
    m_device.present();
}

void GameplayMode::exit() {
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
