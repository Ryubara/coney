# Orig review

This is the rubric for comparing a Coney function that carries an `@orig` tag with the original function at that
address, in the shared Ghidra project (NTSC-U `SLUS_212.15`, [Ghidra](../ghidra.md)). The
[reviewer agent](../agents.md#reviewer) applies it to a task's changed functions ([Task review](task.md): the `orig`
kind of its packet), to a legacy branch's before release (pr mode, one of the
[legacy release checks](../how-we-work.md#pr-review), retired at the cutover) and to whole folders of existing code
(sweep mode, whose findings become issues: [Legacy fidelity](../how-we-work.md#fidelity-audit)). The rule it checks
is [Porting, not transcribing](../conventions.md#porting).

A function that follows the Xbox port carries `@orig xbox 0x<address>` ([Xbox tags](../conventions.md#xbox-tags)).
The same two counts apply, read against that address in the Xbox `default.xbe` in a Ghidra project of the XBE. No
study or compare stamp covers an Xbox address, so the reviewer reads it by hand and says so in the verdict's note. The
verdict names it `xbox 0x<address>` in the function's `addresses` and in `reviewed` ([the verdict](task.md#verdict)),
and `pr verify` wants a passing class for it as for a PS2 address. The legacy `orig check-review` reads the same
spelling.

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

**What makes a difference deliberate.** A comment makes a difference deliberate only when the reason it gives is one
the conventions allow:

- a platform substitution;
- an id in place of a pointer;
- network play;
- a marked stand-in for a part of the original that is not ported yet ([Stand-ins](../testing.md#stand-ins)).

A difference a player can see for any other reason (taste, simplicity, "Coney's version is cleaner") is a
`diff-bug` even when it is commented, because gameplay matches the original until the game is complete. When the
difference is known and commented, but only a run would show whether a player sees it, the class is
`diff-deliberate-ok`, and the note says what to measure.

**A tag on part of a function.** A Coney function may port only one part of a larger original function (the cap check
of a per-gang update), with the rest elsewhere or not ported yet. That is not `wrong-address`. Its comment should
say which part it ports (`the cap check of`); when it does not, say so in a note.

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

`coney-tools orig check-copy` finds the mechanical signs ([orig](../coney-tools-fidelity.md#orig)); the reviewer judges
the shape, which no tool can. For a function with a pairs file, the reviewer's input is that file: each pair names a
piece of the original (by line numbers of the saved study, [Pairs](../stamps.md#pairs)), the Coney code that does it and
whether they are the same, a stand-in or not ported; the tool has already checked that every line is in a pair. The
reviewer also opens the `stubs` of each `fails-first` stamp ([Fails first](../stamps.md#fails-first)): a stub in a test
commit that implements the behaviour, rather than returning a default or an empty result, is a finding.

A short function whose logic is its shape (a formula, a three-way test) is not a transcription just for having the
same shape. The question is whether the code was written from understanding or copied from the listing. Signs of
understanding are names that say what a value means, Coney's own types, and comments that give the reason. Name one
or two concrete signs in the note whenever the class is `transcription`.

## Classes {#classes}

Each function gets exactly one class. Judge `wrong-address` first, because a function cannot be compared with the
wrong original. Otherwise the class is the first in the table, read from the bottom up, that applies. For example,
a transcription that also has a bug is `diff-bug`.

| Class | Meaning | The note gives | A pull request |
| --- | --- | --- | --- |
| `clean` | Same behaviour, own expression. | Nothing, or what was checked. | passes |
| `diff-deliberate-ok` | It differs on purpose, and a comment at the difference says why. | The difference. | passes |
| `unclear` | Reading the code cannot settle it, and only a PCSX2 run can. | What to measure, and where. | passes, with a note |
| `diff-uncommented` | It differs on purpose (platform, design, network play), but nothing says so. | The difference, and the comment it needs. | fails |
| `wrong-address` | The tag points at a function that is not this behaviour. | The right address, if it was found. | fails |
| `transcription` | The shape was copied rather than ported. | The signs (which statements, which artifacts). | fails |
| `diff-bug` | The behaviour differs from the original with no reason. | The original's behaviour, in our own words. | fails |

These cases come up often:

- **An omitted step.** A step the original has and Coney leaves out is `diff-uncommented` when Coney cannot do it
  yet (the system it needs is not ported) and nothing at the code says so. It is `diff-bug` when Coney has what it
  needs.
- **A note on the research page.** A difference described only on the research page (its "Coney choices") has no
  comment at the difference. The comment belongs at the code, and it may link the page.
- **A "Coney's reading" that the original contradicts.** A comment that guesses at the original's behaviour, where
  the original turns out to do something else, is `diff-bug`. The note also asks for the comment to be fixed.
- **A ported function that nothing calls.** A function that matches the original but has no caller, so the
  behaviour never happens in play, is `diff-bug`, and the note names the original's caller.
- **One original function split over several Coney functions, or the reverse.** The finding goes on the Coney
  function that holds the code. A tag that cites only a small getter, while the logic sits on another Coney
  function, gets a note.
- **A tag name that differs from Ghidra's** at the right address is a note, not a class.

A pull request passes the orig review only when every function it changes is `clean`, `diff-deliberate-ok` or
`unclear`. An `unclear` function goes to the [runtime comparison](../testing.md#runtime-comparison) that the
`Runtime:` line of the description already requires. In a sweep, every class except `clean` and
`diff-deliberate-ok` becomes a fix unit for the coordinator, and those two take the function off the
[audit queue](../coney-tools-fidelity.md#orig) until it changes.

Every finding names the Coney function, its `file:line` and the `@orig` address, and gives an evidence level as
research claims do ([Evidence levels](../research-evidence.md#evidence-levels)):

- `confirmed (code)`: you read both sides of what the finding is about;
- `inferred`: part of it rests on a callee you did not open, or on the research page.

In a function of thousands of lines, read every path the finding is about on both sides, and say in the note which
paths you did not read. A `clean` class there means clean on the paths read: write `inferred` unless you read all
of them.

A sweep may also give each finding a **severity**:

- `high`: a player sees it in normal play;
- `low`: an edge case, a one-update overshoot, or a capacity off by one.

Severity is the optional eighth column of `FINDINGS.tsv`, and it orders the fix units.

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

## How the reviewer works {#procedure}

The reviewer only reads. Bash is for read-only commands (`git diff`, `git log`, `git show` and the `coney-tools orig`
commands below); in Ghidra it uses only the read tools it has. The only files it writes are the ones its mode names,
and none holds pasted code ([the limits](#limits)).

**In a task review** the packet lists the changed functions and the tool part has checked that each has a current
`compared` stamp and a clean copy check ([Task review](task.md#tool-part)). The reviewer reads each pair of the pairs
file and the Coney function it names, decompiles each address (`disassemble_function` where the decompiler is unclear),
opens callees as far as the comparison needs, gives each function exactly one [class](#classes) and writes it into the
`functions` of `REVIEW.json` ([format](task.md#verdict)).

**Pr mode**, for the legacy release check that predates `task review` (retired at the cutover):

1. Run `uv run --project python coney-tools orig review-stamp --branch <branch> --base <base>` from the main checkout
   (or `--worktree <path>`). It prints the `tip`, the `base` and the `@orig` functions the branch adds or changes: each
   one's `key`, `where`, `coney`, `addresses` and `body_sha256`. When it lists none, write no verdict and say so: the
   branch needs none.
2. Review every listed function. If there are more than can be read with care, sample by risk as the
   [limits](#limits) say, and name the skipped ones.
3. Compare each function: read the Coney function at `where` and enough of what it calls to know its behaviour,
   decompile each address, and judge both counts.
4. Write the verdict `ORIG-REVIEW.json` next to `PR.md`, in exactly [the format below](#verdict): copy `tip`, `base`,
   each `key` and `body_sha256` from the stamp, give each function the `addresses` compared, set `rubric_version`, list
   every compared address in `reviewed`, make each failing function a `blocker` finding at its `file:line` and every
   other remark a `note`; `verdict` is `pass` only when no class fails and no finding is a blocker.
5. Write `ORIG-REVIEW.md`: the verdict on the first line, then one line per function (Coney name, `file:line`, address,
   class, note), then what was sampled and why. The final message is the verdict and the blockers, in a few lines.

**Sweep mode** works on a group folder under the workspace's `scratch/orig-sweep/` and a scope of `src/` folders or
addresses ([Sweep findings](#findings)). It is a mode for the legacy-fidelity sweep and ends when that sweep is filed,
at the [cutover](../how-we-work.md#cutover):

1. Run `uv run --project python coney-tools orig review-stamp --folder <src/folder/>` (or `--address 0x...`,
   repeatable) from the main checkout. It lists the functions in scope that the ledger has not reviewed at their
   current body, with the `commit` it read. When `PROGRESS.md` already exists in the group folder, continue from it.
2. Compare each function as above and append its row to `FINDINGS.tsv` at once. After each file, update `PROGRESS.md`
   with the files done and the commit.
3. Record with `uv run --project python coney-tools orig record-findings <group>/FINDINGS.tsv --ref <commit>`, so the
   ledger and `orig legacy-queue` know what is done.
4. Summarise in the final message, which the launcher saves as `SUMMARY.md` (the reviewer does not write it): the
   counts per class, the ten worst findings with their Coney function, `file:line` and address, anything systemic, and
   the unmatched rows `orig record-findings` printed.

At 500k context, write `PROGRESS.md` and a handover note in the group folder and stop.

## The pull request verdict {#verdict}

`task review` records a function's class in its own verdict file ([Task review](task.md#verdict)). The release check
that predates it, `orig check-review`, reads the file below for a legacy branch until the cutover removes it. In pr mode
the agent writes `ORIG-REVIEW.json` and `ORIG-REVIEW.md` next to the description file (`PR.md`). The JSON has exactly
these keys:

| Key | Holds |
| --- | --- |
| `verdict` | `"pass"` when every reviewed function's class passes and no finding is a blocker; otherwise `"fail"`. |
| `tip` | The full hash of the commit reviewed (the stamp's `tip`). |
| `base` | The full hash where the branch left its base (the stamp's `base`). |
| `reviewed` | Every `@orig` address that was compared, as `"0x%08x"` strings. For a function that carries many tags (a table of script bindings, each lambda tagged), list the ones the change touches, and say which in the note. |
| `findings` | `{severity, where, text}` objects, as in `REVIEW.json`. `severity` is `blocker` or `note`, and `where` is `file:line`. |
| `rubric_version` | The rubric version above, as an integer. |
| `functions` | One `{key, body_sha256, addresses, class, evidence, note}` object per reviewed function. `key` and `body_sha256` are copied from the stamp, and `addresses` lists the addresses compared for that function (a subset of the stamp's, each also in `reviewed`). |
| `sampled` | `null`, or the reason some functions were not reviewed. |
| `not_reviewed` | The stamp keys of the functions skipped. |

`coney-tools orig check-review` (and the release step that calls it) passes a branch only when:

- the verdict covers every `@orig` function the branch adds or changes;
- each of those functions has a passing class, its stamp's body hash, and at least one compared address, which is
  one of its own and is in `reviewed`;
- no finding is a blocker, and the rubric version matches.

The verdict binds to the function bodies it reviewed (their code: comments and white space are not part of the
[body hash](../coney-tools-fidelity.md#body-hash)), so a rebase that leaves them unchanged keeps it, and any
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
    7. `note`: one line, in our own words;
    8. `severity` (optional): `high` or `low`.
- `PROGRESS.md`: the files done, so a successor continues without redoing work.
- The summary: the counts per class, the ten worst findings and anything systemic. It is the agent's final
  message, which the launcher saves as `SUMMARY.md` beside the findings. Claude Code refuses a subagent's own
  writes of a summary file.

`coney-tools orig record-findings <FINDINGS.tsv>... --ref <commit>` records the rows in the ledger against the functions
at the commit the sweep read, and prints what is still unreviewed.

## The ledger {#ledger}

Every review is recorded in one ledger: `pull_requests.orig_review.ledger` under the workspace's scratch folder
(local, never committed). An entry holds:

- the function's key, its body hash, its `SLUS_212.15` addresses (`addresses`) and, for an `@orig xbox` tag, its
  `default.xbe` addresses (`xbox`), so an Xbox-only function keeps its address;
- the commit, the class and the mode (`pr` or `sweep`);
- the source file of the review.

A function with no entry is unreviewed. One whose body changed since its last review is stale and is reviewed
again. `orig review-stamp --folder` lists only these, and `orig legacy-queue` ranks them, so every `@orig` function is
reviewed once and again after each change. The status by address keeps the two builds apart, as one number names
different code in each: `orig legacy-queue` reads the PS2's, as it ranks the PS2 function index.
