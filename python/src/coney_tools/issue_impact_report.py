# SPDX-License-Identifier: GPL-3.0-or-later
"""The report of `coney-tools issue impact`: text for a person, JSON for tools.

Documented in docs/guides/coney-tools-issues.md#issue-impact.
"""

from __future__ import annotations

from collections.abc import Sequence
from typing import Any

from coney_tools.issue_impact import Candidate, Touched

#: The shape of the JSON `--json` prints; raise it when a key changes meaning.
JSON_VERSION = 1


def report_json(
    touched: Touched, candidates: Sequence[Candidate], excluded: Sequence[dict[str, Any]], notices: Sequence[str]
) -> dict[str, Any]:
    """The machine-readable report (`--json`): a stable output for tools to read, see `JSON_VERSION`."""
    return {
        "version": JSON_VERSION,
        "base": touched.base,
        "head": touched.head,
        "branch": touched.branch,
        "touched": {
            "paths": len(touched.paths),
            "addresses": len(touched.addresses),
            "names": len(touched.names),
            "sections": sum(len(anchors) for anchors in touched.sections.values()),
        },
        "issues": [candidate.to_json() for candidate in candidates],
        "excluded": list(excluded),
        "notices": [*touched.notices, *notices],
    }


def report_lines(
    candidates: Sequence[Candidate], excluded: Sequence[dict[str, Any]], notices: Sequence[str], max_reasons: int
) -> list[str]:
    """The report as text: one line per issue with why it matched, then what was left out and the notices."""
    lines = []
    for candidate in candidates:
        shown = [reason.text() for reason in candidate.reasons[:max_reasons]]
        more = len(candidate.reasons) - max_reasons
        if more > 0:
            shown.append(f"and {more} more")
        lines.append(f"#{candidate.number}  {candidate.title}  --  " + "; ".join(shown))
    lines.append(f"{len(candidates)} open issue(s) may be affected")
    for item in excluded:
        lines.append(f"left out: #{item['number']} ({item['why']})")
    lines.extend(f"note: {notice}" for notice in notices)
    return lines
