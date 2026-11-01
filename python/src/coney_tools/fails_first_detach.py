# SPDX-License-Identifier: GPL-3.0-or-later
"""`orig fails-first --detach`: both builds and test runs in a process of their own, with no time budget
(docs/guides/orig-stages.md, docs/guides/stamps.md#fails-first).

A run you wait on has the call's 45-minute budget (`build --budget`, because an agent's prompt cache lasts an hour), and
a build of a commit far out of date can need longer: at peak load one was 309 steps and a 46-minute median, which
`build` refused at its first look, so the run never queued (issue 1384). A detached run is the same run in a process
apart from the calling shell (`detached_runs.py`, as `build --detach` and `ci run --detach` are), whose builds are
`unattended`: they wait up to four hours and have no budget. It checks the worktree out at each commit, restores it,
writes the stamps as the caller (`spawn_detached` hands the caller's verified identity on, `identity.hand_on`) and
records its exit code in `fails-first-result.json` in the unit's scratch folder, which `orig fails-first --result`
reads.

The caller checks what it can before it starts the child (`fails_first.preflight`: a dirty tree, commits out of order,
a name no test file holds), so a refusal comes back at once, not from a child nobody reads. The child checks the same
again, and the worktree is the child's until it ends: it is detached at the test commit for most of the run, so nothing
is edited or committed in it meanwhile.
"""

from __future__ import annotations

import sys
from argparse import Namespace
from pathlib import Path

from coney_tools import build_cli, detached_runs
from coney_tools.config import ConfigError

#: The detached run (detached_runs.py): its result and its console output in the unit's scratch folder.
KIND = detached_runs.Kind(
    "orig fails-first", "coney-tools orig fails-first --result", "fails-first-result.json", "fails-first-detached.out"
)


def refuse_while_running(folder: Path) -> None:
    """Raise ConfigError when the unit's last detached run still runs: its worktree is detached at a commit with a
    restore marker, which any other run would refuse with a message about an earlier run that left it so."""
    result, _ = KIND.files(folder)
    previous = build_cli._read_record(result)
    if previous is not None and previous.get("state") == "running" and build_cli._alive(previous):
        raise ConfigError(
            f"a detached {KIND.what} of {previous.get('agent')} is still running (pid {previous.get('pid')}): "
            f"`{KIND.result_command}` shows how it ends, and the worktree is its until then"
        )


def child_command(args: Namespace, task: str, agent: str, result: Path) -> list[str]:
    """The command line of the detached run's child: this run's options, the task and agent spelled out (the worktree
    is not on its branch for most of the run, and the build slots need an id), and the file it records itself in."""
    command = [sys.executable, "-m", "coney_tools.cli", "orig", "fails-first", "--task", task, "--agent", agent]
    command += ["--preset", args.preset, "--test-commit", args.test_commit]
    command += ["--fix-commit", args.fix_commit] if args.fix_commit else []
    for name in args.test:
        command += ["--test", name]
    command += ["--keeps"] if args.keeps else []
    return [*command, "--result-file", str(result)]
