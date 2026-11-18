// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "console/command_picker.h"
#include "console/console.h"
#include "core/pad.h"
#include "core/pads.h"

// @coney-only the Coney console's pad gate (docs/guides/studio/console.md#input); the original has no console.

namespace coney::console {

/// Port 1's buttons that open and close the console from a pad, held together: SELECT and R3 (a gamepad's back button
/// and the right stick pressed in). No control of the original holds them together
/// (docs/guides/studio/console.md#input).
inline constexpr std::uint16_t kConsoleChord = pad::kSelect | pad::kR3;

/// What opened the console last, so the front end knows where the focus goes: the line for the keyboard, the picker
/// for the pad.
enum class OpenedBy : std::uint8_t {
    Keyboard, ///< F8, or a test's `--console-open`.
    Pad,      ///< SELECT and R3.
};

/// The console's own small input gate, between the pads and the game (the debug tools' gate is not in the release
/// build): it holds whether the console is open, and
///
/// - **the chord** (kConsoleChord) opens and closes it on the frame both buttons are first held together; while both
///   are held the game never sees them, so the chord does nothing in play;
/// - **while it is open** the game sees port 1 connected with nothing pressed and the sticks centred, and port 1's
///   presses drive the command picker: the d-pad or the left stick moves, cross chooses, circle goes back;
/// - **after it closes**, the buttons still held stay hidden from the game until they are let go, so closing never
///   presses anything in play. Port 2 always passes through.
///
/// It counts frames only, never a clock. A test-mode run leaves it out of the input chain, so a scripted pad holding
/// SELECT and R3 reaches the game (docs/guides/studio/console.md#test-mode); a unit test drives it with an input
/// script directly.
class ConsolePad final : public InputSource {
  public:
    /// A gate over `inner` (null: no pads) whose picker runs lines on `console`; both must outlive it.
    ConsolePad(InputSource* inner, Console& console) : m_inner(inner), m_picker(console) {}

    /// The sample of frame `frame` with the console's share taken out, as described above.
    [[nodiscard]] PortSamples sample(std::uint64_t frame) override;

    /// Whether the console is open.
    [[nodiscard]] bool open() const { return m_open; }
    /// What opened it last.
    [[nodiscard]] OpenedBy openedBy() const { return m_openedBy; }
    /// Opens or closes it (F8, Escape, the bar's Close); opening from the pad puts the picker back at its first
    /// command.
    void setOpen(bool open, OpenedBy by = OpenedBy::Keyboard);

    /// The picker the pad drives.
    [[nodiscard]] CommandPicker& picker() { return m_picker; }
    [[nodiscard]] const CommandPicker& picker() const { return m_picker; }
    /// Port 1's buttons hidden from the game this frame.
    [[nodiscard]] std::uint16_t hiddenButtons() const { return m_hidden; }

  private:
    InputSource* m_inner;
    CommandPicker m_picker;
    bool m_open = false;
    OpenedBy m_openedBy = OpenedBy::Keyboard;
    bool m_chordHeld = false;       // both chord buttons were held last frame
    std::uint16_t m_hidden = 0;     // buttons hidden from the game until let go
    std::uint16_t m_held = 0;       // port 1's buttons as the device gave them last frame
    std::uint16_t m_navigation = 0; // the picker's buttons held last frame, the left stick read as the d-pad
};

} // namespace coney::console
