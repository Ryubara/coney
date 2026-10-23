# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-agent`, the session launcher: starts Claude Code as one of Coney's roles.

A Coney session runs as a role (`claude --agent coordinator`), so the hooks know who is acting: the role rules and
`require_role` read it from the hook input (`.claude/hooks/roles.py`, docs/guides/enforcement.md#hooks). This
command is the one way to start such a session without remembering the options: it finds the Coney checkout that
holds the current folder, checks the role is one of the files in `.claude/agents/`, and runs `claude --agent <role>`
there, adding `--plugin-dir <mod folder>` when the checkout has the Coney mod (it loads in every mode, trusted
folder or not). Options after `--` go to `claude` unchanged. Usage and every parameter:
docs/guides/coney-tools-setup.md#coney-agent.
"""

from __future__ import annotations

import argparse
import os
import shlex
import shutil
import signal
import subprocess
import sys
from collections.abc import Callable, Sequence
from pathlib import Path

#: The role of a session that names none: the main session.
DEFAULT_ROLE = "coordinator"
#: Where the Coney mod lives in a checkout (relative path); loaded with `--plugin-dir` when the folder exists.
MOD_DIR = "tools/claude-mod"

#: Endings of a `claude` that Windows starts through cmd.exe (the npm shim): Ctrl+C then asks about the batch job.
BATCH_SUFFIXES = (".cmd", ".bat")

#: Starts the command in a folder and returns its exit code (a parameter so the tests never start Claude Code).
Runner = Callable[[list[str], Path], int]


def run_claude(command: list[str], folder: Path, posix: bool = os.name == "posix") -> int:
    """Run the command in the folder, attached to this terminal; returns its exit code.

    On Linux and macOS the session launcher is replaced by Claude Code (`execvp`), so Ctrl+C reaches Claude Code alone
    and nothing is left over. Elsewhere it waits for Claude Code with Ctrl+C ignored in itself: the console sends the
    signal to both, and only Claude Code should act on it.
    """
    if posix:
        os.chdir(folder)
        os.execvp(command[0], command)
        return 1  # unreachable unless execvp is replaced (tests)
    previous = signal.signal(signal.SIGINT, signal.SIG_IGN)
    try:
        return subprocess.run(command, cwd=folder, check=False).returncode
    finally:
        signal.signal(signal.SIGINT, previous)


def quote(command: list[str], posix: bool = os.name == "posix") -> str:
    """The command as one line a shell can run: quoted for sh on POSIX, for the Windows command line elsewhere."""
    return shlex.join(command) if posix else subprocess.list2cmdline(command)


def find_checkout(start: Path) -> Path | None:
    """The Coney checkout that holds `start`: the nearest folder above it with `AGENTS.md` and `.claude/agents`."""
    here = start.resolve()
    for folder in (here, *here.parents):
        if (folder / "AGENTS.md").is_file() and (folder / ".claude" / "agents").is_dir():
            return folder
    return None


def role_names(checkout: Path) -> list[str]:
    """The roles a checkout defines: the file names in `.claude/agents/`, sorted."""
    return sorted(path.stem for path in (checkout / ".claude" / "agents").glob("*.md"))


def _build_parser() -> argparse.ArgumentParser:
    """The command line: options and an optional role; everything after `--` is for claude."""
    parser = argparse.ArgumentParser(
        prog="coney-agent",
        description="Start Claude Code as one of Coney's roles, from the checkout that holds the current folder.",
        epilog="Options for claude go after `--`, for example: coney-agent engineer -- --model opus",
    )
    parser.add_argument("role", nargs="?", default=DEFAULT_ROLE, help=f"the role to start (default {DEFAULT_ROLE})")
    parser.add_argument(
        "--mod-dir",
        type=Path,
        help=f"the Coney mod folder to load with --plugin-dir (default {MOD_DIR} of the checkout)",
    )
    parser.add_argument("--dry-run", action="store_true", help="print the command and the folder; start nothing")
    return parser


def main(
    argv: Sequence[str] | None = None,
    start: Path | None = None,
    runner: Runner = run_claude,
    which: Callable[[str], str | None] = shutil.which,
) -> int:
    """Run the session launcher; returns claude's exit code, or 2 on a usage error (an unknown role, no claude)."""
    arguments = list(sys.argv[1:] if argv is None else argv)
    passthrough: list[str] = []
    if "--" in arguments:
        split = arguments.index("--")
        arguments, passthrough = arguments[:split], arguments[split + 1 :]
    args = _build_parser().parse_args(arguments)
    # Outside any checkout the one this file is in is used (python/src/coney_tools/ is three folders down).
    here = (start or Path.cwd()).resolve()
    checkout = find_checkout(here) or Path(__file__).resolve().parents[3]
    names = role_names(checkout)
    if args.role not in names:
        print(f"coney-agent: {args.role!r} is not a role; the roles are {', '.join(names)}", file=sys.stderr)
        return 2
    claude = which("claude")
    if claude is None and not args.dry_run:
        print("coney-agent: claude is not on PATH (install Claude Code first)", file=sys.stderr)
        return 2
    if claude is not None and Path(claude).suffix.casefold() in BATCH_SUFFIXES:
        print(
            f'coney-agent: {claude} is a batch file, so cmd.exe asks "Terminate batch job (Y/N)?" on Ctrl+C: answer N '
            "(Y can leave Claude Code running). The native Claude Code (claude.exe) does not ask.",
            file=sys.stderr,
        )
    # A relative --mod-dir is the caller's: claude runs in the checkout, so it is made absolute from here.
    mod = (here / args.mod_dir) if args.mod_dir else checkout / MOD_DIR
    command = [claude or "claude", "--agent", args.role]
    if mod.is_dir():
        command += ["--plugin-dir", str(mod)]
    elif args.mod_dir:
        print(f"coney-agent: --mod-dir {args.mod_dir}: no such folder ({mod})", file=sys.stderr)
        return 2
    else:
        print(f"coney-agent: no mod folder at {mod}; starting without the Coney mod", file=sys.stderr)
    command += passthrough
    if args.dry_run:
        print(f"{quote(command)}\n(in {checkout})")
        return 0
    return runner(command, checkout)


if __name__ == "__main__":
    sys.exit(main())
