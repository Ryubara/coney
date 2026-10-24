# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the proof profiles: which checks a change needs, from the paths it touches (`proof` in conventions)."""

import pytest
from conventions_support import REMOVE, REPO, changed, conventions_with, real_data

from coney_tools import proof_profiles
from coney_tools.conventions import ConventionsError, load_conventions, parse_conventions

CONVENTIONS = load_conventions(REPO)


def profile_of(*paths: str) -> str:
    """The profile the real conventions require for `paths`."""
    return proof_profiles.required_profile(CONVENTIONS, paths)


def test_the_profiles_are_docs_tooling_and_full_in_that_order() -> None:
    assert [p.name for p in CONVENTIONS.proof.profiles] == ["docs", "tooling", "full"]
    assert CONVENTIONS.proof.build.preset == "ci"
    assert CONVENTIONS.proof.build.checks == ("msvc-ci-build", "ctest-full-disc")


@pytest.mark.parametrize(
    ("paths", "expected"),
    [
        (["docs/guides/stamps.md"], "docs"),
        (["docs/guides/stamps.md", "research/functions/ai.toml"], "docs"),
        (["python/src/coney_tools/cli.py"], "tooling"),
        (["python/src/coney_tools/cli.py", "docs/guides/stamps.md"], "tooling"),
        ([".claude/hooks/protect.py"], "tooling"),
        (["src/ai/brain.cpp"], "full"),
        (["src/ai/brain.cpp", "docs/guides/stamps.md"], "full"),
        (["tests/ai/brain_test.cpp", "python/src/coney_tools/cli.py"], "full"),
        (["CMakeLists.txt"], "full"),
        (["a-file-nobody-classified.xyz"], "full"),
        ([], "docs"),
    ],
)
def test_the_highest_class_among_the_paths_wins(paths: list[str], expected: str) -> None:
    assert profile_of(*paths) == expected


def test_a_workflow_change_asks_for_actionlint_and_others_do_not() -> None:
    with_workflow = proof_profiles.required_checks(CONVENTIONS, "tooling", [".github/workflows/pr.yml"])
    without = proof_profiles.required_checks(CONVENTIONS, "tooling", ["python/src/coney_tools/cli.py"])
    assert "actionlint" in with_workflow and "actionlint" not in without
    assert [name for name in with_workflow if name != "actionlint"] == without


def test_the_checks_of_a_profile_keep_the_order_the_file_lists() -> None:
    assert proof_profiles.required_checks(CONVENTIONS, "docs", ["docs/a.md"]) == ["docs-check", "mkdocs", "pre-commit"]
    full = proof_profiles.required_checks(CONVENTIONS, "full", ["src/a.cpp"])
    assert full[-2:] == ["msvc-ci-build", "ctest-full-disc"]


def test_a_bigger_profile_than_needed_is_allowed_and_a_smaller_one_is_not() -> None:
    assert proof_profiles.at_least("full", "docs", CONVENTIONS)
    assert proof_profiles.at_least("tooling", "tooling", CONVENTIONS)
    assert not proof_profiles.at_least("docs", "tooling", CONVENTIONS)


def test_an_unknown_profile_is_refused() -> None:
    with pytest.raises(ConventionsError, match="no profile"):
        proof_profiles.required_checks(CONVENTIONS, "huge", [])


def test_every_check_the_file_names_has_an_implementation() -> None:
    from coney_tools import task_check_steps

    named = {name for profile in CONVENTIONS.proof.profiles for name in profile.checks}
    built = set(CONVENTIONS.proof.build.checks)
    assert named == set(task_check_steps.IMPLEMENTED) | built and not built & set(task_check_steps.IMPLEMENTED)


def test_the_last_profile_must_cover_every_class() -> None:
    data = changed(("proof.profiles.2.classes", ["docs_only", "tools_only"]))
    with pytest.raises(ConventionsError, match="unknown"):
        parse_conventions(data)


def test_a_class_nobody_knows_is_refused() -> None:
    with pytest.raises(ConventionsError, match="classes"):
        conventions_with(("proof.profiles.0.classes", ["markdown"]))


def test_only_when_may_name_only_checks_of_a_profile() -> None:
    data = real_data()
    data["proof"]["only_when"]["invented"] = ["x/"]
    with pytest.raises(ConventionsError, match="only_when"):
        parse_conventions(data)


def test_the_build_checks_must_be_profile_checks() -> None:
    with pytest.raises(ConventionsError, match="build"):
        conventions_with(("proof.build.checks", ["not-a-check"]))


def test_the_proof_section_is_required() -> None:
    with pytest.raises(ConventionsError, match="proof"):
        parse_conventions(changed(("proof", REMOVE)))
