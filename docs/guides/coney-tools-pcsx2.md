# The coney-tools PCSX2 and clip commands

The commands that record the original's behaviour in PCSX2 and compare it with Coney's: [`pcsx2`](#pcsx2) and
[`trace`](#trace) record and compare per-update traces, and [`clips`](#clips) checks, publishes and cleans up in-game
video clips, whether made in PCSX2 or by `coney --record`. The other groups are listed in
[the command line overview](coney-tools.md#groups).

## pcsx2 {#pcsx2}

The `pcsx2` commands drive the original in PCSX2 over PINE and the PCSX2-MCP fork's debug server, for runtime research
and parity checks. What they do and why, the scenario format and the address expressions are in
[Recording a trace](research-traces.md#recording-a-trace). The folders come from `coney.local.toml` (`pcsx2_dir`,
`game_dir`, `scratch_dir`) unless `--pcsx2-dir`, `--iso` and `--scratch` give them; PINE must be enabled in PCSX2's
`inis/PCSX2.ini`. Problems print one line and exit with code 2.

```sh
uv run --project python coney-tools pcsx2 setup-copies [--count N] [--dry-run]
```

Makes or updates the portable copies `pcsx2`, `pcsx2-b`, ... (`--count`, default `pcsx2_copies`, 5) under
`pcsx2_root` from the fork build `pcsx2_mcp_dir` names: the build's program files only, `portable.txt`, and in each
copy's ini PINE on `pcsx2_pine_base` + *i*, the debug server on `pcsx2_debug_base` + *i* and `SDL = false`. A copy's
settings, BIOS, memory cards, states and recordings stay; a copy whose PCSX2 runs is refused (exit code 1).
`--dry-run` prints what would change. Details: [PCSX2 copies](workspace.md#pcsx2-copies).

Several agents share the copies through claims ([Several at once](research-tools.md#several-at-once)); any
command that starts PCSX2 needs one.

```sh
uv run --project python coney-tools pcsx2 claim --agent ID [--copy NAME] [--json]
uv run --project python coney-tools pcsx2 release --agent ID [--copy NAME] [--force]
uv run --project python coney-tools pcsx2 status [--json]
uv run --project python coney-tools pcsx2 check-ports --agent ID [--debug-port N] [--pine-port N]
```

`claim` takes a free copy (or the named one) atomically and prints its folder, PINE port and debug server port (a line
`pcsx2-dir=... port=... debug-port=...`; `--json` has `port` and `debug_port`, which is `null` for a copy without the
debug server); it hands back your own claim when you hold one, and takes over a stale one (its processes gone, or
older than `--max-age-hours`, default 4, with no process) saying so. A claim whose copy has a live process is never
stale, whatever its age or owner: a PCSX2 running from the copy's folder, a live recorded PCSX2 or holder pid, or a
listening PINE or debug server port; that includes a claim whose owner file is unreadable (shown as holder `?`), which
is stale only after a few seconds and only with nothing running in the copy. A copy running PCSX2 without a claim is
never handed out. `release` closes the PCSX2 it recorded (only while that process is still a PCSX2) and drops the
claim; another agent's claim needs `--force`. `status` lists every copy with its holder, the time, the PINE and debug
ports, whether PCSX2 runs (by pid or by either port) and stale claims; it warns about copies whose ini has
`[InputSources] SDL = true`. The copies are the `pcsx2*` folders with an `inis/PCSX2.ini` under `pcsx2_root` (default:
`../../emulators`); two copies on one port (PINE or debug server) are refused, naming both. The claims are under
`pcsx2_claims_dir` (default: `pcsx2-claims/` in the shared scratch folder, the main checkout's `../../scratch`,
whatever a worktree's `scratch_dir` says, so every worktree sees the same claims).

**A port is only a number, and every copy's ports are readable** (`status`, the ini), so nothing may act on ports it
did not get from its own claim. `check-ports` succeeds (and prints `ok: ID holds NAME`) only when the ports given are
the debug server and PINE ports of one copy whose live claim ID holds, and exits 2 naming the holder otherwise
(`Registry.require_ports`, the check everything else below calls). From a script, `DebugClient.claimed("ID")`
(`coney_tools.debug_server`) connects to the debug port of the claim ID holds and to no other. For the shared `pcsx2`
MCP server, a hook makes the same check before `pcsx2_connect` ([The PCSX2 MCP guard](agents.md#pcsx2-guard)).

```sh
uv run --project python coney-tools pcsx2 keys --agent ID --copy NAME KEY [KEY...] [--hold-ms N] [--gap-ms N]
uv run --project python coney-tools pcsx2 screenshot --copy NAME --out PNG
```

Both are Windows-only and never focus a window. `keys` posts key down and up messages (`WM_KEYDOWN`) to the copy's
window by handle, one key after another (`W+K` presses two together; names: letters, digits, `space`, `return`,
arrows, `f1`..`f12`), each held `--hold-ms` (default 300). `screenshot` writes the game widget's picture, read by
handle, so it works while other windows cover it (not while it is minimised); it is refused inside the repository.

```sh
uv run --project python coney-tools pcsx2 prepare-state SOURCE OUT [--patch NAME ...]
```

Copies a save state (a `.p2s` file, or `slot:N` for quick-save slot N, which is only read) to `OUT` with the named
patches of `research/traces/patches/` (one file per pass) applied to its EE memory. It refuses an `OUT` inside the
repository or in PCSX2's `sstates/` folder, and refuses the copy when the state does not hold a patch's original
instruction words. PCSX2 saves states with zstd, which Python reads from 3.14 (uv installs it).

```sh
uv run --project python coney-tools pcsx2 repack-state SOURCE OUT
```

Copies a save state (a `.p2s` file or `slot:N`, only read) to `OUT` with every compressed entry rewritten with plain
deflate, the same contents byte for byte. Ghidra's zip reader, which the EE extension's `PCSX2SaveStateImporter.java`
script uses, cannot read PCSX2's zstd, and Python cannot write PCSX2's other choice, Deflate64; plain deflate is read
by all three, so PCSX2's own compression setting stays as it is. The same refusals as `prepare-state` apply. Why we
do not usually load states into Ghidra: [Tools](research-tools.md#tools).

```sh
uv run --project python coney-tools pcsx2 launch [STATE] --agent ID
```

Starts PCSX2 on a state file (`-fastboot -statefile`, the disc through a hard link in the scratch folder when its path
holds commas or parentheses), waits until the game runs, and leaves it running; with no state it boots the disc and
returns once PINE answers; when the copy runs the debug server, it also waits for that port, and prints both. It is the
only way agents start PCSX2: it never takes the keyboard focus (WMI starts the
process outside the agent's process tree; why: [Driving PCSX2](research-tools.md#driving-pcsx2)). With
`CONEY_WINDOW_DISPLAY` set ([Which display](running.md#window-display)), it moves each PCSX2 window onto that
display as it appears, without activating it; `pcsx2 record` does the same. It refuses to
start when something already serves PINE or the copy's debug server port. It runs under `--agent`'s claim (made, and
kept, if you hold none) and records
PCSX2's pid in it;
`--pcsx2-dir` must be that claim's copy.

```sh
uv run --project python coney-tools pcsx2 record SCENARIO --out CSV --agent ID [--state SOURCE] [--attach] [--keep-open]
                                     [--recording P2M2] [--run-record --library-state ID|unknown [--run-name NAME]]
```

Makes the scenario's patched state copy, starts PCSX2 on it, plays the scenario's input script and writes one CSV row
per character update, then closes PCSX2. It prints the updates recorded, the reads per poll, the time per poll and the
updates missed. The scenario names a quick-save slot (`slot`) or a state file under the scratch folder (`state`);
`--state` copies another state, `--attach` records a PCSX2 already running a patched state, and `--keep-open` leaves
PCSX2 running; the claim is released at the end when `record` made it. The CSV is a measurement of the game: it is
refused inside the repository. With
hooks among the scenario's patches it also writes each hook's call log to `<CSV stem>.<hook>.csv` beside it, and
makes the scenario's `calls` ([Hooks](research-traces.md#hooks)).

`--recording P2M2` takes the pad from a PCSX2 input recording instead of the scenario's input script, so a runtime
trace can be repeated exactly: the recording (and its start savestate `<P2M2>_SaveState.p2s`, patched as the
scenario says) is copied into the copy's `recordings/coney-record/`, PCSX2 starts on that savestate, and through the
debug server `record` pauses emulation, plays the recording (which loads its savestate again), samples step 0 and
resumes. The trace's steps then count from the recording's first frame. A recording that ends before the scenario's
`updates` stops with PCSX2 paused at its last frame and an error saying so; a power-on recording (no savestate) takes
no patches. It cannot be combined with `--attach` or `--state`, and needs the copy's debug server.

`--run-record` also writes the trace's [run record](research-evidence.md#run-records) beside the work, in
`research/runs/`: the scenario's name, its patches, the `--let` names, the counts (updates, missed updates, columns)
and the SHA-256 of the CSV, which stays in scratch. `--library-state` says which state of the library the run started
from (`unknown` is allowed); `--run-name` names the record (default: the scenario's). The record is written after the
CSV; a PCSX2 that no longer answers when its version is read gives `unknown` and a warning rather than losing the trace.

```sh
uv run --project python coney-tools pcsx2 recordings make NAME --script SCRIPT [--state STATE.p2s] [--updates N]
    [--frames-per-update 2] [--author TEXT] [--force]
```

Writes an input script (Coney's format, [Input scripts](running.md#input-scripts)) as a PCSX2 input recording
`NAME.p2m2` in the scratch folder's `recordings/`, so the original replays the script exactly, frame by frame, as Coney
does. A script's frame is one character update of 1/30 s and PCSX2 takes the pad at 60 Hz, so each script frame becomes
`--frames-per-update` frames (2). Port 1 follows the script with the pad bytes Coney makes (buttons with full
pressure, sticks from the percentages), port 2 stays at rest. `--state` is the savestate it starts from, copied beside
it as `NAME.p2m2_SaveState.p2s`; without it the recording starts from power-on. The length is the script's last frame
plus one second unless `--updates` says. A recording already there needs `--force`. Then `recordings push` it to a copy
and play it ([Input recordings](research-tools.md#input-recordings)). The layout it writes is PCSX2's own, from
`pcsx2/Recording/InputRecordingFile.h`.

```sh
uv run --project python coney-tools pcsx2 recordings push|pull NAME --agent ID --copy NAME [--force]
```

Copies an input recording (`NAME.p2m2` below `recordings/`, such as `fights/rumble-1.p2m2`) and its start savestate
between the shared scratch folder's `recordings/` and the claimed copy's `recordings/` (`push` to the copy, `pull`
back), checking each copy's SHA-256. A different file already there needs `--force`. Recordings are made and played
in the copy with the MCP server's `pcsx2_input_recording` ([Input recordings](research-tools.md#input-recordings)).

```sh
uv run --project python coney-tools pcsx2 clip STATE --out CLIP --agent ID [--seconds N] [--iso DISC] [--scratch DIR]
```

Records a short clip of the original (`--seconds`, 1 to 10, default 10) with **PCSX2's own video capture of its GS
output**: nothing on the desktop is read. For the run, the claimed copy's ini switches the capture on (with sound),
points PCSX2's videos folder at a staging folder beside the clip, turns every on-screen display element off (FPS,
messages, the recording indicator) and binds "Toggle Video Capture" to F12, which is posted to PCSX2's window by handle
to start and stop it; the ini is put back when PCSX2 closes. The capture is then re-encoded into `CLIP` as Coney's
clips are (960 x 720, H.264 and AAC, no metadata, cut at the length) through `clips.run_ffmpeg`, and the staging
folder deleted. PCSX2 2.9.94 tears the first ~1.15 s of its capture (a black band along the bottom), so the capture
runs 1.5 s longer (`CAPTURE_START_TRIM`) and the clip starts after that, still lasting `--seconds`.
`--out` is a path under the scratch folder, `<workspace>/scratch/<task>/clips/NAME.mp4`; give it absolute (the
folder `clips folder TASK` prints), because a relative `../../scratch/...` is wrong from a worktree, which sits three
levels below the workspace. The game comes from `game_dir` in `coney.local.toml`; without it pass `--iso DISC`,
and likewise `--scratch DIR` (an absolute path) without `scratch_dir`.
`CLIP` follows the [clips](#clips) rules. It needs FFmpeg as `clips` finds it, and Windows (keys by
handle). PCSX2 starts on `STATE` under `--agent`'s claim, released at the end when `clip` made it.

```sh
uv run --project python coney-tools pcsx2 recipe list
uv run --project python coney-tools pcsx2 recipe show NAME
uv run --project python coney-tools pcsx2 recipe run NAME --copy COPY --agent ID [--arg NAME=VALUE ...] [--json]
    [--record --state ID|unknown [--name NAME]]
uv run --project python coney-tools pcsx2 recipe render [--check]
```

The shared [PCSX2 recipes](pcsx2-recipes.md) of `research/pcsx2-recipes.toml` (the schema is the docstring of
`repo:python/src/coney_tools/recipes.py`). `list` names them by group, `show` prints one with its parameters,
preconditions, evidence and steps. `run` makes its steps on a copy `--agent` holds a claim on and whose PCSX2 runs,
over that copy's PINE port (and its debug server for `pause`, `resume` and `step`): it refuses another agent's copy, a
copy that runs nothing, an emulation state the recipe does not take and a running state that lacks a patch or hook the
recipe names (read back from memory). `--arg` gives a parameter, `--json` adds one line with every value shown.
`render` writes [the page](pcsx2-recipes.md) from the file; with `--check` it writes nothing and exits with 1 when
the page is out of date, as CI runs it. Every recipe must have an evidence level, a Ghidra address and a
`confirmed_runtime` date, copy and state, or the file is refused. A recipe may name a library state it starts from
(`state = "ID"`, shown among its preconditions; `recipe show` prints it).

`--record` makes a good run write its [run record](research-evidence.md#run-records), `research/runs/r-<date>-<name>.toml`
in the checkout: the numbers the `show` steps showed (texts stay out: a record holds numbers only), the arguments as
given, the recipe's patches and the PCSX2 version and copy, which the tool reads itself. `--state` says which library
state the copy was started from (`unknown` is allowed, and is written as such) because the runner cannot tell; the
state's patches are added from the library. `--name` is the run's name in its id (default: the recipe's). Every rule
of the record is checked before the recipe runs (the state, the id, a record that already exists, the who and the
arguments), and a run that fails writes nothing. A shown number the record cannot hold (a name that is not printable
ASCII, or one past the `runs` limits) is left out and counted, so a good run is always recorded. The command prints the
id to cite, `confirmed (runtime), run <id>`.

```sh
uv run --project python coney-tools pcsx2 record-run --agent ID --copy COPY --state ID|unknown --name NAME --method TEXT
    [--value NAME=NUMBER ...] [--address NAME=0xADDRESS ...] [--patch NAME ...]
```

Writes a [run record](research-evidence.md#run-records) for an observation made by hand, through the `pcsx2` MCP server:
a watch, a memory read, a count of frames. It refuses a copy `--agent` does not hold or whose PCSX2 is not running, and
reads the copy's PCSX2 version over PINE; the date, the agent and its model (from the identity the hook's
`CONEY_AGENT_TOKEN` names, marked `unverified` without one: [Who](stamps.md#who)), the copy and the state's patches
from the library are filled in by the tool. `--method` is how the value was observed, in words; `--value` is a number
read and `--address` an address of the original the run names, both repeatable, within the
[record's limits](research-evidence.md#run-records).
Nothing else goes in: no memory, no game text. The record is `research/runs/r-<date>-<name>.toml`; it is never
overwritten, so a second run of the same name on the same day needs another `--name`. It prints the id to cite.

```sh
uv run --project python coney-tools pcsx2 states list [--level LEVEL] [--tag TAG] [--json]
uv run --project python coney-tools pcsx2 states show ID [--json]
uv run --project python coney-tools pcsx2 states add FILE --id ID --desc TEXT [--level L] [--checkpoint N] [--tag T ...]
    [--made-by TEXT] [--author NAME] [--copy COPY --agent ID] [--position X,Y,Z] [--heading DEG] [--no-read]
uv run --project python coney-tools pcsx2 states use ID --copy COPY --agent ID [--patch NAME ...] [--launch]
uv run --project python coney-tools pcsx2 states verify [ID ...]
uv run --project python coney-tools pcsx2 states render [--out PAGE]
```

The shared [state library](pcsx2-states.md): save states of the original, each with a description, so nobody drives
to the same place twice. The library is a folder beside the repository (save states are game memory), the
`pcsx2_states_dir` of `coney.local.toml` or else `state-library` in the workspace's `emulators/` folder, holding one
read-only `ID.p2s` per state and a `catalog.json` (the fields: [The catalog](pcsx2-states.md#the-catalog)). `list`
filters by level or tag and `show` prints an entry in full. `add` copies a `.p2s` file (or `slot:N`, only read) in
under a new id (an existing id, or a file already there, is refused), checks the copy's SHA-256, makes it read-only
and records the level, checkpoint, player position and heading by running the `where` and `position` recipes over the
state's own memory, or over a claimed copy's PINE port with `--copy`; flags win over both, and `unknown` is allowed for
the level and checkpoint. The PCSX2 version and the date come from the file, and the patches it holds from comparing
its memory with `research/traces/patches/`. Catalog writes hold a lock file, so two agents adding at once both
land. `use` writes a copy of a state into the claimed copy's `library-states/` folder (after checking the library file
against the catalog), with any `--patch` applied as `prepare-state` does, and prints the path; `--launch` starts PCSX2
on it. The library file is never played on or changed. `verify` hashes the files against the catalog and lists any
that is missing, changed or not listed (exit code 1). `render` rewrites the list between the markers of
[the page](pcsx2-states.md#the-states) from the catalog.

## clips {#clips}

```sh
uv run --project python coney-tools clips folder TASK
uv run --project python coney-tools clips check CLIP [CLIP...]
uv run --project python coney-tools clips publish (--pr N | --issue N) [--edit] (--body TEXT | --body-file FILE) CLIP [CLIP...]
uv run --project python coney-tools clips clean [--older-than HOURS] [--dry-run]
```

In-game video clips for issues and pull requests, made by `coney --record` ([Recording a clip](running.md#record)) or
`pcsx2 clip` (above). The rules and why: [Clips](testing.md#clips); the limits are `workspace.clips` in
`.github/conventions.yml`.

* `folder` makes and prints a task's clips folder, `<scratch>/<task>/clips/`, where `<scratch>` is the workspace's
  shared scratch folder (`../../scratch` beside the main checkout).
* `check` runs ffprobe on each clip and prints its length, size, picture and whether it has sound. It refuses a clip
  outside a task's clips folder or inside a git checkout, one longer than `max_seconds` (10), and one with any tag
  beyond the fixed ones an MP4's boxes carry (`major_brand`, `minor_version`, `compatible_brands`, `language`,
  `handler_name`, `vendor_id` and a version-free `encoder`); one bigger than `warn_mb` (10 MB) gets a warning.
* `publish` makes the same checks, then uploads every clip in one gh command, `gh pr|issue comment N` (or `edit` with
  `--edit`, into the description) with one `--attach` per clip, and **deletes the clips once gh succeeds**. When gh
  fails, it keeps them and says so (exit 1). A body may place a clip with a `./clips/<name>.mp4` reference, which gh
  replaces with the upload when the clip is named the same way. It is the only way clips are uploaded.
* `clean` deletes every file in the task clips folders older than `keep_hours` (24; `--older-than` overrides it),
  including what a failed recording left (part files, a `pcsx2 clip` staging folder); `--dry-run` only counts.

FFmpeg and ffprobe are found in `ffmpeg_dir` of `coney.local.toml`, else beside `CONEY_FFMPEG` (the variable Coney's
`--record` reads), else on `PATH`. Every FFmpeg command the tools run goes through `clips.run_ffmpeg`, which refuses a
capture device or any input that is not a file.

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
original's `pcsx2 record` of the all-humans field set; [Tracing](running.md#tracing)), by the rules of
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
