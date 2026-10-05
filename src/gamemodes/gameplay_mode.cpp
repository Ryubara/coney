// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/gameplay_mode.h"

#include <array>
#include <format>
#include <utility>

#include "core/game_timer.h"

namespace coney {

GameplayMode::GameplayMode(graphics::RenderDevice& device, script::ScriptSystem& scripts, const GameState& state,
                           CreatedHumans& humans, LevelLoader loader, std::function<void(std::string_view)> log)
    : m_device(device), m_scripts(scripts), m_state(state), m_humans(humans), m_loader(std::move(loader)),
      m_log(std::move(log)) {}

void GameplayMode::enter() {
    // InitLevel's script step: the level script creates player 1 at the checkpoint's start, before anything streams.
    m_level.reset();
    const LevelStart& start = m_start.emplace(runLevelScript(m_scripts, m_state, m_humans, m_levelName));
    const HumanCreation* player = start.player ? &*start.player : nullptr;
    if (player != nullptr && player->position) {
        const std::array<float, 3>& p = *player->position;
        m_log(
            std::format("gameplay: {} checkpoint {}: player 1 {} (type {}) at ({:.2f}, {:.2f}, {:.2f}) heading {:.0f}, "
                        "from the level script; {} humans created\n",
                        start.level, start.checkpoint, player->name, player->type, p[0], p[1], p[2],
                        player->headingDegrees, m_humans.all().size()));
    } else {
        m_log(std::format("gameplay: {} checkpoint {}: the level script made no player 1 with a position\n",
                          start.level, start.checkpoint));
    }

    // The level itself, with the player at that start; entering it preloads the world around him.
    std::expected<std::unique_ptr<GameMode>, Error> level = fail(ErrorCode::NotFound, "no level loader");
    if (m_loader) {
        level = m_loader(start);
    }
    if (!level) {
        m_log(std::format("gameplay: {}: {}\n", start.level, level.error().message));
        return;
    }
    m_level = std::move(*level);
    m_level->enter();
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
    m_scripts.update(nowMs, frame.seconds);
    return result;
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
        m_level.reset();
    }
    m_humans.clear();
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
