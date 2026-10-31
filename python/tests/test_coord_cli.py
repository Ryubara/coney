# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools coord status`: a made-up scratch folder, worktrees and `gh` answers; no network, no git."""

import argparse
import json
import os
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any

import pytest

from coney_tools import cli, coord_cli
from coney_tools.coord_cli import Sources, actions, collect

NOW = datetime(2026, 10, 20, 12, 0, tzinfo=UTC)


def stamp(path: Path, hours_ago: float) -> None:
    """Set a file's last change `hours_ago` hours before NOW."""
    moment = (NOW - timedelta(hours=hours_ago)).timestamp()
    os.utime(path, (moment, moment))


def write(path: Path, hours_ago: float = 1.0, text: str = "x\n") -> Path:
    """Write a file with a last change `hours_ago` hours before NOW."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    stamp(path, hours_ago)
    return path


def when(hours_ago: float) -> str:
    """A GitHub timestamp `hours_ago` hours before NOW."""
    return (NOW - timedelta(hours=hours_ago)).strftime("%Y-%m-%dT%H:%M:%SZ")


class FakeGh:
    """`gh` answering from canned JSON by its first two arguments; records every call."""

    def __init__(self, answers: dict[tuple[str, ...], Any]) -> None:
        self.answers = answers
        self.calls: list[list[str]] = []

    def __call__(self, args: list[str]) -> str:
        self.calls.append(args)
        for key, value in self.answers.items():
            if tuple(args[: len(key)]) == key:
                if isinstance(value, Exception):
                    raise value
                return json.dumps(value)
        raise OSError(f"unexpected gh {args}")


@pytest.fixture
def layout(tmp_path: Path) -> dict[str, Path]:
    """A scratch folder with three branches' folders and two worktrees."""
    scratch = tmp_path / "scratch"
    trees = tmp_path / "worktrees"
    for name in ("unit-a", "unit-b"):
        (trees / name).mkdir(parents=True)
    write(scratch / "unit-a" / "PR.md", hours_ago=2)
    write(scratch / "unit-a" / "PR-2.md", hours_ago=2)
    write(scratch / "unit-a" / "PR-notes.md", hours_ago=2)  # not a unit file
    write(scratch / "unit-a" / "STATUS.md", hours_ago=0.5)
    write(scratch / "unit-b" / "PR.md", hours_ago=30)
    write(scratch / "unit-b" / "STATUS.md", hours_ago=200)  # old, but its worktree exists: still a track
    (trees / "unit-b" / ".coney-busy").write_text("", encoding="utf-8")
    write(scratch / "gone" / "PR.md", hours_ago=5)  # no worktree: history, never queued
    write(scratch / "gone" / "STATUS.md", hours_ago=24 * 10)  # no worktree and old: not a track
    write(scratch / "fresh" / "STATUS.md", hours_ago=3)  # no worktree but recent: a track
    return {"scratch": scratch, "trees": trees}


def sources(layout: dict[str, Path], gh: FakeGh | None = None, **changes: Any) -> Sources:
    """Sources over the made-up layout."""
    trees = layout["trees"]
    results = {
        "unit-a": {
            "state": "done",
            "agent": "a",
            "preset": "dev",
            "exit_code": 0,
            "started": 0.0,
            "ended": (NOW - timedelta(minutes=10)).timestamp(),
        },
        "unit-b": {
            "state": "done",
            "agent": "b",
            "preset": "dev",
            "exit_code": 1,
            "started": 0.0,
            "ended": (NOW - timedelta(hours=3)).timestamp(),
        },
    }
    base: dict[str, Any] = {
        "main": layout["scratch"].parent / "repos" / "coney",
        "scratch": layout["scratch"],
        "now": lambda: NOW,
        "gh": gh or FakeGh({}),
        "worktrees": lambda: [(trees / "unit-a", "unit-a"), (trees / "unit-b", "unit-b")],
        "remote_branches": lambda: {"unit-b"},
        "pcsx2": lambda: [{"copy": "pcsx2", "state": "free", "claim": None, "running": False}],
        "builds": lambda: {"main": {"slots": [{"slot": 1, "stale": False, "claim": None}], "queue": []}},
        "build_result": lambda path: results.get(path.name),
        "health": lambda pulls, required, now: {
            "stuck": [
                {"number": 7, "cause": "checks-failed"},
                {"number": 7, "cause": "conflict"},
            ],
            "other": [],
            "notices": [],
        },
    }
    base.update(changes)
    return Sources(**base)


def github_answers() -> dict[tuple[str, ...], Any]:
    """`gh` answers: three issues, one open pull request, the ruleset, a pull request list of every state, runs."""
    rollup = [
        {
            "__typename": "CheckRun",
            "name": "build-ok",
            "status": "COMPLETED",
            "conclusion": "FAILURE",
            "startedAt": when(1),
            "completedAt": when(0.9),
        }
    ]
    return {
        ("issue", "list"): [
            {
                "number": 12,
                "labels": [{"name": "S-needs-triage"}, {"name": "bug"}, {"name": "A-ai"}],
                "createdAt": when(48),
                "updatedAt": when(1),
            },
            {
                "number": 3,
                "labels": [{"name": "S-ready-to-start"}, {"name": "P-high"}, {"name": "task"}],
                "createdAt": when(240),
                "updatedAt": when(2),
            },
            {"number": 4, "labels": [], "createdAt": when(1), "updatedAt": when(1)},
        ],
        ("pr", "list", "--state", "open"): [
            {
                "number": 7,
                "headRefName": "unit-b",
                "baseRefName": "main",
                "isDraft": False,
                "mergeable": "CONFLICTING",
                "autoMergeRequest": None,
                "statusCheckRollup": rollup,
                "updatedAt": when(1),
            },
        ],
        ("api",): {
            "rules": [
                {"type": "required_status_checks", "parameters": {"required_status_checks": [{"context": "build-ok"}]}}
            ]
        },
        ("pr", "list", "--state", "all"): [
            {"number": 7, "headRefName": "unit-b", "state": "OPEN", "closedAt": None},
            {"number": 5, "headRefName": "unit-a", "state": "MERGED", "closedAt": when(10)},  # older than its PR.md
            {"number": 2, "headRefName": "gone", "state": "MERGED", "closedAt": when(1)},
        ],
        ("run", "list", "--status", "queued"): [{"databaseId": 1}, {"databaseId": 2}],
        ("run", "list", "--status", "in_progress"): [{"databaseId": 3}],
    }


def test_local_read_never_calls_gh_and_lists_the_local_parts(layout: dict[str, Path]) -> None:
    gh = FakeGh({})
    report = collect(sources(layout, gh), github=False)
    assert gh.calls == []
    assert report["issues"] is None and report["prs"] is None and report["ci"] is None
    assert report["pcsx2"]["copies"][0]["copy"] == "pcsx2"
    assert report["builds"]["lanes"]["main"]["slots"][0]["slot"] == 1
    tracks = {t["branch"]: t for t in report["tracks"]["tracks"]}
    assert set(tracks) == {"unit-a", "unit-b", "fresh"}
    assert tracks["unit-b"]["busy"] and not tracks["unit-a"]["busy"]
    assert report["tracks"]["tracks"][0]["branch"] == "unit-a"  # newest first
    units = [(u["branch"], u["file"], u["pushed"], u["queued"]) for u in report["release"]["units"]]
    assert units == [
        ("unit-a", "PR-2.md", False, None),
        ("unit-a", "PR.md", False, None),
        ("unit-b", "PR.md", True, None),
    ]
    # only the recent finished build is news; a local read has no GitHub kinds
    assert report["action"] == ["build:done:unit-a"]


def test_github_read_adds_issues_prs_ci_and_the_release_queue(layout: dict[str, Path]) -> None:
    gh = FakeGh(github_answers())
    report = collect(sources(layout, gh), github=True)
    issues = {i["number"]: i for i in report["issues"]["issues"]}
    assert issues[12]["status"] == "S-needs-triage" and issues[12]["area"] == "A-ai" and issues[12]["labels"] == ["bug"]
    assert issues[3]["priority"] == "P-high" and issues[3]["age_days"] == 10.0
    assert "title" not in issues[12]
    assert report["issues"]["counts"] == {"S-needs-triage": 1, "S-ready-to-start": 1, "no status": 1}
    pr = report["prs"]["prs"][0]
    assert (pr["number"], pr["branch"], pr["ci"], pr["mergeable"]) == (7, "unit-b", "fail", "CONFLICTING")
    assert pr["causes"] == ["checks-failed", "conflict"]
    assert report["ci"] == {"error": None, "queued": 2, "in_progress": 1}
    queued = {(u["branch"], u["file"]): (u["queued"], u["pr"]) for u in report["release"]["units"]}
    # unit-a's merged pull request is older than its files: they are new units; unit-b has an open one
    assert queued == {
        ("unit-a", "PR.md"): (True, None),
        ("unit-a", "PR-2.md"): (True, None),
        ("unit-b", "PR.md"): (False, 7),
    }
    assert report["action"] == [
        "build:done:unit-a",
        "issue:12:S-needs-triage",
        "pr:7:checks-failed",
        "pr:7:conflict",
        "unit:unit-a:PR-2.md",
        "unit:unit-a:PR.md",
    ]
    assert all(call[0] in ("issue", "pr", "api", "run") for call in gh.calls)


def test_a_merged_pull_request_newer_than_the_unit_file_releases_it(layout: dict[str, Path]) -> None:
    answers = github_answers()
    answers[("pr", "list", "--state", "all")] = [
        {"number": 9, "headRefName": "unit-a", "state": "MERGED", "closedAt": when(1)}
    ]
    report = collect(sources(layout, FakeGh(answers)), github=True)
    unit = next(u for u in report["release"]["units"] if u["branch"] == "unit-a")
    assert (unit["queued"], unit["pr"], unit["pr_state"]) == (False, 9, "MERGED")


def test_a_section_that_cannot_be_read_says_why_and_the_others_stand(layout: dict[str, Path]) -> None:
    answers = github_answers()
    answers[("issue", "list")] = OSError("gh issue list failed: not signed in")

    def broken() -> list[dict[str, object]]:
        raise OSError("no PCSX2 copies configured")

    report = collect(sources(layout, FakeGh(answers), pcsx2=broken), github=True)
    assert report["issues"] == {"error": "gh issue list failed: not signed in"}
    assert report["pcsx2"] == {"error": "no PCSX2 copies configured"}
    assert report["prs"]["error"] is None
    assert "pr:7:conflict" in report["action"]


def test_no_scratch_folder_is_an_error_not_an_empty_queue(layout: dict[str, Path], tmp_path: Path) -> None:
    report = collect(sources(layout, scratch=tmp_path / "missing"), github=False)
    assert report["release"]["error"].startswith("no scratch directory")
    assert report["tracks"]["error"].startswith("no scratch directory")


def test_actions_from_a_report() -> None:
    report = {
        "issues": {"issues": [{"number": 1, "status": "S-needs-triage"}, {"number": 2, "status": "S-blocked"}]},
        "prs": {"health": {"stuck": [{"number": 4, "cause": "ci-stuck"}]}},
        "release": {"units": [{"branch": "b", "file": "PR.md", "queued": True}, {"branch": "c", "file": "PR.md"}]},
        "results": {
            "results": [
                {"worktree": "w", "state": "done", "recent": True},
                {"worktree": "x", "state": "running", "recent": False},
            ]
        },
    }
    assert actions(report) == ["build:done:w", "issue:1:S-needs-triage", "pr:4:ci-stuck", "unit:b:PR.md"]


def test_the_command_prints_json_and_text(layout: dict[str, Path]) -> None:
    lines: list[str] = []
    assert coord_cli.run(argparse.Namespace(json=True, github=False), sources(layout), lines.append) == 0
    assert json.loads(lines[0])["action"] == ["build:done:unit-a"]
    lines.clear()
    coord_cli.run(argparse.Namespace(json=False, github=False), sources(layout), lines.append)
    assert lines[-1] == "action: build:done:unit-a"
    assert any(line.startswith("release: 3 units") for line in lines)


def test_the_cli_registers_coord_status() -> None:
    args = cli._build_parser().parse_args(["coord", "status", "--json", "--github"])
    assert (args.group, args.command, args.json, args.github) == ("coord", "status", True, True)


def ids_args(github: bool, *kinds: str) -> argparse.Namespace:
    """The arguments of `coord status --ids [--github] [--kind K ...]`."""
    return argparse.Namespace(json=False, github=github, ids=True, kind=list(kinds) or None)


def test_ids_print_one_id_per_line_of_the_kinds_asked(layout: dict[str, Path]) -> None:
    gh = FakeGh(github_answers())
    lines: list[str] = []
    assert coord_cli.run(ids_args(True, "unit"), sources(layout, gh), lines.append) == 0
    assert lines == ["unit:unit-a:PR-2.md", "unit:unit-a:PR.md"]
    # only what the release queue needs is read from GitHub: no issues, pull-request health or CI queue
    assert [tuple(call[:3]) for call in gh.calls] == [("pr", "list", "--state")]


def test_ids_of_a_local_kind_never_read_github(layout: dict[str, Path]) -> None:
    gh = FakeGh({})
    lines: list[str] = []
    assert coord_cli.run(ids_args(False, "build"), sources(layout, gh), lines.append) == 0
    assert lines == ["build:done:unit-a"]
    assert gh.calls == []


def test_ids_without_a_kind_print_every_id_of_the_read(layout: dict[str, Path]) -> None:
    lines: list[str] = []
    assert coord_cli.run(ids_args(True), sources(layout, FakeGh(github_answers())), lines.append) == 0
    assert lines == [
        "build:done:unit-a",
        "issue:12:S-needs-triage",
        "pr:7:checks-failed",
        "pr:7:conflict",
        "unit:unit-a:PR-2.md",
        "unit:unit-a:PR.md",
    ]


def test_ids_fail_when_a_section_they_need_could_not_be_read(layout: dict[str, Path]) -> None:
    answers = github_answers()
    answers[("pr", "list", "--state", "all")] = OSError("gh pr list failed: not signed in")
    lines: list[str] = []
    errors: list[str] = []
    code = coord_cli.run(ids_args(True, "unit"), sources(layout, FakeGh(answers)), lines.append, errors.append)
    # an empty list would read as "nothing waits": the reader is told the read failed instead
    assert code == 1
    assert lines == []
    assert errors == ["coord status: release not read: gh pr list failed: not signed in"]


def test_ids_of_a_github_kind_need_github(layout: dict[str, Path]) -> None:
    errors: list[str] = []
    assert coord_cli.run(ids_args(False, "unit"), sources(layout), print, errors.append) == 2
    assert errors == ["coord status: --kind unit needs --github"]


def test_the_cli_takes_ids_and_kinds() -> None:
    args = cli._build_parser().parse_args(["coord", "status", "--ids", "--github", "--kind", "unit", "--kind", "issue"])
    assert (args.ids, args.github, args.kind) == (True, True, ["unit", "issue"])
    with pytest.raises(SystemExit):
        cli._build_parser().parse_args(["coord", "status", "--ids", "--kind", "nope"])


@pytest.mark.reads("docs_only")
def test_the_documented_tether_pollers_run_commands_that_parse() -> None:
    # docs/guides/coney-tools-setup.md#coord-wakes is the one source of the coordinator's wake settings: its commands
    # must still be valid `coney-tools` command lines, and its issues poller must watch the triage label
    page = (Path(__file__).resolve().parents[2] / "docs" / "guides" / "coney-tools-setup.md").read_text(
        encoding="utf-8"
    )
    section = page[page.index("{#coord-wakes}") :]
    block = section[section.index("```json") + len("```json") : section.index("```", section.index("```json") + 7)]
    pollers = json.loads(block)["watch"]["pollers"]
    assert {p["id"] for p in pollers} == {"triage", "builds"}
    triage = next(p for p in pollers if p["id"] == "triage")
    assert (triage["kind"], triage["labels"]) == ("issues", [coord_cli.TRIAGE_LABEL])
    for poller in pollers:
        command = poller.get("command")
        if not command:
            continue
        args = cli._build_parser().parse_args(command[command.index("coney-tools") + 1 :])
        if args.group == "coord":
            assert args.ids and args.kind
            assert args.github == any(kind in coord_cli.GITHUB_KINDS for kind in args.kind)


def broken(message: str) -> Any:
    """A source that fails with `message`, as an unreadable one does."""

    def read(*_: object) -> Any:
        raise OSError(message)

    return read


@pytest.mark.parametrize(
    ("kind", "github", "change", "reason"),
    [
        (
            "unit",
            True,
            {"worktrees": broken("git worktree list failed")},
            "worktrees not read: git worktree list failed",
        ),
        (
            "build",
            False,
            {"worktrees": broken("git worktree list failed")},
            "worktrees not read: git worktree list failed",
        ),
        ("build", False, {"build_result": broken("result unreadable")}, "results not read: result unreadable"),
        ("pr", True, {"health": broken("pr-health failed")}, "prs not read: pr-health failed"),
    ],
)
def test_ids_fail_when_any_section_their_kind_needs_failed(
    layout: dict[str, Path], kind: str, github: bool, change: dict[str, Any], reason: str
) -> None:
    lines: list[str] = []
    errors: list[str] = []
    gh = FakeGh(github_answers())
    assert coord_cli.run(ids_args(github, kind), sources(layout, gh, **change), lines.append, errors.append) == 1
    assert lines == []
    assert errors == [f"coord status: {reason}"]


def test_ids_of_issues_fail_when_the_issues_were_not_read(layout: dict[str, Path]) -> None:
    answers = github_answers()
    answers[("issue", "list")] = OSError("gh issue list failed")
    errors: list[str] = []
    assert coord_cli.run(ids_args(True, "issue"), sources(layout, FakeGh(answers)), print, errors.append) == 1
    assert errors == ["coord status: issues not read: gh issue list failed"]


def test_a_failure_in_a_section_the_ids_do_not_need_leaves_them_standing(layout: dict[str, Path]) -> None:
    answers = github_answers()
    answers[("issue", "list")] = OSError("gh issue list failed")
    lines: list[str] = []
    # the release queue does not need the issues, and pcsx2 is never an id's section
    src = sources(layout, FakeGh(answers), pcsx2=broken("no copies"))
    assert coord_cli.run(ids_args(True, "unit"), src, lines.append) == 0
    assert lines == ["unit:unit-a:PR-2.md", "unit:unit-a:PR.md"]
    lines.clear()
    assert coord_cli.run(ids_args(False), sources(layout, pcsx2=broken("no copies")), lines.append) == 0
    assert lines == ["build:done:unit-a"]


def test_a_kind_reads_only_its_sections(layout: dict[str, Path]) -> None:
    gh = FakeGh({})
    src = sources(layout, gh, pcsx2=broken("must not be read"), builds=broken("must not be read"))
    report = collect(src, github=False, kinds=["build"])
    assert report["pcsx2"] is None and report["builds"] is None and report["tracks"] is None
    assert report["release"] is None and report["results"]["error"] is None
    assert report["action"] == ["build:done:unit-a"]
    assert gh.calls == []


def test_ids_and_json_together_are_refused(layout: dict[str, Path]) -> None:
    errors: list[str] = []
    args = argparse.Namespace(json=True, github=False, ids=True, kind=None)
    assert coord_cli.run(args, sources(layout), print, errors.append) == 2
    assert errors == ["coord status: --ids and --json print different things; give one"]
