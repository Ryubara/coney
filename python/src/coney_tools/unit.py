# SPDX-License-Identifier: GPL-3.0-or-later
"""Where a unit of work keeps its proof: the task's folder in the scratch directory (docs/guides/stamps.md#layout).

```
<scratch>/<task>/stamps/   the stamp log (written only by the stage commands)
              study/<addr>/   the saved decompile and what `original study` made of it (never committed)
              mapping/<addr>.toml   the pairs of one function
              compare/<addr>.txt    the side-by-side view
```

The task is `--task`, else `CONEY_TASK`, else the checkout's branch. The unit record that `task start` writes belongs
to the task commands; this module only knows the folders, so a change of layout is a change here.
"""

from __future__ import annotations

import os
import re
import subprocess
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from coney_tools.config import ConfigError, load_config, local_config_file, workspace_folder


def _git(root: Path, *args: str) -> str:
    """Run git in `root` and return its stdout; ConfigError with its message when it fails."""
    done = subprocess.run(["git", "-C", str(root), *args], capture_output=True, encoding="utf-8", check=False)
    if done.returncode != 0:
        raise ConfigError(f"git {' '.join(args)}: {done.stderr.strip() or 'failed'}")
    return done.stdout


def task_name(root: Path, explicit: str | None = None, env: Mapping[str, str] | None = None) -> str:
    """The task a command is about: `explicit`, else CONEY_TASK, else the branch (a `/` becomes `-`), else the
    checkout's folder name (a detached HEAD)."""
    env = os.environ if env is None else env
    name = explicit if explicit is not None else env.get("CONEY_TASK")
    if name is None:
        branch = _git(root, "rev-parse", "--abbrev-ref", "HEAD").strip()
        name = root.name if branch == "HEAD" else branch.replace("/", "-")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", name):
        raise ConfigError(f"{name!r}: not a task name (letters, digits, '.', '_' and '-'; it names a scratch folder)")
    return name


def scratch_folder(root: Path) -> Path:
    """The scratch directory: `scratch_dir` of coney.local.toml, else the workspace's `scratch/`."""
    configured = load_config(root).paths.get("scratch_dir") if local_config_file(root) else None
    return configured or workspace_folder(root, "scratch")


def address(text: str) -> str:
    """An original address written one way, `0x00230d00`; ConfigError for anything that is not up to 8 hex digits."""
    match = re.fullmatch(r"(?:0[xX])?([0-9a-fA-F]{1,8})", text.strip())
    if match is None:
        raise ConfigError(f"{text!r}: not an address (up to 8 hex digits, such as 0x00230d00)")
    return f"0x{int(match.group(1), 16):08x}"


@dataclass(frozen=True)
class Unit:
    """The folders of one task."""

    task: str
    dir: Path

    @classmethod
    def of(cls, root: Path, task: str) -> Unit:
        """The unit of `task` in the scratch folder of the checkout at `root`."""
        return cls(task, scratch_folder(root) / task)

    @property
    def stamps(self) -> Path:
        """The stamp log's folder."""
        return self.dir / "stamps"

    def study(self, addr: str) -> Path:
        """The folder of one function's study."""
        return self.dir / "study" / addr

    def mapping(self, addr: str) -> Path:
        """The pairs file of one function."""
        return self.dir / "mapping" / f"{addr}.toml"

    def compare(self, addr: str) -> Path:
        """The side-by-side view of one function."""
        return self.dir / "compare" / f"{addr}.txt"


def head_commit(root: Path) -> str:
    """The full hash of HEAD."""
    return _git(root, "rev-parse", "--verify", "HEAD").strip()


def dirty_paths(root: Path, paths: Sequence[str]) -> list[str]:
    """The `paths` (repository-relative) with uncommitted changes, or untracked, in the order given."""
    if not paths:
        return []
    out = _git(root, "status", "--porcelain", "--untracked-files=all", "-z", "--", *paths)
    changed = {entry[3:] for entry in out.split("\0") if len(entry) > 3}
    return [p for p in paths if p in changed]
