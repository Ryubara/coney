# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.audit_files: the checkout checks, on this checkout and on copies with one thing changed.

The issue forms, the pull request template, the CI scope script and pre-commit's size limit each keep a copy of part of
`.github/conventions.yml`; every check here fails when its copy and the file differ, and these tests make each of them
differ once.
"""

import os
import shutil
import subprocess
from pathlib import Path
from typing import Any

import pytest
import yaml
from audit_support import CONVENTIONS, found, items
from conventions_support import REPO

from coney_tools import audit_files as af
from coney_tools.audit import registered
from coney_tools.conventions import CONVENTIONS_FILE, load_conventions

FILES_CHECKS = [spec.id for spec in CONVENTIONS.hygiene.checks.values() if spec.scope == "files"]
GIT_ENV = {
    **os.environ,
    "GIT_AUTHOR_NAME": "t",
    "GIT_AUTHOR_EMAIL": "t@e.invalid",
    "GIT_COMMITTER_NAME": "t",
    "GIT_COMMITTER_EMAIL": "t@e.invalid",
}


def checkout(root: Path) -> Path:
    """A made-up checkout: the real files the checks read, and empty stand-ins for the rest the conventions name."""
    for name in (
        CONVENTIONS_FILE,
        ".github/pull_request_template.md",
        ".github/scripts/ci-scope.sh",
        ".github/ISSUE_TEMPLATE/bug.yml",
        ".github/ISSUE_TEMPLATE/task.yml",
        ".github/ISSUE_TEMPLATE/config.yml",
        ".pre-commit-config.yaml",
        "CLAUDE.md",
        "GEMINI.md",
        "README.md",
        "AGENTS.md",
        "CONTRIBUTING.md",
    ):
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(REPO / name, root / name)
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")  # marks a Coney checkout
    shutil.copytree(REPO / "docs" / "guides", root / "docs" / "guides")
    for path in CONVENTIONS.repo.tools.values():
        if not (root / path).exists():
            (root / path).parent.mkdir(parents=True, exist_ok=True)
            (root / path).write_text("", encoding="utf-8")
    for folder in (*CONVENTIONS.paths.code, *CONVENTIONS.paths.docs):
        (root / folder).mkdir(parents=True, exist_ok=True)
    for generated in CONVENTIONS.paths.generated:
        if generated.by.startswith("coney-tools "):
            (root / generated.path).mkdir(parents=True, exist_ok=True)
        for excluded in generated.excluding:
            (root / excluded).parent.mkdir(parents=True, exist_ok=True)
            (root / excluded).write_text("", encoding="utf-8")
    return root


def context(root: Path) -> af.FilesContext:
    """The files context of a made-up checkout."""
    return af.FilesContext(root, load_conventions(root))


@pytest.fixture
def root(tmp_path: Path) -> Path:
    """A fresh made-up checkout."""
    return checkout(tmp_path)


def edit(root: Path, name: str, old: str, new: str) -> None:
    """Replace `old` with `new` in the checkout file `name`; the replacement must change something."""
    text = (root / name).read_text(encoding="utf-8")
    assert old in text, f"{name} does not hold {old!r}"
    (root / name).write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


def edit_yaml(root: Path, name: str, change: Any) -> None:
    """Load the YAML file `name`, apply `change(data)` and write it back."""
    data = yaml.safe_load((root / name).read_text(encoding="utf-8"))
    change(data)
    (root / name).write_text(yaml.safe_dump(data), encoding="utf-8")


# --- this checkout and the clean copy ---------------------------------------------------------------------------


@pytest.mark.parametrize("check_id", FILES_CHECKS)
def test_this_checkout_has_no_finding(check_id: str) -> None:
    problems = found(registered()[check_id], af.FilesContext(REPO, CONVENTIONS))
    assert problems == [], [f"{p.item}: {p.message}" for p in problems]


@pytest.mark.parametrize("check_id", FILES_CHECKS)
def test_the_clean_copy_has_no_finding(root: Path, check_id: str) -> None:
    assert found(registered()[check_id], context(root)) == []


# --- files.pointer and files.exist --------------------------------------------------------------------------------


def test_a_pointer_file_with_its_own_instructions_is_found(root: Path) -> None:
    (root / "GEMINI.md").write_text("@AGENTS.md\n" + "more rules\n" * 100, encoding="utf-8")
    (problem,) = found(af.pointer_files, context(root))
    assert problem.item == "GEMINI.md" and "over the" in problem.message


def test_a_missing_pointer_file_is_found(root: Path) -> None:
    (root / "CLAUDE.md").unlink()
    assert "CLAUDE.md" in items(found(af.pointer_files, context(root)))


@pytest.mark.parametrize(
    "name", [".clang-tidy", ".github/pull_request_template.md", ".github/ISSUE_TEMPLATE/bug.yml", "src/", "README.md"]
)
def test_a_named_file_or_folder_that_is_missing_is_found(root: Path, name: str) -> None:
    target = root / name
    if target.is_dir():
        shutil.rmtree(target)
    else:
        target.unlink()
    assert name in items(found(af.named_files_exist, context(root)))


# --- files.address ------------------------------------------------------------------------------------------------


def test_the_address_in_a_file_other_than_the_readme_is_found(root: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("GITHUB_REPOSITORY", "Someone/Coney")
    (root / "README.md").write_text("See https://github.com/someone/coney for more.\n", encoding="utf-8")
    assert found(af.address_only_in_readme, context(root)) == []
    (root / "docs" / "page.md").write_text("Clone git@GitHub.com:someone/coney.git\n", encoding="utf-8")
    (root / "docs" / "site.md").write_text("Read https://someone.github.io/coney/guides\n", encoding="utf-8")
    (root / "docs" / "other.md").write_text("https://github.com/someone/other-project\n", encoding="utf-8")
    assert items(found(af.address_only_in_readme, context(root))) == ["docs/page.md", "docs/site.md"]


def test_the_address_is_read_from_the_origin_remote_without_the_environment(
    root: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.delenv("GITHUB_REPOSITORY", raising=False)
    subprocess.run(["git", "init", "-q"], cwd=root, check=True, env=GIT_ENV)
    subprocess.run(["git", "remote", "add", "origin", "https://github.com/Someone/Coney.git"], cwd=root, check=True)
    assert af.repository_address_terms(root) == [
        "github.com/someone/coney",
        "github.com:someone/coney",
        "someone.github.io/coney",
    ]
    (root / "docs" / "page.md").write_text("github.com/someone/coney\n", encoding="utf-8")
    subprocess.run(["git", "add", "-A"], cwd=root, check=True, env=GIT_ENV)
    assert items(found(af.address_only_in_readme, context(root))) == ["docs/page.md"]


def test_without_a_known_address_nothing_is_found(root: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("GITHUB_REPOSITORY", raising=False)
    (root / "docs" / "page.md").write_text("github.com/someone/coney\n", encoding="utf-8")
    assert af.repository_address_terms(root) == [] and found(af.address_only_in_readme, context(root)) == []


# --- files.forms --------------------------------------------------------------------------------------------------


def test_a_form_with_another_label_or_title_is_found(root: Path) -> None:
    edit_yaml(root, ".github/ISSUE_TEMPLATE/task.yml", lambda data: data.update(labels=["task"], title="Chore: "))
    (problem, *rest) = found(af.forms_match, context(root))
    assert problem.item == ".github/ISSUE_TEMPLATE/task.yml" and "title is 'Chore: '" in problem.message
    assert "labels are ['task']" in rest[0].message


def test_a_form_field_that_is_removed_or_made_optional_is_found(root: Path) -> None:
    def drop(data: dict[str, Any]) -> None:
        data["body"] = [item for item in data["body"] if item.get("id") != "done"]

    edit_yaml(root, ".github/ISSUE_TEMPLATE/task.yml", drop)
    (problem,) = found(af.forms_match, context(root))
    assert "fields (id, label, required) differ from issues.forms.task.sections" in problem.message

    def optional(data: dict[str, Any]) -> None:
        for item in data["body"]:
            if item.get("id") == "steps":
                item["validations"] = {"required": True}

    checkout_again = checkout(root.parent / "second")
    edit_yaml(checkout_again, ".github/ISSUE_TEMPLATE/task.yml", optional)
    assert items(found(af.forms_match, context(checkout_again))) == [".github/ISSUE_TEMPLATE/task.yml"]


def test_a_bug_form_with_a_renamed_heading_is_found(root: Path) -> None:
    edit(root, ".github/ISSUE_TEMPLATE/bug.yml", "label: Steps to reproduce", "label: How to reproduce")
    (problem,) = found(af.forms_match, context(root))
    assert problem.item.endswith("bug.yml") and "How to reproduce" in problem.message


def test_the_area_options_must_be_the_area_labels(root: Path) -> None:
    def change(data: dict[str, Any]) -> None:
        for item in data["body"]:
            if item.get("id") == "area":
                item["attributes"]["options"].pop()

    edit_yaml(root, ".github/ISSUE_TEMPLATE/task.yml", change)
    assert "the area options are" in found(af.forms_match, context(root))[0].message


def test_a_form_that_stops_telling_about_the_none_line_is_found(root: Path) -> None:
    def change(data: dict[str, Any]) -> None:
        for item in data["body"]:
            if item.get("id") == "screenshots":
                item["attributes"]["description"] = "Drag a picture here."

    edit_yaml(root, ".github/ISSUE_TEMPLATE/bug.yml", change)
    assert "Screenshots: none (<reason>)" in found(af.forms_match, context(root))[0].message


def test_blank_issues_must_stay_off(root: Path) -> None:
    edit(root, ".github/ISSUE_TEMPLATE/config.yml", "blank_issues_enabled: false", "blank_issues_enabled: true")
    (problem,) = found(af.forms_match, context(root))
    assert problem.item == af.ISSUE_CONFIG and "not false" in problem.message


# --- files.template -----------------------------------------------------------------------------------------------


def test_a_template_with_a_renamed_or_missing_heading_is_found(root: Path) -> None:
    edit(root, ".github/pull_request_template.md", "## Testing", "## Verification")
    (problem,) = found(af.template_matches, context(root))
    assert "headings are" in problem.message and "Verification" in problem.message


def test_a_template_that_forgets_the_ai_use_choices_or_the_docs_none_line_is_found(root: Path) -> None:
    edit(root, ".github/pull_request_template.md", "a person / an AI", "a human / an AI")
    edit(root, ".github/pull_request_template.md", "`Docs: none`", "`Docs: skip`")
    messages = " | ".join(problem.message for problem in found(af.template_matches, context(root)))
    assert "does not list the choices" in messages and "does not mention `Docs: none`" in messages


# --- files.generated-blocks ---------------------------------------------------------------------------------------


def test_a_block_edited_by_hand_is_found(root: Path) -> None:
    edit(
        root,
        "docs/guides/issues.md",
        "| Type | `bug` | Something in Coney is broken |",
        "| Type | `bug` | Edited by hand |",
    )
    (problem,) = found(af.generated_blocks_current, context(root))
    assert problem.item == "docs/guides/issues.md" and "coney-tools repo render" in problem.message


# --- files.ci-scope and files.large-file-limit ------------------------------------------------------------------


def test_a_scope_script_with_its_own_list_is_found(root: Path) -> None:
    edit(root, ".github/scripts/ci-scope.sh", 'DOCS_ONLY="$(docs_only_regex)"', "DOCS_ONLY='^(docs/|research/)'")
    messages = [problem.message for problem in found(af.ci_scope_reads_the_file, context(root))]
    assert any("its own regular expression" in message for message in messages)


def test_a_scope_script_that_no_longer_names_the_file_is_found(root: Path) -> None:
    text = (
        (root / ".github/scripts/ci-scope.sh").read_text(encoding="utf-8").replace("conventions.yml", "somewhere.yml")
    )
    (root / ".github/scripts/ci-scope.sh").write_text(text, encoding="utf-8", newline="\n")
    assert any("does not read" in problem.message for problem in found(af.ci_scope_reads_the_file, context(root)))


def test_a_docs_only_list_the_script_cannot_parse_is_found(root: Path) -> None:
    edit(root, CONVENTIONS_FILE, '    - "docs/"\n', "    - 'docs/'\n")
    (problem,) = found(af.ci_scope_reads_the_file, context(root))
    assert problem.item == CONVENTIONS_FILE and "one quoted pattern per line" in problem.message


def test_the_docs_only_list_is_read_the_way_the_script_reads_it() -> None:
    text = '  docs_only:\n    - "docs/"   # why\n\n    # a comment\n    - "*.md"\n  generated:\n    - "no"\n'
    assert af.docs_only_as_written(text) == ["docs/", "*.md"]


def test_a_limit_that_pre_commit_does_not_pass_is_found(root: Path) -> None:
    edit(root, ".pre-commit-config.yaml", "--maxkb=512", "--maxkb=1024")
    (problem,) = found(af.large_file_limit_matches, context(root))
    assert "--maxkb=512" in problem.message


# --- files.markdown-hook ------------------------------------------------------------------------------------------


def test_the_markdownlint_hook_is_in_the_clean_copy(root: Path) -> None:
    assert "id: markdownlint-cli2" in (root / ".pre-commit-config.yaml").read_text(encoding="utf-8")


def test_a_pre_commit_config_without_the_markdownlint_hook_is_found(root: Path) -> None:
    edit(root, ".pre-commit-config.yaml", "id: markdownlint-cli2", "id: something-else")
    (problem,) = found(af.markdown_hook_exists, context(root))
    assert problem.item == ".pre-commit-config.yaml" and "markdownlint-cli2" in problem.message


def test_a_missing_pre_commit_config_is_found_by_the_markdownlint_check(root: Path) -> None:
    (root / ".pre-commit-config.yaml").unlink()
    (problem,) = found(af.markdown_hook_exists, context(root))
    assert "missing" in problem.message


# --- the scope script itself --------------------------------------------------------------------------------------


def find_bash() -> str | None:
    """A bash that runs the script: Git's on Windows (not the Windows Subsystem for Linux one), else the one on PATH."""
    if os.name == "nt":
        git = shutil.which("git")
        for parent in Path(git).parents if git else []:
            for candidate in (parent / "bin" / "bash.exe", parent / "usr" / "bin" / "bash.exe"):
                if candidate.is_file():
                    return str(candidate)
        return None
    return shutil.which("bash")


needs_bash = pytest.mark.skipif(find_bash() is None, reason="no bash")


def run_scope(root: Path, changed: list[str], event: str = "pull_request", draft: str = "false") -> dict[str, str]:
    """Commit a base, then `changed`, in a made-up repository holding the script and the conventions; run the script."""
    subprocess.run(["git", "init", "-q", "-b", "main"], cwd=root, check=True, env=GIT_ENV)
    subprocess.run(["git", "add", "-A"], cwd=root, check=True, env=GIT_ENV)
    subprocess.run(["git", "commit", "-q", "-m", "base"], cwd=root, check=True, env=GIT_ENV)
    for name in changed:
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        with (root / name).open("a", encoding="utf-8", newline="\n") as handle:
            handle.write("# changed\n")
    subprocess.run(["git", "add", "-A"], cwd=root, check=True, env=GIT_ENV)
    subprocess.run(["git", "commit", "-q", "-m", "change"], cwd=root, check=True, env=GIT_ENV)
    output = root / "out.txt"
    env = {**GIT_ENV, "EVENT": event, "DRAFT": draft, "GITHUB_OUTPUT": str(output)}
    bash = find_bash()
    assert bash is not None
    done = subprocess.run(
        [bash, ".github/scripts/ci-scope.sh"], cwd=root, env=env, capture_output=True, text=True, check=False
    )
    assert done.returncode == 0, done.stderr
    return dict(line.split("=", 1) for line in output.read_text(encoding="utf-8").splitlines())


@needs_bash
@pytest.mark.parametrize(
    "changed",
    [
        ["docs/guides/a.md"],
        ["README.md"],
        ["research/x.yaml"],
        [".github/conventions.yml"],
        ["mkdocs.yml"],
        ["docs/a.md", "CONTRIBUTING.md"],
    ],
)
def test_a_documentation_only_change_skips_the_builds(root: Path, changed: list[str]) -> None:
    result = run_scope(root, changed)
    assert result["full"] == "false" and result["python-os"] == '["ubuntu-24.04"]' and result["python"] == "true"


@needs_bash
@pytest.mark.parametrize(
    "changed",
    [
        ["src/core/a.cpp"],
        ["docs/a.md", "src/b.cpp"],
        ["python/README.md"],
        ["CMakeLists.txt"],
        ["sub/README.md"],
        ["docs.md/x"],
        [".github/workflows/pr.yml"],
    ],
)
def test_a_change_that_reaches_the_engine_runs_everything(root: Path, changed: list[str]) -> None:
    assert run_scope(root, changed)["full"] == "true"


@needs_bash
def test_the_script_reads_a_changed_list_from_the_conventions(root: Path) -> None:
    """Add a docs-only pattern to the file and the script follows it: there is no second copy."""
    edit(root, CONVENTIONS_FILE, '    - "mkdocs.yml"\n', '    - "mkdocs.yml"\n    - "tools-notes/"\n')
    assert run_scope(root, ["tools-notes/a.txt"])["full"] == "false"


@needs_bash
def test_a_conventions_file_without_the_list_stops_the_script(root: Path) -> None:
    text = (root / CONVENTIONS_FILE).read_text(encoding="utf-8").replace("  docs_only:", "  docs_only_renamed:")
    (root / CONVENTIONS_FILE).write_text(text, encoding="utf-8", newline="\n")
    bash = find_bash()
    assert bash is not None
    done = subprocess.run(
        [bash, ".github/scripts/ci-scope.sh"],
        cwd=root,
        env={**GIT_ENV, "EVENT": "push"},
        capture_output=True,
        text=True,
        check=False,
    )
    assert done.returncode != 0 and "found no paths.docs_only" in done.stderr


@needs_bash
def test_pushes_and_drafts_behave_as_before(root: Path) -> None:
    assert run_scope(root, ["docs/a.md"], event="push")["full"] == "true"


@needs_bash
def test_a_draft_runs_only_the_cheap_checks(tmp_path: Path) -> None:
    result = run_scope(checkout(tmp_path), ["src/a.cpp"], draft="true")
    assert result["full"] == "false" and result["python"] == "false"
