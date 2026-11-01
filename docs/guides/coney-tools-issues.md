# The coney-tools issue commands

The commands that draft, check and create issues, and keep the claims on them: [`issue`](#issue) and [`claim`](#claim)
(the issue claims). [`task`](coney-tools-repo.md#task) claims issues and writes a task file. The other groups are
listed in [the command line overview](coney-tools.md#groups).

## issue {#issue}

```sh
uv run --project python coney-tools issue template [task|bug]
uv run --project python coney-tools issue check --title T --body FILE [--form F] [--leftover] [--online] [--attach F]
uv run --project python coney-tools issue create --title T --body FILE [--label L ...] [--parent N] [--leftover]
    [--attach F] [--offline] [--dry-run]
uv run --project python coney-tools issue edit N --body FILE [--title T] [--offline] [--dry-run]
uv run --project python coney-tools issue set-parent N --parent P [--dry-run]
uv run --project python coney-tools issue titles [--json] [--apply [--include-review]] [--limit N]
uv run --project python coney-tools issue impact [--base REF] [--head REF] [--replaces PR_OR_BRANCH ...]
    [--description FILE] [--json] [--refresh] [--issues FILE]
```

Drafts, checks and files issues in their form's exact shape ([Filing an issue](issues.md#filing)). Every rule is
read from `issues` in `.github/conventions.yml` and from the form files in `.github/ISSUE_TEMPLATE/`; nothing is
copied into the code.

* `template` prints a blank body: the form's `###` headings in order, each preceded by an HTML comment with the form's
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
  `issue set-parent` command to run if only that step fails. `--dry-run` checks and prints the `gh` commands, running
  none.
* `edit` runs the same check on the new body (with `--title`, or the issue's current title read with `gh`) before
  `gh issue edit`. `set-parent` makes an existing issue a sub-issue of another.
* `titles` lists the open issues whose title breaks the title rule, each with a proposed title, how sure the proposal is
  (`auto`, or `review` when a verb or an area was guessed or the title was cut) and the `gh issue edit` command that
  applies it; it changes nothing. `--apply` runs the `auto` renames, `--include-review` the others too (the
  coordinator does that once, when the rule starts). Exit 1 while some title is wrong.
* `impact` lists the open issues a branch may have changed, for the pull request's Issues affected section
  ([below](#issue-impact)). It reads only; it is a helper, and nothing requires that it was run.

Every argument and option of `titles`:

| Option | Default | What it does |
| --- | --- | --- |
| `--json` | off | Print the proposals as JSON (`number`, `current`, `proposed`, `confidence`, `why`) instead of lines and commands |
| `--apply` | off | Rename the issues whose proposal is `auto` with `gh issue edit`; reports a failed rename and goes on |
| `--include-review` | off | With `--apply`, rename the `review` proposals too |
| `--limit N` | 500 | How many open issues to read |

The body is filed without the draft's comments. Exit 2 when the conventions, a form file or the body file cannot be
read.

### issue impact {#issue-impact}

Every pull request lists, in its **Issues affected** section, the open issues it partly fixes, changes the behaviour of
or makes out of date ([Issues a pull request changes](issues.md#affected)). `issue impact` suggests that list from the
diff, so the author reads a handful of issues instead of searching three hundred. It is a helper for whoever has the
tools: a pull request made by hand writes `None (one-line fix)` or its own list, and the check
([`pr check-body`](coney-tools-repo.md#repo)) accepts either. The suggestion is never the section itself: the author
reads each issue and lists the ones the change really affects, with a line on how. [`pr text`](coney-tools-pr.md#pr-text)
runs it (`--json`, for the unit's base and tip) when the engineer's `PR.md` has no Issues affected section, and writes
the candidates, less the issues the task finishes, into a `TODO:` that the checks refuse until the engineer replaces it
([What is generated](pr.md#generated)).

What it reads from the branch (`git diff -U0` from the merge base with `--base` to `--head`):

* the changed paths, a renamed file under both names;
* the `@orig` addresses and Coney names of the functions whose body or tags changed, and of those that only
  [moved](orig-body-hash.md#moved) (the same reader as
  [`orig check-review`](coney-tools-fidelity.md#orig)), and every `0x????????` address in a changed line;
* the code-like names (an underscore, `::`, a digit or an inner capital) in changed lines and hunk headers;
* for a changed Markdown page, the anchors of the sections whose lines changed, with the enclosing sections;
* the pull requests and branches it replaces (`--replaces`, and `Replaces #N` lines in the commit messages), and the
  issues it fixes (`Fixes #N` in a commit message or the `--description` file), which are left out.

It matches them against the open issues' bodies and says why each matched, strongest first:

| Kind | The issue |
| --- | --- |
| `replaced` | cites a pull request (`#630`) or a branch this change replaces |
| `branch` | names this branch |
| `address` | cites an `@orig` address the change touches |
| `section` | cites `page.md#anchor` for a section the change edits, by the anchors of the base as well as the head, so a heading the change removed or renamed still matches (a section it does not edit is no match) |
| `path` | cites a file the change adds, modifies, deletes or renames |
| `folder` | cites a folder (`src/ai/`) the change touches a file in |
| `name` | names, in a code span, a function or constant that a changed line holds |

A name or address that only a changed test file holds is not counted (invented fixture values would match real
issues by chance); the test file's path still is.

An address, path or name that more than `issues.impact.common_max_issues` open issues cite says little about any one
of them and is skipped. The open issues come from `gh issue list`, kept in the scratch folder
(`issue-impact/open-issues.json`) for `issues.impact.cache_minutes`; if `gh` cannot be reached an older cache is used
with a note, and with none the command exits 2.

| Option | Default | What it does |
| --- | --- | --- |
| `--base REF` | `origin/` and the default branch | The branch the change is made against |
| `--head REF` | `HEAD` | The tip of the change; its branch name is matched too |
| `--replaces N` or `BRANCH` | none | A pull request number or a branch the change replaces; may be repeated |
| `--description FILE` | none | The pull request text; its `Fixes #N` issues are left out |
| `--json` | off | Print the report as JSON (below) |
| `--refresh` | off | Read the open issues from GitHub even when the cache is fresh |
| `--issues FILE` | none | Read the open issues from a `gh issue list --json` file instead of GitHub (offline, tests) |

The text output is one line per issue, `#N  title  --  why; why`, at most `issues.impact.max_reasons` reasons each,
then the count, the issues left out and any notes. `--json` is a stable output for tools to read and keeps this
shape (`version` is 1; a key that changes meaning raises it):

```json
{
  "version": 1,
  "base": "origin/main", "head": "HEAD", "branch": "tools-example",
  "touched": {"paths": 12, "addresses": 3, "names": 40, "sections": 5},
  "issues": [
    {"number": 12, "title": "...", "url": "...", "labels": ["task", "A-ai"], "strength": "address",
     "reasons": [{"kind": "address", "what": "0x001490b8", "detail": "cites 0x001490b8: ..."}]}
  ],
  "excluded": [{"number": 7, "why": "this change fixes it (Fixes #N): merging closes it"}],
  "notices": []
}
```

Exit 0 whether or not anything matched, 2 when a ref, the `--description` or `--issues` file or the open issues cannot
be read.

<!-- command-reference:begin issue -->
## Parameters of `issue` {#issue-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `issue template` {#issue-template-usage}

Print a blank issue body in the form's exact headings, with the rules of each section as comments.

```text
coney-tools issue template [FORM]
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

### `issue impact` {#issue-impact-usage}

List the open issues whose bodies cite what the branch touches (for the Issues affected section).

```text
coney-tools issue impact [--base BASE] [--head HEAD] [--replaces PR_OR_BRANCH] [--description FILE] [--json] [--refresh]
    [--issues FILE]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--base BASE` | text | none | the branch the change is made against (default: origin/ and the default branch) |
| `--head HEAD` | text | `HEAD` | the tip of the change (default: HEAD) |
| `--replaces PR_OR_BRANCH` | text | none | a pull request number or a branch the change replaces; the commits' 'Replaces #N' lines count too. Repeatable. |
| `--description FILE` | text | none | the pull request text: its 'Fixes #N' issues are left out of the list |
| `--json` | flag | off | print the report as JSON (version 1, a stable output for tools to read) |
| `--refresh` | flag | off | read the open issues from GitHub even if the cache is fresh |
| `--issues FILE` | text | none | read the open issues from this `gh issue list --json` file, not GitHub |

### `issue set-parent` {#issue-set-parent-usage}

Make issue N a sub-issue of issue --parent.

```text
coney-tools issue set-parent NUMBER --parent P [--dry-run]
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
uv run --project python coney-tools claim unclaim ISSUE [ISSUE ...] [--no-wait]
uv run --project python coney-tools claim renew ISSUE [ISSUE ...] [--no-wait]
uv run --project python coney-tools claim check-pr --payload FILE
uv run --project python coney-tools claim is-command TEXT
```

The claim Action's commands ([Claiming an issue](issues.md#claiming)); `.github/workflows/claim.yml` only starts them.
`unclaim` and `renew` are the coordinator's way to change a claim, since nobody writes a command comment by hand.
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
* `unclaim` gives claims back and `renew` starts their idle clock again: each posts the command (`/unclaim`,
  `/claim renew`) on every issue named, as the signed-in account, the way `task create` posts `/claim`, and the claim
  Action acts on it like anyone's. It first checks what the Action would refuse (no claim on the issue; a claim of
  someone else, which only a maintainer may give back and only the claimant may renew) and then posts nothing, with
  the Action's words. After posting it waits, at most `claims.create_wait_seconds` per issue, until the card lists the
  command as handled (`done`) or a reply refuses it. A wait that runs out stops the command: the Action runs one job at
  a time, so the next issue would wait as long; it names the issues it did not try, and the command it posted stays
  for the next run or the sweep. A hook refuses both to the engineer, tooling and reviewer roles.
* `check-pr` is the `claim` job of `.github/workflows/pr.yml`: every issue the pull request in the event file closes
  (`Fixes #N`) must hold a claim of its author, or the author is a maintainer ([Work starts with a
  claim](issues.md#claiming)). A bot's pull request is not checked. With `claims.require_on_pr` false each problem is
  a `::warning::` annotation and the exit is 0; true makes them `::error::` and exit 1.
* `is-command` exits 0 and prints the kind when the text is exactly one claim command of `claims.commands` (a stage
  `/progress` knows), and 1 with the reason otherwise; `-` reads the text from stdin. It is for the user-level
  no-comment-as-user hook, which is outside the repository: once the maintainer installs the updated hook it asks this
  command before it lets a `gh issue comment` through, so the command shapes are read from the conventions, not copied
  into the hook (the installed one still copies them).

Every argument and option:

| Command | Argument or option | Default | What it does |
| --- | --- | --- | --- |
| `run` | `--event NAME` | required | The GitHub event's name: `issue_comment`, `issues`, `pull_request_target` (also `pull_request`), `schedule` or `workflow_dispatch`; any other name is an error (exit 2) |
| `run` | `--payload FILE` | required | The event's JSON file (`$GITHUB_EVENT_PATH`); it must hold a JSON object. Only the issue or pull request number, a pull request's `Fixes #N` lines (at most `claims.closing_per_event`), its action and its author are taken from it |
| `sweep` | none | | Reconciles every issue that may need it, enforces the limits, lists the orphans and deletes old replies |
| `list` | `--json` | off | Prints the held claims as JSON (issue, claimant, status, task, scope, stage, pull request, `claimed_at`, `last_activity`, `deadline` (null while the clock waits; an issue with the trial label uses the trial's hours), `waiting` (what it waits for), `warned`, `adopted`, `exempt` (outside the limit: `claims.exempt_label`), `counted` (whether the claim counts toward the open-claims limit: false when exempt, when a maintainer's stage freed its slot or while the claimant's own ready pull request frees it)) instead of one line each |
| `unclaim`, `renew` | `ISSUE ...` | required | The issues whose claims to give back or renew, handled in turn |
| `unclaim`, `renew` | `--no-wait` | off | Posts and returns without waiting for the Action's answer |
| `check-pr` | `--payload FILE` | required | The `pull_request` event's JSON file (`$GITHUB_EVENT_PATH`): the author, the author's association and the description are read from it, the claim cards from GitHub |
| `is-command` | `TEXT` | required | The comment's whole text, or `-` for stdin |

It needs `gh` signed in (the Action passes `GH_TOKEN`). Exit 1 when an issue could not be finished (for `unclaim` and
`renew`: refused, or no answer in time), 2 when the payload or the conventions cannot be read.

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

### `claim unclaim` {#claim-unclaim-usage}

Give claims back: post /unclaim on each issue and wait for the claim Action.

```text
coney-tools claim unclaim ISSUE [ISSUE ...] [--no-wait]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `ISSUE [ISSUE ...]` | integer | required | the issue numbers |
| `--no-wait` | flag | off | post and return without waiting for the answer |

### `claim renew` {#claim-renew-usage}

Start the idle clock of your claims again: post /claim renew on each issue and wait.

```text
coney-tools claim renew ISSUE [ISSUE ...] [--no-wait]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `ISSUE [ISSUE ...]` | integer | required | the issue numbers |
| `--no-wait` | flag | off | post and return without waiting for the answer |

### `claim check-pr` {#claim-check-pr-usage}

Check that the issues a pull request closes are claimed by its author (the pr workflow).

```text
coney-tools claim check-pr --payload PAYLOAD
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--payload PAYLOAD` | path | required | the pull_request event's JSON file |

### `claim is-command` {#claim-is-command-usage}

Exit 0 when the text is exactly one claim command, else 1.

```text
coney-tools claim is-command TEXT
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `TEXT` | text | required | the comment's whole text; - reads it from stdin |
<!-- command-reference:end -->
