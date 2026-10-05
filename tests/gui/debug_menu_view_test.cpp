// SPDX-License-Identifier: GPL-3.0-or-later
// The pad debug menu's renderer: Coney's bitmap font, the view of every item kind, the rows it fits, the overlays,
// and the text painters over a recording device.
#include "gui/debug_menu_view.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "debug/debug_session.h"
#include "debug/menu_model.h"
#include "graphics/bitmap_font.h"
#include "support/font_fixtures.h"
#include "support/recording_device.h"

using coney::debug::DebugServices;
using coney::debug::DebugSession;
using coney::debug::ItemKind;
using coney::debug::MenuPage;
using coney::debug::TunableRegistry;
using coney::graphics::BitmapFont;
using coney::gui::DebugMenuView;

namespace {

// A painter that keeps every text it is asked to draw, at a fixed 6 x 10 cell.
class RecordingPainter final : public coney::gui::DebugTextPainter {
  public:
    [[nodiscard]] float lineHeight() const override { return 10.0F; }
    [[nodiscard]] float measure(std::string_view text) const override { return static_cast<float>(text.size()) * 6.0F; }
    void text(std::string_view text, float /*x*/, float /*y*/, coney::graphics::Rgba /*colour*/) override {
        texts.emplace_back(text);
    }
    void flush(coney::graphics::RenderDevice& /*device*/) override { ++flushes; }

    [[nodiscard]] bool drew(std::string_view wanted) const {
        return std::ranges::any_of(texts,
                                   [wanted](const std::string& t) { return t.find(wanted) != std::string::npos; });
    }

    std::vector<std::string> texts;
    int flushes = 0;
};

// Adds a page "Kinds" with one item of every kind, labelled by the kind's name.
void addKindsPage(DebugSession& session) {
    session.model().addPage("Kinds", [](MenuPage& page) {
        page.add(coney::debug::actionItem("action", [] {}));
        page.add(coney::debug::toggleItem("toggle", [] { return true; }, [](bool) {}));
        page.add(coney::debug::numberItem("number", [] { return 2.5; }, [](double) {}, 0, 5, 0.5, false));
        page.add(
            coney::debug::choiceItem("choice", {"first", "second"}, [] { return std::size_t{1}; }, [](std::size_t) {}));
        page.add(coney::debug::textItem("text", [] { return std::string("typed"); }, [](const std::string&) {}, false));
        page.add(coney::debug::submenuItem("submenu", [] { return std::make_shared<MenuPage>("Sub"); }));
        page.add(coney::debug::watchItem("watch", [] { return std::string("live"); }, "Input/Left stick x"));
        page.add(coney::debug::logItem("log", [] { return std::vector<std::string>{"log line"}; }));
    });
}

// Opens the session's menu on the page titled `title`.
void openPage(DebugSession& session, std::string_view title) {
    auto& nav = session.navigator();
    nav.open();
    const auto titles = session.model().pageTitles();
    const auto index = std::ranges::find(titles, title) - titles.begin() + 1; // after Favourites
    for (long i = 0; i < index; ++i) {
        coney::debug::MenuInputFrame down;
        down.action = coney::debug::MenuAction::Down;
        nav.apply(down);
    }
    coney::debug::MenuInputFrame accept;
    accept.action = coney::debug::MenuAction::Accept;
    nav.apply(accept);
}

} // namespace

TEST_CASE("the bitmap font has a glyph for every printable character and merges pixel runs", "[debug][gui]") {
    CHECK(BitmapFont::glyph('-') == std::array<std::uint8_t, 7>{0, 0, 0, 0x1f, 0, 0, 0});
    CHECK(BitmapFont::glyph('\x01') == BitmapFont::glyph('?'));
    for (char c = ' '; c <= '~'; ++c) {
        INFO(c);
        const auto rows = BitmapFont::glyph(c);
        CHECK((c == ' ' || std::ranges::any_of(rows, [](std::uint8_t row) { return row != 0; })));
    }
    std::vector<coney::graphics::LogicalQuad> quads;
    const float end = BitmapFont::draw(quads, "-", 10.0F, 20.0F, 2.0F, coney::graphics::kWhite);
    REQUIRE(quads.size() == 1); // one run of five pixels
    CHECK(quads[0].x == 10.0F);
    CHECK(quads[0].y == 26.0F);
    CHECK(quads[0].width == 10.0F);
    CHECK(end == 22.0F);
    CHECK(BitmapFont::measure("abc", 1.0F) == 18.0F);
}

TEST_CASE("every item kind has a renderer in the pad menu", "[debug][gui]") {
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    addKindsPage(session);
    openPage(session, "Kinds");
    RecordingPainter painter;
    coney::test::RecordingDevice device;
    DebugMenuView view;
    view.draw(session, painter, device);
    // Each kind's label and what it shows.
    for (std::size_t kind = 0; kind < coney::debug::kItemKindCount; ++kind) {
        CHECK(painter.drew(coney::debug::itemKindName(static_cast<ItemKind>(kind))));
    }
    CHECK(painter.drew("on"));
    CHECK(painter.drew("2.5"));
    CHECK(painter.drew("second"));
    CHECK(painter.drew("typed"));
    CHECK(painter.drew("live"));
    CHECK(painter.drew("log line")); // a log's lines go to the footer
    CHECK(painter.drew("Debug > Kinds"));
    CHECK(painter.flushes == 1);
    REQUIRE(device.draws.size() == 1); // the panel's flat quads, before the text
    CHECK(device.draws[0].texture == nullptr);
}

TEST_CASE("the view tells the navigator how many rows fit and draws nothing while closed", "[debug][gui]") {
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    RecordingPainter painter;
    coney::test::RecordingDevice device;
    DebugMenuView view;
    view.draw(session, painter, device);
    CHECK(device.draws.empty());
    CHECK(painter.texts.empty());
    openPage(session, "Natives");
    view.draw(session, painter, device);
    // 448 logical pixels of 10-pixel lines, less the title and the footer.
    CHECK(session.navigator().visibleRows() > 20);
    CHECK(session.navigator().visibleRows() < 44);
}

TEST_CASE("the Display page's overlays draw with the menu closed", "[debug][gui]") {
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    session.display().frameStats = true;
    session.display().safeArea = true;
    session.display().logicalBounds = true;
    session.time().setPaused(true);
    RecordingPainter painter;
    coney::test::RecordingDevice device;
    DebugMenuView view;
    view.draw(session, painter, device);
    REQUIRE(device.draws.size() == 1);
    CHECK(device.draws[0].quads.size() == 9); // two outlines of four edges and the stats' backing
    CHECK(painter.drew("PAUSED"));
}

TEST_CASE("text that does not fit is cut and marked", "[debug][gui]") {
    RecordingPainter painter;
    CHECK(coney::gui::fitText(painter, "short", 60.0F) == "short");
    CHECK(coney::gui::fitText(painter, "a long label", 30.0F) == "a lo~");
}

TEST_CASE("the bitmap painter draws flat quads in one call", "[debug][gui]") {
    coney::gui::BitmapTextPainter painter;
    coney::test::RecordingDevice device;
    CHECK(painter.lineHeight() == 9.0F);
    painter.text("Hi", 0.0F, 0.0F, coney::graphics::kWhite);
    painter.flush(device);
    REQUIRE(device.draws.size() == 1);
    CHECK(device.draws[0].texture == nullptr);
    CHECK_FALSE(device.draws[0].quads.empty());
    painter.flush(device);
    CHECK(device.draws.size() == 1); // nothing queued, nothing drawn
}

TEST_CASE("the game font painter sizes the font to the line and draws through the 2D pass", "[debug][gui]") {
    coney::gui::GameFontPainter painter(coney::test::testFont(), 10.0F);
    coney::test::RecordingDevice device;
    CHECK(painter.lineHeight() == 10.0F);
    // A glyph of the synthetic font is half as wide as it is high, so a character is about 5 logical pixels.
    const float width = painter.measure("WW");
    CHECK(width > 10.0F);
    CHECK(width < 40.0F);
    painter.text("ab", 100.0F, 50.0F, coney::graphics::kWhite);
    painter.flush(device);
    REQUIRE(device.draws.size() == 1);
    CHECK(device.draws[0].texture != nullptr);
    REQUIRE(device.draws[0].quads.size() == 2);
    // The line's top is where it was asked for, within a pixel.
    CHECK(device.draws[0].quads[0].y > 49.0F);
    CHECK(device.draws[0].quads[0].y + device.draws[0].quads[0].height < 61.0F);
}
