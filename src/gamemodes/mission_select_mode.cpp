// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/mission_select_mode.h"

#include <array>
#include <format>
#include <utility>
#include <vector>

#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "scripting/lua_value.h"

namespace coney {

MissionSelectMode::MissionSelectMode(graphics::RenderDevice& device, PauseSheetLoader loadSheet,
                                     PauseRecordLoader loadRecord, const gui::GlobalStrings& strings,
                                     FrontEndServices& services, graphics::ScreenFade& fade, GameState& state,
                                     script::ScriptSystem& scripts, GameModeStack& stack, bool europe,
                                     std::function<void(std::string_view)> log)
    : m_device(device), m_loadSheet(std::move(loadSheet)), m_loadRecord(std::move(loadRecord)), m_strings(strings),
      m_services(services), m_fade(fade), m_state(state), m_scripts(scripts), m_stack(stack), m_log(std::move(log)) {
    m_screen.setStrings(&m_strings);
    m_screen.setLayout(gui::MissionSelectLayout::forFlags(europe, false, false));
    m_screen.setSoundSink([this](int cue) { m_services.playCue(cue); });
}

void MissionSelectMode::launch(std::string_view onCancel, std::string_view onChoose) {
    m_log(std::format("mission select: shown, onCancel \"{}\", onChoose \"{}\"\n", onCancel, onChoose));
    if (m_stack.topId() != kId) {
        m_onCancel = onCancel;
        m_onChoose = onChoose;
        m_stack.push(*this);
    }
}

void MissionSelectMode::enter() {
    using Screen = gui::MissionSelectScreen;
    // Nothing chosen yet; the fade in waits for the screen. The interface bank, unless it is current. Coney's
    // stand-in: the task manager's phase is not set, as Coney runs no world frame under the screen.
    m_fadingOut = false;
    m_fadeInPending = true;
    m_choiceApplied = false;
    if (!m_services.bankLoaded(kSoundBank)) {
        m_services.loadBank(kSoundBank);
    }
    // The batches of `MissionSelect_Build`: the frame sheet (record 20, or 21 in the alternative layout) and the
    // gang logos.
    const std::uint32_t frameRecord =
        m_screen.layout().alternative ? Screen::kAlternativeFrameRecord : Screen::kFrameRecord;
    m_layer.load(m_loadSheet, m_loadRecord, frameRecord, kFrameCapacity, kFrameDepth, m_log, "mission select");
    gui::MissionSelectBatches batches;
    batches.frame = m_layer.sprites();
    batches.logos = m_layer.loadExtra(m_loadSheet, m_loadRecord, {}, Screen::kLogoRecord, kLogoCapacity, kLogoDepth,
                                      m_log, "mission select");
    m_screen.setBatches(batches);
    // The areas from the level table and the unlockables (`MissionSelect_Init`, on the next update's time).
    m_openPending = true;
}

void MissionSelectMode::resetPlayersForChoice() {
    // From the second checkpoint on: the inventories' callbacks and counts cleared and their checkpoint copy taken
    // (`Inventory_Reset(inv, 1)`), both players' statistics cleared (`Stats_ResetPlayers`) and theirs taken
    // (`Stats_SaveCheckpoint`). Coney keeps three of the four callback names the reset clears (`+0x1014` has no
    // field in Coney).
    PlayerState& players = m_state.player;
    players.pickupCallback.clear();
    players.huInventoryCallback.clear();
    players.moneyCallback.clear();
    players.inventory.clearCounts();
    players.stats.reset();
    players.saveCheckpoint();
}

ModeResult MissionSelectMode::update(GameModeStack& stack, const FrameTime& frame) {
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    // Coney's stand-in: no world frame (the original runs the level's world, cameras and audio on, unseen under the
    // map), and no fixed-step switch (Coney's game timer always steps fixed); the fade's step.
    m_fade.update(nowMs);
    if (m_openPending) {
        m_openPending = false;
        std::vector<gui::MissionSelectArea> areas = gui::buildMissionSelectAreas(
            m_state.levels, m_state.player.unlocks, m_state.saved, m_screen.layout().column);
        std::size_t missions = 0;
        for (const gui::MissionSelectArea& area : areas) {
            missions += area.missions.size();
        }
        m_log(std::format("mission select: {} areas, {} missions\n", areas.size(), missions));
        m_screen.open(std::move(areas), nowMs);
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
    // The choice from the second checkpoint on resets the players (`MissionSelect_OnAccept`), once.
    if (m_screen.chosen() && !m_choiceApplied) {
        m_choiceApplied = true;
        if (m_screen.chosenCheckpoint() > 1) {
            resetPlayersForChoice();
        }
    }
    // Done: the fade out once (after a choice the music stops and the fade is longer), and the pop on the first frame
    // it is over.
    if (m_screen.done()) {
        if (!m_fadingOut) {
            m_fadingOut = true;
            if (!m_screen.cancelled()) {
                m_services.stopMusic();
                m_fade.queue(graphics::ScreenFade::kFadeOut, kChooseFadeSeconds, nowMs);
            } else {
                m_fade.queue(graphics::ScreenFade::kFadeOut, kFadeSeconds, nowMs);
            }
        } else if (!m_fade.running()) {
            stack.pop();
        }
    }
    return ModeResult::Stay;
}

void MissionSelectMode::render(const RenderTime& /*time*/) {
    m_device.beginFrame(graphics::kBlack);
    m_layer.draw(m_device);
    graphics::ScreenFade::draw(m_device, m_fade.level());
    m_device.present();
}

void MissionSelectMode::exit() {
    const bool cancelled = m_screen.cancelled();
    const bool chosen = m_screen.chosen();
    const int level = m_screen.chosenLevel();
    const int checkpoint = m_screen.chosenCheckpoint();
    m_screen.close();
    m_layer.begin();
    m_layer.release();
    // A cancel with a callback: the `sound` bank, then `onCancel()`; otherwise a choice with a callback:
    // `onChoose(level, checkpoint)`.
    if (cancelled && !m_onCancel.empty()) {
        m_services.loadBank(kCancelBank);
        m_log(std::format("mission select: cancelled, calling {}\n", m_onCancel));
        (void)m_scripts.call(m_onCancel);
    } else if (chosen && !m_onChoose.empty()) {
        m_log(std::format("mission select: chose level {} checkpoint {}, calling {}\n", level, checkpoint, m_onChoose));
        const std::array<script::Value, 2> args{script::Value(static_cast<double>(level)),
                                                script::Value(static_cast<double>(checkpoint))};
        (void)m_scripts.call(m_onChoose, args);
    }
}

} // namespace coney
