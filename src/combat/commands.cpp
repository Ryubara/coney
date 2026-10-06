// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/commands.h"

#include "core/assert.h"
#include "core/pad.h"

namespace coney::combat {

namespace {

// The order the tables are matched in; a later match overwrites an earlier one (docs/research/combat.md#commands).
// Tapped only matches on a sample with a release, which its own test already ensures.
constexpr std::array<Trigger, kTriggerCount> kMatchOrder{
    Trigger::Held,       Trigger::HistoryHold, Trigger::Pressed,  Trigger::Query,    Trigger::ComboHeld,
    Trigger::ComboPress, Trigger::Tapped,      Trigger::LongHold, Trigger::Released,
};

// The index of `trigger`'s table.
std::size_t tableIndex(Trigger trigger) { return static_cast<std::size_t>(trigger) - 1; }

} // namespace

void CommandTables::add(Trigger trigger, std::uint16_t buttons, CommandId id, std::uint16_t second) {
    CONEY_ASSERT(trigger != Trigger::ComboPress || second != 0);
    m_tables.at(tableIndex(trigger)).push_back(CommandEntry{buttons, second, id});
}

std::span<const CommandEntry> CommandTables::table(Trigger trigger) const { return m_tables.at(tableIndex(trigger)); }

CommandTables CommandTables::street() {
    using namespace command;
    CommandTables tables;
    // 1, held.
    tables.add(Trigger::Held, pad::kL1, kL1Held);
    tables.add(Trigger::Held, pad::kR1, kR1Held);
    tables.add(Trigger::Held, pad::kL2, kL2Held);
    tables.add(Trigger::Held, pad::kR2, kR2Held);
    tables.add(Trigger::Held, pad::kSquare, kSquareHeld);
    tables.add(Trigger::Held, pad::kCross, kCrossHeld);
    // 2, pressed.
    tables.add(Trigger::Pressed, pad::kUp, kDpadUp);
    tables.add(Trigger::Pressed, pad::kDown, kDpadDown);
    tables.add(Trigger::Pressed, pad::kRight, kDpadRight);
    tables.add(Trigger::Pressed, pad::kLeft, kDpadLeft);
    tables.add(Trigger::Pressed, pad::kTriangle, kTrianglePressed);
    tables.add(Trigger::Pressed, pad::kL1, kL1Pressed);
    tables.add(Trigger::Pressed, pad::kR1, kR1Pressed);
    tables.add(Trigger::Pressed, pad::kSquare, kSquarePressed);
    tables.add(Trigger::Pressed, pad::kCross, kCrossPressed);
    tables.add(Trigger::Pressed, pad::kCircle, kCirclePressed);
    // 3, released.
    tables.add(Trigger::Released, pad::kL1, kL1Released);
    tables.add(Trigger::Released, pad::kR2, kR2Released);
    // 5, tapped; 6, long hold; 7, history hold.
    tables.add(Trigger::Tapped, pad::kCircle, kCircleTapped);
    tables.add(Trigger::LongHold, pad::kCross, kCrossLongHold);
    tables.add(Trigger::HistoryHold, pad::kTriangle, kTriangleHeld);
    tables.add(Trigger::HistoryHold, pad::kCircle, kCircleHeld);
    // 8, combination held.
    tables.add(Trigger::ComboHeld, pad::kSelect | pad::kUp, kSelectUp);
    tables.add(Trigger::ComboHeld, pad::kSelect | pad::kRight, kSelectRight);
    tables.add(Trigger::ComboHeld, pad::kSelect | pad::kDown, kSelectDown);
    tables.add(Trigger::ComboHeld, pad::kSelect | pad::kLeft, kSelectLeft);
    tables.add(Trigger::ComboHeld, pad::kSelect | pad::kL3, kSelectL3);
    tables.add(Trigger::ComboHeld, pad::kCircle | pad::kCross, kCircleCross);
    tables.add(Trigger::ComboHeld, pad::kL1 | pad::kR1, kL1R1);
    // 9, combination press: the first mask held, the second newly pressed.
    tables.add(Trigger::ComboPress, pad::kL2, kL2Square, pad::kSquare);
    tables.add(Trigger::ComboPress, pad::kL2, kL2Cross, pad::kCross);
    tables.add(Trigger::ComboPress, pad::kCross, kCrossSquare, pad::kSquare);
    tables.add(Trigger::ComboPress, pad::kCircle, kCircleTriangle, pad::kTriangle);
    return tables;
}

CommandId CommandMatcher::update(std::uint16_t buttons, const CommandTables& tables, int historyHoldSamples,
                                 std::uint64_t disabled) {
    // Take the sample: the edges, then each button's hold count (and, for one that came up, how long it was held).
    const auto previous = m_buttons;
    m_buttons = buttons;
    m_pressed = static_cast<std::uint16_t>(buttons & ~previous);
    m_released = static_cast<std::uint16_t>(previous & ~buttons);
    for (std::size_t bit = 0; bit < m_holdCounts.size(); ++bit) {
        const bool held = ((buttons >> bit) & 1U) != 0;
        m_releasedAfter.at(bit) = held ? 0 : m_holdCounts.at(bit);
        m_holdCounts.at(bit) = held ? m_holdCounts.at(bit) + 1 : 0;
    }

    // Match the tables in order; every match overwrites the command so far. A disabled command's entries are skipped.
    m_command = command::kNone;
    for (const Trigger trigger : kMatchOrder) {
        for (const CommandEntry& entry : tables.table(trigger)) {
            const bool off = entry.command < 64 && ((disabled >> entry.command) & 1U) != 0;
            if (!off && matches(trigger, entry, historyHoldSamples)) {
                m_command = entry.command;
            }
        }
    }
    return m_command;
}

bool CommandMatcher::matches(Trigger trigger, const CommandEntry& entry, int historyHoldSamples) const {
    const std::uint16_t mask = entry.buttons;
    switch (trigger) {
    case Trigger::Held:
        return (m_buttons & mask) != 0;
    case Trigger::Pressed:
        return (m_pressed & mask) != 0;
    case Trigger::Released:
        return (m_released & mask) != 0;
    case Trigger::Query:
        return false;
    case Trigger::Tapped:
        return releasedAfter(mask, 1, kTapMaxSamples);
    case Trigger::LongHold:
        return releasedAfter(mask, 1, kLongHoldSamples - 1) || heldFor(mask, kLongHoldSamples);
    case Trigger::HistoryHold:
        return heldFor(mask, historyHoldSamples);
    case Trigger::ComboHeld:
        return (m_buttons & mask) == mask && (m_pressed & mask) != 0;
    case Trigger::ComboPress: {
        const auto all = static_cast<std::uint16_t>(mask | entry.second);
        return (m_buttons & all) == all && (m_pressed & entry.second) != 0;
    }
    }
    return false;
}

bool CommandMatcher::heldFor(std::uint16_t mask, int count) const {
    for (std::size_t bit = 0; bit < m_holdCounts.size(); ++bit) {
        if (((mask >> bit) & 1U) != 0 && m_holdCounts.at(bit) == count) {
            return true;
        }
    }
    return false;
}

bool CommandMatcher::releasedAfter(std::uint16_t mask, int low, int high) const {
    for (std::size_t bit = 0; bit < m_releasedAfter.size(); ++bit) {
        const int held = m_releasedAfter.at(bit);
        if (((mask >> bit) & 1U) != 0 && held >= low && held <= high) {
            return true;
        }
    }
    return false;
}

} // namespace coney::combat
