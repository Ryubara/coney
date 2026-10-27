# The coney-tools pull request commands

The `pr` commands: whether a unit is ready to become a pull request, the text it carries, its release, a hold on its
problems, the checks on its title, description and commits, and the list of stuck pull requests.
[`repo`](coney-tools-repo.md#repo) and [`task`](coney-tools-repo.md#task) are on the repository commands page; the other
groups are listed in [the command line overview](coney-tools.md#groups).

## pr {#pr}

The checks on a pull request and its commits: whether a unit is ready to become a pull request (`pr verify`), the text
it will carry (`pr text`), the release itself (`pr open`), a hold on a pull request's problems (`pr hold`) and the
trailers of a commit (`pr check-commit`); the checks CI runs on a title, a description and the docs rule
([`pr check-title`, `check-docs` and `check-body`](#pr-checks)), and the open pull requests that are stuck
([`pr list-stuck`](#pr-list-stuck)). What the first three check and do, and what each refusal asks you to do,
is [Releasing a pull request](pr.md).

### pr verify {#pr-verify}

```sh
uv run --project python coney-tools pr verify [TASK] [--worktree DIR] [--remote NAME] [--text FILE] [--json]
```

Steps 1 to 4 of a release, read only: the stamp log is intact, Check ran a big enough profile on this change, every
`@orig` function the branch changes was studied and compared at its current body, every test it adds failed first, and
Review passed for this change, these pairs and this text. Prints one block per refusal (the message, then what to
redo), or `ok`. It changes no stamp, file or git state and does not fetch.

| Argument or option | Default | What it does |
| --- | --- | --- |
| `TASK` | `CONEY_TASK`, else the branch | The task whose unit is verified; its `TASK.md` must exist. A task whose front block has `parent: <task>` is a fix: it continues its parent's log and text |
| `--worktree DIR` | the current checkout | The unit's worktree: its HEAD is the tip, and it must be clean |
| `--remote NAME` | `origin` | The remote whose default branch is the base of the change; the rules are always read from `origin/<default branch>`, the branch `origin/HEAD` names |
| `--text FILE` | `PR.md` in the task's scratch folder | The engineer's file whose title, paragraph and pictures Review covered |
| `--json` | off | Print the result as one JSON object: the facts, `ok`, the refusals (`stage`, `text`, `redo`) and the notes |

Exit codes: 0 everything holds, 1 something is refused, 2 it cannot run (no `TASK.md`, an unknown base, unreadable rules).
The refusals, one by one: [pr verify](pr.md#verify).

### pr text {#pr-text}

```sh
uv run --project python coney-tools pr text [TASK] [--worktree DIR] [--remote NAME] [--text FILE] [--dry-run] [--json]
```

Step 5: generates the pull request's title and description from the engineer's file, `TASK.md` and the stamps, checks
them with the checks CI runs, and writes `PR-TITLE.txt` and `PR-BODY.md` next to the engineer's file. When a check
fails it prints each problem with what to redo and writes nothing. When the engineer's file has no Issues affected
section, it runs [`issue impact`](coney-tools-issues.md#issue-impact) (which reads GitHub) and writes the candidates into
a `TODO:` the checks refuse, since only the engineer can confirm them. `TASK`, `--worktree`, `--remote` and `--json` are
as for `pr verify`.

| Option | Default | What it does |
| --- | --- | --- |
| `--text FILE` | `PR.md` in the task's scratch folder | The engineer's file; the generated files are written in its folder |
| `--dry-run` | off | Check and print; write no file |

With `--json` the result holds the title and the hashes a release records (`title_sha256`, `paragraph_sha256`,
`images_sha256`, `facts_sha256`). Exit codes as for `pr verify`. The file's form, what is generated and the checks:
[pr text](pr.md#text).

### pr open {#pr-open}

```sh
uv run --project python coney-tools pr open [TASK] [--worktree DIR] [--text FILE] [--remote NAME] [--repo OWNER/NAME]
    [--reopen] [--no-auto-merge] [--dry-run] [--json]
```

The coordinator's release command: it runs `pr verify` and `pr text` on the unit, rebases it onto the remote's default
branch, pushes it with a lease, opens or updates the pull request with the generated title and description and the
pictures attached, turns on auto-merge, reads the pull request back and records it. It commits nothing. An engineer or
reviewer identity is refused. The steps, each refusal and what to redo: [pr open](pr.md#open). There is deliberately no
option to skip a check, to take a stamp from elsewhere or to name another base branch. Its own `git` and `gh` calls are
subprocesses, which no hook sees; once a maintainer turns on `pull_requests.open_only`, the PR guard refuses the
hand-typed ways around it in every session ([The PR guard](enforcement.md#pr-guard)).

| Argument or option | Default | What it does |
| --- | --- | --- |
| `TASK` | `CONEY_TASK`, else the branch | The task whose unit is released. A fix task (`parent:` in its `TASK.md`) is released onto its parent's pull request |
| `--worktree DIR` | the worktree `task start` recorded | The unit's worktree, which `pr open` rebases; it must be clean and on the unit's branch |
| `--text FILE` | `PR.md` in the task's scratch folder (a fix task: its parent's) | The engineer's title, paragraph, `Runtime:` line and pictures; `gh` runs in its folder, and the generated files and `pr-published.json` stay in the task's folder |
| `--remote NAME` | `origin` | The git remote the branch is pushed to and whose default branch is the base |
| `--repo OWNER/NAME` | what `gh` resolves for the remote (the parent of a fork) | The repository the pull request is opened in |
| `--reopen` | off | Reopen a closed, unmerged pull request of the branch and update it |
| `--no-auto-merge` | off | Open without turning on auto-merge, and turn it off when it is already on for the pull request (it is turned on when `merge.auto_merge` is true, the change touches no path of `merge.manual_paths` and the viewer's permission allows it) |
| `--dry-run` | off | Steps 1 to 5 in a throwaway worktree: the rebase is tried there and nothing in the unit's folder (no lock, no `REBASE.md`), the log, git or GitHub changes |
| `--json` | off | Print the result as one JSON object: `ok`, `exit`, `task`, `dry_run`, `message`, `refusals`, `log`, `pr`, `tip`, `stamp`, `auto_merge` |

Exit codes:

| Exit | Meaning | The coordinator does |
| --- | --- | --- |
| 0 | Opened or updated and read back; with `--dry-run`, every check passed | nothing: the claim card shows the pull request |
| 1 | Refused: a stage or text check failed, the claim is not the opener's, or the pull request is merged or closed. Every problem is listed under the stage to redo | send the list to the engineer and wait for the next Review |
| 2 | Cannot run: wrong caller, `gh` missing or old, no unit, dirty worktree, lock held, no fetch, a unit started from a base other than the default branch, `origin/HEAD` unset. Nothing was touched | fix the cause (the messages are in [pr open](pr.md#open)) |
| 3 | Rebase conflict. The branch is as it was; `REBASE.md` in the task's folder lists the paths | send the paths to the engineer |
| 4 | A git or GitHub step failed once the branch had been touched (the rebase, the lease, create, edit, merge, read-back), or auto-merge that must be off stays on (nothing is pushed then). The pull request may exist | run it again: every step is idempotent |

### pr hold {#pr-hold}

```sh
uv run --project python coney-tools pr hold PR [--who NAME] [--hours N] [--cause CAUSE] [--note TEXT]
uv run --project python coney-tools pr hold PR --release
uv run --project python coney-tools pr hold --list [--json]
```

The coordinator's record that somebody holds a pull request's problems (an agent fixing them, or a waiver with a note):
the problem is neither told by [Conclave's watchers](conclave.md#watchers) nor guarded while the hold runs on the head
commit it was made on, and is open again when it ends or the pull request gets a new push. It writes one file of the
scratch directory and nothing on GitHub. What each form does, the limits and the exit codes: [pr hold](pr.md#hold).

### pr check-commit {#pr-check-commit}

```sh
uv run --project python coney-tools pr check-commit FILE [--model ID]
```

Checks the trailers and the e-mail addresses of one commit message against `commits.trailers` of
`.github/conventions.yml` ([AGENTS.md](repo:AGENTS.md), Commits and GitHub): the only trailer is the AI co-author line
(`Co-Authored-By: <model name> <noreply address>`), and no e-mail address but a model's noreply address appears
anywhere in the message. An address is what `hygiene.attachments.patterns.email` of the same file matches (the pattern
the attachment scan uses): a package pin such as `markdownlint-cli2@0.18.1` or an asset name such as `icon@2x.png` is
not one. It is the `commit-message` hook of `.pre-commit-config.yaml` at the `commit-msg` stage, so `uvx pre-commit
install` makes git run it before each commit ([Formatting and pre-commit](building.md#formatting-and-pre-commit) says
how, and that an older clone installs again); pre-commit passes it the message file. Prints `commit message: ok` or
`commit message refused: <reasons>`.

What it reads of the message: nothing from git's scissors line on (the staged diff that `git commit -v` or
`commit.verbose` shows below it), in the comment character `core.commentChar` sets (`#`; the scissors line names it
under `auto` the scissors line, or failing that the editor template, names it). Comment lines are dropped only when
git will strip them, which is when the editor template is in the file; after `git commit -m` or `-F` they stay in
history, so an address there is refused. A file that is not UTF-8 is read as far as it goes. A trailer block is the
last paragraph when every line of it has the shape `Token: value` and it is not the title's paragraph. What it does
not judge: a merge message (`Merge ...`) and a squash merge's (`Squashed commit of the following:`), which list other
people's commits; and the messages git writes about an earlier commit (`Revert "..."`, `fixup!`, `squash!` and
`amend!` commits) are checked for other trailers and addresses but not required to carry the agent's trailer.

* `FILE` (required): the commit message file, as git passes it to the hook (`.git/COMMIT_EDITMSG`).
* `--model ID`: the id of the model the committing agent runs on (`claude-sonnet-5-5`). Then the message must carry
  `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` and no trailer naming another model; an id that is not a
  Claude id is not judged. Default: the model in the identity file that the Claude Code hooks wrote for this command
  and `CONEY_AGENT_TOKEN` names ([Who runs a command](agents.md#identity)), when there is a valid one. A person
  committing outside Claude Code has none, and typed variables are not trusted, so only the shape of the trailers is
  checked.

Exit codes: 0 the message passes, 1 it is refused, 2 the file cannot be read or the conventions are unusable.

### pr check-title, check-docs and check-body {#pr-checks}

```sh
uv run --project python coney-tools pr check-title FILE|-
uv run --project python coney-tools pr check-docs [--body FILE]      # changed paths on stdin
uv run --project python coney-tools pr check-body [--body FILE|-] [--title TITLE] [--worktree WT [--base REF]]
    [--local-images]
```

The checks CI's `title`, `docs` and `body` jobs run on a pull request, against the rules of `.github/conventions.yml`
([Repository conventions](repo-conventions.md)); a missing or malformed file is a one-line error and exit code 2.

* `pr check-title` checks the first line of a commit message or a title (`-` reads stdin) against `commits.title`:
  `area: Verb the rest`, a known area and verb, the length limit, no trailing period, the breaking suffix last. The
  `title` job of the `pr` workflow runs it on the pull request title.
* `pr check-docs` reads the changed paths on stdin and refuses a change under `paths.code` that has no change under
  `paths.docs` and no `Docs: none` line in the body.
* `pr check-body` checks a pull request description against the template ([What a pull request
  shows](testing.md#pr-evidence)). Given `--worktree`, it also reads the branch's commits (`--base`, default
  `origin/main`, to `HEAD`) and requires the AI use `Model:` line to name every model a `Co-Authored-By` trailer on them
  credits: see [the Model line](#pr-model-line). Given `--title` (the `body` job passes the pull request's title), a
  title area a player sees (`pull_requests.runtime.areas`: `gameplay`, `graphics`, `audio`) must have a line starting
  `Runtime:` in the Testing section: the evidence of a comparison with the original running, with numbers, or `Runtime:
  none (<reason>)` ([the Runtime line](testing.md#runtime-line)). Without `--title` that rule is not checked and the
  output says so; `legacy check-pr-review` always has the title (line 1 of `PR.md`).
  The **Issues affected** section must hold one `- #N: how it is affected` item for each open issue the change affects,
  or the single line `None (<reason>)` with any short reason (`None (one-line fix)`); an empty section, a `None` with
  no reason, an item with no line on how, a number listed twice and prose are refused, and the message says what to
  write. Whether the list is complete is the reviewer's judgement;
  [`issue impact`](coney-tools-issues.md#issue-impact) suggests it.
  `--local-images` accepts an image that points at a local file, as in a description that `gh pr create --attach`
  is about to upload; the release guard hook passes it ([The release queue](how-we-work.md#release-queue)).

### The Model line and the branch's trailers {#pr-model-line}

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
  refused. When the AI use lines are themselves unusable (a missing or empty `Model:` for an AI-written branch) only
  that is reported, not the trailers too.
* **The base must be fresh and right.** `base..HEAD` is what the branch has that the base lacks. With a stale
  `origin/main`, a branch that has merged a newer `main` also counts the commits it merged in, and so their trailers
  (other authors' models) must be named: run `git fetch` and the refusal goes away. A stacked branch
  ([Dependent units are stacked](how-we-work.md#release-queue)) with the default base also counts the parent unit's
  trailers: pass `--base <parent branch>`.
* **The message.** One reason per missing model, naming it:
  `AI use: 'Model:' does not name Claude Opus 5.5, which a Co-Authored-By trailer on the branch's commits credits`.
* **Where it runs.** Locally, where the branch is at hand: `legacy check-pr-review`, `legacy pr-review` and `repo
  review-stamp` read the commits of the worktree after `--base` (default `origin/main`) and refuse before the agent
  starts, and `pr check-body --worktree WT` does the same for a description on its own. **CI does not run it:** the
  `body` job gets the description from the event and has no commits (its checkout is shallow, and a full history for one
  string comparison would slow a required check on every edit), so it passes no models and stays a description-only
  check, as `Docs: none` is. The coordinator's release step is the gate
  ([The description review](how-we-work.md#pr-review)). A branch git cannot list (an unknown `--base`, not a worktree)
  exits 2 rather than passing as one without trailers.
* **One source.** The trailer's key and addresses are `commits.trailers` and the rule's switch is
  `pull_requests.ai_use.model.names_trailer_models` in [.github/conventions.yml](repo:.github/conventions.yml); the
  check holds no copy, and the generated rules in [CONTRIBUTING.md](repo:CONTRIBUTING.md) state it.

### pr list-stuck {#pr-list-stuck}

`pr list-stuck` answers "which open pull requests need someone to act?" with one read of the open pull requests and
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
unknown keeps its conflict label as it is. The labels must exist in the repository with their colours: `repo sync-labels
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
| the text was fixed | the `title` or `body` check failed and the pull request's **current** title or description passes `pr check-title` or `check-body` | status `new-event`: the report says to edit the title or description; no rerun is made |
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

<!-- command-reference:begin pr -->
## Parameters of `pr` {#pr-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `pr check-title` {#pr-check-title-usage}

Check a commit or pull request title against the commit title rules.

```text
coney-tools pr check-title FILE
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `FILE` | text | required | a file whose first line (not blank, not #) is the title; - for stdin |

### `pr check-docs` {#pr-check-docs-usage}

Check that a pull request which changes code also changes the docs (changed paths on stdin).

```text
coney-tools pr check-docs [--body BODY]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--body BODY` | text | none | a file holding the pull request description (for `Docs: none`) |

### `pr check-body` {#pr-check-body-usage}

Check a pull request description against the template: sections, summary, AI use.

```text
coney-tools pr check-body [--body BODY] [--title TITLE] [--worktree WORKTREE] [--base BASE] [--local-images]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--body BODY` | text | none | a file holding the pull request description; - or omitted reads stdin |
| `--title TITLE` | text | none | the pull request's title: a title area a player sees (gameplay, graphics, audio) then needs the Testing section's `Runtime:` line, as in CI; without it that rule is not checked |
| `--worktree WORKTREE` | path | none | the branch's worktree: also require the Model line to name every model its commits credit in a Co-Authored-By trailer (what `legacy check-pr-review` does) |
| `--base BASE` | text | `origin/main` | what the branch is compared with, with --worktree (default: origin/main) |
| `--local-images` | flag | off | accept an image that points at a local file, which `gh pr create --attach` uploads |

### `pr verify` {#pr-verify-usage}

Steps 1-4 of a release, read only: the stamp log, Check, the compared functions and Review hold for this change
(docs/guides/pr.md#verify).

```text
coney-tools pr verify [TASK] [--worktree WORKTREE] [--remote REMOTE] [--json] [--text FILE]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[TASK]` | text | none | the task (default: CONEY_TASK, else the branch) |
| `--worktree WORKTREE` | path | none | the unit's worktree (default: the current checkout) |
| `--remote REMOTE` | text | `origin` | the git remote whose default branch is the base of the change (the rules are always origin's) |
| `--json` | flag | off | print the result as one JSON object |
| `--text FILE` | path | none | the file Review covered (default: PR.md) |

### `pr text` {#pr-text-usage}

Step 5: generate the pull request's title and description from PR.md, the task and the stamps, check them and write
PR-TITLE.txt and PR-BODY.md (docs/guides/pr.md#text).

```text
coney-tools pr text [TASK] [--worktree WORKTREE] [--remote REMOTE] [--json] [--text FILE] [--dry-run]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[TASK]` | text | none | the task (default: CONEY_TASK, else the branch) |
| `--worktree WORKTREE` | path | none | the unit's worktree (default: the current checkout) |
| `--remote REMOTE` | text | `origin` | the git remote whose default branch is the base of the change (the rules are always origin's) |
| `--json` | flag | off | print the result as one JSON object |
| `--text FILE` | path | none | the engineer's file (default: PR.md) |
| `--dry-run` | flag | off | check and print; write no file |

### `pr open` {#pr-open-usage}

Release a unit: check it, rebase, push with a lease, open or update the pull request, turn on auto-merge and read it
back (docs/guides/pr.md#open).

```text
coney-tools pr open [TASK] [--worktree WORKTREE] [--text FILE] [--remote REMOTE] [--repo OWNER/NAME] [--reopen]
    [--no-auto-merge] [--dry-run] [--json]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[TASK]` | text | none | the task (default: CONEY_TASK, else the branch) |
| `--worktree WORKTREE` | path | none | the unit's worktree (default: the one task start recorded) |
| `--text FILE` | path | none | the engineer's file (default: PR.md in the task's folder) |
| `--remote REMOTE` | text | `origin` | the git remote the branch is pushed to |
| `--repo OWNER/NAME` | text | none | the repository to open it in (default: what gh makes of the remote) |
| `--reopen` | flag | off | reopen a closed, unmerged pull request of the branch |
| `--no-auto-merge` | flag | off | open without turning on auto-merge (and turn it off if it is on) |
| `--dry-run` | flag | off | try everything but the push and GitHub in a throwaway worktree |
| `--json` | flag | off | print the result as one JSON object |

### `pr hold` {#pr-hold-usage}

Say that somebody owns a pull request's problems for a few hours, so the coordinator is not woken or kept from stopping
for them (docs/guides/pr.md#hold).

```text
coney-tools pr hold [PR] [--who WHO] [--hours HOURS] [--cause CAUSE] [--note NOTE] [--release] [--list] [--json]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[PR]` | integer | none | the pull request's number |
| `--who WHO` | text | none | the agent or track fixing it; leave out for a waiver (--note then says why) |
| `--hours HOURS` | number | none | how long the hold lasts (default and most: conclave.watch) |
| `--cause CAUSE` | choice | none | hold only this cause (default: every cause). One of `checks-failed`, `conflict`, `ci-stuck`, `auto-merge-off`. |
| `--note NOTE` | text | none | what is being done, or why waiting is right |
| `--release` | flag | off | end the pull request's hold now |
| `--list` | flag | off | print the holds that run |
| `--json` | flag | off | print the result as JSON (with --list) |

### `pr check-commit` {#pr-check-commit-usage}

Check a commit message's trailers and e-mail addresses (the git commit-msg hook runs this).

```text
coney-tools pr check-commit FILE [--model MODEL]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `FILE` | text | required | the commit message file, as git passes it to the commit-msg hook |
| `--model MODEL` | text | none | the id of the model the committing agent runs on (claude-sonnet-5-5): the Co-Authored-By trailer must name it (default: the model in the identity file the Claude Code hooks wrote for this command, if there is one; typed variables are not trusted) |

### `pr list-stuck` {#pr-list-stuck-usage}

List the open pull requests that are stuck (failed required check, conflict, stuck CI, no auto-merge).

```text
coney-tools pr list-stuck [--json] [--pending-hours H] [--verbose] [--labels] [--rerun] [--apply]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--json` | flag | off | print the report as JSON instead of text |
| `--pending-hours H` | number | none | hours a required check may be pending (default: hygiene.thresholds.pr_pending_hours) |
| `--verbose` | flag | off | also show drafts, stacked pull requests and unknown mergeability |
| `--labels` | flag | off | also list the labels to add to or remove from the open pull requests (hygiene.pr_health.labels) |
| `--rerun` | flag | off | also list the failed required checks that are stale (text fixed since, runner lost) and would be rerun |
| `--apply` | flag | off | with --labels: make those label changes through gh; with --rerun: really rerun the checks (gh run rerun) |
<!-- command-reference:end -->
