# The client's world

How a client that runs no simulation draws the server's humans: the jitter buffer that holds the snapshots it has
decoded, the clock that plays them a little behind the newest, the blend between two snapshots and again between the
client's last two steps, and the pose evaluated from each human's replicated animation layers. It is part of [Network
multiplayer](multiplayer.md); the design it follows is [Interpolation](network-architecture.md#interpolation) in the
architecture, and what it draws comes from [snapshots](snapshots.md). The original has no network play, so none of
this code carries an `@orig` tag.

**Built:** `src/net/jitter_buffer.h` (`JitterBuffer`) and `src/net/client_world.h` (`ClientWorld`, the blend
`blendHumans()`, the pose `humanPose()` over a `ClipSource`) and `src/net/character_clips.h` (`CharacterClips`, a
character's clips as a `ClipSource`), platform-neutral in `coney_net`; and the game's side, [the client
game](#client-game) (`--join ADDR --net-view`): `src/platform/client_game.h` (`ClientGame`), which follows the host's
levels, steps a `ClientWorld` from the snapshots the session decodes, presents the host's game events and steps the
guest's HUD, `src/gamemodes/client_event_player.h` (`ClientEventPlayer`), which applies an event to the guest's sound
and HUD, and `src/platform/client_level_scene.h` (`ClientLevelScene`), which draws the level, the world's humans (each
as the character his snapshot names) and the HUD through the renderer, and the guest's own camera, `src/net/client_camera.h`
(`ClientCamera`, [The client's camera](#client-camera)). The host fills its snapshots from its own humans
every step ([The game's humans](multiplayer.md#game-humans)). **Not built yet:** the human each player slot
drives, which the snapshots do not say yet (the camera follows the host's player 1 until then, [The client's
camera](#client-camera)).

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
| Health, its maximum, the other clips, grounded, knocked out, the move style, the character | `to`'s |

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

## A character's clips {#clips}

`CharacterClips` is the `ClipSource` of one character: its `AnimSet` (its own character data, then the generic
defaults) by anim id, and the gait blends its locomotion plays, built from the anim slots as `HumanAnimator::gaitBlend()`
builds them: the walk, jog, run, sprint and sprint slots (408 to 411 for the player's slots, `AnimSlots::player()`), and
the hidden style's blend, whose walk is 633 `STEALTH_WALK`. A blend is answered only when the set has all five clips.

The jog, run and sprint are in both styles' blends, so `gaitClips(animId, style)` answers them with the blend of the
**move style** the human's snapshot carries (`HumanState::moveStyle`, [a human's state](snapshots.md#human-state)),
as `HumanAnimator::gaitBlend()` builds it from the human's current slots. A style's own walk leads that style's blend
whichever style is asked: when a human changes style, the layer of its old style's blend fades out under the new one's,
and its walk (408 or 633) still names the blend it belongs to. A fading layer led by the jog, run or sprint cannot say
which style built it, so it takes the new style's blend for the length of the fade.

## A guest's characters {#characters}

A snapshot names each human's character as an id, a Character List index plus one ([A human's
character](snapshots.md#character)). The client game keeps a `net::CharacterRoster` of the characters it draws with: a
human whose id names a character gets his own model, textures and clips (`DrawnCharacter`: a `PlayerCharacter` loaded
by index from the guest's own disc with `PlayerCharacter::loadAt()`, its textures and its [clips](#clips)), loaded the
first time a human of it is drawn and kept, so a crowd of one character loads it once. A human who becomes another
character (his id changes in a snapshot) has his mesh made afresh from that snapshot on.

**Coney's choice:** a human whose snapshot names no character (id 0), or whose character does not load on the guest's
disc, is drawn as Rembrandt (`human::kPlayerModel`), as the play mode draws a cast human whose model fails to load;
the failure is logged once and remembered, not tried again each frame. The roster is a template over what a client
draws with, in `src/net/character_roster.h`, so its choice is tested without a disc; `ClientLevelScene` instantiates it
with `DrawnCharacter`.

## The client game {#client-game}

`--join ADDR --net-view` starts a guest that runs **no game of its own**: no front end, no scripts, no simulation. Its
one mode is `ClientGame`, and what it shows is the host's game as the snapshots carry it. The guest's pad still plays the
host's port 2 ([Player two](multiplayer.md#player-two)), and the client game sends nothing the host's game reads beyond
that pad (it also says it is in the level and answers each movie as ended), so the host's game reads what it reads with
any guest.

| When | What the client game does |
| --- | --- |
| The host's level order arrives | `NetLevels` follows it as for any guest's game ([Levels](multiplayer.md#levels)), with `ClientGame` as its `LevelGame`. The level loads at the next step (`ClientLevelScene::load()`), counts as in play at once, and the host is told the step after |
| A level loads | A fresh `ClientWorld`, which skips what the receiver still keeps of the last level (`skipDecoded()`), so no human of the last level is drawn in the new one, and a fresh HUD, so none of the last level's messages show; the host's events its playback had not reached are dropped with the old level (and any that arrive while there is no level), so none of them is presented in the new one |
| Each step, in a level | The world takes every snapshot decoded and steps ([The model](#model)); the host's events its playback has reached are presented ([Events](#client-events)); the HUD steps ([The HUD](#client-hud)); then the scene streams round its camera |
| Each frame | The scene draws the world, `drawn(alpha)` of its last two steps, with the HUD over it |
| A host's movie starts | It plays none, and answers each one as ended at once (`NetPlay::movieDone()`, not skipped), so the host waits only for its own copy |
| The host quits, or the session ends | It leaves the level, stops the sounds the host started in it and clears the screen, as it does before the host's first order |

A level the guest's disc has no `.lev` file for is refused; NetLevels asks again each step and notes the refusal once
in the log.

**The scene.** `ClientLevelScene` loads the level's scenery as the play mode does (`LevelPlayScenery`), lights it with
the play mode's stand-in lights, and draws each human of the world as a character mesh posed by
[`humanPose()`](#pose) over its character's [clips](#clips), at its feet and heading, each human as [his own
character](#characters). It draws through the client game's own [camera](#client-camera) (`ClientCamera::view()` at
the frame's alpha; the newest step's view for streaming): the scenery streams round the camera, and the lights and the
visibility pass are placed by it, round the human it follows. With no human to follow (the world has none yet) the
camera has no view, and the scene stands at the level's start,
4 m behind it and 2 m above (**Coney's choice**).

### The client's camera {#client-camera}

A client's view is **its own follow camera**, `camera::FollowCamera` (the original's `Cam_Follow`,
[Cameras](../research/camera.md)), run by the client on the human its player slot drives. The snapshots carry no
camera, only which human each player slot drives ([below](#client-camera-human)); why is [The client's
camera](multiplayer.md#client-camera-decision) in the
multiplayer design. What a snapshot cannot give a client rebuilt, and so still waits for a camera entry of its own, is
the host's scripted, scene and fixed cameras: **while the host plays a scene, a guest keeps its follow shot**
(**stand-in**, below).

`ClientCamera` (`src/net/client_camera.h`, in `coney_net`, no display, clock or random) is stepped by the client game
once per client step, **after** the world's step and with what the world shows, so the camera follows the jitter
buffer's playback exactly as the humans are drawn, not the newest snapshot (a camera on the newest would swing ahead of
the humans a blend of 3 steps behind). It keeps its last two views for the frames between, as the play mode keeps its
player's (`view(alpha)`: the eye and look-at point lerped, the ends exact), and a human it follows anew gets a camera
made behind him at once (a cut: `view(0)` is `view(1)`, no swing across the level; counted by `cuts()`). The inputs of
a step:

| Input | From | Why |
| --- | --- | --- |
| The human followed | `followedHuman(humans, huds, slot)` over the world's humans and the newest snapshot's HUD entries | the human the player slot drives ([below](#client-camera-human)) |
| His gait speeds | `ClientScene::speedsOf(character)`, `human::speedsOf()` over the character's clips (the roster's character, [A guest's characters](#characters)) | the gait is read from the speed, not replicated (the replicated `Gait` is a blend's value, not the locomotion's gait) |
| The right stick | the guest's own pad on port 1 (`GameModeStack::pads()`, raw bytes) | the view turns with the guest's own right stick; the host's game never reads it (the guest's pad is what plays the host's port 2, [Player two](multiplayer.md#player-two), so the same stick also goes to the host as its player 2's input, as any second pad's does) |
| The collision | `ClientScene::collision()`, the level's | the pull-in when the world is in the way |
| The step | the client step's length, 1/30 s | the camera's clock is the sum of its updates |

#### The human followed {#client-camera-human}

Each player slot's HUD entry in the snapshot carries the human the slot drives, as a network id (protocol 13, #1654;
[HUD state](snapshots.md#hud-state)). The host fills it from the game mode's player slots
(`PlayLevelSimulation::playerHumans()`, resolved to the ids the humans have in the same step's capture,
`GameHumans::captureHuds()`), so a client names no pointer and no pool index. `followedHuman()` reads it for the
client's own slot and answers the human of that id **among the humans the world shows**:

- **A named human the world does not show yet is waited for.** The HUD entries are the newest snapshot's and the
  humans the jitter buffer's playback, a few steps behind, so a human new to the level is named before he is shown. The
  camera then has no human for those steps, rather than following another and cutting back, and follows him once he is
  shown (the id of another generation of his slot is not him).
- **A slot that drives no human follows the host's player 1** (**Coney's choice**, until the second player is built):
  the host's game has one player so far, the guest's pad plays port 2 and drives no human of its own ([Player
  two](multiplayer.md#player-two); `GameState_SyncPlayers` is not built), so the guest's slot has an entry that names
  none, or none at all, and the guest watches the human with the lowest network id, who is the host's player 1 (his
  brain comes first in the host's capture). A client with a slot that drives a human never does. Without this
  a guest's view would be empty until the host has a second player; the rule ends when the game gives the guest's slot
  a human of its own, and then the same code follows him.
- **A slot's human changing** (made afresh, or given to another) cuts the camera to him once, as any new human does.

#### Stand-ins {#client-camera-stand-ins}

Each is to be replaced by a faithful reading when the snapshots carry the state ([Stand-ins](testing.md#stand-ins)):

- **The camera's set-up** is `followDefaults()`: a level's `CfgFollowCamera` and the scripts' camera calls run on the
  host only, so a level that sets its own band (the street's, [Cameras](../research/camera.md#level99-values)) is
  followed with the default one.
- **The target's other state**: no nearest enemy, no lock-on (L1 combat camera), no grab, the stick-back rule off, the
  body point 1 m up (the aim point stays level), no long fall. The follow shot therefore differs from the host's own
  camera for the same human in a fight and in a sprint near enemies; only the framing, never what the host reads.
- **Scenes and scripted cameras**: the host's scene, scripted and fixed cameras are not replicated, so while the host
  plays a scene (its roles are humans in the snapshots like any other) the guest's camera keeps its follow shot, and the
  scene's shots are not seen. The movie, which the client game does not play either, is the host's alone.
- **Slow motion** is not applied: the camera steps 1/30 s whatever the host's characters' step is.

### The host's events {#client-events}

Each step, once the world's playback has reached a host step (`ClientWorld::shownTime()`, the newest snapshot's step
less the jitter buffer's delay), the client game takes the events of the host's steps up to it from the session
(`NetPlay::takeArrivedEvents()`: each once, in step order and within a step in the order the host raised them, whichever
delivery brought it, [Game events](multiplayer.md#events)) and hands each to `ClientEventPlayer`, so an event is
presented when the world it belongs to is drawn, never before and never twice; the events of a level that has ended and
the playback had not reached are dropped with it (`NetPlay::discardArrivedEvents()`). The player applies what a guest can
show or play on this machine, to its sound and its HUD:

| Events | Presented as |
| --- | --- |
| Sound2D, Sound3D, SoundStop, SoundMove | The guest's own sound plays, stops and moves each sound by the host's instance (`SoundEventPlayer`) |
| MusicStart, MusicStop, LoopStart, LoopStop | The track (looped, or the system music fading in over its bars) and the ambient loop on the guest's sound |
| HudMessage, Objective, Hint | `Hud::setAnnouncement()`, `setObjective()` and the hint box's queue, clear and flush, and the game's own hints on or off, as the HUD's bindings do on the host |
| Cue (2D) | The HUD's interface cue |

The kinds it cannot present yet are counted (`ClientGame::eventsUnpresented()`) and nothing else: a human's sound, line
or command (HumanSound, Hit, Speech, Command, SpeechStop: the human is named by a network id, and his character,
now in the snapshots (#1612), is not yet turned into his sounds on the guest), a particle start and an object breaking
(the scene has no particle systems or world-object effects yet), a material pair, a cue a world object raises at a
place, and a scene or a movie (the client game plays none).
Presentation only: the host's own events were taken and sent as before.

**Stand-in: no sound set-up.** The host's sounds name banks, interface cues and material tables that the level's
scripts load, and the client game runs no script, so on the guest a sound whose bank is not loaded plays nothing and
the HUD's interface cues have no names (`HudSound::cueName` is empty). The events and the code that plays them are
there; hearing them waits for the guest to load the level's sound set-up (the banks, the interface table and the
sound matrix), which the host would have to send or the guest read from the level's scripts.

### The HUD {#client-hud}

Each level has its own `hud::HudOwner`, made when the level loads with the services the run gives (the strings from
the disc and the sound output), so a level's messages are gone with it. Each step the client game gives it, from the
newest decoded snapshot, what the host's HUD held for the guest's own player slot (`NetPlay::playerSlot()`: 1 for the
first guest, the host being 0): the slot's panel state (`net::playerHudForSlot()`, [HUD state](snapshots.md#hud-state))
and the HUD's standing state with that slot's part as the HUD's one player (`net::standingForSlot()`,
[Standing state](snapshots.md#hud-standing)), and steps it (`Hud::applyStanding()`, `Hud::update()`). The guest is one
player, so his panel is panel 0 and the host's other players' panels are not drawn; what the host's HUD holds for
everyone (its counters and bars, the scoreboard, the radar's settings, whether the HUD shows) is the host's as it is.
`ClientLevelScene` draws the HUD over the frame through a `HudLayer`, as the play mode does.

One thing the HUD does not have, by choice: the HUD takes the newest snapshot's state, not the playback's (**Coney's
choice**: it keeps no history to blend, so it is up to the jitter buffer's delay ahead of the humans, while its messages
and sounds arrive with the humans).

**The radar.** The blips are the host's ([The radar's blips](snapshots.md#hud-blips)), taken with the standing state and
keyed by their slots. Its view (where the player stands and which way he faces, how fast he moved over the last step and
which way the camera looks) is the human the [client's camera](#client-camera) follows, as the world shows him now, and
the guest's own camera (`ClientGame::radarView()`); with none to follow the radar draws nothing. A blip that follows a
human is found through the world (`Hud::setRadarFollowLocator()`), by the network id the snapshot names, so it moves as
the human is drawn; one with a place is where the snapshot puts it. An objective's up or down arrow and its colour are
the host's, worked out from the host player's height ([The radar's blips](snapshots.md#hud-blips)).

**The radar's map** is the guest's own: nothing of it travels. The original's `Radar_Setup` names the map after the
level record's world and draws it with the record's three map floats ([The radar on
screen](../research/hud-radar.md#the-radar-on-screen)); the records are the level table the preloads fill (`CfgLevels`
in `config_preload3.lua`), which the guest, running no preloads, reads from its own disc once at start-up, as the HUD's
strings are read with `loadGlobalStrings()`, and hands the client game in its `ClientPresentation`. `loadLevelTable()`
runs all four preloads in the legal screen's order (`enum_preload.lua`, `config_preload.lua`, `config_preload2.lua`,
`config_preload3.lua`) with only four bindings: the real `CfgLevelName`, and `doFile`, `GetLanguage` and `GetPlatform`,
which `config_preload2.lua` needs to build the tables `config_preload3.lua`'s objects read; every other call is
skipped. It fails only when `config_preload3.lua` is missing or does not run; an earlier preload that fails is named in
the run log as a script error (`script error: level table: ...`), and nothing else the read does is logged: the
bindings it skips are left out on purpose, and what the scripts print is the game's text. Each level's HUD is given
the map of the host's level's record when the level loads (`radarMapOf()`, the same function `GameplayMode` gives its
own HUD the map with), so the guest draws the disc the host draws. A level its table has no record of has
no map, and its radar draws the blips and the arrow with no disc; that is a mod's level, which the host numbers at run
time, and whose record on the host has no map floats, so the host draws no disc for it either.

## Testing {#testing}

`tests/net/jitter_buffer_test.cpp` covers the clock: its start the delay behind the newest and a whole step each client
step, the blend between 15 Hz snapshots and across a lost one, slowing for late snapshots and hurrying for early ones
and coming back onto the delay and onto whole steps, holding when starved and jumping when far off.

`tests/net/client_world_test.cpp` covers the blend (position, heading the short way, a playhead wrapping round its
clip, the exact ends), jumps, cuts and a layer pushed down by a new one, the words' round trip, a client's steps through
the buffer with a new human and the blend between two steps through `DrawnPoses`, and a pose evaluated from layers
against `AnimTaskStack::sample()` itself: two layers in a fade, a gait blend between two clips, a gait clip played as a
plain loop, and a clip that is not loaded. It also covers a gait blend crossing from the walk's pair into the jog's
and back, two snapshots decoded in one step both reaching the buffer, and a world told to skip what was decoded taking
only the snapshots after, and a human's character reaching the drawn human. `tests/net/character_id_test.cpp` covers
the id's mapping to a list index and [the roster](#characters): none and out-of-range ids the fallback, a character
loaded once and shared, a failed load said once. `tests/net/character_clips_test.cpp` covers [a character's
clips](#clips) on synthetic ones: a clip by its id, each style's gait blend and the blend each style answers for the
jog, run and sprint, a set missing one of a blend's clips, and a jog-to-run layer posed as the server's gait blend
samples it. The move style is pinned where it is carried: in the words' round trip and the pose of a hidden human's
gait blend (`client_world_test.cpp`), in `replicatedState()` for a human that hides (`game_humans_test.cpp`), and in
the human field table's 267 bits.

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

`tests/gamemodes/client_event_player_test.cpp` covers [the host's events](#client-events) on a recording sound and a HUD:
the music and the loop, a sound's start and stop by instance, the messages, objectives and hints, a cue, and the kinds
it counts and does not play.
`tests/platform/client_game_test.cpp` runs [the client game](#client-game) against a host, two `NetPlay`s on the
loopback in fixed steps with no display (a fake scene stands for the drawing): the host's level loads once and the
host stops holding; settled, for 200 steps, every step the client draws, its humans are the host's of that step, every
field's word equal, and it never draws more than the buffer's delay and one step behind the newest the host sent, and
at its most behind draws at least the delay behind it (the test bounds both, not an exact lag). It also covers a new
level's world starting with none of the last level's humans, a host's movie that waits for no copy on the guest, a quit
leaving the level, and a level the disc lacks refused. Two peers also pin the events: a sound, a hint, a message and a
track the host raises in one step are presented on the guest once, none before its playback has reached that step and
all in the step it does, in the order raised, on the guest's own sound and HUD; and the HUD: the guest's own slot's
panel is panel 0 with its rage and money and its standing flags, the host's player's values are on no panel, what the
host's HUD holds for everyone follows it, and a new level has a new HUD; and the radar's map: the HUD of a level the
guest's level table has a record of has that record's map, and one with no record has none.
`tests/net/client_camera_test.cpp` covers [the client's camera](#client-camera): the human each slot follows (the one
its HUD entry names, and the lowest id for a slot that drives none, two clients each following their own slot's human
and cutting when the host swaps them), what the follow camera reads of a replicated human (his velocity from the move
between two steps, the gait nearest to it, in the air, out and down), the camera made behind a human it follows and
looking at him, trailing a human running away within the leash band, the client's own right stick turning the view the
way it turns the follow camera's, a cut to another human and the frames between two steps, and the whole of it over
the loopback through the bad link of the "What a client sees" test: the camera follows the server's player round a
circle within the follow camera's band. Its case in `client_game_test.cpp` has the client game hand the camera the
guest's own pad's stick, the character's speeds and the level's collision, and a new level start with a new camera.
`tests/platform/disc_client_level_test.cpp`, with the disc, loads the scene of `level99` headless and draws two humans a
client's world was sent, both posed with every layer's clip found, the follow camera behind player 1, and a human gone from
the snapshots losing his mesh. Its radar case starts the game session's `level80` as `coney --play-level` does, then a
client game with the level table read from the same disc loading `level80`, and checks that the guest's radar map is
the host's HUD's: the same sheet, offsets and scale. `tests/gamemodes/story_start_test.cpp` covers the level table
read alone (a level's record with its map floats and the enumerations run first, and no `config_preload3.lua` an
error) and `radarMapOf()`.
