# SPDX-License-Identifier: GPL-3.0-or-later
"""The engine page kind: `docs/engine/`, how Coney works (docs/guides/writing-docs.md#engine-pages).

An engine page describes Coney's own internals and port notes, never what the original does (that is a research page).
It is a guide in how it reads and what the Style rules refuse, and holds the same length warning, but it keeps three
rules a guide does not, because it says where Coney's code stands against the original:

* `history`: no date (in code too; only a fenced block and the inline target of a link are not read), and an issue or
  pull request number only as the whole text of a link whose target is `issue:<same number>`, or inside a code span (an
  issue tracks a stand-in that is to be replaced, so the reader can follow it, but the page does not narrate it);
* `address-form`, `address-resolve`, `address-name`: an address of `SLUS_212.15` that a page cites is written in full,
  resolves in the executable and is named as Ghidra names it (the research rule, `research_check.check_addresses`);
* `index`: the folder has a landing page, `docs/engine/index.md`.

There is no front matter, no fixed list of sections and no evidence level: nothing here claims what the original does.
"""

from __future__ import annotations

import re
from collections.abc import Iterable

from coney_tools.docs_page import Finding, Page
from coney_tools.research_check import check_addresses
from coney_tools.research_pages import FrontMatter
from coney_tools.research_subject import CODE_SPAN, DATE, ISSUE, LINK_TARGET, ResearchEnv, Subject

#: The folder of the engine pages and the landing page every page of it needs.
FOLDER = "docs/engine/"
INDEX = "docs/engine/index.md"

#: The whole text of a link that names an issue or pull request, with its number: `#123`, `issue 123`, `issue #123`,
#: `PR 45`, `PR #45`, `pull request #45`.
_ISSUE_TEXT = re.compile(r"\s*(?:#(\d+)|(?:issue|pull request|PR)s?\s+#?(\d+))\s*", re.IGNORECASE)
#: A Markdown link: its text, then an inline target `(...)` or a reference label `[...]` (`[]` means the text itself); a
#: link with neither is a shortcut reference.
_LINK = re.compile(r"\[(?P<text>[^\]\n]*)\](?:\((?P<target>[^)\n]*)\)|\[(?P<label>[^\]\n]*)\])?")
#: The one target the docs build resolves to an issue's page (`mkdocs_hooks.py`): `issue:` and digits, nothing else.
_ISSUE_TARGET = re.compile(r"issue:(\d+)")
#: A link reference definition that is whole-line `[label]: issue:<number>`; with anything after the target it is no
#: definition, and the build leaves it alone.
_ISSUE_DEFINITION = re.compile(r"^ {0,3}\[(?P<label>[^\]\n]+)\]:[ \t]*issue:(?P<number>\d+)[ \t]*$")
#: Any link reference definition, whatever its target: it claims its label, so a later definition of it is ignored.
_DEFINITION = re.compile(r"^ {0,3}\[(?P<label>[^\]\n]+)\]:[ \t]*\S")
#: `#<n>` right after one of these words numbers a thing of Coney's own ("player #1", "pad #0"), not an issue. This list
#: is the whole rule: after any other word, `#<n>` is an issue reference.
_COUNTED = re.compile(
    r"\b(?:player|pad|gamepad|slot|port|controller|button|channel|lane|axis|stick|trigger)s?\s+#\d+\b", re.IGNORECASE
)


def check_engine(path: str, page: Page, env: ResearchEnv | None) -> list[Finding]:
    """The engine rules for the page at `path`, read as `page`. Without `env` (no executable to resolve against) only
    the history rule runs."""
    found = list(_check_history(page))
    if env is not None:
        # The addresses rule reads the page as a research subject; an engine page has no front matter or sections.
        subject = Subject(path, page, "engine", FrontMatter(), env, False, ["" for _ in page.headings])
        found.extend(check_addresses(subject))
    return found


def index_finding() -> Finding:
    """The finding for an engine folder that has no landing page."""
    return Finding("index", 1, f"{INDEX} is missing: the engine section needs its landing page, which links the others")


def _label(text: str) -> str:
    """A link label as Markdown matches it: case and runs of white space do not count."""
    return " ".join(text.casefold().split())


def _issue_definitions(page: Page) -> dict[str, str | None]:
    """The page's link reference definitions by label: the number when the first definition of the label is
    `issue:<number>`, else None. Markdown takes the first definition of a label, so a later one changes nothing."""
    found: dict[str, str | None] = {}
    for line in page.lines:
        if line.fenced:
            continue
        match = _ISSUE_DEFINITION.match(line.text)
        if match:
            found.setdefault(_label(match.group("label")), match.group("number"))
            continue
        other = _DEFINITION.match(line.text)
        if other:
            found.setdefault(_label(other.group("label")), None)
    return found


def _check_history(page: Page) -> Iterable[Finding]:
    """No date outside a fenced block, and no issue or pull request number outside code that is not the whole text of a
    link to `issue:` and the same number (inline, or by a reference whose definition says so)."""
    definitions = _issue_definitions(page)

    def named_issue(link: re.Match[str]) -> str:
        """The link as a blank when its text names an issue that its target is the page of, else as it is."""
        text = _ISSUE_TEXT.fullmatch(link.group("text"))
        if not text:
            return link.group(0)
        number = text.group(1) or text.group(2)
        target = link.group("target")
        if target is not None:
            target_number = _ISSUE_TARGET.fullmatch(target)
            same = target_number is not None and target_number.group(1) == number
        else:
            # `[text][label]`, `[text][]` and `[text]` all read the definition of their label.
            label = link.group("label") or link.group("text")
            same = definitions.get(_label(label)) == number
        return " " if same else link.group(0)

    for line in page.lines:
        if line.fenced or not line.text.strip() or _ISSUE_DEFINITION.match(line.text):
            continue
        plain = LINK_TARGET.sub("]", _LINK.sub(named_issue, line.text))
        date = DATE.search(plain)
        if date:
            yield Finding(
                "history",
                line.number,
                f"'{date.group(0)}' dates the page: say what is true (git keeps the history; a date in code counts, "
                "a fenced block and a link's inline target do not)",
            )
            continue
        issue = ISSUE.search(CODE_SPAN.sub(" ", _COUNTED.sub(" ", plain)))
        if issue:
            yield Finding(
                "history",
                line.number,
                f"'{issue.group(0)}' points at an issue other than as the whole text of a link to issue:<same number> "
                "(or a reference whose definition is one)",
            )
