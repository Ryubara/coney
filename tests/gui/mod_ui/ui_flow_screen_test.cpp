// SPDX-License-Identifier: GPL-3.0-or-later
// The UI widget model's host on a menu's screen flow (docs/guides/mod-ui.md#widgets). Synthetic screens only.

#include <cstdint>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "gui/mod_ui/ui_flow_screen.h"
#include "gui/mod_ui/ui_model.h"
#include "gui/screen_flow_controller.h"
#include "gui/widget.h"

using coney::Pad;
using coney::PadSample;
using coney::gui::GuiFrame;
using coney::gui::ScreenFlowController;
using coney::gui::ScreenFlowState;
using coney::gui::UiFlowScreen;
using coney::gui::UiModel;
using coney::gui::UiScreenDef;
using coney::gui::UiScreenId;
namespace pad = coney::pad;

namespace {

constexpr const char* kProject = "yard-brawl";

// A screen-flow state that stays: the menu under a mod screen.
class StayingScreen final : public ScreenFlowState {
  public:
    [[nodiscard]] std::string_view name() const override { return "Menu"; }
    int update() override { return kStay; }
};

} // namespace

TEST_CASE("A mod screen on a menu's screen flow gives the flow back when it closes") {
    // Expected: Coney's own rule, docs/guides/studio/ui.md#widgets
    UiModel model;
    model.registerProject(kProject, "Yard Brawl");
    const UiScreenId screen = model.createScreen(kProject, UiScreenDef{}).value();
    REQUIRE(model.openScreen(kProject, screen, 0, 0).has_value());

    Pad pad;
    std::uint64_t timeMs = 0;
    StayingScreen menu;
    UiFlowScreen host(model, 0, [&] { return GuiFrame{.timeMs = timeMs, .pad = &pad}; });
    ScreenFlowController flow;
    flow.push(menu);
    flow.push(host);

    // Back (triangle, on its release) closes the mod screen and the flow returns to the menu.
    for (const std::uint16_t buttons : {std::uint16_t{0}, pad::kTriangle, std::uint16_t{0}}) {
        PadSample sample;
        sample.connected = true;
        sample.buttons = buttons;
        pad.update(sample);
        timeMs += 33;
        flow.update();
    }
    CHECK(model.top(0) == std::nullopt);
    CHECK(flow.top() == &menu);
}
