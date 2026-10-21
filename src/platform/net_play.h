// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/pads.h"
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
    /// Leaves any session, then keeps the transports still closing going until they have closed or kQuitWaitNs of
    /// `wait`'s time has passed, so the other games get this one's Bye rather than a lost connection. For the end of a
    /// run, outside the main loop: each call of `wait` waits a little and returns the nanoseconds that passed (a
    /// FramePacer's waitForFrame() in the game, a fixed step in tests).
    void shutdown(const std::function<std::uint64_t()>& wait);
    /// How many left sessions' transports are still closing.
    [[nodiscard]] std::size_t closingTransports() const { return m_closing.size(); }

    /// Steps a left session's transport is kept going for its close (half a second; Coney's choice): a few round
    /// trips on a home or internet link, after which what is left is closed at once.
    static constexpr std::uint32_t kCloseSteps = 15;
    /// The longest shutdown() waits for the closes (Coney's choice).
    static constexpr std::uint64_t kQuitWaitNs = 500'000'000;

    /// This step's pads through network play, given the game's own (`local`). Hosting with a guest joined, their pad is
    /// port 2, as a second pad plugged into the host's console would be, and the host's own port 2 is set aside until
    /// they leave; port 1 is always the host's. Joined to a host, port 1 is what the session sends them, and the pads
    /// come back unchanged. Offline, they pass through. NetInput calls it.
    [[nodiscard]] PortSamples pads(const PortSamples& local);
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
    // Drops the session and sets its transport aside to close (after disconnect() has said Bye).
    void teardown();
    // Gives the closing transports `elapsedNs` and one service: their events are read and dropped, what is queued is
    // sent, and the ones that have closed, or (with `countStep`) have used up their steps, are destroyed.
    void serviceClosing(std::uint64_t elapsedNs, bool countStep);

    // A left session's transport and the conditioner in front of it, kept until its connections have closed: ENet
    // throws away a peer's undelivered packets when that peer's close arrives, so a transport destroyed right behind
    // the Bye lost it. Declared so the conditioner goes before the transport under it.
    struct Closing {
        std::unique_ptr<net::Transport> wire;
        std::unique_ptr<net::LinkConditioner> link;
        std::uint32_t stepsLeft = kCloseSteps;
    };

    static constexpr std::size_t kLogLines = 50;

    TransportFactory m_factory;
    std::function<void(std::string_view)> m_print;
    std::string m_playerName = "Player";
    std::uint16_t m_port;
    std::string m_address = "127.0.0.1";
    debug::NetSimulation m_simulation;
    std::deque<std::string> m_log;
    std::vector<Closing> m_closing;
    std::uint64_t m_closingElapsedNs = 0; // time passed since the closing transports were last serviced

    // The session: one transport, the conditioner in front of it, and a server or a client over the conditioner.
    // Declared in this order so the session goes first and the transport last.
    std::unique_ptr<net::Transport> m_wire;
    std::unique_ptr<net::LinkConditioner> m_link;
    std::unique_ptr<net::NetServer> m_server;
    std::unique_ptr<net::NetClient> m_client;
    std::string m_lastClientState; // the client's describe() last logged
};

/// The game's pad input through network play: what `inner` (null: no pads) gives, passed through NetPlay::pads().
/// The main loop puts it between the debug menus' input gate and the mode stack, so a guest sends what their own game
/// reads (never their debug menu's input) and a host's port 2 is the guest's pad.
class NetInput final : public InputSource {
  public:
    /// Network play's input over `inner`; both must outlive it.
    NetInput(InputSource* inner, NetPlay& net) : m_inner(inner), m_net(net) {}
    /// The inner source's sample for `frame`, through NetPlay::pads().
    [[nodiscard]] PortSamples sample(std::uint64_t frame) override;

  private:
    InputSource* m_inner;
    NetPlay& m_net;
};

/// A transport over UDP (EnetTransport), for NetPlay in the game.
[[nodiscard]] std::unique_ptr<net::Transport> makeEnetTransport();

} // namespace coney::platform
