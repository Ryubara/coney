// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"
#include "gui/bar.h"
#include "gui/base_widget.h"
#include "gui/global_strings.h"
#include "gui/menu_input.h"
#include "gui/text_widget.h"
#include "gui/widget.h"

namespace coney::gui {

/// The Stats screen's bar labels (docs/research/pause.md#stats-screen).
namespace stats_strings {
inline constexpr std::uint32_t kStrength = 0xf3;
inline constexpr std::uint32_t kStamina = 0xf4;
inline constexpr std::uint32_t kHealth = 0xf5;
inline constexpr std::uint32_t kRage = 0xf6;
} // namespace stats_strings

/// What the Stats screen reads of the pausing player's human: his character type and the maxima the game computes for
/// him (`Human_GetMaxPower`, `Human_StaminaMax`, `Human_GetRageMax`), his Warrior class's `damage_scale` (the
/// `CfgWarriorClass` record's byte `+0x06`) and his power class's byte `+0x40`.
struct WarriorStats {
    int characterType = 0;
    int damageScale = 0;
    int powerMax = 0;
    int staminaMax = 0;
    int rageMax = 0;
    int powerClassByte40 = 0;
};

/// The pictures and text of one of the nine Warriors' Stats screens: the kind `Human_GetPortraitKind` (`0x00229570`)
/// gives his character type.
///
/// The sprite at `+0x7b0` is the Warrior's name (red, with two black shadows) and the one at `+0xab0` his face
/// (confirmed at runtime on the page): nameRecord is `+0x7b0`'s, faceRecord `+0xab0`'s.
struct StatsPortrait {
    int kind = 0;                  ///< 0-8: Swan, Cochise, Cowboy, Ajax, Rembrandt, Fox, Vermin, Snow, Cleon.
    std::uint32_t nameRecord = 0;  ///< `+0x7b0`'s sheet-table record (sprite word record << 16, rectangle 0).
    std::uint32_t faceRecord = 0;  ///< `+0xab0`'s.
    std::uint32_t description = 0; ///< The description's global string.
};

/// The pause menu's Stats screen (`PauseStats`, a `GameMenu` at pause menu `+0x3bb0`): the pausing player's Warrior,
/// with his name, his face, four bars (Strength, Stamina, Health, Rage) and a description.
///
/// - **Availability** (setup()): only the nine Warriors' character types (portraitOf()) have a screen; for any other
///   (Ash, Lynx, anyone) it is unavailable and the pause menu's Stats item opens nothing.
/// - **Layout** (default mode, base B = (0.5, 0.5); 16:9 moves B to y 0.45): the name (`+0x7b0`) with its left edge
///   at (0.42, 0.3), 0.09 high, tinted (150, 30, 30), with two black shadows at alpha 128 offset (+0.0025, +0.004)
///   and (−0.001, −0.002); the face (`+0xab0`) centred at (0.33, 0.401), 0.3 high; four bars 0.22 × 0.025 with their
///   left edge at x 0.64 and y 0.39 + 0.05 i, fill (150, 30, 30) over (37, 37, 37), drawn as the rage meter from
///   `part_page0` rectangles 54 (body), 55 (fill), 56 and 57 (end caps); their labels left-aligned at their anchor
///   (0.38, 0.42 + 0.05 i) plus the bars' label offset (0.04, -0.035), so at (0.42, 0.385 + 0.05 i); the description
///   at (0.26, 0.6), size 1.0, grey `0x005fd310`, font slot 3, wrapping at 0.55, showing the lines 0-5 of a window.
/// - **Values** (barValues()): from the difficulty's row of the table at `0x0050f230`.
/// - **Input** (handle()): up and down move the description's line window by one line (cue 4; `0xe` when it cannot
///   move: up at line 0, down once the text's last line is the window's bottom line), left and right play `0xe`, back
///   plays `0xf` and closes the screen.
///
/// Coney's choices: the screen is placed at once (Coney loads its pictures when the pause starts, so there is nothing
/// to wait for, where the original waits up to 10 s); the name's and shadows' depths (15,000 and 11,000) are kept by
/// drawing the shadows first in the name's batch; the down move's limit is the line count the last render() or
/// measure() found (the original's draw counts the lines too); the `0x08` and `0x02` layouts are not modelled.
///
/// Research: docs/research/pause.md#stats-screen
/// @orig 0x001e5498 PauseStats_Construct (unknown)
class PauseStats {
  public:
    /// The base point B (`0x0050f110`, `0x0050f118`) and its 16:9 y.
    static constexpr float kBaseX = 0.5F;
    static constexpr float kBaseY = 0.5F;
    static constexpr float kWideBaseY = 0.45F;
    /// The name (`+0x7b0`): offset from B, height, tint (`0x0063ee30`), depth.
    static constexpr float kPortraitDx = -0.08F;
    static constexpr float kPortraitDy = -0.2F;
    static constexpr float kPortraitSize = 0.09F;
    static constexpr graphics::Rgba kPortraitTint{150, 30, 30, 255};
    static constexpr float kPortraitDepth = 15000.0F;
    /// The two shadows' offsets from the name (`0x0050f2a8`-`0x0050f2b4`) and alpha.
    static constexpr std::array<std::array<float, 2>, 2> kShadowOffsets{{{0.0025F, 0.004F}, {-0.001F, -0.002F}}};
    static constexpr std::uint8_t kShadowAlpha = 128;
    /// The face (`+0xab0`): offset from B, height, depth.
    static constexpr float kBannerDx = -0.17F;
    static constexpr float kBannerDy = -0.099F;
    static constexpr float kBannerSize = 0.3F;
    static constexpr float kBannerDepth = 11000.0F;
    /// The bars: offset from B, step, size, colours, and their `part_page0` rectangles (body, fill, end caps).
    static constexpr float kBarDx = 0.14F;
    static constexpr float kBarDy = -0.11F;
    static constexpr float kBarStep = 0.05F;
    static constexpr float kBarWidth = 0.22F;
    static constexpr float kBarHeight = 0.025F;
    static constexpr graphics::Rgba kBarFill{150, 30, 30, 255};
    static constexpr graphics::Rgba kBarBack{37, 37, 37, 255};
    static constexpr std::size_t kBarRect = 54;
    static constexpr std::size_t kBarFillRect = 55;
    static constexpr std::size_t kBarLeftCapRect = 56;
    static constexpr std::size_t kBarRightCapRect = 57;
    /// The labels' anchor x (`0x0050f2b8`) and first y from B (`0x0050f178`), and the bars' label offset
    /// (`0x0050e960` / `0x0050e964`) added to it.
    static constexpr float kLabelX = 0.38F;
    static constexpr float kLabelDy = -0.08F;
    static constexpr float kLabelOffsetX = 0.04F;
    static constexpr float kLabelOffsetY = -0.035F;
    /// The description: offset from B, wrap width (`MessageHUD_ReadAutoIndent`'s 0.55), size and font slot.
    static constexpr float kDescriptionDx = -0.24F;
    static constexpr float kDescriptionDy = 0.1F;
    static constexpr float kDescriptionWrap = 0.55F;
    /// The description's first line window: lines 0 to 5, both shown (`+0x1a4`, `+0x1a8`).
    static constexpr std::size_t kDescriptionLastLine = 5;
    static constexpr int kTextFontSlot = 3;
    /// Cues.
    static constexpr int kMoveCue = 4;
    static constexpr int kRefusedCue = 0xe;
    static constexpr int kBackCue = 0xf;
    /// The bars.
    static constexpr std::size_t kBars = 4;

    PauseStats();

    /// The Stats screen of character type `type`: one of the nine Warriors' (the kinds 0-8 of `Human_GetPortraitKind`),
    /// nothing for any other type.
    [[nodiscard]] static std::optional<StatsPortrait> portraitOf(int type);

    /// The four bars' fills, Strength, Stamina, Health and Rage, for `stats` at difficulty `difficulty` (the game
    /// state's `+0x154`, clamped to 0-2 to pick the table's row t): Strength (damage_scale − t0) / (t1 − t0), not
    /// clamped; Stamina (power max + stamina max − t2 + t4) / (t3 + t5 − t2 + t4), at most 1; Health the power class's
    /// byte `+0x40` / 100, at most 1; Rage the rage maximum / 150, at most 1.
    [[nodiscard]] static std::array<float, kBars> barValues(const WarriorStats& stats, int difficulty);

    /// Draws the name (and its shadows), the face and the bars into these batches (each over its sheet: the name's and
    /// face's records, `part_page0`; not owned, null: none).
    void setBatches(graphics::SpriteBatch* name, graphics::SpriteBatch* face, graphics::SpriteBatch* bars);

    /// Sets the screen up for the pausing player, `stats` (nothing: no human), at `difficulty`, with `strings` (which
    /// must outlive the screen; null: empty texts), in 16:9 when `widescreen`. Returns whether the screen is available.
    /// @orig 0x001e5618 PauseStats_Setup (unknown)
    bool setup(const std::optional<WarriorStats>& stats, int difficulty, const GlobalStrings* strings, bool widescreen);
    /// Whether the last setup() found a Warrior.
    [[nodiscard]] bool available() const { return m_portrait.has_value(); }
    /// The Warrior found.
    [[nodiscard]] const std::optional<StatsPortrait>& portrait() const { return m_portrait; }

    /// Sends cues to `playCue` (empty: none).
    void setSoundSink(std::function<void(int cue)> playCue) { m_playCue = std::move(playCue); }
    /// Acts on one command; returns true when back closes the screen.
    /// @orig 0x001e6698 PauseStats_OnCommand (unknown)
    [[nodiscard]] bool handle(MenuCommand command);
    /// Places every part (the layout above) and advances the texts' time.
    /// @orig 0x001e6778 PauseStats_Update (unknown)
    void update(const GuiFrame& frame);
    /// Adds the shadows and the name, the face, the bars, their labels and the description.
    /// @orig 0x001e6cf8 PauseStats_Render (unknown)
    void render(const GuiCanvas& canvas) const;
    /// The menu's alpha, 0 to 1, on every part.
    /// @orig 0x001e6cf0 PauseStats_SetAlpha (unknown)
    void setFade(float fade);

    /// The bars' fills (as computed, before a bar clamps them to 0-1).
    [[nodiscard]] const std::array<float, kBars>& values() const { return m_values; }
    /// The parts, as placed by the last update().
    [[nodiscard]] const BaseWidget& name() const { return m_name; }
    [[nodiscard]] const BaseWidget& shadow(std::size_t index) const { return m_shadows.at(index); }
    [[nodiscard]] const BaseWidget& face() const { return m_face; }
    [[nodiscard]] const Bar& bar(std::size_t index) const { return m_bars.at(index); }
    [[nodiscard]] const TextWidget& label(std::size_t index) const { return m_labels.at(index); }
    [[nodiscard]] const TextWidget& description() const { return m_description; }
    /// The description's line window: its first and last lines shown.
    [[nodiscard]] std::size_t firstLine() const { return m_description.style().firstLine; }
    [[nodiscard]] std::size_t lastLine() const { return m_description.style().lastLine; }
    /// Counts the description's lines with `canvas`'s fonts, as render() does (the down move's limit).
    void measure(const GuiCanvas& canvas) const;
    /// The texts shown, in drawing order (for tests and the log): the four labels and the description.
    [[nodiscard]] std::vector<std::string> lines() const;

  private:
    // Plays `cue`, if there is a sink.
    void playCue(int cue) const;

    std::function<void(int cue)> m_playCue;
    std::optional<StatsPortrait> m_portrait;
    std::array<float, kBars> m_values{};
    float m_baseY = kBaseY;
    float m_fade = 1.0F;
    std::array<BaseWidget, 2> m_shadows{{{nullptr, 0}, {nullptr, 0}}};
    BaseWidget m_name{nullptr, 0};
    BaseWidget m_face{nullptr, 0};
    std::array<Bar, kBars> m_bars{{{nullptr, kBarRect}, {nullptr, kBarRect}, {nullptr, kBarRect}, {nullptr, kBarRect}}};
    std::array<TextWidget, kBars> m_labels;
    TextWidget m_description;
    mutable std::size_t m_descriptionLines = 0; // the description's line count when last measured (`+0x1ac` + 1)
};

} // namespace coney::gui
