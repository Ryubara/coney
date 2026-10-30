# Snapshots

How the server tells each client the state of the humans it needs: the snapshot, its packet, the delta against an
acknowledged baseline, the byte budget and the priority that fills it. It is part of [Network
multiplayer](multiplayer.md); the design it follows is [Snapshots](network-architecture.md#snapshots) in the
architecture. The original has no network play, so none of this code carries an `@orig` tag.

**Built:** `src/net/snapshot.h` (the server's `SnapshotSender` and the client's `SnapshotReceiver`), the Snapshot and
SnapshotAck messages of [the protocol](multiplayer.md#protocol), and the sessions' side: `NetServer::sendSnapshots()`
and `NetClient::snapshots()`. **Not yet:** the game's side (filling the humans from the host's game and drawing them on
the guest), the jitter buffer and interpolation, interest (which humans a client needs, and how fast each one's
priority grows), and releasing a removed human's id once its removal is acknowledged.

## The model {#model}

Quake 3's: the server keeps, for each client, the last 32 snapshots it sent (`kSnapshotHistory`) and what each one
leaves the client holding. Each new snapshot is a delta against the newest one that client has acknowledged, or full
when it has acknowledged none that is still kept. The client keeps the last 32 it decoded, decodes each new one against
its baseline, and acknowledges it. A lost snapshot costs nothing to repair: the next one is a delta against an older
baseline, and holds every change since.

| Step | Server (`SnapshotSender`) | Client (`SnapshotReceiver`) |
| --- | --- | --- |
| Each step, after the simulation | `build(step, humans)`: the delta, fitted to the budget, and its record of what the client will hold | |
| On receipt | | `apply(snapshot)`: decoded against its baseline when that is held, then `SnapshotAck` with its sequence |
| On the acknowledgement | `acknowledge(sequence)`: later snapshots take it as their baseline | |

A snapshot older than the newest decoded is ignored (Stale), and so is one whose baseline the client does not hold
(NoBaseline); the delivery is UnreliableSequenced, so a transport drops most stale ones before they arrive. The
acknowledgement is unreliable: a lost one is replaced by the next step's. A late one, older than the newest
acknowledged, changes nothing.

## A human's state {#human-state}

A human travels as its [field table](multiplayer.md#field-tables)'s 13 fields, each as its **word**: the bits
`fieldWord()` writes for it, so a delta compares quantized values and a change too small to reach the next step costs
nothing. The game hands the server a `SnapshotHuman` per human: its `NetId`, its words (`humanWords()` of its values)
and its priority rate. What a snapshot leaves a client holding is a map from id to words, walked in id order on every
machine.

The fields: position (3 × 18 bits), heading (10), health and its maximum (16 each, the original's s16), and two
animation layers: the clip playing and its playhead, the clip it fades in over and that one's playhead, and how far
the fade has gone (10 bits; 1 with no fade). Of the up to 12 layers `AnimTaskStack` holds, only a fade shows two at
once. Then grounded and knocked out, a bit each: 152 bits in all.

## The packet {#packet}

Message 10, Snapshot, server to client, UnreliableSequenced:

| Field | Bits |
| --- | --- |
| Message id | 8 |
| Sequence: 1 for a client's first, one more each after | varint, 32 bits at most |
| How many snapshots back the baseline is: 0 for none, else 1 to 32 and no further back than the first | varint |
| The server's step it shows | varint, 32 bits at most |
| The number of entries | varint |
| Each entry, in rising id order: the `NetId`; a removed bit; when not removed, for each field a changed bit and, when set, the field's word | 22 + 1 + 13 + the words |

Message 11, SnapshotAck, client to server, Unreliable: the sequence (a varint).

A human the baseline does not hold comes with every field. **Decoding is strict**, since the bytes come from the
network: a sequence of 0, a baseline beyond the history, a count the packet cannot hold (checked before allocating),
ids not rising or not valid, a kept entry with nothing changed, a word over its field's range and trailing bytes are
refused, and the client closes the connection (protocol error). Decoding against the baseline refuses a removal of a
human it does not hold, a new human with a field left out, and a field "changed" to the word it has. The server
closes a connection that acknowledges a snapshot it never sent.

## The budget and the priority accumulator {#budget}

A snapshot's packet takes at most 1,200 bytes (`kSnapshotBudgetBytes`, `ServerConfig::snapshotBudgetBytes`), one
datagram below every common path's MTU. `build()` fills it:

1. **Removals first**, each 23 bits: a client must not keep a human that has gone.
2. **Then the changes, by priority.** Every human with a change against the baseline adds its rate
   (`SnapshotHuman::priority`, 1 for most) to its priority; the highest go first, equal ones in id order. A change that
   does not fit waits and a smaller one after it may still go.
3. A human sent has its priority back at 0; one with nothing to send has none.

A human left out stays as the baseline has it (not at all, when new) and goes in a later snapshot with a higher
priority, so far or quiet humans update less often and nobody waits for ever. The rates are the place for
[interest](network-architecture.md#interest): near the player, on screen or fighting him, a human's grows faster.

A cost of fitting a budget against acknowledged baselines: a human sent in a snapshot the client has decoded but the
server has not yet seen acknowledged is, when left out of the next one, as the older baseline has it. The client then
shows it a step back for a snapshot. It happens only while a step's changes are over the budget, and the next snapshot
that carries it puts it right.

## What a scene costs {#costs}

Pinned by a test (`snapshots: a busy scene's bytes per snapshot are pinned`): sixty humans in a row, every fifth
walking.

| Snapshot | Entries | Bytes |
| --- | --- | --- |
| The first (full): 50 humans of 188 bits fit, the other 10 wait | 50 | 1,180 |
| The second: the 10 in full, and the 10 walkers among the 50 (x, heading and playhead: 74 bits each) | 20 | 333 |
| Every one after: the 12 walkers | 12 | 116 |

116 bytes at 30 snapshots a second is about 3.5 KB/s per client, under the [architecture's
estimate](network-architecture.md#snapshots) of 340 bytes for a similar scene.

## Testing {#testing}

`tests/net/snapshot_test.cpp` covers the full first snapshot and empty deltas, a delta of the changed fields only, a
removal and a slot's next generation, the baseline's choice (the newest acknowledged, full once it leaves the history),
the client's Stale, NoBaseline and refusals, the packet's pinned bytes and the decoder's refusals, the budget and the
priority accumulator (every packet within 1,200 bytes, nobody waiting more than four snapshots, a high-rate human in
every one), the busy scene's bytes, and a round trip by hand through loss, held-back and repeated snapshots and late
acknowledgements, where every snapshot decoded leaves the client holding exactly the server's record of it.
`tests/net/sessions_test.cpp` runs snapshots between a `NetServer` and a `NetClient` on the loopback through a link
conditioner with loss, jitter, duplicates and reordering both ways, and drops a client that acknowledges a snapshot
never sent.
