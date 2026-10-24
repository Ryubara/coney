# The coney-tools PCSX2 and clip commands

The commands that record the original's behaviour in PCSX2: [`pcsx2`](#pcsx2) drives the emulator and records
per-update traces ([`trace`](coney-tools-trace.md#trace) compares them with Coney's), and [`clips`](#clips) checks,
publishes and cleans up in-game video clips, whether made in PCSX2 or by `coney --record`. The other groups are listed
in [the command line overview](coney-tools.md#groups).

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
`CONEY_WINDOW_DISPLAY` set ([Which display](running-options.md#window-display)), it moves each PCSX2 window onto that
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

Writes an input script (Coney's format, [Input scripts](running-input.md#input-scripts)) as a PCSX2 input recording
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
`render` writes [the index](pcsx2-recipes.md) (the intro and every recipe with its summary) and one page per recipe
group in `docs/guides/pcsx2-recipes/` (each recipe's full section, at the anchor of its name) from the file, and removes
a group page no group makes any more (only one that carries the generated marker: a page in that folder without it is
kept, reported, and makes `render` exit 1 with and without `--check`); with `--check` it writes nothing and exits with 1
when any page is out of date, as CI runs it. Cite a recipe by its group page, `pcsx2-recipes/<group>.md#<name>`; the
index lists every recipe with an anchor of its name too, so `pcsx2-recipes.md#<name>` also lands on it, one link from
the section. Every recipe must have an evidence level, a Ghidra address and a `confirmed_runtime` date, copy and state,
or the file is refused. A recipe may name a library state it starts from (`state = "ID"`, shown among its preconditions;
`recipe show` prints it).

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

<!-- command-reference:begin pcsx2 -->
## Parameters of `pcsx2` {#pcsx2-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `pcsx2 prepare-state` {#pcsx2-prepare-state-usage}

Copy a save state with patches applied to its EE memory.

```text
coney-tools pcsx2 prepare-state SOURCE OUT [--patch NAME [NAME ...]] [--pcsx2-dir PCSX2_DIR]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `SOURCE` | text | required | a .p2s file, or slot:N for quick-save slot N (only read) |
| `OUT` | path | required | the patched copy (outside the repository and sstates/) |
| `--patch NAME [NAME ...]` | text | none | patches of research/traces/patches/ |
| `--pcsx2-dir PCSX2_DIR` | path | none | the portable PCSX2 folder; default: pcsx2_dir |

### `pcsx2 repack-state` {#pcsx2-repack-state-usage}

Copy a save state with plain deflate in place of zstd.

```text
coney-tools pcsx2 repack-state SOURCE OUT [--pcsx2-dir PCSX2_DIR]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `SOURCE` | text | required | a .p2s file, or slot:N for quick-save slot N (only read) |
| `OUT` | path | required | the repacked copy (outside the repository and sstates/) |
| `--pcsx2-dir PCSX2_DIR` | path | none | the portable PCSX2 folder; default: pcsx2_dir |

### `pcsx2 launch` {#pcsx2-launch-usage}

Start PCSX2 (never taking the focus) on a state file and wait until its game runs.

```text
coney-tools pcsx2 launch [STATE] [--agent AGENT] [--pcsx2-dir PCSX2_DIR] [--iso ISO] [--scratch SCRATCH]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[STATE]` | path | none | a .p2s file (a patched copy); omitted: boot the disc and wait for PINE |
| `--agent AGENT` | text | none | your id; PCSX2 starts under your claim (made if you hold none) |
| `--pcsx2-dir PCSX2_DIR` | path | none | the portable PCSX2 folder; default: pcsx2_dir |
| `--iso ISO` | path | none | the disc image (or a folder with one); default: game_dir |
| `--scratch SCRATCH` | path | none | where state copies and the disc link go; default: scratch_dir |

### `pcsx2 record` {#pcsx2-record-usage}

Play a scenario on the original and write its per-update trace.

```text
coney-tools pcsx2 record SCENARIO --out OUT [--state STATE] [--attach] [--keep-open] [--agent AGENT]
    [--recording RECORDING] [--let NAME=EXPR] [--run-record] [--library-state LIBRARY_STATE] [--run-name RUN_NAME]
    [--pcsx2-dir PCSX2_DIR] [--iso ISO] [--scratch SCRATCH]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `SCENARIO` | path | required | a scenario TOML (research/traces/scenarios/) |
| `--out OUT` | path | required | the trace CSV (outside the repository) |
| `--state STATE` | text | none | the state to copy instead of the scenario's slot: a .p2s file or slot:N |
| `--attach` | flag | off | record a PCSX2 already running a patched state |
| `--keep-open` | flag | off | leave PCSX2 running afterwards |
| `--agent AGENT` | text | none | your id; runs under your claim (made, and released at the end, if none) |
| `--recording RECORDING` | path | none | an input recording (.p2m2, its savestate beside it) to replay through the debug server as the pad |
| `--let NAME=EXPR` | text | none | give or replace a name of the scenario's let (a random stream's offset per match); repeatable |
| `--run-record` | flag | off | also write research/runs/\<id\>.toml (scenario, patches, counts, the CSV's hash) for a claim to cite |
| `--library-state LIBRARY_STATE` | text | none | with --run-record: the state library id the run started from, or unknown |
| `--run-name RUN_NAME` | text | none | with --run-record: the run's name in its id (default: the scenario's) |
| `--pcsx2-dir PCSX2_DIR` | path | none | the portable PCSX2 folder; default: pcsx2_dir |
| `--iso ISO` | path | none | the disc image (or a folder with one); default: game_dir |
| `--scratch SCRATCH` | path | none | where state copies and the disc link go; default: scratch_dir |

### `pcsx2 clip` {#pcsx2-clip-usage}

Record a short clip of the original with PCSX2's own capture of its game output (never the screen).

```text
coney-tools pcsx2 clip STATE --out OUT [--seconds SECONDS] [--agent AGENT] [--pcsx2-dir PCSX2_DIR] [--iso ISO]
    [--scratch SCRATCH]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `STATE` | path | required | the .p2s state to start from (a copy outside sstates/) |
| `--out OUT` | path | required | the clip: \<scratch\>/\<task\>/clips/\<name\>.mp4 |
| `--seconds SECONDS` | integer | `10` | how long, 1 to the cap of 10 (default 10) |
| `--agent AGENT` | text | none | your id; runs under your claim (made, and released at the end, if none) |
| `--pcsx2-dir PCSX2_DIR` | path | none | the portable PCSX2 folder; default: pcsx2_dir |
| `--iso ISO` | path | none | the disc image (or a folder with one); default: game_dir |
| `--scratch SCRATCH` | path | none | where state copies and the disc link go; default: scratch_dir |

### `pcsx2 claim` {#pcsx2-claim-usage}

Claim a free PCSX2 copy for an agent (every PCSX2 use starts here).

```text
coney-tools pcsx2 claim --agent AGENT [--copy COPY] [--json] [--max-age-hours MAX_AGE_HOURS]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--agent AGENT` | text | required | your id: letters, digits, '.', '_' and '-' |
| `--copy COPY` | text | none | a copy by name (pcsx2, pcsx2-b, ...); default: the first free one |
| `--json` | flag | off | print the claim as one JSON line |
| `--max-age-hours MAX_AGE_HOURS` | number | none | a claim with no process is stale after this (default 4) |

### `pcsx2 release` {#pcsx2-release-usage}

Close the copy's PCSX2 and drop the claim.

```text
coney-tools pcsx2 release --agent AGENT [--copy COPY] [--force]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--agent AGENT` | text | required | your id |
| `--copy COPY` | text | none | one copy; default: all of the agent's |
| `--force` | flag | off | release another agent's claim |

### `pcsx2 status` {#pcsx2-status-usage}

Who holds each PCSX2 copy and whether its PCSX2 runs.

```text
coney-tools pcsx2 status [--json] [--max-age-hours MAX_AGE_HOURS]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--json` | flag | off | print JSON instead of a table |
| `--max-age-hours MAX_AGE_HOURS` | number | none | a claim with no process is stale after this (default 4) |

### `pcsx2 check-ports` {#pcsx2-check-ports-usage}

Exit 2 unless the ports are those of a copy you hold (the check the pcsx2 MCP guard makes).

```text
coney-tools pcsx2 check-ports --agent AGENT [--debug-port DEBUG_PORT] [--pine-port PINE_PORT]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--agent AGENT` | text | required | your id (must hold the claim) |
| `--debug-port DEBUG_PORT` | integer | none | the debug server port you are about to connect to |
| `--pine-port PINE_PORT` | integer | none | the PINE port you are about to connect to |

### `pcsx2 keys` {#pcsx2-keys-usage}

Post keys to a claimed copy's window by handle (never takes focus).

```text
coney-tools pcsx2 keys KEY [KEY ...] --agent AGENT --copy COPY [--hold-ms HOLD_MS] [--gap-ms GAP_MS]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `KEY [KEY ...]` | text | required | a key (space, return, up, f4, w, ...); W+K presses both |
| `--agent AGENT` | text | required | your id (must hold the claim) |
| `--copy COPY` | text | required | the copy |
| `--hold-ms HOLD_MS` | integer | `300` | how long each key stays down (default 300) |
| `--gap-ms GAP_MS` | integer | `100` | pause after each key (default 100) |

### `pcsx2 screenshot` {#pcsx2-screenshot-usage}

Write a copy's window as a PNG, read by handle (no focus change).

```text
coney-tools pcsx2 screenshot --copy COPY --out OUT
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--copy COPY` | text | required | the copy |
| `--out OUT` | path | required | the PNG (outside the repository) |

### `pcsx2 setup-copies` {#pcsx2-setup-copies-usage}

Make or update the PCSX2 copies from the PCSX2-MCP fork build pcsx2_mcp_dir names.

```text
coney-tools pcsx2 setup-copies [--count COUNT] [--dry-run]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--count COUNT` | integer | none | how many copies: pcsx2, pcsx2-b, ... (default: pcsx2_copies, 5) |
| `--dry-run` | flag | off | print what would change, change nothing |

### `pcsx2 recordings` {#pcsx2-recordings-usage}

Make an input recording from an input script, or move one (.p2m2 and its savestate) between scratch and a claimed copy.

```text
coney-tools pcsx2 recordings DIRECTION NAME [--agent AGENT] [--copy COPY] [--force] [--script SCRIPT] [--state STATE]
    [--updates UPDATES] [--frames-per-update FRAMES_PER_UPDATE] [--author AUTHOR]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `DIRECTION` | choice | required | push: scratch to the copy's recordings/; pull: back; make: write one from an input script. One of `push`, `pull`, `make`. |
| `NAME` | text | required | the recording below recordings/, such as fights/rumble-1.p2m2 |
| `--agent AGENT` | text | none | push and pull: your id (must hold the claim) |
| `--copy COPY` | text | none | push and pull: the copy |
| `--force` | flag | off | replace a different file of that name |
| `--script SCRIPT` | path | none | make: the input script (Coney's format) |
| `--state STATE` | path | none | make: the savestate it starts from (none: from power-on) |
| `--updates UPDATES` | integer | none | make: how many 1/30 s updates (default: the script and 1 s) |
| `--frames-per-update FRAMES_PER_UPDATE` | integer | `2` | make: PCSX2 frames in one script frame (default 2) |
| `--author AUTHOR` | text | none | make: the author written into the header |

### `pcsx2 recipe list` {#pcsx2-recipe-list-usage}

List every recipe with its one-line summary.

```text
coney-tools pcsx2 recipe list
```

Takes no parameters.

### `pcsx2 recipe show` {#pcsx2-recipe-show-usage}

Print one recipe: what it does, its parameters, preconditions and steps.

```text
coney-tools pcsx2 recipe show NAME
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `NAME` | text | required | the recipe's name (pcsx2 recipe list shows them) |

### `pcsx2 recipe run` {#pcsx2-recipe-run-usage}

Run a recipe on a PCSX2 copy you hold a claim on.

```text
coney-tools pcsx2 recipe run NAME --copy COPY --agent AGENT [--arg NAME=VALUE] [--json] [--record] [--state STATE]
    [--name RUN_NAME]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `NAME` | text | required | the recipe's name (pcsx2 recipe list shows them) |
| `--copy COPY` | text | required | the claimed copy (pcsx2, pcsx2-b, ...) |
| `--agent AGENT` | text | required | your id (must hold the claim) |
| `--arg NAME=VALUE` | text | none | a parameter; repeatable |
| `--json` | flag | off | print what the show steps showed as one JSON line too |
| `--record` | flag | off | after a good run, write research/runs/\<id\>.toml: the numbers it showed, for a runtime claim to cite |
| `--state STATE` | text | none | with --record: the state library id the copy started from, or unknown |
| `--name RUN_NAME` | text | none | with --record: the run's name in its id (default: the recipe's name) |

### `pcsx2 recipe render` {#pcsx2-recipe-render-usage}

Write the recipe pages (pcsx2-recipes.md and pcsx2-recipes/) from the recipe file.

```text
coney-tools pcsx2 recipe render [--check]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--check` | flag | off | write nothing; exit 1 when a page is out of date |

### `pcsx2 record-run` {#pcsx2-record-run-usage}

Write a run record for an observation made by hand on a PCSX2 copy you hold (research/runs/).

```text
coney-tools pcsx2 record-run --agent AGENT --copy COPY --state STATE --name NAME --method METHOD [--value NAME=NUMBER]
    [--address NAME=0xADDRESS] [--patch NAME]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--agent AGENT` | text | required | your id (must hold the claim) |
| `--copy COPY` | text | required | the claimed copy the observation was made on |
| `--state STATE` | text | required | the state library id the copy started from, or unknown |
| `--name NAME` | text | required | what the run is about; the record's id is r-\<date\>-\<name\> |
| `--method METHOD` | text | required | how it was observed, in words (a watch, a read, a frame count) |
| `--value NAME=NUMBER` | text | none | a number read; repeatable |
| `--address NAME=0xADDRESS` | text | none | an address named; repeatable |
| `--patch NAME` | text | none | a patch the run needed; repeatable |

### `pcsx2 states list` {#pcsx2-states-list-usage}

List the states, filtered by level or tag.

```text
coney-tools pcsx2 states list [--level LEVEL] [--tag TAG] [--json]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--level LEVEL` | text | none | only this level (level99) |
| `--tag TAG` | text | none | only states with this tag |
| `--json` | flag | off | print the entries as JSON |

### `pcsx2 states show` {#pcsx2-states-show-usage}

Print one state's whole catalog entry.

```text
coney-tools pcsx2 states show ID [--json]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `ID` | text | required | the state's id (pcsx2 states list shows them) |
| `--json` | flag | off | print the entry as JSON |

### `pcsx2 states add` {#pcsx2-states-add-usage}

Copy a state file into the library and describe it.

```text
coney-tools pcsx2 states add FILE --id ID --desc DESC [--level LEVEL] [--checkpoint CHECKPOINT] [--tag TAG]
    [--made-by MADE_BY] [--author AUTHOR] [--made DATE] [--pcsx2 PCSX2] [--position X,Y,Z] [--heading DEG] [--copy COPY]
    [--agent AGENT] [--no-read] [--pcsx2-dir PCSX2_DIR]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `FILE` | text | required | a .p2s file, or slot:N for quick-save slot N (only read) |
| `--id ID` | text | required | the state's id: a short slug (pedestrian-grab) |
| `--desc DESC` | text | required | what the scene shows and what it is set up for |
| `--level LEVEL` | text | none | the level (level99) or unknown; default: read from the state |
| `--checkpoint CHECKPOINT` | text | none | the checkpoint number or unknown; default: read from the state |
| `--tag TAG` | text | none | a tag (repeatable): grab, fence, ... |
| `--made-by MADE_BY` | text | none | the recipe or steps that produced it |
| `--author AUTHOR` | text | none | who made it (default: --agent) |
| `--made DATE` | text | none | when it was made, YYYY-MM-DD (default: the file's own date) |
| `--pcsx2 PCSX2` | text | none | the PCSX2 version that saved it (default: the file's own) |
| `--position X,Y,Z` | text | none | the player's position in metres (default: read) |
| `--heading DEG` | number | none | the player's heading in degrees (default: read) |
| `--copy COPY` | text | none | read level, checkpoint, position and heading over PINE from this running copy |
| `--agent AGENT` | text | none | your id (must hold the claim on --copy) |
| `--no-read` | flag | off | read nothing from the state: give everything by flags |
| `--pcsx2-dir PCSX2_DIR` | path | none | the portable PCSX2 folder for slot:N; default: pcsx2_dir |

### `pcsx2 states use` {#pcsx2-states-use-usage}

Hand out a copy of a state to a PCSX2 copy you hold a claim on.

```text
coney-tools pcsx2 states use ID --copy COPY --agent AGENT [--patch NAME [NAME ...]] [--launch]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `ID` | text | required | the state's id (pcsx2 states list shows them) |
| `--copy COPY` | text | required | the claimed copy (pcsx2, pcsx2-b, ...) |
| `--agent AGENT` | text | required | your id (must hold the claim) |
| `--patch NAME [NAME ...]` | text | none | patches of research/traces/patches/ |
| `--launch` | flag | off | start PCSX2 on the copy of the state |

### `pcsx2 states verify` {#pcsx2-states-verify-usage}

Hash the library's files against the catalog.

```text
coney-tools pcsx2 states verify [ID ...]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[ID ...]` | text | none | only these (default: every state) |

### `pcsx2 states render` {#pcsx2-states-render-usage}

Write the library's list into docs/guides/pcsx2-states.md.

```text
coney-tools pcsx2 states render [--out OUT]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--out OUT` | path | none | another page with the marked block (default: the guide) |
<!-- command-reference:end -->

## clips {#clips}

```sh
uv run --project python coney-tools clips folder TASK
uv run --project python coney-tools clips check CLIP [CLIP...]
uv run --project python coney-tools clips publish (--pr N | --issue N) [--edit] (--body TEXT | --body-file FILE) CLIP [CLIP...]
uv run --project python coney-tools clips clean [--older-than HOURS] [--dry-run]
```

In-game video clips for issues and pull requests, made by `coney --record` ([Recording a
clip](running-options.md#record)) or `pcsx2 clip` (above). The rules and why: [Clips](testing.md#clips); the limits are
`workspace.clips` in `.github/conventions.yml`.

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

<!-- command-reference:begin clips -->
## Parameters of `clips` {#clips-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `clips folder` {#clips-folder-usage}

Make and print a task's clips folder (\<scratch\>/\<task\>/clips).

```text
coney-tools clips folder TASK
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `TASK` | text | required | the task's scratch folder name (your branch or track) |

### `clips check` {#clips-check-usage}

Check clips as publish does (place, length, metadata), upload nothing.

```text
coney-tools clips check CLIP [CLIP ...]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `CLIP [CLIP ...]` | path | required | the clip files to check |

### `clips publish` {#clips-publish-usage}

Upload clips with gh --attach to a pull request or issue, then delete them.

Exactly one of `--pr`, `--issue` is required.

```text
coney-tools clips publish CLIP [CLIP ...] (--pr PR | --issue ISSUE) [--edit] [--body BODY | --body-file BODY_FILE]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `CLIP [CLIP ...]` | path | required | the clip files to upload |
| `--pr PR` | integer | none | the pull request's number. Not with `--issue`. |
| `--issue ISSUE` | integer | none | the issue's number. Not with `--pr`. |
| `--edit` | flag | off | put them into the description instead of a comment |
| `--body BODY` | text | none | the comment's (or description's) text. Not with `--body-file`. |
| `--body-file BODY_FILE` | path | none | a file with the text; ./clips/\<name\>.mp4 references are replaced. Not with `--body`. |

### `clips clean` {#clips-clean-usage}

Delete clips left in the scratch clips folders past the age limit.

```text
coney-tools clips clean [--older-than HOURS] [--dry-run]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--older-than HOURS` | number | none | default: workspace.clips.keep_hours (24) |
| `--dry-run` | flag | off | list what would go, delete nothing |
<!-- command-reference:end -->
