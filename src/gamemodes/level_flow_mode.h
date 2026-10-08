// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gamemodes/front_end_scene.h"
#include "gamemodes/front_end_services.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/loading_screen.h"
#include "gamemodes/profile_manager_mode.h"
#include "graphics/render_device.h"
#include "scenes/scene_player.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"

namespace coney {

class GameModeStack;
class GameplayMode;

/// Game mode 8, the level flow: the bottom of the stack. On its first entry it starts the front end (level 0,
/// `level100`: its scripts, the `menu` music and the level script's `Menu.onStart`, which shows the profile manager);
/// once the menus choose a level (`MenuLoadLevel`) it finishes the front end and starts that level. It never leaves.
///
/// Fields, as the original's (`+0x20`, `+0x24`, `+0x28`): the level chosen next (-1 none), whether the front-end level
/// is loaded, and "load the front end on the next resume" (set by enter(), cleared by the memory-card mode's exit
/// when this mode is below it).
///
/// Coney's stand-ins (docs/research/frontend.md#coneys-implementation):
/// - **`InitLevel`** loads the level's world as the scene behind the menus (FrontEndScene, through the loader given),
///   then its script step: the level entry (`global.lua`, then `level100.lua`). Without a loader the background is
///   black.
/// - **The front end's scenes**: with a scene maker, `InitLevel` also makes the level's scene system, hosted by the
///   world and stepped by the menus, so `level100.lua`'s `WonderWheelAnim` plays `WonderWheel_100`
///   (docs/research/frontend.md#background). Until the front end finishes, the scene bindings work on it and the
///   lighting bindings on the world's lights, as gameplay's do for a level.
/// - **Without scripts** (no script system, or a `Menu.onStart` that does not show the menus) Coney shows the profile
///   manager itself with the two callbacks `Menu.onStart` passes, so the menus always come up.
/// - **Starting a chosen level**: the update finishes the front end as the original does (`Menu.onFinish`, then the
///   unload, which makes a fresh Lua state), selects the level and pushes gameplay (mode 1, GameplayMode). A flow made
///   without gameplay (no disc to load levels from, as in the tests) logs "level start requested" instead and starts
///   the front end again, so the player is back on the menus.
/// - The original's waits for the save system and its vsync and timer switches have nothing to act on.
///
/// Research: docs/research/frontend.md#mode-flow, docs/research/frontend.md#mode-8-fields
class LevelFlowMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 8;
    /// "No level chosen".
    static constexpr int kNoLevel = -1;
    /// The front end's level name, when the level table has no record 0.
    static constexpr std::string_view kFrontEndLevel = "level100";
    /// The Lua callbacks `Menu.onStart` hands to `ShowProfileManager`, for the stand-in without scripts.
    static constexpr std::string_view kOnRumble = "Menu.fadeToRMI";
    static constexpr std::string_view kOnStartGame = "Menu.startGame";
    /// The fade in `Menu.onStart` starts, for the stand-in without scripts.
    static constexpr double kOnStartFadeSeconds = 1.5;

    /// Draws through `device`, pushes `profileManager` on `stack`, sends music to `services`, runs `scripts` and
    /// selects levels in `state`; each must outlive the mode. `log` gets a line when the front end starts or a level
    /// is asked for. A chosen level starts in `gameplay`, pushed on `stack`; null keeps the stand-in above.
    LevelFlowMode(graphics::RenderDevice& device, GameModeStack& stack, ProfileManagerMode& profileManager,
                  FrontEndServices& services, script::ScriptSystem& scripts, GameState& state,
                  std::function<void(std::string_view)> log, GameplayMode* gameplay = nullptr);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// Sets "load the front end on resume" and resumes.
    /// @orig 0x0015c688 Mode8::Enter (unknown)
    void enter() override;

    /// Starts the front end when asked to and no level is chosen.
    /// @orig 0x0015c6f8 Mode8::Resume (unknown)
    void resume() override;

    /// One step: with a level chosen, finishes the front end, selects the level and pushes gameplay (or, without
    /// gameplay, starts the front end again); then the scripts. Always stays.
    /// @orig 0x0015c858 Mode8::Update (unknown)
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// The front-end world's frame: no world yet, so the black background the front end sets, presented.
    void render(const RenderTime& time) override;

    /// `MenuLoadLevel(name)`: chooses the level named `name` (by the level table) to start on the next update. A name
    /// the table does not have is logged and ignored.
    /// @orig 0x00160d78 MenuLoadLevel_Choose (unknown)
    /// @orig 0x0015c7b0 LevelFlow_ChooseLevel (unknown)
    void chooseLevel(std::string_view name);

    /// Chooses the level at `index` of the level table to start on the next update (the mission-complete mode's
    /// "reload" and "next level"). An index with no record is logged and ignored.
    void chooseLevelIndex(std::size_t index);

    /// Coney's direct start (`--play-level`, the debug menu's jumps before the front end, the disc tests): the next
    /// enter skips the front end. It runs what the legal screen and the front end run before a story level, the
    /// preloads (runPreloadScripts()), then `ready` (when set) in that same Lua state, where the menus would have run
    /// (the Rumble menu's set-up of an arena), then the fresh Lua state of the front end's unload; it sets the
    /// checkpoint and chooses `level`, which gameplay then starts exactly as it starts a level chosen from the menus.
    /// After the level, the front end comes as usual.
    ///
    /// Coney's tool: the original always starts through the menus (docs/research/level-loading.md#story-into-level99).
    void startAtLevel(std::string level, int checkpoint, std::function<void()> ready = {});

    /// Loads the front-end world through `loadScene` when the front end starts (empty: none, a black background).
    void setSceneLoader(FrontEndSceneLoader loadScene) { m_loadScene = std::move(loadScene); }
    /// The front-end world while the front end is loaded; null otherwise or without a loader.
    [[nodiscard]] FrontEndScene* scene() const { return m_scene.get(); }

    /// Makes the front end's scene system (over the disc's scene list); null or empty: no scenes.
    using SceneMaker = std::function<std::unique_ptr<scenes::SceneSystem>()>;
    /// Makes a scene system with `maker` each time the front end starts, and hands it (and the world's lights and
    /// screen tint) to the bindings through `context` while the front end is loaded. `context` must outlive the mode.
    void setScenes(SceneMaker maker, script::BindingContext* context) {
        m_sceneMaker = std::move(maker);
        m_context = context;
    }
    /// The front end's scene system while the front end is loaded; null otherwise or without a maker.
    [[nodiscard]] scenes::SceneSystem* scenes() const { return m_scenes.get(); }

    /// Shows the start-up front end's load behind `screen`'s memory-card form (LoadingScreen::beginMemoryCard()):
    /// while the profile manager's load-screen flag is set (`0x0050f5b8`, 1 at boot), the front end's load is timed
    /// as gameplay's is: the screen fades in, the front end loads, the screen holds until kMemoryCardHoldMilliseconds
    /// after its start, fades out, then the menus show. Null, or after the first time: the load is immediate.
    /// `screen` must outlive the mode. docs/research/level-loading.md#memory-card-screen
    void setLoadingScreen(LoadingScreen* screen) { m_loadingScreen = screen; }
    /// How long the memory-card screen stays up from its start before its fade out. **Coney's stand-in** for the
    /// PS2's start-up load, long enough for the second picture and its spinner (from 5 s in) to show.
    static constexpr std::uint64_t kMemoryCardHoldMilliseconds = 7000;
    /// Whether the memory-card screen is up (from the resume that starts the front end to its fade out's end).
    [[nodiscard]] bool memoryCardScreenUp() const { return m_cardLoad.has_value(); }

    /// What the memory-card mode's exit does to this mode when it is below: no front end on the next resume.
    void cancelFrontEndLoad() { m_loadFrontEndOnResume = false; }

    /// The level chosen next (`+0x20`), kNoLevel for none.
    [[nodiscard]] int chosenLevel() const { return m_chosenLevel; }
    /// Whether the front-end level is loaded (`+0x24`).
    [[nodiscard]] bool frontEndLoaded() const { return m_frontEndLoaded; }
    /// Whether the next resume starts the front end (`+0x28`).
    [[nodiscard]] bool loadFrontEndOnResume() const { return m_loadFrontEndOnResume; }
    /// The level selected last (`level100` once the front end started; empty before).
    [[nodiscard]] const std::string& currentLevel() const { return m_currentLevel; }
    /// Levels the menus asked to start, oldest first (by name).
    [[nodiscard]] const std::vector<std::string>& levelRequests() const { return m_levelRequests; }

  private:
    /// Selects level 0, runs its scripts, loads the sound bank `menu`, calls `Menu.onStart` and marks the front end
    /// loaded.
    /// @orig 0x0015c4b0 LevelFlow_StartFrontEnd (unknown)
    void startFrontEnd();
    /// startFrontEnd()'s load: level 0 selected, its world, scenes and scripts, the sound bank `menu`.
    void loadFrontEnd();
    /// startFrontEnd()'s end: `Menu.onStart` (or Coney's own showing of the menus) and the front end marked loaded.
    void showMenus();
    /// One step of the memory-card screen at game time `nowMs` (the step began `stepMs` earlier): begun on the first,
    /// the front end loaded once faded in, finished after the hold, the menus shown once the fade out is over.
    /// @orig 0x001619d0 MemCardLoadScreen_Tick (unknown)
    void stepMemoryCardScreen(std::uint64_t nowMs, std::uint64_t stepMs);

    /// Calls `Menu.onFinish` and unloads the front-end level: its part of `UnloadLevel` here is the fresh Lua state.
    /// @orig 0x0015c5f8 LevelFlow_FinishFrontEnd (unknown)
    void finishFrontEnd();

    /// InitLevel's world step for the front end: loads the scene of the current level, logs a failure, and hands it
    /// to the menus.
    void loadScene();

    /// InitLevel's scenes for the front end: a fresh scene system hosted by the world, its Lua calls into the scripts,
    /// given to the bindings and to the menus, which step it.
    void makeScenes();
    /// Takes the front end's scene system and lights back from the bindings and the menus, and drops the system.
    void dropScenes();

    graphics::RenderDevice& m_device;
    GameModeStack& m_stack;
    ProfileManagerMode& m_profileManager;
    FrontEndServices& m_services;
    script::ScriptSystem& m_scripts;
    GameState& m_state;
    std::function<void(std::string_view)> m_log;
    GameplayMode* m_gameplay; // where a chosen level starts; null: the stand-in
    int m_chosenLevel = kNoLevel;
    bool m_frontEndLoaded = false;
    bool m_loadFrontEndOnResume = false;
    LoadingScreen* m_loadingScreen = nullptr; // the memory-card screen's; null: none
    bool m_loadScreenFlag = true;             // the profile manager's `0x0050f5b8`: the next front end's load is timed
    // The memory-card screen's load while it is up: its phase and start.
    struct CardLoad {
        enum class Phase : std::uint8_t { FadeIn, Loading, FadeOut } phase = Phase::FadeIn;
        std::optional<std::uint64_t> startMs;
    };
    std::optional<CardLoad> m_cardLoad;
    std::string m_currentLevel;
    std::vector<std::string> m_levelRequests;
    FrontEndSceneLoader m_loadScene;
    std::unique_ptr<FrontEndScene> m_scene; // the front-end world while the front end is loaded
    SceneMaker m_sceneMaker;
    script::BindingContext* m_context = nullptr;   // where the bindings find the scenes and lights; null: not given
    std::unique_ptr<scenes::SceneSystem> m_scenes; // the front end's scenes while it is loaded
    // The direct start startAtLevel() asked for, carried out by the next enter.
    struct DirectStart {
        std::string level;
        int checkpoint = 1;
        std::function<void()> ready;
    };
    std::optional<DirectStart> m_directStart;
};

} // namespace coney
