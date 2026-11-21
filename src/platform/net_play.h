// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/game_events.h"
#include "core/pads.h"
#include "hud/hud.h"
#include "net/event_channel.h"
#include "net/link_conditioner.h"
#include "net/net_client.h"
#include "net/net_server.h"
#include "net/net_status.h"
#include "net/protocol.h"
#include "net/snapshot.h"
#include "net/state_digest.h"
#include "net/transport.h"

namespace coney::platform {

/// Network play in the running game: owns the session (a NetServer when hosting, a NetClient when joining), the
/// transport under it with a link conditioner in front, and the settings the Network page and the command line set.
/// The main loop calls advance() once a frame with the frame's elapsed time, beforeStep() and afterStep() around each
/// step (FrameHooks). Coney's own; the original has no network play (docs/guides/multiplayer.md).
///
/// Each session gets a fresh transport from the factory it was made with: ENet in the game (makeEnetTransport()),
/// the loopback in tests.
class NetPlay final {
  public:
    /// Makes the transport of each new session.
    using TransportFactory = std::function<std::unique_ptr<net::Transport>()>;

    /// Network play that makes its transports with `factory` and passes each log line to `print` (which may be
    /// empty) as well as keeping it for the page.
    NetPlay(TransportFactory factory, std::function<void(std::string_view)> print);
    NetPlay(const NetPlay&) = delete;
    NetPlay& operator=(const NetPlay&) = delete;
    NetPlay(NetPlay&&) = delete;
    NetPlay& operator=(NetPlay&&) = delete;
    /// Leaves any session (disconnect()).
    ~NetPlay();

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
    /// The speed this game's clock should run at, percent: joined to a host, the session's
    /// (net::NetClient::clockSpeedPercent()), a little fast or slow so its input commands reach the host as early as
    /// the host wants them; else 100. The main loop multiplies the debug menus' time scale by it.
    [[nodiscard]] std::uint32_t clockSpeedPercent() const;
    /// Hosting: the humans of the host's game this step (net::GameHumans::humans()), which afterStep() sends each
    /// joined player as a snapshot. The game hands them over once a step, before afterStep(); the last ones given are
    /// sent while no new ones come (the game paused, or between levels with none).
    void setGameHumans(std::span<const net::SnapshotHuman> humans);
    /// Hosting: each player slot's HUD in the host's game this step (net::GameHumans::huds()) and the HUD's standing
    /// state (`elements` and `texts`, net::GameHumans::hudElements() and hudTexts()), which afterStep() sends with the
    /// humans in each joined player's snapshot. Handed over and kept as setGameHumans()'s humans are.
    void setGameHuds(std::span<const net::SnapshotHud> huds, std::span<const net::SnapshotHudElement> elements = {},
                     std::span<const net::SnapshotHudText> texts = {});
    /// Joined: each player slot's HUD the newest decoded snapshot holds, by slot: what this game draws each player's
    /// panel from (net::playerHudState() of the words). Empty when not joined, and when hosting (a host draws its own).
    [[nodiscard]] net::SnapshotHuds arrivedHuds() const;
    /// Joined: the HUD's standing state the newest decoded snapshot holds (net::hudStanding() of its elements and
    /// texts), which this game's HUD takes (hud::Hud::applyStanding()). The HUD's defaults when not joined, and when
    /// hosting.
    [[nodiscard]] hud::HudStanding arrivedHudStanding() const;
    /// Where each snapshot's digest goes (`--digest-net`, docs/guides/multiplayer.md#digest): hosting, a line for each
    /// snapshot afterStep() sends player 2, with what it leaves them holding (net::SnapshotSender::holding()); joined,
    /// a line for each snapshot the session decoded, by the host's step it shows. Empty for none.
    void setSnapshotDigests(std::function<void(const net::DigestLine&)> sink) { m_snapshotDigests = std::move(sink); }
    /// The one call through which the game reports what its players see or hear (coney::GameEvents): it records while
    /// this game hosts, and afterStep() sends the step's events to each joined player; offline or joined it keeps
    /// nothing. The run hands it to the game's subsystems (GameSession::setGameEvents()); it outlives every mode.
    [[nodiscard]] GameEvents& gameEvents() { return m_gameEvents; }
    /// Hosting: maps an event's subject (a script handle) to the network id the guest knows it by; empty (the default)
    /// sends every subject as no id, until the game's humans and objects carry ids in the session.
    void setEventSubjectIds(net::EventSubjectIds ids) { m_eventIds = std::move(ids); }
    /// Joined: the host's game events whose step is at most `step` (a host's step the guest shows), each once, in
    /// order (net::NetClient::takeEvents()); none when not joined.
    [[nodiscard]] std::vector<net::ArrivedEvent> takeArrivedEvents(std::uint32_t step);
    /// The host's game starts loading `level` at `checkpoint` (a level change or a checkpoint restart,
    /// docs/guides/multiplayer.md#levels): hosting, every joined guest is told to load it and holdsStep() holds the
    /// host's steps until they have; a guest who joins later is told on joining. Kept while offline too, so hosting
    /// later in the level tells the guests that join. A checkpoint of 0, or a name net::validLevelName() refuses
    /// (empty, over 32 bytes, a character other than a letter, digit, '_' or '-'), is a programmer error
    /// (CONEY_ASSERT).
    void loadLevel(const std::string& level, std::uint16_t checkpoint);
    /// Hosting: the host's mission has ended, and every joined guest is told how. Nothing otherwise.
    void endMission(net::MissionOutcome outcome);
    /// Whether the host's game must hold its next step: hosting, with a joined guest still loading the host's level
    /// (the loading barrier; one who never loads is dropped after its time-out). False offline and joined.
    [[nodiscard]] bool holdsStep() const;
    /// The host's game has started a movie, in this step (its MovieStart event's): hosting, every guest in the level
    /// is waited for. Returns the step that names the movie (steps()).
    std::uint32_t startMovie();
    /// The host's own copy of the movie has ended, or the host skipped it.
    void endHostMovie();
    /// Hosting: whether a guest skipped the movie, so the host ends its own copy as any pad's skip does.
    [[nodiscard]] bool movieSkipped() const;
    /// Whether the host may go on from its movie: offline or joined always; hosting, once its own copy ended and every
    /// guest watching has ended or skipped theirs, or after the movie time-out.
    [[nodiscard]] bool movieMayEnd() const;
    /// Joined: the newest level the host ordered and nothing has taken yet (net::NetClient::takeLevelOrder()), which
    /// this game should load; nothing otherwise.
    [[nodiscard]] std::optional<net::LoadLevel> takeLevelOrder();
    /// Joined: this game has loaded the level of order `load`, and the host is told.
    void levelLoaded(std::uint32_t load);
    /// Joined: the ends of the host's missions since the last call, oldest first.
    [[nodiscard]] std::vector<net::MissionEnd> takeMissionEnds();
    /// Joined: this game's copy of the host's movie of `movieStep` has ended, or its player skipped it.
    void movieDone(std::uint32_t movieStep, bool skipped);
    /// Joined: the host's newest movie (net::NetClient::hostMovie()), which this game's own copy names in movieDone();
    /// nothing otherwise, or before the first.
    [[nodiscard]] std::optional<net::HostMovie> hostMovie() const;
    /// Whether this game hosts, is joining or joined a host, or neither (status()'s role, without the figures).
    [[nodiscard]] net::NetRole role() const;
    /// Keeps a line of the game's side of network play (NetLevels) in the log, as network play's own lines are: on
    /// the Network page, printed as a `net:` line and noted in the session record.
    void report(const std::string& line) { note(line); }
    /// The steps run so far (afterStep() calls): a host's snapshots and events name the step they are from, counted
    /// from 0.
    [[nodiscard]] std::uint32_t steps() const { return m_steps; }
    /// One line for the end of a run: the role, port and players, or the client's state.
    [[nodiscard]] std::string summary() const;

    /// The figures now.
    [[nodiscard]] net::NetStatus status() const;
    /// The name sent when joining (and the server's name when hosting).
    [[nodiscard]] std::string playerName() const { return m_playerName; }
    /// Sets the name from the next host or join; false, changing nothing, for one that is not 1 to 32 printable ASCII
    /// characters.
    bool setPlayerName(const std::string& name);
    /// The port host() listens on, and join() connects to when the address names none.
    [[nodiscard]] std::uint16_t port() const { return m_port; }
    /// Sets port().
    void setPort(std::uint16_t port) { m_port = port; }
    /// The server join() connects to: a host name or IPv4 address, optionally with `:PORT`.
    [[nodiscard]] std::string address() const { return m_address; }
    /// Sets address().
    void setAddress(const std::string& address) { m_address = address; }
    /// Leaves any session and hosts on port() on every interface; false when the port cannot be opened (the log says
    /// why).
    bool host() { return hostOn({}); }
    /// Leaves any session and joins address(); false when it cannot even start (the log says why). Whether it joined
    /// shows in status() a few steps later.
    bool join();
    /// Leaves the session: says Bye to the server, or to every player when hosting.
    void disconnect();
    /// Whether a session is open: hosting, or joining or joined (not a join that has ended).
    [[nodiscard]] bool inSession() const;
    /// The simulated network.
    [[nodiscard]] net::NetSimulation simulation() const { return m_simulation; }
    /// Changes the simulated network, live, on the current session and the next.
    void setSimulation(const net::NetSimulation& simulation);
    /// What happened, oldest first: hosting, joins, leaves, refusals, errors.
    [[nodiscard]] std::vector<std::string> log() const { return {m_log.begin(), m_log.end()}; }

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
    // Hosting: sends each joined player a snapshot of the game's humans at `step`, and writes the digest of what it
    // leaves them holding.
    void sendSnapshots(std::uint32_t step);
    // Hosting: notes each joined player who has loaded the host's level since the last call.
    void noteLoaded();
    // Joined: writes the digest of each snapshot the session decoded since the last call.
    void noteDecoded();

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
    net::NetSimulation m_simulation;
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

    std::vector<net::SnapshotHuman> m_gameHumans;                  // setGameHumans()
    std::vector<net::SnapshotHud> m_gameHuds;                      // setGameHuds()
    std::vector<net::SnapshotHudElement> m_gameHudElements;        // setGameHuds()
    std::vector<net::SnapshotHudText> m_gameHudTexts;              // setGameHuds()
    std::function<void(const net::DigestLine&)> m_snapshotDigests; // setSnapshotDigests()
    std::uint32_t m_steps = 0;                                     // afterStep() calls
    std::uint32_t m_lastDecoded = 0;                               // the newest snapshot a digest was written for
    GameEvents m_gameEvents;                                       // gameEvents(): records while hosting
    net::EventSubjectIds m_eventIds;                               // setEventSubjectIds()
    std::optional<std::pair<std::string, std::uint16_t>> m_level;  // loadLevel(): the host's level and checkpoint
    std::vector<std::uint8_t> m_loadingSlots;                      // the joined slots loading at the last noteLoaded()
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
