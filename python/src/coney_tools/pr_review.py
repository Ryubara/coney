# SPDX-License-Identifier: GPL-3.0-or-later
"""The release gate on a pull request's description: `coney-tools repo review`, `check-review` and `review-stamp`.

`repo check-body` checks a description's structure; it cannot see that a screenshot shows a loading screen instead of
gameplay, that a before and after pair is the same file, or that a count in Testing appears in no log. The
`reviewer` agent (`.claude/agents/reviewer.md`) reads the description, the branch's diff, the logs it cites and
every picture it shows, and writes a verdict next to the description file. This module binds that verdict to what was
reviewed and checks it before a push:

* `review-stamp` prints what a verdict must quote: the branch tip, the description's hash and each local image's hash.
  The agent copies them, so it never computes a hash itself. It first runs the title and body checks (`precheck`, the
  same ones `check-review` and `review` run) and refuses, printing their reasons and no stamp, when they fail: a
  description CI would refuse is not worth a review, and any later edit to it makes the verdict stale.
* `check-review` passes only when the title and body pass the same checks CI runs (`check_title`, `check_body`) and the
  verdict is `pass` with no blocker and every quoted value is still current, so a new commit, an edit or a replaced
  picture sends the description back for review.
* `review` runs the agent headlessly (`claude -p`) and then `check-review`; the launcher is injected for the tests.

The file names, the agent and the hash rule are `pull_requests.review` in `.github/conventions.yml`.
"""

from __future__ import annotations

import argparse
import dataclasses
import hashlib
import json
import re
import subprocess
import sys
from collections.abc import Callable, Sequence
from pathlib import Path
from typing import Any

import yaml

from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import Conventions, Review, Trailers, load_conventions
from coney_tools.repo_checks import branch_trailer_models, check_body, check_title, image_targets

#: Reads a worktree's HEAD commit: the tip a verdict must quote.
HeadReader = Callable[[Path], str]
#: Reads the models a branch's commits credit in `Co-Authored-By` trailers, given its worktree, its base and the
#: conventions' trailer rules (the real one is `branch_trailer_models`).
ModelReader = Callable[[Path, str, Trailers], list[str]]
#: Runs a command in a folder and returns its exit code (the real one starts `claude`).
Launcher = Callable[[list[str], Path], int]

#: The verdicts a reviewer may give; only the first releases.
VERDICTS = ("pass", "fail")
#: How serious a finding is: a blocker holds the release, a note does not.
SEVERITIES = ("blocker", "note")
#: The keys of a verdict file, each required.
VERDICT_KEYS = ("tip", "body_sha256", "verdict", "images", "findings")
#: The keys of one finding.
FINDING_KEYS = ("severity", "where", "text")
#: The agent definition handed to `claude --agents`, written next to the description for the run and removed after.
AGENTS_FILE = ".reviewer-agents.json"
#: What a branch is compared with unless told otherwise.
DEFAULT_BASE = "origin/main"
#: How long one review may take before it is stopped.
DEFAULT_TIMEOUT_MINUTES = 30

_SHA1 = re.compile(r"[0-9a-f]{40}")
_SHA256 = re.compile(r"[0-9a-f]{64}")
_REMOTE = re.compile(r"^[a-z][a-z0-9+.-]*:", re.IGNORECASE)


class ReviewError(ConfigError):
    """A review cannot run or be checked at all: a file is unreadable, git or `claude` fails, an agent file is bad."""


# --- what a verdict quotes ------------------------------------------------------------------------------------------


def description_sha256(text: str) -> str:
    """The description's hash (`sha256-lf`): SHA-256 of its UTF-8 text, leading BOM dropped, CRLF and CR made LF.

    Normalised so that a checkout or an editor that changes line endings does not make a verdict stale.
    """
    text = text.removeprefix("﻿").replace("\r\n", "\n").replace("\r", "\n")
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def read_description(path: Path) -> str:
    """The description file's text; ReviewError when it cannot be read."""
    try:
        return path.read_bytes().decode("utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise ReviewError(f"{path}: cannot be read ({error})") from error


def local_images(description: str) -> list[str]:
    """The images (Markdown or `<img>`) the description shows from local files, each once, in order of appearance.

    An uploaded image (any URL) is left out: it is already on GitHub and was reviewed when it was uploaded.
    """
    found: list[str] = []
    for target in image_targets(description):
        if target and not _REMOTE.match(target) and target not in found:
            found.append(target)
    return found


def _file_sha256(path: Path) -> str | None:
    """SHA-256 of a file's bytes, or None when it does not exist."""
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError:
        return None


def git_head(worktree: Path) -> str:
    """The commit a worktree's HEAD points at; ReviewError when git cannot say."""
    try:
        result = subprocess.run(
            ["git", "-C", str(worktree), "rev-parse", "HEAD"], capture_output=True, text=True, check=False
        )
    except OSError as error:
        raise ReviewError(f"git cannot be run ({error})") from error
    if result.returncode != 0:
        raise ReviewError(f"{worktree}: not a git worktree ({result.stderr.strip()})")
    return result.stdout.strip()


def make_stamp(pr_file: Path, worktree: Path, head_of: HeadReader | None = None) -> dict[str, object]:
    """What a verdict on `pr_file` must quote now: `tip`, `body_sha256` and `images` (path and hash; None if gone)."""
    description = read_description(pr_file)
    images = [{"path": image, "sha256": _file_sha256(pr_file.parent / image)} for image in local_images(description)]
    return {
        "tip": (head_of or git_head)(worktree),
        "body_sha256": description_sha256(description),
        "images": images,
    }


# --- check-review ---------------------------------------------------------------------------------------------------


def _verdict_shape(data: object) -> list[str]:
    """What is wrong with the shape of a verdict file's data; empty when every key is there with the right type."""
    if not isinstance(data, dict):
        return ["the verdict file is not a JSON object"]
    problems = [f"the verdict file is missing the key '{key}'" for key in VERDICT_KEYS if key not in data]
    problems += [f"the verdict file has an unknown key '{key}'" for key in data if key not in VERDICT_KEYS]
    if problems:
        return problems
    if not isinstance(data["tip"], str) or not _SHA1.fullmatch(data["tip"]):
        problems.append("tip: expected a full 40-character commit id")
    if not isinstance(data["body_sha256"], str) or not _SHA256.fullmatch(data["body_sha256"]):
        problems.append("body_sha256: expected 64 lower-case hex digits")
    if data["verdict"] not in VERDICTS:
        problems.append(f"verdict: expected one of {', '.join(VERDICTS)}, got {data['verdict']!r}")
    images = data["images"]
    if not isinstance(images, list) or not all(
        isinstance(image, dict) and isinstance(image.get("path"), str) for image in images
    ):
        problems.append("images: expected a list of {path, sha256}")
    findings = data["findings"]
    if not isinstance(findings, list):
        problems.append("findings: expected a list")
    else:
        for number, finding in enumerate(findings):
            if not isinstance(finding, dict) or set(finding) != set(FINDING_KEYS):
                problems.append(f"findings[{number}]: expected exactly the keys {', '.join(FINDING_KEYS)}")
            elif finding["severity"] not in SEVERITIES:
                problems.append(f"findings[{number}]: severity must be one of {', '.join(SEVERITIES)}")
    return problems


def _blockers(findings: list[dict[str, Any]]) -> list[str]:
    """The blocker findings, one line each: where, then what."""
    return [f"{finding['where']}: {finding['text']}" for finding in findings if finding["severity"] == "blocker"]


def _image_problems(stamp_images: list[dict[str, Any]], reviewed: list[dict[str, Any]], rules: Review) -> list[str]:
    """Images that are missing, identical to another, unreviewed or changed since the review."""
    problems: list[str] = []
    seen: dict[str, str] = {}
    by_path = {image["path"]: image.get("sha256") for image in reviewed}
    for image in stamp_images:
        path, digest = image["path"], image["sha256"]
        if digest is None:
            problems.append(f"{path}: the image is missing (paths are relative to the description file)")
            continue
        if rules.identical_images == "refused" and digest in seen:
            problems.append(f"{path} and {seen[digest]} are byte-identical: a capture went wrong, take it again")
        seen.setdefault(digest, path)
        if path not in by_path:
            problems.append(f"{path}: not reviewed (the verdict does not list it)")
        elif by_path[path] != digest:
            problems.append(f"{path}: changed since the review (stale verdict)")
    return problems


def description_problems(description: str, conventions: Conventions, branch_models: Sequence[str] = ()) -> list[str]:
    """What CI's `title` and `body` jobs would refuse in the description file's text, each reason prefixed `title:` or
    `body:`.

    The file follows the PR.md convention (the title on line 1, the body after it); the coordinator passes them to
    `gh pr create` as the title and `--body-file`. A local
    image is accepted where CI wants an uploaded one, because `gh --attach` uploads it when the pull request is opened
    (a missing or identical picture is the verdict check's business). `branch_models` are the models the branch's
    commits credit in trailers: CI sees only the description and passes none, the local gate passes the branch's.
    """
    text = description.removeprefix("﻿").replace("\r\n", "\n").replace("\r", "\n")
    title, _, body = text.partition("\n")
    rules = conventions.pull_requests
    draft_rules = dataclasses.replace(rules, screenshots=dataclasses.replace(rules.screenshots, uploaded_only=False))
    return [f"title: {problem}" for problem in check_title(title, conventions.commits.title)] + [
        f"body: {problem}" for problem in check_body(body, draft_rules, branch_models, title)
    ]


def precheck(
    pr_file: Path,
    worktree: Path,
    conventions: Conventions,
    models_of: ModelReader | None = None,
    base: str = DEFAULT_BASE,
) -> list[str]:
    """What is wrong with the description file before anything is reviewed: the title and body checks CI runs, plus the
    Model line held to the trailers of the branch's commits after `base`; empty when it is fit to review.

    The one place the rule lives: `check-review` (as the first half of its answer), `review` (before the agent starts)
    and `review-stamp` (before the agent has a stamp to copy) all call it. ReviewError when the description cannot be
    read or the commits cannot be listed.
    """
    models = (models_of or branch_trailer_models)(worktree, base, conventions.commits.trailers)
    return description_problems(read_description(pr_file), conventions, models)


def check_review(
    pr_file: Path,
    worktree: Path,
    conventions: Conventions,
    head_of: HeadReader | None = None,
    models_of: ModelReader | None = None,
    base: str = DEFAULT_BASE,
) -> list[str]:
    """Why `pr_file` does not release the branch in `worktree`; empty when it does.

    Both halves are reported together: the title and body problems CI would find (`description_problems`), then the
    verdict's. The body check also holds the description's Model line to the trailers of the commits after `base`,
    which only the local gate can read. ReviewError when the description itself cannot be read, the worktree has no
    HEAD or its commits cannot be listed.
    """
    problems = precheck(pr_file, worktree, conventions, models_of, base)
    return problems + _verdict_problems(pr_file, worktree, conventions.pull_requests.review, head_of)


def _verdict_problems(pr_file: Path, worktree: Path, rules: Review, head_of: HeadReader | None) -> list[str]:
    """Why the verdict next to `pr_file` does not release the branch in `worktree`; empty when it does."""
    stamp = make_stamp(pr_file, worktree, head_of)
    verdict_path = pr_file.parent / rules.verdict_file
    if not verdict_path.is_file():
        return [f"no {rules.verdict_file} next to {pr_file.name}: run `coney-tools repo review` first"]
    try:
        data = json.loads(verdict_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, ValueError) as error:
        return [f"{rules.verdict_file} is not JSON ({error})"]
    problems = _verdict_shape(data)
    if problems:
        return [f"{rules.verdict_file}: {problem}" for problem in problems]
    blockers = _blockers(data["findings"])
    if data["verdict"] != "pass":
        problems.append(f"the verdict is {data['verdict']}")
    elif blockers:
        problems.append("the verdict is pass but lists a blocker finding")
    problems += [f"blocker: {blocker}" for blocker in blockers]
    if data["tip"] != stamp["tip"]:
        problems.append(
            f"stale verdict: it reviewed tip {data['tip'][:12]}, the worktree is at {str(stamp['tip'])[:12]}"
        )
    if data["body_sha256"] != stamp["body_sha256"]:
        problems.append(f"stale verdict: the description changed since the review ({pr_file.name})")
    stamp_images = stamp["images"]
    assert isinstance(stamp_images, list)
    problems += _image_problems(stamp_images, data["images"], rules)
    return problems


# --- the reviewer agent and the launcher ----------------------------------------------------------------------------


def agent_definition(agent_file: Path) -> dict[str, object]:
    """An agent file (`.claude/agents/<name>.md`) as `claude --agents` takes one: description, prompt, tools, model.

    The front matter between the `---` lines gives the fields; the Markdown after it is the prompt.
    """
    try:
        text = agent_file.read_text(encoding="utf-8").replace("\r\n", "\n")
    except (OSError, UnicodeDecodeError) as error:
        raise ReviewError(f"{agent_file}: cannot be read ({error})") from error
    match = re.match(r"---\n(?P<front>.*?)\n---\n(?P<prompt>.*)", text, re.DOTALL)
    if match is None:
        raise ReviewError(f"{agent_file}: no front matter between --- lines")
    front = yaml.safe_load(match.group("front"))
    if not isinstance(front, dict) or not isinstance(front.get("description"), str):
        raise ReviewError(f"{agent_file}: the front matter needs a description")
    definition: dict[str, object] = {"description": front["description"], "prompt": match.group("prompt").strip()}
    for key in ("tools", "disallowedTools"):
        if isinstance(front.get(key), str):
            definition[key] = [tool.strip() for tool in front[key].split(",") if tool.strip()]
    if isinstance(front.get("model"), str):
        definition["model"] = front["model"]
    return definition


def review_command(
    claude: str, agents_file: Path, agent: str, model: str, prompt: str, effort: str | None = None
) -> list[str]:
    """The `claude` command line that runs `agent` once, headless, with no permission prompts (a throwaway run)."""
    command = [claude, "-p", "--agents", str(agents_file), "--agent", agent, "--model", model]
    if effort is not None:
        command += ["--effort", effort]
    return [*command, "--dangerously-skip-permissions", prompt]


def review_prompt(pr_file: Path, worktree: Path, base: str, rules: Review) -> str:
    """The task the launcher gives the agent: which files, which diff, the stamp command and where to write."""
    stamp = f'"{sys.executable}" -m coney_tools.cli repo review-stamp "{pr_file}" --worktree "{worktree}" --base {base}'
    return (
        f"Review the pull request description in {pr_file} before it is released.\n"
        f'- The branch is checked out in {worktree}; its change is `git -C "{worktree}" diff {base}...HEAD`.\n'
        f"- Run `{stamp}` and copy its tip, body_sha256 and images into the verdict.\n"
        f"- Write {rules.verdict_file} and {rules.report_file} in {pr_file.parent} and nothing else.\n"
        "Follow your instructions to the letter: open every image the description shows."
    )


def launch_claude(command: list[str], cwd: Path, timeout_minutes: float = DEFAULT_TIMEOUT_MINUTES) -> int:
    """Run `command` in `cwd`, its output going to ours, and return its exit code (124 when it timed out)."""
    try:
        return subprocess.run(
            command, cwd=cwd, stdin=subprocess.DEVNULL, check=False, timeout=timeout_minutes * 60
        ).returncode
    except FileNotFoundError as error:
        raise ReviewError(f"{command[0]}: not found; install Claude Code or pass --claude") from error
    except subprocess.TimeoutExpired:
        print(f"coney-tools: the review took longer than {timeout_minutes:g} minutes and was stopped", file=sys.stderr)
        return 124


def run_review(
    pr_file: Path,
    worktree: Path,
    base: str,
    root: Path,
    conventions: Conventions,
    *,
    launch: Launcher | None = None,
    head_of: HeadReader | None = None,
    models_of: ModelReader | None = None,
    claude: str = "claude",
    model: str | None = None,
    effort: str | None = None,
) -> int:
    """Run the reviewer agent of the checkout at `root` on `pr_file`, then check its verdict; 0 when it releases.

    A description that fails the title or body checks is refused before the agent starts. The agent runs in the
    description's folder (the scratch folder), so whatever it writes lands there. An old verdict
    is removed first: a run that fails to write one must not leave an earlier pass behind.
    """
    rules = conventions.pull_requests.review
    pr_file = pr_file.resolve()
    worktree = worktree.resolve()
    folder = pr_file.parent
    # A description CI would refuse is not worth a review run: send it back first.
    early = precheck(pr_file, worktree, conventions, models_of, base)
    if early:
        return _report(early, folder / rules.report_file)
    definition = agent_definition(root / ".claude" / "agents" / f"{rules.agent}.md")
    for name in (rules.verdict_file, rules.report_file):
        (folder / name).unlink(missing_ok=True)
    agents_file = folder / AGENTS_FILE
    agents_file.write_text(json.dumps({rules.agent: definition}), encoding="utf-8")
    chosen = model or str(definition.get("model", "sonnet"))
    command = review_command(
        claude, agents_file, rules.agent, chosen, review_prompt(pr_file, worktree, base, rules), effort
    )
    try:
        status = (launch or launch_claude)(command, folder)
    finally:
        agents_file.unlink(missing_ok=True)
    if status != 0:
        print(f"review: {claude} exited with {status}", file=sys.stderr)
        return 1
    return _report(check_review(pr_file, worktree, conventions, head_of, models_of, base), folder / rules.report_file)


def _report(problems: list[str], report: Path) -> int:
    """Print the gate's answer; 1 when refused."""
    if problems:
        print("review refused:")
        for problem in problems:
            print(f"  {problem}")
        if report.is_file():
            print(f"  (the reviewer's report: {report})")
        return 1
    print("review: ok")
    return 0


# --- the command line -----------------------------------------------------------------------------------------------


def add_review_parsers(repo_commands: argparse._SubParsersAction[argparse.ArgumentParser]) -> None:
    """Add `repo review`, `repo check-review` and `repo review-stamp` to the `repo` command group."""
    wt_help = "the branch's worktree (its HEAD is the tip the verdict must quote)"
    pr_help = "the description file: the title on line 1, then the body (PR.md in the scratch folder)"
    base_help = "what the branch is compared with, for the diff and the commits' trailers (default: %(default)s)"
    review = repo_commands.add_parser(
        "review",
        help="run the reviewer agent headlessly on a description, then check its verdict",
        description=(
            "Runs `claude -p` with the reviewer agent in the description's folder; the agent writes the verdict "
            "and a report there. Then runs check-review. Exit 0 when the verdict releases the branch, 1 when not, "
            "2 when the review cannot run."
        ),
    )
    review.add_argument("pr_file", type=Path, help=pr_help)
    review.add_argument("--worktree", type=Path, required=True, help=wt_help)
    review.add_argument("--base", default=DEFAULT_BASE, help=base_help)
    review.add_argument("--claude", default="claude", help="the Claude Code executable (default: %(default)s)")
    review.add_argument("--model", help="the model (default: the agent file's)")
    review.add_argument("--effort", choices=("low", "medium", "high", "xhigh", "max"), help="the effort level")
    review.add_argument(
        "--timeout-minutes", type=float, default=DEFAULT_TIMEOUT_MINUTES, help="stop the review after this long"
    )
    check = repo_commands.add_parser(
        "check-review",
        help=(
            "check the title and body as CI does, and that the reviewer's verdict passes and is current "
            "(tip, description and images unchanged)"
        ),
    )
    check.add_argument("pr_file", type=Path, help=pr_help)
    check.add_argument("--worktree", type=Path, required=True, help=wt_help)
    check.add_argument("--base", default=DEFAULT_BASE, help=base_help)
    stamp = repo_commands.add_parser(
        "review-stamp",
        help=(
            "print, as JSON, the tip, description hash and image hashes a verdict must quote; refuses, with the "
            "title and body reasons, a description check-review would refuse"
        ),
    )
    stamp.add_argument("pr_file", type=Path, help=pr_help)
    stamp.add_argument("--worktree", type=Path, required=True, help=wt_help)
    stamp.add_argument("--base", default=DEFAULT_BASE, help=base_help)


def run_review_command(args: argparse.Namespace) -> int:
    """Run `repo review`, `check-review` or `review-stamp`."""
    if args.command == "review-stamp":
        # The agent runs this in the description's folder (the scratch folder), outside any checkout, so the rules
        # come from the checkout this tool is installed in (the one `review` started it from), never the current folder.
        conventions = load_conventions(find_repo_root(Path(__file__)))
        # Refuse before the reviewer has a stamp to copy: no stamp on stdout, the reasons on stderr.
        problems = precheck(args.pr_file, args.worktree, conventions, base=args.base)
        if problems:
            print("review-stamp refused: fix the description first, then stamp and review it:", file=sys.stderr)
            for problem in problems:
                print(f"  {problem}", file=sys.stderr)
            return 1
        print(json.dumps(make_stamp(args.pr_file, args.worktree), indent=2))
        return 0
    root = find_repo_root(Path.cwd())
    conventions = load_conventions(root)
    rules = conventions.pull_requests.review
    if args.command == "check-review":
        problems = check_review(args.pr_file, args.worktree, conventions, base=args.base)
        return _report(problems, args.pr_file.parent / rules.report_file)
    return run_review(
        args.pr_file,
        args.worktree,
        args.base,
        root,
        conventions,
        launch=lambda command, cwd: launch_claude(command, cwd, args.timeout_minutes),
        claude=args.claude,
        model=args.model,
        effort=args.effort,
    )
