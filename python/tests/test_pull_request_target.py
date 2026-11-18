# SPDX-License-Identifier: GPL-3.0-or-later
"""The workflows that run on `pull_request_target` (docs/guides/repo-conventions.md#pull-request-target).

From 2026-12-02 GitHub blocks that event in a public repository unless its Actions event policy allows the workflow,
and allows it only for workflows that never run a pull request's code. `ci.pull_request_target.workflows` of
.github/conventions.yml lists the files the maintainer allows; these tests refuse the event in any other workflow and
hold each listed one to GitHub's rules: the base branch is the only code checked out, nothing of the pull request is
fetched, run or restored from an artifact, no expression reaches a shell's text, no credential stays in the checkout,
no secret is read, and the token writes nothing but issues and pull requests (their comments and labels)."""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

import pytest
import yaml
from conventions_support import REPO

from coney_tools.conventions import load_conventions

WORKFLOWS = REPO / ".github" / "workflows"
LISTED = load_conventions(REPO).ci.pull_request_target.workflows

# What fetches, names or runs a pull request's own code, or reads a secret or a cache it could have written.
FORBIDDEN = (
    "head.sha",
    "head.ref",
    "head_ref",
    "head.repo",
    "refs/pull",
    "merge_commit_sha",
    "gh pr checkout",
    "git fetch",
    "git pull",
    "git clone",
    "git remote add",
    "gh repo clone",
    "gh pr diff",
    ".patch",
    ".diff",
    "tarball",
    "zipball",
    "download-artifact",
    "actions/cache",
    "cache-mode",
    "allow-unsafe-pr-checkout",
    "secrets.",
)

# The only actions such a workflow uses: the checkout and uv, each pinned to a commit.
ACTIONS = ("actions/checkout@", "astral-sh/setup-uv@")

# The scopes a token there may write: the comments and labels of issues and pull requests.
WRITABLE = {"issues", "pull-requests"}


def parsed(path: Path) -> dict[Any, Any]:
    """The parsed workflow (comments dropped)."""
    data: dict[Any, Any] = yaml.safe_load(path.read_text(encoding="utf-8"))
    return data


def triggers(data: dict[Any, Any]) -> dict[str, Any]:
    """The `on` table; YAML 1.1 reads the key `on` as true."""
    on = data["on"] if "on" in data else data[True]
    return {on: None} if isinstance(on, str) else dict.fromkeys(on) if isinstance(on, list) else on


def steps(data: dict[Any, Any]) -> list[dict[str, Any]]:
    """Every step of every job."""
    return [step for job in data["jobs"].values() for step in job.get("steps", [])]


def test_only_the_listed_workflows_run_on_pull_request_target() -> None:
    using = sorted(path.name for path in WORKFLOWS.glob("*.yml") if "pull_request_target" in triggers(parsed(path)))
    # A new one needs the maintainer to allow it in the Actions event policy first, and the list here with it.
    assert using == sorted(LISTED)


@pytest.mark.parametrize("name", LISTED)
def test_every_checkout_is_the_base_branch_and_keeps_no_credentials(name: str) -> None:
    checkouts = [step for step in steps(parsed(WORKFLOWS / name)) if str(step.get("uses", "")).startswith(ACTIONS[0])]
    assert checkouts, name
    for step in checkouts:
        # No `ref` and no `repository`: on pull_request_target that is the base branch, never the pull request's code.
        assert step.get("with") == {"persist-credentials": False}, name


@pytest.mark.parametrize("name", LISTED)
def test_every_job_runs_its_own_steps(name: str) -> None:
    # A job-level `uses:` (a reusable workflow) would run steps none of these tests sees.
    for job_id, job in parsed(WORKFLOWS / name)["jobs"].items():
        assert "uses" not in job and job.get("steps"), f"{name}: {job_id}"


@pytest.mark.parametrize("name", LISTED)
def test_nothing_of_the_pull_request_is_fetched_run_or_restored(name: str) -> None:
    # Searched in the parsed workflow, so a comment that explains a rule does not trip it.
    text = json.dumps(parsed(WORKFLOWS / name))
    for word in FORBIDDEN:
        assert word not in text, f"{name}: {word}"


@pytest.mark.parametrize("name", LISTED)
def test_no_expression_reaches_a_shell(name: str) -> None:
    for step in steps(parsed(WORKFLOWS / name)):
        # Pull request text (title, body, branch names, comments) reaches a script only through `env`.
        assert "${{" not in step.get("run", ""), f"{name}: {step.get('name')}"


@pytest.mark.parametrize("name", LISTED)
def test_the_actions_are_the_checkout_and_uv_pinned_to_a_commit(name: str) -> None:
    for step in steps(parsed(WORKFLOWS / name)):
        if "uses" in step:
            assert step["uses"].startswith(ACTIONS) and re.search(r"@[0-9a-f]{40}$", step["uses"]), step["uses"]


@pytest.mark.parametrize("name", LISTED)
def test_the_token_writes_only_issues_and_pull_requests(name: str) -> None:
    data = parsed(WORKFLOWS / name)
    # The workflow names its permissions, so the repository's default token never applies.
    blocks = [data["permissions"], *(job["permissions"] for job in data["jobs"].values() if "permissions" in job)]
    for block in blocks:
        assert isinstance(block, dict), f"{name}: {block}"
        written = {scope for scope, level in block.items() if level != "read"}
        assert written <= WRITABLE, f"{name}: {written}"
        assert all(level in ("read", "write") for level in block.values()), name
