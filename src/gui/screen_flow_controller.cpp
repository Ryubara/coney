// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/screen_flow_controller.h"

#include <algorithm>

#include "core/assert.h"

namespace coney::gui {

ScreenFlowState* ScreenFlowState::transition(int code) const {
    const auto found = m_transitions.find(code);
    return found == m_transitions.end() ? nullptr : found->second;
}

void ScreenFlowController::addTransition(ScreenFlowState& from, int code, ScreenFlowState& to) {
    from.m_transitions[code] = &to;
}

void ScreenFlowController::push(ScreenFlowState& screen) {
    CONEY_ASSERT(!contains(screen));
    if (ScreenFlowState* covered = top(); covered != nullptr) {
        exitScreen(*covered);
    }
    m_stack.push_back(&screen);
    enterScreen(screen);
}

void ScreenFlowController::pop() {
    CONEY_ASSERT(!m_stack.empty());
    exitScreen(*m_stack.back());
    m_stack.pop_back();
    if (ScreenFlowState* uncovered = top(); uncovered != nullptr) {
        enterScreen(*uncovered);
    }
}

void ScreenFlowController::unwind(ScreenFlowState& target) {
    const auto found = std::ranges::find(m_stack, &target);
    CONEY_ASSERT(found != m_stack.end());
    for (ScreenFlowState* screen : m_stack) {
        exitScreen(*screen);
    }
    m_stack.erase(found + 1, m_stack.end());
    enterScreen(target);
}

bool ScreenFlowController::update() {
    ScreenFlowState* screen = top();
    if (screen == nullptr) {
        return true;
    }
    const int result = screen->update();
    if (result == ScreenFlowState::kStay) {
        return false;
    }
    if (result == ScreenFlowState::kBack) {
        pop();
        return empty();
    }
    // A code: follow the screen's transition, if it has one for it.
    ScreenFlowState* next = screen->transition(result);
    if (next == nullptr) {
        return false;
    }
    if (contains(*next)) {
        unwind(*next);
    } else {
        push(*next);
    }
    return false;
}

void ScreenFlowController::clear() {
    if (ScreenFlowState* screen = top(); screen != nullptr) {
        exitScreen(*screen);
    }
    m_stack.clear();
}

bool ScreenFlowController::contains(const ScreenFlowState& screen) const {
    return std::ranges::find(m_stack, &screen) != m_stack.end();
}

void ScreenFlowController::enterScreen(ScreenFlowState& screen) {
    if (!screen.m_entered) {
        // Marked first, so a screen that changes the flow from enter() is seen as entered.
        screen.m_entered = true;
        screen.enter(*this);
    }
}

void ScreenFlowController::exitScreen(ScreenFlowState& screen) {
    if (screen.m_entered) {
        screen.m_entered = false;
        screen.exit();
    }
}

} // namespace coney::gui
