# Xbox executable (evaluation)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`) and the NTSC-U Xbox
`default.xbe` (SHA1 `fe246a74501cd10d2ce89ca5dbda5080d2b2edcc`). No runtime claims. Evaluation date 2026-10-05.

Addresses written **XBE `0x...`** are virtual addresses in `default.xbe` (base `0x00010000`); plain addresses are in
`SLUS_212.15`. Everything else about the Xbox disc is on [Xbox assets](xbox-assets.md).

## Purpose

The question: is the Xbox build's executable easier to reverse engineer than the PS2's, and is it worth using? The
PS2 stays the only reference for behaviour; this page measures whether the Xbox executable helps as a
cross-reference. Short answer: **use it as a cross-reference, not as a second reference.** It reads better in places:
string literals, calling conventions, library names. But it has no symbols, no RTTI and different struct layouts,
so it does not replace PS2 analysis.

## Method {#method}

Both executables were imported fresh into a separate headless Ghidra 12.1.4 project, outside the repository, with
default auto-analysis and no analyst names: the ELF with the Emotion Engine extension, the XBE with the
open-source ghidra-xbe loader (XboxDev, build `202602250354`, built for Ghidra 12.0.3, its version field changed
to load). That loader applies XbSymbolDatabase library signatures; Ghidra's own Function ID also ran. Throwaway
scripts then exported, per function, its string references (references, immediates and, on MIPS, `lui`/`addiu`
pairs) and decompiled a matched sample. All numbers are for that out-of-the-box state; the main PS2 project has
more names and types.

**Matched sample.** A string that both builds share and that exactly one function references on each side pairs
those two functions. Of 2,023 strings referenced on both sides, 1,760 are unique in this way. They give **341
one-to-one function pairs** (114 backed by two or more strings). Of the 90 pairs where both functions also load a
source path, 87 load the same file, and 302 of 340 consecutive pairs keep the same order in both `.text`
sections, so the pairs are sound. **Evidence:** inferred.

## Numbers {#numbers}

| Measure | PS2 (`SLUS_212.15`) | Xbox (`default.xbe`) |
| --- | ---: | ---: |
| Functions after auto-analysis | 13,789 | 13,373 (10,843 in `.text`, 2,530 in the XDK and Bink sections) |
| `.text` bytes inside a function | 4,020,160 of 4,154,744 (97%) | 2,576,903 of 3,457,788 (75%) |
| Functions named by library signatures | 55 (EE kernel stubs) | 580 (CRT, XAPI, kernel, C++ EH; D3D 104, DSOUND 199, XACT 20) |
| Game functions named | 0 | 0 (no PDB on the disc, no exports) |
| RTTI type names | 0 from game code | 5, all CRT exception classes |
| Source-path strings | 152, one copy each | 819 copies (one per use) |
| Functions loading a source path (anchors) / files anchored | 300 / 134 | 338 / 141 |
| Anchors in `Camera/`, `TaskEngine/`, `Human/`, `Animation/` | 17, 9, 3, 3 | 16, 9, 4, 3 |
| Functions referencing any string | 1,521 | 1,577 |
| Lua binding names in one registration function | 955 at `0x0037d420` | 959 at XBE `0x0028d8a0` (955 shared) |
| **Decompiled sample: the 341 pairs** | | |
| Lines of C | 43,326 | 38,775 |
| String literals shown inline | 85 | 2,316 |
| Signatures with a calling convention | 0 | 140 (`__fastcall` 87, `__thiscall` 53) |
| `unaff_` / `in_` / `extraout_` artefacts (functions affected) | 301 (23) | 802 (69) |
| `CONCAT` artefacts | 185 | 43 |
| `float` declarations per 100 lines | 2.1 | 4.5 |
| Warnings (of which the stack-cookie injection note) | 57 (0) | 126 (62) |
| Functions using VU0 macro registers (`in_vf*`) | 21 | 0 |

**Evidence:** inferred (measured with the method above; counts depend on Ghidra's analysis).

## What the numbers say {#findings}

**Easier on the Xbox.**

- **Strings read in place.** x86 pushes a string's address as an immediate, so the decompiler prints the literal
  (`"vags/misc/click_01"`). On the PS2 the same call shows a raw address built from a `lui`/`addiu` pair
  (`0x554910`). This is the largest single gain: 27 times as many literals in the same 341 functions.
- **Calling conventions.** MSVC's `__thiscall` (this in `ECX`) and `__fastcall` show which argument is the object.
  GCC 2.x on the EE passes `this` in `a0` like any argument.
- **Library code is named.** 580 functions (C runtime, XAPI, D3D, DirectSound) are named by signatures, so calls
  like `sprintf` and `strncpy` read as such; on the PS2, RenderWare, the SCE libraries and libc are unnamed until
  an analyst names them.
- **Floats and vectors.** Float code decompiles as plain `float` arithmetic; 21 of the 341 PS2 functions use VU0
  macro instructions, which decompile as opaque `in_vf0` registers.
- **More anchors.** MSVC keeps one copy of `__FILE__` per use, so 338 functions in 141 files load a path, against
  300 in 134. **Evidence:** inferred: 819 path copies, 818 of whose addresses occur as words in `.text`.

**Not easier.**

- **No names for game code.** The PDB path is embedded but the PDB is not on the disc; there are no exports, no
  RTTI for game classes (built without it, as on the PS2) and, on both builds, no assert messages but Lua's own.
- **Register artefacts.** MSVC's optimiser passes values in `EAX`, `ESI`, `EBX` and the stack across calls
  (`in_EAX` 183, `unaff_ESI` 180, `in_stack` 256), so three times as many functions need prototype fixes as on
  the PS2.
- **Coverage.** A quarter of the XBE's `.text` is in no function after auto-analysis (padding, jump tables, or code
  reached only through vtables), against 3% on the PS2.
- **Different layouts.** The same function reads a field at `+0x3ac` on the Xbox and at `+0x454` on the PS2,
  steps an array of `0xdc`-byte elements against `0x100`, and calls a virtual through a 4-byte slot (`+0x44`)
  against a GCC 2.x 8-byte `{delta, fn}` slot (`+0x90`) (XBE `0x000e1640`, `0x001b8d38`). Offsets, sizes and vtable
  slots found on the Xbox **do not transfer** to the PS2; only the shape of the logic does. **Evidence:** confirmed
  (code) at those two addresses.
- **Lua bindings give no extra names.** Both builds register the same 955 binding names from one function; the
  Xbox adds four (`SoundHandle`, `WorldPath`, `XBoxCompileVertexShader`, `XBoxLoadVS`).

## Cross-referencing cost {#cross-reference}

Cheap at the file and function level, not below it.

- **File level: free.** 121 source files are anchored on both builds, and both linkers keep the object order, so a
  PS2 range on [Source map](source-map.md) has a matching Xbox range.
- **Function level: about 340 pairs for free, then manual.** Unique shared strings pair 341 functions. Filling the
  gaps between pairs by position adds only 106 more: MSVC and GCC inline differently, so the counts of functions
  between two pairs rarely agree.
- **Use.** For a PS2 function in a pair, the Xbox twin shows which string, sound or binding each call uses, and
  which argument is `this`. Every claim on a research page must still be read in the PS2 code at a PS2 address.

## Runtime access {#runtime}

xemu (QEMU-based) has a GDB stub (`-s`, TCP port 1234; `-S` starts paused) and the QEMU monitor (memory dumps,
registers, trace events). GDB's remote protocol gives memory reads and writes, breakpoints and watchpoints, about
what PCSX2's debugger and PINE give together. Coney has no tooling for it, and an observation in xemu is not
`confirmed (runtime)` for the PS2. **Evidence:** from xemu's guest-debugging documentation; not tried.

## Recommendation {#recommendation}

**Adopt the Xbox executable as an optional, read-only cross-reference for analysts; do not make it a reference
and do not build xemu tooling.** It is not easier overall: game code is equally unnamed, MSVC's register
artefacts and lower function coverage offset the cleaner x86 output, and its layouts differ from the PS2's. It does
pay off for string-heavy and float-heavy code: front end, audio cues, scripting glue, camera and other vector maths.
There, opening the Xbox twin of a PS2 function first (found through a shared string) shows its literals,
`this` argument and float expressions quickly. Analysts who do so cite only PS2 addresses as evidence and keep the
XBE project outside the repository.

## Open questions

1. How much of the XBE's unanalysed 25% of `.text` is code that a vtable scan or Ghidra's aggressive instruction
   finder would recover?
2. Do the XDK's `D3D`/`DSOUND` signatures cover the statically linked XGraphics and Bink sections (6 and 0 named)?
