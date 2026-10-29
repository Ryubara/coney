# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `task review`: the packet the reviewer is given, the record of its verdict as a `reviewed` stamp, and the
headless run. Git is real (a temporary repository); GitHub, the agent, the diff hash and the page checks are fakes.
Invented code, names and addresses only."""

import argparse
import dataclasses
import hashlib
import json
import shutil
from collections.abc import Callable
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any

import pytest
from command_support import commands_in, unknown_commands
from conventions_support import REMOVE, changed, write_checkout
from git_support import git
from review_support import make_verdict

from coney_tools import identity, orig_pairs, pr_proof, review_verdict, stamps, task_review
from coney_tools.config import ConfigError
from coney_tools.conventions import ConventionsError, load_conventions
from coney_tools.review_verdict import problems as verdict_problems
from coney_tools.unit import Unit
from coney_tools.unit_record import UnitRecord, read_record, write_record

BRAIN_H = """#pragma once
namespace coney::ai {
class Brain {
  public:
    /// Thinks.
    /// @orig 0x00100010 Brain_Think (Brain.cpp)
    int think(int hunger);
};
} // namespace coney::ai
"""
BRAIN_CPP = """#include "ai/brain.h"
namespace coney::ai {
int Brain::think(int hunger) {
    return hunger > 3 ? 1 : 0;
}
} // namespace coney::ai
"""
PR_TEXT = """gameplay: Fix the brain

## Summary

Brain::think now uses the original's threshold.

## Testing

Runtime: 4 of 4 matched

## Screenshots

![after](./shots/after.png)
"""
PAGE = """---
kind: system
---
# Brain

| Address | Name | Level |
| --- | --- | --- |
| 0x00100010 | Brain_Think | confirmed (code) |
| 0x00100020 | Brain_Wait | confirmed (code) |
| 0x00100030 | Brain_Drop | inferred |
"""
WHO_ENGINEER = {
    "kind": "tool",
    "tool": "coney-tools orig compare",
    "agent": {"id": "e1", "type": "engineer", "model": "claude-sonnet-5-5"},
    "source": "hook",
}
REVIEWER = identity.Identity("r1", "reviewer", "claude-opus-5-5", verified=True)
PASSING_PRE = ((), (), ())


class Clock:
    """A clock that moves a second per reading."""

    def __init__(self) -> None:
        self.now = datetime(2026, 10, 23, 12, 0, 0, tzinfo=UTC)

    def __call__(self) -> datetime:
        self.now += timedelta(seconds=1)
        return self.now


class Rig:
    """A checkout on `main`, a branch that changes Brain::think and a research page, a stamp log and a description."""

    def __init__(self, tmp_path: Path) -> None:
        self.root = tmp_path / "repos" / "coney"
        self.root.mkdir(parents=True)
        write_checkout(self.root)
        agents = self.root / ".claude" / "agents"
        agents.mkdir(parents=True)
        shutil.copy(Path(__file__).resolve().parents[2] / ".claude" / "agents" / "reviewer.md", agents / "reviewer.md")
        for name, text in {"src/ai/brain.h": BRAIN_H, "src/ai/brain.cpp": BRAIN_CPP}.items():
            (self.root / name).parent.mkdir(parents=True, exist_ok=True)
            (self.root / name).write_text(text, encoding="utf-8")
        git(self.root, "init", "-q", "-b", "main")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-q", "-m", "base")
        git(self.root, "branch", "origin/main")
        git(self.root, "checkout", "-q", "-b", "feature")
        (self.root / "src/ai/brain.cpp").write_text(BRAIN_CPP.replace("> 3", "> 4"), encoding="utf-8")
        (self.root / "docs/research/ai").mkdir(parents=True)
        (self.root / "docs/research/ai/brain.md").write_text(PAGE, encoding="utf-8")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-q", "-m", "change think")
        self.conventions = load_conventions(Path(__file__).resolve().parents[2])
        self.rules = self.conventions.stamps
        self.unit = Unit("t1", tmp_path / "scratch" / "t1")
        self.unit.dir.mkdir(parents=True)
        self.text = self.unit.dir / "PR.md"
        self.text.write_text(PR_TEXT, encoding="utf-8")
        (self.unit.dir / "shots").mkdir()
        (self.unit.dir / "shots" / "after.png").write_bytes(b"png")
        self.clock = Clock()
        self.diff = "1" * 64
        self.docs: list[str] = []
        self.copies: list[str] = []
        self.identity = REVIEWER
        self.reviewer_who: dict[str, Any] = {
            "kind": "tool",
            "tool": "coney-tools task review",
            "agent": REVIEWER.agent(),
            "source": "hook",
        }

    def tip(self) -> str:
        """The branch's tip."""
        return git(self.root, "rev-parse", "HEAD").strip()

    def env(self, **changes: Any) -> task_review.ReviewEnv:
        """The environment of a run."""
        values: dict[str, Any] = {
            "root": self.root,
            "worktree": self.root,
            "unit": self.unit,
            "conventions": self.conventions,
            "base_ref": "origin/main",
            "text_file": self.text,
            "change_hash": lambda worktree, base, tip: self.diff,
            "docs_problems": lambda worktree, base: list(self.docs),
            "copy_problems": lambda worktree, base: list(self.copies),
            "identity": self.identity,
            "who": dict(self.reviewer_who),
            "clock": self.clock,
        }
        values.update(changes)
        return task_review.ReviewEnv(**values)

    def stamp(
        self,
        step: str,
        subject: str = "",
        result: str = "pass",
        covers: dict[str, str] | None = None,
        who: dict[str, Any] | None = None,
        detail: dict[str, Any] | None = None,
    ) -> stamps.Stamp:
        """Append a stamp to the unit's log."""
        return stamps.append(
            self.unit.stamps,
            rules=self.rules,
            task="t1",
            step=step,
            subject=subject,
            result=result,
            commit=self.tip(),
            covers=covers or {},
            detail=detail or {},
            who=who or dict(WHO_ENGINEER),
            clock=self.clock,
        )

    def compare(self, body: str | None = None, function: str = "") -> None:
        """What `orig compare` leaves for Brain::think: a pairs file and a `compared` stamp covering it (the body
        hash `body`, when a stamp from before the hash left comments out is wanted). `function` is the key of the
        function compared, written in the stamp's detail as `orig compare` does (empty: a stamp from before it)."""
        from coney_tools import orig_review

        pairs = self.unit.mapping("0x00100010")
        pairs.parent.mkdir(parents=True, exist_ok=True)
        pairs.write_text("version = 1\n", encoding="utf-8")
        functions = orig_review.pr_stamp(self.root, self.tip(), "origin/main")["functions"]
        chosen = next((f for f in functions if f["key"] == function), functions[0])
        body_hash = chosen["body_sha256"]
        self.stamp(
            "compared",
            "0x00100010",
            detail={"coney": function} if function else {},
            covers={
                "decompile_sha256": "a" * 64,
                "ghidra_function_hash": "b" * 64,
                "cut_sha256": "c" * 64,
                "pairs_sha256": orig_pairs.file_sha256(pairs),
                "coney_body_sha256": body_hash if body is None else body,
            },
        )

    def prepare(self, **changes: Any) -> tuple[dict[str, Any], list[str]]:
        """The packet and the tool part's problems."""
        return task_review.build_packet(self.env(**changes), "full")

    def verdict_path(self) -> Path:
        """Where the reviewer writes its verdict."""
        return self.unit.dir / "review" / "REVIEW.json"

    def write_verdict(self, **changes: Any) -> dict[str, Any]:
        """Write a passing verdict that answers the packet as it is now."""
        packet, problems = self.prepare()
        assert problems == [], problems
        verdict = make_verdict(packet, **changes)
        # The research page in the packet has three addresses; the verdict checks the first two.
        self.verdict_path().parent.mkdir(parents=True, exist_ok=True)
        self.verdict_path().write_text(json.dumps(verdict), encoding="utf-8")
        return verdict


@pytest.fixture
def rig(tmp_path: Path) -> Rig:
    """A change that is ready for a review: compared, with a description."""
    made = Rig(tmp_path)
    made.stamp("assigned")
    made.stamp("claimed")
    made.compare()
    return made


# --- the packet -----------------------------------------------------------------------------------------------------


def test_the_packet_says_what_to_judge_and_quotes_the_hashes(rig: Rig) -> None:
    packet, problems = rig.prepare()
    assert problems == []
    assert packet["task"] == "t1" and packet["mode"] == "full" and packet["tip"] == rig.tip()
    assert packet["base"] == git(rig.root, "rev-parse", "origin/main").strip() and packet["diff_sha256"] == rig.diff
    assert packet["kinds"] == ["code", "research", "orig", "text", "commits"]
    assert packet["files"] == ["docs/research/ai/brain.md", "src/ai/brain.cpp"]
    assert [f["coney"] for f in packet["functions"]] == ["Brain::think"]
    assert packet["pages"] == [
        {"path": "docs/research/ai/brain.md", "addresses": ["0x00100010", "0x00100020", "0x00100030"]}
    ]
    text = packet["text"]
    assert text["title"] == "gameplay: Fix the brain" and text["paragraph"].startswith("Brain::think")
    assert text["images"][0]["path"] == "./shots/after.png" and len(text["images_sha256"]) == 64
    assert len(packet["pairs_set_sha256"]) == 64 and packet["rubric_version"] == 1


def test_a_research_data_file_is_research_work_but_no_page(rig: Rig) -> None:
    rules = rig.conventions.tasks.review
    assert rules.is_research_page("docs/research/ai/brain.md") and rules.is_research_page("research/notes.md")
    assert not rules.is_research_page("research/legacy-files.toml")
    assert not rules.is_research_page("research/missions.yaml")
    assert not rules.is_research_page("docs/guides/pr.md")
    git(rig.root, "checkout", "-q", "-b", "datafile", "origin/main")
    (rig.root / "research").mkdir()
    (rig.root / "research/legacy-files.toml").write_text('files = ["a"]\n', encoding="utf-8")
    git(rig.root, "add", "-A")
    git(rig.root, "commit", "-q", "-m", "prune")
    packet, problems = rig.prepare()
    assert problems == [] and packet["pages"] == [] and "research" in packet["kinds"]


def test_a_docs_change_needs_the_docs_checklist_and_a_code_change_the_code_one(rig: Rig) -> None:
    git(rig.root, "checkout", "-q", "-b", "docsonly", "origin/main")
    (rig.root / "docs/guides").mkdir(parents=True)
    (rig.root / "docs/guides/x.md").write_text("# X\n", encoding="utf-8")
    git(rig.root, "add", "-A")
    git(rig.root, "commit", "-q", "-m", "guide")
    packet, problems = rig.prepare()
    assert problems == [] and packet["kinds"] == ["docs", "text", "commits"]
    assert packet["functions"] == [] and packet["pages"] == []


def test_a_python_change_is_code(rig: Rig) -> None:
    git(rig.root, "checkout", "-q", "-b", "py", "origin/main")
    (rig.root / "python").mkdir()
    (rig.root / "python/x.py").write_text("x = 1\n", encoding="utf-8")
    git(rig.root, "add", "-A")
    git(rig.root, "commit", "-q", "-m", "py")
    assert rig.prepare()[0]["kinds"] == ["code", "text", "commits"]


def test_the_pairs_hash_covers_every_changed_function(rig: Rig) -> None:
    first = rig.prepare()[0]["pairs_set_sha256"]
    (rig.unit.mapping("0x00100010")).write_text("version = 1\n# edited\n", encoding="utf-8")
    packet, problems = rig.prepare()
    # The pairs file changed after `compared`: refused, and the packet's hash moved with it.
    assert any("pairs file changed after it was compared" in p for p in problems)
    assert packet["pairs_set_sha256"] != first


def test_the_pairs_hash_is_the_one_pr_verify_recomputes(rig: Rig) -> None:
    packet = rig.prepare()[0]
    addresses = [address for function in packet["functions"] for address in function["addresses"]]
    assert packet["pairs_set_sha256"] == pr_proof.pairs_set_sha256(rig.unit, addresses)


def test_every_problem_of_the_tool_part_is_listed(rig: Rig) -> None:
    (rig.root / "src/ai/brain.cpp").write_text("changed\n", encoding="utf-8")  # uncommitted
    rig.text.write_text(PR_TEXT.replace("gameplay: Fix", "Fix"), encoding="utf-8")
    (rig.unit.dir / "shots" / "after.png").unlink()
    rig.docs = ["docs/research/ai/brain.md:3: levels: an address has no level"]
    rig.copies = ["src/ai/brain.cpp:3: `iVar1`: automatic name"]
    _, problems = rig.prepare()
    text = "\n".join(problems)
    for expected in (
        "uncommitted changes",
        "title:",
        "./shots/after.png: the picture is missing",
        "docs check: docs/research/ai/brain.md:3",
        "copy-check: src/ai/brain.cpp:3",
    ):
        assert expected in text, (expected, problems)


def test_a_function_that_was_never_compared_is_refused(tmp_path: Path) -> None:
    rig = Rig(tmp_path)
    rig.stamp("assigned")
    _, problems = rig.prepare()
    assert any("Brain::think (0x00100010)" in p and "no pairs file" in p for p in problems)


BRAIN_H_TWO = """#pragma once
namespace coney::ai {
class Brain {
  public:
    /// Thinks.
    /// @orig 0x00100010 Brain_Think (Brain.cpp)
    int think(int hunger);
    /// Thinks too: the second part of the same original function.
    /// @orig 0x00100010 Brain_Think (Brain.cpp)
    int ponder(int hunger);
};
} // namespace coney::ai
"""
BRAIN_CPP_TWO = """#include "ai/brain.h"
namespace coney::ai {
int Brain::think(int hunger) {
    return hunger > 4 ? 1 : 0;
}
int Brain::ponder(int hunger) {
    return hunger;
}
} // namespace coney::ai
"""


def cite_twice(rig: Rig) -> None:
    """Commit a second Brain function that carries the same `@orig` address as Brain::think."""
    (rig.root / "src/ai/brain.h").write_text(BRAIN_H_TWO, encoding="utf-8")
    (rig.root / "src/ai/brain.cpp").write_text(BRAIN_CPP_TWO, encoding="utf-8")
    git(rig.root, "commit", "-q", "-a", "-m", "a second function of the address")


def test_two_functions_of_one_address_are_each_accepted_when_each_was_compared(rig: Rig) -> None:
    cite_twice(rig)
    rig.compare(function="Brain::think(int)")
    rig.compare(function="Brain::ponder(int)")
    assert rig.prepare()[1] == []
    rig.compare(function="Brain::think(int)")  # comparing one again does not make the other stale
    assert rig.prepare()[1] == []


def test_a_second_function_of_the_address_is_refused_until_it_is_compared_itself(rig: Rig) -> None:
    cite_twice(rig)
    rig.compare(function="Brain::think(int)")
    problems = rig.prepare()[1]
    assert len(problems) == 1 and "Brain::ponder (0x00100010)" in problems[0], problems
    assert "no current `compared` stamp" in problems[0]


def test_a_function_that_changed_after_it_was_compared_is_refused(rig: Rig) -> None:
    path = rig.root / "src/ai/brain.cpp"
    path.write_text(path.read_text(encoding="utf-8").replace("> 4", "> 5"), encoding="utf-8")
    git(rig.root, "commit", "-q", "-a", "-m", "again")
    _, problems = rig.prepare()
    assert any("0x00100010" in p and "no current `compared` stamp" in p for p in problems)


def old_body_hash(rig: Rig) -> str:
    """Brain::think's body hash as a stamp made before comments were left out holds it."""
    lines = (rig.root / "src/ai/brain.cpp").read_text(encoding="utf-8").splitlines()[2:5]
    return hashlib.sha256(chr(10).join(line.rstrip() for line in lines).encode("utf-8")).hexdigest()


def comment_think(root: Path) -> None:
    """Commit a comment inside Brain::think."""
    path = root / "src/ai/brain.cpp"
    path.write_text(
        path.read_text(encoding="utf-8").replace("    return", "    // a link" + chr(10) + "    return"),
        encoding="utf-8",
    )
    git(root, "commit", "-q", "-a", "-m", "repoint a link")


def test_a_comment_edit_after_it_was_compared_is_not_a_change(rig: Rig) -> None:
    comment_think(rig.root)
    assert rig.prepare()[1] == []


def test_a_stamp_made_under_the_old_hash_still_covers_an_unchanged_function(tmp_path: Path) -> None:
    made = Rig(tmp_path)
    made.stamp("assigned")
    made.stamp("claimed")
    made.compare(body=old_body_hash(made))
    assert made.prepare()[1] == []


def test_a_stamp_made_under_the_old_hash_covers_a_comment_edit_but_not_a_code_edit(tmp_path: Path) -> None:
    made = Rig(tmp_path)
    made.stamp("assigned")
    made.stamp("claimed")
    made.compare(body=old_body_hash(made))
    comment_think(made.root)
    assert made.prepare()[1] == []
    path = made.root / "src/ai/brain.cpp"
    path.write_text(path.read_text(encoding="utf-8").replace("> 4", "> 5"), encoding="utf-8")
    git(made.root, "commit", "-q", "-a", "-m", "code")
    assert any("0x00100010" in p and "no current `compared` stamp" in p for p in made.prepare()[1])


def test_a_log_that_is_not_intact_is_refused(rig: Rig) -> None:
    segment = rig.unit.stamps / "0001.jsonl"
    segment.write_text(segment.read_text(encoding="utf-8").replace('"seq":2', '"seq":9'), encoding="utf-8")
    assert any("stamp log" in p for p in rig.prepare()[1])


def test_an_added_test_needs_a_passing_fails_first_stamp(rig: Rig) -> None:
    (rig.root / "tests").mkdir()
    (rig.root / "tests/brain_test.cpp").write_text('TEST_CASE("Brain thinks higher") {}\n', encoding="utf-8")
    git(rig.root, "add", "-A")
    git(rig.root, "commit", "-q", "-m", "test")
    _, problems = rig.prepare()
    assert any("Brain thinks higher" in p and "fails-first" in p for p in problems)
    rig.stamp("fails-first", "Brain thinks higher")
    assert rig.prepare()[1] == []
    rig.stamp("fails-first", "Brain thinks higher", result="fail")
    assert any("Brain thinks higher" in p for p in rig.prepare()[1])


def test_nothing_to_review_is_refused(rig: Rig) -> None:
    git(rig.root, "checkout", "-q", "-b", "empty", "origin/main")
    found = [p for p in rig.prepare()[1] if "nothing to review" in p]
    # #1094: it says which branch of which checkout it looked at, since main's checkout has nothing beyond main.
    assert len(found) == 1 and "'empty'" in found[0] and str(rig.root) in found[0] and "origin/main" in found[0], found
    assert "--worktree" in found[0]


def test_a_missing_description_is_refused(rig: Rig) -> None:
    rig.text.unlink()
    assert any("PR.md" in p and "cannot be read" in p for p in rig.prepare()[1])


def test_a_docs_none_line_is_the_text_files_business(rig: Rig) -> None:
    git(rig.root, "checkout", "-q", "-b", "code", "origin/main")
    (rig.root / "src/ai").mkdir(parents=True, exist_ok=True)
    (rig.root / "src/ai/other.cpp").write_text("int x;\n", encoding="utf-8")
    git(rig.root, "add", "-A")
    git(rig.root, "commit", "-q", "-m", "code")
    _, problems = rig.prepare()
    assert any("docs:" in p for p in problems)
    rig.text.write_text(PR_TEXT.replace("Brain::think now", "Docs: none\n\nBrain::think now"), encoding="utf-8")
    assert not any(p.startswith("docs:") for p in rig.prepare()[1])


# --- prepare ---------------------------------------------------------------------------------------------------------


def test_prepare_writes_the_packet_and_removes_an_old_verdict(rig: Rig, capsys: pytest.CaptureFixture[str]) -> None:
    rig.verdict_path().parent.mkdir(parents=True)
    rig.verdict_path().write_text("{}", encoding="utf-8")
    (rig.verdict_path().parent / "REVIEW.md").write_text("old", encoding="utf-8")
    assert task_review.prepare(rig.env(), "full") == 0
    folder = rig.unit.dir / "review"
    packet = json.loads((folder / "PACKET.json").read_text(encoding="utf-8"))
    assert packet["tip"] == rig.tip() and not rig.verdict_path().exists() and not (folder / "REVIEW.md").exists()
    out = capsys.readouterr().out
    assert "task review t1" in out and "task review t1 --record" in out and "PACKET.json" in out


def test_prepare_with_problems_writes_nothing_and_exits_one(rig: Rig, capsys: pytest.CaptureFixture[str]) -> None:
    rig.copies = ["src/ai/brain.cpp:3: `iVar1`: automatic name"]
    assert task_review.prepare(rig.env(), "full") == 1
    assert not (rig.unit.dir / "review" / "PACKET.json").exists()
    assert "refused" in capsys.readouterr().out


# --- record ----------------------------------------------------------------------------------------------------------


def test_record_appends_a_reviewed_stamp_bound_to_the_change(rig: Rig, capsys: pytest.CaptureFixture[str]) -> None:
    rig.write_verdict()
    assert task_review.record(rig.env(), "full") == 0
    report = stamps.verify(rig.unit.stamps, rig.rules)
    assert report.ok
    stamp = report.stamps[-1]
    assert (stamp.step, stamp.subject, stamp.result, stamp.commit) == ("reviewed", "", "pass", rig.tip())
    packet = rig.prepare()[0]
    assert stamp.covers["diff_sha256"] == rig.diff
    assert stamp.covers["pairs_set_sha256"] == packet["pairs_set_sha256"]
    assert stamp.covers["title_sha256"] == packet["text"]["title_sha256"]
    assert stamp.covers["paragraph_sha256"] == packet["text"]["paragraph_sha256"]
    assert stamp.covers["images_sha256"] == packet["text"]["images_sha256"]
    assert len(stamp.covers["review_sha256"]) == 64
    assert stamp.detail["reviewer"] == REVIEWER.agent() and stamp.detail["scope"] == "full"
    assert stamp.detail["functions"] == {"0x00100010": "c"} and stamp.detail["pages"] == ["docs/research/ai/brain.md"]
    assert stamp.who["agent"] == REVIEWER.agent() and stamp.who["source"] == "hook"
    recorded = rig.unit.dir / "review" / f"recorded-{stamp.seq}.json"
    assert json.loads(recorded.read_text(encoding="utf-8"))["verdict"] == "pass"
    # The recorded copy is `reviewed.json`: `review.json` is the description review's REVIEW.json on Windows.
    assert (rig.unit.dir / "reviewed.json").read_bytes() == recorded.read_bytes()
    assert review_verdict.RECORDED == "reviewed.json" and not (rig.unit.dir / "review.json").exists()
    assert "reviewed" in capsys.readouterr().out


def test_record_saves_a_fail_verdict_as_a_fail_stamp(rig: Rig) -> None:
    verdict = rig.write_verdict()
    verdict["verdict"] = "fail"
    verdict["findings"] = [{"severity": "important", "where": "src/ai/brain.cpp:3", "text": "no comment"}]
    rig.verdict_path().write_text(json.dumps(verdict), encoding="utf-8")
    assert task_review.record(rig.env(), "full") == 1
    stamp = stamps.verify(rig.unit.stamps, rig.rules).stamps[-1]
    assert stamp.step == "reviewed" and stamp.result == "fail" and stamp.detail["blockers"] == 1


def test_a_failed_full_review_says_the_next_review_is_a_full_one(rig: Rig, capsys: pytest.CaptureFixture[str]) -> None:
    # Even when every blocker is about the description: `--text` needs a passing full review of the same change.
    verdict = rig.write_verdict()
    verdict["verdict"] = "fail"
    verdict["findings"] = [{"severity": "important", "where": "PR.md:3", "text": "names no issue it affects"}]
    rig.verdict_path().write_text(json.dumps(verdict), encoding="utf-8")
    assert task_review.record(rig.env(), "full") == 1
    out = capsys.readouterr().out
    assert f"next: the engineer fixes the findings; then a full `task review {rig.unit.task} --run`" in out
    assert "--text" in out and "passing full review" in out
    # A passing review gives no such advice.
    rig.write_verdict()
    assert task_review.record(rig.env(), "full") == 0
    assert "next:" not in capsys.readouterr().out


def test_a_review_of_a_changed_branch_is_stale_and_writes_nothing(rig: Rig, capsys: pytest.CaptureFixture[str]) -> None:
    rig.write_verdict()
    (rig.root / "docs/research/ai/brain.md").write_text(PAGE + "\nMore.\n", encoding="utf-8")
    git(rig.root, "commit", "-q", "-a", "-m", "more")
    rig.diff = "2" * 64
    before = len(stamps.verify(rig.unit.stamps, rig.rules).stamps)
    assert task_review.record(rig.env(), "full") == 1
    assert len(stamps.verify(rig.unit.stamps, rig.rules).stamps) == before
    out = capsys.readouterr().out
    assert "stale" in out and "the tip" in out and "the change" in out


def test_an_edited_paragraph_makes_the_review_stale(rig: Rig, capsys: pytest.CaptureFixture[str]) -> None:
    rig.write_verdict()
    rig.text.write_text(PR_TEXT.replace("now uses", "uses"), encoding="utf-8")
    assert task_review.record(rig.env(), "full") == 1
    assert "the paragraph" in capsys.readouterr().out


def test_no_verdict_or_a_broken_one_is_refused(rig: Rig, capsys: pytest.CaptureFixture[str]) -> None:
    assert task_review.record(rig.env(), "full") == 1
    assert "no REVIEW.json" in capsys.readouterr().out
    rig.verdict_path().parent.mkdir(parents=True)
    rig.verdict_path().write_text("{", encoding="utf-8")
    assert task_review.record(rig.env(), "full") == 1
    assert "not JSON" in capsys.readouterr().out


def test_recording_the_same_verdict_twice_writes_one_stamp(rig: Rig, capsys: pytest.CaptureFixture[str]) -> None:
    rig.write_verdict()
    assert task_review.record(rig.env(), "full") == 0
    count = len(stamps.verify(rig.unit.stamps, rig.rules).stamps)
    assert task_review.record(rig.env(), "full") == 0
    assert len(stamps.verify(rig.unit.stamps, rig.rules).stamps) == count
    assert "unchanged" in capsys.readouterr().out


@pytest.mark.parametrize(
    ("who", "message"),
    [
        (identity.Identity("e2", "engineer", "claude-opus-5-5", verified=True), "only a reviewer records a review"),
        (identity.Identity("r1", "reviewer", "claude-sonnet-5-5", verified=True), "reverse-engineering review"),
    ],
)
def test_the_wrong_reviewer_is_refused(
    rig: Rig, who: identity.Identity, message: str, capsys: pytest.CaptureFixture[str]
) -> None:
    rig.write_verdict()
    assert task_review.record(rig.env(identity=who), "full") == 1
    assert message in capsys.readouterr().out
    assert stamps.verify(rig.unit.stamps, rig.rules).stamps[-1].step != "reviewed"


def caller(agent_id: str, kind: str, session: str = "") -> identity.Identity:
    """A verified caller as the hooks record one: the id is `main` in a main session, a unique one in a subagent."""
    return identity.Identity(agent_id, kind, "claude-opus-5-5", verified=True, session=session)


def stamped_by(rig: Rig, who: identity.Identity, step: str, subject: str = "") -> None:
    """A stamp of `step` written by `who`."""
    rig.stamp(
        step,
        subject,
        who={"kind": "tool", "tool": "coney-tools", "agent": who.agent(), "source": "hook"},
    )


def reviewer_env(rig: Rig, who: identity.Identity) -> task_review.ReviewEnv:
    """The environment of a run by `who`, with the `who` a stamp of its would hold."""
    mine = {"kind": "tool", "tool": "coney-tools task review", "agent": who.agent(), "source": "hook"}
    return rig.env(identity=who, who=mine)


def test_a_main_session_reviewer_is_not_the_namesake_of_another_main_session(rig: Rig) -> None:
    # Every main session has the id `main`; an engineer started with `claude --agent engineer` is one of them.
    stamped_by(rig, caller("main", "engineer", "s-engineer"), "checked")
    rig.write_verdict()
    assert task_review.record(reviewer_env(rig, caller("main", "reviewer", "s-reviewer")), "full") == 0


def test_a_main_session_that_did_the_work_may_not_review_it(rig: Rig, capsys: pytest.CaptureFixture[str]) -> None:
    stamped_by(rig, caller("main", "engineer", "s1"), "checked")
    rig.write_verdict()
    assert task_review.record(reviewer_env(rig, caller("main", "reviewer", "s1")), "full") == 1
    assert "also worked on this task" in capsys.readouterr().out


def test_main_sessions_are_told_apart_by_their_session(rig: Rig) -> None:
    stamped_by(rig, caller("main", "none", "s1"), "checked")
    stamped_by(rig, caller("main", "engineer", "s2"), "fails-first", "x")
    rig.write_verdict()
    assert task_review.record(reviewer_env(rig, caller("main", "reviewer", "s3")), "full") == 0


def test_a_subagent_reviewer_is_not_a_subagent_engineer(rig: Rig) -> None:
    stamped_by(rig, caller("agent-a1", "engineer", "s1"), "checked")
    rig.write_verdict()
    assert task_review.record(reviewer_env(rig, caller("agent-r1", "reviewer", "s1")), "full") == 0


def test_a_subagent_that_did_the_work_may_not_review_it(rig: Rig, capsys: pytest.CaptureFixture[str]) -> None:
    stamped_by(rig, caller("agent-a1", "engineer", "s1"), "checked")
    rig.write_verdict()
    assert task_review.record(reviewer_env(rig, caller("agent-a1", "reviewer", "s1")), "full") == 1
    assert "also worked on this task" in capsys.readouterr().out


def test_a_reviewers_own_compared_stamp_bars_it_from_reviewing(rig: Rig, capsys: pytest.CaptureFixture[str]) -> None:
    # `[roles.reviewer]` does not deny `orig compare`: the check has to catch what the reviewer wrote itself.
    me = caller("agent-r1", "reviewer", "s1")
    stamped_by(rig, me, "compared", "0x00100020")
    rig.write_verdict()
    assert task_review.record(reviewer_env(rig, me), "full") == 1
    assert "also worked on this task" in capsys.readouterr().out


def test_a_reviewer_may_record_again_after_its_own_review(rig: Rig) -> None:
    me = caller("agent-r1", "reviewer", "s1")
    rig.write_verdict(verdict="fail", findings=[{"severity": "important", "where": "x:1", "text": "y"}])
    assert task_review.record(reviewer_env(rig, me), "full") == 1
    rig.write_verdict()
    assert task_review.record(reviewer_env(rig, me), "full") == 0


def test_a_main_session_with_no_session_id_is_let_through_as_unverifiable(rig: Rig) -> None:
    stamped_by(rig, caller("main", "engineer"), "checked")
    rig.write_verdict()
    assert task_review.record(reviewer_env(rig, caller("main", "reviewer")), "full") == 0


def test_a_smaller_model_may_review_a_change_with_no_reverse_engineering(rig: Rig) -> None:
    git(rig.root, "checkout", "-q", "-b", "py", "origin/main")
    (rig.root / "python").mkdir()
    (rig.root / "python/x.py").write_text("x = 1\n", encoding="utf-8")
    git(rig.root, "add", "-A")
    git(rig.root, "commit", "-q", "-m", "py")
    rig.write_verdict()
    sonnet = identity.Identity("r1", "reviewer", "claude-sonnet-5-5", verified=True)
    assert task_review.record(rig.env(identity=sonnet), "full") == 0


def test_an_unverified_caller_is_recorded_as_unverified(rig: Rig) -> None:
    rig.write_verdict()
    unknown = identity.Identity()
    who = {"kind": "tool", "tool": "coney-tools task review", "agent": unknown.agent(), "source": "unverified"}
    assert task_review.record(rig.env(identity=unknown, who=who), "full") == 0
    stamp = stamps.verify(rig.unit.stamps, rig.rules).stamps[-1]
    assert stamp.who["source"] == "unverified" and stamp.result == "pass"


# --- the text alone --------------------------------------------------------------------------------------------------


def test_a_text_review_needs_a_current_full_pass(rig: Rig, capsys: pytest.CaptureFixture[str]) -> None:
    packet, problems = task_review.build_packet(rig.env(), "text")
    assert any("no passing full review" in p for p in problems)
    rig.write_verdict()
    assert task_review.record(rig.env(), "full") == 0
    packet, problems = task_review.build_packet(rig.env(), "text")
    assert problems == [] and packet["mode"] == "text" and packet["kinds"] == ["text"]
    assert packet["functions"] == [] and packet["pages"] == []
    rig.diff = "2" * 64
    assert any("no passing full review" in p for p in task_review.build_packet(rig.env(), "text")[1])


def test_a_text_review_keeps_the_full_reviews_functions_and_pages(rig: Rig) -> None:
    rig.write_verdict()
    assert task_review.record(rig.env(), "full") == 0
    full = stamps.verify(rig.unit.stamps, rig.rules).stamps[-1]
    rig.text.write_text(PR_TEXT.replace("now uses", "uses"), encoding="utf-8")
    packet, problems = task_review.build_packet(rig.env(), "text")
    assert problems == []
    verdict = make_verdict(packet)
    rig.verdict_path().write_text(json.dumps(verdict), encoding="utf-8")
    assert task_review.record(rig.env(), "text") == 0
    stamp = stamps.verify(rig.unit.stamps, rig.rules).stamps[-1]
    assert stamp.step == "reviewed" and stamp.detail["scope"] == "text" and stamp.detail["full_seq"] == full.seq
    assert stamp.detail["functions"] == full.detail["functions"] and stamp.detail["pages"] == full.detail["pages"]
    assert stamp.covers["pairs_set_sha256"] == full.covers["pairs_set_sha256"]
    assert stamp.covers["paragraph_sha256"] != full.covers["paragraph_sha256"]


# --- the headless run ------------------------------------------------------------------------------------------------


class Launcher:
    """A fake `claude`: records its command and, as the reviewer would, records the verdict it was given."""

    def __init__(self, rig: Rig, status: int = 0, record: bool = True) -> None:
        self.rig = rig
        self.status = status
        self.record = record
        self.commands: list[tuple[list[str], Path]] = []

    def __call__(self, command: list[str], cwd: Path) -> int:
        self.commands.append((command, cwd))
        if self.record:
            self.rig.write_verdict()
            task_review.record(self.rig.env(), "full")
        return self.status


def run(rig: Rig, launch: Callable[[list[str], Path], int], **more: Any) -> int:
    """`task review --run` with the fake launcher."""
    return task_review.run(rig.env(), "full", launch=launch, claude="claude", **more)


def test_run_prepares_launches_the_reviewer_and_reports_its_stamp(rig: Rig, capsys: pytest.CaptureFixture[str]) -> None:
    launcher = Launcher(rig)
    assert run(rig, launcher) == 0
    command, cwd = launcher.commands[0]
    assert cwd == rig.root and command[:2] == ["claude", "-p"]
    assert command[command.index("--agent") + 1] == "reviewer"
    prompt = command[-1]
    assert "task review t1 --record" in prompt and "REVIEW.json" in prompt and "PACKET.json" in prompt
    assert "reviewed: pass" in capsys.readouterr().out
    assert not (rig.unit.dir / "review" / ".reviewer-agents.json").exists()


def test_run_does_not_start_the_reviewer_when_the_tool_part_fails(rig: Rig) -> None:
    rig.copies = ["src/ai/brain.cpp:3: `iVar1`: automatic name"]
    launcher = Launcher(rig)
    assert run(rig, launcher) == 1 and launcher.commands == []


def test_run_fails_when_claude_fails_or_nothing_was_recorded(rig: Rig, capsys: pytest.CaptureFixture[str]) -> None:
    assert run(rig, Launcher(rig, status=3, record=False)) == 1
    assert "exited with 3" in capsys.readouterr().out
    assert run(rig, Launcher(rig, record=False)) == 1
    assert "did not record" in capsys.readouterr().out


def test_run_exits_one_for_a_review_that_fails(rig: Rig, capsys: pytest.CaptureFixture[str]) -> None:
    def failing(command: list[str], cwd: Path) -> int:
        verdict = rig.write_verdict()
        verdict["verdict"] = "fail"
        verdict["findings"] = [{"severity": "critical", "where": "src/ai/brain.cpp:3", "text": "game data"}]
        rig.verdict_path().write_text(json.dumps(verdict), encoding="utf-8")
        task_review.record(rig.env(), "full")
        return 0

    assert run(rig, failing) == 1
    assert "reviewed: fail" in capsys.readouterr().out


def test_the_default_agent_file_is_the_repository_s_reviewer() -> None:
    root = Path(__file__).resolve().parents[2]
    rules = load_conventions(root).tasks.review
    assert (root / ".claude" / "agents" / f"{rules.agent}.md").is_file()


def test_problems_of_a_verdict_use_the_conventions_minimum(rig: Rig) -> None:
    packet = rig.prepare()[0]
    verdict = make_verdict(packet)
    assert verdict_problems(verdict, packet, rig.conventions.tasks.review.pages_min_addresses) == []


# --- the command line ------------------------------------------------------------------------------------------------


def test_the_command_line_selects_prepare_record_run_and_the_text_mode(
    rig: Rig, monkeypatch: pytest.MonkeyPatch
) -> None:
    from coney_tools.cli import main

    calls: list[tuple[str, str]] = []

    def stub(name: str) -> Callable[..., int]:
        """A replacement for a step that only notes it was called."""

        def called(env: object, mode: str, **more: object) -> int:
            calls.append((name, mode))
            return 0

        return called

    monkeypatch.setattr(task_review, "environment", lambda args: rig.env())
    monkeypatch.setattr(task_review, "prepare", stub("prepare"))
    monkeypatch.setattr(task_review, "record", stub("record"))
    monkeypatch.setattr(task_review, "run", stub("run"))
    assert main(["task", "review", "t1"]) == 0
    assert main(["task", "review", "t1", "--text"]) == 0
    assert main(["task", "review", "t1", "--record"]) == 0
    assert main(["task", "review", "t1", "--record", "--text"]) == 0
    assert main(["task", "review", "t1", "--run", "--model", "opus", "--effort", "high"]) == 0
    assert calls == [("prepare", "full"), ("prepare", "text"), ("record", "full"), ("record", "text"), ("run", "full")]


def test_record_and_run_cannot_be_asked_together() -> None:
    from coney_tools.cli import main

    with pytest.raises(SystemExit) as stopped:
        main(["task", "review", "t1", "--record", "--run"])
    assert stopped.value.code == 2


def test_an_error_outside_the_tool_part_exits_two(
    rig: Rig, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    from coney_tools.cli import main

    def broken(args: object) -> task_review.ReviewEnv:
        raise ConfigError("no such checkout")

    monkeypatch.setattr(task_review, "environment", broken)
    assert main(["task", "review", "t1"]) == 2
    assert "no such checkout" in capsys.readouterr().err


def test_the_environment_comes_from_the_checkout_and_its_branch(rig: Rig) -> None:
    import argparse

    args = argparse.Namespace(worktree=rig.root, task=None, base=None, pr_file=None)
    env = task_review.environment(args)
    assert env.worktree == rig.root.resolve() and env.base_ref == "origin/main"
    assert env.unit.task == "feature" and env.unit.dir == rig.unit.dir.parent / "feature"
    assert env.text_file == env.unit.dir / "PR.md" and env.folder == env.unit.dir / "review"
    named = argparse.Namespace(worktree=rig.root, task="t1", base="origin/dev", pr_file=rig.text)
    other = task_review.environment(named)
    assert (other.unit.task, other.base_ref, other.text_file) == ("t1", "origin/dev", rig.text)


def test_the_environment_reads_a_branch_cut_before_a_conventions_change(
    rig: Rig, capsys: pytest.CaptureFixture[str]
) -> None:
    """#1093: `task review` of a unit whose file lacks a key main has since required, or adds one main does not know."""
    whole = load_conventions(rig.root)
    write_checkout(rig.root, changed(("tasks.start.slots.tracks", REMOVE), ("claims.a_later_rule", 3)))
    args = argparse.Namespace(worktree=rig.root, task=None, base=None, pr_file=None)
    assert task_review.environment(args).conventions == whole
    err = capsys.readouterr().err
    assert "ignoring claims.a_later_rule" in err and "missing tasks.start.slots.tracks" in err
    write_checkout(rig.root, changed(("tasks.start.slots.tracks", REMOVE), ("merge.auto_merge", "yes")))
    with pytest.raises(ConventionsError, match=r"merge\.auto_merge: expected true or false"):
        task_review.environment(args)


def test_the_default_change_hash_is_the_stamp_formats(rig: Rig) -> None:
    from coney_tools import pr_diff

    base = git(rig.root, "rev-parse", "origin/main").strip()
    assert task_review.default_change_hash(rig.root, base, rig.tip()) == pr_diff.change_sha256(
        rig.root, base, rig.tip()
    )


# --- the unit's worktree is the default (#1094) ----------------------------------------------------------------------


def start_unit(rig: Rig, record_worktree: Path | None = None) -> Path:
    """What `task start` leaves: a worktree of its own, one commit beyond the main checkout's branch, and the unit
    record that names it (`record_worktree`: the folder the record names instead, which may not exist)."""
    worktree = rig.root.parent.parent / "worktrees" / "coney" / "t1"
    git(rig.root, "worktree", "add", "-q", "-b", "t1", str(worktree), "feature")
    (worktree / "NOTES.md").write_text("notes\n", encoding="utf-8")
    git(worktree, "add", "-A")
    git(worktree, "commit", "-q", "-m", "work in the unit's worktree")
    write_record(
        rig.unit.dir / "unit.json",
        UnitRecord(
            task="t1",
            kind="tooling",
            issues=(1094,),
            claimant="Someone",
            branch="t1",
            worktree=record_worktree or worktree,
            base_ref="origin/main",
            base_commit="a" * 40,
            started=datetime(2026, 10, 28, 12, 0, 0, tzinfo=UTC),
        ),
    )
    return worktree


def arguments(**changes: Any) -> argparse.Namespace:
    """What `task review t1` parsed to."""
    values: dict[str, Any] = {"worktree": None, "task": "t1", "base": None, "pr_file": None}
    values.update(changes)
    return argparse.Namespace(**values)


def test_review_takes_the_worktree_the_unit_record_names_from_any_checkout(
    rig: Rig, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    unit_tree = start_unit(rig)
    monkeypatch.chdir(rig.root)  # main's checkout, on another branch
    env = task_review.environment(arguments())
    assert env.worktree == unit_tree.resolve() and env.root == env.worktree
    assert env.unit.task == "t1" and env.unit.dir == rig.unit.dir
    assert env.text_file == rig.unit.dir / "PR.md"
    # It reviews the unit's commits, not main's checkout's, and says where it looked.
    packet, _ = task_review.build_packet(
        dataclasses.replace(env, change_hash=lambda worktree, base, tip: rig.diff, docs_problems=lambda w, b: []),
        "full",
    )
    assert packet["tip"] == git(unit_tree, "rev-parse", "HEAD").strip() != rig.tip()
    assert "NOTES.md" in packet["files"]
    err = capsys.readouterr().err
    assert "the unit record" in err and str(unit_tree.resolve()) in err


def test_review_inside_the_units_worktree_needs_no_note(
    rig: Rig, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    unit_tree = start_unit(rig)
    monkeypatch.chdir(unit_tree)
    assert task_review.environment(arguments(task=None)).worktree == unit_tree.resolve()
    assert capsys.readouterr().err == ""


def test_a_fix_task_is_reviewed_in_its_parents_log_and_text(
    rig: Rig, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    unit_tree = start_unit(rig)
    # `task start` of a fix task: a record of its own that copies the parent's branch and worktree and names it.
    write_record(
        rig.unit.dir.parent / "t1-fix" / "unit.json",
        dataclasses.replace(read_record(rig.unit.dir / "unit.json"), task="t1-fix", kind="fix", parent="t1"),
    )
    monkeypatch.chdir(rig.root)
    env = task_review.environment(arguments(task="t1-fix"))
    assert env.worktree == unit_tree.resolve()
    # The parent's log is stamped and the parent's PR.md is the description (UnitRecord.log_task).
    assert env.unit.task == "t1" and env.unit.dir == rig.unit.dir
    assert env.text_file == rig.unit.dir / "PR.md"
    assert "t1-fix is a fix of t1" in capsys.readouterr().err


def test_the_worktree_option_wins_over_the_unit_record(rig: Rig, monkeypatch: pytest.MonkeyPatch) -> None:
    start_unit(rig)
    monkeypatch.chdir(rig.root)
    assert task_review.environment(arguments(worktree=rig.root)).worktree == rig.root.resolve()


def test_a_task_started_without_a_record_is_reviewed_where_the_command_runs(
    rig: Rig, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(rig.root)
    assert not (rig.unit.dir / "unit.json").exists()
    assert task_review.environment(arguments()).worktree == rig.root.resolve()
    assert capsys.readouterr().err == ""


def test_a_record_naming_a_worktree_that_is_gone_is_refused_with_the_way_out(
    rig: Rig, monkeypatch: pytest.MonkeyPatch
) -> None:
    gone = rig.root.parent.parent / "worktrees" / "coney" / "gone"
    start_unit(rig, record_worktree=gone)
    monkeypatch.chdir(rig.root)
    with pytest.raises(
        ConfigError, match=r"t1: the unit record names the worktree .*gone.*, which does not exist"
    ) as why:
        task_review.environment(arguments())
    assert "--worktree" in str(why.value)


def test_a_damaged_record_is_refused_not_ignored(rig: Rig, monkeypatch: pytest.MonkeyPatch) -> None:
    (rig.unit.dir / "unit.json").write_text("not json", encoding="utf-8")
    monkeypatch.chdir(rig.root)
    with pytest.raises(ConfigError, match="not valid JSON") as why:
        task_review.environment(arguments())
    assert "--worktree DIR" in str(why.value) and "unit.json" in str(why.value)
    # The way out works: the worktree option does not read the record.
    assert task_review.environment(arguments(worktree=rig.root)).worktree == rig.root.resolve()


def test_the_command_line_reviews_the_units_worktree_from_main(rig: Rig, monkeypatch: pytest.MonkeyPatch) -> None:
    """`task review t1`, run from main's checkout, prepares the unit's worktree and not main's."""
    from coney_tools.cli import main

    unit_tree = start_unit(rig)
    monkeypatch.chdir(rig.root)
    seen: list[Path] = []

    def prepare(env: task_review.ReviewEnv, mode: str) -> int:
        seen.append(env.worktree)
        return 0

    monkeypatch.setattr(task_review, "prepare", prepare)
    assert main(["task", "review", "t1"]) == 0
    assert seen == [unit_tree.resolve()]


# --- the description's generated sections (#1094) --------------------------------------------------------------------


def test_the_packet_names_the_sections_pr_text_writes_and_the_file_it_writes_them_to(rig: Rig) -> None:
    packet = rig.prepare()[0]
    rules = rig.conventions.pull_requests
    generated = packet["text"]["generated_sections"]
    named = {item["section"] for item in generated}
    assert [item["section"] for item in generated] == [name for name in rules.sections if name in named]
    assert named == {rules.closing.section, rules.checklist.section, rules.runtime.section, rules.ai_use.section}
    assert all(item["what"] for item in generated)
    assert packet["text"]["generated_body"] == {"path": str(rig.text.parent / "PR-BODY.md"), "exists": False}
    (rig.text.parent / "PR-BODY.md").write_text("## Summary\n", encoding="utf-8")
    assert rig.prepare()[0]["text"]["generated_body"]["exists"] is True


def test_prepare_tells_the_reviewer_which_sections_are_generated(rig: Rig, capsys: pytest.CaptureFixture[str]) -> None:
    assert task_review.prepare(rig.env(), "full") == 0
    out = capsys.readouterr().out
    assert "PR.md is the engineer's file" in out and "`pr text`" in out
    assert "not written yet" in out and str(rig.text.parent / "PR-BODY.md") in out
    for item in rig.prepare()[0]["text"]["generated_sections"]:
        assert item["section"] in out
    (rig.text.parent / "PR-BODY.md").write_text("## Summary\n", encoding="utf-8")
    assert task_review.prepare(rig.env(), "full") == 0
    assert "not written yet" not in capsys.readouterr().out


def test_the_headless_reviewer_is_told_the_generated_sections_are_not_the_engineers(rig: Rig) -> None:
    prompt = task_review.review_prompt(rig.env(), "full")
    assert "PR-BODY.md" in prompt and "generated" in prompt


# --- every refusal names a command that exists (#1094) ---------------------------------------------------------------


def test_every_command_the_refusals_of_the_tool_part_name_exists(tmp_path: Path) -> None:
    """The refusal texts are built from pieces (`run `{redo}``), so this reads them as printed."""
    rig = Rig(tmp_path)
    rig.stamp("assigned")
    (rig.root / "tests").mkdir()
    (rig.root / "tests/brain_test.cpp").write_text('TEST_CASE("Brain thinks higher") {}\n', encoding="utf-8")
    git(rig.root, "add", "-A")
    git(rig.root, "commit", "-q", "-m", "test")
    cite_twice(rig)
    problems = rig.prepare()[1]
    text = "\n".join(problems)
    mentioned = commands_in(text)
    assert any(c.startswith("orig compare 0x00100010 --coney-function") for c in mentioned), problems
    assert "orig fails-first" in mentioned, problems
    assert unknown_commands(text) == []
