# Research review checklist

For a reviewer of pages under `docs/research/` and, once it exists, the research database in `research/`.
This is reverse-engineering work, so it is reviewed on Opus 5.5 ([index](index.md#which-model-reviews-what)). The
reviewer may consult the original binary; it reports findings in the
[format on the index page](index.md#finding-format) and never edits.

How claims are graded and written up is set in the [research workflow](../research-workflow.md); the page layout
is in [Writing these docs](../writing-docs.md#subsystem-page). This page says what to check.

## Checklist

- [ ] **Every non-obvious claim has an evidence level**, written next to it, using the four levels of
  [Evidence levels](../research-workflow.md#evidence-levels). Read the page claim by claim; an ungraded claim is
  Important.
- [ ] **The levels are used correctly.**
    - *confirmed (code)* cites an address where the claim can be read in the original code;
    - *confirmed (runtime)* is used only for a PCSX2 observation, and the claim states the PCSX2 version and the
      method (breakpoint, memory watch, frame). A check against the game's files is corroboration written beside
      the claim, never this level;
    - *inferred* names the evidence it follows from;
    - *speculative* is written as a guess, with how to test it.
- [ ] **Addresses are well-formed:** `0x` and eight hex digits (`0x001490b8`), all in NTSC-U `SLUS_212.15`, and
  the page says which executable it was checked against.
- [ ] **Addresses resolve.** Spot-check two addresses per page, chosen by the reviewer, not the author: the
  function or global at that address matches the name and behaviour the page gives. Use ghidra-mcp when it is
  available, and say which two you checked. When it is not available, say so in the report; the item is then
  unchecked, not passed.
- [ ] **No whole-function quotes.** No decompiled or disassembled function pasted in, and no long stretch of one:
  only short snippets that make a point, and pseudocode that explains behaviour rather than repeating control flow
  line by line ([LEGAL.md](repo:LEGAL.md#research-limits)). Pasted code is
  Critical.
- [ ] **No game data.** Tables of offsets, constants and counts are fine; the bytes of a game file, a dump or an
  extracted asset are Critical.
- [ ] **The subsystem page has all six parts, in order:** Purpose, Original structure, Data, Behaviour, Coney's
  implementation, Open questions. A part with nothing known says so in one line. File-format pages follow the same
  order.
- [ ] **An implementer could write the code from the page alone.** Imagine writing the code with only this page.
  For every point where you would have to look at the original, the report names what is missing: a field offset,
  a unit, a byte order, a constant, the order of two steps, an edge case. Each gap is a finding: Important when
  code cannot be written without it, Minor otherwise.
- [ ] **Open questions are real.** Each says what is unknown, what depends on it and how it could be found out.
- [ ] **The research database matches the page** (once it exists): each function, global and type the page names
  has a YAML entry with the same address, name and evidence level, and no entry contradicts the page. A page
  claim with a higher level than its entry, or the other way round, is Important.
- [ ] **The living page was updated in place.** No new dated notes page where a living page for the subject
  exists; cross-links work.
- [ ] **`mkdocs build --strict` passes.** Run it (`.venv/Scripts/python -m mkdocs build --strict`); every new page
  is in the nav.

## Severity examples

- **Critical:** a whole decompiled function pasted into a page; game data (file bytes, a dump) on a page; a
  build that fails.
- **Important:** a claim with no evidence level; a level used wrongly (a file check graded as confirmed
  (runtime)); an address that does not resolve to what the page says; a missing part of the subsystem page; a gap
  that stops an implementer.
- **Minor:** unclear wording; a page that says "Confidence: confirmed from code" instead of the current wording;
  an open question that does not say how to find the answer out.
