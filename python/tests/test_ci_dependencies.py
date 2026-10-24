# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools ci dependencies` and `ci rerun-dependencies`: made-up pull requests as `gh` gives them."""

import argparse
import json
from pathlib import Path
from typing import Any

import pytest
from conventions_support import REPO, write_checkout

from coney_tools import ci_cli
from coney_tools.ci_dependencies import dependency_numbers, rerun_waiting, waiting_on
from coney_tools.config import ConfigError
from coney_tools.conventions import load_conventions

CONVENTIONS = load_conventions(REPO)
LINE = CONVENTIONS.pull_requests.stacked.depends_line
SHA = "c" * 40


class FakeGh:
    """A `gh` that answers the pull request reads and the run reads of the commands, and records every call."""

    def __init__(
        self,
        states: dict[int, str] | None = None,
        pulls: list[dict[str, Any]] | None = None,
        runs: dict[str, list[dict[str, Any]]] | None = None,
        body: str = "",
    ) -> None:
        self.states = states or {}
        self.pulls = pulls or []
        self.runs = runs or {}
        self.body = body
        self.calls: list[list[str]] = []

    def __call__(self, arguments: list[str]) -> str:
        self.calls.append(arguments)
        if arguments[:2] == ["pr", "list"]:
            return json.dumps(self.pulls)
        if arguments[:2] == ["pr", "view"]:
            number = int(arguments[2])
            if "body" in arguments[-1]:
                return json.dumps({"body": self.body})
            if number not in self.states:
                raise OSError("gh pr view failed: Could not resolve to a PullRequest with the number of 99")
            return json.dumps({"number": number, "state": self.states[number]})
        if arguments[:2] == ["run", "list"]:
            return json.dumps(self.runs.get(arguments[arguments.index("--commit") + 1], []))
        assert arguments[:2] == ["run", "rerun"], arguments
        return ""

    def reruns(self) -> list[list[str]]:
        """The `gh run rerun` calls made."""
        return [call for call in self.calls if call[:2] == ["run", "rerun"]]


# --- reading the description ---------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("body", "expected"),
    [
        ("", ()),
        ("## Summary\n\nNothing depends on anything.\n", ()),
        ("Depends on #12\n", (12,)),
        ("Depends on #12\nDepends on #7\n", (12, 7)),
        ("depends  on #12, #13 and #14\n", (12, 13, 14)),
        ("  Depends on #12\n", (12,)),
        ("Depends on #12\nDepends on #12\n", (12,)),
        # not a line of its own, or inside a comment: the template's guidance never counts
        ("This one depends on #12 being merged.\n", ()),
        ("<!-- Depends on #12 -->\n", ()),
        ("Depends on the merge of #12\n", ()),
        ("Depends on #12\r\nDepends on #13\r\n", (12, 13)),
    ],
)
def test_the_description_names_the_pull_requests_it_depends_on(body: str, expected: tuple[int, ...]) -> None:
    assert dependency_numbers(body, LINE) == expected


def test_the_line_comes_from_the_conventions_not_from_the_code() -> None:
    assert dependency_numbers("Blocked by PR #5\n", "Blocked by PR #N") == (5,)
    assert dependency_numbers("Depends on #5\n", "Blocked by PR #N") == ()


# --- the check ------------------------------------------------------------------------------------------------------


def test_a_pull_request_with_no_dependency_waits_for_nothing() -> None:
    gh = FakeGh()
    assert waiting_on(10, "No dependency.\n", LINE, gh) == []
    assert gh.calls == []


def test_merged_dependencies_do_not_wait() -> None:
    gh = FakeGh({12: "MERGED", 13: "MERGED"})
    assert waiting_on(10, "Depends on #12\nDepends on #13\n", LINE, gh) == []


def test_an_open_dependency_waits_and_says_so() -> None:
    gh = FakeGh({12: "MERGED", 13: "OPEN"})
    result = waiting_on(10, "Depends on #12\nDepends on #13\n", LINE, gh)
    assert [(item.number, item.line()) for item in result] == [(13, "#13 is still open: it must be merged first")]


def test_a_closed_unmerged_dependency_waits_for_ever_and_says_so() -> None:
    result = waiting_on(10, "Depends on #12\n", LINE, FakeGh({12: "CLOSED"}))
    assert result[0].line() == "#12 was closed without merging: change or remove the Depends on line"


def test_a_dependency_that_is_not_a_pull_request_is_refused() -> None:
    result = waiting_on(10, "Depends on #99\n", LINE, FakeGh({}))
    assert result[0].line() == "#99 is not a pull request: Depends on names pull requests, not issues"


def test_a_pull_request_cannot_depend_on_itself() -> None:
    gh = FakeGh({10: "OPEN"})
    result = waiting_on(10, "Depends on #10\n", LINE, gh)
    assert result[0].line() == "#10 is this pull request itself"
    assert gh.calls == []


def test_a_github_failure_is_an_error_not_a_wait() -> None:
    def broken(arguments: list[str]) -> str:
        raise OSError("gh pr view failed: HTTP 502")

    with pytest.raises(OSError, match="HTTP 502"):
        waiting_on(10, "Depends on #12\n", LINE, broken)


# --- the command ----------------------------------------------------------------------------------------------------


def command(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, gh: FakeGh, *flags: str) -> int:
    """Run `ci <flags>` in a checkout of the real conventions with the fake gh."""
    write_checkout(tmp_path)
    monkeypatch.chdir(tmp_path)
    parser = argparse.ArgumentParser()
    ci_cli.add_ci_parser(parser.add_subparsers(dest="group"))
    return ci_cli.run_ci(parser.parse_args(["ci", *flags]), gh)


def test_the_check_reads_the_live_description_and_passes(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    gh = FakeGh({12: "MERGED"}, body="Depends on #12\n")
    assert command(tmp_path, monkeypatch, gh, "dependencies", "--pr", "10") == 0
    assert capsys.readouterr().out == "dependencies: ok (#12 merged)\n"
    assert gh.calls[0][:3] == ["pr", "view", "10"] and "body" in gh.calls[0][-1]


def test_the_check_fails_with_one_line_per_waiting_pull_request(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    gh = FakeGh({12: "OPEN", 13: "CLOSED"}, body="Depends on #12\nDepends on #13\n")
    assert command(tmp_path, monkeypatch, gh, "dependencies", "--pr", "10") == 1
    out = capsys.readouterr().out.splitlines()
    assert out[0].startswith("dependencies: waiting on 2 pull requests")
    assert out[1:] == [
        "  #12 is still open: it must be merged first",
        "  #13 was closed without merging: change or remove the Depends on line",
    ]


def test_the_check_with_no_dependency_passes_without_asking_github_for_one(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    gh = FakeGh(body="## Summary\n\nNone.\n")
    assert command(tmp_path, monkeypatch, gh, "dependencies", "--pr", "10") == 0
    assert capsys.readouterr().out == "dependencies: ok (no Depends on line)\n"


def test_the_check_reads_a_description_file_for_a_local_try(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    (tmp_path / "pr.md").write_text("Depends on #12\n", encoding="utf-8")
    gh = FakeGh({12: "OPEN"})
    assert command(tmp_path, monkeypatch, gh, "dependencies", "--body", "pr.md") == 1
    assert "#12 is still open" in capsys.readouterr().out


def test_the_check_needs_a_pull_request_or_a_file(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    with pytest.raises(ConfigError, match="--pr or --body"):
        command(tmp_path, monkeypatch, FakeGh(), "dependencies")


def test_a_github_failure_exits_2(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    def broken(arguments: list[str]) -> str:
        raise OSError("gh could not run: not installed")

    write_checkout(tmp_path)
    monkeypatch.chdir(tmp_path)
    parser = argparse.ArgumentParser()
    ci_cli.add_ci_parser(parser.add_subparsers(dest="group"))
    args = parser.parse_args(["ci", "dependencies", "--pr", "10"])
    assert ci_cli.run_ci(args, broken) == 2
    assert "not installed" in capsys.readouterr().err


# --- the rerun ------------------------------------------------------------------------------------------------------


def waiting_pull(number: int, body: str, head: str = SHA) -> dict[str, Any]:
    """An open pull request as `gh pr list --json number,body,headRefOid` gives it."""
    return {"number": number, "body": body, "headRefOid": head}


def run_of(run_id: int, conclusion: str, status: str = "completed", event: str = "pull_request") -> dict[str, Any]:
    """A workflow run as `gh run list --json databaseId,conclusion,status,event` gives it."""
    return {"databaseId": run_id, "conclusion": conclusion, "status": status, "event": event}


def test_a_failed_run_whose_dependencies_have_all_merged_is_rerun() -> None:
    gh = FakeGh({12: "MERGED"}, [waiting_pull(10, "Depends on #12\n")], {SHA: [run_of(700, "failure")]})
    results = rerun_waiting(CONVENTIONS, gh, apply=True)
    assert [(item.number, item.status) for item in results] == [(10, "reran")]
    assert gh.reruns() == [["run", "rerun", "700", "--failed"]]


def test_without_apply_the_rerun_is_only_planned() -> None:
    gh = FakeGh({12: "MERGED"}, [waiting_pull(10, "Depends on #12\n")], {SHA: [run_of(700, "failure")]})
    results = rerun_waiting(CONVENTIONS, gh, apply=False)
    assert [(item.number, item.status) for item in results] == [(10, "would-rerun")]
    assert gh.reruns() == []


def test_a_pull_request_still_waiting_on_one_dependency_is_left_alone() -> None:
    gh = FakeGh(
        {12: "MERGED", 13: "OPEN"},
        [waiting_pull(10, "Depends on #12\nDepends on #13\n")],
        {SHA: [run_of(700, "failure")]},
    )
    assert rerun_waiting(CONVENTIONS, gh, apply=True) == []
    assert gh.reruns() == []


def test_a_run_that_passed_or_is_still_running_is_not_rerun() -> None:
    pulls = [waiting_pull(10, "Depends on #12\n", "a" * 40), waiting_pull(11, "Depends on #12\n", "b" * 40)]
    runs = {"a" * 40: [run_of(1, "success")], "b" * 40: [run_of(2, "", status="in_progress")]}
    gh = FakeGh({12: "MERGED"}, pulls, runs)
    assert rerun_waiting(CONVENTIONS, gh, apply=True) == []
    assert gh.reruns() == []


def test_only_the_newest_pull_request_run_is_looked_at() -> None:
    runs = {SHA: [run_of(9, "success"), run_of(8, "failure")]}
    gh = FakeGh({12: "MERGED"}, [waiting_pull(10, "Depends on #12\n")], runs)
    assert rerun_waiting(CONVENTIONS, gh, apply=True) == []


def test_a_pull_request_with_no_run_yet_is_reported_not_rerun() -> None:
    gh = FakeGh({12: "MERGED"}, [waiting_pull(10, "Depends on #12\n")], {})
    assert rerun_waiting(CONVENTIONS, gh, apply=True) == []


def test_a_pull_request_without_a_depends_line_is_never_looked_at() -> None:
    gh = FakeGh({}, [waiting_pull(10, "No dependency.\n")], {SHA: [run_of(700, "failure")]})
    assert rerun_waiting(CONVENTIONS, gh, apply=True) == []
    assert not [call for call in gh.calls if call[0] == "run"]


def test_a_refused_rerun_is_reported_and_the_others_still_go() -> None:
    class Refusing(FakeGh):
        def __call__(self, arguments: list[str]) -> str:
            if arguments[:3] == ["run", "rerun", "700"]:
                raise OSError("gh run rerun failed: the run is still in progress")
            return super().__call__(arguments)

    pulls = [waiting_pull(10, "Depends on #12\n", "a" * 40), waiting_pull(11, "Depends on #12\n", "b" * 40)]
    runs = {"a" * 40: [run_of(700, "failure")], "b" * 40: [run_of(701, "failure")]}
    results = rerun_waiting(CONVENTIONS, Refusing({12: "MERGED"}, pulls, runs), apply=True)
    assert [(item.number, item.status) for item in results] == [(10, "refused"), (11, "reran")]
    assert "still in progress" in results[0].detail


def test_one_pass_restarts_at_most_the_limit() -> None:
    pulls = [waiting_pull(n, "Depends on #12\n", f"{n:040d}") for n in range(20, 25)]
    runs = {f"{n:040d}": [run_of(n * 10, "failure")] for n in range(20, 25)}
    gh = FakeGh({12: "MERGED"}, pulls, runs)
    from dataclasses import replace

    limited = replace(
        CONVENTIONS, ci=replace(CONVENTIONS.ci, dependencies=replace(CONVENTIONS.ci.dependencies, rerun_limit=2))
    )
    results = rerun_waiting(limited, gh, apply=True)
    assert [item.status for item in results] == ["reran", "reran", "capped", "capped", "capped"]
    assert len(gh.reruns()) == 2


def test_the_rerun_command_prints_one_line_each(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    gh = FakeGh({12: "MERGED"}, [waiting_pull(10, "Depends on #12\n")], {SHA: [run_of(700, "failure")]})
    assert command(tmp_path, monkeypatch, gh, "rerun-dependencies") == 0
    assert capsys.readouterr().out == "#10 would rerun run 700 (dependencies are merged)\n"
    assert command(tmp_path, monkeypatch, gh, "rerun-dependencies", "--apply") == 0
    assert capsys.readouterr().out == "#10 reran run 700 (dependencies are merged)\n"
    # the workflow file that is asked for comes from the conventions
    assert any(call[:4] == ["run", "list", "--workflow", "dependencies.yml"] for call in gh.calls)


def test_the_workflow_of_the_conventions_exists_and_runs_the_two_commands() -> None:
    import yaml

    path = REPO / ".github" / "workflows" / CONVENTIONS.ci.dependencies.workflow
    workflow = yaml.safe_load(path.read_text(encoding="utf-8"))
    commands = "\n".join(str(step.get("run", "")) for job in workflow["jobs"].values() for step in job["steps"])
    assert "ci dependencies --pr" in commands and "ci rerun-dependencies --apply" in commands
    # the check is the job's name: it is a required check
    assert "dependencies" in workflow["jobs"] and "dependencies" in CONVENTIONS.merge.required_checks
    assert workflow["jobs"]["rerun"]["permissions"]["actions"] == "write"
