# SPDX-License-Identifier: GPL-3.0-or-later
"""Readers for the reference lists that come from what the compiled scripts pass to bindings.

The text labels (string-table keys, never their text), the pad and Warrior commands, the speech commands and voice
sets, the ambient sounds, emitters and speech lines, and the gang messages. Each function takes the scripts' facts
(`lua4.ChunkFacts` by script name) and returns plain entries, so the tests can feed it synthetic facts; the disc
readers in `refs_extract` call them.

Research: docs/research/gui.md#strings, docs/research/combat.md#commands, docs/research/ai.md#warrior-commands,
docs/research/sound.md#speech, docs/guides/research-workflow.md#reference-lists.
"""

from __future__ import annotations

import collections
import re
from collections.abc import Callable, Iterable, Iterator, Mapping
from typing import Any

from coney_tools import lua4

#: The languages of the string tables, in `GetLanguage` order (docs/research/gui.md#strings).
LANGUAGES = ("en", "de", "fr", "it", "es")
#: The globals that hold string tables: `GSTRING` and `LABEL` (global.lua), `TSTRING` (tutorial), `RUMBLE` (the Rumble
#: arenas) and a level's own `LEVEL<n>` (`LEVEL95C` for Coney Island's second script).
TEXT_TABLE = re.compile(r"^(GSTRING|LABEL|TSTRING|RUMBLE|LEVEL\d+[A-Z]*)$")
_LANGUAGE_FILE = re.compile(r"_strings_([a-z]{2})\.lua$")
#: How many scripts or callees a list cell shows before "... n more".
SHOWN = 8


def candidate_script_names() -> list[str]:
    """Script names to try against the WAD's hashes: the language files, which no script names in full
    (`doFile("config_strings_" .. ext)`)."""
    names = [f"config_strings_{lang}.lua" for lang in LANGUAGES]
    for level in range(200):
        names.append(f"level{level}_strings.lua")
        names += [f"level{level}_strings_{lang}.lua" for lang in LANGUAGES]
    return names


def _shown(items: Iterable[str]) -> list[str] | None:
    """At most SHOWN items, then a "... n more" item; None when empty."""
    items = list(items)
    if len(items) > SHOWN:
        return [*items[:SHOWN], f"... {len(items) - SHOWN} more"]
    return items or None


def _globals_in(value: lua4.Value) -> Iterator[str]:
    """Every global name read inside a value: itself, a table's members, a call result's arguments."""
    if isinstance(value, lua4.Global):
        yield value.name
    elif isinstance(value, lua4.Table):
        for item in [*value.items.values(), *value.fields.values()]:
            yield from _globals_in(item)
    elif isinstance(value, lua4.CallResult):
        for item in value.args:
            yield from _globals_in(item)


def _ranges(numbers: Iterable[int]) -> str:
    """`0-3, 5, 7-9` for a set of whole numbers."""
    ordered = sorted(set(numbers))
    parts: list[str] = []
    start = previous = ordered[0] if ordered else 0
    for number in [*ordered[1:], None]:
        if number is not None and number == previous + 1:
            previous = number
            continue
        parts.append(str(start) if start == previous else f"{start}-{previous}")
        if number is not None:
            start = previous = number
    return ", ".join(parts) if ordered else ""


def _natural(text: str) -> list[Any]:
    """A sort key that orders `MS_2` before `MS_10`."""
    return [int(part) if part.isdigit() else part for part in re.split(r"(\d+)", text)]


def _table_order(table: str) -> tuple[int, int, str]:
    """The order of the string tables on the page: the shared ones, then the levels by number."""
    shared = ("GSTRING", "LABEL", "TSTRING", "RUMBLE")
    if table in shared:
        return (0, shared.index(table), "")
    match = re.match(r"LEVEL(\d+)(.*)", table)
    return (1, int(match.group(1)), match.group(2)) if match else (2, 0, table)


# --- text labels -------------------------------------------------------------------------------------------------


def text_labels(scripts: Mapping[str, lua4.ChunkFacts]) -> list[dict[str, Any]]:
    """Every key of the scripts' string tables, with where it is defined and what scripts pass it to.

    A string table is a global matching TEXT_TABLE that a script assigns a table to, once per language (a level script
    builds all five and keeps `GetLanguage`'s); a key is one of its fields. The text is never read. A key that holds
    a table of strings read by number or name (`GSTRING.HUD`, which the front end hands to the engine, or a level's
    lists of lines) gets its ids and size instead.
    """
    defined: dict[str, set[str]] = collections.defaultdict(set)
    languages: collections.Counter[str] = collections.Counter()
    language_files: dict[str, set[str]] = collections.defaultdict(set)
    ids: dict[str, set[int | str]] = {}
    for script, facts in scripts.items():
        per_script: collections.Counter[str] = collections.Counter()
        file_language = _LANGUAGE_FILE.search(script)
        for assignment in facts.assignments:
            if not TEXT_TABLE.match(assignment.name) or not isinstance(assignment.value, lua4.Table):
                continue
            table = assignment.value
            members: list[tuple[str, lua4.Value]] = [
                (f"{assignment.name}.{key}", value) for key, value in table.fields.items() if isinstance(key, str)
            ]
            if table.items and not table.fields:  # a numbered table itself (TSTRING)
                members = [(assignment.name, table)]
            for label, value in members:
                if value is None:  # a key set to nil
                    continue
                if isinstance(value, lua4.Table):
                    ids.setdefault(label, set()).update(_table_ids(value))
                defined[label].add(script)
                per_script[label] += 1
                if file_language:
                    language_files[label].add(file_language.group(1))
        for label, count in per_script.items():
            languages[label] = max(languages[label], count)
    uses: collections.Counter[str] = collections.Counter()
    callees: dict[str, collections.Counter[str]] = collections.defaultdict(collections.Counter)
    users: dict[str, collections.Counter[str]] = collections.defaultdict(collections.Counter)
    for script, facts in scripts.items():
        for callee, value in _referencing_values(facts):
            for name in _globals_in(value):
                parts = name.split(".")
                if len(parts) < 2 or not TEXT_TABLE.match(parts[0]):
                    continue
                label = f"{parts[0]}.{parts[1]}"
                uses[label] += 1
                users[label][script] += 1
                if callee and callee != "?":
                    callees[label][callee] += 1
    entries = []
    for label in set(defined) | set(uses):
        table_name, _, key = label.partition(".")
        held = ids.get(label)
        numbers = sorted(i for i in held or () if isinstance(i, int))
        names = [i for i in held or () if isinstance(i, str)]
        entries.append(
            {
                "id": label,
                "table": table_name,
                "key": key or label,
                "ids": (_ranges(numbers) if numbers else f"{len(names)} names") if held else None,
                "size": len(held) if held else None,
                "languages": max(languages[label], len(language_files[label])),
                "defined_in": _shown(sorted(defined[label], key=_natural)),
                "uses": uses[label],
                "passed_to": [name for name, _ in callees[label].most_common(6)] or None,
                "used_in": _shown(sorted(users[label], key=_natural)),
            }
        )
    return sorted(entries, key=lambda e: (_table_order(e["table"]), e["ids"] is None, _natural(e["key"])))


def _table_ids(table: lua4.Table) -> set[int | str]:
    """The ids of a table of strings: its numbered entries and its named ones."""
    found: set[int | str] = set(table.items)
    found.update(int(key) if isinstance(key, float) else key for key in table.fields)
    return found


def _referencing_values(facts: lua4.ChunkFacts) -> Iterator[tuple[str | None, lua4.Value]]:
    """Every value that may name a label, with the function it is passed to (None when stored, not passed)."""
    for call in facts.calls:
        for arg in call.args:
            yield call.callee, arg
    for _, table in facts.constructed:
        yield None, table
    for assignment in facts.assignments:
        if isinstance(assignment.value, lua4.Global):
            yield None, assignment.value


# --- commands ----------------------------------------------------------------------------------------------------

#: `AddCommand`'s trigger kinds (docs/references/bindings/character.md#addcommand, confirmed (code) at 0x00147940).
TRIGGERS = {
    1: "held",
    2: "pressed",
    3: "released",
    4: "pad query",
    5: "tapped",
    6: "long hold",
    7: "history hold",
    8: "combination held",
    9: "combination press",
}
#: The Warrior commands: the seven entries of the command menu (`WCIssueCommand`'s 0-6).
WARRIOR_COMMANDS = 7


def button_names(mask: int, buttons: Iterable[tuple[int, str, str]]) -> str:
    """A pad mask as button names joined with " + " (`select + d-pad up`)."""
    return " + ".join(name for bit, name, _ in buttons if mask & bit)


def commands(scripts: Mapping[str, lua4.ChunkFacts], buttons: Iterable[tuple[int, str, str]]) -> list[dict[str, Any]]:
    """The trigger kinds, the pad commands the scripts bind with `AddCommand`, and the Warrior commands.

    A pad command's `bound` lists each binding as "trigger: buttons"; `toggled_in` the scripts that switch it per human
    with `EnableCommand`. A Warrior command's `used_in` the scripts that enable, disable or issue it.
    """
    buttons = list(buttons)
    entries: list[dict[str, Any]] = [
        {"id": f"trigger:{number}", "kind": "trigger", "number": number, "name": name}
        for number, name in TRIGGERS.items()
    ]
    bound: dict[int, list[str]] = collections.defaultdict(list)
    bound_in: dict[int, set[str]] = collections.defaultdict(set)
    toggled: dict[int, set[str]] = collections.defaultdict(set)
    for script, facts in scripts.items():
        for call in facts.calls:
            args = call.args
            if call.callee == "AddCommand" and len(args) >= 3 and isinstance(args[0], float):
                number = int(args[0])
                trigger = TRIGGERS.get(int(args[1]), "?") if isinstance(args[1], float) else "?"
                mask = int(args[2]) if isinstance(args[2], float) else None
                extra = int(args[3]) if len(args) > 3 and isinstance(args[3], float) else 0
                if mask is None:
                    pad = "set at run time"
                elif trigger.startswith("combination") and extra:
                    pad = f"{button_names(mask, buttons)} + {button_names(extra, buttons)}"
                else:
                    pad = button_names(mask, buttons)
                bound[number].append(f"{trigger}: {pad}")
                bound_in[number].add(script)
            elif call.callee == "EnableCommand" and len(args) >= 2 and isinstance(args[1], float):
                toggled[int(args[1])].add(script)
    for number in sorted(set(bound) | set(toggled)):
        entries.append(
            {
                "id": f"pad:{number}",
                "kind": "pad command",
                "number": number,
                "hex": number,
                "bound": bound.get(number) or None,
                "bound_in": sorted(bound_in[number]) or None,
                "toggled_in": _shown(sorted(toggled[number], key=_natural)),
            }
        )
    used: dict[int, set[str]] = collections.defaultdict(set)
    for script, facts in scripts.items():
        for call in facts.calls:
            position = {"WCEnableCommand": 1, "WCIssueCommand": 1, "IssueWarriorCommand": 0}.get(call.callee)
            given = call.args[position] if position is not None and len(call.args) > position else None
            if isinstance(given, float):
                used[int(given)].add(script)
    entries += [
        {
            "id": f"warrior:{number}",
            "kind": "Warrior command",
            "number": number,
            "used_in": _shown(sorted(used[number], key=_natural)),
        }
        for number in range(WARRIOR_COMMANDS)
    ]
    return entries


# --- speech --------------------------------------------------------------------------------------------------------

#: The name of a voice line the game looks for (format at 0x005481e0, used by 0x001164a8); lines count from 01.
VOICE_LINE = "vags/character/voices/{voice}/{command}_{line:02d}"
#: The most lines one command of one voice set can have (the probe stops before 0x37).
MAX_VOICE_LINES = 54


def voice_lines(voices: int, names: list[str], known: Callable[[str], bool]) -> dict[tuple[int, int], int]:
    """(voice set, speech command) -> how many lines the game finds, probing names as the game does: line 1, 2, ...
    until one is missing from the sound list (`known`)."""
    found: dict[tuple[int, int], int] = {}
    for voice in range(voices):
        for command, name in enumerate(names):
            if not name or command == 0:
                continue
            count = 0
            while count < MAX_VOICE_LINES and known(VOICE_LINE.format(voice=voice, command=name, line=count + 1)):
                count += 1
            if count:
                found[(voice, command)] = count
    return found


def speech(
    scripts: Mapping[str, lua4.ChunkFacts],
    names: list[str],
    lines: Mapping[tuple[int, int], int],
    voice_types: Mapping[int, list[int]],
) -> list[dict[str, Any]]:
    """The speech commands (by the executable's name table) and the voice sets that have lines or are named.

    `lines` comes from `voice_lines`; `voice_types` maps a voice set to the character types `CfgChar` gives it.
    """
    played: dict[int, set[str]] = collections.defaultdict(set)
    percent: dict[int, list[str]] = collections.defaultdict(list)
    responses: dict[int, set[str]] = collections.defaultdict(set)
    for script, facts in scripts.items():
        for call in facts.calls:
            args = call.args
            if call.callee == "SoundPlayCommand" and len(args) > 1 and isinstance(args[1], float):
                played[int(args[1])].add(script)
            elif call.callee == "SndSetCommandSoundPercent" and len(args) > 2:
                voice, command, chance = args[:3]
                if isinstance(voice, float) and isinstance(command, float) and isinstance(chance, float):
                    who = "every voice" if voice < 0 else f"voice {int(voice)}"
                    percent[int(command)].append(f"{who}: {int(chance)} %")
            elif call.callee == "HuSetStateRespVoiceIndex" and len(args) > 1 and isinstance(args[1], float):
                responses[int(args[1])].add(script)
    by_command: collections.Counter[int] = collections.Counter()
    voices_of: collections.Counter[int] = collections.Counter()
    by_voice: collections.Counter[int] = collections.Counter()
    commands_of: collections.Counter[int] = collections.Counter()
    for (voice, command), count in lines.items():
        by_command[command] += count
        voices_of[command] += 1
        by_voice[voice] += count
        commands_of[voice] += 1
    entries: list[dict[str, Any]] = [
        {
            "id": f"command:{number}",
            "kind": "speech command",
            "number": number,
            "name": name,
            "voices": voices_of[number] or None,
            "lines": by_command[number] or None,
            "percent": percent.get(number),
            "used_in": _shown(sorted(played[number], key=_natural)),
        }
        for number, name in enumerate(names)
    ]
    for voice in sorted(set(by_voice) | set(voice_types) | set(responses)):
        entries.append(
            {
                "id": f"voice:{voice}",
                "kind": "voice set",
                "number": voice,
                "commands": commands_of[voice] or None,
                "lines": by_voice[voice] or None,
                "types": sorted(voice_types.get(voice, [])) or None,
                "used_in": _shown(sorted(responses[voice], key=_natural)),
            }
        )
    return entries


# --- sounds by name ------------------------------------------------------------------------------------------------


def speech_path(name: str) -> str:
    """A speech line's sound name: `SetVag("l11_t25_006")` stands for `vags/speeches/l11/l11_t25_006` (inferred: 537
    of the 566 names so formed are in the sound list); a name with a folder is already whole."""
    return name if "/" in name else f"vags/speeches/{name.split('_')[0]}/{name}"


def sound_names(
    scripts: Mapping[str, lua4.ChunkFacts], known: Callable[[str], bool] | None = None
) -> list[dict[str, Any]]:
    """The ambient sounds (`AddAmbientSound`, and the sounds emitters name), the ambient emitters
    (`AddAmbientSoundEmitter2`) and the speech lines (`SetVag`, `HuSpeak`, `HuSpeakNI`), by name.

    `known` says whether a sound name is in the game's sound list; without it `in_list` is left out."""
    ambient: dict[str, list[int]] = collections.defaultdict(list)
    ambient_users: dict[str, set[str]] = collections.defaultdict(set)
    emitters: dict[str, dict[str, Any]] = {}
    lines: dict[str, set[str]] = collections.defaultdict(set)
    for script, facts in scripts.items():
        for call in facts.calls:
            args = call.args
            if call.callee == "AddAmbientSound" and len(args) > 1 and isinstance(args[1], str):
                if isinstance(args[0], float):
                    ambient[args[1]].append(int(args[0]))
                ambient_users[args[1]].add(script)
            elif call.callee == "AddAmbientSoundEmitter2" and len(args) > 5 and isinstance(args[0], str):
                index, count = args[3], args[5]
                emitter = emitters.setdefault(
                    args[0],
                    {
                        "id": f"emitter:{args[0]}",
                        "kind": "ambient emitter",
                        "name": args[0],
                        "values": [int(v) if isinstance(v, float) else None for v in (index, count)],
                        "used_by": set(),
                    },
                )
                emitter["used_by"].add(script)
                if isinstance(args[4], str) and args[4]:
                    ambient_users[args[4]].add(script)
            elif call.callee == "AddAmbientSoundEmitter" and len(args) > 3 and isinstance(args[3], str):
                ambient_users[args[3]].add(script)
            elif call.callee in ("HuSpeak", "HuSpeakNI") and len(args) > 1 and isinstance(args[1], str):
                lines[speech_path(args[1])].add(script)
            elif call.callee == "SetVag" and args and isinstance(args[0], str):
                lines[speech_path(args[0])].add(script)
            for arg in args:  # a line built inline: HuSpeak(h, SetVag("l11_t25_006"))
                if (
                    isinstance(arg, lua4.CallResult)
                    and arg.callee == "SetVag"
                    and arg.args
                    and isinstance(arg.args[0], str)
                ):
                    lines[speech_path(arg.args[0])].add(script)
    entries: list[dict[str, Any]] = []
    for name in sorted(set(ambient) | set(ambient_users), key=_natural):
        entries.append(
            {
                "id": f"ambient:{name}",
                "kind": "ambient sound",
                "name": name,
                "values": sorted(ambient[name]) or None,
                "in_list": known(name) if known else None,
                "used_by": _shown(sorted(ambient_users[name], key=_natural)),
            }
        )
    for name in sorted(emitters, key=_natural):
        emitter = emitters[name]
        emitter["used_by"] = _shown(sorted(emitter["used_by"], key=_natural))
        entries.append(emitter)
    for name in sorted(lines, key=_natural):
        entries.append(
            {
                "id": f"speech:{name}",
                "kind": "speech line",
                "name": name,
                "in_list": known(name) if known else None,
                "used_by": _shown(sorted(lines[name], key=_natural)),
            }
        )
    return entries


# --- messages ------------------------------------------------------------------------------------------------------


def message_handlers(
    scripts: Mapping[str, lua4.ChunkFacts], binding: str
) -> tuple[dict[int, collections.Counter[str]], collections.Counter[int]]:
    """Per message number, the callback names `binding` (`SetMsgHandler`, `GangSetMsgHandler`) is given, and how many
    calls pass nil (removing the handler)."""
    handlers: dict[int, collections.Counter[str]] = collections.defaultdict(collections.Counter)
    clears: collections.Counter[int] = collections.Counter()
    for facts in scripts.values():
        for call in facts.calls:
            if call.callee != binding or len(call.args) < 3 or not isinstance(call.args[1], float):
                continue
            message = int(call.args[1])
            callback = call.args[2]
            if callback is None:
                clears[message] += 1
            elif isinstance(callback, str):
                handlers[message][callback] += 1
            elif isinstance(callback, lua4.Global):
                handlers[message][callback.name] += 1
            else:
                handlers[message]["?"] += 1
    return handlers, clears
