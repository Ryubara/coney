// SPDX-License-Identifier: GPL-3.0-or-later
// The Inspector's page for a selection: the debug menu's item kinds in groups, as the merged entity panel lays them out
// (docs/guides/debug-menu.md#entity-inspector).
#include "studio/inspector.h"

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "debug/menu_model.h"
#include "sandbox/sandbox_layout.h"
#include "studio/level_document.h"

using coney::debug::ItemKind;
using coney::debug::MenuPage;
using coney::studio::inspectorPage;
using coney::studio::LevelDocument;

namespace {

// A layout of a textured box and a spawn point.
LevelDocument boxAndSpawn() {
    auto layout = coney::sandbox::parseSandboxLayout("texture floor dark_grid.png\n"
                                                     "fog start=50 end=150\n"
                                                     "box at=1,2,0 size=4,5,1.5 yaw=30 texture=floor solid=no\n"
                                                     "spawn start at=3,4,0 heading=90\n");
    REQUIRE(layout.has_value());
    return LevelDocument::fromLayout(std::move(*layout));
}

// The text of the watch `label` in the group `group` of `page`.
std::string watched(const MenuPage& page, std::string_view group, std::string_view label) {
    const auto* submenu = page.find(group);
    REQUIRE(submenu != nullptr);
    REQUIRE(submenu->kind == ItemKind::Submenu);
    const auto inner = submenu->open();
    REQUIRE(inner != nullptr);
    const auto* item = inner->find(label);
    REQUIRE(item != nullptr);
    REQUIRE(item->kind == ItemKind::Watch);
    return item->watch();
}

} // namespace

TEST_CASE("the Inspector shows a primitive's identity, transform, shape, look and collision", "[studio]") {
    // Expected: docs/guides/sandbox.md#statements, the layout's own values shown as written.
    const LevelDocument level = boxAndSpawn();
    const auto page = inspectorPage(level, 0);
    CHECK(page->title() == "Inspector");
    CHECK(watched(*page, "Identity", "Name") == "Box 1");
    CHECK(watched(*page, "Identity", "Kind") == "Primitive (box)");
    CHECK(watched(*page, "Transform", "Position") == "1.00, 2.00, 0.00 m");
    CHECK(watched(*page, "Transform", "Yaw") == "30.0 deg");
    CHECK(watched(*page, "Shape", "Size") == "4.00 x 5.00 x 1.50 m");
    CHECK(watched(*page, "Look", "Texture") == "floor (dark_grid.png)");
    CHECK(watched(*page, "Collision", "Solid") == "no");
}

TEST_CASE("the Inspector shows a spawn point's place and heading, and the level with nothing chosen", "[studio]") {
    // Expected: docs/guides/sandbox.md#statements, the layout's own values shown as written.
    const LevelDocument level = boxAndSpawn();
    const auto spawn = inspectorPage(level, 1);
    CHECK(watched(*spawn, "Identity", "Kind") == "Spawn point");
    CHECK(watched(*spawn, "Transform", "Position") == "3.00, 4.00, 0.00 m");
    CHECK(watched(*spawn, "Transform", "Heading") == "90.0 deg");
    CHECK(spawn->find("Shape") == nullptr);

    const auto levelPage = inspectorPage(level, std::nullopt);
    CHECK(watched(*levelPage, "Lighting", "Fog") == "50.0 to 150.0 m");
    CHECK(watched(*levelPage, "Identity", "Entities") == "3"); // a layout with no view gets one behind its first spawn
    CHECK(inspectorPage(level, 42)->find("Lighting") != nullptr);
}
