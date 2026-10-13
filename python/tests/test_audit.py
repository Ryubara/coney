# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.audit: the registry, findings, ordering and the text and JSON reports."""

import json

import pytest
from audit_support import CONVENTIONS
from conventions_support import changed, conventions_with, real_data

from coney_tools import (
    audit,
    audit_cli,
    audit_files,
    audit_github,
    audit_local,
)
from coney_tools.audit import (
    Finding,
    Problem,
    Report,
    check,
    format_text,
    registered,
    run_checks,
    sort_findings,
    specs_in,
    to_json,
)
from coney_tools.config import ConfigError
from coney_tools.conventions import ConventionsError, parse_conventions

# --- the registry and the code agree ----------------------------------------------------------------------------


def test_every_check_of_the_registry_has_code_and_every_check_in_code_has_an_entry() -> None:
    assert set(CONVENTIONS.hygiene.checks) == set(registered())


def test_a_check_is_registered_once() -> None:
    with pytest.raises(ValueError, match="registered twice"):
        check("files.pointer")(lambda context: [])


def test_the_registry_groups_follow_the_modules() -> None:
    prefixes = {"files": ("files.",), "github": ("issue.", "pr.", "gh."), "local": ("ws.",)}
    for spec in CONVENTIONS.hygiene.checks.values():
        assert spec.id.startswith(prefixes[spec.scope]), spec.id


def test_every_check_names_keys_the_file_has_and_a_summary() -> None:
    data = real_data()
    for spec in CONVENTIONS.hygiene.checks.values():
        assert spec.summary and spec.keys
        for key in spec.keys:
            node = data
            for part in key.split("."):
                node = node[part]


def test_the_error_checks_include_what_the_user_pointed_out_by_hand() -> None:
    errors = {spec.id for spec in CONVENTIONS.hygiene.checks.values() if spec.severity == "error"}
    assert {
        "issue.sections",
        "issue.placeholder",
        "issue.labels",
        "issue.closed-status",
        "issue.closed-unchecked",
        "pr.unfinished-items",
        "pr.remaining-uncited",
    } <= errors
    assert {"pr.body", "issue.owner-word", "pr.owner-word"} <= errors


# --- the loader reads the registry strictly -------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("field", "value", "message"),
    [
        ("scope", "everywhere", "scope: expected one of: files, github, local"),
        ("severity", "fatal", "severity: expected one of: error, warn"),
        ("fix", "magic", "fix: expected one of: auto, skill, owner"),
        ("keys", ["paths.nothing"], "'paths.nothing' is not a key of this file"),
        ("keys", [], "keys: expected a non-empty list"),
        ("summary", "", "summary: expected non-empty text"),
        ("extra", 1, "unknown key 'extra'"),
    ],
)
def test_a_bad_check_entry_is_refused(field: str, value: object, message: str) -> None:
    data = real_data()
    data["hygiene"]["checks"]["files.pointer"][field] = value
    with pytest.raises(ConventionsError, match=message):
        parse_conventions(data)


def test_a_registry_without_checks_is_refused() -> None:
    with pytest.raises(ConventionsError, match="expected at least one check"):
        parse_conventions(changed(("hygiene.checks", {})))


def test_a_check_id_must_look_like_one() -> None:
    data = real_data()
    data["hygiene"]["checks"]["Bad Name"] = data["hygiene"]["checks"]["files.pointer"]
    with pytest.raises(ConventionsError, match="not a check id"):
        parse_conventions(data)


def test_the_registry_keeps_file_order_and_cannot_be_changed() -> None:
    checks = CONVENTIONS.hygiene.checks
    assert next(iter(checks)) == "files.pointer"
    with pytest.raises(TypeError):
        checks["x"] = checks["files.pointer"]  # type: ignore[index]


@pytest.mark.parametrize("key", ["docs_only", "tools_only", "engine"])
@pytest.mark.parametrize("pattern", ["docs/(a)", "a b", "$x", "a|b", "[a]"])
def test_a_scope_pattern_the_scope_script_cannot_read_is_refused(key: str, pattern: str) -> None:
    data = changed((f"paths.{key}.0", pattern))
    with pytest.raises(ConventionsError, match=rf"paths\.{key}\[0\]"):
        parse_conventions(data)


@pytest.mark.parametrize("key", ["tools_only", "engine"])
def test_a_missing_scope_list_is_refused(key: str) -> None:
    data = real_data()
    del data["paths"][key]
    with pytest.raises(ConventionsError, match=key):
        parse_conventions(data)


def test_the_wording_and_the_placeholders_are_read() -> None:
    assert CONVENTIONS.repo.wording.person == "user" and CONVENTIONS.repo.wording.avoid == ("owner",)
    assert "x" in CONVENTIONS.issues.placeholders and "tbd" in CONVENTIONS.issues.placeholders
    assert conventions_with(("repo.wording.avoid", ["owner", "boss"])).repo.wording.avoid == ("owner", "boss")


# --- running checks ---------------------------------------------------------------------------------------------


def test_specs_are_chosen_by_scope_and_by_name_or_group() -> None:
    files = specs_in(CONVENTIONS, ["files"])
    assert files and all(spec.scope == "files" for spec in files)
    assert [spec.id for spec in specs_in(CONVENTIONS, ["files", "github"], ["files.pointer"])] == ["files.pointer"]
    issue_checks = specs_in(CONVENTIONS, ["github"], ["issue"])
    assert issue_checks and all(spec.id.startswith("issue.") for spec in issue_checks)
    assert specs_in(CONVENTIONS, ["local"], ["files.pointer"]) == []


def test_an_unknown_check_name_is_an_error_not_a_clean_run() -> None:
    with pytest.raises(ConfigError, match=r"--only issue\.nothing: no such check"):
        specs_in(CONVENTIONS, ["github"], ["issue.nothing"])


def test_a_group_name_does_not_match_a_longer_group() -> None:
    assert specs_in(CONVENTIONS, ["github"], ["pr"]) and not any(
        spec.id.startswith("gh.") for spec in specs_in(CONVENTIONS, ["github"], ["pr"])
    )


def test_run_checks_gives_each_problem_the_severity_and_fix_of_its_spec(monkeypatch: pytest.MonkeyPatch) -> None:
    report = Report(("files",))
    spec = CONVENTIONS.hygiene.checks["files.generated-blocks"]
    monkeypatch.setitem(audit._CHECKS, spec.id, lambda context: [Problem("A.md", "stale", "http://x")])
    run_checks([spec], {"files": object()}, report)
    (found,) = report.findings
    assert (found.check, found.severity, found.fix, found.item, found.url) == (
        "files.generated-blocks",
        "error",
        "auto",
        "A.md",
        "http://x",
    )
    assert found.keys == spec.keys and report.ran == ["files.generated-blocks"]


def test_a_scope_without_a_context_does_not_run() -> None:
    report = Report(("github",))
    run_checks(specs_in(CONVENTIONS, ["github"]), {}, report)
    assert report.ran == [] and report.findings == []


# --- findings and reports -------------------------------------------------------------------------------------------


def finding(check_id: str = "issue.labels", item: str = "#1", severity: str = "error", message: str = "m") -> Finding:
    """A finding with the other fields fixed."""
    return Finding(check_id, severity, item, message, "skill", ("issues.labels",), "http://x")


def test_findings_are_ordered_by_check_then_by_number() -> None:
    order = ["issue.labels", "pr.title"]
    mixed = [
        finding("pr.title", "#2"),
        finding("issue.labels", "#10"),
        finding("issue.labels", "#9"),
        finding("pr.title", "#1"),
    ]
    assert [(f.check, f.item) for f in sort_findings(mixed, order)] == [
        ("issue.labels", "#9"),
        ("issue.labels", "#10"),
        ("pr.title", "#1"),
        ("pr.title", "#2"),
    ]


def test_the_report_fails_on_an_error_and_on_a_warning_only_when_strict() -> None:
    report = Report(("github",), ran=["issue.labels", "issue.links"])
    assert not report.failed() and not report.failed(strict=True)
    report.findings.append(finding(severity="warn"))
    assert not report.failed() and report.failed(strict=True)
    report.findings.append(finding(severity="error"))
    assert report.failed() and report.count() == 2 and report.count("warn") == 1


def test_the_text_report_groups_by_check_and_counts() -> None:
    report = Report(("github",), ran=["issue.labels", "issue.links"], notices=["no network for X"])
    report.findings = [finding(item="#1", message="a"), finding(item="#2", message="b")]
    lines = list(format_text(report))
    assert lines[0] == "note: no network for X"
    assert "issue.labels (error, fix: skill): 2" in lines and "  #1: a" in lines and "  #2: b" in lines
    assert lines[-2] == "audit github: 2 checks, 2 errors, 0 warnings" and lines[-1] == "clean: issue.links"


def test_the_text_report_cuts_a_long_list_but_not_the_counts() -> None:
    report = Report(("github",), ran=["issue.labels"])
    report.findings = [finding(item=f"#{n}") for n in range(1, 6)]
    lines = list(format_text(report, show=2))
    assert "issue.labels (error, fix: skill): 5" in lines and "  #2: m" in lines and "  #3: m" not in lines
    assert any("3 more" in line for line in lines)


def test_a_clean_report_is_one_line() -> None:
    assert list(format_text(Report(("files",), ran=["files.pointer"]))) == [
        "audit files: 1 checks, 0 errors, 0 warnings"
    ]


def test_the_json_report_has_the_fields_the_skill_reads() -> None:
    report = Report(("files", "github"), ran=["issue.labels", "pr.title"], notices=["n"])
    report.findings = [finding()]
    data = json.loads(to_json(report))
    assert data["errors"] == 1 and data["warnings"] == 0 and data["notices"] == ["n"]
    assert data["checks"] == {"issue.labels": 1, "pr.title": 0} and data["scopes"] == ["files", "github"]
    assert data["findings"] == [
        {
            "check": "issue.labels",
            "keys": ["issues.labels"],
            "severity": "error",
            "item": "#1",
            "url": "http://x",
            "message": "m",
            "fix": "skill",
        }
    ]


def test_audit_cli_is_importable() -> None:
    assert audit_cli.DEFAULT_SHOWN > 0 and audit_files.CI_SCOPE_SCRIPT.endswith("ci-scope.sh")
    assert audit_github.FETCH_LIMIT > 1000 and audit_local.CAPTURE_SUFFIXES
