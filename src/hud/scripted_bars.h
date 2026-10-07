// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "graphics/render_device.h"
#include "hud/hud_canvas.h"

namespace coney::hud {

/// The generic bars' slots (HUD `+0x9970`, `0x3f0` bytes each).
inline constexpr std::size_t kGenericBars = 4;

/// The kinds of bar `HUDEnableBar` makes.
enum class BarKind : std::uint8_t {
    Red = 0,      ///< A red bar (slot 0).
    Labelled = 1, ///< A labelled bar whose fill runs from red to green (slot 0): a boss's or an object's health.
    Gauge = 2,    ///< The chase gauge, a marker between two values.
    Stack = 3,    ///< Up to four labelled bars, one per slot.
};

/// One generic bar: a meter of the rage meter's look with a label above it, laid out in the HUD's right column.
struct GenericBar {
    bool exists = false;
    bool shown = true;                         ///< Kind 3's per-bar show / hide.
    std::string label;                         ///< Markup; empty for none.
    float fill = 1.0F;                         ///< 0-1; a new bar starts full.
    bool gradient = true;                      ///< The fill colour follows the fill, red to green (HudBar `+0x60`).
    graphics::Rgba colour{255, 255, 255, 255}; ///< The fill colour while `gradient` is off (HudBar `+0x28`).
    float width = 0.22F;                       ///< GUI units.
    float height = 0.025F;                     ///< GUI units.
};

/// The chase gauge (HUD `+0xa940`): a track, an end icon and a marker placed by where `value` lies between `min` and
/// `max`.
struct ChaseGauge {
    bool exists = false;
    std::uint32_t endIcon = 0x1a0004; ///< `texA`: the icon at the track's right end.
    std::uint32_t track = 0x1a0000;   ///< `texB`: the track.
    float value = 30.0F;              ///< `+0x340`.
    float min = 0.0F;                 ///< `+0x344`.
    float max = 60.0F;                ///< `+0x348`.
};

/// The bars the scripts make with `HUDEnableBar`, fill with `HUDSetBarPercentage` and recolour with
/// `HUDSetBarProperty`: the four generic bars (kinds 0, 1 and 3) and the chase gauge (kind 2).
///
/// **Coney's readings** where the pages are silent: kind 0's bar is drawn with the generic bars' meter rectangles (its
/// own sprite word `0x020a0000` is not loaded), and kind 3's bars take kind 1's size; one batch draws the gauge's three
/// sprites in order (track, marker, icon) in place of their three depths.
///
/// Research: docs/research/hud.md#scripted-bars, docs/references/bindings/hud.md#hudenablebar
class ScriptedBars {
  public:
    /// The marker's sprite word (`chasebar` rectangle 7).
    static constexpr std::uint32_t kGaugeMarker = 0x1a0007;

    /// `HUDEnableBar(kind, on, labels, count, flag, texA, texB)`.
    /// @orig 0x001b53b0 HUD_EnableBar (unknown)
    /// @orig 0x001b4ba0 HUD_EnableRedBar (unknown)
    /// @orig 0x001b4c98 HUD_EnableLabelledBar (unknown)
    /// @orig 0x001b4f00 HUD_EnableGaugeBar (unknown)
    /// @orig 0x001b5008 HUD_EnableBarStack (unknown)
    void enable(int kind, bool on, std::span<const std::string> labels, std::uint32_t count, bool flag,
                std::uint32_t texA, std::uint32_t texB);
    /// `HUDSetBarPercentage(kind, fill, fill2, index, value2)`: kinds 0, 1 and 3 fill bar `index` (kind 0 slot 0)
    /// with `fill` clamped to 0-1; kinds 1 and 2 set the gauge's value, max and min to `fill`, `fill2` and `value2`.
    /// @orig 0x001b5450 HUD_SetBarPercentage (unknown)
    /// @orig 0x001c2530 HudGenericBar_SetFill (unknown)
    void setPercentage(int kind, float fill, float fill2, std::uint32_t index, float value2);
    /// `HUDSetBarProperty(index, gradient, rgba, width)`: bar `index`'s fill colour, gradient switch and width.
    /// @orig 0x001b54f0 HUD_SetBarProperty (unknown)
    /// @orig 0x001c22b0 HudGenericBar_SetWidth (unknown)
    void setProperty(std::uint32_t index, bool gradient, graphics::Rgba colour, float width);
    /// Every bar removed: a new level.
    void clear();

    /// Adds the visible bars' sprites: the generic bars in the right column moved down by `columnShift` (GUI units:
    /// the counter panels' and the stopwatch's height), then the gauge.
    /// @orig 0x001a3c00 ChaseGauge_Render (unknown)
    void render(const HudCanvas& canvas, float columnShift) const;

    /// The generic bar in slot `slot`.
    [[nodiscard]] const GenericBar& bar(std::size_t slot) const { return m_bars.at(slot); }
    /// The chase gauge.
    [[nodiscard]] const ChaseGauge& gauge() const { return m_gauge; }
    /// Where the gauge's marker lies along its track: 0 at the left end (`value` = `max`), 1 at the right (`min`).
    [[nodiscard]] float gaugePosition() const;
    /// The colour a bar with the gradient on draws its fill at `fill`: red (255, 16, 16) × (1 − fill) + green
    /// (115, 183, 11) × fill.
    [[nodiscard]] static graphics::Rgba gradientColour(float fill);

  private:
    // Draws the generic bar `bar` with its right end at GUI x `right`, centred on `y`, its label above.
    static void renderBar(const HudCanvas& canvas, const GenericBar& bar, float right, float y);
    // Draws the chase gauge.
    // @orig 0x001a3990 ChaseGauge_Update (unknown)
    void renderGauge(const HudCanvas& canvas) const;

    std::array<GenericBar, kGenericBars> m_bars{};
    ChaseGauge m_gauge;
};

} // namespace coney::hud
