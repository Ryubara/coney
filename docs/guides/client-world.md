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
| Take | `ClientWorld::receive()` pushes the newest decoded snapshot into the buffer, by its server step | `JitterBuffer::push()` |
| Step | `ClientWorld::step()` moves the buffer's clock on by one server step (or a little more or less) and samples it: the humans of the two snapshots either side of the playback time, blended | `JitterBuffer::advance()`, `sample()`, `blendHumans()` |
| Render | `ClientWorld::drawn(alpha)` blends the last two client steps with the frame's alpha | `Interpolated<T>`, `DrawnPoses<NetId>` |

So a client blends twice: between two snapshots, which may be steps apart (15 Hz snapshots, a lost one), and between
its own last two steps, which is the same blend an offline mode does for a display faster than 30 Hz ([Update and
render](conventions.md#update-and-render)). Both ends come from the jitter buffer instead of a simulation. All of it
is presentation: a client changes nothing the server reads.

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
| A layer's playhead | forward round the clip (`lerpLooping()`, a period of 1), when the layer plays the same clip at both ends |
| A layer's gait value and fade | lerped, under the same condition |
| Health, its maximum, the clips, grounded, knocked out | `to`'s |

That is the rule offline rendering keeps: what moves continuously is blended, the newest step's discrete state is drawn
as it is, and a jump is drawn where it lands.

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
plain loop, and a clip that is not loaded.

Its last case is the architecture's **What a client sees** test ([Testing](network-architecture.md#testing)): a
`NetServer` and a `NetClient` on the loopback, each behind a link conditioner with 20 ms latency, 15 ms jitter, 5 %
loss, 5 % duplicates and 10 % reordering, the server sending seven humans whose every field moves (walking, turning,
losing health, changing clips, a gait blend, a fading layer, landing, a human leaving and another coming). After three
seconds to settle, for 310 steps, whenever the client draws a step a snapshot shows, the humans it draws are the very
humans the server had at that step, every field's word equal (equal within quantization); the buffer is settled at
least 90 % of the steps, never jumps, and draws 1 to 5 steps behind the newest.
