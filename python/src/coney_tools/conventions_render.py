# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools repo render`: the generated blocks that show `.github/conventions.yml` in AGENTS.md and the guides.

Agents read AGENTS.md, CONTRIBUTING.md and the guides as raw files, so a list of the conventions (the commit areas,
the labels, the rules for a pull request description) is committed text, not filled in when the docs site is built.
A block sits between two marker lines and `repo render` rewrites everything between them from the conventions:

    <!-- conventions:begin commit-areas -->
    ... generated; never edited by hand ...
    <!-- conventions:end -->

`repo render --check` changes nothing and fails when a block is stale, as CI does. Markers inside a fenced code block
are an example and are left alone. The reasons for the design are in docs/guides/repo-conventions.md#generated-blocks.
"""

from __future__ import annotations

import re
import sys
import textwrap
from collections.abc import Callable
from pathlib import Path
from typing import NamedTuple

from coney_tools.config import ConfigError
from coney_tools.conventions import CI_WHERE, Conventions, load_conventions

#: The docs' line length (.markdownlint.yaml, MD013); generated prose is wrapped to it. Tables are not.
WIDTH = 120
#: The files outside docs/guides/ that may hold blocks.
ROOT_FILES = ("AGENTS.md", "CONTRIBUTING.md")
#: The folder whose Markdown files (at any depth) may hold blocks.
GUIDES_DIR = "docs/guides"

_BEGIN = re.compile(r"^<!-- conventions:begin ([a-z][a-z0-9-]*) -->$")
_END = "<!-- conventions:end -->"
#: A Markdown code span without a backtick inside; its spaces must not become line breaks.
_CODE_SPAN = re.compile(r"`[^`\n]*`")
#: Stands in for a space inside a code span while a paragraph is wrapped.
_HOLD = "\x00"


class BlockError(ConfigError):
    """A marker in a file is wrong (unknown block, no end, an end without a begin); the message names file and line."""


# --- wrapping and small helpers -------------------------------------------------------------------------------------


def _wrap(text: str, first: str = "", rest: str = "") -> list[str]:
    """`text` as lines of at most WIDTH characters, never breaking inside a code span.

    `first` starts the first line (a bullet marker) and `rest` the others.
    """
    held = _CODE_SPAN.sub(lambda span: span.group(0).replace(" ", _HOLD), " ".join(text.split()))
    lines = textwrap.wrap(
        held, WIDTH, initial_indent=first, subsequent_indent=rest, break_long_words=False, break_on_hyphens=False
    )
    return [line.replace(_HOLD, " ") for line in lines]


def _bullet(text: str) -> list[str]:
    """One wrapped bullet of a list."""
    return _wrap(text, "- ", "  ")


def _codes(values: tuple[str, ...] | list[str]) -> str:
    """Each value as a code span, comma separated."""
    return ", ".join(f"`{value}`" for value in values)


def _join_and(values: list[str]) -> str:
    """`a`, `a and b`, `a, b and c`."""
    if len(values) <= 1:
        return "".join(values)
    return ", ".join(values[:-1]) + " and " + values[-1]


def _join_or(values: list[str]) -> str:
    """`a`, `a or b`, `a, b or c`."""
    if len(values) <= 1:
        return "".join(values)
    return ", ".join(values[:-1]) + " or " + values[-1]


def _cell(text: str) -> str:
    """Text safe inside a table cell."""
    return text.replace("|", "\\|")


# --- the blocks -----------------------------------------------------------------------------------------------------


def _commit_rules(conventions: Conventions) -> list[str]:
    """The commit title and message rules, as two bullets (AGENTS.md)."""
    title = conventions.commits.title
    trailers = conventions.commits.trailers
    co_author = trailers.ai_co_author
    sentence = f"Title `{title.form}`, at most {title.max_length} characters, present tense"
    if not title.trailing_period:
        sentence += ", no trailing period"
    sentence += f". Verbs: {', '.join(title.verbs)}. Areas: {_codes(list(title.areas))}."
    sentence += f" A breaking change ends the title with `{title.breaking_suffix}`."
    sentence += (
        " What each area covers: `commits.title` in `.github/conventions.yml`, which CI checks PR titles against."
    )
    trailer = (
        f"A body says what changed for a user or developer and why, wrapped at {conventions.commits.body.wrap} "
        f"columns. An agent ends the message with `{co_author.key}: {co_author.value}` "
        f"(the vendor's noreply address: {_codes(co_author.noreply)})"
    )
    if trailers.only_ai_co_author:
        trailer += " and no other trailer"
    trailer += "."
    if not trailers.other_emails:
        trailer += " No other email address appears in a message."
    return _bullet(sentence) + _bullet(trailer)


def _commit_areas(conventions: Conventions) -> list[str]:
    """The areas with what each covers, and the verbs (CONTRIBUTING.md, review/commits.md)."""
    title = conventions.commits.title
    lines = ["| Area | Covers |", "| --- | --- |"]
    lines += [f"| `{area}` | {_cell(covers)} |" for area, covers in title.areas.items()]
    return [*lines, "", *_wrap(f"Verbs: {', '.join(title.verbs)}.")]


#: A label family's heading in the table, by the name `Labels.family` gives it.
_FAMILIES = (
    ("type", "Type"),
    ("area", "Area"),
    ("priority", "Priority"),
    ("status", "Status"),
    ("other", "Other"),
)


def _labels(conventions: Conventions) -> list[str]:
    """Every label with its family and description (docs/guides/issues.md)."""
    labels = conventions.labels
    lines = ["| Family | Label | Meaning |", "| --- | --- | --- |"]
    for family, heading in _FAMILIES:
        for number, label in enumerate(label for label in labels.list if labels.family(label.name) == family):
            family_cell = f" {heading} |" if number == 0 else " |"  # an empty cell is one space (MD060)
            lines.append(f"|{family_cell} `{label.name}` | {_cell(label.description)} |")
    return lines


def _pr_rules(conventions: Conventions) -> list[str]:
    """What CI checks in a pull request description (CONTRIBUTING.md, docs/guides/testing.md)."""
    rules = conventions.pull_requests
    paths = conventions.paths
    docs_none = rules.docs_none
    needs = getattr(paths, docs_none.when_changed)
    satisfies = getattr(paths, docs_none.unless_changed)
    lines = _bullet(f"Sections, in this order: {', '.join(rules.sections)}.")
    lines += _bullet(
        f"{_join_and(list(rules.filled))} must hold text of their own: a section left as the template has it "
        f"(its comments, {_codes(rules.placeholders)}) counts as empty."
    )
    lines += _bullet(
        f"Docs: a change under {_codes(needs)} needs a change under {_codes(satisfies)} or the line "
        f"`{docs_none.line}` in the description."
    )
    lines += _bullet(
        f"Closing: a line `{rules.closing.form}` in {rules.closing.section} for each issue the pull request finishes"
        f"{', one per line' if rules.closing.one_per_line else ''}; merging closes it."
    )
    lines += _bullet(
        f"{rules.checklist.section}: every item is ticked (`- [x]`): a pull request lists only the work it finishes, "
        "and a task it does not finish is left out and lives in its own issue."
    )
    affected = rules.issues_affected
    lines += _bullet(
        f"{affected.section}: one line `- #N: how it is affected` for each open issue the pull request partly fixes, "
        f"changes the behaviour of or makes out of date, or the single line `{affected.none} (<reason>)` with any "
        f"short reason, for example `{affected.example}`. `coney-tools issue impact` suggests the list; running it "
        "is optional."
    )
    shots = rules.screenshots
    lines += _bullet(
        f"{shots.section}: "
        + ("an uploaded image (a path on your machine is refused)" if shots.uploaded_only else "an image")
        + f" or a line `{shots.none} (<reason>)`"
        + (" that gives a reason." if shots.reason == "required" else ".")
    )
    runtime = rules.runtime
    lines += _bullet(
        f"{runtime.section}: when the title's area is one a player sees ({_codes(runtime.areas)}), a line starting "
        f"`{runtime.line}` with the evidence from the original running (a trace diff, an event diff, or named PCSX2 "
        f"captures or reads compared with Coney's, with numbers) or `{runtime.none} (<reason>)`. It is checked given "
        "the title (CI's `body` job and `repo check-review` give it)."
    )
    ai = rules.ai_use
    lines += _bullet(
        f"{ai.section}: after `{ai.written_by.label}` one of {_codes(ai.written_by.choices)}; unless it is "
        f"`{ai.written_by.no_ai}`, `{ai.model.label}` names the model (`{ai.model.none}` is not enough)."
    )
    if ai.model.names_trailer_models:
        lines += _bullet(
            f"`{ai.model.label}` also names every model that a `{conventions.commits.trailers.ai_co_author.key}` "
            "trailer on the branch's commits names (any case, anywhere on the line), whoever wrote them. It is checked "
            "where the branch is at hand: `repo check-review`, and `repo check-body` given `--worktree`; CI's `body` "
            "job sees only the description."
        )
    if not rules.local_paths:
        lines += _bullet(
            "The text names no path on your machine (`scratch/`, `../../`, `C:\\`) outside an image reference."
        )
    return lines


def _pr_review(conventions: Conventions) -> list[str]:
    """The release gate on the reviewer's verdict (docs/guides/how-we-work.md, the release queue)."""
    review = conventions.pull_requests.review
    lines = _bullet(
        f"The `{review.agent}` agent (`.claude/agents/{review.agent}.md`) writes `{review.verdict_file}` and "
        f"`{review.report_file}` next to the description file: `{review.verdict_file}` holds `tip`, `body_sha256`, "
        "`verdict` (`pass` or `fail`), `images` (each local image's `path` and `sha256`) and `findings` (each with "
        "`severity` `blocker` or `note`, `where` and `text`)."
    )
    lines += _bullet(
        "`repo check-review` passes only when the description's title (line 1) and body (the rest) pass `repo "
        "check-title` and `repo check-body` (a local image is allowed, as `gh --attach` uploads it), the verdict is "
        "`pass` with no `blocker` finding, `tip` is the worktree's HEAD, `body_sha256` is the description file's "
        "hash now and every local image the description shows is listed with its hash now; any new commit, edit or "
        "replaced picture makes the verdict stale."
    )
    if review.body_hash == "sha256-lf":
        lines += _bullet(
            "The description's hash is the SHA-256 of the whole file (title line included) as UTF-8, with a leading "
            "BOM dropped and CRLF or CR turned into LF."
        )
    if review.identical_images == "refused":
        lines += _bullet("Two byte-identical images in one description are refused, whatever the verdict.")
    return lines


#: What GitHub's squash settings mean in words.
_SQUASH_MESSAGES = {
    "PR_BODY": "the pull request's description",
    "COMMIT_MESSAGES": "the branch's commit messages",
    "BLANK": "empty",
}


def _required_checks(conventions: Conventions) -> list[str]:
    """What GitHub requires before a pull request merges (docs/guides/building.md)."""
    merge = conventions.merge
    lines: list[str] = []
    if merge.methods == ("squash",):
        lines += _bullet(
            "Merge method: squash only. The commit on `main` has the pull request's title; its message is "
            f"{_SQUASH_MESSAGES[merge.squash_message]}."
        )
    else:
        lines += _bullet(f"Merge methods: {', '.join(merge.methods)}.")
    lines += _bullet(f"Required checks: {_codes(merge.required_checks)}.")
    settings = [
        "auto-merge is " + ("on" if merge.auto_merge else "off"),
        "the branch is " + ("deleted" if merge.delete_branch_on_merge else "kept") + " after the merge",
        "history is " + ("linear" if merge.linear_history else "not required to be linear"),
        "a branch " + ("must" if merge.up_to_date_required else "need not") + " be up to date with `main`",
    ]
    lines += _bullet(f"{_join_and(settings).capitalize()}.")
    lines += _bullet(f"Who merges: {_join_and(list(merge.who))}, never the agent that wrote the work.")
    return lines


def _maintainer_decisions(conventions: Conventions) -> list[str]:
    """The decisions only a maintainer takes, as one bullet (AGENTS.md, docs/guides/how-we-work.md)."""
    decisions = "; ".join(conventions.repo.maintainer_decisions)
    return _bullet(
        "**Maintainer decisions** are never an agent's or a coordinator's: it stops and reports instead of acting. "
        f"They are: {decisions}."
    )


def _scope_paths(patterns: tuple[str, ...]) -> list[str]:
    """One CI scope list of `paths`, each pattern with what it matches (docs/guides/building.md)."""
    lines: list[str] = []
    for pattern in patterns:
        folder, _, name = pattern.rpartition("/")
        if pattern.endswith("/"):
            meaning = "the folder and everything under it"
        elif "*" in name and folder:
            meaning = f"matching files directly in `{folder}/`"
        elif "*" in name:
            meaning = "matching files at the top level only"
        else:
            meaning = "that file"
        lines.append(f"- `{pattern}`: {meaning}")
    return lines


def _claim_rules(conventions: Conventions) -> list[str]:
    """The numbers and names of claiming an issue (docs/guides/issues.md#claiming)."""
    claims = conventions.claims
    limits = claims.limits
    lines = _bullet(
        f"An issue can be claimed only when it carries `{claims.claimable_status}`, whoever claims it. A claim sets "
        f"`{claims.working_label}` (`{claims.review_label}` while its pull request is open); a release or "
        f"`/unclaim` sets `{claims.released_label}`."
    )
    lines += _bullet(
        f"A maintainer (someone with {_join_or([f'`{name}`' for name in claims.maintainer_permissions])} permission "
        f"on the repository) holds {limits.maintainer} open claims at most, a contributor {limits.contributor}. "
        f"Issues whose cards share a `task:` id count as one claim, and a task holds {limits.issues_per_task} issues "
        "at most."
    )
    lines += _bullet(
        f"The claim of an issue labelled `{claims.exempt_label}` (a unit that builds the new workflow) neither counts "
        "toward that limit nor is refused by it; the cap of a task still holds. Only someone who may label issues "
        "(triage access or more) can set the label."
    )
    lines += _bullet(
        f"While an open `{claims.critical_label}` issue that can be claimed has no claim, only a "
        f"`{claims.critical_label}` issue can be claimed."
    )
    lines += _bullet(
        f"A claimed issue that carries {_codes(claims.paused_labels)} keeps that label; the claim stays and its "
        "release clock waits, as it does while the claimant's pull request is open."
    )
    lines += _bullet(
        f"{_codes(claims.hold_labels)} also takes the place of the claim's own label: while it is on, the claimed "
        f"issue loses `{claims.working_label}` (or `{claims.review_label}`), and when it comes off while the claim "
        "stands, that label comes back."
    )
    state = "on" if claims.release else "off until the cutover"
    lines += _bullet(
        f"Release is {state}: a claim is released after {claims.deadline_hours} hours without activity (`/progress`, a "
        f"pull request opened, reopened, made ready or pushed to by the claimant, `/claim renew`); the claimant is "
        f"mentioned at {claims.warn_hours} hours, and a claim is never released sooner than "
        f"{claims.deadline_hours - claims.warn_hours} hours after that warning. The stages `/progress` accepts: "
        f"{_codes(claims.stages)}; a passing stage stamp posts it, the same stage again after "
        f"{claims.progress_refresh_hours} hours."
    )
    lines += _bullet(
        f"An issue labelled `{claims.trial.label}` (a throwaway test issue) has adoption and release on whatever the "
        f"switches say, with a warning at {claims.trial.warn_hours} and a release at "
        f"{claims.trial.deadline_hours} hours."
    )
    pr_check = "fails" if claims.require_on_pr else "only warns until the cutover"
    lines += _bullet(
        "The `claim` check of a pull request wants every issue it closes (`Fixes #N`) held by a claim of its author "
        f"(a maintainer's: by any claim); a problem {pr_check}."
    )
    adopt = "on" if claims.adopt else "off until the cutover"
    lines += _bullet(
        f"Adoption is {adopt}: the sweep gives a card (and the deadline) to an issue assigned by hand. The text after "
        f"`scope:` is at most {claims.scope_max_characters} characters, and at most {claims.closing_per_event} "
        "`Fixes #N` lines of one pull request event are acted on."
    )
    lines += _bullet(
        f"The sweep runs on the cron `{claims.sweep}` (UTC): it repairs cards, answers unhandled commands, releases "
        "claims over the limit and reports issues in progress with nobody assigned."
    )
    lines += _bullet(
        "Claim jobs run one at a time for the whole repository; each also handles the commands other issues received "
        f"in the last {claims.catch_up_minutes} minutes, so the command of a job GitHub cancelled while it waited "
        "is usually answered by the next one, and otherwise by the next sweep that runs."
    )
    return lines


def _audit_checks(conventions: Conventions) -> list[str]:
    """The checks `coney-tools repo audit` runs (`hygiene.checks`) as a table: scope, severity, fix class, finds."""
    lines = ["| Check | Scope | Severity | Fix | Finds |", "| --- | --- | --- | --- | --- |"]
    for spec in conventions.hygiene.checks.values():
        lines.append(f"| `{spec.id}` | {spec.scope} | {spec.severity} | {spec.fix} | {spec.summary} |")
    return lines


#: How a place's CI `where` words read in the table, after "CI:".
_CI_PLACE_WORDS = {
    "ci": "every pull request",
    "main": "a push to main",
    "release": "a push to a release branch",
    "nightly": "nightly",
    "manual": "a manual run",
    "issues": "issue and pull request events",
}


def _place_where(where: tuple[str, ...]) -> str:
    """Where a check runs, in one phrase: `local`, then `CI: ...` with its CI words in their usual order, each once."""
    parts = ["local"] if "local" in where else []
    ci_words = [_CI_PLACE_WORDS[name] for name in CI_WHERE if name in where and name != "local"]
    if ci_words:
        parts.append("CI: " + ", ".join(ci_words))
    return "; ".join(parts)


def _check_places(conventions: Conventions) -> list[str]:
    """One place per check (`ci.places`) as a table: the check, where it runs and the jobs behind it."""
    lines = ["| Check | Where | CI jobs |", "| --- | --- | --- |"]
    for place in conventions.ci.places:
        where = _place_where(place.where)
        where += f" ({place.note})" if place.note else ""
        jobs = ", ".join(f"`{job}`" for job in place.jobs) or "none"
        lines.append(f"| {place.check} | {where} | {jobs} |")
    return lines


def _research_pages(conventions: Conventions) -> list[str]:
    """The kinds of research page as a table (sections, limit, front matter), and the limits that apply to all."""
    research = conventions.research
    lines = ["| Kind | Sections, in order | Lines | `system` | `owns` |", "| --- | --- | --- | --- | --- |"]
    for kind in research.kinds.values():
        sections = ", ".join(f"*{name}* (optional)" if name in kind.optional else name for name in kind.sections)
        lines.append(f"| `{kind.name}` | {sections} | {kind.max_lines:,} | {kind.system} | {kind.owns} |")
    lines.append("")
    aliases = [f"`{old}` for {new}" for old, new in research.aliases.items()]
    lines += _bullet(f"Old section names are accepted as the new ones: {_join_and(aliases)}.")
    kind_aliases = [
        f"`{old}` for {new} on a `{kind.name}` page"
        for kind in research.kinds.values()
        for old, new in kind.aliases.items()
    ]
    if kind_aliases:
        lines += _bullet(f"And {_join_and(kind_aliases)}.")
    lines += _bullet(
        f"The `Verified against:` line is one line of at most {research.verified_max_chars} characters. A paragraph of "
        f"{research.copy_min_chars} characters or more written on two research pages is a copy."
    )
    lines += _bullet(f"An address needs no level in {_join_and(list(research.ungraded_sections))}.")
    lines += _bullet(f"A date or an issue number may stand only in {_join_and(list(research.dated_sections))}.")
    lines += _bullet(
        f"A page with no front matter is read as `{research.inferred_default}`"
        + "".join(f", or `{kind}` under `{folder}`" for folder, kind in research.inferred_folders.items())
        + "."
    )
    return lines


#: Block name to what fills it. The names appear in the marker lines of the files.
BLOCKS: dict[str, Callable[[Conventions], list[str]]] = {
    "commit-rules": _commit_rules,
    "commit-areas": _commit_areas,
    "labels": _labels,
    "pr-rules": _pr_rules,
    "pr-review": _pr_review,
    "required-checks": _required_checks,
    "maintainer-decisions": _maintainer_decisions,
    "docs-only-paths": lambda conventions: _scope_paths(conventions.paths.docs_only),
    "tools-only-paths": lambda conventions: _scope_paths(conventions.paths.tools_only),
    "engine-paths": lambda conventions: _scope_paths(conventions.paths.engine),
    "audit-checks": _audit_checks,
    "check-places": _check_places,
    "claim-rules": _claim_rules,
    "research-pages": _research_pages,
}


def render_block(name: str, conventions: Conventions) -> list[str]:
    """The lines that fill the block `name`: no trailing blank line and no markers."""
    return BLOCKS[name](conventions)


# --- the markers ----------------------------------------------------------------------------------------------------


def _render(text: str, conventions: Conventions, where: str) -> tuple[str, int]:
    """`text` with every block refilled, and how many blocks there are; BlockError for a wrong marker."""
    lines = text.replace("\r\n", "\n").split("\n")
    out: list[str] = []
    in_fence = False
    opened: tuple[str, int] | None = None
    blocks = 0
    for number, line in enumerate(lines, 1):
        stripped = line.strip()
        begin = _BEGIN.match(stripped)
        if opened is not None:
            # The old content of a block is dropped; only a marker matters until it ends.
            if stripped == _END:
                out.append(line)
                opened = None
            elif begin or stripped.startswith("<!-- conventions:begin"):
                raise BlockError(
                    f"{where}:{number}: a conventions:begin marker inside the block opened on line {opened[1]}"
                )
            continue
        if stripped.startswith(("```", "~~~")):
            in_fence = not in_fence
            out.append(line)
            continue
        if in_fence:
            out.append(line)
            continue
        if stripped == _END:
            raise BlockError(f"{where}:{number}: a conventions:end marker without a begin")
        if begin:
            name = begin.group(1)
            if name not in BLOCKS:
                raise BlockError(f"{where}:{number}: unknown block '{name}' (known: {', '.join(BLOCKS)})")
            out.append(line)
            out += render_block(name, conventions)
            opened = (name, number)
            blocks += 1
            continue
        out.append(line)
    if opened is not None:
        raise BlockError(f"{where}:{opened[1]}: block '{opened[0]}' is never closed (no conventions:end marker)")
    return "\n".join(out), blocks


def render_text(text: str, conventions: Conventions, where: str) -> str:
    """`text` with the content of every block rewritten from `conventions`; `where` names it in an error.

    Line ends become LF. Raises BlockError for an unknown block, an end without a begin or a block never closed.
    """
    return _render(text, conventions, where)[0]


# --- the files and the command --------------------------------------------------------------------------------------


def block_files(root: Path) -> list[Path]:
    """The files that may hold blocks: AGENTS.md, CONTRIBUTING.md and every Markdown file under docs/guides/."""
    files = [root / name for name in ROOT_FILES if (root / name).is_file()]
    guides = root / GUIDES_DIR
    if guides.is_dir():
        files += sorted(guides.rglob("*.md"), key=lambda path: path.as_posix())
    return files


class _Scanned(NamedTuple):
    """One file that may hold blocks, read and rendered."""

    name: str
    path: Path
    #: The text as it should be, and as it is (both with LF line ends).
    text: str
    current: str
    blocks: int

    @property
    def stale(self) -> bool:
        """Whether the file's blocks differ from what the conventions render."""
        return self.text != self.current


def _scan(root: Path, conventions: Conventions) -> list[_Scanned]:
    """Read and render every file that may hold blocks."""
    found: list[_Scanned] = []
    for path in block_files(root):
        name = path.relative_to(root).as_posix()
        current = path.read_bytes().decode("utf-8").replace("\r\n", "\n")
        text, count = _render(current, conventions, name)
        found.append(_Scanned(name, path, text, current, count))
    return found


def stale_files(root: Path, conventions: Conventions) -> list[str]:
    """The files (relative, with forward slashes) whose blocks are out of date; changes none."""
    return [item.name for item in _scan(root, conventions) if item.stale]


def run_render(root: Path, check: bool) -> int:
    """Rewrite the stale blocks of every file under `root`, or with `check` only report them and return 1."""
    scanned = _scan(root, load_conventions(root))
    stale = [item for item in scanned if item.stale]
    if check and stale:
        print("coney-tools: generated blocks out of date (run `coney-tools repo render`):", file=sys.stderr)
        for item in stale:
            print(f"  {item.name}", file=sys.stderr)
        return 1
    for item in stale:
        item.path.write_bytes(item.text.encode("utf-8"))
        print(f"updated {item.name}")
    holders = sum(1 for item in scanned if item.blocks)
    blocks = sum(item.blocks for item in scanned)
    print(f"generated blocks: {'updated' if stale else 'ok'} ({blocks} blocks in {holders} files)")
    return 0
