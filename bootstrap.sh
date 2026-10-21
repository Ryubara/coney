#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sets up a Coney workspace on Linux or macOS: installs uv if it is missing, then runs
# `coney-tools workspace init` with the arguments given (docs/guides/workspace.md#bootstrap).
#
# From a checkout:  ./bootstrap.sh [--fetch all] [--dry-run]
# On its own:       sh bootstrap.sh --repo-url URL [--workspace DIR] [...]   (clones the repository)
set -eu

# The uv release installed when uv is missing. Astral's installer for that version downloads uv into ~/.local/bin
# and adds it to PATH in your shell profiles (.profile, .bashrc, .zshrc, ...); set UV_NO_MODIFY_PATH=1 to stop that.
uv_version=0.13.0

if ! command -v uv >/dev/null 2>&1; then
    echo "bootstrap: installing uv $uv_version (https://docs.astral.sh/uv/); it adds itself to PATH in your shell profiles"
    curl -LsSf "https://astral.sh/uv/$uv_version/install.sh" | sh
    PATH="$HOME/.local/bin:$PATH"
    export PATH
fi

# ccache makes every build after the first one fast (docs/guides/building.md#compiler-cache), so it is fetched unless
# the command line says what to fetch. On Linux and macOS init says to install it with apt or Homebrew instead.
fetch_given=no
for argument in "$@"; do
    case "$argument" in
        --fetch|--fetch=*) fetch_given=yes ;;
    esac
done
if [ "$fetch_given" = no ]; then
    set -- "$@" --fetch ccache
fi

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -f "$here/python/pyproject.toml" ]; then
    # Inside a checkout: run its own coney-tools from the checkout, so init finds the workspace around it.
    cd "$here"
    exec uv run --isolated --frozen --project "$here/python" coney-tools workspace init "$@"
fi

# On its own: coney-tools comes from the repository named by --repo-url.
url=""
previous=""
for argument in "$@"; do
    case "$argument" in
        --repo-url=*) url=${argument#--repo-url=} ;;
    esac
    if [ "$previous" = "--repo-url" ]; then url=$argument; fi
    previous=$argument
done
if [ -z "$url" ]; then
    echo "bootstrap: not inside a Coney checkout; pass --repo-url URL (the address in the README)" >&2
    exit 2
fi
exec uvx --from "git+$url#subdirectory=python" coney-tools workspace init "$@"
