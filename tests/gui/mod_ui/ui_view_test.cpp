// SPDX-License-Identifier: GPL-3.0-or-later
// The UI widget model's renderer (docs/guides/studio/ui.md#no-spoofing): the frame label in the trusted layer above
// every widget, which screens are drawn and which projects are labelled.
// Synthetic font, sheet and screens only.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "graphics/font.h"
#include "graphics/sprite_batch.h"
#include "gui/mod_ui/ui_model.h"
#include "gui/mod_ui/ui_view.h"
#include "support/font_fixtures.h"
#include "support/recording_device.h"

using coney::graphics::SpriteBatch;
using coney::graphics::SpriteSheet;
using coney::graphics::UvRect;
using coney::gui::GuiFrame;
using coney::gui::UiItem;
using coney::gui::UiLayer;
using coney::gui::UiModel;
using coney::gui::UiScreenDef;
using coney::gui::UiScreenId;
using coney::gui::UiView;
using coney::gui::UiWidgetDef;
using coney::gui::UiWidgetId;
using coney::gui::UiWidgetKind;

namespace {

constexpr const char* kProject = "yard-brawl";
constexpr const char* kTitle = "Yard Brawl";
constexpr const char* kOther = "other-mod";
constexpr const char* kOtherTitle = "Other Mod";

// The synthetic font in every slot and one sprite sheet, `menu_system`, of two rectangles.
struct Resources {
    coney::graphics::Font font = coney::test::testFont();
    SpriteSheet sheet = [] {
        SpriteSheet s;
        s.page.rects = {UvRect{0.0F, 0.0F, 0.5F, 0.5F}, UvRect{0.5F, 0.5F, 1.0F, 1.0F}};
        s.texture = std::make_shared<coney::test::FakeTexture>(256, 128);
        return s;
    }();
    UiView view{[this](int /*slot*/) { return &font; },
                [this](std::string_view name) { return name == "menu_system" ? &sheet : nullptr; }};
};

// A menu screen of kProject with a text, a sprite and a grid.
UiScreenId populatedScreen(UiModel& model) {
    model.registerProject(kProject, kTitle);
    const UiScreenId screen =
        model.createScreen(kProject, UiScreenDef{.layer = UiLayer::Menu, .title = "Rounds", .pause = false}).value();
    UiWidgetDef text;
    text.text = "ROUNDS";
    text.x = 0.2F;
    text.y = 0.2F;
    REQUIRE(model.addWidget(kProject, screen, text).has_value());
    UiWidgetDef sprite;
    sprite.kind = UiWidgetKind::Sprite;
    sprite.sheet = "menu_system";
    sprite.rect = 1;
    REQUIRE(model.addWidget(kProject, screen, sprite).has_value());
    UiWidgetDef grid;
    grid.kind = UiWidgetKind::Grid;
    grid.rows = {2};
    const UiWidgetId g = model.addWidget(kProject, screen, grid).value();
    REQUIRE(model.addItem(kProject, g, UiItem{.text = "THREE", .value = "3", .enabled = true}).has_value());
    REQUIRE(model.addItem(kProject, g, UiItem{.text = "FIVE", .value = "5", .enabled = true}).has_value());
    return screen;
}

// The number of sprites in `batch`, 0 for none.
std::size_t spritesIn(const SpriteBatch* batch) { return batch == nullptr ? 0 : batch->sprites().size(); }

} // namespace

TEST_CASE("The frame label is drawn above every widget") {
    // Expected: Coney's own rule, docs/guides/studio/ui.md#no-spoofing
    Resources res;
    UiModel model;
    const UiScreenId screen = populatedScreen(model);
    // A widget of the project's placed exactly where the label goes, with the label's own text.
    UiWidgetDef imitation;
    imitation.text = kTitle;
    imitation.x = UiView::kLabelX;
    imitation.y = UiView::kLabelY;
    imitation.alignment = coney::gui::TextAlignment::Right;
    REQUIRE(model.addWidget(kProject, screen, imitation).has_value());
    REQUIRE(model.openScreen(kProject, screen, 0, 0).has_value());

    res.view.list(model, GuiFrame{});

    // The label is the registered title, in the trusted layer's batch.
    CHECK(res.view.labels().at(0) == kTitle);
    const SpriteBatch* label = res.view.labelBatch(coney::gui::kTextFontSlot);
    REQUIRE(spritesIn(label) > 0);
    // Every batch holding a widget's sprites is below the label's: the imitation too.
    const SpriteBatch* text = res.view.textBatch(coney::gui::kTextFontSlot);
    const SpriteBatch* big = res.view.textBatch(coney::gui::kBigFontSlot);
    const SpriteBatch* sprite = res.view.spriteBatch("menu_system");
    REQUIRE(spritesIn(text) > 0);
    REQUIRE(spritesIn(big) > 0); // the grid's items, in big_font
    REQUIRE(spritesIn(sprite) > 0);
    for (const SpriteBatch* batch : {text, big, sprite}) {
        CHECK(batch->depth() < label->depth());
    }
    // Above the highest 2D depth Coney draws at: the HUD's lock-pick dial face, 20,001 (src/platform/hud_layer.cpp).
    CHECK(label->depth() > 20001.0F);
    // The label batch holds the label alone: removing the imitation leaves it as it was.
    const std::size_t labelSprites = spritesIn(label);
    REQUIRE(model.removeWidget(kProject, model.screen(screen)->widgets().back()->id, 0).has_value());
    res.view.list(model, GuiFrame{});
    CHECK(spritesIn(res.view.labelBatch(coney::gui::kTextFontSlot)) == labelSprites);
}

TEST_CASE("The view draws only the top screen of each flow and labels every shown screen's project") {
    // Expected: Coney's own rule, docs/guides/studio/ui.md#no-spoofing
    Resources res;
    UiModel model;
    const UiScreenId under = populatedScreen(model);
    const UiScreenId over = model.createScreen(kProject, UiScreenDef{}).value();
    const UiScreenId hud =
        model.createScreen(kProject, UiScreenDef{.layer = UiLayer::Hud, .title = "", .pause = false}).value();
    UiWidgetDef counter;
    counter.text = "3";
    REQUIRE(model.addWidget(kProject, hud, counter).has_value());
    model.registerProject(kOther, kOtherTitle);
    const UiScreenId otherHud =
        model.createScreen(kOther, UiScreenDef{.layer = UiLayer::Hud, .title = "", .pause = false}).value();

    // Nothing open: nothing drawn, no label.
    res.view.list(model, GuiFrame{});
    CHECK(spritesIn(res.view.textBatch(coney::gui::kTextFontSlot)) == 0);
    CHECK(res.view.labels().empty());

    // A HUD screen draws, with its project's label.
    REQUIRE(model.openScreen(kProject, hud, 0, 0).has_value());
    res.view.list(model, GuiFrame{});
    const std::size_t hudSprites = spritesIn(res.view.textBatch(coney::gui::kTextFontSlot));
    CHECK(hudSprites > 0);
    CHECK(res.view.labels() == std::vector<std::string>{kTitle});

    // The empty screen over the populated one hides it; its project, already labelled, gets no second line.
    REQUIRE(model.openScreen(kProject, under, 0, 0).has_value());
    REQUIRE(model.openScreen(kProject, over, 0, 0).has_value());
    res.view.list(model, GuiFrame{});
    CHECK(spritesIn(res.view.spriteBatch("menu_system")) == 0);
    CHECK(spritesIn(res.view.textBatch(coney::gui::kTextFontSlot)) == hudSprites);
    CHECK(res.view.labels() == std::vector<std::string>{kTitle});

    // Another project's HUD screen, even an empty one, adds its own line under the first.
    REQUIRE(model.openScreen(kOther, otherHud, 1, 0).has_value());
    res.view.list(model, GuiFrame{});
    CHECK(res.view.labels() == std::vector<std::string>{kTitle, kOtherTitle});
}
