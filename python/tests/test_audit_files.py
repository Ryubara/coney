# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.audit_files: the checkout checks, on this checkout and on copies with one thing changed.

The issue forms, the pull request template, the CI scope script and pre-commit's size limit each keep a copy of part of
`.github/conventions.yml`; every check here fails when its copy and the file differ, and these tests make each of them
differ once.
"""

import os
import re
import shutil
import subprocess
from pathlib import Path
from typing import Any

import pytest
import yaml
from audit_support import CONVENTIONS, found, items
from conventions_support import CI_CLASSES, REPO

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
    shutil.copytree(REPO / ".github" / "workflows", root / ".github" / "workflows")
    for path in CONVENTIONS.repo.tools.values():
        if not (root / path).exists():
            (root / path).parent.mkdir(parents=True, exist_ok=True)
            (root / path).write_text("", encoding="utf-8")
    for folder in (*CONVENTIONS.paths.code, *CONVENTIONS.paths.docs):
        (root / folder).mkdir(parents=True, exist_ok=True)
    for generated in CONVENTIONS.paths.generated:
        if generated.by.startswith("coney-tools ") and generated.path.endswith("/"):
            (root / generated.path).mkdir(parents=True, exist_ok=True)
        elif generated.by.startswith("coney-tools "):
            # A generated page (docs/guides/pcsx2-recipes.md) rather than a folder of them.
            (root / generated.path).parent.mkdir(parents=True, exist_ok=True)
            (root / generated.path).touch()
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


# --- files.command-reference --------------------------------------------------------------------------------------


def test_a_parameter_block_edited_by_hand_is_found(root: Path) -> None:
    edit(root, "docs/guides/coney-tools-repo.md", "| `--check` | flag | off |", "| `--check` | flag | on |")
    (problem,) = found(af.command_reference_current, context(root))
    assert problem.item == "docs/guides/coney-tools-repo.md"
    assert "out of date" in problem.message and "coney-tools docs render" in problem.message


def test_a_group_whose_block_was_removed_is_found(root: Path) -> None:
    page = root / "docs/guides/coney-tools-setup.md"
    text = page.read_text(encoding="utf-8")
    start = text.index("<!-- command-reference:begin doctor -->")
    end = text.index("<!-- command-reference:end -->", start) + len("<!-- command-reference:end -->")
    page.write_text(text[:start] + text[end:], encoding="utf-8", newline="\n")
    (problem,) = found(af.command_reference_current, context(root))
    assert problem.item == "doctor" and "no block of its parameters" in problem.message


def test_one_stale_block_on_a_page_that_holds_several_names_only_its_group(root: Path) -> None:
    # coney-tools-setup.md holds eight blocks; the finding is about the one that changed.
    edit(
        root,
        "docs/guides/coney-tools-setup.md",
        "<!-- command-reference:begin doctor -->\n",
        "<!-- command-reference:begin doctor -->\nedited by hand\n",
    )
    (problem,) = found(af.command_reference_current, context(root))
    assert problem.item == "docs/guides/coney-tools-setup.md"
    assert problem.message == "the block for 'doctor' is out of date (run `coney-tools docs render`)"


def test_the_guides_are_read_from_the_folder_paths_docs_names(root: Path) -> None:
    shutil.move(root / "docs" / "guides", root / "manual-guides")
    (root / "manual").mkdir()
    shutil.move(root / "manual-guides", root / "manual" / "guides")
    # Still `docs/` in the conventions: the blocks are not where the key says, so every group lacks one ...
    assert {p.item for p in found(af.command_reference_current, context(root)) if "no block" in p.message}
    # ... and when the key names the new folder the check finds them, current.
    edit_yaml(root, CONVENTIONS_FILE, lambda data: data["paths"].__setitem__("docs", ["manual/", "research/"]))
    assert found(af.command_reference_current, context(root)) == []
    edit(
        root,
        "manual/guides/coney-tools-setup.md",
        "<!-- command-reference:begin doctor -->\n",
        "<!-- command-reference:begin doctor -->\nedited by hand\n",
    )
    (problem,) = found(af.command_reference_current, context(root))
    assert problem.item == "manual/guides/coney-tools-setup.md" and "'doctor'" in problem.message


# --- files.ci-scope and files.large-file-limit ------------------------------------------------------------------


@pytest.mark.parametrize(
    ("line", "own"),
    [
        ('DOCS_ONLY="$(regex_of docs_only)"', "DOCS_ONLY='^(docs/|research/)'"),
        ('TOOLS_ONLY="$(regex_of tools_only)"', "TOOLS_ONLY='^(python/)'"),
        ('ENGINE="$(regex_of engine)"', "ENGINE='^(src/|tests/)'"),
    ],
)
def test_a_scope_script_with_its_own_list_is_found(root: Path, line: str, own: str) -> None:
    edit(root, ".github/scripts/ci-scope.sh", line, own)
    messages = [problem.message for problem in found(af.ci_scope_reads_the_file, context(root))]
    assert any("its own regular expression" in message for message in messages)
    assert any("does not read paths." in message for message in messages)


def test_a_scope_script_that_no_longer_names_the_file_is_found(root: Path) -> None:
    text = (
        (root / ".github/scripts/ci-scope.sh").read_text(encoding="utf-8").replace("conventions.yml", "somewhere.yml")
    )
    (root / ".github/scripts/ci-scope.sh").write_text(text, encoding="utf-8", newline="\n")
    assert any("does not read" in problem.message for problem in found(af.ci_scope_reads_the_file, context(root)))


@pytest.mark.parametrize(("key", "item"), [("docs_only", "docs/"), ("tools_only", "python/"), ("engine", "src/")])
def test_a_scope_list_the_script_cannot_parse_is_found(root: Path, key: str, item: str) -> None:
    edit(root, CONVENTIONS_FILE, f'    - "{item}"\n', f"    - '{item}'\n")
    (problem,) = found(af.ci_scope_reads_the_file, context(root))
    assert problem.item == CONVENTIONS_FILE and f"paths.{key} must be" in problem.message


@pytest.mark.parametrize("key", ["docs_only", "tools_only", "engine"])
def test_a_scope_list_is_read_the_way_the_script_reads_it(key: str) -> None:
    text = f'  {key}:\n    - "docs/"   # why\n\n    # a comment\n    - "*.md"\n  generated:\n    - "no"\n'
    assert af.scope_list_as_written(text, key) == ["docs/", "*.md"]
    assert af.scope_list_as_written(text, "other") == []


def test_a_limit_that_pre_commit_does_not_pass_is_found(root: Path) -> None:
    edit(root, ".pre-commit-config.yaml", "--maxkb=512", "--maxkb=1024")
    (problem,) = found(af.large_file_limit_matches, context(root))
    assert "--maxkb=512" in problem.message


def large_file_hook(data: dict[str, Any]) -> dict[str, Any]:
    """The check-added-large-files entry of a loaded pre-commit config."""
    return next(hook for repo in data["repos"] for hook in repo["hooks"] if hook["id"] == "check-added-large-files")


def test_the_clean_large_file_hook_passes(root: Path) -> None:
    assert found(af.large_file_limit_matches, context(root)) == []


@pytest.mark.parametrize("key", ["exclude", "files"])
def test_a_large_file_hook_that_skips_files_is_found(root: Path, key: str) -> None:
    edit_yaml(root, ".pre-commit-config.yaml", lambda data: large_file_hook(data).update({key: "^assets/"}))
    (problem,) = found(af.large_file_limit_matches, context(root))
    assert f"`{key}`" in problem.message and "never exclude" in problem.message


def test_a_large_file_hook_without_enforce_all_is_found(root: Path) -> None:
    edit(root, ".pre-commit-config.yaml", ', "--enforce-all"]', "]")
    (problem,) = found(af.large_file_limit_matches, context(root))
    assert "--enforce-all" in problem.message


# --- files.game-files ---------------------------------------------------------------------------------------------


def game_files_pattern() -> re.Pattern[str]:
    """The `files` pattern of the repository's own no-game-files hook."""
    data = yaml.safe_load((REPO / ".pre-commit-config.yaml").read_text(encoding="utf-8"))
    hook = next(hook for repo in data["repos"] for hook in repo["hooks"] if hook["id"] == af.GAME_FILES_HOOK)
    return re.compile(hook["files"])


@pytest.mark.parametrize("path", af.GAME_FILE_SAMPLES)
def test_the_game_files_hook_refuses_game_data_and_local_records(path: str) -> None:
    assert game_files_pattern().search(path), path


def test_the_game_files_hook_matches_no_tracked_file() -> None:
    tracked = subprocess.run(
        ["git", "-C", str(REPO), "ls-files"], capture_output=True, text=True, check=True
    ).stdout.splitlines()
    pattern = game_files_pattern()
    assert [path for path in tracked if pattern.search(path)] == []


def test_the_clean_game_files_hook_passes(root: Path) -> None:
    assert found(af.game_files_refused, context(root)) == []


def test_a_config_without_the_game_files_hook_is_found(root: Path) -> None:
    edit(root, ".pre-commit-config.yaml", f"id: {af.GAME_FILES_HOOK}", "id: something-else")
    (problem,) = found(af.game_files_refused, context(root))
    assert af.GAME_FILES_HOOK in problem.message


def test_a_game_files_hook_that_misses_a_capture_suffix_is_found(root: Path) -> None:
    edit(root, ".pre-commit-config.yaml", "p2s|", "")
    messages = [problem.message for problem in found(af.game_files_refused, context(root))]
    assert any(".p2s" in message and "workspace.game_capture_suffixes" in message for message in messages)


def test_a_game_files_hook_that_misses_the_busy_marker_is_found(root: Path) -> None:
    edit(root, ".pre-commit-config.yaml", r"|\.coney-busy", "")
    problems = found(af.game_files_refused, context(root))
    assert problems and all(".coney-busy" in problem.message for problem in problems)


def test_a_game_files_hook_that_misses_a_local_record_is_found(root: Path) -> None:
    edit(root, ".pre-commit-config.yaml", "HANDOFF|STATUS|", "HANDOFF|")
    (problem,) = found(af.game_files_refused, context(root))
    assert "STATUS.md" in problem.message and "GAME_FILE_SAMPLES" in problem.message


def test_a_game_files_hook_with_an_exclude_is_found(root: Path) -> None:
    def add_exclude(data: dict[str, Any]) -> None:
        hook = next(hook for repo in data["repos"] for hook in repo["hooks"] if hook["id"] == af.GAME_FILES_HOOK)
        hook["exclude"] = "^tests/"

    edit_yaml(root, ".pre-commit-config.yaml", add_exclude)
    (problem,) = found(af.game_files_refused, context(root))
    assert "`exclude`" in problem.message


def game_files_hook(data: dict[str, Any]) -> dict[str, Any]:
    """The no-game-files entry of a loaded pre-commit config."""
    return next(hook for repo in data["repos"] for hook in repo["hooks"] if hook["id"] == af.GAME_FILES_HOOK)


@pytest.mark.parametrize(
    ("key", "value"),
    [
        ("types", ["binary"]),
        ("types_or", ["text"]),
        ("exclude_types", ["binary"]),
        ("stages", ["manual"]),
        ("pass_filenames", False),
    ],
)
def test_a_guard_narrowed_by_a_hook_setting_is_found(root: Path, key: str, value: object) -> None:
    edit_yaml(root, ".pre-commit-config.yaml", lambda data: game_files_hook(data).update({key: value}))
    edit_yaml(root, ".pre-commit-config.yaml", lambda data: large_file_hook(data).update({key: value}))
    for check in (af.game_files_refused, af.large_file_limit_matches):
        (problem,) = found(check, context(root))
        assert key in problem.message


def test_a_game_files_hook_that_is_not_language_fail_is_found(root: Path) -> None:
    edit_yaml(root, ".pre-commit-config.yaml", lambda data: game_files_hook(data).update({"language": "pygrep"}))
    (problem,) = found(af.game_files_refused, context(root))
    assert "language: fail" in problem.message


@pytest.mark.parametrize(
    ("key", "value", "words"),
    [
        ("exclude", "^(assets/|LICENSE)", "top-level `exclude`"),
        ("files", "^src/", "top-level `files`"),
        ("default_stages", ["manual"], "default_stages"),
    ],
)
def test_a_top_level_setting_that_hides_files_from_the_guards_is_found(
    root: Path, key: str, value: object, words: str
) -> None:
    edit_yaml(root, ".pre-commit-config.yaml", lambda data: data.update({key: value}))
    for check in (af.game_files_refused, af.large_file_limit_matches):
        (problem,) = found(check, context(root))
        assert words in problem.message


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


def commit_all(root: Path, message: str) -> None:
    """Stage everything in the made-up repository and commit it."""
    subprocess.run(["git", "add", "-A"], cwd=root, check=True, env=GIT_ENV)
    subprocess.run(["git", "commit", "-q", "--allow-empty", "-m", message], cwd=root, check=True, env=GIT_ENV)


def run_scope_on(root: Path, event: str = "pull_request", draft: str = "false") -> tuple[dict[str, str], str]:
    """Run the script in `root` on the commit HEAD against HEAD^1; its outputs and what it printed."""
    output = root / "out.txt"
    output.unlink(missing_ok=True)
    env = {**GIT_ENV, "EVENT": event, "DRAFT": draft, "GITHUB_OUTPUT": str(output)}
    bash = find_bash()
    assert bash is not None
    done = subprocess.run(
        [bash, ".github/scripts/ci-scope.sh"], cwd=root, env=env, capture_output=True, text=True, check=False
    )
    assert done.returncode == 0, done.stderr
    outputs = dict(line.split("=", 1) for line in output.read_text(encoding="utf-8").splitlines())
    return outputs, done.stdout


def make_change(
    root: Path, changed: list[str], moved: tuple[tuple[str, str], ...] = (), deleted: tuple[str, ...] = ()
) -> None:
    """Commit a base, then a change (`changed` files written, `moved` pairs renamed, `deleted` files removed), in a
    made-up repository holding the script and the conventions."""
    for name in (*(old for old, _ in moved), *deleted):
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_text("base\n", encoding="utf-8")
    subprocess.run(["git", "init", "-q", "-b", "main"], cwd=root, check=True, env=GIT_ENV)
    commit_all(root, "base")
    for name in changed:
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        with (root / name).open("a", encoding="utf-8", newline="\n") as handle:
            handle.write("# changed\n")
    for old, new in moved:
        (root / new).parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "mv", old, new], cwd=root, check=True, env=GIT_ENV)
    for name in deleted:
        (root / name).unlink()
    commit_all(root, "change")


def run_scope(
    root: Path,
    changed: list[str],
    event: str = "pull_request",
    draft: str = "false",
    moved: tuple[tuple[str, str], ...] = (),
    deleted: tuple[str, ...] = (),
) -> dict[str, str]:
    """`make_change`, then the script's outputs for that change."""
    make_change(root, changed, moved, deleted)
    return run_scope_on(root, event, draft)[0]


ALL_OS = '["ubuntu-24.04","windows-2022","macos-15"]'
LINUX = '["ubuntu-24.04"]'

#: What a ready pull request runs for each change: the engine jobs (build, sanitizers, clang-tidy) or not, and the
#: OSes python runs on. One class at a time, then mixed pull requests, which get the union of what their files need.
SCOPE_CASES = [
    # docs_only: no engine jobs, python on Linux.
    (["docs/guides/a.md"], "false", LINUX),
    (["README.md"], "false", LINUX),
    (["research/x.yaml"], "false", LINUX),
    ([".github/conventions.yml"], "false", LINUX),
    (["mkdocs.yml"], "false", LINUX),
    (["docs/a.md", "CONTRIBUTING.md"], "false", LINUX),
    ([".github/workflows/pr.yml"], "false", LINUX),
    ([".github/workflows/docs.yml"], "false", LINUX),
    # tools_only: no engine jobs, python on every OS.
    (["python/src/coney_tools/cli.py"], "false", ALL_OS),
    (["python/README.md"], "false", ALL_OS),
    ([".claude/hooks/a.py"], "false", ALL_OS),
    ([".pre-commit-config.yaml"], "false", ALL_OS),
    ([".github/ISSUE_TEMPLATE/bug.yml"], "false", ALL_OS),
    ([".github/workflows/python.yml"], "false", ALL_OS),
    (["bootstrap.sh", "tools/a.ps1"], "false", ALL_OS),
    # engine: the engine jobs, python on Linux.
    (["src/core/a.cpp"], "true", LINUX),
    (["tests/core/a_test.cpp"], "true", LINUX),
    (["assets/sandbox/a.txt"], "true", LINUX),
    (["CMakeLists.txt"], "true", LINUX),
    (["cmake/deps.cmake"], "true", LINUX),
    ([".clang-tidy"], "true", LINUX),
    ([".github/workflows/build.yml"], "true", LINUX),
    ([".github/actions/ccache/action.yml"], "true", LINUX),
    # unknown: everything.
    (["sub/README.md"], "true", ALL_OS),
    (["docs.md/x"], "true", ALL_OS),
    ([".github/workflows/new.yml"], "true", ALL_OS),
    ([".gitattributes"], "true", ALL_OS),
    # Mixed: the union.
    (["docs/a.md", "python/src/coney_tools/cli.py"], "false", ALL_OS),
    (["docs/a.md", "src/b.cpp"], "true", LINUX),
    (["python/src/coney_tools/cli.py", "src/b.cpp"], "true", ALL_OS),
    (["src/b.cpp", ".github/workflows/python.yml"], "true", ALL_OS),
    (["docs/a.md", "src/b.cpp", "LICENSE"], "true", ALL_OS),
    ([".github/workflows/build.yml", ".github/workflows/python.yml"], "true", ALL_OS),
]


@needs_bash
@pytest.mark.parametrize(("changed", "build", "python_os"), SCOPE_CASES)
def test_a_pull_request_runs_what_its_files_need(root: Path, changed: list[str], build: str, python_os: str) -> None:
    result = run_scope(root, changed)
    assert (result["build"], result["python"], result["python-os"]) == (build, "true", python_os)


@needs_bash
def test_the_script_sorts_files_the_way_the_conventions_do(root: Path) -> None:
    """Every path of the conventions' table, changed at once: the class the script prints for each is `ci_class`."""
    make_change(root, [path for path, _ in CI_CLASSES])
    _, printed = run_scope_on(root)
    sorted_by_script: dict[str, str] = {}
    for line in printed.splitlines():
        if line.startswith("  "):
            name, path = line.split()
            sorted_by_script[path] = name
    paths = load_conventions(root).paths
    assert sorted_by_script == {path: paths.ci_class(path) for path, _ in CI_CLASSES}
    assert sorted_by_script == dict(CI_CLASSES)


@needs_bash
@pytest.mark.parametrize(
    ("moved", "build", "python_os"),
    [
        # Both sides of a move count: out of src/ is still an engine change, into python/ a tools change.
        ((("src/a.cpp", "docs/a.md"),), "true", LINUX),
        ((("docs/a.md", "src/a.cpp"),), "true", LINUX),
        ((("python/a.py", "python/b.py"),), "false", ALL_OS),
        ((("docs/a.md", "docs/b.md"),), "false", LINUX),
        ((("docs/a.md", "python/a.md"),), "false", ALL_OS),
        ((("misc/a.txt", "docs/a.md"),), "true", ALL_OS),
    ],
)
def test_a_rename_counts_both_of_its_sides(
    root: Path, moved: tuple[tuple[str, str], ...], build: str, python_os: str
) -> None:
    result = run_scope(root, [], moved=moved)
    assert (result["build"], result["python-os"]) == (build, python_os)


@needs_bash
@pytest.mark.parametrize(
    ("deleted", "build", "python_os"),
    [(("src/a.cpp",), "true", LINUX), (("python/a.py",), "false", ALL_OS), (("docs/a.md",), "false", LINUX)],
)
def test_a_deleted_file_counts_like_a_changed_one(
    root: Path, deleted: tuple[str, ...], build: str, python_os: str
) -> None:
    result = run_scope(root, [], deleted=deleted)
    assert (result["build"], result["python-os"]) == (build, python_os)


@needs_bash
def test_an_empty_change_runs_everything(root: Path) -> None:
    result = run_scope(root, [])
    assert (result["build"], result["python"], result["python-os"]) == ("true", "true", ALL_OS)


@needs_bash
@pytest.mark.parametrize("key", ["docs_only", "tools_only", "engine"])
def test_the_script_reads_a_changed_list_from_the_conventions(root: Path, key: str) -> None:
    """Add a pattern to one list of the file and the script follows it: there is no second copy."""
    text = (root / CONVENTIONS_FILE).read_text(encoding="utf-8")
    (root / CONVENTIONS_FILE).write_text(
        text.replace(f"  {key}:\n", f'  {key}:\n    - "notes-x/"\n', 1), encoding="utf-8", newline="\n"
    )
    result = run_scope(root, ["notes-x/a.txt"])
    expected = {"docs_only": ("false", LINUX), "tools_only": ("false", ALL_OS), "engine": ("true", LINUX)}
    assert (result["build"], result["python-os"]) == expected[key]


@needs_bash
def test_a_change_to_the_lists_is_judged_by_the_lists_it_merges_into(root: Path) -> None:
    """The conventions file is docs-only, so a pull request that edits src/ and also lists src/ as docs-only must
    still run the engine jobs: the script reads the lists of the base (HEAD^1), not of the merge commit."""
    subprocess.run(["git", "init", "-q", "-b", "main"], cwd=root, check=True, env=GIT_ENV)
    commit_all(root, "base")
    edit(root, CONVENTIONS_FILE, "  docs_only:\n", '  docs_only:\n    - "src/"\n')
    (root / "src").mkdir(exist_ok=True)
    (root / "src" / "a.cpp").write_text("int a;\n", encoding="utf-8")
    commit_all(root, "change")
    outputs, printed = run_scope_on(root)
    assert (outputs["build"], outputs["python-os"]) == ("true", LINUX)
    assert "engine     src/a.cpp" in printed


@needs_bash
def test_the_script_falls_back_to_its_own_lists_when_the_base_has_none(root: Path) -> None:
    """A pull request that adds the conventions file has no list in its base, so the script reads the file it has."""
    subprocess.run(["git", "init", "-q", "-b", "main"], cwd=root, check=True, env=GIT_ENV)
    subprocess.run(["git", "add", "-A"], cwd=root, check=True, env=GIT_ENV)
    subprocess.run(["git", "reset", "-q", "--", CONVENTIONS_FILE], cwd=root, check=True, env=GIT_ENV)
    subprocess.run(["git", "commit", "-q", "-m", "base"], cwd=root, check=True, env=GIT_ENV)
    (root / "src").mkdir(exist_ok=True)
    (root / "src" / "a.cpp").write_text("int a;\n", encoding="utf-8")
    commit_all(root, "change")
    outputs, _ = run_scope_on(root)
    assert (outputs["build"], outputs["python-os"]) == ("true", LINUX)


@needs_bash
@pytest.mark.parametrize("key", ["docs_only", "tools_only", "engine"])
def test_a_conventions_file_without_a_list_stops_the_script(root: Path, key: str) -> None:
    text = (root / CONVENTIONS_FILE).read_text(encoding="utf-8").replace(f"  {key}:", f"  {key}_renamed:")
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
    assert done.returncode != 0 and f"found no paths.{key}" in done.stderr


@needs_bash
@pytest.mark.parametrize("event", ["push", "schedule", "workflow_dispatch"])
def test_pushes_nightly_and_manual_runs_get_everything(root: Path, event: str) -> None:
    result = run_scope(root, ["docs/a.md"], event=event)
    assert (result["build"], result["python"], result["python-os"]) == ("true", "true", ALL_OS)


@needs_bash
@pytest.mark.parametrize("changed", [["src/a.cpp"], ["python/a.py"], ["docs/a.md"], ["misc/a"]])
def test_a_draft_runs_only_the_cheap_checks(root: Path, changed: list[str]) -> None:
    result = run_scope(root, changed, draft="true")
    assert result["build"] == "false" and result["python"] == "false"


def test_a_template_that_forgets_the_runtime_line_is_found(root: Path) -> None:
    edit(root, ".github/pull_request_template.md", "- Runtime: ...", "- ...")
    edit(root, ".github/pull_request_template.md", "`Runtime: none (<reason>)`", "`Runtime: skip`")
    messages = " | ".join(problem.message for problem in found(af.template_matches, context(root)))
    assert "does not mention Runtime: ..." in messages and "does not mention `Runtime: none (<reason>)`" in messages


# --- files.ci-places --------------------------------------------------------------------------------------------


def test_a_job_that_is_in_no_place_is_found(root: Path) -> None:
    def add_job(data: dict[str, Any]) -> None:
        data["jobs"]["extra"] = {"runs-on": "ubuntu-24.04", "steps": [{"run": "true"}]}

    edit_yaml(root, ".github/workflows/pr.yml", add_job)
    problems = found(registered()["files.ci-places"], context(root))
    assert [problem.message for problem in problems] == [
        "the job pr/extra is in no place of ci.places (one place per check)"
    ]


def test_a_place_that_names_a_missing_job_is_found(root: Path) -> None:
    def drop_job(data: dict[str, Any]) -> None:
        del data["jobs"]["body"]

    edit_yaml(root, ".github/workflows/pr.yml", drop_job)
    problems = found(registered()["files.ci-places"], context(root))
    assert [problem.message for problem in problems] == ["ci.places names the job pr/body, which no workflow has"]


def test_a_new_workflow_must_be_placed_too(root: Path) -> None:
    (root / ".github" / "workflows" / "new.yml").write_text(
        "name: new\non: push\njobs:\n  check:\n    runs-on: ubuntu-24.04\n    steps:\n      - run: 'true'\n",
        encoding="utf-8",
    )
    problems = found(registered()["files.ci-places"], context(root))
    assert [problem.message for problem in problems] == [
        "the job new/check is in no place of ci.places (one place per check)"
    ]


def test_a_template_that_forgets_the_issues_affected_none_line_is_found(root: Path) -> None:
    edit(root, ".github/pull_request_template.md", "`None (<reason>)`", "`nothing`")
    messages = " | ".join(problem.message for problem in found(af.template_matches, context(root)))
    assert "does not mention `None (<reason>)`" in messages and "pull_requests.issues_affected" in messages
    edit(root, ".github/pull_request_template.md", "`None (one-line fix)`", "`None (x)`")
    messages = " | ".join(problem.message for problem in found(af.template_matches, context(root)))
    assert "does not mention `None (one-line fix)`" in messages
