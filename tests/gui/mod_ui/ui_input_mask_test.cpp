// SPDX-License-Identifier: GPL-3.0-or-later
// The UI widget model's input mask (docs/guides/mod-ui.md#input-mask): a mod screen's port hidden from gameplay.
// Synthetic samples and screens only.

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "core/pads.h"
#include "gui/mod_ui/ui_input_mask.h"
#include "gui/mod_ui/ui_model.h"
#include "gui/mod_ui/ui_types.h"

using coney::InputSource;
using coney::PadSample;
using coney::PortSamples;
using coney::gui::UiInputMask;
using coney::gui::UiLayer;
using coney::gui::UiModel;
using coney::gui::UiScreenDef;
using coney::gui::UiScreenId;
namespace pad = coney::pad;

namespace {

constexpr const char* kProject = "yard-brawl";

// A menu screen of kProject.
UiScreenId menuScreen(UiModel& model) {
    model.registerProject(kProject, "Yard Brawl");
    return model.createScreen(kProject, UiScreenDef{.layer = UiLayer::Menu, .title = "Rounds", .pause = false}).value();
}

// A source of one fixed sample per port.
class FixedInput final : public InputSource {
  public:
    PortSamples samples{};
    PortSamples sample(std::uint64_t /*frame*/) override { return samples; }
};

} // namespace

TEST_CASE("The input mask hides a mod screen's port from gameplay until its buttons are let go") {
    // Expected: Coney's own rule, docs/guides/studio/ui.md#focus
    UiModel model;
    const UiScreenId screen = menuScreen(model);
    FixedInput input;
    input.samples[0].connected = true;
    input.samples[1].connected = true;
    input.samples[0].buttons = pad::kCross | pad::kDown;
    input.samples[1].buttons = pad::kSquare;
    input.samples[0].pressure.at(0) = 200;
    UiInputMask mask(&input, model);

    // No screen: both ports pass through.
    CHECK(mask.sample(0)[0].buttons == (pad::kCross | pad::kDown));

    // A menu screen for slot 0: gameplay sees port 1 connected and empty; port 2 still plays; the screen reads it raw.
    REQUIRE(model.openScreen(kProject, screen, 0, 0).has_value());
    const PortSamples open = mask.sample(1);
    CHECK(open[0].connected);
    CHECK(open[0].buttons == 0);
    CHECK(open[0].sticks == PadSample{}.sticks);
    CHECK(open[0].pressure.at(0) == 0);
    CHECK(open[1].buttons == pad::kSquare);
    CHECK(mask.rawPad(0).held(pad::kCross));

    // Closed with cross and down still held: they stay hidden until let go, then come through again.
    REQUIRE(model.closeScreen(kProject, screen, 0).has_value());
    CHECK(mask.sample(2)[0].buttons == 0);
    input.samples[0].buttons = pad::kDown;
    CHECK(mask.sample(3)[0].buttons == 0);
    input.samples[0].buttons = 0;
    CHECK(mask.sample(4)[0].buttons == 0);
    input.samples[0].buttons = pad::kCross;
    CHECK(mask.sample(5)[0].buttons == pad::kCross);
}
