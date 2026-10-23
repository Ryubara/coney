# SPDX-License-Identifier: GPL-3.0-or-later
"""The Conclave snapshot: everything the Claude Code mod draws, as one JSON object (docs/guides/conclave.md).

The mod (`tools/claude-mod`) is written in TypeScript and cannot import what knows Coney's formats: the task file, the
stamp log, the claim cards, the hook settings. So one command reads them and prints the result, and the mod only
draws it. The snapshot is read-only: it writes nothing, to GitHub or to disk. It reuses `coord status` for the build
slots, the detached builds and the pull requests, and `claim list` for the claim cards.

Each section is an object with `error` (None when it was read); a section that could not be read holds only its
`error` and the others stand, so a failure is never taken for "nothing there". The GitHub sections (`claims`, `prs`,
`issues`) are None without `github`. `SCHEMA` rises when a section changes shape; the mod refuses a newer one.

Nothing here knows about a session's health (its context window, its prompt cache, its clearing): that stays with
Tether, and the snapshot carries none of it.
"""

from __future__ import annotations

import json
import shutil
import tomllib
from collections.abc import Callable
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from coney_tools import coord_cli, stamps
from coney_tools.audit_github import run_gh
from coney_tools.config import ConfigError, find_repo_root, workspace_root
from coney_tools.conventions import Conventions, load_conventions
from coney_tools.issue_claim_cli import held_cards
from coney_tools.issue_claim_model import Card, iso_time, scopes_overlap
from coney_tools.issue_claim_remote import GhRemote
from coney_tools.task_file import TaskFile, TaskFileError, parse_task_file

#: The version of the snapshot's shape; the mod refuses a snapshot of a newer one.
SCHEMA = 1
#: The longest goal line sent for a unit (the pane shows one line).
GOAL_CHARACTERS = 200
#: How many recently merged pull requests are read.
MERGED_LIMIT = 30
#: How many open issues are read for their titles (the same limit `coord status` uses).
ISSUE_LIMIT = 500
#: A step result that counts as the step being done (`none`: the step was not needed, with a reason in the stamp).
DONE_RESULTS = ("pass", "none")


@dataclass
class Sources:
    """Where `collect` reads from; tests swap each one for a fake."""

    conventions: Conventions
    #: The checkout that holds the hook settings (the one the mod was loaded from).
    checkout: Path
    #: What `coord status` reads: the scratch folder, the worktrees, the build slots, `gh` and the clock.
    coord: coord_cli.Sources
    #: Free bytes on the workspace's drive.
    free_bytes: Callable[[], int]
    #: The held claim cards (OSError or ConfigError when GitHub cannot be read).
    claims: Callable[[], list[Card]]


def default_sources(cwd: Path) -> Sources:
    """The real sources for the checkout that holds `cwd`."""
    root = find_repo_root(cwd)
    conventions = load_conventions(root)
    rules = conventions.claims

    def claims() -> list[Card]:
        return held_cards(GhRemote(rules.bot_login, rules.card_marker, rules.reply_marker, run_gh), conventions)

    return Sources(
        conventions=conventions,
        checkout=root,
        coord=coord_cli.default_sources(cwd),
        free_bytes=lambda: shutil.disk_usage(workspace_root(root)).free,
        claims=claims,
    )


def _iso(moment: datetime) -> str:
    """A moment as ISO 8601 UTC with seconds, `2026-10-23T14:00:00Z`."""
    return moment.astimezone(UTC).strftime("%Y-%m-%dT%H:%M:%SZ")


def _section(read: Callable[[], dict[str, Any]]) -> dict[str, Any]:
    """One section: what `read` returns with `error` None, or only `error` when it could not be read."""
    try:
        return {"error": None, **read()}
    except (ConfigError, OSError, ValueError, KeyError, TypeError) as error:
        return {"error": str(error) or type(error).__name__}


def _strings(value: object) -> list[str]:
    """A settings value that should be a list of strings, as one; anything else gives an empty list."""
    return [item for item in value if isinstance(item, str)] if isinstance(value, list) else []


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
    if isinstance(table, dict):
        for name, role in table.items():
            roles.append(str(name))
            roles.extend(_strings(role.get("aliases")) if isinstance(role, dict) else [])
    return {
        "roles": list(dict.fromkeys(roles)),
        "helpers": _strings(data.get("helpers")),
        "require_role": data.get("require_role") is True,
    }


def _config(src: Sources) -> dict[str, Any]:
    """What the mod needs to know and must not hold itself: the allowed agent types and the numbers it draws by.

    A hook file that cannot be read gives empty lists and the reason; the mod then hides no agent type.
    """
    conclave = src.conventions.conclave
    base: dict[str, Any] = {
        "source": conclave.hooks_file,
        "claude_code": conclave.claude_code,
        "intervals": {
            "local": conclave.local_seconds,
            "github": conclave.github_seconds,
            "github_idle": conclave.github_idle_seconds,
        },
        "pane_min_columns": conclave.pane_min_columns,
        "merged_visible_minutes": conclave.merged_visible_minutes,
        "claim_red_hours": conclave.claim_red_hours,
    }
    try:
        settings = _hook_settings(src)
    except (OSError, ValueError) as error:
        return {"error": str(error), "roles": [], "helpers": [], "require_role": False, **base}
    return {"error": None, **settings, **base}


# --- units ----------------------------------------------------------------------------------------------------------


def _no_signature_check(body: bytes, armor: str, moment: datetime) -> str:
    """Stand-in for the signature check: the snapshot reads the log, `stamp verify` is what proves it."""
    return ""


def _step_rows(report: stamps.Report, steps: tuple[str, ...]) -> tuple[list[dict[str, Any]], str, str | None]:
    """Each step's latest state, the stage (the step after the highest one done) and the step that failed there."""
    latest: dict[str, stamps.Stamp] = {}
    for stamp in report.stamps:
        latest[stamp.step] = stamp
    rows = [
        {
            "step": step,
            "state": latest[step].result if step in latest else "missing",
            "time": _iso(latest[step].moment) if step in latest else None,
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
        "ended": _iso(datetime.fromtimestamp(float(ended), UTC)) if isinstance(ended, int | float) else None,
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
    report = stamps.verify(folder / "stamps", conventions.stamps, verifier=_no_signature_check)
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
        stage_since=_iso(report.stamps[-1].moment) if report.stamps else claimed_at,
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


def _overlaps_with_claims(units: list[dict[str, Any]], cards: list[Card]) -> None:
    """Add the claims whose scope overlaps a unit's (the unit's own claim is no overlap)."""
    for row in units:
        for card in cards:
            if card.issue in row["issues"] or (card.task and card.task == row["task"]):
                continue
            found = scopes_overlap(row["scope"], card.scope)
            if found:
                row["overlaps"].append({"with": f"#{card.issue}", "kind": "claim", "scope": found})


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


# --- GitHub ---------------------------------------------------------------------------------------------------------


def _labels(item: dict[str, Any]) -> list[str]:
    """An issue's label names."""
    return [str(label.get("name", "")) for label in item.get("labels") or [] if isinstance(label, dict)]


def _issue_titles(src: Sources) -> list[dict[str, Any]]:
    """The open issues as `gh` lists them (number, title, labels, assignees)."""
    raw = json.loads(
        src.coord.gh(
            ["issue", "list", "--state", "open", "--limit", str(ISSUE_LIMIT), "--json", "number,title,labels,assignees"]
        )
    )
    if not isinstance(raw, list):
        raise ValueError("gh issue list did not return a list")
    return raw


def _issues_section(open_issues: list[dict[str, Any]], wanted: set[int], critical: str) -> dict[str, Any]:
    """The open issues the pane names: those a claim or a unit holds, and the critical ones."""
    rows: list[dict[str, Any]] = []
    for item in open_issues:
        number = int(item["number"])
        labels = _labels(item)
        if number not in wanted and critical not in labels:
            continue
        rows.append(
            {
                "number": number,
                "title": str(item.get("title") or ""),
                "status": next((name for name in labels if name.startswith("S-")), None),
                "priority": next((name for name in labels if name.startswith("P-")), None),
                "assignees": [str(a.get("login", "")) for a in item.get("assignees") or [] if isinstance(a, dict)],
            }
        )
    rows.sort(key=lambda row: row["number"])
    return {"issues": rows}


def _claims_section(src: Sources, titles: dict[int, str], cards: list[Card], account: str | None) -> dict[str, Any]:
    """The held claims with their deadlines (None while a claim waits), titles and the signed-in account."""
    rules = src.conventions.claims
    rows = [
        {
            "issue": card.issue,
            "title": titles.get(card.issue),
            "claimant": card.claimant,
            "status": card.status,
            "task": card.task,
            "scope": list(card.scope),
            "stage": card.stage,
            "pr": card.pr,
            "claimed_at": iso_time(card.claimed_at),
            "last_activity": iso_time(card.activity),
            "deadline": None if card.waiting else iso_time(card.deadline(rules)),
            "waiting": card.waiting,
            "warned": card.warned_at is not None,
            "adopted": card.adopted,
        }
        for card in cards
    ]
    return {
        "account": account,
        "rules": {"warn_hours": rules.warn_hours, "deadline_hours": rules.deadline_hours, "enforced": rules.release},
        "claims": rows,
    }


def _account(src: Sources) -> str | None:
    """The signed-in `gh` account, or None when it cannot be told (the pane then marks no claim as the viewer's)."""
    try:
        login = json.loads(src.coord.gh(["api", "user"])).get("login")
    except (OSError, ValueError, AttributeError):
        return None
    return str(login) if login else None


def _merged(src: Sources) -> list[dict[str, Any]]:
    """The recently merged pull requests: number, branch and when."""
    raw = json.loads(
        src.coord.gh(
            ["pr", "list", "--state", "merged", "--limit", str(MERGED_LIMIT), "--json", "number,headRefName,mergedAt"]
        )
    )
    return [
        {"number": int(pull["number"]), "branch": str(pull.get("headRefName", "")), "merged_at": pull.get("mergedAt")}
        for pull in raw
        if isinstance(pull, dict) and pull.get("mergedAt")
    ]


def _attach_pull_requests(units: list[dict[str, Any]], prs: dict[str, Any]) -> None:
    """Give each unit the pull request of its branch: an open one, else a recently merged one."""
    opened = {pull["branch"]: pull for pull in prs.get("prs", [])}
    merged = {pull["branch"]: pull for pull in prs.get("merged", [])}
    for row in units:
        for key in (row["branch"], row["task"]):
            if key in opened:
                pull = opened[key]
                row["pr"] = {
                    "number": pull["number"],
                    "state": "open",
                    "ci": pull["ci"],
                    "causes": pull["causes"],
                    "merged_at": None,
                }
                break
            if key in merged:
                row["pr"] = {
                    "number": merged[key]["number"],
                    "state": "merged",
                    "ci": None,
                    "causes": [],
                    "merged_at": merged[key]["merged_at"],
                }
                break


def _github(src: Sources, report: dict[str, Any]) -> None:
    """Add the `claims`, `prs` and `issues` sections, and attach pull requests and claim overlaps to the units."""
    units: list[dict[str, Any]] = (report["units"] or {}).get("units", [])
    prs = coord_cli.collect(src.coord, True, ["pr"])["prs"]
    if isinstance(prs, dict) and not prs.get("error"):
        try:
            prs["merged"] = _merged(src)
        except (OSError, ValueError, KeyError, TypeError) as error:
            prs["merged"] = []
            prs["merged_error"] = str(error)
    report["prs"] = prs
    if isinstance(prs, dict) and not prs.get("error"):
        _attach_pull_requests(units, prs)

    open_issues: list[dict[str, Any]] = []
    issues_error: str | None = None
    try:
        open_issues = _issue_titles(src)
    except (OSError, ValueError, KeyError, TypeError) as error:
        issues_error = str(error) or type(error).__name__
    titles = {int(item["number"]): str(item.get("title") or "") for item in open_issues}

    def claims() -> dict[str, Any]:
        cards = src.claims()
        _overlaps_with_claims(units, cards)
        return _claims_section(src, titles, cards, _account(src))

    report["claims"] = _section(claims)
    if issues_error is not None:
        report["issues"] = {"error": issues_error}
        return
    held = {row["issue"] for row in report["claims"].get("claims", [])}
    mine = {number for row in units for number in row["issues"]}
    report["issues"] = {
        "error": None,
        **_issues_section(open_issues, held | mine, src.conventions.claims.critical_label),
    }


# --- the whole snapshot ---------------------------------------------------------------------------------------------


def collect(src: Sources, github: bool, unit: str | None = None) -> dict[str, Any]:
    """The whole snapshot; the GitHub sections are None without `github`. With `unit`, only that unit's row stays."""
    now = src.coord.now()
    report: dict[str, Any] = {
        "schema": SCHEMA,
        "time": _iso(now),
        "checkout": str(src.checkout),
        "github": github,
        "config": _config(src),
    }
    report["builds"] = _section(lambda: {"lanes": src.coord.builds()})
    lanes = report["builds"].get("lanes") or {}
    report["units"] = _section(lambda: _units(src, lanes, now))
    report["machine"] = _section(lambda: _machine(src))
    report["claims"] = report["prs"] = report["issues"] = None
    if github:
        _github(src, report)
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
    return lines
