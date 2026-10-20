// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "debug/net_controls.h"
#include "net/link_conditioner.h"
#include "net/net_client.h"
#include "net/net_server.h"
#include "net/transport.h"

namespace coney::platform {

/// Network play in the running game: owns the session (a NetServer when hosting, a NetClient when joining), the
/// transport under it with a link conditioner in front, and the settings the Network page and the command line set.
/// The main loop calls advance() once a frame with the frame's elapsed time, beforeStep() and afterStep() around each
/// step (FrameHooks). Coney's own; the original has no network play (docs/guides/multiplayer.md).
///
/// Each session gets a fresh transport from the factory it was made with: ENet in the game (makeEnetTransport()),
/// the loopback in tests.
class NetPlay final : public debug::NetControls {
  public:
    /// Makes the transport of each new session.
    using TransportFactory = std::function<std::unique_ptr<net::Transport>()>;

    /// Network play that makes its transports with `factory` and passes each log line to `print` (which may be
    /// empty) as well as keeping it for the page.
    NetPlay(TransportFactory factory, std::function<void(std::string_view)> print);
    NetPlay(const NetPlay&) = delete;
    NetPlay& operator=(const NetPlay&) = delete;
    /// Leaves any session (disconnect()).
    ~NetPlay() override;

    /// Hosts on `bindAddress` (empty: every interface) and port(). The Network page's Host is host() on every
    /// interface; `--host ADDR:PORT` names one.
    bool hostOn(const std::string& bindAddress);

    /// Passes the frame's elapsed time, nanoseconds, to the session.
    void advance(std::uint64_t elapsedNs);
    /// Before a step: the session receives everything waiting.
    void beforeStep();
    /// After a step: the session sends what is due and flushes; joins, leaves and state changes go to the log.
    void afterStep();
    /// One line for the end of a run: the role, port and players, or the client's state.
    [[nodiscard]] std::string summary() const;

    // NetControls.
    [[nodiscard]] debug::NetStatus status() const override;
    [[nodiscard]] std::string playerName() const override { return m_playerName; }
    bool setPlayerName(const std::string& name) override;
    [[nodiscard]] std::uint16_t port() const override { return m_port; }
    void setPort(std::uint16_t port) override { m_port = port; }
    [[nodiscard]] std::string address() const override { return m_address; }
    void setAddress(const std::string& address) override { m_address = address; }
    bool host() override { return hostOn({}); }
    bool join() override;
    void disconnect() override;
    [[nodiscard]] debug::NetSimulation simulation() const override { return m_simulation; }
    void setSimulation(const debug::NetSimulation& simulation) override;
    [[nodiscard]] std::vector<std::string> log() const override { return {m_log.begin(), m_log.end()}; }

  private:
    // Keeps a line for the page (the newest kLogLines) and prints it.
    void note(const std::string& line);
    // The link conditioner's settings for the page's simulation.
    [[nodiscard]] net::LinkConditions conditions() const;
    // Drops the session and its transport (after disconnect() has said Bye).
    void teardown();

    static constexpr std::size_t kLogLines = 50;

    TransportFactory m_factory;
    std::function<void(std::string_view)> m_print;
    std::string m_playerName = "Player";
    std::uint16_t m_port;
    std::string m_address = "127.0.0.1";
    debug::NetSimulation m_simulation;
    std::deque<std::string> m_log;

    // The session: one transport, the conditioner in front of it, and a server or a client over the conditioner.
    // Declared in this order so the session goes first and the transport last.
    std::unique_ptr<net::Transport> m_wire;
    std::unique_ptr<net::LinkConditioner> m_link;
    std::unique_ptr<net::NetServer> m_server;
    std::unique_ptr<net::NetClient> m_client;
    std::string m_lastClientState; // the client's describe() last logged
};

/// A transport over UDP (EnetTransport), for NetPlay in the game.
[[nodiscard]] std::unique_ptr<net::Transport> makeEnetTransport();

} // namespace coney::platform
