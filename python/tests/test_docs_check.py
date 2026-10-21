# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.docs_check: the page standards of docs/guides/writing-docs.md, on made-up pages.

Each rule is shown failing on the smallest page that breaks it and passing on the nearest page that keeps it, so a
change to a heuristic shows up as a change here. The fixtures hold no game data: the addresses are invented.
"""

from pathlib import Path

import pytest
import yaml
from conventions_support import REPO
from git_support import git

from coney_tools import check_cli, docs_check, docs_cli
from coney_tools.config import ConfigError
from coney_tools.docs_check import Context, Finding, check_page
from coney_tools.local_gate import StepResult

RESEARCH = "docs/research/widget.md"
GUIDE = "docs/guides/widget.md"
VERIFIED = "Verified against: `SLUS_212.15` (NTSC-U, SHA1 `0000`).\n"


def research_page(
    behaviour: str = "The widget spins (confirmed (code) at 0x00123456).\n",
    *,
    drop: tuple[str, ...] = (),
    head: str = VERIFIED,
    extra: str = "",
) -> str:
    """A research page with the six skeleton sections, leaving out the sections named in `drop`."""
    sections = {
        "Purpose": "What the widget does.\n",
        "Original structure": "The `Widget` class.\n",
        "Data": "Fields of the widget.\n",
        "Behaviour": behaviour,
        "Coney's implementation": "`src/widget/`.\n",
        "Open questions": "Nothing is known to be open.\n",
    }
    body = "".join(f"## {name}\n\n{text}\n" for name, text in sections.items() if name not in drop)
    return f"# Widget\n\n{head}\n{extra}{body}"


def rules(findings: list[Finding]) -> list[str]:
    """The rule of each finding, in order."""
    return [finding.rule for finding in findings]


def errors(path: str, text: str, context: Context | None = None) -> list[str]:
    """The rules `check_page` fails on (warnings left out)."""
    return [finding.rule for finding in check_page(path, text, context) if finding.severity == "error"]


# --- kinds --------------------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("path", "kind"),
    [
        ("docs/research/camera.md", "research"),
        ("docs/research/formats/wad-dir.md", "research"),
        ("docs/guides/testing.md", "guide"),
        ("docs/guides/review/docs.md", "guide"),
        ("docs/roadmap.md", "page"),
        ("docs/references/entities.md", "page"),
        ("docs/references/flags/level1.md", "generated"),
        ("docs/references/bindings/ai.md", "generated"),
        ("docs/missions/level2.md", "generated"),
        ("docs/progress/index.md", "generated"),
        ("README.md", "other"),
        ("src/core/readme.md", "other"),
        ("docs/index.html", "other"),
    ],
)
def test_kind_of_follows_the_page_kinds(path: str, kind: str) -> None:
    assert docs_check.kind_of(path) == kind


def test_generated_and_other_files_are_not_checked() -> None:
    text = "no title, C:\\Users\\sam\\x, not yet 0x00123456\n"
    assert check_page("docs/references/flags/level1.md", text) == []
    assert check_page("README.md", text) == []


# --- the skeleton ---------------------------------------------------------------------------------------------------


def test_a_complete_research_page_passes() -> None:
    assert check_page(RESEARCH, research_page()) == []


@pytest.mark.parametrize(
    "name", ["Purpose", "Original structure", "Data", "Behaviour", "Coney's implementation", "Open questions"]
)
def test_a_missing_section_is_reported_by_name(name: str) -> None:
    found = check_page(RESEARCH, research_page(drop=(name,)))
    assert rules(found) == ["skeleton"]
    assert name in found[0].message


def test_sections_out_of_order_are_reported() -> None:
    text = (
        research_page()
        .replace("## Data", "## Temp")
        .replace("## Behaviour", "## Data")
        .replace("## Temp", "## Behaviour")
    )
    found = check_page(RESEARCH, text)
    assert rules(found) == ["skeleton"]
    assert "order" in found[0].message


def test_other_sections_may_sit_between_the_skeleton_ones() -> None:
    text = research_page(extra="## Background\n\nWhere it came from.\n\n")
    assert check_page(RESEARCH, text) == []


def test_a_heading_that_starts_with_the_section_name_counts() -> None:
    text = research_page().replace("## Data", "## Data layout").replace("## Behaviour", "## Behaviour: the spin")
    assert check_page(RESEARCH, text) == []


def test_american_spelling_of_behaviour_counts() -> None:
    assert check_page(RESEARCH, research_page().replace("## Behaviour", "## Behavior")) == []


def test_a_heading_anchor_does_not_hide_a_section() -> None:
    assert check_page(RESEARCH, research_page().replace("## Data", "## Data {#data}")) == []


def test_an_empty_section_is_reported() -> None:
    found = check_page(RESEARCH, research_page().replace("Nothing is known to be open.\n", ""))
    assert rules(found) == ["skeleton"]
    assert "Open questions" in found[0].message and "one line" in found[0].message


def test_a_section_with_only_subsections_is_not_empty() -> None:
    text = research_page().replace("Fields of the widget.\n", "### The header\n\nFour bytes.\n")
    assert check_page(RESEARCH, text) == []


def test_a_heading_inside_a_code_block_is_not_a_section() -> None:
    text = research_page(drop=("Data",), extra="```\n## Data\n```\n\n")
    assert rules(check_page(RESEARCH, text)) == ["skeleton"]


def test_a_missing_title_is_reported() -> None:
    assert rules(check_page(RESEARCH, research_page().replace("# Widget\n", ""))) == ["skeleton"]


def test_the_verified_against_line_is_required() -> None:
    found = check_page(RESEARCH, research_page(head=""))
    assert rules(found) == ["verified"]


def test_the_verified_against_line_must_say_something() -> None:
    assert rules(check_page(RESEARCH, research_page(head="Verified against:\n"))) == ["verified"]


def test_a_verified_against_line_in_bold_or_lower_case_counts() -> None:
    assert check_page(RESEARCH, research_page(head="**Verified against:** the Xbox executable.\n")) == []
    assert check_page(RESEARCH, research_page(head="verified against `SLUS_212.15`.\n")) == []


def test_a_verified_against_line_below_the_first_section_does_not_count() -> None:
    text = research_page(head="") + "\nVerified against: `SLUS_212.15`.\n"
    assert rules(check_page(RESEARCH, text)) == ["verified"]


def test_guides_need_no_skeleton() -> None:
    assert check_page(GUIDE, "# How to\n\nSteps.\n") == []


def test_front_matter_is_not_a_page_title() -> None:
    assert check_page("docs/index.md", "---\ntemplate: home.html\n---\n\n# Coney\n\nA game.\n") == []


# --- evidence levels ------------------------------------------------------------------------------------------------


def test_an_address_with_no_level_is_reported() -> None:
    found = check_page(RESEARCH, research_page("The widget spins at 0x00123456.\n"))
    assert rules(found) == ["evidence"]
    assert "0x00123456" in found[0].message


def test_the_report_names_the_line_of_the_claim() -> None:
    text = research_page("The widget spins at 0x00123456.\n")
    found = check_page(RESEARCH, text)
    assert text.splitlines()[found[0].line - 1].startswith("The widget spins")


@pytest.mark.parametrize("level", ["confirmed (code)", "confirmed (runtime)", "inferred", "speculative", "Evidence: x"])
def test_each_level_in_the_same_paragraph_covers_the_claim(level: str) -> None:
    assert check_page(RESEARCH, research_page(f"The widget spins at 0x00123456 ({level}).\n")) == []


def test_the_level_in_the_paragraph_before_or_after_covers_the_claim() -> None:
    before = "All of this is confirmed (code).\n\nThe widget spins at 0x00123456.\n"
    after = "The widget spins at 0x00123456.\n\nThe above is inferred.\n"
    assert check_page(RESEARCH, research_page(before)) == []
    assert check_page(RESEARCH, research_page(after)) == []


def test_a_level_two_paragraphs_away_does_not_cover_the_claim() -> None:
    text = research_page("Intro.\n\nIt is confirmed (code).\n\nOther.\n\nThe widget spins at 0x00123456.\n\nMore.\n")
    assert rules(check_page(RESEARCH, text)) == ["evidence"]


def test_the_level_in_the_heading_or_in_the_line_under_it_covers_a_section() -> None:
    heading = "### The spin (confirmed (code))\n\nIt spins at 0x00123456.\n\nIt stops.\n\nIt stops at 0x00123460.\n"
    lead = (
        "### The spin\n\nEverything here is confirmed (code).\n\nSpins.\n\nIt spins at 0x00123456.\n\n"
        "It stops.\n\nAt 0x00123460.\n"
    )
    assert check_page(RESEARCH, research_page(heading)) == []
    assert check_page(RESEARCH, research_page(lead)) == []


def test_a_list_item_is_a_claim_of_its_own() -> None:
    items = (
        "- Spins at 0x00123456 (inferred).\n- Stops at 0x00123460.\n- Waits.\n- Waits more.\n- Leaves at 0x00123470.\n"
    )
    found = check_page(RESEARCH, research_page(items))
    assert [finding.message.split()[0] for finding in found] == ["0x00123470"]


def test_a_table_with_an_evidence_column_needs_a_level_in_each_row() -> None:
    table = (
        "| Address | Name | Evidence |\n| --- | --- | --- |\n"
        "| 0x00123456 | Spin | confirmed (code) |\n| 0x00123460 | Stop | |\n"
    )
    found = check_page(RESEARCH, research_page(table))
    assert rules(found) == ["evidence"]
    assert "0x00123460" in found[0].message


def test_a_table_without_an_evidence_column_is_covered_by_a_level_beside_it() -> None:
    table = "| Address | Name |\n| --- | --- |\n| 0x00123456 | Spin |\n| 0x00123460 | Stop |\n"
    assert rules(check_page(RESEARCH, research_page(table))) == ["evidence", "evidence"]
    assert check_page(RESEARCH, research_page(table + "\nAll confirmed (code).\n")) == []
    assert check_page(RESEARCH, research_page("Confirmed (code):\n\n" + table)) == []


def test_short_numbers_and_offsets_are_not_addresses() -> None:
    assert check_page(RESEARCH, research_page("The type is at `+0xd0`, mask 0xff, flag 0x1000.\n")) == []


def test_an_address_outside_the_behaviour_sections_needs_no_level() -> None:
    text = research_page().replace("What the widget does.\n", "It starts at 0x00123456.\n")
    text = text.replace("Nothing is known to be open.\n", "What is 0x00123456 for?\n")
    text = text.replace("`src/widget/`.\n", "`@orig 0x00123456 Widget_Spin`.\n")
    assert check_page(RESEARCH, text) == []


def test_an_address_in_a_code_block_needs_no_level() -> None:
    text = research_page("```text\n0x00123456 jal Widget_Spin\n```\n")
    assert check_page(RESEARCH, text) == []


def test_a_guide_needs_no_levels() -> None:
    assert check_page(GUIDE, "# Guide\n\nBreak at 0x00123456.\n") == []


# --- style ----------------------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    "path",
    [
        r"C:\Users\sam\Desktop\x",
        "C:/Repositories/Coney/scratch/x",
        "/home/sam/coney",
        "/Users/sam/coney",
        "D:/work/coney",
    ],
)
def test_a_machine_path_is_reported(path: str) -> None:
    assert errors(GUIDE, f"# Guide\n\nRun it in `{path}`.\n") == ["machine-path"]


@pytest.mark.parametrize(
    "text",
    [
        r"the disc at `H:\`",
        r"the path `D:\XBoxWad.idx`",
        "the original's `c:/Warriors/Source/Ai/Task.cpp`",
        r"debug path `d:\Warriors\Build\rundata`",
        "the placeholder `C:\\Users\\<name>\\` and `/home/<name>/`",
        r"install to `C:\Program Files\Tool`",
        "write `<workspace>/scratch/` or a relative path",
    ],
)
def test_other_paths_are_not_machine_paths(text: str) -> None:
    assert errors(GUIDE, f"# Guide\n\n{text}\n") == []


@pytest.mark.parametrize("phrase", ["is still small", "is not yet written", "once it exists", "Not yet known"])
def test_time_bound_wording_is_reported(phrase: str) -> None:
    assert errors(GUIDE, f"# Guide\n\nThe list {phrase}.\n") == ["stale-wording"]


def test_a_quoted_or_coded_phrase_is_a_mention_not_a_use() -> None:
    assert errors(GUIDE, '# Guide\n\nNo "not yet" and no `still small` and no "once it exists".\n') == []


def test_wording_in_a_code_block_is_not_checked() -> None:
    assert errors(GUIDE, "# Guide\n\n```text\nnot yet\n```\n") == []


def test_stale_wording_is_reported_on_research_pages_too() -> None:
    assert errors(RESEARCH, research_page(extra="The list is still small.\n\n")) == ["stale-wording"]


def test_not_yet_on_a_research_page_is_status_only_where_it_speaks_of_coney() -> None:
    # The original's own behaviour ("a store that is not yet broken") is not a dated claim about the project.
    assert errors(RESEARCH, research_page("The store is not yet broken (confirmed (code))." + "\n")) == []
    coney = research_page().replace("`src/widget/`.\n", "Not yet written.\n")
    assert errors(RESEARCH, coney) == ["stale-wording"]
    open_question = research_page().replace("Nothing is known to be open.\n", "Not yet known.\n")
    assert errors(RESEARCH, open_question) == ["stale-wording"]
    assert errors(RESEARCH, research_page(extra="Coney does not yet do this.\n\n")) == ["stale-wording"]


def test_a_relative_link_that_leaves_docs_is_reported() -> None:
    assert errors(GUIDE, "# G\n\nSee [the licence](../../LEGAL.md#x).\n") == ["repo-link"]
    assert errors("docs/references/entities.md", "# A\n\n[x](../../src/a.cpp)\n") == ["repo-link"]
    assert errors(GUIDE, "# G\n\n![a picture](../../a.png)\n") == ["repo-link"]
    assert errors(GUIDE, "# G\n\nSee [the licence][l].\n\n[l]: ../../LEGAL.md\n") == ["repo-link"]


@pytest.mark.parametrize(
    "link",
    [
        "[x](repo:LEGAL.md#x)",
        "[x](testing.md#done)",
        "[x](../research/camera.md)",
        "[x](#here)",
        "[x](https://example.org/a)",
        "[x](mailto:a@example.org)",
        "[x](../)",
        "![a picture](../images/a.png)",
        "[l]: ../research/camera.md",
    ],
)
def test_other_links_are_fine(link: str) -> None:
    assert errors(GUIDE, f"# G\n\n{link}\n") == []


def test_the_decider_wording_is_reported_in_guides_only() -> None:
    context = Context(avoid=("owner",))
    assert errors(GUIDE, "# G\n\nAsk the owner.\n", context) == ["person-wording"]
    assert errors(GUIDE, "# G\n\nThe `owner/repo` pair and `owner_name`.\n", context) == []
    assert errors(RESEARCH, research_page(extra="The owner of the body.\n\n"), context) == []


def test_the_site_or_repository_address_is_reported() -> None:
    context = Context(address_terms=("github.com/some/thing",))
    assert errors(GUIDE, "# G\n\nSee https://GitHub.com/Some/Thing/issues.\n", context) == ["address"]
    assert errors(GUIDE, "# G\n\nSee https://example.org.\n", context) == []


def test_the_address_rule_needs_the_terms() -> None:
    assert errors(GUIDE, "# G\n\nSee https://github.com/some/thing.\n") == []


# --- length ---------------------------------------------------------------------------------------------------------


def test_a_long_page_gets_a_warning_not_a_failure() -> None:
    long_guide = "# G\n\n" + "A line.\n\n" * docs_check.LENGTH_WARNING["guide"]
    found = check_page(GUIDE, long_guide)
    assert rules(found) == ["length"] and found[0].severity == "warning"
    assert errors(GUIDE, long_guide) == []


def test_research_pages_may_be_longer_than_guides() -> None:
    assert docs_check.LENGTH_WARNING["research"] > docs_check.LENGTH_WARNING["guide"]
    text = research_page(extra="x\n" * (docs_check.LENGTH_WARNING["research"] + 1))
    assert "length" in rules(check_page(RESEARCH, text))


def test_a_page_at_the_limit_has_no_warning() -> None:
    limit = docs_check.LENGTH_WARNING["guide"]
    text = "# G\n" + "\n" * (limit - 1)
    assert len(text.splitlines()) == limit
    assert check_page(GUIDE, text) == []


# --- a whole change ---------------------------------------------------------------------------------------------------


def write(root: Path, name: str, text: str) -> None:
    """Write a file of the made-up checkout."""
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def checkout(tmp_path: Path) -> Path:
    """A repository whose `main` holds one bad research page and one good one."""
    git(tmp_path, "init", "-q", "-b", "main")
    write(tmp_path, "docs/research/old.md", "# Old\n\nNo sections, still small.\n")
    write(tmp_path, "docs/research/good.md", research_page())
    git(tmp_path, "add", "-A")
    git(tmp_path, "commit", "-q", "-m", "base")
    git(tmp_path, "checkout", "-q", "-b", "work")
    return tmp_path


def test_a_change_is_checked_only_on_the_pages_it_touches(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    write(root, "docs/research/good.md", research_page(extra="Added.\n\n"))
    write(root, "docs/guides/new.md", "# New\n\nHello.\n")
    result = docs_check.check_changed(root, "main", Context())
    assert sorted(result.pages) == ["docs/guides/new.md", "docs/research/good.md"]
    assert result.failed == {}


def test_a_touched_page_is_held_to_the_whole_standard(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    write(root, "docs/research/old.md", "# Old\n\nNo sections, still small.\n\nA new line.\n")
    result = docs_check.check_changed(root, "main", Context())
    assert list(result.failed) == ["docs/research/old.md"]
    assert {finding.rule for finding in result.failed["docs/research/old.md"]} >= {
        "skeleton",
        "verified",
        "stale-wording",
    }


def test_files_that_are_not_pages_are_left_alone(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    write(root, "README.md", "no title not yet\n")
    write(root, "docs/references/flags/x.md", "not yet\n")
    write(root, "src/a.cpp", "int x;\n")
    result = docs_check.check_changed(root, "main", Context())
    assert result.pages == []


def test_a_deleted_page_is_not_checked(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    (root / "docs/research/old.md").unlink()
    assert docs_check.check_changed(root, "main", Context()).pages == []


def test_paths_named_on_the_command_line_are_checked_whatever_changed(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    result = docs_check.check_paths(root, ["docs/research/old.md", "docs/research/good.md"], Context())
    assert list(result.failed) == ["docs/research/old.md"]
    assert result.pages == ["docs/research/old.md", "docs/research/good.md"]


def test_a_folder_on_the_command_line_means_its_pages(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    result = docs_check.check_paths(root, ["docs/research"], Context())
    assert sorted(result.pages) == ["docs/research/good.md", "docs/research/old.md"]


def test_the_summary_counts_pages_per_kind(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    result = docs_check.check_paths(root, ["docs"], Context())
    assert result.by_kind() == {"research": (1, 2)}


# --- the command and the gate ----------------------------------------------------------------------------------------


def checkout_with_conventions(tmp_path: Path) -> Path:
    """`checkout` plus what the commands read to know they are in a Coney checkout: the example config, conventions."""
    root = checkout(tmp_path)
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    (root / ".github").mkdir()
    (root / ".github/conventions.yml").write_bytes((REPO / ".github/conventions.yml").read_bytes())
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "setup")
    git(root, "checkout", "-q", "-B", "work")
    return root


def test_the_command_fails_on_a_touched_page_and_passes_a_good_one(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    root = checkout_with_conventions(tmp_path)
    monkeypatch.chdir(root)
    assert docs_cli.run_check([], "main", False, False) == 0
    write(root, "docs/research/old.md", "# Old\n\nSome line.\n")
    assert docs_cli.run_check([], "main", False, False) == 1
    shown = capsys.readouterr().out
    assert "docs/research/old.md:1: skeleton:" in shown and "1 fail" in shown


def test_the_command_checks_named_pages_whatever_changed(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    root = checkout_with_conventions(tmp_path)
    monkeypatch.chdir(root)
    assert docs_cli.run_check(["docs/research"], "main", True, True) == 1
    shown = capsys.readouterr().out
    assert "research: 1 of 2 pages pass" in shown and "docs/research/old.md" not in shown


def test_the_command_reports_an_unusable_checkout(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(tmp_path)
    assert docs_cli.run_check([], "main", False, False) == 2
    assert "not inside a Coney checkout" in capsys.readouterr().out


def test_the_docs_step_of_check_changed_runs_the_standards_on_the_changed_pages(tmp_path: Path) -> None:
    root = checkout_with_conventions(tmp_path)
    write(root, "docs/research/old.md", "# Old\n\nSome line.\n")
    checked, problems, notes = check_cli._standards(root, ["docs/research/old.md", "docs/research/good.md", "a.cpp"])
    assert checked == 2 and notes == []
    assert any(line.startswith("docs/research/old.md:1: skeleton") for line in problems)
    assert not any("good.md" in line for line in problems)


def test_a_long_page_that_otherwise_passes_is_only_a_note_in_the_gate(tmp_path: Path) -> None:
    root = checkout_with_conventions(tmp_path)
    write(root, "docs/guides/long.md", "# Long\n\n" + "A line.\n\n" * docs_check.LENGTH_WARNING["guide"])
    checked, problems, notes = check_cli._standards(root, ["docs/guides/long.md"])
    assert (checked, problems) == (1, []) and "warning: length" in notes[0]


def test_the_ci_docs_job_runs_the_check_on_the_pages_a_pull_request_changes() -> None:
    workflow = yaml.safe_load((REPO / ".github/workflows/docs.yml").read_text(encoding="utf-8"))
    steps = workflow["jobs"]["mkdocs"]["steps"]
    check = next(step for step in steps if "coney-tools docs check --base HEAD^1" in step.get("run", ""))
    build = next(step for step in steps if step.get("run", "").startswith("mkdocs build --strict"))
    # Pull requests only (on a push HEAD^1 is the previous commit of main), and it does not stop the build ...
    assert check["if"] == "github.event_name == 'pull_request'" and check["continue-on-error"] is True
    assert steps.index(check) < steps.index(build)
    # ... and the job still fails when it did, after the build ran.
    last = steps[-1]
    assert last["if"] == f"steps.{check['id']}.outcome == 'failure'" and "exit 1" in last["run"]


def test_the_docs_step_runs_the_standards_and_the_build_and_fails_on_either(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    root = checkout_with_conventions(tmp_path)
    write(root, "docs/research/old.md", "# Old\n\nSome line.\n")
    built = StepResult("docs", True, "mkdocs build --strict")
    monkeypatch.setattr(check_cli, "_mkdocs_build", lambda root: built)
    failed = check_cli._step_docs(root, ["docs/research/old.md"])
    assert not failed.ok and "page standards failed" in failed.detail and "mkdocs build --strict" in failed.detail
    assert check_cli._step_docs(root, ["docs/research/good.md"]).ok
    built = StepResult("docs", False, "mkdocs build --strict failed", output=("ERROR x",))
    broken = check_cli._step_docs(root, ["docs/research/good.md"])
    assert not broken.ok and "ERROR x" in broken.output


def test_a_named_path_that_is_missing_or_not_a_page_is_an_error(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    (root / "README.md").write_text("# R\n", encoding="utf-8")
    for path, message in [("docs/guides/typo.md", "no such file"), ("README.md", "not a page the check covers")]:
        with pytest.raises(ConfigError, match=message):
            docs_check.check_paths(root, [path], Context())


def test_the_real_page_standards_page_passes_its_own_check() -> None:
    name = "docs/guides/writing-docs.md"
    assert errors(name, (REPO / name).read_text(encoding="utf-8"), docs_check.context_for(REPO)) == []


def test_a_level_column_must_be_headed_evidence_or_confidence() -> None:
    game_levels = "| Level | Address |\n| --- | --- |\n| level80 | 0x00123456 |\n"
    graded = "| Address | Evidence level |\n| --- | --- |\n| 0x00123456 | |\n"
    # "Level" alone is a game level, so the table is covered like a paragraph: by the line after it.
    assert check_page(RESEARCH, research_page(game_levels + "\nAll inferred.\n")) == []
    assert rules(check_page(RESEARCH, research_page(graded + "\nAll inferred.\n"))) == ["evidence"]


def test_a_person_is_not_an_objects_owner() -> None:
    context = Context(avoid=("owner",))
    assert errors(GUIDE, "# G\n\nThe object's owner pointer, and its owner.\n", context) == []
    assert errors(GUIDE, "# G\n\nThe project owner decides.\n", context) == ["person-wording"]


def test_a_heading_that_starts_with_an_ungraded_section_name_is_ungraded() -> None:
    page = (
        research_page()
        .replace("## Purpose", "## Purpose and scope")
        .replace("What the widget does.", "What the widget does, from 0x00123456.")
    )
    assert errors(RESEARCH, page) == []
    coney = research_page().replace("## Coney's implementation", "## Coney's implementation (so far)")
    coney = coney.replace("`src/widget/`.", "`@orig 0x00123456 Spin` in `src/widget/`. Not yet complete.")
    # The @orig tag needs no level, and "not yet" in this section still dates the project.
    assert errors(RESEARCH, coney) == ["stale-wording"]


def test_not_yet_on_a_guide_counts_only_when_it_says_what_the_project_has_not_done() -> None:
    assert errors(GUIDE, "# G\n\nStatus: not yet looked at.\n") == []
    assert errors(GUIDE, "# G\n\nThe game draws scenery not yet loaded as boxes.\n") == []
    assert errors(GUIDE, "# G\n\nThe 16:9 overwrites are not yet listed.\n") == ["stale-wording"]
    assert errors(GUIDE, "# G\n\nThis has not yet been implemented.\n") == ["stale-wording"]


def test_only_addresses_in_main_memory_are_claims() -> None:
    for number in ("0x7e23a6f2", "0x18300000", "0x80000000", "0x00001000"):
        assert errors(RESEARCH, research_page(f"The value {number}.\n")) == [], number
    assert errors(RESEARCH, research_page("The function at 0x01f00000.\n")) == ["evidence"]


def test_a_page_level_sentence_may_name_the_executable_but_not_deny_the_grade() -> None:
    graded = "Every address in SLUS_212.15 is confirmed (code) unless a line says otherwise.\n"
    assert errors(RESEARCH, research_page("The widget at 0x00123456.\n", extra=graded + "\n")) == []
    denied = "Not all of this is confirmed.\n"
    assert errors(RESEARCH, research_page("The widget at 0x00123456.\n", extra=denied + "\n")) == ["evidence"]


def test_a_longer_fence_can_show_a_shorter_one() -> None:
    page = "# G\n\n````markdown\n```text\nnot yet written\n```\nnot yet written\n````\n\nAll done.\n"
    assert errors(GUIDE, page) == []
    assert errors(GUIDE, page + "\nIt is not yet written.\n") == ["stale-wording"]


def test_the_owner_of_a_thing_is_not_a_person() -> None:
    context = Context(avoid=("owner",))
    assert errors(GUIDE, "# G\n\nThe owner of the task runs it.\n", context) == []
