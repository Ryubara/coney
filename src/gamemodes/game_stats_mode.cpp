// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/game_stats_mode.h"

#include <format>
#include <utility>

#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "gui/text_layout.h"

namespace coney {

GameStatsMode::GameStatsMode(graphics::RenderDevice& device, PauseSheetLoader loadSheet, PauseRecordLoader loadRecord,
                             const gui::GlobalStrings& strings, FrontEndServices& services, graphics::ScreenFade& fade,
                             GameState& state, script::ScriptSystem& scripts, GameModeStack& stack,
                             std::function<void(std::string_view)> log)
    : m_device(device), m_loadSheet(std::move(loadSheet)), m_loadRecord(std::move(loadRecord)), m_strings(strings),
      m_services(services), m_fade(fade), m_state(state), m_scripts(scripts), m_stack(stack), m_log(std::move(log)) {
    m_screen.setStrings(&m_strings);
    m_screen.setSoundSink([this](int cue) { m_services.playCue(cue); });
}

void GameStatsMode::launch(std::string_view onClose) {
    m_onClose = onClose;
    m_log(std::format("game stats: shown, onClose \"{}\"\n", onClose));
    if (m_stack.topId() != kId) {
        m_stack.push(*this);
    }
}

void GameStatsMode::enter() {
    using Screen = gui::GameStatsScreen;
    // Coney's stand-in: the task manager's phase and the store tint (a clear colour) are not set; nothing they change
    // shows under the screen. No fade out yet; the fade in waits for the screen. The interface bank, unless it is
    // already in sound RAM.
    m_fadingOut = false;
    m_fadeInPending = true;
    if (!m_services.bankLoaded(kSoundBank)) {
        m_services.loadBank(kSoundBank);
    }
    // The four batches of `GameStats_Init`: the background, the headers (their backs and circles, and the specials'
    // pictures, the same record) and the two picture sheets.
    m_layer.load(m_loadSheet, m_loadRecord, Screen::kBackgroundRecord, 1, Screen::kBackgroundDepth, m_log,
                 "game stats");
    gui::GameStatsBatches batches;
    batches.background = m_layer.sprites();
    batches.header = m_layer.loadExtra(m_loadSheet, m_loadRecord, {}, Screen::kHeaderRecord, kHeaderCapacity,
                                       Screen::kHeaderDepth, m_log, "game stats");
    batches.thumbsA = m_layer.loadExtra(m_loadSheet, m_loadRecord, {}, Screen::kThumbsARecord, kThumbCapacity,
                                        Screen::kThumbDepth, m_log, "game stats");
    batches.thumbsB = m_layer.loadExtra(m_loadSheet, m_loadRecord, {}, Screen::kThumbsBRecord, kThumbCapacity,
                                        Screen::kThumbDepth, m_log, "game stats");
    m_screen.setBatches(batches);
    m_openPending = true;
}

ModeResult GameStatsMode::update(GameModeStack& stack, const FrameTime& frame) {
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    // Coney's stand-in: no world frame (the original runs one, unseen under the screen); the fade's step.
    m_fade.update(nowMs);
    if (m_openPending) {
        m_openPending = false;
        gui::GameStatsData data =
            gui::gatherGameStats(m_state.levels, m_state.player.unlocks, m_state.saved, &m_strings);
        m_log(std::format("game stats: {} missions listed, {} shown\n", data.missionsListed, data.levels.size()));
        m_screen.open(std::move(data), nowMs);
    }
    // The fade in, once, on the frame the screen is ready.
    if (m_fadeInPending && m_screen.isOpen()) {
        m_fadeInPending = false;
        m_fade.queue(graphics::ScreenFade::kFadeIn, kFadeSeconds, nowMs);
    }
    m_layer.begin();
    m_screen.update(gui::GuiFrame{nowMs, &stack.pads().port(0)});
    m_screen.render(m_layer.canvas());
    m_layer.queue();
    // Back closed the screen: a fade out is queued once, and the mode leaves on the first frame it is over.
    if (m_screen.closed()) {
        if (!m_fadingOut) {
            m_fadingOut = true;
            m_fade.queue(graphics::ScreenFade::kFadeOut, kFadeSeconds, nowMs);
        } else if (!m_fade.running()) {
            stack.pop();
        }
    }
    return ModeResult::Stay;
}

void GameStatsMode::render(const RenderTime& /*time*/) {
    m_device.beginFrame(graphics::kBlack);
    m_layer.draw(m_device);
    graphics::ScreenFade::draw(m_device, m_fade.level());
    m_device.present();
}

void GameStatsMode::exit() {
    const bool closed = m_screen.closed();
    m_screen.close();
    m_layer.begin();
    m_layer.release();
    if (closed && !m_onClose.empty()) {
        m_log(std::format("game stats: closed, calling {}\n", m_onClose));
        (void)m_scripts.call(m_onClose);
    }
}

} // namespace coney
