# The coney-tools standards commands

The [`standards`](#standards) group marks what is below today's standards and holds a file or a ported function that a
change touches to them. The other groups are listed in [the command line overview](coney-tools.md#groups).

## standards {#standards}

Everything below today's standards is marked, so a tool can tell old from new and a change that touches an old thing
brings it up to date. Three kinds of mark, one per kind of thing, each in the file that already holds that kind:

| What is below the standard | Where it is marked | Rules (what fails) |
| --- | --- | --- |
| A ported function (`@orig`) | `status` in its entry of the [function database](research-function-database.md#entry): `legacy` until it has been compared side by side with the original, then `compared` | The port was never compared |
| A research page | `research/legacy.toml` ([Legacy pages](writing-docs.md#legacy-pages); `docs legacy`) | The [research page standard](writing-docs.md#research-pages) |
| A source file under `src/` | `research/legacy-files.toml` (`standards.legacy_file`) | `comment`: a function with no comment above it and none on its declaration or definition elsewhere in its folder ([the comment rules](conventions.md#comments-and-orig)), except one whose name says everything: a definition of one statement with no branch or lambda (an accessor, a forwarder) that is not public, a constructor, destructor or operator with no statement, and an `override` or `final` of a virtual, whose doc comment is its base declaration's. A public function (declared in a header, every class around it open, not `static` and in no anonymous namespace) always needs its doc comment, one statement or not (rule 1); `type-doc`: a public type a header declares with no comment (a private or protected nested type, or one in an anonymous namespace, is not public); `tag`: a function of a gameplay folder (`paths.gameplay`) with neither `@orig` nor `@coney-only` (the older "Coney's own" wording is not a marker) |
| A test file under `tests/` | `research/legacy-files.toml` | `citation`: a test case (`TEST_CASE`, `TEST_CASE_METHOD`, `TEMPLATE_TEST_CASE`, `SCENARIO`) with no `// Expected:` line that cites a section of a research page (`<page>.md#<anchor>`, with or without the leading `research/`) or says `unverified, #<issue>` ([citing an expected value](testing.md#citations)); a file with no test case, such as a helper header, has nothing to hold |

The whole-file clang-tidy pass of the code standard is CI's own and is not scanned here. The marks are written once and
only shrink: nothing is added to a legacy file after the first scan, and a file leaves it when it passes.

```sh
uv run --project python coney-tools standards scan [--files]
uv run --project python coney-tools standards init
uv run --project python coney-tools standards prune
uv run --project python coney-tools standards mark
uv run --project python coney-tools standards check [--base origin/main] [--cleanup]
```

| Command | What it does |
| --- | --- |
| `scan` | Prints how many ported functions are `legacy`, `compared` or have no status; how many source and test files are below the standard, with their findings counted by rule; and how many research pages are listed. `--files` also prints every finding as `path:line: rule: message`. Read-only; exit 0 |
| `init` | Writes the legacy file of source and test files from a scan. It refuses when the file exists (exit 2): the file only shrinks |
| `prune` | Takes out the files that now pass, drops the rules a file now passes and lowers the counts to the scan's. Never adds a file or a rule. Run it in the commit that fixes a file |
| `mark` | Gives every ported function that has no `status` the status `legacy` and rewrites the database files that hold one. A function becomes `compared` only by a side-by-side comparison, never here. For a new port the `legacy` it writes is refused by `check`: record it `compared` instead |
| `check` | The marks are consistent and the functions, types and test cases the change touches (and the files it adds) meet the standard, measured from `--base` (default `origin/main`). Prints `path:line: [warning: ]rule: message` and one `info` line per touched file for its other findings (`--cleanup`: each of them), exits 1 when there is an error |

`check` reports:

* a file in the legacy file that is gone, or listed for a rule it now passes (`standards prune` takes it out), so the
  file only shrinks;
* a ported function with no `status`;
* when `--base` already has the legacy file, a file or a rule added to it since the branch left `--base`;
* once `standards.cutover` is set (it is: 2026-10-25), a function marked `legacy` that was not at `--base`: nothing new
  is marked legacy. A new port is recorded `compared`, which needs its side-by-side comparison first
  ([Recording a port](research-function-database.md#ported)).

And the touch-it rule ([Touch a page, fix the page](writing-docs.md#touch-it), for files and functions):

* in a source or test file the change touches (committed or not, untracked included), listed or not, the parts the
  change touched are held to the standard: a `comment` or `tag` finding when a changed line is in its function (from
  its first line to its closing brace; a declaration's one statement), a `type-doc` finding when one is in its type
  (from the `class` or `struct` to its closing brace), a `citation` finding when one is in its test case (from the
  comment block right above the macro to the line before the next case). A new file is held whole, and a file the change
  renamed (as `git diff -M` finds it, at `research.moved_similarity`) is read from git's diff of the renamed pair, as a
  moved page is. A function that only moved, to another file, folder or class with its code, tags and standing as they
  were, is not touched, nor is the line where its old copy was deleted, as long as that deletion took out only the moved
  code, comments and blank lines ([Any function](orig-body-hash.md#any-function)). One report per rule with the number
  of findings and the first;
* every other finding of a touched file is `info`: it fails nothing, and `check` prints one line for the file,
  `path:1: info: touch-it: N findings outside what the change touched (rule count, ...)`. `check --cleanup` (and
  `docs check --cleanup`) prints each one instead, so the coordinator can file them as cleanup issues; `scan --files`
  lists the findings of every file;
* a ported function whose lines the change touches (a new file touches all of its functions but those it only moved
  in) and whose status is not `compared` is reported with the address it ports: study it, pair it and have it reviewed,
  then set `compared`. Its tags count wherever they are written: above it, inside its body, or on its declaration in
  the header, matched overload by overload as [`orig check-tags`](coney-tools-fidelity.md#orig) matches them, so
  touching one overload owes only its own address.

**A comment's research link that follows a page move is no touch.** A hunk of a source or test file's diff that only
rewrites research references in comments does not touch the file (nor the functions whose lines it sits in) when every
reference it changes follows a page the same change moved: the rules of [a link rewrite in a
page](writing-docs.md#legacy-pages), applied to `docs/research/<page>.md#anchor` (also `research/<page>.md#anchor`, and
on a `// Expected:` line a bare `<page>.md#anchor`) with the page's path taken from the repository root. The old page is
renamed or deleted in the change and the new target resolves, or the anchor's section moved between two pages that
stayed. As for a page, when the old page is gone the new anchor is not compared with the old one: it only has to exist,
so a comment redirected to another section of the new page is no touch either. A reference counts only in a comment, as
a scan of the file's lines reads it (`comment_spans.py`): after a `//` that is not inside a string or a character
literal, or inside a `/* ... */`, which a line of a block comment is read from the lines above it. Code on the same
line, a `"// ..."` string (also one a trailing backslash continues over lines, and next to an `L'"'` or `u8'"'`
character literal) and a raw string are not comment, so a link move there is a touch. The rest of the hunk must be
unchanged, line by line (the indent included), or, when every line on both sides is nothing but a comment of the same
kind and none is empty, as one paragraph that clang-format reflowed because the longer path pushed a line past the width
(the words are equal). So any other edit in the hunk, a reference to a page that stayed or that resolves nowhere, a
reference that only changed its form, and a link move in code or a string are a touch, and a file with one such hunk is
held to the standard in the parts that hunk touches. This is what lets a restructure that moves a research page repoint
the files that cite it without those files owing the standard; the check is `code_link_moves.py`, which
`standards_check` calls for each touched file.

Enforcement is on: `standards.enforce` is true and `standards.cutover` is 2026-10-25 in
[.github/conventions.yml](repo:.github/conventions.yml). Every report of `check` but the `info` ones is an error, so a
change that leaves a function, type or test case it touched below the standard, adds a file below it, touches a ported
function that is not `compared`, adds to the legacy file or marks a function `legacy` fails `standards check` and `docs
check`, and so the `mkdocs` job of CI. How far the rule reaches is `standards.touch` there, which `docs check` reads for
the pages. The marks that are out of step say what to run (`coney-tools standards prune` for a file that passes or is
gone; for a ported function with no status, record it `compared` after its comparison, since `standards mark` writes
`legacy`, which the cutover refuses for anything new). The same check runs inside `docs check` whenever the change
touches a source or test file, the function database, the legacy file, the conventions or these checks, so the `mkdocs`
job of CI and the docs step of [`ci run`](coney-tools-ci.md#ci-run) hold it without a step of their own. The group is
`standards`, not `legacy`: that name is reserved for the retired review commands.

`standards` is `standards_scan.py` (the scanners), `standards_file.py` (the legacy file), `standards_check.py`
(the marks and the touch-it rule), `code_link_moves.py` and `comment_spans.py` (the comment link moves) and
`standards_cli.py`; the C++ is read with the reader of `orig_scan.py`.

<!-- command-reference:begin standards -->
## Parameters of `standards` {#standards-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `standards scan` {#standards-scan-usage}

Count what is below the standard: functions, source files, tests, pages.

```text
coney-tools standards scan [--files]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--files` | flag | off | also list every finding, one per line |

### `standards init` {#standards-init-usage}

Write the legacy file of source and test files from a scan (refuses if it exists).

```text
coney-tools standards init
```

Takes no parameters.

### `standards prune` {#standards-prune-usage}

Take the files that pass out of the legacy file and lower the counts; never adds.

```text
coney-tools standards prune
```

Takes no parameters.

### `standards mark` {#standards-mark-usage}

Give every ported function with no status the status legacy (refused for a new port: record it compared).

```text
coney-tools standards mark
```

Takes no parameters.

### `standards check` {#standards-check-usage}

The marks are consistent and the files and functions the change touches meet the standard.

```text
coney-tools standards check [--base BASE] [--cleanup]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--base BASE` | text | `origin/main` | the branch the change is measured from |
| `--cleanup` | flag | off | list each finding of a touched file outside what the change touched (for cleanup issues) |
<!-- command-reference:end -->
