# The coney-tools pull request commands

The `pr` commands: whether a unit is ready to become a pull request, the text it carries and the trailers of a
commit. [`repo`](coney-tools-repo.md#repo) and [`task`](coney-tools-repo.md#task) are on the repository commands page;
the other groups are listed in [the command line overview](coney-tools.md#groups).

## pr {#pr}

The checks on a pull request and its commits: whether a unit is ready to become a pull request (`pr verify`), the text
it will carry (`pr text`) and the trailers of a commit (`pr check-commit`). The pull-request checks of [repo](coney-tools-repo.md#repo)
(`check-title`, `check-body`, `check-docs`) are planned to move here when the command groups are renamed
([Stamps](stamps.md) says the command names are placeholders for that pass). What the first two check, and what each
refusal asks you to do, is [Releasing a pull request](pr.md).

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

<!-- command-reference:begin pr -->
## Parameters of `pr` {#pr-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

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

### `pr check-commit` {#pr-check-commit-usage}

Check a commit message's trailers and e-mail addresses (the git commit-msg hook runs this).

```text
coney-tools pr check-commit FILE [--model MODEL]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `FILE` | text | required | the commit message file, as git passes it to the commit-msg hook |
| `--model MODEL` | text | none | the id of the model the committing agent runs on (claude-sonnet-5-5): the Co-Authored-By trailer must name it (default: the model in the identity file the Claude Code hooks wrote for this command, if there is one; typed variables are not trusted) |
<!-- command-reference:end -->
