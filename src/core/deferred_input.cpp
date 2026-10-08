// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/deferred_input.h"

#include <utility>

namespace coney {

DeferredInput::DeferredInput(InputSource& inner, std::function<bool()> ready)
    : m_inner(inner), m_ready(std::move(ready)) {}

PortSamples DeferredInput::sample(std::uint64_t frame) {
    if (!m_origin && m_ready && m_ready()) {
        m_origin = frame;
    }
    if (!m_origin) {
        // Port 1 plugged in and at rest, as a scripted pad is before its first line.
        PortSamples idle{};
        idle[0].connected = true;
        return idle;
    }
    ++m_played;
    return m_inner.sample(frame - *m_origin);
}

} // namespace coney
