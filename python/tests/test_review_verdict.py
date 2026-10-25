# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the reviewer's verdict file (REVIEW.json): its shape, that it answers the packet it was made for, and the
detail a `reviewed` stamp keeps of it. Invented values only."""

import json
from typing import Any

import pytest
from review_support import make_packet, make_verdict

from coney_tools import review_verdict
from coney_tools.review_verdict import VerdictError

PACKET = make_packet()


def problems(verdict: dict[str, Any], packet: dict[str, Any] | None = None, minimum: int = 2) -> list[str]:
    """The problems of `verdict` against `packet` (default: the module's)."""
    return review_verdict.problems(verdict, packet or PACKET, minimum)


def test_a_complete_verdict_has_no_problem() -> None:
    assert problems(make_verdict(PACKET)) == []


def test_parse_reads_json_and_refuses_the_rest() -> None:
    assert review_verdict.parse(json.dumps({"a": 1})) == {"a": 1}
    with pytest.raises(VerdictError, match="not JSON"):
        review_verdict.parse("{")
    with pytest.raises(VerdictError, match="not a JSON object"):
        review_verdict.parse("[1]")


def test_the_keys_are_exactly_the_documented_ones() -> None:
    verdict = make_verdict(PACKET)
    del verdict["images"]
    verdict["extra"] = 1
    found = problems(verdict)
    assert any("missing the key 'images'" in p for p in found) and any("unknown key 'extra'" in p for p in found)


@pytest.mark.parametrize(
    ("key", "what"),
    [
        ("tip", "the tip"),
        ("base", "the base"),
        ("diff_sha256", "the change"),
        ("pairs_set_sha256", "the pairs"),
        ("title_sha256", "the title"),
        ("paragraph_sha256", "the paragraph"),
        ("images_sha256", "the pictures"),
        ("task", "the task"),
        ("mode", "the mode"),
    ],
)
def test_a_verdict_made_for_another_state_is_stale(key: str, what: str) -> None:
    verdict = make_verdict(PACKET, **{key: "f" * 64 if key.endswith("sha256") else "t9" if key == "task" else "x"})
    found = problems(verdict)
    assert any(what in p and "stale" in p for p in found), found


def test_the_rubric_version_must_match() -> None:
    assert any("rubric" in p for p in problems(make_verdict(PACKET, rubric_version=2)))


def test_every_kind_in_the_packet_must_be_applied() -> None:
    found = problems(make_verdict(PACKET, applied=["code", "text"]))
    assert any("research" in p and "orig" in p and "commits" in p for p in found)
    assert any("not a review kind" in p for p in problems(make_verdict(PACKET, applied=[*PACKET["kinds"], "prose"])))


def test_a_function_left_out_must_be_listed_with_a_reason() -> None:
    verdict = make_verdict(PACKET, functions=[], reviewed=[])
    assert any("Brain::think" in p and "neither reviewed nor listed" in p for p in problems(verdict))
    skipped = make_verdict(PACKET, functions=[], reviewed=[], not_reviewed=["Brain::think(int)"])
    assert any("`sampled` gives no reason" in p for p in problems(skipped))
    assert problems({**skipped, "sampled": "ten functions; read the riskiest three"}) == []


def test_a_function_is_checked_against_its_body_class_and_addresses() -> None:
    verdict = make_verdict(PACKET)
    entry = verdict["functions"][0]
    entry["body_sha256"] = "e" * 64
    assert any("reviewed at another body" in p for p in problems(verdict))
    entry["body_sha256"] = PACKET["functions"][0]["body_sha256"]
    entry["class"] = "great"
    assert any("not one of" in p for p in problems(verdict))
    entry["class"] = "clean"
    entry["addresses"] = ["0x00999999"]
    assert any("`addresses` must be its own and listed in `reviewed`" in p for p in problems(verdict))
    entry["addresses"] = []
    assert any("no compared `addresses`" in p for p in problems(verdict))
    entry["addresses"] = PACKET["functions"][0]["addresses"]
    entry["evidence"] = "sure"
    assert any("evidence" in p for p in problems(verdict))


def test_a_pass_cannot_hold_a_failing_class_or_a_blocking_finding() -> None:
    verdict = make_verdict(PACKET)
    verdict["functions"][0]["class"] = "diff-bug"
    assert any("pass" in p and "diff-bug" in p for p in problems(verdict))
    verdict["functions"][0]["class"] = "clean"
    verdict["findings"] = [{"severity": "important", "where": "src/ai/brain.cpp:9", "text": "no comment"}]
    assert any("pass" in p and "important" in p for p in problems(verdict))
    verdict["findings"] = [{"severity": "minor", "where": "src/ai/brain.cpp:9", "text": "wording"}]
    assert problems(verdict) == []


def test_a_fail_needs_a_reason() -> None:
    assert any("fail" in p and "reason" in p for p in problems(make_verdict(PACKET, verdict="fail")))
    bad = make_verdict(PACKET, verdict="fail")
    bad["functions"][0]["class"] = "transcription"
    assert problems(bad) == []
    found = make_verdict(
        PACKET, verdict="fail", findings=[{"severity": "critical", "where": "x:1", "text": "game data"}]
    )
    assert problems(found) == []


def test_unclear_passes_with_a_note() -> None:
    verdict = make_verdict(PACKET)
    verdict["functions"][0]["class"] = "unclear"
    assert problems(verdict) == []


def test_findings_have_a_known_severity_a_place_and_a_text() -> None:
    verdict = make_verdict(PACKET, findings=[{"severity": "blocker", "where": "", "text": "x"}, {"severity": "minor"}])
    found = problems(verdict)
    assert any("findings[0]" in p and "severity" in p for p in found)
    assert any("findings[1]" in p for p in found)


def test_each_research_page_needs_checked_addresses_of_its_own() -> None:
    verdict = make_verdict(PACKET)
    verdict["pages"][0]["checked"] = ["0x00100010"]
    assert any("docs/research/ai/brain.md" in p and "2 addresses" in p for p in problems(verdict))
    verdict["pages"][0]["checked"] = ["0x00100010", "0x00777777"]
    assert any("0x00777777" in p and "not on the page" in p for p in problems(verdict))
    verdict["pages"] = []
    assert any("docs/research/ai/brain.md" in p and "not reviewed" in p for p in problems(verdict))
    verdict["pages"] = [{"path": "docs/research/other.md", "checked": [], "unchecked": ""}]
    assert any(
        "docs/research/other.md" in p and "is not a research page of this review" in p for p in problems(verdict)
    )


def test_a_page_with_fewer_addresses_than_the_minimum_needs_them_all() -> None:
    packet = make_packet(pages=[{"path": "docs/research/ai/brain.md", "addresses": ["0x00100010"]}])
    assert problems(make_verdict(packet), packet) == []
    verdict = make_verdict(packet)
    verdict["pages"][0]["checked"] = []
    assert any("1 address" in p for p in problems(verdict, packet))


def test_an_unchecked_page_needs_a_reason_and_a_blocking_finding() -> None:
    verdict = make_verdict(PACKET)
    verdict["pages"][0] = {"path": PACKET["pages"][0]["path"], "checked": [], "unchecked": "Ghidra was down"}
    assert any("unchecked" in p and "finding" in p for p in problems(verdict))
    verdict["verdict"] = "fail"
    verdict["findings"] = [
        {"severity": "important", "where": PACKET["pages"][0]["path"], "text": "addresses not checked: Ghidra was down"}
    ]
    assert problems(verdict) == []


def test_every_picture_is_listed_with_its_hash_and_what_it_shows() -> None:
    verdict = make_verdict(PACKET)
    verdict["images"][0]["sha256"] = "9" * 64
    assert any("changed since the review" in p for p in problems(verdict))
    verdict["images"][0]["sha256"] = PACKET["text"]["images"][0]["sha256"]
    verdict["images"][0]["shows"] = ""
    assert any("shows" in p for p in problems(verdict))
    verdict["images"] = []
    assert any("./shots/after.png" in p and "not reviewed" in p for p in problems(verdict))


def test_a_text_review_needs_only_the_text() -> None:
    packet = make_packet(mode="text", kinds=["text"], functions=[], pages=[])
    verdict = make_verdict(packet)
    assert problems(verdict, packet) == []
    assert verdict["functions"] == [] and verdict["pages"] == []


def test_the_stamp_detail_keeps_the_verdict_in_a_kilobyte() -> None:
    verdict = make_verdict(PACKET)
    verdict["findings"] = [
        {"severity": "minor", "where": "a:1", "text": "x"},
        {"severity": "important", "where": "a:2", "text": "y"},
    ]
    verdict["verdict"] = "fail"
    detail = review_verdict.stamp_detail(verdict)
    assert detail["verdict"] == "fail" and detail["scope"] == "full" and detail["kinds"] == PACKET["kinds"]
    assert detail["functions"] == {"0x00100010": "c"}
    assert detail["pages"] == ["docs/research/ai/brain.md"]
    assert (detail["blockers"], detail["notes"]) == (1, 1)


def test_a_long_function_list_leaves_the_detail_and_a_pointer() -> None:
    functions = [
        {
            "key": f"F{n}",
            "body_sha256": "c" * 64,
            "addresses": [f"0x{0x100000 + n:08x}"],
            "class": "diff-deliberate-ok",
            "evidence": "inferred",
            "note": "",
        }
        for n in range(200)
    ]
    detail = review_verdict.stamp_detail(make_verdict(PACKET, functions=functions))
    assert "functions" not in detail and detail["function_count"] == 200
    assert len(json.dumps(detail)) < 2048


def test_the_class_letters_are_one_per_class() -> None:
    letters = review_verdict.CLASS_LETTERS
    assert len(set(letters.values())) == len(letters) == 7
    assert letters["clean"] == "c" and letters["diff-bug"] == "b"


# --- entries are checked against the packet before any is read -------------------------------------------------------


@pytest.mark.parametrize("field", ["functions", "pages", "images"])
@pytest.mark.parametrize("entry", ["text", 3, None, ["x"]])
def test_an_entry_that_is_not_an_object_is_a_problem_not_a_traceback(field: str, entry: object) -> None:
    verdict = make_verdict(PACKET)
    verdict[field].append(entry)
    found = problems(verdict)
    assert any(f"{field}[" in p and "expected an object" in p for p in found)


def test_a_function_the_packet_does_not_have_is_refused() -> None:
    verdict = make_verdict(PACKET)
    verdict["functions"].append({**verdict["functions"][0], "key": "src/x.cpp::Other::think"})
    assert any("is not an @orig function of this review" in p for p in problems(verdict))


def test_a_function_listed_twice_is_refused() -> None:
    verdict = make_verdict(PACKET)
    verdict["functions"].append(dict(verdict["functions"][0]))
    assert any("listed twice" in p for p in problems(verdict))


def test_a_function_with_an_unknown_key_or_wrong_types_is_refused_and_never_recorded() -> None:
    verdict = make_verdict(PACKET)
    verdict["functions"][0]["addresses"] = "0x00100010"
    verdict["functions"][0]["extra"] = 1
    found = problems(verdict)
    assert any("unknown keys extra" in p for p in found)
    assert any("'addresses' must be a list of text" in p for p in found)


def test_a_page_the_packet_does_not_have_or_with_wrong_types_is_refused() -> None:
    verdict = make_verdict(PACKET)
    verdict["pages"].append({"path": "docs/research/other.md", "checked": [], "unchecked": ""})
    verdict["pages"][0]["checked"] = "0x00100010"
    verdict["pages"][0]["unchecked"] = 5
    found = problems(verdict)
    assert any("'docs/research/other.md' is not a research page of this review" in p for p in found)
    assert any("'checked' must be a list of text" in p for p in found)
    assert any("'unchecked' must be text or null" in p for p in found)


def test_a_picture_the_packet_does_not_have_is_refused() -> None:
    verdict = make_verdict(PACKET)
    verdict["images"].append({"path": "./nowhere.png", "sha256": "e" * 64, "shows": "x"})
    assert any("is not a picture of this review" in p for p in problems(verdict))


def test_lists_of_the_wrong_type_are_problems() -> None:
    verdict = make_verdict(PACKET, applied=[1], reviewed=[2], not_reviewed=[["a"]])
    found = problems(verdict)
    assert {f"{k}: expected a list of text" for k in ("applied", "reviewed", "not_reviewed")} <= set(found)


def test_not_reviewed_names_only_packet_functions() -> None:
    verdict = make_verdict(PACKET, not_reviewed=["nope"], sampled="a large change")
    assert any("'nope' is not an @orig function of this review" in p for p in problems(verdict))


def test_a_text_verdict_lists_no_functions_or_pages() -> None:
    packet = make_packet(mode="text", functions=[], pages=[], kinds=["text"])
    assert review_verdict.problems(make_verdict(packet), packet, 2) == []
    verdict = make_verdict(packet, reviewed=["0x00100010"])
    verdict["functions"] = [{"key": "k"}]
    verdict["pages"] = [{"path": "docs/research/ai/brain.md"}]
    found = review_verdict.problems(verdict, packet, 2)
    for field in ("functions", "pages", "reviewed"):
        assert any(p.startswith(f"{field}: a text review has none") for p in found)


# --- an Xbox port (`@orig xbox`) is compared by hand, at addresses of its own ---------------------------------------


def xbox_packet() -> dict[str, Any]:
    """A packet whose one function follows the Xbox port only: no PS2 address, one in `default.xbe`."""
    function = {**PACKET["functions"][0], "addresses": [], "xbox": ["0x000bd3c0"]}
    return make_packet(functions=[function])


def test_an_xbox_only_function_can_be_given_a_verdict() -> None:
    packet = xbox_packet()
    verdict = make_verdict(packet)
    assert verdict["functions"][0]["addresses"] == ["xbox 0x000bd3c0"]
    assert verdict["reviewed"] == ["xbox 0x000bd3c0"]
    assert problems(verdict, packet) == []


def test_an_xbox_address_is_named_with_its_build_and_never_as_a_ps2_one() -> None:
    packet = xbox_packet()
    verdict = make_verdict(packet)
    verdict["functions"][0]["addresses"] = ["0x000bd3c0"]
    verdict["reviewed"] = ["0x000bd3c0"]
    assert any("`addresses` must be its own and listed in `reviewed`" in p for p in problems(verdict, packet))
    verdict["functions"][0]["addresses"] = []
    assert any("no compared `addresses`" in p for p in problems(verdict, packet))


def test_a_function_with_both_builds_may_compare_either_or_both() -> None:
    function = {**PACKET["functions"][0], "xbox": ["0x000bd3c0"]}
    packet = make_packet(functions=[function])
    verdict = make_verdict(packet)
    assert verdict["functions"][0]["addresses"] == ["0x00100010", "xbox 0x000bd3c0"]
    assert problems(verdict, packet) == []


def test_the_stamp_detail_gives_an_xbox_address_its_class_letter() -> None:
    packet = xbox_packet()
    detail = review_verdict.stamp_detail(make_verdict(packet))
    assert detail["functions"] == {"xbox 0x000bd3c0": "c"}
