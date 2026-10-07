// SPDX-License-Identifier: GPL-3.0-or-later
// The HUD bindings (docs/references/bindings/hud.md): their arguments as tolua reads them, acting on the context's HUD;
// with no HUD they do nothing.
#include "scripting/hud_bindings.h"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "gui/global_strings.h"
#include "hud/hud.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
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

    explicit Harness(coney::hud::Hud* hud)
        : scripts(
              [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
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

TEST_CASE("the objective header takes the HUD string and the CfgHUDColor recording", "[scripting][hud]") {
    coney::hud::Hud hud;
    Harness harness(&hud);
    hud.setServices(coney::script::hudServicesOf(harness.context));
    harness.strings.set(0xe5, "OBJECTIVE:");
    harness.call("CfgHUDColor", {Value(4.0), Value("<COLOR ffcc00ff>")});
    harness.call("HUDSetObjective", {Value(0.0), Value("Go"), Value(0.0)});
    REQUIRE(hud.scrollIn().messages().size() == 1);
    CHECK(hud.scrollIn().messages().front().text == "<SIZE 0.8><YOBJ></SIZE><COLOR ffcc00ff>OBJECTIVE:<CR>Go");
}

TEST_CASE("the tutorial bindings queue, check, flush and keep the callback", "[scripting][hud]") {
    coney::hud::Hud hud;
    Harness harness(&hud);
    harness.call("HUDSetTutorialText", {Value("Press X")});
    REQUIRE(hud.hints().queued().size() == 1);
    CHECK(hud.hints().queued().front().priority == 1);
    CHECK(harness.value("HUDCheckTutorialText", {Value("Press X")}).number() == 1.0);
    harness.call("HUDSetTutorialText", {Value("Later"), Value(3.0)});
    harness.call("HUDFlushTutorialText", {Value(3.0)});
    CHECK(hud.hints().queued().size() == 1);
    harness.call("HUDFlushTutorialText", {});
    CHECK(hud.hints().queued().empty());
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
