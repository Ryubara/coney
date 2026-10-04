// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "gui/profile_management_gui/pm_greet.h"
#include "gui/profile_management_gui/pm_mode.h"
#include "gui/profile_management_gui/pm_placeholder.h"
#include "gui/profile_management_gui/pm_shared.h"
#include "gui/screen_flow_controller.h"

namespace coney::gui {

/// The profile manager: the fourteen front-end screens wired into a screen flow by the transition table of
/// docs/research/frontend.md#profile-manager, starting at PM_Greet. It is done when the flow's stack is empty.
///
/// PM_Greet and PM_Mode are written; the twelve others are PmPlaceholder stand-ins (Coney's) until their research
/// and code exist.
///
/// Research: docs/research/frontend.md#profile-manager, docs/research/gui.md#screen-flow
class PmController {
  public:
    /// The screens other than PM_Greet and PM_Mode, in the order of the controller's fields (`+0x68` on).
    static constexpr std::array<std::string_view, 12> kPlaceholderNames{
        "PM_NoSpace", "PM_TooManyProfiles", "PM_Extras", "PM_NumPlayers", "PM_Profile", "PM_Create",
        "PM_Load",    "PM_Continue",        "PM_Delete", "PM_Difficulty", "PM_Light",   "PM_Subtitles"};

    /// Builds the screens over `shared` (which must outlive the controller) and their transitions.
    /// @orig 0x002040f0 PM_Controller::PM_Controller (PM_Controller.cpp)
    explicit PmController(PmShared& shared);

    /// Resets the shared state, keeps `onRumble` (the first Lua callback, `Menu.fadeToRMI` on the disc) and enters
    /// PM_Greet. A flow left from an earlier start is emptied first.
    /// @orig 0x00204a78 PM_Controller_Start (PM_Controller.cpp)
    void start(std::string onRumble);

    /// Runs one frame of the flow (the top screen updates and draws). Returns true when the flow is done.
    /// @orig 0x00204ba0 PM_Controller_Update (PM_Controller.cpp)
    bool update();

    /// Exits the screen on top and empties the flow.
    /// @orig 0x00204c20 PM_Controller_Stop (PM_Controller.cpp)
    void stop();

    /// The screen on top, or null when the flow is done or not started.
    [[nodiscard]] const ScreenFlowState* current() const { return m_flow.top(); }
    /// The name of the screen on top, or an empty string.
    [[nodiscard]] std::string_view currentName() const;
    /// The first Lua callback, as start() was given it.
    [[nodiscard]] const std::string& onRumble() const { return m_onRumble; }
    /// The flow, for tests.
    [[nodiscard]] const ScreenFlowController& flow() const { return m_flow; }
    /// PM_Greet.
    [[nodiscard]] const PmGreet& greet() const { return m_greet; }
    /// PM_Mode.
    [[nodiscard]] const PmMode& mode() const { return m_mode; }

  private:
    // The stand-in named `name`; CONEY_ASSERT that it is one of kPlaceholderNames.
    [[nodiscard]] PmPlaceholder& placeholder(std::string_view name);

    PmShared& m_shared;
    ScreenFlowController m_flow;
    PmGreet m_greet;
    PmMode m_mode;
    std::array<std::unique_ptr<PmPlaceholder>, kPlaceholderNames.size()> m_placeholders;
    std::string m_onRumble;
};

} // namespace coney::gui
