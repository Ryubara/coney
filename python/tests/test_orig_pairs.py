# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the pairs file (`mapping/<addr>.toml`): pre-filling, writing, reading and every rule it is checked by."""

from pathlib import Path

import pytest
from stamp_support import rules

from coney_tools import orig_cut, orig_pairs
from coney_tools.orig_pairs import Pair, PairsError

WIDGET = """
int Widget_Update(long param_1,int param_2)

{
  int iVar1;
  long lVar2;

  iVar1 = *(int *)(param_1 + 0x1c);
  if (param_2 < iVar1) {
    return 0;
  }
  lVar2 = Widget_GetOwner(param_1);
  if (lVar2 == 0) {
    return 1;
  }
  Widget_PlaySound(param_1,0x2a);
  DAT_00501234 = DAT_00501234 + 1;
  return 2;
}
"""
CUT = orig_cut.cut_decompile(WIDGET)  # pieces: 1-3, 4-6, 7, 8, 9


def header(cut: orig_cut.Cut = CUT) -> orig_pairs.Header:
    """The header of the invented function."""
    return orig_pairs.header_from_cut(
        cut,
        address="0x00501000",
        name="Widget_Update",
        program="SLUS_212.15",
        decompile_sha256="aa" * 32,
        ghidra_function_hash="bb" * 32,
        coney="Widget::update(int)",
    )


def filled() -> orig_pairs.PairsFile:
    """A complete, valid pairs file for the invented function."""
    file = orig_pairs.prefill(header(), CUT)
    done = [
        Pair("1-3", "branch", "src/widget.cpp:3", "same", "+0x1c is the ready frame", "", ""),
        Pair("4-6", "call", "src/widget.cpp:5", "same", "", "", ""),
        Pair("7", "call", "", "not-ported", "", "#812", ""),
        Pair("8-9", "field", "src/widget.cpp:8", "stand-in", "counts on the widget", "", "Coney returns the count"),
    ]
    return orig_pairs.PairsFile(file.header, done)


def make_source(root: Path) -> None:
    """The Coney file the pairs point at: 12 lines with a stand-in comment above line 8."""
    (root / "src").mkdir(parents=True, exist_ok=True)
    text = "\n".join(
        ["// Widget"]
        + [f"int line{n};" for n in range(2, 7)]
        + ["// stand-in: counts on the widget", "int bump;"]
        + ["x;"] * 4
    )
    (root / "src" / "widget.cpp").write_text(text + "\n", encoding="utf-8")


def problems(file: orig_pairs.PairsFile, root: Path | None = None) -> list[str]:
    """Every problem of a file against the cut (and the source under `root` when given)."""
    found = orig_pairs.check_structure(file, CUT, "0x00501000", rules().pairs)
    if root is not None:
        found += orig_pairs.check_code(file, root, rules().pairs)
    return found


def test_prefill_has_one_blank_pair_per_piece() -> None:
    file = orig_pairs.prefill(header(), CUT)
    assert [(p.lines, p.kind, p.relation) for p in file.pairs] == [
        ("1-3", "branch", ""),
        ("4-6", "branch", ""),
        ("7", "call", ""),
        ("8", "field", ""),
        ("9", "return", ""),
    ]


def test_a_blank_file_lists_what_is_missing(tmp_path: Path) -> None:
    found = problems(orig_pairs.prefill(header(), CUT))
    assert len(found) == 5 and all("relation" in text for text in found)


def test_render_and_load_round_trip(tmp_path: Path) -> None:
    file = filled()
    path = tmp_path / "mapping" / "0x00501000.toml"
    orig_pairs.write(path, file)
    assert b"\r" not in path.read_bytes()
    again = orig_pairs.load(path)
    assert again.header == file.header and again.pairs == file.pairs


def test_text_with_quotes_and_unicode_survives(tmp_path: Path) -> None:
    file = filled()
    file.pairs[0] = Pair("1-3", "branch", "src/widget.cpp:3", "same", 'a "quote" \\ and é', "", "")
    path = tmp_path / "p.toml"
    orig_pairs.write(path, file)
    assert orig_pairs.load(path).pairs[0].note == 'a "quote" \\ and é'


def test_the_file_hash_ignores_line_ending_style(tmp_path: Path) -> None:
    path = tmp_path / "p.toml"
    orig_pairs.write(path, filled())
    unix = orig_pairs.file_sha256(path)
    path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
    assert orig_pairs.file_sha256(path) == unix


def test_a_complete_file_passes(tmp_path: Path) -> None:
    make_source(tmp_path)
    assert problems(filled(), tmp_path) == []


def test_a_line_in_no_pair_is_named() -> None:
    file = filled()
    del file.pairs[2]
    found = problems(file)
    assert any("line 7" in text and "no pair" in text for text in found)


def test_a_line_in_two_pairs_is_refused() -> None:
    file = filled()
    file.pairs.append(Pair("7", "call", "", "not-ported", "", "#9", ""))
    assert any("line 7" in text and "pair" in text for text in problems(file))


def test_a_pair_may_not_split_a_piece() -> None:
    file = filled()
    file.pairs[0] = Pair("1-2", "branch", "src/widget.cpp:3", "same", "", "", "")
    file.pairs.insert(1, Pair("3", "return", "src/widget.cpp:4", "same", "", "", ""))
    assert any("splits piece" in text for text in problems(file))


def test_merged_pieces_need_a_reason_and_must_be_neighbours() -> None:
    file = filled()
    file.pairs[3] = Pair("8-9", "field", "src/widget.cpp:8", "same", "", "", "")
    assert any("merge_reason" in text for text in problems(file))
    far = filled()
    far.pairs = [
        Pair("1-3,7", "branch", "src/widget.cpp:3", "same", "", "", "because"),
        *far.pairs[1:2],
        *far.pairs[3:],
    ]
    assert any("adjacent" in text for text in problems(far))


def test_lines_outside_the_function_or_unreadable_are_refused() -> None:
    file = filled()
    file.pairs[0] = Pair("1-30", "branch", "src/widget.cpp:3", "same", "", "", "")
    assert any("line 10 is not a line" in text for text in problems(file))
    file.pairs[0] = Pair("one", "branch", "src/widget.cpp:3", "same", "", "", "")
    assert any("one" in text for text in problems(file))


def test_kind_and_relation_must_be_known() -> None:
    file = filled()
    file.pairs[1] = Pair("4-6", "jump", "src/widget.cpp:5", "same", "", "", "")
    file.pairs[2] = Pair("7", "call", "", "almost", "", "", "")
    found = problems(file)
    assert any("kind" in text and "jump" in text for text in found)
    assert any("relation" in text and "almost" in text for text in found)


def test_same_needs_a_coney_location(tmp_path: Path) -> None:
    make_source(tmp_path)
    file = filled()
    file.pairs[1] = Pair("4-6", "call", "", "same", "", "", "")
    assert any("coney" in text for text in problems(file, tmp_path))
    for bad in (
        "src/widget.cpp",
        "src/widget.cpp:0",
        "src/widget.cpp:99",
        "src/missing.cpp:1",
        "../x.cpp:1",
        "docs/a.md:1",
    ):
        file.pairs[1] = Pair("4-6", "call", bad, "same", "", "", "")
        assert any("coney" in text for text in problems(file, tmp_path)), bad


def test_a_range_of_lines_is_a_valid_location(tmp_path: Path) -> None:
    make_source(tmp_path)
    file = filled()
    file.pairs[1] = Pair("4-6", "call", "src/widget.cpp:4-6", "same", "", "", "")
    assert problems(file, tmp_path) == []


def test_stand_in_needs_a_note_and_a_comment_at_the_code(tmp_path: Path) -> None:
    make_source(tmp_path)
    file = filled()
    file.pairs[3] = Pair("8-9", "field", "src/widget.cpp:8", "stand-in", "", "", "r")
    assert any("note" in text for text in problems(file, tmp_path))
    file.pairs[3] = Pair("8-9", "field", "src/widget.cpp:12", "stand-in", "the reason", "", "r")
    assert any("stand-in" in text and "comment" in text for text in problems(file, tmp_path))
    file.pairs[3] = Pair("8-9", "field", "src/widget.cpp:9", "stand-in", "the reason", "", "r")
    assert problems(file, tmp_path) == []  # the marker is on the line above


def test_coneys_choice_also_counts_as_the_marker(tmp_path: Path) -> None:
    make_source(tmp_path)
    source = tmp_path / "src" / "widget.cpp"
    source.write_text(source.read_text(encoding="utf-8").replace("stand-in:", "Coney's choice:"), encoding="utf-8")
    assert problems(filled(), tmp_path) == []


def test_not_ported_needs_an_issue_number() -> None:
    file = filled()
    file.pairs[2] = Pair("7", "call", "", "not-ported", "", "", "")
    assert any("issue" in text for text in problems(file))
    file.pairs[2] = Pair("7", "call", "", "not-ported", "", "812", "")
    assert any("issue" in text for text in problems(file))
    file.pairs[2] = Pair("7", "call", "", "not-ported", "", "#812", "")
    assert not any("issue" in text for text in problems(file))


def test_a_header_from_another_cut_or_address_is_refused() -> None:
    file = filled()
    other = orig_cut.cut_decompile(WIDGET.replace("return 2;", "Foo(); return 2;"))
    file.header = header(other)
    assert any("cut" in text and "--carry" in text for text in problems(file))
    file = filled()
    found = orig_pairs.check_structure(file, CUT, "0x00999999", rules().pairs)
    assert any("address" in text for text in found)


def test_loading_rejects_unknown_keys_and_a_wrong_version(tmp_path: Path) -> None:
    path = tmp_path / "p.toml"
    orig_pairs.write(path, filled())
    text = path.read_text(encoding="utf-8")
    path.write_text(text.replace("[[pair]]", '[[pair]]\nsurprise = "x"', 1), encoding="utf-8")
    with pytest.raises(PairsError, match="surprise"):
        orig_pairs.load(path)
    path.write_text(text.replace("version = 1", "version = 2"), encoding="utf-8")
    with pytest.raises(PairsError, match="version"):
        orig_pairs.load(path)
    path.write_text("this is not toml [", encoding="utf-8")
    with pytest.raises(PairsError):
        orig_pairs.load(path)
    with pytest.raises(PairsError, match=r"missing|cannot"):
        orig_pairs.load(tmp_path / "none.toml")


def test_carry_keeps_every_pair_when_the_shape_is_unchanged() -> None:
    old = filled()
    moved = orig_cut.cut_decompile(WIDGET.replace("DAT_00501234 + 1;", "DAT_00501234 + 2;"))
    carried, dropped = orig_pairs.carry(old, header(moved), moved)
    assert dropped == [] and carried.pairs == old.pairs and carried.header == header(moved)


def test_carry_keeps_nothing_when_the_numbering_moved() -> None:
    old = filled()
    reshaped = orig_cut.cut_decompile(WIDGET.replace("return 1;", "Foo(); return 1;"))
    carried, dropped = orig_pairs.carry(old, header(reshaped), reshaped)
    assert dropped == old.pairs
    assert all(p.relation == "" and p.coney == "" for p in carried.pairs)
    assert sorted(n for p in carried.pairs for n in orig_cut.parse_lines(p.lines)) == [
        s.number for s in reshaped.statements
    ]


def test_carry_keeps_a_pair_that_is_still_a_union_of_pieces_when_the_cut_differs_but_the_shape_does_not() -> None:
    old = filled()
    same_shape = orig_cut.cut_decompile(WIDGET)
    recut = header(same_shape)
    recut = orig_pairs.Header(
        **{**recut.__dict__, "cut_sha256": "00" * 32, "pieces": ("1-3", "4-5", "6", "7", "8", "9")}
    )
    fake = orig_cut.Cut(
        same_shape.statements,
        (
            orig_cut.Piece(1, "branch", (1, 2, 3)),
            orig_cut.Piece(2, "branch", (4, 5)),
            orig_cut.Piece(3, "return", (6,)),
            orig_cut.Piece(4, "call", (7,)),
            orig_cut.Piece(5, "field", (8,)),
            orig_cut.Piece(6, "return", (9,)),
        ),
    )
    carried, dropped = orig_pairs.carry(old, recut, fake)
    assert [p.lines for p in carried.pairs] == ["1-3", "4-6", "7", "8-9"]
    assert dropped == []
