# SPDX-License-Identifier: GPL-3.0-or-later
"""The parts of claiming that are switched on at the cutover, tried before it, and the checks around a claim.

* `claims.trial`: an issue labelled `claim-trial` has adoption and the idle release on, with short hours, so the live
  test (docs/guides/issues.md#claiming) can watch a warning and a release on a throwaway issue.
* A claim is never released unwarned (`Card.deadline`): one already idle past the deadline when the release is
  switched on, or seen late by the sweep, is warned first and keeps the full notice.
* `claim check-pr` (`issue_claim_pr.py`): the issues a pull request closes are claimed by its author.
* `claim is-command`: the one test the user-level no-comment-as-user hook asks for a claim command.
* `claim_progress`: a stage stamp posts `/progress`.
* `claim run` end to end through the real `GhRemote` with a `gh` that answers like GitHub, on event files shaped like
  the ones GitHub sends (`claim_payloads/`), so the argument lists and the parsing are what the Action runs.
"""

import json
import re
from dataclasses import replace
from datetime import timedelta
from pathlib import Path
from typing import Any

import pytest
from audit_support import CONVENTIONS
from claim_support import BOT, START, Clock, FakeGitHub, make_engine
from conventions_support import REPO

from coney_tools import claim_progress, stamps
from coney_tools import issue_claim_cli as cli
from coney_tools import issue_claim_model as model
from coney_tools.issue_claim_engine import Engine
from coney_tools.issue_claim_pr import check_pull_request, report
from coney_tools.unit_record import UnitRecord, write_record

#: The repository's rules as committed: adoption and release off, as they are until the cutover.
OFF = CONVENTIONS
RULES = OFF.claims
TRIAL = RULES.trial.label
PAYLOADS = Path(__file__).parent / "claim_payloads"
READY = ("task", "A-tools", "P-low", "S-ready-to-start")


def card_of(github: FakeGitHub, number: int) -> model.Card:
    card = model.parse_card(github.cards(number)[-1].body, RULES)
    assert card is not None
    return card


def payload(name: str) -> dict[str, Any]:
    data = json.loads((PAYLOADS / name).read_text(encoding="utf-8"))
    assert isinstance(data, dict)
    return data


# --- the trial label ------------------------------------------------------------------------------------------------


def test_the_trial_is_configured_with_short_hours_and_a_listed_label() -> None:
    assert not RULES.release and not RULES.adopt
    assert TRIAL == "claim-trial" and TRIAL in OFF.labels.names()
    assert 0 < RULES.trial.warn_hours < RULES.trial.deadline_hours < RULES.warn_hours


def test_a_trial_issue_is_warned_and_released_on_the_trial_hours_while_release_is_off() -> None:
    clock = Clock()
    github = FakeGitHub(clock)
    github.add_issue(41, labels=(*READY, TRIAL))
    github.add_issue(42, labels=READY)
    for number in (41, 42):
        github.say(number, "Ryubara", "/claim", "OWNER")
        Engine(github, OFF, clock.now).reconcile(number)
    trial_text = github.cards(41)[-1].body
    deadline = model.iso_time(START + timedelta(hours=RULES.trial.deadline_hours))
    assert f'<relative-time datetime="{deadline}">' in trial_text
    assert "not enforced yet" in github.cards(42)[-1].body
    clock.advance(hours=RULES.trial.warn_hours, minutes=5)
    Engine(github, OFF, clock.now).sweep()
    assert len(github.others(41)) == 1 and "@Ryubara your claim of this issue expires" in github.others(41)[0].body
    assert github.others(42) == []
    clock.advance(hours=RULES.trial.deadline_hours - RULES.trial.warn_hours)
    Engine(github, OFF, clock.now).sweep()
    assert github.view(41).assignees == () and card_of(github, 41).status == "released"
    assert "S-ready-to-start" in github.view(41).labels and github.others(41) == []
    assert github.view(42).assignees == ("Ryubara",)


def test_a_trial_issue_assigned_by_hand_is_adopted() -> None:
    clock = Clock()
    github = FakeGitHub(clock)
    github.add_issue(41, labels=(*READY, TRIAL), assignees=("carol",))
    github.add_issue(42, labels=READY, assignees=("carol",))
    Engine(github, OFF, clock.now).sweep()
    assert card_of(github, 41).adopted and github.cards(42) == []


def test_taking_the_trial_label_off_turns_the_deadline_off_again() -> None:
    clock = Clock()
    github = FakeGitHub(clock)
    github.add_issue(41, labels=(*READY, TRIAL))
    github.say(41, "Ryubara", "/claim", "OWNER")
    Engine(github, OFF, clock.now).reconcile(41)
    github.items[41].view = replace(github.view(41), labels=tuple(x for x in github.view(41).labels if x != TRIAL))
    clock.advance(hours=10)
    Engine(github, OFF, clock.now).reconcile(41)
    assert github.view(41).assignees == ("Ryubara",) and "not enforced yet" in github.cards(41)[-1].body


def test_the_engine_goes_back_to_the_repository_rules_after_a_trial_issue() -> None:
    clock = Clock()
    github = FakeGitHub(clock)
    github.add_issue(41, labels=(*READY, TRIAL))
    engine = Engine(github, OFF, clock.now)
    engine.reconcile(41)
    assert engine.rules is OFF.claims


# --- never released unwarned ------------------------------------------------------------------------------------------


def test_switching_the_release_on_warns_an_old_claim_before_it_releases_it() -> None:
    clock = Clock()
    github = FakeGitHub(clock)
    github.add_issue(10, labels=READY)
    github.say(10, "alice", "/claim")
    Engine(github, OFF, clock.now).reconcile(10)
    clock.advance(hours=200)  # idle for days while the release was off
    make_engine(github, clock).reconcile(10)  # the cutover: release on
    assert github.view(10).assignees == ("alice",) and len(github.others(10)) == 1
    notice = RULES.deadline_hours - RULES.warn_hours
    assert f"expires in about {notice} h" in github.others(10)[0].body
    assert card_of(github, 10).deadline(RULES) == clock.now() + timedelta(hours=notice)
    clock.advance(hours=notice - 1)
    make_engine(github, clock).reconcile(10)
    assert github.view(10).assignees == ("alice",)
    clock.advance(hours=1)
    make_engine(github, clock).reconcile(10)
    assert github.view(10).assignees == () and card_of(github, 10).status == "released"


def test_a_warning_on_time_keeps_the_72_hour_deadline() -> None:
    card = model.Card(issue=1, claimant="a", claimed_at=START)
    warned = card.changed(warned_at=START + timedelta(hours=RULES.warn_hours))
    assert warned.deadline(RULES) == card.deadline(RULES) == START + timedelta(hours=RULES.deadline_hours)
    late = card.changed(warned_at=START + timedelta(hours=RULES.warn_hours + 5))
    assert late.deadline(RULES) == START + timedelta(hours=RULES.deadline_hours + 5)


# --- the pull request check -------------------------------------------------------------------------------------------


def claimed(github: FakeGitHub, number: int, login: str) -> None:
    github.add_issue(number, labels=READY)
    github.say(number, login, "/claim", "OWNER" if login == "Ryubara" else "NONE")
    Engine(github, OFF, github.clock.now).reconcile(number)


def test_a_pull_request_of_the_claimant_passes() -> None:
    github = FakeGitHub(Clock())
    claimed(github, 41, "carol")
    claimed(github, 42, "carol")
    result = check_pull_request(payload("pull_request_opened.json"), github, OFF)
    assert result.problems == [] and result.notes == ["#41: claimed by carol", "#42: claimed by carol"]


def test_an_unclaimed_issue_or_someone_elses_is_a_problem() -> None:
    github = FakeGitHub(Clock())
    claimed(github, 41, "alice")
    github.add_issue(42, labels=READY)
    result = check_pull_request(payload("pull_request_opened.json"), github, OFF)
    assert len(result.problems) == 2
    assert result.problems[0].startswith("#41 is claimed by alice, not by the author carol")
    assert result.problems[1].startswith("#42 has no claim")


def test_a_maintainers_pull_request_may_close_anyones_claimed_issue_but_not_an_unclaimed_one() -> None:
    github = FakeGitHub(Clock())
    claimed(github, 41, "alice")
    github.add_issue(42, labels=READY)
    data = payload("pull_request_opened.json")
    data["pull_request"]["user"]["login"] = "Ryubara"
    result = check_pull_request(data, github, OFF)
    assert [p[:4] for p in result.problems] == ["#42 "]


def test_a_bot_or_a_pull_request_that_closes_nothing_is_not_checked() -> None:
    github = FakeGitHub(Clock())
    assert check_pull_request(payload("pull_request_dependabot.json"), github, OFF).problems == []
    data = payload("pull_request_opened.json")
    data["pull_request"]["body"] = "## Summary\n\nPart of #41"
    result = check_pull_request(data, github, OFF)
    assert result.problems == [] and "closes no issue" in result.notes[0]


def test_problems_warn_until_the_cutover_and_fail_after() -> None:
    github = FakeGitHub(Clock())
    github.add_issue(41, labels=READY)
    github.add_issue(42, labels=READY)
    result = check_pull_request(payload("pull_request_opened.json"), github, OFF)
    lines: list[str] = []
    assert report(result, OFF, lines.append) == 0
    assert sum(line.startswith("::warning::") for line in lines) == 2 and "not enforced" in lines[-1]
    on = replace(OFF, claims=replace(RULES, require_on_pr=True))
    lines.clear()
    assert report(result, on, lines.append) == 1
    assert sum(line.startswith("::error::") for line in lines) == 2


def test_the_pr_workflow_runs_the_check_with_read_only_issues() -> None:
    import yaml

    workflow = yaml.safe_load((REPO / ".github/workflows/pr.yml").read_text(encoding="utf-8"))
    job = workflow["jobs"]["claim"]
    assert job["permissions"] == {"contents": "read", "issues": "read", "pull-requests": "read"}
    run = job["steps"][-1]["run"]
    assert 'coney-tools claim check-pr --payload "$GITHUB_EVENT_PATH"' in run and "${{" not in run


# --- is-command, for the no-comment hook -----------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("text", "code", "said"),
    [
        ("/claim", 0, "claim"),
        ("/claim scope: src/ai/, 0x00322b78 task: 41-x", 0, "claim"),
        ("/claim renew", 0, "renew"),
        ("/unclaim", 0, "unclaim"),
        ("/progress studied", 0, "progress"),
        ("/progress lunch", 1, "not a stage"),
        ("/release", 1, "not a claim command"),
        ("/claim\nand more", 1, "whole comment on one line"),
        ("thanks, I'll /claim it", 1, "not a claim command"),
    ],
)
def test_is_command_says_whether_a_text_is_exactly_one_command(
    text: str, code: int, said: str, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(REPO)
    args = parse("is-command", text)
    assert cli.run_claim(args, lambda arguments: "") == code
    assert said in capsys.readouterr().out


def test_is_command_reads_stdin(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    import io

    monkeypatch.chdir(REPO)
    monkeypatch.setattr("sys.stdin", io.StringIO("/claim renew\n"))
    assert cli.run_claim(parse("is-command", "-"), lambda arguments: "") == 0
    assert capsys.readouterr().out.strip() == "renew"


# --- /progress after a stage stamp ------------------------------------------------------------------------------------


def unit_folder(tmp_path: Path, issues: tuple[int, ...] = (41, 42)) -> Path:
    """A started unit: its record and its stamp folder."""
    folder = tmp_path / "41-trial"
    (folder / "stamps").mkdir(parents=True)
    record = UnitRecord("41-trial", "tooling", issues, "Ryubara", "41-trial", tmp_path, "origin/main", "a" * 40, START)
    write_record(folder / OFF.tasks.start.unit_file, record)
    return folder / "stamps"


def stamp(folder: Path, step: str, result: str = "pass") -> list[stamps.Stamp]:
    """Append one stamp (no observer runs: the test calls the poster itself)."""
    entry = {
        "step": step,
        "subject": "0x00100000" if step in ("studied", "compared") else "",
        "result": result,
        "commit": "a" * 40,
        "covers": {"x_sha256": "b" * 64},
        "detail": {},
        "who": {"kind": "tool", "tool": "test"},
    }
    return stamps.append_many(folder, rules=OFF.stamps, task="41-trial", entries=[entry])


def test_a_new_stage_posts_progress_on_every_issue_and_the_same_stage_waits(tmp_path: Path) -> None:
    folder = unit_folder(tmp_path)
    posted: list[tuple[int, str]] = []

    def post(number: int, body: str) -> int:
        posted.append((number, body))
        return 1

    def run(step: str, hours: float = 0, result: str = "pass") -> list[int]:
        when = START + timedelta(hours=hours)
        return claim_progress.post_progress(folder, stamp(folder, step, result), OFF, post, when, print)

    assert run("studied") == [41, 42] and posted == [(41, "/progress studied"), (42, "/progress studied")]
    assert run("studied", 1) == []
    assert run("compared", 2) == [41, 42]
    assert run("compared", 2 + RULES.progress_refresh_hours - 1) == []
    assert run("compared", 2 + RULES.progress_refresh_hours) == [41, 42]
    assert run("checked", 30, "fail") == []
    assert run("fails-first", 31, "none") == [41, 42]


def test_no_unit_record_or_a_post_that_fails_never_stops_the_stamp(tmp_path: Path) -> None:
    lone = tmp_path / "lone" / "stamps"
    lone.mkdir(parents=True)
    assert claim_progress.post_progress(lone, stamp(lone, "checked"), OFF, lambda n, b: 1, START, print) == []
    folder = unit_folder(tmp_path)
    warnings: list[str] = []

    def broken(number: int, body: str) -> int:
        raise OSError("gh: network down")

    assert claim_progress.post_progress(folder, stamp(folder, "checked"), OFF, broken, START, warnings.append) == []
    assert len(warnings) == 2 and "the next stamp tries again" in warnings[0]


def test_an_issue_whose_post_failed_is_tried_again_at_the_next_stamp(tmp_path: Path) -> None:
    """The state is kept per issue: one that took the comment is not posted twice, one that did not is retried."""
    folder = unit_folder(tmp_path)
    posted: list[int] = []
    down = {42}

    def post(number: int, body: str) -> int:
        if number in down:
            raise OSError("gh: network down")
        posted.append(number)
        return 1

    warnings: list[str] = []
    when = START + timedelta(hours=1)
    assert claim_progress.post_progress(folder, stamp(folder, "compared"), OFF, post, when, warnings.append) == [41]
    assert len(warnings) == 1 and "#42" in warnings[0]
    down.clear()
    # Well inside the refresh hours: only the issue that failed is posted again.
    soon = when + timedelta(minutes=5)
    assert claim_progress.post_progress(folder, stamp(folder, "compared"), OFF, post, soon, warnings.append) == [42]
    assert posted == [41, 42]
    assert claim_progress.post_progress(folder, stamp(folder, "compared"), OFF, post, soon, warnings.append) == []
    # A damaged state file is the same as none: everything is posted again.
    (folder.parent / claim_progress.STATE_FILE).write_text("{not json", encoding="utf-8")
    assert claim_progress.post_progress(folder, stamp(folder, "compared"), OFF, post, soon, warnings.append) == [41, 42]


def test_the_post_of_a_stamp_gives_up_after_the_configured_seconds(monkeypatch: pytest.MonkeyPatch) -> None:
    """A slow network must not hang a stamp command: the real post runs `gh` with `claims.progress_post_seconds`."""
    seen: list[tuple[list[str], float]] = []

    def fake_gh(arguments: list[str], timeout: float = 300) -> str:
        seen.append((arguments, timeout))
        return "https://github.invalid/o/r/issues/41#issuecomment-77"

    monkeypatch.setattr(claim_progress, "run_gh", fake_gh)
    assert claim_progress._gh_post(OFF)(41, "/progress studied") == 77
    assert seen[0][0][:3] == ["issue", "comment", "41"] and seen[0][1] == RULES.progress_post_seconds == 30


def test_the_stamp_log_tells_its_observer_and_a_failing_observer_does_not_fail_the_write(tmp_path: Path) -> None:
    folder = tmp_path / "stamps"
    folder.mkdir()
    seen: list[str] = []
    stamps.observe("test", lambda where, written: seen.extend(s.step for s in written))
    try:
        stamp(folder, "checked")
        assert seen == ["checked"]

        def broken(where: Path, written: list[stamps.Stamp]) -> None:
            raise OSError("boom")

        stamps.observe("test", broken)
        assert stamp(folder, "reviewed")[0].step == "reviewed"
    finally:
        stamps.observe("test", None)


def test_under_pytest_the_registered_observer_posts_nothing(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    folder = unit_folder(tmp_path)
    monkeypatch.chdir(REPO)
    calls: list[object] = []
    monkeypatch.setattr(claim_progress, "_gh_post", lambda conventions: calls.append(conventions))
    claim_progress.observer()(folder, stamp(folder, "checked"))
    assert calls == []


def test_the_command_line_registers_the_observer() -> None:
    from coney_tools.cli import main

    main(["claim", "is-command", "/unclaim"])
    assert "claim progress" in stamps._OBSERVERS


# --- claim run through GhRemote, on events shaped like GitHub's -------------------------------------------------------


class GitHubOverGh:
    """A `gh` that answers the claim Action's calls the way GitHub's REST and GraphQL APIs do, from an in-memory
    repository, and records every write; it proves the argument lists and the parsing of `GhRemote`."""

    def __init__(self) -> None:
        self.issues: dict[int, dict[str, Any]] = {}
        self.comments: list[dict[str, Any]] = []
        self.next_id = 50_000
        self.writes: list[str] = []
        self.permissions = {"Ryubara": "admin"}

    def add_issue(self, number: int, labels: tuple[str, ...]) -> None:
        self.issues[number] = {
            "number": number,
            "title": "tools: Fix the claim trial",
            "state": "open",
            "state_reason": None,
            "labels": [{"name": name} for name in labels],
            "assignees": [],
            "body": "### Summary\n\nA throwaway issue.",
        }

    def comment(self, number: int, login: str, body: str, kind: str = "User", at: str = "2026-10-27T12:00:00Z") -> int:
        self.next_id += 1
        self.comments.append(
            {
                "id": self.next_id,
                "issue_url": f"https://api.github.invalid/repos/octo/coney/issues/{number}",
                "user": {"login": login, "type": kind},
                "author_association": "OWNER" if login == "Ryubara" else "NONE",
                "body": body,
                "created_at": at,
                "updated_at": at,
            }
        )
        return self.next_id

    def _field(self, arguments: list[str], name: str) -> str:
        for index, item in enumerate(arguments):
            if item in ("-f", "-F") and arguments[index + 1].startswith(f"{name}="):
                return arguments[index + 1].split("=", 1)[1]
        raise AssertionError(f"no field {name} in {arguments}")

    def __call__(self, arguments: list[str]) -> str:
        if arguments[0] != "api":
            raise AssertionError(f"unexpected gh call {arguments}")
        method = arguments[arguments.index("--method") + 1] if "--method" in arguments else "GET"
        path = next(item for item in arguments[1:] if item.startswith("repos/") or item == "graphql")
        if path == "graphql":
            return json.dumps({"data": {"repository": {"issue": {"closedByPullRequestsReferences": {"nodes": []}}}}})
        found = re.match(r"repos/\{owner\}/\{repo\}/(.*)$", path)
        assert found, path
        rest = found.group(1)
        if method == "GET":
            return self._get(rest)
        self.writes.append(f"{method} {rest.split('?')[0]}")
        return self._write(method, rest, arguments)

    def _get(self, rest: str) -> str:
        if (m := re.fullmatch(r"issues/(\d+)", rest)) is not None:
            return json.dumps(self.issues[int(m.group(1))])
        if (m := re.fullmatch(r"issues/(\d+)/comments", rest)) is not None:
            mine = [c for c in self.comments if c["issue_url"].endswith(f"/{m.group(1)}")]
            return "\n".join(json.dumps(c) for c in mine)
        if rest.startswith("issues/comments"):
            # The repository-wide listings: the bot's cards and replies, or people's commands since a time.
            bot = [c for c in self.comments if c["user"]["login"] == BOT]
            people = [c for c in self.comments if c["user"]["type"] != "Bot" and c["body"].startswith("/")]
            return "\n".join(json.dumps(c) for c in (people if "since=" in rest else bot))
        if rest.startswith("issues?"):
            label = re.search(r"labels=([^&]+)", rest)
            numbers = [
                n
                for n, issue in self.issues.items()
                if issue["state"] == "open"
                and (
                    (label is not None and label.group(1) in {x["name"] for x in issue["labels"]})
                    or ("assignee=*" in rest and issue["assignees"])
                )
            ]
            return "\n".join(str(n) for n in numbers)
        if (m := re.fullmatch(r"collaborators/([^/]+)/permission", rest)) is not None:
            return self.permissions.get(m.group(1), "read") + "\n"
        raise AssertionError(f"unexpected GET {rest}")

    def _write(self, method: str, rest: str, arguments: list[str]) -> str:
        if (m := re.fullmatch(r"issues/(\d+)/assignees", rest)) is not None:
            issue = self.issues[int(m.group(1))]
            login = self._field(arguments, "assignees[]")
            if method == "POST":
                issue["assignees"].append({"login": login})
            else:
                issue["assignees"] = [a for a in issue["assignees"] if a["login"] != login]
            return json.dumps(issue)
        if (m := re.fullmatch(r"issues/(\d+)/labels(?:/(.+))?", rest)) is not None:
            issue = self.issues[int(m.group(1))]
            if method == "POST":
                issue["labels"].append({"name": self._field(arguments, "labels[]")})
            else:
                issue["labels"] = [x for x in issue["labels"] if x["name"] != m.group(2)]
            return "[]"
        if (m := re.fullmatch(r"issues/(\d+)/comments", rest)) is not None:
            made = self.comment(int(m.group(1)), BOT, self._field(arguments, "body"), "Bot")
            return f"{made}\n"
        if (m := re.fullmatch(r"issues/comments/(\d+)", rest)) is not None:
            target = int(m.group(1))
            if method == "DELETE":
                self.comments = [c for c in self.comments if c["id"] != target]
            else:
                next(c for c in self.comments if c["id"] == target)["body"] = self._field(arguments, "body")
            return "{}"
        raise AssertionError(f"unexpected {method} {rest}")


def parse(*words: str) -> Any:
    import argparse

    parser = argparse.ArgumentParser()
    cli.add_claim_parser(parser.add_subparsers(dest="group"))
    return parser.parse_args(["claim", *words])


def test_a_claim_comment_event_runs_end_to_end_through_gh(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(REPO)
    event = payload("issue_comment_claim.json")
    gh = GitHubOverGh()
    gh.add_issue(41, ("task", "A-tools", "P-low", "S-ready-to-start", TRIAL))
    command = gh.comment(41, "Ryubara", event["comment"]["body"])
    event["comment"]["id"] = command
    path = tmp_path / "event.json"
    path.write_text(json.dumps(event), encoding="utf-8")
    assert cli.run_claim(parse("run", "--event", "issue_comment", "--payload", str(path)), gh, lambda: START) == 0
    out = capsys.readouterr().out
    assert "#41: Ryubara claimed it as a maintainer (permission admin)" in out
    assert f"#41: deleted the command comment {command}" in out
    issue = gh.issues[41]
    assert [a["login"] for a in issue["assignees"]] == ["Ryubara"]
    labels = {x["name"] for x in issue["labels"]}
    assert "S-in-progress" in labels and "S-ready-to-start" not in labels
    cards = [c for c in gh.comments if c["body"].startswith("<!-- coney-claim -->")]
    assert len(cards) == 1 and not any(c["id"] == command for c in gh.comments)
    card = model.parse_card(cards[0]["body"], RULES)
    assert card is not None and (card.task, card.scope) == ("41-claim-trial", ("python/",))
    assert "<relative-time datetime=" in cards[0]["body"]  # the trial label: the live countdown
    assert gh.writes[:3] == [
        "POST issues/41/assignees",
        "DELETE issues/41/labels/S-ready-to-start",
        "POST issues/41/labels",
    ]
    assert gh.writes[-1] == f"DELETE issues/comments/{command}"


def test_a_pull_request_event_file_names_the_issues_it_closes() -> None:
    event = payload("pull_request_opened.json")
    assert cli.issues_of_event("pull_request_target", event, OFF) == [41, 42]
    activity = cli._pull_request_activity("pull_request_target", event, lambda: START)
    assert activity is not None and activity.by == "carol"


def test_a_recorded_test_decision_is_not_a_stage_reached(tmp_path: Path) -> None:
    """`orig fails-first --accept-change` appends `fails-first` stamps of result `none` before any test has run; they
    carry `detail.decision`, and the stage counts only once the engineer's own run is stamped."""
    folder = unit_folder(tmp_path)
    entry = {
        "step": "fails-first",
        "subject": "t",
        "result": "none",
        "commit": "a" * 40,
        "covers": {"x_sha256": "b" * 64},
        "detail": {"decision": "changed-test", "reason": "the first version asserted the bug"},
        "who": {"kind": "tool", "tool": "test"},
    }
    written = stamps.append_many(folder, rules=OFF.stamps, task="41-trial", entries=[entry])
    posted: list[int] = []

    def post(number: int, body: str) -> int:
        posted.append(number)
        return 1

    assert claim_progress.stage_reached(written) is None
    assert claim_progress.post_progress(folder, written, OFF, post, START, print) == [] and posted == []
