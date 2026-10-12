# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools repo audit`: report where the repository departs from `.github/conventions.yml`, changing nothing.

Three scopes read different things. `files` reads only the checkout (CI runs it on every pull request), `github` reads
issues, pull requests, labels and settings through `gh` (read only), and `local` reads the workspace. Each check is a
function registered under an id (`@check("issue.labels")`) in `audit_files`, `audit_github` or `audit_local`; its scope,
severity and fix class come from `hygiene.checks` of the conventions file, so the file is the registry and a test
refuses a check that has an entry without code or code without an entry. A check yields problems (an item and a
message); this module turns them into findings, orders them and prints them as text or JSON.
"""

from __future__ import annotations

import json
from collections import Counter
from collections.abc import Callable, Iterable, Iterator, Sequence
from dataclasses import dataclass, field
from typing import NamedTuple

from coney_tools.config import ConfigError
from coney_tools.conventions import CheckSpec, Conventions

#: The severities, worst first.
SEVERITIES = ("error", "warn")
#: The scopes, in the order they run and print.
SCOPES = ("files", "github", "local")


class Problem(NamedTuple):
    """What a check found about one item, before the registry gives it a severity and a fix class."""

    #: What is wrong with: `#201`, `AGENTS.md`, `worktree conventions-3`, `label S-blocked`.
    item: str
    message: str
    #: A link to the item when it has one (an issue or pull request page); may be empty.
    url: str = ""


@dataclass(frozen=True)
class Finding:
    """One thing the audit found: which check, how bad, about what, and who fixes it."""

    check: str
    severity: str
    item: str
    message: str
    #: `auto`, `skill` or `owner` (see `CheckSpec.fix`).
    fix: str
    #: The conventions keys the check enforces.
    keys: tuple[str, ...]
    url: str = ""

    def as_json(self) -> dict[str, object]:
        """The finding as the JSON object the hygiene Action and the /repo-hygiene skill read."""
        return {
            "check": self.check,
            "keys": list(self.keys),
            "severity": self.severity,
            "item": self.item,
            "url": self.url,
            "message": self.message,
            "fix": self.fix,
        }


@dataclass
class Report:
    """The result of one audit run."""

    #: The scopes that ran.
    scopes: tuple[str, ...]
    #: The ids of the checks that ran, in order.
    ran: list[str] = field(default_factory=list)
    findings: list[Finding] = field(default_factory=list)
    #: Things the audit could not look at (no network, no origin/main): not findings, but never silent.
    notices: list[str] = field(default_factory=list)

    def count(self, severity: str | None = None) -> int:
        """How many findings there are, or how many of `severity`."""
        return sum(1 for finding in self.findings if severity is None or finding.severity == severity)

    def per_check(self) -> dict[str, int]:
        """Findings per check id, for every check that ran (0 for a clean one), in run order."""
        counts = Counter(finding.check for finding in self.findings)
        return {check: counts.get(check, 0) for check in self.ran}

    def failed(self, strict: bool = False) -> bool:
        """Whether the audit fails: any error finding, or with `strict` any finding."""
        return self.count("error") > 0 or (strict and self.count() > 0)


# --- the registry of checks ---------------------------------------------------------------------------------------

#: A check takes its scope's context and yields what it found.
CheckFunction = Callable[..., Iterable[Problem]]

_CHECKS: dict[str, CheckFunction] = {}


def check(check_id: str) -> Callable[[CheckFunction], CheckFunction]:
    """Register the decorated function as the code of check `check_id` (a key of `hygiene.checks`)."""

    def register(function: CheckFunction) -> CheckFunction:
        if check_id in _CHECKS:
            raise ValueError(f"check {check_id} is registered twice")
        _CHECKS[check_id] = function
        return function

    return register


def registered() -> dict[str, CheckFunction]:
    """The code of every check, by id (the check modules must have been imported)."""
    return dict(_CHECKS)


def specs_in(conventions: Conventions, scopes: Sequence[str], only: Sequence[str] = ()) -> list[CheckSpec]:
    """The registry entries of the given scopes, in file order; `only` keeps ids equal to, or under, one of its names.

    Raises ConfigError when a name in `only` matches no check, so a typo does not look like a clean run.
    """
    chosen = [spec for spec in conventions.hygiene.checks.values() if spec.scope in scopes]
    if only:
        everything = list(conventions.hygiene.checks)
        for name in only:
            if not any(known == name or known.startswith(name.rstrip(".") + ".") for known in everything):
                raise ConfigError(f"--only {name}: no such check (known: {', '.join(everything)})")
        chosen = [
            spec
            for spec in chosen
            if any(spec.id == name or spec.id.startswith(name.rstrip(".") + ".") for name in only)
        ]
    return chosen


def run_checks(specs: Iterable[CheckSpec], contexts: dict[str, object], report: Report) -> None:
    """Run the check of each spec on the context of its scope and add what it finds to `report`.

    `contexts` maps a scope to the value its checks take; a scope without one is skipped (its notice is the caller's).
    """
    functions = registered()
    for spec in specs:
        context = contexts.get(spec.scope)
        if context is None:
            continue
        function = functions.get(spec.id)
        if function is None:
            raise ConfigError(f"hygiene.checks.{spec.id}: no code for this check in coney-tools")
        report.ran.append(spec.id)
        for problem in function(context):
            report.findings.append(
                Finding(spec.id, spec.severity, problem.item, problem.message, spec.fix, spec.keys, problem.url)
            )


def sort_findings(findings: list[Finding], order: Sequence[str]) -> list[Finding]:
    """The findings by check (in `order`), then item number or name, so a run reads the same every time."""
    position = {name: index for index, name in enumerate(order)}

    def key(finding: Finding) -> tuple[int, int, str]:
        digits = "".join(character for character in finding.item.lstrip("#") if character.isdigit())
        number = int(digits) if finding.item.startswith("#") and digits else 0
        return position.get(finding.check, len(position)), number, finding.item

    return sorted(findings, key=key)


# --- printing -----------------------------------------------------------------------------------------------------


def format_text(report: Report, show: int | None = None) -> Iterator[str]:
    """The report as lines: the notices, the findings by check, then the counts per check and the total.

    `show` limits how many findings of one check are printed (the counts always cover all of them).
    """
    for notice in report.notices:
        yield f"note: {notice}"
    by_check: dict[str, list[Finding]] = {}
    for finding in report.findings:
        by_check.setdefault(finding.check, []).append(finding)
    for check_id, found in by_check.items():
        shown = found if show is None else found[:show]
        yield f"{check_id} ({found[0].severity}, fix: {found[0].fix}): {len(found)}"
        for finding in shown:
            yield f"  {finding.item}: {finding.message}"
        if len(shown) < len(found):
            yield f"  ... {len(found) - len(shown)} more (use --json or --show 0 for all)"
    counts = report.per_check()
    clean = [name for name, count in counts.items() if count == 0]
    yield (
        f"audit {'+'.join(report.scopes)}: {len(report.ran)} checks, {report.count('error')} errors, "
        f"{report.count('warn')} warnings"
    )
    if clean and report.findings:
        yield f"clean: {', '.join(clean)}"


def to_json(report: Report) -> str:
    """The report as JSON: findings, the count per check and the notices."""
    data = {
        "scopes": list(report.scopes),
        "errors": report.count("error"),
        "warnings": report.count("warn"),
        "checks": report.per_check(),
        "notices": report.notices,
        "findings": [finding.as_json() for finding in report.findings],
    }
    return json.dumps(data, indent=2, ensure_ascii=False)
