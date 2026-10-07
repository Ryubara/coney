// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "hud/hud_canvas.h"
#include "hud/hud_layout.h"

namespace coney::hud {

/// The mug meter's rectangles: the bars are `part_page0`'s meter (54-57, sprite word `0x30036`, as the rage meter's),
/// the stick's base and dot and the three arcs `hud_minigames` 6, 5 and 7-9.
inline constexpr std::size_t kMugStickBaseRect = 6;
inline constexpr std::size_t kMugStickDotRect = 5;
inline constexpr std::size_t kMugFirstArcRect = 7;
/// The `GSTRING.HUD` prompts: mugging (modes 0 and 2) and being mugged (1 and 3).
inline constexpr std::uint32_t kMugPromptString = 0x180;
inline constexpr std::uint32_t kMuggedPromptString = 0x181;

/// The mug meter's modes (`MugMeter_SetMode`): mugging, being mugged by the other player, and the two sides of a hold
/// (blue instead of red).
enum class MugMeterMode : std::uint8_t { Mugging = 0, Mugged = 1, Holding = 2, Held = 3 };

/// One player's mugging mini-game panel (player panel `+0x3480`): two bars (the time used and the time on target), the
/// left stick's picture (a dot moving on a ball) and three arcs rippling out on the side the stick points to, with a
/// prompt above. The game drives it each update while the player mugs; it is drawn only then.
///
/// Research: docs/research/hud.md#mug-meter-layout
/// @orig 0x001bf7e0 MugMeter_Setup (MissionSelectHUD.cpp)
class MugMeter {
  public:
    /// Shows the meter (`HUD_MugMeterStart`) or hides it; a shown meter starts with both bars empty.
    void setActive(bool on);
    [[nodiscard]] bool active() const { return m_active; }
    /// The mode: the colours and when the arcs show.
    /// @orig 0x001c0648 MugMeter_SetMode (MissionSelectHUD.cpp)
    void setMode(MugMeterMode mode) { m_mode = mode; }
    [[nodiscard]] MugMeterMode mode() const { return m_mode; }
    /// Both bars' fills, each clamped to 0-1: the time used and the time on target.
    /// @orig 0x001bfcc0 MugMeter_SetFills (MissionSelectHUD.cpp)
    void setFills(float used, float onTarget);
    [[nodiscard]] float usedFill() const { return m_used; }
    [[nodiscard]] float onTargetFill() const { return m_onTarget; }
    /// This update's raw left stick (x right, y up, -1 to 1): the dot's place and, past 0.5 on an axis, the arcs'
    /// direction (the four straight ones need the other axis within 0.1); the last direction picked stays.
    /// @orig 0x001bfd30 MugMeter_SetStick (MissionSelectHUD.cpp)
    void setStick(float x, float y);
    /// `HUDMugMeterSet`: whether the stick is on target, which shows the arcs in modes 0 and 2 (hides them in 1 and 3).
    /// @orig 0x001c0640 MugMeter_SetArrowsOn (MissionSelectHUD.cpp)
    void setOnTarget(bool on) { m_stickOnTarget = on; }

    /// The direction picked (0-7: up, down, left, right, up-left, up-right, down-right, down-left), if any yet.
    [[nodiscard]] std::optional<std::size_t> direction() const { return m_direction; }
    /// Whether the arcs show this update (update() decides).
    [[nodiscard]] bool arcsShown() const { return m_arcsShown; }
    /// The arcs' ripple phase: 0 the first lit, 1 the first at half with the second, 2 the second at half with the
    /// third, 3 none.
    [[nodiscard]] std::size_t ripplePhase() const { return (m_ripple / kRippleFrames) % kRipplePhases; }

    /// One HUD update: whether the arcs show (a stick update since the last one, and the mode's on-target rule), their
    /// ripple advanced while they do, and the stick's mark taken.
    /// @orig 0x001c0bb0 MugMeter_AnimateArrows (MissionSelectHUD.cpp)
    void update();
    /// Adds player `player`'s meter, with `prompt` (the mode's `GSTRING.HUD` text) above it: the bars, the stick, the
    /// arcs while they show, the prompt. Nothing while it is not active.
    /// @orig 0x001c09f8 MugMeter_Render (MissionSelectHUD.cpp)
    void render(const HudCanvas& canvas, std::size_t player, const std::string& prompt) const;

  private:
    static constexpr std::uint32_t kRippleFrames = 2; // +0x848
    static constexpr std::uint32_t kRipplePhases = 4;

    // One bar from its left end: back, fill, caps of `part_page0`.
    static void renderBar(graphics::SpriteBatch* sheet, GuiPoint left, GuiSize size, float fill, graphics::Rgba colour);

    bool m_active = false;
    MugMeterMode m_mode = MugMeterMode::Mugging;
    float m_used = 0.0F;
    float m_onTarget = 0.0F;
    float m_stickX = 0.0F;
    float m_stickY = 0.0F;
    std::optional<std::size_t> m_direction;
    bool m_stickOnTarget = false; // +0x84c
    bool m_stickSet = false;      // +0x850: set by each stick update, cleared by each render
    std::uint32_t m_ripple = 0;
    bool m_arcsShown = false;
};

} // namespace coney::hud
