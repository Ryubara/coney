// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/pad_handlers.h"

#include <bit>
#include <utility>

namespace coney {

namespace {

// The empty name handler() returns for no handler.
const std::string kNone;

} // namespace

void PadHandlers::set(std::size_t record, std::uint16_t mask, std::string function) {
    if (record >= kRecords || mask == 0) {
        return;
    }
    // The highest set bit of the mask names the slot.
    const auto bit = static_cast<std::size_t>(std::bit_width(mask) - 1);
    m_handlers.at(record).at(bit) = std::move(function);
}

const std::string& PadHandlers::handler(std::size_t record, std::size_t bit) const {
    if (record >= kRecords || bit >= kButtons) {
        return kNone;
    }
    return m_handlers.at(record).at(bit);
}

std::vector<std::string> PadHandlers::due(std::size_t record, std::uint16_t pressed) const {
    std::vector<std::string> names;
    if (record >= kRecords) {
        return names;
    }
    for (std::size_t bit = 0; bit < kButtons; ++bit) {
        if ((pressed & (1U << bit)) != 0 && !m_handlers.at(record).at(bit).empty()) {
            names.push_back(m_handlers.at(record).at(bit));
        }
    }
    return names;
}

void PadHandlers::clear() {
    for (auto& record : m_handlers) {
        record.fill(std::string{});
    }
}

} // namespace coney
