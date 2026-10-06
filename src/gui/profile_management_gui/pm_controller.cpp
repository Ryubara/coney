// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/profile_management_gui/pm_controller.h"

#include <algorithm>
#include <initializer_list>
#include <utility>

#include "core/assert.h"

namespace coney::gui {

PmController::PmController(PmShared& shared)
    : m_shared(shared), m_greet(shared), m_mode(shared), m_numPlayers(shared), m_profile(shared), m_create(shared),
      m_load(shared), m_continue(shared), m_delete(shared), m_difficulty(shared), m_light(shared), m_subtitles(shared) {
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
    // A screen by its name: a written one, else a stand-in.
    const auto screen = [this](std::string_view name) -> ScreenFlowState& {
        for (ScreenFlowState* written :
             std::initializer_list<ScreenFlowState*>{&m_greet, &m_mode, &m_numPlayers, &m_profile, &m_create, &m_load,
                                                     &m_continue, &m_delete, &m_difficulty, &m_light, &m_subtitles}) {
            if (name == written->name()) {
                return *written;
            }
        }
        return placeholder(name);
    };
    for (const Transition& transition : kTransitions) {
        ScreenFlowController::addTransition(screen(transition.from), transition.code, screen(transition.to));
    }
}

void PmController::start(std::string onRumble) {
    m_flow.clear();
    m_shared.onRumble = onRumble;
    m_onRumble = std::move(onRumble);
    m_shared.finishing = false;
    // The globals `0x0050f584`-`0x0050f5c0` and the save system's "profile in use" start cleared.
    m_shared.session = PmSession{};
    if (m_shared.profiles != nullptr) {
        m_shared.profiles->setInUse(false);
    }
    m_flow.push(m_greet);
}

bool PmController::update() {
    // The update returns the done flag a screen sets; an empty flow is done too.
    const bool empty = m_flow.update();
    return m_shared.session.done || empty;
}

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
