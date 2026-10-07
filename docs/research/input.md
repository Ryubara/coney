# Input: pads and player commands

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Static reading in Ghidra
(2026-10-07); runtime claims are those of the pages linked.

## Purpose

This page indexes the `Device/ps2/` code that turns the two DualShock 2 pads into what the game reads: the `libpad`
state machine that brings a pad into analog and pressure mode, the pad queries over the 8-sample button history, the
Lua pad handlers, and the per-player record that turns buttons into **command ids**. What the commands mean and how
they are matched is on [Combat, Commands](combat.md#commands); the pad record and the per-frame pad update are on
[Front end, Pad record](frontend.md#pad-record) and [Input on the front end](frontend.md#input); the per-player record
is on [Characters](characters.md#buttons). Every function from `0x00144a08` to `0x00148358` and from `0x00149520` to
`0x00149ef8` is listed here.

## Original structure

The code sits in `Device/ps2/`, between the stub at `0x001449e8` and `DS_PS2Device.cpp`'s stub `0x001485a8`, with
the `libpad` layer after the file systems (`0x00149520`-`0x00149ef8`). No string names its files, so the file is
inferred from position ([Source map](source-map.md)). Names are ours; evidence is confirmed (code) at the address
unless the row says otherwise.

### Pad queries {#pad-queries}

`pad` is a [pad record](frontend.md#pad-record); `cur` is the current button word, `prev(n)` the word `n` samples
back. The matchers of [the command tables](combat.md#commands) are the ones marked "table".

| Address | Name | Returns | Evidence |
| --- | --- | --- | --- |
| `0x00144a08` | `Pad_GetHistory(pad, n)` | `prev(n)`, `n` clamped to 7 | confirmed (code) |
| `0x00144a80` | `Pad_Pressed(pad, 0)` | `cur & ~prev(1)` | confirmed (code) |
| `0x00144b88` | `Pad_IsHeld(pad, mask)` | `cur & mask != 0` (table 1) | confirmed (code) |
| `0x00144ba8` | `Pad_IsNewlyReleased(pad, mask)` | none of `mask` down now, some down one sample back (table 3) | confirmed (code) |
| `0x00144bf0` | `Pad_IsNewlyPressed(pad, mask)` | some of `mask` down now and none one sample back (table 2) | confirmed (code) |
| `0x00144c38` | `Pad_PressedRepeatMask(pad, mask)` | newly pressed, or a d-pad direction in `mask` whose hold counter is 15 (the masked form of the menus' auto-repeat) | confirmed (code) |
| `0x00144ce0` | `Pad_TappedShort(pad, mask)` | newly released, and up in at least one of samples 2-7 back: a press of 1 to 6 samples (table 5) | confirmed (code) |
| `0x00144d60` | `Pad_TapOrHold4(pad, mask)` | newly released after 1 to 3 samples down, or down for exactly the 4th sample (table 6) | confirmed (code), from the instructions |
| `0x00144e50` | `Pad_HeldFrames(pad, mask)` | down in each of the last *N* samples (*N* = `0x0050b708`, [`CfgButtonHeldFrames`](../references/bindings/config.md#cfgbuttonheldframes)) and up in one of the samples *N* to 7 back (table 7) | confirmed (code) |
| `0x00144ef8` | `Pad_ComboHeld(pad, a, b)` | every button of `a` and `b` down now and not all of them one sample back (table 8) | confirmed (code) |
| `0x00144f48` | `Pad_ComboPress(pad, a, b)` | every button of `a` and `b` down now and not all of `b` one sample back (table 9) | confirmed (code) |
| `0x00144f98` | `Pad_SetCurrentButtons(pad, word)` | overwrites `cur`: the movie skip and `PlayerRec_ClearButtons` use it | confirmed (code) |
| `0x001476f0` | `Pad_GetPressure(pad, button)` | the pressure byte of a one-bit button mask, by the order table `0x0050b768` (right, left, up, down, triangle, circle, cross, square, L1, R1, L2, R2); 0 for other masks | confirmed (code) |

### Pads, handlers and the device hooks {#pads}

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00144fb0` / `0x001454a8` | `Pad_Update` / `Pads_Update` | the per-frame update ([Input on the front end](frontend.md#input)) | confirmed (code) |
| `0x00145670` | `Pads_ResetStateMachines` | calls `0x00149ef8`: every `libpad` record back to its first state (`PM_Greet_Enter` calls it) | confirmed (code) |
| `0x00145698` | `Pad_SetLuaHandler(player, button, function)` | `PadSetHandler`: takes the pad index of the HUD's virtual pad (`HUD_GetVirtualPad`) for player `player != 0` when that pad record's player (`+0x42`) is the same, otherwise `player` itself as the pad index, and stores the script reference at `0x005dda90 + pad × 0x40 + 4 × b`, `b` the highest set bit of `button` | confirmed (code) |
| `0x00145780` | `Pads_SetDisconnectCallback(fn)` | stores `fn` in `0x0050b704`, called when a pad is pulled ([State machine](#libpad)); `Pads_Init` sets `Player_UnbindPad` (`0x0022a980`) | confirmed (code) |
| `0x00149520` | `Pads_Init` | `scePadInit`; for each of the 8 `libpad` records: clear, port `i >> 2`, slot `i & 3`, first state; pad records' `+0x42`, `+0x44` = −1; opens port 0 slot 0 and port 1 slot 0 with their DMA buffers (`0x005de3c0`, `0x005de8c0`); clears the 0x200-byte Lua handler table; sets the disconnect callback; one `Pads_Update` | confirmed (code); the `libpad` calls are inferred from their arguments |
| `0x00149668` | `Pad_SetVibration(record, on, strength)` | sets the small motor (`on`) and the big motor's strength in the `libpad` record, only when the pad belongs to a player and vibration is on in the options (`GameState_IsVibrationActive`); otherwise both 0 | confirmed (code) |
| `0x00149ef0` | `Pads_UpdateHook` | empty; `Pads_Update` calls it | confirmed (code) |
| `0x00149ef8` | `Pads_ResetStates` | the 8 records' state back to `0x001499e8` | confirmed (code) |

### The libpad state machine {#libpad}

Each of the 8 records at `0x005de3c0 + i × 0x140` holds the `libpad` DMA buffer, the state function at `+0x100`, port
`+0x104`, slot `+0x108`, connected `+0x10c`, the stick bytes `+0x110`-`+0x113` (left x, y, right x, y), the button
word `+0x114`, 12 pressure bytes `+0x116` and the actuator settings `+0x122`, `+0x128`. `Pad_Read` (`0x001498a8`)
steps it and copies the result into the [pad record](frontend.md#pad-record). The `libpad` entry points are unnamed
in Ghidra; their roles below are inferred from their arguments and the SDK's documented sequence.

| Address | State | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00149770` | `PadPort_Step` | reads the port's state: disconnected → back to the first state and, if the pad belonged to a player (`+0x42` ≠ −1), calls the disconnect callback once; stable → connected; error → first state; then runs the state function | confirmed (code); the state numbers' meanings inferred |
| `0x001499e8` | first | asks the current controller id: digital (4) → `0x00149ab0`, DualShock (7) → `0x00149bc0`, else `0x00149a88` | confirmed (code) |
| `0x00149a88` | unsupported | neutral data, not connected | confirmed (code) |
| `0x00149ab0` | digital | when the pad has other modes, → `0x00149b08`; else unsupported | confirmed (code) |
| `0x00149b08` / `0x00149b58` | set analog / wait | asks for analog mode, locked; waits for the request, then starts over (the pad now reports 7) | confirmed (code) |
| `0x00149bc0` / `0x00149c30` | DualShock / wait | with actuators, sets their alignment (`+0x128`), waits | confirmed (code) |
| `0x00149c98` | pressure check | pressure mode available → `0x00149cf0`, else `0x00149e58` | confirmed (code) |
| `0x00149cf0` / `0x00149d38` | enter pressure / wait | enters pressure mode, waits | confirmed (code) |
| `0x00149da0` | read with pressure | buttons, sticks and the 12 pressure bytes; sends the motors | confirmed (code) |
| `0x00149e58` | read | buttons and sticks; sends the motors | confirmed (code) |
| `0x00149720` | `PadPort_ClearData` | sticks to 0x80 (centre), buttons and pressures 0, motors off | confirmed (code) |

So a pad that has only a digital mode gives no input (inferred), and a DualShock 2 ends in pressure mode, which the
sprint's `CfgPlayerRunButton` option and [`Pad_GetPressure`](#pad-queries) read.

### Commands and the per-player record {#commands}

The script bindings are on [character bindings](../references/bindings/character.md); the tables and the matching
order on [Combat, Commands](combat.md#commands); the record (`0x00660f50 + i × 0x2c`) on
[Characters](characters.md#buttons). The nine tables live at `0x005ddd10`-`0x005de130` with their counts at
`0x0050b740`-`0x0050b760`.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00145fa0` | `PlayerRecord_Init` | pad −1, not pad-controlled, both stick buffers 0, command cleared, input unlocked | confirmed (code) |
| `0x00146000` | `PlayerRecord_ClearCommand` | clears the current stick buffer, `+0x1c`, `+0x1d` and the command `+0x20`; with a pad, also the pad record's `+0x40`, `+0x41` | confirmed (code) |
| `0x00146058` | `PlayerRecord_ClearCommandThunk` | the same, from `Brain_InstallHandlers` | confirmed (code) |
| `0x00146078` | `PlayerRecord_Update` | flips the buffer; with a connected pad and pad control: copies the buttons, stores the stick's angle and length (π/2 and 0 when input is locked), passes `+0x1c`, `+0x1d` to the pad record (`+0x41`, `+0x40`), then `Commands_Match`; without a pad or pad control it calls `PlayerRecord_ClearCommand` instead | confirmed (code) |
| `0x001461e0` | `Commands_Reset` | `ResetCommands`: every entry of the nine tables to mask `0xff`, command 0 | confirmed (code) |
| `0x001463e0` | `PlayerCommand_EnableAllForHuman(human, on)` | `EnableCommands`: sets or clears the human's pad bit in the mask of every bound entry | confirmed (code) |
| `0x001467a8` | `PlayerCommand_EnableForHuman(human, id, on)` | `EnableCommand`: the same for the entries of one command | confirmed (code) |
| `0x00146b90` | `PlayerCommand_Remove(id)` | `DelCommand`: in each table, removes the first entry with that command and moves the rest down (the scan ends after a removal, so a second entry of the same command in one table stays) | confirmed (code) |
| `0x00147218` | `Commands_IsBound(id)` | whether a command id 1-57 is in any table (`AddCommand` checks it) | confirmed (code) |
| `0x00147640` | `PlayerRecord_ForceCommand(record, id, ms)` | the pending command `+0x24` and its end time `+0x28` (game time + `ms`) | confirmed (code) |
| `0x00147660` | `PlayerCommand_ForceForHuman` | `ForceCommand`: for a pad-controlled human and an id 1-57 | confirmed (code) |
| `0x00147738` | `PlayerCommand_IsEnabled(record, id)` | the record's pad bit in the mask of the first entry with that command; `HUD_Update` asks for `0xa` ([Crimes](crimes.md)) | confirmed (code) |
| `0x00147940` | `Commands_Match(record)` | the matcher ([Combat, Commands](combat.md#commands)) | confirmed (code) |
| `0x00147ef8` | `PlayerRecord_GetCommand` | `+0x20` | confirmed (code) |
| `0x00147f00` | `PlayerRecord_GetCommandOrPending` | `+0x20`, else `+0x24` (`PadSetHandlerEx`) | confirmed (code) |
| `0x00147f18` | `PlayerRecord_GetPressure(record, i)` | pressure byte `i` (0-11) of the record's pad | confirmed (code) |
| `0x00147f50` | `PlayerRecord_AnyButtonDown` | the pad's current button word is not 0 (`MessageBox_ClaimPad`) | confirmed (code) |
| `0x00147f98` | `PlayerRec_IsPadHeld(record, mask)` | `Pad_IsHeld` on the record's pad; 0 without a pad | confirmed (code) |
| `0x00147fd0` | `PlayerRecord_IsNewlyPressed(record, mask)` | `Pad_IsNewlyPressed` on the record's pad | confirmed (code) |
| `0x00148008` | `PlayerRecord_GetPadHistory(record, n)` | `Pad_GetHistory` on the record's pad (the button-tap mini-game) | confirmed (code) |
| `0x00148040` | `PlayerRecord_ClearButtons(record, mask)` | removes `mask` from the pad's current word, so a scene's start does not also act on the press | confirmed (code) |
| `0x001480a0` / `0x00148210` | `PadHandlerEx_Set` / its thunk | `PadSetHandlerEx`: keeps the script reference in `0x0050b73c` | confirmed (code) |

**A quirk.** `PlayerCommand_IsEnabled` scans table 5 (tapped) with `≤` its count, one entry past its end, where the
other tables use `<`. Confirmed (code) at `0x00147738`; the extra entry is the next table slot, unused (mask `0xff`,
command 0) unless table 5 is full (inferred).

## Coney's implementation

Coney's pads are `src/core/pad.h` (`Pad`, the pad record and its queries), `src/core/pads.h` and
`src/core/pad_handlers.h`; the SDL layer replaces the `libpad` state machine and always reports an analog pad with
pressures. The command tables are not written yet.

## Open questions

- The role of the pad record's `+0x44` (set to −1 with `+0x42`; `Player_UnbindPad` copies `+0x42` there before clearing
  it, so it may be the last player).
- The `libpad` state numbers 1-5 in `PadPort_Step` (inferred from the SDK, not read in the IOP module).
