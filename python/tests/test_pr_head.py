# SPDX-License-Identifier: GPL-3.0-or-later
"""The one match of a pull request to a branch (`pr_head`): the head repository as well as the branch name, so a fork's
pull request whose branch has the same name as ours is never taken for ours (#1522)."""

import json
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from coney_tools import pr_head, pr_publish


def _pull(branch: str, owner: str = "acme", name: str = "widgets", *, cross: bool | None = False) -> dict[str, Any]:
    """One pull request as `gh pr list --json` shows it, with the head fields."""
    item: dict[str, Any] = {
        "number": 1,
        "headRefName": branch,
        "headRepository": {"id": "R_1", "name": name},
        "headRepositoryOwner": {"id": "U_1", "login": owner},
    }
    if cross is not None:
        item["isCrossRepository"] = cross
    return item


def test_the_fields_name_every_head_field_the_match_reads() -> None:
    fields = set(pr_head.HEAD_FIELDS.split(","))
    assert fields == {"headRefName", "headRepository", "headRepositoryOwner", "isCrossRepository"}


def test_a_pull_request_from_this_repository_is_ours() -> None:
    pull = _pull("285-add-x")
    assert pr_head.is_own(pull)
    assert pr_head.head_label(pull) == "285-add-x"
    assert pr_head.head_repository(pull) == "acme/widgets"
    assert pr_head.head_problem(pull, "285-add-x", "acme/widgets") is None


def test_a_fork_with_the_same_branch_name_is_not_ours() -> None:
    pull = _pull("285-add-x", "contributor", "widgets", cross=True)
    assert not pr_head.is_own(pull)
    assert pr_head.head_label(pull) == "contributor:285-add-x"
    problem = pr_head.head_problem(pull, "285-add-x", "acme/widgets")
    assert problem is not None and "contributor/widgets" in problem


def test_a_pull_request_that_does_not_say_where_its_head_is_counts_as_a_fork() -> None:
    # A caller that forgot the fields must not match a fork by accident: no answer is not "ours".
    pull = _pull("285-add-x", cross=None)
    assert not pr_head.is_own(pull)
    assert pr_head.head_label(pull) == "acme:285-add-x"
    deleted = {"headRefName": "285-add-x", "headRepository": None, "isCrossRepository": True}
    assert pr_head.head_label(deleted) == "?:285-add-x"
    assert pr_head.head_problem(deleted, "285-add-x", "acme/widgets") is not None


def test_another_branch_of_this_repository_is_not_the_one_asked_for() -> None:
    problem = pr_head.head_problem(_pull("other"), "285-add-x", "acme/widgets")
    assert problem is not None and "other" in problem


def test_the_repository_is_compared_without_case() -> None:
    assert pr_head.head_problem(_pull("b", "Acme", "Widgets"), "b", "acme/widgets") is None


def test_a_contributors_clone_matches_its_own_fork_as_the_head() -> None:
    # pr open from a fork's clone pushes to the fork: the head repository is the fork, a cross-repository one.
    pull = _pull("b", "contributor", "widgets", cross=True)
    assert pr_head.head_problem(pull, "b", "contributor/widgets") is None


def test_matching_keeps_only_this_repositorys_pull_requests_of_the_branch() -> None:
    ours, fork = _pull("b"), _pull("b", "contributor", cross=True)
    fork["number"] = 2
    assert pr_head.matching([fork, ours], "b", "acme/widgets") == [ours]
    assert pr_head.matching([fork], "b", "acme/widgets") == []


def test_branch_opened_takes_no_moment_from_a_forks_pull_request(tmp_path: Path) -> None:
    # `ci pr` asks when this branch's pull request was opened; a fork's of the same name is not it.
    def gh_for(answer: dict[str, Any]) -> pr_publish.Gh:
        return lambda args, folder: json.dumps(answer)

    ours = {"createdAt": "2026-11-18T17:00:00Z", "isCrossRepository": False}
    fork = {"createdAt": "2026-11-01T00:00:00Z", "isCrossRepository": True}
    assert pr_publish.branch_opened(gh_for(ours), tmp_path) == datetime(2026, 11, 18, 17, 0, tzinfo=UTC)
    assert pr_publish.branch_opened(gh_for(fork), tmp_path) is None
