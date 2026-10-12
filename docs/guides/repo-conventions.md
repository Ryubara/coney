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
| A rule a program checks (a title, a description, a label, a limit, a path class) | `.github/conventions.yml` | `coney-tools` reads it; nothing hard-codes the value |
| A rule that GitHub reads from its own file (the issue forms, the pull request template, the ruleset) | Its own file | A test checks it against `conventions.yml`, so a change to one without the other fails |
| Tool configuration (`.clang-format`, `.pre-commit-config.yaml`, `mkdocs.yml`, `CMakePresets.json`, ...) | Its own file, in its own format | `repo.tools` names the file; its values are never copied |
| Judgement and reasons (the clean room, evidence levels, roles, how to write a good issue) | The guide that owns the topic | Prose explains why; where it needs a value it names the key |

The file is the answer when two places disagree: the old rule files (`commit-conventions.json`, `pr-body-rules.json`
and `labels.yml`) are gone, and so are the constants that repeated their values in the Python code.

## What the file holds

| Section | What it says | Read by |
| --- | --- | --- |
| `repo` | The default branch, the decisions only a maintainer takes (`owner_decisions`) and the tool configuration files | Tests; agents ([AGENTS.md](repo:AGENTS.md)) |
| `paths` | Which paths are code, documentation and documentation-only; generated paths; the agent pointer files; the one file that may hold the repository's address; the large-file limit | `repo check`, `repo check-docs`, `check changed`; a test holds `ci-scope.sh` and pre-commit to it |
| `commits` | The title form, its limit and suffix, the areas and verbs, the body wrap and the `Co-Authored-By` trailer | `repo check-title`, which the commit-title hook and the `title` and `commits` jobs run |
| `pull_requests` | The description's sections and which must hold text, the `Docs: none` line, `Fixes #N`, the Remaining list, the Screenshots rule, the AI use choices, stacked pull requests | `repo check-body`, `repo check-docs`; a test checks the template |
| `merge` | The merge methods, the squash commit's wording, auto-merge, the required checks and who merges: a copy of the GitHub ruleset and settings | Kept for the audit that compares them; no check reads it yet |
| `issues` | The structure of each issue form (field ids, headings, which are required, default labels), the Screenshots line, the `From:` line, links and how many labels of each family an issue carries | A test checks the forms; the rest is kept for the audit |
| `labels` | The label families and every label with its colour and description | `repo labels` |
| `hygiene` | The limits after which an item counts as stale and the weekly report issue | Kept for the hygiene checks |
| `workspace` | The worktree and scratch layout, the free-space minimum and the busy marker | Local tools; hosted checks ignore it |

The commented file is the reference for each key: open `.github/conventions.yml` and read the comment above it.

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
2. If a file keeps a copy of it, change that too: the issue forms in `.github/ISSUE_TEMPLATE/`, the pull request
   template, `.github/scripts/ci-scope.sh` (the `paths.docs_only` list as a regular expression) or pre-commit's
   `--maxkb`. `python/tests/test_conventions_files.py` and `python/tests/test_repo_checks.py` fail with the file and
   the key until they agree.
3. Update the guide that explains the rule in the same commit ([Documentation](repo:AGENTS.md)).
4. Changing the conventions themselves is a maintainer decision (`repo.owner_decisions`).

## Where the reasons are

| Rule | Guide |
| --- | --- |
| Commit titles, squash merging, who merges, the release queue | [How we work](how-we-work.md), [CONTRIBUTING.md](repo:CONTRIBUTING.md#commits) |
| Pull request descriptions: Testing, Screenshots, Remaining | [Testing](testing.md#pr-evidence), [Issues](issues.md#leftovers) |
| Labels, forms, triage | [Issues](issues.md) |
| What CI runs for which paths | [Building and testing](building.md#what-ci-runs) |
| Code style (SPDX, `@orig`, comments) | [Conventions](conventions.md): code, not repository process |
