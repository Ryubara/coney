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

namespace coney {

class GameModeStack;

/// Game mode 6, the memory-card check, on its boot path. The original scans the memory cards, shows the "checking"
/// message (global string `0xb5`, 3,000 ms) in its message box and any dialog a missing, unformatted or full card calls
/// for, and pops when the save system and the box are done; its exit marks the boot check done and, when the level
/// flow is the mode below, tells it not to load the front end on its next resume.
///
/// **Coney's choices:** Coney has no memory card (its saves will be files, and none exist yet), so the scan finds
/// nothing to ask about: the mode shows the "checking" message for its time on black, then leaves, as the original
/// does with an unformatted card (the message, then no dialog). Which dialog a boot with no card or a saved profile
/// shows is an open question of the research. **Test mode:** the message's time is a constructor argument; `coney`
/// passes the original's kCheckingMessageMs, while the frame-scripted test harnesses pass 0 so the menus come up on the
/// frames their scripts are written for (the message then lasts the mode's one frame).
///
/// Research: docs/research/frontend.md#mode-flow, docs/research/frontend.md#message-box
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

    /// Draws through `device` with the fonts from `loadSheet` and the message from `strings`; `stack` is the stack this
    /// mode runs on and `levelFlow` the mode 8 its exit may find below it. Shows the "checking" message for
    /// `checkingMs` (kCheckingMessageMs in the game, 0 in the frame-scripted tests). Each reference must outlive the
    /// mode; `log` gets a line when a font fails to load.
    MemoryCardMode(graphics::RenderDevice& device, const GameModeStack& stack, LevelFlowMode& levelFlow,
                   SheetLoader loadSheet, const gui::GlobalStrings& strings, std::uint64_t checkingMs,
                   std::function<void(std::string_view)> log = {});

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// Asks for the boot check: what `main` does before pushing the mode.
    /// @orig 0x0015a270 MemoryCard_SetBootCheck (Gm_MemoryCard.cpp)
    void setBootCheck() { m_bootCheck = BootCheck::Pending; }
    /// The boot-check flag.
    [[nodiscard]] BootCheck bootCheck() const { return m_bootCheck; }

    /// Starts the check: loads the message box's font; the message is shown on the first update, which knows the
    /// time. The original also starts the card scan here, which Coney has none to do.
    /// @orig 0x0015baa0 Mode6::Enter (Gm_MemoryCard.cpp)
    void enter() override;

    /// One frame of the box; leaves once the message's time is over.
    /// @orig 0x0015be00 Mode6::Update (Gm_MemoryCard.cpp)
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    /// Clears the screen to black, draws the box of the last update and presents.
    void render(const RenderTime& time) override;

    /// Marks the boot check done and, when the level flow is now on top (the mode below), cancels its front-end load
    /// on resume; releases the font.
    /// @orig 0x0015c2c0 Mode6::Exit (Gm_MemoryCard.cpp)
    void exit() override;

    /// The message box.
    [[nodiscard]] const gui::MessageBox& messageBox() const { return m_box; }

  private:
    graphics::RenderDevice& m_device;
    const GameModeStack& m_stack;
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
};

} // namespace coney
