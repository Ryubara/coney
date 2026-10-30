---
kind: feature
owns:
  functions: ["0x004091c8", "0x00409318", "0x0042c388", "0x0042c420", "0x0042b9b8"]
---
# Scripts: optional arguments, left out versus nil

Verified against: `SLUS_212.15` (NTSC-U).

## Summary

A script calls a binding with fewer arguments than it takes, or passes `nil` in a slot. The two are not the same in
the original: a default is used only for an argument the script left out, and an explicit `nil` reads as 0 (or false).
`HUDTurnOnRadar()` turns on both radars, while `HUDTurnOnRadar(nil)` turns on player 0's. The bindings then narrow the
number to an integer with one of two saturating conversions. How a binding is called, and the rest of the argument and
result conventions, are on [Scripts (Lua)](scripting.md).

## Rules

- A default applies only when the argument is past the last one the script passed.
- An explicit `nil`, a table, or a string that is not a number reads as 0; a numeric string reads as its number.
- A boolean argument left out is its default; `nil` is false; anything else is true when its number is not 0.
- An unsigned integer argument is 0 for anything negative or under 1, and saturates at `0xffffffff`.
- A signed integer argument is truncated toward 0 and saturates at the 32-bit range.

## Map

| Behaviour | Function | Section |
| --- | --- | --- |
| Read a number argument, with a default when left out | `0x004091c8` `Lua_GetOptionalNumberArg` | [The readers](#readers) |
| Read a boolean argument, with a default when left out | `0x00409318` `Lua_GetOptionalBoolArg` | [The readers](#readers) |
| Narrow a number to an unsigned integer | `0x0042c420` `Float64_ToUInt32Saturating` | [The conversions](#conversions) |
| Narrow a number to a signed integer | `0x0042c388` `Float64_ToInt32Saturating` | [The conversions](#conversions) |
| Split a double into class, sign, exponent and mantissa | `0x0042b9b8` `Float64_UnpackParts` | [The conversions](#conversions) |

## Behaviour

### The readers {#readers}

`Lua_GetOptionalNumberArg` (`0x004091c8`) takes the state, a 1-based index and a default. It compares the index with
the top of the stack (`lua_gettop`, `0x00323318`): past the top it returns the default; otherwise it returns
`lua_tonumber` (`0x00323688`) of the slot, whatever its type, so `nil` gives 0. Confirmed (code) at `0x004091c8` and
`0x00323688`.

`Lua_GetOptionalBoolArg` (`0x00409318`) makes the same test first. Within the stack it reads the slot's type tag
(`0x003234b8`): tag 1 (`nil`) is false, and anything else is `lua_tonumber` compared with 0, so a string that is not a
number is false too. Confirmed (code) at `0x00409318`.

Each binding's wrapper names the reader and the conversion of every argument. Thirteen of them, the ones Coney
reads through these rules, are listed under [In Coney](#in-coney). `TriggerSphereCfg` (`0x0036d0e0`) reads its mode
with the number reader and the signed conversion, so a mode that is not a number is 0; the scripts pass 0, 1 (their
`true`) or 2. Confirmed (code) at `0x0036d0e0`.

### The conversions {#conversions}

Both conversions are soft-float routines that start from `Float64_UnpackParts` (`0x0042b9b8`), which gives the class
(0 or 1 NaN, 2 zero or denormal, 3 normal, 4 infinity), the sign, the unbiased exponent and the mantissa. Confirmed
(code) at `0x0042b9b8`.

- `Float64_ToUInt32Saturating` (`0x0042c420`): NaN, zero, a negative number (and minus infinity) and anything under 1
  give 0; a number under 2^32 is truncated; 2^32 and above, and plus infinity, give `0xffffffff`. Confirmed (code) at
  `0x0042c420`.
- `Float64_ToInt32Saturating` (`0x0042c388`): NaN, zero and anything within 1 of 0 give 0; a magnitude under 2^31 is
  truncated toward 0; anything larger, infinity too, gives `0x7fffffff` or `-0x80000000` by its sign. Confirmed
  (code) at `0x0042c388`.

## In Coney

The readers and conversions are in [binding_args.h](repo:src/scripting/binding_args.h): `binding::numberOr`,
`booleanArg`, `toUnsigned`, `toSigned`, and the argument readers built on them, `unsignedArg`, `signedArg`, `floatArg`
and `handleArg`. `binding::absent` (left out or nil) stays for arguments that read the same either way, a string or a
callback.

The orig sweep of 2026-10-21 found thirteen bindings that gave an explicit `nil` the default; since #686 they read as
the original does: `GoalAddressPerson`'s speech (`0x003603a8`), `GoalDealer`'s range and run chance (`0x00363018`),
`BrSetNumFollowSlots`'s second count (`0x0035fc80`), the delays of `ActLookAt` (`0x003647d8`) and `ActTurnToDir`
(`0x00364708`), `TacticDomination`'s range (wrapper `0x00376008`), `HUDSetObjective`'s time (`0x0036f280`),
`HUDFlushTutorialText`'s priority (`0x0036f430`, narrowed to a byte), the player of `HUDTurnOnRadar` (`0x003700c8`)
and of `HUDTurnOffRadar` (`0x00370100`), `HUDSetNumIndicator`'s gang (`0x00370138`), `HUDSetRadarZoomScale`'s
scale, 0 when left out (`0x00370728`), and `TriggerSphereCfg`'s enable, false when left out, and its mode
(`0x0036d0e0`). The tests are in [optional_args_test.cpp](repo:tests/scripting/optional_args_test.cpp) and
[binding_args_test.cpp](repo:tests/scripting/binding_args_test.cpp).

The other bindings keep their own per-file readers, most of which still give an explicit `nil` the default
([Other bindings](#q-nil-defaults)).

## Open questions

### Other bindings {#q-nil-defaults}

Only the thirteen bindings above were checked. The rest read through per-file helpers that treat `nil` as left out,
which is right for a string or a callback and unchecked for a number or a boolean with a default. Each binding's
wrapper, read in Ghidra, settles its arguments; the shipped scripts pass `nil` mostly in string and option positions,
so few levels can show a difference.
