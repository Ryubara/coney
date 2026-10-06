// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/pause_mode.h"

#include <format>
#include <utility>

#include "core/game_timer.h"
#include "core/pad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_flow_mode.h"
#include "graphics/screen_fade.h"
#include "gui/text_layout.h"

namespace coney {

namespace {

// Loads font `name` through `loadSheet`; logs and returns nothing when it fails.
std::optional<graphics::Font> loadFont(const PauseSheetLoader& loadSheet, std::string_view name,
                                       const std::function<void(std::string_view)>& log, std::string_view who) {
    if (!loadSheet) {
        return std::nullopt;
    }
    auto sheet = loadSheet(name);
    auto font = sheet ? graphics::Font::fromSheet(std::move(*sheet))
                      : std::expected<graphics::Font, Error>(std::unexpected(std::move(sheet.error())));
    if (!font) {
        log(std::format("{}: {}: {}\n", who, name, font.error().message));
        return std::nullopt;
    }
    return std::move(*font);
}

// The game time of `frame` in milliseconds.
std::uint64_t millisecondsOf(const FrameTime& frame) { return frame.gameTicks / (GameTimer::kTicksPerSecond / 1000); }

} // namespace

void MenuLayer::load(const PauseSheetLoader& loadSheet, const PauseRecordLoader& loadRecord,
                     std::optional<std::uint32_t> record, std::size_t capacity, float depth,
                     const std::function<void(std::string_view)>& log, std::string_view who) {
    release();
    if (std::optional<graphics::Font> font = loadFont(loadSheet, gui::kTextFontSheet, log, who)) {
        m_textBatch.emplace(font->sheet(), kTextCapacity, kTextDepth);
        m_textFont = std::move(font);
    }
    if (std::optional<graphics::Font> font = loadFont(loadSheet, gui::kBigFontSheet, log, who)) {
        m_bigBatch.emplace(font->sheet(), kTextCapacity, kTextDepth);
        m_bigFont = std::move(font);
    }
    if (record && loadRecord) {
        if (auto sheet = loadRecord(*record); sheet) {
            m_sprites.emplace(std::move(*sheet), capacity, depth);
        } else {
            log(std::format("{}: sheet record {}: {}\n", who, *record, sheet.error().message));
        }
    }
}

void MenuLayer::release() {
    // The queue points at the batches released below.
    m_pass.empty();
    m_sprites.reset();
    m_textBatch.reset();
    m_bigBatch.reset();
    m_textFont.reset();
    m_bigFont.reset();
}

void MenuLayer::queue() {
    for (std::optional<graphics::SpriteBatch>* batch : {&m_sprites, &m_textBatch, &m_bigBatch}) {
        if (*batch) {
            m_pass.queue(**batch);
        }
    }
}

void applyPauseOutcome(gui::PauseOutcome outcome, GameModeStack& stack, GameState& state, LevelFlowMode& levelFlow,
                       FrontEndServices& services, const std::function<void(std::string_view)>& log) {
    using gui::PauseOutcome;
    // The current level's record name, which a restart reloads (`W_GameState + 0x124`).
    const LevelRecord* record = state.levels.at(state.currentLevel);
    const std::string level = record != nullptr ? record->name : std::string();
    switch (outcome) {
    case PauseOutcome::Resume:
        return;
    case PauseOutcome::RestartLevel:
        state.checkPoint = 1;
        levelFlow.chooseLevel(level);
        break;
    case PauseOutcome::RestartCheckpoint:
        levelFlow.chooseLevel(level);
        break;
    case PauseOutcome::QuitToHangout: {
        constexpr std::array<double, 1> kArgs{0.0};
        services.callScript("runNextMission", kArgs);
        break;
    }
    case PauseOutcome::QuitToRumbleQuick:
        services.callScript("PauseGoToRMIQuick");
        break;
    case PauseOutcome::QuitToRumbleHangout:
        services.callScript("PauseGoToRMIHangout");
        break;
    case PauseOutcome::QuitToMainMenu:
        state.checkPoint = 1;
        levelFlow.chooseLevel("menu");
        break;
    }
    log(std::format("pause: outcome {}, level {}, checkpoint {}\n", static_cast<int>(outcome), level,
                    state.checkPoint));
    // The level is left: the level flow below starts the chosen level, or the front end.
    if (stack.topId() == GameplayMode::kId) {
        stack.pop();
    }
}

PauseMode::PauseMode(graphics::RenderDevice& device, PauseSheetLoader loadSheet, PauseRecordLoader loadRecord,
                     const gui::GlobalStrings& strings, FrontEndServices& services, GameState& state,
                     LevelFlowMode& levelFlow, std::function<void(std::string_view)> log)
    : m_device(device), m_loadSheet(std::move(loadSheet)), m_loadRecord(std::move(loadRecord)), m_strings(strings),
      m_services(services), m_state(state), m_levelFlow(levelFlow), m_log(std::move(log)) {
    m_menu.setStrings(&m_strings);
    m_menu.setSoundSink([this](int cue) { m_services.playCue(cue); });
}

void PauseMode::playFrame(GameModeStack& stack, const Pads& pads) {
    // START is checked before the cool-down counts down, so it is ignored for one frame after a close.
    for (std::size_t port = 0; port < 2; ++port) {
        const Pad& pad = pads.port(port);
        if (pad.connected() && pad.pressed(pad::kStart) && toggle(stack, static_cast<int>(port))) {
            break;
        }
    }
    if (m_cooldown > 0) {
        --m_cooldown;
    }
}

bool PauseMode::toggle(GameModeStack& stack, int player) {
    if (m_cooldown != 0) {
        return false;
    }
    if (stack.topId() != kId) {
        m_player = player;
        ++m_pauses;
        m_log(std::format("pause: player {} paused\n", player + 1));
        stack.push(*this);
        return true;
    }
    // The menu's outcome, read before the pop's exit.
    const gui::PauseOutcome outcome = m_menu.outcome();
    stack.pop();
    m_cooldown = 1;
    applyPauseOutcome(outcome, stack, m_state, m_levelFlow, m_services, m_log);
    return true;
}

void PauseMode::enter() {
    // The game's updates stop with gameplay below this mode; the sound pauses and the pause bank comes in.
    if (m_hooks.pauseSound) {
        m_hooks.pauseSound(true);
    }
    m_previousBank = m_services.bank();
    m_services.loadBank(kSoundBank);
    m_layer.load(m_loadSheet, m_loadRecord, kBackgroundRecord, kBackgroundCapacity, gui::PauseMenu::kBackgroundDepth,
                 m_log, "pause");
    m_menu.setBackgroundBatch(m_layer.sprites());
    if (m_hooks.radarsOff) {
        m_hooks.radarsOff();
    }
    m_tint.reset(0.0F);
    m_openPending = true;
}

ModeResult PauseMode::update(GameModeStack& stack, const FrameTime& frame) {
    const std::uint64_t nowMs = millisecondsOf(frame);
    // The menu opens on the first frame, which knows the time; the world is drawn as the game left it.
    if (m_openPending) {
        m_openPending = false;
        m_frozen = RenderTime{.alpha = 1.0F, .gameTicks = frame.gameTicks - frame.stepTicks, .index = 0};
        gui::PauseMenuSetup setup;
        if (const LevelRecord* record = m_state.levels.at(m_state.currentLevel)) {
            setup.level = static_cast<int>(record->number);
        }
        if (m_hooks.objectives) {
            setup.objectives = m_hooks.objectives();
        }
        m_menu.open(setup, nowMs);
    }

    m_tint.commit();
    m_layer.begin();
    m_menu.update(gui::GuiFrame{nowMs, &stack.pads().port(static_cast<std::size_t>(m_player))});
    m_menu.render(m_layer.canvas());
    m_layer.queue();
    m_tint.current() = m_menu.worldTint(nowMs);

    // The menu has faded out: pop and act on its outcome.
    if (m_menu.closed()) {
        (void)toggle(stack, m_player);
    }
    return ModeResult::Stay;
}

void PauseMode::render(const RenderTime& time) {
    const float tint = lerp(m_tint.previous(), m_tint.current(), time.alpha);
    const std::function<void(graphics::RenderDevice&)> overlay = [this, tint](graphics::RenderDevice& device) {
        graphics::ScreenFade::draw(device, tint);
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

void PauseMode::exit() {
    // The sound state as it was: the bank, then the paused sounds.
    if (!m_previousBank.empty() && m_previousBank != kSoundBank) {
        m_services.loadBank(m_previousBank);
    }
    if (m_hooks.radarsBack) {
        m_hooks.radarsBack();
    }
    if (m_hooks.pauseSound) {
        m_hooks.pauseSound(false);
    }
    m_menu.setBackgroundBatch(nullptr);
    m_layer.release();
}

} // namespace coney
