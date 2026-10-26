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
| A test file under `tests/` | `research/legacy-files.toml` | `citation`: a test case (`TEST_CASE`, `TEST_CASE_METHOD`, `TEMPLATE_TEST_CASE`, `SCENARIO`) with no `// Expected:` line that cites a section of a research page (`<page>.md#<anchor>`, with or without the leading `research/`) or says `unverified, #<issue>` ([citing an expected value](testing.md#citations)) |

The whole-file clang-tidy pass of the code standard is CI's own and is not scanned here. The marks are written once and
only shrink: nothing is added to a legacy file after the first scan, and a file leaves it when it passes.

```sh
uv run --project python coney-tools standards scan [--files]
uv run --project python coney-tools standards init
uv run --project python coney-tools standards prune
uv run --project python coney-tools standards mark
uv run --project python coney-tools standards check [--base origin/main]
```

| Command | What it does |
| --- | --- |
| `scan` | Prints how many ported functions are `legacy`, `compared` or have no status; how many source and test files are below the standard, with their findings counted by rule; and how many research pages are listed. `--files` also prints every finding as `path:line: rule: message`. Read-only; exit 0 |
| `init` | Writes the legacy file of source and test files from a scan. It refuses when the file exists (exit 2): the file only shrinks |
| `prune` | Takes out the files that now pass, drops the rules a file now passes and lowers the counts to the scan's. Never adds a file or a rule. Run it in the commit that fixes a file |
| `mark` | Gives every ported function that has no `status` the status `legacy` and rewrites the database files that hold one. A function becomes `compared` only by a side-by-side comparison, never here. For a new port the `legacy` it writes is refused by `check`: record it `compared` instead |
| `check` | The marks are consistent and the files and functions the change touches meet the standard, measured from `--base` (default `origin/main`). Prints `path:line: [warning: ]rule: message`, exits 1 when there is an error |

`check` reports:

* a file in the legacy file that is gone, or listed for a rule it now passes (`standards prune` takes it out), so the
  file only shrinks;
* a ported function with no `status`;
* when `--base` already has the legacy file, a file or a rule added to it since the branch left `--base`;
* once `standards.cutover` is set (it is: 2026-10-25), a function marked `legacy` that was not at `--base`: nothing new
  is marked legacy. A new port is recorded `compared`, which needs its side-by-side comparison first
  ([Recording a port](research-function-database.md#ported)).

And the touch-it rule ([Touch a page, fix the page](writing-docs.md#touch-it), for files and functions):

* a source or test file the change touches (committed or not, untracked included) is held to the standard as a whole,
  listed or not: one report per rule with the number of findings and the first;
* a ported function whose lines the change touches (a new file touches all of its functions) and whose status is not
  `compared` is reported with the address it ports: study it, pair it and have it reviewed, then set `compared`. Its
  tags count wherever they are written: above it, inside its body, or on its declaration in the header, matched
  overload by overload as [`orig check`](coney-tools-fidelity.md#orig) matches them, so touching one overload owes
  only its own address.

Enforcement is on: `standards.enforce` is true and `standards.cutover` is 2026-10-25 in
[.github/conventions.yml](repo:.github/conventions.yml). Every report of `check` is an error, so a change that touches a
file below the standard without bringing the whole file up to it, touches a ported function that is not `compared`, adds
to the legacy file or marks a function `legacy` fails `standards check` and `docs check`, and so the `mkdocs` job of
CI. The marks that are out of step say what to run (`coney-tools standards prune` for a file that passes or is gone;
for a ported function with no status, record it `compared` after its comparison, since `standards mark` writes
`legacy`, which the cutover refuses for anything new). The same check runs inside `docs check` whenever the change
touches a source or test file, the function database, the legacy file, the conventions or these checks, so the `mkdocs`
job of CI and the docs step of [`check changed`](coney-tools-build.md#check) hold it without a step of their own.
The group is `standards`, not `legacy`: that name is reserved for the retired review commands.

`standards` is `standards_scan.py` (the scanners), `standards_file.py` (the legacy file), `standards_check.py`
(the marks and the touch-it rule) and `standards_cli.py`; the C++ is read with the reader of `orig_scan.py`.

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
coney-tools standards check [--base BASE]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--base BASE` | text | `origin/main` | the branch the change is measured from |
<!-- command-reference:end -->
