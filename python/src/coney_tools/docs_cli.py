# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools docs` commands: `check`, the page standards of docs/guides/writing-docs.md (see `docs_check`);
`render`, the parameter reference of the coney-tools guides (see `command_reference`); and `legacy`."""

from __future__ import annotations

from pathlib import Path

from coney_tools import command_reference, docs_check, docs_legacy
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.research_legacy import format_legacy

#: The legacy file is written with LF whatever the system, as the repository's text files are.
NEWLINE = chr(10)


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
    if result.legacy:
        lines.append(f"{result.legacy} research pages are legacy: excused from the rules their entry lists")
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


def run_render(check: bool) -> int:
    """Write the generated parameter blocks of the coney-tools guides from the command line, or with `check` only report
    the stale ones. Exit 1 when a block is stale or missing, 2 when the checkout cannot be found."""
    try:
        return command_reference.run_render(find_repo_root(Path.cwd()), command_reference.command_groups(), check)
    except ConfigError as error:
        print(f"coney-tools docs render: {error}")
        return 2


def run_legacy(mode: str, base: str) -> int:
    """The legacy file of the research pages: `scan` prints what a scan finds, `init` writes the file once, `prune`
    takes out what is fixed, `check` exits 1 when the file lists a page or rule that now passes (or, against `base`,
    grew). Exit 2 when the checkout or the file cannot be used."""
    try:
        root = find_repo_root(Path.cwd())
        context = docs_check.context_for(root)
        env = context.research
        if env is None:
            raise ConfigError("the research standard is not loaded")
        path = root / env.research.files.legacy
        path.parent.mkdir(parents=True, exist_ok=True)
        if mode == "init":
            path.write_text(docs_legacy.initial_text(root, context), encoding="utf-8", newline=NEWLINE)
            print(f"wrote {env.research.files.legacy}")
            return 0
        if mode == "prune":
            left = docs_legacy.pruned(root, context)
            before = len(env.legacy.pages)
            path.write_text(format_legacy(left), encoding="utf-8", newline=NEWLINE)
            print(f"{env.research.files.legacy}: {before} pages listed before, {len(left.pages)} now")
            return 0
        if mode == "check":
            found = docs_legacy.problems(root, context, base)
            for line in found:
                print(line)
            print(f"docs legacy: {len(env.legacy.pages)} pages listed, {len(found)} problems")
            return 1 if found else 0
        for line in docs_legacy.summary_lines(docs_legacy.scan(root, context)):
            print(line)
        return 0
    except ConfigError as error:
        print(f"coney-tools docs legacy: {error}")
        return 2
