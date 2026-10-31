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
    docs = proof_profiles.required_checks(CONVENTIONS, "docs", ["docs/a.md"])
    assert docs == ["docs-check", "mkdocs", "pre-commit", "pytest"]
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


# --- which Python tests run (proof.pytest) ---------------------------------------------------------------------------


def scope_of(*paths: str) -> proof_profiles.PytestScope:
    """The pytest scope the real conventions give `paths`."""
    return proof_profiles.pytest_scope(CONVENTIONS, paths)


def test_the_scoped_classes_are_docs_and_engine_and_the_tools_run_everything() -> None:
    assert CONVENTIONS.proof.pytest.scoped == ("docs_only", "engine")
    assert CONVENTIONS.proof.pytest.whole_when == ("python/", ".claude/", ".github/")


@pytest.mark.parametrize(
    ("paths", "classes"),
    [
        (["docs/guides/stamps.md"], ("docs_only",)),
        (["docs/guides/stamps.md", "research/functions/ai.toml", "AGENTS.md"], ("docs_only",)),
        (["src/ai/brain.cpp"], ("engine",)),
        (["tests/ai/brain_test.cpp", "CMakeLists.txt"], ("engine",)),
        (["src/ai/brain.cpp", "docs/research/ai.md"], ("docs_only", "engine")),
    ],
)
def test_a_change_to_docs_or_engine_files_runs_only_the_tests_that_read_them(
    paths: list[str], classes: tuple[str, ...]
) -> None:
    scope = scope_of(*paths)
    assert not scope.whole and scope.classes == classes
    assert scope.text == "only the tests marked as reading " + ", ".join(classes)


@pytest.mark.parametrize(
    ("paths", "culprit"),
    [
        (["docs/a.md", "python/src/coney_tools/cli.py"], "python/src/coney_tools/cli.py"),
        (["python/tests/test_cli.py"], "python/tests/test_cli.py"),
        ([".claude/hooks/protect.py"], ".claude/hooks/protect.py"),
        # docs_only and engine files under .github/ still run everything: the rules, workflows and scripts
        ([".github/conventions.yml"], ".github/conventions.yml"),
        (["src/a.cpp", ".github/workflows/build.yml"], ".github/workflows/build.yml"),
        ([".github/pull_request_template.md"], ".github/pull_request_template.md"),
        (["tools/run.ps1"], "tools/run.ps1"),
        (["docs/a.md", "a-file-nobody-classified.xyz"], "a-file-nobody-classified.xyz"),
    ],
)
def test_the_tools_the_rules_and_unmapped_files_run_the_whole_suite(paths: list[str], culprit: str) -> None:
    scope = scope_of(*paths)
    assert scope.whole and scope.classes == () and scope.culprit == culprit
    assert scope.text == f"the whole suite ({culprit} runs everything)"


def test_no_changed_path_runs_the_whole_suite() -> None:
    scope = scope_of()
    assert scope.whole and scope.text == "the whole suite"


def test_without_a_pytest_key_nothing_is_scoped() -> None:
    conventions = conventions_with(("proof.pytest", REMOVE))
    assert conventions.proof.pytest.scoped == () and conventions.proof.pytest.whole_when == ()
    assert proof_profiles.pytest_scope(conventions, ["docs/a.md"]).whole


def test_a_scope_covers_one_that_needs_its_classes_or_fewer() -> None:
    whole, docs, both = scope_of("python/a.py"), scope_of("docs/a.md"), scope_of("docs/a.md", "src/a.cpp")
    assert proof_profiles.covers(whole, both) and proof_profiles.covers(both, docs)
    assert not proof_profiles.covers(docs, both) and not proof_profiles.covers(both, whole)


def test_the_scope_is_recorded_and_read_back_from_its_stamp_form() -> None:
    for scope in (scope_of("python/a.py"), scope_of("docs/a.md", "src/a.cpp")):
        assert proof_profiles.scope_from_record(proof_profiles.scope_record(scope)) == scope
    assert proof_profiles.scope_record(scope_of("docs/a.md")) == ["docs_only"]
    assert proof_profiles.scope_record(scope_of("python/a.py")) == "whole"
    # a stamp from before the scope was recorded ran the whole suite
    assert proof_profiles.scope_from_record(None).whole
    with pytest.raises(ConventionsError, match="scope"):
        proof_profiles.scope_from_record(42)


@pytest.mark.parametrize(
    ("value", "match"),
    [(["markdown"], "scoped"), (["unknown"], "scoped"), ("docs_only", "list")],
)
def test_a_scoped_class_must_be_a_ci_class_other_than_unknown(value: object, match: str) -> None:
    with pytest.raises(ConventionsError, match=match):
        conventions_with(("proof.pytest.scoped", value))
