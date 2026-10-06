// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string_view>

#include "core/error.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/level_flow_mode.h"
#include "graphics/font.h"
#include "graphics/overlay_camera.h"
#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"
#include "gui/global_strings.h"
#include "gui/message_box.h"
#include "gui/widget.h"
#include "warriors/profile_store.h"

namespace coney {

class GameModeStack;
class ProfileManagerMode;

/// Game mode 6, the memory-card mode: the boot's check (pushed by `main`), RELOAD PROFILES (`SSMC_StartLoadSequence`)
/// and the write after PM_Delete (`SSMC_StartDeleteSequence`), the last two pushed over the menus. The original scans
/// the memory cards, shows the "checking" message (global string `0xb5`, 3,000 ms) in its message box and any dialog a
/// missing, unformatted or full card calls for, loads the profiles (load kind) or writes the card (save kind, here with
/// the delete flag), and pops when the save system and the box are done; its exit marks the boot check done, tells the
/// level flow below not to load the front end on its next resume, and asks the menus to fade in when they resume
/// (docs/research/save.md#mode-6).
///
/// **Coney's choices:** Coney has no memory card: its profiles are files (docs/research/save.md#coney). The load is the
/// profile store's reload() at enter; the delete has nothing left to write (the store deleted the file when PM_Delete
/// asked). The scan finds nothing to ask about, so every entry shows the "checking" message for its time on black and
/// leaves, as the original does with an unformatted card (the message, then no dialog). After a load or delete over
/// the menus, they re-open the screen on top so it lists the profiles held (the original's PM_Profile builds its items
/// when it opens; what its menus do after the load is not traced). The menus' fade in is asked for only when they are
/// the mode below, so the boot's path is unchanged. **Test mode:** the message's time is a constructor argument;
/// `coney` passes the original's kCheckingMessageMs, while the frame-scripted test harnesses pass 0 so the menus come
/// up on the frames their scripts are written for (the message then lasts the mode's one frame).
///
/// Research: docs/research/frontend.md#mode-flow, docs/research/frontend.md#message-box, docs/research/save.md#mode-6
class MemoryCardMode final : public GameMode {
  public:
    /// The original's id for this mode.
    static constexpr std::uint32_t kId = 6;
    /// The global string of the "checking memory card" message, and how long the original shows it.
    static constexpr std::uint32_t kCheckingString = 0xb5;
    static constexpr std::uint64_t kCheckingMessageMs = 3000;
    /// The message's text batch: how many sprites a frame, and its depth.
    static constexpr std::size_t kTextCapacity = 512;
    static constexpr float kTextDepth = 9000.0F;

    /// Loads a sprite sheet by its resource name; the platform layer reads it from the disc.
    using SheetLoader = std::function<std::expected<graphics::SpriteSheet, Error>(std::string_view resourceName)>;

    /// The boot-check flag: 0 none, 1 the boot check is to run, 2 it ran.
    enum class BootCheck : std::uint8_t { None = 0, Pending = 1, Done = 2 };

    /// What the mode does on its next entry (`0x005e5d80`, with the delete flag `0x0050c700`).
    enum class Kind : std::uint8_t {
        Load,   ///< Read the profiles: the boot and RELOAD PROFILES.
        Delete, ///< Write the deleted profiles out: after PM_Delete.
    };

    /// Draws through `device` with the fonts from `loadSheet` and the message from `strings`; `stack` is the stack this
    /// mode runs on (and pushes itself on for a load or delete) and `levelFlow` the mode 8 its exit may find below it.
    /// Shows the "checking" message for `checkingMs` (kCheckingMessageMs in the game, 0 in the frame-scripted tests).
    /// Each reference must outlive the mode; `log` gets a line when a font fails to load.
    MemoryCardMode(graphics::RenderDevice& device, GameModeStack& stack, LevelFlowMode& levelFlow,
                   SheetLoader loadSheet, const gui::GlobalStrings& strings, std::uint64_t checkingMs,
                   std::function<void(std::string_view)> log = {});

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// The save system the load reads (must outlive the mode); null: nothing to load.
    void setProfiles(ProfileStore* profiles) { m_profiles = profiles; }
    /// The menus (mode 0x12) the exit asks to fade in when they are below (must outlive the mode); null: none.
    void setProfileManager(ProfileManagerMode* profileManager) { m_profileManager = profileManager; }

    /// Asks for the boot check: what `main` does before pushing the mode.
    /// @orig 0x0015a270 MemoryCard_SetBootCheck (Gm_MemoryCard.cpp)
    void setBootCheck() { m_bootCheck = BootCheck::Pending; }
    /// The boot-check flag.
    [[nodiscard]] BootCheck bootCheck() const { return m_bootCheck; }

    /// `SSMC_StartLoadSequence`: pushes the mode in its load kind unless it is already on top.
    /// @orig 0x00155378 SSMC_StartLoadSequence (unknown)
    void startLoadSequence();
    /// `SSMC_StartDeleteSequence`: pushes the mode to write the deleted profiles out unless it is already on top.
    /// @orig 0x001553c0 SSMC_StartDeleteSequence (unknown)
    void startDeleteSequence();

    /// Starts the entry: the load (the profile store reads its folder again) or, for a delete, nothing; then loads the
    /// message box's font, the message being shown on the first update, which knows the time. The original starts the
    /// card scan here instead.
    /// @orig 0x0015baa0 Mode6::Enter (Gm_MemoryCard.cpp)
    void enter() override;

    /// One frame of the box; leaves once the message's time is over.
    /// @orig 0x0015be00 Mode6::Update (Gm_MemoryCard.cpp)
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// Clears the screen to black, draws the box of the last update and presents.
    void render(const RenderTime& time) override;

    /// Marks the boot check done and, by the mode now on top (the mode below): cancels the level flow's front-end load
    /// on resume, or asks the menus to fade in. The next entry is a load again; releases the font.
    /// @orig 0x0015c2c0 Mode6::Exit (Gm_MemoryCard.cpp)
    void exit() override;

    /// The message box.
    [[nodiscard]] const gui::MessageBox& messageBox() const { return m_box; }
    /// The kind of the next (or current) entry.
    [[nodiscard]] Kind kind() const { return m_kind; }
    /// Loads since start-up.
    [[nodiscard]] std::uint64_t loads() const { return m_loads; }

  private:
    // Pushes the mode as `kind` unless it is already on top (then the kind on top stands).
    void startSequence(Kind kind);

    graphics::RenderDevice& m_device;
    GameModeStack& m_stack;
    LevelFlowMode& m_levelFlow;
    SheetLoader m_loadSheet;
    const gui::GlobalStrings& m_strings;
    std::uint64_t m_checkingMs;
    std::function<void(std::string_view)> m_log;
    gui::MessageBox m_box;
    gui::GuiCanvas m_canvas;
    std::optional<graphics::Font> m_bigFont;
    std::optional<graphics::SpriteBatch> m_bigBatch;
    graphics::OverlayCamera m_camera;
    graphics::OverlayPass m_pass;
    BootCheck m_bootCheck = BootCheck::None;
    bool m_showPending = false; // enter() ran; the message starts on the next update
    ProfileStore* m_profiles = nullptr;
    ProfileManagerMode* m_profileManager = nullptr;
    Kind m_kind = Kind::Load;
    std::uint64_t m_loads = 0;
};

} // namespace coney
