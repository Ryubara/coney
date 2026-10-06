// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

namespace coney::hud {

/// A number the HUD shows counting toward its value, as the score (`0x001c7928`) and the money (`0x001becb0`) do: each
/// frame the shown number moves by (difference / 16) ± 1 toward the value, so it closes a large gap fast and a small
/// one a unit a frame. A change of the value starts a popup of the difference, kept with the game time it started.
///
/// Research: docs/research/hud.md#score-and-money
class CountingNumber {
  public:
    /// The popup a change makes: the difference and when it was made.
    struct Popup {
        int delta = 0;
        std::uint64_t startMs = 0;
    };

    /// One frame at game time `nowMs` with the value now `value`. Returns true when the value changed this frame (the
    /// panel's activity). The first frame takes the value as it is, without counting or a popup.
    bool update(int value, std::uint64_t nowMs);

    /// The number shown now.
    [[nodiscard]] int shown() const { return m_shown; }
    /// The value counted toward.
    [[nodiscard]] int value() const { return m_value; }
    /// Whether the shown number is still moving.
    [[nodiscard]] bool counting() const { return m_shown != m_value; }
    /// The latest change's popup, if one was made.
    [[nodiscard]] const std::optional<Popup>& popup() const { return m_popup; }
    /// Forgets the popup (its time is over).
    void clearPopup() { m_popup.reset(); }

  private:
    int m_shown = 0;
    int m_value = 0;
    bool m_started = false;
    std::optional<Popup> m_popup;
};

/// One frame's step of a shown number toward `target`: (target − shown) / 16, truncated, plus one unit toward it,
/// never past the target.
[[nodiscard]] int countStep(int shown, int target);

} // namespace coney::hud
