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
| `workflow` | `ci.yml` | every workflow runs each job under its own name, pins every action to a full commit SHA and sets `permissions:`, with a write scope only on a Pages deploy job |
| `tests` | `ci.yml` | the `coney-tools` unit tests |
| `markdownlint` | `ci.yml` | every Markdown file, with the version and rules the commit hook uses |
| `conventions` | `ci.yml` | `.github/conventions.yml` is well formed, and the pull request template and issue forms match it |
| `site` | `ci.yml`, `pages.yml` | the documentation site builds with `--strict`, from the hash-locked `requirements-docs.txt` |
| `mod` | `ci.yml` | every mod under `mods/` validates and passes its tests, on the Claude Code version the implementer image pins |
| `verify` | `ci.yml` | every research page's verdict, two signatures and hashes (`cleanroom verify`) |
| `orig-check` | `ci.yml` | every `@orig` tag in the engine names an address and job on a gated page (`cleanroom orig-check`) |
| `commit-sigs` | `ci.yml`, pull requests only | every engine commit of the pull request is signed by an allowed implementer key (`cleanroom commit-sigs`) |
| `disclosure` | `pr.yml` | the pull request's title, description and commit messages describe the process in neutral words |

`ci.yml` runs on every pull request and every push to `main`; its `build-ok` job needs every other job, so it is the
one check of `ci.yml` that branch protection requires. `pr.yml` runs on pull requests only, again whenever the
description is edited, and adds two checks from [Groundwork](https://github.com/Ryubara/groundwork)'s checker:
`body` (the description follows the template) and `docs` (a change to code changes the docs too, or the description
says `Docs: none`). Coney's titles are a short imperative summary with no area ([AGENTS.md](../AGENTS.md)), so
Groundwork's title check is not used.

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

## Going public

The repository's settings live as files and are applied on GitHub when the repository is made public (a private
repository may not offer rulesets), from the root of a checkout, with `gh` logged in as a maintainer. Each command
shows the change first; run it again with `--apply` to make it.

```sh
gw="uv run --no-project --with-requirements .github/scripts/requirements.txt python .github/scripts/groundwork.py"
$gw labels -R Ryubara/coney      # the labels in conventions.yml
$gw settings -R Ryubara/coney    # squash merge only, PR title and body as the commit, delete merged branches
$gw ruleset -R Ryubara/coney     # main: no deletion or force-push, pull requests only, linear history, checks
```

1. Before the repository is made public, check every commit message of its history:
   `coney-tools ci --job disclosure --range HEAD`.
2. Make it public, then apply the labels and the settings.
3. Once a pull request has run the required checks (`body`, `docs`, `disclosure`, `build-ok`), apply the ruleset.
4. In the repository's Actions settings: the default token read-only, and approval required before workflows run
   for outside contributors.
5. Publish the documentation site: in the repository's Pages settings, set the source to "GitHub Actions"; set
   the repository variable `gh variable set PAGES_ENABLED --body true -R Ryubara/coney`; run the workflow once,
   `gh workflow run pages.yml -R Ryubara/coney`, and check <https://ryubara.github.io/coney/>; then show the site on
   the repository's page: `gh repo edit Ryubara/coney --homepage https://ryubara.github.io/coney/`.
6. `groundwork.py check -R Ryubara/coney` reports any drift from `conventions.yml` afterwards.
