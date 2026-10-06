// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/debug_session.h"

#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "debug/debug_pages.h"

namespace coney::debug {

DebugSession::DebugSession(TunableRegistry& tunables, DebugServices services, InputSource* inner,
                           std::function<void(std::string_view)> log)
    : m_tunables(tunables), m_services(std::move(services)), m_logCallback(std::move(log)), m_gate(inner, m_navigator) {
    // Between two steps: the queued tunable changes land, then the channels take their sample.
    m_gate.addBetweenSteps([this] { m_tunables.applyPending(); });
    m_gate.addBetweenSteps([this] { m_model.sampleChannels(); });

    addTimePage(*this);
    addTunablesPage(*this);
    addNativesPage(*this);
    addConsolePage(*this);
    addCheatsPage(*this);
    addLevelsPage(*this);
    addPlayerPage(*this);
    addCameraPage(*this);
    addSpawnerPage(*this);
    addFightersPage(*this);
    addHudPage(*this);
    addDebugDrawPage(*this);
    addDisplayPage(*this);
    addAudioPage(*this);
    addInputPage(*this);
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

std::vector<std::string> DebugSession::cornerLines() const {
    std::vector<std::string> lines;
    if (m_display.frameStats) {
        std::string text = std::format("frames {}  steps {}", m_time.frames(), m_time.steps());
        if (m_time.paused()) {
            text += "  PAUSED";
        } else if (m_time.slowMotion() > 1) {
            text += std::format("  1/{} speed", m_time.slowMotion());
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
    return lines;
}

} // namespace coney::debug
