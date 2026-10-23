# SPDX-License-Identifier: GPL-3.0-or-later
"""The legacy file of the research pages: scan it, write it once, shrink it, check it (`coney-tools docs legacy`).

`research/legacy.toml` lists the research pages that do not meet the standard yet, each under the rules it fails
(`research_legacy`). This module builds it from the pages themselves:

* `scan` runs the research rules on every page and counts the errors of each rule (for `length`, the lines over the
  limit), reading each page as a legacy one, so an old-form runtime claim stands;
* `initial_text` is the file's first text, from a scan; it refuses to overwrite a file, because afterwards the file
  only shrinks;
* `pruned` is the file with every rule a page now passes taken out, every page that passes or is gone taken out and the
  counts lowered to the scan's, and nothing added;
* `problems` finds what is wrong with the file as it is: a page that is gone, a rule a page now passes, and (against a
  base) a page or a rule added to a file that already existed there.
"""

from __future__ import annotations

import subprocess
from dataclasses import replace
from pathlib import Path
from typing import TYPE_CHECKING

from coney_tools.config import ConfigError
from coney_tools.docs_page import read_page
from coney_tools.research_check import RULES, check_research
from coney_tools.research_legacy import Legacy, format_legacy, parse_legacy
from coney_tools.research_pages import read_front_matter

if TYPE_CHECKING:
    from coney_tools.docs_check import Context


def scan_page(path: str, text: str, context: Context) -> dict[str, int]:
    """The rules the research page at `path` fails with the number of findings of each (lines over the limit for
    `length`): what its legacy entry would say. Empty for a page that passes."""
    env = context.research
    if env is None:
        raise ConfigError("the research standard is not loaded")
    page = read_page(text)
    found = check_research(path, page, replace(env, legacy=Legacy()), legacy_page=True)
    counts: dict[str, int] = {}
    for finding in found:
        if finding.severity == "error" and finding.rule in RULES:
            counts[finding.rule] = counts.get(finding.rule, 0) + 1
    if "length" in counts:
        kind = read_front_matter(page, env.research)[0].kind or env.research.kind_of_path(path)
        counts["length"] = max(page.body_lines - env.research.kinds[kind].max_lines, 1)
    return {rule: counts[rule] for rule in RULES if rule in counts}


def scan(root: Path, context: Context) -> dict[str, dict[str, int]]:
    """The scan of every research page of the checkout: path to the rules it fails, pages that pass left out."""
    env = context.research
    if env is None:
        raise ConfigError("the research standard is not loaded")
    found: dict[str, dict[str, int]] = {}
    for path in sorted(env.corpus.pages):
        counts = scan_page(path, (root / path).read_text(encoding="utf-8", errors="replace"), context)
        if counts:
            found[path] = counts
    return found


def initial_text(root: Path, context: Context) -> str:
    """The text of the legacy file as a first scan writes it. ConfigError when the file already exists."""
    env = context.research
    if env is None:
        raise ConfigError("the research standard is not loaded")
    if (root / env.research.files.legacy).exists():
        raise ConfigError(f"{env.research.files.legacy} exists: the file only shrinks, use --prune")
    return format_legacy(Legacy(scan(root, context)))


def pruned(root: Path, context: Context) -> Legacy:
    """The current legacy file with what is fixed taken out and the counts lowered; nothing is added."""
    env = context.research
    if env is None:
        raise ConfigError("the research standard is not loaded")
    now = scan(root, context)
    kept: dict[str, dict[str, int]] = {}
    for path, rules in env.legacy.pages.items():
        current = now.get(path, {})
        left = {rule: min(count, current[rule]) for rule, count in rules.items() if rule in current}
        if left:
            kept[path] = left
    return Legacy(kept)


def problems(root: Path, context: Context, base: str | None = None) -> list[str]:
    """What is wrong with the legacy file as it stands, one line each: a page that is gone, a page or a rule that now
    passes and, when `base` has a legacy file, a page or a rule added to it since."""
    env = context.research
    if env is None:
        raise ConfigError("the research standard is not loaded")
    found: list[str] = []
    file = env.research.files.legacy
    now = scan(root, context)
    for path, rules in env.legacy.pages.items():
        if not (root / path).is_file():
            found.append(f"{file}: {path} is gone: remove it")
            continue
        for rule in rules:
            if rule not in now.get(path, {}):
                found.append(f"{file}: {path} now passes '{rule}': remove it")
    before = _file_at(root, base, file) if base else None
    if before is not None:
        earlier = parse_legacy(before, RULES, f"{base}:{file}")
        for path, rules in env.legacy.pages.items():
            if path not in earlier.pages:
                found.append(f"{file}: {path} is added: a page that does not meet the standard is fixed, not listed")
            else:
                found.extend(
                    f"{file}: {path} is excused from '{rule}' that it was not before"
                    for rule in rules
                    if rule not in earlier.pages[path]
                )
    return found


def _file_at(root: Path, ref: str, file: str) -> str | None:
    """The text of `file` as commit `ref` has it, or None when it has none there."""
    done = subprocess.run(
        ["git", "-C", str(root), "show", f"{ref}:{file}"], capture_output=True, encoding="utf-8", check=False
    )
    return done.stdout if done.returncode == 0 else None


def summary_lines(scanned: dict[str, dict[str, int]]) -> list[str]:
    """How many pages a scan lists and how many fail each rule."""
    totals: dict[str, int] = {}
    for rules in scanned.values():
        for rule in rules:
            totals[rule] = totals.get(rule, 0) + 1
    lines = [f"{len(scanned)} research pages do not meet the standard"]
    lines.extend(f"  {rule}: {count} pages" for rule, count in sorted(totals.items(), key=lambda i: (-i[1], i[0])))
    return lines
