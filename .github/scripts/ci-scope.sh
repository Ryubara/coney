#!/usr/bin/env bash
# Decides how much of CI a run needs, so a pull request that changes only documentation does not queue the builds.
# Run inside a checkout whose HEAD is a pull request's merge commit (fetch depth 2, so HEAD^1 is the branch it merges
# into) or any other commit. Reads EVENT (github.event_name) and DRAFT ("true" for a draft pull request) from the
# environment and appends to $GITHUB_OUTPUT (or prints, when it is unset):
#
#   full=true|false      build, sanitizers and clang-tidy run, and python runs on every OS
#   python=true|false    python runs at all
#   python-os=<JSON>     the OSes python runs on
#
# Pushes, the nightly run and manual runs always get everything. A draft pull request gets only the cheap checks (the
# title, commits, docs, format and docs workflows); marking it ready for review runs the rest. A ready pull request
# whose every changed file is documentation (DOCS_ONLY below) skips the engine builds, and runs python on Linux only:
# python's repository checks read docs/, research/ and README.md, but do not depend on the OS. Anything else, or a
# file this script does not recognise, runs everything.
set -euo pipefail

# Paths that never reach the engine build or the python package's behaviour: docs/, research/, the Markdown files at
# the top level and in .github/ (not those under python/, whose tests may read them), the conventions file (whose
# checks run on Linux anyway) and the docs site's own setup. research/ includes research/traces/, which the python
# tests read; that is why a docs-only change still runs python on Linux.
# This is `paths.docs_only` of .github/conventions.yml written as a regular expression, and a test
# (python/tests/test_conventions_files.py) fails when the two differ.
DOCS_ONLY='^(docs/|research/|[^/]+\.md$|\.github/[^/]+\.md$|\.github/conventions\.yml$|mkdocs\.yml$|mkdocs_hooks\.py$|requirements-docs\.txt$|\.markdownlint[^/]*$)'

all_os='["ubuntu-24.04","windows-2022","macos-15"]'
linux_os='["ubuntu-24.04"]'

full=true
python=true
python_os="$all_os"

if [[ "${EVENT:?}" == pull_request ]]; then
  if [[ "${DRAFT:-false}" == true ]]; then
    full=false
    python=false
    echo "Draft pull request: only the cheap checks run."
  else
    # --no-renames lists both sides of a move, so moving a file out of src/ counts as a code change.
    changed="$(git diff --name-only --no-renames HEAD^1 HEAD)"
    echo "Changed files:"
    printf '%s\n' "$changed"
    if [[ -n "$changed" ]] && ! grep -Evq "$DOCS_ONLY" <<<"$changed"; then
      full=false
      python_os="$linux_os"
      echo "Documentation only: the engine builds are skipped and python runs on Linux only."
    fi
  fi
fi

{
  echo "full=$full"
  echo "python=$python"
  echo "python-os=$python_os"
} >>"${GITHUB_OUTPUT:-/dev/stdout}"
