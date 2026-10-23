# Traces and playthroughs

How a run of the original is recorded and compared with Coney's: the per-update trace, its scenario files, patches,
fields and hooks, the comparison of a trace with Coney's own, and the differential playthrough of a mission. The
emulator underneath is driven as [Research tools](research-tools.md#driving-pcsx2) describes; the rest of the research
workflow is listed in [its overview](research-workflow.md#pages).

## Recording a trace {#recording-a-trace}

A parity question ("does Coney's walk start take as long as the original's?") is answered by playing **one input
script** on both games and comparing their per-update traces. The original's side is a **scenario**: a TOML file in
`research/traces/scenarios/` that names the save state, the patches, the input script beside it, how many updates to
record, and the fields to read. Scenarios, patches, field sets and input scripts are committed; the traces they record
are measurements of the game and stay in your scratch folder.

```sh
uv run --project python coney-tools pcsx2 record research/traces/scenarios/walk60.toml --out ../../scratch/walk60-original.csv
```

`record` copies the scenario's quick-save slot (read only), or the state file it names under `scratch_dir`, to
`<scratch_dir>/pcsx2/<scenario>.p2s` with the patches applied, starts PCSX2 on the copy with `-fastboot -statefile`
(through a hard link to the disc when its path has commas or parentheses), waits until the patched state runs, plays the
script and samples every update, then closes PCSX2 and warns if a file appeared in its `sstates/` folder. The folders
come from `coney.local.toml` (`pcsx2_dir`, `game_dir`, `scratch_dir`) or from `--pcsx2-dir`, `--iso` and `--scratch`;
PINE must be on in PCSX2's ini. `--state` records from another state file (or `slot:N`), `--attach` records a PCSX2 you
started yourself on a patched copy (`coney-tools pcsx2 prepare-state`, then `pcsx2 launch`), and `--keep-open` leaves it
running. It prints the number of updates, how many reads each poll made, the time per poll and the updates it missed.

**Timing.** Step 0 is the first sample, before any input. After the sample of step N the recorder writes the pad for
frame N of the script, which the update of step N + 1 reads: the numbering of Coney's `--trace`, where frame N of an
input script is step N + 1. A missed update (one the poll did not see) is listed by step; its input lands an update
late. The stick bytes are made from the script's percentages exactly as Coney makes them, so both games see the same
stick; a tap holds the button for one update. With PCSX2 2.9.94 on Windows a poll of 26 reads took about 0.05 ms, so
the tick count is seen many times per update; no update was missed in the smoke runs (130 to 230 updates). A step
counts from the tick that follows the update's own (the count turning odd), so a sample never sees an update half
done; the frame that catches up a tick (one in about 1000, [Tasks](../research/tasks.md#tick)) runs two updates and
shows as one missed step.

**Where a run's time goes** (`walk60`, 2026-10-05, two runs alike): the patched copy 0.15 s, PCSX2 from start to PINE
answering 12.1 s, the state running 0.5 s later, the 170 updates 5.7 s (real time), closing 0.2 s: 19.1 s, two thirds
of it PCSX2 starting. Started without a state it took as long (11.6-16.4 s to PINE over three launches), so the
state load itself is under a second. Loading each next state into a PCSX2 left running would save about 12 s a run;
PINE loads only numbered slots, so that needs a slot above 10 that the tool fills and empties, which is worth adding
once runs come in batches.

**The scenario file:**

```toml
description = "level99 street: the walk start and the walk, stick 60 % up for 100 updates, then the stop"
research = "docs/research/feel.md"
input = "walk60.txt"              # a Coney input script, beside the scenario
updates = 170                     # updates to record; Coney runs as many frames
fields = ["player", "camera"]     # field sets of research/traces/fields.toml

[original]
slot = 1                          # the quick-save slot to copy, never written
# state = "states/x.p2s"          # or a state file of your own, relative to scratch_dir (never committed)
patches = ["scripted-pad", "right-stick"]
let = { puppet = 'human("PoizoCiv")' }       # names resolved once, before the first update
setup = [ { address = "prec(puppet) + 0x1e", type = "u8", value = "0", frame = 0, until = 0 } ]

[coney]
level = "level99"                 # --play-level
args = []                         # more of Coney's options, such as ["--spawn", "start"]
settle = 0                        # updates Coney plays first with no input, dropped from its trace

[diff]                            # how `coney-tools trace diff --scenario` compares the two traces
start_frame = true
columns = ["x", "y", "heading", "speed", "gait", "clip"]
tolerance = { x = 0.1, speed = 0.05 }
```

`setup` writes are made after the samples of frames `frame` to `until`; the values are expressions worked out at that
moment (`f32(tf(player)) - 1.5 * sin(heading(player))` puts the puppet 1.5 m in front of the player).

`calls` (in `[original]`) make the game call one of its functions after the sample of frame `frame`, through the call
hook among the scenario's patches: `{ function = "0x002b2b90", args = ["u32(enemy + 0x90)", "u32(player + 0x90)"],
frame = 3 }` is `GoalFight(enemy, player)`. One call per frame; the arguments (up to six) are expressions worked out
then, and the recorder stops if the call before was not made.

**Patches** (the files of `research/traces/patches/`) are named groups of edits to the copy's `eeMemory.bin`: a code
edit gives the address, the instruction word the state must hold there and the word that replaces it, and the copy is
refused when the state holds something else. `scripted-pad`, `right-stick` and `puppet` are the patches of
[Driving PCSX2](research-tools.md#driving-pcsx2). The patches and the hooks ([Hooks](#hooks)) are declared in the TOML
files of that folder, read together in file-name order; a name must be unique across them. **A pass adds its own file**
(`patches/<pass>.toml`, with a comment at the top saying what the pass asked and the research page), and never appends
to an existing one: two research branches that each append to one shared file conflict on nearly every merge, two files
never do. Put a patch or hook in an existing file only to change that very entry.

**Fields** (`research/traces/fields.toml`) are named sets of columns. A field is read every update at an
**address expression** (`tf(player) + 0x4`, type `f32`), or worked out by a **formula** from the fields before it
(`wrap(deg(2 * atan2(qz, qw)))`); `hidden = true` keeps a helper out of the CSV, and `follow = true` works the address
out again at every sample, for a chain of pointers that changes as the game plays (the top of an animation stack).
A **value** field (`value = "handles(brain(h) + 0x164, 16)"`) is an expression over memory worked out at every sample,
whose result is the column. Expressions are arithmetic over these names, and nothing else of Python:

| Name | Value |
| --- | --- |
| `player` | the human whose player number `+0x1b0` is 0, among the 60 at `0x00640c80 + i × 0x6d0` in use (`+0xd4` set) |
| `human("Name")` | the human in use with that name (`+0x80`) |
| `index(h)`, `rec(h)` | the handle index (`+0x92`); the record (`*(h + 0xd4)`) |
| `prec(h)`, `tf(h)`, `brain(h)` | `0x00660f50 + index × 0x2c`; `0x00714b00 + index × 0x20`; `0x006d53f0 + index × 0x2f0` |
| `heading(h)` | the heading in radians from the transform's rotation (0 faces +y, anticlockwise) |
| `camera`, `game_time` | `*(0x005d9158)`; `*(0x0050b734) + 0x48` |
| `h` | with `rows = "humans"`, the row's human |
| `hidx(handle)` | the handle index a handle names (high half), -1 for the null handle `0xffffffff` or a stale one (`0x00390180`) |
| `goal_type(vt)`, `goal_at(brain, k)` | a goal class's type id ([goal types](../references/goal-types.md)); the type of stack slot k, -1 above the top |
| `handles(address, n)` | how many of n words are handles, neither 0 nor `0xffffffff` (a handle list's length) |
| `u8` ... `f32` (address) | a value in memory (`u8 s8 u16 s16 u32 s32 f32`) |
| `sin cos atan2 hypot sqrt abs min max deg rad wrap pi` | math; `wrap` takes degrees to (-180, 180] |

A column that Coney's `--trace` also writes has Coney's name and unit ([Tracing](running.md#tracing)): `x`, `y`,
`z`, `heading`, `speed`, `vz`, `gait`, `clip`, `stamina`, `command`, `health`, `power` and the camera's `cam_*`,
`look_*`, `wanted_*`, `cam_distance`, `cam_pitch`, `cam_yaw`, `band_near` and `target_pitch`. `phase` (record
`+0x08`) is the original's only.

**Every human.** `rows = "humans"` in a scenario writes one row per human in use per update (`step`, `human`, then the
fields, with `h` the row's human) instead of one per update: the `humans` field set, a gang battle's every fighter
([Rumble: gang battle trace](../research/rumble.md#gang-battle-trace)), is the contract for Coney's all-humans
`--trace`. One message reads which humans are in use and every fixed address; the `value` and `follow` fields are then
worked out together, one message per level of pointers (four or five messages a sample). The recorder prints how many
samples were **torn**: read while the next update had already started, so they mix two updates. A sample of 30 humans
takes about 11 ms (2026-10-12, PCSX2 2.9.94: 1.6 ms of it PINE), close to the 16 ms between the tick that ends an
update and the next update's, and at full speed 64 of 601 samples tore; `speed` in `[original]` (PCSX2's
`[Framerate]` `NominalScalar`, set in the claimed copy's ini for the run and put back afterwards) slows the emulation:
at 0.4 a 9,000-update match had none missed. The game steps by emulated ticks, but not every wait is a fixed number
of updates (the Rumble intro ended after 151 to 172, [Rumble](../research/rumble.md#gang-battle-setup)), so time a
scenario's calls with a margin. A frame that runs two updates (about one in 1,000) still tears its sample; the trace's
`.quality.txt` lists every missed and torn step.

`pcsx2 record --let NAME=EXPR` gives or replaces a name of the scenario's `let`, so one scenario is recorded with
different values: the gang battle's matches differ by an offset of the brains' random stream (`--let seed=341`).

### Hooks {#hooks}

Hooks are declared in the same file as `[hook.NAME]` and named in a scenario's `patches` like a patch. Each gives the
address, the two instruction words it displaces (checked against the state; neither may be a branch, and the first may
be a jump with its delay slot, as in a two-instruction setter) and up to seven values to `log`: a register (`a0`, `ra`,
`sp`), a float register (`"f28"` logs an `f32`; in a table without `type`, the raw word, to compare floats exactly),
`count` (the EE's cycle counter, 4,915,200 a 60 Hz tick) or a load such as `[a0 + 0x14]`, `[[a0 + 0x0] + 0x200]` or
`[0x005104f4]`, a word unless a table gives `type`. The hook's address must not be a delay slot or a branch target, and
a load must only follow pointers the hooked code itself uses: a bad load crashes the game. `pcsx2 record` writes each
hook's calls to `<trace>.<hook>.csv` (the ring's sequence number, the step, the values). The step is the update the call
was made in, the same numbering as the trace's rows, so a call logged at step N shows its effect in the sample of step
N. It comes from the tick count read in the same message: while the count is 2Q or 2Q + 1 the game is in the update a
sample labels Q + 1 (an entry of a missed update goes to the step after it; logging `[0x005104f4]` pins the tick
exactly). Recordings made before 2026-10-07 labelled entries with the step of the poll before: one step early for a call
made while the count was even (everything in `Humans_Update`), usually right for one made while it was odd (the second
tick, after the pair). `call = true` makes the call hook of `calls` above; put it at the entry of a function that takes
no floating-point arguments (`call-brains` is at `Brains_Update`, before the brains run). The hooks of the
runtime-checks pass: `tick-game` and `humans-update` (the play step and the character step), `wheel-update` (each object
the wheel updates), `state-code` (record `+0x14`'s setter), `set-command` and `get-command` (per-player `+0x20`),
`brain-think`, `brain-event`, `attack-warning`, `try-block`, `block-start`, `skid-test` (the locomotion's run-stop test)
and `call-brains`. The hint-queue pass added `hint-queue`, `hint-clear`, `hint-withdraw` and `hint-flush` (the hint
box's queue, [HUD](../research/hud-messages.md#hint-queue-runtime)) and `anim-dispatch` and `anim-callback-match` (who
starts which anim id and which script callback it runs). The level80 passes added the teammate hooks (`hit-by-chief`,
`hit-reaction`, `attack-target-push`, `push-contact`, `can-push`, `say-command`) and the fire hooks (`ember-init`,
`burn-init`, `fade-flame-init`, `debris-flame` and their update hooks); the revive pass added `revive-start` and
`revive-finish` ([Combat](../research/combat-rage.md#revive-runtime)), the speech pass `say-anim-line`, `stop-speech`,
`play-speech` and `play-speech-cut` (who starts, stops or refuses a human's line, with his line state before each;
[Sound](../research/sound.md#speech-cut-in)), and the scene-mapping pass `scene-find`, `scene-bind-human`,
`scene-bind-object` (what each scene binds, [Scenes](../research/scenes.md#cast-runtime)) and `lua-exec-source`
(`lua-exec` with the chunk name's text), and the gang-battle pass `set-target`, `help-call` and `send-helper` (who
changes a fighter's target, help calls and helpers sent; [Gang fights](../research/gang-fights.md#runtime)). One run
installs at most 64 hooks, fewer than the file holds, so name only the ones a pass needs. Two hooks at one address
cannot be installed together (`hint-show` and `event-hint` are the same place): pick one, and add any extra value it
needs to the one you keep. When the update of an entry matters, log the tick count and use it: in that pass one Lua
call's four `HintBox_Queue` entries, all at one tick, were labelled with two consecutive steps.

A logged load can also log the **text** it points to: `text = N` (at most N bytes, up to 1024) reads the string at the
value plus `text_offset`, and `text_when = "NAME=VALUE"` reads it only when another logged value has that value (a
script value's type tag, so a number is never read as a pointer). The recorder reads the texts while the game is
paused for the ring, writes them to a `<name>_text` column, and takes a text whose second byte is zero for UTF-16.
The event hooks use this: `event-call` (the script VM's call of a C binding, `0x00328a30`, its binding and first two
arguments), `event-callback` (the engine's look-up of a script function by name before it calls it, `0x00356e08`),
`event-hint` (the hint box showing a new text, `0x001cdf7c`) and `event-sound` (a sound started, `0x00111de8`, its
hash).

## Comparing with Coney {#comparing-with-coney}

`coney-tools trace coney` plays the same scenario on Coney headless (`--play-level` from its `[coney]` table,
`--frames` its updates, its input script, `--trace`), and `coney-tools trace diff` compares the two traces. A
scenario whose level opens with a script that holds the pad (level99 checkpoint 3: Vermin's fence lesson, about 9 s)
sets `settle` in `[coney]`: Coney plays that many updates first, the input script is shifted by them, and they are
dropped from the trace (renumbered from 1), so the run starts where the original's saved slot does:

```sh
uv run --project python coney-tools trace coney research/traces/scenarios/walk60.toml --out ../../scratch/walk60-coney.csv
uv run --project python coney-tools trace diff ../../scratch/walk60-original.csv ../../scratch/walk60-coney.csv --scenario research/traces/scenarios/walk60.toml
```

The diff aligns the rows by step, from the first update of input (the script's first frame + 1; `--from` and `--to`
choose others), and prints for each column the first step outside its tolerance, the largest difference and where it
is, then a short table of both traces around each divergence. It exits with 1 when a column is outside its
tolerance, so it can gate a check, and 2 when a trace cannot be read. Without a scenario it compares every numeric
column the two share; `--columns` picks some, `--tolerance COLUMN=VALUE` (or `*=VALUE`) sets tolerances (whole-number
columns are exact by default, others 0.001), `--shift N` compares the original's step s with Coney's s + N, and
headings compare round the circle.

The two games seldom start at the same spot: Coney starts a level where its script puts the player, the original where
the state was saved. `--start-frame` (or `start_frame = true`) moves each trace into its player's frame at the first
compared step, so positions are metres from where he stood, forward along +y, and headings are turns from his
heading then. The camera's points move with him, so a camera that starts at another angle shows as a heading offset.
To start both at the same spot, give Coney the state's place with `--start X,Y,Z,HEADING,DISTANCE,YAW` in the
scenario's `args`, taken from the original trace's first row (`x`, `y`, `z`, `heading`, `cam_distance`, `cam_yaw`).

The smoke scenarios reproduce claims of the research pages: `walk60` (the walk start, level99's street, slot 1),
`run_circle` (the run and the camera's auto-centre turning it into a circle, slot 1), both started at slot 1's spot
(level99 checkpoint 3 with `--start`), and `combat_cross` (`X1` then `XX2` at a puppet civilian 1.5 m away, slot 6,
which walks in to 1.36 m; on Coney the fight yard's `cross` spawn, a still target 1.05 m away that X1 steers onto
alike).

**Gang fights are compared by behaviour, not by step.** The two games draw different random numbers, so a big fight (a
Rumble gang battle) never plays out alike twice, even in the original. Record several matches of the original and
several of Coney with the all-humans trace (one row per human per update; Coney's `--trace-humans`, the original's
`[[humans]]` field set of `research/traces/fields.toml`, with its hook logs beside it), then
`coney-tools trace fight-report --original ... --coney ...` computes rates, shares and distributions per side by the
rules of [Rumble: what the comparison computes](../research/rumble.md#gang-battle-metrics) (attacks, blocks, reactions
and target changes per standing minute, goal shares and fight goals, fighters on one enemy and active places, waiting
distances, help, each side's tactic, the match's length and winner) and flags each metric where Coney is outside the
original's spread ([coney-tools](coney-tools-pcsx2.md#trace)). Give one `--original` and one `--coney` per orientation
(which gang is side 1), as the gangs and the sides each change the numbers. Keep the recordings in the scratch folder,
never in the repository. Match the original's set-up on Coney's side: where the recording hands player 1 to the AI after
the countdown, run Coney with `--ai-player`, and give each Coney match its own `--ai-seed`
([Running: tracing](running.md#tracing)).

## Differential playthroughs {#differential-playthroughs}

A trace diff compares two games update by update, which holds for a few seconds of walking but not for a mission: the
AI, load times and the camera move every later event, and a fixed input script falls out of step with the game it
drives. A **differential playthrough** compares a mission at the level a player notices instead, the **order of
events**, and drives both games through the same stretch with one **adaptive course**:

1. **Event logs on both sides, at the same semantic points.** Each game writes one CSV line per event (`step, kind,
   name, detail`): a script binding called, a call into the scripts by name, a hint shown, a sound started, a human
   appearing, going down or removed. Coney writes them with `--event-log`
   ([Running Coney](running.md#event-log)); the original's come from hooks at the places that do the same thing (the
   script VM's binding call, the engine's script look-up, the hint box, the sound start), cited to their addresses on
   the research pages. A binding's call is the best hook: it is the mission script's own vocabulary, so both games
   name the event alike without interpretation.
2. **One course, two games.** The course is a small policy that looks, once an update, at what is on screen (the
   player and camera, the other humans, the world objects of interest) and at the update's events, and answers with
   the pad: walk into the marker that is shown, fight the nearest enemy when a lesson is armed, stand before a cabinet
   and press square then triangle. Both games give the same observation (Coney through `--pad-pipe`, the original from
   its tables over the emulator's memory interface), so one policy plays both, with the same partial stick
   deflections. A course plays the player's path: from the state at a checkpoint's start or a save made there, through
   the level as the Story path sets it up, never from a debug spawn the player cannot reach.
3. **Compare the order, not the frames** (`coney-tools trace events-diff`). The bindings the rules name become kinds
   (`tutorial`, `objective`, `checkpoint`, `hint`, `speech`, `spawn`, ...), long texts become hashes and arguments
   past a binding's arity are dropped. **Milestones** (tutorial steps, checkpoints, objectives, mission ends) are
   matched in order first and cut both logs into segments; every other kind is matched in order within its segment,
   so a hint is matched only between the milestones it came between. Sounds compare by set (their order follows the
   AI's timing), an event repeated within a few updates is one event, and an event seen more than `frequent` times
   compares by count. The report names the furthest milestone both games reached, the original's next one Coney never
   reached, and the missing, extra and out-of-order events with their steps; milestones whose spacing differs by more
   than a window are listed under timing.

A scenario (`research/traces/missions/*.toml`) names the course, the event that ends the run (`until`), the
original's save state and hooks, and Coney's level and checkpoint:

```sh
uv run --project python coney-tools trace mission research/traces/missions/level99_cp1.toml --side original --agent YOU --out ../../scratch/cp1-original.csv
uv run --project python coney-tools trace mission research/traces/missions/level99_cp1.toml --side coney --out ../../scratch/cp1-coney.csv
uv run --project python coney-tools trace events-diff ../../scratch/cp1-original.csv ../../scratch/cp1-coney.csv
```

Read a report from the top: where Coney stopped is the first finding, the rest are what the player meets on the way.
A missing hint or callback names a script path Coney never takes; a milestone far later on Coney than on the original
names a lesson the course could not finish, which is a Coney bug only once the same course finished it on the
original. When the two runs start at different points (a save state made inside a checkpoint, Coney at its start),
`--from "kind name"` starts both logs at the first event they share. The logs hold the game's text: they stay in
scratch, and `--labels` names the hashed texts from a string table read from your own disc.

Write a course from the player's view only: what a policy cannot see on screen it must not use (a script's internal
state, a handle number), or it will pass on one game for a reason the other does not share. When the course itself
fails on the original, fix the course before reading anything into Coney's run.

The `level3-chase` course (`research/traces/missions/level3_cp4.toml`, the rooftop chase from checkpoint 4) is the
disc test's route driver in Python: the same route and the same moves, so the two can be checked against each
other. It differs where a course sees less: it judges a leg done from where the player stands (the C++ driver also
reads his climb, jump and charge state), and it backs off for another charge when he stops gaining ground short of
the fence row (the C++ driver, when the charge ends short of it). It taught two things.
The course steers by the camera that is shown (`Camera_GetPlayerCamera`'s `0x005d9150`), not the follow camera
alone: the chase runs on rail and fixed cameras. And a course must play as fast as a player who wants to win: one that
walked up to each climb lost Snow to the pursuers on the original as on Coney, and passed on Coney only for some AI
seeds; sprinting into the climbs, holding triangle from a jump's take-off and charging again when a charge stops short
of the fence row, it reached checkpoint 5 on the original in 3,099 updates (issue #531).
