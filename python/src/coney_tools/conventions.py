# SPDX-License-Identifier: GPL-3.0-or-later
"""The repository's conventions: `.github/conventions.yml` read into frozen, typed values.

That file is the one home of every rule about running the repository that a program can check (commit titles, pull
request descriptions, labels, issue forms, merge settings, hygiene limits, path classes; docs/guides/repo-conventions.md
explains each key). Every check reads it through `load_conventions`, so a rule changes in one line. The loader is
strict on purpose: an unknown key, a missing key, a value of the wrong type, a duplicate key or a reference to
something that does not exist is a `ConventionsError` that names the key, so a typo or a bad merge fails loudly
instead of leaving a rule silently off.
"""

from __future__ import annotations

import re
import subprocess
from collections.abc import Callable, Iterable, Mapping
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from types import MappingProxyType
from typing import Any

import yaml

from coney_tools.config import ConfigError

#: Where the conventions live, relative to the repository root.
CONVENTIONS_FILE = ".github/conventions.yml"
#: The only schema version the loader reads; a change to the file's shape raises it together with the loader.
SUPPORTED_VERSION = 1

#: A label colour as GitHub takes it: six hex digits, no `#`.
_COLOUR = re.compile(r"^[0-9a-fA-F]{6}$")
#: The ways the merge settings name how a squash commit is worded (GitHub's own spellings).
_SQUASH_TITLES = ("PR_TITLE", "COMMIT_OR_PR_TITLE")
_SQUASH_MESSAGES = ("PR_BODY", "COMMIT_MESSAGES", "BLANK")
_MERGE_METHODS = ("squash", "merge", "rebase")
#: How many labels of a family an open issue carries.
_LABEL_COUNTS = ("exactly-one", "at-least-one", "exactly-one-after-triage", "any")
#: The `paths` classes a rule may name.
_PATH_CLASSES = ("code", "docs")


class ConventionsError(ConfigError):
    """The conventions file is missing, is not valid YAML or does not hold usable conventions."""


# --- the values ---------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Wording:
    """How issues, pull requests and guides name people: who is the user, who decides, and the words never to use."""

    #: What the person who tested, was affected or created the issue or pull request is called (`user`).
    person: str
    #: What whoever decides is called (`maintainer`).
    decider: str
    #: Words that must not name a person there (`owner`); matched as whole words outside code.
    avoid: tuple[str, ...]


@dataclass(frozen=True)
class Repo:
    """General facts: the default branch, decisions reserved for a maintainer and the tool configuration files."""

    default_branch: str
    wording: Wording
    #: Decisions an agent never takes; it stops and reports them.
    maintainer_decisions: tuple[str, ...]
    #: Role (`format_cpp`, `hooks`, ...) to the repository path of a tool's own configuration file.
    tools: Mapping[str, str]


@dataclass(frozen=True)
class GeneratedPath:
    """A path a generator writes: never edited by hand, regenerated on a merge conflict."""

    path: str
    #: The command or input that produces it.
    by: str
    #: Paths under `path` that are written by hand after all.
    excluding: tuple[str, ...]


@dataclass(frozen=True)
class PointerFiles:
    """The agent entry files (CLAUDE.md, GEMINI.md): one import line and nothing of their own."""

    files: tuple[str, ...]
    #: The exact line each file must hold.
    line: str
    #: A pointer file larger than this has grown instructions of its own, which would drift from AGENTS.md.
    max_bytes: int


@dataclass(frozen=True)
class Paths:
    """Classes of repository paths and the files with special rules."""

    #: Prefixes of the code: a change here needs a docs change or `Docs: none`.
    code: tuple[str, ...]
    #: Prefixes of the documentation, which satisfies that.
    docs: tuple[str, ...]
    #: Patterns of what a documentation-only pull request touches (see `is_docs_only`): CI's first scope class.
    docs_only: tuple[str, ...]
    #: Patterns of the tools (the python package, agent setup): CI's second scope class (see `ci_class`).
    tools_only: tuple[str, ...]
    #: Patterns of what the engine jobs read: CI's third scope class (see `ci_class`).
    engine: tuple[str, ...]
    generated: tuple[GeneratedPath, ...]
    pointer_files: PointerFiles
    #: The only files that may hold the repository's or the docs site's address.
    address_allowed_in: tuple[str, ...]
    large_file_kb: int
    #: The folders of `src/` (`src/ai/`) whose functions carry `@orig` or `@coney-only` (`coney-tools orig check`).
    gameplay: tuple[str, ...]
    #: The folders of `src/` that are Coney's own by nature, whose functions need neither.
    coney_only: tuple[str, ...]

    def gameplay_dir(self, path: str) -> bool:
        """Whether the repository-relative `path` is in one of the `gameplay` folders."""
        return path.startswith(self.gameplay)

    def is_code(self, path: str) -> bool:
        """Whether the repository-relative `path` (forward slashes) is under one of the `code` prefixes."""
        return path.startswith(self.code)

    def is_docs(self, path: str) -> bool:
        """Whether `path` is under one of the `docs` prefixes."""
        return path.startswith(self.docs)

    def is_docs_only(self, path: str) -> bool:
        """Whether `path` matches a `docs_only` pattern: a trailing `/` is a folder, `*` stays within one folder."""
        return any(_pattern_regex(pattern).fullmatch(path) for pattern in self.docs_only)

    def ci_class(self, path: str) -> str:
        """The CI scope class of `path`, as `.github/scripts/ci-scope.sh` sorts it: the first of `docs_only`,
        `tools_only` and `engine` whose patterns match, else `unknown` (which runs everything)."""
        for name, patterns in (("docs_only", self.docs_only), ("tools_only", self.tools_only), ("engine", self.engine)):
            if any(_pattern_regex(pattern).fullmatch(path) for pattern in patterns):
                return name
        return "unknown"

    def docs_only_regex(self) -> str:
        """The `docs_only` patterns as the one regular expression `.github/scripts/ci-scope.sh` holds."""
        return "^(" + "|".join(_pattern_regex(pattern).pattern for pattern in self.docs_only) + ")$"


@dataclass(frozen=True)
class TitleRules:
    """The rules for a commit or pull request title: `area: Verb the rest`."""

    form: str
    max_length: int
    #: Whether a title may end with a period (the rule is that it may not).
    trailing_period: bool
    #: The suffix that marks a breaking change; it must end the title, after a space.
    breaking_suffix: str
    #: Area name to what it covers.
    areas: Mapping[str, str]
    verbs: tuple[str, ...]


@dataclass(frozen=True)
class CommitBody:
    """The shape of a commit message's body."""

    wrap: int


@dataclass(frozen=True)
class CoAuthor:
    """The trailer an AI adds to the commits it writes: `Co-Authored-By: <model name> <noreply address>`."""

    key: str
    value: str
    #: The addresses that count as a model's noreply address.
    noreply: tuple[str, ...]


@dataclass(frozen=True)
class Trailers:
    """The commit trailers: the AI co-author line, and whether anything else is allowed."""

    ai_co_author: CoAuthor
    only_ai_co_author: bool
    other_emails: bool


@dataclass(frozen=True)
class Commits:
    """The commit rules."""

    title: TitleRules
    body: CommitBody
    trailers: Trailers


@dataclass(frozen=True)
class DocsNone:
    """The line a pull request carries when its code change needs no documentation."""

    line: str
    #: The `paths` class whose change asks for the docs (`code`).
    when_changed: str
    #: The `paths` class whose change satisfies it (`docs`).
    unless_changed: str


@dataclass(frozen=True)
class Closing:
    """How a pull request closes the issues it finishes (`Fixes #N`, one per line)."""

    keyword: str
    form: str
    one_per_line: bool
    section: str


@dataclass(frozen=True)
class Checklist:
    """The Checklist section: a pull request merges only when every task in it is finished, so no item is unticked."""

    section: str
    #: What an unchecked `- [ ]` item gets (`refused`: the description fails the check).
    unchecked: str
    #: The moment the rule began (UTC); a pull request merged before it kept the older `Remaining` list.
    since: datetime
    #: The line that opened that older list (`Remaining:`), whose unchecked items had to cite an issue as `#N`.
    legacy_label: str


@dataclass(frozen=True)
class ScreenshotsRule:
    """The Screenshots section: an uploaded image, or a `none` line with a reason."""

    section: str
    #: The start of the line that says there is nothing to show, followed by the reason in parentheses.
    none: str
    #: `required`: the `none` line must give a reason.
    reason: str
    #: Whether an image must be an uploaded URL, not a path on the author's machine.
    uploaded_only: bool


@dataclass(frozen=True)
class RuntimeEvidence:
    """The runtime-evidence line: a pull request whose title area a player sees says how it was checked against the
    original running (maintainer decision 2026-10-20)."""

    #: The title areas (keys of `commits.title.areas`) whose pull request needs the line.
    areas: tuple[str, ...]
    #: The description section that holds the line (`Testing`).
    section: str
    #: The start of the line: the evidence follows it (`Runtime:`).
    line: str
    #: The start of the line that says the original was not run, followed by the reason in parentheses.
    none: str
    #: `required`: the `none` line must give a reason.
    reason: str


@dataclass(frozen=True)
class WrittenBy:
    """Who wrote the commits: one of the choices."""

    label: str
    choices: tuple[str, ...]
    #: The choice that means no AI took part, so no model is needed.
    no_ai: str


@dataclass(frozen=True)
class ModelLine:
    """The line that names the model."""

    label: str
    #: What a person writes when no model applies; not enough when an AI took part.
    none: str
    #: Whether the line must also name every model that a `Co-Authored-By` trailer on the branch's commits names.
    names_trailer_models: bool


@dataclass(frozen=True)
class AiUse:
    """The AI use section: who wrote the commits and which model."""

    section: str
    written_by: WrittenBy
    model: ModelLine


@dataclass(frozen=True)
class Review:
    """The release gate on the pr-reviewer agent's verdict (`coney-tools repo review`, `repo check-review`)."""

    #: The agent's name; its definition is `.claude/agents/<agent>.md`.
    agent: str
    #: The verdict (JSON) the agent writes next to the description file; `repo check-review` reads it.
    verdict_file: str
    #: The readable report the agent writes beside the verdict.
    report_file: str
    #: How the description is hashed: `sha256-lf` (UTF-8, leading BOM dropped, CRLF and CR turned into LF).
    body_hash: str
    #: `refused`: two byte-identical images in one description fail the gate, whatever the verdict.
    identical_images: str


@dataclass(frozen=True)
class OrigReview:
    """The release gate on the orig-reviewer agent's verdict (`coney-tools orig check-review`), and its ledger."""

    #: The agent's name; its definition is `.claude/agents/<agent>.md`.
    agent: str
    #: The verdict (JSON) the agent writes next to the description file; `orig check-review` reads it.
    verdict_file: str
    #: The readable report the agent writes beside the verdict.
    report_file: str
    #: The record of every review, relative to the workspace's scratch folder (never committed).
    ledger: str
    #: The rubric's version (docs/guides/review/orig.md); a verdict made under another version is refused.
    rubric_version: int


@dataclass(frozen=True)
class Stacked:
    """The rules for a pull request whose base is not the default branch."""

    depends_line: str
    merge_into_base: bool
    auto_merge_on_non_default_base: bool
    after_base_merges: str


@dataclass(frozen=True)
class PullRequests:
    """The rules for a pull request's description (`coney-tools repo check-body`) and its life."""

    template: str
    #: The `##` headings the description must have, in the template's order.
    sections: tuple[str, ...]
    #: The sections (a subset of `sections`) that must hold text, not just a comment or a placeholder.
    filled: tuple[str, ...]
    #: Lines of template text that count as empty when left unchanged (HTML comments always do).
    placeholders: tuple[str, ...]
    #: Whether a description may name paths on the author's machine (the rule is that it may not).
    local_paths: bool
    docs_none: DocsNone
    closing: Closing
    checklist: Checklist
    screenshots: ScreenshotsRule
    runtime: RuntimeEvidence
    ai_use: AiUse
    review: Review
    orig_review: OrigReview
    stacked: Stacked


@dataclass(frozen=True)
class Merge:
    """How pull requests are merged: the GitHub ruleset and repository settings, mirrored here."""

    methods: tuple[str, ...]
    squash_title: str
    squash_message: str
    auto_merge: bool
    delete_branch_on_merge: bool
    linear_history: bool
    up_to_date_required: bool
    required_checks: tuple[str, ...]
    #: Who may merge: never the agent that wrote the work.
    who: tuple[str, ...]


@dataclass(frozen=True)
class IssueSection:
    """One field of an issue form."""

    id: str
    heading: str
    required: bool


@dataclass(frozen=True)
class IssueForm:
    """The structure of one issue form (its guidance text stays in the form's own file)."""

    name: str
    file: str
    labels: tuple[str, ...]
    sections: tuple[IssueSection, ...]


@dataclass(frozen=True)
class Provenance:
    """Where an issue came from: a leftover carries a `From:` line."""

    line: str
    required_for: str


@dataclass(frozen=True)
class IssueLinks:
    """How an issue links a page of the repository."""

    docs_paths: str
    link_form: str
    must_exist_on: str


@dataclass(frozen=True)
class IssueLabelRules:
    """How many labels of each family an open issue carries."""

    type: str
    area: str
    priority: str
    status: str
    #: Whether a closed issue keeps its status label (the rule is that it does not).
    closed_keeps_status: bool


@dataclass(frozen=True)
class Issues:
    """The rules for issues: blank issues, the forms, the media line, provenance and labels."""

    blank_issues: bool
    #: Text that is no answer when it is all a section holds (`x`, `TBD`): a placeholder, which the audit refuses.
    placeholders: tuple[str, ...]
    forms: Mapping[str, IssueForm]
    #: The form whose headings an issue filed from the command line uses.
    cli_uses_form: str
    screenshots_none: str
    provenance: Provenance
    #: Which issue a leftover is a sub-issue of (`source_pr_fixes`: the one its source pull request fixes).
    leftover_parent: str
    links: IssueLinks
    status_text: str
    labels: IssueLabelRules
    #: Label name to the title area (a key of `commits.title.areas`) that `issue titles` proposes for an old title.
    title_area_by_label: Mapping[str, str]


@dataclass(frozen=True)
class Label:
    """One issue label: its name, its colour (six hex digits) and its description."""

    name: str
    color: str
    description: str


@dataclass(frozen=True)
class Labels:
    """The labels of the issue tracker and the families they belong to."""

    description_max: int
    #: The labels with no prefix that name an issue's type.
    type_names: tuple[str, ...]
    area_prefix: str
    priority_prefix: str
    status_prefix: str
    list: tuple[Label, ...]

    def family(self, name: str) -> str:
        """Which family the label `name` belongs to: `type`, `area`, `priority`, `status` or `other`."""
        if name in self.type_names:
            return "type"
        for family, prefix in (
            ("area", self.area_prefix),
            ("priority", self.priority_prefix),
            ("status", self.status_prefix),
        ):
            if name.startswith(prefix):
                return family
        return "other"

    def names(self, family: str | None = None) -> tuple[str, ...]:
        """The label names, all of them or only those of `family`, in file order."""
        return tuple(label.name for label in self.list if family is None or self.family(label.name) == family)


@dataclass(frozen=True)
class ReportIssue:
    """The one issue the weekly sweep keeps up to date."""

    title: str
    labels: tuple[str, ...]


@dataclass(frozen=True)
class Comments:
    """How the hygiene workflow comments on issues and pull requests (`coney-tools repo hygiene`)."""

    #: The word the workflow's comments start with, inside an HTML comment, so it finds its own comments again.
    marker: str
    #: The login a comment must be from to count as the workflow's own (the Actions bot); anyone else's is never edited.
    author: str
    #: Whether an issue or pull request event (opened, edited, labelled, ...) updates that item's comment.
    on_events: bool
    #: Whether the weekly sweep updates item comments; false = the sweep only updates the report.
    on_sweep: bool
    #: At most this many item comments a sweep creates or edits (a flood stopper for a first run).
    max_per_sweep: int
    #: Checks whose findings go in the report only, never in an item comment: a comment is activity, which would make an
    #: idle item look active and so hide the very finding it reports.
    report_only: tuple[str, ...]


@dataclass(frozen=True)
class Thresholds:
    """How many days without a change make an item stale."""

    needs_triage_days: int
    needs_info_days: int
    in_progress_idle_days: int
    pull_request_idle_days: int
    #: Hours a required check may stay queued or running before `repo pr-health` calls the pull request stuck in CI.
    pr_pending_hours: int


#: The causes `coney-tools repo pr-health` reports, in the order one pull request lists them.
PR_HEALTH_CAUSES = ("checks-failed", "conflict", "ci-stuck", "auto-merge-off")


@dataclass(frozen=True)
class AttachmentPatterns:
    """The regular expressions the attachment scan looks for, one tuple per kind of finding (`log_scan`)."""

    #: A user's home folder (`C:\\Users\\<name>`, `/home/<name>`, `/Users/<name>`): personal information.
    user_folder: tuple[str, ...]
    #: An email address: personal information.
    email: tuple[str, ...]
    #: A line of script source (the game's scripts are Lua 4.0): possible game data.
    script_source: tuple[str, ...]
    #: A line of a game text or data file (the placed-objects lines, ...): possible game data.
    game_text: tuple[str, ...]


@dataclass(frozen=True)
class Attachments:
    """The text files attached to an issue that the hygiene workflow downloads and scans (`coney_tools.log_scan`)."""

    #: The start of an attachment link (`https://github.com/user-attachments/files/`); the id and name follow it.
    link: str
    #: The file name endings that are scanned (`.txt`, `.log`), compared without regard to case.
    extensions: tuple[str, ...]
    #: At most this many attachments of one issue are downloaded.
    max_files: int
    #: At most this many attachments in one run (the weekly sweep scans every open issue).
    max_per_run: int
    #: At most this many KB of one attachment are downloaded; the rest is never read.
    download_kb: int
    #: How long one download may take.
    timeout_seconds: int
    #: A log larger than this many KB is reported as possible game data.
    large_kb: int
    #: More than this percentage of control characters (or any NUL) means the file is not text.
    control_percent: int
    #: A file with at least this many lines matching `script_source`, or `game_text`, is reported.
    line_threshold: int
    patterns: AttachmentPatterns


@dataclass(frozen=True)
class RerunRules:
    """When `coney-tools repo pr-health --rerun` restarts a failed required check: only for a failure that is stale."""

    #: Reruns of the checks of one workflow on one head commit, at most, so a flaky job cannot loop.
    max_per_sha: int
    #: The required check that runs `repo check-title` on the pull request's title.
    title_check: str
    #: The required check that runs `repo check-body` on the pull request's description.
    body_check: str
    #: Check conclusions that are never the code's fault (upper case, as GitHub writes them).
    conclusions: tuple[str, ...]
    #: Regular expressions (case-insensitive) that, found in a failed job's log, mean the runner or GitHub failed.
    log_patterns: tuple[str, ...]


@dataclass(frozen=True)
class PrHealth:
    """How `coney-tools repo pr-health` sorts failing checks, labels stuck pull requests and reruns stale checks."""

    #: Required checks whose failure is a lint failure (the author fixes the text); any other required check is code.
    lint_checks: tuple[str, ...]
    #: Cause to the label `repo pr-health --labels` puts on an open pull request with that cause and takes off when it
    #: is gone (`conflict` to `S-conflict`); a label of the list, and no other label is ever touched.
    labels: Mapping[str, str]
    #: How many times `--labels` rereads GitHub while it still answers `UNKNOWN` for a pull request's mergeability
    #: (it computes it on demand, so the first read after a push to main is often unknown).
    unknown_retries: int
    #: Seconds between those rereads.
    unknown_wait_seconds: int
    #: When `--rerun` may restart a failed required check.
    rerun: RerunRules


@dataclass(frozen=True)
class CheckSpec:
    """One check of `coney-tools repo audit`: where it runs, how bad a finding is and who fixes it."""

    #: The check's name, `<group>.<what>` (`issue.labels`); a finding carries it.
    id: str
    #: `files` (the checkout, no network), `github` (issues, pull requests, labels, settings) or `local` (workspace).
    scope: str
    #: `error` fails the audit (exit code 1); `warn` is reported and does not.
    severity: str
    #: `auto` (a coney-tools command fixes it), `skill` (the /repo-hygiene skill or the author fixes it with judgement)
    #: or `maintainer` (a maintainer decides).
    fix: str
    #: The conventions keys the check enforces, as dotted paths (`issues.labels`); each exists in the file.
    keys: tuple[str, ...]
    #: One line: what the check finds.
    summary: str


@dataclass(frozen=True)
class Hygiene:
    """The limits the hygiene checks use, and the registry of the audit's checks."""

    report_issue: ReportIssue
    #: The cron schedule of the weekly sweep.
    sweep: str
    comments: Comments
    thresholds: Thresholds
    pr_health: PrHealth
    attachments: Attachments
    #: Check id to its spec, in file order.
    checks: Mapping[str, CheckSpec]


@dataclass(frozen=True)
class Clips:
    """The in-game video clips' rules (`coney-tools clips`, `pcsx2 clip`)."""

    #: The folder name the clips of a task live in: `<scratch>/<task>/<folder>/`.
    folder: str
    #: The longest clip, in seconds (also Coney's `--record-seconds` cap).
    max_seconds: int
    #: A clip bigger than this many megabytes is reported.
    warn_mb: int
    #: `clips clean` deletes clips older than this many hours.
    keep_hours: int


@dataclass(frozen=True)
class Workspace:
    """The local workspace rules (`workspace clean|disk`); hosted checks ignore them."""

    worktrees: str
    scratch: str
    #: Under this much free space on the workspace drive, `workspace clean` deletes idle build folders.
    disk_min_free_gb: int
    #: ... until free space reaches `disk_min_free_gb` plus this many GB.
    disk_margin_gb: int
    #: A `build/` folder unused for this many hours is deleted.
    build_idle_hours: int
    #: A worktree holding a file of this name is never cleaned.
    busy_marker: str
    remove_worktree_when_merged: bool
    #: File types taken from the original (traces, memory dumps, save states); `repo audit --local` finds them in the
    #: scratch folder of merged or abandoned work.
    game_capture_suffixes: tuple[str, ...]
    #: Folder names (any case) the scratch check never walks into, at any depth: build, dependency and tool trees.
    scratch_skip_folders: tuple[str, ...]
    #: Scratch folders that belong to the workspace, not to a branch (the emulator's, the save states', the downloads').
    shared_scratch: tuple[str, ...]
    clips: Clips


@dataclass(frozen=True)
class PairRules:
    """What a pairs file (`mapping/<addr>.toml`) may hold (docs/guides/stamps.md#pairs)."""

    #: The kinds of a pair: the kinds of the anchor statements `original study` cuts pieces from.
    kinds: tuple[str, ...]
    #: How a piece of the original relates to Coney's code.
    relations: tuple[str, ...]
    #: Words a comment at a stand-in must contain one of (docs/guides/testing.md#stand-ins).
    stand_in_markers: tuple[str, ...]


@dataclass(frozen=True)
class Stamps:
    """The stamp log's rules (docs/guides/stamps.md): its steps, which are signed, and how signatures are checked."""

    #: The signature namespace (`ssh-keygen -Y sign -n`).
    namespace: str
    #: The allowed-signers file, relative to the repository root: one public key per maintainer.
    signers_file: str
    #: A log segment is closed before it would pass this many bytes (the repository's file check is 512 KB).
    segment_bytes: int
    #: Every step a stamp may name, in the order of the workflow.
    steps: tuple[str, ...]
    #: The steps whose stamps must carry a valid signature.
    signed: tuple[str, ...]
    #: The results a stamp may have.
    results: tuple[str, ...]
    pairs: PairRules


@dataclass(frozen=True)
class ClaimCommands:
    """The shapes of the claim commands: a comment is a command only when its whole body matches one (a regex)."""

    #: `/claim`, optionally with `scope: ...` and `task: ...` (named groups `scope` and `task`).
    claim: str
    renew: str
    unclaim: str
    #: `/progress <stage>` (named group `stage`).
    progress: str


@dataclass(frozen=True)
class ClaimLimits:
    """How many open claims one account may hold."""

    maintainer: int
    contributor: int
    #: The most issues one task id may hold, so a `task:` id is no way round the limit.
    issues_per_task: int


@dataclass(frozen=True)
class Claims:
    """Claiming an issue: the commands, the claim card, the limits, the labels and the deadlines."""

    #: The login of the Actions bot: only its comments are cards or replies.
    bot_login: str
    #: A card's comment starts with `<!-- card_marker -->`.
    card_marker: str
    #: A refusal reply starts with `<!-- reply_marker:ID -->`, ID being the command comment it answers.
    reply_marker: str
    commands: ClaimCommands
    #: The stages `/progress` may name.
    stages: tuple[str, ...]
    #: The repository permissions (`admin`, `write`) that make a commenter a maintainer.
    maintainer_permissions: tuple[str, ...]
    #: The `author_association` values that decide only when GitHub will not say the permission.
    maintainer_associations: tuple[str, ...]
    limits: ClaimLimits
    critical_label: str
    #: The one status an issue can be claimed in.
    claimable_status: str
    working_label: str
    review_label: str
    released_label: str
    #: The longest scope text after `scope:`.
    scope_max_characters: int
    #: The `Fixes #N` lines of one pull request event that are acted on; the rest are ignored.
    closing_per_event: int
    #: Status labels a claimed issue keeps; the release clock waits while one is on it.
    paused_labels: tuple[str, ...]
    #: Whether the sweep gives a card to an issue assigned by hand.
    adopt: bool
    #: Whether the warning and the release for idleness are enforced.
    release: bool
    deadline_hours: int
    warn_hours: int
    reply_keep_hours: int
    command_lookback_hours: int
    #: Every claim run also handles the unhandled commands of other issues this recent (a run GitHub cancelled).
    catch_up_minutes: int
    poll_interval_seconds: int
    create_wait_seconds: int
    settle_seconds: int
    #: The cron schedule of the hourly sweep.
    sweep: str


@dataclass(frozen=True)
class LeadsRules:
    """How the leads search ranks and limits what it finds."""

    rare_max_entries: int
    common_min_entries: int
    max_per_source: int
    index_max_age_hours: int
    merged_prs: int


@dataclass(frozen=True)
class Tasks:
    """Task files and the leads search."""

    kinds: tuple[str, ...]
    #: The task file's name inside the task's scratch folder.
    file: str
    leads: LeadsRules


@dataclass(frozen=True)
class Conventions:
    """Everything in `.github/conventions.yml`."""

    version: int
    repo: Repo
    paths: Paths
    commits: Commits
    pull_requests: PullRequests
    merge: Merge
    issues: Issues
    claims: Claims
    tasks: Tasks
    labels: Labels
    hygiene: Hygiene
    workspace: Workspace
    stamps: Stamps


# --- reading ------------------------------------------------------------------------------------------------------


class _Loader(yaml.SafeLoader):
    """A safe YAML loader that refuses a key written twice, which `safe_load` would resolve to the later one."""


def _construct_mapping(loader: _Loader, node: yaml.MappingNode, deep: bool = False) -> dict[object, object]:
    """Build a mapping, raising on a duplicate key (a bad merge often leaves one)."""
    seen: set[object] = set()
    for key_node, _ in node.value:
        key = loader.construct_object(key_node, deep=True)
        if key in seen:
            raise yaml.YAMLError(f"line {key_node.start_mark.line + 1}: key {key!r} is written twice")
        seen.add(key)
    return dict(loader.construct_pairs(node, deep=deep))


_Loader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _construct_mapping)


def _fail(where: str, problem: str) -> ConventionsError:
    """The error for the key at `where` (a dotted path, empty for the top level)."""
    return ConventionsError(f"{CONVENTIONS_FILE}: {where + ': ' if where else ''}{problem}")


def _table(value: object, where: str, required: Iterable[str], optional: Iterable[str] = ()) -> dict[str, Any]:
    """`value` as a mapping with exactly the `required` keys and perhaps some `optional` ones; no other key."""
    if not isinstance(value, dict) or not all(isinstance(key, str) for key in value):
        raise _fail(where, "expected a mapping")
    known = (*required, *optional)
    for key in required:
        if key not in value:
            raise _fail(where, f"missing the key '{key}'")
    for key in value:
        if key not in known:
            raise _fail(where, f"unknown key '{key}' (known: {', '.join(known)})")
    return value


def _mapping(value: object, where: str) -> dict[str, Any]:
    """`value` as a mapping with any text keys (a table of names, such as the areas)."""
    if not isinstance(value, dict) or not all(isinstance(key, str) for key in value):
        raise _fail(where, "expected a mapping")
    return value


def _at(where: str, key: str) -> str:
    """The dotted path of `key` under `where`."""
    return f"{where}.{key}" if where else key


def _text(value: object, where: str) -> str:
    """A non-empty string."""
    if not isinstance(value, str) or not value.strip():
        raise _fail(where, "expected non-empty text")
    return value


def _flag(value: object, where: str) -> bool:
    """A yes/no value written as `true` or `false`."""
    if not isinstance(value, bool):
        raise _fail(where, "expected true or false")
    return value


def _moment(value: object, where: str) -> datetime:
    """A moment written as quoted `YYYY-MM-DD` (midnight UTC) or `YYYY-MM-DDTHH:MM:SSZ` text, as an aware UTC time."""
    try:
        moment = datetime.fromisoformat(_text(value, where))
    except ValueError:
        raise _fail(where, "expected a day or moment written as YYYY-MM-DD or YYYY-MM-DDTHH:MM:SSZ") from None
    return moment.replace(tzinfo=UTC) if moment.tzinfo is None else moment.astimezone(UTC)


def _count(value: object, where: str, minimum: int = 1) -> int:
    """A whole number of at least `minimum` (a YAML boolean is not one)."""
    if not isinstance(value, int) or isinstance(value, bool) or value < minimum:
        raise _fail(where, f"expected a whole number of {minimum} or more")
    return value


def _choice(value: object, where: str, choices: Iterable[str]) -> str:
    """One of the allowed words."""
    allowed = tuple(choices)
    if not isinstance(value, str) or value not in allowed:
        raise _fail(where, f"expected one of: {', '.join(allowed)}")
    return value


def _texts(value: object, where: str, empty_ok: bool = False) -> tuple[str, ...]:
    """A list of non-empty strings, without repeats; non-empty unless `empty_ok`."""
    if not isinstance(value, list) or (not value and not empty_ok):
        raise _fail(where, "expected a non-empty list")
    items = tuple(_text(item, f"{where}[{index}]") for index, item in enumerate(value))
    if len(set(items)) != len(items):
        raise _fail(where, "a value is listed twice")
    return items


def _names_in(values: Iterable[str], known: Iterable[str], where: str, what: str) -> None:
    """Require every one of `values` to be among `known`."""
    allowed = tuple(known)
    for value in values:
        if value not in allowed:
            raise _fail(where, f"'{value}' is not {what}")


def _each[T](value: object, where: str, build: Callable[[object, str], T]) -> tuple[T, ...]:
    """A non-empty list whose items are each read by `build`."""
    if not isinstance(value, list) or not value:
        raise _fail(where, "expected a non-empty list")
    return tuple(build(item, f"{where}[{index}]") for index, item in enumerate(value))


def _pattern_regex(pattern: str) -> re.Pattern[str]:
    """The regular expression of a CI scope pattern: `dir/` matches the whole folder, `*` stays within a folder."""
    if pattern.endswith("/"):
        return re.compile(re.escape(pattern) + ".*", re.DOTALL)
    return re.compile("[^/]*".join(re.escape(part) for part in pattern.split("*")))


#: What a CI scope pattern (`docs_only`, `tools_only`, `engine`) may hold: plain path characters and `*`, which
#: `.github/scripts/ci-scope.sh` turns into a regular expression by escaping the dot and nothing more.
_SCOPE_PATTERN = re.compile(r"^[A-Za-z0-9_./*-]+$")


def _scope_patterns(value: object, where: str) -> tuple[str, ...]:
    """One CI scope list of `paths`; a pattern with a character the CI scope script does not escape is refused."""
    patterns = _texts(value, where)
    for index, pattern in enumerate(patterns):
        if not _SCOPE_PATTERN.match(pattern):
            raise _fail(f"{where}[{index}]", f"'{pattern}': only letters, digits, . _ / - and * (ci-scope.sh reads it)")
    return patterns


def _read_repo(data: object, where: str) -> Repo:
    """The `repo` section: the default branch, the wording, the maintainer decisions and the tool files."""
    table = _table(data, where, ("default_branch", "wording", "maintainer_decisions", "tools"))
    tools = _mapping(table["tools"], _at(where, "tools"))
    wording_where = _at(where, "wording")
    wording = _table(table["wording"], wording_where, ("person", "decider", "avoid"))
    return Repo(
        default_branch=_text(table["default_branch"], _at(where, "default_branch")),
        wording=Wording(
            person=_text(wording["person"], _at(wording_where, "person")),
            decider=_text(wording["decider"], _at(wording_where, "decider")),
            avoid=_texts(wording["avoid"], _at(wording_where, "avoid")),
        ),
        maintainer_decisions=_texts(table["maintainer_decisions"], _at(where, "maintainer_decisions")),
        tools=MappingProxyType({key: _text(path, _at(_at(where, "tools"), key)) for key, path in tools.items()}),
    )


def _read_generated(data: object, where: str) -> GeneratedPath:
    """One entry of `paths.generated`."""
    table = _table(data, where, ("path", "by"), ("except",))
    return GeneratedPath(
        path=_text(table["path"], _at(where, "path")),
        by=_text(table["by"], _at(where, "by")),
        excluding=_texts(table.get("except", []), _at(where, "except"), empty_ok=True),
    )


def _read_paths(data: object, where: str) -> Paths:
    """The `paths` section: path classes, generated paths, pointer files and the size limit."""
    keys = (
        "code",
        "docs",
        "docs_only",
        "tools_only",
        "engine",
        "generated",
        "pointer_files",
        "address_allowed_in",
        "large_file_kb",
        "gameplay",
        "coney_only",
    )
    table = _table(data, where, keys)
    gameplay = _source_folders(table["gameplay"], _at(where, "gameplay"))
    coney_only = _source_folders(table["coney_only"], _at(where, "coney_only"))
    for folder in coney_only:
        if folder in gameplay:
            raise _fail(_at(where, "coney_only"), f"'{folder}' is also in {_at(where, 'gameplay')}")
    pointer = _table(table["pointer_files"], _at(where, "pointer_files"), ("files", "line", "max_bytes"))
    pointer_where = _at(where, "pointer_files")
    return Paths(
        code=_texts(table["code"], _at(where, "code")),
        docs=_texts(table["docs"], _at(where, "docs")),
        docs_only=_scope_patterns(table["docs_only"], _at(where, "docs_only")),
        tools_only=_scope_patterns(table["tools_only"], _at(where, "tools_only")),
        engine=_scope_patterns(table["engine"], _at(where, "engine")),
        generated=_each(table["generated"], _at(where, "generated"), _read_generated),
        pointer_files=PointerFiles(
            files=_texts(pointer["files"], _at(pointer_where, "files")),
            line=_text(pointer["line"], _at(pointer_where, "line")),
            max_bytes=_count(pointer["max_bytes"], _at(pointer_where, "max_bytes")),
        ),
        address_allowed_in=_texts(table["address_allowed_in"], _at(where, "address_allowed_in")),
        large_file_kb=_count(table["large_file_kb"], _at(where, "large_file_kb")),
        gameplay=gameplay,
        coney_only=coney_only,
    )


def _source_folders(value: object, where: str) -> tuple[str, ...]:
    """A list of top-level folders of `src/`, each written `src/<name>/`."""
    folders = _texts(value, where)
    for folder in folders:
        if not re.fullmatch(r"src/[A-Za-z0-9_]+/", folder):
            raise _fail(where, f"'{folder}' is not a folder of src/ written src/<name>/")
    return folders


def _read_title(data: object, where: str) -> TitleRules:
    """The `commits.title` rules; areas and verbs are single words, since a title is `area: Verb ...`."""
    table = _table(data, where, ("form", "max_length", "trailing_period", "breaking_suffix", "areas", "verbs"))
    areas_where = _at(where, "areas")
    areas = _mapping(table["areas"], areas_where)
    if not areas:
        raise _fail(areas_where, "expected at least one area")
    for name in areas:
        # The title form is `area: Verb ...`, so an area holds no space or colon.
        if not re.fullmatch(r"[^\s:]+", name):
            raise _fail(areas_where, f"'{name}' must be one word without a colon")
    verbs_where = _at(where, "verbs")
    verbs = _texts(table["verbs"], verbs_where)
    for verb in verbs:
        if not re.fullmatch(r"\S+", verb):
            raise _fail(verbs_where, f"'{verb}' must be one word")
    return TitleRules(
        form=_text(table["form"], _at(where, "form")),
        max_length=_count(table["max_length"], _at(where, "max_length"), minimum=20),
        trailing_period=_flag(table["trailing_period"], _at(where, "trailing_period")),
        breaking_suffix=_text(table["breaking_suffix"], _at(where, "breaking_suffix")),
        areas=MappingProxyType({name: _text(text, _at(areas_where, name)) for name, text in areas.items()}),
        verbs=verbs,
    )


def _read_trailers(data: object, where: str) -> Trailers:
    """The `commits.trailers` rules."""
    table = _table(data, where, ("ai_co_author", "only_ai_co_author", "other_emails"))
    co_where = _at(where, "ai_co_author")
    co = _table(table["ai_co_author"], co_where, ("key", "value", "noreply"))
    return Trailers(
        ai_co_author=CoAuthor(
            key=_text(co["key"], _at(co_where, "key")),
            value=_text(co["value"], _at(co_where, "value")),
            noreply=_texts(co["noreply"], _at(co_where, "noreply")),
        ),
        only_ai_co_author=_flag(table["only_ai_co_author"], _at(where, "only_ai_co_author")),
        other_emails=_flag(table["other_emails"], _at(where, "other_emails")),
    )


def _read_commits(data: object, where: str) -> Commits:
    """The `commits` section."""
    table = _table(data, where, ("title", "body", "trailers"))
    body_where = _at(where, "body")
    body = _table(table["body"], body_where, ("wrap",))
    return Commits(
        title=_read_title(table["title"], _at(where, "title")),
        body=CommitBody(wrap=_count(body["wrap"], _at(body_where, "wrap"), minimum=20)),
        trailers=_read_trailers(table["trailers"], _at(where, "trailers")),
    )


def _read_pull_requests(data: object, where: str) -> PullRequests:
    """The `pull_requests` section; every section it names must be one of its `sections`."""
    keys = (
        "template",
        "sections",
        "filled",
        "placeholders",
        "local_paths",
        "docs_none",
        "closing",
        "checklist",
        "screenshots",
        "runtime",
        "ai_use",
        "review",
        "orig_review",
        "stacked",
    )
    table = _table(data, where, keys)
    sections = _texts(table["sections"], _at(where, "sections"))
    filled = _texts(table["filled"], _at(where, "filled"))
    _names_in(filled, sections, _at(where, "filled"), "one of the sections")

    def sub(name: str, *fields: str) -> tuple[dict[str, Any], str]:
        return _table(table[name], _at(where, name), fields), _at(where, name)

    docs_none, docs_none_where = sub("docs_none", "line", "when_changed", "unless_changed")
    closing, closing_where = sub("closing", "keyword", "form", "one_per_line", "section")
    checklist, checklist_where = sub("checklist", "section", "unchecked", "since", "legacy_label")
    screenshots, screenshots_where = sub("screenshots", "section", "none", "reason", "uploaded_only")
    runtime, runtime_where = sub("runtime", "areas", "section", "line", "none", "reason")
    ai_use, ai_where = sub("ai_use", "section", "written_by", "model")
    review, review_where = sub("review", "agent", "verdict_file", "report_file", "body_hash", "identical_images")
    agent = _text(review["agent"], _at(review_where, "agent"))
    # The agent is a file name under .claude/agents/, so it holds no path separator.
    if not re.fullmatch(r"[a-z][a-z0-9-]*", agent):
        raise _fail(_at(review_where, "agent"), f"'{agent}' must be an agent name (lower case, digits and dashes)")
    verdict_file = _text(review["verdict_file"], _at(review_where, "verdict_file"))
    report_file = _text(review["report_file"], _at(review_where, "report_file"))
    if report_file == verdict_file:
        raise _fail(_at(review_where, "report_file"), "must differ from verdict_file")
    orig, orig_where = sub("orig_review", "agent", "verdict_file", "report_file", "ledger", "rubric_version")
    orig_agent = _text(orig["agent"], _at(orig_where, "agent"))
    if not re.fullmatch(r"[a-z][a-z0-9-]*", orig_agent):
        raise _fail(_at(orig_where, "agent"), f"'{orig_agent}' must be an agent name (lower case, digits and dashes)")
    orig_files = [_text(orig[name], _at(orig_where, name)) for name in ("verdict_file", "report_file")]
    if len({*orig_files, verdict_file, report_file}) != 4:
        raise _fail(
            _at(orig_where, "verdict_file"), "the orig verdict and report must differ from each other and from review's"
        )
    orig_ledger = _text(orig["ledger"], _at(orig_where, "ledger"))
    if orig_ledger.startswith(("/", "\\")) or ".." in orig_ledger.replace("\\", "/").split("/"):
        raise _fail(_at(orig_where, "ledger"), "must be a path inside the scratch folder")
    stacked, stacked_where = sub(
        "stacked", "depends_line", "merge_into_base", "auto_merge_on_non_default_base", "after_base_merges"
    )
    written_where = _at(ai_where, "written_by")
    written = _table(ai_use["written_by"], written_where, ("label", "choices", "no_ai"))
    model_where = _at(ai_where, "model")
    model = _table(ai_use["model"], model_where, ("label", "none", "names_trailer_models"))
    choices = _texts(written["choices"], _at(written_where, "choices"))
    no_ai = _text(written["no_ai"], _at(written_where, "no_ai"))
    _names_in((no_ai,), choices, _at(written_where, "no_ai"), "one of the choices")
    section_fields = (
        (_at(closing_where, "section"), closing["section"]),
        (_at(checklist_where, "section"), checklist["section"]),
        (_at(screenshots_where, "section"), screenshots["section"]),
        (_at(runtime_where, "section"), runtime["section"]),
        (_at(ai_where, "section"), ai_use["section"]),
    )
    for field_where, section in section_fields:
        _names_in((_text(section, field_where),), sections, field_where, "one of the sections")
    return PullRequests(
        template=_text(table["template"], _at(where, "template")),
        sections=sections,
        filled=filled,
        placeholders=_texts(table["placeholders"], _at(where, "placeholders")),
        local_paths=_flag(table["local_paths"], _at(where, "local_paths")),
        docs_none=DocsNone(
            line=_text(docs_none["line"], _at(docs_none_where, "line")),
            when_changed=_choice(docs_none["when_changed"], _at(docs_none_where, "when_changed"), _PATH_CLASSES),
            unless_changed=_choice(docs_none["unless_changed"], _at(docs_none_where, "unless_changed"), _PATH_CLASSES),
        ),
        closing=Closing(
            keyword=_text(closing["keyword"], _at(closing_where, "keyword")),
            form=_text(closing["form"], _at(closing_where, "form")),
            one_per_line=_flag(closing["one_per_line"], _at(closing_where, "one_per_line")),
            section=closing["section"],
        ),
        checklist=Checklist(
            section=checklist["section"],
            unchecked=_choice(checklist["unchecked"], _at(checklist_where, "unchecked"), ("refused",)),
            since=_moment(checklist["since"], _at(checklist_where, "since")),
            legacy_label=_text(checklist["legacy_label"], _at(checklist_where, "legacy_label")),
        ),
        screenshots=ScreenshotsRule(
            section=screenshots["section"],
            none=_text(screenshots["none"], _at(screenshots_where, "none")),
            reason=_choice(screenshots["reason"], _at(screenshots_where, "reason"), ("required",)),
            uploaded_only=_flag(screenshots["uploaded_only"], _at(screenshots_where, "uploaded_only")),
        ),
        runtime=RuntimeEvidence(
            areas=_texts(runtime["areas"], _at(runtime_where, "areas")),
            section=runtime["section"],
            line=_text(runtime["line"], _at(runtime_where, "line")),
            none=_text(runtime["none"], _at(runtime_where, "none")),
            reason=_choice(runtime["reason"], _at(runtime_where, "reason"), ("required",)),
        ),
        ai_use=AiUse(
            section=ai_use["section"],
            written_by=WrittenBy(
                label=_text(written["label"], _at(written_where, "label")), choices=choices, no_ai=no_ai
            ),
            model=ModelLine(
                label=_text(model["label"], _at(model_where, "label")),
                none=_text(model["none"], _at(model_where, "none")),
                names_trailer_models=_flag(model["names_trailer_models"], _at(model_where, "names_trailer_models")),
            ),
        ),
        review=Review(
            agent=agent,
            verdict_file=verdict_file,
            report_file=report_file,
            body_hash=_choice(review["body_hash"], _at(review_where, "body_hash"), ("sha256-lf",)),
            identical_images=_choice(review["identical_images"], _at(review_where, "identical_images"), ("refused",)),
        ),
        orig_review=OrigReview(
            agent=orig_agent,
            verdict_file=orig_files[0],
            report_file=orig_files[1],
            ledger=orig_ledger,
            rubric_version=_count(orig["rubric_version"], _at(orig_where, "rubric_version")),
        ),
        stacked=Stacked(
            depends_line=_text(stacked["depends_line"], _at(stacked_where, "depends_line")),
            merge_into_base=_flag(stacked["merge_into_base"], _at(stacked_where, "merge_into_base")),
            auto_merge_on_non_default_base=_flag(
                stacked["auto_merge_on_non_default_base"], _at(stacked_where, "auto_merge_on_non_default_base")
            ),
            after_base_merges=_text(stacked["after_base_merges"], _at(stacked_where, "after_base_merges")),
        ),
    )


def _read_merge(data: object, where: str) -> Merge:
    """The `merge` section: the ruleset and repository settings, with GitHub's own spellings checked."""
    keys = (
        "methods",
        "squash_title",
        "squash_message",
        "auto_merge",
        "delete_branch_on_merge",
        "linear_history",
        "up_to_date_required",
        "required_checks",
        "who",
    )
    table = _table(data, where, keys)
    methods = _texts(table["methods"], _at(where, "methods"))
    _names_in(methods, _MERGE_METHODS, _at(where, "methods"), f"a merge method ({', '.join(_MERGE_METHODS)})")
    return Merge(
        methods=methods,
        squash_title=_choice(table["squash_title"], _at(where, "squash_title"), _SQUASH_TITLES),
        squash_message=_choice(table["squash_message"], _at(where, "squash_message"), _SQUASH_MESSAGES),
        auto_merge=_flag(table["auto_merge"], _at(where, "auto_merge")),
        delete_branch_on_merge=_flag(table["delete_branch_on_merge"], _at(where, "delete_branch_on_merge")),
        linear_history=_flag(table["linear_history"], _at(where, "linear_history")),
        up_to_date_required=_flag(table["up_to_date_required"], _at(where, "up_to_date_required")),
        required_checks=_texts(table["required_checks"], _at(where, "required_checks")),
        who=_texts(table["who"], _at(where, "who")),
    )


def _read_section(item: object, where: str, section_id: str) -> IssueSection:
    """One field of an issue form, written `id: [heading, required]`."""
    if not isinstance(item, list) or len(item) != 2:
        raise _fail(where, "expected [heading, required]")
    return IssueSection(id=section_id, heading=_text(item[0], f"{where}[0]"), required=_flag(item[1], f"{where}[1]"))


def _read_form(name: str, data: object, where: str) -> IssueForm:
    """One issue form under `issues.forms`."""
    table = _table(data, where, ("file", "labels", "sections"))
    sections_where = _at(where, "sections")
    raw = table["sections"]
    if not isinstance(raw, dict) or not raw or not all(isinstance(key, str) for key in raw):
        raise _fail(sections_where, "expected a non-empty mapping of id to [heading, required]")
    sections = tuple(_read_section(pair, _at(sections_where, key), key) for key, pair in raw.items())
    headings = [section.heading for section in sections]
    if len(set(headings)) != len(headings):
        raise _fail(sections_where, "a heading is used twice")
    return IssueForm(
        name=name,
        file=_text(table["file"], _at(where, "file")),
        labels=_texts(table["labels"], _at(where, "labels")),
        sections=sections,
    )


def _read_issues(data: object, where: str) -> Issues:
    """The `issues` section; the command-line form must be one of the forms."""
    keys = (
        "blank_issues",
        "placeholders",
        "forms",
        "cli_uses_form",
        "screenshots_none",
        "provenance",
        "sub_issues",
        "links",
        "status_text",
        "labels",
        "title_area_by_label",
    )
    table = _table(data, where, keys)
    forms_where = _at(where, "forms")
    raw_forms = table["forms"]
    if not isinstance(raw_forms, dict) or not raw_forms:
        raise _fail(forms_where, "expected a non-empty mapping of forms")
    forms = {str(name): _read_form(str(name), form, _at(forms_where, str(name))) for name, form in raw_forms.items()}
    _names_in(
        (_text(table["cli_uses_form"], _at(where, "cli_uses_form")),), forms, _at(where, "cli_uses_form"), "a form"
    )
    provenance_where = _at(where, "provenance")
    provenance = _table(table["provenance"], provenance_where, ("line", "required_for"))
    sub_where = _at(where, "sub_issues")
    sub_issues = _table(table["sub_issues"], sub_where, ("leftover_parent",))
    links_where = _at(where, "links")
    links = _table(table["links"], links_where, ("docs_paths", "link_form", "must_exist_on"))
    labels_where = _at(where, "labels")
    labels = _table(table["labels"], labels_where, ("type", "area", "priority", "status", "closed_keeps_status"))
    return Issues(
        blank_issues=_flag(table["blank_issues"], _at(where, "blank_issues")),
        placeholders=_texts(table["placeholders"], _at(where, "placeholders")),
        forms=MappingProxyType(forms),
        cli_uses_form=table["cli_uses_form"],
        screenshots_none=_text(table["screenshots_none"], _at(where, "screenshots_none")),
        provenance=Provenance(
            line=_text(provenance["line"], _at(provenance_where, "line")),
            required_for=_choice(provenance["required_for"], _at(provenance_where, "required_for"), ("leftovers",)),
        ),
        leftover_parent=_choice(sub_issues["leftover_parent"], _at(sub_where, "leftover_parent"), ("source_pr_fixes",)),
        links=IssueLinks(
            docs_paths=_choice(links["docs_paths"], _at(links_where, "docs_paths"), ("linked",)),
            link_form=_text(links["link_form"], _at(links_where, "link_form")),
            must_exist_on=_text(links["must_exist_on"], _at(links_where, "must_exist_on")),
        ),
        status_text=_choice(table["status_text"], _at(where, "status_text"), ("truthful",)),
        labels=IssueLabelRules(
            type=_choice(labels["type"], _at(labels_where, "type"), _LABEL_COUNTS),
            area=_choice(labels["area"], _at(labels_where, "area"), _LABEL_COUNTS),
            priority=_choice(labels["priority"], _at(labels_where, "priority"), _LABEL_COUNTS),
            status=_choice(labels["status"], _at(labels_where, "status"), _LABEL_COUNTS),
            closed_keeps_status=_flag(labels["closed_keeps_status"], _at(labels_where, "closed_keeps_status")),
        ),
        title_area_by_label=MappingProxyType(
            {
                label: _text(area, _at(_at(where, "title_area_by_label"), label))
                for label, area in _mapping(table["title_area_by_label"], _at(where, "title_area_by_label")).items()
            }
        ),
    )


_COMMAND_GROUPS = {"claim": ("scope", "task"), "renew": (), "unclaim": (), "progress": ("stage",)}


def _read_claims(data: object, where: str, label_names: tuple[str, ...]) -> Claims:
    """The `claims` section: commands that compile with their groups, labels that exist, deadlines in order."""
    counts = (
        "deadline_hours",
        "warn_hours",
        "reply_keep_hours",
        "command_lookback_hours",
        "catch_up_minutes",
        "poll_interval_seconds",
        "create_wait_seconds",
        "settle_seconds",
    )
    labels = ("critical_label", "claimable_status", "working_label", "review_label", "released_label")
    table = _table(
        data,
        where,
        (
            "bot_login",
            "card_marker",
            "reply_marker",
            "commands",
            "stages",
            "maintainer_permissions",
            "maintainer_associations",
            "limits",
            "sweep",
            "scope_max_characters",
            "closing_per_event",
            "paused_labels",
            "adopt",
            "release",
            *labels,
            *counts,
        ),
    )
    commands_where = _at(where, "commands")
    commands = _table(table["commands"], commands_where, tuple(_COMMAND_GROUPS))
    for name, groups in _COMMAND_GROUPS.items():
        pattern = _text(commands[name], _at(commands_where, name))
        try:
            compiled = re.compile(pattern)
        except re.error as error:
            raise _fail(_at(commands_where, name), f"not a valid regular expression ({error})") from None
        for group in groups:
            if group not in compiled.groupindex:
                raise _fail(_at(commands_where, name), f"the pattern needs a named group '{group}'")
    markers = {}
    for key in ("card_marker", "reply_marker"):
        markers[key] = _text(table[key], _at(where, key))
        if not _MARKER.match(markers[key]):
            raise _fail(_at(where, key), "letters, digits and hyphens only (it sits inside an HTML comment)")
    limits_where = _at(where, "limits")
    limits = _table(table["limits"], limits_where, ("maintainer", "contributor", "issues_per_task"))
    names = {key: _text(table[key], _at(where, key)) for key in labels}
    _names_in(names.values(), label_names, where, "in labels.list")
    paused = _texts(table["paused_labels"], _at(where, "paused_labels"))
    _names_in(paused, label_names, _at(where, "paused_labels"), "in labels.list")
    numbers = {key: _count(table[key], _at(where, key)) for key in counts}
    if numbers["warn_hours"] >= numbers["deadline_hours"]:
        raise _fail(_at(where, "warn_hours"), "must be less than deadline_hours")
    return Claims(
        bot_login=_text(table["bot_login"], _at(where, "bot_login")),
        card_marker=markers["card_marker"],
        reply_marker=markers["reply_marker"],
        commands=ClaimCommands(**{name: str(commands[name]) for name in _COMMAND_GROUPS}),
        stages=_texts(table["stages"], _at(where, "stages")),
        maintainer_permissions=_texts(table["maintainer_permissions"], _at(where, "maintainer_permissions")),
        maintainer_associations=_texts(table["maintainer_associations"], _at(where, "maintainer_associations")),
        scope_max_characters=_count(table["scope_max_characters"], _at(where, "scope_max_characters")),
        closing_per_event=_count(table["closing_per_event"], _at(where, "closing_per_event")),
        paused_labels=paused,
        adopt=_flag(table["adopt"], _at(where, "adopt")),
        release=_flag(table["release"], _at(where, "release")),
        limits=ClaimLimits(
            maintainer=_count(limits["maintainer"], _at(limits_where, "maintainer")),
            contributor=_count(limits["contributor"], _at(limits_where, "contributor")),
            issues_per_task=_count(limits["issues_per_task"], _at(limits_where, "issues_per_task")),
        ),
        critical_label=names["critical_label"],
        claimable_status=names["claimable_status"],
        working_label=names["working_label"],
        review_label=names["review_label"],
        released_label=names["released_label"],
        sweep=_text(table["sweep"], _at(where, "sweep")),
        **numbers,
    )


def _read_tasks(data: object, where: str) -> Tasks:
    """The `tasks` section: the task kinds, the task file's name and the leads limits."""
    table = _table(data, where, ("kinds", "file", "leads"))
    leads_where = _at(where, "leads")
    keys = ("rare_max_entries", "common_min_entries", "max_per_source", "index_max_age_hours", "merged_prs")
    leads = _table(table["leads"], leads_where, keys)
    numbers = {key: _count(leads[key], _at(leads_where, key)) for key in keys}
    if numbers["rare_max_entries"] >= numbers["common_min_entries"]:
        raise _fail(_at(leads_where, "rare_max_entries"), "must be less than common_min_entries")
    file = _text(table["file"], _at(where, "file"))
    if "/" in file or "\\" in file:
        raise _fail(_at(where, "file"), "a file name, not a path")
    return Tasks(kinds=_texts(table["kinds"], _at(where, "kinds")), file=file, leads=LeadsRules(**numbers))


def _read_label(item: object, where: str, description_max: int) -> Label:
    """One entry of `labels.list`; a colour YAML read as a number must have been quoted."""
    table = _table(item, where, ("name", "color", "description"))
    name = _text(table["name"], _at(where, "name"))
    colour = table["color"]
    if isinstance(colour, (int, float)) and not isinstance(colour, bool):
        # YAML reads 000000 or 123456 as a number and drops the leading zeros; the colour has to be quoted.
        raise _fail(_at(where, "color"), f"{name!r}: write the colour in quotes (YAML reads it as a number)")
    if not isinstance(colour, str) or not _COLOUR.match(colour):
        raise _fail(_at(where, "color"), f"{name!r}: expected six hex digits without '#'")
    description = _text(table["description"], _at(where, "description")).strip()
    if len(description) > description_max:
        raise _fail(_at(where, "description"), f"{name!r}: over {description_max} characters")
    return Label(name=name.strip(), color=colour.lower(), description=description)


def _read_labels(data: object, where: str) -> Labels:
    """The `labels` section: the families and the list, with names unique ignoring case."""
    table = _table(data, where, ("description_max", "families", "list"))
    description_max = _count(table["description_max"], _at(where, "description_max"))
    families_where = _at(where, "families")
    families = _table(table["families"], families_where, ("type", "area", "priority", "status"))
    type_where = _at(families_where, "type")
    type_family = _table(families["type"], type_where, ("prefix", "names"))
    if type_family["prefix"] != "":
        raise _fail(_at(type_where, "prefix"), 'the type family has no prefix (write "")')
    prefixes = {}
    for family in ("area", "priority", "status"):
        family_where = _at(families_where, family)
        prefixes[family] = _text(
            _table(families[family], family_where, ("prefix",))["prefix"], _at(family_where, "prefix")
        )
    labels = _each(table["list"], _at(where, "list"), lambda item, at: _read_label(item, at, description_max))
    seen: set[str] = set()
    for label in labels:
        # GitHub ignores case in label names.
        if label.name.lower() in seen:
            raise _fail(_at(where, "list"), f"label {label.name!r} appears twice (GitHub ignores case)")
        seen.add(label.name.lower())
    result = Labels(
        description_max=description_max,
        type_names=_texts(type_family["names"], _at(type_where, "names")),
        area_prefix=prefixes["area"],
        priority_prefix=prefixes["priority"],
        status_prefix=prefixes["status"],
        list=labels,
    )
    _names_in(result.type_names, result.names(), _at(type_where, "names"), "in the label list")
    return result


#: Where a check runs, how bad a finding is and who fixes it (the words `hygiene.checks` may use).
_CHECK_SCOPES = ("files", "github", "local")
_CHECK_SEVERITIES = ("error", "warn")
_CHECK_FIXES = ("auto", "skill", "maintainer")
#: The marker of the hygiene workflow's comments: a word that cannot end or break the HTML comment it sits in.
_MARKER = re.compile(r"^[A-Za-z][A-Za-z0-9-]*$")
#: A check id: a group, a dot and a name.
_CHECK_ID = re.compile(r"^[a-z]+\.[a-z][a-z-]*$")


def _has_key(data: object, dotted: str) -> bool:
    """Whether the dotted key (`issues.labels.type`) exists in the file's data."""
    node = data
    for part in dotted.split("."):
        if not isinstance(node, dict) or part not in node:
            return False
        node = node[part]
    return True


def _read_check(check_id: str, data: object, where: str) -> CheckSpec:
    """One entry of `hygiene.checks`."""
    if not _CHECK_ID.match(check_id):
        raise _fail(where, f"'{check_id}' is not a check id like 'issue.labels'")
    table = _table(data, where, ("scope", "severity", "fix", "keys", "summary"))
    return CheckSpec(
        id=check_id,
        scope=_choice(table["scope"], _at(where, "scope"), _CHECK_SCOPES),
        severity=_choice(table["severity"], _at(where, "severity"), _CHECK_SEVERITIES),
        fix=_choice(table["fix"], _at(where, "fix"), _CHECK_FIXES),
        keys=_texts(table["keys"], _at(where, "keys")),
        summary=_text(table["summary"], _at(where, "summary")),
    )


def _patterns(value: object, where: str) -> tuple[str, ...]:
    """A list of regular expressions, each of which must compile."""
    patterns = _texts(value, where)
    for index, pattern in enumerate(patterns):
        try:
            re.compile(pattern)
        except re.error as error:
            raise _fail(f"{where}[{index}]", f"not a valid regular expression ({error})") from None
    return patterns


def _read_attachments(data: object, where: str) -> Attachments:
    """`hygiene.attachments`: what the attachment scan downloads, its limits and its patterns."""
    counts = (
        "max_files",
        "max_per_run",
        "download_kb",
        "timeout_seconds",
        "large_kb",
        "control_percent",
        "line_threshold",
    )
    table = _table(data, where, ("link", "extensions", "patterns", *counts))
    link = _text(table["link"], _at(where, "link"))
    if not link.startswith("https://") or not link.endswith("/"):
        raise _fail(_at(where, "link"), "expected an https:// address ending with /")
    extensions = _texts(table["extensions"], _at(where, "extensions"))
    for extension in extensions:
        if not re.fullmatch(r"\.[a-z0-9]+", extension):
            raise _fail(_at(where, "extensions"), f"'{extension}' is not a lower-case ending like '.log'")
    numbers = {key: _count(table[key], _at(where, key)) for key in counts}
    if numbers["large_kb"] > numbers["download_kb"]:
        raise _fail(_at(where, "large_kb"), "must not be more than download_kb, or a large file could not be seen")
    if numbers["control_percent"] > 100:
        raise _fail(_at(where, "control_percent"), "expected a percentage of 100 or less")
    patterns_where = _at(where, "patterns")
    kinds = ("user_folder", "email", "script_source", "game_text")
    patterns = _table(table["patterns"], patterns_where, kinds)
    return Attachments(
        link=link,
        extensions=extensions,
        max_files=numbers["max_files"],
        max_per_run=numbers["max_per_run"],
        download_kb=numbers["download_kb"],
        timeout_seconds=numbers["timeout_seconds"],
        large_kb=numbers["large_kb"],
        control_percent=numbers["control_percent"],
        line_threshold=numbers["line_threshold"],
        patterns=AttachmentPatterns(**{kind: _patterns(patterns[kind], _at(patterns_where, kind)) for kind in kinds}),
    )


def _read_hygiene(data: object, where: str) -> Hygiene:
    """The `hygiene` section: the report issue, the sweep schedule, the idle limits and the audit's checks."""
    table = _table(
        data, where, ("report_issue", "sweep", "comments", "thresholds", "pr_health", "attachments", "checks")
    )
    checks_where = _at(where, "checks")
    raw_checks = _mapping(table["checks"], checks_where)
    if not raw_checks:
        raise _fail(checks_where, "expected at least one check")
    checks = {name: _read_check(name, spec, _at(checks_where, name)) for name, spec in raw_checks.items()}
    report_where = _at(where, "report_issue")
    report = _table(table["report_issue"], report_where, ("title", "labels"))
    comments_where = _at(where, "comments")
    comments = _table(
        table["comments"],
        comments_where,
        ("marker", "author", "on_events", "on_sweep", "max_per_sweep", "report_only"),
    )
    marker = _text(comments["marker"], _at(comments_where, "marker"))
    if not _MARKER.match(marker):
        raise _fail(_at(comments_where, "marker"), "letters, digits and hyphens only (it sits inside an HTML comment)")
    report_only = _texts(comments["report_only"], _at(comments_where, "report_only"), empty_ok=True)
    _names_in(report_only, checks, _at(comments_where, "report_only"), "a check in hygiene.checks")
    limits_where = _at(where, "thresholds")
    keys = (
        "needs_triage_days",
        "needs_info_days",
        "in_progress_idle_days",
        "pull_request_idle_days",
        "pr_pending_hours",
    )
    limits = _table(table["thresholds"], limits_where, keys)
    health_where = _at(where, "pr_health")
    health = _table(
        table["pr_health"],
        health_where,
        ("lint_checks", "labels", "unknown_retries", "unknown_wait_seconds", "rerun"),
    )
    labels_where = _at(health_where, "labels")
    cause_labels = _mapping(health["labels"], labels_where)
    for cause, label in cause_labels.items():
        _names_in([cause], PR_HEALTH_CAUSES, labels_where, "a cause of repo pr-health")
        _text(label, _at(labels_where, cause))
    if len(set(cause_labels.values())) != len(cause_labels):
        raise _fail(labels_where, "a label is used for two causes")
    return Hygiene(
        report_issue=ReportIssue(
            title=_text(report["title"], _at(report_where, "title")),
            labels=_texts(report["labels"], _at(report_where, "labels")),
        ),
        sweep=_text(table["sweep"], _at(where, "sweep")),
        comments=Comments(
            marker=marker,
            author=_text(comments["author"], _at(comments_where, "author")),
            on_events=_flag(comments["on_events"], _at(comments_where, "on_events")),
            on_sweep=_flag(comments["on_sweep"], _at(comments_where, "on_sweep")),
            max_per_sweep=_count(comments["max_per_sweep"], _at(comments_where, "max_per_sweep")),
            report_only=report_only,
        ),
        thresholds=Thresholds(**{key: _count(limits[key], _at(limits_where, key)) for key in keys}),
        pr_health=PrHealth(
            lint_checks=_texts(health["lint_checks"], _at(health_where, "lint_checks")),
            labels=MappingProxyType(dict(cause_labels)),
            unknown_retries=_count(health["unknown_retries"], _at(health_where, "unknown_retries"), minimum=0),
            unknown_wait_seconds=_count(health["unknown_wait_seconds"], _at(health_where, "unknown_wait_seconds")),
            rerun=_read_rerun(health["rerun"], _at(health_where, "rerun")),
        ),
        attachments=_read_attachments(table["attachments"], _at(where, "attachments")),
        checks=MappingProxyType(checks),
    )


#: The conclusions of a finished check run that fail it (what `rerun.conclusions` may name).
_FAILING_CONCLUSIONS = ("FAILURE", "TIMED_OUT", "CANCELLED", "STARTUP_FAILURE", "ACTION_REQUIRED", "STALE")


def _read_rerun(data: object, where: str) -> RerunRules:
    """The `hygiene.pr_health.rerun` section: the cap, the two text checks, the conclusions and the log patterns."""
    table = _table(data, where, ("max_per_sha", "title_check", "body_check", "conclusions", "log_patterns"))
    conclusions = _texts(table["conclusions"], _at(where, "conclusions"))
    _names_in(conclusions, _FAILING_CONCLUSIONS, _at(where, "conclusions"), "a failing conclusion")
    patterns = _texts(table["log_patterns"], _at(where, "log_patterns"), empty_ok=True)
    for index, pattern in enumerate(patterns):
        try:
            re.compile(pattern)
        except re.error as error:
            raise _fail(f"{_at(where, 'log_patterns')}[{index}]", f"not a regular expression ({error})") from error
    return RerunRules(
        max_per_sha=_count(table["max_per_sha"], _at(where, "max_per_sha")),
        title_check=_text(table["title_check"], _at(where, "title_check")),
        body_check=_text(table["body_check"], _at(where, "body_check")),
        conclusions=conclusions,
        log_patterns=patterns,
    )


def _read_workspace(data: object, where: str) -> Workspace:
    """The `workspace` section (local only)."""
    keys = (
        "worktrees",
        "scratch",
        "disk_min_free_gb",
        "disk_margin_gb",
        "build_idle_hours",
        "busy_marker",
        "remove_worktree_when_merged",
        "game_capture_suffixes",
        "scratch_skip_folders",
        "shared_scratch",
        "clips",
    )
    table = _table(data, where, keys)
    clips_where = _at(where, "clips")
    clips = _table(table["clips"], clips_where, ("folder", "max_seconds", "warn_mb", "keep_hours"))
    return Workspace(
        worktrees=_text(table["worktrees"], _at(where, "worktrees")),
        scratch=_text(table["scratch"], _at(where, "scratch")),
        disk_min_free_gb=_count(table["disk_min_free_gb"], _at(where, "disk_min_free_gb")),
        disk_margin_gb=_count(table["disk_margin_gb"], _at(where, "disk_margin_gb"), minimum=0),
        build_idle_hours=_count(table["build_idle_hours"], _at(where, "build_idle_hours"), minimum=0),
        busy_marker=_text(table["busy_marker"], _at(where, "busy_marker")),
        remove_worktree_when_merged=_flag(
            table["remove_worktree_when_merged"], _at(where, "remove_worktree_when_merged")
        ),
        game_capture_suffixes=_texts(table["game_capture_suffixes"], _at(where, "game_capture_suffixes")),
        scratch_skip_folders=_texts(table["scratch_skip_folders"], _at(where, "scratch_skip_folders")),
        shared_scratch=_texts(table["shared_scratch"], _at(where, "shared_scratch")),
        clips=Clips(
            folder=_text(clips["folder"], _at(clips_where, "folder")),
            max_seconds=_count(clips["max_seconds"], _at(clips_where, "max_seconds")),
            warn_mb=_count(clips["warn_mb"], _at(clips_where, "warn_mb")),
            keep_hours=_count(clips["keep_hours"], _at(clips_where, "keep_hours")),
        ),
    )


def _read_stamps(data: object, where: str) -> Stamps:
    """The `stamps` section: the stamp log's steps, signing and pairs rules."""
    table = _table(data, where, ("namespace", "signers_file", "segment_bytes", "steps", "signed", "results", "pairs"))
    steps = _texts(table["steps"], _at(where, "steps"))
    signed = _texts(table["signed"], _at(where, "signed"))
    _names_in(signed, steps, _at(where, "signed"), "a step in stamps.steps")
    pairs_where = _at(where, "pairs")
    pairs = _table(table["pairs"], pairs_where, ("kinds", "relations", "stand_in_markers"))
    relations = _texts(pairs["relations"], _at(pairs_where, "relations"))
    return Stamps(
        namespace=_text(table["namespace"], _at(where, "namespace")),
        signers_file=_text(table["signers_file"], _at(where, "signers_file")),
        segment_bytes=_count(table["segment_bytes"], _at(where, "segment_bytes"), minimum=4096),
        steps=steps,
        signed=signed,
        results=_texts(table["results"], _at(where, "results")),
        pairs=PairRules(
            kinds=_texts(pairs["kinds"], _at(pairs_where, "kinds")),
            relations=relations,
            stand_in_markers=_texts(pairs["stand_in_markers"], _at(pairs_where, "stand_in_markers")),
        ),
    )


def parse_conventions(data: object) -> Conventions:
    """Validate the YAML value of the conventions file and build the typed values; ConventionsError names the key."""
    keys = (
        "repo",
        "paths",
        "commits",
        "pull_requests",
        "merge",
        "issues",
        "claims",
        "tasks",
        "labels",
        "hygiene",
        "workspace",
        "stamps",
    )
    table = _table(data, "", ("version", *keys))
    version = table["version"]
    if version != SUPPORTED_VERSION or isinstance(version, bool):
        raise _fail("version", f"this coney-tools reads version {SUPPORTED_VERSION}")
    labels = _read_labels(table["labels"], "labels")
    issues = _read_issues(table["issues"], "issues")
    hygiene = _read_hygiene(table["hygiene"], "hygiene")
    for spec in hygiene.checks.values():
        for key in spec.keys:
            if not _has_key(data, key):
                raise _fail(f"hygiene.checks.{spec.id}.keys", f"'{key}' is not a key of this file")
    commits = _read_commits(table["commits"], "commits")
    pull_requests = _read_pull_requests(table["pull_requests"], "pull_requests")
    _names_in(
        pull_requests.runtime.areas, commits.title.areas, "pull_requests.runtime.areas", "a key of commits.title.areas"
    )
    runtime_line = pull_requests.runtime.line
    if not pull_requests.runtime.none.startswith(runtime_line):
        raise _fail("pull_requests.runtime.none", f"must start with the line '{runtime_line}'")
    names = labels.names()
    claims = _read_claims(table["claims"], "claims", names)
    _names_in(issues.title_area_by_label, names, "issues.title_area_by_label", "in labels.list")
    for form in issues.forms.values():
        _names_in(form.labels, names, f"issues.forms.{form.name}.labels", "in labels.list")
    _names_in(hygiene.report_issue.labels, names, "hygiene.report_issue.labels", "in labels.list")
    _names_in(hygiene.pr_health.labels.values(), names, "hygiene.pr_health.labels", "in labels.list")
    return Conventions(
        version=SUPPORTED_VERSION,
        repo=_read_repo(table["repo"], "repo"),
        paths=_read_paths(table["paths"], "paths"),
        commits=commits,
        pull_requests=pull_requests,
        merge=_read_merge(table["merge"], "merge"),
        issues=issues,
        claims=claims,
        tasks=_read_tasks(table["tasks"], "tasks"),
        labels=labels,
        hygiene=hygiene,
        workspace=_read_workspace(table["workspace"], "workspace"),
        stamps=_read_stamps(table["stamps"], "stamps"),
    )


def load_conventions_at(root: Path, ref: str) -> Conventions:
    """The conventions as the commit `ref` has them (`git show`), not as the working tree has them: what a check that
    must not trust the change under review reads. ConventionsError when the ref or its file cannot be read."""
    done = subprocess.run(
        ["git", "-C", str(root), "show", f"{ref}:{CONVENTIONS_FILE}"],
        capture_output=True,
        encoding="utf-8",
        check=False,
    )
    if done.returncode != 0:
        raise ConventionsError(f"{ref}:{CONVENTIONS_FILE}: cannot be read ({done.stderr.strip() or 'git failed'})")
    try:
        data = yaml.load(done.stdout, Loader=_Loader)
    except yaml.YAMLError as error:
        raise ConventionsError(f"{ref}:{CONVENTIONS_FILE}: not valid YAML ({error})") from error
    return parse_conventions(data)


def load_conventions(root: Path) -> Conventions:
    """Read and validate `root / .github/conventions.yml`; raises ConventionsError naming the first problem."""
    path = root / CONVENTIONS_FILE
    try:
        data = yaml.load(path.read_text(encoding="utf-8"), Loader=_Loader)
    except OSError as error:
        raise ConventionsError(f"{CONVENTIONS_FILE}: cannot be read ({error.strerror or error})") from error
    except (UnicodeDecodeError, yaml.YAMLError) as error:
        raise ConventionsError(f"{CONVENTIONS_FILE}: not valid YAML ({error})") from error
    return parse_conventions(data)
