# Snapshots

How the server tells each client the state of the humans it needs and of each player's HUD: the snapshot, its packet,
the delta against an acknowledged baseline, the byte budget and the priority that fills it. It is part of [Network
multiplayer](multiplayer.md); the design it follows is [Snapshots](network-architecture.md#snapshots) in the
architecture. The original has no network play, so none of this code carries an `@orig` tag.

**Built:** `src/net/snapshot.h` (the server's `SnapshotSender` and the client's `SnapshotReceiver`), the Snapshot and
SnapshotAck messages of [the protocol](multiplayer.md#protocol) (the Snapshot's body in `src/net/snapshot_wire.cpp`,
apart from `protocol.cpp`), and the sessions' side: `NetServer::sendSnapshots()` and `NetClient::snapshots()`. A
client plays what it decodes through [the client's world](client-world.md). Each player slot's HUD state travels in the
same snapshots ([HUD state](#hud-state)); nothing on a guest draws it yet. **Planned:** the game's side (filling the
humans from the host's game and drawing them on the guest), interest (which humans a client needs, and how fast each
one's priority grows), and releasing a removed human's id once its removal is acknowledged.

## The model {#model}

The architecture's [Snapshots](network-architecture.md#snapshots) designs it (Quake 3's model, 32 kept a side:
`kSnapshotHistory`); here, who does what:

| Step | Server (`SnapshotSender`) | Client (`SnapshotReceiver`) |
| --- | --- | --- |
| Each step, after the simulation | `build(step, humans)`: the delta, fitted to the budget, and its record of what the client will hold | |
| On receipt | | `apply(snapshot)`: decoded against its baseline when that is held, then `SnapshotAck` with its sequence |
| On the acknowledgement | `acknowledge(sequence)`: later snapshots take it as their baseline | |

A snapshot older than the newest decoded is ignored (Stale), and so is one whose baseline the client does not hold
(NoBaseline); the delivery is UnreliableSequenced, so a transport drops most stale ones before they arrive. The
acknowledgement is unreliable: a lost one is replaced by the next step's. A late one, older than the newest
acknowledged, changes nothing.

The receiver keeps each of the last `kSnapshotHistory` snapshots it decoded with the server's step it shows
(`holding(sequence)`, `heldStep(sequence)`), so a client's jitter buffer takes every one decoded since it last looked,
not only the newest ([The client's world](client-world.md#model)).

## A human's state {#human-state}

A human travels as its [field table](multiplayer.md#field-tables)'s 23 fields, each as its **word**: the bits
`fieldWord()` writes for it, so a delta compares quantized values and a change too small to reach the next step costs
nothing. The game hands the server a `SnapshotHuman` per human: its `NetId`, its words (`humanWords()` of its values)
and its priority rate. What a snapshot leaves a client holding is a map from id to words, walked in id order on every
machine.

The fields: position (3 × 18 bits), heading (10), health and its maximum (16 each, the original's s16), and four
animation layers: the newest's clip (12), playhead (10) and gait value (10), then for each of three older layers the
same three and how far the layer above it has faded in over it (10 bits; 1 with no such layer, whose clip, playhead
and gait value are then 0). Then grounded and knocked out, a bit each: 256 bits in all.

The **gait value** is for a layer that is the locomotion's walk or run, a `GaitBlendTask`: five clips (walk, jog, run,
sprint, sprint) on a value from 0 to 4, of which the two either side of the value play in step and blend by its
fraction. The clip field names only the leading one of the two, so without the value a guest could not tell a walk
from a walk easing into a jog. With it and the human's own gait clips (the style's slots, which the leading clip's id
shows), the guest rebuilds the blend. It is 0 for any other task. To 10 bits it arrives within 0.002, so the sampler's
thresholds on the fraction (the lower clip alone below 0.01, the upper above 0.995) fall on the same side as on the
host.

Four layers is the most `AnimTaskStack` shows at once. It holds up to 12 tasks (`kMaxTasks`), a fade
counting as one, but `change()` finishes the newest fade early once more than six are held (`kEarlyFinishTasks`), so
changes fading in over one another stack up to four layers and three fades, and never reach twelve. A static assert
ties `kHumanAnimLayers` to that rule, and a test drives the stack to its most and checks the table carries every layer.

## A player's HUD state {#hud-state}

Each player slot's HUD travels beside the humans, named by its slot (0 the host's player, 1 to 3 the remote ones), not
by a network id: it is the player's, not a thing in the world. What travels is `hud::PlayerHudState`
(`src/hud/hud.h`), what the HUD's step is given for that player's panel each step: his rage, its maximum and whether
he rages, the banner's blue-grey, his score, money and four item counts, whether SELECT shows his panel, whether he is
knocked out and whether his camera ignores the right stick. The original's panel reads these from the human, the stats
object and the inventory itself every frame ([The player panel](../research/hud.md#the-player-panel), confirmed
(code) at `0x00214138`; [Score and money](../research/hud.md#score-and-money), [Item
counters](../research/hud.md#item-counters)); Coney's play mode reads them into the slot's state, which builds the
HUD's frame (`hud::applyPlayerHud()`), so panel 0 is driven from slot 0's state and panel 1 from slot 1's.

**Who owns it.** The play mode, as simulation state: `PlayLevelSimulation::playerHuds()`, one per HUD panel, made
afresh each step for player 1's slot; slot 1 stays empty until a second player plays in a level. After the HUD's step
the play mode hands them to network play (`GameHumans::captureHuds()`, `NetPlay::setGameHuds()`), and the server sends
every slot's in each snapshot (`SnapshotSender::build(step, humans, huds)`). A guest holds the newest decoded
(`NetPlay::arrivedHuds()`, `net::playerHudState()` of the words) and can step its own HUD from it, so its panels count,
fade and play their cues as the host's do: the panel's own step is deterministic from these inputs, and a loopback
test steps a client's HUD from what it decodes and finds its panels the server's at every step.

**The fields** ([field table](multiplayer.md#field-tables), `HudField`): rage and its maximum (16 bits each, the s16
range of the original's meter and class), raging and the blue-grey banner (a bit each), the score (25 bits, past the
panel's seven digits), the money (16 bits, an s16), the four item counts (8 bits each, 0 to 255), SELECT shows, knocked
out and the camera's stick (a bit each): 110 bits in full. The money's and the counts' ranges are Coney's choices; the
panel clamps them tighter still (-99 to 9,999 and 9), and a value beyond a range is held at its end, so what a panel
shows is the same. The panel's `promptWakes` does not travel: the HUD works it out from the prompt.

**Deltas.** A HUD is delta-compressed against the same acknowledged baseline as the humans: a slot the baseline does
not hold comes in full, a kept one with its changed fields only (a money pickup is 2 + 1 + 13 + 16 bits, 4 bytes), an
unchanged one costs nothing, and a slot gone is removed. The HUDs go in the budget before the humans' changes, and the
sender keeps room for every slot's in full, so a panel is never a step behind for want of room.

**Not carried yet:** the HUD's standing state outside the panels' inputs: the prompts, the radar's blips and map, the
counter panels and bars, the scoreboard, a panel's script flags (attached, shown, `ForceShowPlayerHud`, the rage bar's
flash). A guest's HUD has its own of these until they are added, and the messages, objectives and hints come as
[game events](multiplayer.md#events).

## The packet {#packet}

Message 10, Snapshot, server to client, UnreliableSequenced:

| Field | Bits |
| --- | --- |
| Message id | 8 |
| Sequence: 1 for a client's first, one more each after | varint, 32 bits at most |
| How many snapshots back the baseline is: 0 for none, else 1 to 32 and no further back than the first | varint |
| The server's step it shows | varint, 32 bits at most |
| The number of entries | varint |
| Each entry, in rising id order: the `NetId`; a removed bit; when not removed, for each field a changed bit and, when set, the field's word | 22 + 1 + 23 + the words |
| The number of HUD entries, at most 4 (protocol 7) | varint |
| Each HUD entry, in rising slot order: the slot; a removed bit; when not removed, for each HUD field a changed bit and, when set, the field's word | 2 + 1 + 13 + the words |

Message 11, SnapshotAck, client to server, Unreliable: the sequence (a varint).

A human the baseline does not hold comes with every field. **Decoding is strict**, since the bytes come from the
network: a sequence of 0, a baseline beyond the history, a count the packet cannot hold (checked before allocating),
ids not rising or not valid, a kept entry with nothing changed, a word over its field's range and trailing bytes are
refused, and the client closes the connection (protocol error). Decoding against the baseline refuses a removal of a
human it does not hold, a new human with a field left out, and a field "changed" to the word it has. The HUD entries
keep the same rules by slot, and more of them than there are player slots is refused before any is read. The server
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
| The first (full): 31 humans of 302 bits fit, the other 29 wait | 31 | 1,177 |
| The second: the 29 in full, and the 7 walkers among the 31 (x, heading and playhead: 84 bits each) | 36 | 1,175 |
| Every one after: the 12 walkers | 12 | 132 |

132 bytes at 30 snapshots a second is about 4 KB/s per client, under the [architecture's
estimate](network-architecture.md#snapshots) of 340 bytes for a similar scene. Carrying four layers rather than two
cost a human in full 70 bits more and a walker's delta 6 (the older layers' changed bits); the four gait values cost
it 44 more and the walker 4. A fight where 20 humans each move, turn and change every animation field takes about
20 × 34 bytes, under 60 % of the budget.

## Testing {#testing}

`tests/net/snapshot_test.cpp` covers the full first snapshot and empty deltas, a delta of the changed fields only, a
removal and a slot's next generation, the baseline's choice (the newest acknowledged, full once it leaves the history),
the client's Stale, NoBaseline and refusals, the packet's pinned bytes and the decoder's refusals, a human in chained
fades with its four layers, a gait value on every layer, the budget and the priority accumulator (every packet within
1,200 bytes, nobody waiting more than seven snapshots, the longest wait being the first full send of 200 humans at 31 a
snapshot, a high-rate human in every one), the busy scene's bytes, and a round trip by hand through loss, held-back and
repeated snapshots and late acknowledgements, where every snapshot decoded leaves the client holding exactly the
server's record of it. `tests/net/sessions_test.cpp` runs snapshots between a `NetServer` and a `NetClient` on the
loopback through a link conditioner with loss, jitter, duplicates and reordering both ways, and drops a client that
acknowledges a snapshot never sent. `tests/net/player_huds_test.cpp` covers the [HUD state](#hud-state): a slot's state
through its words and back (a number past its range held at its end), the HUDs in full first, then their changed fields,
a slot removed and back, a delta against an older baseline, the HUD entries' pinned bytes and the decoder's and the
client's refusals, and the game's HUDs as network play captures them. `sessions_test.cpp` sends both players' HUDs
every step to a client on the loopback, which steps a HUD of its own from what it decodes: on a clean link its panels
are the server's at every step, and over a bad one what it holds is the server's record of each snapshot.
`tests/platform/net_play_test.cpp` hands a host's HUDs to its guest (`NetPlay::setGameHuds()`, `arrivedHuds()`), and
`tests/hud/hud_test.cpp` drives each panel from its own slot's state.
