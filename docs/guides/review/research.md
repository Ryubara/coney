# Research review checklist

For a reviewer of pages under `docs/research/` and the planned research database in `research/`.
This is reverse-engineering work, so it is reviewed on Opus 5.5 ([index](index.md#which-model-reviews-what)). The
reviewer may consult the original binary; it reports findings in the
[format on the index page](index.md#finding-format) and never edits.

How claims are graded is set in [Evidence levels](../research-evidence.md#evidence-levels) and how they are written
up in the [research workflow](../research-workflow.md); the page layout is in
[Writing these docs](../writing-docs.md#research-pages). This page says what to check.

## Checklist

- [ ] **Every non-obvious claim has an evidence level**, written next to it, using the four levels of
  [Evidence levels](../research-evidence.md#evidence-levels). Read the page claim by claim; an ungraded claim is
  Important. `docs check` finds an address with no level in its block, row or lead line; it cannot find a claim that
  cites no address.
- [ ] **The levels are used correctly.**
    - *confirmed (code)* cites an address where the claim can be read in the original code;
    - *confirmed (runtime)* is used only for an observation in PCSX2, or in xemu for a claim about the Xbox build (a
      run record with an `[xemu]` table backs no other claim), and the claim cites the run record that observed it
      (an old claim on a legacy page states the emulator's version and the method instead). The record's values support
      the claim's number and scenario. A check against the game's files is corroboration written beside the claim,
      never this level;
    - *inferred* names the evidence it follows from, and the basis is real and points one way;
    - *speculative* is written as a guess, linked to an open question that says how to test it.
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
- [ ] **The page is the right kind and the right owner.** Its front matter names its kind, and a system or topic page
  its system; the sections are those of that kind ([Research pages](../writing-docs.md#research-pages)). The page
  owns what it describes, the function's role is written there once and other pages link to it (`docs check` finds
  a copied paragraph, not a restated one in other words). A part with nothing known says so in one line.
- [ ] **The page is a map to the original code.** Every described behaviour names the original function that
  implements it (address and name) where its logic lives, so an engineer can open that function in Ghidra from the
  page. Where the code alone would not tell the engineer a field offset, a unit, a byte order, a constant, the order
  of two steps or an edge case, the page says it. Each gap is a finding: Important when a behaviour names no
  function or a number the port needs is missing, Minor otherwise.
- [ ] **The page meets the standard as a whole.** `coney-tools docs check` passes on it (a page the change touches
  is held to all of [Writing these docs](../writing-docs.md#touch-it)), and where a lead line under a `###` heading
  grades the blocks below it ([the evidence rule](../writing-docs.md#evidence-levels)), that grade is true of every
  one of them. A page in the legacy file is excused only from what its entry lists, and no new page is listed.
- [ ] **Open questions are real.** Each says what is unknown, what depends on it and how it could be found out.
- [ ] **The research database matches the page** (planned; applies when it exists): each function, global and type
  the page names has a YAML entry with the same address, name and evidence level, and no entry contradicts the page.
  A page claim with a higher level than its entry, or the other way round, is Important.
- [ ] **The living page was updated in place.** No new dated notes page where a living page for the subject
  exists; cross-links work.
- [ ] **`mkdocs build --strict` passes.** Run it (`.venv/Scripts/python -m mkdocs build --strict`); every new page
  is in the nav.

## Severity examples

- **Critical:** a whole decompiled function pasted into a page; game data (file bytes, a dump) on a page; a
  build that fails.
- **Important:** a claim with no evidence level; a level used wrongly (a file check graded as confirmed
  (runtime)); an address that does not resolve to what the page says; a missing part of the subsystem page; a gap
  that stops an engineer.
- **Minor:** unclear wording; a page that says "Confidence: confirmed from code" instead of the current wording;
  an open question that does not say how to find the answer out.
