# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools standards ...` end to end on a made-up checkout, and the hook in `docs check`."""

from __future__ import annotations

from pathlib import Path

import pytest
import yaml
from git_support import git
from test_standards_check import BAD_SOURCE, BAD_TEST, GOOD_SOURCE, branch, build, write_legacy

from coney_tools import docs_check
from coney_tools.cli import main
from coney_tools.conventions import load_conventions


def checkout(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, files: dict[str, str], **kwargs: str) -> Path:
    """A made-up checkout the commands find from the working directory."""
    root = build(tmp_path, files, **kwargs)
    monkeypatch.chdir(root)
    return root


def test_scan_counts_the_marks_of_every_kind(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    checkout(
        tmp_path,
        monkeypatch,
        {"src/ai/brain.cpp": BAD_SOURCE, "src/ai/ok.cpp": GOOD_SOURCE, "tests/a_test.cpp": BAD_TEST},
    )
    assert main(["standards", "scan"]) == 0
    out = capsys.readouterr().out
    assert "ported functions: 1 (legacy 1, compared 0, no status 0)" in out
    assert "source files: 1 of 2 below the standard (comment 1, tag 1)" in out
    assert "test files: 1 of 1 below the standard (citation 1)" in out
    assert "research pages: 0 listed" in out


def test_scan_files_lists_each_file_with_its_findings(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    checkout(tmp_path, monkeypatch, {"src/ai/brain.cpp": BAD_SOURCE})
    assert main(["standards", "scan", "--files"]) == 0
    out = capsys.readouterr().out
    assert "src/ai/brain.cpp:9: tag:" in out and "src/ai/brain.cpp:9: comment:" in out


def test_init_prune_and_mark_keep_the_files_in_step(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    root = checkout(tmp_path, monkeypatch, {"src/ai/brain.cpp": BAD_SOURCE}, status="")
    assert main(["standards", "init"]) == 0
    assert '["src/ai/brain.cpp"]' in (root / "research/legacy-files.toml").read_text(encoding="utf-8")
    assert main(["standards", "init"]) == 2  # it only shrinks
    (root / "src/ai/brain.cpp").write_text(GOOD_SOURCE, encoding="utf-8")
    assert main(["standards", "prune"]) == 0
    assert '["src/' not in (root / "research/legacy-files.toml").read_text(encoding="utf-8")
    assert main(["standards", "mark"]) == 0
    assert 'status = "legacy"' in (root / "research/functions/ai.toml").read_text(encoding="utf-8")
    assert "1 function marked legacy" in capsys.readouterr().out


def enforce(root: Path, on: bool = True) -> None:
    """Set `standards.enforce` in the made-up checkout's conventions (the real file enforces since the cutover)."""
    conventions = root / ".github/conventions.yml"
    data = yaml.safe_load(conventions.read_text(encoding="utf-8"))
    data["standards"]["enforce"] = on
    conventions.write_text(yaml.safe_dump(data), encoding="utf-8")


def test_check_prints_the_problems_as_warnings_until_the_standard_is_enforced(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    root = checkout(tmp_path, monkeypatch, {"src/ai/ok.cpp": GOOD_SOURCE})
    write_legacy(root, {"src/ai/ok.cpp": {"tag": 1}})
    enforce(root, on=False)
    assert main(["standards", "check", "--base", "main"]) == 0
    out = capsys.readouterr().out
    assert "research/legacy-files.toml:1: warning: legacy: src/ai/ok.cpp now passes 'tag'" in out
    assert "run `coney-tools standards prune`" in out
    assert "standards check: 0 errors, 1 warning" in out
    enforce(root)
    assert main(["standards", "check", "--base", "main"]) == 1
    out = capsys.readouterr().out
    assert "research/legacy-files.toml:1: legacy: src/ai/ok.cpp now passes 'tag'" in out
    assert "standards check: 1 error" in out


def test_check_warns_about_a_touched_file_until_the_standard_is_enforced(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    root = checkout(tmp_path, monkeypatch, {"src/ai/brain.cpp": BAD_SOURCE})
    write_legacy(root, {"src/ai/brain.cpp": {"comment": 1, "tag": 1}})
    branch(root, {"src/ai/brain.cpp": BAD_SOURCE + "// x\n"})
    enforce(root, on=False)
    assert main(["standards", "check", "--base", "main"]) == 0
    assert "src/ai/brain.cpp:9: warning: comment:" in capsys.readouterr().out
    enforce(root)
    assert main(["standards", "check", "--base", "main"]) == 1
    assert "src/ai/brain.cpp:9: comment:" in capsys.readouterr().out


def test_docs_check_reports_the_marks_when_the_change_can_move_them(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    root = checkout(tmp_path, monkeypatch, {"src/ai/brain.cpp": BAD_SOURCE})
    write_legacy(root, {"src/ai/brain.cpp": {"comment": 1, "tag": 1}})
    branch(root, {"src/ai/brain.cpp": GOOD_SOURCE})
    enforce(root, on=False)
    result = docs_check.check_changed(root, "main", docs_check.context_for(root))
    legacy = load_conventions(root).standards.legacy_file
    # the file now passes both rules it is listed for: the docs job asks for them to be removed, as a warning only
    # until the standard is enforced
    assert any("now passes 'tag'" in f.message for f in result.findings.get(legacy, []))
    assert legacy not in result.failed
    enforce(root)
    assert legacy in docs_check.check_changed(root, "main", docs_check.context_for(root)).failed


def test_the_local_docs_step_runs_the_standards_for_a_source_only_change(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    from coney_tools import check_cli

    root = checkout(tmp_path, monkeypatch, {"src/ai/other.cpp": BAD_SOURCE})
    write_legacy(root, {"src/ai/other.cpp": {"comment": 1, "tag": 1}})
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "list")
    branch(root, {"src/ai/other.cpp": BAD_SOURCE + "// x\n"})
    enforce(root, on=False)
    # nothing in the docs changed: the standards run (a warning here), the site build does not
    step = check_cli._step_docs(root, "main", ["src/ai/other.cpp"])
    assert step.ok and not step.skipped and "mkdocs build skipped" in step.detail
    assert any("warning: comment:" in line for line in step.output)
    assert check_cli._step_docs(root, "main", ["tools/other.txt"]).skipped


def test_on_the_real_conventions_check_fails_a_touched_file_and_passes_it_once_fixed(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    # No `enforce()` here: the made-up checkout holds the real conventions, enforced since the cutover.
    root = checkout(tmp_path, monkeypatch, {"src/ai/brain.cpp": BAD_SOURCE}, status='status = "compared"\n')
    write_legacy(root, {"src/ai/brain.cpp": {"comment": 1, "tag": 1}})
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "list")
    branch(root, {"src/ai/brain.cpp": BAD_SOURCE + "// x\n"})
    assert main(["standards", "check", "--base", "main"]) == 1
    out = capsys.readouterr().out
    assert "src/ai/brain.cpp:9: comment:" in out and "standards check: 2 errors, 0 warnings" in out
    (root / "src/ai/brain.cpp").write_text(GOOD_SOURCE, encoding="utf-8")
    write_legacy(root, {})
    assert main(["standards", "check", "--base", "main"]) == 0
    assert "standards check: 0 errors, 0 warnings" in capsys.readouterr().out
