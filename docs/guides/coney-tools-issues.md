# The coney-tools issue commands

The commands that draft, check and create issues, and keep the claims on them: [`issue`](#issue) and [`claim`](#claim)
(the issue claims). [`task`](coney-tools-repo.md#task) claims issues and writes a task file. The other groups are
listed in [the command line overview](coney-tools.md#groups).

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

<!-- command-reference:begin issue -->
## Parameters of `issue` {#issue-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `issue draft` {#issue-draft-usage}

Print a blank issue body in the form's exact headings, with the rules of each section as comments.

```text
coney-tools issue draft [FORM]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[FORM]` | text | none | the form (task or bug); default: the form the command line uses |

### `issue check` {#issue-check-usage}

Check a title and body against the form and the conventions; exit 1 on a problem.

```text
coney-tools issue check --title TITLE --body BODY [--form FORM] [--leftover] [--attach FILE] [--online] [--markdown]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--title TITLE` | text | required | the issue title |
| `--body BODY` | text | required | a file holding the issue body; - reads stdin |
| `--form FORM` | text | none | the form the body follows (default: from the title and headings) |
| `--leftover` | flag | off | the issue is a leftover: it needs a From: line |
| `--attach FILE` | text | none | a capture to upload with the issue (gh 2.99+). Repeatable. |
| `--online` | flag | off | also check the open/merged/closed words against gh |
| `--markdown` | flag | off | print the problems as the comment the issue-check workflow posts |

### `issue create` {#issue-create-usage}

Check, then file the issue with the form's labels (the only way).

```text
coney-tools issue create --title TITLE --body BODY [--form FORM] [--leftover] [--attach FILE] [--label LABEL]
    [--parent N] [--offline] [--dry-run]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--title TITLE` | text | required | the issue title |
| `--body BODY` | text | required | a file holding the issue body; - reads stdin |
| `--form FORM` | text | none | the form the body follows (default: from the title and headings) |
| `--leftover` | flag | off | the issue is a leftover: it needs a From: line |
| `--attach FILE` | text | none | a capture to upload with the issue (gh 2.99+). Repeatable. |
| `--label LABEL` | text | none | an extra label (validated against the labels). Repeatable. |
| `--parent N` | integer | none | make the new issue a sub-issue of issue N |
| `--offline` | flag | off | do not read the cited issues' states with gh |
| `--dry-run` | flag | off | check and print what would run; write nothing |

### `issue edit` {#issue-edit-usage}

Check, then replace the body (and title) of issue N.

```text
coney-tools issue edit NUMBER [--title TITLE] --body BODY [--form FORM] [--leftover] [--attach FILE] [--offline]
    [--dry-run]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `NUMBER` | integer | required | the issue number |
| `--title TITLE` | text | none | the issue title |
| `--body BODY` | text | required | a file holding the issue body; - reads stdin |
| `--form FORM` | text | none | the form the body follows (default: from the title and headings) |
| `--leftover` | flag | off | the issue is a leftover: it needs a From: line |
| `--attach FILE` | text | none | a capture to upload with the issue (gh 2.99+). Repeatable. |
| `--offline` | flag | off | do not read the cited issues' states with gh |
| `--dry-run` | flag | off | check and print what would run; write nothing |

### `issue titles` {#issue-titles-usage}

List the open issues whose title breaks 'area: Verb the rest', with a proposed title and its gh command.

```text
coney-tools issue titles [--json] [--apply] [--include-review] [--limit LIMIT]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--json` | flag | off | print the proposals as JSON |
| `--apply` | flag | off | rename the issues (the coordinator's, once) |
| `--include-review` | flag | off | with --apply, also rename the proposals marked 'review' |
| `--limit LIMIT` | integer | `500` | how many open issues to read (default: 500) |

### `issue link` {#issue-link-usage}

Make issue N a sub-issue of issue --parent.

```text
coney-tools issue link NUMBER --parent P [--dry-run]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `NUMBER` | integer | required | the child issue |
| `--parent P` | integer | required | the parent issue |
| `--dry-run` | flag | off | print what would run; write nothing |
<!-- command-reference:end -->

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

<!-- command-reference:begin claim -->
## Parameters of `claim` {#claim-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `claim run` {#claim-run-usage}

Handle one GitHub event (the claim workflow's step).

```text
coney-tools claim run --event EVENT --payload PAYLOAD
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--event EVENT` | text | required | the event name: issue_comment, issues, pull_request_target, schedule |
| `--payload PAYLOAD` | path | required | the event's JSON file ($GITHUB_EVENT_PATH) |

### `claim sweep` {#claim-sweep-usage}

Reconcile every claimed issue and every unhandled command now.

```text
coney-tools claim sweep
```

Takes no parameters.

### `claim list` {#claim-list-usage}

Print the held claims.

```text
coney-tools claim list [--json]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--json` | flag | off | print the cards' data as JSON for the mod |
<!-- command-reference:end -->
