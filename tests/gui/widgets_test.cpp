// SPDX-License-Identifier: GPL-3.0-or-later
// The menus' widget classes (docs/research/gui.md#widget-classes): the widget base, the sprite widget, the text
// widget, the option grid, the usage line, the bar and the message box, and the colour table, over a synthetic font
// and sheet.

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "graphics/font.h"
#include "graphics/overlay_camera.h"
#include "graphics/sprite_batch.h"
#include "gui/bar.h"
#include "gui/base_widget.h"
#include "gui/colour_table.h"
#include "gui/menu_input.h"
#include "gui/message_box.h"
#include "gui/option_grid.h"
#include "gui/scrolling_menu.h"
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
using coney::gui::BaseWidgetSetup;
using coney::gui::GuiCanvas;
using coney::gui::GuiFrame;
using coney::gui::MenuCommand;
using coney::gui::OptionGrid;
using coney::gui::OptionGridItem;
using coney::gui::OptionGridSetup;
using coney::gui::SpriteAnchor;
using coney::gui::TextWidget;
using coney::gui::UsageInfo;
using coney::gui::Widget;

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

// A two-rectangle sheet over a fake 256 x 128 texture: rectangle 0 is 128 x 64 texels, rectangle 1 128 x 64.
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

// The synthetic font's advance for one ordinary character at `scale` in proportional mode: half a glyph width (its
// rectangles are half as wide as high) plus the spacing.
float advanceAt(float scale) { return scale / 30.0F / 2.0F + 0.003F; }

// A pad record after one sample with `buttons` held.
void sample(coney::Pad& pad, std::uint16_t buttons) {
    coney::PadSample held;
    held.connected = true;
    held.buttons = buttons;
    held.pressure.fill(255);
    pad.update(held);
}

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

TEST_CASE("colour table: the menu colours at their addresses", "[widgets]") {
    CHECK(coney::gui::kColourTable.size() == 26);
    CHECK(coney::gui::colourAt(0x005fd260) == Rgba{0, 0, 0, 255});
    CHECK(coney::gui::colourAt(0x005fd268) == Rgba{255, 255, 255, 255});
    CHECK(coney::gui::kMenuGrey == Rgba{178, 178, 178, 255});
    CHECK(coney::gui::kSelectedGrey == Rgba{178, 178, 178, 255});
    CHECK(coney::gui::kDimGrey == Rgba{80, 80, 80, 255});
    CHECK(coney::gui::kMenuRed == Rgba{170, 43, 43, 255});
    CHECK(coney::gui::colourAt(0x005fd308) == Rgba{36, 75, 130, 255});
}

TEST_CASE("sprite widget: height in overlay units, width from the rectangle's shape, centred by default", "[widgets]") {
    SpriteBatch batch(twoRectSheet(), 10, 8500.0F);
    BaseWidget widget(&batch, 1);
    widget.setup(BaseWidgetSetup{.x = 0.25F, .y = 0.5F, .height = 0.1F, .colour = Rgba{255, 255, 255, 200}});
    widget.render({});
    REQUIRE(batch.sprites().size() == 1); // no shadow unless asked for
    const auto& sprite = batch.sprites()[0];
    CHECK(sprite.colour == Rgba{255, 255, 255, 200});
    CHECK(sprite.uv == UvRect{0.5F, 0.5F, 1.0F, 1.0F});
    CHECK(sprite.position == OverlayCamera::guiToOverlay(0.25F, 0.5F));
    // 128 x 64 texels: twice as wide as high.
    CHECK(sprite.width == Approx(0.2F));
    CHECK(sprite.height == Approx(0.1F));
}

TEST_CASE("sprite widget: the left anchor moves it right by half its width; the shadow stays unshifted", "[widgets]") {
    SpriteBatch batch(twoRectSheet(), 10, 8500.0F);
    BaseWidget widget(&batch, 0);
    widget.setup(BaseWidgetSetup{.x = 0.0F,
                                 .y = 0.2F,
                                 .height = 0.33F,
                                 .colour = coney::gui::kMenuRed,
                                 .anchor = SpriteAnchor::Left,
                                 .shadow = true});
    widget.render({});
    REQUIRE(batch.sprites().size() == 2);
    const float guiWidth = 0.66F * 448.0F / 640.0F;
    CHECK(widget.centre().first == Approx(guiWidth / 2.0F));
    CHECK(batch.sprites()[1].position == OverlayCamera::guiToOverlay(guiWidth / 2.0F, 0.2F));
    CHECK(batch.sprites()[0].position == OverlayCamera::guiToOverlay(0.0025F, 0.204F));
    CHECK(batch.sprites()[0].colour == Rgba{0, 0, 0, 128});
    // The right anchor, and the aspect fix's widening.
    widget.setup(
        BaseWidgetSetup{.x = 1.0F, .y = 0.5F, .height = 0.1F, .anchor = SpriteAnchor::Right, .aspectFix = true});
    CHECK(widget.overlaySize().first == Approx(0.2F * 1.45F * 0.80357F));
    CHECK(widget.centre().first == Approx(1.0F - widget.overlaySize().first * 448.0F / 640.0F / 2.0F));
    // A width given outright.
    widget.setup(BaseWidgetSetup{.height = 1.1F, .width = 1.15F});
    CHECK(widget.overlaySize().first == Approx(1.15F));
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

TEST_CASE("text widget: set up left at x, centred on x, or ending at x", "[widgets]") {
    TextCanvas canvas;
    TextWidget text;
    text.setText("ABCD");
    const float width = 4.0F * advanceAt(1.0F);
    text.setup(coney::gui::TextWidgetSetup{.x = 0.1F, .y = 0.5F, .colour = coney::gui::kMenuRed});
    CHECK(text.width(canvas.canvas) == Approx(width));
    CHECK(text.layout(canvas.canvas).sprites[1].sprite.colour == coney::gui::kMenuRed);
    // The first glyph's centre: the pen plus half its advance.
    const auto firstX = [&] {
        const auto point = text.layout(canvas.canvas).sprites[1].sprite.position;
        return point.x * 448.0F / 640.0F + 0.5F; // back to GUI x
    };
    CHECK(firstX() == Approx(0.1F + advanceAt(1.0F) / 2.0F));
    text.setup(coney::gui::TextWidgetSetup{.x = 0.5F, .y = 0.5F, .alignment = coney::gui::TextAlignment::Centre});
    CHECK(firstX() == Approx(0.5F - width / 2.0F + advanceAt(1.0F) / 2.0F));
    text.setup(coney::gui::TextWidgetSetup{.x = 0.9F, .y = 0.5F, .alignment = coney::gui::TextAlignment::Right});
    CHECK(firstX() == Approx(0.9F - width + advanceAt(1.0F) / 2.0F));
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

TEST_CASE("option grid: rows packed left from x, each row one pitch down; separators follow their item", "[widgets]") {
    TextCanvas canvas;
    OptionGrid grid;
    grid.setup(OptionGridSetup{.y = 0.76F, .rows = {2, 1}, .leftX = 0.0F});
    grid.addItem(OptionGridItem{.text = "AB", .code = 0, .separator = true});
    grid.addItem(OptionGridItem{.text = "CD", .code = 5});
    grid.addItem(OptionGridItem{.text = "EFG", .code = 1});
    CHECK(grid.rows() == 2);
    CHECK(grid.cell(1) == std::pair<std::size_t, std::size_t>{0, 1});
    CHECK(grid.cell(2) == std::pair<std::size_t, std::size_t>{1, 0});
    // Item 1 starts after "AB" and " : " (two spaces at half of 0.56 of a glyph cell, and a colon).
    const float space = (1.15F / 30.0F + 0.003F) * 0.56F / 2.0F;
    const float expected = 2.0F * advanceAt(1.15F) + 2.0F * space + advanceAt(1.15F);
    CHECK(grid.itemPosition(1, canvas.canvas).first == Approx(expected));
    CHECK(grid.itemPosition(1, canvas.canvas).second == Approx(0.76F));
    CHECK(grid.itemPosition(2, canvas.canvas).first == Approx(0.0F));
    // The row pitch: 6h / 7 of size 1.15 plus Coney's addition, the measured 0.0505.
    CHECK(grid.rowPitch(1.15F) == Approx(0.0505F).margin(0.0003F));
    CHECK(grid.itemPosition(2, canvas.canvas).second == Approx(0.76F + grid.rowPitch(1.15F)));
    // Drawn: 2 + 1 (the colon; spaces draw nothing) + 2 + 3 glyphs, each after its shadow.
    grid.render(canvas.canvas);
    CHECK(canvas.batch.sprites().size() == 16U); // 8 glyphs, each after its shadow
}

TEST_CASE("option grid: a centred row is centred on its x", "[widgets]") {
    TextCanvas canvas;
    OptionGrid grid;
    grid.setup(OptionGridSetup{.y = 0.745F, .rows = {2}, .centreX = 0.5F});
    grid.addItem(OptionGridItem{.text = "AB", .code = 0});
    grid.addItem(OptionGridItem{.text = "CD", .code = 1});
    const float rowWidth = 4.0F * advanceAt(1.15F);
    CHECK(grid.itemPosition(0, canvas.canvas).first == Approx(0.5F - rowWidth / 2.0F));
    CHECK(grid.itemPosition(1, canvas.canvas).first == Approx(0.5F));
}

TEST_CASE("option grid: colour only marks the selection, grey when focused; the separator keeps its colour",
          "[widgets]") {
    TextCanvas canvas;
    coney::gui::MenuInput input;
    OptionGrid grid;
    grid.setup(OptionGridSetup{.y = 0.5F, .rows = {2}, .leftX = 0.0F});
    grid.addItem(OptionGridItem{.text = "A", .code = 0, .separator = true});
    grid.addItem(OptionGridItem{.text = "B", .code = 1});
    CHECK(grid.itemColour(0) == coney::gui::kMenuRed); // not focused yet
    grid.takeFocus(input, 0);
    CHECK(grid.itemColour(0) == coney::gui::kSelectedGrey);
    CHECK(grid.itemColour(1) == coney::gui::kMenuRed);
    grid.render(canvas.canvas);
    // A, its shadow; the colon in red after its shadow; B.
    REQUIRE(canvas.batch.sprites().size() == 6);
    CHECK(canvas.batch.sprites()[1].colour == coney::gui::kSelectedGrey);
    CHECK(canvas.batch.sprites()[3].colour == coney::gui::kMenuRed);
    CHECK(canvas.batch.sprites()[1].height == Approx(canvas.batch.sprites()[5].height)); // no size change
    grid.loseFocus();
    CHECK(grid.itemColour(0) == coney::gui::kMenuRed);
}

TEST_CASE("option grid: moves, wrap from three items, refused moves and disabled items", "[widgets]") {
    std::vector<int> cues;
    OptionGrid grid;
    grid.setup(OptionGridSetup{
        .rows = {2, 1}, .leftX = 0.0F, .moveCue = 5, .playCue = [&cues](int cue) { cues.push_back(cue); }});
    CHECK_FALSE(grid.handle(MenuCommand::Accept).has_value());
    grid.addItem(OptionGridItem{.text = "a", .code = 0});
    grid.addItem(OptionGridItem{.text = "b", .code = 5});
    grid.addItem(OptionGridItem{.text = "c", .code = 1});
    CHECK(grid.handle(MenuCommand::Accept) == 0);
    CHECK_FALSE(grid.handle(MenuCommand::Right).has_value());
    CHECK(grid.selected() == 1);
    CHECK(cues == std::vector<int>{5});
    CHECK_FALSE(grid.handle(MenuCommand::Down).has_value()); // column 1 clamped to row 1's one item
    CHECK(grid.selected() == 2);
    CHECK_FALSE(grid.handle(MenuCommand::Down).has_value()); // wraps to the first row, column 0
    CHECK(grid.selected() == 0);
    CHECK_FALSE(grid.handle(MenuCommand::Left).has_value()); // wraps to the last item
    CHECK(grid.selected() == 2);
    CHECK_FALSE(grid.handle(MenuCommand::Back).has_value());

    // Two items: no wrap; a refused move plays 0xe.
    cues.clear();
    grid.setup(OptionGridSetup{.rows = {2}, .playCue = [&cues](int cue) { cues.push_back(cue); }});
    grid.addItem(OptionGridItem{.text = "a", .code = 0});
    grid.addItem(OptionGridItem{.text = "b", .code = 1});
    CHECK_FALSE(grid.handle(MenuCommand::Left).has_value());
    CHECK(grid.selected() == 0);
    CHECK_FALSE(grid.handle(MenuCommand::Up).has_value()); // one row: up does nothing, silently
    CHECK(cues == std::vector<int>{OptionGrid::kRefusedCue});
    (void)grid.handle(MenuCommand::Right);
    CHECK(grid.selected() == 1);
    CHECK(cues.back() == 4); // the default move cue

    // A disabled item is skipped and cannot be accepted.
    grid.setup(OptionGridSetup{.rows = {3}});
    grid.addItem(OptionGridItem{.text = "a", .code = 0});
    grid.addItem(OptionGridItem{.text = "b", .code = 1, .enabled = false});
    grid.addItem(OptionGridItem{.text = "c", .code = 2});
    (void)grid.handle(MenuCommand::Right);
    CHECK(grid.selected() == 2);
    grid.select(1);
    CHECK_FALSE(grid.handle(MenuCommand::Accept).has_value());

    // Staying in the row.
    grid.setup(OptionGridSetup{.rows = {2, 2}, .stayInRow = true});
    for (int i = 0; i < 4; ++i) {
        grid.addItem(OptionGridItem{.text = "x", .code = i});
    }
    (void)grid.handle(MenuCommand::Right);
    (void)grid.handle(MenuCommand::Right);
    CHECK(grid.selected() == 0);
}

TEST_CASE("option grid: a refused move stops the d-pad repeating until the next move", "[widgets]") {
    coney::Pad pad;
    coney::gui::MenuInput input;
    OptionGrid grid;
    grid.setup(OptionGridSetup{.rows = {2}});
    grid.addItem(OptionGridItem{.text = "a", .code = 0});
    grid.addItem(OptionGridItem{.text = "b", .code = 1});
    grid.takeFocus(input, 0);
    // Hold left for 30 samples: the first press is refused, and the auto-repeat (sample 15) gives nothing.
    std::uint64_t now = 100;
    int commands = 0;
    for (int i = 0; i < 30; ++i, now += 33) {
        sample(pad, coney::pad::kLeft);
        if (const auto command = input.dispatch(pad, now)) {
            ++commands;
            CHECK_FALSE(grid.handle(*command).has_value());
        }
    }
    CHECK(commands == 1);
}

TEST_CASE("option grid: at most 50 items; taking focus resets the menu input", "[widgets]") {
    OptionGrid grid;
    grid.setup(OptionGridSetup{});
    for (std::size_t i = 0; i < OptionGrid::kMaxItems; ++i) {
        REQUIRE(grid.addItem(OptionGridItem{.text = "x", .code = static_cast<int>(i)}));
    }
    CHECK_FALSE(grid.addItem(OptionGridItem{.text = "x", .code = 99}));
    CHECK(grid.items() == OptionGrid::kMaxItems);
    CHECK(grid.rows() == OptionGrid::kMaxItems); // no row counts: a row each

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

TEST_CASE("usage line: grey, size 1.0, part_page0, left-aligned or centred at its place", "[widgets]") {
    UsageInfo usage;
    usage.place(0.0F, 0.87F, true);
    usage.setLegend("<X> ok");
    CHECK(usage.text() == "<X> ok");
    CHECK(usage.style().x == Approx(0.0F));
    CHECK(usage.style().y == Approx(0.87F));
    CHECK(usage.style().alignment == coney::gui::TextAlignment::Left);
    CHECK(usage.style().colour == coney::gui::kMenuGrey);
    CHECK(usage.style().scale == Approx(1.0F));
    CHECK(usage.style().fontSlot == coney::gui::kTextFontSlot);
    usage.place(0.5F, 0.91F, false);
    CHECK(usage.style().alignment == coney::gui::TextAlignment::Centre);
    usage.setLegend(std::string(200, 'x'));
    CHECK(usage.text().size() == UsageInfo::kMaxLength);
    CHECK(UsageInfo::kMenuUsageString == 0x1f);
}

TEST_CASE("bar: the back at full width, the fill from the left edge", "[widgets]") {
    SpriteBatch batch(twoRectSheet(), 10, 8500.0F);
    coney::gui::Bar bar(&batch, 1);
    bar.setup(coney::gui::BarSetup{.x = 0.0F, .y = 0.815F, .width = 0.6F, .height = 0.025F});
    bar.setFill(0.4F);
    bar.render({});
    REQUIRE(batch.sprites().size() == 2);
    const float guiWidth = 0.6F * 448.0F / 640.0F;
    CHECK(batch.sprites()[0].width == Approx(0.6F));
    CHECK(batch.sprites()[0].colour == Rgba{64, 64, 64, 255});
    CHECK(batch.sprites()[0].position == OverlayCamera::guiToOverlay(guiWidth / 2.0F, 0.815F));
    CHECK(batch.sprites()[1].width == Approx(0.24F));
    CHECK(batch.sprites()[1].colour == coney::gui::kMenuRed);
    CHECK(batch.sprites()[1].position == OverlayCamera::guiToOverlay(guiWidth * 0.4F / 2.0F, 0.815F));
    bar.setFill(2.0F);
    CHECK(bar.fill() == Approx(1.0F));
}

TEST_CASE("message box: a timed message closes after its time", "[widgets]") {
    TextCanvas canvas;
    coney::gui::MessageBox box;
    box.showTimed("CHECKING", 3000, coney::gui::MessageStyle::Centre, 1000);
    CHECK(box.open());
    CHECK(box.message().style().colour == coney::gui::kMenuGrey);
    CHECK(box.message().style().fontSlot == coney::gui::kBigFontSlot);
    box.update(GuiFrame{.timeMs = 3999, .pad = nullptr});
    CHECK(box.open());
    box.render(canvas.canvas);
    CHECK(canvas.batch.sprites().size() == 16U); // 8 glyphs, each after its shadow
    box.update(GuiFrame{.timeMs = 4000, .pad = nullptr});
    CHECK_FALSE(box.open());
    CHECK_FALSE(box.chosen().has_value());
}

TEST_CASE("message box: a dialog's choices at 0.745, usage at 0.8; accept plays cue 8 and ends it", "[widgets]") {
    TextCanvas canvas;
    coney::gui::MenuInput input;
    coney::Pad pad;
    std::vector<int> cues;
    coney::gui::MessageBox box;
    box.setSoundSink([&cues](int cue) { cues.push_back(cue); });
    box.showChoice("FORMAT?", {"YES", "NO"}, 1, "<X> ok", input, 0);
    CHECK(box.dialog());
    CHECK(box.choices().selected() == 1);
    CHECK(box.choices().itemColour(0) == coney::gui::kDimGrey);
    CHECK(box.choices().itemColour(1) == coney::gui::kSelectedGrey);
    CHECK(box.choices().itemPosition(0, canvas.canvas).second == Approx(0.745F));
    CHECK(box.usage().style().y == Approx(0.8F));
    CHECK(box.usage().style().alignment == coney::gui::TextAlignment::Centre);
    // Left to YES, then cross let go.
    std::uint64_t now = 100;
    const auto frame = [&](std::uint16_t buttons) {
        sample(pad, buttons);
        box.update(GuiFrame{.timeMs = now, .pad = &pad});
        now += 200;
    };
    frame(coney::pad::kLeft);
    frame(0);
    CHECK(box.choices().selected() == 0);
    frame(coney::pad::kCross);
    frame(0);
    CHECK_FALSE(box.open());
    CHECK(box.chosen() == 0);
    CHECK(cues == std::vector<int>{coney::gui::MessageBox::kMoveCue, coney::gui::MessageBox::kAcceptCue});
}

TEST_CASE("message box: a several-line message is centred as a block on y 0.5", "[widgets]") {
    TextCanvas canvas;
    coney::gui::MessageBox box;
    box.showTimed("A<CR>B<CR>C", 1000, coney::gui::MessageStyle::Centre, 0);
    box.render(canvas.canvas);
    REQUIRE(canvas.batch.sprites().size() == 6);
    // The middle line's glyph (after its shadow) sits on y 0.5.
    CHECK(canvas.batch.sprites()[3].position.y == Approx(OverlayCamera::guiToOverlay(0.5F, 0.5F).y));
}

TEST_CASE("text wrap: a word that would pass the wrap width starts the next line", "[gui][text]") {
    const coney::graphics::Font font = coney::test::testFont();
    const coney::gui::FontLookup fonts = [&font](int /*slot*/) { return &font; };
    coney::gui::TextStyle style;
    const float oneWord = coney::gui::layoutText("AAA", style, fonts).width;
    // Without a wrap width the three words stay on one line.
    CHECK(coney::gui::layoutText("AAA AAA AAA", style, fonts).lines == 1);
    // Room for a little more than one word: each word on its own line, no line wider than a word.
    style.wrapWidth = oneWord * 1.5F;
    const coney::gui::TextLayout wrapped = coney::gui::layoutText("AAA AAA AAA", style, fonts);
    CHECK(wrapped.lines == 3);
    CHECK(wrapped.width == Catch::Approx(oneWord));
    // A word wider than the wrap width still gets a line of its own.
    style.wrapWidth = oneWord / 2.0F;
    CHECK(coney::gui::layoutText("AAA AAA", style, fonts).lines == 2);
}

TEST_CASE("scrolling menu: no wrap, the refused cue at the ends, a 100 ms gap and a centred window", "[gui]") {
    coney::gui::MenuInput input;
    coney::gui::ScrollingMenu menu;
    std::vector<int> cues;
    menu.setup(coney::gui::ScrollingMenuSetup{.visible = 3, .playCue = [&cues](int cue) { cues.push_back(cue); }}, 5);
    menu.takeFocus(input, 0);
    using coney::gui::MenuCommand;
    // Up at the top stays, with the refused cue.
    CHECK(!menu.handle(MenuCommand::Up, 1000).has_value());
    CHECK(menu.selected() == 0);
    // Down moves with cue 4; another move 50 ms later is ignored; one 100 ms later moves.
    (void)menu.handle(MenuCommand::Down, 1200);
    (void)menu.handle(MenuCommand::Down, 1250);
    CHECK(menu.selected() == 1);
    (void)menu.handle(MenuCommand::Down, 1300);
    CHECK(menu.selected() == 2);
    CHECK(cues == std::vector<int>{coney::gui::ScrollingMenu::kRefusedCue, 4, 4});
    // The window keeps the cursor in its middle row, clamped to the ends.
    CHECK(menu.firstVisible() == 1);
    (void)menu.handle(MenuCommand::Down, 1400);
    (void)menu.handle(MenuCommand::Down, 1500);
    CHECK(menu.selected() == 4);
    CHECK(menu.firstVisible() == 2);
    // Down at the bottom stays; accept returns the entry; left, right and back do nothing.
    cues.clear();
    (void)menu.handle(MenuCommand::Down, 1600);
    CHECK(menu.selected() == 4);
    CHECK(cues == std::vector<int>{coney::gui::ScrollingMenu::kRefusedCue});
    CHECK(menu.handle(MenuCommand::Accept, 1700) == std::optional<std::size_t>{4});
    CHECK(!menu.handle(MenuCommand::Left, 1800).has_value());
    CHECK(menu.visibleCount() == 3);
}

TEST_CASE("option grid: the fade multiplies every item's alpha", "[gui]") {
    TextCanvas canvas;
    coney::gui::OptionGrid grid;
    grid.setup(coney::gui::OptionGridSetup{.y = 0.5F});
    (void)grid.addItem(coney::gui::OptionGridItem{.text = "AB", .separator = true});
    grid.setFade(0.5F);
    grid.render(canvas.canvas);
    REQUIRE(!canvas.batch.sprites().empty());
    for (const coney::graphics::Sprite& sprite : canvas.batch.sprites()) {
        CHECK(sprite.colour.a <= 128);
    }
}
