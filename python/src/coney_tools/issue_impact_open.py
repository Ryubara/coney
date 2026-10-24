# SPDX-License-Identifier: GPL-3.0-or-later
"""The open issues `coney-tools issue impact` matches a branch against, read from `gh` or its cache.

Documented in docs/guides/coney-tools-repo.md#issue-impact. `gh` is injected, so the tests never touch the network.
"""

from __future__ import annotations

import json
from collections.abc import Callable
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from pathlib import Path

from coney_tools.config import ConfigError
from coney_tools.conventions import ImpactRules

#: What `gh issue list` is asked for.
ISSUE_FIELDS = "number,title,body,labels,url,updatedAt"
#: How many open issues one read asks for (the audit uses the same figure).
ISSUE_LIMIT = 3000


@dataclass(frozen=True)
class OpenIssue:
    """An open issue as the match reads it."""

    number: int
    title: str
    body: str
    url: str
    labels: tuple[str, ...]


#: Runs `gh` and returns its output; raises OSError when it fails.
Runner = Callable[[list[str]], str]


def parse_issues(data: object) -> list[OpenIssue]:
    """The open issues of `gh issue list --json` output; ConfigError when it is not a list of issues."""
    if not isinstance(data, list):
        raise ConfigError("the issue list is not a JSON list")
    try:
        return [
            OpenIssue(
                number=int(item["number"]),
                title=str(item.get("title", "")),
                body=str(item.get("body") or ""),
                url=str(item.get("url", "")),
                labels=tuple(str(label["name"]) for label in item.get("labels") or [] if isinstance(label, dict)),
            )
            for item in data
        ]
    except (KeyError, TypeError, ValueError) as error:
        raise ConfigError(f"the issue list is malformed ({type(error).__name__}: {error})") from error


def read_open_issues(
    cache: Path | None, rules: ImpactRules, run: Runner, now: datetime, refresh: bool = False
) -> tuple[list[OpenIssue], list[str]]:
    """The open issues and the notices about how they were read.

    The list is read from GitHub once and kept in `cache` for `rules.cache_minutes`; `refresh` reads it again. When
    GitHub cannot be reached a cache of any age is used, with a notice; with no cache either, ConfigError. A reply
    that is not a list of issues is refused before it is cached, and a damaged cache counts as no cache.
    """
    notices: list[str] = []
    stored = _load_cache(cache)
    if stored is not None and not refresh and now - stored[0] < timedelta(minutes=rules.cache_minutes):
        return stored[1], notices
    try:
        raw = json.loads(run(["issue", "list", "--state", "open", "--limit", str(ISSUE_LIMIT), "--json", ISSUE_FIELDS]))
    except (OSError, ValueError) as error:
        if stored is None:
            raise ConfigError(f"cannot read the open issues with gh ({error})") from error
        notices.append(
            f"gh could not be reached ({error}); the issues are from the cache of {stored[0]:%Y-%m-%d %H:%M} UTC"
        )
        return stored[1], notices
    # Validate the reply before keeping it, so a bad answer is never served from the cache.
    issues = parse_issues(raw)
    if len(issues) >= ISSUE_LIMIT:
        notices.append(f"{len(issues)} open issues read: the limit, so some may be missing")
    _save_cache(cache, now, raw)
    return issues, notices


def _load_cache(cache: Path | None) -> tuple[datetime, list[OpenIssue]] | None:
    """The cached issues and when they were read; None when there is no cache or it is damaged in any way."""
    if cache is None or not cache.is_file():
        return None
    try:
        data = json.loads(cache.read_text(encoding="utf-8"))
        read = datetime.fromisoformat(str(data["read"]))
        if read.tzinfo is None:
            return None
        return read, parse_issues(data["issues"])
    except (OSError, ValueError, KeyError, TypeError, ConfigError):
        return None


def _save_cache(cache: Path | None, now: datetime, issues: object) -> None:
    """Keep the issue list for the next run; a folder that cannot be written costs only the cache."""
    if cache is None:
        return
    try:
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_text(json.dumps({"read": now.astimezone(UTC).isoformat(), "issues": issues}), encoding="utf-8")
    except OSError:
        return
