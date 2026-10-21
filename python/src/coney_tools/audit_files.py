# SPDX-License-Identifier: GPL-3.0-or-later
"""The `files` checks of `coney-tools repo audit`: the checkout against `.github/conventions.yml`, with no network.

The issue forms, the pull request template, the CI scope script and pre-commit's size limit cannot read the
conventions file, so each keeps a copy of part of it; these checks fail when a copy and the file differ, and name both.
CI runs them on every pull request (the `python` workflow), so a rule is changed in the file and in the one copy that
needs the same edit, or the pull request does not merge.
"""

from __future__ import annotations

import os
import re
import subprocess
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import yaml

from coney_tools.audit import Problem, check
from coney_tools.conventions import CONVENTIONS_FILE, Conventions, IssueForm
from coney_tools.conventions_render import stale_files
from coney_tools.repo_checks import check_pointer_files, strip_comments

#: Where the CI scope script reads the docs-only paths from (relative to the repository root).
CI_SCOPE_SCRIPT = ".github/scripts/ci-scope.sh"
#: pre-commit's configuration.
PRE_COMMIT_CONFIG = ".pre-commit-config.yaml"
#: The issue forms' directory settings.
ISSUE_CONFIG = ".github/ISSUE_TEMPLATE/config.yml"


@dataclass(frozen=True)
class FilesContext:
    """What a `files` check reads: the checkout and its conventions."""

    root: Path
    conventions: Conventions


def _read(root: Path, name: str) -> str | None:
    """The text of the repository file `name`, or None when it is missing or not text."""
    try:
        return (root / name).read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError):
        return None


def _yaml(root: Path, name: str) -> Any:
    """The YAML of the repository file `name`; None when it is missing or not YAML."""
    text = _read(root, name)
    if text is None:
        return None
    try:
        return yaml.safe_load(text)
    except yaml.YAMLError:
        return None


# --- files.pointer and files.exist ------------------------------------------------------------------------------


@check("files.pointer")
def pointer_files(context: FilesContext) -> Iterator[Problem]:
    """CLAUDE.md and GEMINI.md hold the import line and nothing of their own."""
    for message in check_pointer_files(context.root, context.conventions.paths.pointer_files):
        name, _, problem = message.partition(": ")
        yield Problem(name, problem)


@check("files.exist")
def named_files_exist(context: FilesContext) -> Iterator[Problem]:
    """Every file or folder the conventions name is in the checkout."""
    conventions = context.conventions
    named: list[tuple[str, str]] = [(f"repo.tools.{role}", path) for role, path in conventions.repo.tools.items()]
    named.append(("pull_requests.template", conventions.pull_requests.template))
    named += [("paths.pointer_files", name) for name in conventions.paths.pointer_files.files]
    named += [("paths.address_allowed_in", name) for name in conventions.paths.address_allowed_in]
    named += [(f"issues.forms.{form.name}", form.file) for form in conventions.issues.forms.values()]
    named += [("paths.code", prefix) for prefix in conventions.paths.code]
    named += [("paths.docs", prefix) for prefix in conventions.paths.docs]
    for generated in conventions.paths.generated:
        # A path another tool writes (research/*.yaml tables) is not in the repository; coney-tools' generators are.
        if generated.by.startswith("coney-tools "):
            named.append(("paths.generated", generated.path))
        named += [("paths.generated", excluded) for excluded in generated.excluding]
    for key, path in named:
        if not (context.root / path).exists():
            yield Problem(path, f"{key} names it, but it does not exist")


# --- files.address ------------------------------------------------------------------------------------------------

_REMOTE_URL = re.compile(r"github\.com[:/](?P<owner>[^/\s]+)/(?P<name>[^/\s]+?)(?:\.git)?/?$")


def repository_address_terms(root: Path) -> list[str]:
    """The strings that spell the repository's and the docs site's address, lower case.

    From `GITHUB_REPOSITORY` (set in CI) or the checkout's `origin` remote; empty when neither says. The address is
    never written in a file of the repository (only the README carries it), so it is read from where it is known.
    """
    owner_name = os.environ.get("GITHUB_REPOSITORY", "")
    if "/" not in owner_name:
        try:
            done = subprocess.run(
                ["git", "remote", "get-url", "origin"],
                cwd=root,
                capture_output=True,
                text=True,
                encoding="utf-8",
                timeout=30,
                check=False,
            )
        except (OSError, subprocess.SubprocessError):
            return []
        found = _REMOTE_URL.search(done.stdout.strip()) if done.returncode == 0 else None
        if not found:
            return []
        owner_name = f"{found.group('owner')}/{found.group('name')}"
    owner, _, name = owner_name.lower().partition("/")
    return [f"github.com/{owner}/{name}", f"github.com:{owner}/{name}", f"{owner}.github.io/{name}"]


def tracked_files(root: Path) -> list[str]:
    """The repository-relative paths git tracks (forward slashes); every file under `root` when it is not a checkout."""
    try:
        done = subprocess.run(["git", "ls-files", "-z"], cwd=root, capture_output=True, timeout=120, check=False)
    except (OSError, subprocess.SubprocessError):
        done = None
    if done is not None and done.returncode == 0 and (root / ".git").exists():
        return [name for name in done.stdout.decode("utf-8", errors="replace").split("\0") if name]
    return sorted(
        path.relative_to(root).as_posix()
        for path in root.rglob("*")
        if path.is_file() and ".git" not in path.relative_to(root).parts
    )


@check("files.address")
def address_only_in_readme(context: FilesContext) -> Iterator[Problem]:
    """No tracked file but the allowed ones spells the repository's or the docs site's address."""
    terms = [term.encode() for term in repository_address_terms(context.root)]
    if not terms:
        return
    allowed = set(context.conventions.paths.address_allowed_in)
    for name in tracked_files(context.root):
        if name in allowed:
            continue
        try:
            data = (context.root / name).read_bytes().lower()
        except OSError:
            continue
        if any(term in data for term in terms):
            yield Problem(name, "holds the repository's or the docs site's address; only the README may")


# --- files.forms --------------------------------------------------------------------------------------------------


def _form_inputs(data: dict[str, Any]) -> list[dict[str, Any]]:
    """The fields of an issue form that ask for something (its introductory text is not one)."""
    return [item for item in data.get("body", []) if item.get("type") != "markdown"]


def _field_required(item: dict[str, Any]) -> bool:
    """Whether a form field is required; a checkboxes field is when all its boxes are."""
    if item["type"] == "checkboxes":
        return all(option.get("required", False) for option in item["attributes"]["options"])
    return bool(item.get("validations", {}).get("required", False))


def _check_form(context: FilesContext, form: IssueForm) -> Iterator[str]:
    """What differs between one issue form and its entry in the conventions."""
    data = _yaml(context.root, form.file)
    if not isinstance(data, dict):
        yield "cannot be read as an issue form"
        return
    if data.get("title") != form.title_prefix:
        yield f"title is {data.get('title')!r}, the conventions say {form.title_prefix!r}"
    if data.get("labels") != list(form.labels):
        yield f"labels are {data.get('labels')!r}, the conventions say {list(form.labels)!r}"
    try:
        found = [(item["id"], item["attributes"]["label"], _field_required(item)) for item in _form_inputs(data)]
    except (KeyError, TypeError):
        yield "a field has no id, label or attributes"
        return
    wanted = [(section.id, section.heading, section.required) for section in form.sections]
    if found != wanted:
        yield (
            "fields (id, label, required) differ from issues.forms."
            f"{form.name}.sections: the form has {found}, the conventions say {wanted}"
        )
    shots = [item for item in _form_inputs(data) if item.get("id") == "screenshots"]
    none_line = context.conventions.issues.screenshots_none
    if shots and none_line not in str(shots[0].get("attributes", {}).get("description", "")):
        yield f"the screenshots field does not tell the reporter about the line '{none_line} (<reason>)'"
    if form.name == "task":
        areas = [item for item in _form_inputs(data) if item.get("id") == "area"]
        options = areas[0].get("attributes", {}).get("options") if areas else None
        area_labels = list(context.conventions.labels.names("area"))
        if options != area_labels:
            yield f"the area options are {options!r}, the A- labels of the conventions are {area_labels!r}"


@check("files.forms")
def forms_match(context: FilesContext) -> Iterator[Problem]:
    """Each issue form has the title, labels, fields and area options of `issues.forms`, and blank issues are off."""
    for form in context.conventions.issues.forms.values():
        if (context.root / form.file).is_file():
            for message in _check_form(context, form):
                yield Problem(form.file, message)
    config = _yaml(context.root, ISSUE_CONFIG)
    wanted = context.conventions.issues.blank_issues
    if isinstance(config, dict) and config.get("blank_issues_enabled") is not wanted:
        yield Problem(ISSUE_CONFIG, f"blank_issues_enabled is not {str(wanted).lower()} as issues.blank_issues says")


# --- files.template -----------------------------------------------------------------------------------------------


@check("files.template")
def template_matches(context: FilesContext) -> Iterator[Problem]:
    """The pull request template has the headings, AI use lines and instructions the conventions describe."""
    rules = context.conventions.pull_requests
    name = rules.template
    template = _read(context.root, name)
    if template is None:
        return
    headings = tuple(re.findall(r"^## (.+?)\s*$", strip_comments(template), re.MULTILINE))
    if headings != rules.sections:
        yield Problem(name, f"headings are {list(headings)}, pull_requests.sections says {list(rules.sections)}")
    for label in (rules.ai_use.written_by.label, rules.ai_use.model.label):
        if f"**{label}**" not in template:
            yield Problem(name, f"no line '**{label}**' (pull_requests.ai_use)")
    listed = re.search(re.escape(rules.ai_use.written_by.label) + r"\*\*\s*<!--\s*(.+?)\s*-->", template)
    if listed is None or tuple(listed.group(1).split(" / ")) != rules.ai_use.written_by.choices:
        yield Problem(
            name,
            f"the comment after '{rules.ai_use.written_by.label}' does not list the choices "
            f"{' / '.join(rules.ai_use.written_by.choices)}",
        )
    closing = f"`{rules.closing.form}`".replace("#N", "#<number>")
    for text, key in (
        (f"`{rules.docs_none.line}`", "pull_requests.docs_none"),
        (closing, "pull_requests.closing"),
        ("Done:", "pull_requests.checklist"),
        (f"`{rules.screenshots.none} (<reason>)`", "pull_requests.screenshots"),
        (f"`{rules.runtime.none} (<reason>)`", "pull_requests.runtime"),
        (f"{rules.runtime.line} ...", "pull_requests.runtime"),
    ):
        if text not in template:
            yield Problem(name, f"does not mention {text} ({key})")


# --- files.generated-blocks, files.ci-scope, files.large-file-limit ---------------------------------------------


@check("files.generated-blocks")
def generated_blocks_current(context: FilesContext) -> Iterator[Problem]:
    """The generated blocks of AGENTS.md, CONTRIBUTING.md and the guides are what the conventions render."""
    for name in stale_files(context.root, context.conventions):
        yield Problem(name, "a generated block is out of date (run `coney-tools repo render`)")


#: The line of ci-scope.sh that names the file it reads.
_SCOPE_READS_FILE = re.compile(r"conventions\.yml")
#: A regular expression of paths the script must not hold itself (`DOCS_ONLY='^(docs/|...)'`): it reads the lists.
_HARD_CODED_SCOPE = re.compile(r"^\s*[A-Z_]+\s*=\s*'\^\(", re.MULTILINE)
#: How the script finds a list in the file: the items under `  <key>:` written one per line as `    - "pattern"`.
_SCOPE_ITEM = re.compile(r'^    - "(?P<pattern>[^"]+)"[ \t]*(?:#.*)?$')
#: The CI scope lists of `paths` that ci-scope.sh reads, in the order it tries them.
SCOPE_LISTS = ("docs_only", "tools_only", "engine")


def scope_list_as_written(conventions_text: str, key: str) -> list[str]:
    """The patterns of `paths.<key>` read the way ci-scope.sh reads them: quoted, one per line, under the key."""
    found: list[str] = []
    inside = False
    for line in conventions_text.splitlines():
        if line.startswith(f"  {key}:"):
            inside = True
        elif inside and (match := _SCOPE_ITEM.match(line)):
            found.append(match.group("pattern"))
        elif inside and line.strip() and not line.lstrip().startswith("#"):
            break
    return found


@check("files.ci-scope")
def ci_scope_reads_the_file(context: FilesContext) -> Iterator[Problem]:
    """ci-scope.sh reads the CI scope lists of `paths` from the conventions file instead of keeping a copy, and can
    parse each of them."""
    script = _read(context.root, CI_SCOPE_SCRIPT)
    if script is None:
        yield Problem(CI_SCOPE_SCRIPT, "is missing or unreadable")
        return
    if _HARD_CODED_SCOPE.search(script):
        yield Problem(
            CI_SCOPE_SCRIPT, "holds its own regular expression of paths (DOCS_ONLY='^(...') instead of reading it"
        )
    if not _SCOPE_READS_FILE.search(script):
        yield Problem(CI_SCOPE_SCRIPT, f"does not read {CONVENTIONS_FILE}")
    text = _read(context.root, CONVENTIONS_FILE) or ""
    for key in SCOPE_LISTS:
        if f"regex_of {key}" not in script:
            yield Problem(CI_SCOPE_SCRIPT, f"does not read paths.{key} (`regex_of {key}`)")
        written = scope_list_as_written(text, key)
        if tuple(written) != getattr(context.conventions.paths, key):
            yield Problem(
                CONVENTIONS_FILE,
                f'paths.{key} must be written one quoted pattern per line (    - "docs/") so that ci-scope.sh can '
                f"read it; the script finds {written}",
            )


#: The pre-commit hook that refuses game data and local records by name (`.pre-commit-config.yaml`).
GAME_FILES_HOOK = "no-game-files"


def _pre_commit_hook(root: Path, hook_id: str) -> dict[str, Any] | None:
    """The entry of the hook `hook_id` in the pre-commit config, or None when the config or the hook is not there."""
    data = _yaml(root, PRE_COMMIT_CONFIG)
    repos = data.get("repos") if isinstance(data, dict) else None
    for repo in repos if isinstance(repos, list) else []:
        hooks = repo.get("hooks") if isinstance(repo, dict) else None
        for hook in hooks if isinstance(hooks, list) else []:
            if isinstance(hook, dict) and hook.get("id") == hook_id:
                return hook
    return None


def _skips_files(hook: dict[str, Any], name: str) -> Iterator[Problem]:
    """A problem for each key of `hook` that narrows the files it sees: such a guard must see every file."""
    for key in ("exclude", "files") if name == "check-added-large-files" else ("exclude",):
        if key in hook:
            yield Problem(PRE_COMMIT_CONFIG, f"{name} has `{key}`: never exclude a file from a game-data guard")


@check("files.large-file-limit")
def large_file_limit_matches(context: FilesContext) -> Iterator[Problem]:
    """pre-commit's check-added-large-files uses the limit of `paths.large_file_kb`, checks every file
    (`--enforce-all`, so CI's run over all files sees committed ones) and excludes none (AGENTS.md: never exclude a
    file from it)."""
    text = _read(context.root, PRE_COMMIT_CONFIG)
    if text is None:
        yield Problem(PRE_COMMIT_CONFIG, "is missing or unreadable")
        return
    wanted = f"--maxkb={context.conventions.paths.large_file_kb}"
    if wanted not in text:
        yield Problem(PRE_COMMIT_CONFIG, f"does not pass {wanted} to check-added-large-files (paths.large_file_kb)")
    hook = _pre_commit_hook(context.root, "check-added-large-files")
    if hook is None:
        return
    if "--enforce-all" not in (hook.get("args") or []):
        yield Problem(PRE_COMMIT_CONFIG, "check-added-large-files lacks --enforce-all, so a committed file is not seen")
    yield from _skips_files(hook, "check-added-large-files")


@check("files.game-files")
def game_files_refused(context: FilesContext) -> Iterator[Problem]:
    """pre-commit's no-game-files hook refuses every file type of `workspace.game_capture_suffixes` and the busy
    marker, and excludes nothing."""
    hook = _pre_commit_hook(context.root, GAME_FILES_HOOK)
    if hook is None:
        yield Problem(
            PRE_COMMIT_CONFIG, f"has no {GAME_FILES_HOOK} hook, so a small file of game data can be committed"
        )
        return
    try:
        pattern = re.compile(str(hook.get("files", "")))
    except re.error as error:
        yield Problem(PRE_COMMIT_CONFIG, f"{GAME_FILES_HOOK}'s `files` is not a regular expression ({error})")
        return
    workspace = context.conventions.workspace
    for suffix in workspace.game_capture_suffixes:
        if not pattern.search(f"scratch/capture{suffix}"):
            yield Problem(
                PRE_COMMIT_CONFIG,
                f"{GAME_FILES_HOOK} does not refuse {suffix} files (workspace.game_capture_suffixes)",
            )
    if not pattern.search(workspace.busy_marker):
        yield Problem(
            PRE_COMMIT_CONFIG, f"{GAME_FILES_HOOK} does not refuse {workspace.busy_marker} (workspace.busy_marker)"
        )
    yield from _skips_files(hook, GAME_FILES_HOOK)


@check("files.markdown-hook")
def markdown_hook_exists(context: FilesContext) -> Iterator[Problem]:
    """pre-commit runs markdownlint-cli2, so a long Markdown line is refused before the commit, as CI would."""
    text = _read(context.root, PRE_COMMIT_CONFIG)
    if text is None:
        yield Problem(PRE_COMMIT_CONFIG, "is missing or unreadable")
    elif not re.search(r"^\s*-\s*id:\s*markdownlint-cli2\s*$", text, re.MULTILINE):
        yield Problem(
            PRE_COMMIT_CONFIG,
            "has no markdownlint-cli2 hook, so only CI sees a long Markdown line",
        )
