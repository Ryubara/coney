#!/bin/bash
# coney-session BRANCH: one implementer session, started by `coney-tools impl run` as the user impl.
#
# Refreshes the container's clone from the bundle of main that coney-tools streamed to /tmp, makes a fresh worktree
# on a new branch BRANCH from it, checks the egress wall once more and runs Claude Code on the task, without a
# terminal, until it ends. Exit status: Claude Code's, or 10 to 13 when the session could not start.
set -euo pipefail

branch=${1:?usage: coney-session BRANCH}
repo=/work/coney
bundle=/tmp/coney-main.bundle
task=/work/task/task.md
denied=https://example.com # any host off the firewall's allowlist

die() {
    echo "coney-session: $1" >&2
    exit "$2"
}

[ "$(id -u)" != 0 ] || die "refusing to run as root" 10
[ -n "${CONEY_IMPL:-}" ] || die "CONEY_IMPL is not set" 10
[ -s "$task" ] || die "no task file at $task" 10
[ -s "$bundle" ] || die "no bundle of main at $bundle" 10
if curl --silent --max-time 5 --output /dev/null "$denied"; then
    die "$denied is reachable: the egress wall is down" 11
fi

[ -d "$repo/.git" ] || git init --quiet "$repo"
git -C "$repo" bundle verify --quiet "$bundle" >/dev/null 2>&1 || die "the bundle of main does not verify" 12
git -C "$repo" fetch --quiet --no-tags "$bundle" "+refs/heads/main:refs/remotes/origin/main"
grep -qx '/.worktrees/' "$repo/.git/info/exclude" 2>/dev/null || echo '/.worktrees/' >>"$repo/.git/info/exclude"
git -C "$repo" remote get-url outbox >/dev/null 2>&1 || git -C "$repo" remote add outbox /outbox

# Earlier sessions' worktrees go; their branches stay in the clone and in the outbox.
git -C "$repo" worktree list --porcelain | sed -n 's/^worktree //p' | tail -n +2 | while IFS= read -r old; do
    git -C "$repo" worktree remove --force "$old"
done
git -C "$repo" worktree prune
if git -C "$repo" show-ref --verify --quiet "refs/heads/$branch" \
    || git ls-remote --exit-code /outbox "refs/heads/$branch" >/dev/null 2>&1; then
    die "branch $branch exists already: give a new one" 13
fi
work="$repo/.worktrees/${branch//\//-}"
git -C "$repo" worktree add --quiet --no-track -b "$branch" "$work" refs/remotes/origin/main

# The container's name is its signing principal in allowed_signers.
export GIT_AUTHOR_NAME="$CONEY_IMPL" GIT_AUTHOR_EMAIL="$CONEY_IMPL"
export GIT_COMMITTER_NAME="$CONEY_IMPL" GIT_COMMITTER_EMAIL="$CONEY_IMPL"
rm -f "$bundle"
cd "$work"
exec claude -p "Do the task in $task. You are on branch $branch in a fresh worktree of the repository." \
    --append-system-prompt "$(cat /etc/coney/system-prompt.txt)" \
    --plugin-dir /opt/tether \
    --permission-mode bypassPermissions
