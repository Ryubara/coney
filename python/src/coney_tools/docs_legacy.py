# SPDX-License-Identifier: GPL-3.0-or-later
"""The legacy file of the research pages: scan it, write it once, shrink it, check it (`coney-tools docs legacy`).

`research/legacy.toml` lists the research pages that do not meet the standard yet, each under the rules it fails
(`research_legacy`). This module builds it from the pages themselves:

* `scan` runs the research rules and the stale-wording rule on every page and counts the errors of each rule (for
  `length`, the lines over the limit), reading each page as a legacy one, so an old-form runtime claim stands;
* `initial_text` is the file's first text, from a scan; it refuses to overwrite a file, because afterwards the file
  only shrinks;
* `pruned` is the file with every rule a page now passes taken out, every page that passes or is gone taken out and the
  counts lowered to the scan's, and nothing added, except that (against a base) the entry of a page git sees as moved
  follows it to its new path, keeping the grading (`evidence`) alone;
* `problems` finds what is wrong with the file as it is: a page that is gone (or moved, whose entry has not followed), a
  rule a page now passes, and (against a base) a page or a rule added to a file that already existed where the branch
  left it. The one page that may be added is the new path of a moved page, under `evidence` and only if the old entry
  had it: a moved page keeps its grading exemption and nothing else (docs/guides/writing-docs.md#legacy-pages).
"""

from __future__ import annotations

import subprocess
from collections.abc import Mapping
from dataclasses import replace
from pathlib import Path
from typing import TYPE_CHECKING

from coney_tools import local_gate
from coney_tools.config import ConfigError
from coney_tools.docs_page import read_page
from coney_tools.docs_style import check_style
from coney_tools.research_check import RULES, check_research
from coney_tools.research_legacy import Legacy, format_legacy, parse_legacy
from coney_tools.research_pages import read_front_matter

if TYPE_CHECKING:
    from coney_tools.docs_check import Context

#: The rules a legacy entry may excuse: the research rules, and the one Style rule the research pages carry from
#: before the standard (a research page says what is true now, `docs_style`).
LEGACY_RULES: tuple[str, ...] = (*RULES, "stale-wording")


def scan_page(path: str, text: str, context: Context) -> dict[str, int]:
    """The rules the research page at `path` fails with the number of findings of each (lines over the limit for
    `length`): what its legacy entry would say. Empty for a page that passes."""
    env = context.research
    if env is None:
        raise ConfigError("the research standard is not loaded")
    page = read_page(text)
    found = check_research(path, page, replace(env, legacy=Legacy()), legacy_page=True)
    found.extend(check_style(path, page, "research", context.avoid, context.address_terms))
    counts: dict[str, int] = {}
    for finding in found:
        if (finding.severity == "error" or finding.held) and finding.rule in LEGACY_RULES:
            counts[finding.rule] = counts.get(finding.rule, 0) + 1
    if "length" in counts:
        kind = read_front_matter(page, env.research)[0].kind or env.research.kind_of_path(path)
        counts["length"] = max(page.body_lines - env.research.kinds[kind].max_lines, 1)
    return {rule: counts[rule] for rule in LEGACY_RULES if rule in counts}


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


def moved_pages(root: Path, context: Context, base: str | None) -> dict[str, str]:
    """The pages git sees as moved since the branch left `base` (a rename at or above the conventions' similarity),
    as new path to old path. Empty without a base."""
    env = context.research
    if env is None:
        raise ConfigError("the research standard is not loaded")
    if base is None:
        return {}
    return local_gate.renamed_files(root, base, env.research.moved_similarity)


def pruned(root: Path, context: Context, base: str | None = None) -> Legacy:
    """The current legacy file with what is fixed taken out and the counts lowered; nothing is added. Against a `base`,
    the entry of a page git sees as moved follows it to the new path and keeps only the grading, `evidence`."""
    env = context.research
    if env is None:
        raise ConfigError("the research standard is not loaded")
    now = scan(root, context)
    moved = moved_pages(root, context, base)
    moved_to = {old: new for new, old in moved.items()}
    kept: dict[str, dict[str, int]] = {}
    for path, rules in env.legacy.pages.items():
        if path in moved_to and not (root / path).is_file():
            path, rules = moved_to[path], {rule: count for rule, count in rules.items() if rule == "evidence"}
        elif path in moved:
            rules = {rule: count for rule, count in rules.items() if rule == "evidence"}
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
    moved = moved_pages(root, context, base)
    moved_to = {old: new for new, old in moved.items()}
    for path, rules in env.legacy.pages.items():
        if not (root / path).is_file():
            if path in moved_to and moved_to[path] in env.legacy.pages:
                found.append(f"{file}: {path} moved to {moved_to[path]}, which is listed: remove the old entry")
            elif path in moved_to:
                found.append(
                    f"{file}: {path} moved to {moved_to[path]}: its entry follows, grading only "
                    "(`coney-tools docs legacy --prune`)"
                )
            else:
                found.append(f"{file}: {path} is gone: remove it")
            continue
        for rule in rules:
            if rule not in now.get(path, {}):
                found.append(f"{file}: {path} now passes '{rule}': remove it")
    # The file where the branch left `base`: what main has pruned since is not the branch's to match.
    since = local_gate.merge_base(root, base) if base else None
    before = _file_at(root, since, file) if since else None
    if before is not None:
        earlier = parse_legacy(before, LEGACY_RULES, f"{base}:{file}")
        for path, rules in env.legacy.pages.items():
            if path not in earlier.pages:
                found.extend(_added(file, path, rules, earlier, moved))
            else:
                found.extend(
                    f"{file}: {path} is excused from '{rule}' that it was not before"
                    for rule in rules
                    if rule not in earlier.pages[path]
                )
    return found


def _added(file: str, path: str, rules: Mapping[str, int], earlier: Legacy, moved: Mapping[str, str]) -> list[str]:
    """What is wrong with an entry for `path`, which the file did not list where the branch left `base`: all of it,
    unless `path` is the new path of a listed page that moved, which may be listed under `evidence` if the old entry
    was. Its count is not compared with the old entry's: counts say how much is left, and are never compared with a
    page."""
    origin = earlier.pages.get(moved.get(path, ""))
    if origin is None:
        return [f"{file}: {path} is added: a page that does not meet the standard is fixed, not listed"]
    found = [
        f"{file}: {path} is excused from '{rule}': a moved page keeps only its grading exemption ('evidence')"
        for rule in rules
        if rule != "evidence"
    ]
    if "evidence" in rules and "evidence" not in origin:
        found.append(f"{file}: {path} is excused from 'evidence' that it was not before")
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
