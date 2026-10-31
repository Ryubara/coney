# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared by the tests of the research page standard: a made-up executable, made-up pages and the checks run on them.

The fixtures hold no game data: the functions, sections and addresses are invented, and so are the pages.
"""

from __future__ import annotations

import functools
from collections.abc import Mapping
from pathlib import Path

from conventions_support import REPO, real_data
from git_support import git

from coney_tools import docs_check
from coney_tools.conventions import Research, TouchScope, parse_conventions
from coney_tools.docs_check import Context, Finding, check_page
from coney_tools.progress_research import ListedFunction
from coney_tools.research_catalog import Catalog, Section
from coney_tools.research_legacy import Legacy, Touch
from coney_tools.research_pages import Corpus
from coney_tools.research_subject import ResearchEnv

RESEARCH_PATH = "docs/research/widget.md"
VERIFIED = "Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records.\n"
#: The functions of the made-up executable: one named, one not, one far away.
FUNCTIONS = (
    ListedFunction(0x00123400, 0x100, "Widget_Spin", True),
    ListedFunction(0x00123500, 0x80, "", False),
    ListedFunction(0x00200000, 0x40, "Other_Function", True),
)
SECTIONS = (
    Section(".text", 0x00100000, 0x00300000, code=True),
    Section(".data", 0x00300000, 0x00400000),
    Section(".bss", 0x00400000, 0x00500000),
)
#: An address inside `Widget_Spin`, one in the data and one nobody owns.
IN_FUNCTION = "0x00123456"
IN_DATA = "0x00310000"
NOWHERE = "0x00123fff"


@functools.cache
def research() -> Research:
    """The research conventions of this checkout's `.github/conventions.yml`."""
    return parse_conventions(real_data()).research


@functools.cache
def touch_scope() -> TouchScope:
    """How far the touch-it rule reaches, from this checkout's `.github/conventions.yml` (`standards.touch`)."""
    return parse_conventions(real_data()).standards.touch


def catalog(*, runs: frozenset[str] | None = None) -> Catalog:
    """The made-up executable."""
    return Catalog(FUNCTIONS, SECTIONS, runs)


def env(
    pages: Mapping[str, str] | None = None,
    *,
    legacy: Mapping[str, Mapping[str, int]] | None = None,
    runs: frozenset[str] | None = None,
    with_catalog: bool = True,
) -> ResearchEnv:
    """An environment holding the real conventions, the made-up executable and the other pages given as path to text."""
    rules = research()
    return ResearchEnv(
        rules,
        catalog(runs=runs) if with_catalog else Catalog(),
        Corpus.from_texts(pages or {}, rules),
        Legacy(dict(legacy or {})),
    )


def front(kind: str = "topic", **keys: str) -> str:
    """Front matter for a page of `kind`; `keys` replace or add lines (`system="combat"`, `owns="..."`)."""
    lines = {"kind": kind}
    if kind in ("system", "topic"):
        lines["system"] = "widget"
        lines["owns"] = '\n  functions: ["0x00123400"]'
    lines.update(keys)
    return "---\n" + "\n".join(f"{key}: {value}" for key, value in lines.items() if value != "") + "\n---\n"


def page(
    behaviour: str = f"The widget spins (confirmed (code) at {IN_FUNCTION}).\n",
    *,
    kind: str = "topic",
    head: str = VERIFIED,
    heading: str = "Widget",
    drop: tuple[str, ...] = (),
    extra: str = "",
    sections: Mapping[str, str] | None = None,
    meta: str | None = None,
) -> str:
    """A complete page of `kind` (a topic by default): front matter, title, Verified line and every required section.

    `behaviour` is the body of the section that states behaviour; `sections` replaces the body of the sections named,
    `drop` leaves sections out and `extra` goes after the Verified line, before the first section.
    """
    spec = research().kinds[kind]
    given = dict(sections or {})
    stating = {"system": "Behaviour", "topic": "Behaviour", "format": "Layout", "mission": "Flow", "feature": "Rules"}
    given.setdefault(stating.get(kind, "Findings"), behaviour)
    bodies = {
        name: given.get(name, "Nothing more to say.\n")
        for name in spec.sections
        if name in spec.required or name in given
    }
    meta = front(kind) if meta is None else meta
    body = "".join(f"## {name}\n\n{text}\n" for name, text in bodies.items() if name not in drop)
    return f"{meta}# {heading}\n\n{head}\n{extra}{body}"


def findings(path: str, text: str, environment: ResearchEnv | None = None, touch: Touch | None = None) -> list[Finding]:
    """Every finding of the research page `text` at `path`."""
    return check_page(path, text, Context(research=environment or env(), touch=touch_scope()), touch)


def rules_of(found: list[Finding], severity: str = "error") -> list[str]:
    """The rule of each finding of `severity`, in order."""
    return [item.rule for item in found if item.severity == severity]


def errors(text: str, environment: ResearchEnv | None = None, path: str = RESEARCH_PATH) -> list[str]:
    """The rules a page fails on (warnings left out)."""
    return rules_of(findings(path, text, environment))


# --- a made-up checkout with a legacy file ---------------------------------------------------------------------------

LEGACY_FILE = "research/legacy.toml"


def write(root: Path, name: str, text: str) -> None:
    """Write a file of the made-up checkout."""
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def checkout(tmp_path: Path, legacy: str | None = None, files: Mapping[str, str] | None = None) -> Path:
    """A repository with this repository's conventions, a good survey page, a bad page, `files` (path to text) and,
    perhaps, a legacy file; all committed on `main`, with the branch `work` checked out."""
    git(tmp_path, "init", "-q", "-b", "main")
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    (tmp_path / ".github").mkdir()
    (tmp_path / ".github/conventions.yml").write_bytes((REPO / ".github/conventions.yml").read_bytes())
    write(tmp_path, "docs/research/good.md", page(kind="survey", meta="---\nkind: survey\n---\n"))
    write(tmp_path, "docs/research/old.md", "# Old\n\nNo sections.\n\n## Data\n\nSomething at 0x00123456.\n")
    for name, text in (files or {}).items():
        write(tmp_path, name, text)
    if legacy is not None:
        write(tmp_path, LEGACY_FILE, legacy)
    git(tmp_path, "add", "-A")
    git(tmp_path, "commit", "-q", "-m", "base")
    git(tmp_path, "checkout", "-q", "-b", "work")
    return tmp_path


def context(root: Path) -> Context:
    """The context of the made-up checkout."""
    return docs_check.context_for(root)
