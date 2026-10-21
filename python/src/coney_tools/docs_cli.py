# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools docs check` command: the page standards of docs/guides/writing-docs.md (see `docs_check`)."""

from __future__ import annotations

from pathlib import Path

from coney_tools import docs_check
from coney_tools.config import ConfigError, find_repo_root


def page_lines(result: docs_check.Result, page: str) -> list[str]:
    """One `path:line: rule: message` line per finding of `page`, errors and warnings alike."""
    lines: list[str] = []
    for finding in result.findings.get(page, []):
        label = "warning: " if finding.severity == "warning" else ""
        lines.append(f"{page}:{finding.line}: {label}{finding.rule}: {finding.message}")
    return lines


def report_lines(result: docs_check.Result) -> list[str]:
    """The lines of every page, in the order the pages were checked."""
    return [line for page in result.pages for line in page_lines(result, page)]


def summary_lines(result: docs_check.Result) -> list[str]:
    """How many pages pass, per kind, and how many pages each rule fails."""
    lines = [f"{kind}: {passed} of {total} pages pass" for kind, (passed, total) in sorted(result.by_kind().items())]
    counts = result.rule_counts()
    lines.extend(f"  {rule}: {count} pages fail" for rule, count in sorted(counts.items(), key=lambda i: (-i[1], i[0])))
    return lines


def run_check(paths: list[str], base: str, summary: bool, quiet: bool) -> int:
    """Check the named pages (a folder means its pages), or with none the pages changed since `base`; print the
    findings and, with `summary`, the counts. Exit 1 when any page has an error, 2 when the arguments are wrong."""
    try:
        root = find_repo_root(Path.cwd())
        context = docs_check.context_for(root)
        if paths:
            result = docs_check.check_paths(root, paths, context)
            scope = "named"
        else:
            result = docs_check.check_changed(root, base, context)
            scope = f"changed since {base}"
    except ConfigError as error:
        print(f"coney-tools docs check: {error}")
        return 2
    if not quiet:
        for line in report_lines(result):
            print(line)
    if summary:
        for line in summary_lines(result):
            print(line)
    failed = result.failed
    print(f"docs check: {len(result.pages)} pages {scope}, {len(failed)} fail")
    if failed and not quiet:
        print("Bring each failing page wholly up to docs/guides/writing-docs.md (a touched page is held to all of it).")
    return 1 if failed else 0
