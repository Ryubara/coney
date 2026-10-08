// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "core/pad.h"
#include "graphics/render_device.h"
#include "hud/counting_number.h"
#include "hud/hud_audio.h"
#include "hud/hud_canvas.h"
#include "hud/hud_layout.h"
#include "hud/num_indicator.h"

namespace coney::hud {

/// What a player panel shows, read from the player each frame: the original's panel reads the human, the stats
/// object and the inventory itself; Coney's play mode hands the values over.
struct PanelValues {
    int rage = 0;           ///< Human `+0x650`.
    int rageMax = 78;       ///< The Warrior class's s16 `+0x00`.
    bool raging = false;    ///< Human `+0xe0` flag `0x80000`.
    bool altBanner = false; ///< Human state flag `0x200000`: the banner turns blue-grey.
    int score = 0;          ///< The player's score in the stats object.
    int money = 0;          ///< Inventory item 2.
    /// The counted items' numbers, in kCounterItems order (flash, spray paint, handcuffs, keys).
    std::array<int, 4> items{};
    bool selectShows = true;  ///< The per-player record's `+0x1b`: SELECT shows the panel.
    bool promptWakes = false; ///< The player's action prompt names a dealer's goods (promptWakesPanel()): activity.
};

/// Whether an action prompt `text` wakes the player panel: it names `Spray`, `Flash`, `Blades` or `Give Mon` (the
/// dealers' offers and the money one). **Coney's reading**: "names" as contains, matched in any language as the
/// English words (the research gives only these).
[[nodiscard]] bool promptWakesPanel(std::string_view text);

/// Where the four item-counter slots are (x from the panel's base, y), for a money amount `money`; nothing for 1,000
/// or more, where the slots keep their previous places.
[[nodiscard]] std::optional<std::array<GuiPoint, 4>> counterSlots(int money);

/// The sheet-table record of the name banner for character type `type` (human `+0xcc`): the kinds of
/// `PlayerHUD_SetBanner` by id, and record `0x31` for every other type. **Coney's choice:** the kinds the original
/// picks from the human's s8 `+0x1b0` (the language-dependent banners) are not modelled, so those types get `0x31`.
/// @orig 0x0020dc98 PlayerHUD_SetBanner (unknown)
/// @orig 0x00229570 Human_BannerKind (unknown)
[[nodiscard]] std::uint32_t bannerRecord(int type);

/// One player's panel at the top of the screen: the name banner in the rage colour, the rage meter, the score, the
/// money and up to four item counters. It shows when something on it changes (or SELECT is pressed), stays 2 s and
/// fades out over 1 s; ForceShowPlayerHud keeps it up. There is no health bar.
///
/// update() runs on the fixed step with the game time; render() adds the sprites of the newest step.
///
/// Research: docs/research/hud.md#the-player-panel
/// @orig 0x00211ca0 PlayerHUD::PlayerHUD (unknown)
class PlayerPanel {
  public:
    /// The panel's player index (0 or 1), for its layout.
    explicit PlayerPanel(std::size_t player);

    /// Attaches the panel to a player of character type `type` and picks its banner (`HUD_AttachPlayer`): the first
    /// attach also starts the panel's fade clock as activity. Returns false when it was attached already.
    /// @orig 0x00212840 PlayerHUD_Init (unknown)
    bool attach(int type);
    /// Whether a player is attached (`+0x40f8`).
    [[nodiscard]] bool attached() const { return m_attached; }
    /// The banner's sheet-table record.
    [[nodiscard]] std::uint32_t banner() const { return m_banner; }

    /// Shows the panel when it is attached and may show; hides it otherwise (`+0x40fc`).
    /// @orig 0x0020e028 PlayerHUD_Show (unknown)
    void show();
    /// Hides the panel.
    void hide() { m_shown = false; }
    /// Whether the panel is shown (attached and not hidden).
    [[nodiscard]] bool shown() const { return m_shown; }
    /// The "may show" flag (`+0x4108`) HidePlayerHud clears and ShowPlayerHud sets.
    void setMayShow(bool on) { m_mayShow = on; }
    [[nodiscard]] bool mayShow() const { return m_mayShow; }
    /// ForceShowPlayerHud (`+0x4104`): every frame is activity while on.
    void setForceShow(bool on) { m_forceShow = on; }
    [[nodiscard]] bool forceShow() const { return m_forceShow; }
    /// FlashRageBar (`+0x4114`): the meter drawn `frames` frames and skipped as many, repeating; 0 draws it always.
    void setFlashFrames(std::uint32_t frames);
    [[nodiscard]] std::uint32_t flashFrames() const { return m_flashFrames; }

    /// One frame at game time `nowMs`: the values (counting, popups, the counters' slots), the activity that restarts
    /// the fade (a changed score, money or count, SELECT on `pad`, ForceShow), the full meter's sound, the flash
    /// counter.
    /// @orig 0x00214138 PlayerHUD_Update (unknown)
    void update(const PanelValues& values, const Pad* pad, std::uint64_t nowMs, const HudSound& audio);

    /// The panel's alpha at the last update: 1 for kPanelHoldMs after the last activity, then down to 0 over
    /// kPanelFadeMs.
    [[nodiscard]] float fade() const;
    /// Whether the meter is drawn this frame (FlashRageBar's blinking).
    [[nodiscard]] bool meterVisible() const;
    /// The meter's fill and capacity, 0-1: min(rage, max) / 150 and max / 150.
    [[nodiscard]] float fill() const;
    [[nodiscard]] float capacity() const;
    /// The fill colour now: gold or red by rage, or, for a full meter, a gold pulse every 400 ms.
    [[nodiscard]] graphics::Rgba fillColour(bool swapped) const;
    /// The banner's colour now.
    [[nodiscard]] graphics::Rgba bannerColour(bool swapped) const;
    /// The score and the money as shown (counting).
    [[nodiscard]] const CountingNumber& score() const { return m_score; }
    [[nodiscard]] const CountingNumber& money() const { return m_money; }
    /// The counters' slots now (x from the base, y).
    [[nodiscard]] const std::array<GuiPoint, 4>& slots() const { return m_slots; }
    /// `HUDSetNumIndicator` for this panel's player (interface slots `+0x48` and `+0x50`): the tally on or off and the
    /// living members it counts, which Hud::update() refreshes.
    void setTally(bool on, std::uint32_t count) {
        m_tallyOn = on;
        m_tallyCount = count;
    }
    [[nodiscard]] bool tallyOn() const { return m_tallyOn; }
    /// Where tally mark `index` goes, GUI, in a level numbered `levelNumber`: the gang-count pattern from the panel's
    /// base, below the money and the counters by tallyShift().
    /// @orig 0x00213e68 PlayerHUD_LayoutTallyMarks (unknown)
    [[nodiscard]] GuiPoint tallyMarkPlace(std::size_t index, int levelNumber) const;
    /// The tally's drop: none unless the money is 1-999 or a counter shows; then 0.09 when slot 2 or 3 holds an item
    /// on line 2, else 0.045; less 0.03 in a level numbered 100 or more.
    [[nodiscard]] float tallyShift(int levelNumber) const;
    /// The values of the last update.
    [[nodiscard]] const PanelValues& values() const { return m_values; }
    /// The game time of the last activity.
    [[nodiscard]] std::uint64_t activityMs() const { return m_activityMs; }

    /// Adds the panel's sprites for the newest step: nothing when not shown or faded out. `levelNumber` decides the
    /// score (drawn below level 100) and, for player 1, the swapped rage colours.
    /// @orig 0x00213290 PlayerHUD_Render (unknown)
    void render(const HudCanvas& canvas, int levelNumber) const;

  private:
    // The panel's base plus player 0's `offset`, moved right by kPlayer1Shift for player 1: the parts whose player-1
    // entry the page does not give.
    [[nodiscard]] GuiPoint shifted(GuiPoint offset) const;
    // The parts render() draws.
    void renderBanner(const HudCanvas& canvas, float alpha, bool swapped) const;
    void renderMeter(const HudCanvas& canvas, float alpha, bool swapped) const;
    void renderScore(const HudCanvas& canvas, float alpha) const;
    void renderMoney(const HudCanvas& canvas, float alpha) const;
    void renderCounters(const HudCanvas& canvas, float alpha) const;
    void renderTally(const HudCanvas& canvas, float alpha, bool swapped, int levelNumber) const;

    std::size_t m_player;
    bool m_attached = false;
    bool m_shown = false;
    bool m_mayShow = true;
    bool m_forceShow = false;
    std::uint32_t m_banner = 0x31;
    std::uint32_t m_flashFrames = 0;
    std::uint32_t m_flashCounter = 0; // `+0x4118`
    std::uint64_t m_activityMs = 0;
    std::uint64_t m_nowMs = 0;
    bool m_wasFull = false;
    PanelValues m_values;
    CountingNumber m_score;
    CountingNumber m_money;
    std::array<int, 4> m_items{};
    std::array<GuiPoint, 4> m_slots{};
    bool m_tallyOn = false;         // +0x4130
    std::uint32_t m_tallyCount = 0; // +0x413c
};

} // namespace coney::hud
