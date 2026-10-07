// SPDX-License-Identifier: GPL-3.0-or-later
// The HUD object (docs/research/hud.md): hints, objectives and their messages, announcements, what hides what, counter
// panels, the instruction arrow, the radars, showing and hiding, on synthetic data.
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "hud/hud.h"
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

// One HUD step at `nowMs` in level 99.
void step(Hud& hud, std::uint64_t nowMs) {
    HudFrame frame;
    frame.nowMs = nowMs;
    frame.levelNumber = 99;
    hud.update(frame);
}

// A canvas drawing every font slot and the flat quads into recorded batches.
struct Canvas {
    coney::graphics::Font font = coney::test::testFont();
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

TEST_CASE("a hint stays until flushed unless its markup times it; the next one shows with cue 0x15", "[hud]") {
    KeepingAudio audio;
    Hud hud = makeHud(&audio);
    hud.hints().queue("first", 2);
    hud.hints().queue("<DISPLAYTIME 3000>second", 2);
    step(hud, 0);
    REQUIRE(hud.hints().showing().has_value());
    CHECK(coney::test::got(hud.hints().showing()).text == "first");
    CHECK(audio.cues == std::vector<int>{coney::hud::kCueHint});
    CHECK(hud.hints().contains("<DISPLAYTIME 3000>second"));
    // No tag: no timeout at all.
    step(hud, 600000);
    CHECK(coney::test::got(hud.hints().showing()).text == "first");
    CHECK(hud.hints().fade() == Approx(1.0F));
    hud.hints().flush(2);
    CHECK_FALSE(hud.hints().showing().has_value());
    // The timed one shows next, fades over its last second and goes at 3000 ms.
    hud.hints().queue("<DISPLAYTIME 3000>second", 2);
    step(hud, 700000);
    CHECK(audio.cues.size() == 2);
    step(hud, 702500);
    CHECK(hud.hints().fade() == Approx(0.5F));
    step(hud, 703000);
    CHECK_FALSE(hud.hints().showing().has_value());
}

TEST_CASE("hints queue by priority, first in first out within one; a full queue drops", "[hud]") {
    coney::hud::HintBox box;
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
    Hud hud = makeHud(nullptr);
    hud.hints().queue("<DISPLAYTIME 5000>low", 3);
    step(hud, 0);
    step(hud, 4000);
    hud.hints().queue("waits", 3);
    hud.hints().queue("urgent", 1);
    CHECK_FALSE(hud.hints().showing().has_value());
    step(hud, 4033);
    CHECK(coney::test::got(hud.hints().showing()).text == "urgent");
    REQUIRE(hud.hints().queued().size() == 2);
    CHECK(hud.hints().queued()[0].text == "waits");
    CHECK(hud.hints().queued()[1].text == "<DISPLAYTIME 5000>low");
    hud.hints().flush(1);
    step(hud, 4066);
    CHECK(coney::test::got(hud.hints().showing()).text == "waits");
    hud.hints().flush(3);
    step(hud, 4100);
    CHECK_FALSE(hud.hints().showing().has_value());
    // Restarted: the interrupted hint's time counts from its new start.
    hud.hints().queue("<DISPLAYTIME 5000>low", 3);
    step(hud, 10000);
    step(hud, 14000);
    CHECK(hud.hints().showing().has_value());
    hud.hints().flush(coney::hud::kHintFlushAll);
    CHECK_FALSE(hud.hints().showing().has_value());
}

TEST_CASE("HUDSetObjective sets the checklist and queues the header and text at (0.025, 0.88)", "[hud]") {
    KeepingAudio audio;
    Hud hud = makeHud(&audio);
    hud.setObjective(0, "Reach the park", 0, false, 8000);
    REQUIRE(hud.checklist().slots[0].has_value());
    CHECK(coney::test::got(hud.checklist().slots[0]).text == "Reach the park");
    REQUIRE(hud.scrollIn().messages().size() == 1);
    const coney::hud::ScrollInMessage& message = hud.scrollIn().messages().front();
    CHECK(message.text == "<SIZE 0.8><YOBJ></SIZE><C4>H229<CR>Reach the park");
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

TEST_CASE("the first slot-1 objective with game hints on queues tutorial hint 0x15 once", "[hud]") {
    Hud hud = makeHud(nullptr);
    step(hud, 0);
    hud.setObjective(1, "a", 0, false, 8000);
    CHECK(hud.hints().queued().empty());
    hud.setGameTutorialText(true);
    hud.setObjective(1, "b", 0, false, 8000);
    REQUIRE(hud.hints().queued().size() == 1);
    CHECK(hud.hints().queued().front().text == "T21");
    hud.setObjective(1, "c", 0, false, 8000);
    CHECK(hud.hints().queued().size() == 1);
}

TEST_CASE("an announcement hides the messages and the hint box; a message hides the hint box", "[hud]") {
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

TEST_CASE("nothing is drawn and the radars go off while the letterbox is in", "[hud]") {
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

TEST_CASE("radars: on and off by player, off during a screen fade and back after it, kept by HideHud", "[hud]") {
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
