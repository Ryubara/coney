// SPDX-License-Identifier: GPL-3.0-or-later
// The debug session's pages, driven through the model as a front end would: every page opens and its items are
// well formed, a binding is called from its page, the console page runs a line, tunables appear per category, the
// time page drives the time controls, and a level is asked for by name.
#include "debug/debug_session.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/log.h"
#include "core/perf.h"
#include "debug/menu_model.h"

using coney::debug::DebugServices;
using coney::debug::DebugSession;
using coney::debug::ItemKind;
using coney::debug::MenuItem;
using coney::debug::MenuPage;
using coney::debug::TunableRegistry;

namespace {

// Opens the submenu `label` of `page`; null when there is none.
std::shared_ptr<MenuPage> openItem(const MenuPage& page, std::string_view label) {
    const MenuItem* item = page.find(label);
    return item != nullptr && item->kind == ItemKind::Submenu ? item->open() : nullptr;
}

// Checks that an item has the callbacks its kind needs, so every front end can render and use it.
void checkWellFormed(const MenuItem& item) {
    INFO(item.label);
    switch (item.kind) {
    case ItemKind::Action:
        CHECK(item.run);
        break;
    case ItemKind::Toggle:
        CHECK((item.getBool && item.setBool));
        break;
    case ItemKind::Number:
        CHECK((item.getNumber && item.setNumber && item.min <= item.max && item.step > 0.0));
        break;
    case ItemKind::Choice:
        CHECK((item.getChoice && item.setChoice && !item.choices.empty()));
        break;
    case ItemKind::Text:
        CHECK((item.getText && item.setText));
        break;
    case ItemKind::Submenu:
        CHECK(item.open);
        break;
    case ItemKind::Watch:
        CHECK(item.watch);
        break;
    case ItemKind::Log:
        CHECK(item.lines);
        break;
    }
}

} // namespace

TEST_CASE("every page of a session opens and every item on it is well formed", "[debug]") {
    TunableRegistry tunables;
    float speed = 3.0F;
    tunables.add("Movement", "Run speed", &speed).range(0, 10, 0.5);
    DebugSession session(tunables, DebugServices{}, nullptr);
    const auto root = session.model().root();
    CHECK(session.model().pageTitles() == std::vector<std::string>{"Time",
                                                                   "Tunables",
                                                                   "Natives",
                                                                   "Lua console",
                                                                   "Cheats",
                                                                   "Levels",
                                                                   "Missions",
                                                                   "Movies",
                                                                   "Player",
                                                                   "Camera",
                                                                   "Spawner",
                                                                   "AI fighters",
                                                                   "Animation player",
                                                                   "HUD",
                                                                   "Debug draw",
                                                                   "Entity inspector",
                                                                   "Entity editor",
                                                                   "Display",
                                                                   "Audio",
                                                                   "Network",
                                                                   "Input",
                                                                   "Performance"});
    for (const MenuItem& top : root->items()) {
        checkWellFormed(top);
        const auto page = top.open();
        REQUIRE(page != nullptr);
        for (const MenuItem& item : page->items()) {
            checkWellFormed(item);
            (void)coney::debug::valueText(item);
        }
    }
}

TEST_CASE("a binding is called from its Natives page through the sandbox state", "[debug]") {
    TunableRegistry tunables;
    std::vector<std::string> printed;
    DebugSession session(tunables, DebugServices{}, nullptr,
                         [&printed](std::string_view line) { printed.emplace_back(line); });
    const auto natives = session.model().openPage("Natives");
    const auto util = openItem(*natives, "util");
    REQUIRE(util != nullptr);
    CHECK(util->find("ToInt")->detail == "implemented");
    const auto toInt = openItem(*util, "ToInt");
    REQUIRE(toInt != nullptr);
    const MenuItem* arg = toInt->find("1 x (number)");
    REQUIRE(arg != nullptr);
    arg->setNumber(-2.5);
    toInt->find("Call it")->run();
    const auto lines = session.log().lines();
    REQUIRE(lines.size() >= 2);
    CHECK(lines[lines.size() - 2] == "> ToInt(-2.5)  [sandbox state]");
    CHECK(lines.back() == "= -2");
    CHECK(printed.size() == lines.size());
}

TEST_CASE("the console page runs a line and the cheats page reports a missing callback", "[debug]") {
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    const auto console = session.model().openPage("Lua console");
    console->find("Run line")->setText("=ToInt(9.5)");
    CHECK(session.log().lines().back() == "= 9");
    const auto cheats = session.model().openPage("Cheats");
    cheats->find("01 God mode")->run();
    CHECK(session.log().lines().back().starts_with("! DbgEnterCheat is not registered"));
    // With a callback defined, the code's index reaches it.
    console->find("Run line")->setText("DbgEnterCheat = ToInt");
    cheats->find("12 Complete the mission")->run();
    CHECK(session.log().lines().back() == "= 12");
}

TEST_CASE("tunables appear by category and change between steps", "[debug]") {
    TunableRegistry tunables;
    int zone = 10;
    tunables.add("Input", "Dead zone", &zone).range(0, 64, 1);
    DebugSession session(tunables, DebugServices{}, nullptr);
    const auto page = session.model().openPage("Tunables");
    const auto input = openItem(*page, "Input");
    REQUIRE(input != nullptr);
    const MenuItem* item = input->find("Dead zone");
    REQUIRE(item != nullptr);
    CHECK(coney::debug::adjustItem(*item, 3, coney::debug::StepSize::Normal));
    CHECK(zone == 10);
    CHECK(item->getNumber() == 13.0);
    (void)session.gate().sample(0); // the frame boundary
    CHECK(zone == 13);
    CHECK(coney::debug::resetItem(*item));
    (void)session.gate().sample(1);
    CHECK(zone == 10);
}

TEST_CASE("the time page pauses and steps once; the levels page asks the loader", "[debug]") {
    TunableRegistry tunables;
    DebugServices services;
    std::vector<std::string> asked;
    services.loadLevel = [&asked](std::string_view name) {
        asked.emplace_back(name);
        return true;
    };
    DebugSession session(tunables, services, nullptr);
    const auto time = session.model().openPage("Time");
    time->find("Step one")->run();
    CHECK(session.time().paused());
    CHECK(session.time().shouldStep());
    CHECK_FALSE(session.time().shouldStep());
    time->find("Paused")->setBool(false);
    CHECK(session.time().shouldStep());
    const auto levels = session.model().openPage("Levels");
    levels->find("Load by name")->setText("level2");
    CHECK(asked == std::vector<std::string>{"level2"});
    CHECK(levels->find("Level table") != nullptr); // no game state: the table is empty
}

TEST_CASE("an Input page watch has a channel that fills once per frame", "[debug]") {
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    const auto input = session.model().openPage("Input");
    const MenuItem* stick = input->find("Left stick x");
    REQUIRE(stick != nullptr);
    REQUIRE(!stick->channel.empty());
    for (std::uint64_t frame = 0; frame < 5; ++frame) {
        (void)session.gate().sample(frame);
    }
    const auto* series = session.model().channel(stick->channel);
    REQUIRE(series != nullptr);
    CHECK(series->size() == 5);
}

TEST_CASE("the Time page shows and samples the frame time only when the platform measures it", "[debug]") {
    TunableRegistry tunables;
    DebugSession bare(tunables, DebugServices{}, nullptr);
    CHECK(bare.model().openPage("Time")->find("Frame time") == nullptr);
    CHECK(bare.model().channel("Time/Frame ms") == nullptr);

    DebugServices services;
    services.frameMilliseconds = [] { return 16.5; };
    DebugSession timed(tunables, services, nullptr);
    const auto time = timed.model().openPage("Time");
    const MenuItem* frameTime = time->find("Frame time");
    REQUIRE(frameTime != nullptr);
    CHECK(frameTime->watch() == "16.50 ms");
    CHECK(frameTime->channel == "Time/Frame ms");
    timed.model().sampleChannels();
    const coney::debug::TimeSeries* series = timed.model().channel("Time/Frame ms");
    REQUIRE(series != nullptr);
    CHECK(series->latest() == 16.5F);
}

TEST_CASE("the FPS counter shows the platform's rates, and says when there is no real clock", "[debug]") {
    TunableRegistry tunables;
    DebugSession bare(tunables, DebugServices{}, nullptr);
    CHECK(bare.cornerLines().empty());
    bare.model().openPage("Display")->find("FPS counter")->setBool(true);
    CHECK(bare.cornerLines() == std::vector<std::string>{"fps: lockstep, no real clock"});

    DebugServices services;
    std::optional<coney::FrameRateReading> rates;
    services.frameRate = [&rates] { return rates; };
    DebugSession timed(tunables, services, nullptr);
    timed.display().fpsCounter = true;
    timed.display().frameStats = true;
    // The frame stats first, then the counter: measuring until the first half second ends.
    REQUIRE(timed.cornerLines().size() == 2);
    CHECK(timed.cornerLines()[0] == "frames 0  steps 0");
    CHECK(timed.cornerLines()[1] == "fps: measuring");
    rates = coney::FrameRateReading{.framesPerSecond = 59.94, .frameMilliseconds = 16.683, .stepsPerSecond = 30.0};
    CHECK(timed.cornerLines()[1] == "59.9 fps  16.68 ms  30.0 steps/s");
}

TEST_CASE("the Display page changes the frame cap and vsync live where the platform paces frames", "[debug]") {
    TunableRegistry tunables;
    DebugSession bare(tunables, DebugServices{}, nullptr);
    const auto lockstep = bare.model().openPage("Display");
    REQUIRE(lockstep->find("FPS cap") != nullptr);
    CHECK(lockstep->find("FPS cap")->kind == ItemKind::Watch);
    CHECK(lockstep->find("Vsync") == nullptr);

    DebugServices services;
    std::uint32_t cap = 0;
    bool vsync = true;
    services.fpsCap = [&cap] { return cap; };
    services.setFpsCap = [&cap](std::uint32_t fps) { cap = fps; };
    services.vsync = [&vsync] { return vsync; };
    services.setVsync = [&vsync](bool on) { vsync = on; };
    DebugSession paced(tunables, services, nullptr);
    const auto page = paced.model().openPage("Display");
    const MenuItem* capItem = page->find("FPS cap");
    REQUIRE(capItem != nullptr);
    REQUIRE(capItem->kind == ItemKind::Number);
    CHECK(coney::debug::valueText(*capItem) == "uncapped");
    // Three steps of ten up from no cap: 30, the original's rhythm.
    coney::debug::adjustItem(*capItem, 3, coney::debug::StepSize::Normal);
    CHECK(cap == 30);
    CHECK(coney::debug::valueText(*capItem) == "30 fps");
    const MenuItem* vsyncItem = page->find("Vsync");
    REQUIRE(vsyncItem != nullptr);
    vsyncItem->setBool(false);
    CHECK_FALSE(vsync);
}

TEST_CASE("the Display page's Debug UI scale choice sets the overlay's scale override", "[debug][ui-scale]") {
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    const auto page = session.model().openPage("Display");
    const MenuItem* choice = page->find("Debug UI scale");
    REQUIRE(choice != nullptr);
    REQUIRE(choice->kind == ItemKind::Choice);
    // Auto first, then the fixed scales.
    REQUIRE(choice->choices.size() == 8);
    CHECK(choice->choices[0] == "Auto");
    CHECK(choice->choices[4] == "2x");
    CHECK(choice->getChoice() == 0);
    choice->setChoice(4);
    CHECK(session.display().uiScaleChoice == 4);
    // The watch shows the scale the overlay applied last.
    const MenuItem* now = page->find("Debug UI scale now");
    REQUIRE(now != nullptr);
    CHECK(coney::debug::valueText(*now) == "1x");
    session.display().uiScaleApplied = 1.5F;
    CHECK(coney::debug::valueText(*now) == "1.5x");
}

TEST_CASE("the Performance page turns the profiler on and shows its zones", "[debug]") {
    coney::perf::resetForTests();
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    const auto page = session.model().openPage("Performance");
    REQUIRE(page != nullptr);
    const MenuItem* measure = page->find("Measure");
    REQUIRE(measure != nullptr);
    CHECK_FALSE(measure->getBool());
    const MenuItem* frame = page->find("Frame");
    REQUIRE(frame != nullptr);
    CHECK(frame->watch() == "off");
    measure->setBool(true);
    CHECK(coney::perf::enabled());
    // A zone that has run is listed when the page is made again.
    {
        CONEY_PERF_ZONE("test.page_zone");
    }
    coney::perf::endFrame();
    const auto again = session.model().openPage("Performance");
    REQUIRE(again != nullptr);
    CHECK(again->find("test.page_zone") != nullptr);
    CHECK(again->find("Frame")->watch().find("over 1 frames") != std::string::npos);
    coney::perf::resetForTests();
}

TEST_CASE("the Time page writes a bug report: the run log's ring through the service, or the logger's own dump",
          "[debug]") {
    TunableRegistry tunables;
    DebugServices services;
    int asked = 0;
    services.writeBugReport = [&asked] {
        ++asked;
        return std::string("bug report: the last 4 events written to coney.log");
    };
    DebugSession session(tunables, services, nullptr);
    // The page owns its items: keep it alive while the item is used.
    const std::shared_ptr<MenuPage> time = session.model().openPage("Time");
    REQUIRE(time != nullptr);
    const MenuItem* report = time->find("Write bug report log");
    REQUIRE(report != nullptr);
    report->run();
    CHECK(asked == 1);
    CHECK(session.log().last(1) == std::vector<std::string>{"bug report: the last 4 events written to coney.log"});

    // Without the service (no platform log output) the logger dumps to whatever dump sink there is.
    coney::logging::reset();
    std::vector<std::string> dumped;
    coney::logging::setDumpSink([&dumped](std::string_view line) { dumped.emplace_back(line); });
    coney::logging::info(coney::logging::Category::World, "checkpoint: start 1");
    DebugSession bare(tunables, DebugServices{}, nullptr);
    bare.model().openPage("Time")->find("Write bug report log")->run();
    CHECK(dumped.size() == 3);
    CHECK(bare.log().last(1) == std::vector<std::string>{"bug report: the last 1 events written to the log"});
    coney::logging::reset();
}
