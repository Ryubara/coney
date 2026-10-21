// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net/net_status.h"

namespace coney::debug {

/// Network play's figures and simulated network, as network play keeps them (src/net/net_status.h).
using NetRole = net::NetRole;
using NetPlayerStatus = net::NetPlayerStatus;
using NetStatus = net::NetStatus;
using NetSimulation = net::NetSimulation;

/// What the debug menus can reach of network play: host, join, leave, the settings they use, the figures and the
/// link conditioner. Network play's own API (src/platform/net_play.h) does, through NetPlayControls on the debug
/// tools' side (src/platform/net_play_controls.h); the Network page
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
