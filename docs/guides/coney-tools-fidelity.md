# The coney-tools fidelity and research commands

The commands behind the fidelity audit and the research pages: [`orig`](#orig) indexes the `@orig` tags,
[`original`](#original) studies an original function and compares it with Coney's, [`stamp`](#stamp) verifies a unit's
stamp log, [`research`](#research) keeps the function database and reports the claims a branch changed on the
research pages, and [`lua`](#lua) decompiles the game's compiled scripts, a research aid. The other groups are listed in
[the command line overview](coney-tools.md#groups); `standards`, which marks what is below today's standards and
holds a touched file to them, has [its own page](coney-tools-standards.md).

## orig {#orig}

The fidelity audit compares each function Coney reimplements with the original function at its `@orig` address in
Ghidra ([The @orig tag](conventions.md#comments-and-orig)). The `orig` commands give it its index and its order of
work. They only read: the source, the research pages, the shared Ghidra project through reading endpoints, and the
open issues through `gh`.

```sh
uv run --project python coney-tools orig index --out <folder> [--ghidra auto|live|listing|off] [--url URL]
uv run --project python coney-tools orig queue --out <folder> [--issues FILE] [--label bug] [--show N] [--ghidra ...]
```

`orig index` reads every `@orig` tag in `src/` and writes to `<folder>` (use your scratch folder):

* `index.tsv`: one line per tag: the original's address, name and source file, the Coney function the tag
  documents, its file and line, where the tag sits (`comment` above a function, `body` inside one, `other` on a
  class or file), and the research links that document it (the function's comment, else its declaration's, its
  class's or its file's);
* `untagged.tsv`: the functions defined in the gameplay folders (`paths.gameplay` of `.github/conventions.yml`) that
  have no tag and no Coney-only marker. A tag covers a function through its own comment, or through its declaration's:
  the declaration with the same scope, name and parameter types (any one, when the name is not overloaded), in the
  same file pair, folder or, for a member, anywhere; a file's private helper only through a declaration in its own
  file. Each is a `member`, a `free` function, a file's private `helper` (an anonymous namespace or `static`) or an
  `inline` member defined in its class, and its `exempt` column is 1 when it needs neither ([Coney-only
  code](conventions.md#coney-only)). A marker is `@coney-only <reason>` in the function's comment or its file's
  header comment, or the older wording ("Coney's own", "(no @orig)");
* `research-gaps.md`: the research sections tagged code links that cite no original address, links to a page or
  section that does not exist, tags whose linked pages never cite their address, and tags with no research link;
* `report.md`: the counts, the malformed tags (a line with `@orig 0x` that is not the whole form `@orig 0x<address>
  <Name> (<File>.cpp)`), the tags Ghidra does not bear out, and with a running server `callees.tsv` and its
  table: the original functions the tagged ones call that no tag covers (game code only; the libraries the original
  links in are left out, by `docs/progress/totals.toml`), most-called first.

The Ghidra check (`--ghidra`, default `auto`) uses a running ghidra-mcp ([Ghidra + ghidra-mcp](ghidra.md)) when one
answers on `--url`, else the committed listing `docs/progress/ghidra-functions.tsv`, which is as current as its last
`progress ghidra` export and gives no callees. Each tag gets one of: `no-function` (Ghidra has no function there),
`not-a-start` (the address is inside another function), `names-differ` (they share fewer than half the shorter
name's words: the tag may name the wrong function), `renamed` (half or more, usually a rename in Ghidra since the tag was
written) or `unnamed` (Ghidra still has its default `FUN_` name). Names agree when they are equal without case and
punctuation, so `DVDWadIndex::Find` matches `DVDWadIndex_Find`, and a binding's `lua_` wrapper prefix is ignored. A
full live run asks the server about each of the 2,400 tagged functions twice (the function, its callees), one request
at a time; on the development machine it took about 11 seconds.

`orig queue` writes the same files and the audit's queue, `queue.md` (a table per `src/` directory, then the first
`--show` entries) and `queue.tsv` (every entry). An entry is an original function with each Coney place that tags it,
or an untagged function; its score adds up its reasons (`orig_queue.WEIGHTS`):

* an open issue with `--label` (default `bug`) that names it (its address, its original or Coney name), its file or
  a research section it links, weighted by the issue's priority label; the area labels of the issues add at most
  4 points between them;
* a tag Ghidra does not bear out, as above;
* research that grades it only `inferred` or `speculative` anywhere, cites it with no level, or does not cite it;
* a linked section that cites no address, or linked pages that lack its address;
* for untagged code, its kind and length.

`--issues FILE` reads a saved `gh issue list --json number,title,body,labels,comments` instead of asking GitHub. The
order is a starting point for the audit team, which re-ranks as it learns. The files hold addresses, names, paths and
issue numbers, never the original's code; they change with every pull request that touches `src/`, so they are
regenerated when needed and never committed.

```sh
uv run --project python coney-tools orig check [--base origin/main]
```

Refuses a function that a change adds to a gameplay folder with neither `@orig` nor `@coney-only <reason>`, and a
folder of `src/` that is in neither `paths.gameplay` nor `paths.coney_only` ([Coney-only code](conventions.md#coney-only)).
The exempt functions pass: a member defined in its class whose body is one statement (an accessor, a setter, a
forwarder) and a constructor or destructor with an empty body. A file's private helpers are not exempt. Only what
the change adds counts: a function untagged now that was not untagged, under the same scope, name and parameter types
in the same folder (a file's private helper: in the same file), where the branch left `--base`. So editing an old
untagged function, moving it to another file of its folder, or renaming its file with `git mv` (to any folder)
passes; removing a tag, adding an untagged overload, changing an untagged function's parameter types, or growing an
exempt accessor into more than one statement fails.
The older wording ("Coney's own", "(no @orig)") marks only functions that had it at `--base`; a new function needs
`@coney-only <reason>`. The functions that said neither before the check existed are the audit's backlog in
`untagged.tsv`. Each finding names the file, line and function and what to add; exit code 1 when there is one.
`check changed` runs it as its `orig` step and CI's `python` job runs it with `--base HEAD^1`.

```sh
uv run --project python coney-tools orig copy-check [--base origin/main] [--all]
```

This check refuses decompiler fingerprints in the C++ lines a change adds under `src/`, since HEAD left `--base`,
committed or not, with untracked files counted whole ([Porting, not transcribing](conventions.md#porting)). With
`--all` it checks every line of `src/`. It reads only the code part of each line: comments, string and character
literals and raw strings are blanked first, so a shader's `in_pos0` or a comment that cites `FUN_00123456` passes.
It finds these fingerprints:

* Ghidra's automatic names: `iVar1`, `puVar3`, `param_1`, `local_1c`, `auStack_20`, `DAT_`, `FUN_`, `LAB_`,
  `unaff_`, `extraout_`, and `in_` followed by a MIPS register;
* `undefined` and `undefined1` to `undefined8`;
* a raw read at a byte offset, `*(T *)(x + 0x40)` or `*reinterpret_cast<T*>(x + 0x40)`;
* `goto`.

A line with `// copy-check: allow <reason>` passes. Each finding names the file, the line, the text and what to do,
and the exit code is 1 when there is one. `check changed` runs it in its `orig` step, and CI's `python` job runs it
with `--base HEAD^1`. On 2026-10-21 `--all` found none in `src/`. The check cannot see a transcription's shape; the
reviewer agent judges that.

```sh
uv run --project python coney-tools orig review-stamp (--branch B | --worktree WT) [--base origin/main] [--ledger F]
uv run --project python coney-tools orig review-stamp (--folder src/x/ | --address 0x...)... [--ref HEAD] [--every]
uv run --project python coney-tools orig check-review --body PR.md [--head B] [--base B] [--cwd DIR] [--ledger F]
uv run --project python coney-tools orig record [FINDINGS.tsv ...] [--ref COMMIT] [--ledger F]
```

These commands feed the orig review, which compares `@orig` functions with the original
([Orig review](review/orig.md), the [reviewer agent](agents.md#reviewer)). Each one works on **units**. A
unit is a function definition with a body that a tag covers: its own tag, one in its body, or its declaration's.
A unit has a key (`Brain::think(int)`, with the file in front for a file's private helper) and a
[hash of its body](#body-hash): its code, without comments or white space. The source is read from commits, not from
the working tree.

* `review-stamp` prints JSON.
    * With `--branch` or `--worktree` (pr mode) it gives the branch's `tip`, the `base` it left, and the units that
      are new at the tip or whose body or addresses changed.
    * With `--folder` or `--address` (sweep mode) it gives the units at `--ref` in scope that the ledger has not
      seen at their current body; `--every` lists all of them.
* `check-review` is the release gate. A branch that changes no unit passes. Otherwise the passing verdict
  (`pull_requests.orig_review.verdict_file`, `ORIG-REVIEW.json`) next to the description must cover every changed
  unit at its current body, as [the verdict](review/orig.md#verdict) says. A rebase that leaves those bodies as
  they were keeps the verdict.
    * The branch is found in this order: `--head`, then the branch checked out in `--cwd`, then the description's
      folder under the workspace's scratch.
    * The exit code is 0 for a pass (which is recorded in the ledger), 1 for a refusal with the reasons, and 2 when
      the branch cannot be found.
    * The release step calls `orig_review.gate` (or `check_verdict_file`, which writes nothing).
* `record` adds the rows of sweep `FINDINGS.tsv` files ([Sweep findings](review/orig.md#findings)) to the ledger.
  Each row is matched by address and Coney name to the units at `--ref`, the commit the sweep read; rows that match
  nothing are printed. It then prints how many units at HEAD are unreviewed, stale, or in each class.

The ledger is `pull_requests.orig_review.ledger` under the workspace's scratch folder (`orig-review/ledger.jsonl`).
It is one JSON object per review and is never committed. `orig queue` reads it:

* an address whose units were all reviewed `clean` or `diff-deliberate-ok` at their current bodies leaves the queue;
* an unreviewed one gains 3 points, and a stale one 5;
* a review finding adds its class's weight, from `unclear` (6) to `diff-bug` (40).

### The body hash {#body-hash}

A unit's `body_sha256` is the SHA-256 of its definition's **code**, which is what decides that a function changed
(`review-stamp`, `check-review`, the ledger, a `compared` stamp's `coney_body_sha256`, `task review` and `pr verify`
all read this one hash, `orig_review.code_text` and `_body_hash`).

* **Left out:** every comment (`//` to the end of the line, also over a trailing backslash, and `/* ... */` across any
  number of lines) and all white space: indentation, trailing blanks, line breaks, the spacing around an operator.
  Repointing a research link in a comment, rewording one, or re-wrapping a line is not a change of the function, and
  neither `task review` nor `pr verify` asks for a new `original compare` for it.
* **Kept:** every token. A string, a raw string and a character literal are read whole before any comment is looked
  for, so a `//` or `/*` inside one is code and editing it changes the hash; so does a space inside a literal. A
  single space stays where its absence would join two tokens (`return x`, `a + +b` against `a ++b`), because there
  it is code.
* **Not a comment:** commenting code out removes tokens, so it changes the hash; the code is gone from the function.

**Stamps and records made under the old hash** (the lines of the definition, trailing blanks dropped) are read by
rehashing, not migrated, so none is invalidated when the hash changes and no stamp or ledger line is rewritten:

* A `compared` stamp whose `coney_body_sha256` is not the hash now is checked against the function's code at the
  stamp's own `commit`: it stands when that hash is the old-style hash of the function at that commit and the new-style
  hash of the function there equals the one now. So an unchanged function keeps its stamp, a comment edit after the
  stamp keeps it too, and any edit of the code refuses it ("no current `compared` stamp") until `original compare` is
  run again. A stamp whose commit is gone from the repository (a garbage-collected branch) cannot be rehashed and is
  refused the same way; run `original compare` again.
* A ledger entry made under the old hash counts while the function's lines are exactly as they were then. After any
  edit it is stale, as before, and the function is reviewed again.
* An `ORIG-REVIEW.json` verdict is not rehashed: it lives as long as its branch, and one written before this change is
  refused as "reviewed at another body"; the reviewer writes it again from `review-stamp`.

<!-- command-reference:begin orig -->
## Parameters of `orig` {#orig-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `orig index` {#orig-index-usage}

Index every @orig tag in src/, check it against Ghidra, list untagged gameplay functions.

```text
coney-tools orig index --out OUT [--ghidra GHIDRA] [--url URL]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--out OUT` | path | required | the folder to write to (the scratch folder) |
| `--ghidra GHIDRA` | choice | `auto` | check the tags against a running ghidra-mcp (live), the committed listing docs/progress/ghidra-functions.tsv (listing), the first that is there (auto, the default), or not at all. One of `auto`, `live`, `listing`, `off`. |
| `--url URL` | text | `http://127.0.0.1:8090` | the ghidra-mcp server (default: `http://127.0.0.1:8090`) |

### `orig check` {#orig-check-usage}

Refuse a new gameplay function with neither @orig nor @coney-only (check changed and CI run it).

```text
coney-tools orig check [--base BASE]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--base BASE` | text | `origin/main` | what the change is measured from (default: origin/main) |

### `orig copy-check` {#orig-copy-check-usage}

Refuse decompiler fingerprints (iVar1, param_1, undefined4, \*(T \*)(x + 0x10), goto) in changed C++ lines.

```text
coney-tools orig copy-check [--base BASE] [--all]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--base BASE` | text | `origin/main` | what the change is measured from (default: origin/main) |
| `--all` | flag | off | check every line of src/, not only the changed ones |

### `orig queue` {#orig-queue-usage}

Rank the functions to audit first: open bug issues, Ghidra problems, weak research, untagged code.

```text
coney-tools orig queue --out OUT [--ghidra GHIDRA] [--url URL] [--issues ISSUES] [--label LABEL] [--show SHOW]
    [--ledger LEDGER]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--out OUT` | path | required | the folder to write to (the scratch folder) |
| `--ghidra GHIDRA` | choice | `auto` | check the tags against a running ghidra-mcp (live), the committed listing docs/progress/ghidra-functions.tsv (listing), the first that is there (auto, the default), or not at all. One of `auto`, `live`, `listing`, `off`. |
| `--url URL` | text | `http://127.0.0.1:8090` | the ghidra-mcp server (default: `http://127.0.0.1:8090`) |
| `--issues ISSUES` | path | none | read the issues from this saved `gh issue list --json` file |
| `--label LABEL` | text | `bug` | the label of the issues to read (default: bug) |
| `--show SHOW` | integer | `300` | entries written to queue.md (default: 300) |
| `--ledger LEDGER` | path | none | the review ledger (default: pull_requests.orig_review.ledger in the scratch folder) |

### `orig review-stamp` {#orig-review-stamp-usage}

Print, as JSON, the @orig functions an orig review covers (a branch's changes, or a sweep's scope).

Exactly one of `--branch`, `--worktree`, `--folder`, `--address` is required.

```text
coney-tools orig review-stamp (--branch BRANCH | --worktree WORKTREE | --folder FOLDER | --address ADDRESS)
    [--base BASE] [--ref REF] [--every] [--ledger LEDGER]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--branch BRANCH` | text | none | the branch to review (pr mode). Not with `--worktree`, `--folder`, `--address`. |
| `--worktree WORKTREE` | path | none | review the commit checked out in this worktree (pr mode). Not with `--branch`, `--folder`, `--address`. |
| `--folder FOLDER` | text | none | a src/ folder to sweep (repeatable). Not with `--branch`, `--worktree`, `--address`. |
| `--address ADDRESS` | text | none | an @orig address to sweep (repeatable). Not with `--branch`, `--worktree`, `--folder`. |
| `--base BASE` | text | `origin/main` | pr mode: the branch's base (default: origin/main) |
| `--ref REF` | text | `HEAD` | sweep mode: the commit to read (default: HEAD) |
| `--every` | flag | off | sweep mode: list reviewed functions too |
| `--ledger LEDGER` | path | none | the review ledger (default: pull_requests.orig_review.ledger in the scratch folder) |

### `orig check-review` {#orig-check-review-usage}

A branch that changes @orig functions needs a passing ORIG-REVIEW.json for its tip.

```text
coney-tools orig check-review --body BODY [--head HEAD] [--base BASE] [--cwd CWD] [--ledger LEDGER]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--body BODY` | path | required | the description file (PR.md) |
| `--head HEAD` | text | none | the branch (as gh pr create --head names it) |
| `--base BASE` | text | none | the base branch (default: the default branch on origin) |
| `--cwd CWD` | text | none | the folder the release runs in |
| `--ledger LEDGER` | path | none | the review ledger (default: pull_requests.orig_review.ledger in the scratch folder) |

### `orig record` {#orig-record-usage}

Record a sweep's FINDINGS.tsv files in the review ledger and print the ledger's counts.

```text
coney-tools orig record [FINDINGS ...] [--ref REF] [--ledger LEDGER]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[FINDINGS ...]` | path | none | FINDINGS.tsv files |
| `--ref REF` | text | `HEAD` | the commit the findings were made at (default: HEAD) |
| `--ledger LEDGER` | path | none | the review ledger (default: pull_requests.orig_review.ledger in the scratch folder) |
<!-- command-reference:end -->

## original {#original}

The commands of the study-and-compare stage ([Stamps, studies and pairs](stamps.md)). Each writes into the task's
folder in the scratch folder (the task is `--task`, else `CONEY_TASK`, else the branch) and appends a stamp when its
step ends. Exit codes: 0 done, 1 refused (the output says what to fix), 2 it cannot start, 3 no ghidra-mcp answers.

```sh
uv run --project python coney-tools original study ADDR [--task T] [--url URL] [--recut] [--print]
uv run --project python coney-tools original verify ADDR [--task T] [--url URL]
uv run --project python coney-tools original compare ADDR [--task T] [--base REF] [--coney-function KEY] [--carry] [--show]
uv run --project python coney-tools original fails-first --test NAME [--test NAME ...] --test-commit SHA [--fix-commit SHA] [--preset P] --agent ID
uv run --project python coney-tools original fails-first --none REASON
```

* `study` reads the function at ADDR from the running ghidra-mcp (reading endpoints only), saves the decompile, callers,
  callees and globals in scratch (never committed), numbers the statements, cuts the pieces and appends `studied`. It
  writes nothing when Ghidra is down. It ends with the lines of `research where` for the address (the owning page, the
  function database entry, the pages that cite it), or one line saying the lookup is not available.
  [Details](stamps.md#study), [the lookup](research-where.md#study)
* `verify` re-reads the function and says whether the saved study is `fresh`, `renamed` (a one-to-one rename only) or
  `stale`; it exits 1 when stale.
* `compare` writes `mapping/<addr>.toml` with a blank pair per piece on its first run; later runs show the decompile beside
  the Coney function, check the pairs, the committed Coney file and `orig copy-check`, and append `compared` when all of
  it holds. A refusal writes no stamp. [Details](stamps.md#compare)
* `fails-first` runs the named tests through `build --test -R` on the test commit (they must fail) and the fix commit (they
  must pass) and appends a `fails-first` stamp per test. [Details](stamps.md#fails-first)

Every option:

| Command | Option | What it does |
| --- | --- | --- |
| all four | `--task T` | The task whose scratch folder and stamp log are used (default: `CONEY_TASK`, else the branch) |
| `study`, `verify`, `compare` | `ADDR` | The original function's entry point, such as `0x00230d00` (another address is refused, naming the start) |
| `study`, `verify` | `--url URL` | The ghidra-mcp server (default `http://127.0.0.1:8090`) |
| `study` | `--recut` | Cut the saved decompile again without Ghidra; refused unless the saved files and the function's name, program and signature match the newest `studied` stamp |
| `study` | `--print` | Also print the numbered decompile |
| `compare` | `--base REF` | What the copy check measures the Coney change from (default `origin/main`) |
| `compare` | `--coney-function KEY` | Which Coney function to compare, when several carry `@orig ADDR` |
| `compare` | `--carry` | Bring the pairs forward onto a changed or renamed study |
| `compare` | `--show` | Print the side-by-side view |
| `fails-first` | `--test NAME` | A whole test name as ctest lists it, found as `TEST_CASE("NAME"` in the test commit's files (adjacent literals are joined, comments between them dropped, escapes unescaped; a `SECTION` name or any other quoted string is refused; repeatable) |
| `fails-first` | `--test-commit SHA` | The commit that adds the test, and only the stubs it needs to compile |
| `fails-first` | `--fix-commit SHA` | The commit that makes it pass (default `HEAD`) |
| `fails-first` | `--preset P` | The CMake preset the tests are built with (default `dev`) |
| `fails-first` | `--agent ID` | Your id for the build slots, as `build --agent` |
| `fails-first` | `--none REASON` | Run nothing and stamp that the task has no such test (one line, at most 200 characters, not with `--test`) |

<!-- command-reference:begin original -->
## Parameters of `original` {#original-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `original study` {#original-study-usage}

Save an original function's decompile and neighbours from Ghidra, number its lines, cut its pieces.

```text
coney-tools original study ADDRESS [--task TASK] [--url URL] [--recut] [--print]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `ADDRESS` | text | required | the function's address, such as 0x00230d00 |
| `--task TASK` | text | none | the task (default: CONEY_TASK, else the branch) |
| `--url URL` | text | `http://127.0.0.1:8090` | the ghidra-mcp server (default: `http://127.0.0.1:8090`) |
| `--recut` | flag | off | re-cut the saved decompile without Ghidra (the files, name, program and signature must match the newest studied stamp) |
| `--print` | flag | off | also print the numbered decompile |

### `original compare` {#original-compare-usage}

Show the decompile beside the Coney function, check its pairs and stamp `compared`.

```text
coney-tools original compare ADDRESS [--task TASK] [--base BASE] [--coney-function CONEY_FUNCTION] [--carry] [--show]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `ADDRESS` | text | required | the function's address, such as 0x00230d00 |
| `--task TASK` | text | none | the task (default: CONEY_TASK, else the branch) |
| `--base BASE` | text | `origin/main` | what the copy check measures from (default: origin/main) |
| `--coney-function CONEY_FUNCTION` | text | none | the unit key, when several Coney functions carry the address |
| `--carry` | flag | off | bring the pairs forward onto a changed or renamed study |
| `--show` | flag | off | print the side-by-side view |

### `original verify` {#original-verify-usage}

Whether a saved study still matches Ghidra: fresh, renamed or stale.

```text
coney-tools original verify ADDRESS [--task TASK] [--url URL]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `ADDRESS` | text | required | the function's address, such as 0x00230d00 |
| `--task TASK` | text | none | the task (default: CONEY_TASK, else the branch) |
| `--url URL` | text | `http://127.0.0.1:8090` | the ghidra-mcp server (default: `http://127.0.0.1:8090`) |

### `original fails-first` {#original-fails-first-usage}

Run named tests on the test commit and on the fix commit and stamp `fails-first`.

```text
coney-tools original fails-first [--test TEST] [--test-commit TEST_COMMIT] [--fix-commit FIX_COMMIT] [--preset PRESET]
    [--agent AGENT] [--none REASON] [--task TASK]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--test TEST` | text | none | a whole test name, found as TEST_CASE("NAME" in the test commit (repeatable) |
| `--test-commit TEST_COMMIT` | text | none | the commit that adds the test (and only the stubs it needs to compile) |
| `--fix-commit FIX_COMMIT` | text | none | the commit that makes it pass (default: HEAD) |
| `--preset PRESET` | text | `dev` | the CMake preset (default: dev) |
| `--agent AGENT` | text | none | your id for the build slots (as `build --agent`) |
| `--none REASON` | text | none | stamp that there is no fails-first test, with the reason |
| `--task TASK` | text | none | the task (default: CONEY_TASK, else the branch) |
<!-- command-reference:end -->

## stamp {#stamp}

```sh
uv run --project python coney-tools stamp verify [--task T] [--dir LOG] [--require-log]
uv run --project python coney-tools stamp show [--task T] [--dir LOG]
```

`verify` checks a stamp log's chain and every field; stamps are not signed, and the `sig` key of an older log is
ignored ([Stamps](stamps.md#log)). `show` lists the stamps with where each one's identity came from (`hook` or
`unverified`) and exits 1 when the log has errors. Both only read: no command appends a stamp by hand, the stage
commands are the only writers ([Stamps](stamps.md#log)).

| Command | Option | What it does |
| --- | --- | --- |
| both | `--task T` | The task whose log is read (default: `CONEY_TASK`, else the branch); a log of another task fails |
| both | `--dir LOG` | Read this log folder instead of the task's. Independent of `--task`: alone, the log's task is not checked; with `--task T`, the log must be task T's |
| `verify` | `--require-log` | A missing or empty log is an error, not just a start |

<!-- command-reference:begin stamp -->
## Parameters of `stamp` {#stamp-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `stamp verify` {#stamp-verify-usage}

Check the chain and every field of a stamp log.

```text
coney-tools stamp verify [--task TASK] [--dir DIR] [--require-log]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--task TASK` | text | none | the task (default: CONEY_TASK, else the branch) |
| `--dir DIR` | path | none | a stamp log folder (default: the task's, in the scratch folder); with --task, it must be that task's |
| `--require-log` | flag | off | a missing or empty log is an error, not just a start |

### `stamp show` {#stamp-show-usage}

List the stamps of a log, one line each.

```text
coney-tools stamp show [--task TASK] [--dir DIR]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--task TASK` | text | none | the task (default: CONEY_TASK, else the branch) |
| `--dir DIR` | path | none | a stamp log folder (default: the task's, in the scratch folder); with --task, it must be that task's |
<!-- command-reference:end -->

## research {#research}

The research data that tools read directly: the [function database](research-function-database.md), one TOML file per
system in `research/functions/`, the [source files](research-source-files.md) of the executable in
`research/source-files.toml` and the player's words in `research/glossary.yaml`; the research pages made from them
(`render`, `import-table`); the lookup of which page describes an address (`where`); and the claims a branch changed on
the research pages (`claims`). Each file's schema, rules and how to write an entry are on its own page; this section is
the commands.

```sh
uv run --project python coney-tools research render --check
uv run --project python coney-tools research render <folder>
```

`render --check` checks every file of the database and the source files data against the checkout and writes nothing;
CI runs it (`python`), and the docs build runs the same checks before it writes the pages. What it checks is on
[the function database page](research-function-database.md) and [the source files page](research-source-files.md#checks).
It exits with 1 and prints one `error:` line per
problem when one fails; a name Ghidra still has as `FUN_<address>` is a `warning:` line and does not fail. It exits with
2 when it is not run inside a checkout or the arguments are wrong.

With `<folder>` instead of `--check`, it runs the same checks and then writes one page per system,
`<folder>/research/<system>/functions.md`, the find pages `<folder>/research/find/addresses.md` (needs the Ghidra
listing) and `words.md` (needs `research/glossary.yaml`), and the source map with its file tables filled in,
`<folder>/research/source-map.md`: the pages the docs build makes (`mkdocs_hooks.py`; none is committed), so you can
look at one without a build. It writes nothing while the data has a problem. Give a folder or `--check`, not both.
`render --check` also fails on a glossary entry whose page, section or function does not exist.

```sh
uv run --project python coney-tools research where QUERY [--json]
```

Says which research page describes `QUERY`: a hex address, a Ghidra function name, a source file
(`Human/cns/cnsplayertag.cpp`) or a word. For an address it prints the function, its source file, the owning page and
the entry that gives it the page, the function database entry, the `@orig` tags, and the pages that cite it; `--json`
prints the same as one document. It exits with 0 when something matched, 1 when nothing did (and says what a query can
be), and 2 for an empty query, a query like `0xzz`, a malformed data file or a run outside a checkout. The forms, the
output and how the owning page is chosen are on [Finding where a behaviour lives](research-where.md); the project allows
the command without a prompt, as it does `render`.

```sh
uv run --project python coney-tools research import-table PAGE --system NAME [--section ANCHOR] [--replace] [--dry-run]
```

Imports the code-index tables of the research page `PAGE` (a file below `docs/research`) into
`research/functions/NAME.toml`, creating it when it is not there. Which tables and columns it reads, how it merges with
the file and what its report lists are in [the function database page](research-function-database.md).
`--section ANCHOR` limits the import to the tables under that heading, `--replace` takes the page's row for an address
already in the file, and `--dry-run` prints the report and writes nothing. It exits with 0 when at least one function
was read, 1 when the page gave none, and 2 for a page outside `docs/research`, a bad system name or an unknown
`--section`.

The code of `render`, `where` and `import-table` is `research_data.py` (the checks and pages of all the data
together), `source_files.py` (the source files' schema, file format and totals check), `research_find.py` (the file
tables and the find pages), `research_owners.py` (who owns an address, and the `function-entries` rule and the file
half of the `owns` rule it adds to `docs check`), `research_where.py` (the lookup and its text and JSON),
`research_glossary.py` (the glossary's schema and checks), `research_functions.py` (the function database's schema and
file format), `research_functions_check.py` (the checks that read the checkout), `research_render.py` (the pages),
`research_import.py` (the import), `research_links.py` (moving a role's links between folders; pages are read with
`docs_page`, as `docs check` reads them) and `research_cli.py`.

```sh
uv run --project python coney-tools research claims [--base origin/main] [--task TASK] [--stamps DIR] [--require-study] [--json]
```

Lists the graded blocks the branch added or changed on the research pages (`docs/research/`), for the reviewer to work
from ([evidence levels](research-evidence.md#evidence-levels)). The changed lines are those of the working tree since
the branch left `--base`, a new page counting whole. A page moved with git (a rename git finds at the similarity
`research.moved_similarity` of [.github/conventions.yml](repo:.github/conventions.yml) or more, staged so git can pair
it) is not new: it is read as [`docs check`](coney-tools-build.md#docs) reads it, on the lines git's diff of the renamed
pair changes, and its old text is the page at its old path. It reads the pages exactly as
[`docs check`](coney-tools-build.md#docs) does, with the same reader and the same grammar: a block is graded in itself,
a table row in its Evidence column, or by the lead line of its H3-or-deeper heading. For each graded block it prints the
page and line, the level, the addresses and script locators the block cites, what backs it and the tests that cite its section:

```text
docs/research/combat/grabs.md
  L13  confirmed (code)  0x0026c548  studied
  L15  confirmed (runtime)  -  run r-20261023-grab-reach  tests: tests/combat/grab_test.cpp:12
```

What backs a claim: for `confirmed (code)`, a passing `studied` stamp for each cited address in the task's stamp log
(`--task`, default the branch; `--stamps` names another log folder; none found means `none`); for `confirmed (runtime)`,
the run record it cites (`confirmed (runtime), run <id>`, in the block or its lead line), which must exist and meet the
rules of `runs` in [.github/conventions.yml](repo:.github/conventions.yml); for `speculative`, the open question it
links, which must exist when it is on the same page; `inferred` has nothing to look up. A test cites a section with a
line `// Expected: research/<page>.md#<anchor>` in `tests/` or `python/tests/`. The sections of
`research.ungraded_sections` and a lead line itself are not claims. Blocks that cite an address and carry no level are
counted, not listed: `docs check` reports them.

It exits 1 when a claim has an error: a new runtime claim that cites no run record, a runtime claim whose record is
missing or breaks a rule, a `speculative` claim whose question is not on the page, or any file in `research/runs/` that
is not a good record. A runtime claim is a legacy claim, not a new one, when its block shares at least half its words
with a block of the page at `--base` (an edit), or is the same text as a block that left another research page in the
same change (a section cut from one page and pasted into another, whether both pages stay or the old one is deleted),
to the white space and the targets of its links, which a move rewrites. One departed block covers every page it arrives
on: a block pasted into two pages is not new on either. A block edited on the way, a copy that leaves the old page as it
was, and a new block next to a moved one are new. A legacy claim keeps its level with a warning, whether its text names
the PCSX2 version and the method or not. Other warnings: a record the hook did not name (`who.source` is `unverified`)
and `speculative` with no question linked. How a level is written (a bare `inferred`, a refused spelling) is
`docs check`'s. `--require-study` also makes `confirmed (code)` at an address that was not studied in the task an error
(a new or changed claim cites an address studied in its own task, or is written `inferred`); without it `none` is for
the reviewer to check in Ghidra. `--json` prints the same report as JSON.

`research claims` is `research_claims.py` and `research_claims_cli.py`.

<!-- command-reference:begin research -->
## Parameters of `research` {#research-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `research claims` {#research-claims-usage}

List the graded blocks the branch changed on the research pages, with what backs each (for review).

```text
coney-tools research claims [--base BASE] [--task TASK] [--stamps STAMPS] [--require-study] [--json]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--base BASE` | text | `origin/main` | the branch point to compare with (default origin/main) |
| `--task TASK` | text | none | the task whose stamp log backs the code claims (default: CONEY_TASK, else the branch) |
| `--stamps STAMPS` | path | none | a stamp log folder to use instead of the task's |
| `--require-study` | flag | off | an error for a confirmed (code) claim whose addresses were not studied in the task (maintainer decision) |
| `--json` | flag | off | print the report as JSON |

### `research render` {#research-render-usage}

Check the function database, source files and glossary, and write the generated pages to a folder.

```text
coney-tools research render [OUT_DIR] [--check]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[OUT_DIR]` | path | none | a folder to write the pages to (research/\<system\>/functions.md, find/) |
| `--check` | flag | off | write nothing; check the data and exit 1 when it has a problem |

### `research where` {#research-where-usage}

Which page describes an address, function name, source file or player word, and where its logic lives.

```text
coney-tools research where QUERY [--json]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `QUERY` | text | required | 0x\<address\>, a Ghidra function name, a source file (Human/cns/cnsplayertag.cpp) or a word |
| `--json` | flag | off | print the answer as JSON |

### `research import-table` {#research-import-table-usage}

Import a research page's code-index tables into research/functions/\<system\>.toml.

```text
coney-tools research import-table PAGE --system SYSTEM [--section SECTION] [--replace] [--dry-run]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `PAGE` | path | required | the research page, below docs/research |
| `--system SYSTEM` | text | required | the system's file: lower case with hyphens, such as combat |
| `--section SECTION` | text | none | import only the tables under this heading's anchor |
| `--replace` | flag | off | take the page's row for an address the file has with other contents |
| `--dry-run` | flag | off | print the report and write nothing |
<!-- command-reference:end -->

## lua {#lua}

The game's 467 scripts are compiled Lua 4.0 ([Compiled Lua chunks](../research/formats/lua-chunks.md)). `lua
decompile` turns them back into readable Lua for research, with Coney's own decompiler (the `lua4` package under
`python/src/`; how it works and what it gets back: [Lua 4.0 bytecode](../research/formats/lua-bytecode.md)). Its
output is game script text: it is written only outside the repository, and research pages describe what a script does
in our own words ([The game's scripts](research-workflow.md#scripts)).

```sh
uv run --project python coney-tools lua decompile [DISC] --all [--out DIR] [--roundtrip] [--chunks DIR] [--luac PATH]
uv run --project python coney-tools lua decompile [DISC] NAME OUT_DIR [--roundtrip] [--chunks DIR] [--luac PATH]
```

`--all` decompiles every chunk of your disc (`DISC` works as for the `wad` commands) into the **shared folder**,
`lua_dir` in `coney.local.toml` (default `<workspace>/scratch/lua`), so every worktree and agent reads the same copy;
`--out` writes elsewhere. Each chunk becomes `<name>.lua` (the WAD's file name, or the entry's hash in hex when the
name is not known). Beside them it writes:

* `index.md` and `index.json`: per script, every function (named by the global or table field that holds it, with
  its path among the chunk's functions and the line it is defined on), the natives it calls (the names in
  `research/bindings/`), the script functions it calls, and the handlers it registers (calls of a native whose name
  ends in `Handler` or `Callback`, `RegisterUpdate`, `ScheduleFunc*` and `TriggerSphereCfg`) with their constant
  arguments (`python/src/coney_tools/lua_index.py`). Start a level's research here.
* `fingerprints.txt`: a hash of every distinctive line of every script, which the guard below reads.

`NAME OUT_DIR` decompiles one chunk, by file name (`level99.lua`) or hash, into `OUT_DIR`. Both refuse an output
folder inside a checkout. `--chunks DIR` reads the chunks from a folder (`wad extract` or `extract` output) instead of
the disc. The output prints counts only: chunks, functions, the functions that could not be read with the kind of
problem (a pc and an opcode, never a constant). A chunk the loader refuses (another Lua version, cut short) is named
with the reason: `--all` goes on with the rest, still writes the fingerprints and the index for every script it wrote,
and exits 1; one chunk by name is a one-line error.

`--roundtrip` compiles each output with a stock Lua 4.0.1 `luac` and compares the result with the original chunk,
function by function: instructions, constants, parameters, vararg flag, stack size and nested functions. It prints how
many chunks match, the kind of each first difference and the names of the chunks that failed, never the script's
text, and exits 1 when any differs. On the NTSC-U disc all 467 match (about two minutes). The `luac` is `--luac`,
else `luac_program` in `coney.local.toml`, else the one `coney-tools workspace init --fetch lua` builds from Lua's
pinned source in `<workspace>/tools/lua-4.0.1/bin/` ([Fetching the research tools](workspace.md#fetch)).

**The guard.** `.claude/hooks/lua_guard.py` refuses decompiled Lua in the repository: every file the command writes
ends with a marker line (`-- lua4 decompile <hash> <name>`), and text holding that line is refused. So is text in
which three of any five distinctive lines in a row (24 characters or more) have their fingerprint in the shared
folder's `fingerprints.txt`. A line is compared without the markup a paste picks up: quote markers, list bullets and
numbers, inline-code backticks, `--` before commented-out code and a trailing `--` comment are stripped, and each cell
of a table row counts as a line. One quoted line passes, and so do short lines. It runs in pre-commit
(`no-decompiled-lua`, on every text file) and as a Claude Code hook before every Write, Edit, MultiEdit and
NotebookEdit into a checkout ([The hooks](enforcement.md#hooks)). Without the shared folder (CI, or before the first
`--all`) only the marker is checked. A shell command that writes a file is not seen by the hook; pre-commit catches
the commit. Text that never enters a checkout is not checked at all: an issue or pull request body, or a description
file in scratch. Neither is a paraphrase. After a change to the guard's normalising, rerun `--all` so the
fingerprints match it.

**The decompiler's tests** (`python/tests/lua4/`) run on our own Lua source, compiled with a stock `luac` with and
without debug information (`fixtures/<name>.luac`, `<name>.s.luac`), against the decompiles committed beside them
(`<name>.expected.lua`, `<name>.s.expected.lua`). With a `luac` found (`LUA4_LUAC`, else `luac_program` or its
default) they also round-trip each fixture. CI has no `luac`, so it runs neither the round trip nor the check that
the committed chunks match their source: there the expected outputs only catch a change in the output, not a wrong
one, and the round trip is a local check. After a change to the decompiler or a fixture, rewrite them with
`LUA4_REGENERATE=1 uv run --project python pytest python/tests/lua4 -k regenerate`, run the tests again with the
`luac` found and read the diff. The committed chunks have 4-byte sizes, as the game's do, and the index's reader takes
only those. The `luac` that `--fetch lua` builds is a 64-bit program that writes 8-byte sizes (fine for the round
trip, which compares functions, not headers), so the regenerate step refuses it. Regenerate with a 32-bit build named
by `LUA4_LUAC`: on Windows, the same build as `--fetch lua` from the x86 tools (`vcvars32.bat`), with the core
`src/*.c` in a library and `src/luac/*.c` linked against it.

<!-- command-reference:begin lua -->
## Parameters of `lua` {#lua-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `lua decompile` {#lua-decompile-usage}

Decompile one chunk (NAME OUT_DIR) or every chunk (--all) to readable Lua, outside the repository.

```text
coney-tools lua decompile [[DISC] NAME OUT_DIR ...] [--all] [--out DIR] [--chunks DIR] [--luac PATH] [--roundtrip]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[[DISC] NAME OUT_DIR ...]` | text | none | a mounted disc, folder or .iso (default: game_dir), the chunk's file name or hash in hex, the folder to write it to; with --all, only [DISC] |
| `--all` | flag | off | every chunk, into the shared folder (lua_dir) |
| `--out DIR` | path | none | with --all: write here instead of lua_dir |
| `--chunks DIR` | path | none | read chunks from this folder (`wad extract` output), not a disc |
| `--luac PATH` | path | none | the stock Lua 4.0 luac (default: luac_program) |
| `--roundtrip` | flag | off | compile each output with luac and compare it with the chunk; prints counts only; exit 1 on a mismatch |
<!-- command-reference:end -->
