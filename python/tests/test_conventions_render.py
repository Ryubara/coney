# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.conventions_render: the marker parser, each generated block, and `coney-tools repo render`.

Synthetic inputs only: small texts with markers, made-up checkouts holding them, and the real conventions file for the
values the blocks must show.
"""

import re
import shutil
from pathlib import Path

import pytest
from conventions_support import REPO, conventions_with, write_checkout

from coney_tools import check_cli
from coney_tools.cli import main
from coney_tools.conventions import CONVENTIONS_FILE, Conventions, load_conventions
from coney_tools.conventions_render import (
    BLOCKS,
    WIDTH,
    BlockError,
    block_files,
    render_block,
    render_text,
    run_render,
)

CONVENTIONS = load_conventions(REPO)


def marked(name: str, body: str = "") -> str:
    """A text holding one block called `name` with `body` between its markers."""
    return f"before\n\n<!-- conventions:begin {name} -->\n{body}<!-- conventions:end -->\n\nafter\n"


# --- the marker parser ----------------------------------------------------------------------------------------------


def test_a_block_between_markers_is_replaced_and_the_text_around_it_is_kept() -> None:
    result = render_text(marked("commit-areas", "stale\n"), CONVENTIONS, "page.md")
    assert result.startswith("before\n\n<!-- conventions:begin commit-areas -->\n| Area | Covers |")
    assert result.endswith("<!-- conventions:end -->\n\nafter\n")
    assert "stale" not in result


def test_rendering_is_idempotent() -> None:
    once = render_text(marked("labels"), CONVENTIONS, "page.md")
    assert render_text(once, CONVENTIONS, "page.md") == once


def test_several_blocks_in_one_text_are_each_filled() -> None:
    text = marked("commit-areas") + marked("required-checks")
    result = render_text(text, CONVENTIONS, "page.md")
    assert "| `build` |" in result and "`build-ok`" in result


def test_a_text_without_markers_is_returned_unchanged() -> None:
    text = "# Title\n\nNo blocks here.\n"
    assert render_text(text, CONVENTIONS, "page.md") == text


def test_crlf_text_comes_back_with_lf_line_ends() -> None:
    text = marked("commit-areas").replace("\n", "\r\n")
    assert "\r" not in render_text(text, CONVENTIONS, "page.md")


def test_markers_inside_a_code_fence_are_an_example_not_a_block() -> None:
    text = "```markdown\n<!-- conventions:begin no-such-block -->\nx\n<!-- conventions:end -->\n```\n"
    assert render_text(text, CONVENTIONS, "page.md") == text


def test_an_unknown_block_names_the_file_the_line_and_the_known_blocks() -> None:
    with pytest.raises(BlockError, match=r"page\.md:3: unknown block 'nope'.*commit-areas"):
        render_text(marked("nope"), CONVENTIONS, "page.md")


def test_a_begin_without_an_end_is_refused() -> None:
    with pytest.raises(BlockError, match=r"page\.md:3: block 'labels' is never closed"):
        render_text("a\n\n<!-- conventions:begin labels -->\nx\n", CONVENTIONS, "page.md")


def test_an_end_without_a_begin_is_refused() -> None:
    with pytest.raises(BlockError, match=r"page\.md:2: .*end.* without a begin"):
        render_text("a\n<!-- conventions:end -->\n", CONVENTIONS, "page.md")


def test_a_block_opened_inside_a_block_is_refused() -> None:
    text = "<!-- conventions:begin labels -->\n<!-- conventions:begin commit-areas -->\n<!-- conventions:end -->\n"
    with pytest.raises(BlockError, match=r"page\.md:2: .*inside the block"):
        render_text(text, CONVENTIONS, "page.md")


# --- the blocks -----------------------------------------------------------------------------------------------------


def test_every_block_renders_something_that_fits_the_line_length() -> None:
    for name in BLOCKS:
        lines = render_block(name, CONVENTIONS)
        assert lines, name
        assert not lines[0].startswith("<!--") and lines[-1].strip(), name
        for line in lines:
            # Tables are exempt from the line length in .markdownlint.yaml.
            assert len(line) <= WIDTH or line.startswith("|"), f"{name}: {line}"


def test_commit_rules_show_the_title_form_the_limit_the_verbs_and_the_areas() -> None:
    text = "\n".join(render_block("commit-rules", CONVENTIONS))
    title = CONVENTIONS.commits.title
    assert f"`{title.form}`" in text and f"at most {title.max_length} characters" in text
    for verb in title.verbs:
        assert re.search(rf"\b{verb}\b", text)
    for area in title.areas:
        assert f"`{area}`" in text
    assert f"`{title.breaking_suffix}`" in text


def test_commit_rules_follow_a_changed_limit_verb_and_area() -> None:
    conventions = conventions_with(("commits.title.max_length", 80), ("commits.title.verbs", ["Add", "Zap"]))
    text = "\n".join(render_block("commit-rules", conventions))
    assert "at most 80 characters" in text and "Zap" in text and "Fix" not in text


def test_commit_rules_state_the_trailer_and_the_noreply_addresses() -> None:
    text = "\n".join(render_block("commit-rules", CONVENTIONS))
    trailer = CONVENTIONS.commits.trailers.ai_co_author
    assert f"{trailer.key}: {trailer.value}" in text
    for address in trailer.noreply:
        assert address in text
    assert "no other trailer" in text and "no other email address" in text.lower()


def test_commit_rules_drop_the_prohibitions_the_conventions_lift() -> None:
    conventions = conventions_with(
        ("commits.trailers.only_ai_co_author", False), ("commits.trailers.other_emails", True)
    )
    text = "\n".join(render_block("commit-rules", conventions))
    assert "no other trailer" not in text and "no other email address" not in text.lower()


def test_commit_areas_is_a_table_of_every_area_with_what_it_covers_and_the_verbs() -> None:
    lines = render_block("commit-areas", CONVENTIONS)
    assert lines[:2] == ["| Area | Covers |", "| --- | --- |"]
    areas = CONVENTIONS.commits.title.areas
    rows = [line for line in lines if line.startswith("| `")]
    assert rows == [f"| `{area}` | {covers} |" for area, covers in areas.items()]
    assert any(line.startswith("Verbs: ") and "Document" in line for line in lines)


def test_a_pipe_in_an_area_description_does_not_break_the_table() -> None:
    conventions = conventions_with(("commits.title.areas.build", "CMake | presets"))
    assert "| `build` | CMake \\| presets |" in render_block("commit-areas", conventions)


def test_labels_table_lists_every_label_with_its_family_and_description() -> None:
    lines = render_block("labels", CONVENTIONS)
    assert lines[:2] == ["| Family | Label | Meaning |", "| --- | --- | --- |"]
    rows = [line for line in lines if line.startswith("| ") and "`" in line]
    assert len(rows) == len(CONVENTIONS.labels.list)
    for label in CONVENTIONS.labels.list:
        assert any(f"`{label.name}`" in row and label.description in row for row in rows), label.name


def test_labels_table_names_each_family_once_in_the_order_of_the_file() -> None:
    rows = [line for line in render_block("labels", CONVENTIONS) if "`" in line]
    families = [row.split("|")[1].strip() for row in rows]
    named = [name for name in families if name]
    assert named == ["Type", "Area", "Priority", "Status", "GitHub's own"]


def test_pr_rules_name_the_sections_the_filled_ones_and_the_special_lines() -> None:
    text = "\n".join(render_block("pr-rules", CONVENTIONS))
    rules = CONVENTIONS.pull_requests
    assert ", ".join(rules.sections) in text
    assert "Summary and Testing" in text
    assert f"`{rules.docs_none.line}`" in text and f"`{rules.closing.form}`" in text
    assert f"`{rules.remaining.label}`" in text and f"`{rules.remaining.unchecked_cites}`" in text
    assert f"`{rules.screenshots.none} (<reason>)`" in text
    assert f"`{rules.ai_use.written_by.label}`" in text and f"`{rules.ai_use.model.label}`" in text
    for choice in rules.ai_use.written_by.choices:
        assert f"`{choice}`" in text


def test_pr_rules_follow_a_changed_section_list_and_docs_none_line() -> None:
    conventions = conventions_with(("pull_requests.docs_none.line", "Docs: skip"))
    text = "\n".join(render_block("pr-rules", conventions))
    assert "`Docs: skip`" in text and "Docs: none" not in text


def test_pr_rules_say_which_paths_ask_for_docs() -> None:
    text = "\n".join(render_block("pr-rules", CONVENTIONS))
    assert "`src/`" in text and "`python/src/`" in text and "`docs/`" in text and "`research/`" in text


def test_required_checks_list_every_check_and_the_merge_method() -> None:
    text = "\n".join(render_block("required-checks", CONVENTIONS))
    for check in CONVENTIONS.merge.required_checks:
        assert f"`{check}`" in text
    assert "squash" in text


def test_required_checks_follow_a_changed_list() -> None:
    conventions = conventions_with(("merge.required_checks", ["title", "tidy"]))
    text = "\n".join(render_block("required-checks", conventions))
    assert "`tidy`" in text and "`build-ok`" not in text


def test_docs_only_paths_explain_each_kind_of_pattern() -> None:
    lines = render_block("docs-only-paths", CONVENTIONS)
    by_pattern = {line.split("`")[1]: line for line in lines if line.startswith("- `")}
    assert set(by_pattern) == set(CONVENTIONS.paths.docs_only)
    assert "folder" in by_pattern["docs/"]
    assert "top level" in by_pattern["*.md"]
    assert "`.github/`" in by_pattern[".github/*.md"]
    assert "file" in by_pattern["mkdocs.yml"]


def test_a_code_span_is_never_split_across_two_lines() -> None:
    # A long list of verbs forces wrapping; the trailer's code span has spaces in it.
    conventions = conventions_with(("commits.title.verbs", [f"Verb{n}" for n in range(30)]))
    for line in render_block("commit-rules", conventions):
        assert line.count("`") % 2 == 0, line


# --- files and the command ------------------------------------------------------------------------------------------


def checkout_with(tmp_path: Path, files: dict[str, str]) -> Path:
    """A made-up checkout holding a copy of the real conventions file and `files` (name to text).

    The file is copied, not dumped, so the areas and labels keep the real file's order.
    """
    root = write_checkout(tmp_path)
    shutil.copyfile(REPO / CONVENTIONS_FILE, root / CONVENTIONS_FILE)
    for name, text in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="\n")
    return root


def test_the_files_with_blocks_are_the_agents_file_contributing_and_every_guide(tmp_path: Path) -> None:
    root = checkout_with(
        tmp_path,
        {
            "AGENTS.md": "a",
            "CONTRIBUTING.md": "b",
            "README.md": "not scanned",
            "docs/guides/one.md": "c",
            "docs/guides/review/two.md": "d",
            "docs/research/page.md": "not scanned",
        },
    )
    names = [path.relative_to(root).as_posix() for path in block_files(root)]
    assert names == ["AGENTS.md", "CONTRIBUTING.md", "docs/guides/one.md", "docs/guides/review/two.md"]


def test_render_writes_the_stale_block_and_reports_it(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    root = checkout_with(tmp_path, {"AGENTS.md": marked("commit-areas", "stale\n")})
    assert run_render(root, check=False) == 0
    text = (root / "AGENTS.md").read_text(encoding="utf-8")
    assert "stale" not in text and "| `build` |" in text
    assert "AGENTS.md" in capsys.readouterr().out
    assert run_render(root, check=True) == 0


def test_check_fails_naming_the_stale_file_and_changes_nothing(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    stale = marked("commit-areas", "stale\n")
    root = checkout_with(
        tmp_path, {"docs/guides/page.md": stale, "AGENTS.md": render_text(marked("labels"), CONVENTIONS, "x")}
    )
    assert run_render(root, check=True) == 1
    err = capsys.readouterr().err
    assert "docs/guides/page.md" in err and "AGENTS.md" not in err
    assert "repo render" in err
    assert (root / "docs/guides/page.md").read_text(encoding="utf-8") == stale


def test_check_passes_for_files_that_are_current(tmp_path: Path) -> None:
    current = render_text(marked("commit-areas"), CONVENTIONS, "x")
    root = checkout_with(tmp_path, {"CONTRIBUTING.md": current})
    assert run_render(root, check=True) == 0


def test_a_file_with_crlf_line_ends_is_current_when_its_blocks_are(tmp_path: Path) -> None:
    current = render_text(marked("commit-areas"), CONVENTIONS, "x")
    root = checkout_with(tmp_path, {})
    (root / "AGENTS.md").write_bytes(current.replace("\n", "\r\n").encode("utf-8"))
    assert run_render(root, check=True) == 0


def test_a_broken_marker_in_a_file_names_the_file(tmp_path: Path) -> None:
    root = checkout_with(tmp_path, {"AGENTS.md": "x\n<!-- conventions:end -->\n"})
    with pytest.raises(BlockError, match=r"AGENTS\.md:2"):
        run_render(root, check=True)


def test_the_command_exits_2_with_one_line_for_a_broken_marker(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(checkout_with(tmp_path, {"AGENTS.md": "x\n<!-- conventions:end -->\n"}))
    assert main(["repo", "render", "--check"]) == 2
    err = capsys.readouterr().err
    assert "AGENTS.md:2" in err and "Traceback" not in err


def test_the_command_check_flag_decides_between_writing_and_refusing(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    root = checkout_with(tmp_path, {"AGENTS.md": marked("labels", "stale\n")})
    monkeypatch.chdir(root)
    assert main(["repo", "render", "--check"]) == 1
    assert "stale" in (root / "AGENTS.md").read_text(encoding="utf-8")
    assert main(["repo", "render"]) == 0
    assert main(["repo", "render", "--check"]) == 0


# --- this repository ------------------------------------------------------------------------------------------------


def used_blocks() -> dict[str, list[str]]:
    """Which files of this checkout hold each block name."""
    found: dict[str, list[str]] = {}
    for path in block_files(REPO):
        text = path.read_text(encoding="utf-8")
        for name in re.findall(r"^<!-- conventions:begin ([a-z0-9-]+) -->$", text, re.MULTILINE):
            found.setdefault(name, []).append(path.relative_to(REPO).as_posix())
    return found


def test_the_committed_blocks_of_this_repository_are_current(capsys: pytest.CaptureFixture[str]) -> None:
    assert run_render(REPO, check=True) == 0, capsys.readouterr().err


def test_every_block_is_used_somewhere_in_this_repository() -> None:
    assert set(BLOCKS) <= set(used_blocks())


def test_the_agents_file_keeps_its_rules_in_blocks_and_stays_short() -> None:
    assert "commit-rules" in used_blocks() and "AGENTS.md" in used_blocks()["commit-rules"]
    assert len((REPO / "AGENTS.md").read_text(encoding="utf-8").splitlines()) <= 110


def test_the_files_that_used_to_type_the_lists_by_hand_hold_the_blocks() -> None:
    used = used_blocks()
    assert "CONTRIBUTING.md" in used["commit-areas"] and "CONTRIBUTING.md" in used["pr-rules"]
    assert "docs/guides/issues.md" in used["labels"]
    assert "docs/guides/building.md" in used["docs-only-paths"] and "docs/guides/building.md" in used["required-checks"]


def test_every_block_renderer_takes_only_the_conventions() -> None:
    conventions: Conventions = CONVENTIONS
    for renderer in BLOCKS.values():
        assert renderer(conventions)


# --- the local gate -------------------------------------------------------------------------------------------------


def test_the_local_gate_repo_step_reports_a_stale_block(tmp_path: Path) -> None:
    root = checkout_with(tmp_path, {"AGENTS.md": marked("labels", "stale\n")})
    for name in ("CLAUDE.md", "GEMINI.md"):
        (root / name).write_text("@AGENTS.md\n", encoding="utf-8")
    result = check_cli._step_repo(root)
    assert not result.ok and "AGENTS.md" in result.output[0] and "repo render" in result.output[0]
    assert run_render(root, check=False) == 0
    assert check_cli._step_repo(root).ok
