// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <map>
#include <string_view>
#include <vector>

namespace coney::gui {

class ScreenFlowController;

/// One screen of a screen flow: entered when it comes on top of the flow's stack, updated once a frame while it is
/// there, exited when it is covered or removed. Its update returns a result code: kStay, kBack, or a code the flow
/// looks up in the screen's transitions.
///
/// In the original this is the screen-flow state at widget `+0x70` with its own vtable (`Enter` `+0x10`, `Update`
/// `+0x18`, `Exit` `+0x20`) and its transition map at `+0x78`.
///
/// Research: docs/research/gui.md#screen-flow, docs/research/gui.md#widgets
class ScreenFlowState {
  public:
    /// The result that keeps the screen on top.
    static constexpr int kStay = -0x100;
    /// The result that pops the screen and re-enters the one below.
    static constexpr int kBack = -0xff;

    virtual ~ScreenFlowState() = default;
    ScreenFlowState() = default;
    ScreenFlowState(const ScreenFlowState&) = delete;
    ScreenFlowState& operator=(const ScreenFlowState&) = delete;
    ScreenFlowState(ScreenFlowState&&) = delete;
    ScreenFlowState& operator=(ScreenFlowState&&) = delete;

    /// The screen's name, for logs and tests (the original's class string, such as `PM_Greet`).
    [[nodiscard]] virtual std::string_view name() const = 0;

    /// Called when the screen comes on top of `flow`'s stack.
    virtual void enter(ScreenFlowController& flow) { (void)flow; }
    /// Runs one frame of the screen and returns its result: kStay, kBack or a transition code.
    virtual int update() = 0;
    /// Called when the screen is covered by another or removed from the stack.
    virtual void exit() {}

    /// The screen `code` leads to, or null when the screen has no transition for it.
    [[nodiscard]] ScreenFlowState* transition(int code) const;

  private:
    friend class ScreenFlowController;
    std::map<int, ScreenFlowState*> m_transitions; // result code -> next screen; not owned
    bool m_entered = false;                        // enter() has run and exit() has not
};

/// A stack of screens with per-screen transitions: the menus' screen flow. Each frame update() runs the top screen
/// and follows its result: kStay keeps it, kBack pops it, a code with a transition pushes the next screen or, when
/// that screen is already on the stack, unwinds to it; a code with none is ignored. A screen never runs or draws while
/// it is covered: covered screens are exited and re-entered.
///
/// The screens are not owned and must outlive their time in the flow.
///
/// Coney's choice: exit() is called only on a screen that is entered, so "exit every screen" when unwinding exits the
/// top one (the others were exited when they were covered).
///
/// Research: docs/research/gui.md#screen-flow
/// @orig 0x001c7e80 ScreenFlowController::ScreenFlowController (ScreenFlowController.cpp)
class ScreenFlowController {
  public:
    /// Makes result `code` of `from` lead to `to`, replacing any earlier transition for that code.
    /// @orig 0x001c8010 ScreenFlowController_AddTransition (ScreenFlowController.cpp)
    static void addTransition(ScreenFlowState& from, int code, ScreenFlowState& to);

    /// Exits the top screen if there is one, puts `screen` on top and enters it. Pushing a screen already on the stack
    /// is a programmer error (CONEY_ASSERT); unwind() is the way back to one.
    /// @orig 0x001c80e8 ScreenFlowController_Push (ScreenFlowController.cpp)
    void push(ScreenFlowState& screen);

    /// Exits and removes the top screen, then enters the new top if there is one. Popping an empty flow is a
    /// programmer error (CONEY_ASSERT).
    /// @orig 0x001c82e8 ScreenFlowController_Pop (ScreenFlowController.cpp)
    void pop();

    /// Exits the entered screens, removes every screen above `target` and enters `target`, which must be on the stack
    /// (CONEY_ASSERT).
    /// @orig 0x001c81e8 ScreenFlowController_Unwind (ScreenFlowController.cpp)
    void unwind(ScreenFlowState& target);

    /// Runs the top screen once and follows its result. Returns true when the stack is empty (the flow is done),
    /// before or after the update.
    /// @orig 0x001c83c8 ScreenFlowController_Update (ScreenFlowController.cpp)
    bool update();

    /// Exits the top screen and empties the stack.
    void clear();

    /// The top screen, or null when the stack is empty.
    [[nodiscard]] ScreenFlowState* top() const { return m_stack.empty() ? nullptr : m_stack.back(); }
    /// Whether `screen` is on the stack.
    [[nodiscard]] bool contains(const ScreenFlowState& screen) const;
    /// Screens on the stack.
    [[nodiscard]] std::size_t size() const { return m_stack.size(); }
    /// Whether the stack is empty.
    [[nodiscard]] bool empty() const { return m_stack.empty(); }

  private:
    // Enters `screen` if it is not entered.
    void enterScreen(ScreenFlowState& screen);
    // Exits `screen` if it is entered.
    static void exitScreen(ScreenFlowState& screen);

    std::vector<ScreenFlowState*> m_stack; // bottom first; never holds null
};

} // namespace coney::gui
