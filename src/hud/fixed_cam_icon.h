// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/pad.h"
#include "hud/hud_canvas.h"
#include "hud/hud_layout.h"

namespace coney::hud {

/// The fixed-camera icon's `part_page0` rectangle (a crossed-out film camera), its size (an overlay height), its
/// colour, its fade and its place for one player (GUI, the default video mode).
inline constexpr std::size_t kFixedCamRect = 87;
inline constexpr float kFixedCamSize = 0.09F;
inline constexpr graphics::Rgba kFixedCamColour{191, 191, 191, 255};
inline constexpr std::uint64_t kFixedCamFadeMs = 1000;
inline constexpr GuiPoint kFixedCamPlace{0.9F, 0.64F};
/// A raw stick byte outside this range counts as pushed (`HUD_IsPlayerStickPushed`).
inline constexpr std::uint8_t kStickPushLow = 64;
inline constexpr std::uint8_t kStickPushHigh = 176;

/// One player's fixed-camera icon (HUD `+0x135e0` + player × `0x100`): the "this camera can't be turned" hint. While
/// the player's camera ignores the right stick (a fixed, locked, transition or rail camera, or camera switch 0 off),
/// pushing the right stick shows it fully opaque; after he lets go it fades out over 1 s. Any other camera hides it at
/// once. `HUDEnableFixedCamIcon(false)` turns it off for the level.
///
/// Research: docs/research/hud.md#hud-fixed-cam-icon
class FixedCamIcon {
  public:
    /// Whether the right stick of `pad` is pushed: either of its raw bytes outside 64-176.
    /// @orig 0x001aef50 HUD_IsPlayerStickPushed (unknown)
    [[nodiscard]] static bool stickPushed(const Pad& pad);

    /// One HUD update at game time `nowMs`: `ignoresStick` whether the player's camera ignores the right stick, `pad`
    /// his pad (null: not pushed).
    void update(bool ignoresStick, const Pad* pad, std::uint64_t nowMs);
    /// `HUDEnableFixedCamIcon`: the widget's active flag; the level's set-up turns it back on.
    void setEnabled(bool on) { m_enabled = on; }
    [[nodiscard]] bool enabled() const { return m_enabled; }
    [[nodiscard]] bool shown() const { return m_shown; }
    /// The icon's alpha factor at `nowMs`: 1 while the stick is held, then falling to 0 over the fade.
    [[nodiscard]] float alpha(std::uint64_t nowMs) const;

    /// Adds the icon to `canvas.parts` while it is shown and enabled.
    void render(const HudCanvas& canvas, std::uint64_t nowMs) const;

  private:
    bool m_enabled = true;
    bool m_shown = false;
    std::uint64_t m_fadeEndMs = 0; // 0: no fade running
};

} // namespace coney::hud
