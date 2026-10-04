// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: the game's scripts on the front-end path run in Coney's script system the
// way the original runs them (docs/research/scripting.md#life-of-the-lua-state): the four preloads, then global.lua and
// level100.lua in the same state, then Menu.onStart and the menu callbacks, with no script error and no call of a
// binding Coney lacks. It runs only when the environment variable CONEY_DISC names the disc and skips otherwise. It
// prints counts only, never game data (LEGAL.md).

#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gui/global_strings.h"
#include "scripting/config_strings.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"

namespace {

// A host that records what the scripts ask of the front end.
class RecordingHost final : public coney::script::BindingHost {
  public:
    std::vector<std::string> requests;

    void showProfileManager(std::string_view onRumble, std::string_view onStartGame) override {
        requests.push_back(std::string("ShowProfileManager ").append(onRumble).append(" ").append(onStartGame));
    }
    void showRumbleModeInterface(std::string_view onCancel, std::string_view onStart, double /*players*/) override {
        requests.push_back(std::string("ShowRumbleModeInterface ").append(onCancel).append(" ").append(onStart));
    }
    void menuLoadLevel(std::string_view level) override { requests.push_back("MenuLoadLevel " + std::string(level)); }
    void playMovie(std::string_view name) override { requests.push_back("PlayMovie " + std::string(name)); }
    void playMusic(std::string_view /*track*/) override { requests.emplace_back("music"); }
    void stopMusic() override { requests.emplace_back("stop music"); }
    void queueScreenEffect(int type, double /*seconds*/) override {
        requests.push_back("ScreenQueueEffect " + std::to_string(type));
    }

    // Whether a request starting with `prefix` was made.
    [[nodiscard]] bool asked(std::string_view prefix) const {
        for (const std::string& request : requests) {
            if (request.starts_with(prefix)) {
                return true;
            }
        }
        return false;
    }
};

} // namespace

TEST_CASE("the disc's front-end scripts run without errors in one state", "[disc][scripts]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    if (!wad) {
        return;
    }

    coney::GameState state;
    coney::gui::GlobalStrings strings;
    RecordingHost host;
    coney::script::RecordedCalls recorded;
    const coney::script::BindingContext context{&state, &strings, &host, &recorded};
    std::vector<std::string> log;
    coney::script::ScriptSystem scripts(
        coney::script::wadScriptSource(*wad),
        [&context](coney::script::ScriptSystem& system, coney::script::LuaVm& vm) {
            coney::script::installBindings(system, vm, context);
        },
        [&log](std::string_view line) { log.emplace_back(line); });
    scripts.create();
    const std::size_t librariesAndBindings = scripts.vm().globals().size();

    // The legal screen's preloads, then the front end's level entry and Menu.onStart.
    scripts.runFiles(coney::script::kEnumPreloadScripts);
    scripts.runFiles(coney::script::kConfigPreloadScripts);
    const std::size_t afterPreloads = scripts.vm().globals().size();
    scripts.enterLevel("level100");
    const std::size_t atFrontEnd = scripts.vm().globals().size();
    CHECK(scripts.call("Menu.onStart"));
    CHECK(host.asked("ShowProfileManager Menu.fadeToRMI Menu.startGame"));

    // The level table: 111 records, record 0 the front end with level number 100.
    CHECK(state.levels.count() == 111);
    const coney::LevelRecord* frontEnd = state.levels.at(0);
    REQUIRE(frontEnd != nullptr);
    CHECK(frontEnd->name == "level100");
    CHECK(frontEnd->number == 100.0);

    // The menu callbacks: quick rumble (fade out, then the Rumble mode menus after 500 ms), back, and story.
    CHECK(scripts.call("Menu.fadeToRMI"));
    scripts.update(500, 0.5);
    CHECK(host.asked("ShowRumbleModeInterface Menu.cancelRumbleMode Menu.startRumbleMode"));
    CHECK(scripts.call("Menu.cancelRumbleMode"));
    scripts.update(1000, 0.5);
    // The attract movie PM_Greet asks for after 70 s: fade out, the movie 500 ms later, then the fade back in.
    const std::array<coney::script::Value, 1> movie{coney::script::Value(2.0)};
    CHECK(scripts.call("Menu.playMovie", movie));
    scripts.update(1500, 0.5);
    scripts.update(2000, 0.5);
    CHECK(host.asked("PlayMovie"));
    CHECK(scripts.call("Menu.startGame"));
    CHECK(host.asked("MenuLoadLevel"));
    // The level flow's finish of the front end before it unloads the level.
    CHECK(scripts.call("Menu.onFinish"));

    for (const std::string& line : log) {
        UNSCOPED_INFO(line);
    }
    CHECK(scripts.errors() == 0);
    CHECK(scripts.skippedCalls() == 0);
    std::printf("  scripts: %zu globals with the libraries and bindings, %zu after the preloads, %zu at the front end; "
                "%zu level records, %zu CfgObj calls, %zu recorded configuration calls, %zu HUD strings; "
                "%llu errors, %llu skipped calls, %zu front-end requests\n",
                librariesAndBindings, afterPreloads, atFrontEnd, state.levels.count(), recorded.count("CfgObj"),
                recorded.total(), strings.size(coney::gui::StringTable::Hud),
                static_cast<unsigned long long>(scripts.errors()),
                static_cast<unsigned long long>(scripts.skippedCalls()), host.requests.size());
}
