// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "core/pad.h"
#include "core/pads.h"
#include "debug/menu_navigator.h"
#include "debug/pad_menu_input.h"

namespace coney::debug {

/// Port 1's buttons that open and close the pad menu, held together: L3 and R3 (both sticks pressed in). On a
/// keyboard, F4 holds both (src/platform/sdl_input.cpp), and so do F and H together.
inline constexpr std::uint16_t kMenuChord = pad::kL3 | pad::kR3;

/// Sits between the input source and the game: each frame it takes port 1's sample, lets the pad menu read it, and
/// hands the game what the game may see.
///
/// - **The chord** (kMenuChord) opens and closes the menu on the frame both buttons are held together. While both are
///   held the game never sees them, so the chord does nothing in play. No retail control uses L3 and R3 together
///   (docs/research/debug.md#not-present), and the cheat codes are single buttons, so a chord breaks no code either.
/// - **While the menu is open** the game sees port 1 connected with nothing pressed and the sticks centred: menu input
///   never reaches gameplay. Port 2 always passes through.
/// - **After the menu closes**, the buttons still held (the circle that closed it) stay hidden from the game until
///   they are let go, so closing never presses anything in play.
/// - **Between steps**: after the menu's input, the gate runs the between-steps callbacks (the tunables' queued
///   changes, the channels' samples), so the game's next step sees one consistent set of values.
///
/// It reads and counts frames only, never a clock, so a scripted run (`--input-script`) drives the menu exactly as it
/// drives the game.
class InputGate final : public InputSource {
  public:
    /// A gate over `inner` (null: no pads), feeding `navigator`; both must outlive the gate.
    InputGate(InputSource* inner, MenuNavigator& navigator) : m_inner(inner), m_navigator(navigator) {}

    /// Adds a callback run once per frame after the menu's input, before the game's step.
    void addBetweenSteps(std::function<void()> callback) { m_betweenSteps.push_back(std::move(callback)); }

    /// The sample of frame `frame` with the menu's share taken out, as described above.
    [[nodiscard]] PortSamples sample(std::uint64_t frame) override;

    /// Port 1 as the menu sees it (everything the device gave), for the Input page.
    [[nodiscard]] const Pad& rawPad() const { return m_rawPad; }
    /// Port 1's last sample as the device gave it.
    [[nodiscard]] const PadSample& rawSample() const { return m_rawSample; }
    /// Buttons hidden from the game this frame.
    [[nodiscard]] std::uint16_t hiddenButtons() const { return m_hidden; }

  private:
    InputSource* m_inner;
    MenuNavigator& m_navigator;
    PadMenuInput m_menuInput;
    Pad m_rawPad;
    PadSample m_rawSample;
    bool m_chordHeld = false;   // both chord buttons were held last frame
    std::uint16_t m_hidden = 0; // buttons hidden from the game until released
    std::vector<std::function<void()>> m_betweenSteps;
};

} // namespace coney::debug
