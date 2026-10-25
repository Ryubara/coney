# SPDX-License-Identifier: GPL-3.0-or-later
"""The GitHub part of the Conclave snapshot: claims, pull requests and issues (docs/guides/conclave.md).

Every call here is a read: `gh issue list`, `gh pr list`, `gh api user` and the claim cards through the claim reader.
Nothing is created, edited, labelled or merged, and `tests/test_conclave_snapshot.py` fails if a call ever writes.
"""

from __future__ import annotations

import json
from typing import Any

from coney_tools import coord_cli
from coney_tools.conclave_events import collect_events
from coney_tools.conclave_sources import Sources, section
from coney_tools.issue_claim_model import Card, iso_time, scopes_overlap

#: How many recently merged pull requests are read.
MERGED_LIMIT = 30
#: How many open issues are read for their titles (the same limit `coord status` uses).
ISSUE_LIMIT = 500


def _overlaps_with_claims(units: list[dict[str, Any]], cards: list[Card]) -> None:
    """Add the claims whose scope overlaps a unit's (the unit's own claim is no overlap)."""
    for row in units:
        for card in cards:
            if card.issue in row["issues"] or (card.task and card.task == row["task"]):
                continue
            found = scopes_overlap(row["scope"], card.scope)
            if found:
                row["overlaps"].append({"with": f"#{card.issue}", "kind": "claim", "scope": found})


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


def add_github(src: Sources, report: dict[str, Any]) -> None:
    """Add the `claims`, `prs`, `issues` and `events` sections; attach pull requests and claim overlaps to the units."""
    units: list[dict[str, Any]] = (report["units"] or {}).get("units", [])
    read = coord_cli.collect(src.coord, True, ["pr", "unit"])
    prs = read["prs"]
    if isinstance(prs, dict) and not prs.get("error"):
        try:
            prs["merged"] = _merged(src)
        except (OSError, ValueError, KeyError, TypeError) as error:
            prs["merged"] = []
            prs["merged_error"] = str(error)
    report["prs"] = prs
    if isinstance(prs, dict) and not prs.get("error"):
        _attach_pull_requests(units, prs)
    report["events"] = section(lambda: collect_events(src, report, read["release"]))

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

    report["claims"] = section(claims)
    if issues_error is not None:
        report["issues"] = {"error": issues_error}
        return
    held = {row["issue"] for row in report["claims"].get("claims", [])}
    mine = {number for row in units for number in row["issues"]}
    report["issues"] = {
        "error": None,
        **_issues_section(open_issues, held | mine, src.conventions.claims.critical_label),
    }
