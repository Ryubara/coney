// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The pad's buttons turned into one command id per update, through nine trigger tables matched in a fixed order.
// Research: docs/research/combat.md#commands

namespace coney::combat {

/// A command id: what the trigger tables make of one update's buttons; 0 is none. Scripts may add any id.
using CommandId = std::uint32_t;

/// The command ids the street's tables make (docs/research/combat.md#commands), and the ones combat reads.
namespace command {
inline constexpr CommandId kNone = 0;
inline constexpr CommandId kR2Held = 0x01;
inline constexpr CommandId kR2Released = 0x02;
inline constexpr CommandId kR1Pressed = 0x03;
inline constexpr CommandId kR1Held = 0x04;          ///< Blocking: nothing else runs while it is the command.
inline constexpr CommandId kL2Held = 0x05;          ///< Sprint asked for.
inline constexpr CommandId kL1Held = 0x06;          ///< One half of the mash.
inline constexpr CommandId kL1Pressed = 0x07;       ///< Pressed overwrites held on the press's sample.
inline constexpr CommandId kL1Released = 0x08;      ///< Keeps a block.
inline constexpr CommandId kSelectL3 = 0x09;        ///< Select + L3.
inline constexpr CommandId kTrianglePressed = 0x0a; ///< Climb, context action, jump; mug in a grab.
inline constexpr CommandId kTriangleHeld = 0x0b;    ///< Triangle held for the history hold.
inline constexpr CommandId kCircleTapped = 0x0d;    ///< Circle released within 6 samples: grab.
inline constexpr CommandId kCircleHeld = 0x0e;      ///< Circle held for the history hold: tackle.
inline constexpr CommandId kSquarePressed = 0x0f;   ///< Square: attacks.
inline constexpr CommandId kCrossLongHold = 0x10;   ///< Cross released within 3 samples or held 4: the X attack.
/// The square of a chain's later step: what an AI's attack writes for kinds 4, 5 and 7 (docs/research/ai.md); the
/// counter test takes it as a square (docs/research/combat.md, `0x0027b988`). Which pad state gives it is not traced.
inline constexpr CommandId kSquareChain = 0x11;
inline constexpr CommandId kCrossPressed = 0x12; ///< Cross pressed: what a chain buffers.
inline constexpr CommandId kSquareHeld = 0x15;
inline constexpr CommandId kCrossHeld = 0x16;
/// The grab spin an AI sends (`PlayerCmd_IsGrabSpin`, read as R1 pressed in a grab; no pad table makes it).
inline constexpr CommandId kGrabSpin = 0x19;
inline constexpr CommandId kCirclePressed = 0x1e; ///< Circle pressed: the throw or spin in a grab.
inline constexpr CommandId kL1R1 = 0x1f;          ///< L1 + R1: rage with a full meter.
inline constexpr CommandId kL2Cross = 0x20;       ///< L2 held, cross pressed: the charge.
inline constexpr CommandId kL2Square = 0x21;      ///< L2 held, square pressed: the dive.
inline constexpr CommandId kCrossSquare = 0x22;   ///< Cross held, square pressed: the power strike in a grab.
inline constexpr CommandId kCircleCross = 0x23;   ///< Circle + cross: the strong grapple; in a grab, 63.
inline constexpr CommandId kCircleTriangle = 0x24;
inline constexpr CommandId kDpadDown = 0x25;
inline constexpr CommandId kDpadUp = 0x26;
inline constexpr CommandId kDpadLeft = 0x27;
inline constexpr CommandId kDpadRight = 0x28;
inline constexpr CommandId kSelectUp = 0x29;
inline constexpr CommandId kSelectRight = 0x2a;
inline constexpr CommandId kSelectDown = 0x2b;
inline constexpr CommandId kSelectLeft = 0x2c;
} // namespace command

/// The nine triggers, numbered as `AddCommand`'s first argument.
enum class Trigger : std::uint8_t {
    Held = 1,        ///< The button is down.
    Pressed = 2,     ///< It went down this sample.
    Released = 3,    ///< It came up this sample.
    Query = 4,       ///< Not used by the street's tables; never matches in Coney (its matcher is not researched).
    Tapped = 5,      ///< It came up after 1 to kTapMaxSamples samples down.
    LongHold = 6,    ///< It came up after 1 to kLongHoldSamples - 1 samples, or its kLongHoldSamples-th sample is held.
    HistoryHold = 7, ///< Exactly its Nth sample is held, N = CombatTuning::historyHoldSamples.
    ComboHeld = 8,   ///< Every button of the set is down and the last of them went down this sample.
    ComboPress = 9,  ///< Every button of both masks is down and one of the second mask's went down this sample.
};
/// How many triggers there are.
inline constexpr std::size_t kTriggerCount = 9;

/// The longest press, in samples, that still counts as a tap.
inline constexpr int kTapMaxSamples = 6;
/// The long hold fires on the release of a press shorter than this, or on this sample held.
inline constexpr int kLongHoldSamples = 4;

/// One entry of a trigger table: `{mask, command, buttons, extra}` in the original, without the per-player mask.
struct CommandEntry {
    std::uint16_t buttons = 0; ///< The button mask (pad::kL2 ... pad::kLeft).
    std::uint16_t second = 0;  ///< The second mask of a ComboPress entry; 0 otherwise.
    CommandId command = command::kNone;
};

/// The nine trigger tables, filled by add() as `AddCommand` fills them.
///
/// Coney leaves out the entry's per-player mask (`0xff` enables an entry for pad 0, `0xfe` makes it a pending
/// command at `+0x24`): every entry applies to the one player.
class CommandTables {
  public:
    /// Appends an entry to the table of `trigger`. A ComboPress entry needs `second`.
    /// @orig 0x00147430 AddCommand (unknown)
    void add(Trigger trigger, std::uint16_t buttons, CommandId id, std::uint16_t second = 0);

    /// The entries of `trigger`'s table in the order they were added.
    [[nodiscard]] std::span<const CommandEntry> table(Trigger trigger) const;

    /// The tables as `global.lua` fills them for the street, read at runtime (docs/research/combat.md#commands), in
    /// the order listed there.
    [[nodiscard]] static CommandTables street();

  private:
    std::array<std::vector<CommandEntry>, kTriggerCount> m_tables;
};

/// Turns each update's button word into a command, remembering how long each button has been held.
///
/// The tables are matched in the original's order, held, history hold, pressed, query, combination held, combination
/// press, tapped, long hold, released, and a later match overwrites an earlier one. **Coney choice**: inside one
/// table a later entry overwrites an earlier one too (the research does not say).
class CommandMatcher {
  public:
    /// Takes one sample's held buttons and returns its command (command::kNone when nothing matches); call it once
    /// per update. `historyHoldSamples` is CombatTuning::historyHoldSamples. A command whose bit is set in `disabled`
    /// (bit n for id n, `EnableCommand`'s per-pad mask) is not returned: it is kept as the pending command instead
    /// (per-player `+0x24`), which the human does not act on (docs/references/bindings/input.md#padsethandlerex).
    /// @orig 0x00147940 Commands_Match (unknown)
    CommandId update(std::uint16_t buttons, const CommandTables& tables, int historyHoldSamples,
                     std::uint64_t disabled = 0);

    /// The last update's command.
    [[nodiscard]] CommandId command() const { return m_command; }
    /// The last update's disabled match (per-player `+0x24`): what would have been the command but for
    /// `EnableCommand`; command::kNone when none.
    [[nodiscard]] CommandId pending() const { return m_pending; }
    /// The held buttons of the last sample.
    [[nodiscard]] std::uint16_t buttons() const { return m_buttons; }
    /// The buttons that went down with the last sample.
    [[nodiscard]] std::uint16_t pressed() const { return m_pressed; }
    /// The buttons that came up with the last sample.
    [[nodiscard]] std::uint16_t released() const { return m_released; }
    /// Samples the button `bit` (0-15) has been held, this one included; 0 when up.
    [[nodiscard]] int holdCount(std::size_t bit) const { return m_holdCounts.at(bit); }

  private:
    // Whether `entry` matches the sample just taken under `trigger`.
    [[nodiscard]] bool matches(Trigger trigger, const CommandEntry& entry, int historyHoldSamples) const;
    // Whether any button of `mask` is held now with exactly `count` samples held.
    [[nodiscard]] bool heldFor(std::uint16_t mask, int count) const;
    // Whether any button of `mask` came up now after between `low` and `high` samples held.
    [[nodiscard]] bool releasedAfter(std::uint16_t mask, int low, int high) const;

    std::uint16_t m_buttons = 0;
    std::uint16_t m_pressed = 0;
    std::uint16_t m_released = 0;
    std::array<int, 16> m_holdCounts{};    // samples each bit has been held, this one included
    std::array<int, 16> m_releasedAfter{}; // for a bit that came up this sample, how long it had been held
    CommandId m_command = command::kNone;
    CommandId m_pending = command::kNone;
};

} // namespace coney::combat
