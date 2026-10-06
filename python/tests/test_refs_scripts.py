# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the reference lists read from the scripts' calls: text labels, commands, speech, sounds, messages.

Synthetic script facts only; no game data.
"""

from __future__ import annotations

from coney_tools import refs_scripts
from coney_tools.lua4 import Assignment, Call, CallResult, ChunkFacts, Global, Table

BUTTONS = ((0x0040, "cross", "<X>"), (0x0080, "square", "<S>"), (0x0100, "SELECT", "<SELECT>"))


def _call(callee: str, *args: object) -> Call:
    """A call in a script's main function."""
    return Call("main", 0, callee, list(args))  # type: ignore[arg-type]


def test_text_labels_lists_keys_languages_tables_and_uses() -> None:
    """Keys come from the per-language tables, a sub-table gives its ids, and uses name the callee and script."""
    language = Table(fields={"MS_1": "a", "MS_10": "b", "LIST": Table(items={1: "x", 2: "y", 4: "z"}), "GONE": None})
    level = ChunkFacts(assignments=[Assignment("main", 0, "LEVEL7", language)] * 2)
    chapter = ChunkFacts(
        calls=[
            _call("HUDSetObjective", 1.0, Global("LEVEL7.MS_1")),
            _call("?", Global("LEVEL7.MISSING")),
            _call("ObjectiveSetup", Table(items={1: Global("LEVEL7.MS_1")})),
        ]
    )
    entries = {e["id"]: e for e in refs_scripts.text_labels({"level7.lua": level, "level7_c1.lua": chapter})}
    assert [e for e in entries if e.startswith("LEVEL7.")] == list(entries)
    assert list(entries) == ["LEVEL7.LIST", "LEVEL7.MISSING", "LEVEL7.MS_1", "LEVEL7.MS_10"]
    assert entries["LEVEL7.LIST"]["ids"] == "1-2, 4" and entries["LEVEL7.LIST"]["size"] == 3
    used = entries["LEVEL7.MS_1"]
    assert used["languages"] == 2 and used["uses"] == 2 and used["defined_in"] == ["level7.lua"]
    assert used["passed_to"] == ["HUDSetObjective", "ObjectiveSetup"] and used["used_in"] == ["level7_c1.lua"]
    missing = entries["LEVEL7.MISSING"]
    assert missing["languages"] == 0 and missing["passed_to"] is None and missing["defined_in"] is None
    assert "LEVEL7.GONE" not in entries


def test_text_labels_counts_one_language_per_file() -> None:
    """A table built once per language file counts each file's language."""
    table = Table(fields={"TITLE": "t"})
    scripts = {
        f"level9_strings_{lang}.lua": ChunkFacts(assignments=[Assignment("main", 0, "LEVEL9", table)])
        for lang in ("en", "de")
    }
    (entry,) = refs_scripts.text_labels(scripts)
    assert entry["languages"] == 2


def test_commands_list_triggers_bindings_switches_and_warrior_commands() -> None:
    """Pad commands are named by trigger and buttons; combinations join both masks."""
    facts = ChunkFacts(
        calls=[
            _call("AddCommand", 15.0, 2.0, 128.0, 0.0),
            _call("AddCommand", 34.0, 9.0, 64.0, 128.0),
            _call("EnableCommand", Global("player"), 15.0, 0.0),
            _call("WCIssueCommand", Global("player"), 3.0, Global("true")),
            _call("IssueWarriorCommand", 5.0, Global("true")),
        ]
    )
    entries = {e["id"]: e for e in refs_scripts.commands({"global.lua": facts}, BUTTONS)}
    assert entries["trigger:9"]["name"] == "combination press"
    assert entries["pad:15"]["bound"] == ["pressed: square"] and entries["pad:15"]["toggled_in"] == ["global.lua"]
    assert entries["pad:34"]["bound"] == ["combination press: cross + square"]
    assert entries["warrior:3"]["used_in"] == ["global.lua"] and entries["warrior:5"]["used_in"] == ["global.lua"]
    assert entries["warrior:0"]["used_in"] is None and "warrior:7" not in entries


def test_voice_lines_probe_like_the_game() -> None:
    """Lines count from 01 and stop at the first missing one; command 0 has none."""
    known = {
        "vags/character/voices/2/attack_01",
        "vags/character/voices/2/attack_02",
        "vags/character/voices/2/attack_04",
    }
    lines = refs_scripts.voice_lines(3, ["nothing", "attack", "follow"], known.__contains__)
    assert lines == {(2, 1): 2}


def test_speech_lists_commands_and_voice_sets() -> None:
    """Commands carry their voice counts and chances; voice sets their types and script users."""
    facts = ChunkFacts(
        calls=[
            _call("SoundPlayCommand", Global("h"), 1.0, None),
            _call("SndSetCommandSoundPercent", -1.0, 1.0, 20.0),
            _call("HuSetStateRespVoiceIndex", Global("h"), 9.0),
        ]
    )
    entries = refs_scripts.speech({"a.lua": facts}, ["nothing", "attack"], {(2, 1): 3}, {2: [5, 4]})
    by_id = {e["id"]: e for e in entries}
    assert by_id["command:1"] == {
        "id": "command:1",
        "kind": "speech command",
        "number": 1,
        "name": "attack",
        "voices": 1,
        "lines": 3,
        "percent": ["every voice: 20 %"],
        "used_in": ["a.lua"],
    }
    assert by_id["voice:2"]["types"] == [4, 5] and by_id["voice:2"]["commands"] == 1
    assert by_id["voice:9"]["used_in"] == ["a.lua"] and by_id["voice:9"]["lines"] is None


def test_sound_names_reads_ambient_emitters_and_speech_lines() -> None:
    """Ambient slots group by name; speech lines get their whole sound name, wherever the name is built."""
    facts = ChunkFacts(
        calls=[
            _call("AddAmbientSound", 0.0, "vags/ambient/dog"),
            _call("AddAmbientSound", 3.0, "vags/ambient/dog"),
            _call("AddAmbientSoundEmitter2", "tDogs01", Table(), Table(), 0.0, "", 2.0),
            _call("HuSpeakNI", Global("h"), CallResult("SetVag", ("l3_t1_001",)), None),
            _call("HuSpeak", Global("h"), "vags/speeches/l31/l31_t7_001", "Done"),
        ]
    )
    known = {"vags/ambient/dog"}
    entries = {e["id"]: e for e in refs_scripts.sound_names({"l3.lua": facts}, known.__contains__)}
    assert entries["ambient:vags/ambient/dog"]["values"] == [0, 3] and entries["ambient:vags/ambient/dog"]["in_list"]
    assert entries["emitter:tDogs01"]["values"] == [0, 2] and entries["emitter:tDogs01"]["used_by"] == ["l3.lua"]
    assert entries["speech:vags/speeches/l3/l3_t1_001"]["in_list"] is False
    assert "speech:vags/speeches/l31/l31_t7_001" in entries and "speech:Done" not in entries


def test_message_handlers_count_callbacks_and_clears() -> None:
    """Names and nils are counted per message; other bindings are ignored."""
    facts = ChunkFacts(
        calls=[
            _call("GangSetMsgHandler", Global("g"), 18.0, "Dead"),
            _call("GangSetMsgHandler", Global("g"), 18.0, None),
            _call("SetMsgHandler", Global("h"), 18.0, "Other"),
        ]
    )
    handlers, clears = refs_scripts.message_handlers({"a.lua": facts}, "GangSetMsgHandler")
    assert dict(handlers[18]) == {"Dead": 1} and clears[18] == 1


def test_ranges_and_candidate_names() -> None:
    """Ranges compress runs; the candidate names include every language file."""
    assert refs_scripts._ranges([5, 0, 1, 2, 7, 8]) == "0-2, 5, 7-8"
    names = refs_scripts.candidate_script_names()
    assert "config_strings_en.lua" in names and "level3_strings_en.lua" in names
