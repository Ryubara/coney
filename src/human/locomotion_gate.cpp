// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/locomotion_gate.h"

namespace coney::human {

bool stickBusy(const GateInput& input) { return input.airborne || input.attached || (input.flags & kBusyFlags) != 0; }

bool stickVelocityGated(const GateInput& input) {
    return (input.flags & kVelocityGateFlags) != 0 || input.stateCode == kStateCodeStop ||
           input.stateCode == kStateCodeSix;
}

} // namespace coney::human
