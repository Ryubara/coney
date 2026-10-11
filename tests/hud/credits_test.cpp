// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/credits.h"

#include <cstddef>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/font.h"
#include "gui/colour_table.h"
#include "hud/hud.h"
#include "support/font_fixtures.h"

using Catch::Approx;
using coney::hud::CreditLine;
using coney::hud::Credits;
using coney::hud::CreditStyle;

namespace {

// Style numbers as CfgCredits takes them.
constexpr auto kHeading = static_cast<std::uint32_t>(CreditStyle::Heading);
constexpr auto kRole = static_cast<std::uint32_t>(CreditStyle::RoleName);
constexpr auto kLine = static_cast<std::uint32_t>(CreditStyle::Line);
constexpr auto kSection = static_cast<std::uint32_t>(CreditStyle::Section);

// A heading, two names sharing a role, a name with a new role, then a section line: the script credits.lua stands for.
void loadSample(Credits& credits) {
    credits.load([&credits] {
        credits.addLine(kHeading, "Heading", "");
        credits.addLine(kRole, "Role A", "Name 1");
        credits.addLine(kRole, "Role A", "Name 2");
        credits.addLine(kRole, "Role B", "Name 3");
        credits.addLine(kSection, "Section", "");
    });
}

// The y of a line's main widget.
float lineY(const Credits& credits, std::size_t line) {
    const CreditLine& data = credits.lines().at(line);
    REQUIRE(data.widget.has_value());
    return credits.widget(*data.widget).style().y;
}

// The synthetic font in every slot, drawing into one batch.
struct TextCanvas {
    coney::graphics::Font font = coney::test::testFont();
    coney::graphics::SpriteBatch batch{font.sheet(), 8192, 9000.0F};
    coney::gui::GuiCanvas canvas{
        .fonts = [this](int /*slot*/) { return &font; },
        .textBatch = [this](int /*slot*/) { return &batch; },
    };
};

} // namespace

TEST_CASE("CfgCredits appends a line with its index; style 4 is stored as 2", "[hud][credits]") {
    Credits credits;
    credits.load([&credits] {
        credits.addLine(kHeading, "A", "");
        credits.addLine(4, "B", "");
    });
    REQUIRE(credits.lines().size() == 2);
    CHECK(credits.lines()[0].index == 0);
    CHECK(credits.lines()[1].index == 1);
    CHECK(credits.lines()[1].style == kLine);
    CHECK(credits.lines()[1].role == "B");
}

TEST_CASE("PreloadCredits makes a fresh list and a hidden pool; ShowCredits starts it only once loaded",
          "[hud][credits]") {
    Credits credits;
    credits.addLine(kHeading, "Stale", "");
    credits.start(5);
    CHECK_FALSE(credits.running());
    loadSample(credits);
    CHECK(credits.loaded());
    CHECK(credits.lines().size() == 5);
    CHECK(credits.freeWidgets() == coney::hud::kCreditWidgets);
    credits.start(1234);
    CHECK(credits.running());
    CHECK(credits.startMs() == 1234);
    // Already running: a second start changes nothing.
    credits.start(9999);
    CHECK(credits.startMs() == 1234);
}

TEST_CASE("the first update stacks the lines down from y 1.1 with their gaps", "[hud][credits]") {
    Credits credits;
    loadSample(credits);
    credits.start(0);
    credits.update();
    const float heading = Credits::lineHeight(kHeading);
    const float line = Credits::lineHeight(kRole);
    CHECK(heading == Approx(coney::graphics::fontMetrics(1.8F).height));
    CHECK(line == Approx(coney::graphics::fontMetrics(0.9F).height));
    // The heading's centre half its height below the start, then 0.05 after it.
    CHECK(lineY(credits, 0) == Approx(1.1F + (heading / 2.0F)));
    float pen = 1.1F + heading + 0.05F;
    CHECK(lineY(credits, 1) == Approx(pen + (line / 2.0F)));
    pen += line;
    CHECK(lineY(credits, 2) == Approx(pen + (line / 2.0F)));
    // Two names shared Role A and a new role follows: 0.04 after the second.
    CHECK(credits.lines()[2].gap == Approx(0.04F));
    pen += line + 0.04F;
    CHECK(lineY(credits, 3) == Approx(pen + (line / 2.0F)));
    // A role line before a section line: 0.08.
    CHECK(credits.lines()[3].gap == Approx(0.08F));
    pen += line + 0.08F;
    CHECK(lineY(credits, 4) == Approx(pen + (line / 2.0F)));
    CHECK(credits.lines()[4].gap == 0.0F);
}

TEST_CASE("a role line shows its role only when it changes, right-aligned left of the gutter", "[hud][credits]") {
    Credits credits;
    loadSample(credits);
    credits.start(0);
    credits.update();
    const auto& lines = credits.lines();
    REQUIRE(lines[1].roleWidget.has_value());
    CHECK_FALSE(lines[2].roleWidget.has_value());
    REQUIRE(lines[3].roleWidget.has_value());
    // Heading 1 + (role, name) + name + (role, name) + section 1 = 7 widgets.
    CHECK(credits.freeWidgets() == coney::hud::kCreditWidgets - 7);
    const auto& role = credits.widget(*lines[1].roleWidget);
    CHECK(role.text() == "Role A");
    CHECK(role.style().alignment == coney::gui::TextAlignment::Right);
    CHECK(role.style().x == Approx(0.49F));
    const auto& name = credits.widget(*lines[1].widget);
    CHECK(name.text() == "Name 1");
    CHECK(name.style().alignment == coney::gui::TextAlignment::Left);
    CHECK(name.style().x == Approx(0.51F));
    const auto& heading = credits.widget(*lines[0].widget);
    CHECK(heading.style().alignment == coney::gui::TextAlignment::Centre);
    CHECK(heading.style().scale == Approx(1.8F));
    CHECK(name.style().scale == Approx(0.9F));
}

TEST_CASE("every later update moves the live lines up 0.0025", "[hud][credits]") {
    Credits credits;
    loadSample(credits);
    credits.start(0);
    credits.update();
    const float first = lineY(credits, 0);
    const float last = lineY(credits, 4);
    credits.update();
    CHECK(lineY(credits, 0) == Approx(first - 0.0025F));
    CHECK(lineY(credits, 4) == Approx(last - 0.0025F));
}

TEST_CASE("a line starts only while two pool widgets are free", "[hud][credits]") {
    Credits credits;
    credits.load([&credits] {
        for (int i = 0; i < 100; ++i) {
            credits.addLine(kLine, std::to_string(i), "");
        }
    });
    credits.start(0);
    credits.update();
    // One widget per centred line: the 70th line would leave one free, so it waits.
    CHECK(credits.freeWidgets() == 1);
    CHECK(credits.lines()[68].started);
    CHECK_FALSE(credits.lines()[69].started);
}

TEST_CASE("lines past the top give their widgets back, and the scroll ends when none shows", "[hud][credits]") {
    Credits credits;
    credits.load([&credits] { credits.addLine(kLine, "Only", ""); });
    credits.start(0);
    credits.update();
    const float height = Credits::lineHeight(kLine);
    const float start = lineY(credits, 0);
    // Given back once y + height is above -0.1.
    int updates = 1;
    while (credits.running() && updates < 2000) {
        credits.update();
        ++updates;
    }
    CHECK_FALSE(credits.running());
    CHECK_FALSE(credits.loaded());
    CHECK(credits.lines().empty());
    const float travel = start - (-0.1F - height);
    // The placing update, the steps up until past the limit (rounded up), and the update that sees it there.
    CHECK(updates == Approx((travel / 0.0025F) + 2.0F).margin(1.0));
    CHECK(credits.takeEnded());
    CHECK_FALSE(credits.takeEnded());
}

TEST_CASE("the credits draw only widgets whose top is above y 1.1, in the menus' grey", "[hud][credits]") {
    TextCanvas text;
    Credits credits;
    credits.load([&credits] {
        credits.addLine(kLine, "A", "");
        credits.addLine(kLine, "B", "");
    });
    credits.render(text.canvas);
    CHECK(text.batch.sprites().empty());
    credits.start(0);
    credits.update();
    // The first line's top starts at 1.1 (drawn or not by rounding), the second's a line below: at most one line.
    credits.render(text.canvas);
    const std::size_t atStart = text.batch.sprites().size();
    text.batch.clear();
    // One step up: the first line's top is above 1.1, the second's still below.
    credits.update();
    credits.render(text.canvas);
    REQUIRE_FALSE(text.batch.sprites().empty());
    const std::size_t one = text.batch.sprites().size();
    CHECK(atStart <= one);
    bool grey = false;
    for (const auto& sprite : text.batch.sprites()) {
        grey = grey || sprite.colour == coney::gui::kMenuGrey;
    }
    CHECK(grey);
    // Enough steps for the second line too: twice as many sprites.
    text.batch.clear();
    for (int i = 0; i < 40; ++i) {
        credits.update();
    }
    credits.render(text.canvas);
    CHECK(text.batch.sprites().size() == 2 * one);
}

TEST_CASE("the HUD steps and draws the credits even while it is hidden", "[hud][credits]") {
    TextCanvas text;
    coney::hud::HudCanvas canvas;
    canvas.text = text.canvas;
    coney::hud::Hud hud;
    hud.hideAll();
    hud.credits().load([&hud] { hud.credits().addLine(kLine, "A", ""); });
    hud.credits().start(0);
    coney::hud::HudFrame frame;
    hud.update(frame);
    hud.update(frame);
    CHECK(hud.credits().lines()[0].started);
    hud.render(canvas);
    CHECK_FALSE(text.batch.sprites().empty());
}
