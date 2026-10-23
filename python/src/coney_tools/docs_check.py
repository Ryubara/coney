# SPDX-License-Identifier: GPL-3.0-or-later
"""The page standards of docs/guides/writing-docs.md, checked by program: `coney-tools docs check`.

A page is held to the rules of its kind (`kind_of`): a research page keeps the research standard (front matter, the
sections of its kind, a level beside every address it cites, addresses that resolve; `research_check`), and every
hand-written page keeps the Style rules that a program can see (no machine paths, no time-bound wording, `repo:` links,
roles not people, no repository or site address). The check runs on the pages a change touches and on those only, so
the standard ratchets: whoever touches a page brings the whole page up to it, and a research page that cannot meet the
standard yet is listed in the legacy file (`research_legacy`), which excuses it from the rules it fails and no more.

Everything here is a function of the page text, so the tests use made-up pages. The heuristics are deliberately
forgiving (they flag what is surely a gap, not what might be one); each is described where it is implemented and in
docs/guides/coney-tools.md#docs.
"""

from __future__ import annotations

import bisect
import posixpath
import re
from collections import Counter
from collections.abc import Callable, Iterable
from dataclasses import dataclass, field
from pathlib import Path

from coney_tools import docs_legacy, local_gate
from coney_tools.audit_files import repository_address_terms
from coney_tools.config import ConfigError
from coney_tools.conventions import load_conventions
from coney_tools.docs_page import Finding, Page, kind_of, read_page
from coney_tools.research_catalog import load_catalog
from coney_tools.research_check import RULES as RESEARCH_RULES
from coney_tools.research_check import ResearchEnv, check_research
from coney_tools.research_legacy import Touch, apply_legacy, load_legacy
from coney_tools.research_pages import Corpus

__all__ = ["Context", "Finding", "Result", "check_page", "kind_of"]

#: The kinds of file `kind_of` tells apart. Only the first three are checked.
CHECKED_KINDS: tuple[str, ...] = ("research", "guide", "page")
#: Lines above which a page gets a length warning (never a failure). A reader takes a page in whole before changing
#: it, and the Read tool shows 2,000 lines at a time; a guide past 800 (the top tenth, median 260) is several guides.
#: A warning, because splitting a page is the author's call: the cut is usually along a sub-subject that deserves its
#: own page. A research page's limit is its kind's `max_lines` in `.github/conventions.yml`.
LENGTH_WARNING: dict[str, int] = {"guide": 800}
#: Wording that says when something is true instead of saying what is true ("say what is true now").
STALE = re.compile(
    r"\bstill small\b|\bnot yet\b|\bonce (?:it|this|that|they) (?:exists?|is built|are built)\b"
    r"|\b(?:does|do)(?:n't| not) exist yet\b|\byet to be (?:written|built|done|implemented)\b",
    re.IGNORECASE,
)
#: On a guide or another page, "not yet" dates a claim when it says what the project has not done ("not yet
#: implemented"); "not yet looked at" (an issue's state) or "scenery not yet loaded" (the game) describe something else.
#: A research page tells them apart by section and by naming Coney instead (`_is_status`).
NOT_YET_WORK = re.compile(
    r"\bnot yet (?:been )?(?:implemented|written|built|done|ported|supported|listed|documented|wired|available|merged"
    r"|exposed|added|covered|tested|checked|known|understood|decided|in place)\b",
    re.IGNORECASE,
)
#: A path on somebody's machine: a user folder, or a drive-letter path that goes two folders deep. `H:\`, a file at a
#: drive's root, `C:\Program Files\...` (where a tool installs) and the original's build paths (`c:/Warriors/...`) are
#: not the writer's.
_USER_PATH = re.compile(
    r"(?i)\b[a-z]:[\\/]users[\\/](?!<)[^\\/\s<`\"']+|(?<![\w.:\\/])/(?:home|users)/(?!<)[^/\s<`\"']+"
)
_DRIVE_PATH = re.compile(r"(?<![\w])[A-Za-z]:[\\/](?P<first>[^\\/\s`'\"<>|*?:]+)[\\/]")
_DRIVE_OK = frozenset({"program", "windows", "users"})
#: The original's own build paths, which its executable carries as debug strings and the research pages quote.
ORIGINAL_BUILD_PATHS: tuple[str, ...] = ("c:/warriors", "d:/warriors", "/home/ack")
#: Text that mentions a phrase instead of using it: inline code and a short quotation.
_MENTION = re.compile(r"`[^`\n]*`|\"[^\"\n]{1,60}\"|\u201c[^\u201d\n]{1,60}\u201d")
_CODE = re.compile(r"`[^`\n]*`")
_LINK = re.compile(r"!?\[[^\]\n]*\]\(<?(?P<target>[^)\s>]+)")
_SCHEME = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")
_REF_DEF = re.compile(r"^[ \t]{0,3}\[[^\]\n]+\]:[ \t]*<?(?P<target>[^\s>]+)")
_HUNK = re.compile(r"^@@ -\d+(?:,\d+)? \+(?P<start>\d+)(?:,(?P<count>\d+))? @@", re.MULTILINE)
#: The sections of a research page that say what Coney has, where "not yet" dates a claim.
_STATUS_SECTIONS = frozenset({"in coney", "coney's implementation", "open questions"})


@dataclass(frozen=True)
class Context:
    """What the rules take from the repository: the words that must not name a person (`repo.wording.avoid`), the
    lower-case strings that spell the repository's and the docs site's address and, for research pages, everything the
    research standard checks against (`ResearchEnv`; without it a research page is held to the Style rules only)."""

    avoid: tuple[str, ...] = ()
    address_terms: tuple[str, ...] = ()
    research: ResearchEnv | None = None


@dataclass
class Result:
    """The outcome of checking a set of pages: the pages in order, and the findings of those that have any."""

    pages: list[str] = field(default_factory=list)
    findings: dict[str, list[Finding]] = field(default_factory=dict)
    #: How many of the pages are listed in the legacy file.
    legacy: int = 0

    @property
    def failed(self) -> dict[str, list[Finding]]:
        """The pages with at least one error, with all their findings (warnings too)."""
        return {page: found for page, found in self.findings.items() if any(item.severity == "error" for item in found)}

    def by_kind(self) -> dict[str, tuple[int, int]]:
        """For each kind of page checked: how many pass (no error) and how many there are."""
        failed = self.failed
        totals: dict[str, list[int]] = {}
        for page in self.pages:
            if kind_of(page) not in CHECKED_KINDS:
                continue
            counts = totals.setdefault(kind_of(page), [0, 0])
            counts[1] += 1
            counts[0] += page not in failed
        return {kind: (counts[0], counts[1]) for kind, counts in totals.items()}

    def rule_counts(self) -> Counter[str]:
        """For each rule, the number of pages with an error of that rule."""
        counts: Counter[str] = Counter()
        for found in self.findings.values():
            counts.update({item.rule for item in found if item.severity == "error"})
        return counts


# --- style ------------------------------------------------------------------------------------------------------------


def _without_mentions(text: str) -> str:
    """`text` without inline code, short quotations and URLs: a phrase there is mentioned, not used."""
    return re.sub(r"https?://\S+", " ", _MENTION.sub(" ", text))


def _machine_path(line: int, path: str) -> Iterable[Finding]:
    """A finding for `path` unless it is one of the original's build paths (`ORIGINAL_BUILD_PATHS`)."""
    if not path.casefold().replace(chr(92), "/").startswith(ORIGINAL_BUILD_PATHS):
        yield Finding(
            "machine-path", line, f"'{path}' is a path on a machine: write <workspace>, scratch/ or a relative path"
        )


def _line_sections(page: Page) -> list[str]:
    """For each line, the lower-case name of the level-2 section it sits in ('' above the first one)."""
    names: list[str] = []
    current = ""
    headings = {heading.line: heading for heading in page.headings}
    for line in page.lines:
        heading = headings.get(line.number)
        if heading is not None and heading.level <= 2:
            current = heading.text.replace(chr(0x2019), "'").casefold().strip("`*_ ") if heading.level == 2 else ""
        names.append(current)
    return names


def _is_status(section: str, text: str) -> bool:
    """Whether a line of a research page says what Coney has (so "not yet" dates it) and not what the original does
    ("a store that is not yet broken"): it sits in Coney's implementation or the open questions, or names Coney."""
    return section in _STATUS_SECTIONS or "coney" in text.casefold()


def _person_words(words: tuple[str, ...]) -> re.Pattern[str]:
    """The pattern for the avoided words in their person sense: "the owner", "the project owner", "repository owner".
    "An object's owner" and `owner/repo` are not people and stay."""
    alternatives = "|".join(re.escape(word) for word in words)
    return re.compile(
        rf"\b(?:the|project|repository|repo)\s+(?:{alternatives})(?:'s|s)?\b(?![/_])(?!\s+of\b)", re.IGNORECASE
    )


def _check_style(path: str, page: Page, kind: str, context: Context) -> Iterable[Finding]:
    """The Style rules a program can see, line by line."""
    folder = posixpath.dirname(path)
    sections = _line_sections(page)
    for index, line in enumerate(page.lines):
        text = line.text
        if not text.strip():
            continue
        lowered = text.casefold()
        for term in context.address_terms:
            if term in lowered:
                yield Finding(
                    "address", line.number, "names the repository's or the docs site's address; only the README may"
                )
                break
        for found in _USER_PATH.finditer(text):
            yield from _machine_path(line.number, found.group(0))
        for found in _DRIVE_PATH.finditer(text):
            if found.group("first").casefold() not in _DRIVE_OK:
                yield from _machine_path(line.number, found.group(0))
        if line.fenced:
            continue
        mentioned = _without_mentions(text)
        for stale in STALE.finditer(mentioned):
            if stale.group(0).casefold() == "not yet":
                if kind == "research" and not _is_status(sections[index], text):
                    continue
                if kind != "research" and not NOT_YET_WORK.match(mentioned, stale.start()):
                    continue
            yield Finding(
                "stale-wording",
                line.number,
                f"'{stale.group(0)}' says when it is true: say what is true now, or 'planned' in the page that owns it",
            )
        if kind != "research" and context.avoid:
            for person in _person_words(context.avoid).finditer(mentioned):
                yield Finding(
                    "person-wording",
                    line.number,
                    f"'{person.group(0)}' names a person: say 'the user' or 'a maintainer'",
                )
        for target in [*_LINK.finditer(_CODE.sub(" ", text)), *_REF_DEF.finditer(text)]:
            link = target.group("target")
            if _SCHEME.match(link) or link.startswith(("#", "/")):
                continue
            resolved = posixpath.normpath(posixpath.join(folder, link.split("#", 1)[0].split("?", 1)[0]))
            if link.split("#", 1)[0] and resolved != "docs" and not resolved.startswith("docs/"):
                yield Finding("repo-link", line.number, f"'{link}' leaves docs/: write repo:<path>")


def _check_length(kind: str, text: str) -> Iterable[Finding]:
    """A warning when a guide is longer than `LENGTH_WARNING` allows (a research page's limit is its kind's)."""
    limit = LENGTH_WARNING.get(kind)
    count = len(text.splitlines())
    if limit and count > limit:
        yield Finding(
            "length",
            1,
            f"{count} lines is over the {limit} of a {kind} page: split off the sub-subject that has its own page",
            "warning",
        )


# --- the research standard and the legacy file -----------------------------------------------------------------------


def _touched(page: Page, lines: frozenset[int] | None) -> Callable[[Finding], bool]:
    """A test for "this finding is in a section the change touched": the sections are the level-2 headings (and the
    title's part above the first); nothing is touched when the changed lines are not known."""
    if not lines:
        return lambda _finding: False
    starts = [heading.line for heading in page.headings if heading.level <= 2] or [1]
    changed = {max(bisect.bisect_right(starts, number) - 1, 0) for number in lines}
    return lambda finding: max(bisect.bisect_right(starts, finding.line) - 1, 0) in changed


def _check_research(path: str, page: Page, env: ResearchEnv, touch: Touch) -> list[Finding]:
    """The research standard for `path`, with the legacy file's excuses applied."""
    listed = path in env.legacy
    findings = check_research(path, page, env, legacy_page=listed)
    if not listed:
        return findings
    if touch.new:
        findings.append(
            Finding(
                "legacy",
                1,
                f"a new page cannot be legacy: meet the standard and take it out of {env.research.files.legacy}",
            )
        )
        return findings
    return apply_legacy(findings, env.legacy.rules(path), env.research.files.legacy, _touched(page, touch.lines))


# --- checking ---------------------------------------------------------------------------------------------------------


def check_page(path: str, text: str, context: Context | None = None, touch: Touch | None = None) -> list[Finding]:
    """Every finding for the page at `path` (relative to the repository, forward slashes) with content `text`, by line.
    A file that is not a checked page (`kind_of`) has none. `touch` says what the change did to the page, which decides
    how strictly a legacy page is read."""
    kind = kind_of(path)
    if kind not in CHECKED_KINDS:
        return []
    context = context or Context()
    page = read_page(text)
    found: list[Finding] = []
    if kind == "research" and context.research is not None:
        found.extend(_check_research(path, page, context.research, touch or Touch()))
    found.extend(_check_style(path, page, kind, context))
    if kind != "research":
        found.extend(_check_length(kind, text))
    return sorted(found, key=lambda item: item.line)


def context_for(root: Path) -> Context:
    """The `Context` of the checkout at `root`: the wording to avoid, the address of the repository and the research
    standard with the data it checks against."""
    conventions = load_conventions(root)
    research = conventions.research
    env = ResearchEnv(
        research,
        load_catalog(root, research),
        Corpus.from_root(root, research),
        load_legacy(root, research, RESEARCH_RULES),
    )
    return Context(conventions.repo.wording.avoid, tuple(repository_address_terms(root)), env)


def check_files(root: Path, names: Iterable[str], context: Context, touches: dict[str, Touch] | None = None) -> Result:
    """Check each of `names` (repository-relative, forward slashes) that is a checked page and exists."""
    result = Result()
    for name in names:
        if kind_of(name) not in CHECKED_KINDS or not (root / name).is_file():
            continue
        result.pages.append(name)
        if context.research is not None and name in context.research.legacy:
            result.legacy += 1
        text = (root / name).read_text(encoding="utf-8", errors="replace")
        found = check_page(name, text, context, (touches or {}).get(name))
        if found:
            result.findings[name] = found
    return result


def change_touches(root: Path, base: str, names: Iterable[str]) -> dict[str, Touch]:
    """For each research page among `names`, what the change since HEAD left `base` did to it: whether it is new, and
    which lines it added or changed (committed or not)."""
    since = local_gate.merge_base(root, base)
    touches: dict[str, Touch] = {}
    for name in names:
        if kind_of(name) != "research":
            continue
        code, _ = local_gate.run_command(["git", "-C", str(root), "cat-file", "-e", f"{since}:{name}"], root)
        if code != 0:
            touches[name] = Touch(None, True)
            continue
        diff = local_gate.run_command(["git", "-C", str(root), "diff", "-U0", since, "--", name], root)[1]
        lines: set[int] = set()
        for hunk in _HUNK.finditer(diff):
            start, count = int(hunk.group("start")), int(hunk.group("count") or 1)
            lines.update(range(start, start + max(count, 1)))
        touches[name] = Touch(frozenset(lines))
    return touches


def check_changed(root: Path, base: str, context: Context) -> Result:
    """Check the pages changed since the branch left `base`, committed or not, untracked pages included."""
    names = local_gate.changed_files(root, base)
    result = check_files(root, names, context, change_touches(root, base, names))
    legacy_file = context.research.research.files.legacy if context.research else ""
    if legacy_file in names:
        # The legacy file itself changed: it may only have shrunk and may list nothing that now passes.
        found = [Finding("legacy", 1, line) for line in docs_legacy.problems(root, context, base)]
        if found:
            result.pages.append(legacy_file)
            result.findings[legacy_file] = found
    return result


def check_paths(root: Path, paths: Iterable[str], context: Context) -> Result:
    """Check the named files, and for a folder every Markdown page under it, whatever has changed."""
    names: list[str] = []
    for path in paths:
        target = Path(path)
        target = (target if target.is_absolute() else root / target).resolve()
        top = root.resolve()
        if top != target and top not in target.parents:
            raise ConfigError(f"{path} is outside the checkout")
        if target.is_dir():
            names.extend(sorted(item.relative_to(top).as_posix() for item in target.rglob("*.md")))
        elif not target.is_file():
            raise ConfigError(f"{path}: no such file or folder")
        elif kind_of(name := target.relative_to(top).as_posix()) not in CHECKED_KINDS:
            raise ConfigError(f"{path}: not a page the check covers (a hand-written page under docs/)")
        else:
            names.append(name)
    return check_files(root, names, context)
