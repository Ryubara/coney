# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.conventions: the loader reads .github/conventions.yml strictly and the real file is sound."""

import dataclasses
import re
from datetime import UTC, datetime, timedelta
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
    # The legacy gate runs the one merged reviewer (docs/guides/review/index.md), like `task review`.
    assert review.agent == "reviewer"
    assert (review.verdict_file, review.report_file) == ("REVIEW.json", "REVIEW.md")
    assert review.body_hash == "sha256-lf" and review.identical_images == "refused"
    assert (REPO / ".claude" / "agents" / f"{review.agent}.md").is_file()


def test_the_real_orig_review_gate() -> None:
    orig = load_conventions(REPO).pull_requests.orig_review
    assert (orig.agent, orig.verdict_file, orig.report_file) == ("reviewer", "ORIG-REVIEW.json", "ORIG-REVIEW.md")
    assert orig.ledger == "orig-review/ledger.jsonl" and orig.rubric_version >= 1
    assert (REPO / ".claude" / "agents" / f"{orig.agent}.md").is_file()
    # The rubric page states the same version.
    assert f"Rubric version: {orig.rubric_version}." in (REPO / "docs/guides/review/orig.md").read_text(
        encoding="utf-8"
    )


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
    assert (
        len(labels.names("area")) == 11
        and labels.names("priority")[0] == "P-critical"
        and len(labels.names("priority")) == 4
    )
    assert labels.names("status")[0] == "S-needs-triage" and len(labels.names("status")) == 10
    assert labels.names("other") == ("good first issue", "help wanted", "duplicate", "wontfix")
    assert labels.names() == tuple(label.name for label in labels.list)


def test_the_loaded_values_cannot_be_changed() -> None:
    conventions = load_conventions(REPO)
    with pytest.raises(dataclasses.FrozenInstanceError):
        conventions.version = 2  # type: ignore[misc]
    with pytest.raises(TypeError):
        conventions.commits.title.areas["x"] = "y"  # type: ignore[index]


# --- reading ------------------------------------------------------------------------------------------------------


def test_a_since_written_as_a_day_is_midnight_utc() -> None:
    """A plain `YYYY-MM-DD` still works: it means the start of that day in UTC."""
    since = conventions_with(("pull_requests.checklist.since", "2026-10-12")).pull_requests.checklist.since
    assert since == datetime(2026, 10, 12, tzinfo=UTC)


def test_a_since_with_an_offset_is_converted_to_utc() -> None:
    """A moment written with an offset is the same instant in UTC."""
    since = conventions_with(("pull_requests.checklist.since", "2026-10-12T12:39:13+02:00")).pull_requests.checklist
    assert since.since == datetime(2026, 10, 12, 10, 39, 13, tzinfo=UTC)
    assert since.since.utcoffset() == timedelta(0)


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
    (
        "a claim command that does not compile",
        (("claims.commands.renew", "(/claim"),),
        r"claims\.commands\.renew: not a valid",
    ),
    (
        "a claim pattern without its scope group",
        (("claims.commands.claim", "^/claim$"),),
        r"claims\.commands\.claim: the pattern needs a named group 'scope'",
    ),
    (
        "a claim label that is not a label",
        (("claims.working_label", "S-nope"),),
        r"claims: 'S-nope' is not in labels\.list",
    ),
    (
        "a warning after the deadline",
        (("claims.warn_hours", 80),),
        r"claims\.warn_hours: must be less than deadline_hours",
    ),
    ("a bad claim marker", (("claims.card_marker", "a b"),), r"claims\.card_marker: letters, digits and hyphens"),
    (
        "a hold label that does not pause the claim",
        (("claims.hold_labels", ["S-in-progress"]),),
        r"claims\.hold_labels: 'S-in-progress' is not in claims\.paused_labels",
    ),
    ("a task file with a path", (("tasks.file", "a/TASK.md"),), r"tasks\.file: a file name"),
    ("rare above common", (("tasks.leads.rare_max_entries", 500),), r"must be less than common_min_entries"),
    (
        "a review agent with a path",
        (("tasks.review.agent", "agents/reviewer"),),
        r"tasks\.review\.agent: .* must be an agent name",
    ),
    (
        "a review folder outside the task",
        (("tasks.review.folder", "../review"),),
        r"tasks\.review\.folder: .* must be a folder name",
    ),
    (
        "one file for the verdict and the report",
        (("tasks.review.report_file", "REVIEW.json"),),
        r"tasks\.review\.report_file: must differ",
    ),
    (
        "a review kind that is not one",
        (("tasks.review.reverse_engineering_kinds", ["research", "prose"]),),
        r"reverse_engineering_kinds: 'prose' is not a review kind",
    ),
    (
        "no research paths",
        (("tasks.review.research_paths", []),),
        r"tasks\.review\.research_paths: expected a non-empty",
    ),
    (
        "no addresses to check on a page",
        (("tasks.review.pages_min_addresses", 0),),
        r"tasks\.review\.pages_min_addresses: expected a whole number",
    ),
    (
        "a form with the old title prefix",
        (("issues.forms.task.title_prefix", "Task: "),),
        r"unknown key 'title_prefix'",
    ),
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
        "an orig verdict named like review's",
        (("pull_requests.orig_review.verdict_file", "REVIEW.json"),),
        r"orig_review",
    ),
    ("an orig ledger outside the scratch folder", (("pull_requests.orig_review.ledger", "../x.jsonl"),), r"ledger"),
    ("an orig rubric version of zero", (("pull_requests.orig_review.rubric_version", 0),), r"rubric_version"),
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
    ("a run id pattern that is not a regex", (("runs.id_pattern", "r-("),), r"runs\.id_pattern: not a regular"),
    (
        "a run record too small to hold a header",
        (("runs.max_bytes", 100),),
        r"runs\.max_bytes: expected a whole number",
    ),
    ("a run kind listed twice", (("runs.kinds", ["recipe", "recipe"]),), r"runs\.kinds: a value is listed twice"),
    ("a misspelled runs key", (("runs.max_value", 3),), r"runs: unknown key 'max_value'"),
    ("a gameplay entry that is not a src/ folder", (("paths.gameplay", ["ai/"]),), r"paths\.gameplay: 'ai/'"),
    (
        "a folder both gameplay and Coney-only",
        (("paths.coney_only", ["src/ai/", "src/debug/"]),),
        r"paths\.coney_only: 'src/ai/' is also in paths\.gameplay",
    ),
    (
        "a research kind with an optional section it does not have",
        (("research.kinds.topic.optional", ["Appendix"]),),
        r"research\.kinds\.topic\.optional: 'Appendix' is not one of the kind's sections",
    ),
    (
        "a research alias for a section no kind has",
        (("research.aliases.Purpose", "Overview"),),
        r"research\.aliases: 'Overview' is not a section of some kind",
    ),
    (
        "a research kind whose system rule is a guess",
        (("research.kinds.topic.system", "maybe"),),
        r"research\.kinds\.topic\.system: expected one of: required, none",
    ),
    (
        "a research page read as a kind there is none of",
        (("research.inferred_kind.default", "essay"),),
        r"research\.inferred_kind\.default: expected one of",
    ),
    (
        "a research key that is not one",
        (("research.kinds.topic.lines", 10),),
        r"research\.kinds\.topic: unknown key 'lines'",
    ),
    (
        "a research limit that is too small to be one",
        (("research.kinds.topic.max_lines", 5),),
        r"research\.kinds\.topic\.max_lines: expected a whole number of 100 or more",
    ),
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


# --- pull_requests.runtime ----------------------------------------------------------------------------------------


def test_the_real_runtime_rule() -> None:
    runtime = load_conventions(REPO).pull_requests.runtime
    assert {"gameplay", "graphics", "audio"} <= set(runtime.areas)
    assert runtime.section == "Testing" and runtime.line == "Runtime:" and runtime.none == "Runtime: none"


def test_a_runtime_area_must_be_a_commit_title_area() -> None:
    with pytest.raises(ConventionsError, match=r"pull_requests\.runtime\.areas.*commits\.title\.areas"):
        conventions_with(("pull_requests.runtime.areas", ["gameplay", "not-an-area"]))


def test_the_runtime_none_line_must_start_with_the_runtime_line() -> None:
    with pytest.raises(ConventionsError, match=r"pull_requests\.runtime\.none.*must start with"):
        conventions_with(("pull_requests.runtime.none", "Original: none"))


def test_the_runtime_section_must_be_one_of_the_sections() -> None:
    with pytest.raises(ConventionsError, match=r"pull_requests\.runtime\.section"):
        conventions_with(("pull_requests.runtime.section", "Evidence"))


def test_a_misspelled_runtime_key_is_refused() -> None:
    with pytest.raises(ConventionsError, match=r"pull_requests\.runtime"):
        conventions_with(("pull_requests.runtime.area", ["gameplay"]))


def test_the_gameplay_and_coney_only_folders_are_read() -> None:
    paths = conventions_with().paths
    assert "src/ai/" in paths.gameplay and "src/debug/" in paths.coney_only
    assert paths.gameplay_dir("src/ai/brain.cpp") and not paths.gameplay_dir("src/debug/menu.cpp")


def test_the_real_claim_rules() -> None:
    claims = load_conventions(REPO).claims
    assert claims.limits.maintainer == 3 and claims.limits.contributor == 2
    assert (claims.warn_hours, claims.deadline_hours) == (48, 72)
    assert claims.limits.issues_per_task == 3 and claims.closing_per_event == 10 and claims.scope_max_characters == 400
    assert claims.paused_labels == ("S-blocked", "S-needs-info", "S-on-hold")
    assert claims.hold_labels == ("S-on-hold",)
    # Both switch on at the cutover, by a change to this file.
    assert (claims.adopt, claims.release) == (False, False)
    assert claims.maintainer_permissions == ("admin", "write") and claims.maintainer_associations == ("OWNER",)
    assert claims.claimable_status == claims.released_label == "S-ready-to-start"
    assert claims.critical_label == "P-critical" and claims.bot_login == "github-actions[bot]"
    assert "assigned" in claims.stages and "fix" in claims.stages
    assert load_conventions(REPO).tasks.kinds == ("feature", "fix", "docs", "tooling", "ci-fix")


def test_the_real_task_start_rules() -> None:
    start = load_conventions(REPO).tasks.start
    assert start.unit_file == "unit.json"
    # The limit is for code work under the finished workflow: reported, not enforced, until the cutover.
    assert (start.slots.task, start.slots.fix, start.slots.enforce) == (3, 1, False)


@pytest.mark.parametrize(
    ("change", "why"),
    [
        (("tasks.start.unit_file", "a/unit.json"), r"tasks\.start\.unit_file: a file name"),
        (("tasks.start.unit_file", "TASK.md"), r"tasks\.start\.unit_file: a file name"),
        (("tasks.start.slots.task", 0), r"tasks\.start\.slots\.task: expected a whole number"),
        (("tasks.start.slots.enforce", "yes"), r"tasks\.start\.slots\.enforce"),
        (("tasks.start.slots.extra", 1), "unknown key 'extra'"),
        (("tasks.start", REMOVE), "missing the key 'start'"),
    ],
)
def test_wrong_task_start_rules_are_refused(change: tuple[str, object], why: str) -> None:
    with pytest.raises(ConventionsError, match=why):
        conventions_with(change)


def test_the_real_task_review_rules() -> None:
    review = load_conventions(REPO).tasks.review
    assert review.agent == "reviewer" and (REPO / ".claude" / "agents" / f"{review.agent}.md").is_file()
    assert (review.folder, review.packet_file) == ("review", "PACKET.json")
    assert (review.verdict_file, review.report_file) == ("REVIEW.json", "REVIEW.md")
    assert review.research_paths == ("docs/research/", "research/") and review.docs_suffixes == (".md",)
    assert review.pages_min_addresses == 2
    # Reverse-engineering review runs on Opus (AGENTS.md), so research and `@orig` reviews need that model.
    assert review.reverse_engineering_kinds == ("research", "orig") and review.reverse_engineering_models == ("opus",)


def test_the_real_run_record_rules() -> None:
    runs = load_conventions(REPO).runs
    assert runs.folder == "research/runs" and runs.kinds == ("recipe", "trace", "method")
    assert re.fullmatch(runs.id_pattern, "r-20261023-grab-reach")
    assert not re.fullmatch(runs.id_pattern, "grab-reach") and not re.fullmatch(runs.id_pattern, "r-20261023-Grab")
    assert runs.max_bytes <= 16 * 1024 and runs.max_values >= 16 and runs.max_text >= 200


def test_every_title_area_of_a_label_is_a_title_area() -> None:
    conventions = load_conventions(REPO)
    assert set(conventions.issues.title_area_by_label.values()) <= set(conventions.commits.title.areas)
    assert set(conventions.issues.title_area_by_label) <= set(conventions.labels.names())


def test_the_real_research_page_kinds() -> None:
    research = load_conventions(REPO).research
    assert set(research.kinds) == {"system", "topic", "format", "mission", "feature", "survey"}
    topic = research.kinds["topic"]
    assert topic.sections == (
        "Summary",
        "Map",
        "Data",
        "Behaviour",
        "In Coney",
        "Open questions",
        "How this was checked",
    )
    assert topic.required == topic.sections[:-1] and topic.max_lines == 1500
    assert all(research.kinds[name].max_lines == 1000 for name in ("format", "mission", "feature", "survey"))
    assert (research.verified_max_chars, research.copy_min_chars) == (200, 200)
    assert research.files.legacy == "research/legacy.toml"


def test_a_research_section_is_found_by_its_name_or_an_old_name() -> None:
    research = load_conventions(REPO).research
    assert research.canonical("topic", "Summary") == research.canonical("topic", "Purpose") == "Summary"
    assert research.canonical("topic", "original structure") == "Map"
    assert research.canonical("topic", "Coney" + chr(0x2019) + "s implementation") == "In Coney"
    assert research.canonical("format", "Data") == "Layout" and research.canonical("topic", "Layout") == ""
    assert research.canonical("topic", "Data layout") == ""


def test_a_research_page_without_front_matter_is_read_as_the_kind_of_its_folder() -> None:
    research = load_conventions(REPO).research
    assert research.kind_of_path("docs/research/formats/wad-dir.md") == "format"
    assert research.kind_of_path("docs/research/combat.md") == "system"


# --- the conclave section -----------------------------------------------------------------------------------------


def test_the_real_conclave_section() -> None:
    conclave = load_conventions(REPO).conclave
    assert conclave.claude_code.count(".") == 2 and conclave.hooks_file.endswith("hooks.toml")
    assert 0 < conclave.local_seconds <= conclave.github_seconds <= conclave.github_idle_seconds
    # the numbers the band draws by come from here, so the mod holds none of them
    assert 0 < conclave.pane_min_columns <= conclave.pane_wide_columns and conclave.stale_after_reads >= 1


@pytest.mark.parametrize(
    ("path", "value", "message"),
    [
        ("conclave.hooks_file", "../outside.toml", "a path inside the checkout"),
        ("conclave.hooks_file", "C:/hooks.toml", "a path inside the checkout"),
        ("conclave.github_idle_seconds", 10, "must not be less than github_seconds"),
        ("conclave.local_seconds", 0, "expected a whole number of 1 or more"),
        ("conclave.unknown", 1, "unknown key 'unknown'"),
        ("conclave.pane_min_columns", REMOVE, "missing the key 'pane_min_columns'"),
        ("conclave.pane_wide_columns", 50, "must not be less than pane_min_columns"),
        ("conclave.pane_wide_columns", REMOVE, "missing the key 'pane_wide_columns'"),
        ("conclave.band_narrow_columns", REMOVE, "missing the key 'band_narrow_columns'"),
        ("conclave.stale_after_reads", 0, "expected a whole number of 1 or more"),
    ],
)
def test_a_wrong_conclave_section_is_refused(path: str, value: object, message: str) -> None:
    data = changed((path, value))
    with pytest.raises(ConventionsError, match=message):
        parse_conventions(data)


# --- the standards section ----------------------------------------------------------------------------------------


def test_the_real_standards_section() -> None:
    standards = load_conventions(REPO).standards
    assert standards.legacy_file == "research/legacy-files.toml"
    assert standards.code.folders == ("src/",) and ".cpp" in standards.code.suffixes
    assert "TEST_CASE" in standards.tests.macros and standards.tests.citation.marker == "Expected:"
    # Before the cutover a file below standard is a warning and a function may still be marked legacy.
    assert standards.enforce is False and standards.cutover is None


def test_a_cutover_day_is_read_as_a_moment() -> None:
    standards = conventions_with(("standards.cutover", "2026-11-01")).standards
    assert standards.cutover is not None and standards.cutover.year == 2026 and standards.cutover.day == 1


@pytest.mark.parametrize(
    ("path", "value", "message"),
    [
        ("standards.legacy_file", "../legacy.toml", "a path inside the checkout"),
        ("standards.enforce", "yes", "expected true or false"),
        ("standards.cutover", "soon", "expected a day or moment"),
        ("standards.unknown", 1, "unknown key 'unknown'"),
        ("standards.code.folders", ["src"], "trailing slash"),
        ("standards.code.suffixes", ["cpp"], "not a file ending"),
        ("standards.tests.macros", [], "expected a non-empty list"),
        ("standards.tests.citation.reference", "([", "not a regular expression"),
        ("standards.tests.citation.marker", REMOVE, "missing the key 'marker'"),
    ],
)
def test_a_wrong_standards_section_is_refused(path: str, value: object, message: str) -> None:
    data = changed((path, value))
    with pytest.raises(ConventionsError, match=message):
        parse_conventions(data)
