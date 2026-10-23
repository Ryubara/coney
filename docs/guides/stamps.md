# Stamps, studies and pairs

A unit of work carries its proof as a log of **stamps**: records, written only by the stage commands, that a step ended
with a result on a commit, covering exact hashes. For a function ported from the original the proof is a **study** (the
original function saved, its lines numbered and cut into pieces) and a **pairs file** (each piece joined to the Coney
code that does it). This page defines the formats and what each command checks. The commands are in
[the coney-tools reference](coney-tools-fidelity.md#original); the verdict a reviewer gives on the pairs is
[Orig review](review/orig.md). The names of the commands are placeholders for a renaming pass: a name changes in one
parser entry and in this page.

## The stamp log {#log}

One stamp is one line of canonical JSON (UTF-8, sorted keys, no spaces, no floats):

```json
{"commit":"<40 hex>","covers":{"cut_sha256":"..","decompile_sha256":".."},"detail":{"statements":9},
 "prev":"<64 hex>","result":"pass","seq":4,"sig":null,"step":"studied","subject":"0x00501000","task":"fight-resume",
 "time":"2026-10-22T14:03:11Z","v":1,"who":{"agent":{"id":"..","model":"..","type":"engineer"},
 "kind":"tool","source":"hook","tool":"coney-tools original study"}}
```

| Field | Meaning |
| --- | --- |
| `step` | One of `stamps.steps` in [.github/conventions.yml](repo:.github/conventions.yml): `assigned`, `claimed`, `studied`, `fails-first`, `compared`, `checked`, `reviewed`, `pr-opened`, `finished` |
| `subject` | What a repeating step is about: an address (`studied`, `compared`), a test name (`fails-first`), empty for the others |
| `result` | `pass`, `fail` or `none` (`none`: no such step needed, with the reason in `detail`) |
| `commit` | The unit worktree's HEAD when the step ended |
| `covers` | Names mapped to hex hashes of what the step covered (a function's decompile, the cut, the pairs file, a Coney body, a test's files, logs). Hashes only |
| `detail` | Counts and names for the PR text and the mod, at most 2 KB |
| `who` | What ran the step, the agent behind it and where that came from ([Who](#who)) |
| `time` | UTC, never earlier than the stamp before |
| `prev` | The SHA-256 of the previous stamp's body (64 zeros for the first) |
| `sig` | `null`, or `{"signer", "armor"}` ([Signing](#signing)) |

A stamp holds hashes, names and counts, never text from the original, a decompile line or a test's output
([LEGAL.md](repo:LEGAL.md#no-game-data)).

The **body** of a stamp is its canonical JSON without `sig`; its **hash** is the SHA-256 of the body; `prev` links each
stamp to the one before, so the chain keeps history honest: a stamp cannot be edited, removed or moved without breaking
the link after it, and a failed attempt stays in the log.

### Layout {#layout}

The log is a folder of segments `0001.jsonl`, `0002.jsonl`, ... A segment is closed before it would pass
`stamps.segment_bytes` (256 KB, under the repository's 512 KB file check), and the first stamp of the next one carries
the last hash of the previous, so the chain crosses files. A stamp is about 700 bytes: a unit with thirty ported
functions makes about 120 stamps, one segment.

| Where | What |
| --- | --- |
| `<scratch>/<task>/stamps/` | The log while the work is going on (local) |
| `<scratch>/<task>/study/<addr>/` | A function's study: the saved decompile and what was cut from it. Scratch only, never committed |
| `<scratch>/<task>/mapping/<addr>.toml` | The pairs of one function |
| `<scratch>/<task>/compare/<addr>.txt` | The side-by-side view (holds decompile text: scratch only) |
| `research/units/<task>/stamps/` and `research/mappings/<addr>.toml` | Where `pr open` will publish the log and the pairs on the branch, byte for byte |

The task is `--task`, else `CONEY_TASK`, else the branch name. `.gitattributes` keeps the published files at LF
endings: a log is hashed byte for byte.

Only the stage commands append, under a lock, and `append` refuses when the log is not intact. There is deliberately
no command that appends a stamp by hand. The hook dispatcher refuses every Edit or Write under a `stamps/` folder and a
shell write into one, for every session and role, and any naming of the signing key's files or `ssh-keygen -Y sign`
([Which role to start](agents.md#roles)); reading and `stamp verify` are open.

### Who {#who}

`who.agent` is the agent id, type and model that ran the command. A command cannot take these from the agent itself,
which could write anything, so the hook dispatcher writes them, from the hook input and the model in the agent's
transcript, to a file named by a random token and puts only that token (`CONEY_AGENT_TOKEN`) in front of every
`coney-tools` command ([Who runs a command](agents.md#identity)). `who_from_env` reads the file as the command's first
action. The stamp records `who.source` as `hook` when a valid, unexpired file was found, and `unverified` otherwise,
with every field `unknown`, however the environment was set (a person at a terminal, a PowerShell command outside
`bypassPermissions`, an agent that typed variables or a token of its own). Recording never refuses a command: the check
that `source` is `hook` will belong to `pr open` and CI. A [run record](research-workflow.md#run-records) takes its
`who` the same way and marks its source the same way; a run itself is not stamped.

### Verifying a log {#verify}

`coney-tools stamp verify` re-reads the log and reports every problem with the segment and line: a line that is not
whole, not canonical JSON (a hand edit, even a valid one), of an unknown version or step; a `seq` that skips or repeats;
a `prev` that is not the hash of the stamp before; a `time` that goes back; a stamp of another task; a missing segment;
a `pr-opened` seal whose `chain_head` is not the stamp before it; and every signature that does not verify. The chain
detects edits, removals and reordering inside the log; it does not detect that the unsigned tail of an unsealed log was
cut off. That is covered by the signatures on `checked` and `reviewed`, by the seal, and (once `pr open` exists) by it
recomputing every hash it relies on. `stamp verify --task T` fails when the log is another task's; `--release` also
refuses a missing or empty log. `current(...)` (the question a later stage asks) ignores a pass that a later stamp of
the same step and subject replaced with a fail. A stamp's `time` is written by the tool, not attested by anything
else: someone with a key could write back-dated stamps, and a retired key's `valid-before` is checked against that
stamp time. `pr open` is to bound each stamp's time by its commit's date; until it does, treat the time as a record,
not a proof.

## Signing {#signing}

The steps in `stamps.signed` (`checked`, `reviewed`, `pr-opened`) are signed with a dedicated ed25519 key through
`ssh-keygen -Y sign`, namespace `coney-stamp` (`stamps.namespace`), so a signature made for anything else cannot pass
for a stamp. Each maintainer has one key: its private half lives in the SSH agent and is never read by a tool or an
agent, its public half is a line of [.github/stamp-signers](repo:.github/stamp-signers) in the allowed-signers format:

```text
<principal> namespaces="coney-stamp"[,valid-before="YYYYMMDDHHMMSSZ"] ssh-ed25519 AAAA... comment
```

A key is added or retired by pull request, and **a new signer's key merges in its own pull request first**: a release
check reads the allowed-signers file and the `stamps` rules from a trusted commit, not from the change under review (below),
so a pull request cannot vouch for a key it adds. A retired key gets a `valid-before` in UTC with the `Z` suffix; verification
passes the stamp's own time (`-Overify-time`), so what a key signed before its retirement keeps verifying. The signer's
principal is found by `ssh-keygen -Y find-principals`, never read from the stamp.

The setting `stamp_key` of `coney.local.toml` names the public key file; the private half is taken from the agent
(`-U`). Without it a signed step is written unsigned, which is what a contributor's machine does: `stamp verify` warns,
`stamp verify --release` (what `pr open` and CI will run; neither does yet) refuses, and the coordinator re-runs the
check and the review on a machine with a key. A signature covers the stamp's body, and so the whole earlier chain
through `prev`. It sits outside the body, so a stamp can be signed after the fact without breaking the chain.

**The trust anchor.** `coney-tools stamp verify --release --signers-ref <ref>` reads `.github/stamp-signers` and the
`stamps:` section of `.github/conventions.yml` (the namespace, the signed steps, the steps) from the commit `<ref>` with
`git show`. CI will pass the pull request's base (`origin/main`) once it runs the check. Without `--signers-ref` (or
with `--signers FILE`, which cannot be combined with it) the check reads the working tree, which is what a person
verifying their own log wants and what a release check must not do.

Every `ssh-keygen` call is in `coney_tools/stamp_sign.py`, in two functions with one command line each, so the hook
that keeps agents from signing has one pattern to recognise. Tests use throwaway keys in a temporary folder, never a
real one.

## Study {#study}

`coney-tools original study <addr>` reads one original function and saves what the comparison needs.

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
  is refused as predating the identity check, and is made again with `original study`): an edited text is fetched
  from Ghidra again with `original study`, never stamped.
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
  `original verify <addr>` says which. Any restudy that is not *unchanged*, a rename included, makes the pairs file
  stale, since its header binds the decompile's hash: `original compare --carry` brings it forward, and the study
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
usually 10 to 40 pairs. It holds line numbers, Coney locations and notes, no text from the decompile, so it is
committed. The `[function]` table binds the file to its study and carries the cut as numbers (each piece's lines), so
CI will be able to check coverage without Ghidra or the disc.

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

`coney-tools original compare <addr>` is run again and again while the pairs are filled in:

1. The first run needs a current `studied` stamp (one that covers the saved study's name, program and signature too,
   so a study from before `identity_sha256` is studied again) and a Coney function carrying `@orig <addr>` (several:
   `--coney-function KEY`). It writes the pairs file with one blank pair per piece and the side-by-side view, and stops.
2. Each later run shows the saved decompile beside the Coney function (the pair number in the gutter of both sides) and
   checks, naming each pair or line:
    * **coverage**: every numbered line is in exactly one pair; a pair holds whole pieces (never split one, never skip a
      line); several pieces in one pair are adjacent and have a `merge_reason`;
    * **each pair complete** by its relation, as in the table above, with real Coney locations;
    * **the Coney file is committed** (the stamp is bound to the commit), and
    * **`orig copy-check` is clean** on that file ([orig](coney-tools-fidelity.md#orig)): automatic names, `undefined`
      types, raw byte-offset reads and `goto` are refused.
3. When all of it holds it appends the `compared` stamp, covering the decompile, Ghidra's function hash, the cut, the
   pairs file's hash (line endings normalised) and the Coney function's body hash, and a count of the relations. A
   second run that changes nothing writes no stamp.

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
stand-in true. The tool part of that check is that the decompile still matches Ghidra (`original verify`), the cut and
the pairs file hash agree with the stamp, and the Coney body is unchanged.

## Fails first {#fails-first}

`coney-tools original fails-first --test NAME --test-commit SHA [--fix-commit SHA] --agent ID` proves a test detects the
problem. The test is written from the study and committed first; it must fail; the fix is committed; it must pass. The
command checks out the test commit in the unit's worktree and runs `coney-tools build --test -R <names>` (the only way
C++ tests run), does the same on the fix commit (default HEAD), then puts the branch back.

* The worktree must be clean. The test commit must be an ancestor of the fix commit, on the branch, and change at least
  one file under `tests/`.
* **The test must not change** between the two commits: the same blobs under `tests/` in both. A test that had to
  change to pass is a decision point for the coordinator.
* **Stubs.** A test for a function that is new cannot compile on a test-only commit, so the test commit may also
  hold the minimal stubs that let it compile. A stub must not implement the behaviour: it returns a default or an empty
  result. The stamp lists every non-test file the commit touches (`detail.stubs`) for the reviewer.
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
* **Verdict.** `pass`: built at both commits, the test failed on the first and passed on the second. A failed run's
  list of failed tests may be cut off (`... and N more`); a name missing from a cut-off list is not known to pass, so
  that test is run alone. A test that
  passes on the test commit detects nothing, and one that still fails on the fix does not prove it: both write a
  `fail` stamp. A build that does not compile, a build left queued for a slot, or names that match no test are errors
  with no stamp.
* One stamp per test name (`--test` is repeatable; they are written together or not at all), covering both commits,
  the test files and the hashes of both runs' logs (kept in scratch as `red-` and `green-<test commit>-<fix commit>.log`).
* A docs or tooling task has nothing to run: `--none REASON` (one line, at most 200 characters, given alone) stamps
  `result: none` with the reason.

## For the commands that write more stamps {#api}

`task start`, `task check`, `task review`, `pr open` and the hooks, once written, will call the same small API in
`coney_tools/stamps.py`:
`append(folder, rules=, task=, step=, subject=, result=, commit=, covers=, detail=, who=, sign=)`,
`append_many(folder, rules=, task=, entries=, sign=)` (several stamps, all or none),
`verify(folder, rules, signers=, require_signatures=, task=)` returning a `Report` (`ok`, `problems`, `head`, `latest(step,
subject)`, `current(step, subject, covers)`) and `who_from_env(tool)`. Signing is `stamp_sign.signer_for(key,
namespace)`, which gives the `sign` argument or `None` without a key. A `pr-opened` stamp gets `chain_head` filled in by
`append`. `current(...)` is how a later stage asks "does the newest passing stamp cover the hash I just recomputed?".
