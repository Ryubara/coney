// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace coney::net {

/// What this game is in network play.
enum class NetRole : std::uint8_t {
    Offline, ///< neither hosting nor joined (or a session ended)
    Host,    ///< hosting: the server runs in this game
    Client,  ///< joining or joined a game hosted elsewhere
};

/// One joined player, as the host sees him.
struct NetPlayerStatus {
    std::uint8_t slot = 0; ///< 1 for the first remote player (the host is 0)
    std::string name;
    std::uint64_t roundTripNs = 0; ///< smoothed; 0 before the first answer
    float lossFraction = 0.0F;     ///< 0 to 1
    std::int64_t inputLead = 0;    ///< steps of their input commands waiting to play (net::InputBuffer::lead())
    std::uint64_t inputLost = 0;   ///< their steps played with the step before's pad or skipped, since they joined
};

/// Network play's state now (platform::NetPlay::status(), which the debug menus' Network page shows).
struct NetStatus {
    NetRole role = NetRole::Offline;
    std::string state = "offline"; ///< one line: "hosting on port 27615", "connected to Coney as player 2", ...
    std::uint16_t port = 0;        ///< the port listened on while hosting (the free one for port 0)
    std::uint64_t roundTripNs = 0; ///< a client's to the server, smoothed; the host's worst player's
    float lossFraction = 0.0F;     ///< a client's to the server; the host's worst player's
    std::uint64_t bytesSent = 0;   ///< payload bytes of this session
    std::uint64_t bytesReceived = 0;
    std::uint64_t packetsSent = 0;
    std::uint64_t packetsReceived = 0;
    std::vector<NetPlayerStatus> players; ///< the host's joined players
    /// A client's: what the host last said of its input commands (net::InputTiming); nothing before the first.
    std::optional<std::int64_t> inputLead;
    std::uint32_t inputTarget = 0;
    std::uint32_t clockSpeedPercent = 100; ///< a client's game clock (net::NetClient::clockSpeedPercent())
};

/// The bad network played on what this game sends: net::LinkConditions in whole milliseconds and percent, as the
/// Network page's Simulate network menu sets it.
struct NetSimulation {
    std::uint32_t latencyMs = 0;
    std::uint32_t jitterMs = 0;
    float lossPercent = 0.0F; ///< unreliable packets only, as every percent here
    float duplicatePercent = 0.0F;
    float reorderPercent = 0.0F;
};

} // namespace coney::net
