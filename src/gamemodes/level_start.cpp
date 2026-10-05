// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/level_start.h"

#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include "gui/global_strings.h"
#include "scripting/script_bindings.h"

namespace coney {

namespace {

// What the bindings ask of the game while a level's scripts run alone: there are no menus, modes or audio, so every
// request is dropped. A level script that asks for a level (`MenuLoadLevel`) or a movie is not acted on.
class QuietHost final : public script::BindingHost {
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

LevelStart runLevelScript(script::ScriptSystem& scripts, const GameState& state, CreatedHumans& humans,
                          std::string_view level) {
    // The humans of the level before are gone: the original's unload frees every slot.
    humans.clear();
    scripts.enterLevel(level);

    // Player 1 as the script made him. A creation without a readable position cannot be placed, so it counts as none.
    LevelStart start;
    start.level = std::string(level);
    start.checkpoint = static_cast<int>(std::trunc(state.checkPoint));
    if (const HumanCreation* player = humans.player(1); player != nullptr && player->position) {
        start.player = *player;
    }
    return start;
}

LevelScriptRun runLevelScriptAlone(const script::ScriptSource& source, std::string_view level, int checkpoint,
                                   const std::function<void(std::string_view)>& log) {
    // What the bindings work on: a game state, the strings and configuration the preloads fill, and the humans.
    GameState state;
    gui::GlobalStrings strings;
    script::RecordedCalls recorded;
    CreatedHumans humans;
    QuietHost host;
    const script::BindingContext context{&state, &strings, &host, &recorded, &humans};
    script::ScriptSystem scripts(
        source,
        [&context](script::ScriptSystem& system, script::LuaVm& vm) { script::installBindings(system, vm, context); },
        log);

    // The legal screen's preloads, in the first Lua state: they fill the level table.
    scripts.create();
    scripts.runFiles(script::kEnumPreloadScripts);
    scripts.runFiles(script::kConfigPreloadScripts);

    // The front end's unload makes a fresh state, and runNextMission sets the checkpoint and chooses the level by
    // name. A level the table does not list gets index 0 (Coney's choice: the menus never ask for one).
    scripts.create();
    state.checkPoint = checkpoint;
    state.currentLevel = state.levels.find(level).value_or(0);

    LevelScriptRun run;
    run.start = runLevelScript(scripts, state, humans, level);
    run.scriptErrors = scripts.errors();
    run.skippedCalls = scripts.skippedCalls();
    run.humans = humans.all().size();
    return run;
}

} // namespace coney
