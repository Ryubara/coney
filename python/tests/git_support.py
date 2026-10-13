# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic git branches for the tests: a base commit and commits carrying chosen messages, in a temporary folder."""

import subprocess
from pathlib import Path


def git(repo: Path, *args: str) -> str:
    """Run git in `repo` with a fixed identity and no signing; returns its output."""
    identity = ["-c", "user.name=T", "-c", "user.email=t@example.org", "-c", "commit.gpgsign=false"]
    result = subprocess.run(["git", "-C", str(repo), *identity, *args], capture_output=True, text=True, check=True)
    return result.stdout


def message(title: str = "tools: Add a thing", *trailers: str) -> str:
    """A commit message with the given trailer lines after a body."""
    return title + "\n\nWhat changed and why.\n\n" + "".join(f"{line}\n" for line in trailers)


def make_branch(root: Path, messages: list[str]) -> Path:
    """A synthetic repository with a `main` base commit (it credits a model) and one commit per message on top."""
    git(root, "init", "-q", "-b", "main")
    (root / "f.txt").write_text("0", encoding="utf-8")
    git(root, "add", "f.txt")
    git(root, "commit", "-q", "-m", "base\n\nCo-Authored-By: Claude Haiku 5 <noreply@anthropic.com>")
    for number, text in enumerate(messages, start=1):
        (root / "f.txt").write_text(str(number), encoding="utf-8")
        git(root, "commit", "-q", "-a", "-m", text)
    return root
