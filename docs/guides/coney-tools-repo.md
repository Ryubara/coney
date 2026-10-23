# The coney-tools repository commands

The commands that check the repository's own rules and run the issue and pull request process: [`repo`](#repo)
(titles, descriptions, labels), [`pr`](#pr) (commit messages), [`issue`](#issue), [`claim`](#claim) (the issue claims)
and [`task`](#task) (claiming issues and writing a task file). The other groups are listed in
[the command line overview](coney-tools.md#groups).

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
the description's `Model:` line must say so. Without this check only the `pr-reviewer` agent caught a line that named
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

* `repo review` runs the `pr-reviewer` agent once, headlessly: it reads `.claude/agents/pr-reviewer.md` from the
  checkout the command runs in, hands it to `claude -p --agents FILE --agent pr-reviewer --model M
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

## pr {#pr}

The checks on a pull request and its commits. Today it holds one command. The pull-request checks of [repo](#repo)
(`check-title`, `check-body`, `check-docs`) are planned to move here when the command groups are renamed
([Stamps](stamps.md) says the command names are placeholders for that pass).

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

## issue {#issue}

```sh
uv run --project python coney-tools issue draft [task|bug]
uv run --project python coney-tools issue check --title T --body FILE [--form F] [--leftover] [--online] [--attach F]
uv run --project python coney-tools issue create --title T --body FILE [--label L ...] [--parent N] [--leftover]
    [--attach F] [--offline] [--dry-run]
uv run --project python coney-tools issue edit N --body FILE [--title T] [--offline] [--dry-run]
uv run --project python coney-tools issue link N --parent P [--dry-run]
uv run --project python coney-tools issue titles [--json] [--apply [--include-review]] [--limit N]
```

Drafts, checks and files issues in their form's exact shape ([Filing an issue](issues.md#filing)). Every rule is
read from `issues` in `.github/conventions.yml` and from the form files in `.github/ISSUE_TEMPLATE/`; nothing is
copied into the code.

* `draft` prints a blank body: the form's `###` headings in order, each preceded by an HTML comment with the form's
  description, required or optional, and the allowed values (the Area's `A-` labels, a bug's dropdown choices), then
  the `From:` line. The form is `task` unless you name `bug`.
* `check` validates a title and a body file (`-` reads stdin) and prints one line per problem, tagged with its kind
  (`title`, `headings`, `empty`, `choices`, `screenshots`, `from`, `links`, `wording`, `status`); exit 0 when there is
  none, 1 otherwise. A dropdown answer is the option as written under its heading, with or without a list bullet or
  backticks (`--play-level` is an option, not a bullet). The title must be `area: Verb the rest`, as a pull request's
  ([Titles](issues.md#titles)). The form comes from `--form`, else the headings. Docs and research pages and their
  `{#anchor}`s are looked up in `origin/main` of the checkout with git, offline; a missing `origin/main` is a note, not
  a pass. `--leftover` wants the `From:` line. `--online` reads the state of each cited `#N` with `gh` and compares it
  with the words "open", "merged" and "closed" beside it. `--attach` says local screenshots will be uploaded, so a local
  image in the Screenshots section is accepted.
* `create` runs `check` (the states online unless `--offline`), then `gh issue create` with the form's labels and
  the Area's `A-` label. `--label` adds a label (checked against `labels.list`; a type or status label replaces the
  form's). `--parent N` links the new issue as a sub-issue of N through the sub-issues API, and tells you the
  `issue link` command to run if only that step fails. `--dry-run` checks and prints the `gh` commands, running none.
* `edit` runs the same check on the new body (with `--title`, or the issue's current title read with `gh`) before
  `gh issue edit`. `link` makes an existing issue a sub-issue of another.
* `titles` lists the open issues whose title breaks the title rule, each with a proposed title, how sure the proposal is
  (`auto`, or `review` when a verb or an area was guessed or the title was cut) and the `gh issue edit` command that
  applies it; it changes nothing. `--apply` runs the `auto` renames, `--include-review` the others too (the
  coordinator does that once, when the rule starts). Exit 1 while some title is wrong.

Every argument and option of `titles`:

| Option | Default | What it does |
| --- | --- | --- |
| `--json` | off | Print the proposals as JSON (`number`, `current`, `proposed`, `confidence`, `why`) instead of lines and commands |
| `--apply` | off | Rename the issues whose proposal is `auto` with `gh issue edit`; reports a failed rename and goes on |
| `--include-review` | off | With `--apply`, rename the `review` proposals too |
| `--limit N` | 500 | How many open issues to read |

The body is filed without the draft's comments. Exit 2 when the conventions, a form file or the body file cannot be
read.

## claim {#claim}

```sh
uv run --project python coney-tools claim run --event NAME --payload FILE
uv run --project python coney-tools claim sweep
uv run --project python coney-tools claim list [--json]
```

The claim Action's commands ([Claiming an issue](issues.md#claiming)); `.github/workflows/claim.yml` only starts them.
All the logic is in `python/src/coney_tools/issue_claim_*.py` and the rules are `claims` of `.github/conventions.yml`.

* `run` handles one GitHub event. `--payload` is the event's JSON file (`$GITHUB_EVENT_PATH`): only the issue or pull
  request number is taken from it, everything else is read again from GitHub, so no text of a comment or an issue is
  ever parsed from the event. `issue_comment` and `issues` reconcile that issue; `pull_request_target` reconciles each
  issue the pull request closes (`Fixes #N`), and a `synchronize` counts as a push (activity); `schedule` and
  `workflow_dispatch` run the sweep. Reconciling acts on every command comment nobody handled (oldest first), rebuilds
  the card from the assignee, labels and linked pull requests, then (when `claims.release` is on) applies the warning
  (48 h) and release (72 h). A pull request opened, reopened, made ready or pushed to by the claimant counts as
  activity, and while it is open and ready for review the clock waits. Then it handles the commands other issues
  received in the last `claims.catch_up_minutes` (a job the one-at-a-time queue cancelled); a failed read of those is an
  error (exit 1), after the event's own issues are done. Before its first change of each step and each card write it
  reads the issue's cards again and starts over when one changed; when the attempts run out it prints what each did and
  exits 1.
* `sweep` does the same for every issue that may need it: those with a held card, a working or review label, an assignee
  or a command comment from the last day, and it deletes refusal replies a day after their command is gone. It carries
  on past an issue it cannot read and exits 1 at the end, naming it. It also releases the claims that are over the limit
  of open claims or of a task, and lists as `orphan:` the open issues labelled in progress with nobody assigned.
* `list` prints the held claims (issue, claimant, status, task, scope, expiry or what the clock waits for); `--json`
  prints the cards' data for the mod. The claims are read from the cards' hidden JSON.

Every argument and option:

| Command | Argument or option | Default | What it does |
| --- | --- | --- | --- |
| `run` | `--event NAME` | required | The GitHub event's name: `issue_comment`, `issues`, `pull_request_target` (also `pull_request`), `schedule` or `workflow_dispatch`; any other name is an error (exit 2) |
| `run` | `--payload FILE` | required | The event's JSON file (`$GITHUB_EVENT_PATH`); it must hold a JSON object. Only the issue or pull request number, a pull request's `Fixes #N` lines (at most `claims.closing_per_event`), its action and its author are taken from it |
| `sweep` | none | | Reconciles every issue that may need it, enforces the limits, lists the orphans and deletes old replies |
| `list` | `--json` | off | Prints the held claims as JSON (issue, claimant, status, task, scope, stage, pull request, `claimed_at`, `last_activity`, `deadline` (null while the clock waits), `waiting` (what it waits for), `warned`, `adopted`) instead of one line each |

It needs `gh` signed in (the Action passes `GH_TOKEN`). Exit 1 when an issue could not be finished, 2 when the payload or
the conventions cannot be read.

## task {#task}

```sh
uv run --project python coney-tools task create ISSUE [ISSUE ...] [--terms TERM ...] [--scope "a, b"] [--kind K]
    [--name ID] [--resume] [--dry-run]
uv run --project python coney-tools task create --fix-of ISSUE [--terms TERM ...] [--name ID] [--dry-run]
uv run --project python coney-tools task start [TASK] [--base REF] [--remote NAME] [--no-fetch] [--dry-run]
uv run --project python coney-tools task leads --terms TERM ... [--issue N] [--refresh]
uv run --project python coney-tools task index [--no-gh]
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
  are free, then makes the branch (named like the task id) and its worktree in `../../worktrees/coney/<task id>/` from
  the tip of the default branch, writes the unit record `<scratch>/<task>/unit.json`, stamps `assigned` and `claimed`
  in the task's log and posts `/progress claimed`. It prints the branch, worktree, the
  stamps written and `slots: N of L task slots in use`; the limit (3 task, 1 fix) is only reported until
  `tasks.start.slots.enforce` is on. Nothing is made when a check fails (exit 1); a git or fetch failure stops it with
  exit 2; running it again for a started task finishes what is missing and changes nothing else. A fix task opens its
  parent's unit instead of making one. The coordinator's role may not run it (`.claude/hooks/hooks.toml`).
* `leads` runs the leads search alone: one line per match with the source, the place and why it matched, then the terms
  that matched nothing. `--issue N` also matches the addresses, names, paths and references that issue writes.
* `index` rebuilds the cached index (`<scratch>/leads/index.json`, also rebuilt when `HEAD` moved or after
  `tasks.leads.index_max_age_hours`); `--no-gh` leaves the merged pull requests out. The sources, name splitting and
  ranking are in [Task files](issues.md#task-files).

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
| `start` | `--dry-run` | off | Run every check, print the branch, base, worktree and stamps that would be made, and change nothing: no branch, no files, no stamps and no comment |
| `leads` | `--terms TERM ...` | required | The names to search for |
| `leads` | `--issue N` | none | Also match the addresses, code names, repository paths and `#N` references that issue writes |
| `leads` | `--refresh` | off | Rebuild the cached index before searching |
| `index` | `--no-gh` | off | Leave the merged pull requests out of the index (no `gh` call) |
