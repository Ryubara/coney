// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/debug_session.h"

#include <utility>

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
    addDisplayPage(*this);
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

} // namespace coney::debug
