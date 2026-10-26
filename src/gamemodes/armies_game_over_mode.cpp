// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/armies_game_over_mode.h"

#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <utility>

#include "core/game_timer.h"
#include "core/pad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_flow_mode.h"
#include "hud/hud.h"

namespace coney {

namespace {

// The text batches only: the screen has no sprite sheet.
constexpr std::size_t kNoSprites = 0;

} // namespace

ArmiesGameOverMode::ArmiesGameOverMode(graphics::RenderDevice& device, PauseSheetLoader loadSheet,
                                       PauseRecordLoader loadRecord, const gui::GlobalStrings& strings,
                                       FrontEndServices& services, GameState& state, LevelFlowMode& levelFlow,
                                       GameModeStack& stack, hud::Hud* hud, std::function<void(std::string_view)> log)
    : m_device(device), m_loadSheet(std::move(loadSheet)), m_loadRecord(std::move(loadRecord)), m_strings(strings),
      m_services(services), m_state(state), m_levelFlow(levelFlow), m_stack(stack), m_hud(hud), m_log(std::move(log)) {
    m_menu.setStrings(&m_strings);
    m_menu.setSoundSink([this](int cue) { m_services.playCue(cue); });
}

void ArmiesGameOverMode::launch() {
    // A second call while the screen is up toggles it off, with nothing chosen.
    if (m_stack.topId() == kId) {
        m_log("armies: game over closed\n");
        m_stack.pop();
        return;
    }
    m_log("armies: game over\n");
    m_stack.push(*this);
}

void ArmiesGameOverMode::enter() {
    // **Coney's stand-in**: the original also sets the task manager's phase 1 and pauses the audio (Gm_Mode0D_Enter);
    // Coney holds the level below still under the screen (update()) and leaves the sound playing.
    m_layer.load(m_loadSheet, m_loadRecord, std::nullopt, kNoSprites, 0.0F, m_log, "armies game over");
    m_fadeLevel.reset(0.0F);
    m_fade = graphics::ScreenFade{};
    m_continuing = {};
    m_fading = false;
    m_openPending = true;
}

bool ArmiesGameOverMode::playerIn(int player) const {
    if (m_hooks.playerIn) {
        return m_hooks.playerIn(player);
    }
    return player == 0;
}

bool ArmiesGameOverMode::playerUp(int player) const {
    if (m_hooks.playerUp) {
        return m_hooks.playerUp(player);
    }
    return player == 0;
}

void ArmiesGameOverMode::pressStart(int player, std::uint64_t nowMs) {
    if (m_menu.ending() || m_menu.choice()) {
        return;
    }
    // The one who pressed plays on, and the other when he is in and standing.
    const int other = 1 - player;
    m_continuing.at(static_cast<std::size_t>(player)) = true;
    m_continuing.at(static_cast<std::size_t>(other)) = playerUp(other);
    const std::uint32_t credits = m_hud != nullptr ? m_hud->armies().credits() : 0;
    m_menu.pressStart(credits > 0, nowMs);
    if (credits == 0) {
        m_continuing = {};
        return;
    }
    // A credit spent and the stage's timer started again; while credits remain, both join messages come on.
    m_hud->armies().setCredits(credits - 1);
    m_services.callScript(kContinueFunction);
    if (credits - 1 > 0) {
        m_hud->armies().setJoinMessage(0, true);
        m_hud->armies().setJoinMessage(1, true);
    }
}

ModeResult ArmiesGameOverMode::update(GameModeStack& stack, const FrameTime& frame) {
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    // **Coney's stand-in**: the original runs one frame of the world's phase-1 tasks, its streaming and its audio
    // under the screen each update (Gm_ANGameOver_Update); Coney draws the level's last frame (m_frozen) instead.
    // The screen opens on the first frame, which knows the time.
    if (m_openPending) {
        m_openPending = false;
        m_frozen = RenderTime{.alpha = 1.0F, .gameTicks = frame.gameTicks - frame.stepTicks, .index = 0};
        m_menu.open(nowMs);
    }
    // Start from player 1, else player 2 when he is in the game, in the countdown. **Coney's stand-in**: the
    // original also ignores the Start of a player whose brain `+9` is set in `level64` section 4; what `+9` is has not
    // been traced, so Coney takes every Start there.
    for (std::size_t port = 0; port < 2; ++port) {
        const Pad& pad = stack.pads().port(port);
        if (port == 1 && !playerIn(1)) {
            continue;
        }
        if (pad.connected() && (pad.pressedWithRepeat() & pad::kStart) != 0) {
            pressStart(static_cast<int>(port), nowMs);
            break;
        }
    }
    m_menu.update(gui::GuiFrame{nowMs, &stack.pads().port(0)});
    // The end phase fades the world out.
    if (m_menu.ending() && !m_fading) {
        m_fading = true;
        m_fade.queue(graphics::ScreenFade::kFadeOut, kEndFadeSeconds, nowMs);
    }
    m_fadeLevel.commit();
    m_fade.update(nowMs);
    m_fadeLevel.current() = m_fade.level();
    m_layer.begin();
    m_menu.render(m_layer.canvas());
    m_layer.queue();
    // **Coney's stand-in**: the screen closes on the frame of the choice, where the original waits for the menu's last
    // sound cue to end (GameMenu_IsSoundDone, `0x001d1d08`), which Coney's front-end audio cannot report.
    if (const std::optional<gui::ArmiesGameOverChoice> choice = m_menu.choice()) {
        close(stack, *choice);
    }
    return ModeResult::Stay;
}

void ArmiesGameOverMode::close(GameModeStack& stack, gui::ArmiesGameOverChoice choice) {
    stack.pop();
    using gui::ArmiesGameOverChoice;
    switch (choice) {
    case ArmiesGameOverChoice::Continue:
        // The continuing players are healed and stood up, their join messages off.
        for (std::size_t player = 0; player < m_continuing.size(); ++player) {
            if (!m_continuing.at(player)) {
                continue;
            }
            if (m_hooks.healPlayer) {
                m_hooks.healPlayer(static_cast<int>(player));
            }
            // The original also shows his ANHud panel again (slot `+0x24`, ANHud_Enable `0x0020fd40`); Coney's Armies
            // panels are never hidden, so there is nothing to show.
            if (m_hud != nullptr) {
                m_hud->armies().setJoinMessage(player, false);
            }
        }
        m_log(std::format("armies: continue, players {} {}, credits left {}\n", m_continuing.at(0) ? 1 : 0,
                          m_continuing.at(1) ? 1 : 0, m_hud != nullptr ? m_hud->armies().credits() : 0));
        return;
    case ArmiesGameOverChoice::Replay:
        m_log("armies: replay\n");
        m_state.checkPoint = 1;
        m_levelFlow.chooseLevel(std::string(kReplayLevel));
        break;
    case ArmiesGameOverChoice::Quit: {
        m_log("armies: quit\n");
        constexpr std::array<double, 1> kArgs{0.0};
        m_services.callScript(kQuitFunction, kArgs);
        break;
    }
    }
    // The level is left: the level flow below starts the chosen level.
    if (stack.topId() == GameplayMode::kId) {
        stack.pop();
    }
}

void ArmiesGameOverMode::render(const RenderTime& time) {
    const float level = lerp(m_fadeLevel.previous(), m_fadeLevel.current(), time.alpha);
    const std::function<void(graphics::RenderDevice&)> overlay = [this, level](graphics::RenderDevice& device) {
        graphics::ScreenFade::draw(device, level);
        m_layer.draw(device);
    };
    if (m_world != nullptr) {
        m_world->renderWithOverlay(m_frozen, overlay);
        return;
    }
    m_device.beginFrame(graphics::kBlack);
    overlay(m_device);
    m_device.present();
}

void ArmiesGameOverMode::exit() {
    // **Coney's stand-in**: the original closes the menu, finishes the tasks the screen held, resumes the audio and
    // returns the game timer to its variable step (Gm_ANGameOver_Exit); Coney's level below held no tasks, its sound
    // was not paused and its timer always steps fixed, so only the screen's fonts are released.
    m_layer.release();
}

} // namespace coney
