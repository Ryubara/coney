# SPDX-License-Identifier: GPL-3.0-or-later
"""Which repository a pull request or a push belongs to, and the `pull_requests.open_only` switch, for `pr_guard.py`.

Standard library only, as the hooks run without packages.
"""

import re
import subprocess
from collections.abc import Callable
from pathlib import Path

#: The checkout these hooks belong to, and the conventions file the switch is read from.
ROOT = Path(__file__).resolve().parents[2]
CONVENTIONS = ROOT / ".github" / "conventions.yml"

_TRUE = frozenset({"true", "True", "TRUE", "yes", "Yes", "YES", "on", "On", "ON"})
#: The line of the `pull_requests` table that holds the switch: its own indentation, the key, a yes or no, a comment.
_OPEN_ONLY_LINE = re.compile(r"^  open_only:[ \t]+(?P<value>[A-Za-z]+)[ \t]*(?:#.*)?$")
_PULL_REQUESTS_LINE = re.compile(r"^pull_requests:[ \t]*(?:#.*)?$")


def read_open_only(path: Path | None = None) -> bool:
    """The `pull_requests.open_only` switch of the conventions file: True only when its line says yes.

    The hooks run without packages, so this reads the one line (two spaces in, under `pull_requests:`) the way the
    YAML loader of `coney-tools` reads a yes or no (`true`, `yes`, `on` in YAML 1.1's three spellings), and
    `test_pr_guard.py` holds the two together. Anything else, a missing key, another spelling, a file that cannot be
    read, leaves the switch off: the guard then does what it did before the switch existed.
    """
    try:
        lines = (path or CONVENTIONS).read_bytes().decode("utf-8").splitlines()
    except (OSError, UnicodeError):
        return False
    inside = False
    for line in lines:
        if _PULL_REQUESTS_LINE.match(line):
            inside = True
        elif inside and line[:1] not in ("", " ", "\t", "#"):
            return False  # the next table began without the key
        elif inside:
            found = _OPEN_ONLY_LINE.match(line)
            if found is not None:
                return found.group("value") in _TRUE
    return False


#: A remote address, as git writes it: a URL (`https://host/owner/name.git`, `ssh://git@host/owner/name`) or scp-like
#: (`git@host:owner/name.git`).
_URLISH = re.compile(r"^(?:[A-Za-z][A-Za-z0-9+.-]*://|[\w.-]+@[\w.-]+:)")
#: A remote's name (`origin`), as opposed to a path.
_NAME = re.compile(r"^[\w.-]+$")
#: The last two parts of a repository address, split at `/` or `:`.
_LAST_TWO = re.compile(r"(?:^|[/:])(?P<owner>[\w.-]+)/(?P<name>[\w.-]+?)(?:\.git)?/?$")


def repo_name(address: str) -> str | None:
    """`owner/name`, lower-cased, of a repository written in any form gh or git takes: `OWNER/REPO`,
    `HOST/OWNER/REPO`, `https://host/owner/repo(.git)`, `git@host:owner/repo.git`. None when it is not one (an
    expansion, a bare name)."""
    if "$" in address or "%" in address:
        return None
    found = _LAST_TWO.search(address.strip())
    return f"{found.group('owner')}/{found.group('name')}".lower() if found else None


def this_repository(folder: str, remote: str = "origin") -> str | None:
    """The `owner/name` of the folder's remote (`origin` unless named), lower-cased; None when git cannot say."""
    try:
        done = subprocess.run(
            ["git", "-C", folder or ".", "remote", "get-url", "--push", remote],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    return repo_name(done.stdout) if done.returncode == 0 else None


#: Says which `owner/name` a folder's remote (named, `origin` by default) is.
Origin = Callable[..., str | None]


def push_target(folder: str | None, remote: str | None, origin: Origin) -> str | None:
    """The repository a `git push` of `remote` (the word after `push`, or None) from `folder` writes to: the address
    itself when it is one, else the folder's remote of that name (`origin` when none is named); None when it is a
    path or an expansion, or names a remote of a folder the hook cannot tell (None)."""
    if remote is not None and _URLISH.match(remote):
        return repo_name(remote)
    if folder is None:
        return None
    if remote is None:
        return origin(folder, "origin")
    if _NAME.match(remote):
        return origin(folder, remote)
    return None


def is_other(target: str | None, mine: str | None) -> bool:
    """Whether `target` is provably another repository than `mine`: both known and different. Anything the hook
    cannot tell is treated as this repository, so the switch holds."""
    return target is not None and mine is not None and target != mine
