# Research tools

What an engineer studies the original with: Ghidra, PCSX2 and the Xbox executable, how Coney is started so that no
window takes the keyboard focus, and how the runtime passes are done by driving PCSX2 (recipes, states, patches, input
and the claims on the copies). Recording and comparing runs is on [Traces and playthroughs](research-traces.md); the
rest of the research workflow is listed in [its overview](research-workflow.md#pages).

## The tools {#tools}

- **Ghidra with ghidra-mcp.** Static analysis of `SLUS_212.15`, with the Emotion Engine processor extension; the
  ghidra-mcp server lets agents drive it. Setup: [Ghidra + ghidra-mcp](ghidra.md). The Ghidra project holds
  the game's code, so it stays on your machine and is never committed.
- **PCSX2.** For runtime evidence: memory reads and writes on the game as it runs. `coney-tools pcsx2` makes patched
  state copies, starts PCSX2 on one and records a scripted run as a per-update trace
  ([Recording a trace](research-traces.md#recording-a-trace)); `coney-tools trace diff` compares it with Coney's
  ([Comparing with Coney](research-traces.md#comparing-with-coney)). How PCSX2 is driven underneath:
  [Driving PCSX2](#driving-pcsx2).
- **The Xbox executable (optional).** For string- and float-heavy code (front end, audio cues, scripting glue,
  camera maths), the same function in the Xbox build's `default.xbe` can read faster: string literals inline, the
  object argument visible, library calls named. Find the twin through a string both builds share, load the XBE in
  its own Ghidra project (never the main one), and use it only to read the logic: offsets, sizes and vtable slots
  differ, and evidence is cited at `SLUS_212.15` addresses only. Measurements and the loader used:
  [Xbox executable](../research/xbox-executable.md).
- **Save states in Ghidra (evaluated 2026-10-05, not used).** The EE extension's `PCSX2SaveStateImporter.java`
  script pours a state's RAM into a program's writable blocks and the heap above `.bss` (a `.other` block); run on a
  scratch copy of our project with `pcsx2 states repack` first, it took 10 s, and a script labelling level99's 19 live
  brains, their goals and the 7 gangs ran in 2 s. The same walk over PINE gave the same facts in 0.03 s after a
  19 s PCSX2 launch, with names from the static project either way, and reading `eeMemory.bin` straight from the state
  file gives the same frozen moment without either. Ghidra adds nothing PINE lacks but a frozen, searchable heap; it
  costs a 170 MB project copy per state, its labels never reach the main project, and code patches and the hook cave
  below `0x00100000` do not show, so we read live data over PINE or from the state file.
- **Capture analysis.** Recording what the game sends to the graphics hardware or the sound processor and studying
  the capture. It is a later fallback, for questions that the code and the debugger answer badly (exact rendering
  state, timing). Captures contain game data: they stay in your scratch folder and never enter the repository.

## Running Coney {#running-coney}

A window that takes the keyboard focus interrupts whoever is typing, so agents and tools start Coney only in ways that
leave the focus alone:

- **Test mode, always.** Run `coney` with `--frames N` (and `--input-script`, `--screenshot` as needed) or
  `--headless`, never as a plain interactive run. In test mode Coney's window opens without taking the focus (it
  may show on top, but keystrokes stay where they were), and `--render-references` (which hides its window) never
  takes it either. Any other windowed run started by a tool adds `--no-activate`
  ([Frame rate](running-options.md#frame-rate)).
- **On the agents' display.** Every window an agent opens goes on the display set aside for agents, never on the one
  the user works on. The agent tool's environment sets `CONEY_WINDOW_DISPLAY` (the [setup](agents.md#setup)), and
  Coney opens its window centred on that display, never taking the keyboard focus
  ([Which display](running-options.md#window-display)); the PCSX2 launcher
  reads the same variable (below). An agent never overrides it with `--window-display` for its own runs.
- **ctest needs nothing.** The disc tests inside `coney_tests` run on librw's NULL renderer and open no window; the
  `coney` smoke tests (`repo:src/platform/CMakeLists.txt`) run `--headless` or stop at the command line before a
  window opens; the two tests with a window (`coney.frame_copies_upright` and `a renderer started again after one
  stopped meets none of its state`) hide it and so never take the focus. A new smoke test keeps to
  `--headless`. `coney-tools trace coney` runs Coney `--headless` too.
- **How.** librw's GL3 device makes and shows the window with SDL's defaults, which activate it. Before it does,
  `RenderEngine::start` (`repo:src/platform/render_engine.cpp`) turns off SDL's `SDL_WINDOW_ACTIVATE_WHEN_SHOWN` and
  `SDL_WINDOW_ACTIVATE_WHEN_RAISED` hints, so on Windows the window is shown with `SWP_NOACTIVATE`. Unlike PCSX2's Qt
  ([below](#driving-pcsx2)), SDL honours that, so no launcher is needed and the run's output stays on the console.

## Driving PCSX2 {#driving-pcsx2}

How the runtime passes are done. The first ones (from 2026-10-04) ran the official portable PCSX2 2.9.94 over PINE
alone; since 2026-10-20 every copy runs the maintainer's PCSX2-MCP fork, a build of PCSX2 2.9.129 with a debug
server built in, beside PINE. Both are reached through `coney-tools pcsx2` and the `pcsx2` MCP server.

**Recipes first.** The basic things (find the player or a human, teleport, change health or rage, spawn a human, give
money, send an event, run a Lua chunk or call one of the game's functions, load a checkpoint, pause and step) are
[PCSX2 recipes](pcsx2-recipes.md), each confirmed in PCSX2 and run with `coney-tools pcsx2 recipes run`. Use them
rather than working an address out again, and add a recipe when you find a new technique: an entry in
`research/pcsx2-recipes.toml`, run in PCSX2 and its `confirmed_runtime` filled in, then `coney-tools pcsx2 recipes
render`.

**States first.** Do not drive the original to a place someone has already been: the [state
library](pcsx2-states.md) keeps shared save states (a pedestrian to grab, a fence to jump, a store door, a window to
break), each with its level, checkpoint, the player's position and what it is set up for. **Check it first** with
`coney-tools pcsx2 states list`, start a claimed copy on one with `states use ID --copy COPY --agent ID --launch` (it
hands out a copy, so the library file is never changed), and **add any state you make that others could use** with
`states add`, saying in `--made-by` how you got there. It is also the fast way back to a level or checkpoint, in place
of the `load-checkpoint` recipe's slow reload.

- **Set-up.** `coney-tools pcsx2 setup-copies` puts the fork's build (`pcsx2_mcp_dir`) into every copy and sets its
  ini: `[EmuCore]` `EnablePINE = true` with `PINESlot` 28011 + *i*, `EnableDebugServer = true` with
  `DebugServerPort` 21512 + *i* for copy *i* (`pcsx2` is 0, `pcsx2-b` 1, ...), and `[InputSources]` `SDL = false`
  ([PCSX2 copies](workspace.md#pcsx2-copies)). PCSX2 2.9.94 failed to open an ISO whose path holds commas and
  parentheses ("Requested filename ... does not exist"), so the launcher boots an NTFS hard link with a plain name in
  the scratch folder (it copies nothing).
- **PCSX2 is started only by `coney-tools pcsx2 launch` or `trace original`, never by hand** (no `pcsx2-qt.exe` from
  a shell, a script or `Start-Process`). The launcher is what keeps PCSX2 from taking the keyboard focus, which
  interrupts whoever is typing. `pcsx2 launch --agent <id> [<state.p2s>]` starts it on a state file, or with no
  state boots the disc and returns once PINE answers; it runs under your claim and leaves PCSX2 running.
- **How the launcher keeps the focus off** (`repo:python/src/coney_tools/pcsx2_proc.py`, whose docstring says why it
  must stay this way). A no-activate show command alone (`SW_SHOWNOACTIVATE` in `STARTUPINFO`) was not enough: Qt
  ignores it for its first window, and Windows lets a process activate its window when the foreground process
  started it, which every agent's process tree (under the user's terminal) is. So the launcher has WMI start PCSX2
  (`Win32_Process.Create`): its parent is WMI's provider host, outside that tree, with no foreground rights to hand
  down. As a backstop, while `trace original` runs a guard polls every 50 ms and, if a PCSX2 window is in the
  foreground, hands it back to the window that had it (no input sent, PCSX2 never minimised, so screenshots keep
  working). Checked 2026-10-07: the foreground owner logged every 50 ms over a state launch and over a cold boot
  (about 900 polls each) was never PCSX2.
- **Where its windows go.** With `CONEY_WINDOW_DISPLAY` set, the launcher also moves each PCSX2 window (the main
  window, and the game's when it has its own) onto that display as soon as it shows, centred and shrunk to the
  display's usable area if larger, without activating it (`SetWindowPos` with `SWP_NOACTIVATE`); a window is moved
  once, so one dragged elsewhere stays there. The value means what it means for Coney
  ([Which display](running-options.md#window-display)): an index counts the primary display as 1 and the rest in Windows'
  order, as SDL does, and a name matches the display's device name (`DISPLAY2`) or its monitor's description as
  Windows lists it. A value that matches nothing prints one warning listing the displays, and PCSX2 opens where it
  last was (its own `PCSX2.ini` keeps that place, `[UI]` `MainWindowGeometry`; the launcher does not edit it).
  `repo:python/src/coney_tools/pcsx2_proc.py` (`WindowPlacer`).
- **Several at once.**{#several-at-once} A portable PCSX2 keeps its settings, states and memory cards in its own
  folder, so each copy of the folder (`pcsx2`, `pcsx2-b`, ..., each with its own `PINESlot` 28011, 28012, ... and
  `DebugServerPort` 21512, 21513, ...) is an independent instance, and `coney-tools pcsx2` reads both ports from
  that copy's `PCSX2.ini`; two copies on one port are refused. The fork binds its debug server port exclusively, so a
  second PCSX2 on a port in use logs `[DebugServer] Failed to bind on port N` rather than sharing it (stock
  PCSX2-MCP let every instance listen on 21512 and connections landed in any of them). Every PCSX2 use starts with
  `coney-tools pcsx2 claim --agent <your id>` (it prints the copy's folder and both ports; `--json` for scripts) and
  ends with `pcsx2 unclaim --agent <your id>`, which also closes your PCSX2. `pcsx2 status` shows who holds what, built
  from the live claims and processes; a claim whose process is gone is stale and the next `claim` takes it over.
  `pcsx2 launch` and `trace original` take `--agent` and run under your claim (making one if you hold none), and wait
  for both ports. Input to the game is over PINE (scripted pad input, the stick table below) or through the debug server
  (the pad, frame by frame, and input recordings: below); no tool takes the keyboard focus. When a hotkey is
  unavoidable (Space to pause), `pcsx2 send-keys --agent <id> --copy <name> space` posts the key to that copy's window
  by handle, and `pcsx2 screenshot --copy <name> --out <png>` reads the window by handle (not F8); both are Windows-only
  and leave the focus alone. PCSX2 never takes the focus (the launcher, above), and every copy has `[InputSources] SDL =
  false` so the user's gamepad cannot drive it; a maintainer turns SDL on in a copy only to test by hand, and `claim`
  and `status` warn when it is on. Claims live in `pcsx2-claims/` of the main checkout's shared scratch folder
  (`../../scratch`), never a worktree's own `scratch_dir`, so every worktree sees them (`pcsx2_root` and
  `pcsx2_claims_dir` in `coney.local.toml` move the copies and the claims). **Act only on your own copy:** the ports
  of every copy are readable, but a client may use only the ports of the claim it holds ([The PCSX2 MCP
  guard](agents.md#pcsx2-guard) says what is enforced and what to check yourself).
- **What PINE gives.** Memory reads and writes, game info, the emulator's status and save/load state slots; no
  breakpoints, registers, pause or frame capture. PCSX2 serves **one PINE client at a time**: a second client (`trace
  original`, a script of your own) blocks while another is connected, so the MCP server connects in `mode: "debug"`,
  which leaves PINE free.
- **What the debug server gives** (the fork's, newline-delimited JSON over TCP on the copy's `DebugServerPort`; it
  starts with a game and serves several clients at once). Everything the `pcsx2` MCP server's tools do: memory reads,
  writes and pattern searches, all registers at 128 bits, PCSX2's own disassembly and expressions, breakpoints with
  conditions, watchpoints (read, write, change), stepping and the debugger's pause, plus `pcsx2_emulation` (pause or
  resume emulation as a whole), `pcsx2_frame_advance` (run N frames, then pause), `pcsx2_controller` (a DualShock 2's
  whole state, held until the next call), `pcsx2_screenshot` (a PNG at internal resolution in the copy's `snaps/`),
  `pcsx2_capture` (PCSX2's video capture), `pcsx2_gs_dump` (a GS dump of N frames), save and load state, and
  `pcsx2_input_recording` (below). The MCP server connects to nothing at start: `pcsx2_connect` with your claim's
  `debug_port` and `pine_port` picks the copy, and **every `pcsx2_*` call carries `agent: "<your id>"`** (the id you
  claimed with; it names your connection, [Agents](agents.md#setup)). `coney-tools trace original --recording`
  speaks the same protocol itself (`repo:python/src/coney_tools/debug_server.py`).
- <span id="input-recordings"></span>**Input recordings.** PCSX2's own `.p2m2` files hold every frame's pad state from
  a start savestate (`<file>.p2m2_SaveState.p2s` beside it) or from power-on, so a run's input repeats exactly.
  `pcsx2_input_recording` drives them as PCSX2's "Input Recording" menu does: `new` (with `author`, `from`
  `savestate` or `power_on`, `path`) records from now on, `play` loads the start and replays, `stop` closes the file,
  `status` reports the mode and frames. At a replay's last frame PCSX2 pauses and the fork stops the replay there, so
  playing never adds to the file. A `play` sent while emulation is paused (`pcsx2_emulation`) loads the start at once
  and stays paused until resumed, which is how `trace original --recording` makes its step 0 the recording's first frame
  (checked 2026-10-20 on copy `pcsx2`: a 181-frame recording made in the front end, played while paused, put PCSX2's
  frame counter back to the recording's start at recording frame 0, ran on resume, and ended paused with
  `replay_ended`). Paths are relative to the copy's folder and must stay inside it, so recordings live
  in the copy's `recordings/`; `coney-tools pcsx2 recordings make` writes one from a Coney input script (the same file
  that drives Coney, so both games get the same pad; checked 2026-10-21 on PCSX2 2.9.129: a recording of `stick left 0
  60` for 2 s made the player walk at 1.629 m/s, gait 2, as the `pad` recipe's 60 % does, and stop when it ended), and
  `recordings push | pull` moves them (with the savestate,
  SHA-256 checked) to and from the shared scratch folder's `recordings/`, where they are kept like any capture. `trace
  original --recording <p2m2>` replays one while it traces memory, so a runtime trace can be made again on the same
  input ([`pcsx2`](coney-tools-pcsx2.md#pcsx2)). A recording is game data: never committed.
- **States across versions.** The copies keep the states their stock PCSX2 2.9.94 saved in `sstates/`, but PCSX2
  2.9.129 may refuse them (a state is tied to the version that saved it). The maintainer's slots 1 to 10 are in the
  [state library](pcsx2-states.md), saved by 2.9.94, and the 2.9.129 fork loaded the two tried (2026-10-21); a state
  that is refused is made again on the fork build from its catalog entry's steps and added under a new id.
- **Input and hotkeys.** Keyboard messages posted to the PCSX2 window by handle (`pcsx2 send-keys`, `WM_KEYDOWN`) reach
  the hotkeys in `[Hotkeys]` (verified: a posted Space paused and resumed the game with the window unfocused) and the
  pad bindings in `[Pad1]` (Return = Start, K = Cross, arrows = D-pad, WASD = left stick; unchecked with posted
  keys). The hotkeys: **Space pauses**, F8 saves a screenshot to `snaps/` (aspect-corrected, 1240 × 930 for the
  game's 640 × 448), F4 toggles the frame limiter. Hold a pad key for about 300 ms so that a 30 Hz game sees it.
  Leave the frame limiter on: with it off, real-time waits (the legal screen's 5 s) pass in a blink.
- **A chosen analog stick value.** Keyboard keys bound to a stick only give full deflection, but the gait thresholds
  need partial values: the stick table at `0x0050b8d0` sets what full deflection reads as. How, with its addresses:
  the [`stick-magnitude`](pcsx2-recipes/input.md#stick-magnitude) recipe. State the magnitude used with every runtime claim
  that depends on input ("stick magnitude 0.5, straight up").
- **Catching a moment.** Load a state, press Space to pause, make the memory writes, then send Space followed by a
  run of F8 presses (one a second, or faster) and look at the screenshots afterwards. Data drawn in one frame
  usually survives in memory until reused: a sprite batch's arrays keep the last frame's sprites after its count is
  reset, which is how the draw-order keys and colours were read.
- **Staging a fight.** To study the player being hit, write a pedestrian's position and heading over PINE so it
  stands in front of the player, then force its commands and animations so it attacks or grabs: a controlled run that
  repeats exactly. The front end's Rumble mode (quick rumble) is the fallback for live AI fights. Either way, save the
  starting point as a state file of your own.
- **Patching code to see something.** A code write lands before the recompiler first compiles the block if the
  function has not run yet; the legal screen's placement was measured by patching its clear colour to blue and
  halving its size factors, and the start-up movies were skipped by making their skip check (`0x0042a820`) return 1.
  Say on the page which patch a claim depends on. A write to code that has already been compiled is not picked up
  (PCSX2 logs "Impossible block clearing failure", and writing during a state load crashed it).
- **Scripted pad input** (2026-10-05, the traversal pass). For a **closed-loop driver**, one that decides each input
  from what the game shows, the `scripted-pad` patch (`research/traces/patches/state-patches.toml`) makes the pad code
  read player 1's buttons and left stick from spare bytes that a PINE write sets; the patch must be in the state copy
  before the code is compiled (`coney-tools pcsx2 states patch`, then `pcsx2 launch`). The addresses, the byte order
  and how to hold a pad state: the [`pad`](pcsx2-recipes/input.md#pad) recipe. `coney-tools pcsx2 states patch` and
  `trace original` make the copy and restart PCSX2 for you. For input that does not depend on the game's state, use an
  [input recording](#input-recordings) instead: it is the default for runtime evidence (a maintainer decision,
  2026-10-21), because PCSX2 replays it frame by frame, the same every time.
- **The right stick, too** (2026-10-05, the feel pass). The `right-stick` patch puts the right stick's x and y in the
  same spare bytes (the [`pad`](pcsx2-recipes/input.md#pad) recipe). The stick table at `0x0050b8d0` is the same map as
  Coney's `pad::stickValue` (dead band 95-160, `(raw − 160) / 95`), so writing the raw bytes Coney makes from an input
  script's percentages (`160 + round(0.95 × v)`, `95 − round(0.95 × |v|)`, 128 at rest) gives both games the same
  stick, diagonals included, without touching the table.
- **A puppet human** (2026-10-05, the combat runtime pass). To make an AI human attack, grab or block the player,
  write command ids ([Combat](../research/combat/index.md#commands)) into its per-player record with the `puppet` patch,
  which stops the game clearing them: the [`puppet-command`](pcsx2-recipes/input.md#puppet-command) recipe, with what a
  grab and a held button need. Pick the puppet by name: the nearest human changes as pedestrians walk, and an ally's hits
  play reactions without damage.
- **Following a script at runtime** (2026-10-07, the level99 hints pass). To log which Lua functions run and which
  bindings a script calls, with their arguments, hook the Lua 4 VM. Its interpreter entry is `0x00334478`
  (`luaV_execute`, called by `luaD_call` `0x00328b40`); its `a1` is the closure, whose prototype (`+0x00`) holds
  `lineDefined` at `+0x38` and the chunk name at `+0x3c`. The C-function call is `0x00328a30` (`callCclosure`): `a1`
  is the closure, `+0x00` the binding's address (as in a bindings dump), and `a2` its first argument. Arguments are
  16-byte TObjects: type at `+0`, value at `+8` (1 nil, 2 number as a double, 3 string with its text at `+0x10`).
  Only the first argument is reliable: the slots after the last real argument hold old values. The hooks are
  `lua-exec`, `c-call` and `c-call-args`. A script's tables are read the same way, from the globals table
  (`ScriptSystem *0x00512b04` → state `+4` → globals `+0x44`). That gives `Objects.<name>`, a spawn handle (index
  `<< 16`), which leads to the live object through the spawn record (`+0x24` low 16 bits) and the handle table
  `0x006ebd38` ([Objects](../research/objects.md#spawn-records)). A driver that plays a level's steps by what they
  show ([Scripts](../research/scripting.md#level99-hints)) must send partial sticks through Coney's byte map (below):
  a plain linear map falls inside the dead band and the player stands still.
- **One sample per update.** Batch every read of a sample into one PINE message and keep a sample only when a character
  update has run since the last one: the game counts its 60 Hz ticks at `0x005104f4` and steps the characters on each
  tick that makes the count even ([Tasks](../research/tasks.md#tick)), so the count halved numbers the updates. Apply
  the scripted input as each update is seen. `coney-tools trace original` does exactly this
  ([Recording a trace](research-traces.md#recording-a-trace)). (The first recorder counted updates by the game time in
  milliseconds, `*(0x0050b734) + 0x48`; its rounding skipped and repeated steps after the frame that catches a tick up,
  about every 33 s.)
- **Hooks: a call log without breakpoints** (2026-10-05, the runtime-checks pass). PINE has no breakpoints (the debug
  server's do stop the game, which a recording cannot afford), so "who writes this" and "which objects does this run"
  are answered by patching code in the state copy: a hook replaces two instructions with a jump to a cave in memory the
  game leaves empty (`0x000a0000`; zeros from `0x00095100` to the ELF at `0x00100000` in every state checked), which
  writes chosen registers and loads into a ring buffer (`0x000b0000`) and runs the two instructions. The recorder reads
  the ring every poll and writes one CSV per hook ([Hooks](research-traces.md#hooks)). A **call hook** makes the game
  call one of its own functions on its own thread, as a script binding would: that is how a fight is started on a
  pedestrian (`GoalFight`'s `0x002b2b90`) for the AI checks.
- **Absolute paths from a worktree.** Give `pcsx2 launch` and `trace original` absolute `--iso`, `--scratch` and
  `--state` paths when you run them from a worktree: PCSX2 opens the disc link relative to its own folder, so a
  relative path fails ("Requested filename ... does not exist" in its `logs/emulog.txt`) and the tool only reports
  that the state did not run within 60 s. A state that already holds a patch (`scripted-pad` in most saved states)
  is refused by that patch: leave it out of the scenario's `patches`.
- **Leave the quick-save slots alone.** Load existing slots read-only (they may hold someone else's test spots; the
  [state library](pcsx2-states.md) holds copies of the maintainer's, so use those), and keep your own states as files
  in your scratch folder, made as above, and add the useful ones to the library. PINE can only save to a slot number:
  when a state must be saved from a running game, save it to an empty slot above 10 and move the file to your scratch
  folder at once. A state saved from a patched copy still holds the patches; put each patched word back to the
  original that `research/traces/patches/` lists before using it as a scenario's `state`, or the copy is refused.
- **Hygiene.** Save states, screenshots and logs contain game data: keep them in your scratch folder (move the
  `.p2s` files out of `pcsx2/sstates/` afterwards) and close PCSX2 when done. Screenshots are measured, never
  committed; a claim quotes the numbers.
