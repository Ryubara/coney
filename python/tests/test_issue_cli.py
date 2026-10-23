# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.issue_cli (`issue draft`, `check`, `create`, `edit`, `link`) and the `issue.form` audit rule.

`gh` is a recording fake, so nothing reaches the network; the files on main are a made-up tree.
"""

import argparse
import json
from pathlib import Path

import pytest
from audit_support import CONVENTIONS, issue, snapshot
from conventions_support import REPO
from issue_support import GOOD, TREE, body
from pytest import CaptureFixture, MonkeyPatch

from coney_tools import audit_github, cli, issue_cli
from coney_tools.audit import Problem
from coney_tools.config import ConfigError

TITLE = "gameplay: Add the Throw orders"


class FakeGh:
    """Records the gh commands and answers them; a write also records the body file's text as it was then."""

    def __init__(self, states: dict[int, str] | None = None, fail_on: str = "") -> None:
        self.calls: list[list[str]] = []
        self.bodies: list[str] = []
        self.states = states or {}
        self.fail_on = fail_on

    def __call__(self, arguments: list[str]) -> str:
        self.calls.append(arguments)
        if self.fail_on and arguments[:2] == self.fail_on.split():
            raise OSError(f"gh {self.fail_on} failed: boom")
        if "--body-file" in arguments:
            self.bodies.append(Path(arguments[arguments.index("--body-file") + 1]).read_text(encoding="utf-8"))
        if arguments[:2] == ["issue", "create"]:
            return "https://example.invalid/someone/coney/issues/401\n"
        if arguments[0] == "api" and arguments[-1] == ".id":
            return "9001\n"
        if arguments[:2] == ["issue", "view"] and "title" in arguments[-1]:
            return json.dumps({"title": "gameplay: Add the current title"})
        if arguments[0] in ("pr", "issue") and arguments[1] == "view":
            number = int(arguments[2])
            if number in self.states:
                return json.dumps({"state": self.states[number].upper()})
            raise OSError("not found")
        return ""

    def writes(self) -> list[list[str]]:
        """The commands that change something on GitHub."""
        return [call for call in self.calls if call[:2] in (["issue", "create"], ["issue", "edit"]) or "POST" in call]


@pytest.fixture(autouse=True)
def made_up_main(monkeypatch: MonkeyPatch) -> None:
    """Every check sees the made-up main, whatever this checkout has fetched."""
    monkeypatch.setattr(audit_github, "git_tree", lambda root, ref: TREE)


def parse(*arguments: str) -> argparse.Namespace:
    """The parsed command line of `coney-tools issue ...`."""
    return cli._build_parser().parse_args(["issue", *arguments])


def write(tmp_path: Path, text: str) -> str:
    """A body file; returns its path."""
    path = tmp_path / "body.md"
    path.write_text(text, encoding="utf-8")
    return str(path)


# --- draft ----------------------------------------------------------------------------------------------------------


def test_draft_prints_the_task_form_by_default_and_the_bug_form_on_request(capsys: CaptureFixture[str]) -> None:
    assert cli.main(["issue", "draft"]) == 0
    assert "### Fix list or steps" in capsys.readouterr().out
    assert cli.main(["issue", "draft", "bug"]) == 0
    assert "### Steps to reproduce" in capsys.readouterr().out


def test_draft_of_an_unknown_form_is_a_usage_error(capsys: CaptureFixture[str]) -> None:
    assert cli.main(["issue", "draft", "epic"]) == 2
    assert "no form 'epic'" in capsys.readouterr().err


# --- check ----------------------------------------------------------------------------------------------------------


def test_check_passes_a_good_body(tmp_path: Path, capsys: CaptureFixture[str]) -> None:
    assert cli.main(["issue", "check", "--title", TITLE, "--body", write(tmp_path, GOOD)]) == 0
    assert "issue check (task form): ok" in capsys.readouterr().out


def test_check_prints_each_problem_with_its_kind_and_exits_1(tmp_path: Path, capsys: CaptureFixture[str]) -> None:
    bad = body(area="docs").replace("### Screenshots", "## Screenshots")
    assert cli.main(["issue", "check", "--title", TITLE, "--body", write(tmp_path, bad)]) == 1
    out = capsys.readouterr().out
    assert "[choices] Area is 'docs'" in out and "[headings] '## Screenshots' is level 2" in out
    assert "2 problem(s)" in out


def test_check_reads_stdin_with_a_dash(monkeypatch: MonkeyPatch, capsys: CaptureFixture[str]) -> None:
    import io

    monkeypatch.setattr("sys.stdin", io.StringIO(GOOD))
    assert cli.main(["issue", "check", "--title", TITLE, "--body", "-"]) == 0


def test_check_of_a_missing_body_file_is_a_usage_error(capsys: CaptureFixture[str]) -> None:
    assert cli.main(["issue", "check", "--title", TITLE, "--body", "nowhere.md"]) == 2
    assert "cannot be read" in capsys.readouterr().err


def test_check_leftover_wants_the_from_line(tmp_path: Path, capsys: CaptureFixture[str]) -> None:
    no_from = body(screenshots="Screenshots: none (AI logic)")
    path = write(tmp_path, no_from)
    assert cli.main(["issue", "check", "--title", TITLE, "--body", path]) == 0
    assert cli.main(["issue", "check", "--title", TITLE, "--body", path, "--leftover"]) == 1
    assert "[from]" in capsys.readouterr().out


def test_check_online_compares_the_words_with_github(tmp_path: Path, capsys: CaptureFixture[str]) -> None:
    gh = FakeGh(states={317: "merged", 335: "open"})
    args = parse("check", "--title", TITLE, "--body", write(tmp_path, GOOD), "--online")
    assert issue_cli.run_check(args, gh) == 1
    assert "#317 is called open but is merged" in capsys.readouterr().out
    unknown = parse("check", "--title", TITLE, "--body", write(tmp_path, GOOD), "--online")
    assert issue_cli.run_check(unknown, FakeGh()) == 0
    assert "gh could not read #317" in capsys.readouterr().out


def test_check_offline_does_not_call_gh(tmp_path: Path) -> None:
    gh = FakeGh()
    assert issue_cli.run_check(parse("check", "--title", TITLE, "--body", write(tmp_path, GOOD)), gh) == 0
    assert gh.calls == []


# --- create ---------------------------------------------------------------------------------------------------------


def test_create_dry_run_checks_and_prints_the_command_but_writes_nothing(
    tmp_path: Path, capsys: CaptureFixture[str]
) -> None:
    gh = FakeGh()
    args = parse(
        "create", "--title", TITLE, "--body", write(tmp_path, GOOD), "--parent", "334", "--offline", "--dry-run"
    )
    assert issue_cli.run_create(args, gh) == 0
    out = capsys.readouterr().out
    assert 'gh issue create --title "gameplay: Add the Throw orders" --body-file' in out
    assert "--label task --label S-needs-triage --label A-ai" in out
    assert "/issues/334/sub_issues" in out and "dry run: nothing was written" in out
    assert gh.calls == []


def test_create_files_the_issue_with_the_forms_labels_and_the_area_label(
    tmp_path: Path, capsys: CaptureFixture[str]
) -> None:
    gh = FakeGh()
    commented = "<!-- guidance from the draft -->\n" + GOOD
    args = parse("create", "--title", TITLE, "--body", write(tmp_path, commented), "--offline", "--label", "P-low")
    assert issue_cli.run_create(args, gh) == 0
    create = gh.writes()[0]
    assert create[:4] == ["issue", "create", "--title", TITLE]
    labels = [create[i + 1] for i, word in enumerate(create) if word == "--label"]
    assert labels == ["task", "S-needs-triage", "A-ai", "P-low"]
    assert "<!--" not in gh.bodies[0] and gh.bodies[0].startswith("### Summary")
    assert "https://example.invalid/someone/coney/issues/401" in capsys.readouterr().out
    assert len(gh.writes()) == 1


def test_create_links_the_new_issue_to_its_parent_by_database_id(tmp_path: Path, capsys: CaptureFixture[str]) -> None:
    gh = FakeGh()
    args = parse("create", "--title", TITLE, "--body", write(tmp_path, GOOD), "--offline", "--parent", "334")
    assert issue_cli.run_create(args, gh) == 0
    assert ["api", "repos/{owner}/{repo}/issues/401", "--jq", ".id"] in gh.calls
    assert gh.calls[-1] == [
        "api",
        "repos/{owner}/{repo}/issues/334/sub_issues",
        "-X",
        "POST",
        "-F",
        "sub_issue_id=9001",
    ]
    assert "linked #401 as a sub-issue of #334" in capsys.readouterr().out


def test_create_stops_before_gh_when_the_check_fails(tmp_path: Path, capsys: CaptureFixture[str]) -> None:
    gh = FakeGh()
    args = parse("create", "--title", TITLE, "--body", write(tmp_path, body(area="docs")), "--offline")
    assert issue_cli.run_create(args, gh) == 1
    assert gh.calls == []
    assert "Area is 'docs'" in capsys.readouterr().out


def test_create_refuses_an_unknown_label(tmp_path: Path, capsys: CaptureFixture[str]) -> None:
    gh = FakeGh()
    args = parse("create", "--title", TITLE, "--body", write(tmp_path, GOOD), "--offline", "--label", "urgent")
    assert issue_cli.run_create(args, gh) == 1
    assert gh.calls == [] and "'urgent' is not in labels.list" in capsys.readouterr().out


def test_create_reads_the_states_with_gh_unless_offline(tmp_path: Path, capsys: CaptureFixture[str]) -> None:
    gh = FakeGh(states={317: "merged", 335: "open"})
    args = parse("create", "--title", TITLE, "--body", write(tmp_path, GOOD))
    assert issue_cli.run_create(args, gh) == 1
    assert "#317 is called open but is merged" in capsys.readouterr().out
    assert gh.writes() == []


def test_create_reports_a_gh_failure_and_a_failed_link(tmp_path: Path, capsys: CaptureFixture[str]) -> None:
    args = parse("create", "--title", TITLE, "--body", write(tmp_path, GOOD), "--offline")
    assert issue_cli.run_create(args, FakeGh(fail_on="issue create")) == 1
    assert "the issue was not created" in capsys.readouterr().err
    linked = parse("create", "--title", TITLE, "--body", write(tmp_path, GOOD), "--offline", "--parent", "334")
    assert issue_cli.run_create(linked, FakeGh(fail_on="api repos/{owner}/{repo}/issues/401")) == 1
    err = capsys.readouterr().err
    assert "the issue exists" in err and "coney-tools issue link 401 --parent 334" in err


def test_create_with_attachments_accepts_a_local_screenshot(tmp_path: Path) -> None:
    shot = body(screenshots="![the door](./shots/door.png)")
    gh = FakeGh()
    plain = parse("create", "--title", TITLE, "--body", write(tmp_path, shot), "--offline")
    assert issue_cli.run_create(plain, gh) == 1
    attached = parse(
        "create", "--title", TITLE, "--body", write(tmp_path, shot), "--offline", "--attach", "./shots/door.png"
    )
    assert issue_cli.run_create(attached, gh) == 0
    assert gh.writes()[0][-2:] == ["--attach", "./shots/door.png"]


def test_create_notes_a_leftover_without_a_parent(tmp_path: Path, capsys: CaptureFixture[str]) -> None:
    args = parse("create", "--title", TITLE, "--body", write(tmp_path, GOOD), "--offline", "--leftover", "--dry-run")
    assert issue_cli.run_create(args, FakeGh()) == 0
    assert "sub-issue of the issue its source pull request fixes" in capsys.readouterr().out


# --- edit and link --------------------------------------------------------------------------------------------------


def test_edit_checks_with_the_current_title_then_replaces_the_body(tmp_path: Path, capsys: CaptureFixture[str]) -> None:
    gh = FakeGh()
    assert issue_cli.run_edit(parse("edit", "340", "--body", write(tmp_path, GOOD), "--offline"), gh) == 0
    assert gh.calls[0] == ["issue", "view", "340", "--json", "title"]
    assert gh.writes()[0][:4] == ["issue", "edit", "340", "--body-file"]
    assert "--title" not in gh.writes()[0] and "edited #340" in capsys.readouterr().out


def test_edit_can_change_the_title_and_refuses_a_bad_body(tmp_path: Path, capsys: CaptureFixture[str]) -> None:
    gh = FakeGh()
    ok = parse("edit", "340", "--body", write(tmp_path, GOOD), "--title", "tools: Add a new title", "--offline")
    assert issue_cli.run_edit(ok, gh) == 0
    assert (
        gh.writes()[0][-2:] == ["--title", "tools: Add a new title"]
        and gh.calls[0][0] == "issue"
        and gh.calls[0][1] == "edit"
    )
    bad = parse("edit", "340", "--body", write(tmp_path, "no headings"), "--title", "tools: Fix a thing", "--offline")
    other = FakeGh()
    assert issue_cli.run_edit(bad, other) == 1 and other.calls == []


def test_edit_dry_run_writes_nothing(tmp_path: Path, capsys: CaptureFixture[str]) -> None:
    gh = FakeGh()
    args = parse(
        "edit", "340", "--body", write(tmp_path, GOOD), "--title", "tools: Fix a thing", "--offline", "--dry-run"
    )
    assert issue_cli.run_edit(args, gh) == 0
    assert gh.calls == [] and "dry run" in capsys.readouterr().out


def test_edit_without_a_title_and_without_gh_asks_for_one(tmp_path: Path) -> None:
    def broken(arguments: list[str]) -> str:
        raise OSError("no network")

    with pytest.raises(ConfigError, match="give --title"):
        issue_cli.run_edit(parse("edit", "340", "--body", write(tmp_path, GOOD), "--offline"), broken)


def test_link_makes_one_issue_a_sub_issue_of_another(capsys: CaptureFixture[str]) -> None:
    gh = FakeGh()
    assert issue_cli.run_link(parse("link", "401", "--parent", "334"), gh) == 0
    assert gh.calls[-1][-2:] == ["-F", "sub_issue_id=9001"]
    dry = FakeGh()
    assert issue_cli.run_link(parse("link", "401", "--parent", "334", "--dry-run"), dry) == 0
    assert dry.calls == [] and "dry run" in capsys.readouterr().out
    with pytest.raises(ConfigError, match="its own parent"):
        issue_cli.run_link(parse("link", "5", "--parent", "5"), gh)
    assert (
        issue_cli.run_link(
            parse("link", "401", "--parent", "334"), FakeGh(fail_on="api repos/{owner}/{repo}/issues/401")
        )
        == 1
    )


# --- the audit rule -------------------------------------------------------------------------------------------------


def audit(*issues: dict[str, object]) -> list[Problem]:
    """The findings of `issue.form` on made-up open issues."""
    context = audit_github.GithubContext(CONVENTIONS, snapshot(issues=list(issues)), TREE, REPO)
    return list(audit_github.issue_form_shape(context))


def test_the_audit_rule_is_registered_as_a_github_warning() -> None:
    spec = CONVENTIONS.hygiene.checks["issue.form"]
    assert spec.scope == "github" and spec.severity in ("warn", "error")


def test_the_audit_rule_passes_an_issue_in_the_forms_shape() -> None:
    assert audit(issue(1, body=GOOD)) == []


def test_the_audit_rule_flags_headings_extras_and_a_wrong_area() -> None:
    found = audit(issue(1, body=body(area="docs").replace("### Screenshots", "## Screenshots")))
    assert [problem.item for problem in found] == ["#1"]
    assert "'## Screenshots' is level 2" in found[0].message and "Area is 'docs'" in found[0].message


def test_the_audit_rule_flags_an_issue_with_no_form_headings() -> None:
    found = audit(issue(2, body="Follow-up to #346. Blocked on #346."))
    assert "no form headings" in found[0].message


def test_the_audit_rule_leaves_content_to_the_other_rules() -> None:
    # An empty Summary is issue.placeholder's finding, a plain docs path issue.links', "owner" issue.owner-word's.
    text = body(summary="The owner wants docs/research/ai-goals.md.").replace("From:", "From:")
    assert audit(issue(3, body=body(summary="TBD")), issue(4, body=text)) == []


def test_the_audit_rule_skips_closed_issues_and_runs_only_with_a_checkout() -> None:
    assert audit(issue(5, body="nothing", state="CLOSED")) == []
    context = audit_github.GithubContext(CONVENTIONS, snapshot(issues=[issue(6, body="nothing")]), TREE)
    assert list(audit_github.issue_form_shape(context)) == []


def test_the_audit_rule_picks_the_bug_form_from_the_headings() -> None:
    bug = (
        "### Coney version or commit\n\nabc\n\n### How did you start the game?\n\nStory\n\n"
        "### Steps to reproduce\n\n1. x\n\n### What happens\n\nx\n\n### How often does it happen?\n\nAlways\n\n"
        "### Screenshots\n\nScreenshots: none (x)\n\n### No game files\n\n"
        "- [x] I attached no game files: no disc images, BIOS, extracted assets, save files or game text.\n"
    )
    assert audit(issue(7, body=bug, labels=("bug", "A-core", "S-needs-triage"))) == []


# --- the issue-check workflow ---------------------------------------------------------------------------------------


def test_markdown_prints_the_comment_only_when_there_are_problems(tmp_path: Path, capsys: CaptureFixture[str]) -> None:
    assert cli.main(["issue", "check", "--title", TITLE, "--body", write(tmp_path, GOOD), "--markdown"]) == 0
    assert capsys.readouterr().out == ""
    bad = body(area="docs").replace("### Screenshots", "## Screenshots")
    assert cli.main(["issue", "check", "--title", TITLE, "--body", write(tmp_path, bad), "--markdown"]) == 1
    out = capsys.readouterr().out
    assert out.startswith(issue_cli.COMMENT_MARKER + "\n") and "found 2 problem(s)" in out
    assert "- **choices**: Area is 'docs'" in out and "- **headings**: '## Screenshots' is level 2" in out
    assert "../blob/main/docs/guides/issues.md#filing" in out


def test_a_body_written_by_the_web_form_passes() -> None:
    # The form writes `_No response_` for an optional field left empty and ticks the box with [x].
    web = (
        "### Summary\n\nThe pause menu is a stand-in.\n\n### Area\n\nA-hud\n\n### Research page\n\n_No response_\n\n"
        '### What "done" means\n\nA test.\n\n### Fix list or steps\n\n_No response_\n\n'
        "### Blocked by\n\n_No response_\n\n"
        "### Screenshots\n\n![shot](https://github.com/user-attachments/assets/1234)\n"
    )
    assert audit(issue(8, body=web)) == []
    context = audit_github.GithubContext(CONVENTIONS, snapshot(issues=[]), TREE, REPO)
    assert context.root == REPO


def test_the_workflow_runs_on_opened_and_edited_issues_and_may_only_comment() -> None:
    import yaml

    workflow = yaml.safe_load((REPO / ".github" / "workflows" / "issue-check.yml").read_text(encoding="utf-8"))
    triggers = workflow.get("on", workflow.get(True))  # YAML reads a bare `on` key as true
    assert triggers == {"issues": {"types": ["opened", "edited"]}}
    assert workflow["permissions"] == {"contents": "read", "issues": "write"}
    text = (REPO / ".github" / "workflows" / "issue-check.yml").read_text(encoding="utf-8")
    assert "coney-tools issue check" in text and "--markdown" in text and "COMMENT_MARKER" in text
    assert "--add-label" not in text and "gh issue edit" not in text
