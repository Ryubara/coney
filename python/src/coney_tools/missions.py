# SPDX-License-Identifier: GPL-3.0-or-later
"""The mission status list: `research/missions.yaml` loaded, checked and joined with the bindings coverage.

The list is hand-kept (an implementer updates it in the same commit as mission work). `missions_render` turns it into
`docs/missions/`. Each mission's checkpoint count is checked against the Sections column of the levels list, and its
status against its checkpoints', so the page cannot claim more than the checklist shows.

Research: docs/guides/research-workflow.md#missions
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml

from coney_tools import natives
from coney_tools.config import ConfigError

DATA_FILE = Path("research/missions.yaml")
LEVELS_FILE = Path("research/references/levels.yaml")

#: The five states of a mission or checkpoint, in lifecycle order: key -> (icon, label, what it means).
STATUSES: dict[str, tuple[str, str, str]] = {
    "not-started": ("⬜", "Not Started", "No Coney code for it yet."),
    "in-progress": ("\U0001f6a7", "In Progress", "Being built."),
    "pending-approval": (
        "\U0001f3ae",
        "Pending Gameplay Approval",
        "Built to its end in Coney; waits for the owner's play-test against the original.",
    ),
    "needs-fixes": (
        "\U0001f527",
        "Needs Fixes",
        "A play-test found something off; back in maintenance, the known issues are listed.",
    ),
    "approved": ("✅", "Approved", "The owner played it and signed it off."),
}
#: Statuses that mean the code for it is written, whatever the play-test said.
BUILT = ("pending-approval", "needs-fixes", "approved")
GROUPS: dict[str, str] = {
    "story": "Story missions",
    "hub": "The hub",
    "flashback": "Flashback missions",
    "armies": "Armies of the Night",
}


@dataclass(frozen=True)
class Checkpoint:
    """One checkpoint of a mission: its number, status and an optional short note."""

    number: int
    status: str
    note: str = ""


@dataclass(frozen=True)
class Link:
    """A link to a research page: its title and the path under `docs/` (with an optional anchor)."""

    title: str
    path: str


@dataclass(frozen=True)
class Mission:
    """One level's entry of the list."""

    level: int
    group: str
    slot: str
    title: str
    summary: str
    status: str
    checkpoints: tuple[Checkpoint, ...]
    research: tuple[Link, ...] = ()
    issues: tuple[str, ...] = ()
    questions: tuple[str, ...] = ()
    test: str = ""
    notes: str = ""

    @property
    def name(self) -> str:
        """The level's name, `level99`."""
        return f"level{self.level}"

    def built(self) -> int:
        """How many checkpoints are written (pending approval, needing fixes or approved)."""
        return sum(1 for c in self.checkpoints if c.status in BUILT)

    def approved(self) -> int:
        """How many checkpoints the owner has signed off."""
        return sum(1 for c in self.checkpoints if c.status == "approved")


@dataclass(frozen=True)
class MissionList:
    """The whole file: the hand-written intro and lifecycle text, and the missions in page order."""

    about: str
    lifecycle: str
    missions: tuple[Mission, ...] = field(default_factory=tuple)


@dataclass(frozen=True)
class Coverage:
    """How many script bindings a level can call, how many it is the first to call and how far those are done."""

    total: int
    new: int
    new_traced: int
    new_done: int
    families: dict[str, tuple[int, int]]  # category -> (new bindings, of them implemented in Coney)


def _text(entry: dict[str, Any], key: str, where: str, problems: list[str], required: bool = True) -> str:
    """A string field, or "" after noting a problem when it is missing or not a string."""
    value = entry.get(key)
    if isinstance(value, str) and value.strip():
        return value.strip()
    if required or value is not None:
        problems.append(f"{where}: `{key}` must be a non-empty string")
    return ""


def _strings(entry: dict[str, Any], key: str, where: str, problems: list[str]) -> tuple[str, ...]:
    """A list-of-strings field (missing means empty)."""
    value = entry.get(key) or []
    if not isinstance(value, list) or not all(isinstance(v, str) and v.strip() for v in value):
        problems.append(f"{where}: `{key}` must be a list of non-empty strings")
        return ()
    return tuple(v.strip() for v in value)


def _checkpoints(
    entry: dict[str, Any], where: str, sections: int | None, problems: list[str]
) -> tuple[Checkpoint, ...]:
    """The checkpoint list from `count`, `default` and `each`, checked against the levels list's section count."""
    spec = entry.get("checkpoints")
    if not isinstance(spec, dict):
        problems.append(f"{where}: `checkpoints` must be a mapping with `count`, `default` and optional `each`")
        return ()
    count = spec.get("count")
    default = spec.get("default")
    each = spec.get("each") or {}
    if not isinstance(count, int) or count < 1:
        problems.append(f"{where}: checkpoints.count must be a positive integer")
        return ()
    if sections is not None and count != sections:
        problems.append(f"{where}: checkpoints.count is {count} but the levels list has {sections} sections")
    if default not in STATUSES:
        problems.append(f"{where}: checkpoints.default must be one of {', '.join(STATUSES)}")
        return ()
    result = []
    for number in range(1, count + 1):
        item = each.get(number) or {}
        status = item.get("status", default)
        if status not in STATUSES:
            problems.append(f"{where}: checkpoint {number}: status must be one of {', '.join(STATUSES)}")
            status = default
        result.append(Checkpoint(number, status, str(item.get("note", "")).strip()))
    stray = sorted(str(k) for k in each if not isinstance(k, int) or not 1 <= k <= count)
    if stray:
        problems.append(f"{where}: checkpoints.each has keys outside 1-{count}: {stray}")
    return tuple(result)


def _check_status(mission: Mission, problems: list[str]) -> None:
    """The mission's status must agree with its checkpoints' and its known issues."""
    where = mission.name
    states = {c.status for c in mission.checkpoints}
    status = mission.status
    if status == "not-started" and states != {"not-started"}:
        problems.append(f"{where}: Not Started but a checkpoint has started")
    if status in BUILT and states & {"not-started", "in-progress"}:
        problems.append(f"{where}: {STATUSES[status][1]} needs every checkpoint built")
    if status == "approved" and states != {"approved"}:
        problems.append(f"{where}: Approved needs every checkpoint approved")
    if status == "needs-fixes" and not mission.issues:
        problems.append(f"{where}: Needs Fixes must list its known issues")
    if status == "pending-approval" and "needs-fixes" in states:
        problems.append(f"{where}: Pending Gameplay Approval with a checkpoint that needs fixes")
    if status == "in-progress" and states <= {"not-started"}:
        problems.append(f"{where}: In Progress but no checkpoint has started")


def _sections(root: Path) -> dict[int, int]:
    """Level number -> section count from the levels list (empty when the file is missing)."""
    path = root / LEVELS_FILE
    if not path.is_file():
        return {}
    data = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
    return {e["number"]: e["sections"] for e in data.get("entries", []) if "number" in e and "sections" in e}


def _mission(entry: Any, index: int, sections: dict[int, int], problems: list[str]) -> Mission | None:
    """One entry, or None after noting why it is unusable."""
    if not isinstance(entry, dict) or not isinstance(entry.get("level"), int):
        problems.append(f"missions[{index}]: needs an integer `level`")
        return None
    where = f"level{entry['level']}"
    group = entry.get("group")
    if group not in GROUPS:
        problems.append(f"{where}: `group` must be one of {', '.join(GROUPS)}")
    status = entry.get("status")
    if status not in STATUSES:
        problems.append(f"{where}: `status` must be one of {', '.join(STATUSES)}")
    research = []
    for link in entry.get("research") or []:
        if isinstance(link, dict) and isinstance(link.get("title"), str) and isinstance(link.get("path"), str):
            research.append(Link(link["title"], link["path"]))
        else:
            problems.append(f"{where}: each research link needs `title` and `path`")
    return Mission(
        level=entry["level"],
        group=str(group),
        slot=_text(entry, "slot", where, problems),
        title=_text(entry, "title", where, problems),
        summary=_text(entry, "summary", where, problems),
        status=str(status),
        checkpoints=_checkpoints(entry, where, sections.get(entry["level"]), problems),
        research=tuple(research),
        issues=_strings(entry, "issues", where, problems),
        questions=_strings(entry, "questions", where, problems),
        test=_text(entry, "test", where, problems, required=False),
        notes=_text(entry, "notes", where, problems, required=False),
    )


def parse(text: str, sections: dict[int, int] | None = None, expected: set[int] | None = None) -> MissionList:
    """The list from YAML text. Raises ConfigError naming every problem found.

    `expected` is the set of levels that must be listed (default: level 99 and every `natives.STORY_LEVELS` level).
    """
    try:
        data = yaml.safe_load(text)
    except yaml.YAMLError as error:
        raise ConfigError(f"{DATA_FILE}: invalid YAML ({error})") from error
    if not isinstance(data, dict) or not isinstance(data.get("missions"), list):
        raise ConfigError(f"{DATA_FILE}: needs a top-level `missions` list")
    problems: list[str] = []
    about = _text(data, "about", "file", problems)
    lifecycle = _text(data, "lifecycle", "file", problems)
    missions = []
    for index, entry in enumerate(data["missions"]):
        mission = _mission(entry, index, sections or {}, problems)
        if mission is not None:
            missions.append(mission)
            if mission.status in STATUSES:
                _check_status(mission, problems)
    seen: set[int] = set()
    for mission in missions:
        if mission.level in seen:
            problems.append(f"{mission.name}: listed twice")
        seen.add(mission.level)
    # Every level the bindings coverage tracks, and level99, must have a page.
    wanted = expected if expected is not None else {99, *(level for level, _ in natives.STORY_LEVELS)}
    problems += [f"level{level}: missing from the list" for level in sorted(wanted - seen)]
    if problems:
        raise ConfigError(f"{DATA_FILE}:\n" + "\n".join(f"  {p}" for p in problems))
    return MissionList(about, lifecycle, tuple(missions))


def load(root: Path) -> MissionList:
    """`research/missions.yaml` of the checkout at `root`, checked against its levels list."""
    path = root / DATA_FILE
    try:
        text = path.read_bytes().decode("utf-8").replace("\r\n", "\n")
    except (OSError, UnicodeDecodeError) as error:
        raise ConfigError(f"{path}: cannot be read ({error})") from error
    return parse(text, _sections(root))


def coverage(masterlist: natives.Masterlist) -> dict[int, Coverage]:
    """Per level, the bindings numbers `docs/references/bindings/story.md` shows (level 99 as `mission1.md`).

    A level's bindings are those `usage.levels` lists it for; the new ones are those no earlier level of the story
    calls, the first mission's included.
    """
    game = [b for b in masterlist.bindings if b.usage]
    first = [b for b in game if b.usage and b.usage.mission1]
    seen = {b.name for b in first}
    out = {99: _coverage(first, first)}
    for level, _label in natives.STORY_LEVELS:
        used = [b for b in game if b.usage and level in b.usage.levels]
        new = [b for b in used if b.name not in seen]
        seen |= {b.name for b in used}
        out[level] = _coverage(used, new)
    return out


def _coverage(used: list[natives.Binding], new: list[natives.Binding]) -> Coverage:
    """The counts for one level from its bindings and its new ones."""
    families: dict[str, tuple[int, int]] = {}
    for b in new:
        count, done = families.get(b.category, (0, 0))
        families[b.category] = (count + 1, done + (b.coney == "implemented"))
    order = list(natives.CATEGORIES)
    return Coverage(
        total=len(used),
        new=len(new),
        new_traced=sum(1 for b in new if b.depth == "thorough"),
        new_done=sum(1 for b in new if b.coney == "implemented"),
        families=dict(sorted(families.items(), key=lambda kv: order.index(kv[0]))),
    )
