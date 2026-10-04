# Code review checklist

For a reviewer of C++ under `src/` and `tests/`, asset tools under `tools/`, Python under `python/`, and CMake and
scripts. Apply every item to the files in the diff; each one says where to look. The rules themselves are in
[Code conventions](../conventions.md); this page does not repeat them, it says what to check. Report findings in
the [format on the index page](index.md#finding-format).

## Legal

- [ ] **No game data.** Look at every added file and every test fixture: no executable, BIOS, asset, extracted
  file or dump, and no byte array that is a copy of one. Fixtures are synthetic, written by hand (Critical).
- [ ] **No transcribed decompiler output.** Look for Ghidra's habits: variables named `iVar1`, `uVar2`,
  `param_1`, `local_20`; `undefined4` types; `goto` ladders or `LAB_` labels; `(code *)` casts; expressions
  nobody would write by hand. One such sign is a finding; several make it Critical.
- [ ] **Clean room per function.** For each reimplemented function (each `@orig` tag), the author's report (the
  pull request description, or the agent's report) says the author never read its original code, decompiler output
  or disassembly ([clean room](../research-workflow.md#clean-room)). A function whose implementer is also its
  analyst is Critical.
- [ ] **SPDX header.** Each file of ours starts with `// SPDX-License-Identifier: GPL-3.0-or-later` (`#` in Python,
  CMake and scripts), as the first line, or after a `#!` line. Check the exceptions against the
  [licence-header table](../conventions.md#licence-header): `docs/`, generated documents, JSON and small config
  files take none; files another tool installs keep their own header and must not be edited by hand.

## Correctness

- [ ] **Tests exist and test behaviour.** Each new function or format has a test under `tests/` at the mirrored
  path. Read the assertions: they check results, not just that the call returns. A test name says what it checks.
- [ ] **Written test-first where possible.** The history or the author's report shows the test failing before the code.
  A test that cannot fail does not count.
- [ ] **Disc data edge cases.** A parser has tests for empty input, input shorter than its header, input shorter
  than its own entry count, and a count large enough to overflow an allocation. A parser that accepts truncated
  data silently is Critical.
- [ ] **Bad data is an `Error`, never an assert or a crash.** `CONEY_ASSERT` appears only for programmer errors
  ([Errors](../conventions.md#errors)). No exceptions are thrown.
- [ ] **Fixtures are synthetic.** Tests that need the disc skip when it is not configured and print aggregates
  only.
- [ ] **The build is clean.** Warning-free on every supported compiler, and the tests pass. Run them.

## Readability for a hand-writer

The test: could someone write their own version of this subsystem from the code and the docs alone?

- [ ] **Doc comments.** Every public type and function has a `///` (or a docstring) saying what it is for, what it
  returns and how it fails.
- [ ] **Why comments.** Every non-obvious line says why, not what. Look at magic numbers, offsets, padding and
  ordering; each has a reason written next to it.
- [ ] **`@orig` tags.** Each function that reimplements an original one carries
  `@orig 0x<8 hex digits> <Name> (<File>.cpp)`, one per original function; a function with no counterpart carries
  none. A reimplemented function without a tag is Important.
- [ ] **Research link.** The doc comment names the research page it was written from (`docs/research/...`), and
  the path exists. Missing is Important.
- [ ] **No commented-out code.**

## Conventions

- [ ] **Naming:** files `snake_case`, types `PascalCase`, functions and variables `camelCase`, members
  `m_camelCase`, constants `kPascalCase`, macros `CONEY_UPPER` ([Naming](../conventions.md#naming)).
- [ ] **File layout:** one class per header and source pair; folders mirror the original source tree in
  `snake_case`; tests mirror `src/`; no empty placeholder folders; `#pragma once`.
- [ ] **Includes:** own header, standard library, third-party, Coney, each group sorted; nothing relied on
  through another header.
- [ ] **Errors:** `std::expected<T, coney::Error>` for recoverable failures; no exceptions.
- [ ] **Memory:** RAII for every resource; no owning raw pointers; disc data read through `coney::io::Reader`;
  no `reinterpret_cast` of a buffer to a struct or an integer.
- [ ] **Platform isolation:** OS and graphics-API calls only in `src/platform/`; no `#ifdef _WIN32` elsewhere;
  engine code never reads the clock, the random source or input directly (test mode depends on this).
- [ ] **File size:** a source file over about 500 lines has a reason, or the review asks for a split.
- [ ] **Python:** `pathlib` only; type-annotated for `mypy --strict`; reachable through `coney-tools`, not a loose
  script; docstrings on public items.
- [ ] **Formatting** is whatever clang-format and ruff produce; do not raise formatting findings by hand.

## Severity examples

- **Critical:** engine code transcribed from Ghidra output; a game file or extracted asset staged for commit; a
  format parser that silently accepts truncated data.
- **Important:** a reimplemented function without `@orig` or a research link; a research claim with no evidence
  level; a commit title outside the areas in `.github/commit-conventions.json`.
- **Minor:** unclear wording in a research page; a test name that does not say what it checks.
