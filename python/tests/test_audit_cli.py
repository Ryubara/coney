# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools repo audit`: the scopes, the options, the exit codes and the two report formats."""

import json
import shutil
from pathlib import Path
from typing import Any

import pytest
from audit_support import CONVENTIONS, FakeTree, issue, pull, raw_snapshot, task_body
from conventions_support import REPO
from test_audit_files import checkout
from test_workspace_clean import Space, Usage

from coney_tools import audit_github, log_scan
from coney_tools.cli import main


@pytest.fixture(autouse=True)
def in_the_repository(monkeypatch: pytest.MonkeyPatch) -> None:
    """Run every command from this checkout."""
    monkeypatch.chdir(REPO)


def snapshot_file(tmp_path: Path, **kwargs: Any) -> Path:
    """Write the raw snapshot of `raw_snapshot(**kwargs)` to a file and return it."""
    path = tmp_path / "snapshot.json"
    path.write_text(json.dumps(raw_snapshot(**kwargs)), encoding="utf-8")
    return path


def run(capsys: pytest.CaptureFixture[str], *arguments: str) -> tuple[int, str, str]:
    """Run `coney-tools repo audit ...`; the exit code, standard output and standard error."""
    code = main(["repo", "audit", *arguments])
    captured = capsys.readouterr()
    return code, captured.out, captured.err


def no_tree(monkeypatch: pytest.MonkeyPatch) -> None:
    """Pretend this checkout has no origin/main."""
    monkeypatch.setattr(audit_github, "git_tree", lambda root, ref: None)


# --- --files --------------------------------------------------------------------------------------------------------


def test_files_is_the_default_scope_and_this_checkout_is_clean(capsys: pytest.CaptureFixture[str]) -> None:
    code, out, _ = run(capsys)
    assert code == 0 and out.strip() == "audit files: 11 checks, 0 errors, 0 warnings"


def test_files_json_lists_the_checks_that_ran(capsys: pytest.CaptureFixture[str]) -> None:
    code, out, _ = run(capsys, "--files", "--json")
    data = json.loads(out)
    assert (
        code == 0 and data["findings"] == [] and data["errors"] == 0 and next(iter(data["checks"])) == "files.pointer"
    )
    assert all(count == 0 for count in data["checks"].values()) and len(data["checks"]) == 11


def test_a_finding_in_the_checkout_fails_the_run(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    root = checkout(tmp_path)
    (root / "GEMINI.md").write_text("no import here\n", encoding="utf-8")
    monkeypatch.chdir(root)
    code, out, _ = run(capsys, "--files")
    assert code == 1 and "files.pointer (error, fix: skill): 1" in out and "GEMINI.md" in out


def test_only_limits_the_run_to_a_check_or_a_group(capsys: pytest.CaptureFixture[str]) -> None:
    _, out, _ = run(capsys, "--only", "files.address", "--json")
    assert list(json.loads(out)["checks"]) == ["files.address"]
    _, out, _ = run(capsys, "--only", "files.pointer", "--only", "files.exist", "--json")
    assert list(json.loads(out)["checks"]) == ["files.pointer", "files.exist"]


def test_an_unknown_check_is_a_usage_error(capsys: pytest.CaptureFixture[str]) -> None:
    code, _, err = run(capsys, "--only", "files.nothing")
    assert code == 2 and "no such check" in err


def test_list_prints_the_registry(capsys: pytest.CaptureFixture[str]) -> None:
    code, out, _ = run(capsys, "--list")
    lines = out.splitlines()
    assert (
        code == 0
        and len(lines) == len(CONVENTIONS.hygiene.checks) == 44
        and lines[0].startswith("files.pointer")
        and "fix: maintainer" in out
    )


# --- --github -------------------------------------------------------------------------------------------------------


def test_github_from_a_snapshot_reports_errors_and_fails(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    no_tree(monkeypatch)
    path = snapshot_file(tmp_path, issues=[issue(200, body=task_body(summary="x")), issue(5)])
    code, out, _ = run(capsys, "--github", "--snapshot", str(path), "--only", "issue.placeholder")
    assert code == 1 and "issue.placeholder (error, fix: skill): 1" in out and "#200: 'Summary' is 'x'" in out


def test_github_json_carries_what_the_skill_needs(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    no_tree(monkeypatch)
    path = snapshot_file(tmp_path, issues=[issue(200, body=task_body(summary="x"))])
    code, out, _ = run(capsys, "--github", "--snapshot", str(path), "--only", "issue.placeholder", "--json")
    data = json.loads(out)
    (finding,) = data["findings"]
    assert code == 1 and data["errors"] == 1 and data["scopes"] == ["github"]
    assert finding["check"] == "issue.placeholder" and finding["item"] == "#200" and finding["fix"] == "skill"
    assert finding["keys"] == ["issues.placeholders"] and finding["url"].endswith("/issues/200")


def test_warnings_alone_pass_unless_strict(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    no_tree(monkeypatch)
    stale = issue(3, labels=("task", "A-tools", "S-needs-triage"), created=30)
    path = snapshot_file(tmp_path, issues=[stale])
    code, out, _ = run(capsys, "--github", "--snapshot", str(path), "--only", "issue.triage-age")
    assert code == 0 and "issue.triage-age (warn, fix: skill): 1" in out
    code, _, _ = run(capsys, "--github", "--snapshot", str(path), "--only", "issue.triage-age", "--strict")
    assert code == 1


def test_a_clean_snapshot_passes(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    no_tree(monkeypatch)
    path = snapshot_file(tmp_path, issues=[issue(1)], pulls=[pull(2)])
    code, out, _ = run(capsys, "--github", "--snapshot", str(path))
    assert code == 0, out
    assert "issue.links did not run" in out and "gh.ruleset" not in out


def test_the_link_check_runs_when_main_is_there(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr(audit_github, "git_tree", lambda root, ref: FakeTree({"docs/guides/a.md": frozenset()}))
    path = snapshot_file(tmp_path, issues=[issue(1, body=task_body(summary="[a](../blob/main/docs/guides/gone.md)"))])
    code, out, _ = run(capsys, "--github", "--snapshot", str(path), "--only", "issue.links")
    assert code == 0 and "which is not on main" in out and "did not run" not in out


def test_a_snapshot_is_written_and_read_back(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    no_tree(monkeypatch)
    raw = raw_snapshot(issues=[issue(1)])
    monkeypatch.setattr(audit_github, "fetch_raw", lambda: raw)
    saved = tmp_path / "saved.json"
    code, _, _ = run(capsys, "--github", "--save-snapshot", str(saved), "--only", "issue.labels")
    assert code == 0 and json.loads(saved.read_text(encoding="utf-8")) == raw


def test_gh_that_cannot_read_is_a_one_line_error(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    def fail() -> dict[str, Any]:
        raise OSError("gh could not run: not found")

    monkeypatch.setattr(audit_github, "fetch_raw", fail)
    code, out, err = run(capsys, "--github")
    assert code == 2 and out == "" and "cannot read GitHub with gh" in err and "Traceback" not in err


def test_a_snapshot_that_is_not_json_is_an_error(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    path = tmp_path / "bad.json"
    path.write_text("not json", encoding="utf-8")
    code, _, err = run(capsys, "--github", "--snapshot", str(path))
    assert code == 2 and "cannot be read as a snapshot" in err


@pytest.mark.parametrize(
    ("option", "scope"), [("--snapshot", "github"), ("--save-snapshot", "github"), ("--workspace", "local")]
)
def test_an_option_of_a_scope_needs_that_scope(
    option: str, scope: str, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    code, _, err = run(capsys, option, str(tmp_path / "x"))
    assert code == 2 and f"needs --{scope}" in err


# --- --local and several scopes -------------------------------------------------------------------------------------


def test_local_reports_the_workspace(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    space = Space(tmp_path)
    (space.checkout / ".github").mkdir()
    shutil.copyfile(REPO / ".github" / "conventions.yml", space.checkout / ".github" / "conventions.yml")
    space.worktree("done")
    monkeypatch.setattr(shutil, "disk_usage", lambda path: Usage(100 * 1024**3, 0, 50 * 1024**3))
    monkeypatch.setattr("coney_tools.audit_local.gh_merged_heads", lambda checkout: lambda branch: None)
    monkeypatch.chdir(space.checkout)
    code, out, _ = run(capsys, "--local")
    assert code == 0 and "ws.worktrees (warn, fix: auto): 1" in out and "worktree done" in out
    assert "audit local: 3 checks, 0 errors, 1 warnings" in out


def test_scopes_run_together_in_order(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    no_tree(monkeypatch)
    path = snapshot_file(tmp_path, issues=[issue(1)])
    code, out, _ = run(capsys, "--github", "--files", "--snapshot", str(path), "--json")
    data = json.loads(out)
    assert code == 0 and data["scopes"] == ["files", "github"]
    assert next(iter(data["checks"])) == "files.pointer" and "issue.labels" in data["checks"]


# --- --attachments --------------------------------------------------------------------------------------------------


def test_without_attachments_the_attachment_checks_say_they_did_not_run(
    tmp_path: Path, capsys: pytest.CaptureFixture[str], monkeypatch: pytest.MonkeyPatch
) -> None:
    no_tree(monkeypatch)
    path = snapshot_file(tmp_path, issues=[issue(5)])
    code, out, _ = run(capsys, "--github", "--snapshot", str(path), "--only", "issue.attachment-personal")
    assert code == 0 and "attachments not downloaded" in out and "0 checks" in out


def test_attachments_scan_the_open_issues(
    tmp_path: Path, capsys: pytest.CaptureFixture[str], monkeypatch: pytest.MonkeyPatch
) -> None:
    no_tree(monkeypatch)
    link = "https://github.com/user-attachments/files/9/run.log"
    monkeypatch.setattr(log_scan, "download", lambda url, limit, timeout: b"mail me: someone@example.com\n")
    path = snapshot_file(tmp_path, issues=[issue(5, body=task_body(extra=f"{link}\n\n"))])
    code, out, _ = run(
        capsys, "--github", "--snapshot", str(path), "--attachments", "--only", "issue.attachment-personal"
    )
    assert code == 0 and "#5: run.log shows an email address on line 1" in out and "someone" not in out


def test_attachments_need_github(capsys: pytest.CaptureFixture[str]) -> None:
    code, _, err = run(capsys, "--attachments")
    assert code == 2 and "--attachments needs --github" in err
