# Releasing a pull request

Before a unit of work becomes a pull request, two read-only commands say whether it is ready: `coney-tools pr verify`
checks that the proof the unit carries holds for the change on the branch, and `coney-tools pr text` writes the pull
request's title and description from the engineer's one paragraph, the task and the stamps. Neither pushes, opens or
edits anything on GitHub, so an engineer runs them whenever it wants to know where the unit stands. They are the code
that [`pr open`](#open) runs before it pushes, so what they accept is what a release will accept. The commands'
options and exit codes are in [the pull request commands](coney-tools-pr.md#pr); this page is what they check and what
each refusal asks you to do.

The proof is the unit's [stamp log](stamps.md#log), kept in the task's scratch folder. The commands trust nothing they
can recompute: every hash a stamp covers is computed again from the files and the git tree, and every rule that
decides whether a release is allowed is read from the remote's default branch, never from the branch being released
([Where the rules come from](#rules)). A stage that fails is reported together with all the others, each with the
command to run again.

## Where the rules come from {#rules}

A branch can edit `.github/conventions.yml`, and the tool that releases it cannot trust the edit: a branch that sets
`stamps.require_hook_source` to false or widens the models that may review would otherwise loosen its own gate. So
`pr verify`, `pr text` and `pr open` read each section that decides whether a release is allowed from
`origin/<default branch>` (the branch `origin/HEAD` names, whatever `--remote` is) and only the rest from the branch
(`load_release_rules_at`, one reader for the three commands):

| Section of `.github/conventions.yml` | Source | What in a release it decides |
| --- | --- | --- |
| `stamps` | default branch | The stamp log's steps, the pairs vocabulary and `require_hook_source` (the hook identity on every stamp) |
| `tasks.review` | default branch | Who may review, on which model, which paths are research, and the files of a review |
| `proof` | default branch | The `Check` profiles, the checks each needs and the build preset |
| `paths` | default branch | The CI scope classes, the gameplay folders (a test that failed first) and the docs paths (`Docs: none`) |
| `pull_requests` | default branch | The description's sections, `Runtime:`, `Docs: none`, pictures and the AI use section |
| `commits` | default branch | The title's areas, verbs and length, and the trailers |
| `repo`, `merge`, `claims` | default branch | The repository, what auto-merge does and the claim cards (`pr open`'s own steps) |
| `tasks` other than `review` (the task file's name and kinds, `task start`) and every other section | the branch | Nothing that decides a release |

A default branch that has no `proof` section yet (it is older than the section) leaves `proof` and `paths` to the
branch, and the first line says `rules of branch`. A key the tool does not know in the default branch's sections is
skipped, as it is in the branch's file ([How it is read](repo-conventions.md#how-it-is-read)); a missing key or a wrong
value there stops the run (exit 2) and names `origin/<default branch>:.github/conventions.yml`, and the branch's own
rules are never used instead. Their references between sections (the labels `claims` names) are checked against that
file's own lists, never the branch's, so a branch cut before a label was added is released.
[`task check`](coney-tools-repo.md#task-check) reads `proof` and the scope lists from the same place.

## The stages {#stages}

| Step | Stage | Holds when |
| --- | --- | --- |
| 1 | `log` | The stamp chain is intact, starts with `assigned` and `claimed`, and every stamp's time is not in the future and not before the author date of its commit |
| 2 | `checked` | The newest `checked` stamp passed, covers this [change](stamps.md#change), ran a big enough profile with all its checks and (profile `full`) names a build record that was a whole, clean `ci` run |
| 3 | `compared`, `fails-first` | Every `@orig` function the branch adds or changes was studied and compared at the body it has now, at every address it carries, and every test the branch adds has a `fails-first` stamp |
| 4 | `reviewed` | The newest `reviewed` stamp passed, covers this change, these pairs and this text, and judged every changed function `clean` or `diff-deliberate-ok` |
| 5 | `text` | The generated title and description pass the checks CI runs (`pr text` only) |

Stamps are bound to the change, not to the tip, so a clean rebase keeps them valid and a stamp whose commit a rebase
rewrote is still accepted. A stamp's `commit` is a record, checked in two ways only: the stamp's `time` is not before
that commit's author date, and a `full` Check's build record must name that same commit as its `tip` (the record and
the stamp come from one run).

## pr verify {#verify}

```sh
uv run --project python coney-tools pr verify [TASK]
```

Runs steps 1 to 4 on the unit and prints one block per refusal: the message, then what to redo. Exit 0 when everything
holds, 1 when something is refused, 2 when it cannot run (no `TASK.md`, an unknown base, `origin/HEAD` unset, unreadable
rules). It changes nothing: no stamp, no file, no git state. It does not fetch; the base is `<remote>/<default branch>`
as it was last fetched, so run `git fetch` first when main has moved.

A worktree with uncommitted changes is refused first (`worktree:`), because the stamps are bound to what is committed
and the checks read the files. A log that is not intact stops the run after step 1: nothing else in it can be trusted,
and the unit goes to a maintainer. The `Check` profile the change needs is the first of `proof.profiles` whose classes
hold the [CI scope class](building.md#what-a-pull-request-runs) of every changed path (a path in no class means `full`),
and the checks it must have run are that profile's, less those `proof.only_when` says do not apply: the same functions
(`proof_profiles`) [`task check`](coney-tools-repo.md#task-check) stamps with. The `proof` section and the path classes
are read from `origin/<default branch>`, so a branch cannot shrink them, and so is every other rule that decides a
release ([Where the rules come from](#rules)). The default branch is the one `origin/HEAD` names (as `git clone` sets it;
else run `git remote set-head origin --auto`), never the branch's own conventions file, and `--remote` chooses only the
base of the change, not where the rules come from; while the base has no `proof` section yet, the branch's own are used
and the first line says `rules of branch`.

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
| `checked` | `checked: stale: the branch changed since Check (diff a1b2c3d4 now c3d4e5f6)` | `task check` |
| `checked` | `checked: Check ran profile docs, this change needs full (touches src/ai/brain.cpp)` | `task check` |
| `checked` | `checked: Check did not run: ctest-full-disc` (one line per missing check of the profile) | `task check` |
| `checked` | `checked: the build record <12 digits> cannot be read: <why>` (missing, edited, not JSON) | `task check` |
| `checked` | every problem [`build_record.problems`](stamps.md#build-record) finds, the rule `task check` applies, at the commit the `checked` stamp names: `checked: the build record is a filtered run (-R ...); Check needs the full suite`; `... is preset dev, Check needs ci`; `... has version None, this tool reads 1`; `... has no test run`; `checked: no tests ran`; `checked: 1 test failed`; `checked: 41 disc tests skipped; Check needs the disc (CONEY_DISC)`; `... was made from a worktree with uncommitted changes`; `... is for another commit (<10 digits>, the tip is <10 digits>)`; `checked: the log <file> differs from the hash the build record kept (it was edited)`; and `checked: the build record's test counts differ from the Check stamp's` | `task check` |
| `compared` | `compared: Widget::update (0x00501000) was changed and never studied` | `orig study <addr>` |
| `compared` | `compared: 0x00501000: study/decompile.c differs from what the studied stamp covers`; `... the saved study is unusable (<why>)` | `orig study <addr>` |
| `compared` | `compared: 0x00501000: line 14 (piece 5, lines 14-16) is in no pair` (any pairs check: coverage, relations, the Coney locations) | `orig compare <addr>` |
| `compared` | `compared: 0x00501000: Widget::update changed after it was compared (body a1b2 now c3d4)`; `... the pairs file changed after it was compared`; `... was never compared` | `orig compare <addr>` |
| `compared` | `compared: src/ai/brain.cpp:88: <fingerprint> (copy-check)` (on the function's file or a file its pairs cite) | the engineer, then `orig compare` |
| `fails-first` | `fails-first: "Widget ready frame" was added and has no passing fails-first stamp`; `... its test file changed since (tests/ai/widget_test.cpp)` | `orig fails-first` |
| `fails-first` | `fails-first: "Widget ready frame" changed between its commits and rests on decision #12, which is not the coordinator's decision for them` (a pass whose `decision_seq` names no [decision on a changed test](stamps.md#changed-test) for it) | `orig fails-first --accept-change` (the coordinator), then the engineer's `orig fails-first` |
| `fails-first` | `fails-first: a feature or fix under src/ai/ needs a test that failed first, or --none with a reason` | `orig fails-first` |
| `fails-first` | `fails-first: no fails-first stamp; a docs task records orig fails-first --none "<reason>"` (kinds `docs`, `tooling`, `ci-fix` with no test added) | `orig fails-first` |
| `reviewed` | `reviewed: not reviewed`; `reviewed: Review failed (2 blocking findings, see the verdict)` | `task review` |
| `reviewed` | `reviewed: stale: the branch changed since Review (diff ... now ...)`; `... the pairs changed since Review` | `task review` |
| `reviewed` | `reviewed: stale: the pull request title changed since Review`; `... the summary paragraph ...`; `... the screenshots ...` | `task review --text` |
| `reviewed` | `reviewed: the text Review covered cannot be read: <path>: <why>` (no `PR.md`, or one with no title or summary: the three text hashes cannot be compared, so nothing holds) | the engineer, then `task review --text` |
| `reviewed` | `reviewed: 0x00501000 was judged diff-bug`; `reviewed: Review did not cover 0x00501000` | the engineer fixes, then `task check` and `task review`; `task review` |
| `reviewed` | `reviewed: Review did not grade the research pages this change touches` (a change to a [research page](#research-page)) | `task review` |
| `reviewed` | `reviewed: reviewer engineer-1 also worked on this task` (the reviewer wrote any stamp of the unit other than a `reviewed` one, whatever its agent type: the rule `task review --record` applies); `reviewed: the Review stamp names no reviewer` | `task review` (another reviewer) |
| `reviewed` | `reviewed: the reviewer's type is engineer, not reviewer`; `reviewed: a review of orig work must run on opus; this review ran on <model>` (the agent and the model words of `tasks.review`, the rule `task review --record` applies; only with `stamps.require_hook_source`) | `task review` |

`pr verify` recomputes the change hash itself, against the merge base with the remote default branch, so a `checked`
stamp made with `task check --base <a branch commit>` (a hash of only the later commits, or rules read from the
branch) never matches and is refused as stale.

Two notes about the rules behind it. A `none` fails-first stamp is accepted for a docs, tooling or `ci-fix` task and
refused for a feature or fix that changes a gameplay folder (`paths.gameplay`): a gameplay change is proven by a test
that failed first. Python tests are not stamped; a tooling task's tests-first rule rests on the reviewer and on pytest
in its `checked` stamp. And a function that carries several `@orig` addresses needs every address compared against that
one Coney body.

A function the branch removes is printed as a note, not a refusal. A change that touches no C++ file under `src/`
adds, changes and removes no `@orig` function, so the run does not read the source at all.

### What counts as a research page {#research-page}

A research page is a documentation file (`tasks.review.docs_suffixes`, `.md`) under one of `tasks.review.research_paths`
(`docs/research/` and `research/`). `TaskReview.is_research_page` in `repo:python/src/coney_tools/conventions.py` is the
one definition: `task review` lists exactly these in its packet's `pages`, and
`pr verify` demands that the `reviewed` stamp graded a page only when the change touches one of them. A data file under
`research/` (`legacy-files.toml`, which `standards prune` rewrites, or a YAML table) is not a page: no reviewer can
grade it, so a change whose only research paths are data files passes with a review whose `pages` list is empty. It is
still research work: any change under the research paths gets the research checklist and, for a verified reviewer,
the reverse-engineering model rule (`tasks.review.reverse_engineering_kinds`).

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
a `Docs: none` line. Testing holds only the `Runtime:` line, and the facts are generated; which title areas need the
line is in [the `Runtime:` line](testing.md#runtime-line). Issues affected holds
one `- #N: how it is affected` line per open issue the change affects, or
`None (<reason>)` (`pull_requests.issues_affected`; [Issues a pull request changes](issues.md#affected)). Screenshots
holds image references under `./shots/`, or `Screenshots: none (<reason>)`. Anything else is refused: `Fixes` lines,
the markers and the Checklist are generated, so a hand-typed claim cannot disagree with the stamps. A heading that is
not one of `pull_requests.sections` is refused ("is not a section of a pull request description"); a section of that
list that only a person can fill (Issues affected, or one added later) goes in the file under its own heading and is
kept as written. A person's own pull request, with no AI identity in the log and no model in the commits' trailers,
also writes the `## AI use` section itself.

### What is generated {#generated}

The sections are those of `pull_requests.sections` in `.github/conventions.yml`, in its order; a section added there
needs no change to the generator. One that neither the engineer's file nor the generator fills is written as a `TODO:`
line and refused by the checks until it is.

| Section | Generated from |
| --- | --- |
| Summary | The paragraph between `<!-- coney:summary -->` markers, the `Docs: none` line, and one `Fixes #N` line per issue of `TASK.md` (none for a task whose front block says `finishes: false`) |
| Checklist | One ticked item per bullet of the task's Done section, read past the quote marks (`>`) `task create` writes; a bullet that wraps onto more lines is one whole item (its lines are joined, up to a blank line or the next bullet); a Done section of prose gives one item per paragraph. A Done section with no criterion (the `See issue #N.` placeholder) is refused |
| Testing | The [facts block](#facts) between `<!-- coney:facts -->` markers, then the engineer's `Runtime:` line |
| Screenshots | The engineer's section, as written |
| AI use | `Commits written by: an AI` and the models the branch's `Co-Authored-By` trailers name, else the models of the engineers in the log. No trailer lines are added to the description: GitHub copies the branch commits' trailers into the squash commit |
| Issues affected | The engineer's section of `PR.md`, as written (`check-body` checks its form: [Issues a pull request changes](issues.md#affected)). Without it, a `TODO:` that says what the section holds and lists the candidates of [`issue impact`](coney-tools-issues.md#issue-impact) (the issues the task finishes left out); it is refused until the engineer has read the candidates and written the section |
| Any other section added to `pull_requests.sections` that only a person can fill | The engineer's section of `PR.md`, as written. Without it, a bare `TODO:`, which is refused |

The marker names (`pull_requests.markers`) let a later pass find the two generated blocks again.

`PR.md` therefore lacks the Checklist, the facts of Testing, AI use and the `Fixes` lines on purpose; they are in
`PR-BODY.md`. A reviewer judges them there and never fails `PR.md` for lacking them: the review packet lists them
([The description's generated sections](review/task.md#generated)).

### The facts block {#facts}

The facts say which stages passed, with their hashes cut to 12 digits, from the local log alone: the log's task and
chain head, the change and the `<remote>/<default branch>` commit it is on, then one line each for the functions studied
and compared (pairs counted by relation), the tests that failed first, Check (profile, preset, the build's counts,
pytest's counts, build record; a profile with no build says `no build`, or `no build or test run` for `docs`, which
runs no pytest, and names the checks it ran) and Review (verdict, model, functions by class), and the stand-in and
not-ported pieces of the pairs. A stage with no stamp
reads `Check: not run` or `Review: not reviewed`, and a Check or Review made for another change reads `stale (made for
change <12 digits>)` instead of its verdict, as `pr verify` refuses it. Every name from a stamp or a pairs file is a
code span with backticks and `@` neutralised, because those strings are not trusted text. The block is at most 8,000
characters; past that the per-stage lines become counts, and it is cut at a whole line, never inside a code span. It is
a record for the reader, not proof: nothing in CI reads it, and the local log stays the source.

### The checks {#checks}

On the generated text it runs `check-title` and `check-body` as the old description gate ran them (a local picture is
accepted where CI wants an uploaded one, because `gh --attach` uploads it), `check-docs` with the changed paths (so
`Docs: none` is honoured), the `TODO:` rule, and the pictures: each reference must be a relative path under `./shots/`
(an absolute path, a drive or `..` is refused, as it would put a folder of the author's machine into the description),
the file under `shots/` next to PR.md, outside any git checkout, one of `pull_requests.images.extensions` (a clip is
refused: `clips publish` is the only video upload), at most `max_bytes` and not byte-identical to another, and there are
at most `max_count`. Its output also holds the hashes a release records: the title, paragraph, pictures and facts
(`--json`).

## pr open {#open}

```sh
uv run --project python coney-tools pr open [TASK] [--dry-run] [--reopen] [--no-auto-merge] [--adopt HEAD] [--adopt-rewrite] [--json]
```

The coordinator's release command, and the only way the tools turn a unit into a pull request: it proves the unit,
rebases it, pushes it, opens (or updates) the pull request, turns on auto-merge and reads the result back. It replaces
the hand-run sequence of fetch, rebase, `docs check`, force push, `gh pr create` and `gh pr merge --auto`. It makes no
commit: what is pushed is exactly what Check and Review saw. Its options and exit codes are in
[the pull request commands](coney-tools-pr.md#pr-open); this section is what it checks and does, in order.

1. **Who and where (exit 2 when not).** The caller is the coordinator or a person: an engineer or reviewer identity is
   refused. `gh` is 2.99 or newer (`--attach`) and logged in. The task has a unit (`unit.json` from `task start`) and a
   `TASK.md`; the worktree exists, is on the unit's branch (not detached, no rebase or merge in progress) and is clean.
   One `pr open` of a task runs at a time (the lock file `pr-open.lock` in the task's folder; a dry run takes no lock).
   The default branch is the one `origin`'s `HEAD` names (as for `pr verify`), fetched, and the sections that steer
   a release ([Where the rules come from](#rules), and so the claims client's bot login and markers) are read from
   `origin/<default branch>` whatever `--remote` is, never from the branch under release. A unit
   that `task start` cut from any other base (a stacked task) is refused: the rules never come from an unmerged branch,
   and no pull request is stacked ([No stacked work](how-we-work.md#working-in-parallel)).
   The branch's own `.github/conventions.yml` is read leniently by `pr open`, `pr verify` and `pr text` (and by `task
   review`): a key the tool does not know is skipped with a `note: ... ignoring <key>` line (a branch that adds a rule is
   read by the tool `main` already has), and a required key the branch lacks (it was cut before `main` added it) takes
   `main`'s value with a `note: ... missing <key>` line, so an older unit needs no rebase to be released. A wrong value
   is still an error ([How it is read](repo-conventions.md#how-it-is-read)). What it skips or fills adds no steering,
   because the sections that decide a release are not the branch's: `pr open`, `pr verify` and `pr text` read them from
   the default branch (the proof inside `pr open` too), and the branch's file is read only for the rest.
2. **The claim (exit 1).** Each issue of the task is claimed by the person running the command (the card is present and
   not released); otherwise the message names the claimant.
3. **The pull request (exit 1).** The branch's pull request is looked up (`gh pr list --head`). Merged: refused.
   Closed and not merged: refused unless `--reopen`. A fix task (`parent:` in its `TASK.md`) is released onto its
   parent's pull request, log and text, and is refused when the parent has no open pull request. With
   [`--adopt <head>`](#adopt) the pull request must exist, no earlier run may have pushed it, and its branch must be at
   that head on the remote and the unit's branch must hold the changes of its commits (exit 2 otherwise, with nothing
   touched; [Taking over](#adopt)).
4. **The proof (exit 1).** [`pr verify`](#verify) and [`pr text`](#text) run on the unit as it is (`pr_ready.prove`,
   which Conclave's [`unit-ready` wake](conclave.md#ready) asks too), so a doomed run costs seconds and leaves the
   branch alone. Every problem is listed under the stage to redo; the unit goes back to its
   engineer, never to the coordinator's hands.
5. **Rebase.** `git rebase <remote>/<default>`, linear. Up to date: nothing happens. A conflict is aborted, so the
   worktree is as it was; the paths are written to `REBASE.md` in the task's folder and the exit is 3. The engineer
   resolves, and the hashes say which stamps that made stale. The rebase is linear whatever the machine's git
   configuration says (`--no-rebase-merges`, no autosquash). A git that fails before it starts a rebase, or a rebase
   that stops with no path left unmerged (no committer identity, a hook), is exit 4 with git's message, not a conflict;
   the second is aborted too. After a clean rebase the proof runs again on the new tip; a stamp that became stale is
   listed like any refusal (the branch stays rebased locally, exit 1). For a fix task the number of files changed since
   the last release is printed (`fix touches 3 files since the last release`), so the reviewer can judge its scope.
6. **The stamp.** One `pr-opened` stamp is appended: the tip, the base, the change, title, paragraph, facts and pictures
   hashes, the engineer. When the newest stamp of the log is already a `pr-opened` that covers the same things, it is
   reused, so a rerun appends nothing.
7. **Push.** `git push --force-with-lease=<branch>:<expected> <remote> <branch>`, where `<expected>` is the remote tip
   the previous run recorded in `pr-published.json` (empty on the first push, which then fails if the branch exists;
   with `--adopt`, the head given). A
   bare `--force-with-lease` and `--force` are never used. A branch someone else moved is refused (exit 4). A remote
   already at the tip is not pushed to, and a branch that is gone from the remote (GitHub deleted it after the pull
   request was closed) is expected absent, so `--reopen` pushes it again.
8. **Create or update.** `gh pr create` (or `gh pr edit`, after a reopen when asked) with the generated title and
   description and one `--attach` per picture, run in the folder of the engineer's file so `./shots/x.png` is the path
   `gh` matches. The title, the body and `pr-published.json` are kept in the task's folder, whatever file `--text`
   names. On an update only pictures that are new are attached; an unchanged one keeps the address of its upload from
   `pr-published.json`. Then `gh pr merge --squash --auto`, unless `--no-auto-merge` is given, `merge.auto_merge` is
   off, the change touches a path of `merge.manual_paths` (`.github/conventions.yml`, `AGENTS.md`,
   `.claude/settings.json`, `.claude/hooks/`: the maintainer merges those, and the message says so) or the viewer's
   permission on the repository is not admin, maintain or write (a fork's owner has none: the pull request opens
   without it).
9. **Read back.** The title, the head commit, the pictures (as many addresses as pictures), the marked blocks, the
   absence of any local path and auto-merge are read from GitHub. A wrong description is repaired once with `gh pr
   edit`; a second failure is exit 4 with the pull request's address (it exists, and rerunning repairs it).
   Auto-merge that is on when this release should leave it off (a fix round that now touches a `manual_paths` file,
   `--no-auto-merge`, a frozen `merge.auto_merge`, or a hand-set one) is turned off with `gh pr merge --disable-auto`.
   For a pull request that already exists this happens before the push (step 7), so a failed step later cannot leave
   the new tip to merge by itself; if it stays on, the run stops with exit 4 before pushing anything.
10. **Record.** `/progress pr-opened` is posted once on each issue (the claim Action then shows `PR #N open`),
    `pr-published.json` is written (number, address, pushed tip, picture hashes to addresses, the engineer, the
    time) and one line is printed: `opened #812 <url> (task ..., engineer ..., auto-merge on)`. `--json` prints the
    same as one object: `ok`, `exit`, `task`, `dry_run`, `message`, `refusals`, `log`, `pr`, `tip`, `stamp`,
    `auto_merge`.

`--dry-run` does steps 1 to 5 in a throwaway `git worktree add --detach` and says what 6 to 10 would do (open or
update the pull request that exists, with or without auto-merge): nothing in the unit's folder, the log, git or GitHub
changes, not even a lock or `REBASE.md`. It reports a conflict or a refusal as a real run would.

Every step is idempotent, so after exit 4 the coordinator runs the same command again: no second stamp, no second pull
request, no second `/progress`. The facts block is generated from the log without the `pr-opened` stamps, so a rerun
makes identical text. The repository is never written in a file or in the code: `--repo` defaults to what `gh` resolves
for the remote (the parent of a fork), so a contributor's clone releases to the upstream repository. The pull request's
author and the squash commit are the coordinator's account; the engineer is named in the AI use section, the facts
block, the stamp and `pr-published.json`.

What it refuses, besides the proof's refusals above (each message starts with "pr open: "):

| Exit | Message | Redo |
| --- | --- | --- |
| 2 | `only the coordinator opens pull requests; ask it` | none: ask the coordinator |
| 2 | `gh 2.99 or newer is needed (--attach); found 2.97`; `gh is not ready (...)` | none |
| 2 | `<task>: no unit worktree (run task start)`; `a rebase or merge is in progress`; `the worktree is on 'x', the unit's branch is 'y'` | `task start`, or the engineer |
| 2 | `the worktree has uncommitted changes: <first 5 paths>; the engineer commits or removes them` | the engineer |
| 2 | `another pr open of <task> is running`; `cannot fetch <remote> <default>: ...` | wait; none |
| 2 | `<task>: the unit was started from <ref>, not from <remote>/<default>; ...` (a stacked unit) | the work starts again as a unit from the default branch once the branch it builds on has merged |
| 2 | `--adopt: the pull request's branch has N commit(s) whose change the unit's branch lacks (...)` | the engineer cherry-picks them into the unit, or the coordinator adds `--adopt-rewrite` when dropping them is intended |
| 2 | `origin/HEAD is not set here, so the default branch is unknown; run git remote set-head origin --auto` | the coordinator runs the command named |
| 1 | `refused: the claim is not yours to release in <task>`, with the claim's state under it | the coordinator re-claims |
| 1 | `#N already merged this branch; new work is a new task`; `#N was closed; --reopen to continue it`; `<fix> has no open pull request to fix (...)` | a new task; `--reopen`; none |
| 1 | the proof's refusals, each under its stage | the stage named |
| 3 | `rebase onto <remote>/<default> conflicts at <12 digits> in <paths>; the branch is as it was` | the engineer resolves; the stale stamps are redone |
| 4 | `auto-merge on #N stays on and this release must not be merged by it; nothing was pushed` | turn it off on GitHub, rerun |
| 4 | `push: ...` (the lease was refused: fetch the branch and have the engineer rebase onto it); `gh: ...; run pr open again`; `read-back: <url> is wrong: ...`; `git rebase <remote>/<default> stopped with no conflict: <git's message>; aborted, the branch is as it was`; a git error from the rebase on | rerun (after fixing what git names) |

### Taking over a pull request opened by hand {#adopt}

A pull request opened before `pr open` existed, or by hand on a unit's branch, has no `pr-published.json`, so the
first push of `pr open` expects the branch to be absent and is refused (`no earlier pr open pushed it`). Read the head
the branch has on GitHub and name it once:

```sh
gh pr view <N> --json headRefOid --jq .headRefOid
uv run --project python coney-tools pr open <task> --adopt <that 40-digit commit>
```

The head becomes the lease of that one push, so a branch someone moves in between is still refused. Before anything is
touched the run checks that the branch has a pull request, that no earlier `pr open` pushed it and that the remote
branch is at that head (a short id, a ref name or another commit is exit 2). It also checks that the unit's branch holds
everything the head holds: the head is fetched (this moves no local branch) and compared with `git cherry`, by the change
each commit makes, not by ancestry, because a branch rebased or amended past its remote head still holds its changes. A
commit of the head whose change the unit's branch lacks (pushed from elsewhere, or made on GitHub) is exit 2, naming it,
because the push would drop it and the lease would not object: the engineer brings it into the unit first (`git
cherry-pick`), and `--adopt-rewrite` (only with `--adopt`; alone it is exit 2) goes on only when dropping it is
intended. The title, description, pictures and auto-merge are then `pr open`'s own, and `pr-published.json` records
the pull request, so later runs need no `--adopt`.
It needs a unit like any release: a branch with no `unit.json` and no `assigned` and `claimed` stamps cannot be adopted
until `task start --adopt` (or `--adopt-branch`) has made the unit ([Adopting a branch](issues.md#adopting)).

## After the switch {#after-the-switch}

Since 2026-10-27 `pull_requests.open_only` is on and the [PR guard](enforcement.md#pr-guard) refuses the hand-typed
routes in every session. The switch takes effect for a session when the checkout it runs from (the main checkout, for
the coordinator) has the commit that turned it on. What each release need takes now:

| Need | Command | Refused by hand |
| --- | --- | --- |
| Open a pull request for a unit (task record, `unit.json`, stamps) | `pr open <task>` | `gh pr create`, `git push` |
| Update a pull request `pr open` opened: new commits, a new text or new pictures | `pr open <task>` again; a fix round on it: `pr open <issue>-fix` | `git push --force-with-lease`, `gh pr edit` with `--title`, `--body-file` or `--attach` |
| Update a pull request opened by hand on a unit's branch | `pr open <task> --adopt <head>`, once ([above](#adopt)) | the same |
| Pictures | `./shots/` references in `PR.md`; `pr open` attaches the new ones and reads the addresses back | `gh pr create` or `gh pr edit` with `--attach` |
| Bring a branch up to date with `main` | `pr open` rebases it onto `origin/main` | `gh pr update-branch`, `gh api` calls that make, move or delete a ref or commit to the branch, which would move it and make the next lease fail |
| A branch holding merge commits | The engineer merges `origin/main` into the branch as its last commit before Check and Review; a branch that already holds `origin/main` is pushed as it is, merges and all. If `main` moved since, `pr open` rebases linearly (`--no-rebase-merges`); a conflict is exit 3 and the engineer merges `main` again (the stamps bind to the change, so a clean merge leaves them valid) | |
| Auto-merge | `pr open` turns it on, or leaves it off for `merge.manual_paths` and `--no-auto-merge` | `gh pr merge --auto` |
| Merge a pull request whose checks passed, where the maintainer allows the coordinator to | `gh pr merge <N> --squash` (no `--auto`) | |
| Clips | `clips publish --pr <N>` | |
| Labels, comments, reviewers, holds, reruns | `gh pr edit <N> --add-label ...`, `gh pr comment`, `pr hold`, `gh run rerun`, `pr list-stuck --rerun --apply` | |
| A pull request or branch with no unit (released before the task workflow: no `unit.json`, a stamp log that does not start with `assigned` and `claimed`) | `task create` (the coordinator), then `task start <task> --adopt` or `--adopt-branch <branch>` (the engineer): the unit is made on that branch, the old log moved aside, the stages redone; then `pr open <task> --adopt <head>` ([Adopting a branch](issues.md#adopting)) | everything above |
| A pull request stacked on another one | None: `pr open` releases a unit based on the default branch only. Start the work again from `origin/main` once the base has merged, or, for a branch whose base already merged, adopt it as above and rebase it with `git rebase --onto origin/main <old base> <branch>` in the unit's worktree | `git push`, `gh pr edit` of the text |
| A pull request from a fork | Its author pushes and edits it from their own terminal or fork | |

## pr hold {#hold}

```sh
uv run --project python coney-tools pr hold PR [--who NAME] [--hours N] [--cause CAUSE] [--note TEXT]
uv run --project python coney-tools pr hold PR --release
uv run --project python coney-tools pr hold --list [--json]
```

A pull request with a failed check, a conflict, a CI run that never ends or auto-merge off is a problem the
coordinator must see: [Conclave's watchers](conclave.md#watchers) wake it for each one and its stop guard keeps it from
ending a turn on one nobody holds. `pr hold` records that somebody holds the problem, so it is neither told again nor
guarded:

- `pr hold 812 --who engineer-2` says that engineer-2 is fixing #812. Start the agent, then record it.
- `pr hold 812 --note "the runner pool is down"` (no `--who`) is a **waiver**: a known wait, with nobody working on
  it. A waiver needs the note.
- **A conflict is never parked with a bare waiver.** A conflict is work for the fix track, so `pr hold` refuses a
  waiver on the `conflict` cause (`--cause conflict`, or no `--cause` on a pull request GitHub says conflicts now, or
  one whose state it cannot read) unless the note names a maintainer decision (`conclave.watch.conflict_waiver`);
  anything else needs `--who`. What the snapshot does with a conflict, a waiver and the retelling:
  [Conclave](conclave.md#conflicts).
- `--cause checks-failed` (or `conflict`, `ci-stuck`, `auto-merge-off`) covers that cause only; without it the hold
  covers every cause of the pull request.
- `--hours N` sets how long it lasts: `conclave.watch.hold_default_hours` (4) when left out, and no more than
  `hold_max_hours` (24). **A hold ends by itself**: the problem is then open again and told again, so a fix that stalls
  cannot hide. A hold also covers only the head commit it was made on (the command reads it from the pull request
  and stores its first seven digits): a new push is a new problem, so it is not held either, and the coordinator is
  told again of what the new head still fails. A pull request whose head the command cannot read is not held (exit 2).
- `--release` ends the pull request's hold now. `--list` prints the holds that run (`--json`: with their times).

A hold is one small JSON file per pull request in the scratch directory (`conclave.watch.holds_dir`, `pr-holds`),
written whole and replaced by the next hold of the same pull request; `conclave snapshot` reads them and marks the
event `handled`. A file that cannot be read is reported (`holds_problems`) and holds nothing. Only the coordinator
holds: `hooks.toml` refuses the command to the other roles. Exit codes: 0 done, 1 nothing to release, 2 a refused
request (no pull request, a waiver with no note or, on a conflict, with no maintainer decision in it, hours out of
range, `--list` with another option).

## What this does not do {#limits}

These commands guard against mistakes and against agents, not against the person at the machine. `pr open` is the only
release path the tools offer, and the PR guard's switch (`pull_requests.open_only` in `.github/conventions.yml`, on)
refuses the hand-typed ways it recognises of opening, rewriting, auto-merging or moving a pull request and of pushing
this repository ([The PR guard](enforcement.md#pr-guard)); it is a spelling check, so a coordinator determined to skip
`pr open` still can (its "Not covered" list). They also do
not stop a person who can write the files: the stamp log is written only by the stage commands and the hooks keep
agents from writing it, but a person who can write the files can rebuild a chain. An edit made on the GitHub website
is not checked beyond the `title`, `docs` and `body` jobs. The description CI checks is the one that `pr text`
generates; nothing in CI reads the stamps.
