# Playing a level, tracing and logs

`--play-level` and what comes with it: [playing a level](#playing-a-level), [tracing](#tracing) with `--trace`, the
[event logs](#event-log), [the run log](#run-log), [the pad pipe](#pad-pipe) and [matched views](#matched-views). The
other pages are listed in [the overview](running.md#pages).

## Playing a level {#playing-a-level}

`--play-level NAME` puts you in a level (`level2`, `level99`: any of the 79 with a streamed world, listed on the debug
menu's [Levels page](debug-menu.md), which also switches between them) as player 1, driven with a gamepad's analog
sticks, with the follow camera behind him ([Characters](../research/characters.md#coneys-implementation),
[Camera](../research/camera.md#coneys-implementation)). The level's worlds and level file load and stream as in [the
world viewer](running-viewers.md#the-world-viewer); he stands where the level's own script creates player 1, as when the
story reaches the level: Coney runs the scripts the original runs before it (the preloads, then a fresh Lua state with
`SetCheckPoint`), then `global.lua` and the level's script, and takes the position and heading of its `HuCreate` for
player 1, and prints them (`gameplay: level2 checkpoint 3: player 1 Cleon (type 1, model warr_cl) at ...`).
`--checkpoint N` picks the checkpoint (1 by default), so `--play-level level2 --checkpoint 3` starts where level2's
third checkpoint does. He is drawn as the character the script's type names (Cleon at `level2 --checkpoint 3`, Snow at
`level3 --checkpoint 4`), through the type's `CfgChar` model ([Characters](../research/characters.md#type-to-model)); a
type with no model falls back to Rembrandt with a log line. The scripts' world flags are kept, so a script that
teleports player 1 to a flag puts him there with the flag's heading, without snapping him to the ground, as the
original: the hub, `level95`, starts the Warchief at one of its doors, chosen with the game's own random numbers (read
from your disc's executable; door 5 while the tutorial is locked), and an arena run alone
(`--play-level level102`) uses the Rumble menu's default set-up ([Flags](../research/flags.md#coneys-implementation)).
The scripts keep running in play, one script frame per step: their scheduled calls, their volume-box triggers and their
message handlers, and every other human they create is an AI human with its own model, driven by the scripts' goals and
gangs ([AI](../research/ai.md#coney)); bindings Coney lacks are skipped with a log line. A level whose script creates no
player 1 puts him above the middle of its first world's part 1, Coney's stand-in. He walks, runs, sprints, turns and
leans into turns, stops, steps onto kerbs under 25 cm, slides along walls, jumps, climbs fences and walls, and falls off
ledges on the level's collision mesh, with the original's speeds, turn rates and clips: the walk or run start, a blend
across walk, jog, run and sprint, the idle, the run stop, the jump and its landing, and the climbs
([Characters](../research/characters.md#sprint)). He fights as the original's player does
([Combat](../research/combat/index.md#coneys-implementation)): the square and cross chains, the snaps, the run attack,
the charge and the dive, the block, rage, the grab with its strikes, spins, throws and the mugging, and the tackle, with
the original's commands, timing and clips; the sandbox's fight yard (`--play-level sandbox:combat`) has passive targets
that take the hits. With a target (the one he attacked, or the one L1 picks) he locks onto it, as the original's default
settings do: he faces it and the stick walks him at one speed in any direction with the combat walk until it is more
than 2.5 m away or down. The level's glass panes and doors work: his hits break the panes and the breakable doors, and
the lock pick opens a door ([World objects](../research/objects.md#coneys-implementation)). The level's scenes play and
hand him back ([Scenes](../research/scenes.md#coneys-implementation)). A fall out of the world puts him back at the
start. Coney prints a line whenever the clip changes and a summary when it stops (the player's position, speed, gait,
clip, traversal state, stamina, the camera's distance and counts only).

| Pad | Keyboard | Does |
| --- | --- | --- |
| left stick | W A S D (full deflection) | move, relative to the camera: a walk below 95 % of the stick's travel, a run above |
| L2 held | 1 | sprint, with the stick past 95 %, while stamina lasts (135, 6.75 s); let go to refill it |
| triangle | I | climb the fence or wall ahead (within reach, stick pushed); otherwise jump from a run or sprint; in a grab, mug |
| square | J | attack (`S1`; pressed again in the chain window `SS2`, then `SSS3`; with the stick pushed past 95 % to a side or back, a snap; at a run, the run attack); in a grab, strike |
| cross | K or Space | attack on the release (`X1`, then `XX2` or `XS2`); in a grab, strike |
| circle | L | tapped, grab the target in reach; held, tackle; in a grab, throw towards the stick (pushed past 25 %) |
| R1 held | E | block: the stick turns him in place and he does not move; in a grab, R1 pressed spins the hold |
| L1 held | Q | pick the target in front (within 2.5 m): he faces it and the stick walks him round it |
| L1 + R1 | Q + E | start rage, with a full rage meter |
| L2 held, then cross or square | 1, then K or J | the charge or the dive, at a run or sprint |
| right stick | none | turn the camera round him, look up and down |

The stick's direction is turned by the camera's heading, so up always moves away from the camera. The game's own
dead zone (12 %) applies; the walk speed does not depend on how far the stick is pushed, only whether it is pushed
past 95 %. Let go at a walk and he stops at once and settles into the idle; let go (or pull the stick back) at a run
or a sprint and he skids through the run stop. The camera swings round behind him while he walks, runs or sprints
(the original's auto-centre rule), except just after a wall hid him from it, so a stick held to the side runs him in a
circle; it pulls in to 3 m and lowers to 7° over half a second of a sprint and goes back a quarter of a second after
it, and it swings away from a wall beside it.
The jump needs a run (faster than 3.3 m/s) and is refused within 5.5 m of a climbable face, where triangle climbs
or does nothing. In a script: `press l2`, `stick left 0 100`, `tap triangle`. The fights: `tap square` every 6 frames
for the square chain, `tap circle` to grab and `press circle` with a `release circle` 7 or more frames later to
tackle, `press r1` to block (the fight scripts: `tests/support/combat_*.txt`).

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level level99
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level level99 --frames 200 --input-script tests/support/play_walk.txt --screenshot ../../scratch/walk.png
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level level99 --headless --frames 300 --input-script tests/support/play_wall.txt
```

The simulation runs on the fixed 1/30 s step and reads only the pads and game time, so a script gives the same path,
clips and camera every time, in a window or headless. Drawing blends the snapshots of the last two steps (Rembrandt's
feet, heading and pose, the camera's eye and target), so play runs at the same speed and moves smoothly at any frame
rate ([Frame rate](running-options.md#frame-rate)). Scripts drive it with partial deflections, as a player would:
`stick left 0 30` walks forward at 30 %, `stick left 50 87` runs along an arc to the right, `stick right 60 0` turns the
camera at 60 % of its full rate.

`--play-level sandbox` (or `sandbox:NAME` for another layout) plays a [sandbox](sandbox.md) test world in place of a
level: the same player, follow camera and character, on the sandbox's collision mesh. Only the scenery differs.
Rembrandt starts at the layout's first spawn point, or at the one `--spawn NAME` names. Nothing streams; the far clip
is the layout's fog end. The disc is still needed, for the character.

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level sandbox:parkour --spawn lane
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level sandbox --spawn stairs --frames 120 --input-script tests/support/sandbox_walk_short.txt --screenshot ../../scratch/stairs.png
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level sandbox:combat --frames 30 --input-script tests/support/combat_combo.txt --screenshot ../../scratch/combo.png
```

A layout's `target` lines put passive humans to fight there ([Sandbox](sandbox.md#the-layout-format)); the summary
then adds the fight's counts (hits, damage, power, rage, and the targets' health, reactions, stuns and knockdowns).

### Tracing {#tracing}

`--trace FILE` (with `--play-level`) writes the player's and the follow camera's state after every step to `FILE`,
one CSV line per step, so a feel or combat comparison with the original ([Feel comparison](../research/feel.md)) can
be repeated with Coney alone. Drive it with an input script in test mode, so the run is the same every time:

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level sandbox:parkour --spawn lane --headless --frames 300 --input-script ../../scratch/run_turn.txt --trace ../../scratch/run_turn.csv
```

The columns, positions in metres (game axes, z up), angles in degrees, speeds in m/s:

| Columns | What |
| --- | --- |
| `step` | the step, from 1 (frame N of an input script is step N + 1) |
| `x`, `y`, `z`, `heading` | the feet and the facing (0 faces +y, anticlockwise) |
| `speed`, `vz` | the speed across the ground and the vertical speed |
| `gait`, `clip`, `traversal` | the stored gait (0 standing to 5 sprint), the anim id playing, and `none`, `jumping`, `landing`, `run stop`, ... |
| `stamina`, `sprinting` | the stamina meter and whether a sprint is asked for |
| `cam_x` ... `wanted_z` | the camera's position, its look-at point and its wanted position |
| `cam_distance`, `cam_pitch`, `cam_yaw` | the camera's distance from its look-at point, its pitch above it and the heading its view faces |
| `band_near`, `target_pitch`, `auto_turn` | the leash band's near edge (the sprint zoom moves it), the target pitch, and the auto-centre rule's turn this step |
| `command`, `health`, `power` | the command matched this step (0 for none; [Combat](../research/combat/index.md#commands)), the health and the power meter |
| `aim_x`, `aim_y`, `aim_z`, `aim_lag` | the aim point the view faces ([Camera](../research/camera.md#aim-point)) and its lag |

`--trace-humans FILE` (with `--play-level` or `--rumble`) writes **every** human's state after every step to `FILE`, one
CSV row per human per step (`step`, then `human`, the human's number), in a Rumble match as well as in a level. It is
Coney's side of the gang-battle comparison: [`coney-tools trace fight-report`](coney-tools-trace.md#trace) turns the rows
into behaviour statistics per human and per gang and compares them with the original's recording, because the two
games draw different random numbers and a fight cannot be compared update by update. `--trace` stays the player's
trace for `--play-level`, and in a Rumble match (where there is no one level to trace the player of) it means
`--trace-humans`. A Rumble match goes through the menu and the arena's level, so drive the menu with an input
script (the match starts when the arena is confirmed) and give `--frames` for the whole run; the file is opened once
and the rows of every level loaded go into it, numbered by step from the first row.

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --skip-movies --headless --frames 6000 --rumble warparty --arena 102 --gang-size 5 --input-script ../../scratch/rumble_menu.txt --trace-humans ../../scratch/fight.csv
```

Two options make Coney's run match the reference recordings of the original, where every fighter is AI-driven and each
recording draws different random numbers:

- `--ai-player` (with `--rumble`) hands player 1 to the AI when the countdown ends, once the arena script's
  `FinishCountdown` and the rules' `StartIt` have run: the brain his class gives takes him over, his goals and actions
  are cleared and the pad drives no one, as the original's `Human_ReleasePlayer` (`0x0022a2a8`) does to the human it
  takes the pad from ([Rumble: Coney](../research/rumble.md#coney)). The log says `player: player 1 (human N) handed
  to the AI`; his trace rows then have `player` -1 and his class's `btype`. The pad does nothing for the rest of the
  match, and the scripts' hand-over to a team-mate no longer happens, since nobody is a player; nor does the
  game-over check fail the match when he is beaten.
- `--ai-seed N` (with `--rumble` or `--play-level`, 0 to 2147483647) varies the AI's random draws: the game's random
  stream starts N draws in (as each recording of the original starts its index at `0x006eb880` somewhere else), and
  every brain's and gang's own generator is seeded from N. The same N plays the same run; 0, the default, is the
  usual run.

The gang-fight scenario, a 9-a-side Brawl in the Fight Pen (the Furies against the Orphans) played out by the
AI, once per seed (`rumble_menu.txt` drives QUICK RUMBLE and the intro's prompt, as `kQuickRumbleScript` in
`repo:tests/support/disc_play_fixtures.h` does):

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --skip-movies --headless --frames 12000 --rumble 1 --arena 102 --gang-size 9 --ai-player --ai-seed 3 --input-script ../../scratch/rumble_menu.txt --trace-humans ../../scratch/fight_seed3.csv
```

The columns, positions in metres (game axes), angles in degrees, speeds in m/s; those marked (O) are also written by
the original's all-humans recorder under the same name and unit (`research/traces/fields.toml`, `[[humans]]`):

| Columns | What |
| --- | --- |
| `step`, `human` (O) | the step, from 1, and the human's number: the player's brain is 0, the others are numbered as they first appear, and keep the number when another leaves |
| `player`, `btype`, `gang` (O) | the player number a pad drives (0 for player 1, -1 for none), the brain's type (0 player, 1 cop, 2 gang, 3 Warrior, ...) and the gang's id (-1 for none) |
| `x`, `y`, `z`, `heading`, `speed`, `vz`, `gait` (O) | as in the player's trace |
| `clip`, `clip_frame`, `command` (O) | the anim id playing, its frame (30 a second) and the command of the step (0 for none) |
| `health`, `health_max`, `power`, `stamina` (O) | the meters |
| `state` (O) | the record's state word as far as Coney keeps its states: grabbed `0x10` / `0x20` (front, rear), grabbing `0x40` / `0x80`, mounted `0x800`, blocking `0x8000`, wounded `0x10000`, arrested `0x20000`, knocked out `0x40000`, down `0x80000`, stunned `0x100000`; the stance, dying (`0x80000000`) and the other bits stay 0 ([Rumble: Coney](../research/rumble.md#coney)) |
| `phase` (O) | the attack phase, the held flags (`0x1` wind-up, `0x2` chain window, `0x4` end) |
| `down`, `blocking`, `stunned`, `grabbed`, `grabbing`, `attacking` (O) | 1 on the ground, 1 blocking, 1 stunned, 1 held or tackled, 1 holding someone, 1 in an attack phase |
| `lock`, `target` (O) | the number of the human its fighter has locked on, and of its brain's target (-1 for none) |
| `goals`, `goal`, `goal0` ... `goal5`, `react` (O) | the goal stack's depth, its top goal's type id, the stack from the bottom (-1 above the top) and the reaction goal's id ([goal types](../references/goal-types.md)) |
| `actions`, `next_attack`, `attackable`, `warned` (O) | the actions queued, the ms until it may attack and may be attacked again, and the attacks announced on it this step |
| `enemies`, `slots`, `slots_max`, `active` (O) | the enemy list's length, the attackers holding a slot on it, the slots it allows and the active-attacker places held on it |
| `space_up`, `space_down`, `threat` (O) | its spacing bytes (standing and down) and its threat response |
| `fight_updates`, `fight_active` (O) | with the Fight goal on top, the updates it has run and 1 when it holds an active place on its target; -1 with another goal on top |
| `g_alert` (O) | the gang's alert state: always -1, as Coney does not model it ([Gang fights: Coney](../research/gang-fights.md#coney)) |
| `g_tactic` (O) | the gang's tactic as the original's vtable: `0x00543740` TacticConfront, `0x00543320` TacticAttack, `0x00544160` the attack command's; 1 for another tactic (Coney's stand-in), 0 for none |
| `g_leader`, `g_chosen` (O) | the numbers of the gang's leader and chosen target (-1 for none) |
| `g_teamups`, `g_order`, `brain_off` (O) | 1 when the gang's team-ups are allowed (`GangEnableAttackStrategies`), the team-up order running (always -1: Coney builds none) and 1 while the brain is off (`BrDead`) |
| `target_dist`, `action_kind`, `attack_kind`, `slot_held` | Coney's own: metres to the target, the front action (`none`, `attack`, `move`, ...), the attack kind it presses (-1 when it is none) and 1 when it holds a slot on its target |
| `help_calls`, `helpers_sent`, `target_causes` | Coney's own: the help calls the human has made, the HelpRespond goals it has been sent and its target changes by cause (`pick=3;front=1`; empty for none: `pick`, `front`, `hit_back`, `help`, `engage`, `displaced`, `reaction`, `order`, `other`, [Rumble: Coney](../research/rumble.md#coney)) so far, which stand for the original's `help-call`, `send-helper` and `set-target` hook logs |

`--script-trace FILE` (with `--play-level` and a level) writes what the level's scripts do to `FILE`, one line per
call: every script binding they call, with its arguments and what it returned (`HUDSetObjective(0, "...", 0, nil)`,
`HuCreate(...) -> 396`), and every call the engine makes into them by name, marked `>` (`> P1.EnterRiot(74, 398)`, a
box's message, a scene's end). Short lists of numbers (a position) are shown whole, other tables as `{table}`. It is
how a mission's run shows where its scripts wait. The file holds the game's own text (objectives, speech names): keep
it out of the repository.

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level level34 --checkpoint 1 --headless --frames 1800 --script-trace ../../scratch/level34.trace
```

`--start X,Y,Z,HEADING[,DISTANCE,YAW]` (with `--play-level`) moves player 1, after the level's own start, to those
feet (dropped onto the ground below) and heading in degrees, and with the last two puts the follow camera DISTANCE
metres from him with its view facing YAW degrees. It is a test aid: a trace scenario starts Coney where the
original's save state stands, as `walk60` and `run_circle` do from slot 1's first trace row.

`--scene NAME` (with `--play-level`) plays the in-engine scene NAME at once, after any `--start`, as a level99
cinematic: player 1 takes the role named `warrrecv` and the other roles are played by stand-ins drawn from the role's
model (a role whose model Coney does not know is drawn as player 1's), letterboxed and skippable with cross or START
([Scenes](../research/scenes.md)). Any level's scenes play, not only level99's (`--play-level level2 --scene l2_c1`);
the stand-ins are lit like the other humans. It plays on the level's own
scene system, beside the scenes the level's scripts play themselves (level99's intro, `l99_c1`, at checkpoint 1). It
is a test aid:

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level level99 --scene l99_c5 --frames 560 --screenshot ../../scratch/l99_c5.png
```

The original's trace, recorded in PCSX2 by `coney-tools pcsx2 record`, uses the same names and units for the columns
both have, and `coney-tools trace coney` runs a scenario's script on Coney with `--trace`; `coney-tools trace diff`
compares the two, column by column ([Comparing with Coney](research-traces.md#comparing-with-coney)).

**The story's own set-up.** A level played with `--play-level` is the story's level, started there directly: it runs in
the same game session as the story from the main menu and the debug menus' jumps (`src/platform/game_session.h`,
[Test through the player's path](testing.md#test-through-the-players-path)), and differs only in skipping the
start-up movies, the legal screen, the memory-card check and the menus. So it has the loading screen and the level's
intro movie (`--skip-movies` skips the movie), the pause (START) and the mission screens, the game's sound with the
level's own bank, and the debug menus' Lua console and Cheats page work on its scripts. An `--input-script` starts on
the first frame after the loading screen and the movie (with no movie, the level's first step runs in the screen's last
frame, with the pad at rest), and `--frames` and `--screenshot` count from there too, however long the loading screen
and the movie take (a level that never starts playing ends the run after 10 minutes of frames, with a message).

**Going on to the next level.** A level played with `--play-level` (or from the debug menus) goes on to the next one the
way the story does, because it is the story's: when a mission ends (`HUDLaunchMissionComplete`), the mission-complete
mode runs the scripts' `UnlockAndLoad` (the unlocks, then `runNextMission(1)`, [Story
order](../research/scripting.md#run-next-mission)), and the level flow loads the level and checkpoint it chose; a level
a script starts with `MenuLoadLevel` (the hub's missions) is played the same way. The unlocks, bank and saved script
numbers carry from one level to the next, so the story's order holds: 99, 80, 87, the hub (`level95`), 34 and on.
`--frames` still ends the run.

### Event logs {#event-log}

`--event-log FILE` writes what a player would notice to `FILE`, one CSV line per event (`step,kind,name,detail`;
`step` is the update, from 1): every script binding called (`call`, with its arguments as `--script-trace` shows
them), every call into the scripts by name (`callback`), each hint the hint box shows (`hint`, its text), each sound
started (`sound`, its hash), and the level's humans other than player 1 appearing, running out of health and being
removed (`human_in`, `human_out`, `human_gone`). It works in any mode, from the front end on; with `--play-level`
it starts when the level's Lua state is made, before its scripts run, with the step counted from there, so the
loading screen's events stay out. The file is flushed every update, so a run that is killed keeps its events. It is
the Coney side of a [differential playthrough](research-traces.md#differential-playthroughs). Like
`--script-trace` it holds the game's own text: keep it out of the repository.

### The run log {#run-log}

Everything Coney prints about a run is a line of the [run log](run-log.md), stamped `[step N | mm:ss.s | level
cpK]` with a category: on the console, and in `coney.log` in your data folder (a test-mode run writes a file only
with `--log-file FILE`). `--log-verbose ai,combat` keeps the `detail` lines of those categories too (`all` for every
one). How to read it, attach it to a bug report and add lines: [The run log](run-log.md).

### The pad pipe {#pad-pipe}

`--pad-pipe` lets a driver program play player 1 by what it sees, in lock step: before each update Coney
writes one line `@obs {json}` to standard output (the update before's observation: `play`, then while a level is in
play the player's feet and heading, the camera's eye and target, the other humans as `[x, y, alive, standing,
enemy]` (standing: not on the ground, so a held or mounted human stands, as the original's state bits say) and the
observed world objects as `[type, x, y, shown]`, with car stereos among them), then reads lines from
standard input until a pad: `observe PREFIX...` chooses the world object types observed from then on, and `pad
BUTTONS RX RY LX LY` (the buttons in hexadecimal, as an input script's masks, then the four stick bytes, 0x80
centred) is the pad of the update. With `--play-level` the first observation is of the level's first update of
play: until then the session loads the level as the story does and the pad rests. With `--event-log` each event is
also written as `@ev <csv line>`; other lines on standard output are Coney's own messages. At the end of standard
input the pad is released. It is test mode (one step per update) and cannot be combined with `--input-script`.
`coney-tools trace mission` drives it.

### Matched views {#matched-views}

Three options make a frame that can be laid beside a screenshot of the original from the same place:

- `--camera X,Y,Z,QX,QY,QZ,QW[,FOV]` (with `--play-level`) pins player 1's view for the whole run, over the follow,
  scene and free cameras. X,Y,Z is the eye in the game's axes (metres, z up) and QX..QW the orientation quaternion,
  both exactly as the original's camera object holds them at `+0x10` and `+0x20`; FOV is the field of view used, in
  degrees (`+0x48`, 60 when not given). The quaternion turns the camera's +y (the view direction) and +z (up) into the
  world ([The base camera object](../research/camera.md#the-base-camera-object)). The near and far clips are the
  current camera's, as usual, and the scenery streams round the pinned eye.
- `--render-size WxH` draws frames of W × H pixels (`640x448` is the original's frame buffer). The 2D screen fills the
  whole frame and the 3D view keeps the 4:3 shape of a television across it, so a 640 × 448 screenshot lines up pixel
  for pixel with the original's frame buffer. It needs a window.
- `--freeze-world` (with `--play-level`) stops the world after the first step of play: the scripts, people, cars,
  particles and animation stand still, while the fixed step runs on and the scenery still streams in (and fades in)
  round the camera, so every later frame shows the same picture.

```sh
build/dev/src/platform/coney --disc /path/to/warriors.iso --play-level level99 --checkpoint 1 --skip-movies --freeze-world --camera 75.2,41.1,1.8,0,0,0.7071068,0.7071068,65 --render-size 640x448 --frames 300 --screenshot ../../scratch/l99_view.png
```

`--screenshot` needs a window, so these runs are not `--headless`.
