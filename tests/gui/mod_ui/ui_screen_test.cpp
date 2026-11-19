// SPDX-License-Identifier: GPL-3.0-or-later
// One mod screen of the UI widget model (docs/guides/mod-ui.md#widgets), driven directly: which widget holds the focus
// as widgets are hidden and shown again.
// Synthetic screens only: the projects, texts and items are made up here.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gui/menu_input.h"
#include "gui/mod_ui/ui_screen.h"
#include "gui/mod_ui/ui_types.h"

using coney::gui::MenuCommand;
using coney::gui::MenuInput;
using coney::gui::UiEvent;
using coney::gui::UiItem;
using coney::gui::UiLayer;
using coney::gui::UiScreen;
using coney::gui::UiScreenDef;
using coney::gui::UiScreenId;
using coney::gui::UiWidgetDef;
using coney::gui::UiWidgetId;
using coney::gui::UiWidgetKind;

namespace {

// A list `id` named `name` of `count` items on `screen`.
void addList(UiScreen& screen, UiWidgetId id, const std::string& name, std::size_t count) {
    UiWidgetDef def;
    def.kind = UiWidgetKind::List;
    def.name = name;
    REQUIRE(screen.addWidget(id, def, UiScreen::KeyboardStrings{}).has_value());
    for (std::size_t i = 0; i < count; ++i) {
        REQUIRE(
            screen.addItem(id, UiItem{.text = "ITEM " + std::to_string(i), .value = "", .enabled = true}).has_value());
    }
}

} // namespace

TEST_CASE("A widget shown again on a screen with no focus takes the focus") {
    // Expected: Coney's own rule, docs/guides/mod-ui.md#widgets
    UiScreen screen(UiScreenId{1}, "yard-brawl", UiScreenDef{.layer = UiLayer::Menu, .title = "Rounds"}, {});
    const UiWidgetId a{1};
    const UiWidgetId b{2};
    addList(screen, a, "a", 3);
    addList(screen, b, "b", 3);
    MenuInput input;
    screen.takeFocus(0, input);
    REQUIRE(screen.focused() == a);

    // Both hidden (a list refilled out of sight): the screen has no focus.
    screen.setVisible(a, false, 100, input);
    screen.setVisible(b, false, 100, input);
    REQUIRE(screen.focused() == std::nullopt);

    // Shown again, b takes the focus and the screen hears its commands.
    screen.setVisible(b, true, 200, input);
    CHECK(screen.focused() == b);
    std::vector<UiEvent> events;
    screen.handle(MenuCommand::Down, 1000, input, events);
    CHECK(screen.widget(b)->list->selected() == 1);

    // Showing a second widget leaves the focus where it is.
    screen.setVisible(a, true, 1100, input);
    CHECK(screen.focused() == b);
}
