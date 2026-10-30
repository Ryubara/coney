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
game's side: hosting and joining from the debug menus' Network page or the command line ([In the game](#in-the-game)),
and the guest's pad as the host's pad port 2 ([Player two](#player-two)); the network ids and the replicated-field
tables ([Network ids](#net-ids)); and [snapshots](snapshots.md) of humans, delta-compressed against acknowledged
baselines within a 1,200-byte budget. The guest does not see the host's game yet: nothing fills the snapshots from it.
**Next:** the layers under [The plan](#plan).

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
| Network ids | `src/net/net_id.h`, `net_id_table.h` | the server's ids for replicated things, and each side's table from ids to its own handles |
| Field tables | `src/net/replicated_fields.h` | each replicated kind's fields: their encodings, bits and ranges |
| Snapshots | `src/net/snapshot.h` | each client's humans as deltas against what it acknowledged, within a budget ([Snapshots](snapshots.md)) |
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
| UnreliableSequenced | at most once; one older than a packet already delivered is dropped | Snapshot | no flag, channel 1 |
| Unreliable | at most once, any order | Ping, Pong, InputCommands (each repeats the steps before it), InputTiming, SnapshotAck | `ENET_PACKET_FLAG_UNSEQUENCED`, channel 1 |

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

Each message is one packet: a message id byte, its fields, zero padding to a whole byte. `kProtocolVersion` (now 4)
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
| 7 | PadInput | retired: one step's pad (version 2 only); the id is never reused | | |
| 8 | InputCommands | client to server | the newest step (varint, 32 bits at most), the number of pads less one (3 bits), the newest step's pad, then each older step's "same pad" bit or pad (below); since version 3 | unreliable |
| 9 | InputTiming | server to client | the client's lead (zigzag varint, 32 bits at most), the target lead (8); since version 3 | unreliable |
| 10 | Snapshot | server to client | the humans as a delta against an acknowledged snapshot ([The packet](snapshots.md#packet)); since version 4 | unreliable, sequenced |
| 11 | SnapshotAck | client to server | the newest snapshot decoded (varint); since version 4 | unreliable |

**A pad** is connected (1 bit) and, only when connected, the button word (16 bits, the PS2 layout of `core/pad.h`),
the four raw stick bytes (right x, right y, left x, left y) and, for each of the 12 pressure bytes, a bit saying
whether it is above 0 followed by the byte when it is. A pad with nothing held costs 61 bits; a disconnected one, 1
bit, and decodes as a pad at rest. In InputCommands each older step's pad is one bit when it is the same as the step
after it, which it mostly is, so four steps of a held pad cost hardly more than one. A pressure flagged as above 0 but
written as 0, an older pad written in full that is the same as the step after it, and pads for steps below 0 are
refused, so each message has one encoding.

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

**Closing without losing the Bye.** ENet throws away a peer's packets not yet handed to the program when that peer's
close arrives, so a transport destroyed straight behind the Bye lost it, and the other game read a player who quit as
one whose connection was lost (seen on the Network page's log, 2026-10-21). So a session that ends never takes its
transport with it: `NetPlay` keeps the transport and its link conditioner going, serviced once a step and never
waited on, until `Transport::closed()` says every connection is gone (after the far end acknowledged what was sent
before the close), for at most 15 steps (`NetPlay::kCloseSteps`, half a second; Coney's choice), then destroys it,
which closes what is left at once. Hosting again on the port a closing host still holds closes that host at once. At
the end of a run, after the last frame, `NetPlay::shutdown()` does the same with a short real wait: at most half a
second (`kQuitWaitNs`) of the time the wait it is given reports (a `FramePacer` at 100 frames a second in the game, a
fixed step in the tests), and only while something is closing.

## In the game {#in-the-game}

`NetPlay` (`src/platform/net_play.h`) is network play in the running game: it owns the session (a `NetServer` when
hosting, a `NetClient` when joining), a fresh transport per session with a link conditioner in front, and the
settings. Its figures and simulated network are `net::NetStatus` and `net::NetSimulation` (`src/net/net_status.h`).
The Network page (`src/debug/pages_net.cpp`, [The debug menus](debug-menu.md#pages)) is defined over
`debug::NetControls` (`src/debug/net_controls.h`), so it needs no platform code, as the Audio page over
`AudioControls`; `NetPlayControls` (`src/platform/net_play_controls.h`) turns `NetPlay`'s own API into it, on the debug
tools' side ([The debug tools and the game](building.md#debug-tools)), so a build without the debug tools still has
network play.

**The main loop.** `FrameHooks` (`src/gamemodes/game_mode_stack.h`) has `beforeStep` and `afterStep`, called around
every due step, a step the pause holds included, so a paused host keeps its players. Network play receives in
`beforeStep`, before the step's pads are read, so a guest's pad that has arrived is in the same step's samples; it
sends and flushes in `afterStep`, so a step's result leaves in the same frame. Its time is the
frame time the frame pacer measured, passed on once a frame; in test mode, which has no clock, each step passes a
fixed 1/30 s, so a run is the same every time.

**The Network page:** the status line; Player name (also the server's name when hosting), Port (default 27615; 0 is
any free port) and Address (`ADDR` or `ADDR:PORT`); Host (every interface), Join and Disconnect; the ping (plotted
as `Network/Ping (ms)`; a host shows its slowest player's), the loss, bytes and packets sent and received; the
players with their ping and loss; a **Simulate network** submenu (latency, jitter, loss, duplicates, reordering on
what this game sends, live); and a log of what happened, also printed as `net:` lines.

**The command line:** `--host [[ADDR:]PORT]`, `--join ADDR[:PORT]` and `--net-name NAME`
([Running Coney](running-options.md#network-play)).

## Player two {#player-two}

A guest plays the host's game as **player 2 on pad port 2**: their own player 1 pad, sampled by their game each step, is
what the host's game reads on port 2, exactly as a second pad plugged into the host's machine would be. Nothing in
the game knows the pad is remote, so whatever a second local pad does, a remote one does too, and gameplay stays the
original's.

- **Two ports, one remote player.** The PS2 has two pad ports, and the original reads only pad records 0 (port 1) and
  4 (port 2, first multitap slot) (confirmed (code) at `0x001454a8`, [Pad record](../research/frontend.md#pad-record)),
  so its games have two players at most. A host therefore takes one remote player (`ServerConfig::maxClients` 1 in
  `NetPlay`); a second is told the server is full. The guest holds slot 1, which is port 2. Port 1 is always the host's.
- **The path of a pad.** On the guest, `NetInput` (`src/platform/net_play.h`) sits between the debug menus' input gate
  and the mode stack: port 1 as the guest's game reads it (never their debug menu's input, which the gate keeps) goes to
  the session, which sends it in its [input commands](#input-commands) at the end of the step, numbered by the guest's
  step. On the host the server's input buffer plays each of the guest's steps on a step of its own, and the host's
  `NetInput` puts that pad on port 2. On a link with no delay a press reaches the host's game two of the host's steps
  after the one in which the guest's game saw it (one to arrive, one of buffer delay); over a network, add the one-way
  delay.
- **The host's own port 2** plays while nobody has joined, and again once the guest leaves; while they are joined it is
  set aside (Coney's choice: one pad per port).
- **Until the guest's first step plays**, port 2 reads unplugged; a guest with no pad plugged in sends an unplugged
  pad, and port 2 stays unplugged.
- **A stalled link lets go.** When no new step has come for more than 15 steps (half a second;
  `InputBufferConfig::holdSteps`), port 2 reads plugged in with nothing held and the sticks centred, rather than
  holding the last buttons until the 10 s timeout drops the player (Coney's choice). The next step takes over again.
- **What a second pad does in Coney today.** The front end's two-player screens read port 2: the Story's number of
  players (`PM_NumPlayers` and its "player 2 press START"; the main menu first counts the connected pads,
  [Profile manager](../research/frontend.md#profile-manager)) and the Rumble mode's (`RM_NumPlayers`, No 2nd
  Controller, Choose Gangs' second side). In play, the original lets a second pad join when `CfgMultiplayerJoin`
  allows it: `GameState_SyncPlayers` (`0x0041a460`) makes the gang's next Warrior player 2 and calls the multiplayer
  callback ([level99](../research/scripting.md#level99)). Coney has one player in play so far
  (`PlayerState::multiplayerCallback` is kept, not called), so a remote pad 2 joins a level only once that two-player
  sync is built; nothing in network play has to change for it.

**Compared with the original** (2026-10-21). In the original, PCSX2 2.9.129 (the PCSX2-MCP fork) with a DualShock 2
on port 2 and both driven through the debug server's pad: from a cold boot, both pad records read connected
(`+0x4c` of records 0 and 4 at `0x005dd810`); STORY shows 1 PLAYER : 2 PLAYER; 2 PLAYER and cross shows "Player 2
press the START button"; START on port 2 goes on to the Profile Manager. Confirmed (runtime). Coney, a host on the
disc (`--skip-movies`, an input script on port 1) and a guest joined over ENet on 127.0.0.1 whose `--pad-pipe` pad
pressed START: the host went `PM_Mode`, `PM_NumPlayers`, then `PM_Profile` after the guest's START, showing the same
three screens; the host alone went from `PM_Mode` straight to `PM_Profile`. The network carrying the pad has no
counterpart in the original.

## Input commands {#input-commands}

A guest's pad travels as **input commands**: every step, its client sends that step's pad and the pads of the steps
before it (`ClientConfig::inputSteps`, 4), tagged with the newest step's number, unreliably. A lost packet costs
nothing as long as one of the next three arrives, since each carries its steps again; a repeated step is played
once, and one that comes after its turn is dropped.

The server keeps a **buffer** per player (`net::InputBuffer`, `src/net/input_buffer.h`) and each step, after
receiving, plays the next of the player's steps from it, so each of the guest's steps is played on exactly one of the
host's, in order, whatever order the packets came in. Its rules (`InputBufferConfig`, Coney's choices):

- **A delay.** It plays one step (`delaySteps`) behind the newest step that has arrived: a packet up to one step late
  still comes in time. The first step to arrive sets it; until a step plays, port 2 reads unplugged.
- **A missing step** (lost from every packet that carried it) plays the step before's pad again: a held button stays
  held.
- **A stall.** When nothing new has come, the last pad holds and the buffer waits rather than skipping the step, so
  the delay grows by one; the steps still on their way play late, but they play.
- **Too far behind.** When more than `delaySteps` + `slackSteps` (3) steps wait, it skips the oldest and plays
  `delaySteps` behind the newest again, so a long stall does not leave the guest's input late for good.
- **Letting go.** No new step for more than `holdSteps` (15) lets the pad go ([Player two](#player-two)).

The Network page's Players line shows each player's lead (steps waiting: the delay when all is well) and the steps
lost (missing or skipped) since they joined.

### Input timing {#input-timing}

With each Ping (5 a second) the server sends the player an **InputTiming**: their lead and the lead it wants (the
delay). The guest's game then runs its clock a little fast or slow (`NetClient::clockSpeedPercent()`): at 102 % while
the lead is below the target, so its steps, and their commands, come a little sooner; at 98 % while it is over the
target by more than one, so they stop waiting longer than they need to; at its own speed in between
(`ClientConfig::clockNudgePercent`, 2). Two percent is a step gained or lost in 50, too little to see. The guest's
FrameClock takes it as a speed, as it takes the Time page's (`FrameClock::setSpeed()`), and test mode never applies
it. This is Overwatch's way of keeping commands arriving just in time ([Network
architecture](network-architecture.md#input)), with the clock's speed nudged. The Network page's Input timing line
shows it on the guest.

## Network ids {#net-ids}

Nothing crosses the wire as a pointer, a pool index or a script handle ([Network
architecture](network-architecture.md#net-ids)). Each replicated thing has a **`NetId`** (`src/net/net_id.h`): a
16-bit slot and a 6-bit generation, 22 bits on the wire; generation 0 is "no id", for a field that names nothing.

The server's **`NetIdAllocator`** gives the ids. One allocator serves every kind of thing in a session (65,536 slots
at most), so a snapshot names a thing by its id alone. A slot is free, live or retired:

- `allocate()` gives a new live id: the slots never used first, in order, then the released ones, oldest released
  first, so a slot rests as long as it can before it comes back. Its generation is one more than last time (63 wraps
  to 1, never 0).
- `retire(id)` marks the thing gone. Its slot is not free yet: a client may still name it in a command or hold it in
  an older snapshot.
- `release(id)` frees the slot once every client has acknowledged the removal. A repeated or stale acknowledgement
  is refused and changes nothing, since acknowledgements come from the network.

So an id is reused only with a new generation, and a late packet's old id never names the slot's new thing. The
allocator is plain data with no clock and no randomness: the same calls give the same ids.

Each side keeps a **`NetIdTable<Handle>`** (`src/net/net_id_table.h`) from ids to its own handles and back, one per
kind of handle: on the server the handles the game already has (a human's `Human*`, a brain's `Brain*`, a world
object's script handle), on a client the things it made for the server's ids. The server's `assign()` gives a thing
its id (allocating one the first time) and `retire()` takes it away. A thing two tables name (a human by its `Human*`
and its `Brain*`) gets one id through `assignShared()` and loses it through `retireShared()`, whichever table reaches
it first. A client's `bind()` refuses what a packet could
get wrong (no id, a slot bound in another generation that was not unbound first, a handle with an id already), and
every lookup by an id of another generation answers nothing. A table is walked in id order, never by handle value,
so every machine walks it the same way.

### Field tables {#field-tables}

Each kind of replicated thing (a human, a world object, a car) has a **field table** (`src/net/replicated_fields.h`):
its fields' names, encodings, bits and ranges, declared once so the snapshot encoder, its decoder and the tests read
one definition, as Quake 3's `netField_t` tables do. `writeField()` and `readField()` encode one field through the
[bit stream](#bit-stream); a kind's fields are indexed by its enum (`HumanField`, `WorldObjectField`, `CarField`).

| Encoding | On the wire | Round trip |
| --- | --- | --- |
| Bool | one bit | exact |
| Integer | the distance from the range's start, in `bits` bits | exact; a read over the range is refused (Invalid) |
| Float | quantized onto the range (`quantize()`), clamped | the ends exact, the rest within half a step |
| Angle | radians wrapped onto one turn, quantized to `bits` bits | within half a step around the circle; -pi and pi are one angle |

| Kind | Fields | Bits in full |
| --- | --- | --- |
| Human | position (3 x 18 bits, -1,024 to 1,024 m, under 8 mm a step), heading (10 bits), health and its maximum (16 bits each, -32,768 to 32,767: the original's s16 numbers, which a lethal hit takes below 0, [Characters](../research/characters.md)), four animation layers, as many as `AnimTaskStack` shows at once ([A human's state](snapshots.md#human-state)): each one's clip (12 bits), playhead (10 bits, a share of the clip) and gait value (10 bits, 0 to 4, for a walk or run blend), and each older layer's fade under the one above it (10 bits), then grounded, knocked out | 256 |
| World object | position, heading, door opening (10 bits), state (a byte), broken, hidden | 84 |
| Car | position, heading, speed (12 bits, -64 to 64 m/s) | 76 |

The ranges are Coney's choices. Four are stand-ins, marked in the code, for the game's side of snapshots to settle: the
positions' fixed bounds around the origin (in place of each level's own), the clip as an anim id under 4,096 (or the
game's own hash), the world object's state byte and the car's speed range. A snapshot sends a thing's changed fields
only, so a walking human's delta is 84 bits, under 11 bytes ([What a scene costs](snapshots.md#costs)), under the 15 to
25 bytes [the budget](network-architecture.md#snapshots) allows for it. `fieldWord()` and `fieldValue()` give a field's
bits as a number and back, which is what a snapshot compares and sends. `validateFields()` checks a table (named fields,
different names, bits 1 to 32, ranges that fit their bits), and the tests check every table with it.

## Testing {#testing}

Everything above `EnetTransport` is tested on the loopback, frame by frame with no real time, in `tests/net/`: the
bit stream's encodings and refusals, the loopback, the link conditioner's contracts and determinism, the protocol's
round trips, pinned bytes and refusals, link quality, and the sessions (handshake, slots, refusals, leaving, every
timeout, protocol errors, and the round trip and loss through a conditioned link). `tests/platform/
enet_transport_test.cpp` connects two ENet transports on a free port of 127.0.0.1, sends every delivery kind and a
fragmented packet, closes, checks that a transport kept going until `closed()` delivers its Bye before its close,
and runs the sessions' handshake over real UDP. `tests/platform/net_play_test.cpp` hosts
and joins between two `NetPlay`s on the loopback (by address, the figures, the simulated latency in the ping,
leaving and stopping, a leave over a slow link, a close that never completes, hosting again on a closing host's
port, shutting down, refusals, the guest's pad on the host's port 2 and the host's own port 2 around it, one remote
player at most); `tests/net/sessions_test.cpp` also covers the server's side of the pad (by slot, late, out of order
and repeated commands each played once on their own step, a lost packet's steps arriving in the next, a pad before
the handshake, letting go of a quiet pad, InputTiming and the client's clock following it);
`tests/net/input_buffer_test.cpp` covers the buffer's rules one by one; `tests/net/net_id_test.cpp` covers the
ids (reused only with a new generation, the order slots come back in, the generations' wrap, the wire form) and the
tables (a stale generation finds nothing, a client's refused binds, the walk in id order);
`tests/net/replicated_fields_test.cpp` round-trips every table's fields at their range's ends and middle and checks
the refusals and an infinite angle; `tests/net/snapshot_test.cpp` covers snapshots ([Snapshots](snapshots.md#testing));
`tests/net/protocol_test.cpp` pins
InputCommands' and InputTiming's bytes;
`tests/gamemodes/game_mode_stack_test.cpp` checks that `beforeStep` comes before the pads are read;
`tests/debug/net_page_test.cpp` drives the Network page over a fake
`NetControls`; and the `coney.hosts_headless` smoke test runs `coney --headless --frames 3 --host 127.0.0.1:0`.

## The plan {#plan}

Each step is a pull request that builds and is tested on its own.

1. **Foundation** (built): this page, ENet, `coney_net` and its tests.
2. **The Network page and flags** (built): [In the game](#in-the-game).
3. **A second player** (built): the guest's pad on pad port 2 of the host's game ([Player two](#player-two)).
4. **Input commands** (built): the client's pads tagged with the step, the last few repeated in every packet, the
   server's buffer that plays each on its step, and the timing that keeps them coming in time
   ([Input commands](#input-commands)).
5. **Replicated players:** network ids and the field tables (built: [Network ids](#net-ids)), snapshots (built:
   quantized, delta-compressed against acknowledged baselines, [Snapshots](snapshots.md)), the game's humans in them,
   the client's jitter buffer and interpolation, and its own follow camera.
6. **AI humans, events, mission state:** the rest of what a client must see and hear, events reliable.
7. **Prediction:** the local player predicted and corrected from the server's snapshots.
