# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.docs_check: the page standards of docs/guides/writing-docs.md, on made-up pages.

Each rule is shown failing on the smallest page that breaks it and passing on the nearest page that keeps it, so a
change to a heuristic shows up as a change here. The fixtures hold no game data: the addresses are invented.
"""

from pathlib import Path

import pytest
import yaml
from conventions_support import NEWER_KEY, REMOVE, REPO, changed, write_older_checkout
from git_support import git
from research_support import page as survey_page

from coney_tools import check_cli, docs_check, docs_cli, docs_legacy
from coney_tools.config import ConfigError
from coney_tools.conventions import ConventionsError
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


def test_guides_need_no_skeleton() -> None:
    assert check_page(GUIDE, "# How to\n\nSteps.\n") == []


def test_front_matter_is_not_a_page_title() -> None:
    assert check_page("docs/index.md", "---\ntemplate: home.html\n---\n\n# Coney\n\nA game.\n") == []


# --- evidence levels ------------------------------------------------------------------------------------------------


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
        "[#123](issue:123)",
        "[l]: issue:123",
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
    write(tmp_path, "docs/research/good.md", survey_page(kind="survey"))
    git(tmp_path, "add", "-A")
    git(tmp_path, "commit", "-q", "-m", "base")
    git(tmp_path, "checkout", "-q", "-b", "work")
    return tmp_path


def test_a_change_is_checked_only_on_the_pages_it_touches(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    write(root, "docs/research/good.md", survey_page(kind="survey", extra="Added.\n\n"))
    write(root, "docs/guides/new.md", "# New\n\nHello.\n")
    result = docs_check.check_changed(root, "main", Context())
    assert sorted(result.pages) == ["docs/guides/new.md", "docs/research/good.md"]
    assert result.failed == {}


def test_a_touched_page_is_held_to_the_whole_standard(tmp_path: Path) -> None:
    root = checkout_with_conventions(tmp_path)
    write(root, "docs/research/old.md", "# Old\n\nNo sections, still small.\n\nA new line.\n")
    result = docs_check.check_changed(root, "main", docs_check.context_for(root))
    assert list(result.failed) == ["docs/research/old.md"]
    assert {finding.rule for finding in result.failed["docs/research/old.md"]} >= {
        "front-matter",
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
    assert "docs/research/old.md:1: front-matter:" in shown and "docs/research/old.md:1: skeleton:" in shown
    assert "1 fail" in shown


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
    checked, problems, notes = check_cli._standards(root, "main")
    assert checked == 1 and notes == []
    assert any(line.startswith("docs/research/old.md:1: skeleton") for line in problems)
    assert not any("good.md" in line for line in problems)


def test_the_gate_holds_a_touched_legacy_page_to_the_mechanical_rules_as_the_ci_check_does(tmp_path: Path) -> None:
    root = checkout_with_conventions(tmp_path)
    write(root, "research/legacy.toml", docs_legacy.initial_text(root, docs_check.context_for(root)))
    assert check_cli._standards(root, "main")[1] == []  # listed and not touched: excused
    write(root, "docs/research/old.md", "# Old\n\nNo sections, still small.\n\nA new line.\n")
    _, problems, _ = check_cli._standards(root, "main")
    assert any(line.startswith("docs/research/old.md:1: front-matter") for line in problems)
    assert any("stale-wording" in line for line in problems)


def test_the_gate_checks_the_legacy_file_when_a_research_page_changes(tmp_path: Path) -> None:
    root = checkout_with_conventions(tmp_path)
    write(root, "research/legacy.toml", '["docs/research/good.md"]\nhistory = 1\n')
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "a stale entry")
    git(root, "checkout", "-q", "-B", "main")
    git(root, "checkout", "-q", "-b", "work2")
    write(root, "docs/research/old.md", "# Old\n\nNo sections, still small.\n\nA new line.\n")
    _, problems, _ = check_cli._standards(root, "main")
    assert any("good.md now passes 'history'" in line for line in problems)


# --- the touch-it rule's reach: the sections a change touched ---------------------------------------------------------

#: A guide of two sections, each with a phrase the stale-wording rule refuses.
TWO_SECTIONS = "# Widget\n\n## One\n\nThe list does not exist yet.\n\n## Two\n\nThe table does not exist yet.\n"


def guide_on_base(tmp_path: Path, text: str = TWO_SECTIONS) -> Path:
    """A checkout whose branch `base` holds `GUIDE` with `text`, and a branch `edit` left from it."""
    root = checkout_with_conventions(tmp_path)
    write(root, GUIDE, text)
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "the guide")
    git(root, "checkout", "-q", "-b", "base")
    git(root, "checkout", "-q", "-b", "edit")
    return root


def test_a_changed_section_of_an_old_page_is_held_and_the_rest_is_only_listed(tmp_path: Path) -> None:
    root = guide_on_base(tmp_path)
    write(root, GUIDE, TWO_SECTIONS + "\nA line added to section Two.\n")
    result = docs_check.check_changed(root, "base", docs_check.context_for(root))
    found = [(item.line, item.rule, item.severity) for item in result.findings[GUIDE]]
    # Section Two (changed) owes the standard; section One (unchanged) is listed as information, not a failure.
    assert found == [(5, "stale-wording", "info"), (9, "stale-wording", "error")]
    assert list(result.failed) == [GUIDE]


def test_a_subsection_of_a_long_page_is_a_section_of_its_own(tmp_path: Path) -> None:
    # A research page's `##` sections run to hundreds of lines: `standards.touch.section_level` makes `###` a section.
    filler = "A line of text.\n\n" * 200
    text = f"# Widget\n\n## Behaviour\n\n### Spin\n\nIt does not exist yet.\n\n{filler}### Stop\n\n{filler}"
    root = guide_on_base(tmp_path, text)
    write(root, GUIDE, text + "The stop does not exist yet.\n")
    result = docs_check.check_changed(root, "base", docs_check.context_for(root))
    errors = [item.line for item in result.findings[GUIDE] if item.severity == "error"]
    assert errors == [len(text.splitlines()) + 1]
    assert [item.line for item in result.findings[GUIDE] if item.severity == "info"] == [7]


def test_a_change_that_fixes_its_own_section_passes_whatever_the_rest_of_the_page_holds(tmp_path: Path) -> None:
    root = guide_on_base(tmp_path)
    write(root, GUIDE, TWO_SECTIONS.replace("The table does not exist yet.", "The table is in `table.md`."))
    result = docs_check.check_changed(root, "base", docs_check.context_for(root))
    assert result.failed == {}
    assert [(item.line, item.severity) for item in result.findings[GUIDE]] == [(5, "info")]


def test_a_new_page_is_held_whole(tmp_path: Path) -> None:
    root = guide_on_base(tmp_path)
    write(root, "docs/guides/fresh.md", TWO_SECTIONS)
    result = docs_check.check_changed(root, "base", docs_check.context_for(root))
    assert [item.severity for item in result.failed["docs/guides/fresh.md"]] == ["error", "error"]


def test_a_page_named_on_the_command_line_is_held_whole(tmp_path: Path) -> None:
    root = guide_on_base(tmp_path)
    result = docs_check.check_paths(root, [GUIDE], docs_check.context_for(root))
    assert [item.severity for item in result.failed[GUIDE]] == ["error", "error"]


def test_the_command_lists_what_lies_outside_the_change_in_one_line_or_each_with_cleanup(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    root = guide_on_base(tmp_path)
    monkeypatch.chdir(root)
    write(root, GUIDE, TWO_SECTIONS.replace("The table does not exist yet.", "The table is in `table.md`."))
    assert docs_cli.run_check([], "base", False, False) == 0
    shown = capsys.readouterr().out
    assert f"{GUIDE}:1: info: touch-it: 1 finding outside what the change touched (stale-wording 1)" in shown
    assert docs_cli.run_check([], "base", False, False, cleanup=True) == 0
    assert f"{GUIDE}:5: info: stale-wording:" in capsys.readouterr().out


def test_the_gate_reports_what_lies_outside_the_change_as_a_note(tmp_path: Path) -> None:
    root = guide_on_base(tmp_path)
    write(root, GUIDE, TWO_SECTIONS.replace("The table does not exist yet.", "The table is in `table.md`."))
    _, problems, notes = check_cli._standards(root, "base")
    assert problems == [] and any("info: touch-it" in line for line in notes)


def test_a_long_page_that_otherwise_passes_is_only_a_note_in_the_gate(tmp_path: Path) -> None:
    root = checkout_with_conventions(tmp_path)
    write(root, "docs/guides/long.md", "# Long\n\n" + "A line.\n\n" * docs_check.LENGTH_WARNING["guide"])
    checked, problems, notes = check_cli._standards(root, "main")
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
    failed = check_cli._step_docs(root, "main", ["docs/research/old.md"])
    assert not failed.ok and "page standards failed" in failed.detail and "mkdocs build --strict" in failed.detail
    git(root, "checkout", "--", "docs/research/old.md")  # the change is now the good page's alone
    assert check_cli._step_docs(root, "main", ["docs/research/good.md"]).ok
    built = StepResult("docs", False, "mkdocs build --strict failed", output=("ERROR x",))
    broken = check_cli._step_docs(root, "main", ["docs/research/good.md"])
    assert not broken.ok and "ERROR x" in broken.output


def test_a_named_path_that_is_missing_or_not_a_page_is_an_error(tmp_path: Path) -> None:
    root = checkout(tmp_path)
    (root / "README.md").write_text("# R\n", encoding="utf-8")
    for path, message in [("docs/guides/typo.md", "no such file"), ("README.md", "not a page the check covers")]:
        with pytest.raises(ConfigError, match=message):
            docs_check.check_paths(root, [path], Context())


@pytest.mark.reads("docs_only")
def test_the_real_page_standards_page_passes_its_own_check() -> None:
    name = "docs/guides/writing-docs.md"
    assert errors(name, (REPO / name).read_text(encoding="utf-8"), docs_check.context_for(REPO)) == []


def test_a_person_is_not_an_objects_owner() -> None:
    context = Context(avoid=("owner",))
    assert errors(GUIDE, "# G\n\nThe object's owner pointer, and its owner.\n", context) == []
    assert errors(GUIDE, "# G\n\nThe project owner decides.\n", context) == ["person-wording"]


def test_not_yet_on_a_guide_counts_only_when_it_says_what_the_project_has_not_done() -> None:
    assert errors(GUIDE, "# G\n\nStatus: not yet looked at.\n") == []
    assert errors(GUIDE, "# G\n\nThe game draws scenery not yet loaded as boxes.\n") == []
    assert errors(GUIDE, "# G\n\nThe 16:9 overwrites are not yet listed.\n") == ["stale-wording"]
    assert errors(GUIDE, "# G\n\nThis has not yet been implemented.\n") == ["stale-wording"]


def test_a_longer_fence_can_show_a_shorter_one() -> None:
    page = "# G\n\n````markdown\n```text\nnot yet written\n```\nnot yet written\n````\n\nAll done.\n"
    assert errors(GUIDE, page) == []
    assert errors(GUIDE, page + "\nIt is not yet written.\n") == ["stale-wording"]


def test_the_owner_of_a_thing_is_not_a_person() -> None:
    context = Context(avoid=("owner",))
    assert errors(GUIDE, "# G\n\nThe owner of the task runs it.\n", context) == []


# --- a branch cut before main added a conventions key (#1355) -------------------------------------------------------


def test_the_docs_check_fills_a_key_main_added_from_the_defaults(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """`task review` runs the docs check on a branch whose file lacks a key main added since: the key takes main's
    value, as the release commands' lenient read does, instead of stopping the review."""
    root = checkout_with_conventions(tmp_path)
    write_older_checkout(root)
    docs_check.context_for(root)
    assert check_cli._standards(root, "main") == (0, [], [])  # nothing changed to check: the read alone must not fail
    monkeypatch.chdir(root)
    assert docs_cli.run_check([], "main", False, False) == 0
    assert NEWER_KEY not in capsys.readouterr().out


def test_the_docs_check_still_refuses_a_wrong_value_or_a_key_with_no_default(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    root = checkout_with_conventions(tmp_path)
    write_older_checkout(root, (NEWER_KEY, "lots"))
    with pytest.raises(ConventionsError, match=r"check_stall_minutes: expected a whole number"):
        docs_check.context_for(root)
    # Main's file lacking the key too leaves nothing to fill it from.
    write_older_checkout(root)
    monkeypatch.setattr("coney_tools.conventions._own_defaults", lambda: changed((NEWER_KEY, REMOVE)))
    with pytest.raises(ConventionsError, match=r"missing the key 'check_stall_minutes'"):
        docs_check.context_for(root)
    with pytest.raises(ConventionsError, match=r"missing the key 'check_stall_minutes'"):
        check_cli._standards(root, "main")
