# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks on the repository itself, run by `coney-tools repo check`, `check-title`, `check-docs`, `check-body`, CI."""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path

from coney_tools.config import ConfigError

#: Agent entry files that must hold only a pointer to AGENTS.md, the single instruction file.
POINTER_FILES: tuple[str, ...] = ("CLAUDE.md", "GEMINI.md")
#: A pointer file holding more than this has grown instructions of its own, which would drift from AGENTS.md.
MAX_POINTER_BYTES = 400
IMPORT_LINE = "@AGENTS.md"


def check_pointer_files(root: Path) -> list[str]:
    """Check that each of POINTER_FILES exists, has a line that is exactly `@AGENTS.md` and is small.

    Returns one message per problem; an empty list means clean.
    """
    problems: list[str] = []
    for name in POINTER_FILES:
        path = root / name
        if not path.is_file():
            problems.append(f"{name}: missing")
            continue
        data = path.read_bytes()
        if len(data) > MAX_POINTER_BYTES:
            problems.append(f"{name}: {len(data)} bytes, over the {MAX_POINTER_BYTES} allowed for a pointer file")
        if IMPORT_LINE not in data.decode("utf-8", errors="replace").splitlines():
            problems.append(f"{name}: no line that is exactly {IMPORT_LINE}")
    return problems


# --- commit and pull request titles ------------------------------------------------------------------------------

#: The areas and verbs; AGENTS.md's "Commits and GitHub" section states the same rules in prose.
TITLE_RULES_FILE = ".github/commit-conventions.json"
TITLE_MAX = 72
BREAKING = "(BREAKING)"

#: `area: Verb` and at least one more word; the area and verb are then checked against the rules file.
_TITLE_FORM = re.compile(r"^(?P<area>[^\s:]+): (?P<verb>\S+) \S")


class TitleRulesError(ConfigError):
    """The commit conventions file is missing or does not hold usable title rules."""


@dataclass(frozen=True)
class TitleRules:
    """The title rules from the commit conventions file: the known areas and the allowed verbs."""

    areas: frozenset[str]
    verbs: tuple[str, ...]


def load_title_rules(root: Path) -> TitleRules:
    """Read the areas and verbs from `root / TITLE_RULES_FILE`; raises TitleRulesError when they are unusable."""
    path = root / TITLE_RULES_FILE
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise TitleRulesError(f"{TITLE_RULES_FILE}: cannot be read ({error.strerror or error})") from error
    except json.JSONDecodeError as error:
        raise TitleRulesError(f"{TITLE_RULES_FILE}: not valid JSON (line {error.lineno}: {error.msg})") from error
    if not isinstance(data, dict):
        raise TitleRulesError(f"{TITLE_RULES_FILE}: expected a JSON object")
    areas = data.get("areas")
    if not isinstance(areas, dict) or not areas or not all(isinstance(key, str) and key for key in areas):
        raise TitleRulesError(f"{TITLE_RULES_FILE}: 'areas' must be a non-empty object of area names")
    verbs = data.get("verbs")
    if not isinstance(verbs, list) or not verbs or not all(isinstance(verb, str) and verb for verb in verbs):
        raise TitleRulesError(f"{TITLE_RULES_FILE}: 'verbs' must be a non-empty list of words")
    return TitleRules(areas=frozenset(areas), verbs=tuple(verbs))


def check_title(title: str, rules: TitleRules) -> list[str]:
    """Check a title against the commit title rules: `area: Verb the rest`, a known area, an allowed verb, 72 characters
    at most, no trailing period or whitespace, and `(BREAKING)` only at the end.

    Returns one reason per broken rule; an empty list means the title passes. A title not in the form at all stops
    at that one reason, since it has no area or verb to check.
    """
    form = _TITLE_FORM.match(title)
    if not form:
        return ["not in the form 'area: Verb the rest'"]
    area, verb = form.group("area"), form.group("verb")
    problems: list[str] = []
    if area not in rules.areas:
        problems.append(f"unknown area '{area}'")
    if verb not in rules.verbs:
        problems.append(f"'{verb}' is not one of the verbs ({', '.join(rules.verbs)})")
    if len(title) > TITLE_MAX:
        problems.append(f"{len(title)} characters; {TITLE_MAX} at most")
    bare = title.rstrip()
    if bare != title:
        problems.append("ends with whitespace")
    if bare.endswith("."):
        problems.append("ends with a period")
    if BREAKING in bare and not bare.endswith(" " + BREAKING):
        problems.append(f"{BREAKING} must end the title, after a space")
    return problems


def first_line(message: str) -> str:
    """The title of a commit message: its first line that is neither blank nor a `#` comment ("" if none)."""
    for line in message.splitlines():
        if line.strip() and not line.startswith("#"):
            return line
    return ""


#: Paths whose change alters what the project does or how its tools behave, so the docs must follow.
CODE_PREFIXES: tuple[str, ...] = ("src/", "python/src/")
#: Paths that hold the documentation: the site's pages, and the reference lists and bindings behind its generated pages.
DOC_PREFIXES: tuple[str, ...] = ("docs/", "research/")
#: The line a pull request body carries when its code change needs no documentation (a refactor, a fix of a typo).
DOCS_NONE = re.compile(r"^\s*Docs:\s*none\b", re.IGNORECASE | re.MULTILINE)


def check_docs(changed: list[str], body: str) -> list[str]:
    """Check that a pull request changing code changes the docs too, or says `Docs: none` in its body.

    `changed` holds the repository-relative paths the pull request touches (forward slashes). Only `src/` and
    `python/src/` count as code, so a change to tests, CI or build files asks for nothing. Returns the reasons the
    pull request is refused; an empty list means it passes.
    """
    code = [path for path in changed if path.startswith(CODE_PREFIXES)]
    if not code or any(path.startswith(DOC_PREFIXES) for path in changed) or DOCS_NONE.search(body):
        return []
    shown = ", ".join(code[:3]) + (f" and {len(code) - 3} more" if len(code) > 3 else "")
    return [
        f"changes code ({shown}) but nothing under {' or '.join(DOC_PREFIXES)}; "
        "update the living doc in the same pull request, or put a line `Docs: none` in its description"
    ]


# --- pull request descriptions -----------------------------------------------------------------------------------

#: The headings, the AI use choices and the placeholder text of the description; .github/pull_request_template.md
#: carries the same headings and labels, and a test holds the two together.
BODY_RULES_FILE = ".github/pr-body-rules.json"

_COMMENT = re.compile(r"<!--.*?-->", re.DOTALL)
#: A second-level Markdown heading; a deeper one (`###`) is part of the section above it.
_HEADING = re.compile(r"^##[ \t]+(?P<name>\S.*?)[ \t]*$")
#: What a list item or a task-list item starts with, which is not content (`- `, `- [ ] `, `* [x] `).
_LIST_MARKER = re.compile(r"^[-*+](?:[ \t]+\[[ xX]\])?[ \t]*")
#: A Markdown image: its target is the angle-bracketed text (which may hold spaces) or the text up to the first space
#: or `)`; an optional title after the target is ignored.
_MARKDOWN_IMAGE = re.compile(r"!\[[^\]\n]*\]\(\s*(?:<(?P<angle>[^>\n]*)>|(?P<plain>[^\s)]*))[^)\n]*\)")
#: An `<img>` tag's `src`, quoted or bare.
_IMG_SRC = re.compile(
    r"""<img\s[^>]*?\bsrc\s*=\s*(?:"(?P<dq>[^"]*)"|'(?P<sq>[^']*)'|(?P<bare>[^\s>]+))""", re.IGNORECASE
)
#: A whole `<img>` tag.
_IMG_TAG = re.compile(r"<img\s[^>]*>", re.IGNORECASE)
#: A bare link to a GitHub attachment (an image or a video that `gh --attach` or the browser uploaded).
_ATTACHMENT_LINK = re.compile(r"https://github\.com/user-attachments/\S+", re.IGNORECASE)
#: A target a reader can open: an http(s) URL. Anything else (a relative path, a drive path, `file:`) is a local file.
_REMOTE = re.compile(r"https?://\S", re.IGNORECASE)


#: Code the text may show as it is: a fenced block (``` or ~~~) and an inline code span.
_CODE = re.compile(r"^[ \t]*(?P<fence>```|~~~).*?^[ \t]*(?P=fence)[^\n]*$|`[^`\n]*`", re.DOTALL | re.MULTILINE)
#: A URL, whose path may hold anything.
_URL = re.compile(r"https?://\S+", re.IGNORECASE)
#: A path on the author's machine in prose: under `scratch/`, up two folders with `../../`, or on a Windows drive.
_LOCAL_PATH = re.compile(r"(?<![\w-])scratch[/\\]\S*|(?<![\w.])(?:\.\.[/\\]){2,}\S*|(?<![\w])[A-Za-z]:[/\\]\S*")


class BodyRulesError(ConfigError):
    """The pull request description rules file is missing or does not hold usable rules."""


@dataclass(frozen=True)
class BodyRules:
    """What a pull request description must hold: sections, the AI use choices and the template's placeholder text."""

    #: The `##` headings the description must have, in the template's order.
    sections: tuple[str, ...]
    #: The sections (a subset of `sections`) that must hold text, not just a comment or placeholder.
    filled: tuple[str, ...]
    #: Lines of template text that count as empty when left unchanged (HTML comments always do).
    placeholders: tuple[str, ...]
    #: The section that holds the screenshots (or the line saying there are none).
    screenshots_section: str
    #: The start of the line that says there is nothing to show, followed by the reason in parentheses.
    screenshots_none: str
    #: The section that holds the two AI use lines.
    ai_section: str
    written_by_label: str
    #: The allowed values after `written_by_label`.
    choices: tuple[str, ...]
    #: The choice that means no AI took part, so no model is needed.
    no_ai: str
    model_label: str
    #: What a person writes after `model_label` when no model applies; not enough when an AI took part.
    model_none: str
    #: The section that holds the Done and Remaining lists.
    remaining_section: str
    #: The start of the line that opens the list of what the pull request leaves for later (`Remaining`).
    remaining_label: str


def _strings(value: object, what: str) -> tuple[str, ...]:
    """A non-empty list of non-empty strings from the rules file, or BodyRulesError naming `what`."""
    if not isinstance(value, list) or not value or not all(isinstance(item, str) and item for item in value):
        raise BodyRulesError(f"{BODY_RULES_FILE}: '{what}' must be a non-empty list of non-empty strings")
    return tuple(value)


def _text(data: object, key: str, where: str) -> str:
    """The non-empty string at `key` of the object `data`, or BodyRulesError naming `where`."""
    value = data.get(key) if isinstance(data, dict) else None
    if not isinstance(value, str) or not value:
        raise BodyRulesError(f"{BODY_RULES_FILE}: '{where}.{key}' must be a non-empty string")
    return value


def load_body_rules(root: Path) -> BodyRules:
    """Read the description rules from `root / BODY_RULES_FILE`; raises BodyRulesError when they are unusable."""
    path = root / BODY_RULES_FILE
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise BodyRulesError(f"{BODY_RULES_FILE}: cannot be read ({error.strerror or error})") from error
    except json.JSONDecodeError as error:
        raise BodyRulesError(f"{BODY_RULES_FILE}: not valid JSON (line {error.lineno}: {error.msg})") from error
    if not isinstance(data, dict):
        raise BodyRulesError(f"{BODY_RULES_FILE}: expected a JSON object")
    sections = _strings(data.get("sections"), "sections")
    filled = _strings(data.get("filled"), "filled")
    placeholders = data.get("placeholders")
    if not isinstance(placeholders, list) or not all(isinstance(item, str) for item in placeholders):
        raise BodyRulesError(f"{BODY_RULES_FILE}: 'placeholders' must be a list of strings")
    ai_use = data.get("ai_use")
    written_by = ai_use.get("written_by") if isinstance(ai_use, dict) else None
    model = ai_use.get("model") if isinstance(ai_use, dict) else None
    ai_section = _text(ai_use, "section", "ai_use")
    choices = _strings(written_by.get("choices") if isinstance(written_by, dict) else None, "ai_use.written_by.choices")
    no_ai = _text(written_by, "no_ai", "ai_use.written_by")
    screenshots = data.get("screenshots")
    screenshots_section = _text(screenshots, "section", "screenshots")
    screenshots_none = _text(screenshots, "none", "screenshots")
    remaining = data.get("remaining")
    remaining_section = _text(remaining, "section", "remaining")
    remaining_label = _text(remaining, "label", "remaining")
    unknown = [name for name in (*filled, ai_section, screenshots_section, remaining_section) if name not in sections]
    if unknown:
        raise BodyRulesError(f"{BODY_RULES_FILE}: {', '.join(unknown)} is not one of the 'sections'")
    if no_ai not in choices:
        raise BodyRulesError(f"{BODY_RULES_FILE}: 'ai_use.written_by.no_ai' must be one of the choices")
    return BodyRules(
        sections=sections,
        filled=filled,
        placeholders=tuple(placeholders),
        screenshots_section=screenshots_section,
        screenshots_none=screenshots_none,
        ai_section=ai_section,
        written_by_label=_text(written_by, "label", "ai_use.written_by"),
        choices=choices,
        no_ai=no_ai,
        model_label=_text(model, "label", "ai_use.model"),
        model_none=_text(model, "none", "ai_use.model"),
        remaining_section=remaining_section,
        remaining_label=remaining_label,
    )


def strip_comments(text: str) -> str:
    """`text` without its HTML comments, which is how the template's instructions and choices are written."""
    return _COMMENT.sub("", text)


def _split_sections(text: str) -> dict[str, str]:
    """The text under each `##` heading, keyed by its lower-case name; text before the first heading is dropped."""
    sections: dict[str, list[str]] = {}
    current: list[str] | None = None
    for line in text.splitlines():
        heading = _HEADING.match(line)
        if heading:
            current = sections.setdefault(heading.group("name").casefold(), [])
        elif current is not None:
            current.append(line)
    return {name: "\n".join(lines) for name, lines in sections.items()}


def _is_blank(text: str, rules: BodyRules) -> bool:
    """Whether `text` holds nothing but empty lines and the template's placeholders (`- [ ] ...`)."""
    for line in text.splitlines():
        content = _LIST_MARKER.sub("", line.strip()).strip()
        if content and content not in rules.placeholders:
            return False
    return True


def _label_value(section: str, label: str) -> str | None:
    """What follows `label` on its line in `section` (`- **Model:** x` gives `x`); None when no line has the label."""
    pattern = re.compile(
        rf"^[ \t]*(?:[-*+][ \t]+)?\**[ \t]*{re.escape(label)}\**[ \t]*(?P<value>.*)$", re.IGNORECASE | re.MULTILINE
    )
    found = pattern.search(section)
    return found.group("value").strip().strip("*_` \t") if found else None


def _check_ai_use(section: str, rules: BodyRules) -> list[str]:
    """Check the two AI use lines: who wrote the commits (one of the choices) and, when an AI did, which model."""
    label = rules.written_by_label
    value = _label_value(section, label)
    if value is None:
        return [f"AI use: '{label}' is missing"]
    if not value or value in rules.placeholders:
        return [f"AI use: '{label}' is empty; choose one of: {' / '.join(rules.choices)}"]
    chosen = " ".join(value.rstrip(".").casefold().split())
    matching = [choice for choice in rules.choices if " ".join(choice.casefold().split()) == chosen]
    if not matching:
        return [f"AI use: '{label}' {value!r} is not one of: {' / '.join(rules.choices)}"]
    if matching[0] == rules.no_ai:
        return []
    model = _label_value(section, rules.model_label)
    if model is None:
        return [f"AI use: '{rules.model_label}' is missing; name the model, since an AI took part"]
    if not model or model in rules.placeholders or model.casefold() == rules.model_none.casefold():
        return [f"AI use: '{rules.model_label}' is empty; name the model, since an AI took part"]
    return []


#: An unchecked task-list item, `- [ ] text`.
_OPEN_ITEM = re.compile(r"^[ \t]*[-*+][ \t]+\[ \][ \t]*(?P<text>.*)$")

#: A line that opens a labelled list (`Done:`, `Remaining (later units):`), which ends the list before it.
_LIST_LABEL = re.compile(r"^[ \t]*\**[A-Za-z][^:\n]*:\**[ \t]*$")

#: A reference to an issue or pull request, `#123`.
_NUMBER_REFERENCE = re.compile(r"(?<![\w&/])#\d+\b")


def _remaining_items(section: str, rules: BodyRules) -> list[str]:
    """The text of each unchecked item under the section's Remaining line (wrapped lines joined), placeholders aside."""
    label = rules.remaining_label.rstrip(":").casefold()
    items: list[list[str]] = []
    inside = False
    continuing = False
    for line in section.splitlines():
        stripped = line.strip()
        if _LIST_LABEL.match(line):
            inside = stripped.strip("*").casefold().startswith(label)
            continuing = False
            continue
        if not inside:
            continue
        item = _OPEN_ITEM.match(line)
        if item:
            items.append([item.group("text").strip()])
            continuing = True
        elif continuing and stripped and line[:1] in (" ", "\t") and not _LIST_MARKER.match(stripped):
            # A wrapped line belongs to the item above it, so a number on the second line counts.
            items[-1].append(stripped)
        else:
            continuing = False
    texts = [" ".join(parts) for parts in items]
    return [text for text in texts if text and text not in rules.placeholders]


def _check_remaining(section: str, rules: BodyRules) -> list[str]:
    """Refuse an unchecked Remaining item that names no issue or pull request.

    Work a pull request leaves undone is tracked where everyone can see it, not in a description that stops being read
    once the pull request merges. Each item carries the number of the issue (or the follow-up pull request) that holds
    it, such as `#123`; the coordinator files the issue when an agent has none.
    """
    problems: list[str] = []
    for text in _remaining_items(section, rules):
        if not _NUMBER_REFERENCE.search(text):
            shown = text if len(text) <= 60 else text[:57] + "..."
            label = rules.remaining_label.rstrip(":")
            problems.append(
                f"'## {rules.remaining_section}': the {label} item '{shown}' names no issue "
                "or pull request; add its number (for example #123), or tick it off if it is done"
            )
    return problems


def _check_local_paths(text: str) -> list[str]:
    """Report each path on the author's machine that `text` names outside code, URLs and image references.

    A published description is read by people with none of those files, so it says "the scratch folder", not where it
    is. The only local paths that belong in a draft are the image references `gh --attach` rewrites to uploads, which
    `_check_screenshots` handles.
    """
    prose = _CODE.sub(" ", text)
    prose = _MARKDOWN_IMAGE.sub(" ", prose)
    prose = _IMG_TAG.sub(" ", prose)
    prose = _URL.sub(" ", prose)
    found: list[str] = []
    for match in _LOCAL_PATH.finditer(prose):
        path = match.group(0).rstrip(".,;:!?)]}'\"")
        if path not in found:
            found.append(path)
    return [
        f"'{path}' is a local path: a description never names files or folders on your machine "
        "(say 'the scratch folder'); only images may point at local files, and gh --attach uploads those"
        for path in found
    ]


def _image_targets(section: str) -> list[str]:
    """What the Markdown images and `<img>` tags of `section` point at, in order of appearance."""
    found: list[tuple[int, str]] = []
    for image in _MARKDOWN_IMAGE.finditer(section):
        target = image.group("angle") if image.group("angle") is not None else image.group("plain")
        found.append((image.start(), target.strip()))
    for tag in _IMG_SRC.finditer(section):
        target = next(tag.group(name) for name in ("dq", "sq", "bare") if tag.group(name) is not None)
        found.append((tag.start(), target.strip()))
    return [target for _, target in sorted(found)]


def _check_screenshots(section: str, rules: BodyRules) -> list[str]:
    """Check the Screenshots section: an uploaded image or video, or the `none (reason)` line with a real reason.

    A picture counts only when it is reachable by a reader: an http(s) URL (a GitHub attachment or any other). An image
    that points at a local file would show as broken on GitHub, so it is reported with the way to upload it.
    """
    targets = _image_targets(section)
    local = [target for target in targets if not _REMOTE.match(target)]
    if local:
        return [
            f"'## {rules.screenshots_section}': '{target}' is a local file: attach it with gh --attach (gh 2.99+) "
            "so it uploads"
            for target in local
        ]
    if targets or _ATTACHMENT_LINK.search(section):
        return []
    # The label may be bold (`- **Screenshots:** none (...)`), so the asterisks are dropped before matching.
    none = re.compile(rf"^(?:[-*+]\s+)?{re.escape(rules.screenshots_none)}\s*\((?P<reason>.*)\)$", re.IGNORECASE)
    for line in section.splitlines():
        found = none.match(line.replace("**", "").strip())
        if found:
            reason = found.group("reason").strip()
            if reason and reason not in rules.placeholders:
                return []
    return [
        f"'## {rules.screenshots_section}' needs an image (an uploaded screenshot or video), "
        f"or a line '{rules.screenshots_none} (<reason>)' saying why there is nothing to show"
    ]


def check_body(body: str, rules: BodyRules) -> list[str]:
    """Check a pull request description against the template's form.

    Every section must be present as a `##` heading, the `filled` ones (Summary, Testing) must hold text of their
    own, the Screenshots section must hold an image that is an uploaded URL (not a local path) or a
    `Screenshots: none (<reason>)` line, no text may name a path on the author's machine, and the AI use
    section must say who wrote the commits (one of the choices) and, unless a person did, name the model, and every
    unchecked item under Remaining must name an issue or pull request (`#123`).
    HTML comments and unchanged placeholder text count as empty, so the untouched template is refused. Returns one
    reason per broken rule; an empty list means the description passes.
    """
    visible = strip_comments(body)
    sections = _split_sections(visible)
    problems: list[str] = []
    for name in rules.sections:
        text = sections.get(name.casefold())
        if text is None:
            problems.append(f"missing the section '## {name}'")
        elif name in rules.filled and _is_blank(text, rules):
            problems.append(f"'## {name}' is empty (a comment or the template's placeholder does not count)")
    screenshots = sections.get(rules.screenshots_section.casefold())
    if screenshots is not None:
        problems.extend(_check_screenshots(screenshots, rules))
    ai_use = sections.get(rules.ai_section.casefold())
    if ai_use is not None:
        problems.extend(_check_ai_use(ai_use, rules))
    checklist = sections.get(rules.remaining_section.casefold())
    if checklist is not None:
        problems.extend(_check_remaining(checklist, rules))
    problems.extend(_check_local_paths(visible))
    return problems
