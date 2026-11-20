// SPDX-License-Identifier: GPL-3.0-or-later
// The UI widget model for mods (docs/guides/studio/ui.md#widgets): focus and navigation with the game's menu timings
// (docs/research/frontend.md#input), back always closes, the trusted layer and the limits.
// Synthetic screens only: the projects, texts and items are made up here.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "gui/menu_input.h"
#include "gui/mod_ui/ui_model.h"
#include "gui/mod_ui/ui_types.h"

using coney::Pad;
using coney::PadSample;
using coney::gui::MenuInput;
using coney::gui::UiEvent;
using coney::gui::UiEventKind;
using coney::gui::UiItem;
using coney::gui::UiLayer;
using coney::gui::UiModel;
using coney::gui::UiRefusal;
using coney::gui::UiScreenDef;
using coney::gui::UiScreenId;
using coney::gui::UiWidgetDef;
using coney::gui::UiWidgetId;
using coney::gui::UiWidgetKind;
namespace pad = coney::pad;
namespace ui_limit = coney::gui::ui_limit;

namespace {

constexpr const char* kProject = "yard-brawl";
constexpr const char* kOther = "other-mod";

// Game time of step `frame` at the fixed 30 steps a second, in whole milliseconds (33, 66, 100, 133, ...).
std::uint64_t msOf(std::uint64_t frame) { return frame * 100 / 3; }

// One player's pad driven a step at a time, with the cues the model played.
struct Driver {
    UiModel model{[this](int cue) { cues.push_back(cue); }};
    Pad pad;
    std::uint64_t frame = 0;
    std::vector<int> cues;

    // Steps the model once with `buttons` held on slot 0's pad.
    void step(std::uint16_t buttons) {
        PadSample sample;
        sample.connected = true;
        sample.buttons = buttons;
        pad.update(sample);
        model.step(0, pad, msOf(frame));
        ++frame;
    }
    // Presses `button` for one step and lets it go for `gap` - 1 more, so taps are `gap` steps apart.
    void tap(std::uint16_t button, std::uint64_t gap = 4) {
        step(button);
        for (std::uint64_t i = 1; i < gap; ++i) {
            step(0);
        }
    }
    // Back (triangle), which acts on its release.
    void back() {
        step(pad::kTriangle);
        step(0);
        step(0);
        step(0);
    }
};

// A menu screen of kProject.
UiScreenId menuScreen(UiModel& model, bool pause = false) {
    model.registerProject(kProject, "Yard Brawl");
    return model.createScreen(kProject, UiScreenDef{.layer = UiLayer::Menu, .title = "Rounds", .pause = pause}).value();
}

// A widget of `kind` named `name` on `screen`.
UiWidgetId widget(UiModel& model, UiScreenId screen, UiWidgetKind kind, const std::string& name) {
    UiWidgetDef def;
    def.kind = kind;
    def.name = name;
    return model.addWidget(kProject, screen, def).value();
}

// `count` items on grid or list `id`.
void fill(UiModel& model, UiWidgetId id, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        REQUIRE(model.addItem(kProject, id, UiItem{.text = "ITEM " + std::to_string(i), .value = "", .enabled = true})
                    .has_value());
    }
}

// The kinds of the events queued since the last call.
std::vector<UiEventKind> kinds(UiModel& model) {
    const std::vector<UiEvent> events = model.takeEvents();
    std::vector<UiEventKind> out;
    out.reserve(events.size());
    for (const UiEvent& event : events) {
        out.push_back(event.kind);
    }
    return out;
}

} // namespace

TEST_CASE("Navigation reaches every focusable widget of a mod screen by the four directions") {
    // Expected: Coney's own rule, docs/guides/studio/ui.md#focus; the moves and cues frontend.md#input, confirmed
    // (code)
    Driver d;
    const UiScreenId screen = menuScreen(d.model);
    // A title, then a grid of one row of two, a list of three and a keyboard; only the last three take the focus.
    widget(d.model, screen, UiWidgetKind::Text, "title");
    UiWidgetDef gridDef;
    gridDef.kind = UiWidgetKind::Grid;
    gridDef.name = "grid";
    gridDef.rows = {2};
    const UiWidgetId grid = d.model.addWidget(kProject, screen, gridDef).value();
    fill(d.model, grid, 2);
    const UiWidgetId list = widget(d.model, screen, UiWidgetKind::List, "list");
    fill(d.model, list, 3);
    d.model.setKeyboardStrings({.characters = "ABCDEFGHIJKL", .ok = "OK", .del = "DEL"});
    const UiWidgetId keys = widget(d.model, screen, UiWidgetKind::Keyboard, "keys");
    REQUIRE(d.model.openScreen(kProject, screen, 0, msOf(0)).has_value());
    d.step(0); // the step the screen opened on: the focus block (20 ms) holds its d-pad
    d.model.takeEvents();

    // Opening focuses the first focusable widget, the grid.
    const coney::gui::UiScreen& s = *d.model.screen(screen);
    REQUIRE(s.focused() == grid);

    // Right moves inside the grid's row; right at its end moves to the next widget, the list.
    d.tap(pad::kRight);
    CHECK(s.widget(grid)->grid->selected() == 1);
    CHECK(s.focused() == grid);
    d.tap(pad::kRight);
    CHECK(s.focused() == list);
    // Down walks the list, and past its last entry reaches the keyboard.
    d.tap(pad::kDown);
    d.tap(pad::kDown);
    CHECK(s.widget(list)->list->selected() == 2);
    CHECK(s.focused() == list);
    d.tap(pad::kDown);
    CHECK(s.focused() == keys);
    // Up from the keyboard's top row goes back to the list, and up past the list's first entry to the grid.
    d.tap(pad::kUp);
    CHECK(s.focused() == list);
    d.tap(pad::kUp);
    d.tap(pad::kUp);
    CHECK(s.widget(list)->list->selected() == 0);
    d.tap(pad::kUp);
    CHECK(s.focused() == grid);
    // Left at the grid's first item has nowhere to go: the grid refuses it as it would alone.
    d.tap(pad::kLeft);
    d.tap(pad::kLeft);
    CHECK(s.focused() == grid);
    CHECK(d.cues.back() == 0xe);

    // Each of the four moves between widgets queued a Focus event.
    const std::vector<UiEventKind> events = kinds(d.model);
    CHECK(std::count(events.begin(), events.end(), UiEventKind::Focus) == 4);
}

TEST_CASE("Hiding the focused widget moves the focus off it and it takes no command") {
    // Expected: Coney's own rule, docs/guides/mod-ui.md#widgets
    Driver d;
    const UiScreenId screen = menuScreen(d.model);
    const UiWidgetId a = widget(d.model, screen, UiWidgetKind::List, "a");
    fill(d.model, a, 3);
    const UiWidgetId b = widget(d.model, screen, UiWidgetKind::List, "b");
    fill(d.model, b, 3);
    REQUIRE(d.model.openScreen(kProject, screen, 0, msOf(0)).has_value());
    d.step(0);
    const coney::gui::UiScreen& s = *d.model.screen(screen);
    REQUIRE(s.focused() == a);

    // Hidden, the focused list gives the focus to the next one that can hold it.
    REQUIRE(d.model.setVisible(kProject, a, false, msOf(d.frame)).has_value());
    CHECK(s.focused() == b);
    d.step(0);
    d.model.takeEvents();
    // Down now moves b; a, hidden, neither moves nor queues an event.
    d.tap(pad::kDown);
    CHECK(s.widget(a)->list->selected() == 0);
    CHECK(s.widget(b)->list->selected() == 1);
    for (const UiEvent& event : d.model.takeEvents()) {
        CHECK(event.widget != a);
    }

    // With nothing left that can hold the focus, the screen has none and commands go nowhere.
    REQUIRE(d.model.setVisible(kProject, b, false, msOf(d.frame)).has_value());
    CHECK(s.focused() == std::nullopt);
    d.tap(pad::kDown);
    d.tap(pad::kCross);
    d.step(0);
    CHECK(s.widget(b)->list->selected() == 1);
    CHECK(d.model.takeEvents().empty());
}

TEST_CASE("A widget's named neighbour takes the focus before the order") {
    // Expected: Coney's own rule, docs/guides/studio/ui.md#focus
    Driver d;
    const UiScreenId screen = menuScreen(d.model);
    UiWidgetDef first;
    first.kind = UiWidgetKind::List;
    first.name = "first";
    first.neighbours.down = "third";
    const UiWidgetId a = d.model.addWidget(kProject, screen, first).value();
    fill(d.model, a, 1);
    const UiWidgetId b = widget(d.model, screen, UiWidgetKind::List, "second");
    fill(d.model, b, 1);
    const UiWidgetId c = widget(d.model, screen, UiWidgetKind::List, "third");
    fill(d.model, c, 1);
    REQUIRE(d.model.openScreen(kProject, screen, 0, msOf(0)).has_value());
    d.step(0); // the step the screen opened on: the focus block (20 ms) holds its d-pad

    d.tap(pad::kDown);
    CHECK(d.model.screen(screen)->focused() == c);
    // The third names no neighbour: up goes to the one added before it.
    d.tap(pad::kUp);
    CHECK(d.model.screen(screen)->focused() == b);
}

TEST_CASE("A mod screen keeps the game's 110 ms command gap and d-pad auto-repeat") {
    // Expected: frontend.md#input, confirmed (code)
    // docs/research/frontend.md#input: a command needs more than 110 ms since the last; a held direction repeats on
    // its 15th held sample and every 4th after it.
    Driver d;
    const UiScreenId screen = menuScreen(d.model);
    const UiWidgetId list = widget(d.model, screen, UiWidgetKind::List, "list");
    fill(d.model, list, 20);
    REQUIRE(d.model.openScreen(kProject, screen, 0, msOf(0)).has_value());
    d.step(0);
    const coney::gui::UiWidget& w = *d.model.screen(screen)->widget(list);

    // Taps 3 steps apart (100 ms) lose every second one; 4 apart (133 ms) all count.
    d.tap(pad::kDown, 3);
    d.tap(pad::kDown, 3);
    CHECK(w.list->selected() == 1);
    d.tap(pad::kDown, 4);
    d.tap(pad::kDown, 4);
    CHECK(w.list->selected() == 3);

    // Held down: a move on the first sample, then on the 15th and every 4th after it.
    std::vector<std::uint64_t> movedAt;
    std::size_t last = w.list->selected();
    for (std::uint64_t held = 1; held <= 23; ++held) {
        d.step(pad::kDown);
        if (w.list->selected() != last) {
            movedAt.push_back(held);
            last = w.list->selected();
        }
    }
    CHECK(movedAt == std::vector<std::uint64_t>{1, 15, 19, 23});
}

TEST_CASE("A focus move blocks the d-pad for 20 ms and forgets the last command") {
    // Expected: frontend.md#input, confirmed (code)
    // OptionGrid_TakeFocus (0x001d4d28) clears the record and blocks the d-pad: after a focus move the next tap counts
    // at once, the gap restarting from the new widget.
    Driver d;
    const UiScreenId screen = menuScreen(d.model);
    const UiWidgetId a = widget(d.model, screen, UiWidgetKind::List, "a");
    fill(d.model, a, 1);
    const UiWidgetId b = widget(d.model, screen, UiWidgetKind::List, "b");
    fill(d.model, b, 3);
    REQUIRE(d.model.openScreen(kProject, screen, 0, msOf(0)).has_value());
    d.step(0);
    d.tap(pad::kDown, 2); // to b, then 66 ms later
    d.tap(pad::kDown, 2); // a move in b: the gap was forgotten with the focus move
    CHECK(d.model.screen(screen)->focused() == b);
    CHECK(d.model.screen(screen)->widget(b)->list->selected() == 1);
}

TEST_CASE("A widget added to an open screen with no focus takes the focus") {
    // Expected: Coney's own rule, docs/guides/mod-ui.md#widgets
    Driver d;
    const UiScreenId screen = menuScreen(d.model);
    widget(d.model, screen, UiWidgetKind::Text, "title");
    REQUIRE(d.model.openScreen(kProject, screen, 0, msOf(0)).has_value());
    d.step(0);
    REQUIRE(d.model.screen(screen)->focused() == std::nullopt);

    // The list added once the screen is open takes the focus on the next step, and Down moves it.
    const UiWidgetId list = widget(d.model, screen, UiWidgetKind::List, "list");
    fill(d.model, list, 3);
    d.step(0);
    CHECK(d.model.screen(screen)->focused() == list);
    d.tap(pad::kDown);
    CHECK(d.model.screen(screen)->widget(list)->list->selected() == 1);
}

TEST_CASE("A focus change on a covered screen leaves the top screen's input alone") {
    // Expected: Coney's own rule, docs/guides/mod-ui.md#widgets
    // Only the top screen's focus moves block its d-pad; the covered screen takes its focus afresh when it comes back.
    Driver d;
    const UiScreenId under = menuScreen(d.model);
    const UiWidgetId a = widget(d.model, under, UiWidgetKind::List, "a");
    fill(d.model, a, 2);
    const UiWidgetId b = widget(d.model, under, UiWidgetKind::List, "b");
    fill(d.model, b, 2);
    const UiScreenId over = d.model.createScreen(kProject, UiScreenDef{}).value();
    const UiWidgetId top = widget(d.model, over, UiWidgetKind::List, "top");
    fill(d.model, top, 3);
    REQUIRE(d.model.openScreen(kProject, under, 0, msOf(0)).has_value());
    REQUIRE(d.model.openScreen(kProject, over, 0, msOf(0)).has_value());
    for (int i = 0; i < 10; ++i) {
        d.step(0);
    }

    // The covered screen's focus moves and a widget there is hidden; the tap in the same frame still moves the top.
    REQUIRE(d.model.focus(kProject, b, msOf(d.frame)).has_value());
    REQUIRE(d.model.setVisible(kProject, b, false, msOf(d.frame)).has_value());
    CHECK(d.model.screen(under)->focused() == a);
    d.tap(pad::kDown);
    CHECK(d.model.screen(over)->widget(top)->list->selected() == 1);
}

TEST_CASE("Back always closes a mod screen and returns to the one under it") {
    // Expected: Coney's own rule, docs/guides/studio/ui.md#focus; cue 0xf frontend.md#audio-cues-references-only
    Driver d;
    const UiScreenId first = menuScreen(d.model);
    const UiScreenId second = d.model.createScreen(kProject, UiScreenDef{}).value();
    fill(d.model, widget(d.model, first, UiWidgetKind::List, "list"), 2);
    REQUIRE(d.model.openScreen(kProject, first, 0, msOf(0)).has_value());
    REQUIRE(d.model.openScreen(kProject, second, 0, msOf(0)).has_value());
    CHECK(d.model.top(0) == second);
    d.model.takeEvents();

    d.back();
    CHECK(kinds(d.model) == std::vector<UiEventKind>{UiEventKind::Back, UiEventKind::Closed});
    CHECK(d.model.top(0) == first);
    CHECK(d.cues.back() == 0xf);
    d.back();
    CHECK(d.model.top(0) == std::nullopt);
    CHECK_FALSE(d.model.takesPad(0));
}

TEST_CASE("Back ends a choice dialog before it closes the screen") {
    // Expected: Coney's own rule, docs/guides/mod-ui.md#focus
    Driver d;
    const UiScreenId screen = menuScreen(d.model);
    REQUIRE(d.model.openScreen(kProject, screen, 0, msOf(0)).has_value());
    REQUIRE(d.model.askChoice(kProject, screen, "Quit the yard?", {"YES", "NO"}, 1, msOf(0)).has_value());
    d.model.takeEvents();

    d.back();
    const std::vector<UiEvent> events = d.model.takeEvents();
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == UiEventKind::Choice);
    CHECK(events[0].item == std::nullopt);
    CHECK(d.model.top(0) == screen);

    // Accept picks the selected choice.
    REQUIRE(d.model.askChoice(kProject, screen, "Quit the yard?", {"YES", "NO"}, 1, msOf(d.frame)).has_value());
    d.tap(pad::kCross);
    d.step(0);
    const std::vector<UiEvent> chosen = d.model.takeEvents();
    REQUIRE(chosen.size() == 1);
    CHECK(chosen[0].item == 1);
}

TEST_CASE("The keyboard types up to its limit and hands the text over on OK") {
    // Expected: Coney's own rule, docs/guides/studio/ui.md#safety
    Driver d;
    const UiScreenId screen = menuScreen(d.model);
    d.model.setKeyboardStrings({.characters = "AB", .ok = "OK", .del = "DEL"});
    widget(d.model, screen, UiWidgetKind::Keyboard, "keys");
    REQUIRE(d.model.openScreen(kProject, screen, 0, msOf(0)).has_value());
    d.step(0);
    for (std::size_t i = 0; i < ui_limit::kAskText + 1; ++i) {
        d.tap(pad::kCross);
    }
    CHECK(d.cues.back() == 0xe);
    // Right twice reaches OK (A, B, OK in the first row), once the gap from the last accept (on its release) is over.
    d.step(0);
    d.tap(pad::kRight);
    d.tap(pad::kRight);
    d.tap(pad::kCross);
    std::optional<std::string> typed;
    for (const UiEvent& event : d.model.takeEvents()) {
        if (event.kind == UiEventKind::Text) {
            typed = event.text;
        }
    }
    CHECK(typed == std::string(ui_limit::kAskText, 'A'));
}

TEST_CASE("A model screen cannot open over a trusted one") {
    // Expected: Coney's own rule, docs/guides/studio/ui.md#no-spoofing
    Driver d;
    const UiScreenId under = menuScreen(d.model);
    const UiWidgetId list = widget(d.model, under, UiWidgetKind::List, "list");
    fill(d.model, list, 3);
    REQUIRE(d.model.openScreen(kProject, under, 0, msOf(0)).has_value());
    const UiScreenId later = d.model.createScreen(kProject, UiScreenDef{}).value();

    d.model.openTrusted();
    const auto refused = d.model.openScreen(kProject, later, 0, msOf(0));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error() == UiRefusal::TrustedOpen);
    const auto hud = d.model.createScreen(kProject, UiScreenDef{.layer = UiLayer::Hud, .title = "", .pause = false});
    CHECK(d.model.openScreen(kProject, hud.value(), 1, msOf(0)).error() == UiRefusal::TrustedOpen);
    // The screen under the trusted one gets no input.
    d.tap(pad::kDown);
    d.back();
    CHECK(d.model.screen(under)->widget(list)->list->selected() == 0);
    CHECK(d.model.top(0) == under);

    d.model.closeTrusted(msOf(d.frame));
    CHECK(d.model.openScreen(kProject, later, 0, msOf(d.frame)).has_value());
}

TEST_CASE("A HUD screen never takes its player's pad") {
    // Expected: Coney's own rule, docs/guides/studio/ui.md#hud
    UiModel model;
    model.registerProject(kProject, "Yard Brawl");
    const UiScreenId hud =
        model.createScreen(kProject, UiScreenDef{.layer = UiLayer::Hud, .title = "", .pause = false}).value();
    REQUIRE(model.openScreen(kProject, hud, 0, 0).has_value());
    CHECK_FALSE(model.takesPad(0));
    CHECK(model.hud().size() == 1);
}

TEST_CASE("A project names only its own screens and one project holds a slot") {
    // Expected: Coney's own rule, docs/guides/mod-ui.md#widgets
    UiModel model;
    const UiScreenId mine = menuScreen(model);
    model.registerProject(kOther, "Other");
    const UiScreenId theirs = model.createScreen(kOther, UiScreenDef{}).value();

    CHECK(model.addWidget(kOther, mine, UiWidgetDef{}).error() == UiRefusal::UnknownScreen);
    CHECK(model.openScreen(kOther, mine, 0, 0).error() == UiRefusal::UnknownScreen);
    CHECK(model.createScreen("never-registered", UiScreenDef{}).error() == UiRefusal::UnknownProject);
    REQUIRE(model.openScreen(kProject, mine, 0, 0).has_value());
    CHECK(model.openScreen(kOther, theirs, 0, 0).error() == UiRefusal::SlotTaken);
    CHECK(model.openScreen(kOther, theirs, 1, 0).has_value());
    CHECK(model.openScreen(kOther, theirs, 2, 0).error() == UiRefusal::BadSlot);
}

TEST_CASE("The UI model refuses past its limits") {
    // Expected: Coney's own rule, docs/guides/studio/ui.md#limits
    UiModel model;
    const UiScreenId screen = menuScreen(model);

    // Screens: 16 alive at once.
    for (std::size_t i = 1; i < ui_limit::kScreens; ++i) {
        REQUIRE(model.createScreen(kProject, UiScreenDef{}).has_value());
    }
    CHECK(model.createScreen(kProject, UiScreenDef{}).error() == UiRefusal::TooManyScreens);

    // Widgets: 512 over all screens; a name only once per screen.
    for (std::size_t i = 0; i < ui_limit::kWidgets; ++i) {
        REQUIRE(model.addWidget(kProject, screen, UiWidgetDef{}).has_value());
    }
    CHECK(model.addWidget(kProject, screen, UiWidgetDef{}).error() == UiRefusal::TooManyWidgets);
}

TEST_CASE("The UI model refuses a long text, a long list and a fifth HUD screen") {
    // Expected: Coney's own rule, docs/guides/studio/ui.md#limits
    UiModel model;
    const UiScreenId screen = menuScreen(model);
    UiWidgetDef text;
    text.text = std::string(ui_limit::kTextChars + 1, 'x');
    CHECK(model.addWidget(kProject, screen, text).error() == UiRefusal::TextTooLong);
    const UiWidgetId shortText = model.addWidget(kProject, screen, UiWidgetDef{}).value();
    CHECK(model.setText(kProject, shortText, std::string(ui_limit::kTextChars + 1, 'x')).error() ==
          UiRefusal::TextTooLong);
    CHECK(model.addItem(kProject, shortText, UiItem{}).error() == UiRefusal::WrongKind);

    const UiWidgetId list = widget(model, screen, UiWidgetKind::List, "list");
    fill(model, list, ui_limit::kListItems);
    CHECK(model.addItem(kProject, list, UiItem{}).error() == UiRefusal::TooManyItems);
    UiWidgetDef sameName;
    sameName.kind = UiWidgetKind::List;
    sameName.name = "list";
    CHECK(model.addWidget(kProject, screen, sameName).error() == UiRefusal::NameTaken);

    for (std::size_t i = 0; i < ui_limit::kHudShown; ++i) {
        const UiScreenId hud =
            model.createScreen(kProject, UiScreenDef{.layer = UiLayer::Hud, .title = "", .pause = false}).value();
        REQUIRE(model.openScreen(kProject, hud, 0, 0).has_value());
    }
    const UiScreenId fifth =
        model.createScreen(kProject, UiScreenDef{.layer = UiLayer::Hud, .title = "", .pause = false}).value();
    CHECK(model.openScreen(kProject, fifth, 0, 0).error() == UiRefusal::TooManyHud);
}

TEST_CASE("Destroying an open menu screen closes it and gives the screen under it the focus") {
    // Expected: Coney's own rule, docs/guides/studio/ui.md#widgets
    Driver d;
    const UiScreenId under = menuScreen(d.model);
    const UiWidgetId underList = widget(d.model, under, UiWidgetKind::List, "list");
    fill(d.model, underList, 3);
    const UiScreenId over = d.model.createScreen(kProject, UiScreenDef{}).value();
    const UiWidgetId overList = widget(d.model, over, UiWidgetKind::List, "list");
    fill(d.model, overList, 3);
    REQUIRE(d.model.openScreen(kProject, under, 0, msOf(0)).has_value());
    REQUIRE(d.model.openScreen(kProject, over, 0, msOf(0)).has_value());
    d.step(0);
    d.model.takeEvents();
    REQUIRE(d.model.top(0) == over);

    REQUIRE(d.model.destroyScreen(kProject, over, msOf(d.frame)).has_value());

    // It is gone with its widgets, the flow is the screen under it, which has its widget focused, and Closed is queued.
    CHECK(d.model.screen(over) == nullptr);
    CHECK_FALSE(d.model.screenOf(overList).has_value());
    CHECK(d.model.flow(0) == std::vector<UiScreenId>{under});
    CHECK(d.model.top(0) == under);
    CHECK(d.model.screen(under)->focused() == underList);
    const std::vector<UiEvent> events = d.model.takeEvents();
    REQUIRE(events.size() == 1);
    CHECK(events.front().kind == UiEventKind::Closed);
    CHECK(events.front().screen == over);
}

TEST_CASE("Destroying a HUD screen takes it off the shown ones and a screen never opened queues nothing") {
    // Expected: Coney's own rule, docs/guides/studio/ui.md#widgets
    UiModel model;
    model.registerProject(kProject, "Yard Brawl");
    const UiScreenId hud =
        model.createScreen(kProject, UiScreenDef{.layer = UiLayer::Hud, .title = "", .pause = false}).value();
    const UiScreenId idle = model.createScreen(kProject, UiScreenDef{}).value();
    REQUIRE(model.openScreen(kProject, hud, 0, 0).has_value());
    REQUIRE(model.hud().size() == 1);

    REQUIRE(model.destroyScreen(kProject, hud, 0).has_value());
    CHECK(model.hud().empty());
    CHECK(model.screen(hud) == nullptr);
    CHECK(kinds(model) == std::vector<UiEventKind>{UiEventKind::Closed});

    REQUIRE(model.destroyScreen(kProject, idle, 0).has_value());
    CHECK(model.screen(idle) == nullptr);
    CHECK(model.takeEvents().empty());
}

TEST_CASE("A project cannot destroy another project's screen or one that is not there") {
    // Expected: Coney's own rule, docs/guides/mod-ui.md#widgets
    UiModel model;
    const UiScreenId mine = menuScreen(model);
    model.registerProject(kOther, "Other");
    REQUIRE(model.openScreen(kProject, mine, 0, 0).has_value());
    model.takeEvents();

    CHECK(model.destroyScreen(kOther, mine, 0).error() == UiRefusal::UnknownScreen);
    CHECK(model.destroyScreen(kProject, UiScreenId{999}, 0).error() == UiRefusal::UnknownScreen);
    CHECK(model.screen(mine) != nullptr);
    CHECK(model.top(0) == mine);
    CHECK(model.takeEvents().empty());
}

TEST_CASE("Destroying a screen frees its widgets and its place among the screens") {
    // Expected: Coney's own rule, docs/guides/studio/ui.md#limits
    UiModel model;
    const UiScreenId full = menuScreen(model);
    std::vector<UiWidgetId> widgets;
    widgets.reserve(ui_limit::kWidgets);
    for (std::size_t i = 0; i < ui_limit::kWidgets; ++i) {
        widgets.push_back(model.addWidget(kProject, full, UiWidgetDef{}).value());
    }
    for (std::size_t i = 1; i < ui_limit::kScreens; ++i) {
        REQUIRE(model.createScreen(kProject, UiScreenDef{}).has_value());
    }
    REQUIRE(model.createScreen(kProject, UiScreenDef{}).error() == UiRefusal::TooManyScreens);
    const UiScreenId spare = model.screens().rbegin()->first;
    REQUIRE(model.addWidget(kProject, spare, UiWidgetDef{}).error() == UiRefusal::TooManyWidgets);

    REQUIRE(model.destroyScreen(kProject, full, 0).has_value());

    // Its widgets no longer name a screen, a screen and the widgets can be made again.
    CHECK_FALSE(model.screenOf(widgets.front()).has_value());
    CHECK_FALSE(model.screenOf(widgets.back()).has_value());
    CHECK(model.setText(kProject, widgets.front(), "x").error() == UiRefusal::UnknownWidget);
    const UiScreenId again = model.createScreen(kProject, UiScreenDef{}).value();
    for (std::size_t i = 0; i < ui_limit::kWidgets; ++i) {
        REQUIRE(model.addWidget(kProject, again, UiWidgetDef{}).has_value());
    }
    CHECK(model.addWidget(kProject, again, UiWidgetDef{}).error() == UiRefusal::TooManyWidgets);
}

TEST_CASE("A widget's position and a bar's fill are set, the fill clamped to 0 to 1 and refused on another kind") {
    // Expected: Coney's own rule, docs/guides/studio/ui.md#widgets
    UiModel model;
    const UiScreenId screen = menuScreen(model);
    const UiWidgetId bar = widget(model, screen, UiWidgetKind::Bar, "bar");
    const UiWidgetId text = widget(model, screen, UiWidgetKind::Text, "text");
    const UiWidgetId unknown{9999};

    REQUIRE(model.setPosition(kProject, bar, 0.25F, 0.75F).has_value());
    CHECK(model.screen(screen)->widget(bar)->def.x == 0.25F);
    CHECK(model.screen(screen)->widget(bar)->def.y == 0.75F);
    CHECK(model.setPosition(kProject, unknown, 0.0F, 0.0F).error() == UiRefusal::UnknownWidget);
    CHECK(model.setPosition(kOther, bar, 0.0F, 0.0F).error() == UiRefusal::UnknownWidget);

    REQUIRE(model.setFill(kProject, bar, 0.5F).has_value());
    CHECK(model.screen(screen)->widget(bar)->fill == 0.5F);
    REQUIRE(model.setFill(kProject, bar, 1.5F).has_value());
    CHECK(model.screen(screen)->widget(bar)->fill == 1.0F);
    REQUIRE(model.setFill(kProject, bar, -0.5F).has_value());
    CHECK(model.screen(screen)->widget(bar)->fill == 0.0F);
    CHECK(model.setFill(kProject, text, 0.5F).error() == UiRefusal::WrongKind);
    CHECK(model.setFill(kProject, unknown, 0.5F).error() == UiRefusal::UnknownWidget);

    CHECK(model.screenOf(bar) == screen);
    CHECK(model.screenOf(text) == screen);
    CHECK_FALSE(model.screenOf(unknown).has_value());
}
