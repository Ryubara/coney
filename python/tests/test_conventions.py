# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.conventions: the loader reads .github/conventions.yml strictly and the real file is sound."""

import dataclasses
from pathlib import Path
from typing import Any

import pytest
from conventions_support import CI_CLASSES, REMOVE, REPO, changed, conventions_with, real_data, write_checkout

from coney_tools.conventions import (
    CONVENTIONS_FILE,
    Conventions,
    ConventionsError,
    load_conventions,
    parse_conventions,
)

# --- the real file ------------------------------------------------------------------------------------------------


def test_the_real_file_loads() -> None:
    conventions = load_conventions(REPO)
    assert conventions.version == 1 and conventions.repo.default_branch == "main"


def test_the_real_title_rules() -> None:
    title = load_conventions(REPO).commits.title
    assert title.max_length == 72 and title.breaking_suffix == "(BREAKING)" and not title.trailing_period
    assert set(title.areas) == {
        "build",
        "ci",
        "core",
        "gameplay",
        "fileio",
        "graphics",
        "audio",
        "platform",
        "tools",
        "research",
        "agents",
        "tests",
        "legal",
        "docs",
    }
    assert title.verbs == ("Add", "Fix", "Update", "Remove", "Rebuild", "Rework", "Improve", "Document")


def test_the_real_pull_request_rules() -> None:
    rules = load_conventions(REPO).pull_requests
    assert rules.sections == ("Summary", "Checklist", "Testing", "Screenshots", "AI use")
    assert rules.filled == ("Summary", "Testing") and rules.checklist.section == "Checklist"
    assert rules.docs_none.line == "Docs: none"
    assert rules.ai_use.written_by.no_ai == "a person"


def test_the_real_review_gate() -> None:
    review = load_conventions(REPO).pull_requests.review
    assert review.agent == "pr-reviewer"
    assert (review.verdict_file, review.report_file) == ("REVIEW.json", "REVIEW.md")
    assert review.body_hash == "sha256-lf" and review.identical_images == "refused"
    assert (REPO / ".claude" / "agents" / f"{review.agent}.md").is_file()


def test_the_real_merge_settings_follow_the_owners_decisions() -> None:
    merge = load_conventions(REPO).merge
    assert merge.methods == ("squash",) and merge.squash_message == "PR_BODY" and merge.auto_merge
    assert len(merge.required_checks) == 9 and "body" in merge.required_checks


def test_the_real_hygiene_thresholds_follow_the_owners_decisions() -> None:
    limits = load_conventions(REPO).hygiene.thresholds
    assert (limits.needs_triage_days, limits.needs_info_days) == (7, 21)
    assert (limits.in_progress_idle_days, limits.pull_request_idle_days) == (30, 30)


def test_the_real_labels_are_in_their_families() -> None:
    labels = load_conventions(REPO).labels
    assert labels.names("type") == ("bug", "parity", "task", "docs", "enhancement")
    assert len(labels.names("area")) == 11 and len(labels.names("priority")) == 3
    assert labels.names("status")[0] == "S-needs-triage" and len(labels.names("status")) == 9
    assert labels.names("other") == ("good first issue", "help wanted", "duplicate", "wontfix")
    assert labels.names() == tuple(label.name for label in labels.list)


def test_the_loaded_values_cannot_be_changed() -> None:
    conventions = load_conventions(REPO)
    with pytest.raises(dataclasses.FrozenInstanceError):
        conventions.version = 2  # type: ignore[misc]
    with pytest.raises(TypeError):
        conventions.commits.title.areas["x"] = "y"  # type: ignore[index]


# --- reading ------------------------------------------------------------------------------------------------------


def test_a_missing_file_is_a_one_line_error(tmp_path: Path) -> None:
    with pytest.raises(ConventionsError, match=r"conventions\.yml: cannot be read"):
        load_conventions(tmp_path)


def test_a_file_that_is_not_yaml_is_an_error(tmp_path: Path) -> None:
    (tmp_path / ".github").mkdir()
    (tmp_path / CONVENTIONS_FILE).write_text("a: [unclosed\n", encoding="utf-8")
    with pytest.raises(ConventionsError, match="not valid YAML"):
        load_conventions(tmp_path)


def test_a_file_that_is_not_utf8_is_an_error(tmp_path: Path) -> None:
    (tmp_path / ".github").mkdir()
    (tmp_path / CONVENTIONS_FILE).write_bytes(b"version: \xff\xfe\n")
    with pytest.raises(ConventionsError, match="not valid YAML"):
        load_conventions(tmp_path)


def test_a_key_written_twice_is_refused(tmp_path: Path) -> None:
    """A bad merge often leaves a key twice; plain YAML would silently keep the later one."""
    (tmp_path / ".github").mkdir()
    text = (REPO / CONVENTIONS_FILE).read_text(encoding="utf-8") + "\nversion: 1\n"
    (tmp_path / CONVENTIONS_FILE).write_text(text, encoding="utf-8")
    with pytest.raises(ConventionsError, match="written twice"):
        load_conventions(tmp_path)


def test_a_duplicate_key_inside_a_table_is_refused(tmp_path: Path) -> None:
    (tmp_path / ".github").mkdir()
    text = (REPO / CONVENTIONS_FILE).read_text(encoding="utf-8").replace("    wrap: 72", "    wrap: 72\n    wrap: 80")
    (tmp_path / CONVENTIONS_FILE).write_text(text, encoding="utf-8")
    with pytest.raises(ConventionsError, match="'wrap' is written twice"):
        load_conventions(tmp_path)


def test_a_conventions_file_that_is_not_a_mapping_is_an_error() -> None:
    with pytest.raises(ConventionsError, match="expected a mapping"):
        parse_conventions(["version", 1])


def test_parse_builds_the_same_values_as_load() -> None:
    assert parse_conventions(real_data()) == load_conventions(REPO)


def test_a_checkout_with_the_file_loads(tmp_path: Path) -> None:
    assert isinstance(load_conventions(write_checkout(tmp_path)), Conventions)


# --- strictness: every kind of mistake names the key ---------------------------------------------------------------

BAD: list[tuple[str, tuple[tuple[str, object], ...], str]] = [
    ("an unknown top-level key", (("titles", {}),), r"unknown key 'titles'"),
    ("a missing section", (("merge", REMOVE),), r"missing the key 'merge'"),
    ("an unknown nested key", (("commits.title.verb", ["Add"]),), r"commits\.title: unknown key 'verb'"),
    ("a missing nested key", (("commits.title.max_length", REMOVE),), r"commits\.title: missing the key 'max_length'"),
    ("another version", (("version", 2),), r"version: this coney-tools reads version 1"),
    ("a boolean version", (("version", True),), r"version: "),
    (
        "text for a number",
        (("commits.title.max_length", "72"),),
        r"commits\.title\.max_length: expected a whole number",
    ),
    ("a boolean for a number", (("commits.title.max_length", True),), r"max_length: expected a whole number"),
    ("a too small number", (("hygiene.thresholds.needs_triage_days", 0),), r"needs_triage_days: expected a whole"),
    ("a number for a flag", (("merge.auto_merge", 1),), r"merge\.auto_merge: expected true or false"),
    ("text for a list", (("commits.title.verbs", "Add"),), r"commits\.title\.verbs: expected a non-empty list"),
    ("an empty list", (("commits.title.verbs", []),), r"commits\.title\.verbs: expected a non-empty list"),
    ("a number in a list", (("commits.title.verbs", ["Add", 3]),), r"verbs\[1\]: expected non-empty text"),
    ("a repeated verb", (("commits.title.verbs", ["Add", "Add"]),), r"verbs: a value is listed twice"),
    ("a two-word verb", (("commits.title.verbs", ["Add it"]),), r"'Add it' must be one word"),
    ("no areas", (("commits.title.areas", {}),), r"commits\.title\.areas: expected at least one area"),
    ("an area with a colon", (("commits.title.areas", {"a:b": "x"}),), r"'a:b' must be one word without a colon"),
    ("an area with a space", (("commits.title.areas", {"a b": "x"}),), r"'a b' must be one word"),
    ("an area without text", (("commits.title.areas.docs", ""),), r"areas\.docs: expected non-empty text"),
    ("a boolean where text goes", (("repo.default_branch", False),), r"repo\.default_branch: expected non-empty"),
    (
        "a filled section that does not exist",
        (("pull_requests.filled", ["Nope"]),),
        r"'Nope' is not one of the sections",
    ),
    ("a checklist section that does not exist", (("pull_requests.checklist.section", "Nope"),), r"checklist\.section"),
    ("a no_ai choice that is not a choice", (("pull_requests.ai_use.written_by.no_ai", "robot"),), r"no_ai: 'robot'"),
    ("a docs_none class that does not exist", (("pull_requests.docs_none.when_changed", "tests"),), r"when_changed"),
    ("an unsupported unchecked rule", (("pull_requests.checklist.unchecked", "cited"),), r"checklist\.unchecked"),
    ("a since that is no day or moment", (("pull_requests.checklist.since", "someday"),), r"checklist\.since"),
    ("an unknown review hash", (("pull_requests.review.body_hash", "md5"),), r"review\.body_hash"),
    ("an agent name with a path", (("pull_requests.review.agent", "agents/x"),), r"review\.agent"),
    (
        "one file for the verdict and the report",
        (("pull_requests.review.report_file", "REVIEW.json"),),
        r"review\.report_file",
    ),
    (
        "a squash message GitHub does not have",
        (("merge.squash_message", "BODY"),),
        r"merge\.squash_message: expected one of",
    ),
    (
        "a merge method GitHub does not have",
        (("merge.methods", ["fast-forward"]),),
        r"'fast-forward' is not a merge method",
    ),
    ("a label count rule that is unknown", (("issues.labels.area", "some"),), r"issues\.labels\.area: expected one of"),
    ("a colour that is a number", (("labels.list.0.color", 123456),), r"write the colour in quotes"),
    ("a colour that is not hex", (("labels.list.0.color", "red"),), r"six hex digits"),
    ("a colour with a hash", (("labels.list.0.color", "#d73a4a"),), r"six hex digits"),
    ("a label description over the limit", (("labels.list.0.description", "x" * 101),), r"over 100 characters"),
    ("a duplicate label, ignoring case", (("labels.list.1.name", "BUG"),), r"appears twice"),
    (
        "a type name that is not a label",
        (("labels.families.type.names", ["bug", "ghost"]),),
        r"'ghost' is not in the label",
    ),
    ("a type family with a prefix", (("labels.families.type.prefix", "T-"),), r"no prefix"),
    (
        "a form label that is not a label",
        (("issues.forms.bug.labels", ["bug", "ghost"]),),
        r"forms\.bug\.labels: 'ghost'",
    ),
    ("a report label that is not a label", (("hygiene.report_issue.labels", ["ghost"]),), r"report_issue\.labels"),
    ("a comment marker with a space", (("hygiene.comments.marker", "repo hygiene"),), r"comments\.marker: letters"),
    (
        "a comment marker that ends the HTML comment",
        (("hygiene.comments.marker", "x-->"),),
        r"comments\.marker: letters",
    ),
    (
        "a comment flag that is text",
        (("hygiene.comments.on_sweep", "no"),),
        r"comments\.on_sweep: expected true or false",
    ),
    ("no comments per sweep", (("hygiene.comments.max_per_sweep", 0),), r"max_per_sweep: expected a whole"),
    ("a comment author that is empty", (("hygiene.comments.author", ""),), r"comments\.author: expected non-empty"),
    (
        "a report-only check that is unknown",
        (("hygiene.comments.report_only", ["issue.ghost"]),),
        r"report_only: 'issue.ghost'",
    ),
    ("a missing comments key", (("hygiene.comments.on_events", REMOVE),), r"comments: missing the key 'on_events'"),
    ("a cli form that does not exist", (("issues.cli_uses_form", "wish"),), r"cli_uses_form: 'wish' is not a form"),
    (
        "a form section that is not a pair",
        (("issues.forms.bug.sections.os", ["Operating system"]),),
        r"expected \[heading",
    ),
    (
        "a form heading used twice",
        (("issues.forms.bug.sections.os", ["Controller", False]),),
        r"a heading is used twice",
    ),
    ("a form without sections", (("issues.forms.bug.sections", {}),), r"forms\.bug\.sections: expected a non-empty"),
]


@pytest.mark.parametrize(("what", "changes", "message"), BAD, ids=[case[0] for case in BAD])
def test_a_mistake_is_refused_with_the_key_named(
    what: str, changes: tuple[tuple[str, object], ...], message: str
) -> None:
    with pytest.raises(ConventionsError, match=message):
        parse_conventions(changed(*changes))


def test_every_error_names_the_file() -> None:
    with pytest.raises(ConventionsError) as error:
        parse_conventions(changed(("merge.auto_merge", "yes")))
    assert str(error.value).startswith(f"{CONVENTIONS_FILE}: merge.auto_merge: ")


def test_a_trailing_period_may_be_allowed_by_the_file() -> None:
    assert conventions_with(("commits.title.trailing_period", True)).commits.title.trailing_period


def test_a_quoted_numeric_colour_keeps_its_zeros() -> None:
    colours = {label.name: label.color for label in load_conventions(REPO).labels.list}
    assert colours["S-blocked"] == "000000" and colours["help wanted"] == "008672"


def test_a_colour_is_kept_in_lower_case() -> None:
    assert conventions_with(("labels.list.0.color", "D73A4A")).labels.list[0].color == "d73a4a"


# --- helpers on the values ------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("path", "code", "docs", "docs_only"),
    [
        ("src/core/memory.cpp", True, False, False),
        ("python/src/coney_tools/cli.py", True, False, False),
        ("python/tests/test_cli.py", False, False, False),
        ("tests/core/memory_test.cpp", False, False, False),
        ("docs/guides/issues.md", False, True, True),
        ("research/missions.yaml", False, True, True),
        ("README.md", False, False, True),
        ("docs/sub/dir/page.md", False, True, True),
        ("src/README.md", True, False, False),
        (".github/CONTRIBUTING.md", False, False, True),
        (".github/workflows/pr.yml", False, False, True),
        (".github/workflows/build.yml", False, False, False),
        (".github/conventions.yml", False, False, True),
        ("mkdocs.yml", False, False, True),
        ("mkdocs.yml.bak", False, False, False),
        (".markdownlint-cli2.yaml", False, False, True),
        ("CMakeLists.txt", False, False, False),
        ("python/README.md", False, False, False),
    ],
)
def test_paths_are_classified(path: str, code: bool, docs: bool, docs_only: bool) -> None:
    paths = load_conventions(REPO).paths
    assert (paths.is_code(path), paths.is_docs(path), paths.is_docs_only(path)) == (code, docs, docs_only)


@pytest.mark.parametrize(("path", "expected"), CI_CLASSES)
def test_each_path_falls_in_its_ci_scope_class(path: str, expected: str) -> None:
    assert load_conventions(REPO).paths.ci_class(path) == expected


def test_no_scope_pattern_is_hidden_by_an_earlier_class() -> None:
    """A path written like each pattern falls in that pattern's own class: an earlier class never shadows it."""
    paths = load_conventions(REPO).paths
    for key in ("docs_only", "tools_only", "engine"):
        for pattern in getattr(paths, key):
            example = pattern.replace("*", "x") + ("x" if pattern.endswith("/") else "")
            assert paths.ci_class(example) == key, pattern


def test_the_first_matching_class_wins() -> None:
    paths = conventions_with(("paths.tools_only", ["a/"]), ("paths.engine", ["a/", "b/"])).paths
    assert [paths.ci_class(path) for path in ("a/x", "b/x", "c/x")] == ["tools_only", "engine", "unknown"]


def test_the_docs_only_regex_is_the_patterns_joined() -> None:
    regex = conventions_with(("paths.docs_only", ["docs/", "*.md", "a.b"])).paths.docs_only_regex()
    assert regex == r"^(docs/.*|[^/]*\.md|a\.b)$"


def test_label_families() -> None:
    labels = load_conventions(REPO).labels
    assert [labels.family(name) for name in ("bug", "A-ai", "P-low", "S-blocked", "wontfix")] == [
        "type",
        "area",
        "priority",
        "status",
        "other",
    ]


def test_a_form_keeps_its_sections_in_order() -> None:
    form = load_conventions(REPO).issues.forms["task"]
    assert [section.id for section in form.sections][:3] == ["summary", "area", "research"]
    assert form.sections[3].heading == 'What "done" means' and form.sections[3].required


def test_the_real_data_survives_a_round_trip_through_changed() -> None:
    data: dict[str, Any] = changed()
    assert data == real_data()


def test_the_comment_settings_start_at_warn_level() -> None:
    comments = load_conventions(REPO).hygiene.comments
    assert comments.marker == "coney-repo-hygiene" and comments.author == "github-actions[bot]"
    assert comments.on_events and not comments.on_sweep and comments.max_per_sweep == 25
    assert "pr.idle" in comments.report_only


def test_no_report_only_check_is_allowed() -> None:
    assert conventions_with(("hygiene.comments.report_only", [])).hygiene.comments.report_only == ()
