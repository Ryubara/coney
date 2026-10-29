# SPDX-License-Identifier: GPL-3.0-or-later
"""The `pr` workflow runs on `pull_request_target` so that a fork's pull request is checked without a maintainer's
approval; these tests hold the four rules that make that safe (the header of .github/workflows/pr.yml): the base branch
is the only code that runs, nothing from the pull request reaches a script's text, the token only reads, and the
checks stay the required ones."""

from __future__ import annotations

import re
from typing import Any

import yaml
from conventions_support import REPO

from coney_tools.conventions import load_conventions

WORKFLOW = REPO / ".github" / "workflows" / "pr.yml"
CONVENTIONS = load_conventions(REPO)


def workflow() -> dict[str, Any]:
    """The parsed workflow."""
    data: dict[str, Any] = yaml.safe_load(WORKFLOW.read_text(encoding="utf-8"))
    return data


def steps() -> list[dict[str, Any]]:
    """Every step of every job."""
    return [step for job in workflow()["jobs"].values() for step in job["steps"]]


def test_it_runs_on_pull_request_target_only_so_a_forks_first_pull_request_is_checked() -> None:
    # YAML 1.1 reads the key `on` as true.
    data: dict[Any, Any] = workflow()
    on: dict[str, Any] = data["on"] if "on" in data else data[True]
    assert set(on) == {"pull_request_target"}
    assert {"opened", "edited", "reopened", "synchronize"} == set(on["pull_request_target"]["types"])


def test_the_jobs_are_the_required_checks_it_always_was() -> None:
    assert {"title", "docs", "body"} <= set(workflow()["jobs"])
    assert {"title", "docs", "body"} <= set(CONVENTIONS.merge.required_checks)


def test_the_checkout_is_the_base_branch_and_keeps_no_credentials() -> None:
    checkouts = [step for step in steps() if str(step.get("uses", "")).startswith("actions/checkout")]
    assert len(checkouts) == 4
    for step in checkouts:
        # No `ref` and no `repository`: on pull_request_target that is the base branch, never the pull request's code.
        assert set(step["with"]) == {"persist-credentials"} and step["with"]["persist-credentials"] is False


def test_no_expression_reaches_a_script_except_through_the_environment() -> None:
    for step in steps():
        assert "${{" not in step.get("run", ""), step
    for job in workflow()["jobs"].values():
        for step in job["steps"]:
            for value in step.get("env", {}).values():
                assert re.fullmatch(
                    r"\$\{\{ (github\.event\.pull_request\.(title|body|number)|github\.(token|repository)) \}\}", value
                )


def test_the_token_reads_and_no_secret_is_used() -> None:
    assert workflow()["permissions"] == {"contents": "read"}
    assert "secrets." not in WORKFLOW.read_text(encoding="utf-8")


def test_only_the_docs_and_claim_jobs_read_more_and_no_job_widens_the_token_further() -> None:
    # `docs` calls the pull request files API; `claim` reads the claim cards on the issues the description closes.
    allowed = {
        "docs": {"contents": "read", "pull-requests": "read"},
        "claim": {"contents": "read", "issues": "read", "pull-requests": "read"},
    }
    for name, job in workflow()["jobs"].items():
        # A job-level block replaces the workflow's, so one with a write scope (or any scope but these) is refused.
        assert job.get("permissions", workflow()["permissions"]) == allowed.get(name, workflow()["permissions"]), name


def test_nothing_from_the_pull_request_is_run_or_built() -> None:
    text = WORKFLOW.read_text(encoding="utf-8")
    for forbidden in ("head.sha", "head.ref", "head_ref", "head.repo", "refs/pull", "gh pr checkout", "git fetch"):
        assert forbidden not in text, forbidden
    assert "ref:" not in text and "repository:" not in text


def test_the_changed_files_come_from_the_api() -> None:
    text = WORKFLOW.read_text(encoding="utf-8")
    assert 'gh api --paginate "repos/$GH_REPO/pulls/$NUMBER/files"' in text
    assert not any("git diff" in step.get("run", "") for step in steps())


def test_two_pull_requests_do_not_share_a_concurrency_group() -> None:
    group = workflow()["concurrency"]["group"]
    assert "github.event.pull_request.number" in group and "github.ref" not in group


def test_the_actions_are_pinned_to_a_commit() -> None:
    assert all(re.search(r"@[0-9a-f]{40}", step["uses"]) for step in steps() if "uses" in step)
