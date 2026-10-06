// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "hud/hud_audio.h"
#include "hud/hud_canvas.h"
#include "hud/hud_layout.h"

namespace coney::hud {

/// A counter panel's layout (`HUDGetNewPH`'s kind).
enum class CounterKind : std::uint8_t {
    Suffixed = 0,  ///< A value with the label after it.
    Single = 1,    ///< The label and a value.
    GotNeeded = 2, ///< `label got/needed`: the objective counters (`Tags 2/6`).
    Bar = 3,       ///< The label over a bar filled to the value (0-1).
};

/// One of the five counter panels.
struct CounterPanel {
    bool used = false;
    CounterKind kind = CounterKind::GotNeeded;
    std::string label;
    float barWidth = kCounterPanelBarWidth;
    float value = 0.0F;  ///< Field 1.
    float target = 0.0F; ///< Field 2, a `got / needed` panel's needed.
    bool visible = false;
    std::optional<std::uint64_t> hideAtMs; ///< When it hides; none while it stays up.
};

/// The HUD's five counter panels (`HUDGetNewPH`, HUD `+0xacb0`): taken and released by scripts, shown for a time after
/// each change, and drawn in rows at the right from the top, the visible ones only.
///
/// Coney's choices where the page is silent: a panel's text is right-aligned on x 0.96 and centred on its row's y;
/// the texts are `value label` (kind 0), `label value` (kind 1) and `label got/needed` (kind 2) with whole numbers; a
/// bar (kind 3) is a grey bar under its label, filled in white.
///
/// Research: docs/research/hud.md#announcements-and-other-messages, docs/references/bindings/hud.md#hudgetnewph
class CounterPanels {
  public:
    /// `HUDGetNewPH(kind, label, width)`: the first free panel's index, or -1 when all five are taken. It stays
    /// hidden until a value is set.
    /// @orig 0x001b4790 HUD_PanelAlloc (unknown)
    int take(int kind, std::string label, float barWidth);
    /// `HUDReleasePH(panel)`: frees and hides it. An index out of range does nothing.
    /// @orig 0x001b47b8 HUD_PanelRelease (unknown)
    void release(int panel);
    /// `HUDSetPHValue(panel, field, value, show, ms, sound)` at game time `nowMs`: field 1 sets the value and shows the
    /// panel for `ms` (0 keeps it up), field 2 sets the target only; `sound` plays kCounterPanelSound.
    /// @orig 0x001b47e0 HUD_PanelSetValue (unknown)
    void setValue(int panel, int field, float value, std::uint32_t ms, bool sound, std::uint64_t nowMs,
                  const HudSound& audio);
    /// One frame: hides the panels whose time is over.
    void update(std::uint64_t nowMs);
    /// The panels.
    [[nodiscard]] const std::array<CounterPanel, kCounterPanels>& panels() const { return m_panels; }
    /// The text a panel shows.
    [[nodiscard]] static std::string textOf(const CounterPanel& panel);
    /// Draws the visible panels in rows from the top.
    void render(const HudCanvas& canvas) const;

  private:
    std::array<CounterPanel, kCounterPanels> m_panels;
};

} // namespace coney::hud
