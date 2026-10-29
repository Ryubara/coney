# SPDX-License-Identifier: GPL-3.0-or-later
"""The Conclave snapshot: everything the Claude Code mod draws, as one JSON object (docs/guides/conclave.md).

The mod (`tools/claude-mod`) is written in TypeScript and cannot import what knows Coney's formats: the task file, the
stamp log, the claim cards, the hook settings. So one command reads them and prints the result, and the mod only
draws it. The snapshot is read-only: it writes nothing, to GitHub or to the repository (a conflict fetches `main` and
gets a throwaway worktree and a ledger in the scratch folder: `conflict_work`). It reuses `coord status` for the build
slots, the detached builds and the pull requests, and `claim list` for the claim cards.

Each section is an object with `error` (None when it was read); a section that could not be read holds only its
`error` and the others stand, so a failure is never taken for "nothing there". The GitHub sections (`claims`, `prs`,
`issues`, `events`) are None without `github`. `SCHEMA` rises when a section changes shape; the mod refuses a newer one.

Nothing here knows about a session's health (its context window, its prompt cache, its clearing): that stays with
Tether, and the snapshot carries none of it.
"""

from __future__ import annotations

import tomllib
from collections.abc import Callable
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from coney_tools import stamps
from coney_tools.conclave_github import add_github
from coney_tools.conclave_sources import Sources, default_sources, iso_utc, section, strings
from coney_tools.issue_claim_model import iso_time, scopes_overlap
from coney_tools.task_file import TaskFile, TaskFileError, parse_task_file

__all__ = ["SCHEMA", "Sources", "collect", "default_sources", "format_text"]

#: The version of the snapshot's shape; the mod refuses a snapshot of a newer one.
SCHEMA = 2
#: The longest goal line sent for a unit (the pane shows one line).
GOAL_CHARACTERS = 200
#: A step result that counts as the step being done (`none`: the step was not needed, with a reason in the stamp).
DONE_RESULTS = ("pass", "none")


# --- config ---------------------------------------------------------------------------------------------------------


def _hook_settings(src: Sources) -> dict[str, Any]:
    """The roles, helpers and `require_role` from the hook settings, which the mod's agent list is made from."""
    path = src.checkout / src.conventions.conclave.hooks_file
    try:
        data = tomllib.loads(path.read_bytes().decode("utf-8"))
    except OSError as error:
        raise OSError(f"{src.conventions.conclave.hooks_file}: cannot be read ({error.strerror or error})") from error
    except (UnicodeDecodeError, tomllib.TOMLDecodeError) as error:
        raise ValueError(f"{src.conventions.conclave.hooks_file}: not valid TOML ({error})") from error
    roles: list[str] = []
    table = data.get("roles")
    # The role of other agent tools' sessions (`[harnesses] default_role`) is no Claude Code agent type.
    harnesses = data.get("harnesses")
    other = harnesses.get("default_role") if isinstance(harnesses, dict) else None
    if isinstance(table, dict):
        for name, role in table.items():
            if name == other:
                continue
            roles.append(str(name))
            roles.extend(strings(role.get("aliases")) if isinstance(role, dict) else [])
    return {
        "roles": list(dict.fromkeys(roles)),
        "helpers": strings(data.get("helpers")),
        "require_role": data.get("require_role") is True,
    }


def _config(src: Sources) -> dict[str, Any]:
    """What the mod needs to know and must not hold itself: the allowed agent types and the numbers it draws by.

    A hook file that cannot be read gives empty lists and the reason; the mod then hides no agent type.
    """
    conclave = src.conventions.conclave
    watch = conclave.watch
    base: dict[str, Any] = {
        "source": conclave.hooks_file,
        "claude_code": conclave.claude_code,
        "intervals": {
            "local": conclave.local_seconds,
            "github": conclave.github_seconds,
            "github_idle": conclave.github_idle_seconds,
        },
        "pane_min_columns": conclave.pane_min_columns,
        "pane_compact_columns": conclave.pane_compact_columns,
        "pane_wide_columns": conclave.pane_wide_columns,
        "band_narrow_columns": conclave.band_narrow_columns,
        "stale_after_reads": conclave.stale_after_reads,
        "merged_visible_minutes": conclave.merged_visible_minutes,
        "claim_red_hours": conclave.claim_red_hours,
        "watch": {
            "roles": list(watch.roles),
            "preamble": watch.preamble,
            "github_seconds": watch.github_seconds,
            "wake_gap_seconds": watch.wake_gap_seconds,
            "defer_typing_minutes": watch.defer_typing_minutes,
            "resend_minutes": watch.resend_minutes,
            "stop_max_blocks": watch.stop_max_blocks,
            "stop_reset_minutes": watch.stop_reset_minutes,
            "stop_preamble": watch.stop_preamble,
        },
    }
    try:
        settings = _hook_settings(src)
    except (OSError, ValueError) as error:
        return {"error": str(error), "roles": [], "helpers": [], "require_role": False, **base}
    return {"error": None, **settings, **base}


# --- units ----------------------------------------------------------------------------------------------------------


def _step_rows(report: stamps.Report, steps: tuple[str, ...]) -> tuple[list[dict[str, Any]], str, str | None]:
    """Each step's latest state, the stage (the step after the highest one done) and the step that failed there."""
    latest: dict[str, stamps.Stamp] = {}
    for stamp in report.stamps:
        latest[stamp.step] = stamp
    rows = [
        {
            "step": step,
            "state": latest[step].result if step in latest else "missing",
            "time": iso_utc(latest[step].moment) if step in latest else None,
        }
        for step in steps
    ]
    done = max((index for index, row in enumerate(rows) if row["state"] in DONE_RESULTS), default=-1)
    stage = steps[min(done + 1, len(steps) - 1)]
    failed = stage if rows[steps.index(stage)]["state"] == "fail" else None
    return rows, stage, failed


def _agent_of(report: stamps.Report) -> dict[str, str] | None:
    """The id and type of the agent that wrote the newest stamp, if it says."""
    if not report.stamps:
        return None
    who = report.stamps[-1].who
    agent = who.get("agent") if isinstance(who, dict) else None
    if not isinstance(agent, dict):
        return None
    return {"id": str(agent.get("id", "unknown")), "type": str(agent.get("type", "unknown"))}


def _build_of(lanes: dict[str, Any], tree: str) -> dict[str, Any] | None:
    """The build a worktree runs or waits for: in a slot first, else in a queue."""
    for lane, data in lanes.items():
        for slot in data.get("slots", []):
            claim = slot.get("claim")
            if claim and Path(str(claim.get("worktree", ""))).name == tree:
                return {
                    "state": "running",
                    "preset": claim.get("preset"),
                    "step": claim.get("step"),
                    "slot": slot.get("slot"),
                    "lane": lane,
                    "since": claim.get("started"),
                }
    for lane, data in lanes.items():
        for ticket in data.get("queue", []):
            if Path(str(ticket.get("worktree", ""))).name == tree:
                return {
                    "state": "waiting",
                    "preset": ticket.get("preset"),
                    "position": ticket.get("position"),
                    "lane": lane,
                }
    return None


def _last_build(src: Sources, tree: Path) -> dict[str, Any] | None:
    """A worktree's last detached build (`build result`), in the three fields the pane shows."""
    record = src.coord.build_result(tree)
    if record is None:
        return None
    ended = record.get("ended")
    return {
        "state": record.get("state"),
        "exit_code": record.get("exit_code"),
        "ended": iso_utc(datetime.fromtimestamp(float(ended), UTC)) if isinstance(ended, int | float) else None,
    }


def _goal(task: TaskFile) -> str:
    """The first line of the task's Goal section, cut to fit one row."""
    for line in task.sections.get("Goal", "").splitlines():
        if line.strip():
            return line.strip()[:GOAL_CHARACTERS]
    return ""


def _unit(
    src: Sources,
    folder: Path,
    tree: tuple[Path, str | None] | None,
    lanes: dict[str, Any],
    now: datetime,
) -> dict[str, Any] | None:
    """One unit's row, or None when it is too old to show (no worktree and unchanged for `unit_days`)."""
    conventions = src.conventions
    task_file = folder / conventions.tasks.file
    report = stamps.verify(folder / "stamps", conventions.stamps)
    changed = datetime.fromtimestamp(task_file.stat().st_mtime, UTC)
    if report.stamps:
        changed = max(changed, report.stamps[-1].moment)
    if tree is None and (now - changed).total_seconds() > conventions.conclave.unit_days * 86400:
        return None
    path = tree[0] if tree else None
    row: dict[str, Any] = {
        "task": folder.name,
        "error": None,
        "issues": [],
        "kind": None,
        "claimant": None,
        "claimed_at": None,
        "scope": [],
        "goal": "",
        "branch": tree[1] if tree else None,
        "worktree": str(path) if path else None,
        "busy": bool(path and (path / conventions.workspace.busy_marker).exists()),
        "stage": None,
        "stage_since": None,
        "failed": None,
        "steps": [],
        "agent": None,
        "stamp_problems": sum(1 for problem in report.problems if problem.severity == "error"),
        "build": _build_of(lanes, path.name) if path else None,
        "last_build": _last_build(src, path) if path else None,
        "overlaps": [],
        "pr": None,
    }
    try:
        task = parse_task_file(task_file.read_text(encoding="utf-8"), conventions.tasks)
    except (TaskFileError, OSError, UnicodeDecodeError) as error:
        row["error"] = str(error)
        return row
    steps, stage, failed = _step_rows(report, conventions.stamps.steps)
    claimed_at = iso_time(task.claimed_at)
    row.update(
        issues=list(task.issues),
        kind=task.kind,
        claimant=task.claimant,
        claimed_at=claimed_at,
        scope=list(task.scope),
        goal=_goal(task),
        stage=stage,
        failed=failed,
        steps=steps,
        stage_since=iso_utc(report.stamps[-1].moment) if report.stamps else claimed_at,
        agent=_agent_of(report),
    )
    return row


def _units(src: Sources, lanes: dict[str, Any], now: datetime) -> dict[str, Any]:
    """Every folder of the scratch directory that holds a task file, with its worktree, stage, build and overlaps."""
    scratch = src.coord.scratch
    if not scratch.is_dir():
        raise OSError(f"no scratch directory at {scratch}")
    trees = {(branch or "").replace("/", "-"): (path, branch) for path, branch in src.coord.worktrees() if branch}
    rows: list[dict[str, Any]] = []
    for folder in sorted((p for p in scratch.iterdir() if p.is_dir()), key=lambda p: p.name):
        if not (folder / src.conventions.tasks.file).is_file():
            continue
        row = _unit(src, folder, trees.get(folder.name), lanes, now)
        if row is not None:
            rows.append(row)
    for row in rows:
        row["overlaps"] = [
            {"with": other["task"], "kind": "unit", "scope": found}
            for other in rows
            if other is not row and (found := scopes_overlap(row["scope"], other["scope"]))
        ]
    return {"units": rows}


# --- machine --------------------------------------------------------------------------------------------------------


def _machine(src: Sources) -> dict[str, Any]:
    """Free disk space with the two limits the band colours it by."""
    workspace = src.conventions.workspace
    free = src.free_bytes()
    return {
        "free_gb": round(free / 1024**3, 1),
        "min_free_gb": workspace.disk_min_free_gb,
        "warn_free_gb": workspace.disk_min_free_gb + workspace.disk_margin_gb,
    }


# --- the whole snapshot ---------------------------------------------------------------------------------------------


def collect(src: Sources, github: bool, unit: str | None = None) -> dict[str, Any]:
    """The whole snapshot; the GitHub sections are None without `github`. With `unit`, only that unit's row stays."""
    now = src.coord.now()
    report: dict[str, Any] = {
        "schema": SCHEMA,
        "time": iso_utc(now),
        "checkout": str(src.checkout),
        "github": github,
        "config": _config(src),
    }
    report["builds"] = section(lambda: {"lanes": src.coord.builds()})
    lanes = report["builds"].get("lanes") or {}
    report["units"] = section(lambda: _units(src, lanes, now))
    report["machine"] = section(lambda: _machine(src))
    report["claims"] = report["prs"] = report["issues"] = report["events"] = None
    if github:
        add_github(src, report)
    if unit is not None and report["units"].get("units") is not None:
        report["units"]["units"] = [row for row in report["units"]["units"] if row["task"] == unit]
    return report


def format_text(report: dict[str, Any]) -> list[str]:
    """A short text form: one line per section."""
    lines = [f"conclave snapshot {report['time']}{' (with GitHub)' if report['github'] else ''}"]

    def line(name: str, describe: Callable[[dict[str, Any]], str]) -> None:
        section = report.get(name)
        if section is None:
            return
        lines.append(
            f"{name}: not read ({section['error']})" if section.get("error") else f"{name}: {describe(section)}"
        )

    line("config", lambda s: f"{len(s['roles'])} roles, {len(s['helpers'])} helpers")
    line("units", lambda s: f"{len(s['units'])} units")
    line("builds", lambda s: f"{len(s['lanes'])} lanes")
    line("machine", lambda s: f"{s['free_gb']} GB free")
    line("claims", lambda s: f"{len(s['claims'])} held, signed in as {s['account'] or 'unknown'}")
    line("prs", lambda s: f"{len(s['prs'])} open, {len(s.get('merged', []))} merged recently")
    line("issues", lambda s: f"{len(s['issues'])} listed")
    line("events", lambda s: f"{sum(e['state'] == 'open' for e in s['events'])} open, {len(s['events'])} listed")
    return lines
