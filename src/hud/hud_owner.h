// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hud/hud.h"

namespace coney::hud {

/// Stub: platform::HudLayer's switch between its own HUD and the game's (useHud()), to move to the core (#1273).
class HudOwner {
  public:
    HudOwner() = default;
    ~HudOwner() = default;
    HudOwner(const HudOwner&) = delete;
    HudOwner& operator=(const HudOwner&) = delete;
    HudOwner(HudOwner&&) = delete;
    HudOwner& operator=(HudOwner&&) = delete;

    /// The HUD stepped: the owner's own until useHud().
    [[nodiscard]] Hud& hud() { return *m_hud; }
    /// The HUD stepped, to read.
    [[nodiscard]] const Hud& hud() const { return *m_hud; }
    /// Steps `shared` instead of the owner's own HUD.
    void useHud(Hud& shared) { m_hud = &shared; }

  private:
    Hud m_own;
    Hud* m_hud = &m_own;
};

} // namespace coney::hud
