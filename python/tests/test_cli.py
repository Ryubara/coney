# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.cli: exit codes and output of each command."""

import io
import os
import subprocess
import sys
from pathlib import Path

import pytest
from conventions_support import changed, write_checkout
from git_support import make_branch, message

from coney_tools.cli import main, use_utf8_output


def test_version(capsys: pytest.CaptureFixture[str]) -> None:
    """`--version` prints the package version and exits 0."""
    with pytest.raises(SystemExit) as exit_info:
        main(["--version"])
    assert exit_info.value.code == 0
    assert "0.1.0" in capsys.readouterr().out


def test_config_show_lists_the_paths(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    (tmp_path / "coney.local.toml").write_text('game_dir = "game"\n', encoding="utf-8")
    (tmp_path / "game").mkdir()
    monkeypatch.chdir(tmp_path)
    assert main(["config", "show"]) == 0
    out = capsys.readouterr().out
    assert "game_dir" in out and "(found)" in out
    assert "jdk_home = unset" in out


def test_config_error_is_one_line_and_exit_2(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    monkeypatch.chdir(tmp_path)
    assert main(["config", "show"]) == 2
    err = capsys.readouterr().err
    assert "coney.local.toml" in err and "Traceback" not in err


def test_repo_check_fails_with_exit_1_when_a_pointer_file_is_missing(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(write_checkout(tmp_path))
    assert main(["repo", "check"]) == 1
    assert "CLAUDE.md: missing" in capsys.readouterr().out


def test_repo_check_passes_with_the_conventions_and_the_pointer_files(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(write_checkout(tmp_path))
    for name in ("CLAUDE.md", "GEMINI.md"):
        (tmp_path / name).write_text("@AGENTS.md\n", encoding="utf-8")
    assert main(["repo", "check"]) == 0
    assert capsys.readouterr().out.splitlines() == ["conventions: ok", "pointer files: ok"]


def test_repo_check_without_the_conventions_file_exits_2(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    monkeypatch.chdir(tmp_path)
    assert main(["repo", "check"]) == 2
    err = capsys.readouterr().err
    assert "conventions.yml" in err and "Traceback" not in err


def test_repo_check_with_a_mistake_in_the_conventions_names_the_key(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(write_checkout(tmp_path, changed(("merge.auto_merge", "yes"))))
    assert main(["repo", "check"]) == 2
    assert "merge.auto_merge: expected true or false" in capsys.readouterr().err


def title_checkout(tmp_path: Path) -> Path:
    """A made-up checkout whose title rules allow only the area `docs` and the verb `Add`."""
    data = changed(("commits.title.areas", {"docs": "documentation"}), ("commits.title.verbs", ["Add"]))
    return write_checkout(tmp_path, data)


def test_check_title_passes_with_exit_0(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(title_checkout(tmp_path))
    (tmp_path / "msg").write_text("# a comment\n\ndocs: Add the guide\n\nBody.\n", encoding="utf-8")
    assert main(["repo", "check-title", "msg"]) == 0
    assert capsys.readouterr().out.strip() == "title: ok"


def test_check_title_refuses_with_exit_1_and_one_line(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(title_checkout(tmp_path))
    (tmp_path / "msg").write_text("web: Added the guide.\n", encoding="utf-8")
    assert main(["repo", "check-title", "msg"]) == 1
    out = capsys.readouterr().out.strip().splitlines()
    assert len(out) == 1
    assert out[0].startswith("title refused: 'web: Added the guide.'") and "unknown area 'web'" in out[0]
    assert "AGENTS.md" in out[0]


def test_check_title_reads_stdin(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.chdir(title_checkout(tmp_path))
    monkeypatch.setattr("sys.stdin", io.StringIO("docs: Add the guide\n"))
    assert main(["repo", "check-title", "-"]) == 0


def test_check_title_with_unusable_rules_exits_2(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(title_checkout(tmp_path))
    (tmp_path / ".github" / "conventions.yml").write_text("{}\n", encoding="utf-8")
    (tmp_path / "msg").write_text("docs: Add the guide\n", encoding="utf-8")
    assert main(["repo", "check-title", "msg"]) == 2
    err = capsys.readouterr().err
    assert "conventions.yml" in err and "Traceback" not in err


def test_check_title_with_an_unreadable_file_exits_2(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(title_checkout(tmp_path))
    assert main(["repo", "check-title", "missing.txt"]) == 2
    assert "missing.txt" in capsys.readouterr().err


def test_check_title_of_an_empty_message_is_refused(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.chdir(title_checkout(tmp_path))
    (tmp_path / "msg").write_text("\n# only a comment\n", encoding="utf-8")
    assert main(["repo", "check-title", "msg"]) == 1


def test_check_docs_passes_when_the_docs_change_with_the_code(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("sys.stdin", io.StringIO("src/core/a.cpp\nresearch/references/cars.yaml\n"))
    assert main(["repo", "check-docs"]) == 0


def test_check_docs_passes_for_changes_that_are_not_code(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("sys.stdin", io.StringIO(".github/workflows/pr.yml\npython/tests/test_cli.py\ntests/a.cpp\n"))
    assert main(["repo", "check-docs"]) == 0


def test_check_docs_refuses_code_without_docs(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr("sys.stdin", io.StringIO("python/src/coney_tools/wad.py\nsrc/core/a.cpp\n"))
    assert main(["repo", "check-docs"]) == 1
    out = capsys.readouterr().out.strip().splitlines()
    assert len(out) == 1 and out[0].startswith(
        "docs refused: changes code (python/src/coney_tools/wad.py, src/core/a.cpp)"
    )
    assert "Docs: none" in out[0]


def test_check_docs_accepts_a_docs_none_line_in_the_body(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    (tmp_path / "body.md").write_text(
        "## What and why\n\nA rename.\n\ndocs: NONE - no behaviour change\n", encoding="utf-8"
    )
    monkeypatch.setattr("sys.stdin", io.StringIO("src\\core\\a.cpp\n"))
    assert main(["repo", "check-docs", "--body", str(tmp_path / "body.md")]) == 0


def test_check_docs_ignores_a_docs_none_inside_a_sentence(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    (tmp_path / "body.md").write_text("I will say Docs: none later.\n", encoding="utf-8")
    monkeypatch.setattr("sys.stdin", io.StringIO("src/core/a.cpp\n"))
    assert main(["repo", "check-docs", "--body", str(tmp_path / "body.md")]) == 1


def test_check_docs_with_an_unreadable_body_exits_2(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr("sys.stdin", io.StringIO("src/core/a.cpp\n"))
    assert main(["repo", "check-docs", "--body", str(tmp_path / "missing.md")]) == 2
    assert "cannot be read" in capsys.readouterr().err


def body_checkout(tmp_path: Path) -> Path:
    """A made-up checkout whose description rules ask for a Summary and an AI use section."""
    data = changed(
        ("pull_requests.sections", ["Summary", "Checklist", "Screenshots", "AI use"]),
        ("pull_requests.filled", ["Summary"]),
        ("pull_requests.ai_use.written_by.label", "By:"),
        ("pull_requests.ai_use.written_by.choices", ["a person", "an AI"]),
    )
    return write_checkout(tmp_path, data)


GOOD_BODY = (
    "## Summary\n\nAdds a check.\n\n## Checklist\n\nDone:\n\n- [x] The check\n\n"
    "## Screenshots\n\nScreenshots: none (no change)\n\n"
    "## AI use\n\n- By: an AI\n- Model: Claude Sonnet 5.5\n"
)


def test_check_body_passes_with_exit_0(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(body_checkout(tmp_path))
    (tmp_path / "body.md").write_text(GOOD_BODY, encoding="utf-8")
    assert main(["repo", "check-body", "--body", "body.md"]) == 0
    assert capsys.readouterr().out.strip() == "body: ok"


def test_check_body_reads_stdin_when_given_no_file(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.chdir(body_checkout(tmp_path))
    monkeypatch.setattr("sys.stdin", io.StringIO(GOOD_BODY))
    assert main(["repo", "check-body"]) == 0


def test_check_body_reads_stdin_for_a_dash(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.chdir(body_checkout(tmp_path))
    monkeypatch.setattr("sys.stdin", io.StringIO(GOOD_BODY))
    assert main(["repo", "check-body", "--body", "-"]) == 0


def test_check_body_refuses_with_exit_1_and_one_line(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(body_checkout(tmp_path))
    monkeypatch.setattr("sys.stdin", io.StringIO("## Summary\n\n<!-- placeholder -->\n"))
    assert main(["repo", "check-body"]) == 1
    out = capsys.readouterr().out.strip().splitlines()
    assert len(out) == 1 and out[0].startswith("body refused: ")
    assert "'## Summary' is empty" in out[0] and "'## AI use'" in out[0]


def test_check_body_refuses_an_unchecked_checklist_item(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(body_checkout(tmp_path))
    monkeypatch.setattr("sys.stdin", io.StringIO(GOOD_BODY.replace("- [x] The check", "- [ ] Later (#12)")))
    assert main(["repo", "check-body"]) == 1
    out = capsys.readouterr().out
    assert "finish it or take it out" in out and "Later" in out


def test_check_body_with_an_unreadable_file_exits_2(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(body_checkout(tmp_path))
    assert main(["repo", "check-body", "--body", "missing.md"]) == 2
    assert "cannot be read" in capsys.readouterr().err


def test_check_body_with_unusable_rules_exits_2(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr("sys.stdin", io.StringIO(GOOD_BODY))
    assert main(["repo", "check-body"]) == 2
    assert "conventions.yml" in capsys.readouterr().err


def branch_with_opus(root: Path) -> Path:
    """A synthetic branch (under `root`) whose one commit after `main~1` credits Claude Opus 5.5."""
    branch = root / "branch"
    branch.mkdir()
    return make_branch(
        branch, [message("tools: Add a thing", "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>")]
    )


def test_check_body_with_a_worktree_refuses_a_model_the_line_does_not_name(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(body_checkout(tmp_path))
    branch = branch_with_opus(tmp_path)
    (tmp_path / "body.md").write_text(GOOD_BODY, encoding="utf-8")
    argv = ["repo", "check-body", "--body", "body.md", "--worktree", str(branch), "--base", "main~1"]
    assert main(argv) == 1
    out = capsys.readouterr().out
    assert "Claude Opus 5.5" in out and "Co-Authored-By" in out
    (tmp_path / "body.md").write_text(GOOD_BODY.replace("Sonnet", "Opus"), encoding="utf-8")
    assert main(argv) == 0


def test_check_body_without_a_worktree_does_not_read_the_commits(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    # What CI's `body` job runs: the description alone.
    monkeypatch.chdir(body_checkout(tmp_path))
    monkeypatch.setattr("sys.stdin", io.StringIO(GOOD_BODY))
    assert main(["repo", "check-body"]) == 0


def test_check_body_with_a_worktree_git_cannot_read_exits_2(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(body_checkout(tmp_path))
    monkeypatch.setattr("sys.stdin", io.StringIO(GOOD_BODY))
    assert main(["repo", "check-body", "--worktree", str(tmp_path / "missing")]) == 2
    assert "origin/main" in capsys.readouterr().err


def test_output_streams_are_switched_to_utf8_whatever_the_console_code_page() -> None:
    """A redirected stdout on Windows is cp1252 and cannot write an arrow; the tool writes UTF-8 instead."""
    raw = io.BytesIO()
    stream = io.TextIOWrapper(raw, encoding="cp1252")
    use_utf8_output([stream, io.StringIO()])  # a stream without `reconfigure` is left alone
    stream.write("a → b")
    stream.flush()
    assert raw.getvalue() == "a → b".encode()


def test_a_command_run_with_a_cp1252_pipe_prints_unicode(tmp_path: Path) -> None:
    """The real entry point, with stdout a pipe in cp1252 (as under `repo audit --json > file` on Windows)."""
    code = "import sys; from coney_tools.cli import use_utf8_output; use_utf8_output(); print('\u2192')"
    env = {**os.environ, "PYTHONIOENCODING": "cp1252", "PYTHONUTF8": "0"}
    done = subprocess.run([sys.executable, "-c", code], env=env, capture_output=True, check=False)
    assert done.returncode == 0 and done.stdout.strip() == "→".encode()
