// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/debug_session.h"

#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/log.h"
#include "debug/debug_pages.h"

namespace coney::debug {

DebugSession::DebugSession(TunableRegistry& tunables, DebugServices services, InputSource* inner,
                           std::function<void(std::string_view)> log)
    : m_tunables(tunables), m_services(std::move(services)), m_logCallback(std::move(log)), m_gate(inner, m_navigator) {
    // The Entity editor's edits go to the run log, and in a network session only the host applies them.
    m_entityEditor.setLog([](std::string_view line) { logging::info(logging::Category::Engine, line); });
    m_entityEditor.setAuthority([this] {
        const NetControls* net = m_services.net ? m_services.net() : nullptr;
        return net == nullptr || net->status().role != NetRole::Client;
    });
    // Between two steps: the queued tunable changes land, then the queued entity edits, then the channels take their
    // sample.
    m_gate.addBetweenSteps([this] { m_tunables.applyPending(); });
    m_gate.addBetweenSteps([this] {
        PlayControls* mode = play();
        m_entityEditor.applyPending(mode != nullptr ? mode->entityEdits() : nullptr);
    });
    m_gate.addBetweenSteps([this] { m_model.sampleChannels(); });
    // A notice counts down by frames.
    m_gate.addBetweenSteps([this] { m_noticeFramesLeft = m_noticeFramesLeft > 0 ? m_noticeFramesLeft - 1 : 0; });
    // R3 + d-pad left flips the noclip while the Player page's hotkey option is on (it starts on).
    m_gate.addHotkey(
        kNoclipChord, [this] { return m_noclipHotkey; },
        [this] {
            const PlayControls* p = play();
            setNoclip(p == nullptr || !p->noclip());
        });

    addTimePage(*this);
    addTunablesPage(*this);
    addNativesPage(*this);
    addConsolePage(*this);
    addCheatsPage(*this);
    addLevelsPage(*this);
    addMissionsPage(*this);
    addMoviesPage(*this);
    addPlayerPage(*this);
    addCameraPage(*this);
    addSpawnerPage(*this);
    addFightersPage(*this);
    addAnimPlayerPage(*this);
    addHudPage(*this);
    addDebugDrawPage(*this);
    addEntityInspectorPage(*this);
    addEntityEditorPage(*this);
    addDisplayPage(*this);
    addAudioPage(*this);
    addNetworkPage(*this);
    addInputPage(*this);
    addPerformancePage(*this);
}

DebugSession::~DebugSession() = default;

bool DebugSession::usingGameScripts() const {
    script::ScriptSystem* game = m_services.scripts ? m_services.scripts() : nullptr;
    return game != nullptr && game->exists();
}

script::LuaVm& DebugSession::vm() {
    if (usingGameScripts()) {
        return m_services.scripts()->vm();
    }
    if (!m_sandbox) {
        m_sandbox = std::make_unique<SandboxScripts>([this](std::string_view line) { print(std::string(line)); });
    }
    return m_sandbox->scripts().vm();
}

const script::RecordedCalls* DebugSession::recorded() {
    if (usingGameScripts()) {
        return m_services.recorded ? m_services.recorded() : nullptr;
    }
    (void)vm();
    return &m_sandbox->recorded();
}

void DebugSession::print(std::string line) {
    if (m_logCallback) {
        m_logCallback(line + "\n");
    }
    m_log.add(std::move(line));
}

void DebugSession::notice(std::string text) {
    print(text);
    m_notice = std::move(text);
    m_noticeFramesLeft = kNoticeFrames;
}

void DebugSession::setNoclip(bool on) {
    PlayControls* p = play();
    if (p == nullptr) {
        print("noclip: no player");
        return;
    }
    if (!p->canNoclip()) {
        print("noclip: not in this mode");
        return;
    }
    p->setNoclip(on);
    notice(on ? "Noclip on" : "Noclip off");
}

std::vector<std::string> DebugSession::cornerLines() const {
    std::vector<std::string> lines;
    if (m_display.frameStats) {
        std::string text = std::format("frames {}  steps {}", m_time.frames(), m_time.steps());
        if (m_time.paused()) {
            text += "  PAUSED";
        } else if (m_time.scalePercent() != 100) {
            text += std::format("  {} speed", formatTimeScale(m_time.scalePercent()));
        }
        lines.push_back(std::move(text));
    }
    if (m_display.fpsCounter) {
        // The platform's half-second rates; a lockstep run (tests, scripted input) has no real clock to measure.
        if (!m_services.frameRate) {
            lines.emplace_back("fps: lockstep, no real clock");
        } else if (const std::optional<FrameRateReading> rates = m_services.frameRate(); !rates) {
            lines.emplace_back("fps: measuring");
        } else {
            lines.push_back(std::format("{:.1f} fps  {:.2f} ms  {:.1f} steps/s", rates->framesPerSecond,
                                        rates->frameMilliseconds, rates->stepsPerSecond));
        }
    }
    // Player 1's place, from the play mode's own state; nothing in a run without a player.
    if (m_display.coordinates) {
        if (const PlayControls* play = this->play(); play != nullptr) {
            const anim::Vec3 feet = play->playerFeet();
            lines.push_back(std::format("x {:.2f}  y {:.2f}  z {:.2f}  heading {:.1f}", feet.x, feet.y, feet.z,
                                        play->playerHeadingDegrees()));
        } else {
            lines.emplace_back("coordinates: no player");
        }
    }
    // The Entity inspector's selection, named while it is highlighted.
    if (std::string inspected = inspectedCornerLine(*this); !inspected.empty()) {
        lines.push_back(std::move(inspected));
    }
    // The last notice, while it lasts.
    if (m_noticeFramesLeft > 0) {
        lines.push_back(m_notice);
    }
    return lines;
}

} // namespace coney::debug
