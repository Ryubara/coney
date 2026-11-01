// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hud/hud.h"

namespace coney::hud {

/// Which HUD a play mode steps: its own, or, after useHud(), the game's, which the scripts' bindings act on
/// (docs/research/hud.md). The simulation owns it, so a build without a renderer steps the same HUD; the platform's
/// HUD layer (platform::HudLayer) only draws the HUD hud() gives. Not copyable or movable: hud() may point into it.
///
/// Research: docs/guides/network-architecture.md#sub-hud
class HudOwner {
  public:
    HudOwner() = default;
    ~HudOwner() = default;
    HudOwner(const HudOwner&) = delete;
    HudOwner& operator=(const HudOwner&) = delete;
    HudOwner(HudOwner&&) = delete;
    HudOwner& operator=(HudOwner&&) = delete;

    /// The HUD stepped and drawn: the owner's own until useHud().
    [[nodiscard]] Hud& hud() { return *m_hud; }
    /// The HUD stepped and drawn, to read.
    [[nodiscard]] const Hud& hud() const { return *m_hud; }
    /// Steps and draws `shared` (which must outlive the owner's use of it) instead of the owner's own HUD.
    void useHud(Hud& shared) { m_hud = &shared; }

  private:
    Hud m_own;
    Hud* m_hud = &m_own;
};

} // namespace coney::hud
