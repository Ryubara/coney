# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.issue_claim_model: the command grammar, scopes, the claim card and its hidden JSON."""

from dataclasses import replace
from datetime import UTC, datetime, timedelta

import pytest
from audit_support import CONVENTIONS

from coney_tools import issue_claim_model as model
from coney_tools.issue_claim_model import Card, Command, Rejected

RULES = replace(CONVENTIONS.claims, release=True)
T0 = datetime(2026, 10, 22, 14, 3, 11, tzinfo=UTC)


def card(**changes: object) -> Card:
    """A claim of issue 301 by Ryubara at T0, with some fields changed."""
    return Card(issue=301, claimant="Ryubara", claimed_at=T0, **changes)  # type: ignore[arg-type]


# --- commands -------------------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("text", "expected"),
    [
        ("/claim", Command("claim")),
        ("  /claim  \n", Command("claim")),
        ("/claim scope: src/ai/, 0x00322B78", Command("claim", scope=("src/ai/", "0x00322b78"))),
        ("/claim task: 301-fight", Command("claim", task="301-fight")),
        (
            "/claim scope: src/ai/, tests/ai.cpp task: 301-fight",
            Command("claim", scope=("src/ai/", "tests/ai.cpp"), task="301-fight"),
        ),
        ("/claim renew", Command("renew")),
        ("/unclaim", Command("unclaim")),
        ("/progress studied", Command("progress", stage="studied")),
        ("/progress fails-first", Command("progress", stage="fails-first")),
        ("/claim scope: ./src/ai/, src/ai/", Command("claim", scope=("src/ai/",))),
    ],
)
def test_the_four_commands_parse(text: str, expected: Command) -> None:
    assert model.parse_command(text, RULES) == expected


@pytest.mark.parametrize(
    "text",
    [
        "I will /claim this",
        "> /claim",
        "please do /unclaim",
        "claim",
        "Thanks, /progress studied",
        "",
        "   ",
        "random text\n/claim",
    ],
)
def test_a_comment_that_is_not_a_command_is_ignored(text: str) -> None:
    assert model.parse_command(text, RULES) is None


@pytest.mark.parametrize(
    ("text", "why"),
    [
        ("/claim now", "not a command"),
        ("/claim scope:", "not a command"),
        ("/claim scope: src/ai/ extra: x", "not a command"),
        ("/claim renew now", "not a command"),
        ("/claim scope: a b$c", "not a command"),
        ("/unclaim please", "not a command"),
        ("/progress", "not a command"),
        ("/progress Studied", "not a command"),
        ("/progress nonsense", "not a stage"),
        ("/claim scope: ../etc/passwd", "is not a folder"),
        ("/claim scope: src/ai/\n/unclaim", "one line"),
        ("/claim task: UPPER", "not a command"),
        ("/claim scope: " + ", ".join(f"d{n}/" for n in range(41)), "41 entries"),
    ],
)
def test_a_comment_that_starts_like_a_command_gets_the_reason(text: str, why: str) -> None:
    result = model.parse_command(text, RULES)
    assert isinstance(result, Rejected) and why in result.reason


def test_the_reply_to_a_rejected_command_shows_the_grammar() -> None:
    result = model.parse_command("/claim now", RULES)
    assert isinstance(result, Rejected) and "/unclaim" in result.reason and "/progress <stage>" in result.reason


def test_scope_tokens_are_cleaned_and_repeats_dropped() -> None:
    assert model.parse_scope(" src/a/ ,, 0xABCDEF01 , src/a/ ") == ("src/a/", "0xabcdef01")
    with pytest.raises(ValueError, match="empty"):
        model.parse_scope(" , ")


@pytest.mark.parametrize(
    ("first", "second", "shared"),
    [
        (("src/ai/",), ("src/ai/goals.cpp",), ["src/ai/"]),
        (("src/ai/goals.cpp",), ("src/ai/",), ["src/ai/goals.cpp"]),
        (("src/ai/", "0x00322b78"), ("0x00322b78",), ["0x00322b78"]),
        (("src/ai/",), ("src/aim/x.cpp",), []),
        (("src/ai/a.cpp",), ("src/ai/b.cpp",), []),
        ((), ("src/ai/",), []),
    ],
)
def test_scopes_overlap_by_folder_or_address(
    first: tuple[str, ...], second: tuple[str, ...], shared: list[str]
) -> None:
    assert model.scopes_overlap(first, second) == shared


# --- the card -------------------------------------------------------------------------------------------------------


def test_a_card_survives_its_text() -> None:
    original = card(
        scope=("src/ai/", "0x00322b78"),
        task="301-fight",
        stage="studied",
        last_activity=T0 + timedelta(hours=2),
        last_activity_why="progress",
        warned_at=T0 + timedelta(hours=50),
        warning_comment=77,
        pr=412,
        status="pr-open",
        handled=(4411, 4415),
        open_claims=(301, 305),
    )
    text = model.render_card(original, RULES)
    assert model.parse_card(text, RULES) == original
    assert model.parse_card(text.replace("\n", "\r\n"), RULES) == original


def test_a_released_card_keeps_why_and_when() -> None:
    ended = card(status="released", released_at=T0 + timedelta(hours=72), released_why="idle 72 h")
    text = model.render_card(ended, RULES)
    assert model.parse_card(text, RULES) == ended
    assert "released (idle 72 h)" in text and "**Ended:**" in text and "Expiration" not in text


def test_the_first_line_is_the_marker_and_the_json_is_the_last() -> None:
    lines = model.render_card(card(), RULES).splitlines()
    assert lines[0] == "<!-- coney-claim -->" and lines[-1].startswith("<!-- coney-claim-data {")
    assert lines[-1].endswith("} -->")


def test_the_expiration_is_a_live_relative_time_with_the_exact_deadline_inside() -> None:
    text = model.render_card(card(), RULES)
    deadline = T0 + timedelta(hours=72)
    assert '<relative-time datetime="2026-10-25T14:03:11Z">2026-10-25 14:03 UTC</relative-time>' in text
    assert model.parse_time("2026-10-25T14:03:11Z") == deadline


def test_activity_moves_the_deadline() -> None:
    later = card(last_activity=T0 + timedelta(hours=30))
    assert later.deadline(RULES) == T0 + timedelta(hours=102)
    assert later.warn_at(RULES) == T0 + timedelta(hours=78)
    assert card().activity == T0


def test_the_visible_text_names_the_task_scope_and_open_claims() -> None:
    text = model.render_card(card(task="301-fight", scope=("src/ai/",), open_claims=(301, 305)), RULES)
    assert "Claimed by @Ryubara" in text and "`301-fight`" in text and "`src/ai/`" in text
    assert "#301, #305" in text
    assert "not stated" in model.render_card(card(), RULES)


def test_an_adopted_claim_says_it_was_assigned_by_hand() -> None:
    assert "assigned by hand" in model.render_card(card(adopted=True), RULES)


def test_a_hostile_value_cannot_close_the_hidden_comment() -> None:
    nasty = card(released_why="--> <script>alert(1)</script>", status="released")
    text = model.render_card(nasty, RULES)
    last = text.splitlines()[-1]
    assert last.count("-->") == 1 and "<script>" not in last
    assert model.parse_card(text, RULES) == nasty


def test_only_the_json_counts_not_the_words() -> None:
    text = model.render_card(card(), RULES).replace("Claimed by @Ryubara", "Claimed by @someone-else")
    assert model.parse_card(text, RULES) == card()


@pytest.mark.parametrize(
    "body",
    [
        "no card here",
        "<!-- coney-claim-data {not json} -->",
        '<!-- coney-claim-data {"v":2} -->',
        '<!-- coney-claim-data {"v":1,"issue":1,"claimant":"x","claimed_at":"yesterday","status":"claimed"} -->',
        '<!-- coney-claim-data {"v":1,"issue":1,"claimant":"x","claimed_at":"2026-10-22T14:03:11Z","status":"odd"} -->',
        '<!-- coney-claim-data {"v":1,"claimant":"x"} -->',
    ],
)
def test_a_damaged_or_foreign_card_is_not_a_card(body: str) -> None:
    assert model.parse_card(body, RULES) is None


def test_handled_ids_keep_the_most_recent() -> None:
    held = card(handled=tuple(range(model.HANDLED_KEPT)))
    assert model.handled_with(held, 5) == held.handled
    assert model.handled_with(held, 999)[-1] == 999 and len(model.handled_with(held, 999)) == model.HANDLED_KEPT
    assert model.handled_with(held, 999)[0] == 1


def test_a_task_counts_as_one_claim_key() -> None:
    assert card(task="t").task_key == "t" and card().task_key == "issue-301"


def test_replies_are_found_by_their_first_line() -> None:
    line = model.reply_first_line(RULES, 4411)
    assert line == "<!-- coney-claim-reply:4411 -->"
    assert model.reply_target(line + "\nno", RULES) == 4411
    assert model.reply_target("text", RULES) is None
    assert model.is_card("<!-- coney-claim -->\nx", RULES) and not model.is_card("hello", RULES)


def test_blocked_by_numbers_are_the_references_once_each() -> None:
    text = "#317 (a pull request, open) and the spec (#335); see also #317 and a/b#9 and &#39;"
    assert model.blocked_by_numbers(text) == [317, 335]


def test_a_line_that_says_what_the_issue_blocks_names_no_blocker() -> None:
    # "Blocks the release of #N" under Blocked by says what waits for this issue, not what this issue waits for.
    text = "Blocks the release of #1102.\n- blocks #12 too\n#7 (the spec)"
    assert model.blocked_by_numbers(text) == [7]
    assert model.blocked_by_numbers("Blocks the release of #1102") == []
