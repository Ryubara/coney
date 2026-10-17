# Checks and CI

Every check CI runs can be run locally first, the same way: each workflow job calls `coney-tools ci --job <name>`,
so a local run and CI cannot drift apart. CI is the last gate, not the first place a problem shows up.

```sh
python -I python/coney-tools.py ci                  # every job, in order, then a summary
python -I python/coney-tools.py ci --job tests      # one job (repeat --job for more)
python -I python/coney-tools.py ci --list           # the jobs
python -I python/coney-tools.py ci --versions       # the pinned tool versions
```

Exit status: 0 every job passed, 1 a job failed, 2 a job could not run (a tool is missing, say). A local run needs
Python 3.11 or later; the `markdownlint`, `mod` and `verify` jobs need Node.js (`npx`), `verify` also `ssh-keygen`
and git. The first run downloads the pinned packages; the `site` and `conventions` jobs keep their virtual
environments under `build/ci/` and make them again only when a lock file changes.

## The jobs

| Job | Workflow | What it checks |
| --- | --- | --- |
| `workflow` | `ci.yml` | every workflow runs each job under its own name, pins every action to a full commit SHA and sets `permissions:`, with a write scope only on a Pages deploy job or on an issue job no pull request or push can start |
| `tests` | `ci.yml` | the `coney-tools` unit tests |
| `markdownlint` | `ci.yml` | every Markdown file, with the version and rules the commit hook uses |
| `ruff` | `ci.yml` | the Python lints and formatting under `python/`, with the ruff version the commit hook pins |
| `conventions` | `ci.yml` | `.github/conventions.yml` is well formed, the pull request template and issue forms match it, and every tracked YAML file parses |
| `site` | `ci.yml`, `pages.yml` | the documentation site builds with `--strict`, from the hash-locked `requirements-docs.txt` |
| `mod` | `ci.yml` | every mod under `mods/` validates and passes its tests, on the Claude Code version the implementer image pins |
| `verify` | `ci.yml` | every research page's verdict, two signatures and hashes (`cleanroom verify`) |
| `orig-check` | `ci.yml` | every `@orig` tag in the engine names an address and job on a gated page (`cleanroom orig-check`) |
| `commit-sigs` | `ci.yml`, pull requests only | every engine commit of the pull request is signed by an allowed implementer key (`cleanroom commit-sigs`) |
| `engine` | `ci.yml` | the engine (the code gate's `engine_root`) configures, builds and passes its CTest suite with GCC and with Clang, through CMake and Ninja; it passes with a note while the engine has no `CMakeLists.txt` |
| `disclosure` | `pr.yml` | the pull request's title, description and commit messages describe the process in neutral words |

`ci.yml` runs on every pull request and every push to `main`; its `build-ok` job needs every other job, so it is the
one check of `ci.yml` that branch protection requires. `pr.yml` runs on pull requests only, again whenever the
description is edited, and adds three checks from [Groundwork](https://github.com/Ryubara/groundwork)'s checker:
`title` (the title is `area: Verb the rest`, with an area of `.github/conventions.yml`), `body` (the description
follows the template) and `docs` (a change to code changes the docs too, or the description says `Docs: none`). A
commit on a branch gets the same title check from the commit-msg hook (`.pre-commit-config.yaml`).

Two more of Groundwork's workflows keep issues in order. `hygiene.yml` runs every Monday at 06:00 UTC (and by hand)
and keeps one comment on the "Repo hygiene" issue listing idle or malformed issues and pull requests, and checks that
the generated files still match the conventions; it never edits an issue. `issue-status.yml` removes the `S-` labels
of a closed issue and gives a reopened one `S-needs-triage`. Neither is a required check. Both get `issues: write`
on the one job that needs it, and only a schedule, a dispatch or an issue event starts them, so no pull request's
code runs with that token.

Locally, `disclosure` and `commit-sigs` check the commits from `main` to `HEAD`; give `--range A..B` for others, and
`--text FILE` for a pull request's title or description. The `markdownlint` job fails when markdownlint-cli2 reports
that it read no file, so a run that checked nothing never passes.

### The documentation site

`pages.yml` publishes the site to GitHub Pages, at <https://ryubara.github.io/coney/>, on every push to `main` (and
by hand, from the Actions tab). Its `site` job is the `site` job above, building into the folder it uploads
(`ci --job site --site-dir DIR`), so the deployed site is the one CI checked. Its `deploy` job alone may write to
Pages: the `workflow` job refuses a write scope on any job that does more than run `actions/deploy-pages`, and
top-level `permissions:` that grant a write. Until the repository variable `PAGES_ENABLED` is `true`, every job of
`pages.yml` is skipped.

### The checker comes from the base branch

A pull request cannot loosen the check it is judged by: the gate jobs (`verify`, `orig-check`, `commit-sigs`) run
`coney-tools` from the branch the pull request merges into, not from the pull request, and read `allowed_signers`,
the pages, the code gate's settings and markdownlint's version and rules from that branch too (`--ref
origin/<base>`). Every workflow gets a read-only token, and no job uses a secret.

### Keeping Actions usage modest

Every job runs once, on Linux, with no matrix. The `site` and `conventions` jobs install their packages through uv,
whose download cache setup-uv keeps between runs, keyed on the lock file.

## Versions

Nothing is installed at "latest". Each version is read from the one file that already pins it:

| Tool | Pinned in |
| --- | --- |
| Python | `python/pyproject.toml`: the lowest `requires-python` allows |
| uv, and through it the exact Python build | `containers/Dockerfile` (`ARG UV`) |
| Node.js, checked against its SHA-256 | `containers/Dockerfile` (`ARG NODE_VERSION`, `ARG NODE_SHA256`) |
| Claude Code | `containers/Dockerfile` (`ARG CLAUDE_CODE_VERSION`) |
| SDL3, built from source and checked against its SHA-256 | `containers/Dockerfile` (`ARG SDL_VERSION`, `ARG SDL_SHA256`) |
| markdownlint-cli2 | `.pre-commit-config.yaml` (the hook's `rev`) |
| MkDocs, Material and everything they pull in | `requirements-docs.in`, locked with hashes in `requirements-docs.txt` |
| PyYAML for Groundwork's checker | `.github/scripts/requirements.in`, locked in `.github/scripts/requirements.txt` |
| Actions | each `uses:` line, by full commit SHA |

The `site` job refuses a lock whose direct pins differ from `requirements-docs.in`; each lock's header names the
command that regenerates it.

## Hooks

`pre-commit install` (with `uvx pre-commit`) sets up the commit hooks, the commit message check, a check that runs
the `workflow` job when a workflow or `ci.py` changes, and a pre-push check that runs the `verify` job when
research pages change.

## Repository settings

The repository's settings live as files and are applied on GitHub with Groundwork's checker, from the root of a
checkout, with `gh` logged in as a maintainer. Each command shows the change first; run it again with `--apply` to
make it. Apply them again after any change to `.github/conventions.yml` that touches labels, merging or the required
checks, once its pull request has merged.

```sh
gw="uv run --no-project --with-requirements .github/scripts/requirements.txt python .github/scripts/groundwork.py"
$gw labels -R Ryubara/coney      # the labels in conventions.yml
$gw settings -R Ryubara/coney    # squash merge only, PR title and body as the commit, delete merged branches
$gw ruleset -R Ryubara/coney     # main: no deletion or force-push, pull requests only, linear history, checks
$gw check -R Ryubara/coney       # any drift of files, labels, settings or ruleset from conventions.yml
```

The ruleset names the required checks (`title`, `body`, `docs`, `disclosure`, `build-ok`); GitHub offers a check
only after a pull request has run it, so a new required check is added to the ruleset after its first run.

## Going public

1. Before the repository is made public, check every commit message of its history:
   `coney-tools ci --job disclosure --range HEAD`.
2. Make it public, then run `$gw check` and apply anything it reports.
3. In the repository's Actions settings: the default token read-only, and approval required before workflows run
   for outside contributors.
4. Publish the documentation site: in the repository's Pages settings, set the source to "GitHub Actions"; set
   the repository variable `gh variable set PAGES_ENABLED --body true -R Ryubara/coney`; run the workflow once,
   `gh workflow run pages.yml -R Ryubara/coney`, and check <https://ryubara.github.io/coney/>; then show the site on
   the repository's page: `gh repo edit Ryubara/coney --homepage https://ryubara.github.io/coney/`.
5. `$gw check` reports any drift from `conventions.yml` afterwards.
