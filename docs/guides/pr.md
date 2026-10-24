# Releasing a pull request

Before a unit of work becomes a pull request, two read-only commands say whether it is ready: `coney-tools pr verify`
checks that the proof the unit carries holds for the change on the branch, and `coney-tools pr text` writes the pull
request's title and description from the engineer's one paragraph, the task and the stamps. Neither pushes, opens or
edits anything on GitHub, so an engineer runs them whenever it wants to know where the unit stands. They are the code
that `pr open` (planned) will run before it pushes, so what they accept is what a release will accept. The commands'
options and exit codes are in [the repository commands](coney-tools-repo.md#pr); this page is what they check and what
each refusal asks you to do.

The proof is the unit's [stamp log](stamps.md#log), kept in the task's scratch folder. The commands trust nothing they
can recompute: every hash a stamp covers is computed again from the files and the git tree, and the rules (which
checks a change needs, who may review) are read from the remote's default branch, never from the branch being released.
A stage that fails is reported together with all the others, each with the command to run again.

## The stages {#stages}

| Step | Stage | Holds when |
| --- | --- | --- |
| 1 | `log` | The stamp chain is intact, starts with `assigned` and `claimed`, and every stamp's time is not in the future and not before the author date of its commit |
| 2 | `checked` | The newest `checked` stamp passed, covers this [change](stamps.md#change), ran a big enough profile with all its checks and (profile `full`) names a build record that was a whole, clean `ci` run |
| 3 | `compared`, `fails-first` | Every `@orig` function the branch adds or changes was studied and compared at the body it has now, at every address it carries, and every test the branch adds has a `fails-first` stamp |
| 4 | `reviewed` | The newest `reviewed` stamp passed, covers this change, these pairs and this text, and judged every changed function `clean` or `diff-deliberate-ok` |
| 5 | `text` | The generated title and description pass the checks CI runs (`pr text` only) |

Stamps are bound to the change, not to the tip, so a clean rebase keeps them valid and a stamp whose commit a rebase
rewrote is still accepted. A stamp's `commit` is a record; only its `time` is checked against it.

## pr verify {#verify}

```sh
uv run --project python coney-tools pr verify [TASK]
```

Runs steps 1 to 4 on the unit and prints one block per refusal: the message, then what to redo. Exit 0 when everything
holds, 1 when something is refused, 2 when it cannot run (no `TASK.md`, an unknown base, unreadable rules). It changes
nothing: no stamp, no file, no git state. It does not fetch; the base is `<remote>/<default branch>` as it was last
fetched, so run `git fetch` first when main has moved.

A worktree with uncommitted changes is refused first (`worktree:`), because the stamps are bound to what is committed
and the checks read the files. A log that is not intact stops the run after step 1: nothing else in it can be trusted,
and the unit goes to a maintainer. The `Check` profile the change needs is the smallest of `proof.profiles` that takes
the [CI scope class](building.md#what-a-pull-request-runs) of every changed path; a path in no class means `full`.

What it refuses, exactly (`<...>` is filled in):

| Stage | Message | Redo |
| --- | --- | --- |
| `worktree` | `worktree: uncommitted changes: <first 5 paths>; the engineer commits or removes them` | the engineer |
| `log` | `log: 0001.jsonl:14: seq 14: prev does not match the stamp before it (the log was edited)` (any `stamp verify` problem) | none: report to a maintainer |
| `log` | `log: <task> has no stamps; run task start` | `task start` |
| `log` | `log: the log does not start with assigned and claimed (found: <steps>)`; `log: the claimed stamp names no engineer` | `task start` |
| `log` | `log: seq 9 compared 0x00501000 was written without the hook identity (source unverified)` (only with `stamps.require_hook_source`; only the newest stamp of each step and subject counts) | the stage named |
| `log` | `log: seq 9: time <t> is in the future`; `... is before its commit <12 digits> was authored (<t>)` | none: report to a maintainer |
| `log` | `log: mapping/0x00501000.toml pair 7 note repeats a line of the original; say it in your own words` (a statement of 20 or more characters of the saved decompile appears in a note or merge reason) | `orig compare <addr>` |
| `checked` | `checked: no passing Check; run task check`; `checked: the newest Check failed; fix what it found and run task check` | `task check` |
| `checked` | `checked: stale: the branch changed since Check (diff a1b2c3d4 now c3d4e5f6)`, with `; changed since: <files>` when the stamp lists its files | `task check` |
| `checked` | `checked: Check ran profile docs, this change needs full (touches src/ai/brain.cpp)` | `task check` |
| `checked` | `checked: Check did not run: ctest-full-disc` (one line per missing check of the profile) | `task check` |
| `checked` | `checked: the build record <12 digits> is not in the task's build-runs folder (or differs from the stamp)` | `task check` |
| `checked` | `checked: the build record is a filtered run (-R ...); Check needs the full suite`; `... is for preset dev, Check needs ci`; `... run failed (exit 1)`; `... has 1 failed test`; `41 disc tests skipped; Check needs the disc (CONEY_DISC)`; `... was made with uncommitted changes`; `... is for commit <12 digits>, the Check stamp for <12 digits>`; `... test counts differ from the Check stamp's` | `task check` |
| `compared` | `compared: Widget::update (0x00501000) was changed and never studied` | `orig study <addr>` |
| `compared` | `compared: 0x00501000: study/decompile.c differs from what the studied stamp covers`; `... the saved study is unusable (<why>)` | `orig study <addr>` |
| `compared` | `compared: 0x00501000: line 14 (piece 5, lines 14-16) is in no pair` (any pairs check: coverage, relations, the Coney locations) | `orig compare <addr>` |
| `compared` | `compared: 0x00501000: Widget::update changed after it was compared (body a1b2 now c3d4)`; `... the pairs file changed after it was compared`; `... was never compared` | `orig compare <addr>` |
| `compared` | `compared: src/ai/brain.cpp:88: <fingerprint> (copy-check)` (on the function's file or a file its pairs cite) | the engineer, then `orig compare` |
| `fails-first` | `fails-first: "Widget ready frame" was added and has no passing fails-first stamp`; `... its test file changed since (tests/ai/widget_test.cpp)` | `orig fails-first` |
| `fails-first` | `fails-first: a feature or fix under src/ai/ needs a test that failed first, or --none with a reason` | `orig fails-first` |
| `fails-first` | `fails-first: no fails-first stamp; a docs task records orig fails-first --none "<reason>"` (kinds `docs`, `tooling`, `ci-fix` with no test added) | `orig fails-first` |
| `reviewed` | `reviewed: not reviewed`; `reviewed: Review failed (2 blockers, see the verdict)` | `task review` |
| `reviewed` | `reviewed: stale: the branch changed since Review (diff ... now ...)`; `... the pairs changed since Review` | `task review` |
| `reviewed` | `reviewed: stale: the pull request title changed since Review`; `... the summary paragraph ...`; `... the screenshots ...` | `task review --text` |
| `reviewed` | `reviewed: 0x00501000 was judged diff-bug`; `reviewed: Review did not cover 0x00501000` | the engineer fixes, then `task check` and `task review`; `task review` |
| `reviewed` | `reviewed: Review did not grade the research pages this change touches` (a change under `proof.research_paths`) | `task review` |
| `reviewed` | `reviewed: reviewer engineer-1 also worked on this task` | `task review` (another reviewer) |
| `reviewed` | `reviewed: the reviewer's type is engineer, not reviewer`; `reviewed: only Claude Opus 5.5 may review a change to an @orig function; this review ran on <model>` (only with `stamps.require_hook_source`) | `task review` |

Two notes about the rules behind it. A `none` fails-first stamp is accepted for a docs, tooling or `ci-fix` task and
refused for a feature or fix that changes a gameplay folder (`paths.gameplay`): a gameplay change is proven by a test
that failed first. Python tests are not stamped; a tooling task's tests-first rule rests on the reviewer and on pytest
in its `checked` stamp. And a function that carries several `@orig` addresses needs every address compared against that
one Coney body.

A function the branch removes is printed as a note, not a refusal.

## pr text {#text}

```sh
uv run --project python coney-tools pr text [TASK]
```

Generates the title and description, checks them and, when they pass, writes `PR-TITLE.txt` and `PR-BODY.md` in the
folder of the engineer's file (next to `shots/`, so `./shots/x.png` is the path `gh pr create --attach` matches). A
description that fails is printed with what to redo and nothing is written. Exit codes as `pr verify`.

### The engineer's file {#file}

The engineer writes one file, `PR.md` in the task's scratch folder (a fix task uses its parent's):

```markdown
gameplay: Fix the widget ready frame

## Summary

One paragraph: what changed and why.

Docs: none

## Testing

- Runtime: trace diff widget60, 600 updates, 0 outside tolerance

## Screenshots

![after](./shots/after.png)
```

Line 1 is the title (`area: Verb the rest`). The Summary is one paragraph and, if the change needs no documentation,
a `Docs: none` line. Testing holds only the `Runtime:` line (required for the title areas a player sees); the facts are
generated. Screenshots holds image references under `./shots/`, or `Screenshots: none (<reason>)`. Anything else is
refused: `Fixes` lines, the markers and the Checklist are generated, so a hand-typed claim cannot disagree with the
stamps. A section the template has that only a person can fill, such as `## Issues affected`, goes in the file under its
own heading and is kept as written. A person's own pull request, with no AI identity in the log and no model in the
commits' trailers, also writes the `## AI use` section itself.

### What is generated {#generated}

The sections are those of `pull_requests.sections` in `.github/conventions.yml`, in its order; a section added there
needs no change to the generator. One that neither the engineer's file nor the generator fills is written as a `TODO:`
line and refused by the checks until it is.

| Section | Generated from |
| --- | --- |
| Summary | The paragraph between `<!-- coney:summary -->` markers, the `Docs: none` line, and one `Fixes #N` line per issue of `TASK.md` (none for a task whose front block says `finishes: false`) |
| Checklist | One ticked item per bullet of the task's Done section |
| Testing | The [facts block](#facts) between `<!-- coney:facts -->` markers, then the engineer's `Runtime:` line |
| Screenshots | The engineer's section, as written |
| AI use | `Commits written by: an AI` and the models the branch's `Co-Authored-By` trailers name, else the models of the engineers in the log. No trailer lines are added to the description: GitHub copies the branch commits' trailers into the squash commit |
| Issues affected | The engineer's section of `PR.md`, as written (only a person can say how an issue is affected); without it, a `TODO:` line listing the candidates of `coney-tools issue impact` to confirm, or a bare `TODO:` when that command is not there |

The marker names (`pull_requests.markers`) let a later pass find the two generated blocks again.

### The facts block {#facts}

The facts say which stages passed, with their hashes cut to 12 digits, from the local log alone: the log's task and chain
head, the change and the main it is on, then one line each for the functions studied and compared (pairs counted by
relation), the tests that failed first, Check (profile, preset, counts, build record) and Review (verdict, model,
functions by class), and the stand-in and not-ported pieces of the pairs. A stage with no stamp reads `Check: not run`
or `Review: not reviewed`. Every name from a stamp or a pairs file is a code span with backticks and `@` neutralised,
because those strings are not trusted text. The block is at most 8,000 characters; past that the per-stage lines become
counts. It is a record for the reader, not proof: nothing in CI reads it, and the local log stays the source.

### The checks {#checks}

On the generated text it runs `check-title` and `check-body` as the old description gate ran them (a local picture is
accepted where CI wants an uploaded one, because `gh --attach` uploads it), `check-docs` with the changed paths (so
`Docs: none` is honoured), the `TODO:` rule, and the pictures: each must be under `shots/` next to the file, outside any
git checkout, one of `pull_requests.images.extensions` (a clip is refused: `clips publish` is the only video upload), at
most `max_bytes` and not byte-identical to another, and there are at most `max_count`. Its output also holds the hashes
a release records: the title, paragraph, pictures and facts (`--json`).

## What this does not do {#limits}

These commands guard against mistakes and against agents, not against the person at the machine: the stamp log is
written only by the stage commands and the hooks keep agents from writing it, but a person who can write the files can
rebuild a chain. An edit made on the GitHub website is not checked beyond the `title`, `docs` and `body` jobs. The
description CI checks is the one that `pr text` generates; nothing in CI reads the stamps.
