# The coney-tools CI commands

The commands the CI workflows run: [`ci`](#ci) (the `dependencies` check and its reruns, the changed-lines clang-tidy,
the issue for a failure on `main`). The repository's own rules are checked by [`repo`](coney-tools-repo.md#repo); the
other groups are listed in [the command line overview](coney-tools.md#groups).

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
  ([clang-tidy](building.md#clang-tidy)).
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
<!-- command-reference:end -->
