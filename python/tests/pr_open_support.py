# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared by the `pr open` tests: a made-up unit that passes every stage (`pr_support`), a real local bare remote, a
fake `gh` that keeps pull requests in memory, and the claims on a fake GitHub (`claim_support`). No network, no keys.

`Open` wires them into the `Env` that `pr_open.run_open` takes and keeps every call the command made, so a test can
say what was pushed, what was created and what was not touched.
"""

import json
import subprocess
from collections.abc import Sequence
from dataclasses import dataclass, field
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from claim_support import Clock as ClaimClock
from claim_support import FakeGitHub, plant_claim
from git_support import git
from pr_support import Built

from coney_tools import identity, pr_open, pr_publish, unit_record
from coney_tools.pr_publish import GhError

REPO = "acme/widgets"
OPENER = "Ryubara"
PNG = bytes.fromhex(
    "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d49444154789c6360000002000001e5"
    "27d4a20000000049454e44ae426082"
)


class FakeGh:
    """`gh` in memory: pull requests with their title, body, uploads, auto-merge and state. A call that is listed in
    `fail` raises GhError that many times first."""

    def __init__(self, origin: Path) -> None:
        self.origin = origin
        self.calls: list[tuple[list[str], Path | None]] = []
        self.prs: dict[int, dict[str, Any]] = {}
        self.next_number = 812
        self.version = "2.102.0"
        self.logged_in = True
        self.parent: str | None = None
        #: Keys like "pr create" mapped to how many calls of it fail before one works.
        self.fail: dict[str, int] = {}
        #: Create the pull request, then fail the call (the answer was lost on the way).
        self.create_then_fail = False
        #: How many times the stored body keeps a path on the author's machine, whatever was sent.
        self.corrupt_body = 0
        self.uploads = 0

    def names(self) -> list[str]:
        """The first two words of each call, for assertions."""
        return [" ".join(args[:2]) for args, _ in self.calls]

    def writes(self) -> list[list[str]]:
        """The calls that change something on GitHub."""
        return [
            args for args, _ in self.calls if " ".join(args[:2]) in ("pr create", "pr edit", "pr merge", "pr reopen")
        ]

    def _tip(self, branch: str) -> str:
        """The commit the made-up remote has for `branch` (empty when it has none)."""
        done = subprocess.run(
            ["git", "-C", str(self.origin), "rev-parse", f"refs/heads/{branch}"],
            capture_output=True,
            text=True,
            check=False,
        )
        return done.stdout.strip()

    def _body(self, args: Sequence[str], cwd: Path | None) -> str:
        """The body file's text with each `--attach` picture shown as an upload, as `gh` rewrites it."""
        assert cwd is not None
        text = (cwd / args[args.index("--body-file") + 1]).read_text(encoding="utf-8")
        for index, word in enumerate(args):
            if word == "--attach":
                assert (cwd / args[index + 1]).is_file(), "an attached picture must exist under the working directory"
                self.uploads += 1
                text = text.replace(f"]({args[index + 1]})", f"](https://example.test/assets/{self.uploads})")
        if self.corrupt_body > 0:
            self.corrupt_body -= 1
            text += "\nSee C:\\Users\\someone\\scratch\\shots\\a.png\n"
        return text

    def __call__(self, args: Sequence[str], cwd: Path | None) -> str:
        args = list(args)
        self.calls.append((args, cwd))
        key = " ".join(args[:2])
        if self.fail.get(key, 0) > 0:
            self.fail[key] -= 1
            raise GhError(f"gh {key} failed: the network is down")
        if args[0] == "--version":
            return f"gh version {self.version} (2026-10-01)\n"
        if key == "auth status":
            if not self.logged_in:
                raise GhError("gh auth status failed: not logged in")
            return "Logged in\n"
        if key == "repo view":
            parent = {"name": "widgets", "owner": {"login": "up"}} if self.parent else None
            return json.dumps({"nameWithOwner": REPO, "parent": parent})
        if key == "pr list":
            head = args[args.index("--head") + 1]
            return json.dumps([_listing(n, p) for n, p in self.prs.items() if p["head"] == head])
        if key == "pr create":
            number = self.next_number
            self.next_number += 1
            head = args[args.index("--head") + 1]
            self.prs[number] = {
                "head": head,
                "title": args[args.index("--title") + 1],
                "body": self._body(args, cwd),
                "state": "OPEN",
                "merged": False,
                "auto": False,
                "url": f"https://github.com/acme/widgets/pull/{number}",
            }
            if self.create_then_fail:
                self.create_then_fail = False
                raise GhError("gh pr create failed: the connection dropped")
            return f"{self.prs[number]['url']}\n"
        number = int(args[2]) if key.startswith("pr ") and len(args) > 2 else 0
        if key == "pr edit":
            self.prs[number]["title"] = args[args.index("--title") + 1]
            self.prs[number]["body"] = self._body(args, cwd)
            return ""
        if key == "pr merge":
            if "--disable-auto" in args:
                self.prs[number]["auto"] = False
                return ""
            assert "--squash" in args and "--auto" in args
            self.prs[number]["auto"] = True
            return ""
        if key == "pr reopen":
            self.prs[number]["state"] = "OPEN"
            return ""
        if key == "pr view":
            pr = self.prs[number]
            return json.dumps(
                {
                    "title": pr["title"],
                    "body": pr["body"],
                    "headRefOid": self._tip(pr["head"]),
                    "autoMergeRequest": {"enabledBy": {"login": OPENER}} if pr["auto"] else None,
                }
            )
        raise AssertionError(f"the fake gh has no answer for {args}")


def _listing(number: int, pr: dict[str, Any]) -> dict[str, Any]:
    """One pull request as `gh pr list --json` shows it."""
    return {
        "number": number,
        "state": "MERGED" if pr["merged"] else pr["state"],
        "url": pr["url"],
        "mergedAt": "2026-10-24T00:00:00Z" if pr["merged"] else None,
    }


def failed(args: Sequence[str]) -> subprocess.CompletedProcess[str]:
    """A git process that failed."""
    return subprocess.CompletedProcess(list(args), 1, "", "fatal: the remote hung up unexpectedly")


@dataclass
class Open:
    """A unit, its remote, the fake `gh` and the claims, wired into an `Env`."""

    built: Built
    gh: FakeGh
    github: FakeGitHub
    said: list[str] = field(default_factory=list)
    git_calls: list[list[str]] = field(default_factory=list)
    #: How many pushes fail before one works.
    fail_push: int = 0
    #: How many `git ls-remote` calls fail before one works (the lease check of a push).
    fail_ls_remote: int = 0
    #: Whether `git rebase` fails before it starts anything.
    fail_rebase: bool = False
    who: identity.Identity = field(default_factory=identity.Identity)

    def _git(self, folder: Path, args: Sequence[str]) -> subprocess.CompletedProcess[str]:
        self.git_calls.append(list(args))
        if args and args[0] == "push" and self.fail_push > 0:
            self.fail_push -= 1
            return failed(args)
        if args and args[0] == "ls-remote" and self.fail_ls_remote > 0:
            self.fail_ls_remote -= 1
            return failed(args)
        if self.fail_rebase and "rebase" in args and "--abort" not in args:
            return failed(args)
        return pr_publish.real_git(folder, args)

    def env(self) -> pr_open.Env:
        """The `Env` of the made-up machine."""
        return pr_open.Env(
            gh=self.gh,
            git=self._git,
            remote=self.github,
            clock=lambda: datetime(2026, 10, 23, 13, 0, 0, tzinfo=UTC),
            say=self.said.append,
            who=lambda: self.who,
            stamp_who=lambda: {
                "kind": "tool",
                "tool": "coney-tools pr open",
                "agent": {"id": "coord", "type": "coordinator", "model": "claude-opus-5-5"},
                "source": "hook",
            },
        )

    def request(self, **more: Any) -> pr_open.Request:
        """A request for the unit's task from its worktree."""
        values: dict[str, Any] = {"task": self.built.task, "worktree": self.built.root}
        values.update(more)
        return pr_open.Request(**values)

    def run(self, **more: Any) -> int:
        """Run `pr open`."""
        return pr_open.run_open(self.request(**more), self.env())

    def text(self) -> str:
        """Everything the command said."""
        return "\n".join(self.said)

    @property
    def log(self) -> Any:
        """The unit's stamp log."""
        from coney_tools import stamps

        return stamps.verify(self.built.unit.stamps, self.built.conventions.stamps)

    def opened_stamps(self) -> list[Any]:
        """The `pr-opened` stamps of the log."""
        return [s for s in self.log.stamps if s.step == "pr-opened"]

    def origin_tip(self, branch: str = "work") -> str:
        """The branch on the remote."""
        return git(self.built.origin, "rev-parse", f"refs/heads/{branch}").strip()

    def advance_main(self, name: str, text: str, message: str = "main moves") -> str:
        """Another person commits `name` to main on the remote; returns the commit."""
        other = self.built.root.parent.parent / "other"
        if not other.exists():
            subprocess.run(["git", "clone", "-q", str(self.built.origin), str(other)], check=True)
        git(other, "checkout", "-q", "main")
        git(other, "pull", "-q", "origin", "main")
        path = other / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="")
        git(other, "add", "-A")
        git(other, "commit", "-q", "-m", message)
        git(other, "push", "-q", "origin", "main")
        return git(other, "rev-parse", "HEAD").strip()

    def snapshot(self) -> dict[str, str]:
        """The state a dry run must leave alone: refs, status, the log, the remote, the worktree list."""
        root = self.built.root
        return {
            "head": git(root, "rev-parse", "HEAD"),
            "refs": git(root, "for-each-ref", "refs/heads"),
            "status": git(root, "status", "--porcelain"),
            "worktrees": git(root, "worktree", "list", "--porcelain"),
            "origin": git(self.built.origin, "for-each-ref"),
            "log": "".join(p.read_text(encoding="utf-8") for p in sorted(self.built.unit.stamps.glob("*.jsonl"))),
            "published": str((self.built.unit.dir / pr_publish.PUBLISHED_FILE).exists()),
            "folder": "\n".join(sorted(p.name for p in self.built.unit.dir.iterdir())),
        }


def make_open(built: Built, issues: tuple[int, ...] = (757,), branch: str = "work") -> Open:
    """Wire `built` to a fake `gh`, claims held by the opener, and a unit record that points at its worktree."""
    clock = ClaimClock()
    github = FakeGitHub(clock)
    github.actor = OPENER
    github.permissions = {OPENER: "admin"}
    for number in issues:
        github.add_issue(number)
        plant_claim(github, number, OPENER, task=built.task)
    record = unit_record.UnitRecord(
        task=built.task,
        kind=built.kind,
        issues=issues,
        claimant=OPENER,
        branch=branch,
        worktree=built.root,
        base_ref="origin/main",
        base_commit=built.base,
        started=datetime(2026, 10, 22, 11, 0, 0, tzinfo=UTC),
    )
    unit_record.write_record(built.unit.dir / built.conventions.tasks.start.unit_file, record)
    return Open(built, FakeGh(built.origin), github)


def picture(built: Built, name: str, data: bytes = PNG) -> Path:
    """Put a picture in the unit's shots folder."""
    path = built.unit.dir / "shots" / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def show(built: Built, *names: str) -> None:
    """Make `PR.md` show exactly these pictures and stamp a Review of the text as it is now."""
    path = built.unit.dir / "PR.md"
    head = path.read_text(encoding="utf-8").split("## Screenshots")[0]
    shots = (chr(10) * 2).join(f"![{name}](./shots/{name})" for name in names)
    path.write_text(head + "## Screenshots" + chr(10) * 2 + shots + chr(10), encoding="utf-8", newline="")
    built.review()
