# The client's world

How a client that runs no simulation draws the server's humans: the jitter buffer that holds the snapshots it has
decoded, the clock that plays them a little behind the newest, the blend between two snapshots and again between the
client's last two steps, and the pose evaluated from each human's replicated animation layers. It is part of [Network
multiplayer](multiplayer.md); the design it follows is [Interpolation](network-architecture.md#interpolation) in the
architecture, and what it draws comes from [snapshots](snapshots.md). The original has no network play, so none of
this code carries an `@orig` tag.

**Built:** `src/net/jitter_buffer.h` (`JitterBuffer`) and `src/net/client_world.h` (`ClientWorld`, the blend
`blendHumans()`, the pose `humanPose()` over a `ClipSource`), platform-neutral in `coney_net`. **Planned:** the game's
side, a client game mode that loads the host's level ([#557](issue:557)) and draws a `ClientWorld` through the
renderer, with the host filling its snapshots from its own humans; and the client's own player's view, whose camera the
snapshots do not carry.

## The model {#model}

Each client step, in the order the main loop calls them:

| Step | What | Where |
| --- | --- | --- |
| Receive | `NetClient::receive()` decodes the snapshots that arrived | [Snapshots](snapshots.md#model) |
| Take | `ClientWorld::receive()` pushes every snapshot decoded since its last call into the buffer, oldest first, by its server step (`SnapshotReceiver::heldStep()`) | `JitterBuffer::push()` |
| Step | `ClientWorld::step()` moves the buffer's clock on by one server step (or a little more or less) and samples it: the humans of the two snapshots either side of the playback time, blended | `JitterBuffer::advance()`, `sample()`, `blendHumans()` |
| Render | `ClientWorld::drawn(alpha)` blends the last two client steps with the frame's alpha | `Interpolated<T>`, `DrawnPoses<NetId>` |

So a client blends twice: between two snapshots, which may be steps apart (15 Hz snapshots, a lost one), and between
its own last two steps, where an offline mode blends its last two steps for a display faster than 30 Hz ([Update and
render](conventions.md#update-and-render)). Both ends come from the jitter buffer instead of a simulation, and both
blends are `blendHumans()`, which differs from an offline mode's blend as [Blending two humans](#blend) says. All of it
is presentation: a client changes nothing the server reads.

Every decoded snapshot goes in, not only the newest: `NetClient::receive()` decodes all that arrived since the last
step, and two arriving in one step is the jitter the buffer is there to absorb. Taking only the newest would blend
across a step whose snapshot the client had, and draw that step's health, clips and flags as the next one's. A
snapshot the receiver no longer keeps (more than 32, `kSnapshotHistory`, in one step) is not taken.

## The jitter buffer {#jitter-buffer}

`JitterBuffer` holds decoded snapshots by the server's step and plays them with a clock of its own, counted in
thousandths of a server step (`kTimeUnitsPerStep`), never in real time.

- **The delay.** The clock aims at the newest step less `JitterConfig::delaySteps`: 3 steps, 100 ms at 30 Hz, three
  snapshots at 30 Hz and two at 15 Hz, so one lost snapshot still leaves two to blend. It starts there at the first
  advance with a snapshot held.
- **Easing back.** A client step plays a server step while the clock is within the snapshots' interval and a step of
  its target (the interval is the steps between the last two snapshots pushed). Off by more, the clock plays 5 % more
  or less (`nudgePercent`) until it is within a nudge of the target, then steps back onto whole steps by at most a
  nudge a step, so the snapshots' own steps are drawn exactly again.
- **Jumping.** Off by more than `snapSteps` (15, half a second), the clock jumps to the target: a long stall, or a
  server that skipped time.
- **Starving.** The clock never passes the newest snapshot. With nothing newer it holds there (`starvedSteps()`) and
  the world stands still until the next arrives.
- **Gaps.** Step numbers need not follow one another: an overloaded server drops time ([Loop, time and
  input](network-architecture.md#sub-loop)), snapshots at 15 Hz are two steps apart, a lost one leaves a hole. The
  clock blends across a gap as across one step.
- **Settled.** `settled()` is true while the clock is not easing and is within the tolerance of its target: the world
  is drawn as far behind as asked.

`sample()` gives the newer end (the first snapshot at or after the playback time), the older end (the one before it)
and alpha, the share of the way from the older to the newer. At a snapshot's own step alpha is 1, so the step is drawn
exactly as decoded. The buffer keeps only the snapshot it blends from and those after it.

All of `JitterConfig`'s numbers are Coney's choices.

## Blending two humans {#blend}

`blendHumans(from, to, alpha)` draws a human between two snapshots, or two steps:

| Field | Blend |
| --- | --- |
| Position | lerped; a move longer than `DrawnPoses::kMostBlendedMetres` (10 m) is a jump, and the whole human is drawn as `to` |
| Heading | the short way round (`lerpAngle()`) |
| A layer's playhead | forward round the clip (`lerpLooping()`, a period of 1), when the layer is the same task at both ends |
| A layer's gait value and fade | lerped, under the same condition |
| A gait blend's clip | the clip of the pair the blended value plays: `to`'s if in `to`'s pair, else `from`'s |
| Health, its maximum, the other clips, grounded, knocked out | `to`'s |

The same task is the same clip, or one gait blend. What moves continuously is blended, the newer end's discrete state is
drawn as it is, and a jump is drawn where it lands.

**A gait blend's clip.** The clip a gait blend replicates is `GaitBlendTask::animId()`, its leading clip, which changes
while the value and phase run on: to the upper clip of its pair late in the pair, to the next pair's lower clip as the
value crosses a whole number. Only a gait blend carries a gait value above 0, so two layers with values above 0 are
taken as one blend, and the value and phase blend across the change. The clip named is one of the pair the blended value
plays, which is all [`humanPose()`](#pose) needs to rebuild the blend. These are drawn as a cut: a value that crossed
past both ends' pairs in one interval (more than a whole gait in two steps), a clip change with a value of exactly 0 at
one end (which carries 0, like any other task), and a gait blend whose clip changed while a new layer came on top of it
in the same interval.

**How it differs from offline rendering.** An offline mode keeps the two poses its last two steps sampled and draws
`blendPoses()` of them (`Player`, `TargetHuman`), so a new clip cross-fades with the old over one step. A client blends
the layers' parameters instead and draws a new clip unblended, as the architecture's
[Interpolation](network-architecture.md#interpolation) asks: between two snapshots there is no sampled pose to blend
(`ClientWorld` holds no clips; a renderer evaluates `humanPose()` of the drawn state), and the two ends may be steps
apart (15 Hz snapshots, a lost one), where a pose cross-fade would blend clips the server never blended. The
difference shows only at a cut, for one interval.

**Layers pushed down.** A clip that fades in becomes the newest layer and pushes the others down one. The blend finds
where `from`'s newest layer went among `to`'s playing layers, and blends each layer from there down with the one it was.
The layers above it are new, and so is the fade over it: both are `to`'s. When `from`'s newest layer is in none of
`to`'s (a cut, or a fade that ended), every layer is `to`'s.

A human the older snapshot does not hold is drawn as the newer has it; one the newer does not hold is not drawn.

## A human's pose {#pose}

`humanPose(human, clips, defaultRotations)` evaluates a human's skeleton from its replicated layers as its
`AnimTaskStack` sampled it on the server ([The task stack](../research/formats/animation.md#task-stack)):

1. **The layers that play:** the newest, and each older one while the layer above it has not wholly faded in over it
   (`playingLayers()`: a fade under 1).
2. **Each layer at its playhead.** A layer whose clip is one of a gait set (`ClipSource::gaitClips()`) and is one of the
   pair its gait value plays (the lower clip, or the upper one while the value is between whole numbers) is rebuilt as a
   `GaitBlendTask` from its five clips, value and phase. Any other is its clip sampled at the playhead times its
   duration. A layer whose clip the client does not have loaded is left out.
3. **From the oldest up**, each newer layer over what is under it: `blendPoses(newer, older, 1 - fade)`, the stack's
   outgoing weight.

The **fade** a human's snapshot carries for a layer is therefore the incoming weight of the layer above it,
`1 - AnimTaskStack::outgoingWeight(elapsed, duration)`: what the host's game writes for each older layer. Root motion is
the server's (the client is given positions), so the task flags that only change it are not carried.

`ClipSource` is how a client finds the clips the snapshots name, the same disc clips as the server's, by anim id:
`clip(animId)` and the gait set a clip is in.

## Testing {#testing}

`tests/net/jitter_buffer_test.cpp` covers the clock: its start the delay behind the newest and a whole step each client
step, the blend between 15 Hz snapshots and across a lost one, slowing for late snapshots and hurrying for early ones
and coming back onto the delay and onto whole steps, holding when starved and jumping when far off.

`tests/net/client_world_test.cpp` covers the blend (position, heading the short way, a playhead wrapping round its
clip, the exact ends), jumps, cuts and a layer pushed down by a new one, the words' round trip, a client's steps through
the buffer with a new human and the blend between two steps through `DrawnPoses`, and a pose evaluated from layers
against `AnimTaskStack::sample()` itself: two layers in a fade, a gait blend between two clips, a gait clip played as a
plain loop, and a clip that is not loaded. It also covers a gait blend crossing from the walk's pair into the jog's
and back, and two snapshots decoded in one step both reaching the buffer.

Its last case is the architecture's **What a client sees** test ([Testing](network-architecture.md#testing)): a
`NetServer` and a `NetClient` on the loopback, each behind a link conditioner with 20 ms latency, 15 ms jitter, 5 %
loss, 5 % duplicates and 10 % reordering, the server sending seven humans whose every field moves (walking, turning,
losing health, changing clips, a gait blend, a fading layer, landing, a human leaving and another coming). After three
seconds to settle, for 310 steps, whenever the client draws a step a snapshot shows, the humans it draws are the very
humans the server had at that step, every field's word equal (equal within quantization). Every snapshot the client
decoded must reach the buffer: each such step is blended to or from, and drawn from its own snapshot when the clock is
on it. Every step drawn is compared but those whose snapshot the client never decoded (lost, or stale after
reordering): 287 of the 310 on its seeded link, measured. The buffer is settled at least 90 % of the steps, never
jumps, and at every step draws 1 to 5 steps behind the newest (1 to 3, measured).
