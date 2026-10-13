#!/usr/bin/env bash
# Decides how much of CI a run needs, so a pull request runs only the jobs its change can break.
# Run inside a checkout whose HEAD is a pull request's merge commit (fetch depth 2, so HEAD^1 is the branch it merges
# into) or any other commit. Reads EVENT (github.event_name) and DRAFT ("true" for a draft pull request) from the
# environment and appends to $GITHUB_OUTPUT (or prints, when it is unset):
#
#   build=true|false     the engine jobs run: build, sanitizers and clang-tidy (the build workflow)
#   python=true|false    python runs at all
#   python-os=<JSON>     the OSes python runs on
#
# Pushes, the nightly run and manual runs always get everything. A draft pull request gets only the cheap checks (the
# title, docs, format and docs workflows); marking it ready for review runs the rest. A ready pull request sorts each
# changed file into the first CI scope class of .github/conventions.yml it matches (`paths.docs_only`, then
# `paths.tools_only`, then `paths.engine`) and runs:
#
#   docs_only    nothing more than python on Linux, whose repository checks read docs/, research/ and the rest
#   tools_only   python on every OS (the python package and its OS-dependent local gate changed)
#   engine       the engine jobs; python on Linux (its checks that read C++ files do not depend on the OS)
#   anything else, or no changed file at all: everything (the engine jobs and python on every OS)
#
# so a mixed pull request gets the union of what its files need. The table is in
# docs/guides/building.md#what-a-pull-request-runs. Python always runs on Linux for a ready pull request: its repository
# checks (the conventions, the generated pages, the progress inputs) can read any file.
set -euo pipefail

# The lists are keys of `paths` in .github/conventions.yml, read here so there is one copy of them: a trailing slash
# is a folder, and `*` matches within one folder level (so "*.md" is the Markdown files at the top level only).
# research/ includes research/traces/, which the python tests read; that is one reason docs_only still runs python on
# Linux. `coney-tools repo audit --files` (files.ci-scope) checks that this script still reads the file and can parse
# every list.
#
# A pull request is sorted by the lists of the branch it merges into (HEAD^1), not of its own merge commit: the file is
# itself docs-only, so a pull request that edits src/ and also lists src/ as docs-only would otherwise skip its own
# engine jobs. A change to the lists takes effect for the pull requests after it. With no parent that holds the file
# (a first commit, or a pull request that adds it), the checked-out file is read.
conventions_path=".github/conventions.yml"
conventions="$(dirname "${BASH_SOURCE[0]}")/../conventions.yml"
if [[ "${EVENT:-}" == pull_request ]] && base_text="$(git show "HEAD^1:$conventions_path" 2>/dev/null)"; then
  conventions_text="$base_text"
  echo "ci-scope: path lists from the base (HEAD^1)" >&2
else
  conventions_text="$(cat "$conventions")"
  echo "ci-scope: path lists from the checkout" >&2
fi

# The patterns of one list, one per line: the quoted items under the `  <key>:` line of the file.
patterns_of() {
  printf '%s\n' "$conventions_text" | tr -d '\r' | awk -v key="  $1:" '
    index($0, key) == 1 { inside = 1; next }
    inside && /^    - "/ { sub(/^    - "/, ""); sub(/".*$/, ""); print; next }
    inside && /^[ \t]*(#.*)?$/ { next }
    inside { exit }
  '
}

# One list as an extended regular expression: `dir/` matches the folder, any other pattern the whole path.
regex_of() {
  local pattern alternatives=()
  while IFS= read -r pattern; do
    pattern="${pattern//./\\.}"
    pattern="${pattern//\*/[^/]*}"
    if [[ "$pattern" == */ ]]; then
      alternatives+=("$pattern")
    else
      alternatives+=("$pattern\$")
    fi
  done < <(patterns_of "$1")
  if ((${#alternatives[@]} == 0)); then
    echo "ci-scope.sh: found no paths.$1 in $conventions_path" >&2
    return 1
  fi
  local joined
  joined="$(IFS='|'; printf '%s' "${alternatives[*]}")"
  printf '^(%s)' "$joined"
}

DOCS_ONLY="$(regex_of docs_only)"
TOOLS_ONLY="$(regex_of tools_only)"
ENGINE="$(regex_of engine)"

all_os='["ubuntu-24.04","windows-2022","macos-15"]'
linux_os='["ubuntu-24.04"]'

build=true
python=true
python_os="$all_os"

if [[ "${EVENT:?}" == pull_request ]]; then
  if [[ "${DRAFT:-false}" == true ]]; then
    build=false
    python=false
    echo "Draft pull request: only the cheap checks run."
  else
    # --no-renames lists both sides of a move, so moving a file out of src/ counts as an engine change.
    changed="$(git diff --name-only --no-renames HEAD^1 HEAD)"
    echo "Changed files, each with its class:"
    if [[ -n "$changed" ]]; then
      build=false
      python_os="$linux_os"
      # Each file adds what its class needs; nothing ever takes a job away again.
      while IFS= read -r file; do
        if [[ "$file" =~ $DOCS_ONLY ]]; then
          class=docs_only
        elif [[ "$file" =~ $TOOLS_ONLY ]]; then
          class=tools_only
          python_os="$all_os"
        elif [[ "$file" =~ $ENGINE ]]; then
          class=engine
          build=true
        else
          class=unknown
          build=true
          python_os="$all_os"
        fi
        printf '  %-10s %s\n' "$class" "$file"
      done <<<"$changed"
    else
      echo "  (none: everything runs)"
    fi
    echo "Engine jobs: $build; python on: $python_os"
  fi
fi

{
  echo "build=$build"
  echo "python=$python"
  echo "python-os=$python_os"
} >>"${GITHUB_OUTPUT:-/dev/stdout}"
