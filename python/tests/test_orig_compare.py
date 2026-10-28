# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `orig compare`: a made-up checkout with one tagged Coney function, a study of an invented original."""

import json
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any

import pytest
from git_support import git
from stamp_support import rules
from test_orig_study import ADDRESS, COMMIT, FakeGhidra, predate_the_identity

from coney_tools import orig_compare, orig_copy, orig_cut, orig_pairs, orig_study, stamps
from coney_tools.orig_compare import CompareError
from coney_tools.unit import Unit

SOURCE = """// Widget.
namespace coney {
/// @orig 0x00501000 Widget_Update (Widget.cpp)
int Widget::update(int now) {
    if (now < ready_) {
        return 0;
    }
    if (owner_ == 0) {
        return 1;
    }
    // stand-in: the sound plays later
    playSound();
    ++count_;
    return 2;
}
}  // namespace coney
"""
#: A second function that ports another part of the same original function (both carry the same address).
SECOND = """/// @orig 0x00501000 Widget_Update (Widget.cpp)
void Widget::reset() {
    count_ = 0;
}
"""
TWO_FUNCTIONS = SOURCE.replace("}  // namespace coney" + chr(10), SECOND + "}  // namespace coney" + chr(10))
WHO = {"kind": "tool", "tool": "test"}


class Clock:
    """A clock that moves a second per reading."""

    def __init__(self) -> None:
        self.now = datetime(2026, 10, 22, 12, 0, 0, tzinfo=UTC)

    def __call__(self) -> datetime:
        self.now += timedelta(seconds=1)
        return self.now


class Setup:
    """A checkout, a unit and a studied function."""

    def __init__(self, tmp_path: Path) -> None:
        self.root = tmp_path / "repos" / "coney"
        (self.root / "src").mkdir(parents=True)
        (self.root / "coney.local.example.toml").write_text("", encoding="utf-8")
        (self.root / "src" / "widget.cpp").write_text(SOURCE, encoding="utf-8")
        git(self.root, "init", "-q", "-b", "main")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-q", "-m", "base")
        self.unit = Unit("t", tmp_path / "scratch" / "t")
        self.clock = Clock()
        self.findings: list[orig_copy.Finding] = []
        orig_study.study(self.unit, ADDRESS, FakeGhidra(), rules=rules(), commit=COMMIT, who=WHO, clock=self.clock)

    def run(self, **more: Any) -> orig_compare.CompareResult:
        """Run compare."""
        return orig_compare.compare(
            self.unit,
            self.root,
            ADDRESS,
            rules(),
            commit=COMMIT,
            who=WHO,
            clock=self.clock,
            copy_check=lambda root, base: self.findings,
            **more,
        )

    @property
    def path(self) -> Path:
        """The pairs file."""
        return self.unit.mapping(ADDRESS)

    def fill(self) -> None:
        """Complete the pairs file the first run made."""
        file = orig_pairs.load(self.path)
        where = {
            "1-3": ("src/widget.cpp:5", "same", ""),
            "4-6": ("src/widget.cpp:8", "same", ""),
            "7": ("src/widget.cpp:12", "stand-in", "the sound plays later"),
            "8": ("src/widget.cpp:13", "same", ""),
            "9": ("src/widget.cpp:14", "same", ""),
        }
        file.pairs = [
            orig_pairs.Pair(p.lines, p.kind, where[p.lines][0], where[p.lines][1], where[p.lines][2])
            for p in file.pairs
        ]
        orig_pairs.write(self.path, file)

    def cite_twice(self) -> None:
        """Commit a second Coney function that carries the same `@orig` address as the first."""
        (self.root / "src" / "widget.cpp").write_text(TWO_FUNCTIONS, encoding="utf-8")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-q", "-m", "a second function")

    def stamps_of(self, step: str) -> list[Any]:
        """The stamps of one step."""
        return [s for s in stamps.verify(self.unit.stamps, rules()).stamps if s.step == step]


@pytest.fixture
def setup(tmp_path: Path) -> Setup:
    """A unit with a studied function and a committed Coney function."""
    return Setup(tmp_path)


def test_the_first_run_writes_a_blank_pairs_file_and_the_view(setup: Setup) -> None:
    result = setup.run()
    assert result.status == "created" and setup.stamps_of("compared") == []
    file = orig_pairs.load(setup.path)
    assert [p.lines for p in file.pairs] == ["1-3", "4-6", "7", "8", "9"]
    assert file.header.coney == "Widget::update(int)" and file.header.name == "Widget_Update"
    view = result.view_path.read_text(encoding="utf-8")
    assert "ORIGINAL" in view and "CONEY (src/widget.cpp:4-15)" in view
    assert "iVar1 = *(int *)(param_1 + 0x1c);" in view and "if (now < ready_) {" in view


def test_an_incomplete_file_is_refused_with_what_is_missing(setup: Setup) -> None:
    setup.run()
    result = setup.run()
    assert result.status == "refused" and len(result.problems) == 5
    assert all("relation not set" in p for p in result.problems)
    assert setup.stamps_of("compared") == []


def test_a_complete_file_is_stamped_once(setup: Setup) -> None:
    setup.run()
    setup.fill()
    result = setup.run()
    assert result.status == "stamped" and result.problems == []
    [stamp] = setup.stamps_of("compared")
    assert (stamp.subject, stamp.result, stamp.commit) == (ADDRESS, "pass", COMMIT)
    assert stamp.covers["pairs_sha256"] == orig_pairs.file_sha256(setup.path)
    unit = orig_compare.find_unit(setup.root, ADDRESS)
    assert stamp.covers["coney_body_sha256"] == unit.body_sha256
    assert stamp.detail["pairs"] == 5 and stamp.detail["stand-in"] == 1 and stamp.detail["copy_check"] == "ok"
    assert setup.run().status == "unchanged" and len(setup.stamps_of("compared")) == 1


def test_two_functions_of_one_address_each_keep_their_own_compared_stamp(setup: Setup) -> None:
    setup.cite_twice()
    first, second = "Widget::update(int)", "Widget::reset()"
    assert setup.run(key=first).status == "created"
    setup.fill()
    assert setup.run(key=first).status == "stamped"
    assert setup.run(key=second).status == "stamped"
    # Comparing the second function must not make the first one's stamp stale, nor the other way round.
    assert setup.run(key=first).status == "unchanged"
    assert setup.run(key=second).status == "unchanged"
    assert [s.detail["coney"] for s in setup.stamps_of("compared")] == [first, second]


def test_an_edited_pairs_file_is_stamped_again(setup: Setup) -> None:
    setup.run()
    setup.fill()
    setup.run()
    file = orig_pairs.load(setup.path)
    first = file.pairs[0]
    file.pairs[0] = orig_pairs.Pair(first.lines, "branch", first.coney, "same", "now with a note")
    orig_pairs.write(setup.path, file)
    assert setup.run().status == "stamped" and len(setup.stamps_of("compared")) == 2


def test_the_view_marks_the_pairs_on_both_sides(setup: Setup) -> None:
    setup.run()
    setup.fill()
    view = setup.run().view_path.read_text(encoding="utf-8")
    assert "<1" in view and "<3" in view and "[1]" in view and "[5]" in view


def test_nothing_studied_or_a_stale_study_stops_the_comparison(setup: Setup, tmp_path: Path) -> None:
    other = Unit("u", tmp_path / "scratch" / "u")
    with pytest.raises(orig_study.StudyError):
        orig_compare.compare(other, setup.root, ADDRESS, rules(), commit=COMMIT, who=WHO)
    for segment in setup.unit.stamps.glob("*.jsonl"):
        segment.unlink()
    with pytest.raises(CompareError, match="studied"):
        setup.run()


def test_compare_wants_a_stamp_that_covers_the_name_program_and_signature(setup: Setup) -> None:
    meta_path = setup.unit.study(ADDRESS) / "study.json"
    meta = json.loads(meta_path.read_text(encoding="utf-8"))
    meta["name"] = "Widget_Renamed"  # study.json is editable; the stamp is what the name must agree with
    meta_path.write_text(json.dumps(meta), encoding="utf-8")
    with pytest.raises(CompareError, match="studied"):
        setup.run()
    assert not setup.path.exists()


def test_compare_refuses_a_stamp_that_predates_the_identity(setup: Setup) -> None:
    predate_the_identity(setup.unit.dir)
    with pytest.raises(CompareError, match="orig study"):
        setup.run()


def test_a_function_with_no_tag_is_not_comparable(setup: Setup) -> None:
    (setup.root / "src" / "widget.cpp").write_text(SOURCE.replace("0x00501000", "0x00509999"), encoding="utf-8")
    with pytest.raises(CompareError, match="no Coney function carries @orig 0x00501000"):
        setup.run()


def test_a_dirty_coney_file_is_refused(setup: Setup) -> None:
    setup.run()
    setup.fill()
    (setup.root / "src" / "widget.cpp").write_text(SOURCE + "// more\n", encoding="utf-8")
    result = setup.run()
    assert result.status == "refused" and "uncommitted" in result.problems[0]


def test_a_copy_check_hit_in_the_compared_file_is_refused(setup: Setup) -> None:
    setup.run()
    setup.fill()
    setup.findings = [orig_copy.Finding("src/widget.cpp", 5, "ghidra-name", "iVar1")]
    result = setup.run()
    assert result.status == "refused" and "iVar1" in result.problems[0]
    setup.findings = [orig_copy.Finding("src/elsewhere.cpp", 5, "goto", "goto")]
    assert setup.run().status == "stamped"


def test_a_study_with_the_same_cut_keeps_the_pairs_and_a_new_cut_needs_carry(setup: Setup) -> None:
    setup.run()
    setup.fill()
    setup.run()
    renamed = FakeGhidra()
    renamed.text = renamed.text.replace("DAT_00501234 + 1", "DAT_00501234 + 2")
    orig_study.study(setup.unit, ADDRESS, renamed, rules=rules(), commit=COMMIT, who=WHO, clock=setup.clock)
    bound = setup.run()  # same cut, other text: the file is still bound to the old study
    assert bound.status == "refused" and "decompile_sha256" in bound.problems[0] and "--carry" in bound.problems[0]
    brought = setup.run(carry=True)  # nothing is dropped: every pair still fits
    assert brought.status == "stamped" and brought.dropped == []
    assert (
        orig_pairs.load(setup.path).header.decompile_sha256
        == orig_study.load_study(setup.unit, ADDRESS).decompile_sha256
    )
    reshaped = FakeGhidra()
    reshaped.hash = "22" * 32
    reshaped.text = reshaped.text.replace("return 1;", "Widget_Log(); return 1;")
    orig_study.study(setup.unit, ADDRESS, reshaped, rules=rules(), commit=COMMIT, who=WHO, clock=setup.clock)
    stale = setup.run()
    assert stale.status == "refused" and "--carry" in stale.problems[0]
    carried = setup.run(carry=True)
    assert carried.status == "refused" and len(carried.dropped) == 5
    assert setup.path.with_suffix(".toml.old").is_file()
    assert {p.relation for p in orig_pairs.load(setup.path).pairs} == {""}


def test_the_cut_in_the_file_is_the_studys(setup: Setup) -> None:
    setup.run()
    file = orig_pairs.load(setup.path)
    study = orig_study.load_study(setup.unit, ADDRESS)
    assert file.header.cut_sha256 == orig_cut.cut_sha256(study.cut)
    assert file.header.pieces == tuple(orig_cut.format_lines(p.lines) for p in study.cut.pieces)


def test_two_functions_with_one_tag_need_a_key(setup: Setup) -> None:
    extra = "/// @orig 0x00501000 Widget_Update (Widget.cpp)\nint Gadget::update(int now) {\n    return now;\n}\n"
    (setup.root / "src" / "widget.cpp").write_text(
        SOURCE.replace("}  // namespace", extra + "}  // namespace"), encoding="utf-8"
    )
    with pytest.raises(CompareError, match="--coney-function"):
        setup.run()
    assert orig_compare.find_unit(setup.root, ADDRESS, "Gadget::update(int)").coney == "Gadget::update"


def respell(setup: Setup, location: str) -> None:
    """Complete the pairs file, then point the first pair at `location` (the same file written another way)."""
    setup.run()
    setup.fill()
    file = orig_pairs.load(setup.path)
    first = file.pairs[0]
    file.pairs[0] = orig_pairs.Pair(first.lines, first.kind, location, "same")
    orig_pairs.write(setup.path, file)


@pytest.mark.parametrize(
    "spelling", ["src/./widget.cpp:5", "src//widget.cpp:5", "src/WIDGET.cpp:5", "src/x/../widget.cpp:5"]
)
def test_a_path_written_another_way_is_still_checked_for_changes_and_flags(setup: Setup, spelling: str) -> None:
    respell(setup, spelling)
    other = setup.root / "src" / "other.cpp"
    other.write_text("int other;\n", encoding="utf-8")
    git(setup.root, "add", "-A")
    git(setup.root, "commit", "-q", "-m", "other")
    (setup.root / "src" / "widget.cpp").write_text(SOURCE + "// edited\n", encoding="utf-8")
    result = setup.run()
    assert result.status == "refused" and "uncommitted" in result.problems[0]
    git(setup.root, "commit", "-q", "-am", "edit")
    setup.findings = [orig_copy.Finding("src/widget.cpp", 5, "goto", "goto")]
    flagged = setup.run()
    assert flagged.status == "refused" and "goto" in flagged.problems[0]


def test_a_file_git_does_not_track_is_not_a_coney_location(setup: Setup) -> None:
    (setup.root / "src" / "new.cpp").write_text("int new_one;\n", encoding="utf-8")
    respell(setup, "src/new.cpp:1")
    result = setup.run()
    assert result.status == "refused" and "not a file tracked by git" in result.problems[0]


def test_the_header_binds_the_file_to_the_study_it_was_made_from(setup: Setup) -> None:
    setup.run()
    setup.fill()
    file = orig_pairs.load(setup.path)
    header = orig_pairs.Header(**{**file.header.__dict__, "ghidra_function_hash": "cc" * 32})
    orig_pairs.write(setup.path, orig_pairs.PairsFile(header, file.pairs))
    result = setup.run()
    assert result.status == "refused" and "ghidra_function_hash differs" in result.problems[0]


def test_a_broken_log_stops_the_comparison(setup: Setup) -> None:
    setup.run()
    segment = setup.unit.stamps / "0001.jsonl"
    segment.write_bytes(segment.read_bytes() + b"{")
    with pytest.raises(CompareError, match="not intact"):
        setup.run()


def test_statements_on_one_line_show_all_their_numbers_in_the_view(setup: Setup) -> None:
    study = orig_study.load_study(setup.unit, ADDRESS)
    text = study.text.replace("  return 0;\n", "  Foo(); return 0;\n")
    other = orig_study.Study(**{**study.__dict__, "text": text, "cut": orig_cut.cut_decompile(text)})
    view = orig_compare.side_by_side(other, None, "src/widget.cpp", SOURCE.splitlines(), 4)
    row = next(line for line in view.splitlines() if "Foo(); return 0;" in line)
    assert "3,4" in row.split("|")[0]
