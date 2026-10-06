// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc: level2's script, run after the preloads with Coney's bindings, places its 26
// doors (14 swinging, 12 barriers) with their numbers and types' CfgObj, as docs/references/doors.md lists them, and
// its glass panes. Runs only when the environment variable CONEY_DISC names the disc; prints counts only (LEGAL.md).

#include <cstddef>
#include <cstdio>
#include <optional>
#include <string_view>
#include <utility>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/level_start.h"
#include "gui/global_strings.h"
#include "scripting/config_strings.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"
#include "world_objects/level_objects.h"

namespace {

// The disc named by CONEY_DISC, opened; nothing when it is not set.
std::optional<coney::io::Wad> openDisc() {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        return std::nullopt;
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    return wad ? std::optional<coney::io::Wad>(std::move(*wad)) : std::nullopt;
}

// A binding host that ignores every request.
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

} // namespace

TEST_CASE("the disc's level2 places its doors and panes", "[disc][objects]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    coney::script::RecordedCalls recorded;
    coney::CreatedHumans humans;
    coney::world_objects::WorldFlags flags;
    coney::world_objects::LevelObjects objects;
    QuietHost host;
    const coney::script::BindingContext context{.state = &state,
                                                .strings = &strings,
                                                .host = &host,
                                                .recorded = &recorded,
                                                .humans = &humans,
                                                .flags = &flags,
                                                .objects = &objects};
    coney::script::ScriptSystem scripts(coney::script::wadScriptSource(*wad),
                                        [&context](coney::script::ScriptSystem& system, coney::script::LuaVm& vm) {
                                            coney::script::installBindings(system, vm, context);
                                        },
                                        {});
    scripts.create();
    scripts.runFiles(coney::script::kEnumPreloadScripts);
    scripts.runFiles(coney::script::kConfigPreloadScripts);
    scripts.create();
    state.checkPoint = 1;
    state.currentLevel = state.levels.find("level2").value_or(0);
    (void)coney::runLevelScript(scripts, state, humans, flags, "level2");

    std::size_t swinging = 0;
    std::size_t barriers = 0;
    std::size_t leaves = 0;
    std::size_t configured = 0;
    for (const coney::world_objects::Door& door : objects.doors.doors()) {
        swinging += door.doorClass == coney::world_objects::DoorClass::Swinging ? 1 : 0;
        barriers += door.doorClass == coney::world_objects::DoorClass::Barrier ? 1 : 0;
        leaves += door.leaves.size();
        configured += door.maxHitpoints > 0 ? 1 : 0;
        CHECK(door.number >= 1);
        CHECK(door.number <= 26);
    }
    CHECK(objects.doors.doors().size() == 26);
    CHECK(swinging == 14);
    CHECK(barriers == 12);
    CHECK(configured == 26);
    std::size_t glassTypes = 0;
    for (int type = 0; type < static_cast<int>(coney::world_objects::kGlassTypeCount); ++type) {
        const coney::world_objects::GlassType* entry = objects.glass.type(type);
        glassTypes += entry != nullptr && (entry->sprite != 0 || entry->windowLink || entry->alarm) ? 1 : 0;
    }
    CHECK(glassTypes > 0);
    std::printf("  level2: %zu doors (%zu swinging, %zu leaves, %zu barriers), %zu panes, %zu glass types set\n",
                objects.doors.doors().size(), swinging, leaves, barriers, objects.glass.panes().size(), glassTypes);
}
