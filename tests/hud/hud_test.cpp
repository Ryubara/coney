// SPDX-License-Identifier: GPL-3.0-or-later
// The HUD object (docs/research/hud.md): hints, objectives and their messages, announcements, what hides what, counter
// panels, the instruction arrow, the radars, showing and hiding, on synthetic data.
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/font.h"
#include "gui/text_layout.h"
#include "hud/hud.h"
#include "hud/messages.h"
#include "support/font_fixtures.h"
#include "support/hud_fixtures.h"
#include "support/optional_value.h"
#include "support/recording_device.h"

using Catch::Approx;
using coney::hud::Hud;
using coney::hud::HudFrame;

using coney::test::KeepingAudio;

namespace {

// A HUD with strings and colours that name themselves, and `audio`.
Hud makeHud(KeepingAudio* audio) {
    coney::hud::HudServices services;
    services.hudString = [](std::uint32_t id) { return "H" + std::to_string(id); };
    services.tutorialString = [](std::uint32_t id) { return "T" + std::to_string(id); };
    services.announceString = [](std::uint32_t id) { return "A" + std::to_string(id); };
    services.hudColour = [](int slot) { return "<C" + std::to_string(slot) + ">"; };
    services.sound = audio != nullptr ? audio->sound : coney::hud::HudSound{};
    return Hud(std::move(services));
}

// One HUD step at `nowMs` in level 99, with the hint box's step as gameplay makes it.
void step(Hud& hud, std::uint64_t nowMs) {
    HudFrame frame;
    frame.nowMs = nowMs;
    frame.levelNumber = 99;
    hud.update(frame);
    hud.updateHints(nowMs);
}

// A canvas drawing every font slot and the flat quads into recorded batches.
struct Canvas {
    coney::graphics::Font font = coney::test::testFont(262); // as many as big_font: the stick pictures too
    coney::graphics::SpriteBatch text{font.sheet(), 8192, 10000.0F};
    coney::graphics::SpriteBatch flat{coney::graphics::SpriteSheet{}, 64, 8000.0F};
    coney::hud::HudCanvas canvas;
    Canvas() {
        canvas.text.fonts = [this](int) { return &font; };
        canvas.text.textBatch = [this](int) { return &text; };
        canvas.flat = &flat;
    }
};

} // namespace

TEST_CASE("a hint stays until cleared unless its markup times it; a text set on a free box shows at once with cue 0x15",
          "[hud]") {
    // Expected: hud-messages.md#hints-hudsettutorialtext, confirmed (code)
    KeepingAudio audio;
    Hud hud = makeHud(&audio);
    step(hud, 0);
    // Shown within the call that set it, before any step.
    hud.hints().queue("first", 2);
    REQUIRE(hud.hints().showing().has_value());
    CHECK(coney::test::got(hud.hints().showing()).text == "first");
    CHECK(audio.cues == std::vector<int>{coney::hud::kCueHint});
    hud.hints().queue("<DISPLAYTIME 3000>second", 2);
    CHECK(coney::test::got(hud.hints().showing()).text == "first");
    CHECK(hud.hints().contains("<DISPLAYTIME 3000>second"));
    // No tag: no timeout at all.
    step(hud, 600000);
    CHECK(coney::test::got(hud.hints().showing()).text == "first");
    CHECK(hud.hints().fade() == Approx(1.0F));
    // A nil clears without taking the next; the next step takes it.
    hud.hints().clearShowing();
    CHECK_FALSE(hud.hints().showing().has_value());
    step(hud, 700000);
    CHECK(coney::test::got(hud.hints().showing()).text == "<DISPLAYTIME 3000>second");
    CHECK(audio.cues.size() == 2);
    // The timed one fades over its last second and goes at 3000 ms.
    step(hud, 702500);
    CHECK(hud.hints().fade() == Approx(0.5F));
    step(hud, 703000);
    CHECK_FALSE(hud.hints().showing().has_value());
}

TEST_CASE("two nil-then-text pairs in one update leave nothing queued; the same text twice shows twice", "[hud]") {
    // Expected: hud-messages.md#hint-queue-runtime, confirmed (runtime)
    // P1.StartSnaps's four calls in one tick: the box showed the text, cleared it and showed it again, cue twice
    // (docs/research/hud-messages.md#hint-queue-runtime).
    KeepingAudio audio;
    Hud hud = makeHud(&audio);
    step(hud, 0);
    hud.hints().queue("lesson", 1);
    for (int i = 0; i < 2; ++i) {
        hud.hints().clearShowing();
        hud.hints().queue("snaps", 1);
    }
    CHECK(coney::test::got(hud.hints().showing()).text == "snaps");
    CHECK(hud.hints().queued().empty());
    CHECK(audio.cues.size() == 3);
    // A later pair shows its text at once, not one text late.
    step(hud, 33);
    hud.hints().clearShowing();
    hud.hints().queue("next", 1);
    CHECK(coney::test::got(hud.hints().showing()).text == "next");
    CHECK(hud.hints().queued().empty());
}

TEST_CASE("a timed hint's successor shows on the step after it ends; a withdrawn hint's at once", "[hud]") {
    // Expected: hud-messages.md#hint-queue-runtime, confirmed (runtime)
    Hud hud = makeHud(nullptr);
    step(hud, 0);
    hud.hints().queue("<DISPLAYTIME 3000>held", 1);
    hud.hints().queue("behind", 1);
    step(hud, 2967);
    CHECK(coney::test::got(hud.hints().showing()).text == "<DISPLAYTIME 3000>held");
    // TT_26a's end: cleared on one step, TT_26b taken on the next.
    step(hud, 3000);
    CHECK_FALSE(hud.hints().showing().has_value());
    CHECK(hud.hints().queued().size() == 1);
    step(hud, 3033);
    CHECK(coney::test::got(hud.hints().showing()).text == "behind");
    // Withdrawing the showing hint takes the next in the same call; withdrawing a queued one only removes it.
    hud.hints().queue("third", 1);
    hud.hints().queue("fourth", 1);
    hud.hints().withdraw("fourth");
    CHECK(hud.hints().queued().size() == 1);
    hud.hints().withdraw("behind");
    CHECK(coney::test::got(hud.hints().showing()).text == "third");
    CHECK(hud.hints().queued().empty());
    // A flush does not take the next.
    hud.hints().queue("fifth", 1);
    hud.hints().queue("sixth", 2);
    hud.hints().flush(1);
    CHECK_FALSE(hud.hints().showing().has_value());
    step(hud, 3066);
    CHECK(coney::test::got(hud.hints().showing()).text == "sixth");
}

TEST_CASE("hints queue by priority, first in first out within one; a full queue drops", "[hud]") {
    // Expected: hud-messages.md#hints-hudsettutorialtext, confirmed (code)
    coney::hud::HintBox box;
    // The most urgent priority showing, so the rest wait in the queue.
    box.queue("showing", 0);
    box.queue("b3", 3);
    box.queue("a1", 1);
    box.queue("c3", 3);
    box.queue("d1", 1);
    REQUIRE(box.queued().size() == 4);
    CHECK(box.queued()[0].text == "a1");
    CHECK(box.queued()[1].text == "d1");
    CHECK(box.queued()[2].text == "b3");
    CHECK(box.queued()[3].text == "c3");
    for (std::size_t i = box.queued().size(); i < coney::hud::kHintQueueSlots; ++i) {
        box.queue("fill", 3);
    }
    box.queue("dropped", 0);
    CHECK_FALSE(box.contains("dropped"));
    box.withdraw("b3");
    CHECK_FALSE(box.contains("b3"));
    CHECK(box.queued().size() == coney::hud::kHintQueueSlots - 1);
}

TEST_CASE("a more urgent hint interrupts; the interrupted one starts again later; flushing removes by priority",
          "[hud]") {
    // Expected: hud-messages.md#hints-hudsettutorialtext, confirmed (code)
    Hud hud = makeHud(nullptr);
    step(hud, 0);
    hud.hints().queue("<DISPLAYTIME 5000>low", 3);
    step(hud, 4000);
    hud.hints().queue("waits", 3);
    // The urgent one clears the showing hint and so shows at once.
    hud.hints().queue("urgent", 1);
    CHECK(coney::test::got(hud.hints().showing()).text == "urgent");
    REQUIRE(hud.hints().queued().size() == 2);
    CHECK(hud.hints().queued()[0].text == "waits");
    CHECK(hud.hints().queued()[1].text == "<DISPLAYTIME 5000>low");
    hud.hints().flush(1);
    CHECK_FALSE(hud.hints().showing().has_value());
    step(hud, 4066);
    CHECK(coney::test::got(hud.hints().showing()).text == "waits");
    // Restarted: the interrupted hint's time counts from its new start (here 4,066 ms).
    hud.hints().withdraw("waits");
    CHECK(coney::test::got(hud.hints().showing()).text == "<DISPLAYTIME 5000>low");
    step(hud, 9033);
    CHECK(hud.hints().showing().has_value());
    step(hud, 9066);
    CHECK_FALSE(hud.hints().showing().has_value());
    hud.hints().queue("again", 3);
    hud.hints().flush(coney::hud::kHintFlushAll);
    CHECK_FALSE(hud.hints().showing().has_value());
}

TEST_CASE("HUDSetObjective sets the checklist and queues the header and text at (0.025, 0.88)", "[hud]") {
    // Expected: hud-messages.md#objectives-hudsetobjective, confirmed (code)
    KeepingAudio audio;
    Hud hud = makeHud(&audio);
    hud.setObjective(0, "Reach the park", 0, false, 8000);
    REQUIRE(hud.checklist().slots[0].has_value());
    CHECK(coney::test::got(hud.checklist().slots[0]).text == "Reach the park");
    REQUIRE(hud.scrollIn().messages().size() == 1);
    const coney::hud::ScrollInMessage& message = hud.scrollIn().messages().front();
    CHECK(message.text == "<SIZE 0.8><MONEYFONT><YOBJ></MONEYFONT></SIZE><C4>H229<CR>Reach the park");
    CHECK(message.place == coney::hud::kObjectiveMessagePlace);
    step(hud, 0);
    CHECK(hud.scrollIn().showing());
    CHECK(audio.cues == std::vector<int>{coney::hud::kCueObjective});
    // Shown for its time plus the 500 ms tail, fading over the tail.
    step(hud, 8250);
    CHECK(hud.scrollIn().fade() == Approx(0.5F));
    step(hud, 8500);
    CHECK_FALSE(hud.scrollIn().showing());

    // Silent sets no message; mode 2 marks with two messages; mode 3 marks silently; mode 1 clears.
    hud.setObjective(1, "Find Ash", 0, true, 8000);
    CHECK(hud.scrollIn().messages().empty());
    hud.setObjective(1, "Find Ash", 2, false, 4000);
    CHECK(coney::test::got(hud.checklist().slots[1]).marked);
    CHECK(hud.scrollIn().messages().size() == 2);
    hud.setObjective(0, "Done", 3, false, 8000);
    CHECK(coney::test::got(hud.checklist().slots[0]).marked);
    CHECK(hud.scrollIn().messages().size() == 2);
    hud.setObjective(1, "", 1, false, 8000);
    CHECK_FALSE(hud.checklist().slots[1].has_value());
    hud.removeGoalText();
    CHECK_FALSE(hud.checklist().slots[0].has_value());
}

TEST_CASE("the objective header's first glyph is the icon rectangle, not the letter", "[hud]") {
    // Expected: gui.md#moneyfont, confirmed (code)
    // Issue #235: the header drew "ANew Objective", the icon character as a plain letter. The icon tags are rectangles
    // of the money font (28 for <YOBJ>, 22 for <BOBJ>, docs/research/gui.md#moneyfont), so the header wraps the tag in
    // <MONEYFONT>; the heading after it is ordinary text.
    const coney::graphics::Font font = coney::test::testFont(262);
    const coney::gui::FontLookup fonts = [&font](int) -> const coney::graphics::Font* { return &font; };
    Hud hud = makeHud(nullptr);
    hud.setObjective(0, "Go", 0, false, 8000);
    hud.setObjective(1, "Find Ash", 0, false, 8000);
    REQUIRE(hud.scrollIn().messages().size() == 2);
    struct Case {
        std::size_t message;
        int rectangle;
        std::uint8_t letter;
    };
    for (const Case c : {Case{0, 28, 'A'}, Case{1, 22, ';'}}) {
        const std::string& text = hud.scrollIn().messages().at(c.message).text;
        const coney::gui::TextLayout layout = coney::gui::layoutText(text, coney::gui::TextStyle{}, fonts);
        INFO(text);
        REQUIRE(layout.sprites.size() > 2);
        CHECK(layout.sprites[0].sprite.uv == font.sheet().page.rect(c.rectangle));
        CHECK(layout.sprites[0].sprite.uv != font.glyph(c.letter));
        // The heading ("H229": the test strings name themselves) is drawn as letters again.
        CHECK(layout.sprites[1].sprite.uv == font.glyph('H'));
        // (the test colour "<C4>" is not a real tag, so skippedTags is not checked here)
    }
}

TEST_CASE("completing an objective (mode 2) queues the icon header, then the text", "[hud]") {
    // Expected: hud-messages.md#objectives-hudsetobjective, confirmed (code)
    // Issue #238. hud.md: "two scroll-in messages, the header with the HUD string 0xe5/0xe6 and then the text". The
    // header is the same icon header as mode 0's (so its icon is the rectangle, #235), alone; the text follows as a
    // plain message. Which string reads "complete" is an open question (hud.md), so only the order, the
    // icon and the pairing of slot and string are pinned.
    KeepingAudio audio;
    Hud hud = makeHud(&audio);
    hud.setObjective(0, "Go", 2, false, 4000);
    hud.setObjective(1, "Find Ash", 2, false, 4000);
    REQUIRE(hud.scrollIn().messages().size() == 4);
    const auto& messages = hud.scrollIn().messages();
    CHECK(messages[0].text == "<SIZE 0.8><MONEYFONT><YOBJ></MONEYFONT></SIZE><C4>H229");
    CHECK(messages[1].text == "Go");
    CHECK(messages[2].text == "<MONEYFONT><BOBJ></MONEYFONT><C6>H230");
    CHECK(messages[3].text == "Find Ash");
    CHECK(messages[0].place == coney::hud::kObjectiveMessagePlace);
    CHECK(messages[1].place == coney::hud::kObjectiveMessagePlace);
    CHECK(messages[0].ms == 4000);
    CHECK(messages[1].ms == 4000);
}

TEST_CASE("the first slot-1 objective with game hints on queues tutorial hint 0x15 once", "[hud]") {
    // Expected: hud-messages.md#objectives-hudsetobjective, confirmed (code)
    Hud hud = makeHud(nullptr);
    step(hud, 0);
    hud.setObjective(1, "a", 0, false, 8000);
    CHECK_FALSE(hud.hints().showing().has_value());
    hud.setGameTutorialText(true);
    hud.setObjective(1, "b", 0, false, 8000);
    REQUIRE(hud.hints().showing().has_value());
    CHECK(coney::test::got(hud.hints().showing()).text == "T21");
    hud.setObjective(1, "c", 0, false, 8000);
    CHECK(hud.hints().queued().empty());
}

TEST_CASE("an announcement hides the messages and the hint box; a message hides the hint box", "[hud]") {
    // Expected: hud-messages.md#announcements-and-other-messages, confirmed (code)
    KeepingAudio audio;
    Hud hud = makeHud(&audio);
    Canvas canvas;
    hud.hints().queue("hint", 1);
    step(hud, 0);
    CHECK_FALSE(hud.hintsHidden());
    hud.render(canvas.canvas);
    CHECK(canvas.flat.sprites().size() == 1); // the box
    canvas.flat.clear();
    hud.setObjective(0, "objective", 0, false, 8000);
    step(hud, 33);
    CHECK(hud.hintsHidden());
    CHECK_FALSE(hud.scrollInHidden());
    hud.render(canvas.canvas);
    CHECK(canvas.flat.sprites().empty());
    hud.setAnnouncement(2, "", false);
    CHECK(coney::test::got(hud.announcement()).text == "A2");
    CHECK(audio.cues.back() == coney::hud::kCueAnnounce);
    CHECK(hud.scrollInHidden());
    // No tag: it stays; a new one replaces it at once.
    step(hud, 600000);
    CHECK(coney::test::got(hud.announcement()).text == "A2");
    hud.setAnnouncement(3, "", false);
    CHECK(coney::test::got(hud.announcement()).text == "A3");
    // Kind 5 is the centred one: it hides nothing and lasts as its markup says.
    hud.setAnnouncement(5, "<DISPLAYTIME 2000>CHEAT", true);
    CHECK(coney::test::got(hud.centredAnnouncement()).text == "<DISPLAYTIME 2000>CHEAT");
    CHECK(coney::test::got(hud.announcement()).text == "A3");
    step(hud, 602000);
    CHECK_FALSE(hud.centredAnnouncement().has_value());
}

TEST_CASE("the action prompt sits at (0.5, 0.86) in grey, raised over the hint box or message below", "[hud]") {
    // Expected: hud-messages.md#action-prompts, confirmed (code)
    Hud hud = makeHud(nullptr);
    Canvas canvas;
    hud.setActionPrompt(0, "Pick up");
    step(hud, 0);
    hud.render(canvas.canvas);
    REQUIRE_FALSE(canvas.text.sprites().empty());
    CHECK(canvas.text.sprites().back().colour.r == 128);
    CHECK(hud.promptRaise(canvas.canvas.text.fonts) == Approx(coney::hud::kPromptRaiseOverMessage));
    // A hint box: 0.05 less the box's height.
    hud.hints().queue("Press X to attack", 1);
    step(hud, 33);
    const float box = hud.hints().boxHeight(canvas.canvas.text.fonts);
    CHECK(box > coney::hud::kHintBoxExtra.height);
    CHECK(hud.promptRaise(canvas.canvas.text.fonts) == Approx(coney::hud::kPromptRaiseOverHint - box));
    // A scroll-in message hides the box; the prompt then clears the message.
    hud.setObjective(0, "objective", 0, true, 8000);
    hud.scrollIn().queue(coney::hud::ScrollInMessage{"objective", coney::hud::kObjectiveMessagePlace, 8000, {}});
    step(hud, 66);
    CHECK(hud.promptRaise(canvas.canvas.text.fonts) < coney::hud::kPromptRaiseOverMessage);
    // A bottom-left announcement hides it.
    canvas.text.clear();
    hud.scrollIn().clear();
    hud.hints().flush(coney::hud::kHintFlushAll);
    hud.setAnnouncement(2, "", false);
    step(hud, 100);
    hud.render(canvas.canvas);
    const std::size_t withAnnouncement = canvas.text.sprites().size();
    canvas.text.clear();
    hud.setActionPrompt(0, "");
    hud.render(canvas.canvas);
    CHECK(canvas.text.sprites().size() == withAnnouncement);
}

TEST_CASE("no action prompt is drawn in an Armies of the Night level", "[hud]") {
    // Expected: armies.md#hud-in-play, confirmed (code): HUD_Render (0x001b1688) draws the prompts only outside an
    // Armies level (GameState_IsArmiesLevel).
    const auto drawn = [](int level, const char* prompt) {
        Hud hud = makeHud(nullptr);
        Canvas canvas;
        hud.setActionPrompt(0, prompt);
        HudFrame frame;
        frame.levelNumber = level;
        hud.update(frame);
        hud.render(canvas.canvas);
        return canvas.text.sprites().size();
    };
    CHECK(drawn(99, "Pick up") > drawn(99, ""));
    CHECK(drawn(60, "Pick up") == drawn(60, ""));
    CHECK(drawn(69, "Pick up") == drawn(69, ""));
}

TEST_CASE("nothing is drawn and the radars go off while the letterbox is in", "[hud]") {
    // Expected: hud.md#showing-and-hiding, confirmed (code)
    Hud hud = makeHud(nullptr);
    Canvas canvas;
    hud.hints().queue("hint", 1);
    HudFrame frame;
    frame.nowMs = 0;
    frame.letterbox = true;
    hud.update(frame);
    hud.render(canvas.canvas);
    CHECK(canvas.text.sprites().empty());
    CHECK(canvas.flat.sprites().empty());
    CHECK_FALSE(hud.radar().on[0]);
    frame.letterbox = false;
    hud.update(frame);
    hud.render(canvas.canvas);
    CHECK(canvas.flat.sprites().size() == 1);
}

TEST_CASE("the hint box sits on the bottom left, its box grown round the text", "[hud]") {
    // Expected: hud.md#hint-box-layout-0x0050eb50, confirmed (code)
    Hud hud = makeHud(nullptr);
    Canvas canvas;
    hud.hints().queue("Press X to attack", 1);
    step(hud, 0);
    hud.render(canvas.canvas);
    REQUIRE(canvas.flat.sprites().size() == 1);
    const coney::graphics::Sprite& box = canvas.flat.sprites()[0];
    CHECK(box.colour.a == coney::hud::kHintBoxDrawAlpha);
    // The box's bottom is the text's bottom: 1.0 less the box's extra height, in overlay space y = 0.5 - GUI y.
    const float bottom = box.position.y - box.height / 2.0F;
    CHECK(bottom == Approx(0.5F - (coney::hud::kHintBottom - coney::hud::kHintBoxExtra.height)));
    // Its left edge at the text's x less 0.018.
    const float left = box.position.x - box.width / 2.0F;
    CHECK(left == Approx(coney::graphics::OverlayCamera::guiToOverlay(coney::hud::kHintTextX - 0.018F, 0.0F).x));
}

TEST_CASE("a long hint wraps at spaces outside tags to the box's width", "[hud]") {
    // Expected: hud.md#hint-box-layout-0x0050eb50, confirmed (code)
    const coney::graphics::Font font = coney::test::testFont();
    const coney::gui::FontLookup fonts = [&font](int) { return &font; };
    const coney::gui::TextStyle style = coney::hud::HintBox::textStyle();
    const std::string wrapped =
        coney::hud::wrapText("one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen "
                             "sixteen <COLOR ffffffff>seventeen</COLOR> eighteen",
                             style, fonts, 0.70F);
    CHECK(wrapped.find("<CR>") != std::string::npos);
    CHECK(wrapped.find("<COLOR ffffffff>") != std::string::npos);
    CHECK(coney::gui::layoutText(wrapped, style, fonts).width <= 0.70F);
    CHECK(coney::hud::wrapText("short", style, fonts, 0.70F) == "short");
}

TEST_CASE("the wrap measures each line with its tags stripped and takes a leading AUTOINDENT's width", "[hud]") {
    // Expected: hud.md#hint-box-layout-0x0050eb50, confirmed (code)
    const coney::graphics::Font font = coney::test::testFont();
    const coney::gui::FontLookup fonts = [&font](int) { return &font; };
    const coney::gui::TextStyle style = coney::hud::HintBox::textStyle();
    // A button icon takes no width: a line that fits without it fits with it.
    const float plain = coney::gui::layoutText("press  to climb", style, fonts).width;
    CHECK(coney::hud::wrapText("press <T> to climb", style, fonts, plain) == "press <T> to climb");
    CHECK(coney::hud::wrapText("press <T> to climb", style, fonts, plain * 0.9F).find("<CR>") != std::string::npos);
    // Only the line being built counts: the lines before a break are not measured again.
    CHECK(coney::hud::wrapText("press<CR>to climb", style, fonts, plain * 0.8F) == "press<CR>to climb");
    // <AUTOINDENT f> sets the width.
    CHECK(coney::hud::wrapText("<AUTOINDENT 0.01>one two", style, fonts, 10.0F).find("<CR>") != std::string::npos);
    CHECK(coney::hud::kHintWrapWidth == 0.74F);
}

TEST_CASE("counter panels: five to take, shown for a time after a change, in rows from the top", "[hud]") {
    // Expected: hud.md#item-counters, confirmed (code)
    KeepingAudio audio;
    coney::hud::CounterPanels panels;
    for (int i = 0; i < 5; ++i) {
        CHECK(panels.take(2, "Tags", 0.22F) == i);
    }
    CHECK(panels.take(2, "Tags", 0.22F) == -1);
    panels.release(3);
    CHECK(panels.take(1, "Money", 0.22F) == 3);
    CHECK_FALSE(panels.panels()[0].visible);
    panels.setValue(0, 2, 6.0F, 3000, false, 0, audio.sound);
    CHECK_FALSE(panels.panels()[0].visible);
    panels.setValue(0, 1, 2.0F, 3000, true, 100, audio.sound);
    CHECK(panels.panels()[0].visible);
    CHECK(coney::hud::CounterPanels::textOf(panels.panels()[0]) == "Tags 2/6");
    CHECK(audio.sounds == std::vector<std::string>{coney::hud::kCounterPanelSound});
    panels.update(3099);
    CHECK(panels.panels()[0].visible);
    panels.update(3100);
    CHECK_FALSE(panels.panels()[0].visible);
    panels.setValue(1, 1, 1.0F, 0, false, 0, coney::test::kSilent);
    panels.update(1'000'000);
    CHECK(panels.panels()[1].visible); // 0 keeps it up
}

TEST_CASE("the instruction arrow bobs up by 2 a frame to 10 and back by 0.5", "[hud]") {
    // Expected: hud-messages.md#the-instruction-arrow-hudenableinstarrow, confirmed (code)
    Hud hud = makeHud(nullptr);
    hud.enableArrow(true, 0.5F, 0.3F, 3.14159265F);
    std::vector<float> steps;
    for (int i = 0; i < 26; ++i) {
        step(hud, static_cast<std::uint64_t>(i) * 33);
        steps.push_back(hud.arrow().step);
    }
    CHECK(steps[4] == 10.0F);
    CHECK(steps[5] == 9.5F);
    CHECK(steps[24] == 0.0F);
    CHECK(steps[25] == 2.0F);
    // Pointing down (π), the bob moves it down the screen: (0, step / 200).
    hud.arrow().step = 10.0F;
    CHECK(hud.arrow().offset().x == Approx(0.0F).margin(1e-6));
    CHECK(hud.arrow().offset().y == Approx(0.05F));
    // Turning it off keeps the place; turning it on takes the new one.
    hud.enableArrow(false, 0.9F, 0.9F, 0.0F);
    CHECK_FALSE(hud.arrow().on);
    CHECK(hud.arrow().place.x == 0.5F);
}

TEST_CASE("the radar disc turns blue only while the HUD shows, blending over 500 ms", "[hud][hiding]") {
    // Expected: hud-radar.md#the-radar-on-screen, confirmed (code)
    Hud hud = makeHud(nullptr);
    hud.hideAll();
    hud.setRadarTint(0, coney::hud::RadarTint::Blue);
    CHECK(hud.radar().tint[0] == coney::hud::RadarTint::Grey);
    CHECK(hud.radarColour(0) == coney::graphics::Rgba{191, 191, 191, 240});
    hud.showAll();
    HudFrame frame;
    frame.nowMs = 1000;
    hud.update(frame);
    hud.setRadarTint(0, coney::hud::RadarTint::Blue);
    CHECK(hud.radar().tint[0] == coney::hud::RadarTint::Blue);
    CHECK(hud.radarColour(0) == coney::graphics::Rgba{191, 191, 191, 240});
    frame.nowMs = 1250;
    hud.update(frame);
    CHECK(hud.radarColour(0) == coney::graphics::Rgba{146, 156, 196, 240});
    frame.nowMs = 1500;
    hud.update(frame);
    CHECK(hud.radarColour(0) == coney::graphics::Rgba{100, 120, 200, 240});
}

TEST_CASE("radars: on and off by player, off during a screen fade and back after it, kept by HideHud", "[hud]") {
    // Expected: hud.md#radars-across-a-scene, confirmed (code)
    Hud hud = makeHud(nullptr);
    hud.radarOff(2);
    CHECK_FALSE(hud.radar().on[0]);
    CHECK_FALSE(hud.radar().scriptOn);
    hud.radarOn(0);
    CHECK(hud.radar().on[0]);
    CHECK_FALSE(hud.radar().on[1]);
    HudFrame fading;
    fading.screenFading = true;
    hud.update(fading);
    CHECK_FALSE(hud.radar().on[0]);
    hud.showAll();
    CHECK_FALSE(hud.radar().on[0]); // RestoreHud does not turn a radar on
    HudFrame clear;
    hud.update(clear);
    CHECK(hud.radar().on[0]); // the automatic return, the last call being HUDTurnOnRadar
    CHECK(hud.radar().on[1]);
    hud.hideAll();
    CHECK(hud.radar().on[0]);
}

TEST_CASE("HideHud draws nothing; panels attach once and HidePlayerHud keeps them hidden", "[hud]") {
    // Expected: hud.md#showing-and-hiding, confirmed (code)
    Hud hud = makeHud(nullptr);
    Canvas canvas;
    CHECK(hud.attachPlayer(0, 32) == 0);
    CHECK(hud.attachPlayer(0, 32) == -1);
    CHECK(hud.attachPlayer(2, 32) == -1);
    CHECK(hud.panel(0).shown());
    hud.hidePlayers();
    CHECK_FALSE(hud.panel(0).shown());
    hud.showAll();
    CHECK_FALSE(hud.panel(0).shown()); // may not show
    hud.showPlayers();
    CHECK(hud.panel(0).shown());
    hud.hints().queue("hint", 1);
    step(hud, 0);
    hud.hideAll();
    hud.render(canvas.canvas);
    CHECK(canvas.text.sprites().empty());
    CHECK(canvas.flat.sprites().empty());
    // Attached while hidden: hidden too.
    CHECK(hud.attachPlayer(1, 40) == 1);
    CHECK_FALSE(hud.panel(1).shown());
}

TEST_CASE("the letterbox going out shows the HUD a scene's HideHud hid, on the second step with the bars out",
          "[hud]") {
    // Expected: hud.md#who-shows-the-hud-again, confirmed (code)
    Hud hud = makeHud(nullptr);
    Canvas canvas;
    REQUIRE(hud.attachPlayer(0, 32) == 0);
    // level99: RestoreHud, the radars turned off, then SuperRunScene's HideHud; a hint queued while hidden is kept.
    hud.showAll();
    hud.radarOff(2);
    hud.hideAll();
    CHECK_FALSE(hud.panel(0).shown());
    hud.hints().queue("hint", 1);
    // The bars in and out: nothing steps or draws, the hint's clock does not run.
    HudFrame frame;
    frame.letterbox = true;
    for (std::uint64_t ms = 0; ms <= 3000; ms += 33) {
        frame.nowMs = ms;
        hud.update(frame);
    }
    CHECK_FALSE(hud.visible());
    CHECK(hud.hints().shownMs() == 0);
    hud.render(canvas.canvas);
    CHECK(canvas.flat.sprites().empty());
    // The first step with the bars out stamps the restore; the next shows the HUD and its panel.
    frame.letterbox = false;
    frame.nowMs = 3033;
    hud.update(frame);
    CHECK_FALSE(hud.visible());
    frame.nowMs = 3066;
    hud.update(frame);
    CHECK(hud.visible());
    CHECK(hud.panel(0).shown());
    // HUDTurnOffRadar was the last radar call: the radars stay off.
    CHECK_FALSE(hud.radar().on[0]);
    CHECK_FALSE(hud.radar().on[1]);
    hud.render(canvas.canvas);
    CHECK(hud.hints().showing().has_value());
    CHECK(canvas.flat.sprites().size() == 1); // the hint box
    CHECK_FALSE(canvas.text.sprites().empty());
    // The restore happens once: a later HideHud holds.
    hud.hideAll();
    frame.nowMs = 3100;
    hud.update(frame);
    hud.update(frame);
    CHECK_FALSE(hud.visible());
}

TEST_CASE("RestoreHud keeps each part's own flags; the radars come back by themselves after the bars when on",
          "[hud]") {
    // Expected: hud.md#who-shows-the-hud-again, confirmed (code)
    Hud hud = makeHud(nullptr);
    REQUIRE(hud.attachPlayer(0, 32) == 0);
    // A panel HidePlayerHud forbids stays hidden through RestoreHud; a radar that is off stays off.
    hud.hidePlayers();
    hud.radarOff(1);
    hud.hideAll();
    hud.showAll();
    CHECK(hud.visible());
    CHECK_FALSE(hud.panel(0).shown());
    CHECK(hud.radar().on[0]);
    CHECK_FALSE(hud.radar().on[1]);
    // HideHud saves nothing and leaves the radars' flags: a radar on stays on while hidden (it is not drawn).
    hud.hideAll();
    CHECK(hud.radar().on[0]);
    // A letterbox turns both off; with the last radar call "on" both return once the bars are out.
    hud.radarOn(2);
    HudFrame frame;
    frame.letterbox = true;
    hud.update(frame);
    CHECK_FALSE(hud.radar().on[0]);
    frame.letterbox = false;
    hud.update(frame);
    CHECK(hud.radar().on[0]);
    CHECK(hud.radar().on[1]);
    // A HUDTurnOffRadar after that is kept: no automatic return until the next letterbox or fade and a HUDTurnOnRadar.
    hud.radarOff(0);
    hud.update(frame);
    CHECK_FALSE(hud.radar().on[0]);
}

TEST_CASE("a level starts with the HUD hidden and the radars' automatic return on", "[hud]") {
    // Expected: hud.md#showing-and-hiding, confirmed (code)
    Hud hud = makeHud(nullptr);
    REQUIRE(hud.attachPlayer(0, 32) == 0);
    hud.radarOff(2);
    hud.levelSetUp();
    CHECK_FALSE(hud.visible());
    CHECK_FALSE(hud.panel(0).shown());
    CHECK(hud.radar().scriptOn);
    // level99's Main shows it at once.
    hud.showAll();
    CHECK(hud.visible());
    CHECK(hud.panel(0).shown());
}

TEST_CASE("the action cycle swaps its word every framesPerIcon updates and blinks on and off", "[hud]") {
    // Expected: hud-messages.md#action-prompts, confirmed (code)
    coney::hud::ActionCycle cycle{.on = true, .framesPerIcon = 4, .blinkFrames = 6, .iconA = 79, .iconB = 73};
    // The first update swaps already, then every fourth: B for updates 0-3, A for 4-7, B for 8-11.
    std::vector<std::uint32_t> icons;
    std::vector<bool> on;
    for (int i = 0; i < 12; ++i) {
        cycle.step();
        icons.push_back(cycle.icon());
        on.push_back(cycle.iconOn);
    }
    CHECK(icons == std::vector<std::uint32_t>{73, 73, 73, 73, 79, 79, 79, 79, 73, 73, 73, 73});
    // On for six updates, off for six.
    CHECK(on == std::vector<bool>{true, true, true, true, true, true, false, false, false, false, false, false});
}

TEST_CASE("the action cycle hides the prompt's text and draws its button at the prompt's anchor", "[hud]") {
    // Expected: hud-messages.md#action-prompts, confirmed (code)
    Hud hud = makeHud(nullptr);
    Canvas canvas;
    coney::graphics::SpriteSheet sheet;
    sheet.texture = std::make_shared<coney::test::FakeTexture>(512, 256);
    for (int i = 0; i < 100; ++i) {
        sheet.page.rects.push_back(coney::graphics::UvRect{0.0F, 0.0F, 34.0F / 512.0F, 34.0F / 256.0F});
    }
    coney::graphics::SpriteBatch parts(sheet, 16, 10000.0F);
    canvas.canvas.parts = &parts;
    hud.setActionPrompt(0, "Lift");
    hud.startActionCycle(
        0, coney::hud::ActionCycle{.on = true, .framesPerIcon = 4, .blinkFrames = 0, .iconA = 79, .iconB = 79});
    step(hud, 0);
    hud.render(canvas.canvas);
    // The text is hidden; the button is drawn, 0.1 high, centred at x 0.5.
    CHECK(canvas.text.sprites().empty());
    REQUIRE(parts.sprites().size() == 1);
    CHECK(parts.sprites()[0].height == Approx(coney::hud::kCycleIconSize));
    CHECK(parts.sprites()[0].position.x == Approx(coney::graphics::OverlayCamera::guiToOverlay(0.5F, 0.86F).x));
    // A new text shows again; the cycle's end leaves the text alone.
    parts.clear();
    hud.setActionPrompt(0, "Lift again");
    hud.stopActionCycle(0);
    step(hud, 33);
    hud.render(canvas.canvas);
    CHECK_FALSE(canvas.text.sprites().empty());
    CHECK(parts.sprites().empty());
}

TEST_CASE("on the clubhouse's base the action prompt takes no raise: only the bases 0.86, 0.78, 1.0 and 0.8 do",
          "[hud]") {
    // Expected: hud-messages.md#action-prompts, confirmed (code): ActionPrompt_Update (0x0019f528) adds the raise to
    // the icon's and the text's y only when the base is one of those four.
    Hud hud = makeHud(nullptr);
    Canvas canvas;
    coney::graphics::SpriteSheet sheet;
    sheet.texture = std::make_shared<coney::test::FakeTexture>(512, 256);
    for (int i = 0; i < 100; ++i) {
        sheet.page.rects.push_back(coney::graphics::UvRect{0.0F, 0.0F, 34.0F / 512.0F, 34.0F / 256.0F});
    }
    coney::graphics::SpriteBatch parts(sheet, 16, 10000.0F);
    canvas.canvas.parts = &parts;
    hud.setClubActionText(true);
    hud.setActionPrompt(0, "Lift");
    hud.startActionCycle(
        0, coney::hud::ActionCycle{.on = true, .framesPerIcon = 4, .blinkFrames = 0, .iconA = 79, .iconB = 79});
    step(hud, 0);
    REQUIRE(hud.promptRaise(canvas.canvas.text.fonts) != Approx(0.0F));
    hud.render(canvas.canvas);
    REQUIRE(parts.sprites().size() == 1);
    // The icon's centre at the clubhouse's base itself, 0.125.
    CHECK(parts.sprites()[0].position.y ==
          Approx(coney::graphics::OverlayCamera::guiToOverlay(0.5F, coney::hud::kClubPromptY).y));
    // At the normal base, 0.86, the raise is added.
    parts.clear();
    hud.setClubActionText(false);
    hud.render(canvas.canvas);
    REQUIRE(parts.sprites().size() == 1);
    CHECK(parts.sprites()[0].position.y ==
          Approx(coney::graphics::OverlayCamera::guiToOverlay(0.5F, coney::hud::kPromptPlace.y +
                                                                        hud.promptRaise(canvas.canvas.text.fonts))
                     .y));
    // The text takes neither the raise nor the lift of a prompt of several lines on the clubhouse's base: its first
    // line sits as far from the same text's at the normal base as 0.125 is from 0.86 plus the raise less two lifts.
    hud.stopActionCycle(0);
    hud.setActionPrompt(0, "Lift<CR>again");
    step(hud, 33);
    const auto firstLineY = [&canvas, &hud](bool club) {
        canvas.text.clear();
        hud.setClubActionText(club);
        hud.render(canvas.canvas);
        REQUIRE_FALSE(canvas.text.sprites().empty());
        return canvas.text.sprites()[0].position.y;
    };
    const float normal = firstLineY(false);
    const float club = firstLineY(true);
    const float normalBase =
        coney::hud::kPromptPlace.y + hud.promptRaise(canvas.canvas.text.fonts) - (2.0F * coney::hud::kPromptLineRaise);
    CHECK(club - normal == Approx(normalBase - coney::hud::kClubPromptY).margin(1e-5));
}

TEST_CASE("the stopwatch's minutes form is its label and %2u:%02u, right-aligned at (1.0, 0.24)", "[hud]") {
    // Expected: hud-functions-panels.md#fn-after-textentrypad, confirmed (code): StopWatchHud_UpdateMinutes
    // (0x001cd5e8) and StopWatchHud_ApplyVideoMode (0x001cd400), the default mode's place.
    std::int32_t ms = 65999;
    coney::hud::HudServices services;
    services.stopWatchTime = [&ms] { return ms; };
    Hud hud(std::move(services));
    hud.showStopWatch(true, "TIME ");
    step(hud, 0);
    // What the original's message widget draws: the minutes padded to two places, the block's right edge at x 1.0.
    const auto expected = [](std::string_view text) {
        Canvas drawn;
        coney::gui::TextStyle style = coney::hud::messageStyle(1.0F);
        style.alignment = coney::gui::TextAlignment::Right;
        coney::hud::drawMessage(drawn.canvas, text, style, 0.24F);
        return drawn.text.sprites();
    };
    const auto shown = [&hud] {
        Canvas canvas;
        hud.render(canvas.canvas);
        return canvas.text.sprites();
    };
    const auto check = [](const auto& got, const auto& want) {
        REQUIRE_FALSE(want.empty());
        REQUIRE(got.size() == want.size());
        for (std::size_t i = 0; i < got.size(); ++i) {
            CHECK(got[i].position.x == Approx(want[i].position.x));
            CHECK(got[i].position.y == Approx(want[i].position.y));
            CHECK(got[i].uv == want[i].uv);
        }
    };
    check(shown(), expected("TIME  1:05"));
    ms = 605000;
    check(shown(), expected("TIME 10:05"));
}

TEST_CASE("the level's shut-down clears every hint, objective and panel the level left, and keeps the game's",
          "[hud]") {
    // Expected: hud.md#hud-object, confirmed (code)
    // docs/research/hud.md#hud-object: UnloadLevel's HUD_ShutdownLevel runs every part's shutdown; the hint box's
    // clears its text, both lists and the tutorial callback.
    Hud hud = makeHud(nullptr);
    REQUIRE(hud.attachPlayer(0, 32) == 0);
    hud.setGameTutorialText(true);
    hud.hints().queue("lesson", 1);
    step(hud, 100);
    hud.hints().queue("queued", 3);
    REQUIRE(hud.hints().showing().has_value());
    hud.setTutorialCallback("P1.BasicAttacks");
    hud.setObjective(0, "objective", 0, true, 0);
    REQUIRE(hud.checklist().slots[0].has_value());
    hud.setAnnouncement(5, "centred", false);
    hud.setActionPrompt(0, "prompt");
    hud.enableArrow(true, 0.5F, 0.5F, 0.0F);
    hud.showStopWatch(true, "time");
    hud.radar().blips.try_emplace(7.0, coney::hud::RadarBlip{});
    hud.showAll();

    hud.levelShutDown();
    CHECK_FALSE(hud.hints().showing().has_value());
    CHECK(hud.hints().queued().empty());
    CHECK(hud.tutorialCallback().empty());
    CHECK_FALSE(hud.checklist().slots[0].has_value());
    CHECK_FALSE(hud.centredAnnouncement().has_value());
    CHECK(hud.actionPrompt(0).empty());
    CHECK_FALSE(hud.arrow().on);
    CHECK_FALSE(hud.stopWatch().shown);
    CHECK(hud.radar().blips.empty());
    // The panel is free for the next level's player.
    CHECK(hud.attachPlayer(0, 32) == 0);
    // The game state's switch, the services and the shown flag outlive the level.
    CHECK(hud.gameTutorialText());
    CHECK(hud.services().hudString(1) == "H1");
    CHECK(hud.visible());
    // A hint the next level queues shows as before.
    hud.hints().queue("next", 1);
    step(hud, 200);
    REQUIRE(hud.hints().showing().has_value());
    CHECK(hud.hints().showing().value_or(coney::hud::Hint{}).text == "next");
}

TEST_CASE("the Armies credits do not outlive a level: the next level's set-up clears them", "[hud][armies]") {
    // Expected: hud-functions-parts.md#fn-pre-basewidget, confirmed (code): HUD_InitLevel zeroes HUD +0x190d0, the
    // word 0x00619910 HUDANSetCredit writes, and the Armies stages carry their credits in the Lua save data.
    Hud hud = makeHud(nullptr);
    hud.levelSetUp();
    hud.armies().setCredits(5);
    hud.levelShutDown();
    hud.levelSetUp();
    CHECK(hud.armies().credits() == 0);
}

TEST_CASE("an Armies level asks the absent player 2 to join from its set-up: attaching player 1 clears only his",
          "[hud][armies]") {
    // Expected: armies.md#the-join-messages, confirmed (runtime) in level60: HUD_InitLevel turns both panels' join
    // flag on (0x004ec650); Human_MakePlayer -> HUD_AttachPlayer -> ANHud_Init clears player 1's on the panel's first
    // attach only; player 2's stays on.
    Hud hud = makeHud(nullptr);
    hud.levelSetUp();
    REQUIRE(hud.attachPlayer(0, 32) == 0);
    CHECK_FALSE(hud.armies().joinMessage(0));
    CHECK(hud.armies().joinMessage(1));
    // A script turning his message on again is not undone by a refused second attach.
    hud.armies().setJoinMessage(0, true);
    CHECK(hud.attachPlayer(0, 32) == -1);
    CHECK(hud.armies().joinMessage(0));
}

TEST_CASE("a context prompt's GSTRING.ACTIONBUTTON draws the triangle button, not the letter n", "[hud]") {
    // Expected: hud-messages.md#action-prompts, confirmed (code)
    // User item 9 ("n Give Weapon"): the prompt strings start with <MONEYFONT><ST></MONEYFONT> and a space, and in the
    // prompt's part_page0 font the <ST> character n inside <MONEYFONT> is rectangle 11 + ('n' - '0') = 73.
    Hud hud = makeHud(nullptr);
    Canvas canvas;
    hud.setActionPrompt(0, "<MONEYFONT><ST></MONEYFONT> Give Weapo");
    step(hud, 0);
    hud.render(canvas.canvas);
    const auto& sprites = canvas.text.sprites();
    REQUIRE(sprites.size() == 10);
    const coney::graphics::UvRect triangle = canvas.font.sheet().page.rect(73);
    CHECK(sprites.front().uv == triangle);
    CHECK(std::ranges::none_of(sprites, [&](const auto& s) { return s.uv == canvas.font.glyph('n'); }));
}

TEST_CASE("the revive prompt's stick picture alternates with the prompt's own time", "[hud]") {
    // Expected: hud-messages.md#action-prompts, confirmed (code)
    // HUD string 4's form: <SDL> in big_font's money font is rectangle 259 (its character 4), then 257 (character 2),
    // by turns every 500 ms, counted from when the prompt's text was set (the widget's stick timer).
    Hud hud = makeHud(nullptr);
    const coney::graphics::UvRect own = Canvas{}.font.sheet().page.rect(259);
    const coney::graphics::UvRect off = Canvas{}.font.sheet().page.rect(257);
    const auto stickAt = [&hud](std::uint64_t nowMs) {
        step(hud, nowMs);
        Canvas canvas;
        hud.render(canvas.canvas);
        const auto& sprites = canvas.text.sprites();
        REQUIRE_FALSE(sprites.empty());
        return sprites.back().uv;
    };
    step(hud, 10000);
    const std::string revive = "<BIGFONT><MONEYFONT><SDL></MONEYFONT></BIGFONT>";
    hud.setActionPrompt(0, revive);
    CHECK(stickAt(10000) == own);
    CHECK(stickAt(10499) == own);
    CHECK(stickAt(10500) == off);
    CHECK(stickAt(11000) == own);
    // The same text again keeps its time; a new text starts it over.
    hud.setActionPrompt(0, revive);
    CHECK(stickAt(11600) == off);
    hud.setActionPrompt(0, revive + " ");
    CHECK(stickAt(11700) == own);
}
