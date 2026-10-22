# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the stamp log: records, the append-only chain, segments, signatures and verification."""

import dataclasses
import json
import threading
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any

import pytest
from stamp_support import allowed_signers, make_key, needs_ssh_keygen, rules

from coney_tools import stamp_sign, stamps
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


def test_the_body_leaves_out_the_signature_and_the_hash_covers_the_body(tmp_path: Path) -> None:
    stamp = add(tmp_path)
    assert b'"sig"' not in stamp.body
    assert stamps.sha256_hex(stamp.body) == stamp.hash
    assert stamp.line.encode("utf-8").startswith(b"{")


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


def test_signed_steps_without_a_signature_are_a_warning_locally_and_an_error_for_a_release(tmp_path: Path) -> None:
    add(tmp_path, step="checked", subject="")
    local = stamps.verify(tmp_path, rules())
    assert local.ok and [p.severity for p in local.problems] == ["warning"]
    strict = stamps.verify(tmp_path, rules(), require_signatures=True)
    assert not strict.ok and "unsigned" in strict.problems[0].text


def fake_signer(body: bytes) -> dict[str, Any]:
    """A signer that signs by reversing the body's hash, for the log's tests."""
    return {"signer": "alice", "armor": "SIG:" + stamps.sha256_hex(body)}


def fake_verifier(body: bytes, armor: str, when: datetime) -> str:
    """The matching verifier."""
    if armor != "SIG:" + stamps.sha256_hex(body):
        raise stamp_sign.SignError("the signature does not verify: bad")
    return "alice"


def test_a_signed_stamp_verifies_and_a_changed_body_does_not(tmp_path: Path) -> None:
    clock = Clock()
    add(tmp_path, step="checked", subject="", sign=fake_signer, clock=clock)
    ok = stamps.verify(tmp_path, rules(), require_signatures=True, verifier=fake_verifier)
    assert ok.ok and ok.problems == []
    edit_line(tmp_path, 0, lambda o: o["detail"].update(statements=99))
    bad = stamps.verify(tmp_path, rules(), require_signatures=True, verifier=fake_verifier)
    assert not bad.ok and any("signature" in p.text for p in bad.problems)


def test_a_signature_on_an_unsigned_step_is_checked_too(tmp_path: Path) -> None:
    add(tmp_path, step="studied", sign=fake_signer)
    edit_line(tmp_path, 0, lambda o: o["sig"].update(armor="SIG:wrong"))
    assert any("signature" in text for text in problems(tmp_path, verifier=fake_verifier))


def test_a_failing_signer_writes_nothing(tmp_path: Path) -> None:
    def broken(body: bytes) -> dict[str, Any]:
        raise stamp_sign.SignError("no key in the agent")

    with pytest.raises(StampError, match="no key in the agent"):
        add(tmp_path, step="checked", subject="", sign=broken)
    assert lines(tmp_path) == []


def test_the_seal_names_the_stamp_before_it(tmp_path: Path) -> None:
    clock = Clock()
    before = add(tmp_path, step="reviewed", subject="", clock=clock)
    seal = add(tmp_path, step="pr-opened", subject="", sign=fake_signer, covers={"pr_text_sha256": SHA}, clock=clock)
    assert seal.covers["chain_head"] == before.hash
    assert stamps.verify(tmp_path, rules(), verifier=fake_verifier).ok
    edit_line(tmp_path, 1, lambda o: o["covers"].update(chain_head=SHA))
    assert any("chain_head" in text for text in problems(tmp_path))


@needs_ssh_keygen
def test_a_real_signed_chain_verifies_against_the_signers_file(tmp_path: Path) -> None:
    key = make_key(tmp_path / "keys")
    signers = allowed_signers(tmp_path / "signers", ("alice", key))
    folder = tmp_path / "log"
    rule = rules()
    sign = stamp_sign.signer_for(key.private, rule.namespace)
    assert sign is not None

    # signer_for takes the agent path (-U); the test signs with the private key file instead.
    def by_file(body: bytes) -> dict[str, Any]:
        return {"signer": "alice", "armor": stamp_sign.sign(body, key.private, rule.namespace, use_agent=False)}

    clock = Clock()
    add(folder, step="studied", clock=clock)
    add(folder, step="checked", subject="", sign=by_file, clock=clock)
    add(folder, step="reviewed", subject="", sign=by_file, clock=clock)
    add(folder, step="pr-opened", subject="", sign=by_file, covers={"pr_text_sha256": SHA}, clock=clock)
    report = stamps.verify(folder, rule, signers=signers, require_signatures=True)
    assert report.problems == [] and report.ok
    other = make_key(tmp_path / "other", comment="mallory")
    allowed_signers(signers, ("mallory", other))
    assert not stamps.verify(folder, rule, signers=signers, require_signatures=True).ok


def test_who_is_hook_sourced_only_when_the_marker_is_set() -> None:
    env = {"CONEY_AGENT_ID": "a1", "CONEY_AGENT_TYPE": "engineer", "CONEY_AGENT_MODEL": "Opus 5.5"}
    unverified = stamps.who_from_env("coney-tools original study", env)
    assert unverified["source"] == "unverified" and unverified["agent"]["id"] == "a1"
    hooked = stamps.who_from_env("coney-tools original study", {**env, "CONEY_AGENT_INJECTED": "1"})
    assert hooked["source"] == "hook" and hooked["agent"] == {"id": "a1", "type": "engineer", "model": "Opus 5.5"}
    empty = stamps.who_from_env("t", {})
    assert empty["source"] == "unverified" and empty["agent"] == {
        "id": "unknown",
        "type": "unknown",
        "model": "unknown",
    }
    assert stamps.who_from_env("t", {"CONEY_AGENT_INJECTED": "yes"})["source"] == "unverified"
    assert empty["kind"] == "tool" and empty["tool"] == "t"
