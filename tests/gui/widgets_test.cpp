// SPDX-License-Identifier: GPL-3.0-or-later
// The widgets the first menu screens use: the widget base, the sprite widget, the text widget, the option grid and
// the usage line.

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "graphics/font.h"
#include "graphics/overlay_camera.h"
#include "graphics/sprite_batch.h"
#include "gui/base_widget.h"
#include "gui/menu_input.h"
#include "gui/option_grid.h"
#include "gui/text_widget.h"
#include "gui/usage_info.h"
#include "gui/widget.h"
#include "support/font_fixtures.h"
#include "support/recording_device.h"

using Catch::Approx;
using coney::graphics::OverlayCamera;
using coney::graphics::Rgba;
using coney::graphics::SpriteBatch;
using coney::graphics::SpriteSheet;
using coney::graphics::UvRect;
using coney::gui::BaseWidget;
using coney::gui::GuiCanvas;
using coney::gui::GuiFrame;
using coney::gui::MenuCommand;
using coney::gui::OptionGrid;
using coney::gui::OptionGridLayout;
using coney::gui::TextWidget;
using coney::gui::UsageInfo;
using coney::gui::Widget;
using coney::gui::WidgetRect;

namespace {

// A widget that counts its init and shutdown work.
class CountingWidget final : public Widget {
  public:
    int inits = 0;
    int shutdowns = 0;

  protected:
    void onInit() override { ++inits; }
    void onShutdown() override { ++shutdowns; }
};

// A two-rectangle sheet over a fake 256 x 128 texture.
SpriteSheet twoRectSheet() {
    SpriteSheet sheet;
    sheet.page.rects = {UvRect{0.0F, 0.0F, 0.5F, 0.5F}, UvRect{0.5F, 0.5F, 1.0F, 1.0F}};
    sheet.texture = std::make_shared<coney::test::FakeTexture>(256, 128);
    return sheet;
}

// The synthetic font in every slot, drawing into one batch.
struct TextCanvas {
    coney::graphics::Font font = coney::test::testFont();
    SpriteBatch batch{font.sheet(), 4096, 9000.0F};
    GuiCanvas canvas{
        .fonts = [this](int /*slot*/) { return &font; },
        .textBatch = [this](int /*slot*/) { return &batch; },
    };
};

} // namespace

TEST_CASE("widget: init runs once until shutdown", "[widgets]") {
    CountingWidget widget;
    widget.init();
    widget.init();
    CHECK(widget.inits == 1);
    CHECK(widget.initialised());
    widget.shutdown();
    widget.shutdown();
    CHECK(widget.shutdowns == 1);
    widget.init();
    CHECK(widget.inits == 2);
}

TEST_CASE("sprite widget: one sprite of its rectangle, after a black shadow at alpha * 128 / 255", "[widgets]") {
    SpriteBatch batch(twoRectSheet(), 10, 8500.0F);
    BaseWidget widget(&batch, 1);
    widget.setup(WidgetRect{0.25F, 0.5F, 0.2F, 0.1F}, Rgba{255, 255, 255, 200}, true);
    widget.render({});
    REQUIRE(batch.sprites().size() == 2);
    const auto& shadow = batch.sprites()[0];
    const auto& sprite = batch.sprites()[1];
    CHECK(shadow.colour == Rgba{0, 0, 0, 200 * 128 / 255});
    CHECK(sprite.colour == Rgba{255, 255, 255, 200});
    CHECK(sprite.uv == UvRect{0.5F, 0.5F, 1.0F, 1.0F});
    CHECK(sprite.position == OverlayCamera::guiToOverlay(0.25F, 0.5F));
    CHECK(shadow.position == OverlayCamera::guiToOverlay(0.25F + 0.0025F, 0.5F + 0.004F));
    CHECK(sprite.width == Approx(OverlayCamera::guiWidthToOverlay(0.2F)));
    CHECK(sprite.height == Approx(0.1F));
}

TEST_CASE("sprite widget: nothing when hidden, without a batch or past the sheet's rectangles", "[widgets]") {
    SpriteBatch batch(twoRectSheet(), 10, 8500.0F);
    BaseWidget hidden(&batch, 0);
    hidden.setVisible(false);
    hidden.render({});
    BaseWidget pastEnd(&batch, 2);
    pastEnd.render({});
    BaseWidget unbound(nullptr, 0);
    unbound.render({});
    CHECK(batch.sprites().empty());
    unbound.setBatch(&batch);
    unbound.setup(WidgetRect{}, coney::graphics::kWhite, false);
    unbound.render({});
    CHECK(batch.sprites().size() == 1);
}

TEST_CASE("text widget: lays out its text centred, faded, with its time from the first update", "[widgets]") {
    TextCanvas canvas;
    TextWidget text;
    text.setText("AB");
    text.centreOn(0.5F, 0.3F, 0.6F);
    text.setFade(0.5F);
    text.update(GuiFrame{.timeMs = 1000, .pad = nullptr});
    text.update(GuiFrame{.timeMs = 1250, .pad = nullptr});
    CHECK(text.style().timeMs == 250);
    CHECK(text.style().x == Approx(0.2F));
    CHECK(text.style().alignment == coney::gui::TextAlignment::Centre);
    text.render(canvas.canvas);
    // Two glyphs, each after its shadow.
    REQUIRE(canvas.batch.sprites().size() == 4);
    // Half of 255, rounded either way.
    CHECK(canvas.batch.sprites()[1].colour.a >= 127);
    CHECK(canvas.batch.sprites()[1].colour.a <= 128);
    // setText restarts the time.
    text.setText("C");
    text.update(GuiFrame{.timeMs = 2000, .pad = nullptr});
    CHECK(text.style().timeMs == 0);
}

TEST_CASE("text widget: nothing drawn when hidden or without fonts", "[widgets]") {
    TextCanvas canvas;
    TextWidget text;
    text.setText("AB");
    text.render(GuiCanvas{});
    text.setVisible(false);
    text.render(canvas.canvas);
    CHECK(canvas.batch.sprites().empty());
    CHECK(text.layout(GuiCanvas{}).sprites.empty());
}

TEST_CASE("option grid: up and down move the selection with wrapping; accept returns the code", "[widgets]") {
    OptionGrid grid;
    grid.setup(OptionGridLayout{});
    CHECK_FALSE(grid.handle(MenuCommand::Accept).has_value());
    grid.addItem("one", 0);
    grid.addItem("two", 5);
    grid.addItem("three", 1);
    CHECK(grid.selected() == 0);
    CHECK(grid.handle(MenuCommand::Accept) == 0);
    CHECK_FALSE(grid.handle(MenuCommand::Down).has_value());
    CHECK(grid.handle(MenuCommand::Accept) == 5);
    CHECK_FALSE(grid.handle(MenuCommand::Down).has_value());
    CHECK_FALSE(grid.handle(MenuCommand::Down).has_value());
    CHECK(grid.selected() == 0);
    CHECK_FALSE(grid.handle(MenuCommand::Up).has_value());
    CHECK(grid.selected() == 2);
    CHECK_FALSE(grid.handle(MenuCommand::Left).has_value());
    CHECK_FALSE(grid.handle(MenuCommand::Back).has_value());
    CHECK(grid.selected() == 2);
    CHECK(grid.code(1) == 5);
}

TEST_CASE("option grid: the selected item is drawn at 1.15 times the size and in its colour", "[widgets]") {
    OptionGrid grid;
    OptionGridLayout layout;
    layout.top = 0.4F;
    layout.rowGap = 0.1F;
    grid.setup(layout);
    grid.addItem("one", 0);
    grid.addItem("two", 1);
    grid.select(1);
    CHECK(grid.item(0).style().scale == Approx(1.0F));
    CHECK(grid.item(1).style().scale == Approx(1.15F));
    CHECK(grid.item(1).style().colour == layout.selectedColour);
    CHECK(grid.item(0).style().colour == layout.colour);
    CHECK(grid.item(1).style().y == Approx(0.5F));
    TextCanvas canvas;
    grid.render(canvas.canvas);
    CHECK(canvas.batch.sprites().size() == 12); // two items of three glyphs, each after its shadow
}

TEST_CASE("option grid: at most 50 items; taking focus resets the menu input", "[widgets]") {
    OptionGrid grid;
    grid.setup(OptionGridLayout{});
    for (std::size_t i = 0; i < OptionGrid::kMaxItems; ++i) {
        REQUIRE(grid.addItem("x", static_cast<int>(i)));
    }
    CHECK_FALSE(grid.addItem("x", 99));
    CHECK(grid.items() == OptionGrid::kMaxItems);

    coney::Pad pad;
    coney::PadSample held;
    held.connected = true;
    held.buttons = coney::pad::kUp;
    coney::gui::MenuInput input;
    pad.update(held);
    REQUIRE(input.dispatch(pad, 1000) == MenuCommand::Up);
    grid.takeFocus(input, 1000);
    pad.update(coney::PadSample{.connected = true, .buttons = 0, .sticks = held.sticks, .pressure = held.pressure});
    pad.update(held);
    CHECK_FALSE(input.dispatch(pad, 1010).has_value()); // the d-pad is blocked for 20 ms
    pad.update(coney::PadSample{.connected = true, .buttons = 0, .sticks = held.sticks, .pressure = held.pressure});
    pad.update(held);
    CHECK(input.dispatch(pad, 1020) == MenuCommand::Up); // and the 110 ms gap was forgotten
}

TEST_CASE("usage line: centred on the screen at the given height", "[widgets]") {
    UsageInfo usage;
    usage.setLegend("<X> ok", 0.84F);
    CHECK(usage.text() == "<X> ok");
    CHECK(usage.style().y == Approx(0.84F));
    CHECK(usage.style().x + usage.style().boxWidth / 2.0F == Approx(0.5F));
    CHECK(UsageInfo::kMenuUsageString == 0x1f);
}
