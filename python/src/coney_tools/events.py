# SPDX-License-Identifier: GPL-3.0-or-later
"""Event logs of a playthrough, and their comparison: what a player would notice, in order, on each game.

A differential playthrough plays the same mission on the original and on Coney and compares what happened, not where
everyone stood: the hints shown, the objectives and checkpoints set, the scenes, the script callbacks fired, the sounds
started, the humans who ran out of health (docs/guides/research-workflow.md#differential-playthroughs). Each game
writes an **event log**, a CSV of `step,kind,name,detail`:

- Coney writes it itself (`coney --event-log`, src/core/event_log.h).
- The original's comes from hook call logs (original_events(): the hooks of `research/traces/patches.toml` that
  `research/traces/events.toml` names) and the recorder's watch of the humans.

compare() lines the two logs up as **ordered sequences**, not by step: load times and the AI's timing differ, so the
same events come at other steps. Both logs are first **normalized** by the rules file (`research/traces/events.toml`):
a script binding the player sees the effect of becomes its own kind (`SetCheckPoint` a checkpoint), a long text (a
hint, an objective) is named by a hash of it so no game text reaches a report, and an event that fires too often to
order (a per-update callback, footsteps) is compared by count instead. What is left is matched in order; the report
lists the events the original has and Coney lacks (missing), the reverse (extra), the ones both have in another order
(out of order), and matched events whose spacing from the event before differs by more than a window (timing).
"""

from __future__ import annotations

import bisect
import csv
import difflib
import io
import itertools
import struct
import tomllib
import zlib
from collections import Counter
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass, field, replace
from pathlib import Path

#: A text longer than this, or holding markup, is named by its hash (text_name()).
MAX_PLAIN = 40

#: The Lua 4 type tags of a script argument as the original's hooks log them (lua.h: LUA_TNIL ... LUA_TFUNCTION).
LUA_NIL, LUA_NUMBER, LUA_STRING, LUA_TABLE, LUA_FUNCTION = 1, 2, 3, 4, 5

HEADER = ["step", "kind", "name", "detail"]


class EventError(Exception):
    """An event log or rules file that cannot be read."""


@dataclass(frozen=True)
class Event:
    """One event: the update it happened in, its kind (`call`, `hint`, ...), its name and a detail (arguments)."""

    step: int
    kind: str
    name: str
    detail: str = ""


def load_events(path: Path) -> list[Event]:
    """Read an event log CSV (header `step,kind,name,detail`). Raises EventError when it is not one."""
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        raise EventError(f"{path}: {error}") from error
    return parse_events(text, str(path))


def starting_at(events: Sequence[Event], first: str) -> list[Event]:
    """The events from the first whose `kind name` starts with `first` on (all of them when `first` is empty), its
    steps counted from it: two logs that began at different points of a mission, cut to the same start. Raises
    EventError when no event matches."""
    if not first:
        return list(events)
    for index, event in enumerate(events):
        if f"{event.kind} {event.name}".startswith(first):
            return [replace(e, step=e.step - event.step) for e in events[index:]]
    raise EventError(f"no event starts with {first!r}")


def parse_events(text: str, where: str = "events") -> list[Event]:
    """Events from CSV text, as load_events() reads them."""
    rows = csv.reader(io.StringIO(text))
    header = next(rows, None)
    if header is None or [cell.strip() for cell in header[:3]] != HEADER[:3]:
        raise EventError(f"{where}: not an event log (expected the header {','.join(HEADER)})")
    events = []
    for number, row in enumerate(rows, start=2):
        if not row:
            continue
        try:
            step = int(row[0])
        except (ValueError, IndexError) as error:
            raise EventError(f"{where}:{number}: the step is not a number") from error
        cells = [*row[1:], "", "", ""]
        events.append(Event(step, cells[0], cells[1], cells[2]))
    return events


def events_csv(events: Iterable[Event]) -> str:
    """The CSV text of `events`, as Coney writes its log."""
    out = io.StringIO()
    writer = csv.writer(out, lineterminator="\n")
    writer.writerow(HEADER)
    for event in events:
        writer.writerow([event.step, event.kind, event.name, event.detail])
    return out.getvalue()


# --- the original's events from hook logs -----------------------------------------------------------------------


def lua_argument(tag: int, value: int, high: int, text: str) -> str:
    """One script argument as Coney's traces show it: `nil`, a number (`%g`), a quoted string, `{table}`."""
    if tag == LUA_NIL:
        return "nil"
    if tag == LUA_NUMBER:
        number = struct.unpack("<d", struct.pack("<II", value & 0xFFFFFFFF, high & 0xFFFFFFFF))[0]
        return f"{number:g}"
    if tag == LUA_STRING:
        return f'"{text}"'
    if tag == LUA_TABLE:
        return "{table}"
    if tag == LUA_FUNCTION:
        return "function"
    return f"t{tag}"


@dataclass(frozen=True)
class HookEvents:
    """How one hook's log becomes events: the event `kind`, and the column (or the binding map, for `binding`)
    that names each."""

    hook: str
    kind: str
    name: str = ""
    binding: str = ""
    args: tuple[tuple[str, str, str, str], ...] = ()
    format: str = ""


def _number(cell: str) -> int:
    """A logged cell as an integer (hex words and decimals alike); 0 when empty."""
    cell = cell.strip()
    if not cell:
        return 0
    return int(cell, 0) if cell.startswith(("0x", "-0x")) else int(float(cell))


def original_events(
    logs: Mapping[str, list[dict[str, str]]],
    mapping: Sequence[HookEvents],
    bindings: Mapping[int, str],
    arity: Mapping[str, int] | None = None,
) -> list[Event]:
    """The original's events from its hook logs: `logs` maps a hook to its rows (as csv.DictReader gives them, with
    `seq` and `step`); `mapping` says how each hook's rows become events; `bindings` names a script binding by its
    C function's address, and `arity` gives how many arguments it takes (the hook logs two slots whatever the call
    passed, so a slot past a binding's arguments is stack left-overs and is dropped). The events come out in the
    ring's order (`seq`), which is the order the game made them."""
    made: list[tuple[int, Event]] = []
    for rule in mapping:
        for row in logs.get(rule.hook, []):
            name = ""
            if rule.binding:
                address = _number(row.get(rule.binding, ""))
                name = bindings.get(address, f"0x{address:08x}")
            elif rule.name:
                cell = row.get(rule.name, "")
                name = f"0x{_number(cell):08x}" if rule.format == "hex" else cell
            args = [
                lua_argument(_number(row.get(t, "")), _number(row.get(v, "")), _number(row.get(h, "")), row.get(s, ""))
                for t, v, h, s in rule.args
                if row.get(t, "")
            ]
            if arity is not None and name in arity:
                args = args[: arity[name]]
            while len(args) > 1 and args[-1] == "nil":
                args.pop()
            made.append((_number(row["seq"]), Event(_number(row["step"]), rule.kind, name, ", ".join(args))))
    made.sort(key=lambda pair: pair[0])
    return [event for _, event in made]


def read_hook_logs(trace: Path, hooks: Iterable[str]) -> dict[str, list[dict[str, str]]]:
    """The rows of each hook log `<trace stem>.<hook>.csv` beside `trace` (a hook with no file has none)."""
    logs: dict[str, list[dict[str, str]]] = {}
    for hook in hooks:
        path = trace.with_name(f"{trace.stem}.{hook}.csv")
        if path.is_file():
            with path.open(encoding="utf-8", errors="replace", newline="") as handle:
                logs[hook] = list(csv.DictReader(handle))
    return logs


# --- the rules ----------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class CallRule:
    """A script binding that means something to the player: its event `kind` and which argument names it (1-based;
    0 for none)."""

    kind: str
    arg: int = 1


@dataclass
class Rules:
    """How events are normalized and compared (`research/traces/events.toml`)."""

    calls: dict[str, CallRule] = field(default_factory=dict)
    kinds: tuple[str, ...] = ()
    ignore: dict[str, tuple[str, ...]] = field(default_factory=dict)
    frequent: int = 60
    window: int = 300
    burst: int = 15
    progress: tuple[str, ...] = ()
    by_set: tuple[str, ...] = ()
    hooks: tuple[HookEvents, ...] = ()
    unnamed: tuple[str, ...] = ()


def load_rules(path: Path) -> Rules:
    """The rules file. Raises EventError when it cannot be read or is malformed."""
    try:
        with path.open("rb") as handle:
            raw = tomllib.load(handle)
    except (OSError, tomllib.TOMLDecodeError) as error:
        raise EventError(f"{path}: {error}") from error
    try:
        calls = {
            name: CallRule(str(table["kind"]), int(table.get("arg", 1))) for name, table in raw.get("calls", {}).items()
        }
        hooks = tuple(
            HookEvents(
                hook=str(table["hook"]),
                kind=str(table["kind"]),
                name=str(table.get("name", "")),
                binding=str(table.get("binding", "")),
                args=tuple(tuple(str(cell) for cell in arg) for arg in table.get("args", [])),  # type: ignore[misc]
                format=str(table.get("format", "")),
            )
            for table in raw.get("original", [])
        )
        compare = raw.get("compare", {})
        ignore = {kind: tuple(str(n) for n in names) for kind, names in raw.get("ignore", {}).items()}
        return Rules(
            calls=calls,
            kinds=tuple(str(kind) for kind in compare.get("kinds", [])),
            ignore=ignore,
            frequent=int(compare.get("frequent", 60)),
            window=int(compare.get("window", 300)),
            burst=int(compare.get("burst", 15)),
            progress=tuple(str(kind) for kind in compare.get("progress", [])),
            by_set=tuple(str(kind) for kind in compare.get("by_set", [])),
            hooks=hooks,
            unnamed=tuple(str(kind) for kind in compare.get("unnamed", [])),
        )
    except (KeyError, TypeError, ValueError) as error:
        raise EventError(f"{path}: {error}") from error


def text_name(text: str) -> str:
    """A name for a text: itself when short and plain, else `text#` and its CRC-32, so long game text (a hint, an
    objective) can be compared without being shown."""
    if len(text) <= MAX_PLAIN and "<" not in text and "\n" not in text:
        return text
    return f"text#{zlib.crc32(text.encode('utf-8')):08x}"


def first_arguments(detail: str) -> list[str]:
    """The arguments of a call's detail (`"a, b", 2, nil`), split at top-level commas; strings keep their quotes."""
    out: list[str] = []
    current = ""
    quoted = False
    depth = 0
    for char in detail:
        if char == '"':
            quoted = not quoted
        elif not quoted and char == "{":
            depth += 1
        elif not quoted and char == "}":
            depth -= 1
        if char == "," and not quoted and depth == 0:
            out.append(current.strip())
            current = ""
            continue
        current += char
    if current.strip() or out:
        out.append(current.strip())
    return out


def _unquote(argument: str) -> str:
    """A string argument without its quotes; any other argument as it is."""
    if len(argument) >= 2 and argument.startswith('"') and argument.endswith('"'):
        return argument[1:-1]
    return argument


def normalize(event: Event, rules: Rules) -> Event:
    """The event as compared: a binding of `rules.calls` becomes its kind named by its argument, a text gets its
    text_name() (a nil argument none), and the kinds of `rules.unnamed` lose their names (the two games name humans
    differently)."""
    if event.kind == "call" and event.name in rules.calls:
        rule = rules.calls[event.name]
        arguments = first_arguments(event.detail)
        argument = _unquote(arguments[rule.arg - 1]) if 0 < rule.arg <= len(arguments) else ""
        # A nil argument names nothing: the original's log drops arguments past a binding's arity, Coney's keeps them.
        name = f"{event.name}:{text_name(argument)}" if argument and argument != "nil" else event.name
        return Event(event.step, rule.kind, name, event.detail)
    if event.kind in rules.unnamed:
        return Event(event.step, event.kind, "", event.detail)
    if event.kind == "hint":
        return Event(event.step, event.kind, text_name(event.name), "")
    return event


# --- the comparison -----------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Timing:
    """A matched pair whose spacing from the pair before differs: `gap_original` and `gap_coney` updates."""

    original: Event
    coney: Event
    gap_original: int
    gap_coney: int


@dataclass
class EventComparison:
    """What compare() found."""

    matched: list[tuple[Event, Event]] = field(default_factory=list)
    missing: list[Event] = field(default_factory=list)
    extra: list[Event] = field(default_factory=list)
    out_of_order: list[tuple[Event, Event]] = field(default_factory=list)
    timing: list[Timing] = field(default_factory=list)
    counts: list[tuple[str, int, int]] = field(default_factory=list)
    window: int = 0
    reached: Event | None = None
    stopped_before: Event | None = None

    @property
    def ok(self) -> bool:
        """True when Coney has every event the original has, in its order."""
        return not self.missing and not self.out_of_order


def _key(event: Event) -> tuple[str, str]:
    """What two events must share to be the same event."""
    return event.kind, event.name


def prepare(events: Sequence[Event], rules: Rules, kinds: Sequence[str] | None = None) -> list[Event]:
    """The events compare() orders: normalized, of the chosen kinds (all when none are chosen), without the ignored
    names, and without a **burst**: an event whose kind and name came less than `rules.burst` updates before (a
    hint shown again at once, a callback run every update, a pad handler's down and up)."""
    wanted = set(kinds) if kinds else set(rules.kinds)
    out: list[Event] = []
    last: dict[tuple[str, str], int] = {}
    for raw in events:
        event = normalize(raw, rules)
        if wanted and event.kind not in wanted:
            continue
        if any(event.name == name or event.name.startswith(f"{name}:") for name in rules.ignore.get(event.kind, ())):
            continue
        key = _key(event)
        before = last.get(key)
        last[key] = event.step
        if before is not None and event.step - before < rules.burst:
            continue
        out.append(event)
    return out


def _lcs(a: Sequence[Event], b: Sequence[Event]) -> tuple[list[tuple[Event, Event]], list[Event], list[Event]]:
    """The longest common subsequence of two event sequences by kind and name (as difflib finds it): the matched
    pairs, and the events left over on each side."""
    matcher = difflib.SequenceMatcher(None, [_key(e) for e in a], [_key(e) for e in b], autojunk=False)
    matched: list[tuple[Event, Event]] = []
    rest_a: list[Event] = []
    rest_b: list[Event] = []
    last = 0, 0
    for block in matcher.get_matching_blocks():
        rest_a += a[last[0] : block.a]
        rest_b += b[last[1] : block.b]
        matched += list(zip(a[block.a : block.a + block.size], b[block.b : block.b + block.size], strict=True))
        last = block.a + block.size, block.b + block.size
    return matched, rest_a, rest_b


def _segment(step: int, anchors: Sequence[int]) -> int:
    """How many anchor steps come before or at `step`: the segment of the log it falls in."""
    return bisect.bisect_right(anchors, step)


def compare(
    original: Sequence[Event],
    coney: Sequence[Event],
    rules: Rules,
    kinds: Sequence[str] | None = None,
    window: int | None = None,
) -> EventComparison:
    """Compare two event logs (the module docstring) in three passes:

    1. An event seen more than `rules.frequent` times in either log is compared by count only; a kind of
       `rules.by_set` (sounds) by which names each log has at all, since its order follows the AI's timing.
    2. The **milestones**, the events of the `rules.progress` kinds (tutorial, checkpoint, objective, ...), are
       matched in order across both logs; they cut each log into segments, and the furthest milestone both reached
       and the original's next say where Coney's playthrough stopped.
    3. Every other kind is matched in order within each segment pair, so a hint is matched only between the
       milestones it came between. An event left over on both sides is out of order; the rest are missing or extra.

    Timing compares the milestones' spacing."""
    a, b = prepare(original, rules, kinds), prepare(coney, rules, kinds)
    result = EventComparison(window=rules.window if window is None else window)
    # 1. Frequent events by count; set kinds by presence.
    count_a, count_b = Counter(map(_key, a)), Counter(map(_key, b))
    frequent = {key for key in count_a.keys() | count_b.keys() if max(count_a[key], count_b[key]) > rules.frequent}
    result.counts = sorted(
        (f"{kind} {name}".strip(), count_a[(kind, name)], count_b[(kind, name)]) for kind, name in frequent
    )
    a = [event for event in a if _key(event) not in frequent]
    b = [event for event in b if _key(event) not in frequent]
    for kind in rules.by_set:
        first_a = {_key(e): e for e in reversed(a) if e.kind == kind}
        first_b = {_key(e): e for e in reversed(b) if e.kind == kind}
        result.missing += [e for key, e in first_a.items() if key not in first_b]
        result.extra += [e for key, e in first_b.items() if key not in first_a]
        result.matched += [(e, first_b[key]) for key, e in first_a.items() if key in first_b]
    a = [event for event in a if event.kind not in rules.by_set]
    b = [event for event in b if event.kind not in rules.by_set]
    # 2. The milestones, in order across their kinds.
    leftovers_a: list[Event] = []
    leftovers_b: list[Event] = []
    milestones, rest_a, rest_b = _lcs(
        [e for e in a if e.kind in rules.progress], [e for e in b if e.kind in rules.progress]
    )
    result.matched += milestones
    leftovers_a += rest_a
    leftovers_b += rest_b
    anchors_a = [pair[0].step for pair in milestones]
    anchors_b = [pair[1].step for pair in milestones]
    # 3. Every other kind, segment by segment.
    for kind in sorted({e.kind for e in a + b} - set(rules.progress)):
        for segment in range(len(milestones) + 1):
            matched, rest_a, rest_b = _lcs(
                [e for e in a if e.kind == kind and _segment(e.step, anchors_a) == segment],
                [e for e in b if e.kind == kind and _segment(e.step, anchors_b) == segment],
            )
            result.matched += matched
            leftovers_a += rest_a
            leftovers_b += rest_b
    # Leftovers on both sides are out of order; the rest missing or extra.
    pool: dict[tuple[str, str], list[Event]] = {}
    for event in sorted(leftovers_b, key=lambda e: e.step):
        pool.setdefault(_key(event), []).append(event)
    for event in sorted(leftovers_a, key=lambda e: e.step):
        partners = pool.get(_key(event))
        if partners:
            result.out_of_order.append((event, partners.pop(0)))
        else:
            result.missing.append(event)
    result.extra += [event for events in pool.values() for event in events]
    result.matched.sort(key=lambda pair: pair[0].step)
    result.missing.sort(key=lambda event: event.step)
    result.extra.sort(key=lambda event: event.step)
    result.out_of_order.sort(key=lambda pair: pair[0].step)
    # Timing: each milestone's spacing from the one before.
    for (pa, pb), (qa, qb) in itertools.pairwise(milestones):
        gap_a, gap_b = qa.step - pa.step, qb.step - pb.step
        if abs(gap_a - gap_b) > result.window:
            result.timing.append(Timing(qa, qb, gap_a, gap_b))
    # Progress: the furthest milestone both reached, and the original's next one.
    if milestones:
        result.reached = milestones[-1][0]
    after = result.reached.step if result.reached is not None else -1
    result.stopped_before = next((e for e in result.missing if e.kind in rules.progress and e.step > after), None)
    return result


def text_labels(texts: Mapping[str, str]) -> dict[str, str]:
    """Labels for hashed texts: `texts` maps a label (a string table key such as `TT_22a`) to its text, as a table
    read from the game gives it; the result maps each text's text_name() to its label."""
    return {text_name(text): label for label, text in texts.items()}


def report(comparison: EventComparison, limit: int = 40, labels: Mapping[str, str] | None = None) -> str:
    """The comparison as text: the counts, then each list (at most `limit` lines each). `labels` (text_labels())
    shows a hashed text by its label."""
    names = labels or {}

    def _show(event: Event) -> str:
        """One event in a report line, a hashed text by its label when there is one."""
        name = event.name
        for hashed in {part for part in name.replace(":", " ").split() if part.startswith("text#")}:
            if hashed in names:
                name = name.replace(hashed, names[hashed])
        return f"{event.kind} {name}".strip()

    c = comparison
    lines = [
        f"matched {len(c.matched)}, missing {len(c.missing)}, extra {len(c.extra)}, "
        f"out of order {len(c.out_of_order)}, timing {len(c.timing)} (window {c.window} updates)"
    ]

    def section(title: str, rows: list[str]) -> None:
        """A titled list, cut at `limit`."""
        if not rows:
            return
        lines.append("")
        lines.append(f"{title}:")
        lines.extend(f"  {row}" for row in rows[:limit])
        if len(rows) > limit:
            lines.append(f"  ... {len(rows) - limit} more")

    if c.reached is not None or c.stopped_before is not None:
        reached = f"{c.reached.step} {_show(c.reached)}" if c.reached is not None else "none"
        lines.append(f"furthest milestone both reached (original step): {reached}")
        if c.stopped_before is not None:
            lines.append(f"Coney never reached (original step): {c.stopped_before.step} {_show(c.stopped_before)}")
    section("missing in Coney (original step)", [f"{e.step:>7}  {_show(e)}" for e in c.missing])
    section("extra in Coney (Coney step)", [f"{e.step:>7}  {_show(e)}" for e in c.extra])
    section(
        "out of order (original step, Coney step)",
        [f"{a.step:>7} {b.step:>7}  {_show(a)}" for a, b in c.out_of_order],
    )
    section(
        "timing (gap from the event before: original, Coney)",
        [f"{t.gap_original:>7} {t.gap_coney:>7}  {_show(t.original)}" for t in c.timing],
    )
    section(
        "compared by count (original, Coney)",
        [f"{a:>7} {b:>7}  {name}" for name, a, b in c.counts if a != b],
    )
    return "\n".join(lines)
