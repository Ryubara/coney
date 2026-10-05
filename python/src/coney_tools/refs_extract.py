# SPDX-License-Identifier: GPL-3.0-or-later
"""Read the facts of the game reference lists from the player's own disc (`coney-tools refs extract`).

Everything here reads the disc and keeps only names, ids and numbers: the configuration calls of the compiled
scripts (`CfgChar`, `CfgGang`, `CfgObj` ...), the Character List of `warriors.glr`, the animation descriptors, two
tables of the executable and the names recovered for the WAD's entries. Nothing is copied out of the disc; the
lists hold what a modder needs to name a thing.

Each `topic_*` function returns the entries of one list, in the order the page shows them. Hand-written fields are
never produced here; `refs.merge` keeps them from the YAML.

Research: docs/guides/research-workflow.md#reference-lists, docs/research/characters.md,
docs/research/formats/animation.md, docs/research/gui.md#markup, docs/research/frontend.md#input.
"""

from __future__ import annotations

import collections
import contextlib
import hashlib
import re
import struct
import sys
import zlib
from collections.abc import Callable, Iterable
from functools import cached_property
from itertools import pairwise
from pathlib import Path
from typing import Any

from coney_tools import lua4, wad
from coney_tools.chunks import looks_like_container, parse_container
from coney_tools.disc import Disc
from coney_tools.elf import Elf, read_elf

# --- addresses and layouts (NTSC-U SLUS_212.15) -------------------------------------------------------------------

#: The 35 default anim slots a human starts with (docs/research/characters.md#anim-slots).
ANIM_SLOT_TABLE = 0x005105D8
ANIM_SLOTS = 35
#: The markup tag table: 66 strings of 61 bytes (docs/research/gui.md#markup).
TAG_TABLE = 0x0050D718
TAG_COUNT = 66
TAG_SIZE = 61
#: Anim ids per character data (the Character Data chunk's slot count).
ANIM_IDS = 722
#: Chunk types inside a character data resource and the Character List (docs/research/formats/wad-contents.md).
CHUNK_KEYFRAMES, CHUNK_DESCRIPTOR, CHUNK_CHARACTER_DATA, CHUNK_CHARACTER_LIST = 0x00, 0x02, 0x08, 0x44
#: The slots that hold walk, jog, run and sprint (docs/research/characters.md#speed-classes).
GAIT_SLOTS = {"walk": 4, "jog": 5, "run": 6, "sprint": 7}
#: Clips play at 30 frames a second.
FPS = 30
#: Character data resource names: `<prefix>_header`; the generic one fills every slot a character leaves unset.
GENERIC_DATA = "generic_header"
UNSET = 0xFFFFFFFF

#: Our reading of the model name prefixes (the first underscore-separated part of a model name).
FACTIONS = {
    "warr": "Warriors",
    "bopp": "Boppers",
    "dest": "Destroyers",
    "fury": "Furies",
    "hiha": "Hi-Hats",
    "hurr": "Hurricanes",
    "hun": "Huns",
    "huns": "Huns",
    "jsbs": "Jones Street Boys",
    "lizz": "Lizzies",
    "moon": "Moonrunners",
    "orph": "Orphans",
    "punk": "Punks",
    "riff": "Riffs",
    "rogu": "Rogues",
    "samo": "Saracens",
    "sara": "Saracens",
    "sava": "Savage Huns",
    "turn": "Turnbull ACs",
    "civl": "civilians",
    "cops": "police",
    "cop": "police",
    "bum": "bums",
    "boss": "bosses",
}

#: The pad's button word, bit by bit (docs/research/frontend.md#input), with the glyph tag that draws each button.
BUTTONS = (
    (0x0001, "L2", "<L2>"),
    (0x0002, "R2", "<R2>"),
    (0x0004, "L1", "<L1>"),
    (0x0008, "R1", "<R1>"),
    (0x0010, "triangle", "<T>"),
    (0x0020, "circle", "<O>"),
    (0x0040, "cross", "<X>"),
    (0x0080, "square", "<S>"),
    (0x0100, "SELECT", "<SELECT>"),
    (0x0200, "L3", "<L3>"),
    (0x0400, "R3", "<R3>"),
    (0x0800, "START", "<START>"),
    (0x1000, "d-pad up", "<DU>"),
    (0x2000, "d-pad right", "<DR>"),
    (0x4000, "d-pad down", "<DD>"),
    (0x8000, "d-pad left", "<DL>"),
)

#: Script files whose global constants make up the enums list, and globals that are not constants.
ENUM_SCRIPTS = (
    "enum_preload.lua",
    "config_preload.lua",
    "config_preload2.lua",
    "config_preload3.lua",
    "global.lua",
    "rumble_preload.lua",
)
_CONSTANT = re.compile(r"^[A-Z][A-Z0-9]*(_[A-Z0-9]+)+$|^[A-Z]{2,}[0-9]*$")
_LEVEL_SCRIPT = re.compile(r"^level(\d+)")
_FILE_NAME = re.compile(r"^[\w./-]+\.[A-Za-z][A-Za-z0-9_]*$")


def _int(value: Any) -> Any:
    """A whole float as an int; anything else as it is."""
    return int(value) if isinstance(value, float) and value.is_integer() else value


def _global(value: Any, prefix: str = "") -> str | None:
    """The name of a global argument (`PHYS.OBB` -> `OBB` with prefix `PHYS.`), or a plain string, or None."""
    if isinstance(value, lua4.Global):
        name = value.name
        return name[len(prefix) :] if prefix and name.startswith(prefix) else name
    if isinstance(value, str) and value != "none":
        return value
    return None


def _number(value: Any) -> int | float | None:
    """A numeric argument, or None when the walk could not know it."""
    return _int(value) if isinstance(value, float) else None


def _numbers(value: Any) -> list[Any] | None:
    """A table of numbers as a list, rounded to 6 decimals, or None."""
    if not isinstance(value, lua4.Table):
        return None
    items = [_int(round(v, 6)) if isinstance(v, float) else None for v in value.as_list()]
    return items if items and all(v is not None for v in items) else None


def _crc(name: str) -> int:
    """CRC-32 of a name, as the Character List and the resource names use it (lower case)."""
    return zlib.crc32(name.lower().encode("latin-1"))


def _note(message: str) -> None:
    """A progress line on stderr."""
    print(f"coney-tools: {message}", file=sys.stderr)


class DiscFacts:
    """The disc, read once and kept: the WAD's entries and names, the compiled scripts and the resources."""

    def __init__(self, disc: Disc, known_names: Iterable[str] = ()) -> None:
        """Open the disc; `known_names` are names found earlier (the WAD names list, a names file), each kept only
        when it hashes to an entry of this disc."""
        self.disc = disc
        self.entries = wad.load_entries(disc)
        self._handle = disc.open(wad.WAD_FILE)
        self._known = list(known_names)

    def read(self, entry: wad.WadEntry) -> bytes:
        """One entry's bytes."""
        self._handle.seek(entry.offset)
        return self._handle.read(entry.size)

    @cached_property
    def names(self) -> dict[int, str]:
        """Entry hash -> file name (without `./ee_files/`): the known names that match, then those recovered."""
        _note("recovering the WAD's entry names (a full pass over the disc)")
        hashes = {entry.hash for entry in self.entries}
        found = {key: name for key, name in wad.hash_names(self._known).items() if key in hashes}
        for key, name in wad.recover_names(self.disc, self.entries).items():
            found.setdefault(key, wad.display_name(name))
        # A CRC-32 match on a string such as "-61.622" is chance: keep names with a word-like extension only.
        return {key: name for key, name in found.items() if _FILE_NAME.match(name)}

    def by_name(self, name: str) -> wad.WadEntry | None:
        """The entry a file name hashes to, if any."""
        key = wad.name_hash(wad.NAME_PREFIX + name)
        return next((entry for entry in self.entries if entry.hash == key), None)

    @cached_property
    def scripts(self) -> dict[str, lua4.ChunkFacts]:
        """Every compiled script's facts, by file name (or the hash in hex when its name is not recovered)."""
        result: dict[str, lua4.ChunkFacts] = {}
        for entry in self.entries:
            self._handle.seek(entry.offset)
            if self._handle.read(4) != lua4.HEADER[:4]:
                continue
            name = self.names.get(entry.hash, f"{entry.hash:08x}")
            try:
                result[name] = lua4.walk_chunk(lua4.parse_chunk(self.read(entry)))
            except lua4.LuaError as error:
                _note(f"skipped {name}: {error}")
        return result

    def calls(self, callee: str, script: str | None = None) -> Iterable[tuple[str, lua4.Call]]:
        """Every call of `callee` (in one script or in all), with the script's name."""
        for name, facts in self.scripts.items():
            if script is None or name == script:
                yield from ((name, call) for call in facts.calls if call.callee == callee)

    def table(self, script: str, name: str) -> lua4.Table | None:
        """The last table a script gave a global."""
        facts = self.scripts.get(script)
        return facts.tables.get(name) if facts else None

    @cached_property
    def elf(self) -> Elf:
        """The executable."""
        with self.disc.open(wad.ELF_FILE) as handle:
            return read_elf(handle.read())

    def elf_bytes(self, address: int, size: int) -> bytes:
        """`size` bytes of the executable at a virtual address."""
        for section in self.elf.sections.values():
            if section.address <= address < section.address + section.size:
                start = section.offset + address - section.address
                return self.elf.data[start : start + size]
        raise ValueError(f"0x{address:08x} is in no section")

    @cached_property
    def strings(self) -> set[str]:
        """Candidate names: every string of every script, and every recovered file name without its extension."""
        found: set[str] = set()
        for entry in self.entries:
            self._handle.seek(entry.offset)
            if self._handle.read(4) == lua4.HEADER[:4]:
                with contextlib.suppress(lua4.LuaError):  # a chunk the reader cannot parse adds no candidates
                    found.update(s for s in lua4.all_strings(lua4.parse_chunk(self.read(entry))) if s)
        found.update(name.rsplit(".", 1)[0] for name in self.names.values())
        return found

    @cached_property
    def character_list(self) -> list[tuple[int, ...]]:
        """The Character List: 32-byte records of eight words (docs/research/characters.md#files)."""
        entry = self.by_name("warriors.glr")
        if entry is None:
            raise ValueError("warriors.glr is not on the disc")
        data = self.read(entry)
        container = parse_container(data)
        if container is None:
            raise ValueError("warriors.glr is not a chunk container")
        for resource in container.resources:
            for chunk in resource.chunks:
                if chunk.type == CHUNK_CHARACTER_LIST:
                    count = struct.unpack_from("<I", data, chunk.offset)[0]
                    return [struct.unpack_from("<8I", data, chunk.offset + 16 + i * 32) for i in range(count)]
        raise ValueError("warriors.glr has no Character List")

    @cached_property
    def _animation_scan(self) -> tuple[list[Clip], dict[int, tuple[list[Clip], tuple[int, ...]]]]:
        """One streaming pass over every chunk container: the distinct clips, and each character data's clips (in
        load order) with its 722 anim slots. Only these facts are kept, never an entry's bytes."""
        _note("reading every animation (a full pass over the disc)")
        by_digest: dict[str, Clip] = {}
        character_data: dict[int, tuple[list[Clip], tuple[int, ...]]] = {}
        for entry in self.entries:
            self._handle.seek(entry.offset)
            if not looks_like_container(self._handle.read(16), entry.size):
                continue
            data = self.read(entry)
            container = parse_container(data)
            for resource in container.resources if container else []:
                table = next((c for c in resource.chunks if c.type == CHUNK_CHARACTER_DATA), None)
                loaded = []
                for first, second in pairwise(resource.chunks):
                    if first.type != CHUNK_KEYFRAMES or second.type != CHUNK_DESCRIPTOR:
                        continue
                    clip = Clip.read(data, first.offset, first.size, second.offset)
                    clip = by_digest.setdefault(clip.digest, clip)
                    clip.packs.add(entry.index)
                    (clip.data_resources if table else clip.files).add(resource.hash)
                    loaded.append(clip)
                if table is not None and resource.hash not in character_data:
                    slots = struct.unpack_from(f"<{ANIM_IDS}I", data, table.offset + 8)
                    character_data[resource.hash] = (loaded, slots)
        return sorted(by_digest.values(), key=lambda clip: (clip.name, clip.digest)), character_data

    @property
    def clips(self) -> list[Clip]:
        """Every distinct animation clip on the disc, with where it is found."""
        return self._animation_scan[0]

    @property
    def character_data(self) -> dict[int, tuple[list[Clip], tuple[int, ...]]]:
        """Character data resource hash -> its clips in load order and its 722 anim slots."""
        return self._animation_scan[1]

    def clip_for(self, data_hash: int, anim_id: int) -> Clip | None:
        """The clip a character data plays for an anim id: its own slot, else the generic one.

        Slot value n names the n-th clip counted from the last one loaded (the chunk system pops them last-in
        first-out; docs/research/formats/animation.md, "Where clips are found").
        """
        for key in (data_hash, _crc(GENERIC_DATA)):
            if key not in self.character_data:
                continue
            loaded, slots = self.character_data[key]
            slot = slots[anim_id]
            if slot != UNSET and slot < len(loaded):
                return loaded[len(loaded) - 1 - slot]
        return None

    def resource_name(self, key: int) -> str | None:
        """A resource's name, recovered by hashing candidate strings (`<x>_header`, `<model>_geo` ...)."""
        return self._resource_names.get(key)

    @cached_property
    def _resource_names(self) -> dict[int, str]:
        """Hash -> name for the candidate strings and their resource forms."""
        found: dict[int, str] = {}
        for text in self.strings:
            if len(text) > 64 or not re.match(r"^[\w/.-]+$", text):
                continue
            base = text.lower()
            stems = {base}
            parts = base.split("_")
            stems.update("_".join(parts[:k]) for k in range(1, len(parts)))
            for stem in stems:
                for name in (stem, f"{stem}_header", f"{stem}_geo", f"{stem}_tex", f"{stem}_a"):
                    found.setdefault(_crc(name), name)
        return found


class Clip:
    """One distinct clip: its descriptor's facts and where it is found."""

    def __init__(self, name: str, duration: float, dx: float, dy: float, events: int, digest: str) -> None:
        self.name = name
        self.duration = duration
        self.distance = (dx * dx + dy * dy) ** 0.5
        self.events = events
        self.digest = digest
        self.packs: set[int] = set()
        self.data_resources: set[int] = set()
        self.files: set[int] = set()

    @classmethod
    def read(cls, data: bytes, keys_at: int, keys_size: int, descriptor_at: int) -> Clip:
        """A clip from its keyframe chunk and its 80-byte descriptor (docs/research/formats/animation.md)."""
        descriptor = data[descriptor_at : descriptor_at + 80]
        dx, dy, duration = struct.unpack_from("<3f", descriptor, 4)
        events = struct.unpack_from("<H", descriptor, 0x1A)[0]
        name = descriptor[0x25:0x43].split(b"\0")[0].decode("latin-1")
        # Identity: the keys and the descriptor, without the descriptor's load-time pointers.
        digest = hashlib.sha1(data[keys_at : keys_at + keys_size] + descriptor[4:0x1C] + descriptor[0x20:]).hexdigest()
        return cls(name, duration, dx, dy, events, digest)

    def speed(self) -> float:
        """Horizontal root displacement over playing time, m/s (0 for a clip of no length)."""
        return self.distance / self.duration if self.duration > 0 else 0.0


# --- topics ------------------------------------------------------------------------------------------------------


def _cfg_chars(facts: DiscFacts) -> list[list[Any]]:
    """The arguments of every `CfgChar` call, in order."""
    return [call.args for _, call in facts.calls("CfgChar", "config_preload2.lua")]


def _model_records(facts: DiscFacts) -> dict[int, tuple[int, ...]]:
    """Character List records by model-name CRC."""
    return {record[0]: record for record in facts.character_list}


def _hu_creates(facts: DiscFacts) -> tuple[dict[int, collections.Counter[str]], dict[int, set[int]]]:
    """Per character type: the names scripts give its humans, and the level numbers that create one."""
    names: dict[int, collections.Counter[str]] = collections.defaultdict(collections.Counter)
    levels: dict[int, set[int]] = collections.defaultdict(set)
    for script, call in facts.calls("HuCreate"):
        if len(call.args) < 2 or not isinstance(call.args[1], float):
            continue
        kind = int(call.args[1])
        if isinstance(call.args[0], str):
            names[kind][call.args[0]] += 1
        match = _LEVEL_SCRIPT.match(script)
        if match:
            levels[kind].add(int(match.group(1)))
    return names, levels


def _image(images: Path | None, folder: str, *names: str | None) -> str | None:
    """`<folder>/<name>.png` for the first name whose thumbnail exists below images/, else None."""
    for name in names:
        if images is not None and name and (images / folder / f"{name}.png").is_file():
            return f"{folder}/{name}.png"
    return None


def topic_characters(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Character types, from `CfgChar`, the Character List, the clips and every `HuCreate`."""
    records = _model_records(facts)
    created, levels = _hu_creates(facts)
    entries = []
    for args in _cfg_chars(facts):
        kind = int(args[0])
        model = args[9] if isinstance(args[9], str) else None
        record = records.get(_crc(model)) if model else None
        speeds = None
        if record is not None:
            clips = {gait: facts.clip_for(record[1], _anim_slots(facts)[slot]) for gait, slot in GAIT_SLOTS.items()}
            speeds = {gait: round(clip.speed(), 3) for gait, clip in clips.items() if clip is not None} or None
        entries.append(
            {
                "id": kind,
                "model": model,
                "armies_model": f"{model}_a" if model and _crc(f"{model}_a") in records else None,
                "faction": FACTIONS.get(model.split("_")[0]) if model else None,
                "behaviour": _number(args[1]),
                "category": _number(args[2]),
                "speed_class": _number(args[3]),
                "v11d": _number(args[4]),
                "health": _number(args[5]),
                "damage_table": _global(args[6]),
                "attack_table": _global(args[7]),
                "damage_scale": _number(args[8]),
                "hat": _global(args[10]),
                "voice": _number(args[11]),
                "flag_14b": _number(args[12]),
                "range_table": _global(args[13]),
                "drop_group": _global(args[14]),
                "drop_chance": _number(args[15]),
                "weapon": _global(args[16]),
                "speeds": speeds,
                "script_names": [name for name, _ in created[kind].most_common(8)] or None,
                "levels": sorted(levels[kind]) or None,
                "image": _image(images, "characters", model),
            }
        )
    return sorted(entries, key=lambda entry: entry["id"])


def _anim_slots(facts: DiscFacts) -> tuple[int, ...]:
    """The 35 default anim slots from the executable."""
    return struct.unpack(f"<{ANIM_SLOTS}I", facts.elf_bytes(ANIM_SLOT_TABLE, 4 * ANIM_SLOTS))


def topic_character_models(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Every Character List record, with the names recovered for it and the types that use it."""
    types: dict[int, list[int]] = collections.defaultdict(list)
    candidates: dict[int, str] = {}
    for args in _cfg_chars(facts):
        if isinstance(args[9], str):
            types[_crc(args[9])].append(int(args[0]))
            candidates[_crc(args[9])] = args[9].lower()
            candidates[_crc(args[9] + "_a")] = args[9].lower() + "_a"
    entries = []
    for index, record in enumerate(facts.character_list):
        name = candidates.get(record[0]) or facts.resource_name(record[0])
        entries.append(
            {
                "index": index,
                "crc": record[0],
                "name": name,
                "data": facts.resource_name(record[1]),
                "data_crc": record[1],
                "geo_crc": record[2],
                "tex_crc": record[3],
                "data_size": record[4],
                "geo_size": record[5],
                "tex_size": record[7],
                "types": sorted(types.get(record[0], [])) or None,
                # The renderer names an image after the model, or after its hash when it has no name.
                "image": _image(images, "characters", name, f"{record[0]:08x}"),
            }
        )
    return sorted(entries, key=lambda entry: (entry["name"] is None, entry["name"] or "", entry["index"]))


def topic_gangs(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Gang types, from `CfgGang`, the strategy tables, `CfgGangMusic` and every `GangCreate`."""
    music = {
        int(call.args[0]): [t for t in call.args[1].as_list() if isinstance(t, str)]
        for _, call in facts.calls("CfgGangMusic")
        if call.args and isinstance(call.args[0], float) and isinstance(call.args[1], lua4.Table)
    }
    names: dict[int, collections.Counter[str]] = collections.defaultdict(collections.Counter)
    for _, call in facts.calls("GangCreate"):
        if len(call.args) > 1 and isinstance(call.args[0], float) and isinstance(call.args[1], str):
            names[int(call.args[0])][call.args[1]] += 1
    entries = []
    for _, call in facts.calls("CfgGang", "config_preload2.lua"):
        args = call.args
        if not isinstance(args[0], float):
            continue
        kind = int(args[0])
        strategy = _global(args[5])
        table = facts.table("config_preload2.lua", strategy) if strategy else None
        entries.append(
            {
                "id": kind,
                "strategy": strategy,
                "strategy_values": _numbers(table),
                # CfgGang(id, v2, v3, v4, v5, strategy, v7, v8, v9): the arguments by position.
                "v2": _number(args[1]),
                "v3": _number(args[2]),
                "v4": _number(args[3]),
                "v5": _number(args[4]),
                "v7": _number(args[6]),
                "v8": _number(args[7]),
                "v9": _number(args[8]),
                "music": music.get(kind),
                "script_names": [name for name, _ in names[kind].most_common(8)] or None,
            }
        )
    return sorted(entries, key=lambda entry: entry["id"])


def topic_speed_classes(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Speed classes, from `CfgSpeedClass`, and how many character types name each."""
    counts = collections.Counter(int(args[3]) for args in _cfg_chars(facts) if isinstance(args[3], float))
    entries = []
    for _, call in facts.calls("CfgSpeedClass", "config_preload2.lua"):
        values = _numbers(call.args[1]) or []
        values += [None] * (6 - len(values))
        if not isinstance(call.args[0], float):
            continue
        kind = int(call.args[0])
        entries.append(
            dict(zip(("id", "base", "v04", "walk", "jog", "run", "sprint"), [kind, *values[:6]], strict=True))
            | {"types": counts.get(kind)}
        )
    return sorted(entries, key=lambda entry: entry["id"])


#: How object classes are grouped on the page, by class name; anything else is "other".
OBJECT_CATEGORIES = (
    ("weapons", ("melee_weapon", "overhead_weapon", "thrown_weapon")),
    ("hats and masks", ("hat_object", "dyn_masks")),
    ("pick-ups and power-ups", ("pickup_item", "powerup_item", "dyn_pile", "dyn_icon", "dyn_objective")),
    ("doors", ("dyn_door_swinging", "dyn_door_sliding", "sub_swinging_door", "sub_sliding_door")),
    ("props", ("simple_object", "fade_object", "rotating_object", "dyn_table", "dyn_blocker", "dyn_lizzies")),
)


def topic_objects(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Object types, from every `CfgObj` (22 arguments)."""
    category = {cls: group for group, classes in OBJECT_CATEGORIES for cls in classes}
    seen: dict[str, dict[str, Any]] = {}
    for script, call in facts.calls("CfgObj"):
        args = call.args
        if len(args) < 20 or not isinstance(args[0], str) or args[0] in seen:
            continue
        object_type = _global(args[18], "OBJECT.")
        seen[args[0]] = {
            "name": args[0],
            "category": category.get(str(args[1]), "other"),
            "class": _global(args[1]),
            "type": object_type,
            "shape": _global(args[8], "PHYS."),
            "axis": _global(args[9], "AXIS."),
            "size": _numbers(args[7]),
            "mass": _number(args[10]),
            "material": _global(args[12], "MATERIAL."),
            "pickup_anim": _global(args[13], "ANIM."),
            "anim_set": _global(args[19], "ANIM."),
            "script": script,
            "image": _image(images, "objects", args[0]),
        }
    order = [group for group, _ in OBJECT_CATEGORIES] + ["other"]
    return sorted(seen.values(), key=lambda entry: (order.index(entry["category"]), entry["name"]))


def topic_object_groups(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Object groups: the names `CfgChar` gives as a drop group, and any `CfgObjectGroup` definitions."""
    defined = {call.args[0] for _, call in facts.calls("CfgObjectGroup") if call.args and isinstance(call.args[0], str)}
    users: dict[str, list[int]] = collections.defaultdict(list)
    for args in _cfg_chars(facts):
        group = _global(args[14])
        if group:
            users[group].append(int(args[0]))
    return [
        {"name": name, "defined": name in defined, "referenced_by": sorted(users.get(name, [])) or None}
        for name in sorted(set(users) | defined)
    ]


def topic_levels(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Level records, from `config_preload3.lua`'s `levelNames`, with what the disc holds for each."""
    table = facts.table("config_preload3.lua", "levelNames")
    rows = table.as_list() if table else []
    files = set(facts.names.values())
    entries = []
    for index, row in enumerate(rows):
        if not isinstance(row, lua4.Table):
            continue
        values = row.as_list()
        name = values[0] if isinstance(values[0], str) else None
        packs = sum(1 for f in files if name and re.fullmatch(rf"{re.escape(name)}_\d+\.pak", f))
        entries.append(
            {
                "index": index,
                "name": name,
                "world": values[1] if isinstance(values[1], str) else None,
                "number": _number(values[2]),
                "sections": _number(values[3]),
                "order": _number(values[4]),
                "flag1": _global(values[5]),
                "intro": _global(values[6]),
                "outro": _global(values[7]),
                "lock": _global(values[8]),
                "map": [_int(round(v, 4)) for v in values[9:12] if isinstance(v, float)] or None,
                "v78": _number(values[12]),
                "subway": _global(values[13]),
                "v80": _number(values[14]),
                "has_lev": bool(name and f"{name}.lev" in files),
                "packs": packs,
            }
        )
    return entries


#: A story level's main script, and a Rumble arena's per-mode flag script (`level<N>_<mode>_init.lua`).
_LEVEL_MAIN = re.compile(r"^level(\d+)\.lua$")
_RUMBLE_INIT = re.compile(r"^level(\d+)_([a-z0-9]+)_init\.lua$", re.IGNORECASE)
#: Tables a level script keeps its per-checkpoint scripts in: `tMission[k][3]` or `Mission[k]` / `Chapter[k]`.
_CHAPTER_TABLES = ("Mission", "Chapter", "tChapters")
#: The flag list a Rumble arena puts player 1 on the first of (`fP1[1]`, docs/research/characters.md#level-starts).
RUMBLE_PLAYER_FLAGS = "fP1"


def _player_creates(facts: lua4.ChunkFacts) -> dict[str, lua4.Call]:
    """Function path -> its first `HuCreate` for player 1 (sixth argument 1) at a literal position."""
    found: dict[str, lua4.Call] = {}
    for call in facts.calls:
        if call.callee != "HuCreate" or len(call.args) < 6 or call.args[5] != 1.0:
            continue
        pos = call.args[2]
        if isinstance(pos, lua4.Table) and len(_numbers(pos) or []) == 3 and isinstance(call.args[3], float):
            found.setdefault(call.path, call)
    return found


def _checkpoint_creators(facts: lua4.ChunkFacts, creators: set[str]) -> dict[int, str]:
    """Checkpoint -> the function that creates the Warriors for it.

    A level script indexes a list by `GetCheckPoint()` and calls the entry: either the function itself
    (`PlayerGang = {AddWarriors1, ...}`) or a row whose first item is it (`tMission = {{AddWarriors2,
    "Checkpoint1", "level99_combat"}, ...}`). The list is the one, global or local, naming the most creators.
    """
    best: dict[int, str] = {}
    for table in [*facts.tables.values(), *(table for _, table in facts.constructed)]:
        found = {}
        for key, item in table.items.items():
            head = item.items.get(1) if isinstance(item, lua4.Table) else item
            if isinstance(head, lua4.Global) and head.name in creators:
                found[key] = head.name
        if len(found) > len(best):
            best = found
    return best


def _chapter_scripts(facts: lua4.ChunkFacts) -> dict[int, str]:
    """Checkpoint -> the script it loads (`preLoadFile`), from `tMission[k][3]` or a `Mission` / `Chapter` list."""
    scripts: dict[int, str] = {}
    mission = facts.tables.get("tMission")
    if mission is not None:
        for key, row in mission.items.items():
            if isinstance(row, lua4.Table) and isinstance(row.items.get(3), str):
                scripts[key] = str(row.items[3])
    for name in _CHAPTER_TABLES:
        table = facts.tables.get(name)
        if table is not None:
            for key, item in table.items.items():
                if isinstance(item, str):
                    scripts.setdefault(key, item)
    return scripts


def level_starts(scripts: dict[str, lua4.ChunkFacts]) -> list[dict[str, Any]]:
    """Where each level puts player 1: per checkpoint of a story level, and per mode of a Rumble arena."""
    entries: list[dict[str, Any]] = []
    for script, facts in scripts.items():
        match = _LEVEL_MAIN.match(script)
        if not match:
            continue
        level = f"level{match.group(1)}"
        functions = facts.functions()
        creates = _player_creates(facts)
        creators = {name for name, path in functions.items() if path in creates}
        chapters = _chapter_scripts(facts)
        for checkpoint, creator in sorted(_checkpoint_creators(facts, creators).items()):
            call = creates[functions[creator]]
            entries.append(
                {
                    "id": f"{level}-{checkpoint}",
                    "level": level,
                    "checkpoint": checkpoint,
                    "character": call.args[0] if isinstance(call.args[0], str) else None,
                    "type": _int(call.args[1]) if isinstance(call.args[1], float) else None,
                    "pos": [_int(round(v, 4)) for v in _numbers(call.args[2]) or []],
                    "heading": _int(call.args[3]),
                    "via": f"HuCreate in {creator}",
                    "script": chapters.get(checkpoint),
                }
            )
    for script, facts in scripts.items():
        match = _RUMBLE_INIT.match(script)
        flags = facts.tables.get(RUMBLE_PLAYER_FLAGS)
        if match is None or flags is None:
            continue
        flag = flags.items.get(1)
        if not isinstance(flag, lua4.CallResult) or flag.callee != "AddFlag" or len(flag.args) < 3:
            continue
        entries.append(
            {
                "id": f"level{match.group(1)}-{match.group(2).lower()}",
                "level": f"level{match.group(1)}",
                "mode": match.group(2).lower(),
                "pos": [_int(round(v, 4)) for v in _numbers(flag.args[1]) or []],
                "heading": _number(flag.args[2]),
                "via": f"flag {flag.args[0]}" if isinstance(flag.args[0], str) else "flag",
                "script": script.removesuffix(".lua"),
            }
        )
    return sorted(entries, key=lambda e: (int(e["level"][5:]), e.get("checkpoint") or 0, e.get("mode") or ""))


def topic_level_starts(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Player 1's start per level and checkpoint (story) or mode (Rumble), from the level scripts."""
    return level_starts(facts.scripts)


def topic_animations(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Every distinct clip on the disc, with its length, its displacement and where it is found."""
    seen: collections.Counter[str] = collections.Counter()
    entries = []
    for clip in facts.clips:
        seen[clip.name] += 1
        data = sorted(facts.resource_name(h) or f"{h:08x}" for h in clip.data_resources)
        files = sorted(facts.resource_name(h) or f"{h:08x}" for h in clip.files)
        entries.append(
            {
                "id": clip.name if seen[clip.name] == 1 else f"{clip.name}#{seen[clip.name]}",
                "frames": round(clip.duration * FPS),
                "duration": round(clip.duration, 4),
                "distance": round(clip.distance, 4),
                "events": clip.events,
                "characters": data[:6] + ([f"... {len(data) - 6} more"] if len(data) > 6 else []) or None,
                "files": files or None,
                "packs": len(clip.packs),
            }
        )
    return entries


def topic_anim_ids(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """The 722 anim ids: their script names, default slots and the generic and Rembrandt clips."""
    names: dict[int, str] = {}
    facts_royal = facts.scripts.get("royal.lua")
    for assignment in facts_royal.assignments if facts_royal else []:
        if assignment.name.startswith("ANIM_") and isinstance(assignment.value, float):
            names.setdefault(int(assignment.value), assignment.name)
    slots = {anim: slot for slot, anim in reversed(list(enumerate(_anim_slots(facts))))}
    rembrandt = _model_records(facts).get(_crc("warr_re_cv"))
    entries = []
    for anim in range(ANIM_IDS):
        generic = facts.clip_for(_crc(GENERIC_DATA), anim)
        own = facts.clip_for(rembrandt[1], anim) if rembrandt else None
        entries.append(
            {
                "id": anim,
                "name": names.get(anim),
                "slot": slots.get(anim) if anim else None,
                "generic": generic.name if generic else None,
                "rembrandt": own.name if own and own is not generic else None,
            }
        )
    return entries


def topic_controls(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """The pad's buttons and sticks: the button word's bits and the glyph tags that draw them."""
    entries: list[dict[str, Any]] = [
        {"id": f"pad-{name.replace(' ', '-').lower()}", "context": "pad", "input": name, "bit": bit, "glyph": glyph}
        for bit, name, glyph in BUTTONS
    ]
    entries += [
        {"id": "pad-left-stick", "context": "pad", "input": "left stick", "glyph": "<LAS>"},
        {"id": "pad-right-stick", "context": "pad", "input": "right stick", "glyph": "<RAS>"},
    ]
    return entries


def topic_hud_colours(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """The colour table `CL` and the HUD slots `CfgHUDColor` gives its colours."""
    table = facts.table("config_preload2.lua", "CL") or facts.table("global.lua", "CL")
    slots = {
        _global(call.args[1], "CL."): int(call.args[0])
        for _, call in facts.calls("CfgHUDColor")
        if len(call.args) > 1 and isinstance(call.args[0], float)
    }
    entries = []
    for key, value in table.fields.items() if table else []:
        rgba = None
        markup = None
        if isinstance(value, str):
            match = re.search(r"<COLOR ([0-9A-Fa-f]{8})>", value)
            rgba = match.group(1).upper() if match else None
            markup = value
        elif isinstance(value, lua4.Table):
            numbers = _numbers(value)
            if numbers and len(numbers) == 4:
                rgba = "".join(f"{int(v):02X}" for v in numbers)
                markup = str(numbers)
        entries.append(
            {"id": str(key), "rgba": rgba, "swatch": rgba, "markup": markup, "hud_slot": slots.get(str(key))}
        )
    return entries


def topic_text_formatting(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """The 66 markup tags of the executable's tag table, with the glyph characters of the button tags."""
    data = facts.elf_bytes(TAG_TABLE, TAG_COUNT * TAG_SIZE)
    tags = [data[i * TAG_SIZE : (i + 1) * TAG_SIZE].split(b"\0")[0].decode("latin-1") for i in range(TAG_COUNT)]
    # The glyph characters of tags 33-49 (docs/research/gui.md#markup, read from the code).
    glyphs = (0x9F, 0x9D, 0x96, ord("n"), 0x9E, 0x97, 0x93, 0x9C, 0x94, 0x92, 0xA0, 0x95, 0x91, 0x9B, 0x99, 0x9A, 0x98)
    return [
        {"index": index, "tag": tag, "char": glyphs[index - 33] if 33 <= index < 33 + len(glyphs) else None}
        for index, tag in enumerate(tags)
    ]


def _plain_constant(value: Any) -> Any:
    """A constant's value for the list: numbers and strings as they are, anything else skipped (None)."""
    if isinstance(value, float):
        return _int(round(value, 6))
    if isinstance(value, str) and len(value) <= 40:
        return value
    return None


def topic_enums(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Constant tables (`MATERIAL.GLASS`) and loose constants (`LT_PLAY`) the preloads and global.lua define."""
    entries: dict[str, dict[str, Any]] = {}
    for script in ENUM_SCRIPTS:
        chunk = facts.scripts.get(script)
        if chunk is None:
            continue
        for name, table in chunk.tables.items():
            if not _CONSTANT.match(name) or name in ("CL", "ANIM", "GSTRING", "LABEL") or not table.fields:
                continue
            values = {str(k): _plain_constant(v) for k, v in table.fields.items()}
            if not all(isinstance(v, (int, float)) for v in values.values()):
                continue
            for key, value in sorted(values.items(), key=lambda kv: (kv[1], kv[0])):
                entries.setdefault(
                    f"{name}.{key}",
                    {"id": f"{name}.{key}", "enum": name, "name": key, "value": value, "script": script},
                )
        for assignment in chunk.assignments:
            value = _plain_constant(assignment.value)
            if not _CONSTANT.match(assignment.name) or not isinstance(value, (int, float)):
                continue
            prefix = assignment.name.split("_")[0] if "_" in assignment.name else "other"
            entries.setdefault(
                assignment.name,
                {
                    "id": assignment.name,
                    "enum": f"{prefix}_*",
                    "name": assignment.name,
                    "value": value,
                    "script": script,
                },
            )
    return sorted(
        entries.values(),
        key=lambda e: (e["enum"], e["value"] if isinstance(e["value"], (int, float)) else 0, e["name"]),
    )


def topic_sound(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Music tracks, interface sounds, sound matrices and inventory sounds the scripts configure."""
    entries: dict[str, dict[str, Any]] = {}
    played: dict[str, set[str]] = collections.defaultdict(set)
    for callee in ("SoundPlayMusicTrack", "SoundLoopMusicTrack"):
        for script, call in facts.calls(callee):
            if call.args and isinstance(call.args[0], str):
                played[call.args[0]].add(script)
    for _, call in facts.calls("SndCfgMusicInfo"):
        if call.args and isinstance(call.args[0], str):
            entries.setdefault(
                f"music:{call.args[0]}",
                {
                    "id": f"music:{call.args[0]}",
                    "kind": "music track",
                    "name": call.args[0],
                    "values": [_number(v) for v in call.args[1:]] or None,
                    "used_by": sorted(played.get(call.args[0], set()))[:8] or None,
                },
            )
    for _, call in facts.calls("SoundCfgInterfaceSound"):
        if len(call.args) > 1 and isinstance(call.args[0], float) and isinstance(call.args[1], str):
            key = f"interface:{int(call.args[0])}"
            entries.setdefault(
                key, {"id": key, "kind": "interface sound", "name": call.args[1], "number": int(call.args[0])}
            )
    for script, call in facts.calls("SndLoadMatrix"):
        if call.args and isinstance(call.args[0], str):
            key = f"matrix:{call.args[0]}"
            entries.setdefault(key, {"id": key, "kind": "sound matrix", "name": call.args[0], "used_by": []})
            entries[key]["used_by"] = sorted({*entries[key]["used_by"], script})
    for _, call in facts.calls("CfgInventoryItem"):
        args = call.args
        if len(args) > 4 and isinstance(args[1], float) and isinstance(args[3], str):
            key = f"inventory:{int(args[1])}"
            entries.setdefault(
                key,
                {
                    "id": key,
                    "kind": "inventory item",
                    "name": args[3],
                    "number": int(args[1]),
                    "values": [_number(args[2]), _number(args[4])],
                    "used_by": [args[0]] if isinstance(args[0], str) else None,
                },
            )
    order = ("music track", "interface sound", "sound matrix", "inventory item")
    return sorted(entries.values(), key=lambda e: (order.index(e["kind"]), e.get("number") or 0, e["name"]))


def topic_script_events(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Message numbers `SetMsgHandler` registers callbacks for, with the commonest callback names."""
    handlers: dict[int, collections.Counter[str]] = collections.defaultdict(collections.Counter)
    clears: collections.Counter[int] = collections.Counter()
    for _, call in facts.calls("SetMsgHandler"):
        if len(call.args) < 3 or not isinstance(call.args[1], float):
            continue
        message = int(call.args[1])
        callback = call.args[2]
        if callback is None:
            clears[message] += 1
        else:
            handlers[message][_global(callback) or "?"] += 1
    return [
        {
            "id": message,
            "handlers": sum(handlers[message].values()),
            "clears": clears[message],
            "examples": [name for name, _ in handlers[message].most_common(5) if name != "?"] or None,
        }
        for message in sorted(set(handlers) | set(clears))
    ]


def topic_wad_names(facts: DiscFacts, images: Path | None) -> list[dict[str, Any]]:
    """Every WAD entry whose name was recovered, grouped by the name's extension."""
    entries = []
    for entry in facts.entries:
        name = facts.names.get(entry.hash)
        if name is None:
            continue
        kind = name.rsplit(".", 1)[1].lower() if "." in name else "no extension"
        entries.append({"crc": entry.hash, "name": name, "kind": kind, "index": entry.index})
    return sorted(entries, key=lambda e: (e["kind"], e["name"]))


#: The extractor of each topic, by file stem.
EXTRACTORS: dict[str, Callable[[DiscFacts, Path | None], list[dict[str, Any]]]] = {
    "characters": topic_characters,
    "character-models": topic_character_models,
    "gangs": topic_gangs,
    "speed-classes": topic_speed_classes,
    "objects": topic_objects,
    "object-groups": topic_object_groups,
    "levels": topic_levels,
    "level-starts": topic_level_starts,
    "animations": topic_animations,
    "anim-ids": topic_anim_ids,
    "controls": topic_controls,
    "hud-colours": topic_hud_colours,
    "text-formatting": topic_text_formatting,
    "enums": topic_enums,
    "sound": topic_sound,
    "script-events": topic_script_events,
    "wad-names": topic_wad_names,
}
