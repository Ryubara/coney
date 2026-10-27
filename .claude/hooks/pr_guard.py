# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PreToolUse hook (the PR guard, once `release_guard.py`): a pull request opens or changes only through a
title and description that pass CI's checks and, once `pull_requests.open_only` is on, only through `coney-tools pr
open`. What it refuses, what it lets through and what it does not cover: docs/guides/enforcement.md#pr-guard.

The reading of a shell command is `pr_guard_shell.py` (and, for `git push` and `gh api`, `pr_guard_routes.py`); the
switch and the repository a command belongs to are `pr_guard_repo.py`; this file decides. The switch is
`pull_requests.open_only` in `.github/conventions.yml`, read by `read_open_only` because the hooks run without packages
(off when the file cannot be read).

With the switch off the hook checks a hand-typed pull request's title and description the way CI's required `title`
and `body` jobs do (on 2026-10-20 and 21 six pull requests were pushed whose descriptions the `body` job then refused),
and fails open when it cannot tell. With it on it refuses the hand-typed routes and, unlike off, refuses what it cannot
tell: a folder it cannot follow, an argument list it cannot split. The lists themselves live in the guide only.

Exit code 2 blocks the call and shows stderr to the agent. Standard library only; the checks themselves run in a
subprocess.
"""

import json
import os
import subprocess
import sys
from collections.abc import Callable
from pathlib import Path

HOOKS = Path(__file__).resolve().parent
if str(HOOKS) not in sys.path:
    sys.path.insert(0, str(HOOKS))
import pr_guard_repo as guard_repo  # noqa: E402
import pr_guard_shell as guard_shell  # noqa: E402, F401
from pr_guard_repo import ROOT, is_other, push_target, read_open_only, repo_name  # noqa: E402, F401
from pr_guard_routes import api_reasons, git_pushes  # noqa: E402
from pr_guard_shell import PrCall, is_absolute, native, parse, pr_calls  # noqa: E402, F401

#: How long one check may take (a first `uv run` may sync the environment).
CHECK_TIMEOUT = 90
HOW = (
    'Open a pull request with `gh pr create --title "<title>" --body-file <file>` from a description that passes '
    "`uv run --project python coney-tools pr check-title` and `pr check-body --title ... --body <file>` (or "
    "`legacy check-pr-review`), the checks CI's required `title` and `body` jobs run "
    "(docs/guides/how-we-work.md#release-queue)."
)
#: What a refusal says while the switch is on; the reasons follow. It points at the `pr open` section of the release
#: guide.
OPEN_ONLY_HEAD = "Blocked: pull requests are opened only by coney-tools pr open <task> (docs/guides/pr.md#open)"


def open_only_refusal(call: PrCall) -> str | None:
    """Why the switch refuses a `gh pr` command, or None: it opens a pull request, rewrites its text or turns on
    auto-merge, all of which `coney-tools pr open` does."""
    if call.dry_run or call.help:
        return None
    if call.verb == "create":
        return "`gh pr create` opens a pull request by hand."
    if call.verb == "edit" and call.text_options:
        return f"`gh pr edit {call.text_options[0]}` rewrites a pull request's text by hand."
    if call.verb == "merge" and call.auto:
        return "`gh pr merge --auto` turns on auto-merge by hand."
    return None


def _unreadable(call: PrCall) -> str:
    """Why the switch refuses a `gh pr` command whose arguments the hook cannot split: it cannot tell what it does."""
    return f"`gh pr {call.verb}` has arguments the hook cannot read (`$'...'` quoting, a quote with no end)."


def _pr_target(call: PrCall, open_only: bool, origin: guard_repo.Origin) -> str | None:
    """The repository a `gh pr` command is for: its `-R`/`--repo`, in any form; else, with the switch on, the remote
    of the folder it runs in. None when it cannot be told: a folder the hook cannot follow, or arguments it cannot
    read that may name `-R`."""
    if call.unreadable and ("-R" in call.raw or "--repo" in call.raw):
        return None
    if call.repo is not None:
        return repo_name(call.repo)
    if not open_only or call.cwd is None:
        return None
    return origin(call.cwd)


def refusal(call: PrCall) -> str | None:
    """Why the command's shape is refused before any check runs, or None."""
    if call.dry_run or call.help:
        return None
    if call.unchecked:
        return f"`gh pr {call.verb} {call.unchecked[0]}` takes the description from somewhere the checks cannot see."
    if call.body is not None:
        return f"`gh pr {call.verb} --body` passes the description inline; write it to a file and use --body-file."
    if call.body_file == "-":
        return f"`gh pr {call.verb} --body-file -` reads the description from stdin; give a file."
    if call.verb == "create" and call.body_file is None:
        return "`gh pr create` has no --body-file."
    if call.verb == "create" and not call.title:
        return "`gh pr create` has no --title."
    return None


#: Runs one `coney-tools` command (its arguments after `coney-tools`) with `stdin`; returns the exit code and output.
Runner = Callable[[list[str], str], tuple[int, str]]


def run_coney_tools(args: list[str], stdin: str) -> tuple[int, str]:
    """Run `coney-tools <args>` from the main checkout; (2, why) when it cannot be started."""
    root = Path(os.environ.get("CLAUDE_PROJECT_DIR") or Path(__file__).resolve().parents[2])
    command = ["uv", "run", "--quiet", "--project", str(root / "python"), "coney-tools", *args]
    try:
        done = subprocess.run(
            command, cwd=root, input=stdin, capture_output=True, text=True, timeout=CHECK_TIMEOUT, check=False
        )
    except (OSError, subprocess.SubprocessError) as error:
        return 2, str(error)
    return done.returncode, (done.stdout + done.stderr).strip()


def _refused(code: int, out: str) -> bool:
    """Whether a check's result is a refusal: exit code 1 with the check's findings, not a crash's traceback."""
    return code == 1 and "Traceback (most recent call last)" not in out


def problems(call: PrCall, runner: Runner) -> list[str]:
    """What CI's `title` and `body` jobs would refuse in the call's title and description file (empty: it passes).

    A check that cannot run (exit code 2: no `coney-tools`, rules unusable; or a crash), a title or file that is a
    shell expansion, a file in a directory the hook cannot follow, or a file that is not there passes: the hook fails
    open, and gh reports a missing file itself.
    """
    found: list[str] = []
    title = call.title if call.title is not None and "$" not in call.title else None
    if title is not None:
        code, out = runner(["repo", "check-title", "-"], title + "\n")
        if _refused(code, out):
            found.append(out)
    if call.body_file is None or "$" in call.body_file or "%" in call.body_file:
        return found
    path = native(call.body_file)
    if not is_absolute(path):
        if call.cwd is None:
            return found
        path = Path(call.cwd) / path
    if path.is_file():
        args = ["repo", "check-body", "--body", str(path)]
        if title is not None:
            args += ["--title", title]
        if call.attach:
            args.append("--local-images")
        code, out = runner(args, "")
        if _refused(code, out):
            found.append(out)
    return found


def check(
    command: str,
    cwd: str,
    powershell: bool = False,
    runner: Runner = run_coney_tools,
    open_only: bool = False,
    repository: guard_repo.Origin | None = None,
) -> str | None:
    """The reason the command is refused, or None when it may run.

    `open_only` is the switch: with it on, a pull request is opened, rewritten or set to auto-merge, and this
    repository is pushed, by `coney-tools pr open` alone, so no title or description is left to check. `repository`
    says which `owner/name` a folder's remote is (default: `git remote get-url --push`).
    """
    origin = repository or guard_repo.this_repository
    calls = pr_calls(command, powershell, cwd)
    pushes = git_pushes(command, powershell, cwd) if open_only else []
    reasons: list[str] = api_reasons(command, powershell) if open_only else []
    if not calls and not reasons and not pushes:
        return None
    if not open_only and any(call.unreadable for call in calls):
        return None  # with the switch off the hook fails open on what it cannot read
    mine = origin(str(ROOT))
    for call in calls:
        if is_other(_pr_target(call, open_only, origin), mine):
            continue
        if open_only:
            why = _unreadable(call) if call.unreadable else open_only_refusal(call)
            if why is not None:
                reasons.append(why)
            continue
        if call.verb == "merge":
            continue  # only the switch cares about merging
        shape = refusal(call)
        if shape is not None:
            reasons.append(shape)
            continue
        if not call.dry_run and not call.help:
            reasons.extend(problems(call, runner))
    for push in pushes:
        if not push.harmless and not is_other(push_target(push.folder, push.remote, origin), mine):
            reasons.append("`git push` pushes a branch by hand.")
    if not reasons:
        return None
    if open_only:
        return OPEN_ONLY_HEAD + ": " + " ".join(reasons) + "\n"
    return "Blocked: " + " ".join(reasons) + "\n" + HOW + "\n"


def main(open_only: bool | None = None) -> int:
    """Read the hook's JSON from stdin; 2 (blocked, with the reason on stderr) or 0.

    `open_only` is the switch; None reads it from the conventions file (`read_open_only`).
    """
    try:
        data = json.loads(sys.stdin.read() or "{}")
        command = data["tool_input"]["command"]
    except (ValueError, KeyError, TypeError, OSError):
        return 0
    if not isinstance(command, str) or ("gh" not in command and "push" not in command):
        return 0
    cwd = data.get("cwd") if isinstance(data.get("cwd"), str) else str(Path.cwd())
    if open_only is None:
        open_only = guard_repo.read_open_only()
    reason = check(command, cwd, data.get("tool_name") == "PowerShell", open_only=open_only)
    if reason is None:
        return 0
    sys.stderr.write(reason)
    return 2


if __name__ == "__main__":
    sys.exit(main())
