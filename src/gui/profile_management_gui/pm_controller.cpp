// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/profile_management_gui/pm_controller.h"

#include <algorithm>
#include <utility>

#include "core/assert.h"

namespace coney::gui {

PmController::PmController(PmShared& shared) : m_shared(shared), m_greet(shared), m_mode(shared) {
    for (std::size_t i = 0; i < kPlaceholderNames.size(); ++i) {
        m_placeholders.at(i) = std::make_unique<PmPlaceholder>(kPlaceholderNames.at(i), shared);
    }

    // The transition table, screen by screen as the controller's constructor adds them.
    struct Transition {
        std::string_view from;
        int code;
        std::string_view to;
    };
    static constexpr std::array kTransitions{
        Transition{"PM_Greet", 0, "PM_Mode"},
        Transition{"PM_Greet", 1, "PM_NoSpace"},
        Transition{"PM_Greet", 2, "PM_TooManyProfiles"},
        Transition{"PM_NoSpace", 0, "PM_Mode"},
        Transition{"PM_TooManyProfiles", 0, "PM_Mode"},
        Transition{"PM_Mode", 0, "PM_Profile"},
        Transition{"PM_Mode", 2, "PM_Create"},
        Transition{"PM_Mode", 3, "PM_Load"},
        Transition{"PM_Mode", 4, "PM_Continue"},
        Transition{"PM_Mode", 5, "PM_Extras"},
        Transition{"PM_Mode", 6, "PM_NumPlayers"},
        Transition{"PM_Mode", 8, "PM_Greet"},
        Transition{"PM_NumPlayers", 0, "PM_Profile"},
        Transition{"PM_NumPlayers", 1, "PM_Create"},
        Transition{"PM_NumPlayers", 2, "PM_Load"},
        Transition{"PM_NumPlayers", 3, "PM_Continue"},
        Transition{"PM_Profile", 0, "PM_Load"},
        Transition{"PM_Profile", 1, "PM_Create"},
        Transition{"PM_Profile", 2, "PM_Load"},
        Transition{"PM_Profile", 4, "PM_Profile"},
        Transition{"PM_Create", 0, "PM_Difficulty"},
        Transition{"PM_Load", 1, "PM_Delete"},
        Transition{"PM_Continue", 1, "PM_Delete"},
        Transition{"PM_Delete", 0, "PM_Profile"},
        Transition{"PM_Delete", 2, "PM_Greet"},
        Transition{"PM_Difficulty", 0, "PM_Light"},
        Transition{"PM_Light", 0, "PM_Subtitles"},
    };
    // A screen by its name: the two written ones, else a stand-in.
    const auto screen = [this](std::string_view name) -> ScreenFlowState& {
        if (name == m_greet.name()) {
            return m_greet;
        }
        if (name == m_mode.name()) {
            return m_mode;
        }
        return placeholder(name);
    };
    for (const Transition& transition : kTransitions) {
        ScreenFlowController::addTransition(screen(transition.from), transition.code, screen(transition.to));
    }
}

void PmController::start(std::string onRumble) {
    m_flow.clear();
    m_onRumble = std::move(onRumble);
    m_shared.finishing = false;
    m_flow.push(m_greet);
}

bool PmController::update() { return m_flow.update(); }

void PmController::stop() { m_flow.clear(); }

std::string_view PmController::currentName() const {
    const ScreenFlowState* screen = m_flow.top();
    return screen != nullptr ? screen->name() : std::string_view{};
}

PmPlaceholder& PmController::placeholder(std::string_view name) {
    const auto found = std::ranges::find(kPlaceholderNames, name);
    CONEY_ASSERT(found != kPlaceholderNames.end());
    return *m_placeholders.at(static_cast<std::size_t>(found - kPlaceholderNames.begin()));
}

} // namespace coney::gui
