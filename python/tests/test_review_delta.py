# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the delta review of `task review` (docs/guides/review/task.md#delta): what the delta is, with a rebase
left out; when it falls back to a full review; the checklists and the model it needs; and its stamp, which stacks on
the earlier full review. Git is real (a temporary repository); the diff hash and the page checks are fakes, as in
test_task_review. Invented code, names and addresses only."""

import json
from typing import Any

import pytest
from conventions_support import REMOVE, changed
from git_support import git
from review_support import make_verdict
from test_task_review import PAGE, PR_TEXT, REVIEWER, Rig, rig

from coney_tools import identity, review_delta, stamps, task_review
from coney_tools.conventions import ConventionsError, parse_conventions

__all__ = ["rig"]  # the fixture, shared with test_task_review

SONNET = identity.Identity("r2", "reviewer", "claude-sonnet-5-5", verified=True)
TEST_CPP = """#include <catch2/catch_test_macros.hpp>
// Checks the brain.
TEST_CASE("brain thinks", "[ai]") {
    REQUIRE(1 == 1);
}
"""


def full_review(rig: Rig) -> stamps.Stamp:
    """A passing full review of the change as it is now, recorded as `task review` does."""
    rig.write_verdict()
    assert task_review.record(rig.env(), "full") == 0
    return stamps.verify(rig.unit.stamps, rig.rules).stamps[-1]


def fix(rig: Rig, files: dict[str, str], diff: str = "2" * 64) -> None:
    """A fix commit on the branch: `files` written, and a new change hash."""
    for name, text in files.items():
        (rig.root / name).parent.mkdir(parents=True, exist_ok=True)
        (rig.root / name).write_text(text, encoding="utf-8")
    git(rig.root, "add", "-A")
    git(rig.root, "commit", "-q", "-m", "fix")
    rig.diff = diff


def delta_packet(rig: Rig, **changes: Any) -> tuple[dict[str, Any], list[str]]:
    """The packet of a delta review and the tool part's problems."""
    return task_review.build_packet(rig.env(**changes), review_delta.DELTA)


def record_delta(rig: Rig, who: identity.Identity = REVIEWER) -> int:
    """Write a passing verdict for the delta packet and record it as `who`."""
    packet, problems = delta_packet(rig)
    assert problems == [], problems
    rig.verdict_path().parent.mkdir(parents=True, exist_ok=True)
    rig.verdict_path().write_text(json.dumps(make_verdict(packet)), encoding="utf-8")
    who_stamp = {"kind": "tool", "tool": "coney-tools task review", "agent": who.agent(), "source": "hook"}
    return task_review.record(rig.env(identity=who, who=who_stamp), review_delta.DELTA)


# --- the delta -------------------------------------------------------------------------------------------------------


def test_a_code_fix_after_a_passing_review_gets_a_delta_of_its_files_alone(rig: Rig) -> None:
    full = full_review(rig)
    fix(rig, {"src/ai/util.cpp": "int helper() { return 1; }\n"})
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is not None and plan.reasons == ()
    assert plan.delta.since.seq == full.seq and plan.delta.full_seq == full.seq
    assert plan.delta.files == ("src/ai/util.cpp",) and plan.delta.rebased == ()
    assert task_review.choose_mode(rig.env()) == review_delta.DELTA
    packet, problems = delta_packet(rig)
    assert problems == []
    assert packet["mode"] == "delta" and packet["files"] == ["src/ai/util.cpp"]
    # The code's checklist and the commits'; no orig, research or description review: those did not change.
    assert packet["kinds"] == ["code", "commits"] and packet["functions"] == [] and packet["pages"] == []
    assert packet["text"]["images"] == []
    assert packet["delta"]["since_seq"] == full.seq and packet["delta"]["files"] == ["src/ai/util.cpp"]
    assert task_review.required_models(packet["kinds"], rig.conventions.tasks.review) == ()


def test_a_rebase_onto_a_moved_main_is_left_out_of_the_delta(rig: Rig) -> None:
    full_review(rig)
    old_tip = rig.tip()
    # Main moves: a new file, and a comment in a file the branch does not change.
    git(rig.root, "checkout", "-q", "origin/main")
    header = rig.root / "src/ai/brain.h"
    header.write_text("// Brains.\n" + header.read_text(encoding="utf-8"), encoding="utf-8")
    (rig.root / "docs/other.md").parent.mkdir(parents=True, exist_ok=True)
    (rig.root / "docs/other.md").write_text("# Other\n", encoding="utf-8")
    git(rig.root, "add", "-A")
    git(rig.root, "commit", "-q", "-m", "main moves")
    git(rig.root, "checkout", "-q", "feature")
    git(rig.root, "rebase", "-q", "origin/main")
    assert rig.tip() != old_tip
    fix(rig, {"src/ai/util.cpp": "int helper() { return 1; }\n"})
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is not None, plan.reasons
    assert plan.delta.files == ("src/ai/util.cpp",)


def test_a_fix_the_rebase_also_touches_reads_patch_against_patch(rig: Rig) -> None:
    full_review(rig)
    git(rig.root, "checkout", "-q", "origin/main")
    source = rig.root / "src/ai/brain.cpp"
    source.write_text(source.read_text(encoding="utf-8") + "// main's note\n", encoding="utf-8")
    git(rig.root, "commit", "-q", "-a", "-m", "main edits the file")
    git(rig.root, "checkout", "-q", "feature")
    git(rig.root, "rebase", "-q", "origin/main")
    rig.diff = "3" * 64
    # The rebase alone changes nothing the branch does: no delta file.
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is not None and plan.delta.files == ()


# --- the fallback ----------------------------------------------------------------------------------------------------


def test_a_delta_that_changes_an_orig_body_falls_back_to_a_full_review(rig: Rig) -> None:
    full_review(rig)
    source = (rig.root / "src/ai/brain.cpp").read_text(encoding="utf-8")
    fix(rig, {"src/ai/brain.cpp": source.replace("> 4", ">= 4")})
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is None
    assert any(
        reason.startswith("orig-body: the delta changes the @orig function Brain::think") for reason in plan.reasons
    )
    assert task_review.choose_mode(rig.env()) == "full"
    problems = delta_packet(rig)[1]
    assert any(p.startswith("delta: orig-body") and "--full" in p for p in problems), problems


def test_a_comment_in_an_orig_function_is_no_body_change(rig: Rig) -> None:
    full_review(rig)
    source = (rig.root / "src/ai/brain.cpp").read_text(encoding="utf-8")
    fix(rig, {"src/ai/brain.cpp": source.replace("return hunger", "// Thresholds match.\n    return hunger")})
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is not None and plan.delta.files == ("src/ai/brain.cpp",)
    assert delta_packet(rig)[0]["kinds"] == ["code", "commits"]


def test_a_delta_that_changes_a_research_claim_falls_back(rig: Rig) -> None:
    full_review(rig)
    fix(rig, {"docs/research/ai/brain.md": PAGE.replace("| 0x00100030 | Brain_Drop | inferred |", "")})
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is None
    assert plan.reasons == ("research-claim: the delta changes a claim in docs/research/ai/brain.md",)
    # An evidence level that changes is a claim too.
    graded = PAGE.replace("Brain_Drop | inferred", "Brain_Drop | confirmed (code)")
    fix(rig, {"docs/research/ai/brain.md": graded}, diff="3" * 64)
    assert task_review.delta_plan(rig.env()).delta is None


def test_prose_on_a_research_page_is_a_delta_that_needs_opus(rig: Rig) -> None:
    full_review(rig)
    fix(rig, {"docs/research/ai/brain.md": PAGE + "\nThe brain waits between thoughts.\n"})
    packet, problems = delta_packet(rig)
    assert problems == []
    assert packet["kinds"] == ["research", "commits"]
    assert [p["path"] for p in packet["pages"]] == ["docs/research/ai/brain.md"]
    assert task_review.required_models(packet["kinds"], rig.conventions.tasks.review) == ("opus",)
    # A Sonnet reviewer may not record it; an Opus one may, and need check no address of the page.
    assert record_delta(rig, SONNET) == 1
    assert record_delta(rig) == 0


def test_a_delta_that_adds_or_changes_a_test_falls_back(rig: Rig) -> None:
    full_review(rig)
    fix(rig, {"tests/ai/brain_test.cpp": TEST_CPP})
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is None
    assert plan.reasons == ("test-body: the delta changes the test brain thinks in tests/ai/brain_test.cpp",)


def test_a_comment_in_a_test_is_no_test_change(rig: Rig) -> None:
    fix(rig, {"tests/ai/brain_test.cpp": TEST_CPP}, diff=rig.diff)
    rig.stamp("fails-first", "brain thinks", detail={"stubs": []})
    plan_full = task_review.delta_plan(rig.env())
    assert plan_full.delta is None  # no review yet
    stamp = rig.stamp(
        "reviewed",
        covers={
            "diff_sha256": rig.diff,
            "pairs_set_sha256": task_review.build_packet(rig.env(), "full")[0]["pairs_set_sha256"],
        },
        detail={"verdict": "pass", "scope": "full", "functions": {"0x00100010": "c"}},
    )
    fix(rig, {"tests/ai/brain_test.cpp": TEST_CPP.replace("// Checks the brain.", "// Checks the whole brain.")})
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is not None and plan.delta.since.seq == stamp.seq
    assert plan.delta.files == ("tests/ai/brain_test.cpp",)


def test_python_test_bodies_ignore_comments_and_layout() -> None:
    before = review_delta.python_test_bodies(
        "def test_a():\n    assert 1  # one\n\n\nclass T:\n    def test_b(self):\n        pass\n"
    )
    after = review_delta.python_test_bodies(
        "def test_a():\n    # why\n    assert 1\n\nclass T:\n    def test_b(self): pass\n"
    )
    assert before == after and set(before) == {"test_a", "T.test_b"}
    assert review_delta.python_test_bodies("def test_a():\n    assert 2\n") != before
    assert review_delta.python_test_bodies("def test_a(:\n") == {"<file>": "def test_a(:\n"}


def test_the_fallback_rules_are_read_from_the_conventions(rig: Rig) -> None:
    full_review(rig)
    source = (rig.root / "src/ai/brain.cpp").read_text(encoding="utf-8")
    fix(rig, {"src/ai/brain.cpp": source.replace("> 4", ">= 4")})
    review = rig.conventions.tasks.review
    assert review.delta is not None and review.delta.full_when == ("orig-body", "research-claim", "test-body")
    conventions = parse_conventions(changed(("tasks.review.delta.full_when", ["research-claim", "test-body"])))
    plan = task_review.delta_plan(rig.env(conventions=conventions))
    assert plan.delta is not None and plan.delta.files == ("src/ai/brain.cpp",)


def test_an_unknown_fallback_or_a_path_for_the_delta_file_is_refused() -> None:
    with pytest.raises(ConventionsError, match="'orig-comment' is not one of orig-body, research-claim, test-body"):
        parse_conventions(changed(("tasks.review.delta.full_when", ["orig-comment"])))
    with pytest.raises(ConventionsError, match="must be a file name"):
        parse_conventions(changed(("tasks.review.delta.diff_file", "review/DELTA.diff")))
    # Rules from before the delta review have no table: every review is a full one.
    assert parse_conventions(changed(("tasks.review.delta", REMOVE))).tasks.review.delta is None


# --- which earlier review --------------------------------------------------------------------------------------------


def test_no_earlier_review_or_a_failed_full_one_means_a_full_review(rig: Rig) -> None:
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is None and plan.reasons == ("no earlier passing review of the code to stack on",)
    verdict = rig.write_verdict()
    verdict["verdict"] = "fail"
    verdict["findings"] = [{"severity": "important", "where": "src/ai/brain.cpp:3", "text": "no comment"}]
    rig.verdict_path().write_text(json.dumps(verdict), encoding="utf-8")
    assert task_review.record(rig.env(), "full") == 1
    fix(rig, {"src/ai/util.cpp": "int helper() { return 1; }\n"})
    assert task_review.delta_plan(rig.env()).delta is None


def test_a_failed_delta_is_passed_over_and_the_pass_before_it_holds(rig: Rig) -> None:
    full = full_review(rig)
    fix(rig, {"src/ai/util.cpp": "int helper() { return 1; }\n"})
    packet, _ = delta_packet(rig)
    verdict = make_verdict(packet)
    verdict["verdict"] = "fail"
    verdict["findings"] = [{"severity": "important", "where": "src/ai/util.cpp:1", "text": "no comment"}]
    rig.verdict_path().write_text(json.dumps(verdict), encoding="utf-8")
    assert task_review.record(rig.env(), review_delta.DELTA) == 1
    fix(rig, {"src/ai/util.cpp": "/// Helps.\nint helper() { return 1; }\n"}, diff="3" * 64)
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is not None and plan.delta.since.seq == full.seq


def test_an_edited_paragraph_adds_the_description_to_the_delta(rig: Rig) -> None:
    full_review(rig)
    fix(rig, {"src/ai/util.cpp": "int helper() { return 1; }\n"})
    rig.text.write_text(PR_TEXT.replace("now uses", "uses"), encoding="utf-8")
    packet, problems = delta_packet(rig)
    assert problems == [] and packet["kinds"] == ["code", "text", "commits"]
    assert [image["path"] for image in packet["text"]["images"]] == ["./shots/after.png"]


# --- the stamp -------------------------------------------------------------------------------------------------------


def test_a_delta_stamp_stacks_on_the_full_review_and_keeps_what_it_judged(
    rig: Rig, capsys: pytest.CaptureFixture[str]
) -> None:
    full = full_review(rig)
    fix(rig, {"src/ai/util.cpp": "int helper() { return 1; }\n"})
    # Any model may review a code-only delta.
    assert record_delta(rig, SONNET) == 0
    stamp = stamps.verify(rig.unit.stamps, rig.rules).stamps[-1]
    assert stamp.step == "reviewed" and stamp.result == "pass" and stamp.covers["diff_sha256"] == "2" * 64
    assert stamp.detail["scope"] == "delta" and stamp.detail["kinds"] == ["code", "commits"]
    assert stamp.detail["since_seq"] == full.seq and stamp.detail["full_seq"] == full.seq
    # What the full review said of the function and the page is carried, so `pr verify` reads it from the newest stamp.
    assert stamp.detail["functions"] == {"0x00100010": "c"} and stamp.detail["pages"] == ["docs/research/ai/brain.md"]
    # Recording it again writes nothing new: the same earlier review is found again.
    assert record_delta(rig, SONNET) == 0
    assert stamps.verify(rig.unit.stamps, rig.rules).stamps[-1].seq == stamp.seq
    # A second fix stacks on the delta, and names the same full review at the bottom.
    fix(rig, {"src/ai/more.cpp": "int more() { return 2; }\n"}, diff="3" * 64)
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is not None and plan.delta.since.seq == stamp.seq and plan.delta.full_seq == full.seq
    assert plan.delta.files == ("src/ai/more.cpp",)
    assert "unchanged" in capsys.readouterr().out


def test_prepare_writes_the_delta_for_the_reviewer(rig: Rig, capsys: pytest.CaptureFixture[str]) -> None:
    full_review(rig)
    fix(rig, {"src/ai/util.cpp": "int helper() { return 1; }\n"})
    assert task_review.prepare(rig.env(), review_delta.DELTA) == 0
    shown = (rig.unit.dir / "review" / "DELTA.diff").read_text(encoding="utf-8")
    assert "## src/ai/util.cpp" in shown and "+int helper()" in shown and "brain.cpp" not in shown
    out = capsys.readouterr().out
    assert "delta since the passing review" in out and "task review t1 --delta --record" in out
    prompt = task_review.review_prompt(rig.env(), review_delta.DELTA)
    assert "delta review" in prompt and "--delta --record" in prompt
    # A full review leaves no delta file behind.
    assert task_review.prepare(rig.env(), "full") == 0
    assert not (rig.unit.dir / "review" / "DELTA.diff").exists()


def test_a_text_review_rests_on_a_delta_as_on_a_full_one(rig: Rig) -> None:
    full = full_review(rig)
    fix(rig, {"src/ai/util.cpp": "int helper() { return 1; }\n"})
    assert record_delta(rig) == 0
    rig.text.write_text(PR_TEXT.replace("now uses", "uses"), encoding="utf-8")
    packet, problems = task_review.build_packet(rig.env(), "text")
    assert problems == [], problems
    rig.verdict_path().write_text(json.dumps(make_verdict(packet)), encoding="utf-8")
    assert task_review.record(rig.env(), "text") == 0
    stamp = stamps.verify(rig.unit.stamps, rig.rules).stamps[-1]
    assert stamp.detail["scope"] == "text" and stamp.detail["full_seq"] == full.seq
    assert stamp.detail["functions"] == {"0x00100010": "c"}
