// SPDX-License-Identifier: GPL-3.0-or-later
// The pad menu's navigation over a model: cursor wrap and paging, submenus and the breadcrumb, the remembered cursor,
// value stepping and clamping, pins and Favourites, and typing text and numbers with the pad's spinners.
#include "debug/menu_navigator.h"

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "debug/menu_model.h"

using coney::debug::ItemKind;
using coney::debug::MenuAction;
using coney::debug::MenuInputFrame;
using coney::debug::MenuModel;
using coney::debug::MenuNavigator;
using coney::debug::MenuPage;
using coney::debug::StepSize;

namespace {

// The state the test pages edit.
struct State {
    double speed = 5.0;
    double count = 3.0;
    bool flag = false;
    std::size_t choice = 0;
    std::string text = "abc";
    int actions = 0;
};

// A model with one page "Tune" (a float, an int, a toggle, a choice, a text, an action, a submenu) and one page
// "Long" of 40 actions.
void buildModel(MenuModel& model, State& state) {
    model.addPage("Tune", [&state](MenuPage& page) {
        auto speed = coney::debug::numberItem(
            "Speed", [&state] { return state.speed; }, [&state](double v) { state.speed = v; }, 0, 10, 0.5, false);
        speed.defaultValue = 5.0;
        page.add(std::move(speed));
        page.add(coney::debug::numberItem(
            "Count", [&state] { return state.count; }, [&state](double v) { state.count = v; }, 0, 100, 1, true));
        page.add(
            coney::debug::toggleItem("Flag", [&state] { return state.flag; }, [&state](bool on) { state.flag = on; }));
        page.add(coney::debug::choiceItem(
            "Choice", {"a", "b", "c"}, [&state] { return state.choice; },
            [&state](std::size_t c) { state.choice = c; }));
        page.add(coney::debug::textItem(
            "Text", [&state] { return state.text; }, [&state](const std::string& t) { state.text = t; }, false));
        page.add(coney::debug::actionItem("Act", [&state] { ++state.actions; }));
        page.add(coney::debug::submenuItem("Inner", [&state] {
            auto inner = std::make_shared<MenuPage>("Inner");
            inner->add(coney::debug::actionItem("Deep", [&state] { state.actions += 10; }));
            return inner;
        }));
    });
    model.addPage("Long", [](MenuPage& page) {
        for (int i = 0; i < 40; ++i) {
            page.add(coney::debug::actionItem("Item " + std::to_string(i), [] {}));
        }
    });
}

// One step's input with `action`.
MenuInputFrame press(MenuAction action, int multiplier = 1, StepSize size = StepSize::Normal) {
    MenuInputFrame frame;
    frame.action = action;
    frame.multiplier = multiplier;
    frame.stepSize = size;
    return frame;
}

// Opens the top-level page `index` (1 is the first after Favourites) from the root.
void enterPage(MenuNavigator& nav, int index) {
    for (int i = 0; i < index; ++i) {
        nav.apply(press(MenuAction::Down));
    }
    nav.apply(press(MenuAction::Accept));
}

} // namespace

TEST_CASE("the menu opens on the root with Favourites first and wraps its cursor", "[debug]") {
    MenuModel model;
    State state;
    buildModel(model, state);
    MenuNavigator nav(model);
    nav.apply(press(MenuAction::Down)); // closed: ignored
    nav.open();
    REQUIRE(nav.page() != nullptr);
    CHECK(nav.page()->title() == "Debug");
    CHECK(nav.current()->label == "Favourites");
    nav.apply(press(MenuAction::Up));
    CHECK(nav.current()->label == "Long");
    nav.apply(press(MenuAction::Back));
    CHECK_FALSE(nav.isOpen());
}

TEST_CASE("left and right step numbers, clamp them and pick fine and coarse steps", "[debug]") {
    MenuModel model;
    State state;
    buildModel(model, state);
    MenuNavigator nav(model);
    nav.open();
    enterPage(nav, 1);
    CHECK(nav.breadcrumb() == std::vector<std::string>{"Debug", "Tune"});
    nav.apply(press(MenuAction::Right));
    CHECK(state.speed == 5.5);
    nav.apply(press(MenuAction::Left, 1, StepSize::Fine));
    CHECK(state.speed == 5.45);
    nav.apply(press(MenuAction::Right, 1, StepSize::Coarse));
    CHECK(state.speed == 10.0); // clamped
    nav.apply(press(MenuAction::Reset));
    CHECK(state.speed == 5.0);
    nav.apply(press(MenuAction::Down));
    nav.apply(press(MenuAction::Right, 10));
    CHECK(state.count == 13.0);
    nav.apply(press(MenuAction::Down));
    nav.apply(press(MenuAction::Accept));
    CHECK(state.flag);
    nav.apply(press(MenuAction::Down));
    nav.apply(press(MenuAction::Left));
    CHECK(state.choice == 2); // wraps
}

TEST_CASE("a submenu opens and Back returns to the cursor where it was", "[debug]") {
    MenuModel model;
    State state;
    buildModel(model, state);
    MenuNavigator nav(model);
    nav.open();
    enterPage(nav, 1);
    for (int i = 0; i < 6; ++i) {
        nav.apply(press(MenuAction::Down));
    }
    REQUIRE(nav.current()->label == "Inner");
    nav.apply(press(MenuAction::Accept));
    CHECK(nav.breadcrumb().back() == "Inner");
    nav.apply(press(MenuAction::Accept));
    CHECK(state.actions == 10);
    nav.apply(press(MenuAction::Back));
    CHECK(nav.current()->label == "Inner");
    // Leaving the page and coming back restores its cursor.
    nav.apply(press(MenuAction::Back));
    nav.apply(press(MenuAction::Accept));
    CHECK(nav.current()->label == "Inner");
}

TEST_CASE("paging moves a screen of items and the scroll keeps the cursor visible", "[debug]") {
    MenuModel model;
    State state;
    buildModel(model, state);
    MenuNavigator nav(model);
    nav.setVisibleRows(10);
    nav.open();
    enterPage(nav, 2);
    nav.apply(press(MenuAction::PageDown));
    CHECK(nav.cursor() == 10);
    CHECK(nav.scrollTop() == 1);
    nav.apply(press(MenuAction::PageDown));
    nav.apply(press(MenuAction::PageDown));
    nav.apply(press(MenuAction::PageDown));
    CHECK(nav.cursor() == 39); // stops at the end
    CHECK(nav.scrollTop() == 30);
    nav.apply(press(MenuAction::Down)); // wraps to the top
    CHECK(nav.cursor() == 0);
    CHECK(nav.scrollTop() == 0);
}

TEST_CASE("square pins an item to Favourites, where it works as itself", "[debug]") {
    MenuModel model;
    State state;
    buildModel(model, state);
    MenuNavigator nav(model);
    nav.open();
    enterPage(nav, 1);
    nav.apply(press(MenuAction::Pin));
    CHECK(model.pinned("Tune/Speed"));
    CHECK(nav.status() == "Pinned to Favourites");
    nav.apply(press(MenuAction::Back));
    nav.apply(press(MenuAction::Up)); // from Tune back to Favourites
    nav.apply(press(MenuAction::Accept));
    REQUIRE(nav.current() != nullptr);
    CHECK(nav.current()->label == "Tune/Speed");
    nav.apply(press(MenuAction::Right));
    CHECK(state.speed == 5.5);
    nav.apply(press(MenuAction::Pin));
    CHECK_FALSE(model.pinned("Tune/Speed"));
    CHECK(nav.current()->kind == ItemKind::Watch); // the empty Favourites placeholder
}

TEST_CASE("text is typed with the spinners and entered with cross", "[debug]") {
    MenuModel model;
    State state;
    buildModel(model, state);
    MenuNavigator nav(model);
    nav.open();
    enterPage(nav, 1);
    for (int i = 0; i < 4; ++i) {
        nav.apply(press(MenuAction::Down));
    }
    REQUIRE(nav.current()->label == "Text");
    nav.apply(press(MenuAction::Accept));
    REQUIRE(nav.editing());
    CHECK(nav.editText() == "abc");
    CHECK(nav.editCursor() == 2);
    nav.apply(press(MenuAction::Up));    // c -> d
    nav.apply(press(MenuAction::Right)); // a new character, copying d
    nav.apply(press(MenuAction::Down));  // d -> c
    CHECK(nav.editText() == "abdc");
    nav.apply(press(MenuAction::Pin)); // delete it
    nav.apply(press(MenuAction::Accept));
    CHECK_FALSE(nav.editing());
    CHECK(state.text == "abd");
    // Circle cancels.
    nav.apply(press(MenuAction::Accept));
    nav.apply(press(MenuAction::Up));
    nav.apply(press(MenuAction::Back));
    CHECK(state.text == "abd");
    CHECK(nav.isOpen());
}

TEST_CASE("a number can be typed exactly, and a bad one is refused", "[debug]") {
    MenuModel model;
    State state;
    buildModel(model, state);
    MenuNavigator nav(model);
    nav.open();
    enterPage(nav, 1);
    nav.apply(press(MenuAction::Accept)); // Speed 5
    CHECK(nav.editText() == "5");
    nav.apply(press(MenuAction::Down));  // 5 -> 4
    nav.apply(press(MenuAction::Right)); // 44
    nav.apply(press(MenuAction::Accept));
    CHECK(state.speed == 10.0); // 44 clamped to the range
    nav.apply(press(MenuAction::Accept));
    for (int i = 0; i < 11; ++i) {
        nav.apply(press(MenuAction::Up)); // the last 0 turned round to '-': "1-"
    }
    nav.apply(press(MenuAction::Left));
    nav.apply(press(MenuAction::Accept));
    CHECK(nav.status() == "Not a number");
    CHECK(nav.editing());
}
