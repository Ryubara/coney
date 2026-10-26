# Repository conventions

Every rule about running the repository that a program can check has one home: `.github/conventions.yml`. Commit
title areas and verbs, the pull request description's sections, the labels, the issue forms' structure, the merge
settings, the hygiene limits and the classes of paths are keys in that file, and every check reads them from there.
To change a rule you edit one line, and the checks and the tests follow. This page says what the file holds, who reads
it and how to change it. The reasons behind the rules stay in the guides this page links; the file holds values, not
arguments.

## One home per rule

| Kind of rule | Where it lives | How the rest of the repository uses it |
| --- | --- | --- |
| A rule a program checks (a title, a description, a label, a limit, a path class) | `.github/conventions.yml` | `coney-tools` reads it; nothing hard-codes the value; AGENTS.md and the guides show a [generated block](#generated-blocks) of the lists |
| A rule that GitHub reads from its own file (the issue forms, the pull request template, the ruleset) | Its own file | [`repo audit`](#audit-checks) checks it against `conventions.yml`, so a change to one without the other fails |
| Tool configuration (`.clang-format`, `.pre-commit-config.yaml`, `mkdocs.yml`, `CMakePresets.json`, ...) | Its own file, in its own format | `repo.tools` names the file; its values are never copied |
| Judgement and reasons (evidence levels, roles, how to write a good issue) | The guide that owns the topic | Prose explains why; where it needs a value it names the key |

Which rules a program enforces, and at which point (a hook, pre-commit, the local gate, CI or review), is the
[Enforcement map](enforcement.md).

The file is the answer when two places disagree: the old rule files (`commit-conventions.json`, `pr-body-rules.json`
and `labels.yml`) are gone, and so are the constants that repeated their values in the Python code.

## What the file holds

| Section | What it says | Read by |
| --- | --- | --- |
| `repo` | The default branch, the wording (the "user" is whoever tested, was affected or created the issue or pull request; the "maintainers" are whoever decides; never "owner" for a person), the decisions only a maintainer takes (`maintainer_decisions`) and the tool configuration files | `repo audit` (`files.exist` and the wording checks); agents ([AGENTS.md](repo:AGENTS.md)) |
| `paths` | Which paths are code and documentation; the CI scope classes (`docs_only`, `tools_only`, `engine`); generated paths; the agent pointer files; the one file that may hold the repository's address; the large-file limit; the gameplay and Coney-only folders of `src/` (`gameplay`, `coney_only`) | `repo check`, `repo check-docs`, `check changed`; `orig check` (the two folder lists); `repo render` (the `docs-only-paths`, `tools-only-paths` and `engine-paths` blocks); `ci-scope.sh` reads the three scope lists itself, and `repo audit` holds it and pre-commit to the file |
| `commits` | The title form, its limit and suffix, the areas and verbs, the body wrap and the `Co-Authored-By` trailer | `repo check-title`, which the `title` job and `repo check-review` run; `repo render` (the `commit-rules` and `commit-areas` blocks) |
| `pull_requests` | The description's sections and which must hold text, the `Docs: none` line, `Fixes #N`, the Issues affected rule (`issues_affected`: the section, the `None (<reason>)` line and its example, when the after-merge audit began), the Checklist (every item ticked), the Screenshots rule, the `runtime` rule (the title areas a player sees need a `Runtime:` line in Testing), the AI use choices, the review gate (`review`: the reviewer agent, its verdict and report files, the description hash), stacked pull requests, the generated description's pictures (`images`) and block markers (`markers`), `open_only` (whether only `pr open` may open a pull request; off for now) | `repo check-body`, `repo check-docs`, `repo review` and `repo check-review`; `pr text`; `repo render` (the `pr-rules` and `pr-review` blocks); `repo audit` checks the template and the stacked-pull-request rules; the PR guard hook reads `open_only` ([The PR guard](enforcement.md#pr-guard)) |
| `merge` | The merge methods, the squash commit's wording, auto-merge, the required checks, who merges and `manual_paths` (the files whose change only a maintainer merges: `pr open` leaves auto-merge off for it): a copy of the GitHub ruleset and settings | `repo render` (the `required-checks` block); `repo audit --github` compares it with the GitHub ruleset and settings |
| `issues` | The structure of each issue form (field ids, headings, which are required, default labels), the Screenshots line, the `From:` line, links, how many labels of each family an issue carries and which title area an old title gets from a label (`title_area_by_label`; the title rule itself is `commits.title`) | `repo audit` (the forms, and every issue's sections, placeholders, Screenshots, labels, links and `From:` line); `issue check`; `issue titles` |
| `claims` | Claiming an issue: the exact shape of each command, the stages, the labels it sets, the limits of open claims, the 48 h and 72 h deadlines, the waits of `task create` and the sweep's schedule | `claim run` and `claim sweep` (`.github/workflows/claim.yml`), `task create`; `repo render` (the `claim-rules` block) |
| `tasks` | The task kinds, the task file's name, the leads search's limits, what `task start` keeps (the unit record's file name and the engineer slots, task and fix, with the switch that enforces them) and the review (`review`: the reviewer agent, the files of a review, which changes need which checklist, the model reverse-engineering review needs) | `task create`, `task leads`, `task start`, `task review` |
| `labels` | The label families and every label with its colour and description | `repo labels`; `repo render` (the `labels` block); `repo audit` (the labels on GitHub) |
| `hygiene` | The limits after which an item counts as stale or stuck (`thresholds`, including `pr_pending_hours`), which required checks are lint (`pr_health.lint_checks`), the labels for stuck pull requests (`pr_health.labels`), when `pr-health --rerun` may restart a check (`pr_health.rerun`: the cap per commit, the conclusions and log patterns of a runner failure), the weekly report issue, how the workflow comments (`comments`), what the scan of attached logs downloads and looks for (`attachments`: the link, the endings, the size and time limits, the patterns) and `checks`, the registry of the audit's checks | `repo audit`, `repo pr-health`, `repo hygiene` |
| `workspace` | The worktree and scratch layout, the free-space minimum and the busy marker | `workspace clean` and `workspace disk`, `repo audit --local`; hosted checks ignore it |
| `conclave` | The Claude Code mod's numbers: the Claude Code version it is tested on, the hook settings file its agent list is read from, how often it reads the snapshot, the pane's two widths and the band's, when the band is stale, how long a merged unit stays, when a claim's countdown turns red and the watchers' settings: who is woken, how often, how long a hold lasts ([Conclave](conclave.md)) | `conclave snapshot` |
| `stamps` | The proof log of a unit of work: the segment size, every step and the results (stamps are not signed), whether `pr verify` requires the hook identity (`require_hook_source`); the pairs' kinds, relations and the words that mark a stand-in at the code ([Stamps](stamps.md)) | `stamp verify`, `original study`, `original compare`, `original fails-first` |
| `proof` | What a change must pass on this machine before it is opened as a pull request: the profiles (`docs`, `tooling`, `full`), the CI scope classes each covers and the checks each runs, the checks that apply only to some paths, and the preset of the build checks ([task check](coney-tools-repo.md#task-check)) | `task check`, `pr verify` (read from the remote's default branch) |
| `research` | The kinds of research page, the sections each keeps, their length limits, the old section names, the data files an address is checked against and the legacy file ([Research pages](writing-docs.md#research-pages)) | `docs check` and `docs legacy`; `repo render` (the `research-pages` block) |
| `standards` | What a touched file is held to: the legacy file of source and test files, whether a touched file below the standard fails (`enforce`, false until the cutover), the cutover day (from it nothing new is marked legacy), the folders and endings of the code and the tests, the test macros and the form of an `// Expected:` citation ([the standards](coney-tools-standards.md#standards)) | `standards scan`, `init`, `prune`, `mark` and `check`; `docs check` |
| `runs` | Run records: the id's shape (their folder is `research.files.runs`), the largest record, the most values and the kinds of run ([Run records](research-evidence.md#run-records)) | `pcsx2 record-run`, `pcsx2 recipe run --record`, `pcsx2 record --run-record`, `research claims` |
| `ci` | The settings of the CI jobs that read them (`dependencies`: the workflow whose runs are restarted; `tidy`: the folders and extensions the per-pull-request clang-tidy checks, and the build-setup files whose change sends it through every file; `failure`: the workflows watched, the events, the labels and the size of the report of a failed run on `main`) and `places`: where each check runs, local or in CI, with its jobs | `ci dependencies`, `ci rerun-dependencies`, `ci tidy-plan`, `ci tidy-run`, `ci report-failure`; `repo render` (the `check-places` block); `repo audit --files` (`files.ci-places`) |

The commented file is the reference for each key: open `.github/conventions.yml` and read the comment above it.

## Generated blocks {#generated-blocks}

Agents read `AGENTS.md`, `CONTRIBUTING.md` and the guides as raw files, so a list of the conventions in them must be
text in the file, not something the docs build fills in. `coney-tools repo render` writes it: between a begin and an end
marker line it replaces everything with the block of that name, built from `.github/conventions.yml`. A marker is a
whole line:

```text
<!-- conventions:begin commit-areas -->
(generated; never edited by hand)
<!-- conventions:end -->
```

Markers inside a fenced code block, like the one above, are examples and are left alone. `AGENTS.md`,
`CONTRIBUTING.md` and every Markdown file under `docs/guides/` may hold blocks. The blocks are:

| Block | Shows | Keys | Used in |
| --- | --- | --- | --- |
| `commit-rules` | The commit title form, verbs and areas; the body and the `Co-Authored-By` trailer | `commits` | `AGENTS.md` |
| `commit-areas` | A table of each area and what it covers, and the verbs | `commits.title` | `CONTRIBUTING.md`, [commit review](review/commits.md) |
| `labels` | Every label with its family and description | `labels` | [Issues](issues.md#labels) |
| `pr-rules` | What CI checks in a pull request description | `pull_requests`, `paths` | `CONTRIBUTING.md`, [Testing](testing.md#pr-evidence) |
| `pr-review` | The release gate on the reviewer's verdict: its files, its fields and what makes it stale | `pull_requests.review` | [How we work](how-we-work.md#pr-review) |
| `required-checks` | The merge method, the required checks and the merge settings | `merge` | [Building and testing](building.md#what-ci-runs) |
| `maintainer-decisions` | The decisions only a maintainer takes | `repo.maintainer_decisions` | `AGENTS.md` (the coordinator section of [How we work](how-we-work.md#coordinator) links to it) |
| `docs-only-paths` | The paths a documentation-only change may touch | `paths.docs_only` | [Building and testing](building.md#what-a-pull-request-runs) |
| `tools-only-paths` | The paths that run python on every OS and skip the engine jobs | `paths.tools_only` | [Building and testing](building.md#what-a-pull-request-runs) |
| `engine-paths` | The paths that run the engine jobs and python on Linux | `paths.engine` | [Building and testing](building.md#what-a-pull-request-runs) |
| `audit-checks` | The table of the audit's checks | `hygiene.checks` | [Audit checks](#audit-checks) |
| `check-places` | The table of where each check runs: local, or CI on pull requests, pushes to main or a release branch, nightly, manual runs or issue events | `ci.places` | [Building and testing](building.md#what-ci-runs) |

After a change to a key, run `uv run --project python coney-tools repo render` and commit the rewritten files with the
change. `repo render --check` fails when a block is stale; the `python` workflow and `coney-tools check changed` run
it, so a block can only drift if someone edits it by hand, and the check says which file. A conflict in a block while
merging is resolved by taking either side and running `repo render` again. To add a block, write its function in
`python/src/coney_tools/conventions_render.py` (a function from the conventions to lines, registered in `BLOCKS`), its
test in `python/tests/test_conventions_render.py`, and put the markers where it belongs. The issue forms and the pull
request template are not generated: GitHub reads them directly, and `repo audit --files` checks them against the file.

## Audit checks {#audit-checks}

`coney-tools repo audit` ([the command](coney-tools-repo.md#repo-audit)) compares the repository with the file and changes
nothing. It reports in three scopes: `files` reads the checkout (CI runs it on every pull request, so a form, the
template or a generated block cannot drift from the file), `github` reads issues, pull requests, labels, the settings
and the ruleset through `gh`, and `local` reads the workspace. `hygiene.checks` in the file is the registry: each entry
says where the check runs, how bad a finding is and who fixes it, and the table below is generated from it.

- **Severity.** `error` makes the audit exit 1; `warn` is reported and does not. An error is a rule a person can fix
  in a minute (a missing section, a missing label); a warning is a judgement or a delay (a stale status, a pull request
  idle for a month). Promoting a warning to an error is one line in the file.
- **Fix class.** `auto`: a coney-tools command fixes it (`repo render`, `repo labels --apply`, `workspace clean`).
  `skill`: it takes judgement, so the author or the coordinator fixes it (the [`/repo-hygiene` skill](#hygiene-skill)
  acts on a finding by this class). `maintainer`: a maintainer decides (the ruleset and the settings of the repository).
- **Items.** Only open issues and open pull requests are checked for their sections, labels, wording and links. History
  is checked only where it loses work: a closed issue or a merged pull request that keeps an unchecked item with no
  follow-up issue number, and a pull request merged into a branch other than `main`.

<!-- conventions:begin audit-checks -->
| Check | Scope | Severity | Fix | Finds |
| --- | --- | --- | --- | --- |
| `files.pointer` | files | error | skill | an agent entry file (CLAUDE.md, GEMINI.md) is missing, too big, or lacks the import line |
| `files.exist` | files | error | skill | a file or folder the conventions name does not exist |
| `files.address` | files | error | skill | the repository's or the docs site's address appears in a file other than the README |
| `files.forms` | files | error | skill | an issue form differs from the conventions: title, labels, field ids, headings, required flags, areas |
| `files.template` | files | error | skill | the pull request template differs from the conventions: headings, AI use lines, the Docs: none and Fixes lines |
| `files.generated-blocks` | files | error | auto | a generated block in AGENTS.md, CONTRIBUTING.md or a guide is out of date (coney-tools repo render) |
| `files.command-reference` | files | error | auto | a coney-tools command or parameter is missing from the guides, or a generated parameter block there is out of date (coney-tools docs render) |
| `files.ci-scope` | files | error | skill | .github/scripts/ci-scope.sh does not read the CI scope paths (docs_only, tools_only, engine) from the conventions file |
| `files.ci-places` | files | error | skill | a job of the workflows is in no place of ci.places, or a place names a job no workflow has |
| `files.large-file-limit` | files | error | skill | pre-commit's check-added-large-files limit differs from the conventions, or the hook or the config skips a file (no --enforce-all, an exclude, types, stages, a wider top-level exclude) |
| `files.game-files` | files | error | skill | pre-commit's no-game-files hook is missing, not language fail, narrowed (an exclude, types, stages, a wider top-level exclude) or lets through a game capture type, the busy marker or a local record |
| `files.markdown-hook` | files | error | skill | pre-commit has no markdownlint-cli2 hook, so a long Markdown line is first caught by CI |
| `issue.sections` | github | error | skill | an open issue lacks a required section of its form (or was filed with other headings) |
| `issue.form` | github | warn | skill | an open issue is not in its form's exact shape: ### headings, names, order, no extra sections, dropdown values (coney-tools issue check) |
| `issue.placeholder` | github | error | skill | a section of an open issue is empty or holds only a placeholder such as x |
| `issue.screenshots` | github | error | skill | an open issue has no uploaded image and no Screenshots: none (reason) line |
| `issue.labels` | github | error | skill | an issue lacks a type, area, priority (after triage) or status label, has two of one, or has an unknown label |
| `issue.closed-status` | github | error | skill | a closed issue still carries an S- status label (the issue-status workflow removes it; this is the backstop) |
| `issue.person-word` | github | error | skill | an open issue says 'owner' for a person (say 'user' for who tested or filed it, 'maintainer' for who decides) |
| `issue.closed-unchecked` | github | error | skill | a closed issue keeps an unchecked item that names no follow-up issue (names the closing pull request when its Checklist is all ticked) |
| `issue.stale-status` | github | warn | skill | an issue calls a pull request open, merged or closed when it is not |
| `issue.links` | github | warn | skill | an issue names a docs or research file as plain text, or links a file or anchor that is not on main |
| `issue.provenance` | github | warn | skill | an issue that an older pull request's Remaining list points at has no From: line |
| `issue.attachment-data` | github | warn | skill | a log attached to an open issue may hold game data: not text, too large, or lines of script source or game text |
| `issue.attachment-personal` | github | warn | skill | a log attached to an open issue shows personal information: a user folder path or an email address |
| `issue.triage-age` | github | warn | skill | an issue has waited in S-needs-triage longer than the limit |
| `issue.info-age` | github | warn | skill | an issue has waited in S-needs-info with no activity longer than the limit |
| `issue.progress-idle` | github | warn | skill | an S-in-progress issue has had no activity for longer than the limit |
| `issue.claim-orphan` | github | warn | skill | an open issue is labelled in progress or in review but nobody is assigned: claim it or set it back to ready |
| `issue.review-without-pr` | github | warn | skill | an S-pending-review issue that no open pull request mentions |
| `pr.title` | github | error | skill | an open pull request's title breaks the commit title rules |
| `pr.body` | github | error | skill | an open pull request's description breaks the template rules (sections, local file paths, images not uploaded) |
| `pr.person-word` | github | error | skill | an open pull request says 'owner' for a person (say 'user' for who tested or filed it, 'maintainer' for who decides) |
| `pr.unfinished-items` | github | error | skill | a pull request merged since the Checklist rule began has an unticked Checklist item (an open one is pr.body) |
| `pr.remaining-uncited` | github | error | skill | a pull request merged before the Checklist rule left an unchecked Remaining item that names no issue |
| `pr.affected-unedited` | github | warn | skill | a pull request merged since the Issues affected rule lists open issues with no body edit or person-made label change after the merge |
| `pr.idle` | github | warn | skill | an open pull request has had no activity for longer than the limit |
| `pr.stacked-depends` | github | error | skill | an open pull request on a branch other than main has no Depends on #N line |
| `pr.stacked-automerge` | github | error | skill | auto-merge is enabled on a pull request whose base is not main |
| `pr.base-merged` | github | warn | skill | an open pull request's base branch was merged: rebase it onto main and retarget it |
| `pr.merged-into-base` | github | error | skill | a pull request was merged into a branch other than main, so the issues it fixes stay open |
| `gh.labels` | github | error | auto | the labels on GitHub differ from the conventions (coney-tools repo labels --apply) |
| `gh.ruleset` | github | error | maintainer | the GitHub ruleset or repository settings differ from the merge section |
| `ws.disk` | local | error | auto | the workspace drive has less free space than the minimum (coney-tools workspace clean) |
| `ws.worktrees` | local | warn | auto | a worktree of merged work is still there, or a stray file sits among the worktrees |
| `ws.scratch` | local | warn | skill | a scratch folder of merged or abandoned work still holds files taken from the original (traces, dumps, save states) |
<!-- conventions:end -->

To add a check, write the function in `python/src/coney_tools/audit_files.py`, `audit_github.py` or `audit_local.py`
with `@check("group.name")` (it yields a `Problem` for each thing it finds), add its entry to `hygiene.checks` and run
`repo render`; a test fails when an entry has no code or code has no entry. The tests give a GitHub check made-up
issues and pull requests in the shape `gh` returns (`python/tests/audit_support.py`), never the network.

## How it is read

`coney_tools/conventions.py` loads the file once into frozen dataclasses (`Conventions`, `conventions.commits.title`,
`conventions.pull_requests.ai_use`, ...) and the checks take the part they need. The loader is strict on purpose:

- an unknown key, a missing key or a value of the wrong type is an error that names the key
  (`.github/conventions.yml: merge.auto_merge: expected true or false`);
- a key written twice is an error, because plain YAML silently keeps the second, and a bad merge often leaves two;
- a reference to something that does not exist (a form label that is not a label, a `filled` section that is not one
  of the `sections`, `no_ai` that is not one of the choices) is an error;
- a colour made only of digits must be quoted (`"000000"`), because YAML reads an unquoted one as a number.

`coney-tools repo check` loads the file, so a mistake in it fails the `python` workflow and `coney-tools check changed`
before anything else runs. Every command that needs a rule exits with code 2 and one line when the file is missing or
unusable, and never with a traceback.

## Changing a rule

1. Edit the key in `.github/conventions.yml`.
2. Run `uv run --project python coney-tools repo render` so the [generated blocks](#generated-blocks) follow. If a
   file keeps a hand-written copy of it, change that too: the issue forms in `.github/ISSUE_TEMPLATE/`, the pull request
   template or pre-commit's `--maxkb`. `coney-tools repo audit --files` fails, naming the file and the key, until they
   agree (`.github/scripts/ci-scope.sh` keeps no copy: it reads `paths.docs_only`, `paths.tools_only` and
   `paths.engine` from the file).
3. Update the guide that explains the rule in the same commit ([Documentation](repo:AGENTS.md)).
4. Changing the conventions themselves is a maintainer decision (`repo.maintainer_decisions`).

## Workflows {#workflows}

The workflows in `.github/workflows/` read the repository and report: they run checks, build, and at most comment or
label. Two rules keep them safe: each declares the narrowest `permissions` it needs (`contents: read` by default), and
nothing from a title, a description or a comment is ever expanded into a script's text; it reaches the script through
an environment variable or a file ([pr.yml](repo:.github/workflows/pr.yml) shows both).

There is one exception, approved by the maintainer on 2026-10-12: `issue-status.yml` edits labels. A status label
describes open work only, so when an issue is closed it removes every `S-` label and when it is reopened it adds
`S-needs-triage` ([What a closed issue keeps](issues.md#closed)). It has `issues: write` and no other permission,
checks out nothing and runs nothing from the repository, and the audit check `issue.closed-status` is the backstop if it
fails. Another workflow that writes to GitHub needs the same maintainer approval first; a new one that only comments
does not.

A second exception, approved on the same day: `hygiene.yml` also edits labels, and only two. On every push to `main`
(and in the weekly sweep) it runs `coney-tools repo pr-health --labels --apply`
([`pr-health`](coney-tools-repo.md#repo-pr-health)), which puts `S-conflict` on each open pull request that conflicts with
`main` and `S-failing` on each that fails a required check, and removes the label when the cause is gone, so the pull
request list shows what is stuck without anyone running a command. A push to `main` is when GitHub's answer about
conflicts changes. The labels and the rereads while GitHub is still working out a conflict are `hygiene.pr_health` in
`.github/conventions.yml`; no other label is touched. The job additionally gets read access to checks and statuses,
which `gh pr list` needs for the checks, and write access to pull requests: the label and comment endpoints are issue
endpoints, but with `issues: write` alone GitHub answers a call on a pull request with HTTP 403 ("Resource not
accessible by integration"), as happened on 2026-10-13. The code only calls those two endpoints, so the extra
permission is not used for anything else. A check that fails after the last push to `main` is labelled at the next
push or sweep, or by running the command by hand.

`hygiene.yml` is the one that comments, and it needed no approval for that: it writes the audit's findings back as
one comment per issue or pull request, edited in place and never deleted, and a weekly report comment ([`repo
hygiene`](coney-tools-repo.md#repo-hygiene)). Its token has `contents: read`, `issues: write`, `pull-requests: write` (a
comment on a pull request needs it) and read access to checks and statuses, nothing more. It runs on
`pull_request_target` so that a pull request from a fork gets its comment, which is safe only because it checks out the
default branch and never the pull request's code, and because what a title or body says reaches a script through
an environment variable and a comment only after it is made harmless (no mention, link, image or HTML). It also
downloads the `.txt` and `.log` files attached to an issue and scans them for game data and personal information
([Attached logs](issues.md#attachments)); that content is only read in memory, within a size cap and a timeout, never
stored, run or quoted. It starts at
warn level (`hygiene.comments.on_sweep` false): item comments come from events, the sweep only reports, and it is not a
required check. The report issue is the one thing it creates besides comments, and only when no open issue has the
title `hygiene.report_issue.title`; creating an issue is a write beyond a comment, so the maintainer approves it
explicitly when merging the pull request that added the workflow. Turning on `on_sweep` is a one-line change to the
conventions.

A third, approved by the maintainer on 2026-10-22: `claim.yml` ([Claiming an issue](issues.md#claiming)) may handle
`/claim`, `/unclaim`, `/claim renew` and `/progress` comments, assign and unassign, set labels, post and edit its own
comments (the claim card, the replies, the 48 hour warning) and delete the command comments it has handled. It has
`contents: read`, `issues: write` and `pull-requests: read`, checks out the default branch only (it runs on
`issue_comment` and `pull_request_target`, so a stranger's comment and a fork's pull request are served), passes the
event to the script as a file and never expands a comment into a script. Its jobs share one concurrency group for the
whole repository, so one runs at a time; a comment that does not start with a slash, or a label event for a label that
is not paused, is skipped in a group of its own so that it cannot replace the pending job of a real command. Each run
handles every unhandled command of its issue and the commands other issues received in the last 30 minutes, so the
command of a job GitHub cancels is usually answered by the next job, and otherwise by the next sweep that runs. A
`workflow_dispatch` started on another branch is skipped, but only while that branch carries `claim.yml` unchanged (a
branch runs its own copy of the file). Two switches in `claims`, `adopt` and `release`, keep the parts that change
existing issues (giving a card to a hand assignment, the 48 and 72 hour deadlines) off until the cutover.
Whether the Actions token may delete another person's comment, and whether it may read a collaborator's permission, are
checked on the real repository; the design works without both (the card remembers which commands it acted on, and the
comment's own association decides for the repository's owner).

Two more write beyond a comment and wait for the maintainer's approval, which merging the pull request that added them
gives (2026-10-23, the CI phase of the workflow plan):

- `dependencies.yml` ([What CI runs](building.md#what-ci-runs)) restarts a failed run. On a push to `main` its `rerun`
  job has `actions: write` (and `contents: read`, `pull-requests: read`) and runs `coney-tools ci rerun-dependencies
  --apply`, which restarts the failed `dependencies` run of an open pull request whose `Depends on #N` pull requests
  have all merged, and nothing else. Its `dependencies` job, on a pull request, only reads. Both check out the default
  branch's own code on `push` and the merge commit on `pull_request`, and the pull request number reaches the command
  through an environment variable.
- `ci-failure.yml` ([Failures on main](building.md#ci-failure)) opens and comments on issues. It runs on
  `workflow_run` of `build`, `python` and `docs` (`ci.failure.workflows`), so it has a token that can write, which is
  why it acts only on a run of this repository on the default branch that did not come from a pull request, checks out
  the default branch (never the tested commit), has `issues: write`, `contents: read` and `actions: read`, and passes the
  run's id through an environment variable. It creates an issue in the task form with the `P-critical` label, or
  comments on the open issue of the same workflow, and edits and deletes nothing.

A third workflow job, `clang-tidy` in `build.yml`, has `security-events: write` to upload its SARIF file to code
scanning (the annotations on a pull request). GitHub does not grant that permission to a pull request from a fork, so
there the job prints workflow commands instead of uploading. It writes nothing else.

## The /repo-hygiene skill {#hygiene-skill}

The coordinator tidies the repository with the `/repo-hygiene` skill
([.claude/skills/repo-hygiene/SKILL.md](repo:.claude/skills/repo-hygiene/SKILL.md)), after every merge round and once a
week. It runs `repo audit` (files, GitHub and local scopes) and `repo pr-health`, then works through the findings by
their fix class: an `auto` finding is fixed by running its command, a `skill` finding is fixed with judgement by the
recipe the skill gives for that check, and an `owner` finding stops the skill, which reports it to the maintainer
instead. Afterwards it fixes the issues that merged pull requests list under Issues affected and the issues that cite them
([After a merge](issues.md#after-a-merge)), and runs the audit again to confirm. Only
the coordinator writes to GitHub, so an agent that is asked to run the skill reports what it would change.

The skill holds a recipe for every check of the registry; a test fails when a check has no recipe or the skill names a
check that does not exist, so adding a check to `hygiene.checks` means adding its line to the skill
([To add a check](#audit-checks)). The `hygiene` workflow ([Workflows](#workflows)) shows the same findings on the items
themselves; the skill is what acts on them.

## Where the reasons are

| Rule | Guide |
| --- | --- |
| Commit titles, squash merging, who merges, the release queue | [How we work](how-we-work.md), [CONTRIBUTING.md](repo:CONTRIBUTING.md#commits) |
| What a reviewer holds a commit, a branch and a pull request title to | [Commit review](review/commits.md) |
| Pull request descriptions: Testing, Screenshots, Checklist | [Testing](testing.md#pr-evidence), [Issues](issues.md#follow-up-work) |
| Labels, forms, triage, what a closed issue keeps | [Issues](issues.md) |
| What a workflow may write to GitHub | [Workflows](#workflows) |
| What CI runs for which paths | [Building and testing](building.md#what-ci-runs) |
| Code style (SPDX, `@orig`, comments) | [Conventions](conventions.md): code, not repository process |
