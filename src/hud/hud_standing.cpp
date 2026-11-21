// SPDX-License-Identifier: GPL-3.0-or-later
// The HUD's standing state as network play reads and writes it (docs/guides/snapshots.md#hud-standing): what the HUD
// holds between steps, read for the host's snapshots and taken by a guest's HUD. Coney's own; the original has no
// network play.
#include <cstddef>

#include "hud/counter_panels.h"
#include "hud/hud.h"
#include "hud/player_panel.h"
#include "hud/scripted_bars.h"

namespace coney::hud {

namespace {

// A cycle's switch, timing and icons with its running part at its start: what travels of it.
// @coney-only network play's view of a prompt cycle; the original has none
ActionCycle cycleSettings(const ActionCycle& cycle) {
    return ActionCycle{.on = cycle.on,
                       .framesPerIcon = cycle.framesPerIcon,
                       .blinkFrames = cycle.blinkFrames,
                       .iconA = cycle.iconA,
                       .iconB = cycle.iconB};
}

} // namespace

PanelFlags PlayerPanel::flags() const {
    return PanelFlags{.attached = m_attached,
                      .banner = m_banner,
                      .shown = m_shown,
                      .mayShow = m_mayShow,
                      .forceShow = m_forceShow,
                      .flashFrames = m_flashFrames};
}

void PlayerPanel::setFlags(const PanelFlags& flags) {
    // Becoming attached starts the fade clock, as attach() does.
    if (flags.attached && !m_attached) {
        m_activityMs = m_nowMs;
    }
    m_attached = flags.attached;
    m_banner = flags.banner;
    m_shown = flags.shown;
    m_mayShow = flags.mayShow;
    m_forceShow = flags.forceShow;
    if (flags.flashFrames != m_flashFrames) {
        setFlashFrames(flags.flashFrames);
    }
}

void CounterPanels::restore(std::size_t index, const CounterPanel& panel) {
    if (index < m_panels.size()) {
        m_panels.at(index) = panel;
    }
}

void ScriptedBars::restoreBar(std::size_t slot, const GenericBar& bar) {
    if (slot < m_bars.size()) {
        m_bars.at(slot) = bar;
    }
}

HudStanding Hud::standing() const {
    HudStanding standing;
    // Each player's: his panel's flags, his prompt and its cycle's settings, his radar's switch and tint.
    for (std::size_t player = 0; player < kPlayers; ++player) {
        PlayerHudStanding& mine = standing.players.at(player);
        mine.panel = m_panels.at(player).flags();
        mine.prompt = m_prompts.at(player);
        mine.promptHidden = m_promptTextHidden.at(player);
        mine.cycle = cycleSettings(m_cycles.at(player));
        mine.radarOn = m_radar.on.at(player);
        mine.radarTint = m_radar.tint.at(player);
    }
    // The shared parts, the counter panels without their hide times.
    SharedHudStanding& shared = standing.shared;
    shared.visible = m_visible;
    shared.clubActionText = m_clubActionText;
    shared.radarScriptOn = m_radar.scriptOn;
    shared.radarRest = m_radar.rest;
    shared.radarFast = m_radar.fast;
    shared.radarZoomScale = m_radar.zoomScale;
    shared.counters = m_counters.panels();
    for (CounterPanel& panel : shared.counters) {
        panel.hideAtMs.reset();
    }
    for (std::size_t slot = 0; slot < kGenericBars; ++slot) {
        shared.bars.at(slot) = m_bars.bar(slot);
    }
    shared.gauge = m_bars.gauge();
    shared.scores = m_progress;
    shared.stopWatch = m_stopWatch;
    shared.stopWatchMs = m_stopWatch.shown && m_services.stopWatchTime ? m_services.stopWatchTime() : 0;
    return standing;
}

void Hud::applyStanding(const HudStanding& standing) {
    for (std::size_t player = 0; player < kPlayers; ++player) {
        const PlayerHudStanding& theirs = standing.players.at(player);
        m_panels.at(player).setFlags(theirs.panel);
        // The text first: a changed one restarts the prompt and shows it, then the cycle's hiding is taken as it is.
        setActionPrompt(player, theirs.prompt);
        m_promptTextHidden.at(player) = theirs.promptHidden;
        // A cycle steps on its own from its start; only a change of its settings starts it again.
        ActionCycle& cycle = m_cycles.at(player);
        if (cycleSettings(cycle) != cycleSettings(theirs.cycle)) {
            cycle = cycleSettings(theirs.cycle);
        }
        m_radar.on.at(player) = theirs.radarOn;
        // A tint that changes blends from the one shown, as setRadarTint() does; the host has already decided it.
        if (m_radar.tint.at(player) != theirs.radarTint) {
            m_radar.previousTint.at(player) = m_radar.tint.at(player);
            m_radar.tint.at(player) = theirs.radarTint;
            m_radar.tintChangedMs.at(player) = m_nowMs;
        }
    }
    const SharedHudStanding& shared = standing.shared;
    m_visible = shared.visible;
    m_clubActionText = shared.clubActionText;
    m_radar.scriptOn = shared.radarScriptOn;
    // A new range jumps the zoom to its rest, as HUDRadarSetRange does; the same range leaves the zoom easing.
    if (m_radar.rest != shared.radarRest || m_radar.fast != shared.radarFast) {
        setRadarRange(shared.radarRest, shared.radarFast);
    }
    m_radar.zoomScale = shared.radarZoomScale;
    for (std::size_t index = 0; index < kCounterPanels; ++index) {
        m_counters.restore(index, shared.counters.at(index));
    }
    for (std::size_t slot = 0; slot < kGenericBars; ++slot) {
        m_bars.restoreBar(slot, shared.bars.at(slot));
    }
    m_bars.restoreGauge(shared.gauge);
    m_progress = shared.scores;
    m_stopWatch = shared.stopWatch;
}

} // namespace coney::hud
