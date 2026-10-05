// SPDX-License-Identifier: GPL-3.0-or-later
// The Rumble menu's screens, built from synthetic chunks run through the real bindings, and the set-up each confirm
// writes (docs/research/frontend.md#rumble-setup, docs/research/frontend.md#rumble-data). Input comes from input
// scripts, as a pad: the d-pad, cross and triangle, and the left stick partly pushed. A synthetic font lays out the
// text.
#include "gui/rumble_mode_gui/rumble_menu.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/input_script.h"
#include "core/pads.h"
#include "graphics/font.h"
#include "graphics/sprite_batch.h"
#include "gui/global_strings.h"
#include "gui/widget.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "support/font_fixtures.h"
#include "support/rumble_fixtures.h"
#include "warriors/game_state.h"

using coney::RumbleSetup;
using coney::gui::RumbleMenu;
using coney::gui::RumbleMenuResult;
using coney::gui::RumbleScreen;

namespace {

// The default 1 ON 1 set-up read at run time on a fresh boot, the 23 values in order: the synthetic gangs carry the
// disc's ids and rosters, so the stand-ins give the same types.
constexpr std::array<std::uint16_t, RumbleSetup::kValues> kDefaultValues{
    3, 12, 1, 4, 2, 91, 94, 91, 92, 93, 94, 91, 92, 93, 225, 226, 224, 225, 226, 227, 228, 225, 226};

// A host for the bindings that asks nothing of a front end.
class QuietHost final : public coney::script::BindingHost {
  public:
    void showProfileManager(std::string_view /*onRumble*/, std::string_view /*onStartGame*/) override {}
    void showRumbleModeInterface(std::string_view /*onCancel*/, std::string_view /*onStart*/,
                                 double /*players*/) override {}
    void menuLoadLevel(std::string_view /*level*/) override {}
    void playMovie(std::string_view /*name*/) override {}
    void playMusic(std::string_view /*track*/) override {}
    void stopMusic() override {}
    void queueScreenEffect(int /*type*/, double /*seconds*/) override {}
};

// A Rumble menu over the synthetic chunks and a synthetic font, played frame by frame from an input script.
struct Harness {
    coney::graphics::Font font = coney::test::testFont();
    coney::graphics::SpriteBatch textBatch{font.sheet(), 4096, 9000.0F};
    coney::gui::GuiCanvas canvas;
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    coney::gui::RumbleData data;
    QuietHost host;
    coney::script::RecordedCalls recorded;
    coney::script::BindingContext context{&state, &strings, &host, &recorded, nullptr, nullptr, &data};
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    std::vector<std::string> chunksRun;
    coney::script::ScriptSystem scripts{
        [this](std::string_view name) -> std::expected<std::vector<std::byte>, coney::Error> {
            const auto found = files.find(name);
            if (found == files.end()) {
                return coney::fail(coney::ErrorCode::NotFound, "no such script");
            }
            return found->second;
        },
        [this](coney::script::ScriptSystem& system, coney::script::LuaVm& vm) {
            coney::script::installBindings(system, vm, context);
        },
        {}};
    std::unique_ptr<RumbleMenu> menu;
    std::unique_ptr<coney::ScriptedInput> input;
    coney::Pads pads;
    std::uint64_t frame = 0;
    RumbleMenuResult last = RumbleMenuResult::Stay;

    // The menu over the synthetic level table and chunks, its Game Type strings invented, playing `script`.
    explicit Harness(std::string_view script) {
        canvas.fonts = [this](int /*slot*/) { return &font; };
        canvas.textBatch = [this](int /*slot*/) { return &textBatch; };
        coney::test::addRumbleChunks(files);
        files["config_preload3.lua"] = coney::test::rumbleLevelTableChunk();
        strings.set(RumbleMenu::kOnePlayerString, "ALONE");
        strings.set(RumbleMenu::kCoopString, "TOGETHER");
        strings.set(RumbleMenu::kVersusString, "AGAINST");
        strings.set(RumbleMenu::kPlayerTwoString, "SECOND PAD");
        scripts.create();
        scripts.runFile("config_preload3.lua");
        menu = std::make_unique<RumbleMenu>(coney::gui::RumbleMenuServices{
            .state = &state,
            .data = &data,
            .strings = &strings,
            .runChunk =
                [this](std::string_view chunk) {
                    chunksRun.emplace_back(chunk);
                    scripts.runFile(chunk);
                },
        });
        input = std::make_unique<coney::ScriptedInput>(coney::parseInputScript(script).value());
        menu->start(msOf(frame));
    }

    // The game time of frame `n` on the 1/30 s step, in milliseconds.
    static std::uint64_t msOf(std::uint64_t n) { return n * 100 / 3; }

    // Runs frames up to and including `last`; stops early at a frame that ends the menu, keeping its result.
    RumbleMenuResult to(std::uint64_t lastFrame) {
        while (frame <= lastFrame) {
            pads.update(input->sample(frame));
            textBatch.clear();
            const coney::gui::GuiFrame guiFrame{.timeMs = msOf(frame), .pad = &pads.port(0)};
            last = menu->update(guiFrame, pads.connectedCount());
            menu->render(canvas);
            ++frame;
            if (last != RumbleMenuResult::Stay) {
                break;
            }
        }
        return last;
    }

    // The set-up the menu writes.
    [[nodiscard]] const RumbleSetup& setup() const { return state.rumble; }
};

} // namespace

TEST_CASE("the Rumble screens list what the chunks add and the fresh profile unlocks", "[rumble_menu]") {
    Harness h("");
    h.to(5);
    CHECK(h.chunksRun == std::vector<std::string>{"rumble_data.lua"});
    CHECK(h.menu->screen() == RumbleScreen::GameMode);
    // Of the four modes only 12 and 14 are unlocked; the list shows the chunk's titles and the selected description.
    REQUIRE(h.menu->grid().items() == 2);
    CHECK(h.menu->grid().item(0).text() == "DUEL");
    CHECK(h.menu->grid().item(1).text() == "SQUAD");
    CHECK(h.menu->detail().text() == "about DUEL");
    CHECK(h.data.modes.at(1).playerOptions == std::array<bool, 3>{true, true, true});
    CHECK(!h.textBatch.sprites().empty());
}

TEST_CASE("accepting each Rumble screen's first entry writes the default set-up, screen by screen", "[rumble_menu]") {
    // Cross on Game Mode, Game Type, side 1, side 2 and the arena.
    Harness h("10 tap cross\n20 tap cross\n30 tap cross\n40 tap cross\n50 tap cross\n");

    // Game Mode: the mode's id, the gang size, the label and the options; the Game Type entries from the HUD strings.
    h.to(15);
    CHECK(h.menu->screen() == RumbleScreen::GameType);
    CHECK(h.setup().values.at(RumbleSetup::kGameType) == 12);
    CHECK(h.setup().values.at(RumbleSetup::kGangSize) == 1);
    CHECK(h.setup().modeLabel == ":DUEL");
    CHECK(h.setup().playerOptions == std::array<bool, 3>{true, false, true});
    CHECK(!h.setup().presetGangs);
    REQUIRE(h.menu->grid().items() == 2);
    CHECK(h.menu->grid().item(0).text() == "ALONE");
    CHECK(h.menu->grid().item(1).text() == "AGAINST");
    CHECK(h.menu->takeCues() == std::vector<int>{coney::gui::kRumbleConfirmCue});

    // Game Type: one player.
    h.to(25);
    CHECK(h.menu->screen() == RumbleScreen::ChooseGangs);
    CHECK(h.setup().values.at(RumbleSetup::kGameMode) == coney::gui::kRumbleOnePlayer);
    // Two gangs unlocked; side 1 starts on the first, side 2 on the second; the names stay empty until both lock.
    REQUIRE(h.data.gangs.size() == 2);
    REQUIRE(h.menu->gangs().gang(0) != nullptr);
    CHECK(h.menu->gangs().gang(0)->name == "RED SIDE");
    CHECK(h.menu->gangs().gang(1)->name == "BLUE SIDE");
    CHECK(h.setup().gangNames[0].empty());

    // Side 1 locks and side 2 chooses; side 2's lock writes the gangs.
    h.to(35);
    CHECK(h.menu->gangs().locked(0));
    CHECK(h.menu->gangs().activeSide() == 1);
    h.to(45);
    CHECK(h.menu->screen() == RumbleScreen::ChooseArea);
    CHECK(h.setup().values == kDefaultValues);
    CHECK(h.setup().gangNames[0] == "RED SIDE");
    CHECK(h.setup().gangNames[1] == "BLUE SIDE");
    // One arena is unlocked and allows mode 12; its label is its level record's title.
    REQUIRE(h.menu->grid().items() == 1);
    CHECK(h.menu->grid().item(0).text() == "ARENA 102");

    // Choose Area: the confirm marks the arena, and the next update launches it with its level number.
    CHECK(h.to(60) == RumbleMenuResult::Started);
    // Released on frame 51, launched on frame 52 (the harness has moved past it).
    CHECK(h.frame == 53);
    CHECK(h.setup().levelNumber == 102);
}

TEST_CASE("the Game Type entries write 3 for one player, 2 for co-op and 1 for versus", "[rumble_menu]") {
    // SQUAD (the stick most of the way down), then co-op or versus with a second pad: the first confirm shows the
    // message, the second accepts.
    for (const auto& [moves, players] :
         {std::pair{std::string(""), coney::gui::kRumbleOnePlayer},
          std::pair{std::string("26 tap down\n"), coney::gui::kRumbleCoop},
          std::pair{std::string("26 tap down\n32 tap down\n"), coney::gui::kRumbleVersus}}) {
        INFO(moves);
        Harness h("1 p2 connect\n10 stick left 0 -70\n12 stick left 0 0\n20 tap cross\n" + moves +
                  "40 tap cross\n50 tap cross\n");
        h.to(30);
        CHECK(h.setup().values.at(RumbleSetup::kGameType) == 14);
        CHECK(h.setup().values.at(RumbleSetup::kGangSize) == 5);
        REQUIRE(h.menu->grid().items() == 3);
        CHECK(h.menu->grid().item(1).text() == "TOGETHER");
        h.to(55);
        CHECK(h.menu->screen() == RumbleScreen::ChooseGangs);
        CHECK(h.setup().values.at(RumbleSetup::kGameMode) == players);
    }
}

TEST_CASE("co-op shows the second player's message and needs a second pad", "[rumble_menu]") {
    Harness h("10 stick left 0 -60\n12 stick left 0 0\n20 tap cross\n30 tap down\n40 tap cross\n"
              "50 tap cross\n120 p2 connect\n125 tap cross\n");
    h.to(45);
    CHECK(h.menu->screen() == RumbleScreen::GameType);
    CHECK(h.menu->detail().text() == "SECOND PAD");
    // Still one pad: the second confirm stays and shows the message again; it goes after 1.5 s.
    h.to(55);
    CHECK(h.menu->screen() == RumbleScreen::GameType);
    h.to(110);
    CHECK(h.menu->detail().text().empty());
    h.to(130);
    CHECK(h.menu->screen() == RumbleScreen::ChooseGangs);
    CHECK(h.setup().values.at(RumbleSetup::kGameMode) == coney::gui::kRumbleCoop);
}

TEST_CASE("WAR PARTY's copy writes mode 14, five a side, its label, all three options and no presets",
          "[rumble_menu]") {
    Harness h("10 tap down\n20 tap cross\n");
    // A mode with presets chosen before must not leave its flag behind.
    h.state.rumble.presetGangs = true;
    h.to(25);
    CHECK(h.setup().values.at(RumbleSetup::kGameType) == 14);
    CHECK(h.setup().values.at(RumbleSetup::kGangSize) == 5);
    CHECK(h.setup().modeLabel == ":SQUAD");
    CHECK(h.setup().playerOptions == std::array<bool, 3>{true, true, true});
    CHECK(!h.setup().presetGangs);
}

TEST_CASE("a mode with presets fills both sides and skips the gang screen", "[rumble_menu]") {
    // RACE is the third entry once unlocked (ENDLESS stays locked); the menu starts again to list it.
    Harness h("10 tap down\n16 tap down\n24 tap cross\n34 tap cross\n");
    h.state.unlockables.unlock(coney::UnlockKind::RumbleMode, 24);
    h.state.unlockables.unlock(coney::UnlockKind::RumbleArena, 103);
    h.menu->start(0);
    h.to(29);
    CHECK(h.setup().values.at(RumbleSetup::kGameType) == 24);
    CHECK(h.setup().presetGangs);
    CHECK(h.setup().values.at(RumbleSetup::kGang1Types) == 458);
    CHECK(h.setup().values.at(RumbleSetup::kGang2Types + 8) == 459);
    h.to(39);
    CHECK(h.menu->screen() == RumbleScreen::ChooseArea);
    CHECK(h.setup().values.at(RumbleSetup::kGang1Pak) == 255);
    CHECK(h.setup().values.at(RumbleSetup::kGang2Pak) == 255);
    // Only arena 103 allows every mode.
    REQUIRE(h.menu->grid().items() == 1);
    CHECK(h.menu->grid().item(0).text() == "ARENA 103");
}

TEST_CASE("left and right rotate the active side's roster to choose the warchief, except in co-op", "[rumble_menu]") {
    // One player: side 1 rotates left once, side 2 right once (the stick partly pushed), then both lock.
    Harness h("10 tap cross\n20 tap cross\n30 tap left\n40 tap cross\n50 stick left 60 0\n52 stick left 0 0\n"
              "70 tap cross\n");
    h.to(35);
    REQUIRE(h.menu->gangs().roster(0) != nullptr);
    CHECK(h.menu->gangs().roster(0)->front() == 94);
    CHECK(h.menu->gangs().roster(0)->back() == 91);
    // Side 2's copy of the same gang is untouched.
    CHECK(h.data.gangs.at(0).rosters[1].front() == 91);
    h.to(75);
    CHECK(h.menu->screen() == RumbleScreen::ChooseArea);
    CHECK(h.setup().values.at(RumbleSetup::kGang1Types) == 94);
    CHECK(h.setup().values.at(RumbleSetup::kGang2Types) == 226);
    CHECK(h.setup().values.at(RumbleSetup::kGang2Types + 1) == 225);

    // Co-op: left does nothing.
    Harness coop("1 p2 connect\n10 tap down\n20 tap cross\n30 tap down\n40 tap cross\n50 tap cross\n60 tap left\n");
    coop.to(65);
    CHECK(coop.setup().values.at(RumbleSetup::kGameMode) == coney::gui::kRumbleCoop);
    REQUIRE(coop.menu->gangs().roster(0) != nullptr);
    CHECK(coop.menu->gangs().roster(0)->front() == 91);
}

TEST_CASE("up and down move the active side's gang, and back unlocks before it leaves the screen", "[rumble_menu]") {
    Harness h("10 tap cross\n20 tap cross\n30 tap down\n40 tap cross\n50 tap triangle\n60 tap triangle\n");
    h.to(35);
    CHECK(h.menu->gangs().gang(0)->name == "BLUE SIDE");
    h.to(45);
    CHECK(h.menu->gangs().activeSide() == 1);
    // Back unlocks side 1 and makes it active again; a second back returns to the Game Type screen.
    h.to(55);
    CHECK(!h.menu->gangs().locked(0));
    CHECK(h.menu->gangs().activeSide() == 0);
    h.to(65);
    CHECK(h.menu->screen() == RumbleScreen::GameType);
}

TEST_CASE("back returns to the Rumble screen before, and cancels from the first", "[rumble_menu]") {
    Harness h("10 stick left 0 -70\n12 stick left 0 0\n20 tap cross\n30 tap cross\n40 tap triangle\n50 tap circle\n"
              "60 tap triangle\n");
    h.to(35);
    CHECK(h.menu->screen() == RumbleScreen::ChooseGangs);
    h.to(45);
    CHECK(h.menu->screen() == RumbleScreen::GameType);
    h.to(55);
    CHECK(h.menu->screen() == RumbleScreen::GameMode);
    // The mode chosen before is still selected.
    CHECK(h.menu->grid().selected() == 1);
    (void)h.menu->takeCues();
    CHECK(h.to(65) == RumbleMenuResult::Cancelled);
    CHECK(h.menu->takeCues() == std::vector<int>{coney::gui::kRumbleBackCue});
}

TEST_CASE("the Rumble defaults run the chunks and take each screen's first entry", "[rumble_menu]") {
    Harness h("");
    const coney::gui::RumbleMenuServices services{
        .state = &h.state,
        .data = &h.data,
        .strings = &h.strings,
        .runChunk = [&h](std::string_view chunk) { h.scripts.runFile(chunk); },
    };
    const std::optional<RumbleSetup> setup = coney::gui::rumbleMenuDefaults(services, 117);
    REQUIRE(setup.has_value());
    CHECK(setup.value_or(RumbleSetup{}).values == kDefaultValues);
    CHECK(setup.value_or(RumbleSetup{}).gangNames[1] == "BLUE SIDE");
    CHECK(setup.value_or(RumbleSetup{}).levelNumber == 117);

    // Without the chunks there is nothing to choose.
    h.files.clear();
    CHECK(!coney::gui::rumbleMenuDefaults(services, 102).has_value());
}
