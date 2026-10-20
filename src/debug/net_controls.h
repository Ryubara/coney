// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace coney::debug {

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
};

/// What the Network page shows of network play now.
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
};

/// The bad network the Simulate network menu plays on what this game sends (net::LinkConditions in the page's units).
struct NetSimulation {
    std::uint32_t latencyMs = 0;
    std::uint32_t jitterMs = 0;
    float lossPercent = 0.0F; ///< unreliable packets only, as every percent here
    float duplicatePercent = 0.0F;
    float reorderPercent = 0.0F;
};

/// What the debug menus can reach of network play: host, join, leave, the settings they use, the figures and the
/// link conditioner. The platform's network play implements it (src/platform/net_play.h); the Network page
/// (src/debug/pages_net.cpp) is defined over it, so it is the same in every front end and needs no platform code of
/// its own. Called on the game thread, between steps. Coney's own: the original has no network play
/// (docs/guides/multiplayer.md).
class NetControls {
  public:
    virtual ~NetControls() = default;
    NetControls() = default;
    NetControls(const NetControls&) = delete;
    NetControls& operator=(const NetControls&) = delete;
    NetControls(NetControls&&) = delete;
    NetControls& operator=(NetControls&&) = delete;

    /// The figures now.
    [[nodiscard]] virtual NetStatus status() const = 0;

    /// The name sent when joining (and the server's name when hosting).
    [[nodiscard]] virtual std::string playerName() const = 0;
    /// Sets the name from the next host or join; false, changing nothing, for one that is not 1 to 32 printable ASCII
    /// characters.
    virtual bool setPlayerName(const std::string& name) = 0;
    /// The port Host listens on, and Join connects to when the address names none.
    [[nodiscard]] virtual std::uint16_t port() const = 0;
    virtual void setPort(std::uint16_t port) = 0;
    /// The server Join connects to: a host name or IPv4 address, optionally with `:PORT`.
    [[nodiscard]] virtual std::string address() const = 0;
    virtual void setAddress(const std::string& address) = 0;

    /// Leaves any session and hosts on port(); false when the port cannot be opened (the log says why).
    virtual bool host() = 0;
    /// Leaves any session and joins address(); false when it cannot even start (the log says why). Whether it
    /// joined shows in status() a few steps later.
    virtual bool join() = 0;
    /// Leaves the session: says Bye to the server, or to every player when hosting.
    virtual void disconnect() = 0;

    /// The simulated network, and how to change it (live, on the current session and the next).
    [[nodiscard]] virtual NetSimulation simulation() const = 0;
    virtual void setSimulation(const NetSimulation& simulation) = 0;

    /// What happened, oldest first: hosting, joins, leaves, refusals, errors.
    [[nodiscard]] virtual std::vector<std::string> log() const = 0;
};

} // namespace coney::debug
