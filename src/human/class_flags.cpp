// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/class_flags.h"

#include "human/human_flags.h"

namespace coney::human {

// The six switches in the record's order, each over its own bits only.
// @orig 0x00222c98 Human_ApplyClassFlags (unknown)
FlagChanges classFlagChanges(const combat::PowerClass& power) {
    FlagChanges changes;
    // A plain switch: 1 sets `bit`, any other value clears it.
    const auto plainSwitch = [&changes](std::int8_t value, std::uint64_t bit) {
        if (value == 1) {
            changes.set |= bit;
        } else {
            changes.cleared |= bit;
        }
    };
    // A paired switch: 1 sets `positive` and clears `negative`, -1 the reverse, any other value clears both.
    const auto pairedSwitch = [&changes](std::int8_t value, std::uint64_t positive, std::uint64_t negative) {
        if (value == 1) {
            changes.set |= positive;
            changes.cleared |= negative;
        } else if (value == -1) {
            changes.set |= negative;
            changes.cleared |= positive;
        } else {
            changes.cleared |= positive | negative;
        }
    };
    plainSwitch(power.capsReaction, flag::kUngroundable);
    plainSwitch(power.hardToStun, flag::kHardToStun);
    plainSwitch(power.noEscape, flag::kNoEscape);
    plainSwitch(power.blockFromReact, flag::kBlockFromReact);
    pairedSwitch(power.reactBoost, flag::kIncreasedReact, flag::kIncreasedReactOff);
    pairedSwitch(power.comboReact, flag::kComboStrength, flag::kReducedReact);
    return changes;
}

} // namespace coney::human
