# SPDX-License-Identifier: GPL-3.0-or-later
"""An issue written in its form's exact shape: the draft an agent fills in, and the check run before it is filed.

Issues filed from the command line skip the web form, so nothing used to check them. Every rule here already lives in
`.github/conventions.yml` (`issues:`: the forms' sections with their required flags and order, the placeholders, the
Screenshots line, the `From:` line, the links and the wording) and in the form files `.github/ISSUE_TEMPLATE/*.yml`
(the guidance text of each field and the allowed values of the dropdowns); this module reads both and hard-codes
neither. `draft_body` prints what an agent fills in, `check_issue` says what is wrong with a filled body, and
`labels_for` says which labels the new issue carries. The commands are in `coney_tools.issue_cli`, the audit rule
(`issue.form`) in `coney_tools.audit_github`; docs/guides/issues.md#filing explains the workflow.

Everything works on strings plus two injected lookups (the files on main and the state of a cited issue), so the tests
need no git and no network.
"""

from __future__ import annotations

import difflib
import re
from collections.abc import Callable, Collection, Iterable
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, NamedTuple

import yaml

from coney_tools import audit_text
from coney_tools.audit_github import MainTree, _label_problems
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.repo_checks import check_title, screenshots_problems

#: What a problem is about, which the audit uses to leave out what a sibling rule already reports.
KINDS = ("title", "headings", "empty", "choices", "screenshots", "from", "links", "wording", "status")

#: A page named in plain text may say it is not on main yet, as the guide asks ("not on `main` yet").
_NOT_ON_MAIN = re.compile(r"\bnot\s+(?:yet\s+)?on\s+`?main`?|\bunreleased\s+branch\b", re.IGNORECASE)
#: A Markdown heading of level one to three (deeper headings are text inside a section).
_HEADING = re.compile(r"^(?P<hashes>#{1,3})[ \t]+(?P<name>\S.*?)[ \t]*#*[ \t]*$")
_FENCE = re.compile(r"^[ \t]*(```|~~~)")
_FENCED_BLOCK = re.compile(r"^[ \t]*(```|~~~).*?^[ \t]*\1[^\n]*$", re.DOTALL | re.MULTILINE)
#: A task-list item: its mark and its text.
_CHECKBOX = re.compile(r"^[ \t]*[-*+][ \t]+\[(?P<mark>[ xX])\][ \t]*(?P<text>.*?)[ \t]*$")
#: `#317 (open, not merged yet)` or `#319 is closed`: a number and the words that give its state.
_STATE_WORDS = re.compile(
    r"(?<![\w&/])#(?P<number>\d+)\b(?:\s*\((?P<paren>[^)\n]*)\)|\s+(?:is|was|has\s+been)\s+(?P<word>[A-Za-z]+(?:\s+[A-Za-z]+)?))"
)


class IssueProblem(NamedTuple):
    """One thing wrong with an issue before it is filed."""

    #: One of `KINDS`.
    kind: str
    message: str


@dataclass(frozen=True)
class FieldSpec:
    """One field of a form: what the conventions say of it and what the form file says to the person filling it."""

    id: str
    heading: str
    required: bool
    #: `textarea`, `input`, `dropdown` or `checkboxes`.
    kind: str
    description: str = ""
    placeholder: str = ""
    #: The values of a dropdown, or the labels of the checkboxes.
    options: tuple[str, ...] = ()
    #: The checkboxes that must be ticked.
    required_options: tuple[str, ...] = ()


@dataclass(frozen=True)
class FormSpec:
    """An issue form: its conventions entry joined with its `.github/ISSUE_TEMPLATE` file."""

    name: str
    title: str
    description: str
    labels: tuple[str, ...]
    fields: tuple[FieldSpec, ...]

    def field(self, field_id: str) -> FieldSpec | None:
        """The field with the id, or None."""
        return next((item for item in self.fields if item.id == field_id), None)


@dataclass(frozen=True)
class CheckOptions:
    """What a check may use beyond the text: the kind of issue, the files on main, issue states, local images."""

    #: The issue is a leftover of a pull request, so it needs a `From:` line.
    leftover: bool = False
    #: The files of the default branch (offline git); None when they cannot be read.
    tree: MainTree | None = None
    #: Looks up the state of an issue or pull request (`open`, `closed`, `merged`; None when unknown); None = offline.
    states: Callable[[int], str | None] | None = None
    #: Lower-case strings that spell the repository's or the docs site's address.
    address_terms: Collection[str] = ()
    #: A Screenshots image must already be uploaded; False when `--attach` will upload local files.
    uploaded_only: bool = True
    #: Kinds of problem to leave out (the audit leaves out what its other rules report).
    skip: frozenset[str] = frozenset()


@dataclass
class CheckResult:
    """The outcome of a check."""

    form: str
    problems: list[IssueProblem] = field(default_factory=list)
    #: Things that could not be checked; not problems.
    notices: list[str] = field(default_factory=list)
    #: The label named by the Area section when it is a valid option, else None.
    area: str | None = None


# --- reading the forms ----------------------------------------------------------------------------------------------


def _text(value: object) -> str:
    """A YAML scalar as text, with the trailing newline of a block scalar dropped."""
    return str(value).strip() if value is not None else ""


def load_form(root: Path, conventions: Conventions, name: str) -> FormSpec:
    """The form `name`: its sections from the conventions joined with the guidance and choices of its form file.

    ConfigError when there is no such form, or its file is missing, unreadable or lacks a field the conventions list.
    """
    forms = conventions.issues.forms
    if name not in forms:
        raise ConfigError(f"no form {name!r} (the conventions have: {', '.join(forms)})")
    form = forms[name]
    path = root / form.file
    try:
        data = yaml.safe_load(path.read_text(encoding="utf-8"))
        items = {str(item["id"]): item for item in data["body"] if isinstance(item, dict) and "id" in item}
    except (OSError, UnicodeDecodeError, yaml.YAMLError, KeyError, TypeError) as error:
        raise ConfigError(f"{form.file}: cannot be read as an issue form ({error})") from error
    fields = []
    for section in form.sections:
        item = items.get(section.id)
        if item is None:
            raise ConfigError(f"{form.file}: no field with id {section.id!r}, which issues.forms.{name} lists")
        attributes: dict[str, Any] = item.get("attributes") or {}
        options = attributes.get("options") or []
        shown = tuple(_text(option["label"] if isinstance(option, dict) else option) for option in options)
        needed = tuple(
            _text(option["label"]) for option in options if isinstance(option, dict) and option.get("required")
        )
        fields.append(
            FieldSpec(
                id=section.id,
                heading=section.heading,
                required=section.required,
                kind=str(item.get("type", "textarea")),
                description=_text(attributes.get("description")),
                placeholder=_text(attributes.get("placeholder")),
                options=shown,
                required_options=needed,
            )
        )
    return FormSpec(
        name=name,
        title=_text(data.get("name")),
        description=_text(data.get("description")),
        labels=form.labels,
        fields=tuple(fields),
    )


def _norm(text: str) -> str:
    """`text` lower-cased with runs of space as one space, for comparing headings."""
    return " ".join(text.casefold().split())


def detect_form(title: str, body: str, conventions: Conventions) -> str:
    """The form a title and body follow: the form most of the body's headings belong to; when the headings tie (a body
    with only the shared "Screenshots" heading) or none match, the form the command line uses (a title says nothing
    about the form: it is `area: Verb the rest` for every issue)."""
    forms = conventions.issues.forms
    _, blocks = _blocks(body)
    seen = {_norm(block.name) for block in blocks}
    scores = {
        name: sum(1 for section in form.sections if _norm(section.heading) in seen) for name, form in forms.items()
    }
    top = max(scores.values(), default=0)
    leaders = [name for name, score in scores.items() if score == top and top > 0]
    if len(leaders) == 1:
        return leaders[0]
    return conventions.issues.cli_uses_form


# --- the draft ------------------------------------------------------------------------------------------------------


def _comment(lines: Iterable[str]) -> str:
    """An HTML comment holding `lines` (a `-->` inside would end it early, so it is spaced out)."""
    inner = "\n".join(line.replace("-->", "- ->") for line in lines)
    return f"<!--\n{inner}\n-->"


def draft_body(spec: FormSpec, conventions: Conventions) -> str:
    """A blank body in the form's exact headings and order, each section preceded by a comment with the form's
    description, whether it is required, and the allowed values; then the `From:` line. This is what an agent fills in.

    The comments are HTML comments: the check ignores them and GitHub does not show them, and `issue create` leaves
    them out of the issue.
    """
    issues = conventions.issues
    link = issues.links.link_form
    out = [
        _comment(
            [
                f"{spec.title or spec.name} form: {spec.description}",
                "Fill in each section below. Keep the ### headings, their names and their order. A required section",
                "must say something real (not x or TBD); an optional one may be left out. Call the person who",
                f"directs the project '{conventions.repo.wording.person}'. Check it with:",
                "  coney-tools issue check --title <title> --body <this file> [--leftover] [--online]",
                "and file it with: coney-tools issue create --title <title> --body <this file>",
                f"A page of the repository is a link, written {link} (the page and its {{#anchor}} must be on "
                f"{issues.links.must_exist_on}).",
            ]
        ),
        "",
    ]
    for item in spec.fields:
        notes = [f"{item.heading}: {'required' if item.required else 'optional'} ({item.kind})."]
        if item.description:
            notes.append(item.description)
        if item.kind == "dropdown":
            notes.append(f"Write exactly one of: {', '.join(item.options)}")
        elif item.kind == "checkboxes":
            notes.append(f"Tick (change [ ] to [x]) the required ones: {'; '.join(item.required_options) or 'none'}")
        if item.placeholder:
            example = re.sub(r"^for example:?\s*", "", item.placeholder, flags=re.IGNORECASE)
            notes.append(f"For example: {example}")
        if item.id == "screenshots" and issues.screenshots_none not in item.description:
            notes.append(f"With nothing to show, write one line: {issues.screenshots_none} (<reason>)")
        if not item.required:
            notes.append("Leave the whole section out if you have nothing for it.")
        out += [_comment(notes), f"### {item.heading}", ""]
        if item.kind == "checkboxes":
            out += [f"- [ ] {option}" for option in item.options]
            out.append("")
    out += [
        _comment(
            [
                f"{issues.provenance.line} where this came from: a pull request, an issue, a track and the date, "
                "for example 'From: user playtest of the preview build, 2026-10-11'. Required for a leftover "
                "(issue check --leftover). It names the source and nothing more.",
            ]
        ),
        issues.provenance.line,
        "",
    ]
    return "\n".join(out)


# --- reading a body -------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Block:
    """The text under one heading of level one to three."""

    level: int
    name: str
    text: str


def _blocks(body: str) -> tuple[str, list[Block]]:
    """The text before the first heading and the blocks of a body; a heading in a code fence is not one."""
    preamble: list[str] = []
    found: list[tuple[int, str, list[str]]] = []
    fenced = False
    for line in audit_text.clean(body).splitlines():
        if _FENCE.match(line):
            fenced = not fenced
        heading = None if fenced else _HEADING.match(line)
        if heading:
            found.append((len(heading.group("hashes")), heading.group("name"), []))
        elif found:
            found[-1][2].append(line)
        else:
            preamble.append(line)
    return "\n".join(preamble).strip(), [Block(level, name, "\n".join(lines).strip()) for level, name, lines in found]


def section_text(body: str, heading: str) -> str | None:
    """The text under the heading `heading` (compared as the form's headings are) in an issue body, or None."""
    wanted = _norm(heading)
    for block in _blocks(body)[1]:
        if _norm(block.name) == wanted:
            return block.text
    return None


def section_heading(conventions: Conventions, section_id: str) -> str | None:
    """The heading of the form section with this id (`blocked-by`) in any form, or None."""
    for form in conventions.issues.forms.values():
        for section in form.sections:
            if section.id == section_id:
                return section.heading
    return None


def _hashes(level: int) -> str:
    """`#` repeated `level` times."""
    return "#" * level


# --- the check ------------------------------------------------------------------------------------------------------


def check_issue(title: str, body: str, spec: FormSpec, conventions: Conventions, options: CheckOptions) -> CheckResult:
    """Everything wrong with an issue before it is filed, as clear messages; an empty `problems` means it passes.

    Checks the title; the headings (level, names, order, none extra or missing); required sections non-empty and not a
    placeholder; dropdown values and required checkboxes; Screenshots; the `From:` line of a leftover; docs and
    research pages written as links to files and anchors that exist on main; the wording; and, when `options.states`
    is given, the words that call a cited issue or pull request open, merged or closed.
    """
    result = CheckResult(spec.name)
    found: list[IssueProblem] = []
    _check_title(title, conventions, found)
    preamble, blocks = _blocks(body)
    by_id = _check_headings(spec, preamble, blocks, found)
    for item in spec.fields:
        block = by_id.get(item.id)
        if block is not None:
            _check_field(item, block, spec, conventions, options, found, result)
    _check_provenance(body, conventions, options, found)
    _check_links(body, conventions, options, found, result)
    _check_words(body, conventions, found)
    if options.states is not None:
        _check_states(body, options.states, found)
    result.problems = [problem for problem in found if problem.kind not in options.skip]
    return result


def _check_title(title: str, conventions: Conventions, found: list[IssueProblem]) -> None:
    """The title is one line in the pull request form, `area: Verb the rest` (`commits.title`, one definition)."""
    if not title.strip():
        found.append(IssueProblem("title", "the title is empty"))
    elif "\n" in title.strip():
        found.append(IssueProblem("title", "the title must be one line"))
    else:
        rules = conventions.commits.title
        found.extend(
            IssueProblem("title", f"{problem} (an issue title reads like a pull request title: {rules.form})")
            for problem in check_title(title.strip(), rules)
        )
    wording = conventions.repo.wording
    if audit_text.word_uses(title, wording.avoid):
        found.append(IssueProblem("wording", f"the title says {wording.avoid[0]!r}: say {wording.person!r}"))


def _check_headings(spec: FormSpec, preamble: str, blocks: list[Block], found: list[IssueProblem]) -> dict[str, Block]:
    """Check the headings against the form; returns the block of each section found, by section id."""
    level = 3
    marks = _hashes(level)
    names = ", ".join(item.heading for item in spec.fields)
    if not blocks:
        found.append(
            IssueProblem(
                "headings",
                f"no form headings: write the body with the {spec.name} form's '{marks}' headings "
                f"(coney-tools issue draft {spec.name} prints them). Sections: {names}",
            )
        )
        return {}
    if preamble:
        found.append(
            IssueProblem("headings", "text before the first heading belongs to no section: put it in a section")
        )
    by_norm = {_norm(item.heading): item for item in spec.fields}
    by_id: dict[str, Block] = {}
    order: list[int] = []
    for block in blocks:
        item = by_norm.get(_norm(block.name))
        if item is None:
            found.append(
                IssueProblem(
                    "headings",
                    f"'{_hashes(block.level)} {block.name}': {block.name!r} is not a section of the {spec.name} "
                    f"form ('coney-tools issue draft {spec.name}' lists its sections)",
                )
            )
            continue
        if block.name != item.heading:
            found.append(
                IssueProblem("headings", f"the heading {block.name!r} is written {item.heading!r} in the form")
            )
        if block.level != level:
            found.append(
                IssueProblem(
                    "headings",
                    f"'{_hashes(block.level)} {item.heading}' is level {block.level}: the form's headings are "
                    f"'{marks} {item.heading}'",
                )
            )
        if item.id in by_id:
            found.append(IssueProblem("headings", f"the section {item.heading!r} appears twice"))
            continue
        by_id[item.id] = block
        order.append(spec.fields.index(item))
    for item in spec.fields:
        if item.required and item.id not in by_id:
            found.append(IssueProblem("headings", f"missing the required section '{marks} {item.heading}'"))
    if order != sorted(order):
        found.append(IssueProblem("headings", f"the sections are out of order: the form's order is {names}"))
    return by_id


def _check_field(
    item: FieldSpec,
    block: Block,
    spec: FormSpec,
    conventions: Conventions,
    options: CheckOptions,
    found: list[IssueProblem],
    result: CheckResult,
) -> None:
    """Check the answer under one heading: filled in, one of the choices, ticked, the Screenshots line."""
    issues = conventions.issues
    said = audit_text.answer(block.text).strip()
    if item.id == "screenshots":
        for message in screenshots_problems(
            block.text,
            item.heading,
            issues.screenshots_none,
            options.uploaded_only,
            conventions.pull_requests.placeholders,
        ):
            found.append(IssueProblem("screenshots", message.replace("'## ", "'### ", 1)))
        return
    if item.kind == "checkboxes":
        _check_checkboxes(item, block, found)
        return
    if item.kind == "dropdown":
        value = said.splitlines()[0].strip() if said else ""
        # An option written as code (`--play-level`) is the same option. A bare `` stays as written, so it is
        # refused as an unknown option rather than read as an empty answer.
        if len(value) > 2 and value.startswith("`") and value.endswith("`"):
            value = value.strip("`").strip()
        if not value:
            if item.required:
                found.append(
                    IssueProblem("choices", f"{item.heading} is empty: write one of {', '.join(item.options)}")
                )
            return
        if value not in item.options:
            near = difflib.get_close_matches(value, item.options, n=1)
            hint = f"; did you mean {near[0]!r}?" if near else ""
            found.append(
                IssueProblem(
                    "choices",
                    f"{item.heading} is {value!r}, which is not one of the form's options: "
                    f"{', '.join(item.options)}{hint}",
                )
            )
        elif item.id == "area":
            result.area = value
        return
    if item.required and audit_text.is_placeholder(block.text, issues.placeholders):
        found.append(
            IssueProblem("empty", f"{item.heading!r} is {'empty' if not said else repr(said)}: write the real answer")
        )


def _check_checkboxes(item: FieldSpec, block: Block, found: list[IssueProblem]) -> None:
    """Each required checkbox is ticked."""
    ticked = set()
    for line in block.text.splitlines():
        match = _CHECKBOX.match(line)
        if match and match.group("mark") in "xX":
            ticked.add(_norm(match.group("text")).rstrip("."))
    for option in item.required_options:
        if _norm(option).rstrip(".") not in ticked:
            found.append(IssueProblem("choices", f"{item.heading}: tick '- [x] {option}'"))


def _check_provenance(body: str, conventions: Conventions, options: CheckOptions, found: list[IssueProblem]) -> None:
    """A leftover has a `From:` line that says something."""
    if not options.leftover:
        return
    line = conventions.issues.provenance.line
    text = _FENCED_BLOCK.sub(" ", audit_text.clean(body))
    pattern = re.compile(rf"^[ \t]*\**{re.escape(line)}\**[ \t]*(?P<rest>.*)$", re.MULTILINE)
    matches = list(pattern.finditer(text))
    if not matches:
        found.append(
            IssueProblem(
                "from",
                f"a leftover needs a '{line}' line that says where it came from (the pull request, issue or track "
                f"and the date), for example '{line} user playtest, 2026-10-11'",
            )
        )
    elif not any(match.group("rest").strip(" *") for match in matches):
        found.append(IssueProblem("from", f"the '{line}' line is empty: say where the issue came from"))


def _check_links(
    body: str, conventions: Conventions, options: CheckOptions, found: list[IssueProblem], result: CheckResult
) -> None:
    """Pages are links to files (and anchors) that exist on main; a page only on a branch is plain text with a note."""
    rules = conventions.issues.links
    main = rules.must_exist_on
    tree = options.tree
    text = _FENCED_BLOCK.sub(" ", audit_text.clean(body))
    problems: list[str] = []
    wanted = False

    def link(path: str) -> str:
        return f"[{path}]({rules.link_form.replace('<path>', path)})"

    for target in audit_text.broken_relative_links(body):
        wanted = True
        problems.append(f"link target {target!r} does not work from an issue page: use {rules.link_form}")
    for path in audit_text.plain_doc_paths(body):
        wanted = True
        bare = path.split("#")[0]
        noted = any(bare in line and _NOT_ON_MAIN.search(line) for line in text.splitlines())
        if tree is not None and tree.has(bare):
            problems.append(f"{bare} is on {main}: write {link(bare)}")
        elif noted:
            continue
        elif tree is not None:
            problems.append(
                f"{bare} is not on {main}: link only pages on {main}; a page only on a branch is named in plain "
                f"text with a note, such as 'not on `{main}` yet'"
            )
        else:
            problems.append(f"{bare} is written as plain text: write {link(bare)}")
    for item in audit_text.repo_links(body):
        wanted = True
        if item.ref != main:
            problems.append(f"links {item.path} on {item.ref!r}, not {main!r}")
        elif tree is not None:
            if not tree.has(item.path):
                problems.append(f"links {item.path}, which is not on {main}")
            elif item.anchor and item.path.endswith(".md"):
                anchors = tree.anchors(item.path)
                if anchors is not None and item.anchor not in anchors:
                    problems.append(f"links {item.path}#{item.anchor}, which has no such section on {main}")
    lowered = f"{text}".lower()
    for term in options.address_terms:
        if term in lowered:
            problems.append("names the repository's or the docs site's address: link a page as above, never by address")
            break
    found.extend(IssueProblem("links", message) for message in problems)
    if tree is None and wanted:
        result.notices.append(
            f"origin/{main} is not in this checkout (git fetch origin): the pages and anchors the issue links were "
            "not checked"
        )


def _check_words(body: str, conventions: Conventions, found: list[IssueProblem]) -> None:
    """The person who directs the project is 'the user'."""
    wording = conventions.repo.wording
    uses = audit_text.word_uses(body, wording.avoid)
    if uses:
        found.append(
            IssueProblem("wording", f"says {wording.avoid[0]!r} {len(uses)} time(s): say {wording.person!r}: {uses[0]}")
        )


# --- statuses -------------------------------------------------------------------------------------------------------


def _state_of(words: str) -> str | None:
    """The state a few words give: `closed`, `open` (also "not merged yet") or `merged`; None when they give none."""
    lowered = words.casefold()
    if re.search(r"\bclosed\b", lowered):
        return "closed"
    if re.search(r"\bnot\s+(?:yet\s+)?(?:merged|landed)\b|\bunmerged\b|\bopen\b", lowered):
        return "open"
    if re.search(r"\b(?:merged|landed)\b", lowered):
        return "merged"
    return None


def status_claims(text: str) -> list[tuple[int, str]]:
    """Each `#N (open ...)`, `#N (merged)` or `#N is closed` of the prose of `text` as (number, claimed state)."""
    claims = []
    for match in _STATE_WORDS.finditer(audit_text.prose(text)):
        state = _state_of(match.group("paren") or match.group("word") or "")
        if state is not None:
            claims.append((int(match.group("number")), state))
    return claims


def _check_states(body: str, states: Callable[[int], str | None], found: list[IssueProblem]) -> None:
    """The words that call a cited issue or pull request open, merged or closed agree with GitHub."""
    wrong: dict[int, str] = {}
    for number, claimed in status_claims(body):
        actual = states(number)
        if actual is None or claimed == actual or (claimed == "closed" and actual == "merged") or number in wrong:
            continue
        wrong[number] = f"#{number} is called {claimed} but is {actual}: say what it is now"
    found.extend(IssueProblem("status", message) for message in wrong.values())


# --- labels ---------------------------------------------------------------------------------------------------------


def labels_for(
    spec: FormSpec, area: str | None, extra: list[str], conventions: Conventions
) -> tuple[list[str], list[str]]:
    """The labels of a new issue and what is wrong with them: the form's labels and the area's, plus `extra`.

    A label in `extra` of a family the form's labels already fill with exactly one (the type, the status) replaces
    that one (`--label docs` for a documentation task); the others are added. The result is checked against
    `issues.labels` (unknown labels, one type, one status, one priority).
    """
    labels_rules = conventions.labels
    replaced = {labels_rules.family(name) for name in extra if labels_rules.family(name) in ("type", "status")}
    labels = [name for name in spec.labels if labels_rules.family(name) not in replaced]
    for name in ([area] if area else []) + extra:
        if name not in labels:
            labels.append(name)
    needs_area = spec.field("area") is not None
    problems = [
        message
        for message in _label_problems(labels, True, conventions)
        if needs_area or not message.startswith("no area label")
    ]
    return labels, problems
