#!/usr/bin/env bash
# Prints what clang-tidy must check for a change: the word ALL when only a full pass is safe, otherwise one source
# file per line (possibly none). Usage: tidy-files.sh <base>, run inside a checkout; <base> is the commit the change
# is measured against (a pull request's merge commit passes HEAD^1).
#
# A changed .cpp is checked itself. A changed header is checked through every .cpp that includes it, directly or
# through other headers, found by searching for the project's quoted include paths (`#include "ai/brain.h"`, the
# path relative to src/ or tests/). A change to the build configuration or to the checks themselves can alter any
# file's result, so it asks for ALL. Anything this misses (an include written another way) the nightly full pass
# catches.
set -euo pipefail

base="${1:?usage: tidy-files.sh <base>}"

changed="$(git diff --name-only --diff-filter=ACMR "$base" HEAD)"

# The build configuration and the tidy setup: any file may now behave differently.
if grep -Eq '^(\.clang-tidy|CMakeLists\.txt|CMakePresets\.json|cmake/|\.github/workflows/build\.yml|\.github/scripts/tidy-files\.sh)' <<<"$changed"; then
  echo ALL
  exit 0
fi

declare -A seen_headers=()
declare -A sources=()
queue=()

while IFS= read -r file; do
  [[ -z "$file" ]] && continue
  case "$file" in
    src/*.cpp | tests/*.cpp) sources["$file"]=1 ;;
    src/*.h | tests/*.h) queue+=("$file") ;;
  esac
done <<<"$changed"

# Walk outward from the changed headers: each includer that is a header joins the queue, each .cpp is collected.
while ((${#queue[@]} > 0)); do
  header="${queue[0]}"
  queue=("${queue[@]:1}")
  [[ -n "${seen_headers[$header]:-}" ]] && continue
  seen_headers["$header"]=1

  include_path="${header#*/}" # drop the leading src/ or tests/
  while IFS= read -r includer; do
    [[ -z "$includer" ]] && continue
    case "$includer" in
      *.cpp) sources["$includer"]=1 ;;
      *.h) queue+=("$includer") ;;
    esac
  done < <(git grep -l -F "#include \"$include_path\"" -- 'src/*.cpp' 'src/*.h' 'tests/*.cpp' 'tests/*.h' || true)
done

for file in "${!sources[@]}"; do
  [[ -f "$file" ]] && echo "$file"
done | sort
