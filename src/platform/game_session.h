// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "core/chunk_system.h"
#include "core/error.h"
#include "core/language.h"
#include "debug/debug_session.h"
#include "debug/play_controls.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/loading_screen.h"
#include "gamemodes/start_up_flow.h"
#include "graphics/font.h"
#include "graphics/particle_page.h"
#include "gui/global_strings.h"
#include "movies/movie_mode.h"
#include "platform/movie_screen.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "world/sector_budget.h"

namespace coney::audio {
class GameSound;
class ObjectSounds;
} // namespace coney::audio

namespace coney::platform {

class AudioOutput;

/// What a game session starts with that differs between runs.
struct GameSessionSettings {
    Language language = Language::English; ///< The legal screen's and the loading screens' language.
    bool skipMovies = false;               ///< `--skip-movies`: every movie ends at once.
    std::optional<RumbleLaunch> rumble;    ///< `--rumble`: the match the Rumble menu starts, whatever it chose.
    /// The saved profiles' folder (DiskProfileStore); nothing: they last the run, as in test mode.
    std::optional<std::filesystem::path> profiles;
    /// How long the memory-card mode shows its "checking" message (MemoryCardMode::kCheckingMessageMs in the game).
    std::uint64_t cardCheckingMs = 0;
};

/// The one way Coney sets up the game over a disc: the start-up flow (StartUpFlow: the script system, the game state,
/// the menus, gameplay with its pause and mission screens) with everything `main` wires into it, the play mode each
/// level loads, the scenes, the movie player, the loading screen, the glass and door sounds, the HUD's and the pause's
/// hooks and, once attachAudio() is called, the game's sound. A story started from the menus (startStory()), a level
/// started directly (startAtLevel(): `--play-level`, the disc tests) and the debug menus' jumps (jumpToLevel()) all run
/// in it, so they differ only in the level and checkpoint they start at
/// (docs/guides/testing.md#test-through-the-players-path).
///
/// Coney's own wiring; each part cites its research where it is made.
class GameSession {
  public:
    /// Sets everything up over `wad` on `stack`, drawing through `renderer`; every argument must outlive the session,
    /// and `log` gets the modes', the scripts' and the play modes' lines. Nothing runs until startStory() or
    /// startAtLevel().
    GameSession(RenderEngine& renderer, GameModeStack& stack, const io::Wad& wad,
                const chunk::ChunkHandlerTable& chunkHandlers, world::SectorBudget& budget, gui::GlobalStrings& strings,
                audio::ObjectSounds& objectSounds, const GameSessionSettings& settings,
                std::function<void(std::string_view)> log);
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;
    GameSession(GameSession&&) = delete;
    GameSession& operator=(GameSession&&) = delete;
    ~GameSession() = default;

    /// Gives the session the game's sound: the scripts' sound bindings, the front end's banks, music and cues, the
    /// loading screen's banks, the movies' mixer, the pause's sound, the HUD's sounds and the play modes' scene sounds.
    /// Call it before the first frame; null: the run stays silent.
    void attachAudio(AudioOutput* audio);
    /// The debug lines every level's play mode draws (the debug session's); null: none.
    void setDebugDraw(const debug::DebugDrawOptions* options) { m_debugDraw = options; }

    /// What the first level loaded does before its first frame (`--start`, `--camera`, `--trace`, `--scene`): called
    /// once, on that level's play mode; an error fails that level's load.
    using LevelSetup = std::function<std::expected<void, Error>(PlayLevelMode&)>;
    void setFirstLevelSetup(LevelSetup setup) { m_firstLevelSetup = std::move(setup); }

    /// The game from boot: the start-up movies, the legal screen, the memory-card check and the menus.
    void startStory();
    /// The game started directly at `level` and `checkpoint` (StartUpFlow::startAtLevel(); `ready` runs once the
    /// level's Lua state is fresh, before its scripts).
    void startAtLevel(std::string_view level, int checkpoint, std::function<void()> ready = {});
    /// The debug menus' jump to `level` at `checkpoint`, carried out at the next beginFrame(); false, changing nothing,
    /// when the level table has no such level.
    bool jumpToLevel(std::string_view level, int checkpoint);
    /// Carries out a jump asked for since the last frame. Call it between frames, outside any step.
    void beginFrame();

    /// The start-up flow.
    [[nodiscard]] StartUpFlow& flow() { return *m_flow; }
    /// Gameplay (mode 1).
    [[nodiscard]] GameplayMode& gameplay() { return m_flow->gameplay(); }
    /// Whether the next step is a frame of play: gameplay on top of the stack with a level playing.
    [[nodiscard]] bool inPlay() const;
    /// The play mode of the level in gameplay; null when none is loaded.
    [[nodiscard]] PlayLevelMode* play();
    /// The movie player.
    [[nodiscard]] movies::MovieMode& movies() { return *m_movieMode; }
    /// Loads a sprite sheet resource by name (or a WAD file name), as the menus do.
    [[nodiscard]] std::expected<graphics::SpriteSheet, Error> loadSheet(std::string_view name) const;

  private:
    // The play mode gameplay loads for a level start: the scripts' cast and configuration, the session's hooks, and
    // for the first level the setup given.
    std::expected<std::unique_ptr<GameMode>, Error> loadLevel(const LevelStart& start, const ScriptedCast& cast);
    // A fresh scene system over the disc's scene list, read once; null when the list cannot be read.
    std::unique_ptr<scenes::SceneSystem> makeScenes();

    RenderEngine& m_renderer;
    GameModeStack& m_stack;
    const io::Wad& m_wad;
    const chunk::ChunkHandlerTable& m_chunkHandlers;
    world::SectorBudget& m_budget;
    audio::ObjectSounds& m_objectSounds;
    std::function<void(std::string_view)> m_log;
    const debug::DebugDrawOptions* m_debugDraw = nullptr;
    AudioOutput* m_audio = nullptr;
    LevelSetup m_firstLevelSetup;
    std::optional<std::string> m_pendingLevel;
    int m_pendingCheckpoint = 1;
    std::optional<scenes::SceneList> m_sceneList;
    bool m_sceneListFailed = false;
    std::optional<RasterMovieScreen> m_movieScreen;
    std::optional<graphics::Font> m_captionFont;
    std::optional<movies::MovieMode> m_movieMode;
    std::optional<LoadingScreen> m_loadingScreen;
    std::unique_ptr<StartUpFlow> m_flow; // after everything its modes point at
};

/// Points the debug menus' game services (the scripts the Lua console and the Cheats page call into, the recorded
/// configuration, the game state) at the session `session` returns, whichever way it started; while it returns null
/// they have none and the menus use their sandbox state. `session` is asked anew each time.
void connectDebugServices(debug::DebugServices& services, const std::function<GameSession*()>& session);

} // namespace coney::platform
