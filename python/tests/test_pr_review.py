# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.pr_review: the description hash, the review stamp, `repo check-review` and `repo review`.

Synthetic fixtures only: a made-up description file, made-up image bytes, a fake branch tip and a fake `claude`
launcher, all under pytest's temporary folder.
"""

import hashlib
import json
from pathlib import Path

import pytest
from conventions_support import REPO
from git_support import make_branch, message

from coney_tools import pr_review
from coney_tools.cli import main
from coney_tools.conventions import Trailers, load_conventions
from coney_tools.pr_review import (
    agent_definition,
    check_review,
    description_sha256,
    local_images,
    make_stamp,
    review_command,
    run_review,
)

#: The real reader of a branch's trailer models, which the fixture below replaces for all but the tests that use it.
REAL_BRANCH_MODELS = pr_review.branch_trailer_models

CONVENTIONS = load_conventions(REPO)
RULES = CONVENTIONS.pull_requests.review
TIP = "a" * 40
OTHER_TIP = "b" * 40

BODY = """tools: Add a thing

## Summary

Adds a thing.

## Checklist

Done:

- [x] The thing is added

## Testing

- pytest: 3 passed

## Screenshots

- **Coney before:** ![before](./shots/before.png)
- **Coney after:** <img src="./shots/after.png" alt="after">
- Uploaded already: ![old](https://github.com/user-attachments/assets/1234)

## AI use

- **Commits written by:** an AI
- **Model:** Claude Sonnet 5.5
"""


@pytest.fixture(autouse=True)
def no_trailers(monkeypatch: pytest.MonkeyPatch) -> None:
    """The fake worktrees here are not git repositories: unless a test says otherwise, the branch has no trailers."""
    monkeypatch.setattr(pr_review, "branch_trailer_models", lambda _worktree, _base, _trailers: [])


def head(_worktree: Path) -> str:
    """A fake branch tip reader: the worktree is always at `TIP`."""
    return TIP


def write_pr(folder: Path, body: str = BODY, shots: dict[str, bytes] | None = None) -> Path:
    """Write `body` as PR.md in `folder` and the shots it names (default: two different images); returns PR.md."""
    (folder / "shots").mkdir(parents=True, exist_ok=True)
    for name, data in (shots if shots is not None else {"before.png": b"before", "after.png": b"after"}).items():
        (folder / "shots" / name).write_bytes(data)
    pr = folder / "PR.md"
    pr.write_text(body, encoding="utf-8", newline="")
    return pr


def write_verdict(pr: Path, **changes: object) -> dict[str, object]:
    """Write a passing REVIEW.json for `pr` as it is now (tip `TIP`), with `changes` applied; returns the data."""
    stamp = make_stamp(pr, pr.parent, head)
    data: dict[str, object] = {**stamp, "verdict": "pass", "findings": []}
    data.update(changes)
    (pr.parent / RULES.verdict_file).write_text(json.dumps(data), encoding="utf-8")
    return data


# --- the description hash -------------------------------------------------------------------------------------------


def test_the_hash_is_sha256_of_the_utf8_text() -> None:
    assert description_sha256("a\nb\n") == hashlib.sha256(b"a\nb\n").hexdigest()


def test_the_hash_ignores_line_endings_and_a_leading_bom() -> None:
    lf = description_sha256("a\nb\n")
    assert description_sha256("a\r\nb\r\n") == lf
    assert description_sha256("a\rb\r") == lf
    assert description_sha256("﻿a\nb\n") == lf


def test_the_hash_changes_with_any_other_edit() -> None:
    assert description_sha256("a\nb\n") != description_sha256("a\nb \n")
    assert description_sha256("a\nb\n") != description_sha256("a\nb")


# --- the images a description references ----------------------------------------------------------------------------


def test_local_images_are_markdown_images_and_img_tags_but_not_uploads() -> None:
    assert local_images(BODY) == ["./shots/before.png", "./shots/after.png"]


def test_an_image_named_twice_is_listed_once() -> None:
    assert local_images("![a](./shots/x.png) ![b](./shots/x.png)") == ["./shots/x.png"]


# --- the stamp ------------------------------------------------------------------------------------------------------


def test_the_stamp_holds_the_tip_the_hash_and_each_image_hash(tmp_path: Path) -> None:
    pr = write_pr(tmp_path)
    stamp = make_stamp(pr, tmp_path, head)
    assert stamp["tip"] == TIP
    assert stamp["body_sha256"] == description_sha256(BODY)
    assert stamp["images"] == [
        {"path": "./shots/before.png", "sha256": hashlib.sha256(b"before").hexdigest()},
        {"path": "./shots/after.png", "sha256": hashlib.sha256(b"after").hexdigest()},
    ]


def test_the_stamp_marks_a_missing_image(tmp_path: Path) -> None:
    pr = write_pr(tmp_path, shots={"before.png": b"before"})
    images = make_stamp(pr, tmp_path, head)["images"]
    assert isinstance(images, list)
    assert images[1] == {"path": "./shots/after.png", "sha256": None}


def test_review_stamp_prints_the_stamp_as_json(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    pr = write_pr(tmp_path)
    monkeypatch.setattr(pr_review, "git_head", head)
    monkeypatch.chdir(REPO)
    assert main(["repo", "review-stamp", str(pr), "--worktree", str(tmp_path)]) == 0
    printed = json.loads(capsys.readouterr().out)
    assert printed["tip"] == TIP and printed["body_sha256"] == description_sha256(BODY)


# --- check-review ---------------------------------------------------------------------------------------------------


def test_a_current_passing_verdict_passes(tmp_path: Path) -> None:
    pr = write_pr(tmp_path)
    write_verdict(pr)
    assert check_review(pr, tmp_path, CONVENTIONS, head) == []


def test_no_verdict_file_is_refused(tmp_path: Path) -> None:
    pr = write_pr(tmp_path)
    assert any("no REVIEW.json" in problem for problem in check_review(pr, tmp_path, CONVENTIONS, head))


def test_a_failing_verdict_is_refused_with_its_blockers(tmp_path: Path) -> None:
    pr = write_pr(tmp_path)
    finding = {"severity": "blocker", "where": "Screenshots", "text": "the after shot shows a loading screen"}
    write_verdict(pr, verdict="fail", findings=[finding])
    problems = check_review(pr, tmp_path, CONVENTIONS, head)
    assert any("verdict is fail" in problem for problem in problems)
    assert any("loading screen" in problem for problem in problems)


def test_a_pass_that_lists_a_blocker_is_refused(tmp_path: Path) -> None:
    pr = write_pr(tmp_path)
    write_verdict(pr, findings=[{"severity": "blocker", "where": "Testing", "text": "no log for the count"}])
    assert any("blocker" in problem for problem in check_review(pr, tmp_path, CONVENTIONS, head))


def test_a_pass_with_notes_only_passes(tmp_path: Path) -> None:
    pr = write_pr(tmp_path)
    write_verdict(pr, findings=[{"severity": "note", "where": "Summary", "text": "could be shorter"}])
    assert check_review(pr, tmp_path, CONVENTIONS, head) == []


def test_a_new_commit_makes_the_verdict_stale(tmp_path: Path) -> None:
    pr = write_pr(tmp_path)
    write_verdict(pr)
    problems = check_review(pr, tmp_path, CONVENTIONS, lambda _worktree: OTHER_TIP)
    assert any("stale" in problem and "tip" in problem for problem in problems)


def test_an_edit_to_the_description_makes_the_verdict_stale(tmp_path: Path) -> None:
    pr = write_pr(tmp_path)
    write_verdict(pr)
    pr.write_text(BODY.replace("Adds a thing.", "Adds two things."), encoding="utf-8")
    assert any(
        "stale" in problem and "description" in problem for problem in check_review(pr, tmp_path, CONVENTIONS, head)
    )


def test_turning_line_endings_into_crlf_keeps_the_verdict_current(tmp_path: Path) -> None:
    pr = write_pr(tmp_path)
    write_verdict(pr)
    pr.write_bytes(BODY.replace("\n", "\r\n").encode("utf-8"))
    assert check_review(pr, tmp_path, CONVENTIONS, head) == []


def test_a_replaced_image_makes_the_verdict_stale(tmp_path: Path) -> None:
    pr = write_pr(tmp_path)
    write_verdict(pr)
    (tmp_path / "shots" / "after.png").write_bytes(b"another after")
    assert any("./shots/after.png" in problem for problem in check_review(pr, tmp_path, CONVENTIONS, head))


def test_an_image_the_verdict_does_not_list_is_refused(tmp_path: Path) -> None:
    pr = write_pr(tmp_path)
    data = write_verdict(pr)
    images = data["images"]
    assert isinstance(images, list)
    write_verdict(pr, images=images[:1])
    assert any("not reviewed" in problem for problem in check_review(pr, tmp_path, CONVENTIONS, head))


def test_two_identical_images_are_refused_whatever_the_verdict(tmp_path: Path) -> None:
    pr = write_pr(tmp_path, shots={"before.png": b"same", "after.png": b"same"})
    write_verdict(pr)
    assert any("identical" in problem for problem in check_review(pr, tmp_path, CONVENTIONS, head))


def test_a_missing_image_is_refused(tmp_path: Path) -> None:
    pr = write_pr(tmp_path, shots={"before.png": b"before"})
    write_verdict(pr)
    assert any("missing" in problem for problem in check_review(pr, tmp_path, CONVENTIONS, head))


@pytest.mark.parametrize(
    ("text", "expected"),
    [
        ("not json", "not JSON"),
        ("[]", "not a JSON object"),
        ('{"tip": "x"}', "missing"),
    ],
)
def test_a_malformed_verdict_file_is_refused(tmp_path: Path, text: str, expected: str) -> None:
    pr = write_pr(tmp_path)
    (tmp_path / RULES.verdict_file).write_text(text, encoding="utf-8")
    assert any(expected in problem for problem in check_review(pr, tmp_path, CONVENTIONS, head))


@pytest.mark.parametrize(
    ("changes", "expected"),
    [
        ({"verdict": "maybe"}, "verdict"),
        ({"findings": "none"}, "findings"),
        ({"findings": [{"severity": "major", "where": "x", "text": "y"}]}, "severity"),
        ({"findings": [{"severity": "note"}]}, "findings"),
        ({"surprise": 1}, "unknown key"),
    ],
)
def test_a_verdict_with_a_bad_field_is_refused(tmp_path: Path, changes: dict[str, object], expected: str) -> None:
    pr = write_pr(tmp_path)
    write_verdict(pr, **changes)
    assert any(expected in problem for problem in check_review(pr, tmp_path, CONVENTIONS, head))


def test_check_review_exits_0_when_current_and_1_when_stale(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    pr = write_pr(tmp_path)
    write_verdict(pr)
    monkeypatch.chdir(REPO)
    monkeypatch.setattr(pr_review, "git_head", head)
    assert main(["repo", "check-review", str(pr), "--worktree", str(tmp_path)]) == 0
    assert "review: ok" in capsys.readouterr().out
    pr.write_text(BODY + "\nmore\n", encoding="utf-8")
    assert main(["repo", "check-review", str(pr), "--worktree", str(tmp_path)]) == 1
    assert "review refused" in capsys.readouterr().out


def test_check_review_exits_2_for_a_missing_description(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.chdir(REPO)
    monkeypatch.setattr(pr_review, "git_head", head)
    assert main(["repo", "check-review", str(tmp_path / "PR.md"), "--worktree", str(tmp_path)]) == 2


# --- the title and body checks check-review also runs -----------------------------------------------------------------


def test_a_description_missing_a_template_section_is_refused(tmp_path: Path) -> None:
    # The pull request that was opened with `Screenshots: none (...)` but no `## Screenshots` heading.
    body = BODY.replace("## Screenshots", "Screenshots: none (no visible change)\n\n## Details")
    pr = write_pr(tmp_path, body)
    write_verdict(pr)
    problems = check_review(pr, tmp_path, CONVENTIONS, head)
    assert any(problem.startswith("body:") and "## Screenshots" in problem for problem in problems)


def test_a_bad_title_is_refused_with_the_title_named(tmp_path: Path) -> None:
    pr = write_pr(tmp_path, BODY.replace("tools: Add a thing", "Add a thing."))
    write_verdict(pr)
    problems = check_review(pr, tmp_path, CONVENTIONS, head)
    assert any(problem.startswith("title:") for problem in problems)


def test_title_and_body_problems_are_reported_alongside_the_verdict(tmp_path: Path) -> None:
    pr = write_pr(tmp_path, BODY.replace("tools: Add a thing", "nope").replace("## Testing", "## Tests"))
    problems = check_review(pr, tmp_path, CONVENTIONS, head)
    assert any(problem.startswith("title:") for problem in problems)
    assert any(problem.startswith("body:") and "## Testing" in problem for problem in problems)
    assert any("no REVIEW.json" in problem for problem in problems)


def test_the_body_check_gets_what_follows_the_title_line(tmp_path: Path) -> None:
    # A title line alone has no sections at all; a title is not part of the body.
    pr = write_pr(tmp_path, "tools: Add a thing\n")
    problems = check_review(pr, tmp_path, CONVENTIONS, head)
    assert any(problem.startswith("body:") and "## Summary" in problem for problem in problems)
    assert not any(problem.startswith("title:") for problem in problems)


def test_local_screenshots_in_a_draft_pass_the_body_check(tmp_path: Path) -> None:
    # `gh pr create --attach` uploads these, so the draft may point at local files.
    pr = write_pr(tmp_path)
    write_verdict(pr)
    assert check_review(pr, tmp_path, CONVENTIONS, head) == []


def test_a_local_path_in_the_text_is_still_refused(tmp_path: Path) -> None:
    pr = write_pr(tmp_path, BODY.replace("Adds a thing.", "Adds a thing, see C:\\Users\\me\\notes.txt."))
    write_verdict(pr)
    assert any("local path" in problem for problem in check_review(pr, tmp_path, CONVENTIONS, head))


def test_check_review_exits_1_for_a_description_missing_a_section(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    pr = write_pr(tmp_path, BODY.replace("## Screenshots", "## Pictures"))
    write_verdict(pr)
    monkeypatch.chdir(REPO)
    monkeypatch.setattr(pr_review, "git_head", head)
    assert main(["repo", "check-review", str(pr), "--worktree", str(tmp_path)]) == 1
    assert "body:" in capsys.readouterr().out


# --- the agent definition and the launcher --------------------------------------------------------------------------


def test_the_committed_agent_definition_reads_as_a_read_only_reviewer() -> None:
    definition = agent_definition(REPO / ".claude" / "agents" / f"{RULES.agent}.md")
    assert definition["model"] == "sonnet"
    tools = definition["tools"]
    assert isinstance(tools, list)
    assert set(tools) == {"Read", "Glob", "Grep", "Bash", "Write"}
    prompt = definition["prompt"]
    assert isinstance(prompt, str)
    assert "REVIEW.json" in prompt and "review-stamp" in prompt
    assert "Edit" not in tools


def test_an_agent_file_without_front_matter_is_refused(tmp_path: Path) -> None:
    agent = tmp_path / "x.md"
    agent.write_text("# no front matter\n", encoding="utf-8")
    with pytest.raises(pr_review.ReviewError):
        agent_definition(agent)


def test_the_review_command_runs_claude_headless_with_the_agent(tmp_path: Path) -> None:
    command = review_command("claude", tmp_path / "agents.json", "pr-reviewer", "sonnet", "review it")
    assert command[0] == "claude" and "-p" in command
    assert command[command.index("--agents") + 1] == str(tmp_path / "agents.json")
    assert command[command.index("--agent") + 1] == "pr-reviewer"
    assert command[command.index("--model") + 1] == "sonnet"
    assert "--dangerously-skip-permissions" in command
    assert command[-1] == "review it"


class FakeClaude:
    """Stands in for `claude -p`: records what it was given and writes the verdict a reviewer would."""

    def __init__(self, verdict: str | None = "pass", status: int = 0) -> None:
        self.verdict = verdict
        self.status = status
        self.calls: list[tuple[list[str], Path]] = []
        self.agents: dict[str, object] = {}

    def __call__(self, command: list[str], cwd: Path, _timeout_minutes: float = 30) -> int:
        self.calls.append((command, cwd))
        self.agents = json.loads(Path(command[command.index("--agents") + 1]).read_text(encoding="utf-8"))
        if self.verdict is not None:
            write_verdict(cwd / "PR.md", verdict=self.verdict)
            (cwd / RULES.report_file).write_text("# Review\n", encoding="utf-8")
        return self.status


def test_review_launches_in_the_description_folder_then_checks_the_verdict(tmp_path: Path) -> None:
    pr = write_pr(tmp_path)
    claude = FakeClaude()
    assert run_review(pr, tmp_path, "origin/main", REPO, CONVENTIONS, launch=claude, head_of=head) == 0
    command, cwd = claude.calls[0]
    assert cwd == tmp_path
    assert RULES.agent in claude.agents
    prompt = command[-1]
    assert str(pr) in prompt and str(tmp_path) in prompt and "origin/main" in prompt
    assert (tmp_path / RULES.verdict_file).exists()
    assert not (tmp_path / pr_review.AGENTS_FILE).exists()


def test_review_removes_an_old_verdict_before_it_starts(tmp_path: Path) -> None:
    pr = write_pr(tmp_path)
    write_verdict(pr)
    assert (
        run_review(pr, tmp_path, "origin/main", REPO, CONVENTIONS, launch=FakeClaude(verdict=None), head_of=head) == 1
    )
    assert not (tmp_path / RULES.verdict_file).exists()


def test_review_fails_on_a_failing_verdict(tmp_path: Path) -> None:
    pr = write_pr(tmp_path)
    assert run_review(pr, tmp_path, "origin/main", REPO, CONVENTIONS, launch=FakeClaude("fail"), head_of=head) == 1


def test_review_fails_when_claude_fails(tmp_path: Path) -> None:
    pr = write_pr(tmp_path)
    claude = FakeClaude(status=3)
    assert run_review(pr, tmp_path, "origin/main", REPO, CONVENTIONS, launch=claude, head_of=head) == 1


def test_review_command_line_uses_the_injected_launcher(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    pr = write_pr(tmp_path)
    claude = FakeClaude()
    monkeypatch.chdir(REPO)
    monkeypatch.setattr(pr_review, "launch_claude", claude)
    monkeypatch.setattr(pr_review, "git_head", head)
    assert main(["repo", "review", str(pr), "--worktree", str(tmp_path), "--model", "opus"]) == 0
    command, _cwd = claude.calls[0]
    assert command[command.index("--model") + 1] == "opus"


def test_review_refuses_a_bad_description_without_launching_the_agent(tmp_path: Path) -> None:
    pr = write_pr(tmp_path, BODY.replace("## Screenshots", "## Pictures"))
    claude = FakeClaude()
    assert run_review(pr, tmp_path, "origin/main", REPO, CONVENTIONS, launch=claude, head_of=head) == 1
    assert claude.calls == []


# --- the Model line against the branch's Co-Authored-By trailers -----------------------------------------------------


def models(*names: str) -> pr_review.ModelReader:
    """A fake reader of the branch's trailer models: always `names`, whatever the worktree and base."""
    return lambda _worktree, _base, _trailers: list(names)


def test_a_model_the_description_does_not_name_is_refused_with_the_model_named(tmp_path: Path) -> None:
    pr = write_pr(tmp_path)
    write_verdict(pr)
    problems = check_review(pr, tmp_path, CONVENTIONS, head, models("Claude Sonnet 5.5", "Claude Opus 5.5"))
    assert len(problems) == 1
    assert problems[0].startswith("body: ") and "Claude Opus 5.5" in problems[0]


def test_the_same_branch_passes_once_the_description_names_the_model(tmp_path: Path) -> None:
    pr = write_pr(
        tmp_path, BODY.replace("- **Model:** Claude Sonnet 5.5", "- **Model:** Claude Sonnet 5.5, Claude Opus 5.5")
    )
    write_verdict(pr)
    assert check_review(pr, tmp_path, CONVENTIONS, head, models("Claude Sonnet 5.5", "Claude Opus 5.5")) == []


def test_a_branch_without_trailers_passes_as_before(tmp_path: Path) -> None:
    pr = write_pr(tmp_path, BODY.replace("an AI\n- **Model:** Claude Sonnet 5.5", "a person\n- **Model:** n/a"))
    write_verdict(pr)
    assert check_review(pr, tmp_path, CONVENTIONS, head, models()) == []


def test_the_reader_gets_the_worktree_and_the_base(tmp_path: Path) -> None:
    pr = write_pr(tmp_path)
    write_verdict(pr)
    calls: list[tuple[Path, str]] = []

    def reader(worktree: Path, base: str, _trailers: Trailers) -> list[str]:
        calls.append((worktree, base))
        return []

    check_review(pr, tmp_path, CONVENTIONS, head, reader, "origin/dev")
    assert calls == [(tmp_path, "origin/dev")]


def test_review_refuses_a_wrong_model_line_without_launching_the_agent(tmp_path: Path) -> None:
    pr = write_pr(tmp_path)
    claude = FakeClaude()
    status = run_review(
        pr, tmp_path, "origin/main", REPO, CONVENTIONS, launch=claude, head_of=head, models_of=models("Claude Opus 5.5")
    )
    assert status == 1 and claude.calls == []


def test_check_review_on_the_command_line_reads_the_trailers_of_a_real_branch(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    repo = tmp_path / "repo"
    repo.mkdir()
    make_branch(repo, [message("tools: Add a thing", "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>")])
    scratch = tmp_path / "scratch"
    scratch.mkdir()
    pr = write_pr(scratch)
    write_verdict(pr)
    monkeypatch.chdir(REPO)
    monkeypatch.setattr(pr_review, "git_head", head)
    monkeypatch.setattr(pr_review, "branch_trailer_models", REAL_BRANCH_MODELS)
    argv = ["repo", "check-review", str(pr), "--worktree", str(repo), "--base", "main~1"]
    assert main(argv) == 1
    assert "Claude Opus 5.5" in capsys.readouterr().out
    pr.write_text(BODY.replace("Claude Sonnet 5.5", "Claude Opus 5.5"), encoding="utf-8")
    write_verdict(pr)
    assert main(argv) == 0


def test_check_review_exits_2_when_the_base_cannot_be_found(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    repo = tmp_path / "repo"
    repo.mkdir()
    make_branch(repo, [message()])
    scratch = tmp_path / "scratch"
    scratch.mkdir()
    pr = write_pr(scratch)
    write_verdict(pr)
    monkeypatch.chdir(REPO)
    monkeypatch.setattr(pr_review, "git_head", head)
    monkeypatch.setattr(pr_review, "branch_trailer_models", REAL_BRANCH_MODELS)
    assert main(["repo", "check-review", str(pr), "--worktree", str(repo)]) == 2
    assert "origin/main" in capsys.readouterr().err
