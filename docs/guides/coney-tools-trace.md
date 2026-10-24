# The coney-tools trace commands

The commands that compare the original's per-update traces, recorded with [`pcsx2`](coney-tools-pcsx2.md#pcsx2), with
Coney's: [`trace`](#trace) plays a scenario on Coney headless, compares two traces step by step, reports a gang
fight and compares two event logs. The other groups are listed in [the command line overview](coney-tools.md#groups).

## trace {#trace}

```sh
uv run --project python coney-tools trace coney SCENARIO --out CSV [--coney EXE] [--disc DISC]
```

Plays the scenario's input script on Coney headless (`--play-level` and the options of its `[coney]` table, `--frames`
its updates, `--trace CSV`). `--coney` defaults to `build/dev/src/platform/coney`, `--disc` to `game_dir`.

```sh
uv run --project python coney-tools trace diff ORIGINAL CONEY [--scenario SCENARIO] [--columns COLUMN ...]
    [--tolerance COLUMN=VALUE ...] [--from STEP] [--to STEP] [--shift N] [--start-frame] [--context N]
```

Compares two traces step by step and prints, per column, the first step outside its tolerance, the largest difference
and its step, then the rows of both around each divergence. It exits with 0 when every column is within tolerance, 1
when one is not and 2 when a trace cannot be read. With `--scenario`, the columns, tolerances, start frame and first
step (the first update of input) come from the scenario's `[diff]` table; flags override them.
[Comparing with Coney](research-traces.md#comparing-with-coney) explains the alignment and the start frame.

```sh
uv run --project python coney-tools trace fight-report TRACE... [--humans] [--hz N] [--exclude HUMAN ...]
    [--engage-distance METRES] [--from STEP] [--original-gangs ID,ID,...]
uv run --project python coney-tools trace fight-report --original TRACE... [--original TRACE...]
    --coney TRACE... [--coney TRACE...] [--margin SHARE] [--original-gangs ID,ID,...] [--coney-gangs ID,ID,...]
```

Behaviour statistics of a gang fight, from the all-humans trace of either game (Coney's `--trace-humans`, the
original's `pcsx2 record` of the all-humans field set; [Tracing](running-play-level.md#tracing)), by the rules of
[Rumble: what the comparison computes](../research/rumble.md#gang-battle-metrics). The two games draw different
random numbers, so a fight is compared by rates, shares and distributions, not update by update. A trace needs the
columns `step`, `human` and `gang`; a metric whose columns are missing is left out.

* **The fighters** are the humans of the gangs in `--original-gangs` (`--coney-gangs` for Coney's runs), or else of
  the gangs that fight (a target, an attack, a block or a knock-out), less a crowd of brain type 4 (`btype`).
* **The fight** runs from the first step a fighter's brain is on (`brain_off` 0, or without that column the first
  goal pushed) to `RumbleOver`, the step before every fighter's `brain_off` is 1 (or every goal stack has been empty
  for 30 steps). A trace without those columns runs from `--from` to the first step where one gang or none stands.
* **Out of the fight** is `Human_IsDownOrDead`'s mask, `state` `0x80050000` (dying, knocked out, wounded) or
  `state_hi` `0x1` (dead), or clip 349, or gone from the trace; in a trace without `clip` and `state`, `ko` 1 or
  `health` 0. Every rate is per minute a human stands, at `--hz` updates a second (30).

For the whole match (`all`) and each side (and each human with `--humans`) it prints:

* **attacks** (`attack.total_per_min`, the wind-ups: `phase` bit `0x1` turning on, with `attack.distance_p10`,
  `p50`, `p90` to the target then; without `phase`, a change to an attack command in `command`, also by kind as
  `attack.square_per_min`, ...);
* **blocks** (`block.per_min`, the Blocking goal, 27, coming on top) and **held blocks** (`block.held_per_min`,
  `blocking` or the block command); **reactions** (`react.per_min`), **knock-downs** (`down.per_min`), **health
  losses** (`health.losses_per_min`) and **taunts** (`taunt.per_min`, clip 603 starting);
* **target changes** (`target.changes_per_min`, split into `target.switch_per_min` A to B, `set_per_min` from none
  and `clear_per_min` to none, and by cause as `target.cause.pick_per_min`, `front`, `hit_back`, `help`, `engage`,
  `displaced`, `reaction`, `order`, `other` and `unknown`: from Coney's `target_causes` column, or the original's
  `set-target` hook log beside the trace, `<stem>.set-target.csv`, counting only the calls that change the target,
  each caller named by the table `SET_TARGET_CAUSES` in `repo:python/src/coney_tools/fight_trace.py`, which follows
  [What each set-target caller means](../research/gang-fights.md#set-target-causes); a caller it does not name yet is
  `unknown`. The causes are not a split of `target.changes_per_min` and need not add up to it: they count every
  change in the window, also while the human is out (the knock-out's drop) and both halves of a pick's clear and set,
  where `changes_per_min` counts the changes seen between two sampled rows while he stands; both are divided by the
  minutes he stands) and goal changes and interruptions (`goal.changes_per_min`,
  `goal.interruptions_per_min`: a goal pushed over another, or a stack replaced instead of popped);
* the **time share of each goal** (`goal.NAME`) and the **idle share** (`idle.share`, an empty stack while standing);
  the **fight goals**, runs of Fight on top, a new one when `fight_updates` starts again (`fight_goal.count`,
  `median_s`, `p90_s`, `share_4s`);
* **fighters on one enemy** (`attackers.mean`, `p90`, `max`, `share1` to `share4`) and the **active places** held on
  a fighter (`active.share0`, `share1`, `share2`, `share3plus`);
* **waiting**: the distance to the target with Fight on top and no active place (`fight_active` 0), split by whether
  the target's own target is him (`wait.mutual_p50`, `wait.other_p50`, and the share in 4-4.75 m, `_band`);
* **help** (`help.calls_per_min`, `help.helpers_per_min`, `help.respond_per_min`): Coney's `help_calls` and
  `helpers_sent` columns, or for the original the `help-call` and `send-helper` hook logs beside the trace
  (`<stem>.help-call.csv`, `<stem>.send-helper.csv`);
* the **distance to the target** (`distance.p10`, `p50`, `p90`), the **time to the first engagement**
  (`engage.first_s`, `median_s`, `share`: the first attack, or a target within `--engage-distance`, 3 m by default)
  and the **knock-out times** (`ko.count`, `first_s`, `median_s`, `last_s`);
* per side, its **fighters out at the end** (`out.at_end`), its **tactic** (`tactic.first`, `last`, `changes`,
  `first_updates`, from `g_tactic`), `teamups.max`, `order.share` and `chosen.share`;
* the **match's length and winner** (`match.length_s`, `match.winner`: the side with fighters standing at the end).

`--exclude` leaves humans out (a spectator, the pad's player); `--from` skips the steps before one.

With `--original` and `--coney` it prints, for every metric, the original's spread (min / median / max over its runs)
beside Coney's and marks with `<<` each metric where Coney's median is outside the original's spread (`above`,
`below`, `missing`, or `different` for a winner the original never had). `--margin` widens the spread by that share of
its width. A goal or an attack one side never uses counts as zero. Each `--original` and `--coney` names the runs of
one orientation (which gang is side 1), and the two are paired in order, so each orientation of Coney's is compared
with the original's runs of the same orientation; give as many of one as of the other. Sides are labelled `g0`, `g1`,
... by rank, the lowest id first, or in the order of `--original-gangs` and `--coney-gangs` when their ids differ. The
exit code is 1 when a metric is outside the spread, 0 when none is, 2 when a trace cannot be read. With one run of the
original the spread is a point, so record several ([Comparing with Coney](research-traces.md#comparing-with-coney)).

```sh
uv run --project python coney-tools trace mission SCENARIO --side coney|original --out CSV [--coney EXE] [--disc DISC]
    [--state STATE] [--agent ID] [--updates N] [--follow ROUTE [--follow-from EVENT]]
```

Plays a mission scenario (`research/traces/missions/*.toml`) on one game with its course, an adaptive pad that plays
by what is on screen, and writes the game's event log to `CSV` (keep it outside the repository: it holds the game's
text). `--side coney` runs Coney headless with `--pad-pipe` and `--event-log` (`--coney`, `--disc` as for `trace
coney`); `--side original` starts PCSX2 on the scenario's state with its patches and event hooks, as `pcsx2 record`
does, under your claim (`--agent`), and reads the events from the hooks' ring. `--state` starts from another state
file or `slot:N` (a copy, never the slot itself); `--updates` caps the run. It prints its progress every 1000 updates
and ends when the scenario's `until` event comes, or at its `updates`. Beside the log it writes the run's route,
`<CSV stem>.route.csv` (`step,x,y,z,heading,buttons,lx,ly`: the player's feet and heading after each update in play,
then the pad given for the next one; the buttons are Coney's bits in hexadecimal, the left stick as raw bytes).

`--follow ROUTE` (Coney only) plays another run's route instead of the course, usually the original's, so Coney's
player goes where the original's went and what is heard and spawned on the way can be compared. The route is cut
into walks (a waypoint each 0.5 m of the recorded travel, walked to with the stick at 60 %) and actions (the updates
in which the recorded player pressed a button, or turned the stick while standing still, played back pad for pad once
he stands within 0.25 m of where it began and faces its heading); updates out of play hold the pad at rest, so a
scene or a load does not use up the route. `--follow-from "kind name"` lets the scenario's course play until that
event (`"call CarSpawnRadio"`), then follows the route from the step its own run saw it, found in the event log beside
the route: for a stretch one game's start state has half done. A route is followed, not replayed: Coney may take
longer over it (a waypoint it cannot reach is given up after 40 updates without headway).

```sh
uv run --project python coney-tools trace events-diff ORIGINAL CONEY [--rules RULES] [--kinds KIND ...] [--window N]
    [--limit N] [--labels JSON] [--from EVENT]
```

Compares two event logs in order, not frame by frame, by the rules of `research/traces/events.toml` (`--rules` for
another): the furthest milestone both reached and the one Coney never reached, then the events missing in Coney, the
extra ones, those out of order, milestones whose spacing differs by more than `--window` updates, and the frequent
events by count. `--from "kind name"` starts both logs at their first such event, for two runs that began at
different points of a mission; `--labels` names hashed texts by a JSON object of label to text (a string table read
from your disc, kept in scratch). It exits with 0 when Coney has every compared event in order, 1 when not.
[Differential playthroughs](research-traces.md#differential-playthroughs) explains the method.

<!-- command-reference:begin trace -->
## Parameters of `trace` {#trace-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `trace coney` {#trace-coney-usage}

Play a scenario on Coney headless and write its --trace.

```text
coney-tools trace coney SCENARIO --out OUT [--coney CONEY] [--disc DISC]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `SCENARIO` | path | required | a scenario TOML (research/traces/scenarios/) |
| `--out OUT` | path | required | the trace CSV (outside the repository) |
| `--coney CONEY` | path | none | Coney's executable; default: build/dev/src/platform/coney |
| `--disc DISC` | text | none | the disc for Coney; default: game_dir |

### `trace diff` {#trace-diff-usage}

Compare the original's trace with Coney's; exit 1 outside tolerance.

```text
coney-tools trace diff ORIGINAL CONEY [--scenario SCENARIO] [--columns COLUMN [COLUMN ...]]
    [--tolerance COLUMN=VALUE [COLUMN=VALUE ...]] [--from START] [--to END] [--shift SHIFT] [--start-frame]
    [--context CONTEXT]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `ORIGINAL` | path | required | the original's trace (pcsx2 record) |
| `CONEY` | path | required | Coney's trace (coney --trace, or trace coney) |
| `--scenario SCENARIO` | path | none | take the columns, tolerances, start and frame from a scenario |
| `--columns COLUMN [COLUMN ...]` | text | none | compare these (default: every shared one) |
| `--tolerance COLUMN=VALUE [COLUMN=VALUE ...]` | text | none | per column; \*=VALUE for all |
| `--from START` | integer | none | the first step (default 1, or the input's first) |
| `--to END` | integer | none | the last step (default: the last both have) |
| `--shift SHIFT` | integer | none | compare original step s with Coney step s + SHIFT (default 0) |
| `--start-frame` | flag | off | compare in each player's frame at the first step |
| `--context CONTEXT` | integer | `3` | rows shown each side of a divergence (default 3) |

### `trace fight-report` {#trace-fight-report-usage}

Behaviour statistics of an all-humans trace; compare the original's runs with Coney's.

```text
coney-tools trace fight-report [TRACES ...] [--original TRACE [TRACE ...]] [--coney TRACE [TRACE ...]] [--hz HZ]
    [--exclude HUMAN [HUMAN ...]] [--engage-distance ENGAGE_DISTANCE] [--from START_FROM] [--humans] [--margin MARGIN]
    [--original-gangs ORIGINAL_GANGS] [--coney-gangs CONEY_GANGS]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[TRACES ...]` | path | none | all-humans traces (coney --trace-humans) to report |
| `--original TRACE [TRACE ...]` | path | none | the original's runs of one orientation (repeat for each orientation). Repeatable. |
| `--coney TRACE [TRACE ...]` | path | none | Coney's runs of one orientation, in the order of --original. Repeatable. |
| `--hz HZ` | integer | `30` | updates a second in the traces (default 30) |
| `--exclude HUMAN [HUMAN ...]` | integer | none | leave these humans out |
| `--engage-distance ENGAGE_DISTANCE` | number | `3.0` | metres to the target that count as engaged |
| `--from START_FROM` | integer | `1` | skip the steps before this one |
| `--humans` | flag | off | also print the metrics of each human |
| `--margin MARGIN` | number | `0.0` | widen the original's spread by this share of its width |
| `--original-gangs ORIGINAL_GANGS` | text | none | the original's sides' gang ids in the order g0,g1,... |
| `--coney-gangs CONEY_GANGS` | text | none | Coney's sides' gang ids in the order g0,g1,... |

### `trace mission` {#trace-mission-usage}

Play a mission scenario's course on one game; write its event log.

```text
coney-tools trace mission SCENARIO --side SIDE --out OUT [--coney CONEY] [--disc DISC] [--state STATE] [--agent AGENT]
    [--updates UPDATES] [--follow FOLLOW] [--follow-from EVENT] [--pcsx2-dir PCSX2_DIR] [--iso ISO] [--scratch SCRATCH]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `SCENARIO` | path | required | a mission scenario TOML (research/traces/missions/) |
| `--side SIDE` | choice | required | the game to play it on. One of `coney`, `original`. |
| `--out OUT` | path | required | the event log CSV (outside the repository) |
| `--coney CONEY` | path | none | Coney's executable; default: build/dev/src/platform/coney |
| `--disc DISC` | text | none | the disc for Coney; default: game_dir |
| `--state STATE` | text | none | the original: another state file (or slot:N) to start from |
| `--agent AGENT` | text | none | the original: your id; runs under your PCSX2 claim |
| `--updates UPDATES` | integer | none | play at most this many updates (default: the scenario's) |
| `--follow FOLLOW` | path | none | Coney: follow this recorded route (an original run's .route.csv) instead of the scenario's course |
| `--follow-from EVENT` | text | none | with --follow: the course plays up to this event ("kind name", "call CarSpawnRadio"), then the route |
| `--pcsx2-dir PCSX2_DIR` | path | none | the portable PCSX2 folder; default: pcsx2_dir |
| `--iso ISO` | path | none | the disc image (or a folder with one); default: game_dir |
| `--scratch SCRATCH` | path | none | where state copies and the disc link go; default: scratch_dir |

### `trace events-diff` {#trace-events-diff-usage}

Compare two event logs in order; exit 1 when Coney lacks some.

```text
coney-tools trace events-diff ORIGINAL CONEY [--rules RULES] [--kinds KIND [KIND ...]] [--window WINDOW] [--limit LIMIT]
    [--labels LABELS] [--from EVENT]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `ORIGINAL` | path | required | the original's event log (trace mission --side original) |
| `CONEY` | path | required | Coney's event log (coney --event-log, or trace mission) |
| `--rules RULES` | path | none | the rules file; default: research/traces/events.toml |
| `--kinds KIND [KIND ...]` | text | none | compare these kinds (default: the rules' list) |
| `--window WINDOW` | integer | none | the timing window in updates (default: the rules') |
| `--limit LIMIT` | integer | `40` | lines shown per list (default 40) |
| `--labels LABELS` | path | none | a JSON object of label to text, naming hashed texts |
| `--from EVENT` | text | none | start both logs at their first 'kind name' EVENT |
<!-- command-reference:end -->
