// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace coney {

/// The bits of a pad's 16-bit button word, a set bit being a held button. The layout is the PS2 `libpad` one the
/// original stores (the inverted bytes 2 and 3 of the pad data), so the masks in the research pages work unchanged.
///
/// Research: docs/research/frontend.md#pad-record
namespace pad {
inline constexpr std::uint16_t kL2 = 0x0001;       ///< L2.
inline constexpr std::uint16_t kR2 = 0x0002;       ///< R2.
inline constexpr std::uint16_t kL1 = 0x0004;       ///< L1.
inline constexpr std::uint16_t kR1 = 0x0008;       ///< R1.
inline constexpr std::uint16_t kTriangle = 0x0010; ///< Triangle.
inline constexpr std::uint16_t kCircle = 0x0020;   ///< Circle.
inline constexpr std::uint16_t kCross = 0x0040;    ///< Cross.
inline constexpr std::uint16_t kSquare = 0x0080;   ///< Square.
inline constexpr std::uint16_t kSelect = 0x0100;   ///< SELECT.
inline constexpr std::uint16_t kL3 = 0x0200;       ///< L3 (left stick pressed in).
inline constexpr std::uint16_t kR3 = 0x0400;       ///< R3 (right stick pressed in).
inline constexpr std::uint16_t kStart = 0x0800;    ///< START.
inline constexpr std::uint16_t kUp = 0x1000;       ///< D-pad up.
inline constexpr std::uint16_t kRight = 0x2000;    ///< D-pad right.
inline constexpr std::uint16_t kDown = 0x4000;     ///< D-pad down.
inline constexpr std::uint16_t kLeft = 0x8000;     ///< D-pad left.
/// The four d-pad directions.
inline constexpr std::uint16_t kDpad = kUp | kRight | kDown | kLeft;

/// The raw stick byte at rest; inside the dead zone (kStickDeadLow to kStickDeadHigh), so it reads as 0.
inline constexpr std::uint8_t kStickCentre = 0x80;
/// The lowest raw stick byte that still reads as 0.
inline constexpr std::uint8_t kStickDeadLow = 95;
/// The highest raw stick byte that still reads as 0.
inline constexpr std::uint8_t kStickDeadHigh = 160;

/// The pressure bytes, in the original's order (`+0x34` of the pad record).
enum class Pressure : std::uint8_t { Right, Left, Up, Down, Triangle, Circle, Cross, Square, L1, R1, L2, R2 };
/// How many pressure bytes a sample has.
inline constexpr std::size_t kPressureCount = 12;

/// The button bit each pressure byte belongs to, indexed by Pressure.
inline constexpr std::array<std::uint16_t, kPressureCount> kPressureButtons{
    kRight, kLeft, kUp, kDown, kTriangle, kCircle, kCross, kSquare, kL1, kR1, kL2, kR2};

/// Turns a raw stick byte into a value in [-1, 1]: 0 inside the dead zone 95..160, `(r - 95) / 95` below it and
/// `(r - 160) / 95` above it, as the original's table at `0x0050b8d0` does. A y axis is negated by the caller.
[[nodiscard]] float stickValue(std::uint8_t raw);
} // namespace pad

/// One reading of a pad, as a device (or a script) delivers it once per step: the input of Pad::update().
///
/// The fields keep the PS2 pad's raw form, so the record above them can be the original's, and every input source
/// (an SDL gamepad, the keyboard, a test script) has to say exactly what a PS2 pad would have reported.
struct PadSample {
    /// The pad is plugged in. A disconnected sample's other fields are ignored.
    bool connected = false;
    /// Held buttons, pad::kL2 ... pad::kLeft.
    std::uint16_t buttons = 0;
    /// Raw stick bytes as `libpad` gives them: right x, right y, left x, left y (bytes 4-7); 0 is left or up, 255
    /// right or down, pad::kStickCentre at rest.
    std::array<std::uint8_t, 4> sticks{pad::kStickCentre, pad::kStickCentre, pad::kStickCentre, pad::kStickCentre};
    /// Pressure of the pressure-sensitive buttons, 0 (released) to 255, indexed by pad::Pressure.
    std::array<std::uint8_t, pad::kPressureCount> pressure{};
};

/// One pad record: the latest samples of one pad and the queries the game asks of them (pressed, released, held,
/// auto-repeat). The front-end menus read their input from records like this one.
///
/// Like the original's 0x50-byte record it keeps the last 8 button words in a ring, hold counters for the four d-pad
/// directions and the sticks in [-1, 1] with y up. What it leaves out, for now: the camera-turned left stick (in-game
/// only), the Lua pad handlers (no script system yet), vibration, and the player the pad belongs to.
///
/// Research: docs/research/frontend.md#pad-record, docs/research/frontend.md#pad-queries
class Pad {
  public:
    /// How many button words the ring keeps.
    static constexpr std::size_t kHistory = 8;
    /// A hold counter's value on the 15th held sample and every 4th after it: the auto-repeat beat.
    static constexpr std::uint8_t kRepeatCount = 15;

    /// The order of the hold counters (`+0x30`-`+0x33`).
    enum class Direction : std::uint8_t { Up, Right, Down, Left };

    /// Takes one sample: advances the ring, stores the button word, counts the d-pad holds, applies the diagonal rule
    /// and converts the sticks. A disconnected sample stores an empty word, clears the hold counters and centres the
    /// sticks (a Coney choice, see the research page).
    ///
    /// The original skips the device read and repeats the previous word when less than 6 ms of real time have passed
    /// since the last read; Coney updates once per fixed step, always more than 6 ms of game time, so every update
    /// reads.
    /// @orig 0x00144fb0 Pad_Update (unknown)
    void update(const PadSample& sample);

    /// The button word `back` samples ago (0 is the current one), `back` clamped to kHistory - 1.
    /// @orig 0x00144a08 Pad_ButtonsBack (unknown)
    [[nodiscard]] std::uint16_t buttons(std::size_t back = 0) const;

    /// Whether any button of `mask` is held now.
    /// @orig 0x00144b88 Pad_Held (unknown)
    [[nodiscard]] bool held(std::uint16_t mask) const { return (buttons() & mask) != 0; }

    /// The buttons that went down with this sample: held now, not held one sample ago.
    /// @orig 0x00144a80 Pad_Pressed (unknown)
    [[nodiscard]] std::uint16_t pressed() const;

    /// The buttons that came up with this sample: held one sample ago, not now.
    /// @orig 0x00144a30 Pad_Released (unknown)
    [[nodiscard]] std::uint16_t released() const;

    /// pressed(), plus each d-pad direction whose hold counter is at kRepeatCount: what menus use, so a held direction
    /// moves again on its 15th sample and every 4th after it.
    /// @orig 0x00144ad0 Pad_PressedRepeat (unknown)
    [[nodiscard]] std::uint16_t pressedWithRepeat() const;

    /// Whether any button of `mask` went down with this sample.
    /// @orig 0x00144bf0 Pad_PressedMask (unknown)
    [[nodiscard]] bool pressed(std::uint16_t mask) const { return (pressed() & mask) != 0; }

    /// Whether any button of `mask` came up with this sample.
    /// @orig 0x00144ba8 Pad_ReleasedMask (unknown)
    [[nodiscard]] bool released(std::uint16_t mask) const { return (released() & mask) != 0; }

    /// The hold counter of one d-pad direction: 0 while released, then 1, 2, ... 15, 12, 13, 14, 15, 12, ...
    [[nodiscard]] std::uint8_t holdCount(Direction direction) const {
        return m_holdCounts.at(static_cast<std::size_t>(direction));
    }

    /// Left stick x in [-1, 1], right positive.
    [[nodiscard]] float leftX() const { return m_leftX; }
    /// Left stick y in [-1, 1], up positive.
    [[nodiscard]] float leftY() const { return m_leftY; }
    /// Right stick x in [-1, 1], right positive.
    [[nodiscard]] float rightX() const { return m_rightX; }
    /// Right stick y in [-1, 1], up positive.
    [[nodiscard]] float rightY() const { return m_rightY; }
    /// The raw stick bytes of the last sample, in PadSample::sticks order.
    [[nodiscard]] const std::array<std::uint8_t, 4>& rawSticks() const { return m_rawSticks; }
    /// The pressure bytes of the last sample, indexed by pad::Pressure.
    [[nodiscard]] const std::array<std::uint8_t, pad::kPressureCount>& pressure() const { return m_pressure; }
    /// Whether the last sample came from a connected pad.
    [[nodiscard]] bool connected() const { return m_connected; }

  private:
    // Counts the d-pad holds for the word just sampled (step 3 of the original's update).
    void countHolds(std::uint16_t word);

    std::array<std::uint16_t, kHistory> m_ring{}; // the last kHistory button words; m_current indexes the newest
    std::size_t m_current = 0;
    std::array<std::uint8_t, 4> m_holdCounts{}; // indexed by Direction
    std::array<std::uint8_t, 4> m_rawSticks{pad::kStickCentre, pad::kStickCentre, pad::kStickCentre, pad::kStickCentre};
    std::array<std::uint8_t, pad::kPressureCount> m_pressure{};
    float m_leftX = 0.0F;
    float m_leftY = 0.0F;
    float m_rightX = 0.0F;
    float m_rightY = 0.0F;
    bool m_connected = false;
};

} // namespace coney
