// SPDX-License-Identifier: GPL-3.0-or-later
// The UI widget model's host over play (docs/guides/mod-ui.md#widgets): a screen that pauses play runs as a game mode
// over gameplay. Synthetic font and screens only.

#include <cstddef>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "core/game_timer.h"
#include "core/pad.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/ui_screen_mode.h"
#include "graphics/font.h"
#include "gui/mod_ui/ui_model.h"
#include "gui/mod_ui/ui_view.h"
#include "support/font_fixtures.h"
#include "support/recording_device.h"

using coney::FrameTime;
using coney::GameModeStack;
using coney::GameTimer;
using coney::Pad;
using coney::UiScreenMode;
using coney::gui::GuiFrame;
using coney::gui::UiLayer;
using coney::gui::UiModel;
using coney::gui::UiScreenDef;
using coney::gui::UiScreenId;
using coney::gui::UiView;
using coney::gui::UiWidgetDef;

namespace {

constexpr const char* kProject = "yard-brawl";

// Step `index` of a run at the fixed step.
FrameTime stepAt(std::uint64_t index) {
    return FrameTime{.index = index,
                     .seconds = 1.0 / 30.0,
                     .gameTicks = (index + 1) * GameTimer::kFixedStepTicks,
                     .stepTicks = GameTimer::kFixedStepTicks};
}

} // namespace

TEST_CASE("A mod screen that pauses play is a game mode that leaves when it closes") {
    // Expected: Coney's own rule, docs/guides/studio/ui.md#focus
    coney::graphics::Font font = coney::test::testFont();
    UiView view([&](int /*slot*/) { return &font; }, [](std::string_view /*name*/) { return nullptr; });
    coney::test::RecordingDevice device;
    UiModel model;
    model.registerProject(kProject, "Yard Brawl");
    const UiScreenId pausing =
        model.createScreen(kProject, UiScreenDef{.layer = UiLayer::Menu, .title = "Rounds", .pause = true}).value();
    const UiScreenId live =
        model.createScreen(kProject, UiScreenDef{.layer = UiLayer::Menu, .title = "Shop", .pause = false}).value();
    UiWidgetDef text;
    text.text = "ROUNDS";
    REQUIRE(model.addWidget(kProject, pausing, text).has_value());

    // A screen that does not pause play leaves play running; one that does asks for the mode.
    REQUIRE(model.openScreen(kProject, live, 0, 0).has_value());
    CHECK_FALSE(model.pausesPlay(0));
    REQUIRE(model.openScreen(kProject, pausing, 0, 0).has_value());
    CHECK(model.pausesPlay(0));
    CHECK_FALSE(model.pausesPlay(1));

    Pad pad;
    UiScreenMode mode(device, model, view, [&](std::size_t /*port*/) { return &pad; }, 0);
    GameModeStack stack;
    stack.push(mode);

    // A step lists the screen's sprites; the render draws them and presents.
    stack.step(stepAt(0));
    CHECK(stack.top() == &mode);
    CHECK(view.labels().size() == 1);
    stack.render(coney::RenderTime{});
    CHECK(device.calls.back() == "present");

    // The pausing screen closed: the one under it stays open over live play, so the mode leaves on its next step.
    REQUIRE(model.closeScreen(kProject, pausing, 0).has_value());
    CHECK(model.top(0) == live);
    CHECK_FALSE(model.pausesPlay(0));
    stack.step(stepAt(1));
    CHECK(stack.empty());
}
