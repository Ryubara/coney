// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "net/transport.h"

// Small helpers the network tests share: packets from text and every event a transport has waiting.

namespace coney::net::test {

/// The bytes of `text`, as a packet.
inline std::vector<std::byte> packet(std::string_view text) {
    const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
    return {bytes.begin(), bytes.end()};
}

/// A packet's bytes as text, for comparing.
inline std::string text(const std::vector<std::byte>& data) {
    std::string out;
    for (const std::byte b : data) {
        out.push_back(static_cast<char>(b));
    }
    return out;
}

/// Every event `transport` has waiting now.
inline std::vector<TransportEvent> drain(Transport& transport) {
    std::vector<TransportEvent> events;
    while (std::optional<TransportEvent> event = transport.poll()) {
        events.push_back(std::move(*event));
    }
    return events;
}

} // namespace coney::net::test
