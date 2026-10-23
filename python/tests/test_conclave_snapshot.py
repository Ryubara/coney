# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `conclave snapshot`: made-up task folders, stamp logs, worktrees and `gh` answers; no network."""

import argparse
import hashlib
import json
import os
from datetime import timedelta
from pathlib import Path
from typing import Any

import pytest
from conclave_support import (
    CONVENTIONS,
    NOW,
    FakeGh,
    Layout,
    ago,
    card,
    github_answers,
    iso,
    make_layout,
    make_task,
    sources,
    stamp_steps,
    unit,
)

from coney_tools import conclave_cli
from coney_tools.conclave_snapshot import SCHEMA, collect
from coney_tools.config import ConfigError
from coney_tools.issue_claim_cli import held_cards
from coney_tools.issue_claim_model import Card
from coney_tools.issue_claim_remote import GhRemote


@pytest.fixture
def layout(tmp_path: Path) -> Layout:
    """Two units: `fix-frame-812` (failed compare, building) and `fix-new` (no stamps), each with a worktree."""
    return make_layout(tmp_path)


# --- the local snapshot --------------------------------------------------------------------------------------------


def test_the_local_snapshot_has_the_schema_and_no_github(layout: Layout) -> None:
    gh = FakeGh({})
    report = collect(sources(layout, gh), github=False)
    assert report["schema"] == SCHEMA and report["github"] is False and report["time"] == iso(NOW)
    assert gh.calls == []
    assert report["claims"] is None and report["prs"] is None and report["issues"] is None


def test_a_unit_is_read_from_its_task_file_and_stamp_log(layout: Layout) -> None:
    row = unit(collect(sources(layout), github=False), "fix-frame-812")
    assert row["issues"] == [741] and row["kind"] == "fix" and row["claimant"] == "Ryubara"
    assert row["goal"] == "Fix the uncuff steer."
    assert row["scope"] == ["src/platform/frame.cpp", "src/ai/"]
    assert row["branch"] == "fix-frame-812" and row["worktree"].endswith("fix-frame-812")
    assert row["agent"] == {"id": "engineer-2", "type": "engineer"}
    assert row["error"] is None and row["stamp_problems"] == 0


def test_the_stage_follows_the_highest_step_done_and_a_failure_is_marked(layout: Layout) -> None:
    row = unit(collect(sources(layout), github=False), "fix-frame-812")
    states = {s["step"]: s["state"] for s in row["steps"]}
    assert states == {
        "assigned": "pass",
        "claimed": "pass",
        "studied": "pass",
        "fails-first": "pass",
        "compared": "fail",
        "checked": "missing",
        "reviewed": "missing",
        "pr-opened": "missing",
        "finished": "missing",
    }
    assert row["stage"] == "compared" and row["failed"] == "compared"
    assert row["stage_since"] == iso(ago(hours=5))


def test_a_unit_with_no_stamps_is_at_the_first_step(layout: Layout) -> None:
    row = unit(collect(sources(layout), github=False), "fix-new")
    assert row["stage"] == "assigned" and row["failed"] is None and row["agent"] is None
    assert {s["state"] for s in row["steps"]} == {"missing"}
    # with nothing stamped, the stage began when the task was claimed
    assert row["stage_since"] == row["claimed_at"]


def test_a_skipped_step_with_result_none_counts_as_done(layout: Layout) -> None:
    make_task(layout.scratch, "docs-only", (9,))
    stamp_steps(layout.scratch, "docs-only", [("assigned", "pass"), ("studied", "none"), ("checked", "pass")])
    row = unit(collect(sources(layout), github=False), "docs-only")
    assert row["stage"] == "reviewed"
    assert {s["step"]: s["state"] for s in row["steps"]}["studied"] == "none"


def test_a_finished_unit_stays_finished(layout: Layout) -> None:
    make_task(layout.scratch, "all-done", (9,))
    stamp_steps(layout.scratch, "all-done", [(step, "pass") for step in CONVENTIONS.stamps.steps])
    assert unit(collect(sources(layout), github=False), "all-done")["stage"] == "finished"


def test_a_broken_stamp_log_is_counted_and_the_unit_still_reads(layout: Layout) -> None:
    segment = layout.scratch / "fix-frame-812" / "stamps" / "0001.jsonl"
    segment.write_bytes(segment.read_bytes() + b"not json\n")
    row = unit(collect(sources(layout), github=False), "fix-frame-812")
    assert row["stamp_problems"] == 1 and row["stage"] == "compared"


def test_the_build_wait_shows_on_the_units_row(layout: Layout) -> None:
    report = collect(sources(layout), github=False)
    running = unit(report, "fix-frame-812")["build"]
    assert running == {
        "state": "running",
        "preset": "dev",
        "step": "build",
        "slot": 1,
        "lane": "main",
        "since": iso(ago(minutes=3)),
    }
    waiting = unit(report, "fix-new")["build"]
    assert waiting == {"state": "waiting", "preset": "dev", "position": 1, "lane": "main"}


def test_a_units_last_detached_build_is_kept(layout: Layout) -> None:
    result = {
        "state": "done",
        "agent": "a",
        "preset": "dev",
        "exit_code": 1,
        "started": 0.0,
        "ended": ago(minutes=9).timestamp(),
    }
    src = sources(layout)
    src.coord.build_result = lambda path: result if path.name == "fix-new" else None
    row = unit(collect(src, github=False), "fix-new")
    assert row["last_build"] == {"state": "done", "exit_code": 1, "ended": iso(ago(minutes=9))}


def test_a_unit_without_a_worktree_stays_for_unit_days_only(layout: Layout) -> None:
    old = make_task(layout.scratch, "old-gone", (1,))
    recent = make_task(layout.scratch, "recent-gone", (2,))
    long_ago = ago(days=CONVENTIONS.conclave.unit_days + 1).timestamp()
    os.utime(old / "TASK.md", (long_ago, long_ago))
    os.utime(recent / "TASK.md", (ago(hours=1).timestamp(),) * 2)
    tasks = {u["task"] for u in collect(sources(layout), github=False)["units"]["units"]}
    assert tasks == {"fix-frame-812", "fix-new", "recent-gone"}


def test_a_folder_without_a_task_file_is_no_unit(layout: Layout) -> None:
    (layout.scratch / "notes").mkdir()
    (layout.scratch / "notes" / "STATUS.md").write_text("x", encoding="utf-8")
    assert len(collect(sources(layout), github=False)["units"]["units"]) == 2


def test_a_bad_task_file_is_reported_on_its_row_and_the_others_stand(layout: Layout) -> None:
    (layout.scratch / "fix-new" / "TASK.md").write_text("no front matter\n", encoding="utf-8")
    report = collect(sources(layout), github=False)
    bad = unit(report, "fix-new")
    assert "does not start with a --- block" in bad["error"] and bad["steps"] == []
    assert unit(report, "fix-frame-812")["error"] is None


def test_units_whose_scopes_overlap_mark_each_other(layout: Layout) -> None:
    make_task(layout.scratch, "other", (50,), ("src/ai/",))
    report = collect(sources(layout), github=False)
    # a folder holds the files below it, so all three overlap one another; the scope listed is the unit's own entries
    assert unit(report, "other")["overlaps"] == [
        {"with": "fix-frame-812", "kind": "unit", "scope": ["src/ai/"]},
        {"with": "fix-new", "kind": "unit", "scope": ["src/ai/"]},
    ]
    assert unit(report, "fix-new")["overlaps"][0] == {
        "with": "fix-frame-812",
        "kind": "unit",
        "scope": ["src/ai/ai_goal.cpp"],
    }
    assert [o["with"] for o in unit(report, "fix-frame-812")["overlaps"]] == ["fix-new", "other"]


def test_a_unit_filter_keeps_one_unit(layout: Layout) -> None:
    report = collect(sources(layout), github=False, unit="fix-new")
    assert [u["task"] for u in report["units"]["units"]] == ["fix-new"]


def test_the_snapshot_reads_builds_and_the_machine(layout: Layout) -> None:
    report = collect(sources(layout), github=False)
    assert report["builds"]["lanes"]["main"]["slots"][0]["slot"] == 1
    machine = report["machine"]
    assert machine["error"] is None and machine["free_gb"] == 61.0
    assert machine["min_free_gb"] == CONVENTIONS.workspace.disk_min_free_gb
    assert machine["warn_free_gb"] == CONVENTIONS.workspace.disk_min_free_gb + CONVENTIONS.workspace.disk_margin_gb


# --- the config ----------------------------------------------------------------------------------------------------


def test_the_config_has_the_roles_with_aliases_and_the_helpers(layout: Layout) -> None:
    config = collect(sources(layout), github=False)["config"]
    assert config["error"] is None and config["source"] == ".claude/hooks/hooks.toml"
    assert config["roles"] == ["coordinator", "reviewer", "research-reviewer", "pr-reviewer"]
    assert config["helpers"] == ["Explore", "Plan", "fork"] and config["require_role"] is False
    assert config["claude_code"] == CONVENTIONS.conclave.claude_code
    assert config["intervals"] == {"local": 15, "github": 60, "github_idle": 300}
    assert config["pane_min_columns"] == 110 and config["merged_visible_minutes"] == 10
    assert config["claim_red_hours"] == 6
    assert config["band_narrow_columns"] == 100 and config["stale_after_reads"] == 3


def test_a_missing_hooks_file_gives_no_lists_and_says_why(layout: Layout) -> None:
    (layout.checkout / ".claude" / "hooks" / "hooks.toml").unlink()
    config = collect(sources(layout), github=False)["config"]
    assert "hooks.toml" in config["error"] and config["roles"] == [] and config["helpers"] == []


def test_a_hooks_file_that_is_not_toml_gives_no_lists(layout: Layout) -> None:
    (layout.checkout / ".claude" / "hooks" / "hooks.toml").write_text("roles = [", encoding="utf-8")
    config = collect(sources(layout), github=False)["config"]
    assert config["error"] and config["roles"] == [] and config["helpers"] == []


def test_a_wrong_shaped_hooks_file_drops_the_wrong_parts(layout: Layout) -> None:
    text = 'helpers = "Explore"\n[roles.coordinator]\naliases = "x"\n[roles]\n'
    (layout.checkout / ".claude" / "hooks" / "hooks.toml").write_text(text, encoding="utf-8")
    config = collect(sources(layout), github=False)["config"]
    assert config["helpers"] == []


# --- the GitHub snapshot -------------------------------------------------------------------------------------------


def test_the_claims_carry_deadlines_titles_and_the_account(layout: Layout) -> None:
    report = collect(sources(layout, FakeGh(github_answers())), github=True)
    claims = report["claims"]
    assert claims["error"] is None and claims["account"] == "Ryubara"
    assert claims["rules"] == {"warn_hours": 48, "deadline_hours": 72, "enforced": CONVENTIONS.claims.release}
    first, second = claims["claims"]
    assert first["issue"] == 741 and first["title"] == "gameplay: Fix the uncuff steer"
    assert first["deadline"] == iso(ago(hours=10) + timedelta(hours=72)) and first["claimant"] == "Ryubara"
    assert second["issue"] == 770 and second["title"] is None  # not an open issue any more
    assert second["warned"] is False and second["waiting"] is None


def test_a_claim_that_waits_has_no_deadline(layout: Layout) -> None:
    src = sources(layout, FakeGh(github_answers()), claims=lambda: [card(741, waiting="S-blocked")])
    [row] = collect(src, github=True)["claims"]["claims"]
    assert row["deadline"] is None and row["waiting"] == "S-blocked"


def test_the_pull_requests_are_matched_to_units_by_branch(layout: Layout) -> None:
    report = collect(sources(layout, FakeGh(github_answers())), github=True)
    assert report["prs"]["error"] is None
    [pull] = report["prs"]["prs"]
    assert pull["number"] == 812 and pull["ci"] == "fail" and pull["causes"] == ["checks-failed"]
    row = unit(report, "fix-frame-812")
    assert row["pr"] == {"number": 812, "state": "open", "ci": "fail", "causes": ["checks-failed"], "merged_at": None}
    assert unit(report, "fix-new")["pr"] is None


def test_a_recently_merged_pull_request_marks_its_unit(layout: Layout) -> None:
    make_task(layout.scratch, "add-task-start", (799,))
    row = unit(collect(sources(layout, FakeGh(github_answers())), github=True), "add-task-start")
    assert row["pr"] == {"number": 799, "state": "merged", "ci": None, "causes": [], "merged_at": iso(ago(minutes=4))}


def test_the_issues_section_holds_claimed_unit_and_critical_issues_only(layout: Layout) -> None:
    issues = collect(sources(layout, FakeGh(github_answers())), github=True)["issues"]
    assert [i["number"] for i in issues["issues"]] == [741, 790]
    assert issues["issues"][0] == {
        "number": 741,
        "title": "gameplay: Fix the uncuff steer",
        "status": "S-in-progress",
        "priority": "P-high",
        "assignees": ["Ryubara"],
    }
    assert issues["issues"][1]["priority"] == "P-critical"


def test_a_claim_scope_overlap_marks_the_unit(layout: Layout) -> None:
    src = sources(
        layout, FakeGh(github_answers()), claims=lambda: [card(741), card(770, "contrib-a", scope=("src/ai/",))]
    )
    row = unit(collect(src, github=True), "fix-frame-812")
    assert {"with": "#770", "kind": "claim", "scope": ["src/ai/"]} in row["overlaps"]
    assert all(o["with"] != "#741" for o in row["overlaps"])  # the unit's own claim is no overlap


def test_a_failed_section_says_why_and_the_others_stand(layout: Layout) -> None:
    def broken() -> list[Card]:
        raise OSError("gh issue list failed: not signed in")

    answers = github_answers()
    answers[("api", "user")] = OSError("gh api failed")
    report = collect(sources(layout, FakeGh(answers), claims=broken), github=True)
    assert report["claims"] == {"error": "gh issue list failed: not signed in"}
    assert report["prs"]["error"] is None and report["units"]["error"] is None


def test_a_failed_pull_request_read_leaves_the_units_without_pr(layout: Layout) -> None:
    answers = github_answers()
    answers[("pr", "list", "--state", "open")] = OSError("gh pr list failed")
    report = collect(sources(layout, FakeGh(answers)), github=True)
    assert report["prs"]["error"]
    assert unit(report, "fix-frame-812")["pr"] is None


def test_a_machine_that_cannot_say_its_disk_reports_the_error(layout: Layout) -> None:
    def broken() -> int:
        raise OSError("no such drive")

    machine = collect(sources(layout, free_bytes=broken), github=False)["machine"]
    assert machine == {"error": "no such drive"}


# --- the command ---------------------------------------------------------------------------------------------------


def args(**changes: Any) -> argparse.Namespace:
    """The parsed arguments of `conclave snapshot`."""
    base: dict[str, Any] = {"command": "snapshot", "json": False, "github": False, "unit": None}
    base.update(changes)
    return argparse.Namespace(**base)


def test_json_prints_one_object(layout: Layout) -> None:
    lines: list[str] = []
    assert conclave_cli.run(args(json=True), sources(layout), lines.append) == 0
    report = json.loads("\n".join(lines))
    assert report["schema"] == SCHEMA and len(report["units"]["units"]) == 2


def test_text_prints_one_line_per_section(layout: Layout) -> None:
    lines: list[str] = []
    assert conclave_cli.run(args(), sources(layout), lines.append) == 0
    assert lines[0].startswith("conclave snapshot ") and any(line.startswith("units: 2 units") for line in lines)
    assert any(line.startswith("machine: 61.0 GB free") for line in lines)


def test_an_unknown_unit_is_a_usage_error(layout: Layout) -> None:
    errors: list[str] = []
    assert conclave_cli.run(args(unit="nope"), sources(layout), print, errors.append) == 2
    assert "nope" in errors[0]


def test_the_command_parses_from_the_cli() -> None:
    from coney_tools import cli

    parsed = cli._build_parser().parse_args(["conclave", "snapshot", "--json", "--github", "--unit", "x"])
    assert (parsed.group, parsed.command, parsed.json, parsed.github, parsed.unit) == (
        "conclave",
        "snapshot",
        True,
        True,
        "x",
    )


#: `gh` arguments that write: a method other than GET, a body, or a verb that changes something.
WRITING = {"-X", "--method", "-f", "-F", "--field", "--raw-field", "--input", "create", "edit", "comment", "close"}
WRITING |= {"merge", "delete", "reopen", "ready", "review", "lock", "unlock", "transfer", "develop", "label"}


def tree_state(root: Path) -> dict[str, tuple[int, int, str]]:
    """Every path under `root` with its size, modification time and content hash (folders have no hash)."""
    state: dict[str, tuple[int, int, str]] = {}
    for path in sorted(root.rglob("*")):
        info = path.stat()
        digest = "" if path.is_dir() else hashlib.sha256(path.read_bytes()).hexdigest()
        state[path.relative_to(root).as_posix()] = (info.st_size, info.st_mtime_ns, digest)
    return state


class NoCards:
    """`gh` for the claim reader when the repository holds no claim card: prints nothing and records each call."""

    def __init__(self) -> None:
        self.calls: list[list[str]] = []

    def __call__(self, args: list[str]) -> str:
        self.calls.append(args)
        return ""


def test_the_snapshot_never_writes_and_only_reads_from_github(layout: Layout) -> None:
    coord_gh = FakeGh(github_answers())
    cards_gh = NoCards()
    rules = CONVENTIONS.claims
    # the real claim reader over a fake `gh`, so the claim path is exercised and not stubbed out
    remote = GhRemote(rules.bot_login, rules.card_marker, rules.reply_marker, cards_gh)
    before = tree_state(layout.root)
    collect(sources(layout, coord_gh, claims=lambda: held_cards(remote, CONVENTIONS)), github=True)
    assert tree_state(layout.root) == before
    calls = [*coord_gh.calls, *cards_gh.calls]
    assert cards_gh.calls and coord_gh.calls, "both readers must have run"
    assert all(not WRITING & set(call) for call in calls), f"gh was asked to write: {calls}"
    assert {call[0] for call in calls} <= {"api", "issue", "pr"}


def test_a_checkout_outside_a_workspace_is_a_config_error() -> None:
    with pytest.raises(ConfigError):
        conclave_cli.default_sources(Path("/"))
