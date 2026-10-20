# Network multiplayer

The design of Coney's networked multiplayer, and what of it is built. It is the [Online
multiplayer](../roadmap.md#online-multiplayer) milestone, which the maintainer started on 2026-10-20. The original
has no network play, so nothing here reimplements the original: none of this code carries an `@orig` tag, and the
game offline runs exactly as before. This page is kept current: a change to the protocol, a layer or a plan updates
it in the same commit.

The architecture this builds toward (the original's own two-player co-op online, then dedicated and scripted
servers), what every subsystem must sync and the checklist every engineer follows are on [Network
architecture](network-architecture.md).

**Built:** the platform-neutral library `coney_net` (bit stream, transport interface, loopback and link conditioner,
protocol, the server and client sessions with their handshake, keep-alive and timeouts), the ENet transport, and the
game's side: hosting and joining from the debug menus' Network page or the command line ([In the game](#in-the-game)).
Nothing of the game itself is shared yet. **Next:** the layers under [The plan](#plan).

## The model {#model}

- **A listen server.** One player hosts: the server runs inside the host's own game, which simulates the one true
  game, and the other players join it as clients. No separate server program yet; the session code does not need
  one, so a dedicated server later is a front end, not a rewrite.
- **Server-authoritative.** Only the server simulates. A client sends what its pad does, never where its character
  is; the server applies it on the step it belongs to and sends the result back. A client cannot move another
  player, teleport or decide a hit.
- **Input up, snapshots down.** Clients send **input commands**: the pad samples of each step, tagged with the step
  number, several recent steps per packet so a lost packet costs nothing. The server sends **snapshots**: the state
  of the entities a client needs, quantized, delta-compressed against the last snapshot that client acknowledged,
  each entity named by a network id.
- **The client shows the past, smoothly.** A client keeps a short jitter buffer of snapshots and draws the world
  interpolated between two of them, slightly behind the newest, and runs its own follow camera. Client-side
  prediction of the local player comes later; until then the local player shows the server's round trip.
- **Fixed steps, no clock.** The network code counts time in the game's fixed steps (30 a second,
  [The play tick](../research/tasks.md#tick)) and, where it needs nanoseconds (latency, round trip), in the elapsed
  time the main loop already measures with its frame pacer. It never reads a clock itself (the `no-real-clock`
  pre-commit check covers `src/net/` as it covers the rest of `src/`), so a test drives it frame by frame.
- **Single-threaded.** The main loop polls the network without blocking, once a frame, beside the pad. No network
  thread, no locks.

## The layers {#layers}

| Layer | Where | What it does |
| --- | --- | --- |
| Bit stream | `src/net/bit_stream.h` | bits, varints, zig-zag, quantized floats, strings; strict reads |
| Transport | `src/net/transport.h` | the interface every packet goes through: connect, listen, send with a delivery kind, poll |
| Loopback | `src/net/loopback_transport.h` | an in-memory network for the tests: several machines in one process |
| Link conditioner | `src/net/link_conditioner.h` | latency, jitter, loss, duplicates and reordering in front of any transport |
| Protocol | `src/net/protocol.h` | the messages, their ids and exact encoding, the protocol version |
| Link quality | `src/net/link_quality.h` | round trip and loss from the sessions' own pings |
| Sessions | `src/net/net_server.h`, `net_client.h` | the handshake, slots, keep-alive and timeouts of each side |
| ENet | `src/platform/enet_transport.h` | the transport over UDP |

`coney_net` is platform-neutral: no socket, no ENet and no operating-system code, the same rule as the rest of
`src/` ([AGENTS.md](repo:AGENTS.md), Layout). ENet is used only in `src/platform/`.

## Bit stream {#bit-stream}

Every message is written with `BitWriter` and read with `BitReader`. Bits are packed least significant first into
bytes, and the last byte is padded with zero bits. What it offers:

- whole numbers of 1 to 32 bits (`writeBits`, and byte, 16- and 32-bit shorthands);
- **varints**: 7 bits and a "more follows" bit per group, so a sequence delta or a count under 128 costs 8 bits;
  signed ones zig-zag mapped (0, -1, 1, -2, ...) so a small negative number is as cheap;
- **quantized floats**: a value clamped to a range and rounded to `bits` bits, ends exact, within half a step; a
  position in a 2 km level to 1 cm needs 18 bits a coordinate instead of 32;
- exact 32-bit floats and length-prefixed strings with a maximum length.

Writing our own state cannot fail: a value out of its range is a programmer error (`CONEY_ASSERT`). **Reading is
strict**, since the bytes come from the network: every read returns `std::expected`, a read past the end is
`Truncated`, a varint over 64 bits or a string over its limit is `Invalid` (checked before allocating), and
`finish()` refuses trailing bytes and set padding bits.

## Transport and delivery {#transport}

`Transport` is what a socket library must offer, as an interface: `listen`, `connect`, `send(peer, delivery,
bytes)`, `advance(elapsedNs)`, `poll()` (never blocks), `flush`, `disconnect`, `boundPort` and byte and packet
totals. Connections are named by a `PeerId` the transport never reuses. Events are Connected, Disconnected and
Received.

### Delivery {#delivery}

| Delivery | Guarantee | Used for | ENet |
| --- | --- | --- | --- |
| ReliableOrdered | exactly once, in order among reliable packets | handshake, Bye; later game events, mission state | `ENET_PACKET_FLAG_RELIABLE`, channel 0 |
| UnreliableSequenced | at most once; one older than a packet already delivered is dropped | later: snapshots | no flag, channel 1 |
| Unreliable | at most once, any order | Ping, Pong; later: input commands (redundant anyway) | `ENET_PACKET_FLAG_UNSEQUENCED`, channel 1 |

An unreliable packet too big for one datagram also gets `ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT`: without it ENet
resends a big unreliable packet's fragments reliably, so a lost snapshot fragment would stall the channel instead of
costing one snapshot.

### Loopback {#loopback}

`LoopbackNetwork` and `LoopbackTransport` are an in-memory network: transports on one network connect to each other
by a name and a port, as machines on a real one do, so a test runs a server and several clients in one process with
no socket. A packet goes straight into the other end's queue: no loss, no delay, in send order. Connecting where
nobody listens, or to a full listener, fails as a refused connection does.

### Link conditioner {#link-conditioner}

`LinkConditioner` sits in front of any transport and makes what this end **sends** arrive late (latency, plus jitter
drawn per packet), never (loss), twice (duplicates) or out of order (a packet held back a further delay). Conditions
on one end add their latency to the round trip once; set both ends for a symmetric link. The delivery contracts
hold under it: a reliable packet is only delayed and never overtakes another reliable one (a real transport resends
what is lost, which looks like delay), and a sequenced packet that would arrive after a newer one is dropped. It
draws from its own seeded xorshift generator, **never from the game's random numbers** (`GameRandom`), so network
conditions never change the game, and a test's conditions are the same on every run. The tests use it in front of
the loopback; the Network page will put it in front of ENet.

## Protocol {#protocol}

Each message is one packet: a message id byte, its fields, zero padding to a whole byte. `kProtocolVersion` (now 1)
changes with any change to a layout or a meaning; a test pins the encoding byte for byte so a change cannot slip in
unnoticed.

| Id | Message | Direction | Fields | Delivery |
| --- | --- | --- | --- | --- |
| 1 | Hello | client to server | magic `CONY` (32 bits), protocol version (16), player name (string, 32 bytes at most) | reliable |
| 2 | Welcome | server to client | player slot (8), step rate in Hz (16), server name | reliable |
| 3 | Reject | server to client | reason (8): version mismatch, server full, bad name; server's version (16) | reliable |
| 4 | Bye | either | reason (8): quit, timed out, protocol error | reliable |
| 5 | Ping | either | sequence (varint, 32 bits at most) | unreliable |
| 6 | Pong | either | the Ping's sequence | unreliable |

**Decoding is strict.** An unknown id or enumerator, the wrong magic, a short packet, a name over its limit or
trailing bytes is an error, and the receiving session closes the connection with Bye (protocol error). The one
exception keeps versions apart cleanly: Hello's magic and version keep their place in every version, and a Hello of
another version decodes to its version alone, so the server can always answer "version mismatch" rather than
misread it.

A player name is 1 to 32 printable ASCII characters (space to tilde), not all spaces.

## Sessions {#sessions}

`NetServer` and `NetClient` are the state machines over a transport. Each step, the main loop calls `receive()`
before the simulation step (every waiting event and message, then the step's timeouts) and `send()` after it (the
pings that are due, then a flush), and `advance(elapsedNs)` once a frame.

**The handshake.** The client connects and sends Hello. The server answers in this order of what the player can do
about it: another protocol version (Reject, version mismatch), a bad name (Reject, bad name), no free slot (Reject,
server full), else Welcome with the lowest free slot (1 to `maxClients`; the host is player 0) and its step rate,
and reports the player Joined. After a Reject the server closes the connection; the reason travels in the Reject.
The server accepts two transport connections more than it has slots, so a player over the limit still hears
"server full" rather than nothing.

**Client states:** Idle, Connecting, Handshaking (Hello sent), Connected (Welcome received), Disconnected, with why:
left, unreachable, timed out, rejected (and the reason), the server closed it (and its Bye reason), connection lost,
protocol error. A client can connect again after a session ends.

**Keep-alive and timeouts, in steps.** Each side pings the other every 6 steps (5 a second at 30 Hz) once joined and
answers every Ping with a Pong. Defaults:

| What | Steps | At 30 Hz |
| --- | --- | --- |
| Ping interval | 6 | 0.2 s |
| A joined peer silent for | 300 | 10 s |
| No Welcome (client) or no Hello (server) within | 150 | 5 s |

A side that times the other out says Bye (timed out) and closes; the server reports the player Left. A Bye from a
client is reported as Left with its reason; a connection the transport drops with no Bye as Left with
`connectionLost`.

### Link quality {#link-quality}

`LinkQuality` measures each connection from the sessions' own Ping and Pong, so the figures are the same over ENet
and over the loopback: the round trip smoothed as TCP does (the newest answer weighs an eighth), the newest round
trip, and the loss: of the last 32 pings, those at least 8 pings old that were never answered. Late, duplicate and
forged answers are ignored.

## ENet {#enet}

[ENet](http://enet.bespin.org) 1.3.18 (MIT) is the UDP transport, approved by the maintainer on 2026-10-20. It is
fetched at the tag's commit by `cmake/deps.cmake` and compiled from its C files into a static library, as Dear ImGui
is, linked by `src/platform/` alone ([Building and testing](building.md#enet)). It gives reliable and sequenced
channels, fragmentation, resends and its own connection timeouts.

`EnetTransport` polls with no wait. ENet keeps its own time for its resends and timeouts, the one clock in network
play Coney does not feed; `advance()` is ignored. A server listens on every interface by default; the tests listen
on 127.0.0.1 only, so no desktop firewall asks about the test program. The default port is **27615** (UDP).

## In the game {#in-the-game}

`NetPlay` (`src/platform/net_play.h`) is network play in the running game: it owns the session (a `NetServer` when
hosting, a `NetClient` when joining), a fresh transport per session with a link conditioner in front, and the
settings. It implements `debug::NetControls` (`src/debug/net_controls.h`), which the Network page
(`src/debug/pages_net.cpp`, [The debug menus](debug-menu.md#pages)) is defined over, so the page needs no platform
code, as the Audio page over `AudioControls`.

**The main loop.** `FrameHooks` (`src/gamemodes/game_mode_stack.h`) has `beforeStep` and `afterStep`, called around
every due step, a step the pause holds included, so a paused host keeps its players. Network play receives in
`beforeStep` and sends and flushes in `afterStep`, so a step's result leaves in the same frame. Its time is the
frame time the frame pacer measured, passed on once a frame; in test mode, which has no clock, each step passes a
fixed 1/30 s, so a run is the same every time.

**The Network page:** the status line; Player name (also the server's name when hosting), Port (default 27615; 0 is
any free port) and Address (`ADDR` or `ADDR:PORT`); Host (every interface), Join and Disconnect; the ping (plotted
as `Network/Ping (ms)`; a host shows its slowest player's), the loss, bytes and packets sent and received; the
players with their ping and loss; a **Simulate network** submenu (latency, jitter, loss, duplicates, reordering on
what this game sends, live); and a log of what happened, also printed as `net:` lines.

**The command line:** `--host [[ADDR:]PORT]`, `--join ADDR[:PORT]` and `--net-name NAME`
([Running Coney](running.md#network-play)).

## Testing {#testing}

Everything above `EnetTransport` is tested on the loopback, frame by frame with no real time, in `tests/net/`: the
bit stream's encodings and refusals, the loopback, the link conditioner's contracts and determinism, the protocol's
round trips, pinned bytes and refusals, link quality, and the sessions (handshake, slots, refusals, leaving, every
timeout, protocol errors, and the round trip and loss through a conditioned link). `tests/platform/
enet_transport_test.cpp` connects two ENet transports on a free port of 127.0.0.1, sends every delivery kind and a
fragmented packet, closes, and runs the sessions' handshake over real UDP. `tests/platform/net_play_test.cpp` hosts
and joins between two `NetPlay`s on the loopback (by address, the figures, the simulated latency in the ping,
leaving and stopping, refusals); `tests/debug/net_page_test.cpp` drives the Network page over a fake
`NetControls`; and the `coney.hosts_headless` smoke test runs `coney --headless --frames 3 --host 127.0.0.1:0`.

## The plan {#plan}

Each step is a pull request that builds and is tested on its own.

1. **Foundation** (built): this page, ENet, `coney_net` and its tests.
2. **The Network page and flags** (built): [In the game](#in-the-game).
3. **A second player** joining on pad port 2 of the host's game.
4. **Input commands:** the client's pad samples, tagged with the step, with the last few steps repeated in every
   packet; the server's buffer that applies each on its step.
5. **Replicated players:** network ids, snapshots (quantized, delta-compressed against acknowledged baselines),
   the client's jitter buffer and interpolation, and its own follow camera.
6. **AI humans, events, mission state:** the rest of what a client must see and hear, events reliable.
7. **Prediction:** the local player predicted and corrected from the server's snapshots.
