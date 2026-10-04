// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/pads.h"

#include <cstddef>

#include "core/assert.h"

namespace coney {

std::size_t Pads::recordOfPort(std::size_t port) {
    CONEY_ASSERT(port < kPadPorts);
    // Each port has four multitap slots; the game reads only the first of each.
    return port * 4;
}

void Pads::update(const PortSamples& samples) {
    for (std::size_t port = 0; port < kPadPorts; ++port) {
        m_records.at(recordOfPort(port)).update(samples.at(port));
    }
}

const Pad& Pads::record(std::size_t index) const {
    CONEY_ASSERT(index < kRecords);
    return m_records.at(index);
}

std::size_t Pads::connectedCount() const {
    std::size_t count = 0;
    for (const Pad& pad : m_records) {
        count += pad.connected() ? 1 : 0;
    }
    return count;
}

} // namespace coney
