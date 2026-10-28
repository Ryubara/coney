# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the stamp log: records, the append-only chain, segments and verification."""

import dataclasses
import json
import threading
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any

import pytest
from conventions_support import REPO
from stamp_support import rules

from coney_tools import stamps
from coney_tools.identity import file_name as identity_file_name
from coney_tools.stamps import ZERO, StampError

SHA = "ab" * 32
COMMIT = "c0ffee" * 6 + "c0ff"


class Clock:
    """A clock that moves one second per reading."""

    def __init__(self) -> None:
        self.now = datetime(2026, 10, 22, 12, 0, 0, tzinfo=UTC)

    def __call__(self) -> datetime:
        self.now += timedelta(seconds=1)
        return self.now


def add(
    folder: Path, step: str = "studied", subject: str = "0x00100000", clock: Clock | None = None, **more: Any
) -> Any:
    """Append one stamp with sensible defaults."""
    arguments: dict[str, Any] = {
        "rules": rules(),
        "task": "t",
        "step": step,
        "subject": subject,
        "result": "pass",
        "commit": COMMIT,
        "covers": {"decompile_sha256": SHA},
        "detail": {"statements": 3},
        "who": {"kind": "tool", "tool": "test"},
        "clock": clock or Clock(),
    }
    arguments.update(more)
    return stamps.append(folder, **arguments)


def lines(folder: Path) -> list[str]:
    """Every line of every segment."""
    return [line for seg in sorted(folder.glob("*.jsonl")) for line in seg.read_text(encoding="utf-8").splitlines()]


def test_canonical_form_is_sorted_compact_and_ascii() -> None:
    assert stamps.canonical({"b": 1, "a": [True, None, "é"]}) == b'{"a":[true,null,"\\u00e9"],"b":1}'
    with pytest.raises(StampError, match="float"):
        stamps.canonical({"a": 1.5})


def test_first_stamp_starts_the_chain_and_the_next_links_to_it(tmp_path: Path) -> None:
    clock = Clock()
    first = add(tmp_path, clock=clock)
    second = add(tmp_path, step="compared", clock=clock)
    assert (first.seq, first.prev) == (1, ZERO)
    assert (second.seq, second.prev) == (2, first.hash)
    assert first.time == "2026-10-22T12:00:01Z"
    assert len(lines(tmp_path)) == 2


def test_a_stamp_has_no_signature_and_its_hash_covers_the_whole_line(tmp_path: Path) -> None:
    stamp = add(tmp_path)
    assert "sig" not in stamp.data and b'"sig"' not in stamp.line.encode("utf-8")
    assert stamp.body == stamp.line.encode("utf-8")
    assert stamps.sha256_hex(stamp.body) == stamp.hash
    assert stamp.line.encode("utf-8").startswith(b"{")
    assert set(stamp.data) == set(stamps.FIELDS)


@pytest.mark.parametrize(
    ("change", "match"),
    [
        ({"step": "invented"}, "step"),
        ({"result": "maybe"}, "result"),
        ({"commit": "abc"}, "commit"),
        ({"covers": {"x": "not hex"}}, "covers"),
        ({"covers": {"x": 5}}, "covers"),
        ({"detail": {"f": 1.5}}, "float"),
        ({"detail": {"big": "x" * 3000}}, "detail"),
        ({"subject": 5}, "subject"),
        ({"who": {}}, "who"),
    ],
)
def test_append_refuses_malformed_input_and_writes_nothing(tmp_path: Path, change: dict[str, Any], match: str) -> None:
    with pytest.raises(StampError, match=match):
        add(tmp_path, **change)
    assert lines(tmp_path) == []


def test_a_clean_log_verifies(tmp_path: Path) -> None:
    clock = Clock()
    for step in ("claimed", "studied", "compared"):
        add(tmp_path, step=step, clock=clock)
    report = stamps.verify(tmp_path, rules())
    assert report.problems == [] and report.ok and len(report.stamps) == 3


def edit_line(folder: Path, index: int, change: Any) -> None:
    """Rewrite one line of the first segment with `change(obj)` applied, in canonical form."""
    segment = sorted(folder.glob("*.jsonl"))[0]
    rows = segment.read_text(encoding="utf-8").splitlines()
    obj = json.loads(rows[index])
    change(obj)
    rows[index] = stamps.canonical(obj).decode("utf-8")
    segment.write_bytes(("\n".join(rows) + "\n").encode("utf-8"))


def seed(folder: Path, count: int = 4) -> None:
    """A log of `count` stamps."""
    clock = Clock()
    for _ in range(count):
        add(folder, clock=clock)


def problems(folder: Path, **more: Any) -> list[str]:
    """The error texts verification gives."""
    return [p.text for p in stamps.verify(folder, rules(), **more).problems if p.severity == "error"]


def test_editing_a_stamp_breaks_the_next_link(tmp_path: Path) -> None:
    seed(tmp_path)
    edit_line(tmp_path, 1, lambda o: o.update(result="fail"))
    found = problems(tmp_path)
    assert any("seq 3" in text and "prev" in text for text in found)


def test_deleting_a_middle_stamp_is_found(tmp_path: Path) -> None:
    seed(tmp_path)
    segment = tmp_path / "0001.jsonl"
    rows = segment.read_text(encoding="utf-8").splitlines()
    del rows[1]
    segment.write_bytes(("\n".join(rows) + "\n").encode("utf-8"))
    assert any("seq" in text for text in problems(tmp_path))


def test_reordering_is_found(tmp_path: Path) -> None:
    seed(tmp_path)
    segment = tmp_path / "0001.jsonl"
    rows = segment.read_text(encoding="utf-8").splitlines()
    rows[1], rows[2] = rows[2], rows[1]
    segment.write_bytes(("\n".join(rows) + "\n").encode("utf-8"))
    assert problems(tmp_path)


def test_hand_formatted_json_is_not_canonical(tmp_path: Path) -> None:
    seed(tmp_path, 2)
    segment = tmp_path / "0001.jsonl"
    rows = segment.read_text(encoding="utf-8").splitlines()
    rows[0] = json.dumps(json.loads(rows[0]), indent=1).replace("\n", " ")
    segment.write_bytes(("\n".join(rows) + "\n").encode("utf-8"))
    assert any("canonical" in text for text in problems(tmp_path))


def test_a_torn_last_line_is_reported_and_blocks_appending(tmp_path: Path) -> None:
    seed(tmp_path, 2)
    segment = tmp_path / "0001.jsonl"
    segment.write_bytes(segment.read_bytes()[:-30])
    assert any("complete" in text or "torn" in text for text in problems(tmp_path))
    with pytest.raises(StampError, match="log is not intact"):
        add(tmp_path)


def test_time_may_not_go_backwards_and_the_task_may_not_change(tmp_path: Path) -> None:
    seed(tmp_path, 3)
    edit_line(tmp_path, 1, lambda o: o.update(time="2020-01-01T00:00:00Z"))
    assert any("time" in text for text in problems(tmp_path))
    other = tmp_path / "other"
    seed(other, 2)
    edit_line(other, 1, lambda o: o.update(task="someone-else"))
    assert any("task" in text for text in problems(other))


def test_an_unknown_step_or_version_is_an_error(tmp_path: Path) -> None:
    seed(tmp_path, 1)
    edit_line(tmp_path, 0, lambda o: o.update(step="made-up"))
    assert any("step" in text for text in problems(tmp_path))
    other = tmp_path / "v"
    seed(other, 1)
    edit_line(other, 0, lambda o: o.update(v=2))
    assert any("version" in text for text in problems(other))


def test_append_refuses_a_broken_chain(tmp_path: Path) -> None:
    seed(tmp_path)
    edit_line(tmp_path, 0, lambda o: o.update(subject="x"))
    with pytest.raises(StampError, match="log is not intact"):
        add(tmp_path)


def test_segments_roll_over_and_the_chain_crosses_them(tmp_path: Path) -> None:
    small = dataclasses.replace(rules(), segment_bytes=4096)
    clock = Clock()
    for index in range(20):
        add(tmp_path, rules=small, subject=f"0x{index:08x}", detail={"pad": "x" * 400}, clock=clock)
    segments = sorted(tmp_path.glob("*.jsonl"))
    assert len(segments) > 1 and segments[0].name == "0001.jsonl"
    assert all(s.stat().st_size <= 4096 for s in segments)
    report = stamps.verify(tmp_path, small)
    assert report.problems == [] and [s.seq for s in report.stamps] == list(range(1, 21))


def test_a_deleted_segment_is_found(tmp_path: Path) -> None:
    small = dataclasses.replace(rules(), segment_bytes=4096)
    clock = Clock()
    for index in range(12):
        add(tmp_path, rules=small, subject=f"{index}", detail={"pad": "x" * 400}, clock=clock)
    (tmp_path / "0002.jsonl").unlink()
    assert stamps.verify(tmp_path, small).problems


def test_concurrent_appenders_get_distinct_sequence_numbers(tmp_path: Path) -> None:
    clock = Clock()
    errors: list[Exception] = []

    def work(name: int) -> None:
        try:
            for index in range(6):
                add(tmp_path, subject=f"{name}-{index}", clock=clock)
        except Exception as error:  # the test reports it below
            errors.append(error)

    threads = [threading.Thread(target=work, args=(n,)) for n in range(4)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    assert errors == []
    report = stamps.verify(tmp_path, rules())
    assert report.problems == [] and [s.seq for s in report.stamps] == list(range(1, 25))


def test_latest_and_current_look_at_step_subject_and_covers(tmp_path: Path) -> None:
    clock = Clock()
    add(tmp_path, subject="0x1", covers={"decompile_sha256": "aa" * 32}, clock=clock)
    add(tmp_path, subject="0x1", covers={"decompile_sha256": "bb" * 32}, clock=clock)
    add(tmp_path, subject="0x2", result="fail", clock=clock)
    report = stamps.verify(tmp_path, rules())
    one, two = report.latest("studied", "0x1"), report.latest("studied", "0x2")
    assert one is not None and one.seq == 2
    assert report.latest("studied", "0x2", result="pass") is None
    assert two is not None and two.result == "fail"
    assert report.current("studied", "0x1", {"decompile_sha256": "bb" * 32}) is not None
    assert report.current("studied", "0x1", {"decompile_sha256": "aa" * 32}) is None
    assert report.head == report.stamps[-1].hash and stamps.verify(tmp_path / "none", rules()).head == ZERO


def test_a_function_finds_its_own_stamp_of_an_address_that_two_functions_cite(tmp_path: Path) -> None:
    clock = Clock()
    first, second = "Camera::update()", "Camera::reset()"
    add(tmp_path, "compared", "0x1", covers={"coney_body_sha256": "aa" * 32}, detail={"coney": first}, clock=clock)
    add(tmp_path, "compared", "0x1", covers={"coney_body_sha256": "bb" * 32}, detail={"coney": second}, clock=clock)
    report = stamps.verify(tmp_path, rules())
    both = (first, second)
    mine = report.current("compared", "0x1", {"coney_body_sha256": "aa" * 32}, function=first, cited_by=both)
    other = report.current("compared", "0x1", {"coney_body_sha256": "bb" * 32}, function=second, cited_by=both)
    assert mine is not None and mine.seq == 1 and other is not None and other.seq == 2
    # Without a function the newest stamp of the address still decides, as it always did.
    assert report.current("compared", "0x1", {"coney_body_sha256": "aa" * 32}) is None


def test_another_functions_later_stamp_or_failure_does_not_replace_a_pass(tmp_path: Path) -> None:
    clock = Clock()
    first, second = "Camera::update()", "Camera::reset()"
    add(tmp_path, "compared", "0x1", detail={"coney": first}, clock=clock)
    add(tmp_path, "compared", "0x1", result="fail", detail={"coney": second}, clock=clock)
    report = stamps.verify(tmp_path, rules())
    both = (first, second)
    assert report.latest("compared", "0x1", function=first, cited_by=both) is not None
    assert report.current("compared", "0x1", {"decompile_sha256": SHA}, function=first, cited_by=both) is not None
    assert report.current("compared", "0x1", {"decompile_sha256": SHA}, function=second, cited_by=both) is None


def test_a_function_with_no_stamp_of_its_own_finds_none_while_another_cites_the_address(tmp_path: Path) -> None:
    add(tmp_path, "compared", "0x1", detail={"coney": "Camera::update()"})
    report = stamps.verify(tmp_path, rules())
    both = ("Camera::update()", "Camera::reset()")
    assert report.latest("compared", "0x1", function="Camera::reset()", cited_by=both) is None
    assert (
        report.current("compared", "0x1", {"decompile_sha256": SHA}, function="Camera::reset()", cited_by=both) is None
    )


def test_a_stamp_that_names_no_function_covers_the_only_function_citing_its_address(tmp_path: Path) -> None:
    clock = Clock()
    add(tmp_path, "compared", "0x1", detail={}, clock=clock)
    add(tmp_path, "compared", "0x2", detail={"coney": "Old::name()"}, clock=clock)
    report = stamps.verify(tmp_path, rules())
    assert report.latest("compared", "0x1", function="Camera::update()", cited_by=("Camera::update()",)) is not None
    # The function was renamed since: it is still the only one citing the address, so the stamp still counts.
    assert report.latest("compared", "0x2", function="New::name()", cited_by=("New::name()",)) is not None
    # With a second function citing the address, a stamp that names neither belongs to neither.
    assert (
        report.latest("compared", "0x1", function="Camera::update()", cited_by=("Camera::update()", "B::b()")) is None
    )


def test_the_seal_names_the_stamp_before_it(tmp_path: Path) -> None:
    clock = Clock()
    before = add(tmp_path, step="reviewed", subject="", clock=clock)
    seal = add(tmp_path, step="pr-opened", subject="", covers={"title_sha256": SHA}, clock=clock)
    assert seal.covers["chain_head"] == before.hash
    assert stamps.verify(tmp_path, rules()).ok
    edit_line(tmp_path, 1, lambda o: o["covers"].update(chain_head=SHA))
    assert any("chain_head" in text for text in problems(tmp_path))


def test_who_is_hook_sourced_only_for_a_token_the_hook_issued(tmp_path: Path) -> None:
    token = "0123456789abcdef0123456789abcdef"
    (tmp_path / identity_file_name(token)).write_text(
        json.dumps(
            {
                "version": 1,
                "id": "a1",
                "type": "engineer",
                "model": "claude-opus-5-5",
                "issued": 0,
                "expires": 1e18,
            }
        ),
        encoding="utf-8",
    )
    hooked = stamps.who_from_env("coney-tools orig study", {"CONEY_AGENT_TOKEN": token}, folder=tmp_path)
    assert hooked["source"] == "hook"
    assert hooked["agent"] == {"id": "a1", "type": "engineer", "model": "claude-opus-5-5"}
    # The three variables and the old marker are typed text: they prove nothing.
    typed = {
        "CONEY_AGENT_ID": "a1",
        "CONEY_AGENT_TYPE": "reviewer",
        "CONEY_AGENT_MODEL": "Opus",
        "CONEY_AGENT_INJECTED": "1",
    }
    forged = stamps.who_from_env("t", typed, folder=tmp_path)
    assert forged["source"] == "unverified" and forged["agent"] == {
        "id": "unknown",
        "type": "unknown",
        "model": "unknown",
    }
    other = stamps.who_from_env("t", {"CONEY_AGENT_TOKEN": "f" * 32}, folder=tmp_path)
    assert other["source"] == "unverified" and other["agent"]["type"] == "unknown"
    assert forged["kind"] == "tool" and forged["tool"] == "t"


def test_a_check_that_needs_a_log_refuses_a_missing_or_empty_one(tmp_path: Path) -> None:
    for folder in (tmp_path / "none", tmp_path):
        report = stamps.verify(folder, rules(), require_stamps=True)
        assert not report.ok and "no stamps" in report.problems[0].text
        assert stamps.verify(folder, rules()).ok  # otherwise an empty log is just a start


def test_the_log_must_belong_to_the_expected_task(tmp_path: Path) -> None:
    seed(tmp_path, 2)
    assert stamps.verify(tmp_path, rules(), task="t").ok
    report = stamps.verify(tmp_path, rules(), task="another")
    assert not report.ok and "task" in report.problems[0].text


def test_a_hash_with_a_trailing_newline_or_a_boolean_version_is_refused(tmp_path: Path) -> None:
    with pytest.raises(StampError, match="covers"):
        add(tmp_path, covers={"x": SHA + "\n"})
    seed(tmp_path, 2)
    edit_line(tmp_path, 0, lambda o: o.update(v=True))
    assert any("version" in text for text in problems(tmp_path))
    other = tmp_path / "seq"
    seed(other, 1)
    edit_line(other, 0, lambda o: o.update(seq=True))
    assert problems(other)


def test_a_pass_that_a_later_fail_replaced_is_not_current(tmp_path: Path) -> None:
    clock = Clock()
    covers = {"decompile_sha256": SHA}
    add(tmp_path, step="compared", subject="0x1", covers=covers, clock=clock)
    assert stamps.verify(tmp_path, rules()).current("compared", "0x1", covers) is not None
    add(tmp_path, step="compared", subject="0x1", result="fail", covers=covers, clock=clock)
    assert stamps.verify(tmp_path, rules()).current("compared", "0x1", covers) is None
    add(tmp_path, step="compared", subject="0x1", covers=covers, clock=clock)
    assert stamps.verify(tmp_path, rules()).current("compared", "0x1", covers) is not None


def entry(subject: str, **more: Any) -> dict[str, Any]:
    """The arguments of one stamp for append_many."""
    base: dict[str, Any] = {
        "step": "fails-first",
        "subject": subject,
        "result": "pass",
        "commit": COMMIT,
        "covers": {"x": SHA},
        "detail": {},
        "who": {"kind": "tool", "tool": "test"},
    }
    return {**base, **more}


def test_append_many_writes_all_or_none(tmp_path: Path) -> None:
    written = stamps.append_many(tmp_path, rules=rules(), task="t", entries=[entry("a"), entry("b")], clock=Clock())
    assert [s.subject for s in written] == ["a", "b"] and [s.seq for s in written] == [1, 2]
    assert written[1].prev == written[0].hash and stamps.verify(tmp_path, rules()).ok
    with pytest.raises(StampError, match="result"):
        stamps.append_many(tmp_path, rules=rules(), task="t", entries=[entry("c"), entry("d", result="maybe")])
    assert [s.subject for s in stamps.verify(tmp_path, rules()).stamps] == ["a", "b"]


def legacy_row(seq: int, prev: str, sig: Any, step: str = "studied") -> bytes:
    """A line as stamps were written while they were signed: the same keys plus `sig`, outside the hashed body."""
    body: dict[str, Any] = {
        "v": 1,
        "seq": seq,
        "task": "t",
        "step": step,
        "subject": "0x00100000",
        "result": "pass",
        "commit": COMMIT,
        "covers": {"decompile_sha256": SHA},
        "detail": {},
        "who": {"kind": "tool", "tool": "test"},
        "time": f"2026-10-22T12:00:0{seq}Z",
        "prev": prev,
    }
    return stamps.canonical({**body, "sig": sig}) + b"\n"


def test_a_log_written_with_signature_fields_still_reads_and_can_be_added_to(tmp_path: Path) -> None:
    armor = {"signer": "alice", "armor": "-----BEGIN SSH SIGNATURE-----\nAAAA\n-----END SSH SIGNATURE-----\n"}
    first = legacy_row(1, ZERO, None)
    first_hash = stamps.sha256_hex(stamps.canonical({k: v for k, v in json.loads(first).items() if k != "sig"}))
    second = legacy_row(2, first_hash, armor, step="checked")
    tmp_path.mkdir(exist_ok=True)
    (tmp_path / "0001.jsonl").write_bytes(first + second)
    report = stamps.verify(tmp_path, rules())
    assert report.ok and report.problems == [] and [s.seq for s in report.stamps] == [1, 2]
    assert report.stamps[1].line.encode("utf-8") + b"\n" == second  # the old line is kept as it was
    third = add(tmp_path, clock=Clock())
    assert third.prev == report.stamps[1].hash and "sig" not in third.data
    assert stamps.verify(tmp_path, rules()).ok  # a new stamp chains onto the old ones


def test_a_signature_field_that_is_neither_null_nor_an_object_is_refused(tmp_path: Path) -> None:
    (tmp_path / "0001.jsonl").write_bytes(legacy_row(1, ZERO, "not a signature"))
    assert any("sig" in text for text in problems(tmp_path))


def test_the_signature_of_an_old_stamp_is_not_checked_any_more(tmp_path: Path) -> None:
    (tmp_path / "0001.jsonl").write_bytes(
        legacy_row(1, ZERO, {"signer": "mallory", "armor": "nonsense"}, step="checked")
    )
    report = stamps.verify(tmp_path, rules())
    assert report.ok and report.problems == []


def test_the_current_rules_know_no_signing() -> None:
    assert not (REPO / ".github" / "stamp-signers").exists()
    assert not hasattr(rules(), "signed") and not hasattr(rules(), "namespace") and not hasattr(rules(), "signers_file")
    assert not hasattr(stamps, "Sign") and not hasattr(stamps, "Verifier")
