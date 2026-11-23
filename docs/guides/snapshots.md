# Snapshots

How the server tells each client the state of the humans it needs and of each player's HUD: the snapshot, its packet,
the delta against an acknowledged baseline, the byte budget and the priority that fills it. It is part of [Network
multiplayer](multiplayer.md); the design it follows is [Snapshots](network-architecture.md#snapshots) in the
architecture. The original has no network play, so none of this code carries an `@orig` tag.

**Built:** `src/net/snapshot.h` (the server's `SnapshotSender` and the client's `SnapshotReceiver`), the Snapshot and
SnapshotAck messages of [the protocol](multiplayer.md#protocol) (the Snapshot's body in `src/net/snapshot_wire.cpp`,
apart from `protocol.cpp`), and the sessions' side: `NetServer::sendSnapshots()` and `NetClient::snapshots()`. A
client plays what it decodes through [the client's world](client-world.md). Each player slot's HUD state and the HUD's
standing state travel in the same snapshots ([HUD state](#hud-state), [The HUD's standing state](#hud-standing)), and a
guest's HUD takes them (the client game's, [The HUD](client-world.md#client-hud)). The host's game fills the humans
every step ([The game's humans](multiplayer.md#game-humans)), and a guest running [the client
game](client-world.md#client-game) draws them. **Planned:** interest (which humans a client needs, and how fast each
one's priority grows), and releasing a removed human's id once its removal is acknowledged. **Not in a snapshot, by
decision:** a camera. A client runs its own follow camera on the human its player slot follows, from his replicated
state ([The client's camera](multiplayer.md#client-camera-decision)), so the protocol version is unchanged by it; the
host's scripted and scene cameras would get an entry, and a bump, when built.

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

A human travels as its [field table](multiplayer.md#field-tables)'s 24 fields, each as its **word**: the bits
`fieldWord()` writes for it, so a delta compares quantized values and a change too small to reach the next step costs
nothing. The game hands the server a `SnapshotHuman` per human: its `NetId`, its words (`humanWords()` of its values)
and its priority rate. What a snapshot leaves a client holding is a map from id to words, walked in id order on every
machine.

The fields: position (3 × 18 bits), heading (10), health and its maximum (16 each, the original's s16), and four
animation layers: the newest's clip (12), playhead (10) and gait value (10), then for each of three older layers the
same three and how far the layer above it has faded in over it (10 bits; 1 with no such layer, whose clip, playhead
and gait value are then 0). Then grounded, knocked out and the hidden move style, a bit each, and [the
character](#character) (10 bits): 267 bits in all.

The **gait value** is for a layer that is the locomotion's walk or run, a `GaitBlendTask`: five clips (walk, jog, run,
sprint, sprint) on a value from 0 to 4, of which the two either side of the value play in step and blend by its
fraction. The clip field names only the leading one of the two, so without the value a guest could not tell a walk
from a walk easing into a jog. With it and the human's own gait clips (the style's slots, picked by the **move style**
below), the guest rebuilds the blend. It is 0 for any other task. To 10 bits it arrives within 0.002, so the sampler's
thresholds on the fraction (the lower clip alone below 0.01, the upper above 0.995) fall on the same side as on the
host.

The **move style** is one bit: set while the human is in the hidden style (`HumanAnimator::stealthStyle()`, the
original's style `0x14`, [Sneaking](../research/stealth.md#sneaking)), which swaps the walk of its gait blend for 633
`STEALTH_WALK`. The jog, run and sprint are in both styles' blends, so the clip field cannot say which blend a layer led
by one of them is; the style does ([A character's clips](client-world.md#clips)). The field is Coney's own and was
added in protocol 11 (#1613).

Four layers is the most `AnimTaskStack` shows at once. It holds up to 12 tasks (`kMaxTasks`), a fade
counting as one, but `change()` finishes the newest fade early once more than six are held (`kEarlyFinishTasks`), so
changes fading in over one another stack up to four layers and three fades, and never reach twelve. A static assert
ties `kHumanAnimLayers` to that rule, and a test drives the stack to its most and checks the table carries every layer.

## A human's character {#character}

A snapshot says which character each human is, as one more field of his state (`character`): the **Character List
index plus one** in 10 bits, 0 for none. The Character List of `warriors.glr` is the disc's own table of the characters'
models, textures and clips (543 records), and the original keeps a human's index in it at `+0x37c` (the model index,
[Characters](../research/characters.md), confirmed (code)). Coney sends that index as an id, never a pointer: a guest
turns it back into the character's model, textures and clips from its own disc ([A guest's
characters](client-world.md#characters)), so it needs no configuration scripts to know a character's model. The `+1` is
so that 0, a state's default, means "no character named" (`net::kNoCharacter`; `characterIdOf()` and
`characterListIndexOf()` in `src/net/character_id.h` convert). Ten bits are fixed by the original's data, with room
to spare.

The host's game says it: the play mode hands `GameHumans::capture()` a function that answers, for each human, the
character the mode draws him as (the player's current character, whatever the debug menus or a script's hand-over
made him; a cast human's model), as the Character List index its resources were found by. A human it cannot say
(a test's, or a character loaded without the list) has none.

It is a field like the others, so it is delta-encoded: it comes in full with a human new to the client (its changed bit
and 10 bits), costs one bit in an entry that is sent anyway, and is sent again, alone, only when a human becomes another
character. It joins the snapshot's budget as 11 bits on a human in full and 1 on a delta ([What a scene
costs](#costs)). The field is Coney's own and was added in protocol 12 (#1612).

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
(`NetPlay::arrivedHuds()`, `net::playerHudState()` of the words) and steps its own HUD from it, the one slot that is its
own as the HUD's panel 0 (`net::playerHudForSlot()`, [The client's HUD](client-world.md#client-hud)), so its panel
counts, fades and plays its cues as the host's does: the panel's own step is deterministic from these inputs, and a
loopback test steps a client's HUD from what it decodes and finds its panels the server's at every step.

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

The messages, objectives and hints come as [game events](multiplayer.md#events); the rest of what the HUD holds is its
standing state, next.

## The HUD's standing state {#hud-standing}

What the HUD holds between steps, set by the scripts' HUD bindings and the game rather than handed to each step:
`hud::HudStanding` (`src/hud/hud.h`), read with `Hud::standing()` and taken by a guest's HUD with
`Hud::applyStanding()` (`src/hud/hud_standing.cpp`).

| Part | Whose | What travels | Where the HUD keeps it |
| --- | --- | --- | --- |
| The panel's script flags | each player slot | attached and the banner the attach picked, shown, may show (`HidePlayerHud`), `ForceShowPlayerHud`, the rage bar's flash frames | [The player panel](../research/hud.md#the-player-panel), `PlayerPanel` `+0x40f8` to `+0x4114` |
| The action prompt | each player slot | its text, whether the icon cycle's start hid it, and the cycle's switch, timing and two icons | [Action prompts](../research/hud-messages.md#action-prompts), prompt `+0x440` to `+0x45c` |
| The radar's switch and tint | each player slot | on or off, grey or blue | [The radar](../research/hud-radar.md#the-radar-on-screen), radar `+0x04`, `+0x38` |
| The counter panels | shared | each panel taken: its kind, label, bar width, value, target and whether it shows | [Announcements and other messages](../research/hud-messages.md#announcements-and-other-messages) |
| The bars | shared | each generic bar that exists (shown, label, fill, gradient, colour, size) and the chase gauge (its icons, value and range) | [Scripted bars](../research/hud-messages.md#scripted-bars) |
| The scoreboard | shared | each row shown: its label, score and colour | `HUDEnableTextProgress`, widgets at `0x00615320` |
| The stopwatch | shared | while shown: its label and the mission stopwatch's time | `W_ShowStopWatch` |
| The HUD's settings | shared | the HUD shown (`HideHud`, `RestoreHud`), the prompt at the clubhouse's place, the radars' script switch, their range and zoom scale | HUD `+0x177a0`, `+0x177ac`, radar `+0x24`, `+0x28`, `+0x2920` |

**Whose.** The panel, the prompt and the radar are each player's, so they go per player slot. The counter panels, the
bars, the scoreboard, the stopwatch and the settings are one each on the original's screen, which both of its players
share (the HUD object at `0x00600840`, [The HUD object](../research/hud.md#hud-object)), so they travel once, for
every player. **Who owns it:** the play mode's HUD, as simulation state (`PlayLevelSimulation::hudStanding()`); after
the HUD's step the play mode hands it to network play with the slots' HUD state (`GameHumans::captureHuds(slots,
&standing)`, `NetPlay::setGameHuds()`), and a guest reads the newest decoded with `NetPlay::arrivedHudStanding()`.

**Derived on the guest, not carried.** The level's radar map comes from the level record the guest loads itself. The
running part of a prompt's icon cycle (its count, which icon shows, the blink) steps on each HUD from the cycle's start,
and `applyStanding()` starts a guest's cycle again only when its settings change, so it steps as the host's does. A
counter panel's hide time is the host's: its "shows" flag travels and a guest's panel hides when the host's does. The
scoreboard's row counts only sort the rows when a script sets a score, on the host.

**Not carried yet:** the radar's blips. They are a map keyed by script handle, each following an object a guest cannot
look up by that handle, so carrying them needs each blip's place or the network id of what it follows, which the game
does not give yet ([Game events](multiplayer.md#events)). They wait for #1537. The second radar is not
driven yet either ([HUD, menus and pause](network-architecture.md#sub-hud)).

**Elements.** Each part travels as an element named by a key (5 bits), with its own field table
([Field tables](multiplayer.md#field-tables)): 0 to 3 each player slot's (`HudPlayer`, 14 fields), 4 to 8 the counter
panels (`HudCounter`), 9 to 12 the generic bars (`HudBar`), 13 the chase gauge (`HudGauge`), 14 to 19 the scoreboard's
rows (`HudScoreRow`), 20 the stopwatch (`HudStopWatch`) and 21 the settings (`HudSettings`). A part that is not there
(a panel not taken, a bar that does not exist, a row not shown, the stopwatch hidden) has no element; the players' and
the settings' are always there. A script's number the HUD shows or compares (a counter's value, a bar's fill, a radius)
travels **Exact**, the float's own 32 bits, so a guest's HUD rounds the very value the host's does; a frame count, an
icon, a sprite word or a score gets the bits the HUD needs (16 or 24), and one beyond is held at the range's end.

**Texts.** Each prompt and each label travels as a **HUD text** named by its element's key: 1 to 255 bytes, sent only
when it differs from the baseline's, and removed when it empties or its element goes. A longer text is cut at 255 bytes.

**Deltas and the budget.** Elements are delta-compressed as the HUDs are: in full when the baseline does not hold
them, then their changed fields only (a counter counting is 5 + 1 + 5 + 32 bits, 6 bytes; a still HUD costs nothing),
and removed when gone. The elements go in the budget with the HUDs, and the sender keeps room for every element in
full and one 255-byte text. The texts go next, in key order, each where it fits (the room left always takes the
longest, so none waits for ever), then the humans' removals and changes. A first snapshot in a mission (two players,
two counter panels, two bars, the gauge, two rows, the stopwatch and their seven texts) takes about 190 bytes of the
1,200 before the humans.

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
| The number of HUD elements, at most 22 (protocol 9) | varint |
| Each HUD element, in rising key order: the key; a removed bit; when not removed, for each of its fields a changed bit and, when set, the field's word | 5 + 1 + its fields + the words |
| The number of HUD texts, at most 22 (protocol 9) | varint |
| Each HUD text, in rising key order: the key; a removed bit; when not removed, the text's length (1 to 255) and its bytes | 5 + 1 + varint + 8 a byte |

Message 11, SnapshotAck, client to server, Unreliable: the sequence (a varint).

A human the baseline does not hold comes with every field. **Decoding is strict**, since the bytes come from the
network: a sequence of 0, a baseline beyond the history, a count the packet cannot hold (checked before allocating),
ids not rising or not valid, a kept entry with nothing changed, a word over its field's range and trailing bytes are
refused, and the client closes the connection (protocol error). Decoding against the baseline refuses a removal of a
human it does not hold, a new human with a field left out, and a field "changed" to the word it has. The HUD entries
keep the same rules by slot, and more of them than there are player slots is refused before any is read. The HUD
elements keep them by key, and a key past the last or an Exact word that is not a finite number is refused; a HUD text
for a key with no text (the gauge's, the settings'), an empty one, one over 255 bytes, keys not rising, a removal of a
text not held and a text "changed" to the one held are refused, and more elements or texts than there are keys is
refused before any is read. The server closes a connection that acknowledges a snapshot it never sent.

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
| The first (full): 30 humans of 315 bits fit, the other 30 wait | 30 | 1,190 |
| The second: the 30 in full, and the one walker among the first 30 that the room left (x, heading and playhead: 86 bits) | 31 | 1,200 |
| Every one after: the 12 walkers | 12 | 137 |

137 bytes at 30 snapshots a second is about 4 KB/s per client, under the [architecture's
estimate](network-architecture.md#snapshots) of 340 bytes for a similar scene. Carrying four layers rather than two
cost a human in full 70 bits more and a walker's delta 6 (the older layers' changed bits); the four gait values cost
it 44 more and the walker 4; the move style, 2 more for a human in full (its changed bit and its word) and 1 for a
walker (its changed bit); the character, 11 more and the walker 1. A fight where 20 humans each move, turn and change
every animation field takes about 20 × 34 bytes, under 60 % of the budget.

## Testing {#testing}

`tests/net/snapshot_test.cpp` covers the full first snapshot and empty deltas, a delta of the changed fields only, a
removal and a slot's next generation, the baseline's choice (the newest acknowledged, full once it leaves the history),
the client's Stale, NoBaseline and refusals, the packet's pinned bytes and the decoder's refusals, a human in chained
fades with its four layers, a gait value on every layer, the budget and the priority accumulator (every packet within
1,200 bytes, nobody waiting more than seven snapshots, the longest wait being the first full send of 200 humans at 30 a
snapshot, a high-rate human in every one), the busy scene's bytes, the players' HUDs taking their room in a full budget
before the humans (both in the first snapshot, 29 humans beside them), and a round trip by hand through loss, held-back
and repeated snapshots and late acknowledgements, where every snapshot decoded leaves the client holding exactly the
server's record of it, and a human's character coming with the human, then costing a bit, then sent alone when it
changes. `tests/net/character_id_test.cpp` covers [the id](#character) both ways and the client's roster of characters.
`tests/net/hud_standing_test.cpp` covers [the HUD's standing state](#hud-standing): its
elements and texts and back (a number past its range held, a long text cut), the game's capture, the elements and
texts in full first, then their changes, a part gone removed and a delta against an older baseline, their pinned bytes,
the decoder's and the client's refusals, and the budget's order (elements with the HUDs, texts after them, the humans
last); `tests/hud/hud_test.cpp` reads a HUD's standing state from its parts and has a guest's HUD take it (a cycle
started again only when its settings change, a tint blending, a panel released, `HideHud`), and
`tests/net/replicated_fields_test.cpp` the Exact encoding. `tests/net/sessions_test.cpp` runs snapshots between a
`NetServer` and a `NetClient` on the
loopback through a link conditioner with loss, jitter, duplicates and reordering both ways, and drops a client that
acknowledges a snapshot never sent. `tests/net/player_huds_test.cpp` covers the [HUD state](#hud-state): a slot's state
through its words and back (a number past its range held at its end), the HUDs in full first, then their changed fields,
a slot removed and back, a delta against an older baseline, the HUD entries' pinned bytes and the decoder's and the
client's refusals, and the game's HUDs as network play captures them. `sessions_test.cpp` sends both players' HUDs every
step to a client on the loopback, which steps a HUD of its own from what it decodes: on a clean link its panels are the
server's at every step, and over a bad one what it holds is the server's record of each snapshot; it sends the HUD's
standing state the same way, and the client's HUD takes each snapshot's as the server sent it.
`tests/platform/net_play_test.cpp` hands a host's HUDs and standing state to its guest (`NetPlay::setGameHuds()`,
`arrivedHuds()`, `arrivedHudStanding()`), and
`tests/hud/hud_test.cpp` drives each panel from its own slot's state.
