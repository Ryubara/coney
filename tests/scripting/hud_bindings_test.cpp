// SPDX-License-Identifier: GPL-3.0-or-later
// The HUD bindings (docs/references/bindings/hud.md): their arguments as tolua reads them, acting on the context's HUD;
// with no HUD they do nothing.
#include "scripting/hud_bindings.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/game_events.h"
#include "gui/global_strings.h"
#include "hud/hud.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "support/lua_fixtures.h"
#include "support/optional_value.h"
#include "warriors/game_state.h"

using Catch::Approx;
using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Value;

namespace {

// A host that ignores every request: these bindings ask nothing of it.
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

// A script system with Coney's bindings acting on `hud` (null for none).
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    coney::script::RecordedCalls recorded;
    QuietHost host;
    coney::script::BindingContext context;
    ScriptSystem scripts;

    std::map<std::string, std::vector<std::byte>, std::less<>> files; ///< The scripts runFile() finds, by name.

    explicit Harness(coney::hud::Hud* hud)
        : scripts(
              [this](std::string_view name) -> std::expected<std::vector<std::byte>, coney::Error> {
                  if (const auto found = files.find(name); found != files.end()) {
                      return found->second;
                  }
                  return coney::fail(coney::ErrorCode::NotFound, "no such script in this test");
              },
              [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); }, {}) {
        context.state = &state;
        context.strings = &strings;
        context.host = &host;
        context.recorded = &recorded;
        context.hud = hud;
        scripts.create();
    }

    // Calls the binding `name` with `args`; REQUIREs success and returns its first result (nil for none).
    Value value(std::string_view name, const std::vector<Value>& args) {
        auto result = scripts.vm().call(scripts.vm().global(name), args);
        REQUIRE(result.has_value());
        return result->empty() ? Value() : result->front();
    }

    // Calls the binding `name` with `args`; REQUIREs success.
    void call(std::string_view name, const std::vector<Value>& args) { (void)value(name, args); }
};

} // namespace

TEST_CASE("the HUD bindings are real and do nothing without a HUD", "[scripting][hud]") {
    for (const std::string_view name : coney::script::kHudBindings) {
        const auto& table = coney::script::bindingTable();
        const auto found = std::ranges::find(table, name, &coney::script::BindingInfo::name);
        REQUIRE(found != table.end());
        CHECK(found->kind == coney::script::BindingKind::Real);
    }
    Harness harness(nullptr);
    harness.call("HideHud", {});
    harness.call("HUDSetObjective", {Value(0.0), Value("x"), Value(0.0)});
    CHECK(harness.value("HUDGetNewPH", {Value(2.0), Value("Tags")}).number() == coney::script::kNoCounterPanel);
    CHECK(harness.value("HUDCheckTutorialText", {Value("x")}).isNil());
}

TEST_CASE("HUDSetObjective reads slot, text, mode, silent and ms, 8000 by default", "[scripting][hud]") {
    coney::hud::Hud hud;
    Harness harness(&hud);
    harness.call("HUDSetObjective", {Value(0.0), Value("Follow Ash"), Value(0.0)});
    REQUIRE(hud.scrollIn().messages().size() == 1);
    CHECK(hud.scrollIn().messages().front().ms == 8000);
    harness.call("HUDSetObjective", {Value(1.0), Value("Win"), Value(0.0), Value(1.0), Value(3000.0)});
    CHECK(hud.scrollIn().messages().size() == 1);
    REQUIRE(hud.checklist().slots[1].has_value());
    harness.call("HUDRemoveAllGoalText", {});
    CHECK_FALSE(hud.checklist().slots[0].has_value());
}

// Expected: unverified, #547 (the event is Coney's own network play; the original has none)
TEST_CASE("HUDSetObjective raises its objective as a game event while recording", "[scripting][hud][events]") {
    coney::hud::Hud hud;
    Harness harness(&hud);
    coney::GameEvents events;
    harness.context.events = &events;
    // Offline nothing is kept.
    harness.call("HUDSetObjective", {Value(0.0), Value("Follow Ash"), Value(0.0)});
    CHECK(events.size() == 0);
    events.setRecording(true);
    harness.call("HUDSetObjective", {Value(1.0), Value("Win"), Value(2.0), Value(1.0), Value(3000.0)});
    const std::vector<coney::GameEvent> raised = events.take();
    REQUIRE(raised.size() == 1);
    CHECK(
        raised[0] ==
        coney::GameEvent{
            .kind = coney::GameEventKind::Objective, .id = 1, .value = (2U << 1U) | 1U, .extra = 3000, .text = "Win"});
    // The HUD shows it as before.
    REQUIRE(hud.checklist().slots[1].has_value());
}

// Expected: unverified, #547 (the event is Coney's own network play; the original has none)
TEST_CASE("HUDRemoveAllGoalText raises slot 0's clearing as an objective event while recording",
          "[scripting][hud][events]") {
    coney::hud::Hud hud;
    Harness harness(&hud);
    coney::GameEvents events;
    harness.context.events = &events;
    harness.call("HUDSetObjective", {Value(0.0), Value("Follow Ash"), Value(0.0)});
    REQUIRE(hud.checklist().slots[0].has_value());
    events.setRecording(true);
    harness.call("HUDRemoveAllGoalText", {});
    // The event HUDSetObjective(0, "", 1, silent) raises: slot 0 cleared (mode 1), silently, so a guest's HUD that
    // applies it as an objective clears the same line.
    const std::vector<coney::GameEvent> raised = events.take();
    REQUIRE(raised.size() == 1);
    CHECK(raised[0] == coney::GameEvent{.kind = coney::GameEventKind::Objective, .id = 0, .value = (1U << 1U) | 1U});
    // The HUD clears it as before.
    CHECK_FALSE(hud.checklist().slots[0].has_value());
}

// Expected: unverified, #547 (the event is Coney's own network play; the original has none)
TEST_CASE("HUDSetAnnounceMsg raises its message as a game event while recording", "[scripting][hud][events]") {
    coney::hud::Hud hud;
    Harness harness(&hud);
    coney::GameEvents events;
    events.setRecording(true);
    harness.context.events = &events;
    harness.call("HUDSetAnnounceMsg", {Value(5.0), Value("<DISPLAYTIME 4000>Go"), Value(1.0)});
    const std::vector<coney::GameEvent> raised = events.take();
    REQUIRE(raised.size() == 1);
    CHECK(raised[0] ==
          coney::GameEvent{
              .kind = coney::GameEventKind::HudMessage, .id = 5, .value = 1, .text = "<DISPLAYTIME 4000>Go"});
}

// Expected: unverified, #547 (the events are Coney's own network play; the original has none)
TEST_CASE("the tutorial bindings raise their hints as game events while recording", "[scripting][hud][events]") {
    coney::hud::Hud hud;
    Harness harness(&hud);
    coney::GameEvents events;
    harness.context.events = &events;
    // Offline nothing is kept.
    harness.call("HUDSetTutorialText", {Value("Press X")});
    CHECK(events.size() == 0);
    events.setRecording(true);
    harness.call("HUDSetTutorialText", {Value("Later"), Value(3.0)});
    harness.call("HUDSetTutorialText", {Value()});
    harness.call("HUDFlushTutorialText", {Value(3.0)});
    harness.call("HUDFlushTutorialText", {});
    harness.call("HUDEnableGameTutorialText", {Value(1.0)});
    // The callback is the server's own script's business: no event.
    harness.call("HUDSetTutorialCallback", {Value("P1.BasicAttacks")});
    const std::vector<coney::GameEvent> raised = events.take();
    using coney::GameEvent;
    using coney::GameEventKind;
    REQUIRE(raised.size() == 5);
    CHECK(raised[0] == GameEvent{.kind = GameEventKind::Hint, .id = 0, .value = 3, .text = "Later"});
    CHECK(raised[1] == GameEvent{.kind = GameEventKind::Hint, .id = 1});
    CHECK(raised[2] == GameEvent{.kind = GameEventKind::Hint, .id = 2, .value = 3});
    CHECK(raised[3] == GameEvent{.kind = GameEventKind::Hint, .id = 2, .value = coney::hud::kHintFlushAll});
    CHECK(raised[4] == GameEvent{.kind = GameEventKind::Hint, .id = 3, .value = 1});
    // The HUD acts as before.
    CHECK(hud.gameTutorialText());
}

TEST_CASE("the objective header takes the HUD string and the CfgHUDColor recording", "[scripting][hud]") {
    coney::hud::Hud hud;
    Harness harness(&hud);
    hud.setServices(coney::script::hudServicesOf(harness.context));
    harness.strings.set(0xe5, "OBJECTIVE:");
    harness.call("CfgHUDColor", {Value(4.0), Value("<COLOR ffcc00ff>")});
    harness.call("HUDSetObjective", {Value(0.0), Value("Go"), Value(0.0)});
    REQUIRE(hud.scrollIn().messages().size() == 1);
    CHECK(hud.scrollIn().messages().front().text ==
          "<SIZE 0.8><MONEYFONT><YOBJ></MONEYFONT></SIZE><COLOR ffcc00ff>OBJECTIVE:<CR>Go");
}

TEST_CASE("the tutorial bindings queue, check, flush and keep the callback", "[scripting][hud]") {
    coney::hud::Hud hud;
    Harness harness(&hud);
    harness.call("HUDSetTutorialText", {Value("Press X")});
    // Shown at once on the free box.
    REQUIRE(hud.hints().showing().has_value());
    const coney::hud::Hint shown = hud.hints().showing().value_or(coney::hud::Hint{});
    CHECK(shown.text == "Press X");
    CHECK(shown.priority == 1);
    CHECK(harness.value("HUDCheckTutorialText", {Value("Press X")}).number() == 1.0);
    harness.call("HUDSetTutorialText", {Value("Later"), Value(3.0)});
    harness.call("HUDFlushTutorialText", {Value(3.0)});
    CHECK(hud.hints().showing().has_value());
    CHECK(hud.hints().queued().empty());
    harness.call("HUDFlushTutorialText", {});
    CHECK_FALSE(hud.hints().showing().has_value());
    harness.call("HUDSetTutorialCallback", {Value("P1.BasicAttacks")});
    CHECK(hud.tutorialCallback() == "P1.BasicAttacks");
    harness.call("HUDSetTutorialCallback", {Value()});
    CHECK(hud.tutorialCallback().empty());
    harness.call("HUDEnableGameTutorialText", {Value(1.0)});
    CHECK(hud.gameTutorialText());
}

TEST_CASE("the panel bindings: show, hide, force-show and flash", "[scripting][hud]") {
    coney::hud::Hud hud;
    hud.attachPlayer(0, 32);
    Harness harness(&hud);
    harness.call("ShowHud", {Value(0.0)});
    harness.call("HideHud", {});
    CHECK_FALSE(hud.visible());
    harness.call("RestoreHud", {});
    CHECK(hud.visible());
    harness.call("ForceShowPlayerHud", {Value(0.0), Value(1.0)});
    CHECK(hud.panel(0).forceShow());
    harness.call("ForceShowPlayerHud", {Value(5.0), Value(1.0)}); // no such player: ignored
    harness.call("FlashRageBar", {Value(0.0), Value(5.0)});
    CHECK(hud.panel(0).flashFrames() == 5);
    harness.call("HidePlayerHud", {});
    CHECK_FALSE(hud.panel(0).shown());
    harness.call("ShowPlayerHud", {});
    CHECK(hud.panel(0).shown());
}

TEST_CASE("the counter-panel, arrow, announcement and radar bindings", "[scripting][hud]") {
    coney::hud::Hud hud;
    Harness harness(&hud);
    const Value panel = harness.value("HUDGetNewPH", {Value(2.0), Value("Tags")});
    CHECK(panel.number() == 0.0);
    CHECK(hud.counterPanels().panels()[0].barWidth == coney::hud::kCounterPanelBarWidth);
    harness.call("HUDSetPHValue", {panel, Value(2.0), Value(6.0), Value(1.0)});
    harness.call("HUDSetPHValue", {panel, Value(1.0), Value(2.0), Value(1.0), Value(4000000000.0)});
    CHECK(hud.counterPanels().panels()[0].visible);
    CHECK(hud.counterPanels().panels()[0].hideAtMs == std::optional<std::uint64_t>(4000000000ULL));
    harness.call("HUDReleasePH", {panel});
    CHECK_FALSE(hud.counterPanels().panels()[0].used);

    harness.call("HUDEnableInstArrow", {Value(1.0), Value(0.2), Value(0.1), Value(3.14)});
    CHECK(hud.arrow().on);
    CHECK(hud.arrow().angle == 3.14F);
    harness.call("HUDSetAnnounceMsg", {Value(5.0), Value("CHEAT"), Value()});
    CHECK(coney::test::got(hud.centredAnnouncement()).text == "CHEAT");

    harness.call("HUDTurnOffRadar", {});
    CHECK_FALSE(hud.radar().on[0]);
    harness.call("HUDTurnOnRadar", {Value(1.0)});
    CHECK(hud.radar().on[1]);
    CHECK_FALSE(hud.radar().on[0]);
    harness.call("HUDAddRadarMissionObjective", {Value(12.0)});
    harness.call("HUDSetRadarItemTexture", {Value(12.0), Value(27.0)});
    harness.call("HUDSetRadarObjectFlash", {Value(12.0), Value(1.0)});
    REQUIRE(hud.radar().blips.contains(12.0));
    CHECK(hud.radar().blips.at(12.0).type == 10);
    CHECK(hud.radar().blips.at(12.0).icon == 27);
    // The dot's 0.7 times the call's factor (1 by default); a new objective blinks for 100 updates.
    CHECK(hud.radar().blips.at(12.0).scale == Approx(0.7F));
    CHECK(hud.radar().blips.at(12.0).flashCountdown == 100);
    CHECK(hud.radar().blips.at(12.0).flashing);
    // The lock (the fourth argument) keeps the icon and size until a call clears it; factors compound.
    harness.call("HUDSetRadarItemTexture", {Value(12.0), Value(28.0), Value(0.5), Value(1.0)});
    CHECK(hud.radar().blips.at(12.0).icon == 28);
    CHECK(hud.radar().blips.at(12.0).scale == Approx(0.35F));
    harness.call("HUDSetRadarItemTexture", {Value(12.0), Value(22.0)});
    CHECK(hud.radar().blips.at(12.0).icon == 28);
    harness.call("HUDSetRadarItemTexture", {Value(12.0), Value(29.0), Value(0.8)});
    CHECK(hud.radar().blips.at(12.0).icon == 29);
    CHECK(hud.radar().blips.at(12.0).scale == Approx(0.28F));
    CHECK(hud.radar().blips.at(12.0).colour == coney::hud::kRadarDealerColour);
    harness.call("HUDAddSecondaryRadarMissionObjective", {Value(13.0)});
    harness.call("HUDAddRadarHuman", {Value(14.0)});
    CHECK(hud.radar().blips.size() == 3);
    CHECK(hud.radar().blips.at(14.0).icon == 365);
    CHECK(hud.radar().blips.at(14.0).scale == Approx(0.56F));
    // The range and the zoom scale.
    harness.call("HUDRadarSetRange", {Value(25.0), Value(35.0)});
    CHECK(hud.radar().rest == 25.0F);
    CHECK(hud.radar().fast == 35.0F);
    CHECK(hud.radar().zoom == 25.0F);
    harness.call("HUDSetRadarZoomScale", {Value(0.5)});
    CHECK(hud.radar().zoomScale == 0.5F);
    harness.call("HUDDeleteRadarMissionObjective", {Value(12.0)});
    harness.call("HUDDeleteRadarObject", {Value(14.0)});
    CHECK(hud.radar().blips.size() == 1);
}

TEST_CASE("PreloadCredits runs credits.lua into a fresh list; ShowCredits starts the scroll",
          "[script][hud][credits]") {
    coney::hud::Hud hud;
    Harness harness(&hud);
    // credits.lua's stand-in: a heading, then a role line with a name.
    coney::test::LuaAsm script;
    script.getGlobal("CfgCredits").pushInt(0).pushString("Heading").call(2);
    script.getGlobal("CfgCredits").pushInt(1).pushString("Role").pushString("Name").call(3);
    harness.files.emplace("credits.lua", coney::test::luaChunk(script.end()));
    // A line configured before the load is dropped by it.
    harness.call("CfgCredits", {Value(4.0), Value(std::string("Early")), Value()});
    REQUIRE(hud.credits().lines().size() == 1);
    CHECK(hud.credits().lines()[0].style == 2);
    CHECK(hud.credits().lines()[0].name.empty());
    harness.call("ShowCredits", {});
    CHECK_FALSE(hud.credits().running());
    harness.call("PreloadCredits", {});
    CHECK(hud.credits().loaded());
    REQUIRE(hud.credits().lines().size() == 2);
    CHECK(hud.credits().lines()[0].role == "Heading");
    CHECK(hud.credits().lines()[1].role == "Role");
    CHECK(hud.credits().lines()[1].name == "Name");
    harness.call("ShowCredits", {});
    CHECK(hud.credits().running());
}

TEST_CASE("the credit bindings do nothing without a HUD", "[script][hud][credits]") {
    // Expected: frontend.md#fn-credits, confirmed (code), for the bindings; a missing HUD is Coney's own harness
    Harness harness(nullptr);
    harness.call("PreloadCredits", {});
    harness.call("CfgCredits", {Value(1.0), Value(std::string("Role")), Value(std::string("Name"))});
    harness.call("ShowCredits", {});
}

TEST_CASE("HUDSetANBossTexture(human, texture, on) registers a boss's picture on both Armies panels; on defaults to "
          "true and nil removes it",
          "[scripting][hud]") {
    // Expected: hud-functions-lua.md#fn-hudlua, confirmed (code): lua_HUDSetANBossTexture (0x0036e8d8) reads the human
    // and the texture as unsigned integers and `on` as a boolean defaulting to true; HUD_SetANBossTexture
    // (0x001b5b80) hands them to both panels.
    coney::hud::Hud hud;
    Harness harness(&hud);
    // level60_destroyers' Lemmy, as its main chunk registers him.
    harness.call("HUDSetANBossTexture", {Value(40.0), Value(1966095.0)});
    CHECK(hud.armies().bossTexture(0, 40) == std::optional<std::uint32_t>{1966095});
    CHECK(hud.armies().bossTexture(1, 40) == std::optional<std::uint32_t>{1966095});
    // A Lua 4 false is nil: the picture leaves.
    harness.call("HUDSetANBossTexture", {Value(40.0), Value(1966095.0), Value()});
    CHECK_FALSE(hud.armies().bossTexture(0, 40).has_value());
    harness.call("HUDSetANBossTexture", {Value(41.0), Value(1966110.0), Value(1.0)});
    CHECK(hud.armies().bossTexture(1, 41) == std::optional<std::uint32_t>{1966110});
    harness.call("HUDSetANBossTexture", {Value(41.0), Value(1966110.0), Value(0.0)});
    CHECK_FALSE(hud.armies().bossTexture(1, 41).has_value());
}

TEST_CASE("HUDANEnableJoinMsg(player, on) reads a negative player as player 1's panel", "[scripting][hud]") {
    // Expected: hud-functions-lua.md#fn-hudlua, confirmed (code): lua_HUDANEnableJoinMsg (0x003701c8) reads the player
    // as an unsigned integer, which tolua's conversion gives as 0 for a negative number: panel 0.
    coney::hud::Hud hud;
    Harness harness(&hud);
    hud.armies().levelSetUp();
    REQUIRE(hud.armies().joinMessage(0));
    harness.call("HUDANEnableJoinMsg", {Value(-1.0), Value()});
    CHECK_FALSE(hud.armies().joinMessage(0));
    CHECK(hud.armies().joinMessage(1));
}
