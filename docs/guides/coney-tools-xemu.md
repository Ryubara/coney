# The coney-tools xemu commands

The commands that run the Xbox build of *The Warriors* in xemu, so a claim about the Xbox can be checked in the running
game the way PCSX2 checks the PS2: [`xemu`](#xemu) makes the copies from your own xemu install, hands them out one per
agent, starts them, reads memory and writes [run records](#run-records); the [`xemu` MCP server](#mcp) reads and drives
the game for agents. The PS2 stays the only reference for behaviour: what the Xbox does is evidence for the Xbox, never
for the PS2 ([Xbox executable](../research/xbox-executable.md)). The other groups are listed in
[the command line overview](coney-tools.md#groups).

## xemu {#xemu}

xemu is a QEMU-based Xbox emulator. It needs files from your own console: the MCPX boot ROM, the flash BIOS and a
hard-disk image, and a disc image of your own game. **None of them, and nothing made from them, ever enters the
repository, an issue, CI or a log** ([LEGAL.md](repo:LEGAL.md#no-game-data)): the copies live beside the repository in
the workspace's `emulators/` folder, and the tools refuse a copy folder, a disc or a screenshot inside a checkout. The
folders and ports come from `coney.local.toml` ([the `xemu_*` keys](workspace.md#local-config)). Problems print one line
and exit with code 2.

```sh
uv run --project python coney-tools xemu find-install
uv run --project python coney-tools xemu setup-copies [--count N] [--refresh-hdd] [--dry-run]
uv run --project python coney-tools xemu claim --agent ID [--copy NAME] [--json]
uv run --project python coney-tools xemu launch --agent ID [--dvd IMAGE] [--paused]
uv run --project python coney-tools xemu read --agent ID ADDRESS [--type u32|f32|...] [--count N]
uv run --project python coney-tools xemu record-run --agent ID --name NAME --method "..." --value NAME=NUMBER ...
uv run --project python coney-tools xemu release --agent ID [--copy NAME] [--force]
uv run --project python coney-tools xemu status [--json]
uv run --project python coney-tools xemu check-ports --agent ID [--gdb-port N] [--monitor-port N]
```

`find-install` looks for an `xemu.exe` in the usual places (your Desktop, Downloads and Documents, the program folders,
three levels down) and prints each with its settings file; it only reads, and exits 1 when it finds none. Set
`xemu_install` to the one to use.

### The copies {#copies}

`setup-copies` makes or updates `xemu`, `xemu-b`, ... (`--count`, default `xemu_copies`, 2) under `xemu_root` from the
install `xemu_install` names. The install's settings are its `xemu.toml`: `xemu_config` when set, else the one beside
`xemu.exe` (a portable install), else `%APPDATA%/xemu/xemu/xemu.toml`. The install is only read. Each copy gets:

| In the copy | What it is |
| --- | --- |
| `xemu.exe` and its `.dll` files | the install's program files, and nothing else: a file the settings name is never taken for one |
| `xemu.toml` | the install's settings with the copy's own disk image and EEPROM, the [headless settings](#settings), and the boot ROM and BIOS of the install by absolute path (read in place, never copied) |
| `hdd/xbox_hdd.qcow2` | the copy's own hard-disk image, copied once from the install's: xemu locks the image while it runs, and the image holds the copy's [snapshots](#snapshots) |
| `eeprom.bin` | the copy's EEPROM, copied once from the install's when it has one |
| `coney-copy.toml` | Coney's file: the copy's GDB stub port (`xemu_gdb_base` + *i*) and monitor port (`xemu_monitor_base` + *i*), which `launch` passes on xemu's command line |

A second run copies only what changed and keeps each copy's disk image, and so its snapshots; `--refresh-hdd` copies
the install's image again. A copy whose xemu runs is refused (exit code 1). `--dry-run` prints what would change.
xemu reads `xemu.toml` beside its executable before the user's own, which is what keeps the copies apart (portable
mode, confirmed on 0.8.139: its log says `base path: <the copy's folder>`, and it writes its log, shader cache and
settings there, never into the install).

Until this page's tools are on `main`, put `xemu_install` and `xemu_dvd` in the **worktree's own** `coney.local.toml`
(a copy of the main one, which a worktree reads first), not in the main checkout's: a coney-tools without these keys
refuses the file as having unknown keys, and so breaks every other worktree's checks. After the merge they belong in
the main file.

### Headless settings {#settings}

What every copy's `xemu.toml` gets on top of the install's settings, so that an agent's xemu stays out of the user's
way and its frames compare (`COPY_SETTINGS` in `repo:python/src/coney_tools/xemu_copies.py`):

| Table and key | Value | Why |
| --- | --- | --- |
| `general` `show_welcome`, `skip_boot_anim` | `false`, `true` | no window to dismiss, a shorter boot |
| `general.updates` `check` | `false` | no update prompt |
| `display.window` `fullscreen_on_startup`, `startup_size` | `false`, `640x480` | a window of a fixed size, never the whole screen |
| `display.quality` `surface_scale` | `1` | a fixed render scale, so frames of two runs compare |
| `display.ui` `show_notifications` | `false` | nothing drawn over the game |
| `input` `auto_bind`, `background_input_capture` | `false`, `false` | the user's gamepad never drives an agent's game |
| `input.bindings` `port1` | `keyboard` | the virtual pad: port 1 takes keys posted to the copy's window by handle |

xemu warns of a key it does not know, in the `xemu.log` beside its executable (`Warning: unrecognized key '...'`):
read that log after a change to the table. Checked live on xemu 0.8.139 (Windows): every key is known, `skip_boot_anim`
reaches QEMU as `short-animation=on` and the window is 640 x 480; the first draft's `fullscreen_on_boot` was wrong
(the key is `fullscreen_on_startup`). That `port1 = 'keyboard'` is the keyboard binding is not yet exercised: no
key has been sent to a copy. `launch` starts xemu through WMI, outside the agent's process tree, so it cannot take
the keyboard focus ([How the launcher keeps the focus off](research-tools.md#driving-pcsx2)).

### Claims {#claims}

The copies are shared through claims, with the rules of the [PCSX2 copies](research-tools.md#several-at-once): `claim`
takes a free copy (or the named one) atomically and prints its folder, GDB port and monitor port (`--json` for scripts),
hands back the claim you hold, and takes over a stale one, saying so. A claim is stale when nothing of it lives (its
recorded xemu and holder are gone, no xemu runs from the copy's folder and nothing serves its ports) and it names pids
or is older than `--max-age-hours` (default 4). A copy that runs xemu without a claim is not handed out. `release`
closes the copy's xemu and drops the claim; another agent's live claim needs `--force`. `status` shows every copy, its
holder and whether its xemu runs, from the claim folders and the processes now. `check-ports` exits 2 unless the ports
are those of a copy the agent holds: the check a script makes before pointing a client at ports it was given. The
claims are in `xemu-claims/` of the main checkout's shared scratch folder (`xemu_claims_dir` moves them), never a
worktree's own `scratch_dir`.

### Launching {#launch}

`launch` starts your claimed copy (it claims one when you hold none) on the disc image `--dvd` names, else
`xemu_dvd`, and returns once the monitor answers (`--wait`, default 60 s). It writes the disc into the copy's
`[sys.files] dvd_path` and starts xemu with `-gdb tcp:127.0.0.1:<gdb port>` and
`-monitor tcp:127.0.0.1:<monitor port>,server,nowait`, both on localhost only; `--paused` adds `-S`, so the guest waits
at its first instruction for a breakpoint. A copy whose xemu already runs is left as it is. xemu reads XISO images; a
full disc image may need converting with `extract-xiso` first.

### The GDB stub {#gdb}

xemu inherits QEMU's GDB stub, the remote protocol GDB speaks (`repo:python/src/coney_tools/gdb_remote.py`). The
guest is 32-bit x86, and its virtual addresses are the XBE's (base `0x00010000`), so an address from a research page
needs no translation. The stub is all-stop, which shapes every tool:

- **Attaching stops the guest.** The tools resume a game that ran. QEMU also sends the stop (`T02`) unasked, ahead of
  the answer to the first packet, when the guest ran at the attach; the client skips it (`connect` in `gdb_remote`).
  Taken for the answer it shifted every reply by one, and every other `xemu read` of a running game failed with `not a
  stop reply: 'PacketSize=...'` (found live, 6 of 12 attaches).
- **A running guest reads nothing.** QEMU takes any byte it is sent then as a request to stop, so each read, write,
  register or breakpoint request stops the guest for as long as it takes and lets it run again: a read of a running
  game is a pause of milliseconds.
- **One debugger at a time.** A second connection to a copy's stub waits or is refused; `xemu_disconnect` and
  `release` free it.

`read` is a quick look from a shell: `--type` `hex` (default), `u8`, `u16`, `u32`, `i32` or `f32`, `--count` values,
4 KiB at most. A read is for numbers, never for a dump of the game's memory.

### The monitor {#monitor}

QEMU's human monitor on the copy's monitor port (`repo:python/src/coney_tools/qemu_monitor.py`) answers what the stub
cannot: whether the guest runs (`info status`) without stopping it, pause and resume (`stop`, `cont`), the version
(`info version`) and the snapshots (`savevm`, `loadvm`, `delvm`, `info snapshots`). The tools keep the stub's view of
the guest in step with the monitor's, so a pause from xemu's own menu is seen (a "paused" with no stop from the stub is
asked a second time first: the two sockets have no order between them). Two things differ from stock QEMU
(xemu 0.8.139, QEMU 10.2.0):

- `info version` answers QEMU's version (`10.2.0`), never xemu's. xemu's own release is the `xemu_version:` line at the
  top of the copy's `xemu.log`, which `status` and the [run records](#run-records) read (`xemu v0.8.139 (QEMU 10.2.0)`).
- There is **no `screendump`** (`unknown command`): see [the screenshot](#screenshot).

### The xemu MCP server {#mcp}

`.mcp.json` registers `xemu`, Coney's own server (`repo:python/src/coney_tools/xemu_mcp.py`), started through
[`coney-tools mcp serve xemu`](coney-tools-setup.md#mcp). Every tool takes **`agent`**, the id you claimed a copy
with, and the server connects that id only to the copy it holds now, read from the claims on every call: there are no
ports to pass. Claim in a shell first, so [the hook](agents.md#xemu-guard) knows the id is yours.

| Tool | What it does | Reviewer |
| --- | --- | --- |
| `xemu_status` | the copy, its ports, running or stopped, xemu's version, why it stopped and where (`eip`), your points | yes |
| `xemu_read_memory` | `address`, `type` (`hex` or a value type) and `count`: hex rows or values, 4 KiB at most | yes |
| `xemu_write_memory` | `address` and `hex` bytes, or one `value` of a `type` | |
| `xemu_read_registers` | the i386 core registers, `eax` to `gs` | yes |
| `xemu_write_register` | one register; pause first | |
| `xemu_set_breakpoint`, `xemu_remove_breakpoint` | `kind` `break` (default) or `hardware`, or a watchpoint `write`, `read` or `access` of `length` bytes | |
| `xemu_pause`, `xemu_continue`, `xemu_step` | pause; run (with `wait_seconds`, up to 60, wait for a point to hit and say where); run `count` instructions | |
| `xemu_screenshot` | a PNG of the screen (`out`, default `xemu/<copy>-<time>.png` in the shared scratch folder; never inside a checkout; `width` and `height` of the [front buffer](#screenshot), default 640 x 480) | yes |
| `xemu_list_snapshots`, `xemu_save_snapshot`, `xemu_load_snapshot` | the [snapshots](#snapshots) of your copy's disk image | list only |
| `xemu_disconnect` | close your connection; the game stays running or paused as it was | |

The reviewer's tool list holds the read-only tools only (`READ_ONLY_TOOLS` in the server, which a test holds
`.claude/agents/reviewer.md` to).

#### The screenshot {#screenshot}

`xemu_screenshot` tries three ways in turn and says which gave the picture: the monitor's `screendump` (xemu has
none), the copy's window read by handle without taking the focus (used only when it is not all black), and the
guest's own **front buffer**. On the maintainer's machine xemu's window read black (`PrintWindow` and a read of the
screen both), while the game was drawing, so the third way is the one that works: the NV2A's scan-out start register
`NV_PCRTC_START` (`0xFD600800`, which the Xbox kernel maps at its physical address) holds an offset into the
console's RAM, the kernel maps RAM from `0x80000000`, and the pixels there are linear 32-bit BGRA (confirmed live
at 640 x 480, the game's mode: a picture of the intro). The guest is stopped for the whole read so the picture is
not torn, and runs on after it. `width` and `height` (default 640 x 480) say the size: the tool does not read the
mode, so another size is the caller's to name. A screenshot is a picture of the game: it stays in scratch and is
never committed.

### Snapshots and a state library {#snapshots}

xemu's save states are QEMU snapshots, and QEMU keeps them **inside the copy's qcow2 disk image**, not in files of
their own: `xemu_save_snapshot NAME` writes one into the image, `xemu_load_snapshot NAME` goes back to it and
`xemu_list_snapshots` lists them. That shapes how states are shared:

- A snapshot belongs to one copy's image. `setup-copies` keeps each copy's image, so its snapshots stay; only
  `--refresh-hdd` replaces the image, and its snapshots go with it.
- A snapshot holds the whole machine, the console's own state with it, so it is game data like a PCSX2 state: never
  in the repository.
- A **state library** for xemu is a disk image of its own beside the copies (`emulators/xemu-states/`), holding the
  shared snapshots, each described in a catalog like [the PCSX2 one](pcsx2-states.md). A copy takes it up by copying
  that image over its own while its xemu is closed. That needs no new tool yet, so it waits until the first shared
  Xbox state is worth keeping; until then name your snapshots after what they show (`shadow-edge-level1`) and say in
  a run record which one a run started from (`record-run --snapshot`).

### Run records {#run-records}

`record-run` writes `research/runs/<id>.toml` for an observation made on your claimed copy, as `pcsx2 record-run` does
for PCSX2 ([Run records](research-evidence.md#run-records)): the method in words, the numbers read (`--value`), the
addresses named (`--address`) and the snapshot the run started from (`--snapshot`, default `unknown`). The record has
an `[xemu]` table, xemu's version (`v0.8.139`, from the copy's log: [the monitor](#monitor)) and the copy, in place
of `[pcsx2]`. A claim cites it as `confirmed (runtime), run <id>`, and
[`research claims`](coney-tools-fidelity.md#research) accepts it only for a claim about the Xbox: a block, grading
line or page that names one of `runs.xbox_words` in [.github/conventions.yml](repo:.github/conventions.yml) (`Xbox`,
`XBE`, `xemu`), or a page named `xbox-*.md`.

### The first live run {#live-run}

Run on 2026-10-25 with the maintainer's portable xemu 0.8.139 (Windows 11, `xemu_install` and `xemu_dvd` in a
worktree's `coney.local.toml`), his install untouched (every file's time unchanged), a copy made by `setup-copies`
(about 1 GB each, the disk image dominating) and an XISO of his own disc. What it showed:

| Check | Result |
| --- | --- |
| `launch` | the game booted to its intro in the claimed copy; `-gdb tcp:127.0.0.1:1234` and `-monitor tcp:127.0.0.1:4444,server,nowait` accepted on the Windows build |
| `read 0x00010000 --type u32 --count 4` | `1212498520` first, which is `0x48454258`: the bytes `XBEH`, the XBE header magic at the image base, as the executable page says |
| portable lookup | xemu logged `base path` and `config path` in the copy's folder |
| `xemu.toml` keys | all known except the draft's `fullscreen_on_boot`, fixed to `fullscreen_on_startup` ([the table](#settings)) |
| `info version` | `10.2.0`, QEMU's; xemu's release comes from `xemu.log` |
| `screendump` | not a command; the window read black while the game drew; the front buffer gives the picture ([the screenshot](#screenshot)) |
| the stub | an unsolicited stop at the attach, now skipped ([the stub](#gdb)); pause, continue, registers and reads from the MCP server answered, and `eip` moved between pauses |
| snapshots | `savevm`, `info snapshots` and `loadvm` work: one snapshot grew the copy's image by about 60 MB and a load kept the game running; `delvm NAME` through the monitor removes it (no tool yet) |
| run record | `research/runs/r-20261026-xbe-header-magic.toml`, from `record-run` |
| `release` | closed the copy's xemu and dropped the claim |

Not exercised: a key sent to a copy's window (the virtual pad), a second copy running beside the first, `--paused`,
breakpoints and watchpoints on the real game (they are tested against the fake stub only).

<!-- command-reference:begin xemu -->
## Parameters of `xemu` {#xemu-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `xemu claim` {#xemu-claim-usage}

Claim a free xemu copy for an agent (every xemu use starts here).

```text
coney-tools xemu claim --agent AGENT [--copy COPY] [--json] [--max-age-hours MAX_AGE_HOURS]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--agent AGENT` | text | required | your id: letters, digits, '.', '_' and '-' |
| `--copy COPY` | text | none | a copy by name (xemu, xemu-b, ...); default: the first free one |
| `--json` | flag | off | print the claim as one JSON line |
| `--max-age-hours MAX_AGE_HOURS` | number | none | a claim with no process is stale after this (default 4) |

### `xemu release` {#xemu-release-usage}

Close the copy's xemu and drop the claim.

```text
coney-tools xemu release --agent AGENT [--copy COPY] [--force]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--agent AGENT` | text | required | your id |
| `--copy COPY` | text | none | one copy; default: all of the agent's |
| `--force` | flag | off | release another agent's claim |

### `xemu status` {#xemu-status-usage}

Who holds each xemu copy and whether its xemu runs.

```text
coney-tools xemu status [--json] [--max-age-hours MAX_AGE_HOURS]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--json` | flag | off | print JSON instead of a table |
| `--max-age-hours MAX_AGE_HOURS` | number | none | a claim with no process is stale after this (default 4) |

### `xemu check-ports` {#xemu-check-ports-usage}

Exit 2 unless the ports are those of a copy you hold.

```text
coney-tools xemu check-ports --agent AGENT [--gdb-port GDB_PORT] [--monitor-port MONITOR_PORT]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--agent AGENT` | text | required | your id (must hold the claim) |
| `--gdb-port GDB_PORT` | integer | none | the GDB stub port you are about to connect to |
| `--monitor-port MONITOR_PORT` | integer | none | the monitor port you are about to connect to |

### `xemu setup-copies` {#xemu-setup-copies-usage}

Make or update the xemu copies from your own install (xemu_install).

```text
coney-tools xemu setup-copies [--count COUNT] [--refresh-hdd] [--dry-run]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--count COUNT` | integer | none | how many copies: xemu, xemu-b, ... (default: xemu_copies, 2) |
| `--refresh-hdd` | flag | off | copy the install's disk image again (the copy's snapshots go) |
| `--dry-run` | flag | off | print what would change, change nothing |

### `xemu launch` {#xemu-launch-usage}

Start your claimed copy's xemu (never taking the focus).

```text
coney-tools xemu launch --agent AGENT [--dvd DVD] [--paused] [--wait WAIT]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--agent AGENT` | text | required | your id; xemu starts under your claim (made if you hold none) |
| `--dvd DVD` | path | none | the disc image to boot (default: xemu_dvd) |
| `--paused` | flag | off | start with the guest stopped (-S), for a breakpoint |
| `--wait WAIT` | number | `60.0` | seconds to wait for the monitor (default 60) |

### `xemu read` {#xemu-read-usage}

Read values from your claimed copy's running game (a quick look).

```text
coney-tools xemu read ADDRESS --agent AGENT [--type TYPE] [--count COUNT]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `ADDRESS` | text | required | an XBE virtual address, such as 0x0032dd40 |
| `--agent AGENT` | text | required | your id (must hold the claim) |
| `--type TYPE` | choice | `hex` | how to show it (default hex). One of `hex`, `u8`, `u16`, `u32`, `i32`, `f32`. |
| `--count COUNT` | integer | `16` | values (or hex bytes) to read (default 16) |

### `xemu record-run` {#xemu-record-run-usage}

Write a run record for an observation made on your claimed copy (research/runs/).

```text
coney-tools xemu record-run --agent AGENT [--copy COPY] [--snapshot SNAPSHOT] --name NAME --method METHOD
    [--value NAME=NUMBER] [--address NAME=0xADDRESS]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--agent AGENT` | text | required | your id (must hold the claim) |
| `--copy COPY` | text | none | the claimed copy (default: the one you hold) |
| `--snapshot SNAPSHOT` | text | none | the snapshot of the copy the run started from, or unknown (default) |
| `--name NAME` | text | required | what the run is about; the record's id is r-\<date\>-\<name\> |
| `--method METHOD` | text | required | how it was observed, in words (a read, a watch, a frame) |
| `--value NAME=NUMBER` | text | none | a number read; repeatable |
| `--address NAME=0xADDRESS` | text | none | an address named; repeatable |

### `xemu find-install` {#xemu-find-install-usage}

Look for an xemu install in the usual places (read-only).

```text
coney-tools xemu find-install
```

Takes no parameters.
<!-- command-reference:end -->
