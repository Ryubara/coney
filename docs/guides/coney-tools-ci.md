# The coney-tools CI commands

What CI checks: [`ci`](#ci), the local gate that runs CI's checks on the files a change touched
([`ci run`](#ci-run)), and the commands the CI workflows run (the `dependencies` check and its reruns, the
changed-lines clang-tidy, the issue for a failure on `main`). The repository's own rules are checked by
[`repo`](coney-tools-repo.md#repo); the other groups are listed in [the command line overview](coney-tools.md#groups).

## ci {#ci}

```sh
uv run --project python coney-tools ci dependencies (--pr N | --body FILE)
uv run --project python coney-tools ci rerun-dependencies [--apply]
uv run --project python coney-tools ci tidy-plan --diff FILE|- --out FILE
uv run --project python coney-tools ci tidy-run --plan FILE --build-dir DIR [--clang-tidy PROGRAM] [--jobs N]
    [--sarif FILE] [--summary FILE] [--annotate]
uv run --project python coney-tools ci report-failure --run ID [--apply]
```

The commands the CI workflows run, so that a workflow holds no logic and a person can run the same command by hand. The
settings are the `ci` section of [.github/conventions.yml](repo:.github/conventions.yml) ([Repository
conventions](repo-conventions.md)); a missing or malformed file is a one-line error. Exit code 2 means `gh`, git or the
conventions could not be read. [What CI runs](building.md#what-ci-runs) says where each runs.

* `dependencies` reads a pull request's description, from GitHub as it is now (`--pr`) or from a file (`--body`), for
  lines `Depends on #N` (`pull_requests.stacked.depends_line`: several numbers may share a line, joined by commas or
  `and`; a comment or a sentence that only mentions the words is not a line) and asks GitHub whether each pull
  request is merged. It prints `dependencies: ok` or one line per pull request still waited on (open, closed without
  merging, not a pull request, or the pull request itself) and exits 1. The `dependencies` job of the `dependencies`
  workflow runs it with `--pr`; it reads the description when it runs, not from the event, so a rerun sees the text as
  it is. Exit 1 when something is waiting.
* `rerun-dependencies` reads the open pull requests and, for each whose `Depends on` pull requests are all merged and
  whose newest `dependencies` run failed, lists that run; with `--apply` it restarts it (`gh run rerun --failed`). At
  most `ci.dependencies.rerun_limit` runs are restarted in a pass (the rest are `capped` and the next push to `main`
  restarts them). The workflow runs it with `--apply` on every push to `main`, which is when a dependency merges.
* `tidy-plan` reads a unified diff (`git diff -U0 --no-renames base...head`, `-` for stdin) and writes the plan of the
  changed-lines clang-tidy (a pull request, a push): the sources to check and the changed lines to report. A changed
  source is checked itself; a changed header through **one** source that includes it (the one with the header's name,
  else the nearest). A header that no source includes is noted and left to the nightly pass. A change to the build setup
  (`ci.tidy.whole_when`: the top `CMakeLists.txt`, `cmake/`, `CMakePresets.json`, `.clang-tidy`, also a deleted one)
  makes it a plan for every file: the job then runs the whole-codebase pass instead of `tidy-run`, and `tidy-run`
  refuses such a plan. It prints the files, and sets `run` and `all` (`true` or `false`) in `$GITHUB_OUTPUT` when that
  is set: `all=true` sends the job to the whole pass. Only the folders and extensions of `ci.tidy` count otherwise
  ([clang-tidy](clang-tidy.md)).
* `tidy-run` runs `clang-tidy` on the plan's sources, `--jobs` at a time (default: the cores), against the compile
  database in `--build-dir`, with the plan's line filter (one too long for an argument, over 100 KB, is left off and
  applied afterwards, with the same result), and prints each finding as `file:line:col: error: message [check]`.
  `--sarif FILE` writes the findings as SARIF 2.1.0 (also when there are none) for code scanning; `--summary FILE`
  appends a Markdown summary (`$GITHUB_STEP_SUMMARY`); `--annotate` also prints each as a `::error` workflow command,
  for a pull request from a fork whose token cannot upload SARIF. Exit 1 on any finding, and when clang-tidy itself
  fails on a source without printing a finding (a missing program or compile database, a crash) or a source does
  not compile (a compiler error on any line); 2 when the plan cannot be read.
* `report-failure` reads a workflow run (`--run`: its id) and, when it is a failure of a workflow in
  `ci.failure.workflows` on the default branch from an event in `ci.failure.events`, opens an issue in the task form
  with the labels `ci.failure.labels` (`P-critical` among them): the failed jobs (a cancelled job is not one), the errors
  of the failed steps' log, and the files the log names with their last `ci.failure.commits_per_file` commits. When the
  workflow already has an open issue (found by `<!-- coney-ci-failure:<workflow> -->` in its body) it adds a comment
  instead, and a run already on the issue is not repeated. Without `--apply` it prints the title and text it would write;
  any other run is reported as `skipped` with the reason.

Every argument and option:

| Command | Argument or option | Default | What it does |
| --- | --- | --- | --- |
| `dependencies` | `--pr N` | none | The pull request whose description is read from GitHub and checked |
| `dependencies` | `--body FILE` | none | A file holding the description; with no `--pr` the pull request is not checked for depending on itself. One of the two is required |
| `rerun-dependencies` | `--apply` | off | Restart the runs (`gh run rerun --failed`); without it they are only listed |
| `tidy-plan` | `--diff FILE` | required | The unified diff of the change; `-` reads stdin |
| `tidy-plan` | `--out FILE` | required | Where the plan (JSON) is written |
| `tidy-run` | `--plan FILE` | required | The plan `tidy-plan` wrote |
| `tidy-run` | `--build-dir DIR` | required | The folder holding `compile_commands.json`, relative to the checkout |
| `tidy-run` | `--clang-tidy PROGRAM` | `clang-tidy` | The clang-tidy to run (CI passes `clang-tidy-19`) |
| `tidy-run` | `--jobs N` | the cores | clang-tidy processes at once |
| `tidy-run` | `--sarif FILE` | none | Write the findings as SARIF |
| `tidy-run` | `--summary FILE` | none | Append a Markdown summary to this file |
| `tidy-run` | `--annotate` | off | Also print the findings as `::error` workflow commands |
| `report-failure` | `--run ID` | required | The workflow run's id |
| `report-failure` | `--apply` | off | Create the issue or the comment; without it the text is printed |

### ci run and ci result {#ci-run}

```sh
uv run --project python coney-tools ci run [--base origin/main] [--only STEP ...] [--skip STEP ...]
    [--detach]
uv run --project python coney-tools ci result
```

The local gate for the files a change touched ([Testing](testing.md#local-gate)): run it in your worktree before you
hand a unit over. The changed files are everything added or modified since your branch left `--base` (the merge
base), committed or not, with untracked files. Each step runs only when the change touches what it checks, prints one
line, and any failure makes the command exit 1:

| Step | Runs when | What it does |
| --- | --- | --- |
| `repo` | always | `repo audit --files`: `.github/conventions.yml` loads, and the agent pointer files, the generated blocks, the issue forms, the pull request template, the CI scope script and pre-commit's size limit agree with it |
| `orig` | a file under `src/` changed | [`orig check-tags`](coney-tools-fidelity.md#orig): a new function in a gameplay folder carries `@orig` or `@coney-only`, and every folder of `src/` is listed as gameplay or Coney-only |
| `pre-commit` | a file changed | `pre-commit run --files` on the changed files: clang-format, ruff, whitespace, the 512 KB guard, the real-clock rule |
| `docs` | a file matching `paths.docs_only` of `.github/conventions.yml` changed (`docs/`, `research/`, top-level Markdown, `mkdocs.yml`, the MkDocs hooks and theme overrides, the shared artwork, `requirements-docs.txt`, ...), or the python the build imports (`python/src/coney_tools/progress*.py`, `research*.py`, `config.py`) | [`docs check`](coney-tools-build.md#docs) on the changed pages, then `mkdocs build --strict` into a throwaway folder; both run, so one run shows every problem |
| `python` | `python/` or `.claude/` changed (the tests also run the hooks) | `ruff check` and `ruff format --check` over `python` and `.claude/hooks`, `pytest python/tests` and `mypy --config-file python/pyproject.toml python/src python/tests` |

**clang-tidy is not a step.** It is not run locally, on Windows least of all (no local WSL builds or clang-tidy
runs): CI tidies every pull request on the lines it changes, in its own required check, and the whole codebase
nightly ([building](clang-tidy.md)). A finding comes back as an annotation on the pull request and is fixed
forward on the branch. Every program the command starts runs below normal priority, like a build's (Windows:
below-normal priority class; elsewhere `nice -n 5`).

`--detach` runs the whole gate in the background, apart from the shell, and returns at once; `ci result` prints its
output and exits with its code (75 while it runs, as `build result` does, and 1 when its process is gone; "still
running" is judged as for `build result`, by the pid and when that process started). The detached run is recorded
under the worktree's name (`CONEY_AGENT`, else the folder's name).

CI runs the `repo` and `pre-commit` steps on Windows, Linux and macOS in its `python` workflow, so the command's
handling of paths, programs and long command lines is tested on all three.

`--only` and `--skip` take step names, for a rerun of the one that failed. Exit codes: 0 all passed, 1 a step failed, 2
a setup problem (a program the step needs is missing), 75 from `ci result` while a detached run goes on. The command
prints at most 40 lines of a failing step; run the step's tool by hand for the rest. What stays CI's: warnings only GCC
emits, macOS, the sanitizers and clang-tidy (see [the local gate](testing.md#local-gate)).

### ci pr {#ci-pr}

```sh
uv run --project python coney-tools ci pr --title TITLE --body FILE [--base origin/main] [--only STEP ...]
    [--skip STEP ...] [--preset dev] [--agent ID]
```

Everything CI checks on a pull request, run before it is opened: [`ci run`](#ci-run) plus the pull request's
text and the engine. It is what a person runs before `gh pr create`, whatever agent tool made the change: the hooks
of every tool refuse a bad command before it runs ([Agent tools](enforcement.md#harnesses)), and this is the backstop
that sees the whole change ([Agent tools other than Claude Code](agents.md#other-tools)).
`--title` is the title and `--body` a file holding the description written from the
[template](repo:.github/pull_request_template.md). The steps run in this order and print one line each; the five
between the text steps and `engine` are the steps of `ci run`, with the same rules:

| Step | CI job it is | What it does |
| --- | --- | --- |
| `title` | `title` | [`pr check-title`](coney-tools-pr.md#pr-check-title-usage): `area: Verb the rest`, the length, the tense, no trailing period |
| `body` | `body` | [`pr check-body`](coney-tools-pr.md#pr-check-body-usage) with the title, so the `Runtime:` line of a title a player sees is checked, and with the branch's commits, so the `Model:` line names every model a `Co-Authored-By` trailer names (CI sees only the description and cannot) |
| `docs-rule` | `docs` | [`pr check-docs`](coney-tools-pr.md#pr-check-docs-usage): a change to `src/` or `python/src/` changes `docs/` or `research/`, or the description has `Docs: none` |
| `repo`, `orig`, `pre-commit`, `docs`, `python` | `python`, `format`, `markdownlint`, `mkdocs` | the steps of `ci run` above |
| `engine` | `build-ok` | `coney-tools build --preset dev --test` (`--preset` and `--agent` change it), when a changed file is in the `engine` class of `paths` in `.github/conventions.yml` or in no class (CI runs everything for it); skipped for a change to docs or tools alone. It takes as long as a build and the tests do, so `--skip engine` leaves it out for a first pass |

`--only` and `--skip` take these step names. `--base` (default `origin/main`) is the branch the change is measured
from, for the files and for the `Model:` rule's commits: in a fork `origin` is the fork, so pass `--base upstream/main`
after fetching it, and a cloud checkout of one branch needs the base fetched first
([agents.md](agents.md#other-tools)). `docs-rule` and `engine` read the files the way CI's `docs` job lists them for the
pull request: the committed changes since the base, deletions included, so commit first (the other steps also look at
what is uncommitted, as `ci run` does). `--agent` is the id of the engine step's build; without it the step
uses `build`'s own default for a person at a terminal, else the worktree's name (`CONEY_AGENT`, else the folder).
The description must not hold a picture that is a local file (a picture has no address before the pull request exists):
write `Screenshots: none (<reason>)` for the check and add the picture on GitHub after opening
([CONTRIBUTING.md](repo:CONTRIBUTING.md)). The exit codes are those of `ci run`: 0 all passed, 1 a step
failed (CI would refuse the pull request too), 2 a setup problem (the description file cannot be read, a program is
missing). What stays CI's: the other compilers, macOS, the sanitizers and clang-tidy on the changed lines, and the
checks that need the pull request itself (the `Fixes #N` issues' state, `dependencies`).

CI's `pr` workflow (`title`, `docs`, `body`) runs on a fork's pull request without a maintainer's approval
([Workflows](repo-conventions.md#pr-workflow)); its `build`, `python` and `docs` workflows run the contributor's code, so
GitHub holds them for a first-time contributor until a maintainer approves them. This command is how the contributor
finds out first.

<!-- command-reference:begin ci -->
## Parameters of `ci` {#ci-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `ci dependencies` {#ci-dependencies-usage}

Fail while a pull request this one says it `Depends on` is not merged.

```text
coney-tools ci dependencies [--pr N] [--body FILE]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--pr N` | integer | none | the pull request; its description is read from GitHub |
| `--body FILE` | text | none | a file holding the description (no --pr: not checked for itself) |

### `ci rerun-dependencies` {#ci-rerun-dependencies-usage}

Restart the failed `dependencies` run of each pull request whose dependencies have all merged.

```text
coney-tools ci rerun-dependencies [--apply]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--apply` | flag | off | really restart the runs (gh run rerun); default: list them |

### `ci report-failure` {#ci-report-failure-usage}

Open a P-critical issue for a failed run on the default branch (or comment on the open one).

```text
coney-tools ci report-failure --run ID [--apply]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--run ID` | integer | required | the workflow run's id |
| `--apply` | flag | off | really create the issue or comment (default: print it) |

### `ci tidy-plan` {#ci-tidy-plan-usage}

Choose the files and lines clang-tidy checks for a pull request's diff.

```text
coney-tools ci tidy-plan --diff FILE --out FILE
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--diff FILE` | text | required | the diff; - reads stdin |
| `--out FILE` | text | required | where the plan (JSON) is written |

### `ci tidy-run` {#ci-tidy-run-usage}

Run clang-tidy as a plan says and report only the changed lines.

```text
coney-tools ci tidy-run --plan FILE --build-dir DIR [--clang-tidy PROGRAM] [--jobs N] [--sarif FILE] [--summary FILE]
    [--annotate]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--plan FILE` | text | required | the plan `tidy-plan` wrote |
| `--build-dir DIR` | text | required | the folder holding compile_commands.json |
| `--clang-tidy PROGRAM` | text | `clang-tidy` | default: clang-tidy |
| `--jobs N` | integer | none | clang-tidy processes at once (default: the cores) |
| `--sarif FILE` | text | none | write the findings as SARIF (also when there are none) |
| `--summary FILE` | text | none | append a Markdown summary to this file |
| `--annotate` | flag | off | also print ::error workflow commands (a fork's token cannot upload SARIF) |

### `ci run` {#ci-run-usage}

Pre-commit, docs, Python and repo checks on what changed (clang-tidy runs in CI).

```text
coney-tools ci run [--base BASE] [--only ONLY] [--skip SKIP] [--detach]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--base BASE` | text | `origin/main` | the branch the change is measured from |
| `--only ONLY` | choice | none | run only this step (repeatable). One of `repo`, `orig`, `pre-commit`, `docs`, `python`. |
| `--skip SKIP` | choice | none | leave this step out (repeatable). One of `repo`, `orig`, `pre-commit`, `docs`, `python`. |
| `--detach` | flag | off | run in the background, apart from this shell, and return at once; `ci result` shows the outcome |

### `ci result` {#ci-result-usage}

The output and outcome of the worktree's last `ci run --detach`.

```text
coney-tools ci result
```

Takes no parameters.
<!-- command-reference:end -->
