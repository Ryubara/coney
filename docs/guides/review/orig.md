# Orig review

This is the rubric for comparing a Coney function that carries an `@orig` tag with the original function at that
address, in the shared Ghidra project (NTSC-U `SLUS_212.15`, [Ghidra](../ghidra.md)). The
[orig-reviewer agent](../agents.md#orig-reviewer) applies it to a branch's changed functions before release (pr mode)
and to whole folders of existing code (sweep mode). The [fidelity audit](../how-we-work.md#fidelity-audit) uses it
as well. The rule it checks is [Porting, not transcribing](../conventions.md#porting).

**Rubric version: 1.** The version is `pull_requests.orig_review.rubric_version` in
[.github/conventions.yml](repo:.github/conventions.yml). Raise both together when a class or a count changes
meaning; a verdict made under another version is refused.

## The two counts

Each function is judged on two counts, and a function can fail either one.

### 1. Fidelity: the same behaviour {#fidelity}

The same inputs give the same effects in the same order. Compare:

- the math and the constants, including the units (frames or seconds, degrees or radians), float against integer
  arithmetic, and where the original rounds, truncates or clamps;
- branch conditions and their order, loop bounds, early returns, and the default case;
- the order of effects: which state is written before which call, and which events fire and when;
- random draws: how many there are, their range, and their order (in Coney, from `GameRandom` only);
- the calls made, and what each callee does when the callee is also ported.

Platform substitutions are fine when the behaviour is kept and a comment at the substitution says so. Examples are
SDL3 in place of the PS2 pads, librw in place of the GS, ids in place of pointers, and a `GameRandom` draw in place of
the original's `rand`. Precision is a substitution too: the EE computes in single precision, and a computation that
Coney does in `double`, or with a different evaluation order, must not change an outcome the player can see.

### 2. Own expression: ported, not transcribed {#expression}

Ours looks like this:

- Coney's types, names, ids and APIs;
- structure chosen for Coney's design (its classes, its events, its data tables);
- the logic kept;
- comments that explain why.

Signs of a transcription:

- the decompiler's statement-by-statement shape, with names swapped in;
- raw pointer-offset reads in place of Coney's types;
- Ghidra's temporaries, casts and goto-shaped control flow;
- `undefined*` types;
- dead code or compiler artifacts carried over (redundant reloads, sign-extension casts that mean nothing in C++, a
  flag tested twice because two inlined callees each tested it).

`coney-tools orig copy-check` finds the mechanical signs ([orig](../coney-tools.md#orig)); the reviewer judges the
shape, which no tool can.

A short function whose logic is its shape (a formula, a three-way test) is not a transcription just for having the
same shape. The question is whether the code was written from understanding or copied from the listing. Signs of
understanding are names that say what a value means, Coney's own types, and comments that give the reason. Name one
or two concrete signs in the note whenever the class is `transcription`.

## Classes {#classes}

Each function gets exactly one class: the first of these that applies, read from the bottom up. For example, a
transcription that also has a bug is `diff-bug`.

| Class | Meaning | The note gives | A pull request |
| --- | --- | --- | --- |
| `clean` | Same behaviour, own expression. | Nothing, or what was checked. | passes |
| `diff-deliberate-ok` | It differs on purpose, and a comment at the difference says why. | The difference. | passes |
| `unclear` | Reading the code cannot settle it, and only a PCSX2 run can. | What to measure, and where. | passes, with a note |
| `diff-uncommented` | It differs on purpose (platform, design, network play), but nothing says so. | The difference, and the comment it needs. | fails |
| `wrong-address` | The tag points at a function that is not this behaviour. | The right address, if it was found. | fails |
| `transcription` | The shape was copied rather than ported. | The signs (which statements, which artifacts). | fails |
| `diff-bug` | The behaviour differs from the original with no reason. | The original's behaviour, in our own words. | fails |

A pull request passes the orig review only when every function it changes is `clean`, `diff-deliberate-ok` or
`unclear`. An `unclear` function goes to the [runtime comparison](../testing.md#runtime-comparison) that the
`Runtime:` line of the description already requires. In a sweep, every class except `clean` and
`diff-deliberate-ok` becomes a fix unit for the coordinator, and those two take the function off the
[audit queue](../coney-tools.md#orig) until it changes.

Every finding names the Coney function, its `file:line` and the `@orig` address, and gives an evidence level as
research claims do ([Evidence levels](../research-workflow.md)):

- `confirmed (code)`: you read both sides;
- `inferred`: part of it rests on a callee you did not open.

## Limits {#limits}

- **Read only.** The reviewer never renames, comments on or retypes anything in Ghidra, and never edits `src/`,
  `docs/` or the research database. It writes only its report files.
- **No decompiled code in any file.** A note describes the original in our own words. At most a short phrase (a
  constant, an operator, a few tokens) may point at a line, as on a research page ([LEGAL.md](repo:LEGAL.md)).
  Never paste a listing, a pseudo-C block or a run of disassembly.
- **Large diffs are sampled by risk.** When a branch changes more functions than can be read with care, review them
  in this order:
    1. new tags;
    2. functions whose math or branches changed;
    3. functions named in open bug issues;
    4. the rest by size.

  List what was skipped in `not_reviewed`, and give the reason in `sampled`.

## The pull request verdict {#verdict}

In pr mode the agent writes `ORIG-REVIEW.json` and `ORIG-REVIEW.md` next to the description file (`PR.md`). The JSON
has exactly these keys:

| Key | Holds |
| --- | --- |
| `verdict` | `"pass"` when every reviewed function's class passes and no finding is a blocker; otherwise `"fail"`. |
| `tip` | The full hash of the commit reviewed (the stamp's `tip`). |
| `base` | The full hash where the branch left its base (the stamp's `base`). |
| `reviewed` | Every `@orig` address that was compared, as `"0x%08x"` strings. For a function that carries many tags (a table of script bindings, each lambda tagged), list the ones the change touches, and say which in the note. |
| `findings` | `{severity, where, text}` objects, as in `REVIEW.json`. `severity` is `blocker` or `note`, and `where` is `file:line`. |
| `rubric_version` | The rubric version above, as an integer. |
| `functions` | One `{key, body_sha256, class, evidence, note}` object per reviewed function, with `key` and `body_sha256` copied from the stamp. |
| `sampled` | `null`, or the reason some functions were not reviewed. |
| `not_reviewed` | The stamp keys of the functions skipped. |

`coney-tools orig check-review` (and the release step that calls it) passes a branch only when:

- the verdict covers every `@orig` function the branch adds or changes;
- each of those functions has a passing class, its stamp's body hash, and at least one of its addresses in
  `reviewed`;
- no finding is a blocker, and the rubric version matches.

The verdict binds to the function bodies it reviewed, so a rebase that leaves them unchanged keeps it, and any
change to one of them makes it stale. A pass is recorded in the ledger. `ORIG-REVIEW.md` is the same verdict for a
person: the verdict, then one line per function with its class and note, then the findings.

## Sweep findings {#findings}

In sweep mode the agent writes to `scratch/orig-sweep/<group>/` (a group is a `src/` folder or a list of
addresses):

- `FINDINGS.tsv`: a header row, then one row per tagged function with these tab-separated columns, written as each
  function is done so nothing is lost:
    1. `addr`: `0x%08x`;
    2. `orig_name`: the tag's name;
    3. `coney_function`: such as `Brain::think`;
    4. `file:line`;
    5. `class`;
    6. `evidence`;
    7. `note`: one line, in our own words.
- `SUMMARY.md`: the counts per class, the ten worst findings, and anything systemic.
- `PROGRESS.md`: the files done, so a successor continues without redoing work.

`coney-tools orig record <FINDINGS.tsv>... --ref <commit>` records the rows in the ledger against the functions at
the commit the sweep read, and prints what is still unreviewed.

## The ledger {#ledger}

Every review is recorded in one ledger: `pull_requests.orig_review.ledger` under the workspace's scratch folder
(local, never committed). An entry holds:

- the function's key, its addresses and its body hash;
- the commit, the class and the mode (`pr` or `sweep`);
- the source file of the review.

A function with no entry is unreviewed. One whose body changed since its last review is stale and is reviewed
again. `orig review-stamp --folder` lists only these, and `orig queue` ranks them, so every `@orig` function is
reviewed once and again after each change.
