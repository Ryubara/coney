# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools trace mission` and `trace events-diff` commands: a mission playthrough on either game, and the
comparison of two event logs (docs/guides/coney-tools.md#trace)."""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

from coney_tools import natives, pcsx2_claims
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.course import COURSES, Course
from coney_tools.events import (
    EventError,
    compare,
    events_csv,
    load_events,
    load_rules,
    report,
    starting_at,
    text_labels,
)
from coney_tools.mission import ConeyGame, MissionError, MissionScenario, OriginalGame, Playthrough, load_mission, play
from coney_tools.pcsx2_state import StateError
from coney_tools.pine import PineClient, PineError
from coney_tools.recorder import RecordError
from coney_tools.wad import refuse_inside_repo

#: The rules file, relative to the checkout.
RULES_FILE = Path("research/traces/events.toml")


def _rules_path(given: Path | None) -> Path:
    """The rules file given, else the checkout's."""
    return given if given is not None else find_repo_root(Path.cwd()) / RULES_FILE


def run_events_diff(
    original: Path,
    coney: Path,
    rules_path: Path | None,
    kinds: list[str] | None,
    window: int | None,
    limit: int,
    labels_path: Path | None = None,
    first: str = "",
) -> int:
    """`trace events-diff`: print the comparison; 0 when Coney has every compared event in order, 1 otherwise.
    `labels_path` is a JSON object of label to text (a string table read from the game, kept in scratch) that names
    the hashed texts in the report; `first` (`kind name`) cuts both logs to start at their first such event."""
    try:
        rules = load_rules(_rules_path(rules_path))
        a, b = (starting_at(load_events(path), first) for path in (original, coney))
        comparison = compare(a, b, rules, kinds, window)
        labels = text_labels(json.loads(labels_path.read_text(encoding="utf-8"))) if labels_path else None
    except (EventError, OSError, ValueError) as error:
        raise ConfigError(str(error)) from error
    print(report(comparison, limit, labels))
    return 0 if comparison.ok else 1


def _progress(updates: int, course: Course) -> None:
    """One line every 1000 updates: where the course is."""
    print(f"  {updates} updates; {course.status()}", flush=True)


def _finish(result: Playthrough, scenario: MissionScenario, out: Path, side: str) -> int:
    """Report a playthrough; 0 when it reached the scenario's end."""
    end = f"{scenario.until[0]} {scenario.until[1]}" if scenario.until else "the update limit"
    state = "reached" if result.finished else "NOT reached"
    print(f"{out}: {len(result.events)} events in {result.updates} updates on {side}; {end} {state}")
    return 0 if result.finished or scenario.until is None else 1


def _coney_command(scenario: MissionScenario, executable: Path, disc: str, out: Path) -> list[str]:
    """Coney's command line for a mission scenario: headless, its level and options, the pad pipe and event log."""
    if not scenario.level:
        raise ConfigError(f"{scenario.path}: has no [coney] level")
    return [
        str(executable),
        "--disc",
        disc,
        "--play-level",
        scenario.level,
        *scenario.args,
        "--headless",
        "--frames",
        str(scenario.updates + 1000),
        "--pad-pipe",
        "--event-log",
        str(out),
    ]


def run_mission_coney(
    scenario_path: Path, out: Path, executable: Path | None, disc: str | None, updates: int | None = None
) -> int:
    """`trace mission --side coney`: play the scenario's course on Coney; Coney writes the event log to `out`."""
    from coney_tools.trace_cli import _coney_executable, _disc

    refuse_inside_repo(out)
    try:
        scenario = load_mission(scenario_path)
        rules = load_rules(_rules_path(None))
        if updates is not None:
            scenario.updates = updates
    except (MissionError, EventError) as error:
        raise ConfigError(str(error)) from error
    out.parent.mkdir(parents=True, exist_ok=True)
    command = _coney_command(scenario, _coney_executable(executable), _disc(disc), out)
    with out.with_suffix(".stderr.txt").open("w", encoding="utf-8") as stderr:
        game = ConeyGame(command, stderr)
        try:
            result = play(
                game, COURSES[scenario.course](), rules, scenario.updates, scenario.until, scenario.after, _progress
            )
        except MissionError as error:
            raise ConfigError(str(error)) from error
        finally:
            game.close()
    return _finish(result, scenario, out, "Coney")


def run_mission_original(
    scenario_path: Path,
    out: Path,
    state: str | None,
    flags: tuple[Path | None, Path | None, Path | None],
    agent: str | None,
    updates: int | None = None,
) -> int:
    """`trace mission --side original`: play the scenario's course on PCSX2 from its patched state and write the
    original's event log to `out`, under `agent`'s claim (made, and released at the end, when it holds none)."""
    from coney_tools import pcsx2_cli

    refuse_inside_repo(out)
    try:
        scenario = load_mission(scenario_path)
        rules = load_rules(_rules_path(None))
        if updates is not None:
            scenario.updates = updates
    except (MissionError, EventError) as error:
        raise ConfigError(str(error)) from error
    root = find_repo_root(Path.cwd())
    masterlist = natives.load(root).bindings
    bindings = {
        variant.wrapper: binding.name
        for binding in masterlist
        for variant in (binding.main, *binding.overloads)
        if variant.wrapper is not None
    }
    arity = {binding.name: len(binding.main.args) for binding in masterlist if binding.main.args}
    registry = pcsx2_claims.default_registry()
    claim, created = registry.claim_for_run(agent, flags[0], os.getpid())
    emulator = None
    client: PineClient | None = None
    try:
        patches, hooks = pcsx2_cli._patches(scenario.patches)
        paths = pcsx2_cli.resolve_paths(Path(claim.path), flags[1], flags[2])
        source_name = state or (str(paths.scratch / scenario.state) if scenario.state else None)
        if source_name is None:
            raise ConfigError(f"{scenario_path}: names no [original] state; pass --state")
        source = pcsx2_cli._source_state(source_name, paths.pcsx2_dir)
        copy = paths.scratch / f"{scenario_path.stem}.p2s"
        pcsx2_cli._make_copy(source, copy, patches, paths.pcsx2_dir)
        emulator = pcsx2_cli.Emulator(paths, copy, patches, on_start=lambda pid: registry.record_pid(claim, pid))
        client = emulator.client
        game = OriginalGame(client, hooks, rules.hooks, bindings, scenario.friends, scenario.enemy_brain, arity)
        try:
            result = play(
                game, COURSES[scenario.course](), rules, scenario.updates, scenario.until, scenario.after, _progress
            )
        finally:
            game.release()
        if game.ring is not None and game.ring.log.lost:
            print(f"warning: {game.ring.log.lost} hook entries were lost (the ring overflowed)", file=sys.stderr)
    except (PineError, RecordError, StateError, MissionError) as error:
        raise ConfigError(str(error)) from error
    finally:
        if client is not None:
            client.close()
        if emulator is not None:
            emulator.close()
        if created:
            registry.release(claim.agent, claim.copy)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(events_csv(result.events), encoding="utf-8")
    return _finish(result, scenario, out, "the original")
