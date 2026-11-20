# SPDX-License-Identifier: GPL-3.0-or-later
"""The one match of a pull request to a branch: by its head repository as well as its head branch name
(docs/guides/pr.md#head-match).

GitHub's `gh pr list --head <branch>` and a pull request's `headRefName` name the branch only, without the repository
it lives in. A contributor's fork can have a branch of the same name (task names are made from issue titles), and on
2026-11-20 `pr open` took the fork pull request #1415 for task 285's own, retitled it, replaced its body and turned on
auto-merge (#1522). So every tool that finds a pull request by branch reads the head fields (`HEAD_FIELDS`) and asks
here: `head_problem` when it knows the repository it pushed to, `is_own` and `head_label` when it lists the pull
requests of the repository it runs in, whose own branches are those not from another repository.

A pull request that does not say where its head is (the fields were not asked for, or the fork is deleted) is never
taken for ours: no answer is a fork's answer.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping
from typing import Any

#: The `gh pr list` / `gh pr view` JSON fields the match reads; a caller adds them to its `--json` list.
HEAD_FIELDS = "headRefName,headRepository,headRepositoryOwner,isCrossRepository"


def _login(item: Mapping[str, Any]) -> str:
    """The head repository's owner, or an empty string when the pull request does not say."""
    owner = item.get("headRepositoryOwner")
    return str(owner.get("login") or "") if isinstance(owner, Mapping) else ""


def head_repository(item: Mapping[str, Any]) -> str:
    """`OWNER/NAME` of the repository the pull request's head branch lives in, or an empty string when unknown."""
    repository = item.get("headRepository")
    name = str(repository.get("name") or "") if isinstance(repository, Mapping) else ""
    owner = _login(item)
    return f"{owner}/{name}" if owner and name else ""


def is_own(item: Mapping[str, Any]) -> bool:
    """Whether the pull request's head is a branch of the repository it was opened in (not a fork's, nor unknown)."""
    return item.get("isCrossRepository") is False


def head_label(item: Mapping[str, Any]) -> str:
    """The head as GitHub labels it: the bare branch for one of this repository, `owner:branch` for a fork's (`?:` when
    the owner is unknown). A tool that matches local branches against it never matches a fork's pull request."""
    branch = str(item.get("headRefName") or "")
    return branch if is_own(item) else f"{_login(item) or '?'}:{branch}"


def head_problem(item: Mapping[str, Any], branch: str, repo: str) -> str | None:
    """Why the pull request is not the one of `branch` in `repo` (`OWNER/NAME`, the repository the branch was pushed
    to), or None when it is."""
    found_branch = str(item.get("headRefName") or "")
    found_repo = head_repository(item) or "an unknown repository"
    if found_branch != branch or found_repo.casefold() != repo.casefold():
        return f"its head is {found_repo}:{found_branch}, not {repo}:{branch}"
    return None


def matching(items: Iterable[Mapping[str, Any]], branch: str, repo: str) -> list[Mapping[str, Any]]:
    """The pull requests among `items` whose head is `branch` of `repo`, in their order."""
    return [item for item in items if head_problem(item, branch, repo) is None]
