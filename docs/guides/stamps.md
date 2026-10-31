# Stamps, studies and pairs

Stamps are **local agent bookkeeping**: the stage commands write them on the machine doing agent work, and what
reads them today is the stage commands themselves and `stamp verify`; `pr open` will read them too, to keep that
work in order. They are never checked in CI, and the facts block `pr open` will put in a pull request description is
a record of them, not proof. A person working by hand needs none ([CONTRIBUTING.md](repo:CONTRIBUTING.md)).

A unit of work carries its proof as a log of **stamps**: records, written only by the stage commands, that a step
ended with a result on a commit, covering exact hashes. Stamps are not signed, and the log stays in the task's scratch
folder, never committed. For a function ported from the original the proof is a **study** (the original function
saved, its lines numbered and cut into pieces) and a **pairs file** (each piece joined to the Coney code that does
it). This page defines the formats and what each command checks. The commands are in
[the coney-tools reference](coney-tools-fidelity.md#orig-stages); the verdict a reviewer gives on the pairs is
[Orig review](review/orig.md). The stage commands are `orig study`, `orig compare` and `orig fails-first`; their
first names (`original ...`) still work until the cutover ([Renamed commands](coney-tools.md#renamed)).

## The stamp log {#log}

One stamp is one line of canonical JSON (UTF-8, sorted keys, no spaces, no floats):

```json
{"commit":"<40 hex>","covers":{"cut_sha256":"..","decompile_sha256":".."},"detail":{"statements":9},
 "prev":"<64 hex>","result":"pass","seq":4,"step":"studied","subject":"0x00501000","task":"fight-resume",
 "time":"2026-10-22T14:03:11Z","v":1,"who":{"agent":{"id":"..","model":"..","type":"engineer"},
 "kind":"tool","source":"hook","tool":"coney-tools orig study"}}
```

| Field | Meaning |
| --- | --- |
| `step` | One of `stamps.steps` in [.github/conventions.yml](repo:.github/conventions.yml): `assigned`, `claimed`, `studied`, `fails-first`, `compared`, `checked`, `reviewed`, `pr-opened`, `finished` |
| `subject` | What a repeating step is about: an address (`studied`, `compared`), a test name (`fails-first`), empty for the others |
| `result` | `pass`, `fail`, `none` (no such step needed, with the reason in `detail`) or `keeps` (written only by `orig fails-first --keeps`, for a test that passes before the change too: [Keeps](#keeps)). The list is one vocabulary for every step (`stamps.results` in [.github/conventions.yml](repo:.github/conventions.yml)); the log's check does not tie a result to a step, the commands decide which they write |
| `commit` | The unit worktree's HEAD when the step ended: a record, never compared, because a rebase rewrites it |
| `covers` | Names mapped to hex hashes of what the step covered (a function's decompile, the cut, the pairs file, a Coney body, a test's own code and registration (`test_sha256`), logs, and for the steps that vouch for the whole change `diff_sha256`: [The change hash](#change)). Hashes only |
| `detail` | Counts and names for the PR text and the mod, at most 2 KB |
| `who` | What ran the step, the agent behind it and where that came from ([Who](#who)) |
| `time` | UTC, never earlier than the stamp before |
| `prev` | The SHA-256 of the previous stamp's line (64 zeros for the first) |

A stamp holds hashes, names and counts, never text from the original, a decompile line or a test's output
([LEGAL.md](repo:LEGAL.md#no-game-data)).

The **hash** of a stamp is the SHA-256 of its line without a `sig` key (an old line is hashed as if it had none);
`prev` links each stamp to the one before, so the chain keeps history honest: a stamp cannot be edited, removed or
moved without breaking the link after it, and a failed attempt stays in the log. An earlier format signed some stamps,
and a log written then has the extra key `sig` on its lines (`null`, or `{"signer", "armor"}`): it is still read, left
out of the hash (as it always was) and otherwise ignored, and nothing writes it. A `sig` that is neither `null` nor an
object is refused.

### Layout {#layout}

The log is a folder of segments `0001.jsonl`, `0002.jsonl`, ... A segment is closed before it would pass
`stamps.segment_bytes` (256 KB, under the repository's 512 KB file check), and the first stamp of the next one carries
the last hash of the previous, so the chain crosses files. A stamp is about 700 bytes: a unit with thirty ported
functions makes about 120 stamps, one segment.

| Where | What |
| --- | --- |
| `<scratch>/<task>/unit.json` | The unit record `task start` wrote: branch, worktree, base commit, which task's log it writes to ([Starting a task](issues.md#starting)) |
| `<scratch>/<task>/stamps/` | The log (local; it holds no game data, so `task finish`, planned, is to keep it) |
| `<scratch>/<task>/study/<addr>/` | A function's study: the saved decompile and what was cut from it. Scratch only, never committed |
| `<scratch>/<task>/mapping/<addr>.toml` | The pairs of one function (scratch only, never committed) |
| `<scratch>/<task>/compare/<addr>.txt` | The side-by-side view (holds decompile text: scratch only) |

The task is `--task`, else `CONEY_TASK`, else the branch name. A fix task's unit record names its parent, and the stage
commands given the fix's id write to the parent's folder (`UnitRecord.log_task`, [A fix task](issues.md#starting)).

Only the stage commands append, under a lock, and `append` refuses when the log is not intact. There is deliberately
no command that appends a stamp by hand. The hook dispatcher refuses every Edit or Write under a `stamps/` folder and a
shell write into one, for every session and role ([Which role to start](agents.md#roles)); reading and `stamp verify`
are open.

### Who {#who}

`who.agent` is the agent id, type and model that ran the command, and the Claude Code session it ran in (`session`,
when the hook recorded one: every main session has the id `main`, so the id alone names no one,
`identity.same_agent`). A command cannot take these from the agent itself, which could write anything, so the hook
dispatcher writes them, from the hook input and the model in the agent's transcript, to a file named by a random token
and puts only that token (`CONEY_AGENT_TOKEN`) in front of every `coney-tools` command ([Who runs a
command](agents.md#identity)). `who_from_env` reads the file as the command's first action. The stamp records
`who.source` as `hook` when a valid, unexpired file was found, and `unverified` otherwise, with every field `unknown`,
however the environment was set (a person at a terminal, a PowerShell command outside `bypassPermissions`, an agent
that typed variables or a token of its own). Recording never refuses a command: [`pr verify`](pr.md#verify) requires
`source` to be `hook` on the stamps it relies on when `stamps.require_hook_source` is on. A [run
record](research-evidence.md#run-records) takes its `who` the same way and marks its source the same way; a run itself
is not stamped.

### Verifying a log {#verify}

`coney-tools stamp verify` re-reads the log and reports every problem with the segment and line: a line that is not
whole, not canonical JSON (a hand edit, even a valid one), of an unknown version or step; a `seq` that skips or repeats;
a `prev` that is not the hash of the stamp before; a `time` that goes back; a stamp of another task; a missing segment;
and a `pr-opened` stamp whose `chain_head` is not the stamp before it. `--task T` fails when the log is another task's;
`--require-log` also refuses a missing or empty log. `current(...)` (the question a later stage asks) ignores a pass that
a later stamp of the same step and subject replaced with a fail.

What the chain protects, honestly: without signatures, a log can be rebuilt by whoever can write its files. The protection
is that only `coney-tools` appends to it, that the hooks refuse an agent's direct write to a `stamps/` folder, and that
the chain catches an edit made any other way (by hand, a stray tool) and a truncated segment. A stamp's `time` is written
by the tool and attested by nothing else; `pr verify` bounds it by the clock and by the author date of its
commit while the commit exists ([pr verify](pr.md#verify)). The design guards against
agents and mistakes, not against the person at the machine.

## The change hash {#change}

`checked`, `reviewed` and `pr-opened` say that the whole change is good, so they are bound to the **change**, not to a
commit: `covers.diff_sha256`. `pr open` rebases the branch, which rewrites every commit id, and main moves all day, so a
stamp bound to a commit would be stale the moment it is used. The change hash is the same after a clean rebase and
different after any edit of the branch's own lines. `commit` stays in each stamp as a record. The code is
`coney_tools/pr_diff.py`: `change_of(root, target, tip)` finds the merge base with `target` and returns the base, the
tip, the changed paths and the hash; `change_sha256(root, base, tip)` takes the base. A stage asks "is my stamp still
good?" with `Report.current(step, subject, {stamps.CHANGE: <the hash now>})`.

The hash is the SHA-256 of one text: a version line (`coney-change 1`), then for every changed path in git's path order
(the byte order of the path, from `git diff --raw -z`), with renames off (a moved file is a deletion and an addition):

* `file <path>`, `status <A|M|D|T>` (`T`: a change of file type, such as a file made a symlink) and
  `mode <old> <new>`;
* for a text file the removed and added lines of `git diff -U0`, in order, with git's "no newline at end of file"
  marker, and no hunk header, `index` line or `---`/`+++` line, so neither line numbers (which move when main edits the
  same file elsewhere) nor the pre-image blob id (which changes with main) take part;
* for a binary file `binary <SHA-256 of the file at the tip>`, or `binary deleted`. A file is binary when its first
  8000 bytes hold a NUL byte: the new file's bytes, or the old file's for a deletion (a deleted file has no new side).
  `.gitattributes` (`-diff`, `binary`, `diff=driver`) is ignored.

Whitespace counts: a tab, a CRLF or a missing final newline is a changed line. The same commits give the same hash on
every machine: the diff runs as `git diff --no-renames --no-color --no-ext-diff --no-textconv --no-prefix
--no-relative --no-color-moved --full-index --no-abbrev --text --diff-algorithm=myers --indent-heuristic
--inter-hunk-context=0 --submodule=short --ignore-submodules=none -O<an empty file>` with `-U0` for the patch (`--raw
-z` for the list). `--no-abbrev` is what gives the `--raw` list full object ids, whatever the user's `core.abbrev`
says: `--full-index` only fills the `index` line of a patch, which is not hashed. The call runs
under `-c` overrides of `core.quotepath`, `core.autocrlf`, `color.ui` and the `diff.*` settings that change the
output, and without `GIT_DIFF_OPTS` and `GIT_EXTERNAL_DIFF`. The base is the merge base of the branch with its
target, so the target (`origin/main`) must be at least as new as the branch's own base: fetch first, or main's
changes since are counted as the branch's.

A rebase that touches only other files, or other lines of the same files, keeps the hash. A conflict resolved by hand, or
an upstream change that makes part of the branch a no-op, changes it, and everything bound to it is stale: the change is
another one. The accepted cost is that a clean rebase can combine with upstream into a build nobody compiled with the
`ci` preset; CI builds the merge result anyway.

## The build record {#build-record}

`task check` and `pr verify` must know that the engine built and the whole suite ran with the
disc, without parsing printed text. Every run of `coney-tools build` that configured, built or tested therefore ends
by writing one record to `<scratch>/<task>/build-runs/<sha256>.json` (the task is `CONEY_TASK`, else the branch; no
record outside a git checkout). The file is canonical JSON, like a stamp's line, and is named by the SHA-256 of its
bytes, so the `checked`
stamp covers it by `build_record_sha256` and an edited record no longer reads. The code is `coney_tools/build_record.py`
(`make`, `write`, `read`, `problems`).

| Field | Meaning |
| --- | --- |
| `record_version` | The format, `1` |
| `task`, `agent` | Whose run it was |
| `tip`, `dirty` | The commit built, and whether the worktree had uncommitted changes, both read before the run starts (a commit made during the run is not the one built) |
| `preset`, `filters`, `command_line` | The CMake preset, the test filters (`-R`, `-L`; any makes it a partial run) and the arguments |
| `exit_code`, `ran` | The run's exit code, and which of `configure`, `build`, `test` started |
| `tests` | `total`, `passed`, `failed`, `skipped` and `skipped_disc` (the skipped tests that need the disc: all skips when the run had none, else those `ctest -N -L disc` lists); null when no test run ended |
| `tool_revision` | The commit of the coney-tools code that ran the build (null for a copy outside a git checkout) |
| `disc` | Where the disc came from (`CONEY_DISC`, a config file), null when there was none; the ISO is not hashed (slow, and a hash proves nothing about what the player owns) |
| `log`, `log_sha256` | The run's log in scratch and its hash |
| `started`, `ended` | Times, UTC |

`problems(record, tip=, preset=)` returns every reason a record is not one whole, clean, full-suite run of the preset
on the tip with the disc: another preset or version, filters, a non-zero exit, another commit, uncommitted changes, no
test run, no tests, failed tests, skipped disc tests, a log that no longer matches its hash. A skipped test counts as
a disc test when `ctest -N -L disc` lists it; if that listing fails the run fails (every skip would otherwise pass as a
non-disc skip). `task check` refuses to count a record that has one. The record holds no game data and no test output,
only counts. Only `coney-tools build` writes it, the protect hook refuses an agent's Edit, Write or shell write into
a `build-runs` folder as it does for `stamps`, and the newest 20 records are kept (the logs' rule), except a record
that a stamp in any task's live log covers, which is never removed. None of this stops the person at the machine, as
with the stamps.

## Study {#study}

`coney-tools orig study <addr>` reads one original function and saves what the comparison needs.

* **How it reaches Ghidra.** It talks HTTP to the ghidra-mcp server (`127.0.0.1:8090`, `--url` to change it) with the
  same client `orig index` uses, reading endpoints only: the program (it must be `SLUS_212.15`), the function (its
  entry point must be the address), the decompile, Ghidra's hash of the function's bytes, the callers, the callees and
  the globals. It does not go through the MCP bridge, which exists only inside a Claude Code session. See
  [Ghidra](ghidra.md#start-and-stop-the-server) for the server.
* **When Ghidra is down** (connection refused, no program open) the command exits with 3 and writes nothing, not a
  file and not a stamp. `--recut` re-cuts the saved decompile without Ghidra. It checks the saved `decompile.c` and
  the saved callers, callees and globals, and the function's name, program and signature, against the newest `studied`
  stamp of the address in the verified log, not against `study.json` (which sits beside them and could be edited with
  them), and refuses when there is no such stamp or any hash differs (a stamp made before `identity_sha256` was added
  is refused as predating the identity check, and is made again with `orig study`): an edited text is fetched
  from Ghidra again with `orig study`, never stamped.
* **What is saved**, in `study/<addr>/` of the scratch folder: `decompile.c` (newlines normalised; its hash is the
  stamp's), `numbered.txt` (a reading copy: each statement's number and its piece), `callers.json`, `callees.json`,
  `globals.json` and `study.json` (hashes and the cut). The decompile is text from the game: it stays in scratch and is
  never committed ([LEGAL.md](repo:LEGAL.md#no-game-data)). A test against a running server, `test_orig_study_live`, is
  left out of a normal run and runs with `pytest -m ghidra`.
* **The stamp** `studied` (subject: the address) covers the decompile, Ghidra's function hash, the cut and its shape,
  the addresses of the callers, callees and globals, and a hash of the function's name, program and signature
  (`identity_sha256`). A study that changes nothing writes no new stamp, unless the newest one does not cover all of
  it (none at all, or one made before `identity_sha256`): then it writes a stamp and says `unchanged; stamp
  written`.
* **Freshness.** The shared Ghidra project is renamed all the time, so the decompile's text changes while the function
  does not. A saved study is *fresh* when Ghidra's function hash, the cut's shape and the addresses of its callees and
  globals are unchanged and the text's *skeleton* is the same: comments and strings blanked and each name replaced by
  the number of its first appearance. A rename in Ghidra is one-to-one, so it keeps the skeleton (*renamed*); swapped
  operands, another variable, global or callee in a place, an operator, a number or the structure do not (*stale*).
  `orig verify <addr>` says which. Any restudy that is not *unchanged*, a rename included, makes the pairs file
  stale, since its header binds the decompile's hash: `orig compare --carry` brings it forward, and the study
  says so. A study refuses to run on a log that is not intact,
  before it saves anything.

### Lines and pieces {#pieces}

A **line** is a logical statement of the function body, numbered from 1 in text order: a condition, an assignment, a
call, a return, a label. Not numbered: the comment and signature before the body, the block of local declarations,
braces, a bare `else`, `do {`. A long condition over several physical lines is one line, `if (c) goto L;` is one line,
`else if (c)` is one line (the `else` dropped), and `case N:`, `default:` and `LAB_...:` are lines, so no code is
unnumbered.

A **piece** is one anchor statement (a branch, loop, call, field write, constant or return) with the lines that only feed
it. The cut is a function of the text alone, so every run and every machine cuts the same way. Each statement gets a kind
from its shape:

| Kind | Statement |
| --- | --- |
| `branch` | `if`, `else if`, `switch` |
| `loop` | `while`, `for`, the closing `while` of a `do` |
| `return` | `return ...;` |
| `call` | a call, alone or assigned to a local |
| `field` | an assignment to anything but a local or parameter: a dereference, a member, an array element, a global |
| `constant` | a local assigned a literal (`x = 0x18;`, `b = false;`) |
| `compute` | any other local assignment |
| `jump`, `label` | `goto`, `break`, `continue`; `case`, `default`, `LAB_..:` |

The anchors start pieces; the rest attach, in this order:

1. **Early exit.** A `return` that is the only statement of its block, directly under a branch, belongs to that branch:
   a guard clause is one piece.
2. **Result feeder.** A call that assigns a local, read by exactly one later branch, return or field write before the
   local is assigned again or its block ends, belongs to that reader. A result read twice, or never, is its own piece.
3. **Compute feeder.** A `compute` belongs to the first later anchor that reads it, else to the next statement's piece,
   else to the previous.
4. **Labels** belong to the next statement's piece (a case label belongs to the code it selects), and `goto`, `break`
   and `continue` to the previous one. Where that would loop (`case 3: goto L;`) the group is one branch piece.

A group with no anchor (labels and jumps pointing at each other, or a body of one statement that nothing attaches to) is
one `branch` piece, a transfer of control. Several statements that start on one physical line (`Foo(); return 2;`) all
show their numbers in `numbered.txt` and in the side-by-side view.

Pieces are numbered in order of their first line, and list their lines (`3-5`, or `1,8-9` when a feeder is not
adjacent). A switch with many constant cases is many pieces; they are adjacent, so a pair may merge them with a reason.
An invented example in Ghidra's style, with the pieces the command cuts (the gutter is `numbered.txt`'s):

```text
  1  iVar1 = *(int *)(param_1 + 0x1c);    P1  compute feeding the branch
  2  if (param_2 < iVar1) {               P1  branch
  3    return 0;                          P1  early exit
  4  lVar2 = Widget_GetOwner(param_1);    P2  call read once, by the branch
  5  if (lVar2 == 0) {                    P2
  6    return 1;                          P2
  7  Widget_PlaySound(param_1,0x2a);      P3  call
  8  DAT_00501234 = DAT_00501234 + 1;     P4  global write
  9  return 2;                            P5  return
```

## Pairs {#pairs}

A **pair** joins a piece of the original to the Coney code that does it. One file per function, `mapping/<addr>.toml`,
usually 10 to 40 pairs. It holds line numbers, Coney locations and notes, no text from the decompile, but it stays in
the task's scratch folder and is never committed: the lasting part, each port's stand-ins and not-ported pieces with
their issue numbers, goes into the function database (`research/functions/`). The `[function]` table binds the file to
its study and carries the cut as numbers (each piece's lines), so coverage can be checked without Ghidra or the disc.

```toml
version = 1

[function]
address = "0x00501000"
name = "Widget_Update"
program = "SLUS_212.15"
decompile_sha256 = "..."
ghidra_function_hash = "..."
shape_sha256 = "..."
statements = 9
pieces = ["1-3", "4-6", "7", "8", "9"]
cut_sha256 = "..."
coney = "Widget::update(int)"

[[pair]]
lines = "1-3"
kind = "branch"            # branch loop call field constant return
coney = "src/world/widget.cpp:41"
relation = "same"          # same | stand-in | not-ported
note = "+0x1c is the ready frame"
issue = ""                 # not-ported: "#812", the issue that ports it
merge_reason = ""          # needed when lines cover more than one piece
```

| Relation | Needs |
| --- | --- |
| `same` | `coney`: a `path:line` (or `path:first-last`) under `src/` that exists |
| `stand-in` | `coney`, a `note` that says why, and a comment containing `stand-in` or `Coney's choice` (`stamps.pairs.stand_in_markers`) on that line or the three above it ([Stand-ins](testing.md#stand-ins)) |
| `not-ported` | `issue = "#N"`, the issue that ports it; `coney` may be empty |

## Compare {#compare}

`coney-tools orig compare <addr>` is run again and again while the pairs are filled in:

1. The first run needs a current `studied` stamp (one that covers the saved study's name, program and signature too,
   so a study from before `identity_sha256` is studied again) and a Coney function carrying `@orig <addr>` (several:
   `--coney-function KEY`). It writes the pairs file with one blank pair per piece and the side-by-side view, and stops.
2. Each later run shows the saved decompile beside the Coney function (the pair number in the gutter of both sides) and
   checks, naming each pair or line:
    * **coverage**: every numbered line is in exactly one pair; a pair holds whole pieces (never split one, never skip a
      line); several pieces in one pair are adjacent and have a `merge_reason`;
    * **each pair complete** by its relation, as in the table above, with real Coney locations;
    * **the Coney file is committed** (the stamp is bound to the commit), and
    * **`orig check-copy` is clean** on that file ([orig](coney-tools-fidelity.md#orig)): automatic names, `undefined`
      types, raw byte-offset reads and `goto` are refused.
3. When all of it holds it appends the `compared` stamp, covering the decompile, Ghidra's function hash, the cut, the
   pairs file's hash (line endings normalised) and the Coney function's body hash ([its code without comments or white
   space](coney-tools-fidelity.md#body-hash)), and a count of the relations. A second run that changes nothing writes
   no stamp.

### Two Coney functions, one address {#shared-address}

When two Coney functions port parts of one original function
(`FollowCamera::update` and `FollowCamera::resetWhenLost` both port `0x0012ae58`), both carry `@orig` with that address
and share its study and pairs file, and each is compared with `--coney-function KEY`. A `compared` stamp is read **by
address and Coney function**: the stamp names its function in `detail.coney` (the function's key), and while another
function also carries the address, `compare`, `task review` and `pr verify` look only at the stamps that name this
function, so comparing the second never makes the first stale, and a function with no stamp of its own is "never
compared" even though its sibling has one. A function that is alone at its address reads every stamp of the address, as
before: a stamp made before the function was named, or under a name since changed, still covers the only function that
cites the address. A stamp that names no function covers neither of two functions that share an address (compare each
again). The code is `Report.latest(..., function=, cited_by=)` and `Report.current(...)` in `coney_tools/stamps.py`,
and `orig_review.citing` lists the functions at each address. A refusal for such a function says to run `orig compare
<addr> --coney-function KEY`.

A run that finds something wrong prints it and writes **no stamp**: this is an edit-and-rerun loop, and the log records
verdicts, not drafts. The same holds for every environment fault (Ghidra down, a git error, a build that does not
compile). Only the steps whose result is a verdict write a `fail` stamp: `fails-first`, `checked`, `reviewed` and a
`compared` that a reviewer later rejects.

The pairs file is bound to its study: its header carries the program and the decompile, function, shape and cut
hashes, and `compare` refuses a file whose header differs from the saved study's in any of them. A Coney location is
read as git stores it (forward slashes, `./` and `//` collapsed, the case of the tracked name), must be a tracked file
under `src/`, and that same name is the one checked for uncommitted changes and for copy-check findings.

If the study changed, the file is bound to the old one and `compare` refuses; `--carry` rebuilds the file from the new
study (the old one is kept as `<addr>.toml.old`). With an unchanged shape every pair whose lines are still whole
pieces is kept; with another shape the line numbers mean other statements, so nothing is kept and the dropped pairs
are listed.

The reviewer ([Orig review](review/orig.md)) answers each pair: condition, value, effect and order the same, or the
stand-in true. The tool part of that check is that the decompile still matches Ghidra (`orig verify`), the cut and
the pairs file hash agree with the stamp, and the Coney body is unchanged. A comment or white space edit is not a
change of the body; a stamp made when the hash still covered them is read by rehashing the function at the stamp's
commit, so it keeps covering an unchanged function ([The body hash](coney-tools-fidelity.md#body-hash)).

## Fails first {#fails-first}

`coney-tools orig fails-first --test NAME --test-commit SHA [--fix-commit SHA] --agent ID` proves a test detects the
problem. The test is written from the study and committed first; it must fail; the fix is committed; it must pass. The
command checks out the test commit in the unit's worktree and runs `coney-tools build --test -R <names>` (the only way
C++ tests run), does the same on the fix commit (default HEAD), then puts the branch back.

* The worktree must be clean. The test commit must be an ancestor of the fix commit, on the branch, and change at least
  one file under `tests/`. The test commit and the fix commit are two commits: a test committed with the change has
  no before, so a test commit that is the fix commit is refused.
* **The test must not change** between the two commits: each test file the test commit changes reads the same in
  both as the compiler reads it (`orig_review.code_text`, which [the body hash](coney-tools-fidelity.md#body-hash) uses:
  comments and layout aside, every literal and raw string exactly as written), so a commit that adds a cite comment to
  a test is not a change. A test that had to change to pass is a decision point for the coordinator: the run is
  refused, and says to ask through `QUESTION.md` and which command records a yes ([A changed test](#changed-test)).
* **Stubs.** A test for a function that is new cannot compile on a test-only commit, so the test commit may also
  hold the minimal stubs that let it compile. A stub does what the code did before the change and never the new
  behaviour: for a test that must fail first it returns a default or an empty result; for a `keeps` test it is the old
  behaviour (the lookup's stub does the scan it replaces; [Keeps](#keeps)). The stamp lists every non-test file the
  commit touches (`detail.stubs`) for the reviewer.
* Each name must be a whole test name, written `TEST_CASE("NAME"` (then `)`, or `,` and tags), in a test file the test
  commit adds or changes; part of a longer name, a `SECTION` name or any other quoted string is refused. A name split
  into adjacent literals (`TEST_CASE("A " "B")`) is joined and matches as `A B`, whatever comment (`/* ... */` or
  `// ...`) sits between them; escapes in a literal (`\"`, `\\`, `\n`, `\x41`, `\101`, `\u00e9`) are read as C++ reads
  them, so `TEST_CASE("say \"hi\"")` matches `--test 'say "hi"'`, the name ctest registers. A comment is dropped
  before the match, wherever it is (a `TEST_CASE` inside a comment is not declared, and a `,` or `)` inside a comment
  does not end a name), so `TEST_CASE("Foo" // see, below` followed by a macro on the next line is a macro-built name
  and does not match `Foo`. A name built by a macro does not match. Not handled, so refused as not found: C++23's
  delimited escapes (`\x{41}`, `\o{101}`, `\N{...}`), the GNU `\e` and a name written as a raw string (`R"(...)"`).
  A `\x` or octal escape above 0x7F is read as the code point, not as the byte ctest would list, so such a name
  may be refused. The branch to come back to is written to
  `fails-first/restore.json` in the unit's scratch folder before the first checkout and removed after the last: if a
  run is killed, the next one refuses while the worktree is detached and says `git checkout <branch>`; a restore that
  fails is reported as `COULD NOT RESTORE` with that command.
* **Verdict.** `pass`: built at both commits, the test failed on the first and passed on the second. Which tests
  failed is read from the build's log, the file whose path the build prints first (`log: ...`): ctest's own list of
  failures there is one name a line, so a name with a comma in it stays whole, and a failed list the build cut short
  loses none. The one-line `failed: a, b, ...` summary is never read for it, because it joins names with commas and is
  cut at a width, even in the middle of a name. When the log cannot be read, or its list holds fewer names than ctest
  counted, the run does not say which tests failed: each name is then run alone, and a name that did not pass alone
  failed. A test that passes on the test commit detects nothing, and one that still fails on the fix does not
  prove it: both write a `fail` stamp. A build that does not compile, a build left queued for a slot, or names
  that match no test are errors with no stamp.
* One stamp per test name (`--test` is repeatable; they are written together or not at all), covering both commits,
  the test itself (`test_sha256`: [What a stamp binds](#fails-first-binds)) and the hashes of both runs' logs (kept in
  scratch as `red-` and `green-<test commit>-<fix commit>.log`).
* **A test added later**, in a review round, gets its own `orig fails-first` run with its own test commit (and the
  stubs it needs); the stamps of the earlier tests stay valid, also when the new test is in the same file.
* **`--keeps`** is the other mode, for a test that cannot fail first ([Keeps](#keeps)).
* A docs or tooling task has nothing to run: `--none REASON` (one line, at most 200 characters, given alone) stamps
  `result: none` with the reason.

### A changed test {#changed-test}

When the coordinator agrees that a test had to change between its commits (the first version asserted the bug, say),
it records the answer, and only it can:

```sh
uv run --project python coney-tools orig fails-first --accept-change "<reason>" --test NAME --test-commit A --fix-commit B
```

It runs nothing, and runs in the unit's worktree like the engineer's own run (from the main checkout it finds no unit
and refuses). It checks the commits and names as a run does, refuses when the test files did not change (comments and
layout aside: there is nothing to decide) and refuses any agent but the coordinator (a person at the machine, with no
hook identity, may record it, as they may run `pr open`). It appends one `fails-first` stamp per name with result
`none`, `covers` the two commits and `detail` `{"decision": "changed-test", "reason", "changed"}` (the test files that
differ); it posts no `/progress`, as no test has run yet. The engineer then runs `orig fails-first` with the same names
and commits: the run finds the decision and its stamps carry `detail.decision_seq`, the decision's `seq`. A decision is
for exactly that pair of commits; a later edit of the test needs another.

[`pr verify`](pr.md#verify), and so `pr open`, accept such a pass when the stamp its `decision_seq` names is a decision
for the same test and commits (with `stamps.require_hook_source`, one the hook vouched the coordinator wrote); a
decision alone is no proof that the test failed first, and it is no `--none` stamp for a task with nothing to test.

### What a stamp binds {#fails-first-binds}

A `fails-first` stamp is bound to **the one test it is about**, not to the files it sits in, so a test added to the same
file in a later commit (a review round often adds one) leaves the earlier stamps valid. `covers.test_sha256` is the
SHA-256 of (`fails_first.test_sha256`):

* the test's name and, for each test file of the stamp (`detail.test_files`) that declares it, the file's path;
* the test's **code**: the `TEST_CASE(...)` macro with its name and tags through the closing brace of its body, without
  comments and white space (the rule of [the body hash](coney-tools-fidelity.md#body-hash), `orig_review.code_text`),
  so clang-format or a reworded comment is no change; a string (a raw string too, whatever it holds), a tag or a
  number is code;
* its **registration**: the lines of the `CMakeLists.txt` files from the test file's folder up to `tests/` that name the
  file (`ai/widget_test.cpp` in `tests/CMakeLists.txt`, or `widget_test.cpp` in `tests/ai/CMakeLists.txt`), comments
  dropped. Another file added to the same list is not this file's line, so it changes nothing.

It is recomputed at the branch's tip by `pr verify` and `task review` through one function, `fails_first.staleness`,
which gives the reason a stamp is stale or nothing when it holds:

| What happens after the stamp | Stamp |
| --- | --- |
| A test is added to the file (or above or below this one), or another test of the file is edited | valid |
| A comment or layout edit, or a rebase that leaves the test's lines alone | valid |
| The test's code or tags are edited, its file is taken out of the list or its line is changed | stale: `its test or its registration changed since (<files>)` |
| The test is renamed or removed | stale, and a renamed test is a new one without a stamp |

What it does not bind: code the test calls (a helper or fixture in the file, a header, the code under test). The
stamp proves the test failed and then passed as written; the build and the reviewer cover the rest.

**Stamps in the old form** carry `covers.tests_sha256`, the files as they were whole, and no `test_sha256`. They are
still read: such a stamp holds while the files are as they were, and also when the files at the stamp's own
`fix_commit` still hash to `tests_sha256` (the stamp was not edited, and the commit still exists) and the test's
`test_sha256` there equals the one now, which is what a file that only grew means. Nothing rewrites an old stamp.

### Keeps: a test that guards a refactor {#keeps}

A test that guards a pure refactor, a performance change, or behaviour Coney already has and the original matches
passes before the change as well as after it, so it can never fail first. `coney-tools orig fails-first --keeps --test
NAME [--test NAME ...] --test-commit SHA [--fix-commit SHA] --agent ID` records that instead:

* It runs the same way (the same checks of the worktree, the commits, the names and the unchanged test files, with the
  same [decision on a changed test](#changed-test); the same restore of the branch), but the test must **pass** on the
  test commit, which holds the state before the change (the test, plus any stub it needs to compile: a test of a new
  lookup compiles against a stub that does the old scan), and pass on the fix commit.
* **Verdict.** `keeps`: built at both commits and the test passed on both. A test that fails on either is a `fail`
  stamp (one that fails on the test commit is an ordinary fails-first test: run it without `--keeps`); the same errors
  with no stamp as above. The stamp covers the same hashes and `detail` as a `pass`, with `red` `passed`, and above
  all the same `test_sha256`: it goes stale by the same rule, [`fails_first.staleness`](#fails-first-binds), as any
  other stamp.
* **The test commit is not the fix commit**, in either mode: the before-run is the test commit, so a test committed
  together with the change would be shown to pass before a change it already holds. `fails-first` refuses it, and
  `task review` and `pr verify` refuse a `keeps` stamp whose `test_commit` equals its `fix_commit`.
* `task review`'s tool part and `pr verify` accept `keeps` for an added test, with the same check that the test is
  unchanged since; a test with no stamp is refused as before. A `keeps` stamp is no proof for a gameplay change that
  adds no test: that still needs a stamp that failed first ([pr verify](pr.md#verify)). A gameplay change whose only
  added test is `keeps` passes the tool part, and whether it really keeps that behaviour is the reviewer's alone: no
  program can tell a refactor from a fix whose test happens to pass before it.
* The stamp holds the test, not the helpers around it: a shared helper or fixture the change edits is not seen by the
  hash, so for a `keeps` test the reviewer also reads what the change edits under `tests/`.
* The review packet lists each test with its stamp result, `pr text` names each as `Kept behaviour` apart from the
  tests that failed first, and the reviewer decides whether the change really keeps that behaviour and whether `PR.md`
  says so ([Tests that keep behaviour](review/task.md#keeps)).

## For the commands that write more stamps {#api}

`task start` (which writes the first two stamps, `assigned` and `claimed`: [Starting a task](issues.md#starting)),
`task check`, `task review`, `pr open` and the hooks, once written, call the same small API in
`coney_tools/stamps.py`:
`append(folder, rules=, task=, step=, subject=, result=, commit=, covers=, detail=, who=)`,
`append_many(folder, rules=, task=, entries=)` (several stamps, all or none),
`verify(folder, rules, require_stamps=, task=)` returning a `Report` (`ok`, `problems`, `head`, `latest(step,
subject)`, `current(step, subject, covers)`) and `who_from_env(tool)`. A `pr-opened` stamp gets `chain_head` filled in by
`append`. `current(...)` is how a later stage asks "does the newest passing stamp cover the hash I just recomputed?".
For a `compared` stamp, `latest` and `current` also take the Coney function (`function=`) and every function citing the
address (`cited_by=`) ([Two Coney functions, one address](#shared-address)).

After a write, `append_many` tells the observers registered with `observe(name, observer)` what it wrote; an observer
that fails is a warning and never undoes the stamp. The command line registers one: a passing stage stamp posts
`/progress <stage>` on the unit's issues, so the claim's idle clock restarts while the work goes on
([`/progress` from the stamps](issues.md#claiming)). It posts as the signed-in account, from inside the command, and
the text is always the claim command built from `claims.stages`, never free text: that includes the reviewer's
`task review --record`, whose passing `reviewed` stamp posts `/progress reviewed`. That post is what frees the unit's
claim from the open-claims limit when the account is a maintainer.

What the three steps bound to the change record (all unsigned, in the local log):

| Step | `covers` | `detail` (at most 2 KB) |
| --- | --- | --- |
| `checked` | `diff_sha256`; `build_record_sha256` (profile `full` only); `checks_sha256`, the SHA-256 of the canonical JSON of the sorted list `detail.checks` | `profile` (`docs`, `tooling` or `full`: the checks of `proof.profiles` in [.github/conventions.yml](repo:.github/conventions.yml), written by `task check`), `rules` (`base`: the profiles and path classes were read from the base's file; `branch`: the base has no `proof` section yet), `checks` (the names that ran, in order), `failed` (the names that failed, on a `fail`), `tests` {total, passed, failed, skipped, skipped_disc} (the build's, which `pr verify` matches against its record), `pytest` {passed, skipped, deselected, and any other kind pytest printed} (its last line, by kind), `pytest_scope` (`whole`, or the list of the `proof.pytest.scoped` classes whose tests ran, such as `["docs_only"]`; a stamp without it ran the whole suite, and `pr verify` refuses one that does not hold the classes of the change's paths: [pytest scope](coney-tools-repo.md#task-check-pytest)), `preset`, `minutes` |
| `reviewed` | `diff_sha256`, `pairs_set_sha256`, `title_sha256`, `paragraph_sha256`, `images_sha256`, `review_sha256` (the recorded verdict, kept as `<scratch>/<task>/reviewed.json`) | `verdict`, `scope`, `kinds`, `functions` (address to a one-letter class; `function_count` when the list would not fit), `pages`, `reviewer` {id, type, model}, `blockers`, `notes`, `not_reviewed`; a text review adds `full_seq` ([Task review](review/task.md#stamp)) |
| `pr-opened` | `tip`, `base`, `diff_sha256`, `title_sha256`, `paragraph_sha256`, `facts_sha256`, `images_sha256`, `chain_head` | `engineer`, `opener`, `kind`, `issues`, `profile`, `rebased_from`, `fix` |

`pr open` appends the `pr-opened` stamp after the last check and before the push. If the newest stamp of the log is
already a `pr-opened` whose `covers` are identical, it is reused and nothing is appended, so a rerun after a failed push
or `gh` call leaves one stamp. The facts block of the description is generated from the log without its `pr-opened`
stamps, so a rerun produces the same text and the same `facts_sha256` ([pr open](pr.md#open)).

The hashes come from one place each, so the stage that writes a stamp and the check that reads it cannot differ:
`pr_diff` for the change, `orig_pairs.set_sha256` for the pairs set (one `<address> <pairs file hash>` line for each
changed function, sorted; `task review` and `pr_proof.pairs_set_sha256` both call it) and `pr_text_file` for the title,
the paragraph and the pictures (`pr_text.hashes_of` calls it).

A `full` Check names a **build record**, `<scratch>/<task>/build-runs/<sha256>.json`, that `coney-tools build` writes
and `task check` reads instead of parsing printed text; the stamp's `build_record_sha256` is the SHA-256 of the file.
Its keys are those of [the build record](#build-record). `pr verify` reads it with `build_record.read` and applies
`build_record.problems`, the rule `task check` applies, with the preset `proof.build.preset` and the commit the
`checked` stamp names (so the log the record's `log` names must still match `log_sha256` when it exists), and
also requires the stamp's test counts.
