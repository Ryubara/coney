# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the delta review of `task review` (docs/guides/review/task.md#delta): what the delta is, with a rebase
left out; when it falls back to a full review; the checklists and the model it needs; and its stamp, which stacks on
the earlier full review. Git is real (a temporary repository); the diff hash and the page checks are fakes, as in
test_task_review. Invented code, names and addresses only."""

import json
from pathlib import Path
from typing import Any

import pytest
import yaml
from conventions_support import REMOVE, changed
from git_support import git
from review_support import make_verdict
from test_task_review import PAGE, PR_TEXT, REVIEWER, Rig, rig

from coney_tools import identity, review_delta, stamps, task_review
from coney_tools.conventions import CONVENTIONS_FILE, ConventionsError, parse_conventions

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
    # The code's checklist, the description's (read against what the delta changes) and the commits'; no orig or
    # research review: those did not change.
    assert packet["kinds"] == ["code", "text", "commits"] and packet["functions"] == [] and packet["pages"] == []
    assert [image["path"] for image in packet["text"]["images"]] == ["./shots/after.png"]
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
    # A fix of a line of that file outside the function is in the delta, read patch against patch.
    text = (rig.root / "src/ai/brain.cpp").read_text(encoding="utf-8")
    fix(rig, {"src/ai/brain.cpp": "// Brains think.\n" + text}, diff="4" * 64)
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is not None and plan.delta.files == ("src/ai/brain.cpp",)
    assert plan.delta.rebased == ("src/ai/brain.cpp",)
    assert task_review.prepare(rig.env(), review_delta.DELTA) == 0
    shown = (rig.unit.dir / "review" / "DELTA.diff").read_text(encoding="utf-8")
    then, now = shown.split("### then")[1].split("### now")
    assert "main changed it too" in shown
    assert "+// Brains think." not in then and "+// Brains think." in now
    assert "+// main's note" not in then and "+// main's note" not in now  # main's line is never the branch's


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


@pytest.mark.parametrize(
    ("path", "old", "new"),
    [
        # A comment inside the body: what makes a difference deliberate (diff-deliberate-ok) or not.
        ("src/ai/brain.cpp", "    return hunger", "    // Thresholds match.\n    return hunger"),
        # The name part of the tag.
        ("src/ai/brain.h", "Brain_Think (Brain.cpp)", "Brain_Ponder (Brain.cpp)"),
        # The separate declaration's doc comment.
        ("src/ai/brain.h", "/// Thinks.", "/// Thinks about food."),
    ],
)
def test_a_comment_tag_or_declaration_edit_of_an_orig_function_opens_the_orig_checklist(
    rig: Rig, path: str, old: str, new: str
) -> None:
    full_review(rig)
    source = (rig.root / path).read_text(encoding="utf-8")
    assert old in source
    fix(rig, {path: source.replace(old, new)})
    plan = task_review.delta_plan(rig.env())
    # No full review (the body and the addresses stand) but the function is judged again, on Opus.
    assert plan.delta is not None, plan.reasons
    assert [f["coney"] for f in plan.delta.functions] == ["Brain::think"]
    packet, problems = delta_packet(rig)
    assert problems == [], problems
    assert "orig" in packet["kinds"] and [f["coney"] for f in packet["functions"]] == ["Brain::think"]
    assert task_review.required_models(packet["kinds"], rig.conventions.tasks.review) == ("opus",)
    assert record_delta(rig, SONNET) == 1


def test_a_delta_that_rejudges_a_function_keeps_its_new_class(rig: Rig) -> None:
    full_review(rig)
    source = (rig.root / "src/ai/brain.cpp").read_text(encoding="utf-8")
    fix(rig, {"src/ai/brain.cpp": source.replace("    return hunger", "    // Why the threshold.\n    return hunger")})
    packet, _ = delta_packet(rig)
    verdict = make_verdict(packet)
    verdict["functions"][0]["class"] = "diff-deliberate-ok"
    rig.verdict_path().write_text(json.dumps(verdict), encoding="utf-8")
    assert task_review.record(rig.env(), review_delta.DELTA) == 0
    stamp = stamps.verify(rig.unit.stamps, rig.rules).stamps[-1]
    # The delta's own judgement replaces the one carried from the full review.
    assert stamp.detail["functions"] == {"0x00100010": "d"}


def test_a_comment_outside_any_orig_function_is_code_alone(rig: Rig) -> None:
    full_review(rig)
    source = (rig.root / "src/ai/brain.cpp").read_text(encoding="utf-8")
    fix(rig, {"src/ai/brain.cpp": source + "// The end.\n"})
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is not None and plan.delta.functions == ()
    assert delta_packet(rig)[0]["kinds"] == ["code", "text", "commits"]


def test_pairs_compared_again_fall_back_to_a_full_review(rig: Rig) -> None:
    full_review(rig)
    fix(rig, {"src/ai/util.cpp": "int helper() { return 1; }\n"})
    rig.unit.mapping("0x00100010").write_text("version = 1\n# compared again\n", encoding="utf-8")
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is None
    assert "orig-body: the pairs of the @orig functions changed since the earlier review" in plan.reasons


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
    assert packet["kinds"] == ["research", "text", "commits"]
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


@pytest.mark.parametrize(
    ("path", "before", "after"),
    [
        # A C++ helper beside the tests.
        ("tests/ai/brain_test.cpp", TEST_CPP, TEST_CPP + "static int hunger() { return 3; }\n"),
        # A Python fixture in a conftest, and a parametrize table.
        ("python/tests/conftest.py", "import pytest\n", "import pytest\n\n\n@pytest.fixture\ndef x():\n    return 1\n"),
        ("python/tests/test_x.py", "CASES = [1, 2]\n", "CASES = [1, 3]\n"),
        # A data file the tests read.
        ("tests/data/brain.json", "{}\n", '{"hunger": 3}\n'),
    ],
)
def test_a_delta_that_changes_test_helpers_fixtures_or_data_falls_back(
    rig: Rig, path: str, before: str, after: str
) -> None:
    fix(rig, {path: before}, diff=rig.diff)
    rig.stamp("fails-first", "brain thinks", detail={"stubs": []})
    pairs = task_review.build_packet(rig.env(), "full")[0]["pairs_set_sha256"]
    rig.stamp(
        "reviewed",
        covers={"diff_sha256": rig.diff, "pairs_set_sha256": pairs},
        detail={"verdict": "pass", "scope": "full", "functions": {"0x00100010": "c"}},
    )
    fix(rig, {path: after})
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is None
    assert any(reason.startswith("test-body:") and path in reason for reason in plan.reasons), plan.reasons


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


def test_a_recorded_verdict_the_stamp_does_not_cover_gives_no_base(rig: Rig) -> None:
    full = full_review(rig)
    recorded = rig.unit.dir / "review" / f"recorded-{full.seq}.json"
    data = json.loads(recorded.read_text(encoding="utf-8"))
    data["base"] = full.commit  # the reviewed tip as its own base would hide every line of the change
    recorded.write_text(json.dumps(data), encoding="utf-8")
    fix(rig, {"src/ai/util.cpp": "int helper() { return 1; }\n"})
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is not None
    assert plan.delta.since_base == git(rig.root, "rev-parse", "origin/main").strip()
    assert plan.delta.files == ("src/ai/util.cpp",)


def main_gains_rest(rig: Rig) -> str:
    """Main gains a second `@orig` function, which the branch does not change; the branch is rebased onto it. Returns
    the function's file."""
    git(rig.root, "checkout", "-q", "origin/main")
    rest = (
        '#include "ai/brain.h"\nnamespace coney::ai {\n/// Rests.\n/// @orig 0x00100040 Brain_Rest (Brain.cpp)\n'
        "int rest(int hours) {\n    return hours * 2;\n}\n} // namespace coney::ai\n"
    )
    (rig.root / "src/ai/rest.cpp").write_text(rest, encoding="utf-8")
    git(rig.root, "add", "-A")
    git(rig.root, "commit", "-q", "-m", "main adds rest")
    git(rig.root, "checkout", "-q", "feature")
    git(rig.root, "rebase", "-q", "origin/main")
    return rest


def test_a_function_that_moved_in_the_delta_needs_opus(rig: Rig) -> None:
    # The review passes; then the fix renames main's second function.
    rest = main_gains_rest(rig)
    full_review(rig)
    (rig.root / "src/ai/rest.cpp").write_text(rest.replace("int rest(", "int doze("), encoding="utf-8")
    git(rig.root, "commit", "-q", "-a", "-m", "rename rest")
    rig.diff = "2" * 64
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is not None, plan.reasons
    assert [f["coney"] for f in plan.delta.moved] == ["doze"]
    packet, problems = delta_packet(rig)
    assert problems == [], problems
    assert "orig" in packet["kinds"] and [m["coney"] for m in packet["moved"]] == ["doze"]
    assert task_review.required_models(packet["kinds"], rig.conventions.tasks.review) == ("opus",)


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
    assert stamp.detail["scope"] == "delta" and stamp.detail["kinds"] == ["code", "text", "commits"]
    # It names the delta rules it was planned under, which `pr verify` holds to the default branch's.
    rules = rig.conventions.tasks.review.delta
    assert rules is not None and stamp.covers["delta_rules_sha256"] == review_delta.rules_sha256(rules)
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


def test_a_delta_on_a_review_of_many_pages_carries_the_short_list_and_the_count(rig: Rig) -> None:
    pages = {f"docs/research/characters/system/the-character-page-number-{n:03d}.md": PAGE for n in range(60)}
    fix(rig, pages, diff="1" * 64)
    full = full_review(rig)
    assert full.detail["pages"] and full.detail["pages_left_out"] > 0
    fix(rig, {"src/ai/util.cpp": "int helper() { return 1; }\n"})
    assert record_delta(rig, SONNET) == 0
    stamp = stamps.verify(rig.unit.stamps, rig.rules).stamps[-1]
    # The delta's own keys (`since_seq`, `full_seq`) take room too, so it may keep one page fewer than the review below.
    kept = stamp.detail["pages"]
    assert stamp.detail["scope"] == "delta" and kept and kept == full.detail["pages"][: len(kept)]
    assert len(kept) + stamp.detail["pages_left_out"] == len(full.detail["pages"]) + full.detail["pages_left_out"]


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
    # It names the delta it rests on, so `pr verify` walks the stack down from it.
    delta = stamps.verify(rig.unit.stamps, rig.rules).stamps[-2]
    assert delta.detail["scope"] == "delta" and stamp.detail["since_seq"] == delta.seq
    assert stamp.detail["functions"] == {"0x00100010": "c"}


# --- what a delta may not leave out (review #9) ----------------------------------------------------------------------


def commit_bytes(rig: Rig, path: str, data: bytes, diff: str) -> None:
    """A commit on the branch that writes `data` to `path` as it is, and the change hash after it."""
    (rig.root / path).parent.mkdir(parents=True, exist_ok=True)
    (rig.root / path).write_bytes(data)
    git(rig.root, "add", "-A")
    git(rig.root, "commit", "-q", "-m", f"bytes of {path}")
    rig.diff = diff


def test_a_comment_edit_of_a_function_the_reviewed_change_moved_opens_the_orig_checklist(rig: Rig) -> None:
    # The reviewed change renamed main's function (a move: its compare stands); the fix then edits a comment inside it.
    rest = main_gains_rest(rig)
    moved = rest.replace("int rest(", "int doze(")
    fix(rig, {"src/ai/rest.cpp": moved}, diff=rig.diff)
    full_review(rig)
    fix(rig, {"src/ai/rest.cpp": moved.replace("    return hours", "    // Twice, as the original.\n    return hours")})
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is not None, plan.reasons
    # It moved in the reviewed change, not in the delta, but its text changed: it is judged again.
    assert plan.delta.moved == ()
    assert [f["coney"] for f in plan.delta.functions] == ["doze"]
    packet, problems = delta_packet(rig)
    assert problems == [], problems
    assert "orig" in packet["kinds"] and [f["coney"] for f in packet["functions"]] == ["doze"]
    assert task_review.required_models(packet["kinds"], rig.conventions.tasks.review) == ("opus",)


WRAPPED = PAGE + (
    "\nBrain_Wait waits 3 frames before the next thought, as the loop that counts them at\n"
    "0x00100020 reads it (confirmed (code)).\n"
)


def test_a_wrapped_claim_changed_in_a_delta_keeps_the_address_minimum(rig: Rig) -> None:
    # The value is on one line, its address and evidence level on the next: the line alone holds no claim pattern,
    # so the delta stays a delta, and the research page in it is held to the full review's address minimum.
    page = "docs/research/ai/brain.md"
    fix(rig, {page: WRAPPED}, diff=rig.diff)
    full_review(rig)
    fix(rig, {page: WRAPPED.replace("3 frames", "4 frames")})
    packet, problems = delta_packet(rig)
    assert problems == [], problems
    assert "research" in packet["kinds"] and [p["path"] for p in packet["pages"]] == [page]
    verdict = make_verdict(packet)
    verdict["pages"][0]["checked"] = []
    rig.verdict_path().write_text(json.dumps(verdict), encoding="utf-8")
    assert task_review.record(rig.env(), review_delta.DELTA) == 1
    assert record_delta(rig) == 0


def test_binaries_that_differ_only_in_bytes_that_are_not_utf8_still_differ(rig: Rig) -> None:
    commit_bytes(rig, "assets/brain.bin", b"\x89BIN\x80\n", rig.diff)
    commit_bytes(rig, "tests/pictures/reference/brain.png", b"\x89PNG\x80\n", rig.diff)
    full_review(rig)
    commit_bytes(rig, "assets/brain.bin", b"\x89BIN\x81\n", "2" * 64)
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is not None and plan.delta.files == ("assets/brain.bin",), plan
    # The same under the test paths: a reference picture is what a test compares with.
    commit_bytes(rig, "tests/pictures/reference/brain.png", b"\x89PNG\x81\n", "3" * 64)
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is None
    assert any(r.startswith("test-body:") and "brain.png" in r for r in plan.reasons), plan.reasons


def test_a_delta_that_drops_the_orig_tags_of_a_function_falls_back(rig: Rig) -> None:
    full_review(rig)
    header = (rig.root / "src/ai/brain.h").read_text(encoding="utf-8")
    fix(rig, {"src/ai/brain.h": header.replace("    /// @orig 0x00100010 Brain_Think (Brain.cpp)\n", "")})
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is None
    assert (
        "orig-body: the delta drops the @orig function or its tags: Brain::think (src/ai/brain.cpp:3)" in plan.reasons
    )


def test_an_edit_far_down_a_long_declaration_reopens_the_function(rig: Rig) -> None:
    header = (rig.root / "src/ai/brain.h").read_text(encoding="utf-8")
    long = header.replace("int think(int hunger);", "int think(" + "\n" * 12 + "        int hunger = 3);")
    fix(rig, {"src/ai/brain.h": long}, diff=rig.diff)
    full_review(rig)
    fix(rig, {"src/ai/brain.h": long.replace("= 3", "= 4")})
    plan = task_review.delta_plan(rig.env())
    assert plan.delta is not None, plan.reasons
    assert [f["coney"] for f in plan.delta.functions] == ["Brain::think"]


def test_without_delta_rules_on_main_every_review_is_full(rig: Rig) -> None:
    full_review(rig)
    fix(rig, {"src/ai/util.cpp": "int helper() { return 1; }\n"})
    plan = task_review.delta_plan(rig.env(delta_rules=None))
    assert plan.delta is None and plan.reasons == ("the default branch's conventions have no tasks.review.delta",)
    # Rules that could not be read say why.
    plan = task_review.delta_plan(rig.env(delta_rules=None, delta_note="origin/HEAD is not set here"))
    assert plan.reasons == ("the default branch's delta rules cannot be read: origin/HEAD is not set here",)


def test_the_delta_rules_are_the_default_branchs_whatever_the_branch_sets(tmp_path: Path) -> None:
    root = tmp_path / "checkout"
    (root / ".github").mkdir(parents=True)

    def main_has(data: dict[str, Any]) -> None:
        """Commit `data` as main's conventions and point origin/main and origin/HEAD at it."""
        (root / CONVENTIONS_FILE).write_text(yaml.safe_dump(data, sort_keys=False), encoding="utf-8")
        git(root, "add", "-A")
        git(root, "commit", "-q", "-m", "rules")
        git(root, "update-ref", "refs/remotes/origin/main", "HEAD")
        git(root, "symbolic-ref", "refs/remotes/origin/HEAD", "refs/remotes/origin/main")

    git(root, "init", "-q", "-b", "main")
    main_has(changed())
    # The branch's own file empties full_when and drops its test paths; main's rules are read all the same.
    own = parse_conventions(changed(("tasks.review.delta.full_when", []), ("tasks.review.delta.test_paths", ["docs/"])))
    rules, note = task_review.delta_rules_of(root, own)
    assert rules is not None and note == ""  # no note: the rules were read
    assert rules.full_when == ("orig-body", "research-claim", "test-body") and "tests/" in rules.test_paths
    main_has(changed(("tasks.review.delta", REMOVE)))
    rules, note = task_review.delta_rules_of(root, own)
    assert rules is None and note == ""
