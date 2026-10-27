# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for run records (research/runs/<id>.toml): the schema, the writer, the digest and what is refused."""

from __future__ import annotations

import dataclasses
import tomllib
from datetime import date
from pathlib import Path
from typing import Any

import pytest
from conventions_support import REPO

from coney_tools import run_records
from coney_tools.conventions import Runs, load_conventions
from coney_tools.research_catalog import load_catalog
from coney_tools.run_records import RunRecord, RunRecordError

RULES: Runs = load_conventions(REPO).runs


def record(**changes: Any) -> RunRecord:
    """A good recipe record, with `changes` applied."""
    base = RunRecord(
        id="r-20261023-find-human",
        date="2026-10-23",
        kind="recipe",
        tool="pcsx2 recipes run",
        agent="tooling-1",
        agent_type="engineer",
        model="claude-opus-5-5",
        source="hook",
        pcsx2_version="v2.9.94",
        copy="pcsx2-b",
        state="pedestrian-grab",
        patches=("scripted-pad",),
        name="find-human",
        args={"who": "Hero"},
        method="",
        values={"index": 5, "x": 1.5},
        addresses={"human": 0x006E0000},
        hashes={},
    )
    return dataclasses.replace(base, **changes)


def make_root(tmp_path: Path) -> Path:
    """A checkout holding the real conventions file."""
    (tmp_path / ".github").mkdir()
    (tmp_path / ".github" / "conventions.yml").write_bytes((REPO / ".github" / "conventions.yml").read_bytes())
    return tmp_path


# --- the file ---------------------------------------------------------------------------------------------------------


def test_a_record_is_a_toml_file_that_reads_back_to_the_same_values() -> None:
    text = run_records.to_toml(record(), RULES)
    data = tomllib.loads(text)
    assert data["id"] == "r-20261023-find-human" and data["pcsx2"]["version"] == "v2.9.94"
    assert data["values"] == {"index": 5, "x": 1.5} and data["addresses"] == {"human": 0x006E0000}
    assert run_records.parse(text, RULES) == record()


def test_addresses_are_written_in_hex_and_values_in_decimal() -> None:
    text = run_records.to_toml(record(), RULES)
    assert "human = 0x006e0000" in text and "index = 5" in text


def test_the_text_is_the_same_each_time() -> None:
    assert run_records.to_toml(record(), RULES) == run_records.to_toml(record(), RULES)


def test_a_key_that_is_not_bare_is_quoted() -> None:
    odd = record(values={"player.x": 1.0, "f32(tf(h) + 0x0)": 2.0})
    text = run_records.to_toml(odd, RULES)
    assert '"player.x" = 1.0' in text
    assert run_records.parse(text, RULES) == odd


def test_a_trace_record_holds_a_file_hash() -> None:
    trace = record(
        id="r-20261023-walk60",
        kind="trace",
        tool="trace original",
        name="walk60",
        args={},
        values={"updates": 600, "missed_updates": 0},
        addresses={},
        hashes={"trace_csv": "ab" * 32},
    )
    assert run_records.parse(run_records.to_toml(trace, RULES), RULES) == trace


def test_a_method_record_needs_its_method() -> None:
    method = record(
        id="r-20261023-grab-reach",
        kind="method",
        tool="pcsx2 record-run",
        name="",
        args={},
        method="watch on 0x0026c548",
    )
    assert run_records.parse(run_records.to_toml(method, RULES), RULES).method == "watch on 0x0026c548"
    with pytest.raises(RunRecordError, match="a method record needs the method in words"):
        run_records.to_toml(dataclasses.replace(method, method=""), RULES)


# --- the digest -------------------------------------------------------------------------------------------------------


def test_a_hand_edited_record_fails_its_digest() -> None:
    text = run_records.to_toml(record(), RULES)
    with pytest.raises(RunRecordError, match="digest"):
        run_records.parse(text.replace("index = 5", "index = 6"), RULES)


def test_a_record_without_a_digest_is_refused() -> None:
    text = "\n".join(
        line for line in run_records.to_toml(record(), RULES).splitlines() if not line.startswith("digest")
    )
    with pytest.raises(RunRecordError, match="digest"):
        run_records.parse(text, RULES)


def test_the_digest_covers_every_field() -> None:
    texts = {
        run_records.to_toml(record(**change), RULES)
        for change in (
            {},
            {"copy": "pcsx2-c"},
            {"state": "unknown"},
            {"values": {"index": 6, "x": 1.5}},
            {"addresses": {"human": 0x006E0004}},
            {"agent": "tooling-2"},
        )
    }
    assert len({line for text in texts for line in text.splitlines() if line.startswith("digest")}) == len(texts)


# --- what is refused --------------------------------------------------------------------------------------------------

BAD: list[tuple[str, dict[str, Any], str]] = [
    ("an id without the date", {"id": "find-human"}, "not a run id"),
    ("an id in capitals", {"id": "r-20261023-Find"}, "not a run id"),
    ("a date that is not a day", {"date": "2026-13-01"}, "date"),
    ("an id whose date differs from the record's", {"id": "r-20261022-find-human"}, "date"),
    ("a kind that does not exist", {"kind": "dump"}, "kind"),
    ("a source that is neither hook nor unverified", {"source": "me"}, "source"),
    ("no PCSX2 version", {"pcsx2_version": ""}, "pcsx2 version"),
    ("no copy", {"copy": ""}, "copy"),
    ("a state id that is not a slug", {"state": "Level 99"}, "state"),
    ("a patch name with a space", {"patches": ("a b",)}, "patch"),
    ("a recipe record without a recipe", {"name": ""}, "needs the name"),
    ("a value that is text", {"values": {"name": "Hero"}}, "number"),
    ("a value that is a boolean", {"values": {"on": True}}, "number"),
    ("a value that is not finite", {"values": {"x": float("nan")}}, "finite"),
    ("an address above 32 bits", {"addresses": {"a": 1 << 32}}, "address"),
    ("a negative address", {"addresses": {"a": -1}}, "address"),
    ("text with a newline", {"args": {"who": "a\nb"}}, "one line"),
    ("text that is not ASCII", {"args": {"who": "テ"}}, "printable ASCII"),
    ("a method longer than the limit", {"kind": "method", "name": "", "method": "x" * 601}, "longer than"),
    ("an arg value longer than the limit", {"args": {"who": "x" * 601}}, "longer than"),
    ("a hash that is not 64 hex digits", {"hashes": {"trace_csv": "abc"}}, "SHA-256"),
    ("an empty value name", {"values": {"": 1}}, "name"),
]


@pytest.mark.parametrize(("what", "changes", "message"), BAD, ids=[case[0] for case in BAD])
def test_a_bad_record_is_refused_with_the_field_named(what: str, changes: dict[str, Any], message: str) -> None:
    with pytest.raises(RunRecordError, match=message):
        run_records.to_toml(record(**changes), RULES)


def test_too_many_values_are_refused() -> None:
    many = {f"v{k}": k for k in range(RULES.max_values + 1)}
    with pytest.raises(RunRecordError, match="more than"):
        run_records.to_toml(record(values=many, addresses={}), RULES)
    run_records.to_toml(record(values=dict(list(many.items())[:-1]), addresses={}, args={}), RULES)


def test_the_writer_refuses_a_record_larger_than_its_reader_takes() -> None:
    big = record(
        method="m" * 600,
        values={f"{n:03d}" + "v" * 117: n for n in range(63)},
        addresses={},
    )
    with pytest.raises(RunRecordError, match="max_bytes"):
        run_records.to_toml(big, RULES)


def test_args_count_with_the_values_against_the_limit() -> None:
    many = record(args={f"a{n}": "x" for n in range(10)}, values={f"v{n}": n for n in range(55)})
    with pytest.raises(RunRecordError, match="more than 64"):
        run_records.to_toml(many, RULES)


WRONG_TYPES = [
    ("an unquoted date", 'date = "2026-10-23"', "date = 2026-10-23", "date"),
    ("an integer id", 'id = "r-20261023-find-human"', "id = 7", "id"),
    ("an integer state", 'state = "pedestrian-grab"', "state = 3", "start.state"),
    ("a patch that is not text", 'patches = ["scripted-pad"]', "patches = [1]", "start.patches"),
    ("an integer agent", 'agent = "tooling-1"', "agent = 1", "who.agent"),
    ("a list as the method", 'method = ""', "method = []", "run.method"),
]


@pytest.mark.parametrize(("what", "old", "new", "field_name"), WRONG_TYPES, ids=[case[0] for case in WRONG_TYPES])
def test_a_field_of_the_wrong_type_is_a_bad_record_not_a_crash(
    what: str, old: str, new: str, field_name: str, tmp_path: Path
) -> None:
    text = run_records.to_toml(record(), RULES)
    assert old in text, what
    with pytest.raises(RunRecordError, match=field_name):
        run_records.parse(text.replace(old, new, 1), RULES)
    root = make_root(tmp_path)
    (root / "research" / "runs").mkdir(parents=True)
    (root / "research" / "runs" / "r-20261023-find-human.toml").write_text(text.replace(old, new, 1), encoding="utf-8")
    records, problems = run_records.load_all(root)
    assert not records and len(problems) == 1


def test_fit_keeps_the_numbers_that_fit_and_counts_the_rest() -> None:
    base = record(values={}, addresses={}, method="m" * 600)
    numbers: dict[str, int | float] = {"café": 1, "x" * 121: 2, **{f"{n:03d}" + "v" * 117: n for n in range(70)}}
    kept, left = run_records.fit(base, numbers, RULES)
    assert "café" not in kept and "x" * 121 not in kept
    assert left == len(numbers) - len(kept) and 0 < len(kept) < 64
    run_records.to_toml(dataclasses.replace(base, values=kept), RULES)


def test_a_file_with_a_key_the_schema_does_not_know_is_refused() -> None:
    text = run_records.to_toml(record(), RULES)
    with pytest.raises(RunRecordError, match="unknown key 'dump'"):
        run_records.parse(text.replace("[start]", 'dump = "aa"\n\n[start]'), RULES)
    with pytest.raises(RunRecordError, match="unknown key 'memory'"):
        run_records.parse(text + '\n[memory]\nbytes = "00"\n', RULES)


def test_a_file_larger_than_the_limit_is_refused() -> None:
    with pytest.raises(RunRecordError, match="bytes"):
        run_records.parse(run_records.to_toml(record(), RULES) + "# " + "x" * RULES.max_bytes, RULES)


def test_text_that_is_not_toml_is_refused() -> None:
    with pytest.raises(RunRecordError, match="not valid TOML"):
        run_records.parse("this = = that", RULES)


# --- ids --------------------------------------------------------------------------------------------------------------


def test_an_id_is_the_day_and_a_name() -> None:
    assert run_records.new_id("Grab reach!", date(2026, 10, 23), RULES) == "r-20261023-grab-reach"
    assert run_records.new_id("find_human", date(2026, 10, 23), RULES) == "r-20261023-find-human"


def test_an_id_name_is_cut_to_fit_and_must_hold_a_letter_or_digit() -> None:
    assert run_records.new_id("a" * 80, date(2026, 10, 23), RULES) == "r-20261023-" + "a" * 48
    with pytest.raises(RunRecordError, match="name"):
        run_records.new_id("!!!", date(2026, 10, 23), RULES)


# --- the folder -------------------------------------------------------------------------------------------------------


def test_writing_puts_the_record_in_the_runs_folder(tmp_path: Path) -> None:
    root = make_root(tmp_path)
    path = run_records.write(root, record())
    assert path == root / "research" / "runs" / "r-20261023-find-human.toml"
    assert run_records.load(root, "r-20261023-find-human") == record()


def test_docs_check_s_catalog_sees_a_written_record(tmp_path: Path) -> None:
    # One folder for both: the writer's (`runs.folder`) is `research.files.runs`, which `docs check` reads.
    root = make_root(tmp_path)
    research = load_conventions(root).research
    assert not load_catalog(root, research).has_records
    run_records.write(root, record())
    catalog = load_catalog(root, research)
    assert catalog.has_records and catalog.has_run("r-20261023-find-human")


def test_a_record_is_never_overwritten(tmp_path: Path) -> None:
    root = make_root(tmp_path)
    run_records.write(root, record())
    with pytest.raises(RunRecordError, match="already exists"):
        run_records.write(root, record(values={"index": 9}))
    assert run_records.load(root, "r-20261023-find-human").values["index"] == 5


def test_a_missing_record_is_named(tmp_path: Path) -> None:
    with pytest.raises(RunRecordError, match=r"no run record r-20261023-nope \(research/runs/r-20261023-nope.toml\)"):
        run_records.load(make_root(tmp_path), "r-20261023-nope")


def test_a_record_must_be_filed_under_its_own_id(tmp_path: Path) -> None:
    root = make_root(tmp_path)
    folder = root / "research" / "runs"
    folder.mkdir(parents=True)
    (folder / "r-20261023-other.toml").write_text(run_records.to_toml(record(), RULES), encoding="utf-8")
    with pytest.raises(RunRecordError, match=r"r-20261023-other\.toml: the id is r-20261023-find-human"):
        run_records.load(root, "r-20261023-other")


def test_load_all_lists_good_records_and_names_the_bad(tmp_path: Path) -> None:
    root = make_root(tmp_path)
    run_records.write(root, record())
    (root / "research" / "runs" / "r-20261023-junk.toml").write_text("x = = 1", encoding="utf-8")
    (root / "research" / "runs" / "notes.txt").write_text("not a record", encoding="utf-8")
    records, problems = run_records.load_all(root)
    assert list(records) == ["r-20261023-find-human"]
    assert len(problems) == 1 and problems[0].startswith("research/runs/r-20261023-junk.toml: not valid TOML")


def test_no_runs_folder_is_no_records(tmp_path: Path) -> None:
    assert run_records.load_all(make_root(tmp_path)) == ({}, [])


def test_every_record_in_the_repository_is_valid() -> None:
    records, problems = run_records.load_all(REPO)
    assert problems == [] and all(r.id == name for name, r in records.items())
