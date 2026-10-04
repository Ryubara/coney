// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "gui/profile_management_gui/pm_shared.h"
#include "gui/screen_flow_controller.h"
#include "gui/text_widget.h"

namespace coney::gui {

/// Coney's stand-in for a profile-manager screen that is not written yet (PM_Profile, PM_Extras and the others the
/// main menu leads to): it shows the screen's name and goes back on the back command, so the flow's transitions can be
/// followed and tested before every screen exists. Nothing in the original corresponds.
class PmPlaceholder final : public ScreenFlowState {
  public:
    /// A stand-in named `name` (the original's class string) over `shared`, which must outlive it.
    PmPlaceholder(std::string_view name, PmShared& shared) : m_name(name), m_shared(shared) {}

    [[nodiscard]] std::string_view name() const override { return m_name; }

    /// Shows the name and takes the focus.
    void enter(ScreenFlowController& flow) override;
    /// One frame: back returns kBack; then draws the name.
    int update() override;
    /// Releases the text.
    void exit() override { m_text.shutdown(); }

  private:
    std::string m_name;
    PmShared& m_shared;
    TextWidget m_text;
};

} // namespace coney::gui
