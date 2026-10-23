# SPDX-License-Identifier: GPL-3.0-or-later
"""Refuse decompiled game Lua in the repository: a pre-commit check and a Claude Code PreToolUse hook.

`coney-tools lua decompile` (docs/guides/coney-tools.md#lua) turns the game's compiled scripts back into Lua, in a
folder outside the repository: like Ghidra's output it is a research aid, and the game's script text never enters the
repository, an issue or CI (LEGAL.md#no-game-data). Research pages describe what a script does in our own words. Two
marks catch a slip:

* **The marker.** Every file the command writes ends with `-- lua4 decompile <16 hex digits> <chunk name>`
  (`marker_line`; last, so the decompile's lines keep their numbers). Text holding that line anywhere is refused.
* **Fingerprints.** `lua decompile --all` also writes `fingerprints.txt` into the shared folder (`lua_dir`): a hash
  of every distinctive line of every decompiled script (at least MIN_LENGTH characters once its spacing is
  collapsed). Text holding RUN or more consecutive distinctive lines that are all listed is a pasted block and is
  refused; a single line quoted in a research page passes, and so do short lines (`end`, `local l1 = 1`). Without the
  shared folder (in CI, or before the first `--all`) only the marker is checked.

This file is the one definition of both marks: `coney_tools/lua_decompile.py` loads it to write them, and a test
checks that what it writes is refused here. It is standard-library only, as every hook is.

* pre-commit: `python .claude/hooks/lua_guard.py FILE...` prints each refused file and exits 1.
* Claude Code: `.claude/hooks/dispatch.py` runs `main()` for Write, Edit, MultiEdit and NotebookEdit; it reads the
  hook's JSON from stdin and returns 2 with the reason on stderr when the new text would go into a Coney checkout.
  Anything it cannot read passes. A shell command that writes a file is not seen here; pre-commit catches the commit.

The shared folder is found as `coney-tools` finds it (`coney_tools/config.py`): `lua_dir` in the `coney.local.toml`
of the checkout or of its main checkout, relative to that file, else `scratch/lua` in the workspace (two levels above
the main checkout in the workspace layout, else a folder named after the clone with `-workspace` beside it).
"""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
import tomllib
from pathlib import Path

#: The last line of every file `lua decompile` writes: the chunk's hash and name follow.
MARKER = re.compile(r"^-- lua4 decompile [0-9a-f]{16}(?: |$)", re.MULTILINE)
#: A line shorter than this (spacing collapsed) is too common to fingerprint.
MIN_LENGTH = 24
#: This many consecutive fingerprinted lines make a pasted block.
RUN = 3
#: The fingerprint file in the shared folder.
FINGERPRINTS = "fingerprints.txt"
#: Marks a Coney checkout (as coney_tools/config.py finds one).
EXAMPLE_FILE = "coney.local.example.toml"
LOCAL_FILE = "coney.local.toml"

MESSAGE = (
    "Blocked: this is decompiled game Lua (LEGAL.md#no-game-data). The output of `coney-tools lua decompile` stays in "
    "the shared folder outside the repository; a research page describes what a script does in our own words and "
    "cites the chunk and function, never its text (docs/guides/research-workflow.md#scripts).\n"
)


def marker_line(name: str, data: bytes) -> str:
    """The marker line for a decompile of the chunk `data` named `name`."""
    return f"-- lua4 decompile {hashlib.sha256(data).hexdigest()[:16]} {name}"


def _distinctive(line: str) -> str | None:
    """The line with its spacing collapsed, or None when it is too short to fingerprint."""
    text = " ".join(line.split())
    return text if len(text) >= MIN_LENGTH else None


def fingerprint(line: str) -> str | None:
    """The fingerprint of one line of Lua, or None when it is too short to have one."""
    text = _distinctive(line)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:16] if text is not None else None


def pasted(text: str, prints: frozenset[str]) -> bool:
    """Whether `text` holds RUN consecutive distinctive lines that are all fingerprints of decompiled scripts."""
    if not prints:
        return False
    run = 0
    for line in text.splitlines():
        found = fingerprint(line)
        if found is None:
            continue
        run = run + 1 if found in prints else 0
        if run >= RUN:
            return True
    return False


def problem(text: str, prints: frozenset[str]) -> str | None:
    """Why `text` is refused, or None."""
    if MARKER.search(text):
        return "it holds the marker line of a `coney-tools lua decompile` output"
    if pasted(text, prints):
        return f"it holds {RUN} or more consecutive lines of a decompiled script"
    return None


def checkout_of(path: Path) -> Path | None:
    """The Coney checkout that holds `path`, or None."""
    path = path.resolve()
    for candidate in (path, *path.parents):
        if (candidate / EXAMPLE_FILE).is_file():
            return candidate
    return None


def _main_checkout(root: Path) -> Path:
    try:
        done = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "--path-format=absolute", "--git-common-dir"],
            capture_output=True,
            text=True,
            check=False,
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError):
        return root
    common = Path(done.stdout.strip()) if done.returncode == 0 and done.stdout.strip() else None
    return common.parent if common is not None and common.name == ".git" else root


def lua_dir(root: Path) -> Path:
    """The shared folder of decompiled scripts for the checkout at `root`."""
    main = _main_checkout(root)
    for candidate in dict.fromkeys((root, main)):
        config = candidate / LOCAL_FILE
        if config.is_file():
            try:
                value = tomllib.loads(config.read_text(encoding="utf-8")).get("lua_dir", "")
            except (OSError, ValueError):
                value = ""
            if isinstance(value, str) and value:
                return (candidate / value).resolve()
            break
    workspace = main.parent.parent if main.parent.name == "repos" else main.parent / (main.name + "-workspace")
    return workspace / "scratch" / "lua"


def load_fingerprints(root: Path) -> frozenset[str]:
    """The fingerprints `lua decompile --all` wrote for the checkout at `root` (none when it has not run)."""
    try:
        text = (lua_dir(root) / FINGERPRINTS).read_text(encoding="ascii")
    except (OSError, UnicodeDecodeError):
        return frozenset()
    return frozenset(line.strip() for line in text.splitlines() if line.strip())


def _new_text(data: dict[str, object]) -> tuple[str, str] | None:
    """The file path and the text a Write, Edit, MultiEdit or NotebookEdit call would put there."""
    tool_input = data.get("tool_input")
    if not isinstance(tool_input, dict):
        return None
    path = tool_input.get("file_path") or tool_input.get("notebook_path")
    if not isinstance(path, str):
        return None
    parts = [tool_input.get(key) for key in ("content", "new_string", "new_source")]
    edits = tool_input.get("edits")
    if isinstance(edits, list):
        parts += [edit.get("new_string") for edit in edits if isinstance(edit, dict)]
    return path, "\n".join(part for part in parts if isinstance(part, str))


def hook_main(stdin: str) -> int:
    """The Claude Code hook: 2 (blocked, reason on stderr) or 0."""
    try:
        data = json.loads(stdin or "{}")
    except ValueError:
        return 0
    found = _new_text(data) if isinstance(data, dict) else None
    if found is None:
        return 0
    path, text = found
    root = checkout_of(Path(path).parent)
    if root is None:
        return 0
    reason = problem(text, load_fingerprints(root))
    if reason is None:
        return 0
    sys.stderr.write(f"{path}: {reason}.\n{MESSAGE}")
    return 2


def files_main(paths: list[str]) -> int:
    """The pre-commit check: 1 when any file is refused (each named with the reason), else 0."""
    root = checkout_of(Path.cwd())
    prints = load_fingerprints(root) if root is not None else frozenset()
    refused = 0
    for name in paths:
        try:
            text = Path(name).read_bytes().decode("utf-8", errors="replace")
        except OSError:
            continue
        reason = problem(text, prints)
        if reason is not None:
            print(f"{name}: {reason}")
            refused += 1
    if refused:
        sys.stdout.write(MESSAGE)
    return 1 if refused else 0


def main() -> int:
    """The Claude Code hook as `dispatch.py` runs it: the hook's JSON on stdin."""
    return hook_main(sys.stdin.read())


if __name__ == "__main__":
    sys.exit(files_main(sys.argv[1:]) if len(sys.argv) > 1 else main())
