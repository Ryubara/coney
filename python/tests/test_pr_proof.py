# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for steps 1-4 of the release: the log, Check, the compared functions and Review, each refusal with its text."""

import dataclasses
import hashlib
import json
import os
import subprocess
from datetime import UTC, datetime
from pathlib import Path
from types import SimpleNamespace
from typing import cast

import pytest
from conventions_support import REMOVE, changed
from git_support import git
from pr_support import (
    ENGINEER,
    NOW,
    PARAGRAPH,
    SOURCE_FILE,
    TEST_FILE,
    TEST_NAME,
    UNVERIFIED,
    Built,
    agent,
    commit_all,
    make,
    sha,
    write,
)
from review_support import make_packet, make_verdict
from test_orig_compare import SOURCE, TWO_FUNCTIONS
from test_orig_study import ADDRESS

from coney_tools import build_record, orig_copy, orig_pairs, orig_review, pr_diff, pr_proof, review_verdict, stamps
from coney_tools import conventions as conventions_module
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions, parse_conventions


def texts(result: pr_proof.ProofResult, stage: str | None = None) -> list[str]:
    """The refusals' texts, of one stage or of all."""
    return [r.text for r in result.refusals if stage is None or r.stage == stage]


def redo(result: pr_proof.ProofResult, start: str) -> list[str]:
    """What to redo for the refusals whose text starts with `start`."""
    return [r.redo for r in result.refusals if r.text.startswith(start)]


def with_rules(built: Built, *changes: tuple[str, object]) -> Conventions:
    """The conventions with changes (to switch a flag on)."""
    return parse_conventions(changed(*changes))


def test_a_complete_unit_passes(tmp_path: Path) -> None:
    built = make(tmp_path)
    result = built.verify()
    assert texts(result) == [] and result.ok
    assert result.subject.diff_sha256 == built.diff and result.subject.profile == "full"
    assert [u.coney for u in result.subject.changed] == ["Widget::update"]


@pytest.mark.parametrize("shape", ["docs", "tooling"])
def test_a_docs_or_tooling_unit_passes_with_a_none_fails_first_and_no_build_record(tmp_path: Path, shape: str) -> None:
    built = make(tmp_path, shape=shape)
    assert texts(built.verify()) == []
    assert not (built.unit.dir / "build-runs").exists()
    assert built.subject().profile == shape


def test_a_change_that_touches_no_source_file_reads_no_source(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """Reading every `src/` file at two commits takes seconds; a guide or a tool cannot add an `@orig` function."""
    built = make(tmp_path, shape="tooling")

    def scan(*args: object) -> None:
        raise AssertionError("the source was scanned")

    monkeypatch.setattr(orig_review, "units_at", scan)
    subject = built.subject()
    assert subject.changed == () and subject.removed == () and texts(built.verify()) == []


def test_a_change_to_a_header_or_a_deleted_source_file_is_scanned(tmp_path: Path) -> None:
    built = make(tmp_path)
    assert [u.coney for u in built.subject().changed] == ["Widget::update"]
    assert pr_proof.changed_functions(built.root, built.base, built.tip, ["src/ai/widget.h"])[0] != ()
    assert pr_proof.changed_functions(built.root, built.base, built.tip, ["docs/guides/widget.md", "src/a.txt"]) == (
        (),
        (),
        {},
    )


def test_the_stamp_log_alone_refuses_a_missing_or_failed_check_and_review(tmp_path: Path) -> None:
    """`pr_proof.unsettled` is what Conclave asks before it reads git; it says what `verify` says."""
    built = make(tmp_path, shape="tooling", skip=frozenset({"checked", "reviewed"}))
    assert [r.text for r in pr_proof.unsettled(built.subject().report)] == [
        "checked: no passing Check; run task check",
        "reviewed: not reviewed",
    ]
    assert {r.text for r in pr_proof.unsettled(built.subject().report)} <= set(texts(built.verify()))
    built.check("tooling", checks=[])
    built.review(verdict="fail", blockers=1)
    assert [r.text for r in pr_proof.unsettled(built.subject().report)] == [
        "reviewed: Review failed (1 blocking finding, see the verdict)"
    ]


def test_a_unit_whose_stamps_all_pass_is_not_unsettled(tmp_path: Path) -> None:
    assert pr_proof.unsettled(make(tmp_path, shape="tooling").subject().report) == []


# --- step 1: the log ------------------------------------------------------------------------------------------------


def test_an_edited_log_line_is_refused_and_nothing_else_is_judged(tmp_path: Path) -> None:
    built = make(tmp_path)
    segment = built.unit.stamps / "0001.jsonl"
    lines = segment.read_text(encoding="utf-8").splitlines()
    lines[3] = lines[3].replace('"pass"', '"fail"', 1)
    # Bytes, not write_text: text mode writes CRLF on Windows, which makes every line non-canonical there.
    segment.write_bytes(("\n".join(lines) + "\n").encode("utf-8"))
    result = built.verify()
    # Editing line 4 breaks the link of the stamp after it, so the log names line 5.
    assert any(t.startswith("log: 0001.jsonl:5: ") and "edited" in t for t in texts(result)), texts(result)
    assert {r.stage for r in result.refusals} == {"log"}
    assert redo(result, "log: 0001.jsonl:5")[0] == "none"


def test_a_missing_log_is_refused_with_task_start(tmp_path: Path) -> None:
    built = make(tmp_path)
    for segment in built.unit.stamps.glob("*.jsonl"):
        segment.unlink()
    result = built.verify()
    assert texts(result) == [f"log: {built.task} has no stamps; run task start"]


def test_a_stamp_without_the_hook_identity_is_refused_only_when_the_flag_is_on(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"checked"}))
    built.stamp(
        "checked",
        covers={"diff_sha256": built.diff, "build_record_sha256": built.build_record()},
        detail=_checked(built),
        who=UNVERIFIED,
    )
    assert texts(built.verify()) == []
    strict = with_rules(built, ("stamps.require_hook_source", True))
    refused = texts(built.verify(conventions=strict))
    assert any(
        t.startswith("log: seq ") and "checked" in t and "without the hook identity (source unverified)" in t
        for t in refused
    )
    assert any(r.redo == "task check" for r in built.verify(conventions=strict).refusals)


def _checked(built: Built) -> dict[str, object]:
    """The detail of a passing full Check."""
    return {
        "profile": "full",
        "checks": list(built.conventions.proof.profiles[2].checks),
        "tests": {"total": 412, "passed": 412, "failed": 0, "skipped": 0, "skipped_disc": 0},
        "preset": "ci",
    }


def test_a_superseded_unverified_stamp_does_not_refuse_the_unit(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"checked"}))
    built.stamp("checked", result="fail", covers={}, detail={}, who=UNVERIFIED)
    built.check()
    strict = with_rules(built, ("stamps.require_hook_source", True))
    assert texts(built.verify(conventions=strict)) == []


def test_a_note_that_repeats_a_line_of_the_original_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path)
    file = orig_pairs.load(built.pairs_path)
    pairs = list(file.pairs)
    pairs[2] = dataclasses.replace(pairs[2], note="the check is if (param_2 < iVar1) { in the original")
    file.pairs = pairs
    orig_pairs.write(built.pairs_path, file)
    refused = texts(built.verify())
    assert any(
        t == "log: mapping/0x00501000.toml pair 3 note repeats a line of the original; say it in your own words"
        for t in refused
    ), refused


def test_a_log_that_does_not_start_with_assigned_and_claimed_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path, start=False)
    refused = texts(built.verify())
    assert any(t.startswith("log: the log does not start with assigned and claimed") for t in refused), refused


def test_a_stamp_dated_before_its_commit_or_in_the_future_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path)
    env = {
        **os.environ,
        "GIT_AUTHOR_DATE": "2030-01-01T00:00:00+0000",
        "GIT_COMMITTER_DATE": "2030-01-01T00:00:00+0000",
    }
    write(built.root, "src/ai/later.cpp", "// later\n")
    subprocess.run(["git", "-C", str(built.root), "add", "-A"], check=True)
    subprocess.run(
        ["git", "-C", str(built.root), "-c", "user.name=T", "-c", "user.email=t@e.org", "commit", "-q", "-m", "later"],
        check=True,
        env=env,
    )
    late = built.tip
    built.stamp("finished", commit=late)  # 2026, before the commit's 2030
    refused = texts(built.verify())
    assert any("is before its commit" in t and late[:12] in t for t in refused), refused
    built.clock.now = datetime(2031, 1, 1, tzinfo=UTC)
    built.stamp("finished")
    assert any("is in the future" in t for t in texts(built.verify()))


# --- step 2: Check ----------------------------------------------------------------------------------------------------


def test_no_check_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"checked"}))
    result = built.verify()
    assert texts(result, "checked") == ["checked: no passing Check; run task check"]
    assert redo(result, "checked:") == ["task check"]


def test_a_failed_newest_check_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path)
    built.stamp("checked", result="fail", covers={"diff_sha256": built.diff}, detail={"profile": "full"})
    assert texts(built.verify(), "checked") == [
        "checked: the newest Check failed; fix what it found and run task check"
    ]


def test_a_check_goes_stale_after_a_source_edit_and_names_the_diffs(tmp_path: Path) -> None:
    built = make(tmp_path)
    before = built.diff
    built.edit("src/ai/placeholder.cpp", "// placeholder changed\n")
    [line] = texts(built.verify(), "checked")
    assert line == (f"checked: stale: the branch changed since Check (diff {before[:8]} now {built.diff[:8]})")


def test_a_check_stamped_against_a_later_base_is_stale(tmp_path: Path) -> None:
    # `task check --base <a branch commit>` hashes only the commits after that base; only the hash against the merge
    # base with main is the change that merges, so `pr verify` refuses the stamp however its rules say they were read.
    built = make(tmp_path)
    earlier = built.tip
    built.edit("src/ai/placeholder.cpp", "// a later commit\n")
    partial = pr_diff.change_sha256(built.root, earlier, "HEAD")
    assert partial != built.diff
    old = built.subject().report.latest("checked")
    assert old is not None
    built.stamp("checked", covers={**old.covers, "diff_sha256": partial}, detail=dict(old.detail))
    [line] = texts(built.verify(), "checked")
    assert line == f"checked: stale: the branch changed since Check (diff {partial[:8]} now {built.diff[:8]})"


def test_a_check_of_a_smaller_profile_than_the_change_needs_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"checked"}))
    built.check("docs")
    [line] = texts(built.verify(), "checked")
    assert line.startswith("checked: Check ran profile docs, this change needs full (touches ")
    assert SOURCE_FILE in line or TEST_FILE in line


def test_a_check_that_did_not_run_a_listed_check_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"checked"}))
    full = list(next(p.checks for p in built.conventions.proof.profiles if p.name == "full"))
    built.check("full", checks=[check for check in full if check != "ctest-full-disc"])
    assert texts(built.verify(), "checked") == ["checked: Check did not run: ctest-full-disc"]


def test_a_missing_or_changed_build_record_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"checked"}))
    record = built.build_record()
    built.check("full", record=record)
    path = built.unit.dir / build_record.FOLDER / f"{record}.json"
    path.write_text(path.read_text(encoding="utf-8") + " ", encoding="utf-8")
    [line] = texts(built.verify(), "checked")
    assert line.startswith(f"checked: the build record {record[:12]} cannot be read: ")
    assert "does not hash to its name" in line
    path.unlink()
    [line] = texts(built.verify(), "checked")
    assert line.startswith(f"checked: the build record {record[:12]} cannot be read: ")
    assert "is not in" in line


@pytest.mark.parametrize(
    ("more", "message"),
    [
        (
            {"filters": ["-R", "widget"]},
            "checked: the build record is a filtered run (-R widget); Check needs the full suite",
        ),
        ({"preset": "dev"}, "checked: the build record is preset dev, Check needs ci"),
        ({"exit_code": 1}, "checked: the build record's run failed (exit 1)"),
        (
            {"tests": {"total": 412, "passed": 371, "failed": 0, "skipped": 41, "skipped_disc": 41}},
            "checked: 41 disc tests skipped; Check needs the disc (CONEY_DISC)",
        ),
        (
            {"tests": {"total": 412, "passed": 411, "failed": 1, "skipped": 0, "skipped_disc": 0}},
            "checked: 1 test failed",
        ),
        (
            {"tests": {"total": 0, "passed": 0, "failed": 0, "skipped": 0, "skipped_disc": 0}},
            "checked: no tests ran",
        ),
        ({"ran": ["configure", "build"]}, "checked: the build record has no test run"),
        ({"tests": None}, "checked: the build record has no test run"),
        ({"dirty": True}, "checked: the build record was made from a worktree with uncommitted changes"),
        ({"tip": "ab" * 20}, "checked: the build record is for another commit (ababababab, the tip is "),
    ],
)
def test_a_build_record_that_is_not_a_full_ci_run_is_refused(
    tmp_path: Path, more: dict[str, object], message: str
) -> None:
    built = make(tmp_path, skip=frozenset({"checked"}))
    built.check("full", record=built.build_record(**more))
    refused = texts(built.verify(), "checked")
    assert any(t.startswith(message) for t in refused), refused


def test_a_build_record_of_another_version_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"checked"}))
    record = build_record.make(**built.record_fields())
    record["record_version"] = None
    built.check("full", record=build_record.write(built.unit.dir / build_record.FOLDER, record).stem)
    refused = texts(built.verify(), "checked")
    assert any(t.startswith("checked: the build record has version None") for t in refused), refused


def test_a_build_record_whose_log_was_edited_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"checked"}))
    built.check("full", record=built.build_record())
    assert texts(built.verify(), "checked") == []
    (built.unit.dir / "build-logs" / "ci.log").write_bytes(b"another log")
    assert texts(built.verify(), "checked") == [
        "checked: the log ci.log differs from the hash the build record kept (it was edited)"
    ]


def test_a_record_whose_counts_differ_from_the_stamps_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"checked"}))
    built.check("full", tests={"total": 400, "passed": 400, "failed": 0, "skipped": 0, "skipped_disc": 0})
    assert texts(built.verify(), "checked") == ["checked: the build record's test counts differ from the Check stamp's"]


# --- step 3: the compared functions ----------------------------------------------------------------------------------


def test_a_changed_function_that_was_never_studied_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"study", "compare"}))
    result = built.verify()
    assert "compared: Widget::update (0x00501000) was changed and never studied" in texts(result, "compared")
    assert redo(result, "compared: Widget::update") == ["orig study 0x00501000"]


def test_a_study_whose_files_were_edited_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path)
    saved = built.unit.study(ADDRESS) / "decompile.c"
    saved.write_bytes(saved.read_bytes().replace(b"iVar1", b"iVar9"))
    refused = texts(built.verify(), "compared")
    assert any(t.startswith("compared: 0x00501000: ") and "decompile.c" in t for t in refused), refused


def test_pairs_with_a_line_in_no_pair_are_refused(tmp_path: Path) -> None:
    built = make(tmp_path)
    file = orig_pairs.load(built.pairs_path)
    file.pairs = file.pairs[:-1]
    orig_pairs.write(built.pairs_path, file)
    result = built.verify()
    assert any(t.startswith("compared: 0x00501000: ") and "is in no pair" in t for t in texts(result, "compared"))
    assert "orig compare 0x00501000" in redo(result, "compared: 0x00501000")


def test_a_body_edited_after_it_was_compared_is_refused_with_both_hashes(tmp_path: Path) -> None:
    built = make(tmp_path)
    built.edit(SOURCE_FILE, SOURCE.replace("return 2;", "return 3;"))
    refused = texts(built.verify(), "compared")
    assert any(
        t.startswith("compared: 0x00501000: Widget::update changed after it was compared (body ") and " now " in t
        for t in refused
    ), refused


def test_a_comment_edited_after_it_was_compared_is_not_a_change(tmp_path: Path) -> None:
    built = make(tmp_path)
    note = "// stand-in: the sound plays later"
    built.edit(SOURCE_FILE, SOURCE.replace(note, note + " (see the page)"))
    assert texts(built.verify(), "compared") == []


def test_a_compare_made_under_the_old_body_hash_still_covers_the_unchanged_function(tmp_path: Path) -> None:
    built = make(tmp_path)
    last = stamps.verify(built.unit.stamps, built.conventions.stamps).latest("compared", ADDRESS)
    assert last is not None
    lines = SOURCE.splitlines()[3:15]
    old = hashlib.sha256(chr(10).join(line.rstrip() for line in lines).encode("utf-8")).hexdigest()
    built.stamp("compared", subject=ADDRESS, covers={**last.covers, "coney_body_sha256": old})
    assert texts(built.verify(), "compared") == []
    built.edit(SOURCE_FILE, SOURCE.replace("return 2;", "return 3;"))
    assert any("changed after it was compared" in t for t in texts(built.verify(), "compared"))


def cite_twice(built: Built) -> None:
    """Commit a second Coney function that carries the same `@orig` address as Widget::update."""
    built.edit(SOURCE_FILE, TWO_FUNCTIONS)


def test_two_functions_of_one_address_are_accepted_when_each_was_compared(tmp_path: Path) -> None:
    built = make(tmp_path)
    cite_twice(built)
    built.compare(key="Widget::reset()")  # the stamp for Widget::update came with the unit; this one is the second's
    assert texts(built.verify(), "compared") == []


def test_a_subject_lists_every_function_citing_an_address_and_a_scan_adds_it(tmp_path: Path) -> None:
    """`citing` tells a `compared` stamp's function from its sibling; `scan=False` leaves it out, like `changed`."""
    built = make(tmp_path)
    cite_twice(built)
    assert built.subject().citing == {ADDRESS: ("Widget::update(int)", "Widget::reset()")}
    unscanned = built.subject(scan=False)
    assert unscanned.citing == {} and pr_proof.scanned(unscanned).citing == built.subject().citing


def test_the_second_function_of_one_address_is_refused_until_it_is_compared_itself(tmp_path: Path) -> None:
    built = make(tmp_path)
    cite_twice(built)
    refused = texts(built.verify(), "compared")
    assert refused == ["compared: 0x00501000: Widget::reset was never compared"], refused


def test_a_function_compared_before_a_sibling_changed_stays_compared(tmp_path: Path) -> None:
    built = make(tmp_path)
    cite_twice(built)
    built.compare(key="Widget::reset()")
    built.edit(SOURCE_FILE, TWO_FUNCTIONS.replace("count_ = 0;", "count_ = 1;"))
    refused = texts(built.verify(), "compared")
    assert len(refused) == 1 and refused[0].startswith("compared: 0x00501000: Widget::reset changed after"), refused


def test_a_pairs_file_edited_after_it_was_compared_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path)
    file = orig_pairs.load(built.pairs_path)
    pairs = list(file.pairs)
    pairs[2] = dataclasses.replace(pairs[2], note="the sound plays after the counter")
    file.pairs = pairs
    orig_pairs.write(built.pairs_path, file)
    refused = texts(built.verify(), "compared")
    assert "compared: 0x00501000: the pairs file changed after it was compared" in refused


def test_a_copy_check_finding_in_the_functions_file_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path)
    built.findings = [orig_copy.Finding(SOURCE_FILE, 8, "ghidra-name", "iVar3")]
    elsewhere = orig_copy.Finding("src/ai/placeholder.cpp", 1, "ghidra-name", "iVar4")
    built.findings.append(elsewhere)
    refused = texts(built.verify(), "compared")
    assert len(refused) == 1 and refused[0].startswith(f"compared: {SOURCE_FILE}:8: ")
    assert refused[0].endswith("(copy-check)")


def test_a_function_with_two_addresses_needs_both_compared(tmp_path: Path) -> None:
    built = make(tmp_path)
    second = SOURCE.replace(
        "    // stand-in: the sound plays later",
        "    // stand-in: the sound plays later @orig 0x00501080 Widget_Other (Widget.cpp)",
    )
    built.edit(SOURCE_FILE, second)
    refused = texts(built.verify(), "compared")
    assert any("0x00501080" in t and "never studied" in t for t in refused), refused


def test_a_removed_orig_function_is_a_note_not_a_refusal(tmp_path: Path) -> None:
    built = make(tmp_path)
    git(built.root, "checkout", "-q", "main")
    write(built.root, "src/ai/old.cpp", "// @orig 0x00509999 Old_Thing (Old.cpp)\nint oldThing() { return 1; }\n")
    commit_all(built.root, "old")
    git(built.root, "push", "-q", "origin", "main")
    git(built.root, "checkout", "-q", "work")
    git(built.root, "rebase", "-q", "main")
    git(built.root, "rm", "-q", "src/ai/old.cpp")
    commit_all(built.root, "remove old")
    result = built.verify()
    assert any("Old_Thing" in note or "oldThing" in note for note in result.notes), result.notes


# --- fails first ------------------------------------------------------------------------------------------------------


def test_a_test_added_without_a_fails_first_stamp_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path)
    text = (built.root / TEST_FILE).read_text(encoding="utf-8")
    built.edit(TEST_FILE, text + 'TEST_CASE("Widget other") {}\n')
    refused = texts(built.verify(), "fails-first")
    assert 'fails-first: "Widget other" was added and has no passing fails-first stamp' in refused
    assert any(t == f'fails-first: "{TEST_NAME}": its test file changed since ({TEST_FILE})' for t in refused), refused


def test_a_gameplay_change_with_no_test_and_only_a_none_stamp_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path, shape="no-test")
    result = built.verify()
    assert texts(result, "fails-first") == [
        "fails-first: a feature or fix under src/ai/ needs a test that failed first, or --none with a reason"
    ]
    assert redo(result, "fails-first:") == ["orig fails-first"]


def test_a_docs_unit_without_any_fails_first_stamp_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path, shape="docs", skip=frozenset({"fails-first"}))
    refused = texts(built.verify(), "fails-first")
    assert len(refused) == 1 and "--none" in refused[0]


# --- step 4: Review ---------------------------------------------------------------------------------------------------


def test_no_review_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"reviewed"}))
    result = built.verify()
    assert texts(result, "reviewed") == ["reviewed: not reviewed"]
    assert redo(result, "reviewed:") == ["task review"]


def test_a_failed_review_is_refused_with_its_blocking_findings(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"reviewed"}))
    built.stamp("reviewed", result="fail", covers={"diff_sha256": built.diff}, detail={"blockers": 2})
    assert texts(built.verify(), "reviewed") == ["reviewed: Review failed (2 blocking findings, see the verdict)"]


def test_a_review_goes_stale_when_the_branch_the_paragraph_or_the_pairs_change(tmp_path: Path) -> None:
    built = make(tmp_path)
    before = built.diff
    built.edit("src/ai/placeholder.cpp", "// changed\n")
    assert f"reviewed: stale: the branch changed since Review (diff {before[:8]} now {built.diff[:8]})" in texts(
        built.verify()
    )
    built = make(tmp_path / "b")
    path = built.unit.dir / "PR.md"
    path.write_text(path.read_text(encoding="utf-8").replace(PARAGRAPH, PARAGRAPH + " It also logs."), encoding="utf-8")
    result = built.verify()
    assert "reviewed: stale: the summary paragraph changed since Review" in texts(result)
    assert redo(result, "reviewed: stale: the summary") == ["task review --text"]
    built = make(tmp_path / "c")
    file = orig_pairs.load(built.pairs_path)
    pairs = list(file.pairs)
    pairs[2] = dataclasses.replace(pairs[2], note="changed")
    file.pairs = pairs
    orig_pairs.write(built.pairs_path, file)
    assert "reviewed: stale: the pairs changed since Review" in texts(built.verify())


def test_a_review_that_judged_a_function_bad_or_skipped_it_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"reviewed"}))
    built.review(functions={ADDRESS: "diff-bug"})
    assert texts(built.verify(), "reviewed") == ["reviewed: 0x00501000 was judged diff-bug"]
    built = make(tmp_path / "b", skip=frozenset({"reviewed"}))
    built.review(functions={})
    assert texts(built.verify(), "reviewed") == ["reviewed: Review did not cover 0x00501000"]
    built = make(tmp_path / "c", skip=frozenset({"reviewed"}))
    built.review(functions={ADDRESS: "diff-deliberate-ok"})
    assert texts(built.verify(), "reviewed") == []


def test_a_review_of_an_xbox_only_function_is_held_to_its_xbox_address(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"reviewed"}))
    shadow = (
        "namespace coney::render {\n/// Draws the shadows the Xbox port adds.\n"
        "/// @orig xbox 0x000bd3c0 Shadow_Draw (Shadow.cpp)\nvoid draw() {}\n} // namespace coney::render\n"
    )
    built.edit("src/render/shadow.cpp", shadow)
    units = built.subject().changed
    xbox = next(u for u in units if u.xbox)
    assert xbox.review_addresses == ["xbox 0x000bd3c0"]
    # The packet lists the changed functions as `to_json` writes them; the verdict and the stamp name each by
    # `review_addresses`, so the spelling the reviewer writes is the one `check_reviewed` reads back.
    packet = make_packet(functions=[u.to_json() for u in units])
    detail = review_verdict.stamp_detail(make_verdict(packet))
    assert {*detail["functions"]} == {a for u in units for a in u.review_addresses}
    built.review(functions=detail["functions"])
    assert texts(built.verify(), "reviewed") == []
    built.review(functions={ADDRESS: "clean"})
    assert texts(built.verify(), "reviewed") == ["reviewed: Review did not cover xbox 0x000bd3c0"]


def test_a_reviewer_who_also_worked_on_the_task_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"reviewed"}))
    built.review(reviewer=ENGINEER["agent"])
    assert texts(built.verify(), "reviewed") == ["reviewed: reviewer engineer-1 also worked on this task"]


def test_a_reviewer_who_wrote_any_other_stamp_is_refused_whatever_its_type(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"reviewed"}))
    checked = built.subject().report.latest("checked")
    assert checked is not None
    tooling = agent("tooling", 3)
    built.stamp("checked", covers=dict(checked.covers), detail=dict(checked.detail), who=tooling)
    built.review(reviewer=tooling["agent"])
    assert texts(built.verify(), "reviewed") == ["reviewed: reviewer tooling-3 also worked on this task"]


def test_a_review_stamp_that_names_no_reviewer_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"reviewed"}))
    built.review(reviewer={"type": "reviewer", "model": "claude-opus-5-5"})
    assert texts(built.verify(), "reviewed") == ["reviewed: the Review stamp names no reviewer"]


def test_the_reviewers_type_and_model_are_held_to_the_rules_only_when_the_flag_is_on(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"reviewed"}))
    built.review(reviewer={"id": "reviewer-2", "type": "engineer", "model": "claude-haiku-5"})
    assert texts(built.verify(), "reviewed") == []
    strict = with_rules(built, ("stamps.require_hook_source", True))
    refused = texts(built.verify(conventions=strict), "reviewed")
    assert "reviewed: the reviewer's type is engineer, not reviewer" in refused
    assert any("a review of orig work must run on opus" in t and "claude-haiku-5" in t for t in refused), refused
    built.review(reviewer={"id": "reviewer-2", "type": "reviewer", "model": "claude-opus-5-5"})
    assert texts(built.verify(conventions=strict), "reviewed") == []


def test_a_change_to_research_pages_needs_the_pages_graded(tmp_path: Path) -> None:
    built = make(tmp_path, shape="docs", skip=frozenset({"reviewed"}))
    built.edit("docs/research/widget.md", "# Widget\n")
    built.check("docs")
    built.review(functions={})
    assert texts(built.verify(), "reviewed") == [
        "reviewed: Review did not grade the research pages this change touches"
    ]
    built = make(tmp_path / "b", shape="docs", skip=frozenset({"reviewed", "checked"}))
    built.edit("docs/research/widget.md", "# Widget\n")
    built.check("docs")
    built.review(functions={}, pages=["docs/research/widget.md"])
    assert texts(built.verify()) == []


# --- the whole -------------------------------------------------------------------------------------------------------


def test_every_problem_is_reported_not_the_first(tmp_path: Path) -> None:
    built = make(tmp_path)
    built.edit(SOURCE_FILE, SOURCE.replace("return 2;", "return 3;"))
    refused = texts(built.verify())
    stages = {r.stage for r in built.verify().refusals}
    assert {"checked", "compared", "reviewed"} <= stages
    assert len(refused) >= 3


def test_a_stamp_commit_that_a_rebase_made_unreachable_is_accepted(tmp_path: Path) -> None:
    built = make(tmp_path)
    old_tip = built.tip
    git(built.root, "checkout", "-q", "main")
    write(built.root, "docs/other.md", "# other\n")
    commit_all(built.root, "main moves")
    git(built.root, "push", "-q", "origin", "main")
    git(built.root, "checkout", "-q", "work")
    git(built.root, "rebase", "-q", "main")
    assert built.tip != old_tip
    reachable = subprocess.run(
        ["git", "-C", str(built.root), "merge-base", "--is-ancestor", old_tip, "HEAD"], check=False
    ).returncode
    assert reachable != 0
    assert texts(built.verify()) == []


def test_an_unclean_worktree_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path)
    write(built.root, SOURCE_FILE, SOURCE + "// uncommitted\n")
    result = built.verify()
    assert texts(result)[0].startswith("worktree: uncommitted changes: src/ai/widget.cpp")
    assert redo(result, "worktree:") == ["engineer"]


def test_the_hashes_of_the_paragraph_title_and_images_are_stable(tmp_path: Path) -> None:
    built = make(tmp_path)
    one = built.text_hashes()
    assert one == built.text_hashes() and one.paragraph_sha256 == sha(PARAGRAPH)
    assert NOW.year == 2026
    assert json.loads(json.dumps(dataclasses.asdict(one))) == dataclasses.asdict(one)
    assert agent("reviewer")["agent"]["id"] == "reviewer-1"


# --- where the rules come from ---------------------------------------------------------------------------------------


def test_the_rules_are_read_from_origins_default_branch_whatever_the_branch_names(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    built = make(tmp_path)
    # The branch names another default branch, points a ref of that name at its own tip and adds a remote of its own.
    path = built.root / ".github" / "conventions.yml"
    built.edit(
        ".github/conventions.yml",
        path.read_text(encoding="utf-8").replace("default_branch: main", "default_branch: lax"),
    )
    git(built.root, "update-ref", "refs/remotes/origin/lax", "HEAD")
    git(built.root, "remote", "add", "elsewhere", str(built.origin))
    git(built.root, "fetch", "-q", "elsewhere")
    git(built.root, "update-ref", "refs/remotes/elsewhere/main", "HEAD")
    seen: list[str] = []
    real = conventions_module.load_release_rules_at

    def spy(root: Path, ref: str, own: Conventions) -> tuple[Conventions, str]:
        seen.append(ref)
        return real(root, ref, own)

    monkeypatch.setattr(pr_proof, "load_release_rules_at", spy)
    subject = pr_proof.load_subject(built.root, built.unit, built.task, built.kind, remote="elsewhere")
    assert seen == ["origin/main"] and subject.rules_from == "base"
    assert subject.remote_ref == "elsewhere/main"


#: One edit per section that decides whether a release is allowed, as a branch could make it to its own file: the
#: section's path in `Conventions`, and a change to a key in it that the real file does not already have.
RELEASE_SECTIONS = [
    pytest.param(("repo",), ("repo.default_branch", "sneaky"), id="repo"),
    pytest.param(("paths",), ("paths.large_file_kb", 99999), id="paths"),
    pytest.param(("commits",), ("commits.title.max_length", 200), id="commits"),
    pytest.param(("pull_requests",), ("pull_requests.runtime.areas", ["graphics", "audio"]), id="pull_requests"),
    pytest.param(("merge",), ("merge.auto_merge", False), id="merge"),
    pytest.param(("claims",), ("claims.bot_login", "sneaky[bot]"), id="claims"),
    pytest.param(("tasks", "review"), ("tasks.review.reverse_engineering_models", ["opus", "haiku"]), id="review"),
    pytest.param(("stamps",), ("stamps.require_hook_source", True), id="stamps"),
    pytest.param(("proof",), ("proof.build.preset", "dev"), id="proof"),
]


def section_of(rules: Conventions, path: tuple[str, ...]) -> object:
    """The part of `rules` that `path` names (`("tasks", "review")` is `rules.tasks.review`)."""
    value: object = rules
    for part in path:
        value = getattr(value, part)
    return value


@pytest.mark.parametrize(("path", "edit"), RELEASE_SECTIONS)
def test_a_section_that_decides_a_release_is_the_default_branchs_whatever_the_branch_edits(
    tmp_path: Path, path: tuple[str, ...], edit: tuple[str, object]
) -> None:
    built = make(tmp_path)
    built.edit_rules(edit)
    own = conventions_module.load_conventions(built.root)
    assert section_of(own, path) != section_of(built.conventions, path), "the edit must change the section"
    subject = built.subject()
    assert section_of(subject.conventions, path) == section_of(built.conventions, path)
    assert subject.rules_from == "base"


def test_the_part_of_a_section_a_branch_leaves_alone_is_still_its_own_where_no_release_rule_reads_it(
    tmp_path: Path,
) -> None:
    built = make(tmp_path)
    built.edit_rules(("tasks.leads.max_per_source", 7))
    subject = built.subject()
    assert subject.conventions.tasks.leads.max_per_source == 7
    assert subject.conventions.tasks.review == built.conventions.tasks.review


def test_a_stricter_branch_is_not_held_to_more_than_the_default_branch_asks(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"checked"}))
    built.stamp(
        "checked",
        covers={"diff_sha256": built.diff, "build_record_sha256": built.build_record()},
        detail=_checked(built),
        who=UNVERIFIED,
    )
    built.edit_rules(("stamps.require_hook_source", True))
    assert not any("without the hook identity" in t for t in texts(built.verify()))


def test_a_branch_that_switches_the_hook_source_off_is_held_to_the_default_branchs_rule(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"checked"}))
    built.set_base_rules(("stamps.require_hook_source", True))  # the branch's own file says false
    built.stamp(
        "checked",
        covers={"diff_sha256": built.diff, "build_record_sha256": built.build_record()},
        detail=_checked(built),
        who=UNVERIFIED,
    )
    result = built.verify()
    assert any("checked" in t and "without the hook identity (source unverified)" in t for t in texts(result, "log"))
    assert result.subject.conventions.stamps.require_hook_source


def test_a_branch_that_widens_the_models_a_review_may_run_on_is_held_to_the_default_branchs(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"reviewed"}))
    built.set_base_rules(("stamps.require_hook_source", True))
    built.edit_rules(
        ("stamps.require_hook_source", False), ("tasks.review.reverse_engineering_models", ["opus", "haiku"])
    )
    built.review(reviewer={"id": "reviewer-2", "type": "reviewer", "model": "claude-haiku-5"})
    refused = texts(built.verify(), "reviewed")
    assert any("a review of orig work must run on opus" in t and "claude-haiku-5" in t for t in refused), refused


def test_a_branch_that_calls_its_gameplay_folder_coney_only_still_needs_a_test_that_failed_first(
    tmp_path: Path,
) -> None:
    built = make(tmp_path, shape="no-test")
    paths = built.conventions.paths
    built.edit_rules(
        ("paths.gameplay", [folder for folder in paths.gameplay if folder != "src/ai/"]),
        ("paths.coney_only", [*paths.coney_only, "src/ai/"]),
    )
    assert "src/ai/" not in conventions_module.load_conventions(built.root).paths.gameplay
    assert texts(built.verify(), "fails-first") == [
        "fails-first: a feature or fix under src/ai/ needs a test that failed first, or --none with a reason"
    ]


def test_an_unknown_key_in_the_default_branchs_rules_is_skipped_and_the_rest_still_steers(tmp_path: Path) -> None:
    """A tool older than the default branch reads a key it does not know there as it reads one on a branch: it never
    steers anything, so the release goes on with the rest."""
    built = make(tmp_path)
    built.set_base_rules(("stamps.a_later_rule", True), ("stamps.require_hook_source", True), ("a_later_section", {}))
    subject = built.subject()
    assert subject.conventions.stamps.require_hook_source and subject.rules_from == "base"


@pytest.mark.parametrize("key", ["stamps.pairs", "tasks.review.folder", "pull_requests.runtime", "commits"])
def test_rules_of_the_default_branch_that_cannot_be_read_stop_the_run_instead_of_the_branchs_being_used(
    tmp_path: Path, key: str
) -> None:
    built = make(tmp_path)
    built.set_base_rules((key, REMOVE))
    with pytest.raises(ConfigError, match=rf"origin/main:\.github/conventions\.yml: .*{key.split('.')[-1]}"):
        built.subject()


def test_a_default_branch_that_predates_the_proof_section_leaves_only_the_proof_to_the_branch(tmp_path: Path) -> None:
    built = make(tmp_path)
    built.set_base_rules(("proof", REMOVE))
    built.edit_rules(("proof.build.preset", "dev"), ("stamps.require_hook_source", True))
    subject = built.subject()
    assert subject.rules_from == "branch" and subject.conventions.proof.build.preset == "dev"
    assert not subject.conventions.stamps.require_hook_source


def test_without_origins_head_the_default_branch_is_unknown_and_nothing_runs(tmp_path: Path) -> None:
    built = make(tmp_path)
    git(built.root, "remote", "set-head", "origin", "--delete")
    with pytest.raises(ConfigError, match=r"origin/HEAD is not set here.*git remote set-head origin --auto"):
        pr_proof.load_subject(built.root, built.unit, built.task, built.kind)


# --- the recorded verdict --------------------------------------------------------------------------------------------


@pytest.mark.parametrize("name", ["reviewed.json", "review.json"])
def test_a_function_map_too_big_for_the_stamp_is_read_from_the_recorded_verdict(tmp_path: Path, name: str) -> None:
    # `reviewed.json` now; `review.json` for a review recorded before the rename (it was the same file as a description
    # review's REVIEW.json on Windows). The stamp's hash decides which one is the verdict it rests on.
    raw = json.dumps({"functions": [{"addresses": ["0x00100010"], "class": "c"}]}).encode()
    (tmp_path / name).write_bytes(raw)
    if name == "reviewed.json":
        (tmp_path / "REVIEW.json").write_bytes(b"{}")  # the description review's verdict beside it
    subject = cast(pr_proof.Subject, SimpleNamespace(unit=SimpleNamespace(dir=tmp_path)))
    stamp = cast(stamps.Stamp, SimpleNamespace(detail={}, covers={"review_sha256": hashlib.sha256(raw).hexdigest()}))
    assert pr_proof._review_functions(subject, stamp) == {"0x00100010": "c"}
    other = cast(stamps.Stamp, SimpleNamespace(detail={}, covers={"review_sha256": "0" * 64}))
    assert pr_proof._review_functions(subject, other) is None
