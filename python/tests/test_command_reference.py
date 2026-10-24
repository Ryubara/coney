# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.command_reference: the generated parameter blocks of the coney-tools guides.

Synthetic parsers and made-up checkouts, except the last tests, which hold the real guides to the real command line.
"""

import argparse
from pathlib import Path

import pytest
from conventions_support import REPO

from coney_tools.cli import main
from coney_tools.command_params import Group, read_groups, render_group
from coney_tools.command_reference import (
    GUIDES_DIR,
    BlockError,
    block_files,
    check_blocks,
    command_groups,
    render_text,
    run_render,
)


def make_parser() -> argparse.ArgumentParser:
    """A command line with two groups."""
    parser = argparse.ArgumentParser(prog="coney-tools")
    groups = parser.add_subparsers(dest="group", required=True)
    alpha = groups.add_parser("alpha", help="the alpha group").add_subparsers(dest="command", required=True)
    alpha.add_parser("go", help="go there").add_argument("--fast", action="store_true", help="hurry")
    groups.add_parser("beta", help="the beta group").add_subparsers(dest="command", required=True).add_parser(
        "stop", help="stop"
    )
    return parser


def marked(name: str, body: str = "") -> str:
    """A text holding one block called `name` with `body` between its markers."""
    return f"before\n\n<!-- command-reference:begin {name} -->\n{body}<!-- command-reference:end -->\n\nafter\n"


def checkout(tmp_path: Path, pages: dict[str, str]) -> Path:
    """A made-up checkout whose guides are `pages` (name to text)."""
    for name, text in pages.items():
        path = tmp_path / GUIDES_DIR / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="\n")
    return tmp_path


def current(groups: dict[str, Group], name: str) -> str:
    """A page holding the up-to-date block of group `name`."""
    return render_text(marked(name), groups, "page.md")


# --- the marker parser ----------------------------------------------------------------------------------------------


def test_a_block_is_filled_from_its_group_and_the_text_around_it_is_kept() -> None:
    groups = read_groups(make_parser())
    result = render_text(marked("alpha", "stale\n"), groups, "page.md")
    assert result.startswith("before\n\n<!-- command-reference:begin alpha -->\n## Parameters of `alpha`")
    assert result.endswith("<!-- command-reference:end -->\n\nafter\n")
    assert "stale" not in result
    assert "\n".join(render_group(groups["alpha"])) in result


def test_rendering_is_idempotent_and_leaves_a_text_without_markers_alone() -> None:
    groups = read_groups(make_parser())
    once = render_text(marked("beta"), groups, "page.md")
    assert render_text(once, groups, "page.md") == once
    assert render_text("# Title\n\nText.\n", groups, "page.md") == "# Title\n\nText.\n"


def test_an_unknown_group_is_an_error_naming_the_line_and_the_groups() -> None:
    with pytest.raises(BlockError, match=r"page\.md:3: unknown group 'gamma' \(known: alpha, beta\)"):
        render_text(marked("gamma"), read_groups(make_parser()), "page.md")


def test_an_end_without_a_begin_and_a_block_never_closed_are_errors() -> None:
    groups = read_groups(make_parser())
    with pytest.raises(BlockError, match="end marker without a begin"):
        render_text("text\n<!-- command-reference:end -->\n", groups, "page.md")
    with pytest.raises(BlockError, match="never closed"):
        render_text("<!-- command-reference:begin alpha -->\nx\n", groups, "page.md")


def test_a_begin_inside_a_block_is_an_error() -> None:
    text = "<!-- command-reference:begin alpha -->\n<!-- command-reference:begin beta -->\n"
    with pytest.raises(BlockError, match="inside the block opened on line 1"):
        render_text(text, read_groups(make_parser()), "page.md")


def test_markers_in_a_code_fence_are_an_example() -> None:
    text = "```\n<!-- command-reference:begin gamma -->\n```\n"
    assert render_text(text, read_groups(make_parser()), "page.md") == text


def test_crlf_text_is_read_and_written_with_lf() -> None:
    groups = read_groups(make_parser())
    assert "\r" not in render_text(marked("alpha").replace("\n", "\r\n"), groups, "page.md")


# --- the files --------------------------------------------------------------------------------------------------------


def test_the_guides_are_scanned_at_any_depth_and_nothing_else(tmp_path: Path) -> None:
    root = checkout(tmp_path, {"a.md": "x", "sub/b.md": "y"})
    (root / "docs" / "other.md").write_text("z", encoding="utf-8")
    assert [path.relative_to(root).as_posix() for path in block_files(root)] == [
        "docs/guides/a.md",
        "docs/guides/sub/b.md",
    ]


def test_current_blocks_for_every_group_pass(tmp_path: Path) -> None:
    groups = read_groups(make_parser())
    root = checkout(tmp_path, {"a.md": current(groups, "alpha"), "b.md": current(groups, "beta")})
    assert check_blocks(root, groups) == []


def test_a_stale_block_is_reported_with_its_page_and_group(tmp_path: Path) -> None:
    groups = read_groups(make_parser())
    root = checkout(tmp_path, {"a.md": marked("alpha", "old\n"), "b.md": current(groups, "beta")})
    assert check_blocks(root, groups) == [
        "docs/guides/a.md: the block for 'alpha' is out of date (run `coney-tools docs render`)"
    ]


def test_a_group_with_no_block_is_reported(tmp_path: Path) -> None:
    groups = read_groups(make_parser())
    root = checkout(tmp_path, {"a.md": current(groups, "alpha")})
    found = check_blocks(root, groups)
    assert len(found) == 1
    assert found[0].startswith("beta: no block of its parameters in docs/guides (add ")
    assert "<!-- command-reference:begin beta -->" in found[0]


def test_a_group_with_two_blocks_is_reported(tmp_path: Path) -> None:
    groups = read_groups(make_parser())
    root = checkout(
        tmp_path, {"a.md": current(groups, "alpha"), "b.md": current(groups, "beta") + current(groups, "alpha")}
    )
    assert check_blocks(root, groups) == ["alpha: two blocks, in docs/guides/a.md and docs/guides/b.md"]


def test_a_wrong_marker_is_a_problem_not_a_crash(tmp_path: Path) -> None:
    groups = read_groups(make_parser())
    root = checkout(tmp_path, {"a.md": marked("gamma")})
    assert any("unknown group 'gamma'" in line for line in check_blocks(root, groups))


def test_a_command_without_help_text_is_a_problem(tmp_path: Path) -> None:
    parser = argparse.ArgumentParser(prog="coney-tools")
    parser.add_subparsers(dest="group").add_parser("g", help="g").add_subparsers(dest="c").add_parser("x")
    groups = read_groups(parser)
    root = checkout(tmp_path, {"a.md": current(groups, "g")})
    assert check_blocks(root, groups) == ["g x: no help text"]


# --- the command ------------------------------------------------------------------------------------------------------


def test_render_rewrites_a_stale_page_and_check_changes_nothing(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    parser = make_parser()
    groups = read_groups(parser)
    root = checkout(tmp_path, {"a.md": marked("alpha", "old\n") + current(groups, "beta")})
    page = root / GUIDES_DIR / "a.md"
    before = page.read_bytes()
    assert run_render(root, read_groups(parser), check=True) == 1
    assert page.read_bytes() == before
    assert "docs/guides/a.md" in capsys.readouterr().err
    assert run_render(root, read_groups(parser), check=False) == 0
    assert page.read_bytes() != before
    assert check_blocks(root, groups) == []
    assert run_render(root, read_groups(parser), check=True) == 0


def test_render_still_reports_a_group_with_no_block_and_exits_1(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    parser = make_parser()
    root = checkout(tmp_path, {"a.md": current(read_groups(parser), "alpha")})
    assert run_render(root, read_groups(parser), check=False) == 1
    assert "beta: no block" in capsys.readouterr().err


def test_docs_render_is_a_command_of_the_command_line(capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["docs", "render", "--check"]) == 0, capsys.readouterr().err


# --- the real guides --------------------------------------------------------------------------------------------------


def test_the_real_guides_hold_a_current_block_for_every_group() -> None:
    assert check_blocks(REPO, command_groups()) == []


def test_every_group_is_listed_on_the_overview_page() -> None:
    overview = (REPO / "docs/guides/coney-tools.md").read_text(encoding="utf-8")
    missing = [name for name in command_groups() if f"`{name}" not in overview]
    assert missing == []


def test_the_launcher_is_a_group_beside_the_coney_tools_groups() -> None:
    groups = command_groups()
    assert "coney-agent" in groups and "repo" in groups
    assert groups["coney-agent"].entries[0].synopsis.startswith("coney-agent ")
