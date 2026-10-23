# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.command_params: reading an argparse tree into command entries and rendering them.

Synthetic parsers only, except the last test, which reads the real command line for what every entry must have.
"""

import argparse
import os
from collections.abc import Callable
from pathlib import Path

import pytest

from coney_tools import cli
from coney_tools.command_params import WIDTH, Entry, Group, problems, read_groups, read_program, render_group, synopsis


def make_parser() -> argparse.ArgumentParser:
    """A small command line with two groups, a nested group, an alias and a suppressed command."""
    parser = argparse.ArgumentParser(prog="coney-tools")
    parser.add_argument("--version", action="version", version="x")
    groups = parser.add_subparsers(dest="group", required=True)
    alpha = groups.add_parser("alpha", help="the alpha group")
    alpha_commands = alpha.add_subparsers(dest="command", required=True)
    check = alpha_commands.add_parser("check", help="check the thing")
    check.add_argument("paths", nargs="*", help="pages to check")
    check.add_argument("--base", default="origin/main", help="the branch to compare with")
    check.add_argument("--quiet", action="store_true", help="print only counts")
    show = alpha_commands.add_parser("show", help="show it", aliases=["list"])
    show.add_argument("name", help="which one")
    alpha_commands.add_parser("hidden", help=argparse.SUPPRESS)
    beta = groups.add_parser("beta", help="the beta group")
    nested = beta.add_subparsers(dest="command", required=True).add_parser("nest", help="a nested group")
    nested.add_subparsers(dest="direction", required=True).add_parser("go", help="go there")
    return parser


def entry_of(parser: argparse.ArgumentParser, group: str, command: str) -> Entry:
    """The entry for `group command` of `parser`."""
    found = {entry.path: entry for entry in read_groups(parser)[group].entries}
    return found[(group, command)]


def one_command(configure: Callable[[argparse.ArgumentParser], object]) -> Entry:
    """The entry of a one-command parser that `configure` adds arguments to."""
    parser = argparse.ArgumentParser(prog="coney-tools")
    command = (
        parser.add_subparsers(dest="group")
        .add_parser("g", help="g")
        .add_subparsers(dest="c")
        .add_parser("do", help="do it")
    )
    configure(command)
    return read_groups(parser)["g"].entries[0]


# --- walking the parser ---------------------------------------------------------------------------------------------


def test_every_group_and_its_commands_are_found_in_definition_order() -> None:
    found = read_groups(make_parser())
    assert list(found) == ["alpha", "beta"]
    assert [entry.path for entry in found["alpha"].entries] == [("alpha", "check"), ("alpha", "show")]


def test_an_alias_is_one_command_and_a_suppressed_command_is_left_out() -> None:
    paths = [entry.path for entry in read_groups(make_parser())["alpha"].entries]
    assert ("alpha", "list") not in paths
    assert ("alpha", "hidden") not in paths


def test_a_group_that_only_dispatches_has_entries_for_its_commands_not_for_itself() -> None:
    paths = [entry.path for entry in read_groups(make_parser())["beta"].entries]
    assert paths == [("beta", "nest", "go")]


def test_a_dispatching_node_with_options_of_its_own_gets_an_entry() -> None:
    parser = argparse.ArgumentParser(prog="coney-tools")
    group = parser.add_subparsers(dest="group").add_parser("g", help="group g")
    group.add_argument("--all", action="store_true", help="every one")
    group.add_subparsers(dest="c").add_parser("sub", help="a sub command")
    assert [entry.path for entry in read_groups(parser)["g"].entries] == [("g",), ("g", "sub")]


def test_group_help_is_the_text_the_group_was_added_with() -> None:
    found = read_groups(make_parser())
    assert (found["alpha"].help, found["beta"].help) == ("the alpha group", "the beta group")


def test_the_command_help_and_a_command_without_help_are_read() -> None:
    parser = make_parser()
    assert entry_of(parser, "alpha", "check").help == "check the thing"
    bare = argparse.ArgumentParser(prog="coney-tools")
    bare.add_subparsers(dest="group").add_parser("g", help="g").add_subparsers(dest="c").add_parser("x")
    assert read_groups(bare)["g"].entries[0].help == ""


# --- one parameter ----------------------------------------------------------------------------------------------------


def test_a_required_positional_shows_required_and_a_star_positional_none() -> None:
    entry = entry_of(make_parser(), "alpha", "check")
    paths = entry.params[0]
    assert (paths.signature, paths.default, paths.meaning) == ("`[PATHS ...]`", "none", "pages to check")
    name = entry_of(make_parser(), "alpha", "show").params[0]
    assert (name.signature, name.default) == ("`NAME`", "required")


def test_an_option_shows_its_default_and_a_flag_shows_off() -> None:
    entry = entry_of(make_parser(), "alpha", "check")
    base, quiet = entry.params[1], entry.params[2]
    assert (base.signature, base.type, base.default) == ("`--base BASE`", "text", "`origin/main`")
    assert (quiet.signature, quiet.type, quiet.default) == ("`--quiet`", "flag", "off")


def test_an_option_without_a_default_shows_none_and_a_required_one_shows_required() -> None:
    entry = one_command(
        lambda c: (c.add_argument("--out", help="where"), c.add_argument("--id", required=True, help="i"))
    )
    assert [param.default for param in entry.params] == ["none", "required"]


def test_a_metavar_names_the_value_and_choices_are_listed_in_the_meaning() -> None:
    entry = one_command(
        lambda c: (
            c.add_argument("--file", metavar="FILE", help="a file"),
            c.add_argument("--mode", choices=["a", "b"], help="a mode"),
        )
    )
    assert [param.signature for param in entry.params] == ["`--file FILE`", "`--mode MODE`"]
    assert entry.params[1].type == "choice"
    assert entry.params[1].meaning == "a mode. One of `a`, `b`."


def test_the_type_is_named_for_a_reader() -> None:
    entry = one_command(
        lambda c: (
            c.add_argument("--n", type=int, default=3, help="a count"),
            c.add_argument("--x", type=float, help="a ratio"),
            c.add_argument("--p", type=Path, help="a path"),
        )
    )
    assert [param.type for param in entry.params] == ["integer", "number", "path"]
    assert entry.params[0].default == "`3`"


def test_a_repeatable_option_says_so_and_an_empty_list_default_is_none() -> None:
    entry = one_command(lambda c: c.add_argument("--label", action="append", default=[], help="a label"))
    param = entry.params[0]
    assert param.default == "none"
    assert param.meaning == "a label. Repeatable."


def test_a_help_text_that_already_says_repeatable_is_not_told_so_again() -> None:
    entry = one_command(lambda c: c.add_argument("--only", action="append", help="run only this step (repeatable)"))
    assert entry.params[0].meaning == "run only this step (repeatable)"


def test_a_list_default_is_shown_as_its_values() -> None:
    entry = one_command(lambda c: c.add_argument("--n", type=int, nargs="+", default=[1, 2, 3], help="counts"))
    assert entry.params[0].default == "`1, 2, 3`"
    assert entry.params[0].signature == "`--n N [N ...]`"


def test_an_option_with_a_short_form_lists_both() -> None:
    entry = one_command(lambda c: c.add_argument("-o", "--out", help="the output"))
    assert entry.params[0].signature == "`-o, --out OUT`"


def test_a_store_false_flag_shows_on() -> None:
    entry = one_command(lambda c: c.add_argument("--no-history", dest="history", action="store_false", help="skip it"))
    assert entry.params[0].default == "on"


def test_help_expands_the_default_and_escapes_markdown() -> None:
    entry = one_command(lambda c: c.add_argument("--limit", type=int, default=7, help="at most %(default)s | <N> rows"))
    assert entry.params[0].meaning == r"at most 7 \| \<N\> rows"


def test_a_web_address_in_help_becomes_a_code_span() -> None:
    entry = one_command(lambda c: c.add_argument("--url", help="the server (default http://127.0.0.1:8090)"))
    assert entry.params[0].meaning == "the server (default `http://127.0.0.1:8090`)"


def test_a_suppressed_argument_is_left_out() -> None:
    entry = one_command(
        lambda c: (c.add_argument("--hidden", help=argparse.SUPPRESS), c.add_argument("--shown", help="seen"))
    )
    assert [param.signature for param in entry.params] == ["`--shown SHOWN`"]


def test_mutually_exclusive_options_name_each_other() -> None:
    def configure(command: argparse.ArgumentParser) -> None:
        group = command.add_mutually_exclusive_group()
        group.add_argument("--init", action="store_true", help="write it")
        group.add_argument("--prune", action="store_true", help="trim it")

    entry = one_command(configure)
    assert entry.params[0].meaning == "write it. Not with `--prune`."
    assert entry.params[1].meaning == "trim it. Not with `--init`."


def test_a_required_exclusive_group_is_said_once_in_the_notes() -> None:
    def configure(command: argparse.ArgumentParser) -> None:
        group = command.add_mutually_exclusive_group(required=True)
        group.add_argument("--event", help="one event")
        group.add_argument("--sweep", action="store_true", help="all of them")

    assert one_command(configure).notes == ["Exactly one of `--event`, `--sweep` is required."]


# --- the synopsis -----------------------------------------------------------------------------------------------------


def test_the_synopsis_marks_optional_and_required_parts() -> None:
    entry = one_command(
        lambda c: (
            c.add_argument("name", help="n"),
            c.add_argument("--flag", action="store_true", help="f"),
            c.add_argument("--must", required=True, help="m"),
            c.add_argument("--value", help="v"),
        )
    )
    assert entry.synopsis == "coney-tools g do NAME [--flag] --must MUST [--value VALUE]"


def test_a_long_synopsis_is_wrapped_to_the_docs_width_with_an_indented_continuation() -> None:
    entry = one_command(lambda c: [c.add_argument(f"--option-number-{n}", help="x") for n in range(12)])
    lines = entry.synopsis.splitlines()
    assert len(lines) > 1
    assert all(len(line) <= WIDTH for line in lines)
    assert all(line.startswith("    ") for line in lines[1:])


def test_synopsis_needs_no_terminal_width(monkeypatch: pytest.MonkeyPatch) -> None:
    parser = make_parser()
    monkeypatch.setenv("COLUMNS", "20")
    narrow = [entry.synopsis for entry in read_groups(parser)["alpha"].entries]
    monkeypatch.setenv("COLUMNS", "300")
    assert narrow == [entry.synopsis for entry in read_groups(parser)["alpha"].entries]
    assert os.environ["COLUMNS"] == "300"


def test_exclusive_options_share_brackets_in_the_synopsis() -> None:
    def configure(command: argparse.ArgumentParser) -> None:
        optional = command.add_mutually_exclusive_group()
        optional.add_argument("--init", action="store_true", help="write it")
        optional.add_argument("--prune", action="store_true", help="trim it")
        needed = command.add_mutually_exclusive_group(required=True)
        needed.add_argument("--event", help="one event")
        needed.add_argument("--sweep", action="store_true", help="all of them")

    assert one_command(configure).synopsis == "coney-tools g do [--init | --prune] (--event EVENT | --sweep)"


def test_synopsis_function_joins_a_prog_and_the_parts() -> None:
    assert synopsis("coney-tools a b", ["X", "[--y]"]) == "coney-tools a b X [--y]"


# --- rendering --------------------------------------------------------------------------------------------------------


def test_a_group_renders_a_heading_and_a_table_per_command() -> None:
    parser = make_parser()
    lines = render_group(read_groups(parser)["alpha"])
    text = "\n".join(lines)
    assert "## Parameters of `alpha` {#alpha-parameters}" in text
    assert "### `alpha check` {#alpha-check-usage}" in text
    assert "| Parameter | Type | Default | Meaning |" in text
    assert "| `--base BASE` | text | `origin/main` | the branch to compare with |" in text
    assert "```text\nconey-tools alpha check [PATHS ...] [--base BASE] [--quiet]\n```" in text


def test_the_anchor_of_a_nested_command_joins_every_word() -> None:
    parser = make_parser()
    text = "\n".join(render_group(read_groups(parser)["beta"]))
    assert "### `beta nest go` {#beta-nest-go-usage}" in text


def test_a_command_without_parameters_says_so() -> None:
    parser = make_parser()
    text = "\n".join(render_group(read_groups(parser)["beta"]))
    assert "Takes no parameters." in text


def test_rendering_has_no_trailing_blank_line_and_is_stable() -> None:
    parser = make_parser()
    first = render_group(read_groups(parser)["alpha"])
    assert first[-1] != ""
    assert first == render_group(read_groups(parser)["alpha"])


def test_a_table_cell_never_holds_a_raw_pipe_or_line_break() -> None:
    entry = one_command(lambda c: c.add_argument("--x", help="a | b\nc"))
    lines = render_group(Group("g", "h", [entry]))
    row = next(line for line in lines if line.startswith("| `--x"))
    assert row.replace(r"\|", "").count("|") == 5
    assert "\n" not in row


# --- what a complete reference needs ---------------------------------------------------------------------------------


def test_a_command_or_parameter_without_help_is_a_problem() -> None:
    parser = argparse.ArgumentParser(prog="coney-tools")
    command = parser.add_subparsers(dest="group").add_parser("g", help="g").add_subparsers(dest="c").add_parser("x")
    command.add_argument("--bare")
    assert problems(read_groups(parser)) == ["g x: no help text", "g x --bare: no help text"]


def test_the_real_command_line_documents_everything() -> None:
    found = read_groups(cli._build_parser())
    assert problems(found) == []
    assert {"repo", "docs", "build", "pcsx2"} <= set(found)
    assert all(group.entries for group in found.values())


# --- a leaf group and a program of its own ----------------------------------------------------------------------------


def test_a_group_that_is_one_command_of_its_name_has_a_single_heading() -> None:
    parser = argparse.ArgumentParser(prog="coney-tools")
    parser.add_subparsers(dest="group").add_parser("doctor", help="check the machine").add_argument(
        "--fix", action="store_true", help="repair what it can"
    )
    text = "\n".join(render_group(read_groups(parser)["doctor"]))
    assert sum(line.startswith("#") for line in text.splitlines()) == 1
    assert "## Parameters of `doctor` {#doctor-parameters}" in text
    assert "coney-tools doctor [--fix]" in text


def test_a_program_of_its_own_is_a_group_named_after_it() -> None:
    parser = argparse.ArgumentParser(prog="coney-agent", description="Start a   session.")
    parser.add_argument("role", nargs="?", default="engineer", help="the role")
    group = read_program(parser)
    assert (group.name, group.help) == ("coney-agent", "Start a session.")
    assert group.entries[0].synopsis == "coney-agent [ROLE]"
    assert group.entries[0].params[0].default == "`engineer`"
