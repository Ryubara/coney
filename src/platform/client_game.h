// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include "core/error.h"
#include "gamemodes/game_mode.h"
#include "graphics/render_device.h"
#include "net/client_world.h"
#include "platform/net_levels.h"

// The client game: a guest that runs no game of its own and draws the host's from the snapshots it decodes
// (`--join ADDR --net-view`). Coney's own; the original has no network play (docs/guides/client-world.md#client-game).

namespace coney::platform {

class NetPlay;

/// What the client game draws of the host's level: its scenery and the humans of the client's world. ClientLevelScene
/// in the run (src/platform/client_level_scene.h), a fake in the tests. Presentation only: it reads the world and
/// changes nothing the host reads.
/// @coney-only network play; the original has none
class ClientScene {
  public:
    ClientScene() = default;
    virtual ~ClientScene() = default;
    ClientScene(const ClientScene&) = delete;
    ClientScene& operator=(const ClientScene&) = delete;
    ClientScene(ClientScene&&) = delete;
    ClientScene& operator=(ClientScene&&) = delete;

    /// The scene's part of a client step, after the world's (ClientWorld::step()): streaming round its camera.
    virtual void step(const net::ClientWorld& world, const FrameTime& frame) = 0;
    /// Draws a frame of `world` between its last two steps (ClientWorld::drawn() at the frame's alpha) and presents it.
    virtual void render(const net::ClientWorld& world, const RenderTime& time) = 0;
};

/// Loads what the client game draws of a level (its name as the host's LoadLevel gives it, and the checkpoint).
using ClientSceneLoader = std::function<std::expected<std::unique_ptr<ClientScene>, Error>(const LevelAt& level)>;

/// The client game (docs/guides/client-world.md#client-game): the mode a guest runs in place of its own game, so what
/// it shows is the host's. It is the game NetLevels follows the host's levels with (LevelGame): the host's level order
/// loads that level's ClientScene between two steps, and the load counts as in play at once, so the host is told the
/// next step. Each step while in a level it takes the snapshots the session decoded into its net::ClientWorld (made
/// afresh for each load, taking nothing the last level left in the receiver) and steps the world and the scene; each
/// frame the scene draws the world. With no level (before the host's first order, after a quit, or when the session
/// ends) it clears the screen.
///
/// It plays no movie: each of the host's movies is answered as ended at once (NetPlay::movieDone(), not skipped), so
/// the host waits only for its own copy. It sends nothing else: the guest's pad goes to the host as in any game
/// (NetInput), and what the host's game reads is unchanged.
/// @coney-only network play; the original has none
class ClientGame final : public GameMode, public LevelGame {
  public:
    /// The mode's id, outside the original's range.
    static constexpr std::uint32_t kId = 0x10a;

    /// A client game over `net` (which must outlive it), loading levels with `load`; `levelExists` says whether a
    /// level the host names can load here (its level file is on this disc); `print` takes the log lines; `device`
    /// (null: none) is cleared and presented on a frame with no level.
    ClientGame(NetPlay& net, ClientSceneLoader load, std::function<bool(std::string_view)> levelExists,
               std::function<void(std::string_view)> print, graphics::RenderDevice* device = nullptr);

    [[nodiscard]] std::uint32_t id() const override { return kId; }
    /// One step: a leave or a load asked for since the last, then the world's and the scene's step, and the host's
    /// movies answered.
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;
    /// The scene's frame of the world, or a cleared screen with no level.
    void render(const RenderTime& time) override;

    // ---- LevelGame: the host's levels, as NetLevels follows them ----

    /// Whether a level is loaded and drawn.
    [[nodiscard]] bool inPlay() const override;
    [[nodiscard]] std::uint64_t levelStarts() const override { return m_starts; }
    [[nodiscard]] std::optional<LevelAt> currentLevel() const override { return m_level; }
    /// None: the client game ends no mission of its own.
    [[nodiscard]] MissionCounts missionEnds() const override { return {}; }
    /// Loads `level` at the next step; false, changing nothing, when it cannot load here.
    bool jumpToLevel(std::string_view level, int checkpoint) override;
    /// Leaves the level at the next step; false with none loaded or asked for.
    bool leaveLevel() override;

    /// The world of the level loaded; null with none.
    [[nodiscard]] const net::ClientWorld* world() const { return m_world.get(); }
    /// The scene of the level loaded; null with none.
    [[nodiscard]] const ClientScene* scene() const { return m_scene.get(); }

  private:
    // Drops the level: its scene and its world.
    void dropLevel();

    NetPlay& m_net;
    ClientSceneLoader m_load;
    std::function<bool(std::string_view)> m_levelExists;
    std::function<void(std::string_view)> m_print;
    graphics::RenderDevice* m_device;
    std::optional<LevelAt> m_pending;             // jumpToLevel(): loaded at the next step
    bool m_leavePending = false;                  // leaveLevel(): carried out at the next step
    std::uint64_t m_starts = 0;                   // levelStarts()
    std::optional<LevelAt> m_level;               // currentLevel()
    std::unique_ptr<ClientScene> m_scene;         // the level's; null with none, or when its load failed
    std::unique_ptr<net::ClientWorld> m_world;    // the level's; null with none
    std::optional<std::uint32_t> m_movieAnswered; // the host's movie step last answered
};

} // namespace coney::platform
