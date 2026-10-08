# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for mission-level differential playthroughs: hook texts, event logs and their comparison, the adaptive course
and the playthrough loop. Every trace here is synthetic; nothing needs PCSX2, Coney or game data."""

from __future__ import annotations

import itertools
import math
import struct
from collections.abc import Sequence
from pathlib import Path

import pytest

from coney_tools.course import (
    CROSS,
    SQUARE,
    TRIANGLE,
    Level99Street,
    Level99Tutorial,
    Observation,
    Seen,
    pad_of,
    stick_towards,
)
from coney_tools.events import (
    CallRule,
    Event,
    EventError,
    HookEvents,
    Rules,
    compare,
    events_csv,
    first_arguments,
    load_rules,
    lua_argument,
    normalize,
    original_events,
    parse_events,
    report,
    starting_at,
    text_labels,
    text_name,
)
from coney_tools.hooks import RING_BASE, HookError, RingReader, decode_text, entry_row, parse_hook
from coney_tools.input_script import PadState
from coney_tools.mission import load_mission, pad_line, parse_event_line, parse_observation, play
from coney_tools.pine import Read, Write

REPO = Path(__file__).resolve().parents[2]


class Bytes:
    """A fake EE memory: a byte array, read in little-endian words as PINE gives them."""

    def __init__(self, size: int = 0x00200000) -> None:
        """Zeroed memory of `size` bytes."""
        self.data = bytearray(size)

    def put(self, address: int, raw: bytes) -> None:
        """Write `raw` at `address`."""
        self.data[address : address + len(raw)] = raw

    def batch(self, reads: Sequence[Read], writes: Sequence[Write] = ()) -> list[int]:
        """Apply the writes, then answer the reads."""
        for write in writes:
            self.put(write.address, write.value.to_bytes(write.size, "little"))
        return [int.from_bytes(self.data[r.address : r.address + r.size], "little") for r in reads]


# --- hook texts ---------------------------------------------------------------------------------------------------


def _call_hook() -> object:
    """A hook logging a pointer and a type tag, with the text behind the pointer when the tag is 3."""
    return parse_hook(
        "call",
        {
            "address": 0x00328A30,
            "original": [0x27BDFFB0, 0x7FB00040],
            "log": [
                {"value": "[a2 + 0x0]", "name": "t1"},
                {"value": "[a2 + 0x8]", "name": "v1", "text": 32, "text_offset": 0x10, "text_when": "t1=3"},
            ],
        },
        "test",
    )


def test_a_hook_reads_the_text_its_value_points_to_when_asked() -> None:
    hook = _call_hook()
    memory = Bytes()
    memory.put(0x1000 + 0x10, b"P1.BasicAttacks\0")
    entry = RING_BASE + 0x10

    def add(n: int, tag: int, pointer: int) -> None:
        """Ring entry `n`: hook id 1, the tag and the pointer."""
        memory.put(entry + 32 * n, struct.pack("<III", 1, tag, pointer))

    add(0, 3, 0x1000)
    add(1, 2, 0x1000)  # a number: no text read
    memory.put(RING_BASE, struct.pack("<I", 2))
    ring = RingReader(memory, [hook])  # type: ignore[list-item]
    ring.drain(2, 5)
    entries = ring.log.entries["call"]
    assert [values[-1] for _, _, values in entries] == ["P1.BasicAttacks", ""]
    assert entry_row(hook, *entries[0]) == {  # type: ignore[arg-type]
        "seq": "0",
        "step": "5",
        "t1": "0x3",
        "v1": "0x1000",
        "v1_text": "P1.BasicAttacks",
    }
    assert ring.log.csv(hook).splitlines()[0] == "seq,step,t1,v1,v1_text"  # type: ignore[arg-type]


def test_a_text_is_utf16_when_its_second_byte_is_zero() -> None:
    assert decode_text("Hi".encode("utf-16-le") + b"\0\0junk") == "Hi"
    assert decode_text(b"plain\0rest") == "plain"


def test_a_text_condition_must_name_a_logged_value() -> None:
    with pytest.raises(HookError, match="text_when"):
        parse_hook(
            "bad",
            {
                "address": 0x1000,
                "original": [0, 0],
                "log": [{"value": "a1", "name": "v", "text": 8, "text_when": "x=3"}],
            },
            "test",
        )


# --- event logs ---------------------------------------------------------------------------------------------------


def test_an_event_log_round_trips_through_csv() -> None:
    events = [Event(1, "call", "F", '"a, b", 2'), Event(3, "hint", "line\nbreak")]
    assert parse_events(events_csv(events)) == events
    with pytest.raises(EventError, match="not an event log"):
        parse_events("a,b\n1,2\n")


def test_call_arguments_split_at_top_level_commas() -> None:
    assert first_arguments('"a, b", {1, 2}, nil') == ['"a, b"', "{1, 2}", "nil"]
    assert first_arguments("") == []


def test_a_lua_argument_reads_as_coney_shows_it() -> None:
    low, high = struct.unpack("<II", struct.pack("<d", 2.5))
    assert lua_argument(2, low, high, "") == "2.5"
    assert lua_argument(3, 0, 0, "P1.Target") == '"P1.Target"'
    assert lua_argument(1, 0, 0, "") == "nil"


def _rules() -> Rules:
    """Rules like the repository's, small."""
    return Rules(
        calls={"HUDSetTutorialCallback": CallRule("tutorial"), "SetCheckPoint": CallRule("checkpoint")},
        kinds=("hint", "tutorial", "checkpoint", "callback", "sound", "human_out"),
        frequent=5,
        window=50,
        burst=3,
        progress=("tutorial", "checkpoint"),
        by_set=("sound",),
        unnamed=("human_out",),
    )


def test_normalizing_names_a_binding_by_its_argument_and_hashes_long_texts() -> None:
    rules = _rules()
    assert normalize(Event(1, "call", "SetCheckPoint", "2"), rules) == Event(1, "checkpoint", "SetCheckPoint:2", "2")
    long_text = "<COLOR B2B2B2FF>" + "x" * 50
    assert normalize(Event(1, "hint", long_text), rules).name == text_name(long_text)
    assert text_name(long_text).startswith("text#")
    assert normalize(Event(1, "human_out", "Ash"), rules).name == ""
    assert text_labels({"TT_1": long_text}) == {text_name(long_text): "TT_1"}
    # A nil argument names nothing, so a call the original logged without it matches.
    assert normalize(Event(1, "call", "HUDSetAnnounceMsg", "0, nil"), rules).name == "HUDSetAnnounceMsg"


def test_two_logs_are_cut_to_start_at_the_same_event() -> None:
    events = [Event(5, "hint", "a"), Event(9, "callback", "P2.Loot", "10"), Event(12, "hint", "b")]
    assert starting_at(events, "callback P2.Loot") == [Event(0, "callback", "P2.Loot", "10"), Event(3, "hint", "b")]
    assert starting_at(events, "") == events
    with pytest.raises(EventError):
        starting_at(events, "callback P9")


def test_the_original_events_come_in_ring_order_with_binding_names_and_their_own_arguments() -> None:
    logs = {
        "call": [
            {"seq": "2", "step": "7", "function": "0x36f380", "t1": "0x3", "v1": "0", "h1": "0", "v1_text": "T"},
            {
                "seq": "1",
                "step": "7",
                "function": "0x37b760",
                "t1": "0x2",
                "v1": "0x0",
                "h1": "0x40000000",
                "t2": "0x3",
                "v2": "0",
                "h2": "0",
                "v2_text": "junk",
            },
        ],
        "sound": [{"seq": "3", "step": "8", "hash": "0x1234"}],
    }
    mapping = [
        HookEvents(
            "call", "call", binding="function", args=(("t1", "v1", "h1", "v1_text"), ("t2", "v2", "h2", "v2_text"))
        ),
        HookEvents("sound", "sound", name="hash", format="hex"),
    ]
    events = original_events(
        logs, mapping, {0x36F380: "HUDSetTutorialText", 0x37B760: "SetCheckPoint"}, {"SetCheckPoint": 1}
    )
    assert events == [
        Event(7, "call", "SetCheckPoint", "2"),
        Event(7, "call", "HUDSetTutorialText", '"T"'),
        Event(8, "sound", "0x00001234"),
    ]


def _cb(step: int, name: str) -> Event:
    """A tutorial callback being armed."""
    return Event(step, "call", "HUDSetTutorialCallback", f'"{name}"')


def test_compare_finds_missing_extra_and_out_of_order_events_between_milestones() -> None:
    original = [
        _cb(10, "L1"),
        Event(12, "hint", "A"),
        Event(14, "hint", "B"),
        Event(15, "callback", "X"),
        Event(16, "callback", "Y"),
        _cb(30, "L2"),
        Event(31, "hint", "C"),
        Event(40, "call", "SetCheckPoint", "2"),
    ]
    coney = [
        _cb(100, "L1"),
        Event(101, "hint", "A"),
        Event(105, "callback", "Y"),
        Event(106, "callback", "X"),
        Event(107, "hint", "Z"),
        _cb(130, "L2"),
    ]
    result = compare(original, coney, _rules())
    assert [(e.kind, e.name) for e in result.missing] == [
        ("hint", "B"),
        ("hint", "C"),
        ("checkpoint", "SetCheckPoint:2"),
    ]
    assert [(e.kind, e.name) for e in result.extra] == [("hint", "Z")]
    # X and Y swapped: one of them matches in order, the other is out of order.
    assert [(a.name == b.name, a.name in "XY") for a, b in result.out_of_order] == [(True, True)]
    assert result.reached is not None and result.reached.name == "HUDSetTutorialCallback:L2"
    assert result.stopped_before is not None and result.stopped_before.name == "SetCheckPoint:2"
    assert not result.ok
    text = report(result, labels={})
    assert "Coney never reached (original step): 40 checkpoint SetCheckPoint:2" in text


def test_a_hint_is_matched_only_between_the_milestones_it_came_between() -> None:
    original = [_cb(1, "L1"), Event(2, "hint", "A"), _cb(5, "L2")]
    coney = [_cb(1, "L1"), _cb(5, "L2"), Event(6, "hint", "A")]
    result = compare(original, coney, _rules())
    assert [(a.step, b.step) for a, b in result.out_of_order] == [(2, 6)]


def test_sounds_compare_by_set_bursts_collapse_and_frequent_events_by_count() -> None:
    original = [Event(1, "sound", "0x1"), Event(2, "sound", "0x2"), Event(3, "hint", "A"), Event(4, "hint", "A")]
    original += [Event(10 * k, "callback", "Update") for k in range(1, 10)]
    coney = [Event(1, "sound", "0x2"), Event(5, "sound", "0x3"), Event(3, "hint", "A")]
    result = compare(original, coney, _rules())
    assert [e.name for e in result.missing] == ["0x1"]
    assert [e.name for e in result.extra] == ["0x3"]
    assert result.counts == [("callback Update", 9, 0)]
    assert ("hint", "A") in {(a.kind, a.name) for a, _ in result.matched}
    assert result.ok is False


def test_timing_lists_milestones_whose_spacing_differs_beyond_the_window() -> None:
    original = [_cb(0, "L1"), _cb(100, "L2"), _cb(200, "L3")]
    coney = [_cb(0, "L1"), _cb(400, "L2"), _cb(500, "L3")]
    result = compare(original, coney, _rules())
    assert [(t.gap_original, t.gap_coney) for t in result.timing] == [(100, 400)]
    assert result.ok


def test_the_repository_rules_load() -> None:
    rules = load_rules(REPO / "research/traces/events.toml")
    assert rules.calls["SetCheckPoint"].kind == "checkpoint"
    assert {hook.hook for hook in rules.hooks} == {"event-call", "event-callback", "event-hint", "event-sound"}
    assert "sound" in rules.by_set


# --- the course ---------------------------------------------------------------------------------------------------


def _seen(**fields: object) -> Observation:
    """An observation in play: player at the origin facing +y, the camera behind him."""
    base: dict[str, object] = {
        "frame": 0,
        "play": True,
        "player": (0.0, 0.0, 0.0, 0.0),
        "camera": (0.0, -5.0, 0.0, 0.0),
    }
    base.update(fields)
    return Observation(**base)  # type: ignore[arg-type]


def test_the_stick_points_where_to_go_as_the_camera_looks() -> None:
    observation = _seen()
    assert stick_towards(observation, (0.0, 10.0), 60) == (0, 60)
    assert stick_towards(observation, (10.0, 0.0), 60) == (60, 0)
    assert pad_of(CROSS, 0, 100).sticks[3] < 0x80


def test_the_tutorial_course_walks_into_the_shown_marker_first() -> None:
    course = Level99Tutorial()
    course.see(_seen(objects=[("dyn_w_mission", 0.0, 5.0, 1), ("dyn_w_mission", 3.0, 0.0, -1)]), [])
    pad = course.pad()
    assert pad.buttons == 0 and pad.sticks[3] < 0x80 and pad.sticks[2] == 0x80


def test_the_tutorial_course_plays_the_armed_lesson_at_an_enemy_in_reach() -> None:
    course = Level99Tutorial()
    enemy = Seen(0.0, 1.0, True, True, True)
    course.see(_seen(humans=[enemy]), [_cb(1, "P1.BasicAttacks")])
    assert course.lesson == "P1.BasicAttacks"
    course.pad()  # one update of aim
    course.see(_seen(humans=[enemy]), [])
    assert course.pad().buttons == SQUARE


def test_the_tutorial_course_frees_the_player_when_a_walk_makes_no_headway() -> None:
    course = Level99Tutorial()
    far = Seen(0.0, 9.0, True, True, True)
    buttons: list[int] = []
    for _ in range(Level99Tutorial.STUCK + 3):
        course.see(_seen(humans=[far]), [_cb(1, "P1.LightCombos")] if not buttons else [])
        buttons.append(course.pad().buttons)
    assert buttons[0] == 0 and any(b for b in buttons)


def test_the_tutorial_course_takes_a_bat_after_the_throws() -> None:
    course = Level99Tutorial()
    course.see(_seen(), [_cb(1, "P1.Throws")])
    course.pad()
    course.see(_seen(objects=[("dyn_bat_tuff", 0.0, 0.3, -1)]), [Event(2, "call", "HUDSetTutorialCallback", "nil")])
    assert course.pad().buttons == TRIANGLE


def _run(course: Level99Street, observation: Observation, updates: int, events: Sequence[Event] = ()) -> list[PadState]:
    """The pads `course` answers over `updates` updates of the same observation (`events` on the first)."""
    pads = []
    for index in range(updates):
        course.see(observation, events if index == 0 else [])
        pads.append(course.pad())
    return pads


def test_the_street_course_breaks_a_cabinet_from_the_room_side_and_takes_the_item() -> None:
    # A cabinet's items in a row along y at x 5 and the rest of the loot east: the stand point is east of the row.
    loot = [("dyn_ring", 5.0, 1.0, -1), ("dyn_ring", 5.0, 1.5, -1), ("dyn_pwatch", 8.0, 3.0, -1)]
    course = Level99Street()
    course.see(_seen(objects=loot), [])
    assert course._stand((5.0, 1.0)) == (5.0 + Level99Street.STAND_OFF, 1.0)
    standing = _seen(player=(5.0 + Level99Street.STAND_OFF, 1.0, 0.0, 90.0), objects=loot)
    buttons = [pad.buttons for pad in _run(Level99Street(), standing, 120)]
    assert SQUARE in buttons and TRIANGLE in buttons
    assert buttons.index(SQUARE) < buttons.index(TRIANGLE)


def test_the_street_course_turns_the_stick_anticlockwise_at_the_stereo() -> None:
    stereo = ("dyn_carstereo", 0.0, 0.0, -1)
    course = Level99Street()
    beside = _seen(player=(Level99Street.STEREO_OFF, 0.0, 0.0, 90.0), objects=[stereo])
    pads = _run(course, beside, 160, [Event(1, "call", "CarSpawnRadio", "141")])
    buttons = [pad.buttons for pad in pads]
    assert SQUARE in buttons and TRIANGLE in buttons
    turning = [pad.sticks[2:] for pad in pads[-20:]]
    angles = [math.atan2(0x80 - y, x - 0x80) for x, y in turning]
    steps = [(b - a + math.pi) % (2 * math.pi) - math.pi for a, b in itertools.pairwise(angles)]
    assert all(step > 0 for step in steps)  # anticlockwise, y up
    assert all(math.hypot(x - 0x80, y - 0x80) > 100 for x, y in turning)  # fully out


def test_the_street_course_tries_a_stereo_from_another_side_after_a_miss() -> None:
    course = Level99Street()
    course.radio = True
    course.tries[(0.0, 0.0)] = 2
    course.see(_seen(player=(5.0, 5.0, 0.0, 0.0), objects=[("dyn_carstereo", 0.0, 0.0, -1)]), [])
    course.pad()
    # The third try stands 90 degrees round from the first: north of the stereo, so the walk goes up and left.
    assert course.status().startswith("radio")
    pad = course.pad()
    assert pad.sticks[2] < 0x80


# --- the playthrough ----------------------------------------------------------------------------------------------


def test_coney_lines_parse() -> None:
    observation = parse_observation(
        '{"frame":3,"play":true,"player":[1,2,0,90],"camera":[0,0,1,1],"humans":[[1,2,1,0,1]],'
        '"objects":[["dyn_bat_tuff",4,5,-1]]}'
    )
    assert observation.humans == [Seen(1.0, 2.0, True, False, True)]
    assert observation.objects == [("dyn_bat_tuff", 4.0, 5.0, -1)]
    assert parse_observation('{"frame":0,"play":false}').play is False
    assert parse_event_line('7,hint,"a, b",') == Event(7, "hint", "a, b", "")
    assert parse_event_line("x") is None
    assert pad_line(PadState(0x40, [128, 128, 200, 60])) == "pad 40 128 128 200 60\n"


class FakeGame:
    """A game whose script arms a lesson, then sets checkpoint 2 at update `done`."""

    def __init__(self, done: int) -> None:
        """Set the checkpoint at update `done`."""
        self.done = done
        self.update = 0
        self.pads: list[PadState] = []

    def start(self, objects: Sequence[str]) -> tuple[Observation, list[Event]]:
        """The first update."""
        return _seen(), [_cb(0, "P1.BasicAttacks")]

    def step(self, pad: PadState) -> tuple[Observation, list[Event]] | None:
        """One update; the checkpoint when due."""
        self.pads.append(pad)
        self.update += 1
        events = [Event(self.update, "call", "SetCheckPoint", "2")] if self.update == self.done else []
        return _seen(frame=self.update), events


def test_a_playthrough_stops_a_little_after_its_end_event() -> None:
    game = FakeGame(done=50)
    result = play(game, Level99Tutorial(), _rules(), 1000, ("checkpoint", "SetCheckPoint:2"), after=10)
    assert result.finished and result.updates == 60
    unfinished = play(FakeGame(done=5000), Level99Tutorial(), _rules(), 100, ("checkpoint", "SetCheckPoint:2"))
    assert not unfinished.finished and unfinished.updates == 100


def test_the_repository_mission_scenarios_load() -> None:
    scenario = load_mission(REPO / "research/traces/missions/level99_cp1.toml")
    assert scenario.course == "level99-tutorial"
    assert scenario.until == ("checkpoint", "SetCheckPoint:2")
    assert "event-call" in scenario.patches
    street = load_mission(REPO / "research/traces/missions/level99_cp2.toml")
    assert street.course == "level99-street"
    assert street.until == ("callback", "P2.CarRadioStolen")
