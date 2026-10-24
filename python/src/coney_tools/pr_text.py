# SPDX-License-Identifier: GPL-3.0-or-later
"""Step 5 of a release: the pull request's title and description, generated from the engineer's file, the task and the
stamps, and checked with the checks CI runs (docs/guides/pr.md#text).

The engineer writes one small file, `PR.md` in the task's scratch folder: line 1 the title, a `## Summary` with one
paragraph (and perhaps a `Docs: none` line), a `## Testing` holding only the `Runtime:` line and a `## Screenshots`
holding image references or `Screenshots: none (reason)`. Everything else is generated: the `Fixes` lines and the
Checklist from `TASK.md`, the facts block from the stamp log, the AI use section from the commits and the log, and every
other section the template has. The generated description therefore cannot disagree with the stamps, and the sections
it has are the template's: they are read from `pull_requests.sections` in `.github/conventions.yml`, never listed here.

`generate` is pure: it reads no file but the pictures and writes none; `pr text` writes `PR-TITLE.txt` and
`PR-BODY.md`.
"""

from __future__ import annotations

import hashlib
import importlib.util
import json
import re
import subprocess
import sys
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml

from coney_tools import orig_pairs, pr_proof, stamps
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.pr_proof import Refusal, TextHashes
from coney_tools.pr_review import description_problems
from coney_tools.repo_checks import check_docs
from coney_tools.task_file import TaskFile

#: The most characters the facts block may have before its per-stage lines become counts.
FACTS_LIMIT = 8000
#: File endings of a video; a clip is uploaded by `clips publish`, never in a description.
VIDEO_EXTENSIONS = ("mp4", "webm", "mkv", "mov", "avi", "m4v")
#: The folder of the pictures, next to the engineer's file (`![alt](./shots/x.png)`).
SHOTS = "shots"
_HEADING = re.compile(r"^##[ \t]+(?P<name>\S.*?)[ \t]*$")
_IMAGE_LINE = re.compile(r"^!\[(?P<alt>[^\]\n]*)\]\((?P<ref>[^)\s]+)\)\s*$")
_ZERO_WIDTH = "​"


# --- the engineer's file --------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Image:
    """A picture the engineer's file shows."""

    #: The reference as written (`./shots/after.png`).
    ref: str
    alt: str
    #: Where it is on disk (relative to the file's folder).
    path: Path


@dataclass(frozen=True)
class EngineerText:
    """The engineer's file as data; `problems` are the ways it is not in the form (see `read_engineer_file`)."""

    folder: Path
    title: str
    #: The one paragraph, whitespace made single spaces (a wrapped line is the same paragraph).
    paragraph: str
    docs_none: bool
    #: The `Runtime:` line's text without a list marker, or None.
    runtime: str | None
    images: tuple[Image, ...]
    #: The reason of a `Screenshots: none (reason)` line, or None.
    screenshots_none: str | None
    #: A person's own `Commits written by:` and `Model:` values, when the file has an `## AI use` section.
    ai_use: tuple[str, str] | None
    problems: tuple[str, ...] = ()
    #: The text of every other section of the template that the file holds (a section the conventions add, such as
    #: "Issues affected", whose content only a person can confirm), by the template's name for it.
    extra: dict[str, str] = field(default_factory=dict)


def _sha(text: str | bytes) -> str:
    """SHA-256 of text (UTF-8) or bytes, as hex."""
    return hashlib.sha256(text.encode("utf-8") if isinstance(text, str) else text).hexdigest()


def _squash(text: str) -> str:
    """`text` with every run of whitespace made one space."""
    return " ".join(text.split())


def _sections(lines: Sequence[str]) -> dict[str, list[str]]:
    """The lines under each `##` heading of the file (the key keeps the heading's own spelling)."""
    found: dict[str, list[str]] = {}
    current: list[str] | None = None
    for line in lines:
        heading = _HEADING.match(line)
        if heading:
            current = found.setdefault(heading.group("name"), [])
        elif current is not None:
            current.append(line)
    return found


def _labelled(lines: Sequence[str], label: str) -> str | None:
    """What follows `label` on its line (a list marker and bold are ignored), or None when no line has it."""
    pattern = re.compile(rf"^[ \t]*(?:[-*+][ \t]+)?\**[ \t]*{re.escape(label)}\**[ \t]*(?P<value>.*)$", re.IGNORECASE)
    for line in lines:
        found = pattern.match(line)
        if found:
            return found.group("value").strip().strip("*_ ")
    return None


def read_engineer_file(path: Path, conventions: Conventions) -> EngineerText:
    """Parse the engineer's file at `path`; a way it is not in the form goes into `problems`, one message each.

    ConfigError when the file cannot be read. The sections the file may hold are `Summary`, `Testing`, `Screenshots`
    and (for a person's own pull request) `AI use`, and so may any section the template has besides the Checklist (a
    section only a person can fill, such as "Issues affected"); the Checklist is generated and refused here, and so is
    any heading the template does not have.
    """
    try:
        raw = path.read_bytes().decode("utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise ConfigError(f"text: {path}: cannot be read ({error})") from error
    text = raw.removeprefix("﻿").replace("\r\n", "\n").replace("\r", "\n")
    title, _, rest = text.partition("\n")
    rules = conventions.pull_requests
    name = path.name
    problems: list[str] = []
    sections = _sections(rest.split("\n"))
    summary_name = rules.closing.section
    testing_name = rules.runtime.section
    shots_name = rules.screenshots.section
    ai_name = rules.ai_use.section
    standard = {summary_name.casefold(), testing_name.casefold(), shots_name.casefold(), ai_name.casefold()}
    generated = {rules.checklist.section.casefold()}
    known = {s.casefold(): s for s in rules.sections}
    extra: dict[str, str] = {}
    for heading, lines in sections.items():
        key = heading.casefold()
        if key in standard:
            continue
        if key in generated:
            problems.append(f'text: {name}: "## {heading}" is generated; remove it')
        elif key in known:
            extra[known[key]] = "\n".join(lines).strip()
        else:
            problems.append(f'text: {name}: "## {heading}" is not a section of a pull request description')
    if not title.strip():
        problems.append(f"text: {name}: line 1 must be the title")
    paragraph, docs_none = _summary(_find(sections, summary_name), rules, name, problems)
    runtime = _runtime(_find(sections, testing_name), rules, name, problems)
    images, none = _screenshots(_find(sections, shots_name), path.parent, rules, name, problems)
    ai_lines = _find(sections, ai_name)
    ai_use = None
    if ai_lines is not None:
        written = _labelled(ai_lines, rules.ai_use.written_by.label)
        model = _labelled(ai_lines, rules.ai_use.model.label)
        ai_use = (written or "", model or "")
    return EngineerText(
        path.parent, title.strip(), paragraph, docs_none, runtime, tuple(images), none, ai_use, tuple(problems), extra
    )


def _find(sections: dict[str, list[str]], name: str) -> list[str] | None:
    """The lines of the section called `name` (any case), or None."""
    for heading, lines in sections.items():
        if heading.casefold() == name.casefold():
            return lines
    return None


def _summary(lines: list[str] | None, rules: Any, name: str, problems: list[str]) -> tuple[str, bool]:
    """The paragraph and whether a `Docs: none` line is there; anything else in the section is refused."""
    if lines is None:
        problems.append(f'text: {name}: "## {rules.closing.section}" is missing')
        return "", False
    docs_none = False
    closing = re.compile(rf"^\s*{re.escape(rules.closing.keyword)}\b", re.IGNORECASE)
    paragraphs: list[list[str]] = [[]]
    for line in lines:
        stripped = line.strip()
        if re.match(rf"^{re.escape(rules.docs_none.line)}\s*$", stripped, re.IGNORECASE):
            docs_none = True
        elif closing.match(line):
            problems.append(f'text: {name}: "{rules.closing.keyword}" lines are generated from TASK.md; remove them')
        elif stripped.startswith("<!--"):
            problems.append(f"text: {name}: the markers are generated; remove them")
        elif stripped:
            paragraphs[-1].append(stripped)
        elif paragraphs[-1]:
            paragraphs.append([])
    paragraphs = [p for p in paragraphs if p]
    if not paragraphs:
        problems.append(f'text: {name}: "## {rules.closing.section}" has no paragraph')
        return "", docs_none
    if len(paragraphs) > 1:
        problems.append(f'text: {name}: "## {rules.closing.section}" is one paragraph; found {len(paragraphs)}')
    return _squash(" ".join(paragraphs[0])), docs_none


def _runtime(lines: list[str] | None, rules: Any, name: str, problems: list[str]) -> str | None:
    """The `Runtime:` line of the Testing section; the facts are generated, so nothing else belongs there."""
    if lines is None:
        return None
    runtime: list[str] = []
    for line in lines:
        stripped = re.sub(r"^[-*+]\s+", "", line.strip()).replace("**", "")
        if not stripped:
            continue
        if stripped.startswith(rules.runtime.line):
            runtime = [stripped]
        elif runtime and line[:1] in (" ", "\t"):
            runtime.append(stripped)
        else:
            problems.append(
                f'text: {name}: "## {rules.runtime.section}" holds only the "{rules.runtime.line}" line; '
                "the facts are generated"
            )
            break
    return _squash(" ".join(runtime)) if runtime else None


def _screenshots(
    lines: list[str] | None, folder: Path, rules: Any, name: str, problems: list[str]
) -> tuple[list[Image], str | None]:
    """The image references and the `Screenshots: none (reason)` line of the Screenshots section."""
    images: list[Image] = []
    none: str | None = None
    none_line = re.compile(
        rf"^(?:[-*+]\s+)?{re.escape(rules.screenshots.none)}\s*\((?P<reason>.*)\)\s*$", re.IGNORECASE
    )
    for line in lines or ():
        stripped = line.strip()
        if not stripped:
            continue
        image = _IMAGE_LINE.match(re.sub(r"^[-*+]\s+(?:\*\*[^*]*\*\*\s*)?", "", stripped))
        reason = none_line.match(stripped.replace("**", ""))
        if image:
            ref = image.group("ref")
            images.append(Image(ref, image.group("alt"), folder / ref))
        elif reason:
            none = reason.group("reason").strip()
        else:
            problems.append(
                f'text: {name}: "## {rules.screenshots.section}" holds image references '
                f"(![alt](./{SHOTS}/file.png)) or a {rules.screenshots.none} (<reason>) line; found: {stripped[:60]}"
            )
    return images, none


def _image_hash(image: Image) -> str:
    """The hash of a picture's file, or `missing` when it is not there."""
    try:
        return _sha(image.path.read_bytes())
    except OSError:
        return "missing"


def hashes_of(text: EngineerText) -> TextHashes:
    """What a review covers of the engineer's file: the title, the paragraph and the pictures.

    The pictures' hash is the hash of the sorted list of their files' hashes, so renaming a picture does not change
    it and replacing one does. These three are what `task review` records in `reviewed.covers` and `pr verify`
    compares (`pr_proof.check_reviewed`).
    """
    pictures = "\n".join(sorted(_image_hash(i) for i in text.images))
    return TextHashes(_sha(text.title), _sha(text.paragraph), _sha(pictures))


# --- the task file's optional keys ----------------------------------------------------------------------------------


def optional_task_keys(task_file_text: str) -> dict[str, Any]:
    """The optional keys of `TASK.md`'s front block that `pr text` reads: `finishes` (false for a task that does not
    finish its issue, so no `Fixes` line is written; default true) and `parent` (a fix task's parent task, whose
    log, branch and text it uses)."""
    text = task_file_text.replace("\r\n", "\n")
    head = text.split("\n---\n", 1)[0].removeprefix("---\n")
    try:
        front = yaml.safe_load(head)
    except yaml.YAMLError:
        return {}
    if not isinstance(front, dict):
        return {}
    return {key: front[key] for key in ("finishes", "parent") if key in front}


def done_criteria(task: TaskFile) -> list[str]:
    """The bullets of the task's Done section (D7): one Checklist item each; none when it is still the placeholder."""
    items: list[str] = []
    for line in task.sections.get("Done", "").splitlines():
        match = re.match(r"^\s*[-*+]\s+(?:\[[ xX]\]\s+)?(?P<text>\S.*)$", line)
        if match:
            items.append(_squash(match.group("text")))
    return items


# --- the facts ------------------------------------------------------------------------------------------------------


def code(value: object) -> str:
    """`value` as a Markdown code span that cannot break out or mention anyone: a backtick becomes an apostrophe, an
    `@` gets a zero-width space after it, a line break becomes a space. Names from stamps and pairs are not trusted."""
    text = _squash(str(value)).replace("`", "'").replace("@", "@" + _ZERO_WIDTH)
    return f"`{text}`" if text else "``"


def human_model(model: str) -> str:
    """A model id as a person writes it: `claude-opus-5-5` is `Claude Opus 5.5`; anything else is kept."""
    found = re.fullmatch(r"claude-([a-z]+)-(\d+)-(\d+)", model)
    if found:
        return f"Claude {found.group(1).capitalize()} {found.group(2)}.{found.group(3)}"
    return model


def _h(stamp: stamps.Stamp | None, size: int = 12) -> str:
    """A stamp's hash cut for the facts."""
    return stamp.hash[:size] if stamp else "none"


def _newest_pass(subject: pr_proof.Subject, step: str, subject_name: str | None = None) -> stamps.Stamp | None:
    """The newest stamp of `step` when it passed."""
    stamp = subject.report.latest(step, subject_name)
    return stamp if stamp is not None and stamp.result == "pass" else None


def _stand_ins(subject: pr_proof.Subject) -> list[str]:
    """One line per stand-in and not-ported pair of the changed functions."""
    lines: list[str] = []
    for unit in subject.changed:
        for address in pr_proof._addresses(unit):
            try:
                file = orig_pairs.load(subject.unit.mapping(address))
            except ConfigError:
                continue
            for pair in file.pairs:
                if pair.relation == "stand-in":
                    lines.append(f"- {code(pair.coney)} {code(pair.note)}")
                elif pair.relation == "not-ported":
                    lines.append(f"- not ported ({code(pair.issue)}): {code(address)} lines {code(pair.lines)}")
    return lines


def _stage_lines(subject: pr_proof.Subject, short: bool) -> list[str]:
    """The per-stage lines of the facts; with `short` each stage is one count (the long form is over the limit)."""
    lines: list[str] = []
    studied, compared = [], []
    for unit in subject.changed:
        for address in pr_proof._addresses(unit):
            studied.append((address, _newest_pass(subject, "studied", address)))
            compared.append((address, _newest_pass(subject, "compared", address)))
    if short:
        if studied:
            lines.append(f"- Studied {sum(1 for _, s in studied if s)} of {len(studied)} functions")
            lines.append(f"- Compared {sum(1 for _, s in compared if s)} of {len(compared)} functions")
    else:
        for address, stamp in studied:
            name = stamp.detail.get("name", "") if stamp else ""
            lines.append(
                f"- Studied {code(address)} {code(name)} (stamp `{_h(stamp)}`)"
                if stamp
                else f"- Not studied {code(address)}"
            )
        for address, stamp in compared:
            if stamp is None:
                lines.append(f"- Not compared {code(address)}")
                continue
            d = stamp.detail
            counts = (
                f"{d.get('pairs', 0)} pairs ({d.get('same', 0)} same, {d.get('stand-in', 0)} stand-in, "
                f"{d.get('not-ported', 0)} not-ported)"
            )
            pairs_hash = str(stamp.covers.get("pairs_sha256", ""))[:12]
            lines.append(f"- Compared {code(address)}: {counts} (stamp `{_h(stamp)}`, pairs `{pairs_hash}`)")
    firsts = {s.subject: s for s in subject.report.stamps if s.step == "fails-first"}
    passed = [s for s in firsts.values() if s.result == "pass"]
    nones = [s for s in firsts.values() if s.result == "none"]
    if short and passed:
        lines.append(f"- Failed first: {len(passed)} tests")
    elif not short:
        for stamp in passed:
            lines.append(
                f"- Failed first: {code(stamp.subject)} failed before the fix, passes after (stamp `{_h(stamp)}`)"
            )
    for stamp in nones:
        lines.append(f"- No tests that failed first ({subject.kind}): {code(stamp.detail.get('reason', ''))}")
    checked = _newest_pass(subject, "checked")
    if checked is None:
        lines.append("- Check: not run")
    else:
        d = checked.detail
        tests = d.get("tests") if isinstance(d.get("tests"), dict) else {}
        record = checked.covers.get("build_record_sha256")
        extra = f", build record `{record[:12]}`" if record else ""
        lines.append(
            f"- Check: profile {code(d.get('profile', ''))}, preset {code(d.get('preset', ''))}, "
            f"{tests.get('passed', 0)} passed, "
            f"{tests.get('failed', 0)} failed, {tests.get('skipped', 0)} skipped (stamp `{_h(checked)}`{extra})"
        )
    reviewed = _newest_pass(subject, "reviewed")
    if reviewed is None:
        lines.append("- Review: not reviewed")
    else:
        functions = reviewed.detail.get("functions")
        classes: dict[str, int] = {}
        for value in functions.values() if isinstance(functions, dict) else ():
            classes[str(value)] = classes.get(str(value), 0) + 1
        reviewer = reviewed.detail.get("reviewer")
        model = human_model(str(reviewer.get("model", "unknown"))) if isinstance(reviewer, dict) else "unknown"
        total = sum(classes.values())
        noun = f"{total} function{'s' if total != 1 else ''}"
        if not classes:
            counted = "no functions"
        elif len(classes) == 1:
            counted = f"{noun} {next(iter(classes))}"
        else:
            counted = f"{noun}: " + ", ".join(f"{n} {name}" for name, n in sorted(classes.items()))
        lines.append(f"- Review: pass, {model}, {counted} (stamp `{_h(reviewed)}`)")
    return lines


def facts_block(subject: pr_proof.Subject) -> str:
    """The facts: which stages passed, with their hashes (cut to 12 digits), from the local log alone.

    A record for the reader, not proof: nothing in CI reads it. The same unit gives the same bytes. Over `FACTS_LIMIT`
    characters the per-stage lines become counts and the stand-ins a count.
    """
    head = (
        f"Stages (local log of {code(subject.task)}, chain head `{subject.report.head[:12]}`; "
        f"change `diff {subject.diff_sha256[:12]}` on main `{subject.base[:12]}`):"
    )
    stand_ins = _stand_ins(subject)
    long = "\n".join([head, *_stage_lines(subject, False), *(["Stand-ins:", *stand_ins] if stand_ins else [])])
    if len(long) <= FACTS_LIMIT:
        return long
    note = (
        [f"Stand-ins and not-ported pieces: {len(stand_ins)} (listed in the local log's pairs files)"]
        if stand_ins
        else []
    )
    return "\n".join([head, *_stage_lines(subject, True), *note])[:FACTS_LIMIT]


# --- the whole description ------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Generated:
    """The title and description `pr text` made, and the hashes `pr-opened` records."""

    title: str
    body: str
    facts: str
    text_hashes: TextHashes
    facts_sha256: str
    refusals: tuple[Refusal, ...] = ()

    @property
    def ok(self) -> bool:
        """True when every check passed."""
        return not self.refusals


@dataclass
class _Context:
    """What a section's builder reads."""

    subject: pr_proof.Subject
    task: TaskFile
    text: EngineerText
    keys: dict[str, Any]
    models: Sequence[str]
    facts: str
    extra: dict[str, str] = field(default_factory=dict)


#: The module of `coney-tools issue impact`; without it the "Issues affected" section has no suggestion to offer.
IMPACT_MODULE = "coney_tools.issue_impact"
#: The text of a section nothing generates (yet); a check refuses a description that still holds it.
TODO = "TODO:"

#: Runs `coney-tools issue impact --json` for a branch and returns its report; raises ConfigError when it fails.
ImpactRunner = Callable[[Path, str, str], dict[str, Any]]


def _run_impact(root: Path, base: str, tip: str) -> dict[str, Any]:
    """The report of `coney-tools issue impact --base BASE --head TIP --json`, run in `root`."""
    command = [sys.executable, "-m", "coney_tools.cli", "issue", "impact", "--base", base, "--head", tip, "--json"]
    done = subprocess.run(command, cwd=root, capture_output=True, encoding="utf-8", check=False)
    if done.returncode != 0:
        said = (done.stderr or done.stdout).strip().splitlines()
        raise ConfigError(said[-1] if said else "it failed")
    try:
        report = json.loads(done.stdout)
    except ValueError as error:
        raise ConfigError(f"its output is not JSON ({error})") from error
    if not isinstance(report, dict):
        raise ConfigError("its output is not a JSON object")
    return report


def issues_affected(root: Path, base: str, tip: str, run: ImpactRunner | None = None) -> str | None:
    """The text for the "Issues affected" section when the engineer's file has none: the candidate issues from
    `coney-tools issue impact` as a TODO that asks the engineer to confirm them, since a suggestion is never a verdict.

    None when that command does not exist (the section is then a bare TODO). The command reads GitHub, so a failure to
    run it is also said in the TODO, never raised.
    """
    if importlib.util.find_spec(IMPACT_MODULE) is None:
        return None
    ask = (
        f"{TODO} write this section in PR.md: one line `- #N: how it is affected` for each open issue this change "
        "affects, or `None (<reason>)`."
    )
    try:
        report = (run or _run_impact)(root, base, tip)
    except ConfigError as error:
        return f"{ask} `coney-tools issue impact` could not run ({error})."
    lines = [ask]
    for item in report.get("issues", []):
        reasons = item.get("reasons") or [{}]
        first = reasons[0].get("detail") or reasons[0].get("what") or ""
        lines.append(
            f"- {code('#' + str(item.get('number')))} {code(item.get('title', ''))} (candidate: {code(first)})"
        )
    if len(lines) == 1:
        lines.append("`coney-tools issue impact` found no candidate.")
    return "\n".join(lines)


#: Sections whose text comes from a function, by the section's name; any other section of the template that is not a
#: standard one gets the TODO text.
_EXTRA_SECTIONS: dict[str, Callable[[pr_proof.Subject], str | None]] = {
    "Issues affected": lambda subject: issues_affected(subject.root, subject.base, subject.tip),
}


def _marked(name: str, content: str) -> str:
    """`content` between the `<!-- name -->` and `<!-- /name -->` lines."""
    return f"<!-- {name} -->\n{content}\n<!-- /{name} -->"


def _summary_section(ctx: _Context) -> str:
    rules = ctx.subject.conventions.pull_requests
    parts = [_marked(rules.markers.summary, ctx.text.paragraph)]
    if ctx.text.docs_none:
        parts.append(rules.docs_none.line)
    if ctx.keys.get("finishes", True) is not False:
        parts.append("\n".join(f"{rules.closing.keyword} #{issue}" for issue in ctx.task.issues))
    return "\n\n".join(parts)


def _checklist_section(ctx: _Context) -> str:
    items = done_criteria(ctx.task)
    return "Done:\n\n" + "\n".join(f"- [x] {item}" for item in items) if items else ""


def _testing_section(ctx: _Context) -> str:
    rules = ctx.subject.conventions.pull_requests
    parts = [_marked(rules.markers.facts, ctx.facts)]
    if ctx.text.runtime:
        parts.append(f"- {ctx.text.runtime}")
    return "\n\n".join(parts)


def _screenshots_section(ctx: _Context) -> str:
    rules = ctx.subject.conventions.pull_requests
    lines = [f"![{image.alt}]({image.ref})" for image in ctx.text.images]
    if ctx.text.screenshots_none is not None and not lines:
        lines = [f"{rules.screenshots.none} ({ctx.text.screenshots_none})"]
    return "\n\n".join(lines)


def _ai_section(ctx: _Context) -> str:
    rules = ctx.subject.conventions.pull_requests.ai_use
    if ctx.models:
        written, model = rules.written_by.choices[1], ", ".join(ctx.models)
    elif ctx.text.ai_use is not None:
        written, model = ctx.text.ai_use
    else:
        written, model = "", ""
    return f"- **{rules.written_by.label}** {written}\n- **{rules.model.label}** {model}".rstrip()


def models_of(subject: pr_proof.Subject, trailer_models: Sequence[str]) -> list[str]:
    """The models that wrote the work: the ones the branch's `Co-Authored-By` trailers name, else the models of the
    engineers in the log (an id like `claude-opus-5-5` written as a person writes it). Empty for a person's own work."""
    if trailer_models:
        return list(trailer_models)
    found: dict[str, str] = {}
    for stamp in subject.report.stamps:
        agent = stamp.who.get("agent")
        if isinstance(agent, dict) and agent.get("type") == "engineer" and agent.get("model") not in (None, "unknown"):
            name = human_model(str(agent["model"]))
            found.setdefault(name.casefold(), name)
    return list(found.values())


def generate(
    subject: pr_proof.Subject,
    task: TaskFile,
    text: EngineerText,
    *,
    keys: dict[str, Any] | None = None,
    trailer_models: Sequence[str] = (),
) -> Generated:
    """The title and description for `subject`, with every check run on them.

    The sections come from `pull_requests.sections`, in the template's order. The five standard ones are built from
    the engineer's file, `TASK.md` and the stamps; any other section is `_EXTRA_SECTIONS` text, or a TODO that the
    checks refuse, so adding a section to the template is a change to the conventions and, if anything fills it, one
    entry in `_EXTRA_SECTIONS`.
    """
    rules = subject.conventions.pull_requests
    ctx = _Context(subject, task, text, keys or {}, models_of(subject, trailer_models), facts_block(subject))
    standard: dict[str, Callable[[_Context], str]] = {
        rules.closing.section: _summary_section,
        rules.checklist.section: _checklist_section,
        rules.runtime.section: _testing_section,
        rules.screenshots.section: _screenshots_section,
        rules.ai_use.section: _ai_section,
    }
    blocks: list[str] = []
    for name in rules.sections:
        builder = standard.get(name)
        if builder is not None:
            content = builder(ctx)
        else:
            written = text.extra.get(name, "")
            producer = _EXTRA_SECTIONS.get(name)
            produced = written or (producer(subject) if producer else None)
            content = produced if produced else f"{TODO} nothing fills this section yet"
        blocks.append(f"## {name}\n\n{content}".rstrip())
    body = "\n\n".join(blocks) + "\n"
    extra = []
    if ctx.models and text.ai_use is not None:
        extra.append(f'text: PR.md: "## {rules.ai_use.section}" is generated from the log and the commits; remove it')
    refusals = tuple(Refusal("text", p, "engineer") for p in (*text.problems, *extra)) + tuple(
        check(subject, text, ctx.models, body)
    )
    return Generated(text.title, body, ctx.facts, hashes_of(text), _sha(ctx.facts), refusals)


# --- the checks ----------------------------------------------------------------------------------------------------


def check(subject: pr_proof.Subject, text: EngineerText, models: Sequence[str], body: str) -> list[Refusal]:
    """The checks CI runs on the title and body (`check_title`, `check_body` with a local picture accepted, as the old
    description gate ran them), the `Docs:` rule, a section still left as a TODO, and the pictures."""
    conventions = subject.conventions
    found: list[Refusal] = []
    for problem in description_problems(f"{text.title}\n{body}", conventions, models):
        found.append(Refusal("text", problem, "engineer"))
    for problem in check_docs(list(subject.paths), body, conventions.paths, conventions.pull_requests.docs_none):
        found.append(Refusal("text", f"docs: {problem}", "engineer"))
    for name, content in _section_texts(body).items():
        if content.lstrip().startswith(TODO):
            found.append(Refusal("text", f"body: '## {name}' is not filled in ({content.strip()[:60]})", "engineer"))
    found += [Refusal("text", p, "engineer") for p in image_problems(text, conventions)]
    return found


def _section_texts(body: str) -> dict[str, str]:
    """The text under each `##` heading of a generated body."""
    return {name: "\n".join(lines) for name, lines in _sections(body.split("\n")).items()}


def _inside_checkout(path: Path) -> bool:
    """Whether `path` lies in a git checkout (a folder with a `.git` above it)."""
    return any((parent / ".git").exists() for parent in path.resolve().parents)


def image_problems(text: EngineerText, conventions: Conventions) -> list[str]:
    """What is wrong with the pictures: missing, not under the shots folder, in a checkout, a clip, too big, not a
    picture type, two byte-identical, or too many. One message each, with the `images:` prefix."""
    rules = conventions.pull_requests.images
    found: list[str] = []
    seen: dict[str, str] = {}
    shots = (text.folder / SHOTS).resolve()
    if len(text.images) > rules.max_count:
        found.append(f"images: {len(text.images)} pictures; at most {rules.max_count}")
    for image in text.images:
        label = image.ref.removeprefix("./")
        path = image.path.resolve()
        if shots not in path.parents:
            found.append(f"images: {label} is not under {SHOTS}/ next to PR.md")
            continue
        if not path.is_file():
            found.append(f"images: {label} does not exist")
            continue
        extension = path.suffix.lower().lstrip(".")
        if extension in VIDEO_EXTENSIONS:
            found.append(f"images: {label} is a video; use clips publish")
            continue
        if extension not in rules.extensions:
            found.append(f"images: {label} is not a picture ({', '.join(rules.extensions)})")
            continue
        if _inside_checkout(path):
            found.append(f"images: {label} is inside a git checkout; game pictures stay in the scratch folder")
        size = path.stat().st_size
        if size > rules.max_bytes:
            found.append(f"images: {label} is {size // (1024 * 1024)} MB (limit {rules.max_bytes // (1024 * 1024)})")
        digest = _sha(path.read_bytes())
        if digest in seen:
            found.append(f"images: {label} is identical to {seen[digest]}")
        else:
            seen[digest] = label
    return found
