# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools pr verify` and `pr text` through `main`: exit codes, output, the files written."""

import json
from pathlib import Path

import pytest
from git_support import git
from pr_support import PARAGRAPH, Built, make

from coney_tools.cli import main
from coney_tools.conventions import CONVENTIONS_FILE


def run(built: Built, monkeypatch: pytest.MonkeyPatch, *args: str) -> int:
    """Run `coney-tools pr ...` from the unit's checkout."""
    monkeypatch.chdir(built.root)
    return main(["pr", *args])


def test_verify_says_ok_for_a_complete_unit(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    built = make(tmp_path)
    assert run(built, monkeypatch, "verify") == 0
    out = capsys.readouterr().out
    assert f"pr verify work: change {built.diff[:12]} since origin/main" in out and "profile full" in out
    assert "ok: the log, Check, the compared functions and Review all hold for this change" in out


def add_unknown_key(built: Built) -> None:
    """A conventions file with a section the tool in these tests does not know (a branch that adds a rule)."""
    path = built.root / CONVENTIONS_FILE
    extra = "a_later_section: {x: 1}"
    path.write_text(path.read_text(encoding="utf-8") + "\n" + extra + "\n", encoding="utf-8")


def test_verify_reads_a_conventions_file_with_a_key_this_tool_does_not_know(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """The first release of a branch that adds a rule is read by the tool main already has: the file's new key is
    skipped and named once on stderr (stdout stays the result), and the run goes on to its own checks."""
    built = make(tmp_path)
    add_unknown_key(built)
    assert run(built, monkeypatch, "verify") == 1
    captured = capsys.readouterr()
    assert "unknown key" not in captured.out + captured.err and "refused: 1 problem" in captured.out
    assert "worktree: uncommitted changes: .github/conventions.yml" in captured.out
    assert captured.err.count("ignoring a_later_section") == 1 and "note: " in captured.err
    assert "ignoring" not in captured.out


def test_text_reads_a_conventions_file_with_a_key_this_tool_does_not_know(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """`pr text` skips the key the same way and names it once, whatever it then says about the description."""
    built = make(tmp_path)
    add_unknown_key(built)
    run(built, monkeypatch, "text", "--dry-run")
    captured = capsys.readouterr()
    assert "unknown key" not in captured.out + captured.err
    assert captured.err.count("ignoring a_later_section") == 1 and "ignoring" not in captured.out


def test_verify_lists_every_refusal_with_what_to_redo_and_exits_1(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    built = make(tmp_path, skip=frozenset({"checked", "reviewed"}))
    assert run(built, monkeypatch, "verify") == 1
    out = capsys.readouterr().out
    assert "refused: 2 problems" in out
    assert "  checked: no passing Check; run task check\n      redo: task check" in out
    assert "  reviewed: not reviewed\n      redo: task review" in out


def test_verify_json_has_the_facts_and_the_refusals(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    built = make(tmp_path, skip=frozenset({"reviewed"}))
    assert run(built, monkeypatch, "verify", "--json") == 1
    data = json.loads(capsys.readouterr().out)
    assert data["ok"] is False and data["task"] == "work" and data["profile"] == "full"
    assert data["diff_sha256"] == built.diff and data["tip"] == built.tip
    assert data["refusals"] == [{"stage": "reviewed", "text": "reviewed: not reviewed", "redo": "task review"}]


@pytest.mark.parametrize("text", [None, "# tools: Add the widget\n\n## Testing\n\nRan it.\n"])
def test_verify_refuses_when_the_text_review_covered_cannot_be_read(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], text: str | None
) -> None:
    built = make(tmp_path)
    path = built.unit.dir / "PR.md"
    if text is None:
        path.unlink()
    else:
        path.write_text(text, encoding="utf-8")
    assert run(built, monkeypatch, "verify") == 1
    out = capsys.readouterr().out
    assert f"  reviewed: the text Review covered cannot be read: {path}: " in out
    assert out.count(str(path)) == 1, out
    assert "redo: engineer, then task review --text" in out and "ok:" not in out


def test_verify_changes_nothing(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    built = make(tmp_path, skip=frozenset({"checked"}))
    before = (git(built.root, "status", "--porcelain"), sorted(p.name for p in built.unit.dir.rglob("*")))
    stamps_before = (built.unit.stamps / "0001.jsonl").read_bytes()
    run(built, monkeypatch, "verify")
    assert (git(built.root, "status", "--porcelain"), sorted(p.name for p in built.unit.dir.rglob("*"))) == before
    assert (built.unit.stamps / "0001.jsonl").read_bytes() == stamps_before


def test_verify_without_a_task_file_exits_2(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    built = make(tmp_path)
    (built.unit.dir / "TASK.md").unlink()
    assert run(built, monkeypatch, "verify") == 2
    assert "no TASK.md" in capsys.readouterr().err


def test_verify_with_an_unknown_base_exits_2(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    built = make(tmp_path)
    assert run(built, monkeypatch, "verify", "--remote", "elsewhere") == 2
    assert "elsewhere/main is not known here; run `git fetch elsewhere main` first" in capsys.readouterr().err


def test_a_fix_task_continues_its_parents_unit(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    built = make(tmp_path)
    fix = built.unit.dir.parent / "work-fix"
    fix.mkdir()
    task = (built.unit.dir / "TASK.md").read_text(encoding="utf-8")
    (fix / "TASK.md").write_text(
        task.replace("task: work", "task: work-fix\nparent: work").replace("kind: feature", "kind: fix"),
        encoding="utf-8",
    )
    assert run(built, monkeypatch, "verify", "work-fix") == 0
    assert "pr verify work:" in capsys.readouterr().out


def test_text_writes_the_title_and_the_description_that_the_repo_checks_accept(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    built = make(tmp_path)
    assert run(built, monkeypatch, "text") == 0
    out = capsys.readouterr().out
    title = (built.unit.dir / "PR-TITLE.txt").read_text(encoding="utf-8")
    body = (built.unit.dir / "PR-BODY.md").read_text(encoding="utf-8")
    assert title == "gameplay: Fix the widget ready frame\n" and f"pr text work: ok: {title.strip()}" in out
    assert PARAGRAPH in body and "\r" not in body and body.endswith("\n")
    assert main(["repo", "check-title", str(built.unit.dir / "PR-TITLE.txt")]) == 0
    assert main(["repo", "check-body", "--body", str(built.unit.dir / "PR-BODY.md"), "--title", title.strip()]) == 0


def test_text_writes_nothing_when_a_check_fails_or_on_a_dry_run(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    built = make(tmp_path)
    assert run(built, monkeypatch, "text", "--dry-run") == 0
    assert not (built.unit.dir / "PR-BODY.md").exists() and "dry run" in capsys.readouterr().out
    path = built.unit.dir / "PR.md"
    path.write_text(path.read_text(encoding="utf-8").replace("gameplay: Fix", "gameplay: fixed"), encoding="utf-8")
    assert run(built, monkeypatch, "text") == 1
    out = capsys.readouterr().out
    assert "refused:" in out and "title:" in out and "redo: engineer" in out
    assert not (built.unit.dir / "PR-BODY.md").exists() and not (built.unit.dir / "PR-TITLE.txt").exists()


def test_text_json_has_the_hashes_a_release_records(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    built = make(tmp_path)
    assert run(built, monkeypatch, "text", "--json") == 0
    data = json.loads(capsys.readouterr().out)
    hashes = built.text_hashes()
    assert data["ok"] is True and data["title"] == "gameplay: Fix the widget ready frame"
    assert (data["title_sha256"], data["paragraph_sha256"], data["images_sha256"]) == (
        hashes.title_sha256,
        hashes.paragraph_sha256,
        hashes.images_sha256,
    )
    assert len(data["facts_sha256"]) == 64 and len(data["written"]) == 2


def test_text_reads_another_file_when_told_to(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    built = make(tmp_path)
    other = tmp_path / "elsewhere" / "PR.md"
    other.parent.mkdir()
    other.write_text(
        (built.unit.dir / "PR.md").read_text(encoding="utf-8").replace("Fix the", "Fix a"), encoding="utf-8"
    )
    assert run(built, monkeypatch, "text", "--text", str(other)) == 0
    assert (other.parent / "PR-TITLE.txt").read_text(encoding="utf-8").startswith("gameplay: Fix a widget")
    assert "wrote" in capsys.readouterr().out
