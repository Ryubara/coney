# The coney-tools repository commands

The commands that check the repository's own rules and run the pull request process: [`repo`](#repo) (titles,
descriptions, labels), [`pr`](coney-tools-pr.md#pr) (pull request readiness, text and commit messages) and
[`task`](#task) (claiming issues and writing a task file).
[`issue`](coney-tools-issues.md#issue), [`claim`](coney-tools-issues.md#claim) and [`ci`](coney-tools-ci.md#ci) (the
commands the CI workflows run) have a page of their own. The other groups are listed in [the command line
overview](coney-tools.md#groups).

## repo {#repo}

```sh
uv run --project python coney-tools repo check
uv run --project python coney-tools repo check-title FILE|-
uv run --project python coney-tools repo check-docs [--body FILE]      # changed paths on stdin
uv run --project python coney-tools repo check-body [--body FILE|-] [--title TITLE] [--worktree WT [--base REF]]
    [--local-images]
uv run --project python coney-tools repo review PR.md --worktree WT [--base REF] [--model M] [--effort E]
    [--claude EXE] [--timeout-minutes N]
uv run --project python coney-tools repo check-review PR.md --worktree WT [--base REF]
uv run --project python coney-tools repo review-stamp PR.md --worktree WT [--base REF]
uv run --project python coney-tools repo labels [--apply] [--repo OWNER/NAME]
uv run --project python coney-tools repo render [--check]
uv run --project python coney-tools repo audit [--files] [--github] [--local] [--json] [--only CHECK ...] [--strict]
    [--show N] [--snapshot FILE] [--save-snapshot FILE] [--attachments] [--workspace DIR] [--list]
uv run --project python coney-tools repo pr-health [--json] [--pending-hours H] [--verbose] [--labels] [--rerun] [--apply]
uv run --project python coney-tools repo hygiene (--event N | --sweep) [--apply] [--snapshot FILE]
```

The checks of the repository's own rules, which CI runs on every pull request. Every value they apply (the commit
areas and verbs, the description's sections, the path classes, the labels) is a key of `.github/conventions.yml`
([Repository conventions](repo-conventions.md)); a missing or malformed file is a one-line error and exit code 2.

* `repo check` loads and validates `.github/conventions.yml` and checks that each agent entry file (`CLAUDE.md`,
  `GEMINI.md`) holds the `@AGENTS.md` import line and nothing more. Exit 1 when a pointer file is wrong.
* `repo check-title` checks the first line of a commit message or a title (`-` reads stdin) against `commits.title`:
  `area: Verb the rest`, a known area and verb, the length limit, no trailing period, the breaking suffix last. The
  `title` job of the `pr` workflow runs it on the pull request title.
* `repo check-docs` reads the changed paths on stdin and refuses a change under `paths.code` that has no change under
  `paths.docs` and no `Docs: none` line in the body.
* `repo check-body` checks a pull request description against the template ([What a pull request
  shows](testing.md#pr-evidence)). Given `--worktree`, it also reads the branch's commits (`--base`, default
  `origin/main`, to `HEAD`) and requires the AI use `Model:` line to name every model a `Co-Authored-By` trailer on them
  credits: see [the Model line](#repo-model-line). Given `--title` (the `body` job passes the pull request's title), a
  title area a player sees (`pull_requests.runtime.areas`: `gameplay`, `graphics`, `audio`) must have a line starting
  `Runtime:` in the Testing section: the evidence of a comparison with the original running, with numbers, or `Runtime:
  none (<reason>)` ([the Runtime line](testing.md#runtime-line)). Without `--title` that rule is not checked and the
  output says so; `repo check-review` always has the title (line 1 of `PR.md`).
  The **Issues affected** section must hold one `- #N: how it is affected` item for each open issue the change affects,
  or the single line `None (<reason>)` with any short reason (`None (one-line fix)`); an empty section, a `None` with
  no reason, an item with no line on how, a number listed twice and prose are refused, and the message says what to
  write. Whether the list is complete is the reviewer's judgement;
  [`issue impact`](coney-tools-issues.md#issue-impact) suggests it.
  `--local-images` accepts an image that points at a local file, as in a description that `gh pr create --attach`
  is about to upload; the release guard hook passes it ([The release queue](how-we-work.md#release-queue)).
* `repo review`, `repo check-review` and `repo review-stamp` are the release gate on a description's content: see
  [review](#repo-review). All three refuse a description whose title or body fails the checks above.
* `repo labels` prints the `gh label create` commands for `labels.list`; `--apply` runs them (the coordinator does).
* `repo render` rewrites the generated blocks of `AGENTS.md`, `CONTRIBUTING.md` and every guide under `docs/guides/`
  from the conventions: the text between a `<!-- conventions:begin NAME -->` line and the next
  `<!-- conventions:end -->` line (the commit areas and verbs, the label table, the rules for a pull request
  description, the required checks, the documentation-only paths). Run it after changing `.github/conventions.yml` and
  commit both. `--check` changes nothing and exits 1 naming each file with a stale block, as the `python` workflow does;
  a wrong marker (an unknown block name, an end without a begin, a block never closed) is a one-line error with the
  file and line, exit code 2. [Generated blocks](repo-conventions.md#generated-blocks) lists the blocks.
* `repo audit` reports where the repository departs from the conventions and changes nothing: see [audit](#repo-audit).
* `repo pr-health` lists the open pull requests that are stuck, and with `--rerun` restarts the checks that failed for a
  reason that no longer holds: see [pr-health](#repo-pr-health).
* `repo hygiene` writes the audit's findings back as comments: see [hygiene](#repo-hygiene).

### The Model line and the branch's trailers {#repo-model-line}

A branch whose commits carry a `Co-Authored-By: <model name> <noreply address>` trailer was written with that model, so
the description's `Model:` line must say so. Without this check only the `reviewer` agent caught a line that named
the wrong model, and it cost three review rounds (pull requests #460, #466 and #467).

* **What counts.** A trailer on a commit of `base..HEAD` whose key is `commits.trailers.ai_co_author.key` (any case) and
  whose address is one of `commits.trailers.ai_co_author.noreply`; a person credited as a co-author is not a model. Each
  model counts once (compared without regard to case or runs of spaces).
* **The match.** The model name must appear on the `Model:` line, in any case and anywhere on it (`Claude Opus 5.5` is
  found in `Claude Sonnet 5.5 and claude opus 5.5 (review)`), not as the start of a longer version (`Claude Opus 5.55`).
  A branch with no such trailer needs nothing; a `Model:` line of `n/a` for a human-written change passes as before.
  Whoever the description says wrote the commits, a trailer on them has to be named: `a person` with an AI trailer is
  refused. When the AI use lines are themselves unusable (a missing or empty `Model:` for an AI-written branch) only that
  is reported, not the trailers too.
* **The base must be fresh and right.** `base..HEAD` is what the branch has that the base lacks. With a stale
  `origin/main`, a branch that has merged a newer `main` also counts the commits it merged in, and so their trailers
  (other authors' models) must be named: run `git fetch` and the refusal goes away. A stacked branch
  ([Dependent units are stacked](how-we-work.md#release-queue)) with the default base also counts the parent unit's
  trailers: pass `--base <parent branch>`.
* **The message.** One reason per missing model, naming it:
  `AI use: 'Model:' does not name Claude Opus 5.5, which a Co-Authored-By trailer on the branch's commits credits`.
* **Where it runs.** Locally, where the branch is at hand: `repo check-review`, `repo review` and `repo review-stamp` read
  the commits of the worktree after `--base` (default `origin/main`) and refuse before the agent starts, and
  `repo check-body --worktree WT` does the same for a description on its own. **CI does not run it:** the `body` job
  gets the description from the event and has no commits (its checkout is shallow, and a full history for one string
  comparison would slow a required check on every edit), so it passes no models and stays a description-only check, as
  `Docs: none` is. The coordinator's release step is the gate
  ([The description review](how-we-work.md#pr-review)). A branch git cannot list (an unknown `--base`, not a worktree)
  exits 2 rather than passing as one without trailers.
* **One source.** The trailer's key and addresses are `commits.trailers` and the rule's switch is
  `pull_requests.ai_use.model.names_trailer_models` in [.github/conventions.yml](repo:.github/conventions.yml); the check
  holds no copy, and the generated rules in [CONTRIBUTING.md](repo:CONTRIBUTING.md) state it.

### repo review {#repo-review}

The gate a description passes before the coordinator pushes it ([The description review](how-we-work.md#pr-review)).
`PR.md` is the description file as agents write it: the title on line 1, then the body; image paths in it are relative
to its folder.

* `repo review` runs the `reviewer` agent once, headlessly: it reads `.claude/agents/reviewer.md` from the
  checkout the command runs in, hands it to `claude -p --agents FILE --agent reviewer --model M
  --dangerously-skip-permissions` (a throwaway run, in `PR.md`'s folder) with a task naming `PR.md`, the worktree and
  `--base` (default `origin/main`), and then runs `check-review`. Its task has the agent stamp with the same `--base`.
  It removes an old `REVIEW.json` and `REVIEW.md` first, so a run that writes no verdict cannot leave an earlier pass
  behind. `--model` overrides the agent file's model, `--effort` sets the effort level, `--claude` names the
  executable and `--timeout-minutes` (default 30) stops a run that hangs. Exit 0 when the verdict releases the branch,
  1 when it does not or `claude` failed, 2 when the review cannot run (no description, no agent file, not a worktree).
* `repo check-review` first runs the checks CI runs on the description: `check-title` on `PR.md`'s line 1 and
  `check-body` on the rest (`PR.md`'s convention: title on line 1, body after it), except that a local image counts as an
  upload, since `gh --attach` uploads it when the pull request is opened. Each reason is printed with a `title:` or
  `body:` prefix, together with the verdict's, so a missing template section is caught before the push. It also holds
  the `Model:` line to the `Co-Authored-By` trailers of the commits after `--base` (default `origin/main`), which CI
  cannot see ([the Model line](#repo-model-line)). `repo review` runs the same checks before it starts the agent and
  stops at once when they fail. It then reads `REVIEW.json` next to
  `PR.md` and passes only when the verdict is `pass` with no blocker, its `tip` is the worktree's HEAD, its
  `body_sha256` is `PR.md`'s hash now, and every local image `PR.md` shows is listed with its hash now and differs from
  every other one. Exit 1 lists each reason (a title or body CI would refuse, a stale tip, an edited description, a
  replaced or unreviewed picture, the blockers); 2 when `PR.md` cannot be read.
* `repo review-stamp` prints those values as JSON (`tip`, `body_sha256`, `images` with `path` and `sha256`, `null` for a
  missing file). The agent copies them into its verdict, so the hash is computed in one place. It first runs the same
  title and body checks as `check-review` (one function, `precheck`, including the `Model:` line against the commits
  after `--base`, default `origin/main`) and, when they fail, prints the reasons with their `title:` or `body:` prefix on
  stderr, prints no stamp and exits 1, so a reviewer launched as a subagent cannot review a description that
  `check-review` would refuse at release. Exit 2 when `PR.md` cannot be read or the worktree's commits cannot be listed.
  It takes the rules from the checkout the tool is installed in, so it runs from any folder (the agent runs it in
  the scratch folder).

The file names, the agent and the hash rule are `pull_requests.review` in `.github/conventions.yml`; the rules in full
are the generated list in [The description review](how-we-work.md#pr-review).

### repo audit {#repo-audit}

`repo audit` runs the checks of `hygiene.checks` in `.github/conventions.yml`, in up to three scopes
([the checks](repo-conventions.md#audit-checks)):

* `--files` reads the checkout and needs no network (the default when no scope is given). The `python` workflow runs
  it on every pull request, and `check changed` runs it as its `repo` step. It checks the pointer files, that every
  file the conventions name exists, that the repository's address is only in the README, that the issue forms, the
  pull request template, `.github/scripts/ci-scope.sh` and pre-commit's size limit still agree with the file, and that
  the generated blocks are current.
* `--github` reads issues, pull requests, labels, the repository settings and the ruleset through `gh` (read only;
  `gh` must be signed in) and checks them: the sections of each open issue (a missing one, one left as `x`, headings
  that are not the form's `###` headings in its order, an Area that is not an option), its Screenshots, its labels,
  a closed issue that still carries an `S-` status label, the word "owner" for a person (say "user" for who tested or
  filed it, "maintainer" for who decides), closed issues and merged pull requests that
  keep an unchecked item with no follow-up issue number, open pull requests whose description names a path on your
  machine or an image that is not uploaded, stacked pull requests, how long an item has waited, and drift between the
  labels, settings and ruleset on GitHub and the file. `--save-snapshot FILE` also writes what it read, and
  `--snapshot FILE` audits a saved snapshot instead of reading GitHub (the tests do; so can you, offline). The link
  check needs `origin/main` in the checkout (`git fetch origin`) and says so when it is missing. `--attachments` also
  downloads the `.txt` and `.log` files attached to open issues and scans them for possible game data and personal
  information (`issue.attachment-data`, `issue.attachment-personal`; [Attached logs](issues.md#attachments)); without
  it those two checks do not run and a notice says so.
* `--local` reads the workspace around the checkout: the free space of its drive, merged worktrees and stray files
  among the worktrees (`workspace clean` would remove them), and scratch folders of merged or abandoned branches that
  still hold files taken from the original (save states, traces, dumps). The limits are the `workspace` section of the
  file. It asks GitHub once for the merged pull requests of every branch, judges the worktrees and walks the scratch
  folders several at a time, and does not read the last activity of a worktree it will not report (a `build/` folder
  is `workspace clean`'s business), so a workspace of 80 worktrees takes seconds, not minutes.

Each finding has the check's id, a severity (`error` or `warn`), the item (`#201`, a file, a worktree), a message and a
fix class: `auto` (a coney-tools command fixes it), `skill` (a person or the `/repo-hygiene` skill fixes it with
judgement) or `maintainer` (a maintainer decides). The text report groups findings by check and prints the counts;
`--show N` limits the findings printed per check (0 prints all). `--json` prints one object with `errors`,
`warnings`, `checks` (the count per check that ran), `notices` (what could not be read) and `findings`, each with
`check`, `keys` (the conventions keys it enforces), `severity`, `item`, `url`, `message` and `fix`. `--only` takes a
check id or a group (`issue`) and may be repeated. Exit code 1 when a finding has severity `error` (any finding with
`--strict`), 2 when `gh` or the snapshot cannot be read or the conventions are unusable, otherwise 0. The audit never
edits an issue, a pull request or a label; fixing the findings is the coordinator's job.

### repo pr-health {#repo-pr-health}

`repo pr-health` answers "which open pull requests need someone to act?" with one read of the open pull requests and
their checks (`gh pr list --json`) and one of the branch ruleset (`gh api repos/{owner}/{repo}/rules/branches/main`,
which names the required checks, so nothing is hard-coded). It prints one line per cause, sorted by pull request
number, and changes nothing (except `--rerun --apply`, below):

```text
#7 fix-bat-cross checks-failed lint: body, markdownlint -> author
#9 research-x checks-failed code: python-ok; lint: markdownlint -> author
#12 throw-arc conflict with main -> author
#15 char-stats ci-stuck build-ok 4.2 h (limit 3.0 h) -> coordinator
#18 workouts-2 auto-merge-off into main -> coordinator
```

| Cause | When | Acts |
| --- | --- | --- |
| `checks-failed` | a **required** check failed (failure, timeout, cancelled). Its class is `lint` for the checks in `hygiene.pr_health.lint_checks` (title, body, docs, format, mkdocs, markdownlint, actionlint) and `code` for the others (`build-ok`, `python-ok`); a mix lists both | author |
| `conflict` | GitHub says the pull request conflicts with its base | author |
| `ci-stuck` | a required check has been queued or running longer than `hygiene.thresholds.pr_pending_hours` (default 3; `--pending-hours H` overrides it for one run) | coordinator |
| `auto-merge-off` | the pull request is not a draft, targets `main` and has auto-merge off | coordinator |

A failing check that is not required is ignored. A draft is never stuck. A stacked pull request (base other than
`main`) is reported as `stacked` and never as lacking auto-merge; its failures and conflicts count. GitHub answers
`UNKNOWN` for mergeability while it computes it, which means "not known yet": it is not a finding and shows only with
`--verbose`, together with the drafts and stacked pull requests, so a rerun a minute later settles it.
`--json` prints `checked`, `stuck` (each with `number`, `branch`, `cause`, `kind`, `detail`, `who`, `checks`, `url`),
`other` (the `--verbose` lines), `notices` and `labels` (see below). Exit code 0 when nothing is stuck, 1 when something
is, 2 when `gh` or the conventions cannot be read. Run it from the coordinator's loop to find the pull requests to
unstick or to push.

`--labels` also compares the labels on every open pull request with its causes and prints what would change, after the
findings: `add S-conflict to #12 (conflict with main)`, `remove S-failing from #7 (cleared)` (`would add ...` until
`--labels --apply`). `hygiene.pr_health.labels` in `.github/conventions.yml` maps the causes to labels (`conflict` to
`S-conflict`, `checks-failed` to `S-failing`); a pull request with the cause gets the label, one without it loses the
label (a draft is never stuck, so it carries neither), and no other label is ever added or removed.
`--labels --apply` makes the
changes through `gh api` on the issue labels endpoints and needs a token that may write issues; the exit code is still
the findings' (the `hygiene` workflow accepts 1). While GitHub answers `UNKNOWN` for a pull request's mergeability the
command rereads up to `hygiene.pr_health.unknown_retries` times, `unknown_wait_seconds` apart, and a pull request still
unknown keeps its conflict label as it is. The labels must exist in the repository with their colours: `repo labels
--apply` creates them from `labels.list`.

**`--rerun`** restarts CI that failed for a reason that no longer holds, so a stuck pull request does not wait for a
person to notice. It adds a line per run, `#<n> <branch> rerun <checks> (run <id>): <why> -> <outcome>`, and in
`--json` a `rerun` list (an empty list when there is nothing to rerun; the `stuck` list and the exit code are the same
with or without `--rerun`, so exit code 1 still means a pull request is stuck). Without `--apply` it is a dry run that
prints `would rerun` and changes nothing; `--apply` calls `gh run rerun <run> --failed`. It reads the
head commit and description of each pull request and, for a failed required check, the runs of the head commit and
sometimes its job log. A required check is judged stale in two ways, and a real lint, build or test failure is never
restarted:

| Stale because | How it is known | What happens |
| --- | --- | --- |
| the text was fixed | the `title` or `body` check failed and the pull request's **current** title or description passes `repo check-title` or `check-body` | status `new-event`: the report says to edit the title or description; no rerun is made |
| the runner or GitHub failed | the check concluded `CANCELLED`, `STARTUP_FAILURE` or `TIMED_OUT`, or it failed and its log matches a pattern of `hygiene.pr_health.rerun.log_patterns` (the runner was lost or shut down, an action could not be downloaded) | `would rerun` / `reran` |

Each `rerun` entry is one run, with these fields (stable; scripts such as the PR guard read them):

| Field | Type | Meaning |
| --- | --- | --- |
| `number` | integer | The pull request's number |
| `branch` | string | Its head branch |
| `run` | integer | The workflow run (an id of `gh run list`) holding the stale checks |
| `checks` | list of strings | The stale required checks of that run, sorted |
| `reasons` | list of strings | Why each is stale, in the order of `checks` |
| `status` | string | `would-rerun` (dry run), `reran`, `refused` (GitHub would not), `capped` (already rerun `max_per_sha` times) or `new-event` (a fixed title or description) |
| `detail` | string | Empty for `would-rerun` and `reran`; otherwise what happened and the new event the pull request needs |

A rerun reuses the event the run started from, with the title and description of that time, so it cannot make `title`
or `body` see a fix (observed: a rerun made after a description was fixed failed on the old text). A fixed text needs a
new event instead (an edit of the title or description, or closing and reopening the pull request), which the report
names; it is not done automatically because reopening changes the pull request's state.

The limits are in `hygiene.pr_health.rerun` of the conventions file: `max_per_sha` (a workflow is rerun at most that
many times on one head commit, counted from the attempts GitHub keeps for its runs, so restarting the tool loses
nothing; at the cap the status is `capped` and the line says to push a commit), the names of the title and
description checks, the conclusions and the log patterns. A run is restarted with `--failed` only when all its failed
jobs are stale; when it also holds a real failure, only the stale jobs are restarted (`--job`). When GitHub refuses
(`gh run rerun` fails, for example "cannot be retried"), the status is `refused` and the line carries GitHub's message
and the new event the pull request needs: push a commit for a runner failure. The reads are `gh run list --commit`
and `gh api repos/{owner}/{repo}/actions/jobs/<id>/logs`; only `--apply` writes.

### repo hygiene {#repo-hygiene}

`repo hygiene` is what the `hygiene` workflow runs ([Workflows](repo-conventions.md#workflows)); you can run it too.
It audits GitHub (`repo audit --github`, plus `--files` for a sweep) and writes the result back as comments, with the
settings in `hygiene.comments` of `.github/conventions.yml`:

* `--event N` is for issue or pull request `N` that just changed. It creates the item's one comment when it has
  findings, edits it when they change, and edits it to "found nothing to fix here now" when they are gone. A comment is
  never deleted. Findings of the checks in `hygiene.comments.report_only` (the idleness checks) are left out, because a
  comment is activity and would end the idleness it reports.
* `--sweep` is the weekly run. It keeps one report comment (counts per check, then the findings) on the open issue
  titled `hygiene.report_issue.title`, creating that issue (with the report issue's labels and a task-form body) when
  it is missing. Item comments are written by a sweep only when `hygiene.comments.on_sweep` is true, and then at most
  `max_per_sweep` of them, resolves first; it starts false, so the sweep only reports and item comments come from
  events.
* Both scan attached logs, as `repo audit --attachments` does ([Attached logs](issues.md#attachments)): an event the
  logs of its own issue, a sweep those of every open issue, within `hygiene.attachments.max_per_run` downloads. A
  download that fails is a notice (printed, and in the report), not a finding.
* Without `--apply` nothing is written: it prints `would create comment on #7: ...` for each write. `--snapshot FILE`
  audits a saved snapshot ([audit](#repo-audit)); the comments are still read from GitHub.

Only a comment by `hygiene.comments.author` (the Actions bot) whose first line is `<!-- MARKER -->` (the report's is
`<!-- MARKER:report -->`) counts as the workflow's own, so pasting the marker never makes it edit someone else's
comment. Text from GitHub quoted in a comment (a title in a finding) cannot mention anyone, link, embed an image or
close the marker, and the report writes `#12` so that it makes no cross-reference in the item's timeline. Exit code 0
when it ran, 2 when `gh` or the conventions cannot be read.

<!-- command-reference:begin repo -->
## Parameters of `repo` {#repo-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `repo check` {#repo-check-usage}

Check .github/conventions.yml and the agent pointer files.

```text
coney-tools repo check
```

Takes no parameters.

### `repo check-title` {#repo-check-title-usage}

Check a commit or pull request title against the commit title rules.

```text
coney-tools repo check-title FILE
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `FILE` | text | required | a file whose first line (not blank, not #) is the title; - for stdin |

### `repo check-docs` {#repo-check-docs-usage}

Check that a pull request which changes code also changes the docs (changed paths on stdin).

```text
coney-tools repo check-docs [--body BODY]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--body BODY` | text | none | a file holding the pull request description (for `Docs: none`) |

### `repo check-body` {#repo-check-body-usage}

Check a pull request description against the template: sections, summary, AI use.

```text
coney-tools repo check-body [--body BODY] [--title TITLE] [--worktree WORKTREE] [--base BASE] [--local-images]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--body BODY` | text | none | a file holding the pull request description; - or omitted reads stdin |
| `--title TITLE` | text | none | the pull request's title: a title area a player sees (gameplay, graphics, audio) then needs the Testing section's `Runtime:` line, as in CI; without it that rule is not checked |
| `--worktree WORKTREE` | path | none | the branch's worktree: also require the Model line to name every model its commits credit in a Co-Authored-By trailer (what `repo check-review` does) |
| `--base BASE` | text | `origin/main` | what the branch is compared with, with --worktree (default: origin/main) |
| `--local-images` | flag | off | accept an image that points at a local file, which `gh pr create --attach` uploads |

### `repo render` {#repo-render-usage}

Rewrite the generated blocks of AGENTS.md, CONTRIBUTING.md and the guides from .github/conventions.yml.

```text
coney-tools repo render [--check]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--check` | flag | off | change nothing; exit 1 when a block is out of date |

### `repo audit` {#repo-audit-usage}

Report where the repository departs from .github/conventions.yml (read only).

```text
coney-tools repo audit [--files] [--github] [--local] [--json] [--only CHECK] [--strict] [--show N] [--snapshot FILE]
    [--attachments] [--save-snapshot FILE] [--workspace WORKSPACE] [--list]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--files` | flag | off | check the checkout (the default when no scope is given) |
| `--github` | flag | off | check issues, pull requests, labels and settings (gh) |
| `--local` | flag | off | check the workspace: disk, worktrees, scratch folders |
| `--json` | flag | off | print the report as JSON instead of text |
| `--only CHECK` | text | none | run only this check or group (issue). Repeatable. |
| `--strict` | flag | off | exit 1 on a warning too |
| `--show N` | integer | `10` | findings printed per check; 0 for all (text only) |
| `--snapshot FILE` | path | none | with --github: read this saved snapshot, not GitHub |
| `--attachments` | flag | off | with --github: download the logs attached to open issues and scan them (hygiene.attachments) |
| `--save-snapshot FILE` | path | none | with --github: also write the snapshot read |
| `--workspace WORKSPACE` | path | none | with --local: the workspace folder (default: found from the checkout) |
| `--list` | flag | off | list the checks and exit |

### `repo pr-health` {#repo-pr-health-usage}

List the open pull requests that are stuck (failed required check, conflict, stuck CI, no auto-merge).

```text
coney-tools repo pr-health [--json] [--pending-hours H] [--verbose] [--labels] [--rerun] [--apply]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--json` | flag | off | print the report as JSON instead of text |
| `--pending-hours H` | number | none | hours a required check may be pending (default: hygiene.thresholds.pr_pending_hours) |
| `--verbose` | flag | off | also show drafts, stacked pull requests and unknown mergeability |
| `--labels` | flag | off | also list the labels to add to or remove from the open pull requests (hygiene.pr_health.labels) |
| `--rerun` | flag | off | also list the failed required checks that are stale (text fixed since, runner lost) and would be rerun |
| `--apply` | flag | off | with --labels: make those label changes through gh; with --rerun: really rerun the checks (gh run rerun) |

### `repo review` {#repo-review-usage}

Run the reviewer agent headlessly on a description, then check its verdict.

```text
coney-tools repo review PR_FILE --worktree WORKTREE [--base BASE] [--claude CLAUDE] [--model MODEL] [--effort EFFORT]
    [--timeout-minutes TIMEOUT_MINUTES]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `PR_FILE` | path | required | the description file: the title on line 1, then the body (PR.md in the scratch folder) |
| `--worktree WORKTREE` | path | required | the branch's worktree (its HEAD is the tip the verdict must quote) |
| `--base BASE` | text | `origin/main` | what the branch is compared with, for the diff and the commits' trailers (default: origin/main) |
| `--claude CLAUDE` | text | `claude` | the Claude Code executable (default: claude) |
| `--model MODEL` | text | none | the model (default: the agent file's) |
| `--effort EFFORT` | choice | none | the effort level. One of `low`, `medium`, `high`, `xhigh`, `max`. |
| `--timeout-minutes TIMEOUT_MINUTES` | number | `30` | stop the review after this long |

### `repo check-review` {#repo-check-review-usage}

Check the title and body as CI does, and that the reviewer's verdict passes and is current (tip, description and images
unchanged).

```text
coney-tools repo check-review PR_FILE --worktree WORKTREE [--base BASE]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `PR_FILE` | path | required | the description file: the title on line 1, then the body (PR.md in the scratch folder) |
| `--worktree WORKTREE` | path | required | the branch's worktree (its HEAD is the tip the verdict must quote) |
| `--base BASE` | text | `origin/main` | what the branch is compared with, for the diff and the commits' trailers (default: origin/main) |

### `repo review-stamp` {#repo-review-stamp-usage}

Print, as JSON, the tip, description hash and image hashes a verdict must quote; refuses, with the title and body
reasons, a description check-review would refuse.

```text
coney-tools repo review-stamp PR_FILE --worktree WORKTREE [--base BASE]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `PR_FILE` | path | required | the description file: the title on line 1, then the body (PR.md in the scratch folder) |
| `--worktree WORKTREE` | path | required | the branch's worktree (its HEAD is the tip the verdict must quote) |
| `--base BASE` | text | `origin/main` | what the branch is compared with, for the diff and the commits' trailers (default: origin/main) |

### `repo hygiene` {#repo-hygiene-usage}

Write the audit's findings as comments on issues and pull requests (what the hygiene workflow runs).

Exactly one of `--event`, `--sweep` is required.

```text
coney-tools repo hygiene (--event N | --sweep) [--apply] [--snapshot FILE]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--event N` | integer | none | issue or pull request N changed: update its comment. Not with `--sweep`. |
| `--sweep` | flag | off | the weekly run: update the report (and item comments if on). Not with `--event`. |
| `--apply` | flag | off | write the comments (default: print the plan only) |
| `--snapshot FILE` | path | none | read this saved audit snapshot, not GitHub |

### `repo labels` {#repo-labels-usage}

Print the gh commands for the issue labels of .github/conventions.yml, or sync them with --apply.

```text
coney-tools repo labels [--apply] [--repo REPO]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--apply` | flag | off | create or update the labels through gh now |
| `--repo REPO` | text | none | OWNER/NAME to sync; default: the repository gh finds from the checkout |
<!-- command-reference:end -->

## task {#task}

```sh
uv run --project python coney-tools task create ISSUE [ISSUE ...] [--terms TERM ...] [--scope "a, b"] [--kind K]
    [--name ID] [--resume] [--dry-run]
uv run --project python coney-tools task create --fix-of ISSUE [--terms TERM ...] [--name ID] [--dry-run]
uv run --project python coney-tools task start [TASK] [--base REF] [--remote NAME] [--no-fetch] [--dry-run]
uv run --project python coney-tools task slots [--json]
uv run --project python coney-tools task slots --admit [--agent-type TYPE] [--task ID | --brief FILE|-] [--reserve]
uv run --project python coney-tools task leads --terms TERM ... [--issue N] [--refresh]
uv run --project python coney-tools task index [--no-gh]
uv run --project python coney-tools task check [TASK] [--worktree DIR] [--base REF] [--agent ID] [--profile P]
    [--keep-going] [--dry-run] [--max-wait MIN]
uv run --project python coney-tools task review [TASK] [--text] [--record | --run] [--worktree DIR] [--base REF]
    [--pr-file FILE] [--claude CMD] [--model M] [--effort E] [--timeout-minutes N]
```

The coordinator's triage step ([Task files](issues.md#task-files)); the rules are `claims` and `tasks` of
`.github/conventions.yml`.

* `create` claims every issue through the claim Action and writes `<scratch>/<task id>/TASK.md` only after the claim
  held. List the issues first, then the options (`--terms` takes several words). It checks the claim locally with the
  Action's own function (ready status, `P-critical` first, blockers, your limit of open tasks), warns when `--scope`
  overlaps another held claim, posts `/claim scope: ... task: <id>` on each issue and waits for the card (at most 5
  minutes; a refusal reply ends it with the reason), settles for 60 seconds (the card still names you, you are the only
  assignee, no earlier `/claim` waits) and then writes the file and posts `/progress assigned`. Anything that goes wrong
  before the file is written leaves no file; a claim that landed late is continued with `--resume`, which posts nothing.
  The task id is `--name` or `<first issue>-<words of its title>`; `--kind` is one of `tasks.kinds` (default from the
  labels). A fix task is `--fix-of ISSUE`: it reuses your claim, posts `/progress fix` and writes `<issue>-fix`.
  `--dry-run` posts and writes nothing and prints the file. Exit 1 when it stopped, 0 when the file was written.
* `start` is the engineer's first step for a task `create` made ([Starting a task](issues.md#starting)). It checks that
  the claim is still yours for this task, that you are within your limit of open claims and that the branch and folder
  are free and its stamp log is usable, then makes the branch (named like the task id) and its worktree, at the place
  the `workspace.worktrees` setting gives for that branch ([Worktrees](workspace.md#worktrees)), from the tip of the
  default branch, writes the unit record `<scratch>/<task>/unit.json`, stamps `assigned` and `claimed` in the task's
  log and posts `/progress claimed`. It prints the branch, worktree, the stamps written and `slots: N of L task slots
  in use`; a start over the limit (`tasks.start.slots`) is refused while `tasks.start.slots.enforce` is on, and a task
  whose issue carries the `workflow` label now (read from its held claim card) holds no slot while
  `tasks.start.slots.exempt_workflow` is on. Nothing is made when a check fails (exit 1); a git or fetch
  failure stops it with exit 2; running it again for a started task finishes what is missing and changes nothing else.
  A fix task opens its parent's unit instead of making one. The coordinator's role may not run it
  (`.claude/hooks/hooks.toml`).
* `slots` shows the [engineer slots](agents.md#slots): the units holding each of the task and fix slots (in work,
  with a record that cannot be read, or reserved for an agent that has not run `task start` yet), the workflow
  units that hold none (read from their issues' claim cards on GitHub; a unit whose cards cannot be read is counted
  and named), the fix tasks waiting (oldest first) and the worktrees no unit record names, which no slot counts;
  `--json` prints the same as data. `--admit` is the check the coordinator's Agent guard runs
  (`.claude/hooks/slot_guard.py`). For an `--agent-type` that holds a slot (`engineer`, `tooling`) it needs the
  task from `--task` or from the `Task id: <id>` line of the brief (`--brief FILE`, or `-` for standard input), the
  task file, a claim that is held now (one bounded read of the claim cards of each issue from GitHub) and a free
  slot; it exits 0 when the agent may start, **3 with the reasons when it is refused**, and any other code (a
  crash is 1, a bad option or a slot lock it cannot have 2) when it could not judge. A GitHub that cannot be read
  leaves only the claim unchecked: the slot is counted as usual (no unit counts as a workflow unit then), and a start
  let through prints a `warning:` line, which the guard logs. `--reserve` keeps the slot
  for the agent for `tasks.start.slots.reserve_minutes` or until its `task start`. Other types pass without a line.
* `leads` runs the leads search alone: one line per match with the source, the place and why it matched, then the terms
  that matched nothing. `--issue N` also matches the addresses, names, paths and references that issue writes.
* `index` rebuilds the cached index (`<scratch>/leads/index.json`, also rebuilt when `HEAD` moved or after
  `tasks.leads.index_max_age_hours`); `--no-gh` leaves the merged pull requests out. The sources, name splitting and
  ranking are in [Task files](issues.md#task-files).
* `check` ([below](#task-check)) runs everything a change must pass on this machine and writes the `checked` stamp.
* `review` is the review stage of a task: the tool part and the packet, the reviewer's verdict recorded as a `reviewed`
  stamp, or both with a headless reviewer. What it checks, the files, the verdict and the stamp are in [Task
  review](review/task.md); every option is in [its command table](review/task.md#command). Exit 0 when the reviewer
  may start or a pass was recorded, 1 when it refused or recorded a fail, 2 when it cannot run.

Every argument and option:

| Command | Argument or option | Default | What it does |
| --- | --- | --- | --- |
| `create` | `ISSUE ...` | none | The issue numbers of the task, one to `claims.limits.issues_per_task` of them; written before the options. Optional with `--fix-of` |
| `create` | `--terms TERM ...` | none | One or more names to search the indexes for (a name, a function, a player's word); each becomes part of "Where to look" or of "Searched, not related" |
| `create` | `--scope TEXT` | none | The planned folders, files and original addresses, comma separated (`"src/ai/, 0x00322b78"`), at most `claims.scope_max_characters` characters; it goes into the `/claim` comment and the task file, and is compared with the other held claims |
| `create` | `--kind K` | from the labels: `docs` for the `docs` label, `tooling` for `A-tools`, else `feature` | The task kind, one of `tasks.kinds`; `fix` is only for `--fix-of` |
| `create` | `--name ID` | `<first issue>-<words of its title>` (a resumed claim: the task id on its card) | The task id and the name of the scratch folder; lower case letters, digits and hyphens, 60 at most. It is the `task:` of the claim, so issues of one task share it |
| `create` | `--fix-of ISSUE` | none | Make a fix task for an issue whose claim you hold: no `/claim`, no limit; posts `/progress fix` and writes `<issue>-fix` (`-2`, `-3` for the next ones) |
| `create` | `--resume` | off | Continue a claim of yours that landed after the wait ended: issues you hold are not claimed again, and the task id must match the one on their cards |
| `create` | `--dry-run` | off | Check, search and print the claims it would post and the file it would write; post and write nothing |
| `start` | `TASK` | `CONEY_TASK` | The task id, the name of its scratch folder; without it and without `CONEY_TASK` the command stops (it does not guess from the branch: it is run from the main checkout, which is on `main`) |
| `start` | `--base REF` | `<remote>/<default branch>` (`repo.default_branch`) | What the branch starts from: any commit, branch or remote-tracking ref. A `<remote>/<branch>` ref is fetched first |
| `start` | `--remote NAME` | `origin` | The git remote that is fetched and that names the default base |
| `start` | `--no-fetch` | off (fetch) | Start from the base as it is in this checkout, for example without network |
| `start` | `--dry-run` | off | Run every check, print the branch, base, worktree and stamps that would be made (for a task already started: what would be finished), and change nothing: no branch, no files, no stamps and no comment |
| `leads` | `--terms TERM ...` | required | The names to search for |
| `leads` | `--issue N` | none | Also match the addresses, code names, repository paths and `#N` references that issue writes |
| `leads` | `--refresh` | off | Rebuild the cached index before searching |
| `index` | `--no-gh` | off | Leave the merged pull requests out of the index (no `gh` call) |

### task check {#task-check}

The local gate that stamps. Before a branch becomes a pull request, `task check` runs here what the change must pass
and writes one `checked` stamp ([Stamps](stamps.md#api)), `pass` or `fail`, bound to the change (`diff_sha256`), so a
clean rebase keeps it and any edit makes it stale. What a change needs follows from its paths: the profile is the first
of `proof.profiles` in [.github/conventions.yml](repo:.github/conventions.yml) whose `classes` hold the CI scope class
of every changed path. The profiles and path classes are read from the **base's** conventions file (`--base`), so a
branch cannot shrink its own profile by editing them. Only the base's `proof` section and the scope lists of `paths` are
read, so a base that gained other keys is still read; the stamp's `detail.rules` says `base`. Only a base with no `proof`
section at all (while this command is not merged) gets the branch's file (`rules: branch`), said so; a base whose rules
cannot be read ends the run with exit 2.

| Profile | For | Checks (in this order) |
| --- | --- | --- |
| `docs` | only documentation paths | `docs-check` (`coney-tools docs check --base`), `mkdocs` (`mkdocs build --strict`), `pre-commit` (all files), `actionlint` |
| `tooling` | documentation and tooling paths (`python/`, `.claude/`, `.github/ISSUE_TEMPLATE/`, `python.yml`, ...; `paths.tools_only`) | the above and `ruff` (check and format), `mypy` (strict), `pytest` |
| `full` | anything else, including a path in no class (`.github/scripts/`) | the above and `msvc-ci-build` and `ctest-full-disc` |

`actionlint` runs only when a changed path is under `.github/workflows/` or `.github/actions/` (`proof.only_when`).
The build checks run `coney-tools build --preset ci --test` once, with the whole suite and the disc, and read the build
record it leaves ([The build record](stamps.md#build-record)): one answers whether it built (MSVC with /WX), the other
whether the suite ran whole with no disc test skipped. The ruff and mypy commands are the ones of `check changed`; there
is no clang-tidy here: clang-tidy is CI's alone, on the changed lines of every pull request and on the whole
codebase nightly ([building](clang-tidy.md)).

* A fault of the machine is not a verdict about the change: a program that is missing, no disc for a `full` run
  (including a `CONEY_DISC` that names nothing), a worktree that changed during the run, a build that got no slot.
  These write no stamp.
* Only a profile with a build check needs `--agent` (or a hook identity or default id): the others take no build slot.
* A `fail` stamp names the checks that failed in `detail.failed`; fix, commit and run again.
* The stamp covers `diff_sha256`, `checks_sha256` (the checks that ran) and, for `full`, `build_record_sha256` (also
  when the build failed, if it left a record). Its `detail` holds the profile, the checks, the test counts, the preset
  and the minutes.
* A `full` run can take an hour. Start it with `run_in_background`, write STATUS.md and stop; it has no budget cap and
  waits for a slot up to `--max-wait`.

Exit 0 every check passed and the stamp is written; 1 a check failed (a `fail` stamp is written); 2 it cannot run or
cannot stamp (nothing is written); 75 the build gave up waiting for a slot (nothing is written; run it again).

<!-- command-reference:begin task -->
## Parameters of `task` {#task-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `task create` {#task-create-usage}

Claim the issues through the claim Action, wait until the claim holds, write \<scratch\>/\<task\>/TASK.md.

```text
coney-tools task create [ISSUE ...] [--terms TERM [TERM ...]] [--scope SCOPE] [--kind KIND] [--name NAME]
    [--fix-of ISSUE] [--resume] [--dry-run]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[ISSUE ...]` | integer | none | the issues of the task (before the options) |
| `--terms TERM [TERM ...]` | text | none | names to search the indexes for |
| `--scope SCOPE` | text | none | the planned folders, files and addresses, comma separated |
| `--kind KIND` | text | none | the task kind (default: from the labels) |
| `--name NAME` | text | none | the task id (default: \<first issue\>-\<words of its title\>) |
| `--fix-of ISSUE` | integer | none | a fix task: reuse your claim on this issue |
| `--resume` | flag | off | continue a claim of yours that landed late |
| `--dry-run` | flag | off | check and print; post and write nothing |

### `task start` {#task-start-usage}

Make the branch, worktree and unit record of a claimed task and stamp assigned and claimed.

```text
coney-tools task start [TASK] [--base REF] [--remote REMOTE] [--no-fetch] [--dry-run]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[TASK]` | text | none | the task id (default: CONEY_TASK) |
| `--base REF` | text | none | what the branch starts from (default: \<remote\>/\<default branch\>) |
| `--remote REMOTE` | text | `origin` | the git remote to fetch from (default: origin) |
| `--no-fetch` | flag | off | start from the base as it is here, without fetching |
| `--dry-run` | flag | off | check and print; make, stamp and post nothing |

### `task slots` {#task-slots-usage}

Show the engineer slots, or check that an agent may start (the coordinator's Agent guard).

```text
coney-tools task slots [--json] [--admit] [--agent-type AGENT_TYPE] [--task TASK] [--brief FILE] [--reserve]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--json` | flag | off | print the slots as JSON |
| `--admit` | flag | off | check a start instead of listing: exit 0 when it may, 3 with the reasons when it is refused (any other code: it could not judge) |
| `--agent-type AGENT_TYPE` | text | `engineer` | with --admit: the subagent type (default: engineer) |
| `--task TASK` | text | none | with --admit: the task id (default: the `Task id:` line of the brief) |
| `--brief FILE` | text | none | with --admit: the agent's brief, a file or - for standard input |
| `--reserve` | flag | off | with --admit: keep the slot for the agent until its task start |

### `task leads` {#task-leads-usage}

Search the indexes for the terms (no claim, nothing written).

```text
coney-tools task leads --terms TERM [TERM ...] [--issue N] [--refresh]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--terms TERM [TERM ...]` | text | required | names to search for |
| `--issue N` | integer | none | also match the identifiers this issue writes |
| `--refresh` | flag | off | rebuild the index first |

### `task index` {#task-index-usage}

Rebuild the cached leads index.

```text
coney-tools task index [--no-gh]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--no-gh` | flag | off | leave the merged pull requests out |

### `task check` {#task-check-usage}

Run every check the change needs (MSVC ci build and the full suite with the disc for engine changes) and stamp the
result, bound to the change.

```text
coney-tools task check [TASK] [--worktree WORKTREE] [--base BASE] [--agent AGENT] [--profile PROFILE] [--keep-going]
    [--dry-run] [--max-wait MIN]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[TASK]` | text | none | the task (default: CONEY_TASK, else the branch) |
| `--worktree WORKTREE` | path | none | the task's worktree (default: the current checkout) |
| `--base BASE` | text | none | the branch the change is measured from (default: origin/\<default branch\>) |
| `--agent AGENT` | text | none | your id for the build slots (default: the agent the hook vouched for) |
| `--profile PROFILE` | text | none | run a bigger profile than the change needs (docs, tooling or full) |
| `--keep-going` | flag | off | run every check even after one fails |
| `--dry-run` | flag | off | print the profile and the checks; run nothing |
| `--max-wait MIN` | number | `240.0` | minutes the build may wait for a slot before giving up with exit 75 (default 240.0) |

### `task review` {#task-review-usage}

Review a task: prepare what to judge, record the reviewer's verdict as a `reviewed` stamp, or run both.

```text
coney-tools task review [TASK] [--worktree WORKTREE] [--base BASE] [--pr-file PR_FILE] [--text] [--record | --run]
    [--claude CLAUDE] [--model MODEL] [--effort EFFORT] [--timeout-minutes TIMEOUT_MINUTES]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[TASK]` | text | none | the task (default: CONEY_TASK, else the branch) |
| `--worktree WORKTREE` | path | none | the unit's worktree (default: the current checkout) |
| `--base BASE` | text | none | what the branch is compared with (default: origin/\<default branch\>) |
| `--pr-file PR_FILE` | path | none | the description file (default: \<scratch\>/\<task\>/PR.md) |
| `--text` | flag | off | review the description alone (a full pass must still hold) |
| `--record` | flag | off | record the reviewer's verdict as a `reviewed` stamp. Not with `--run`. |
| `--run` | flag | off | prepare, run a headless reviewer and report what it recorded. Not with `--record`. |
| `--claude CLAUDE` | text | `claude` | the Claude Code executable for --run (default: claude) |
| `--model MODEL` | text | none | the model for --run (default: the agent file's) |
| `--effort EFFORT` | choice | none | the effort level for --run. One of `low`, `medium`, `high`, `xhigh`, `max`. |
| `--timeout-minutes TIMEOUT_MINUTES` | number | `60` | stop a --run review after this long |
<!-- command-reference:end -->
