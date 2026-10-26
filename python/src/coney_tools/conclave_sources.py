# SPDX-License-Identifier: GPL-3.0-or-later
"""Where the Conclave snapshot reads from, and the two helpers every part of it shares (docs/guides/conclave.md).

`Sources` is the one seam the tests swap for fakes; `section` is how a part that cannot be read becomes an `error`
instead of a crash, so a failure is never taken for "nothing there".
"""

from __future__ import annotations

import shutil
from collections.abc import Callable
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from coney_tools import coord_cli, pr_ready
from coney_tools.audit_github import run_gh
from coney_tools.config import ConfigError, find_repo_root, workspace_root
from coney_tools.conventions import Conventions, load_conventions
from coney_tools.issue_claim_cli import held_cards
from coney_tools.issue_claim_model import Card
from coney_tools.issue_claim_remote import GhRemote
from coney_tools.unit import Unit


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
    #: Whether `pr open` would pass the proof for a unit: (its worktree, its scratch folder's unit, the description
    #: file, the conventions, the clock) to a `Readiness`. Tests swap it for a made-up answer.
    ready: Callable[[Path, Unit, Path, Conventions, datetime], pr_ready.Readiness] = (
        lambda root, unit, text, conventions, now: pr_ready.readiness(root, unit, text, conventions, now=now)
    )


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


def iso_utc(moment: datetime) -> str:
    """A moment as ISO 8601 UTC with seconds, `2026-10-23T14:00:00Z`."""
    return moment.astimezone(UTC).strftime("%Y-%m-%dT%H:%M:%SZ")


def section(read: Callable[[], dict[str, Any]]) -> dict[str, Any]:
    """One section: what `read` returns with `error` None, or only `error` when it could not be read."""
    try:
        return {"error": None, **read()}
    except (ConfigError, OSError, ValueError, KeyError, TypeError) as error:
        return {"error": str(error) or type(error).__name__}


def strings(value: object) -> list[str]:
    """A settings value that should be a list of strings, as one; anything else gives an empty list."""
    return [item for item in value if isinstance(item, str)] if isinstance(value, list) else []
