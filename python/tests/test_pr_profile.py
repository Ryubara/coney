# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the Check profile a change needs: the highest CI scope class among its paths."""

import pytest
from conventions_support import REPO

from coney_tools import pr_profile
from coney_tools.conventions import load_conventions


@pytest.mark.parametrize(
    ("paths", "profile"),
    [
        ([], "docs"),
        (["docs/guides/pr.md", "research/missions.yaml", "README.md"], "docs"),
        (["docs/guides/pr.md", "python/src/coney_tools/cli.py"], "tooling"),
        (["python/tests/test_pr_diff.py", ".claude/hooks/x.py"], "tooling"),
        (["docs/guides/pr.md", "src/ai/brain.cpp"], "full"),
        (["tests/ai/brain_test.cpp"], "full"),
        (["python/src/coney_tools/cli.py", "something/not/in/any/class.txt"], "full"),
    ],
)
def test_the_required_profile_is_the_highest_class_of_the_paths_and_an_unknown_path_means_full(
    paths: list[str], profile: str
) -> None:
    conventions = load_conventions(REPO)
    assert pr_profile.profile_of(paths, conventions).name == profile


def test_the_classes_name_the_paths_that_decided_them() -> None:
    conventions = load_conventions(REPO)
    classes = pr_profile.path_classes(["src/ai/brain.cpp", "docs/a.md", "python/x.py", "zzz/odd"], conventions.paths)
    assert classes == {
        "engine": ["src/ai/brain.cpp"],
        "docs_only": ["docs/a.md"],
        "tools_only": ["python/x.py"],
        "unknown": ["zzz/odd"],
    }
