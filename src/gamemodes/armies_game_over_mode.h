// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string_view>
#include <utility>

#include "core/interpolation.h"
#include "gamemodes/front_end_services.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/pause_mode.h"
#include "graphics/render_device.h"
#include "graphics/screen_fade.h"
#include "gui/global_strings.h"
#include "gui/pause_menu/armies_game_over_menu.h"
#include "warriors/game_state.h"

namespace coney {

namespace hud {
class Hud;
}

class GameModeStack;
class GameplayMode;
class LevelFlowMode;

/// What the Armies of the Night game-over mode needs from the level below it.
struct ArmiesGameOverHooks {
    /// Whether player `player` (0 or 1) is in the game (the game state's `+0x228` / `+0x22c` human, whose pad record
    /// the screen reads); empty: player 1 only.
    std::function<bool(int player)> playerIn;
    /// Whether player `player` (0 or 1) is in the game and not knocked out (`Human_IsKnockedOut` on the game state's
    /// `+0x228` / `+0x22c` human); empty: player 1 only, standing.
    std::function<bool(int player)> playerUp;
    /// Heals player `player` in full and stands him up (`Human_Heal(human, 1)`, state `0x20000000`): he plays on.
    std::function<void(int player)> healPlayer;
};

/// Game mode 0xd, the Armies of the Night game over (`ANGameOver_Toggle`, `0x001557f8`): `HUDLaunchANGameOver` pushes
/// it over gameplay (launch()), which then stands still beneath it, and it shows gui::ArmiesGameOverMenu.
///
/// - **Start** from either pad in the countdown: with a credit left (`HUDANSetCredit`'s counter) one is spent, the
///   Lua function `F1.Time` is called and, while credits remain, both panels' join messages come on; the players
///   continuing are the one who pressed and the other if he is in and standing. Without a credit the end phase
///   starts, as it does when the count runs out.
/// - **The end phase** fades the world to black over 2 s (screen effect 1 on each player's view, `0x005fdeb8`) under
///   the texts.
/// - **Closing** (ANGameOver_Toggle again): Continue heals the continuing players and turns their join messages off;
///   **Replay** loads `level60` at section 1; **Quit** calls Lua `runNextMission(0)`, as the pause menu's quit to the
///   hangout does.
///
/// Coney's stand-ins (docs/research/pause.md#an-game-over), each marked where it acts: the screen closes at once where
/// the original waits for its last sound cue; the original's refusal of a Start in `level64` section 4 while the
/// presser's brain `+9` is set is not built; the sound is left playing.
///
/// Research: docs/research/pause.md#an-game-over, docs/research/armies.md
class ArmiesGameOverMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 0xd;
    /// The world's fade to black in the end phase, in seconds (`0x40000000`).
    static constexpr double kEndFadeSeconds = 2.0;
    /// The level and section Replay loads (`0x0054e7d8`).
    static constexpr std::string_view kReplayLevel = "level60";
    /// The Lua functions it calls (`0x00555b08`, `0x0054e798`).
    static constexpr std::string_view kContinueFunction = "F1.Time";
    static constexpr std::string_view kQuitFunction = "runNextMission";

    /// Every reference must outlive the mode; `hud` holds the credits and join messages (null: no credits).
    /// @coney-only wires Coney's services; the original's mode is a static object (`Gm_ANGameOver_StaticInit`)
    ArmiesGameOverMode(graphics::RenderDevice& device, PauseSheetLoader loadSheet, PauseRecordLoader loadRecord,
                       const gui::GlobalStrings& strings, FrontEndServices& services, GameState& state,
                       LevelFlowMode& levelFlow, GameModeStack& stack, hud::Hud* hud,
                       std::function<void(std::string_view)> log);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// Sets what it needs from the level.
    void setHooks(ArmiesGameOverHooks hooks) { m_hooks = std::move(hooks); }
    /// Draws `world` (not owned; null: black) under the screen.
    void setWorld(GameplayMode* world) { m_world = world; }

    /// `HUDLaunchANGameOver(on)` (on is read and not used): pushes the mode, or, when it is on top, closes it with no
    /// choice made (the original's toggle).
    /// @orig 0x001cede8 HUD_LaunchANGameOver (unknown)
    /// @orig 0x001557f8 ANGameOver_Toggle (unknown)
    void launch();

    /// Loads the fonts; the screen opens on the next update.
    /// @orig 0x00155b30 Gm_Mode0D_Enter (unknown)
    void enter() override;
    /// Start from either pad, the screen's frame and, once it has a choice, the close.
    /// @orig 0x00155c78 Gm_ANGameOver_Update (unknown)
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;
    /// The world, the end phase's fade and the screen.
    /// @orig 0x00155c78 Gm_ANGameOver_Update (unknown)
    void render(const RenderTime& time) override;
    /// Releases the fonts.
    /// @orig 0x00155bc0 Gm_ANGameOver_Exit (unknown)
    void exit() override;

    /// The screen.
    [[nodiscard]] const gui::ArmiesGameOverMenu& menu() const { return m_menu; }

  private:
    // Start pressed by `player`: the players continuing, a credit spent and F1.Time, or the end phase.
    /// @orig 0x001cf468 ANGameOver_Update (unknown)
    void pressStart(int player, std::uint64_t nowMs);
    // The close: pops the mode and acts on the choice (ANGameOver_Toggle's pop half).
    /// @orig 0x001557f8 ANGameOver_Toggle (unknown)
    void close(GameModeStack& stack, gui::ArmiesGameOverChoice choice);
    // Whether player `player` is in and standing.
    /// @orig 0x001cf468 ANGameOver_Update (unknown)
    [[nodiscard]] bool playerUp(int player) const;
    // Whether player `player` is in the game.
    /// @orig 0x001cf468 ANGameOver_Update (unknown)
    [[nodiscard]] bool playerIn(int player) const;

    graphics::RenderDevice& m_device;
    PauseSheetLoader m_loadSheet;
    PauseRecordLoader m_loadRecord;
    const gui::GlobalStrings& m_strings;
    FrontEndServices& m_services;
    GameState& m_state;
    LevelFlowMode& m_levelFlow;
    GameModeStack& m_stack;
    hud::Hud* m_hud;
    std::function<void(std::string_view)> m_log;
    ArmiesGameOverHooks m_hooks;
    GameplayMode* m_world = nullptr;
    gui::ArmiesGameOverMenu m_menu;
    MenuLayer m_layer;
    graphics::ScreenFade m_fade;
    Interpolated<float> m_fadeLevel{0.0F}; // the fade at the last two steps
    RenderTime m_frozen;
    std::array<bool, 2> m_continuing{}; // +0x7ac, +0x7b0
    bool m_openPending = false;
    bool m_fading = false;
};

} // namespace coney
