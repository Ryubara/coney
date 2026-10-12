# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks that the files which keep a copy of a rule still agree with `.github/conventions.yml`.

The issue forms, the pull request template's labels, the CI scope script and pre-commit's size limit cannot read the
conventions file, so they repeat part of it. These tests make a change to one of them without the other fail here
(a later change moves them into `coney-tools repo audit --files`).
"""

import re
from typing import Any

import pytest
import yaml
from conventions_support import REPO

from coney_tools.conventions import IssueForm, load_conventions

CONVENTIONS = load_conventions(REPO)
FORMS = sorted(CONVENTIONS.issues.forms.values(), key=lambda form: form.name)


def _form_file(form: IssueForm) -> dict[str, Any]:
    """The YAML of an issue form."""
    data: dict[str, Any] = yaml.safe_load((REPO / form.file).read_text(encoding="utf-8"))
    return data


def _inputs(form: IssueForm) -> list[dict[str, Any]]:
    """The fields of an issue form that ask for something (its introductory text is not one)."""
    return [item for item in _form_file(form)["body"] if item["type"] != "markdown"]


# --- the files named by the conventions exist ---------------------------------------------------------------------


@pytest.mark.parametrize(("role", "path"), sorted(CONVENTIONS.repo.tools.items()))
def test_every_tool_configuration_file_exists(role: str, path: str) -> None:
    assert (REPO / path).is_file(), f"repo.tools.{role}: {path} does not exist"


def test_the_template_the_pointer_files_and_the_forms_exist() -> None:
    assert (REPO / CONVENTIONS.pull_requests.template).is_file()
    for name in CONVENTIONS.paths.pointer_files.files + CONVENTIONS.paths.address_allowed_in:
        assert (REPO / name).is_file(), name
    for form in FORMS:
        assert (REPO / form.file).is_file(), form.file


def test_every_generated_path_of_a_coney_tools_generator_exists() -> None:
    """The tables folder is written by another tool and is not in the repository, so only coney-tools' are checked."""
    for generated in CONVENTIONS.paths.generated:
        if generated.by.startswith("coney-tools "):
            assert (REPO / generated.path).exists(), generated.path
        for excluded in generated.excluding:
            assert (REPO / excluded).exists(), excluded


def test_the_path_classes_name_real_folders() -> None:
    for prefix in (*CONVENTIONS.paths.code, *CONVENTIONS.paths.docs):
        assert (REPO / prefix).is_dir(), prefix


# --- the issue forms ----------------------------------------------------------------------------------------------


@pytest.mark.parametrize("form", FORMS, ids=[form.name for form in FORMS])
def test_a_form_has_the_title_prefix_and_labels_of_the_conventions(form: IssueForm) -> None:
    data = _form_file(form)
    assert data["title"] == form.title_prefix and data["labels"] == list(form.labels)


@pytest.mark.parametrize("form", FORMS, ids=[form.name for form in FORMS])
def test_a_form_asks_for_the_sections_of_the_conventions_in_order(form: IssueForm) -> None:
    found = []
    for item in _inputs(form):
        if item["type"] == "checkboxes":
            required = all(option.get("required", False) for option in item["attributes"]["options"])
        else:
            required = bool(item.get("validations", {}).get("required", False))
        found.append((item["id"], item["attributes"]["label"], required))
    assert found == [(section.id, section.heading, section.required) for section in form.sections]


def test_the_task_forms_area_options_are_the_area_labels() -> None:
    (dropdown,) = [item for item in _inputs(CONVENTIONS.issues.forms["task"]) if item["id"] == "area"]
    assert dropdown["attributes"]["options"] == list(CONVENTIONS.labels.names("area"))


def test_blank_issues_follow_the_conventions() -> None:
    config = yaml.safe_load((REPO / ".github" / "ISSUE_TEMPLATE" / "config.yml").read_text(encoding="utf-8"))
    assert config["blank_issues_enabled"] is CONVENTIONS.issues.blank_issues


def test_every_form_asks_for_screenshots_as_the_conventions_say() -> None:
    for form in FORMS:
        (screenshots,) = [section for section in form.sections if section.id == "screenshots"]
        assert screenshots.required
        (field,) = [item for item in _inputs(form) if item["id"] == "screenshots"]
        assert CONVENTIONS.issues.screenshots_none in field["attributes"]["description"]


# --- the other copies of a rule -----------------------------------------------------------------------------------


def test_the_template_uses_the_ai_use_labels_and_the_docs_none_line() -> None:
    template = (REPO / CONVENTIONS.pull_requests.template).read_text(encoding="utf-8")
    rules = CONVENTIONS.pull_requests
    assert f"`{rules.docs_none.line}`" in template and f"`{rules.closing.form}`".replace("#N", "#<number>") in template
    assert rules.remaining.label.rstrip(":") in template


def test_pre_commit_holds_the_large_file_limit_of_the_conventions() -> None:
    text = (REPO / ".pre-commit-config.yaml").read_text(encoding="utf-8")
    assert f"--maxkb={CONVENTIONS.paths.large_file_kb}" in text


def _scope_regex() -> re.Pattern[str]:
    """The DOCS_ONLY expression of .github/scripts/ci-scope.sh."""
    script = (REPO / ".github" / "scripts" / "ci-scope.sh").read_text(encoding="utf-8")
    found = re.search(r"^DOCS_ONLY='(?P<regex>.*)'$", script, re.MULTILINE)
    assert found is not None, "ci-scope.sh no longer defines DOCS_ONLY"
    return re.compile(found.group("regex"))


def _examples(pattern: str) -> list[str]:
    """Paths a docs_only pattern should match, and near misses it should not."""
    if pattern.endswith("/"):
        return [pattern + "page.md", pattern + "sub/deeper/file.txt", pattern.rstrip("/") + "x/page.md"]
    sample = pattern.replace("*", "name")
    return [sample, "sub/" + sample, sample + ".bak"]


def test_ci_scope_treats_the_same_paths_as_documentation() -> None:
    """The script repeats paths.docs_only as a regular expression until it reads the file; they must not drift."""
    scope = _scope_regex()
    paths = CONVENTIONS.paths
    candidates = [example for pattern in paths.docs_only for example in _examples(pattern)] + [
        "src/core/a.cpp",
        "tests/a.cpp",
        "python/src/coney_tools/cli.py",
        "python/README.md",
        "CMakeLists.txt",
    ]
    for path in candidates:
        assert bool(scope.search(path)) == paths.is_docs_only(path), path


def test_the_docs_only_regex_of_the_conventions_agrees_with_the_script() -> None:
    scope = _scope_regex()
    generated = re.compile(CONVENTIONS.paths.docs_only_regex())
    for pattern in CONVENTIONS.paths.docs_only:
        for path in _examples(pattern):
            assert bool(scope.search(path)) == bool(generated.search(path)), path
