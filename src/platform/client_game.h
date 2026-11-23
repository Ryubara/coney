// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include "core/error.h"
#include "gamemodes/client_event_player.h"
#include "gamemodes/game_mode.h"
#include "graphics/render_device.h"
#include "hud/hud.h"
#include "hud/hud_owner.h"
#include "human/locomotion.h"
#include "net/client_camera.h"
#include "net/client_world.h"
#include "platform/net_levels.h"
#include "scripting/sound_bindings.h"

// The client game: a guest that runs no game of its own and draws the host's from the snapshots it decodes
// (`--join ADDR --net-view`). Coney's own; the original has no network play (docs/guides/client-world.md#client-game).

namespace coney::platform {

class NetPlay;

/// What the client game draws of the host's level: its scenery, the humans of the client's world and the HUD the client
/// game steps. ClientLevelScene in the run (src/platform/client_level_scene.h), a fake in the tests. Presentation
/// only: it reads the world and the HUD and changes nothing the host reads.
/// @coney-only network play; the original has none
class ClientScene {
  public:
    ClientScene() = default;
    virtual ~ClientScene() = default;
    ClientScene(const ClientScene&) = delete;
    ClientScene& operator=(const ClientScene&) = delete;
    ClientScene(ClientScene&&) = delete;
    ClientScene& operator=(ClientScene&&) = delete;

    /// The scene's part of a client step, after the world's (ClientWorld::step()), the camera's and the HUD's:
    /// streaming round the client's camera (`camera`, whose view is none while it follows nobody: the level's start
    /// then), and the HUD's newest step queued for drawing.
    virtual void step(const net::ClientWorld& world, const net::ClientCamera& camera, const FrameTime& frame) = 0;
    /// Draws a frame of `world` between its last two steps (ClientWorld::drawn() at the frame's alpha) through the
    /// client's camera (ClientCamera::view() at the same alpha) with the HUD over it, and presents it.
    virtual void render(const net::ClientWorld& world, const net::ClientCamera& camera, const RenderTime& time) = 0;
    /// The level's collision, which the client's camera pulls in by; null for none (the default).
    [[nodiscard]] virtual const raycast::CollisionMesh* collision() const { return nullptr; }
    /// The gait speeds of the human of character `character` (net::HumanState::character), which the client's camera
    /// reads his gait by; the default, none of them, reads every move as the sneak (nearest of zeros).
    [[nodiscard]] virtual human::Speeds speedsOf(std::int32_t /*character*/) { return human::Speeds{}; }
};

/// Loads what the client game draws of a level (its name as the host's LoadLevel gives it, and the checkpoint), with
/// the HUD owner the client game steps for it, which the scene draws and which outlives the scene.
using ClientSceneLoader =
    std::function<std::expected<std::unique_ptr<ClientScene>, Error>(const LevelAt& level, hud::HudOwner& hud)>;

/// What the client game presents the host's events and HUD with on this machine: the sound that plays the host's
/// sounds and music (null: none), and the services the HUD reads (its strings, colours and sound output, which the run
/// fills from the disc; empty: a HUD with no strings and no sound).
/// @coney-only network play; the original has none
struct ClientPresentation {
    script::SoundHost* sound = nullptr;
    hud::HudServices hud;
};

/// The client game (docs/guides/client-world.md#client-game): the mode a guest runs in place of its own game, so what
/// it shows is the host's. It is the game NetLevels follows the host's levels with (LevelGame): the host's level order
/// loads that level's ClientScene between two steps, and the load counts as in play at once, so the host is told the
/// next step. Each step while in a level it takes the snapshots the session decoded into its net::ClientWorld (made
/// afresh for each load, taking nothing the last level left in the receiver) and steps the world and the scene; each
/// frame the scene draws the world. With no level (before the host's first order, after a quit, or when the session
/// ends) it clears the screen.
///
/// It presents the host's game events and draws the HUD of the guest's own player slot. Each step, once the world's
/// playback has reached a host step, the events of the steps up to it are handed over once each, in the order the host
/// raised them, and presented (ClientEventPlayer); then the level's HUD, made afresh for each level, takes the guest's
/// slot's panel state and the HUD's standing state from the newest snapshot (net::standingForSlot()) and steps.
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
    /// (null: none) is cleared and presented on a frame with no level; `presentation` is what the host's events and
    /// HUD are presented with (its sound must outlive the game).
    ClientGame(NetPlay& net, ClientSceneLoader load, std::function<bool(std::string_view)> levelExists,
               std::function<void(std::string_view)> print, graphics::RenderDevice* device = nullptr,
               ClientPresentation presentation = {});

    [[nodiscard]] std::uint32_t id() const override { return kId; }
    /// One step: a leave or a load asked for since the last, then the world's step, the host's events and the HUD's
    /// step and the scene's, and the host's movies answered.
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
    /// The client's own camera of the level loaded: the follow camera on the human its player slot follows
    /// (net::ClientCamera); with no level, or none to follow, it has no view.
    [[nodiscard]] const net::ClientCamera& camera() const { return m_camera; }
    /// The HUD of the level loaded, as the guest's own player slot has it; null with none.
    [[nodiscard]] const hud::Hud* hud() const { return m_hud ? &m_hud->hud() : nullptr; }
    /// How many of the host's events were presented on this machine.
    [[nodiscard]] std::uint64_t eventsPresented() const { return m_events.presented(); }
    /// How many of the host's events were of a kind this machine cannot present yet (ClientEventPlayer).
    [[nodiscard]] std::uint64_t eventsUnpresented() const { return m_events.unpresented(); }

  private:
    // Drops the level: its scene, its HUD and its world, the sounds the host started in it and the events of it the
    // playback had not reached.
    void dropLevel();
    // Presents the host's events the world's playback has reached (once each, in order).
    void presentEvents();
    // Steps the client's camera one step on the human its slot follows, with this machine's own right stick.
    void stepCamera(const GameModeStack& stack, const FrameTime& frame);
    // Steps the level's HUD one step: the guest's slot's panel state and the standing state of the newest snapshot.
    void stepHud(const FrameTime& frame);

    NetPlay& m_net;
    ClientSceneLoader m_load;
    ClientPresentation m_presentation;
    std::function<bool(std::string_view)> m_levelExists;
    std::function<void(std::string_view)> m_print;
    graphics::RenderDevice* m_device;
    std::optional<LevelAt> m_pending;             // jumpToLevel(): loaded at the next step
    bool m_leavePending = false;                  // leaveLevel(): carried out at the next step
    std::uint64_t m_starts = 0;                   // levelStarts()
    std::optional<LevelAt> m_level;               // currentLevel()
    std::unique_ptr<hud::HudOwner> m_hud;         // the level's HUD; null with none; outlives m_scene, which draws it
    std::unique_ptr<ClientScene> m_scene;         // the level's; null with none, or when its load failed
    std::unique_ptr<net::ClientWorld> m_world;    // the level's; null with none
    net::ClientCamera m_camera;                   // the level's: made afresh for each load
    ClientEventPlayer m_events;                   // presents the host's events on this machine's sound and the HUD
    std::uint64_t m_hudMs = 0;                    // the HUD's own game time: this level's steps counted
    int m_levelNumber = 0;                        // the level's number as the HUD reads it (`level99`: 99)
    std::optional<std::uint32_t> m_movieAnswered; // the host's movie step last answered
};

} // namespace coney::platform
