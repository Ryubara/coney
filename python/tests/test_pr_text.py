# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for step 5: the pull request text generated from the engineer's file, the task and the stamps, then checked."""

import dataclasses
import re
from datetime import UTC, datetime
from pathlib import Path

import pytest
from conventions_support import changed
from pr_support import PARAGRAPH, SOURCE_FILE, Built, make
from test_orig_compare import TWO_FUNCTIONS

from coney_tools import pr_proof, pr_text, stamps
from coney_tools.config import ConfigError
from coney_tools.conventions import parse_conventions
from coney_tools.issue_claim_remote import IssueView
from coney_tools.issue_form import section_heading
from coney_tools.pr_text import Generated
from coney_tools.repo_checks import issues_affected_problems, issues_affected_shape
from coney_tools.task_create import CreateRequest, build_task_file
from coney_tools.task_file import parse_task_file, render_task_file
from coney_tools.task_leads import LeadsResult


def generate(built: Built, text: str | None = None, trailers: tuple[str, ...] = (), **more: object) -> Generated:
    """Generate the description of the unit as it is now, from `text` (default: the PR.md the unit has)."""
    if text is not None:
        (built.unit.dir / "PR.md").write_text(text, encoding="utf-8")
    conventions = more.pop("conventions", None)
    subject = built.subject(**({"conventions": conventions} if conventions else {}))
    task_text = (built.unit.dir / "TASK.md").read_text(encoding="utf-8")
    task = parse_task_file(task_text, subject.conventions.tasks)
    engineer = pr_text.read_engineer_file(built.unit.dir / "PR.md", subject.conventions)
    return pr_text.generate(
        subject,
        task,
        engineer,
        keys=pr_text.optional_task_keys(task_text),
        trailer_models=trailers,
        **more,
    )


def problems(generated: Generated) -> list[str]:
    """The refusals' texts."""
    return [r.text for r in generated.refusals]


@pytest.mark.parametrize("shape", ["feature", "docs", "tooling"])
def test_the_generated_description_passes_every_check_for_every_kind_of_task(tmp_path: Path, shape: str) -> None:
    built = make(tmp_path, shape=shape)
    generated = generate(built)
    assert problems(generated) == []
    assert generated.title == (built.unit.dir / "PR.md").read_text(encoding="utf-8").splitlines()[0]
    assert "Co-Authored-By" not in generated.body


def test_the_generated_headings_are_the_templates_sections(tmp_path: Path) -> None:
    built = make(tmp_path)
    body = generate(built).body
    headings = [line[3:] for line in body.splitlines() if line.startswith("## ")]
    assert headings == list(built.conventions.pull_requests.sections)
    template = (built.root / ".github" / "pull_request_template.md").read_text(encoding="utf-8")
    assert headings == [line[3:] for line in template.splitlines() if line.startswith("## ")]


def without_issues_affected(built: Built) -> str:
    """The unit's PR.md with its Issues affected section taken out."""
    text = (built.unit.dir / "PR.md").read_text(encoding="utf-8")
    return text.replace("## Issues affected\n\nNone (no open issue is affected)\n\n", "")


def test_a_branch_that_adds_a_title_area_is_held_to_the_default_branchs_areas(tmp_path: Path) -> None:
    built = make(tmp_path)
    text = (built.unit.dir / "PR.md").read_text(encoding="utf-8")
    assert problems(generate(built)) == []
    built.edit_rules(("commits.title.areas.sneaky", "an area only this branch has"))
    generated = generate(built, text.replace("gameplay: Fix the widget", "sneaky: Fix the widget", 1))
    assert any("sneaky" in t for t in problems(generated)), problems(generated)


def test_a_branch_that_drops_the_runtime_line_for_gameplay_is_held_to_the_default_branchs_rule(tmp_path: Path) -> None:
    built = make(tmp_path)
    text = (built.unit.dir / "PR.md").read_text(encoding="utf-8")
    without = text.replace("- Runtime: trace diff widget60, 600 updates, 0 outside tolerance\n", "")
    assert without != text
    built.edit_rules(("pull_requests.runtime.areas", ["graphics", "audio"]))
    generated = generate(built, without)
    assert any("Runtime" in t for t in problems(generated)), problems(generated)


def test_a_section_the_conventions_add_is_generated_as_a_todo_the_checks_refuse(tmp_path: Path) -> None:
    built = make(tmp_path)
    sections = [*built.conventions.pull_requests.sections, "Rollout"]
    conventions = parse_conventions(changed(("pull_requests.sections", sections)))
    generated = generate(built, conventions=conventions)
    assert "## Rollout\n\nTODO:" in generated.body
    assert any(t.startswith("body: '## Rollout' is not filled in") for t in problems(generated))


def test_the_engineer_writes_the_issues_affected_section_and_it_is_kept_as_written(tmp_path: Path) -> None:
    built = make(tmp_path)
    text = (built.unit.dir / "PR.md").read_text(encoding="utf-8")
    generated = generate(built, text.replace("None (no open issue is affected)", "- #812: its step 2 is done"))
    assert "## Issues affected\n\n- #812: its step 2 is done\n" in generated.body
    assert problems(generated) == []


def fake_impact(monkeypatch: pytest.MonkeyPatch, seen: list[tuple[Path, str, str]]) -> None:
    """Make `pr text` read two candidates from `issue impact`: #760 and #757, which is the unit's own issue."""

    def impact(root: Path, base: str, tip: str) -> dict[str, object]:
        seen.append((root, base, tip))
        detail = [{"detail": "modifies src/ai/widget.cpp"}]
        return {
            "issues": [
                {"number": 760, "title": "Widget `ready` @team", "reasons": detail},
                {"number": 757, "title": "The task's own issue", "reasons": detail},
            ]
        }

    monkeypatch.setattr(pr_text, "_run_impact", impact)


def issues_affected_todo(generated: Generated) -> str:
    """The text of the Issues affected section of a generated body."""
    return generated.body.split("## Issues affected" + chr(10) * 2)[1].split(chr(10) * 2 + "## ")[0]


def test_without_the_section_the_candidates_of_issue_impact_are_a_todo_the_engineer_confirms(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    built = make(tmp_path)
    (built.unit.dir / "PR.md").write_text(without_issues_affected(built), encoding="utf-8")
    seen: list[tuple[Path, str, str]] = []
    fake_impact(monkeypatch, seen)
    generated = generate(built)
    section = issues_affected_todo(generated)
    shape = issues_affected_shape(built.conventions.pull_requests)
    assert section.startswith(f"TODO: write this section in PR.md: {shape}.")
    assert "- `#760` `Widget 'ready' @\u200bteam` (candidate: `modifies src/ai/widget.cpp`)" in section
    assert "#757" not in section  # the issue the task finishes is not a candidate
    assert any(t.startswith("body: '## Issues affected' is not filled in") for t in problems(generated))
    assert seen == [(built.root, built.base, built.tip)]


def test_a_task_that_does_not_finish_its_issue_keeps_it_as_a_candidate(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    built = make(tmp_path)
    (built.unit.dir / "PR.md").write_text(without_issues_affected(built), encoding="utf-8")
    path = built.unit.dir / "TASK.md"
    path.write_text(
        path.read_text(encoding="utf-8").replace("kind: feature", "kind: feature\nfinishes: false"), encoding="utf-8"
    )
    fake_impact(monkeypatch, [])
    generated = generate(built)
    assert "Fixes" not in generated.body  # the issue stays open, so it is an issue this change affects
    section = issues_affected_todo(generated)
    assert "- `#757` `The task's own issue` (candidate: `modifies src/ai/widget.cpp`)" in section


def test_a_failing_issue_impact_is_said_in_the_todo_not_raised(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    built = make(tmp_path)
    (built.unit.dir / "PR.md").write_text(without_issues_affected(built), encoding="utf-8")

    def broken(root: Path, base: str, tip: str) -> dict[str, object]:
        raise ConfigError("cannot read the open issues with gh (offline)")

    monkeypatch.setattr(pr_text, "_run_impact", broken)
    generated = generate(built)
    assert "`coney-tools issue impact` could not run (cannot read the open issues with gh (offline))." in generated.body
    assert not generated.ok


def test_what_the_generated_section_asks_for_is_what_check_body_accepts(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    built = make(tmp_path)
    (built.unit.dir / "PR.md").write_text(without_issues_affected(built), encoding="utf-8")
    fake_impact(monkeypatch, [])
    asked = issues_affected_todo(generate(built))
    rules = built.conventions.pull_requests
    item = re.search(r"'(- #N: [^']+)'", asked)
    example = re.search(r"for example '([^']+)'", asked)
    assert item is not None and example is not None  # the TODO shows the item form and the example it asks for
    for section in (item.group(1).replace("#N", "#12"), example.group(1)):
        assert issues_affected_problems(section, rules) == []


def test_the_engineers_file_may_not_hold_generated_sections_fixes_lines_or_markers(tmp_path: Path) -> None:
    built = make(tmp_path)
    text = f"""gameplay: Fix the widget ready frame

## Summary

{PARAGRAPH}

Fixes #757
<!-- coney:facts -->

## Checklist

- [x] done

## Testing

- Runtime: trace diff widget60, 600 updates, 0 outside tolerance
- 412 passed

## AI use

- **Commits written by:** an AI

## Screenshots

Screenshots: none (no visible change)
"""
    refused = problems(generate(built, text))
    assert 'text: PR.md: "## Checklist" is generated; remove it' in refused
    assert 'text: PR.md: "Fixes" lines are generated from TASK.md; remove them' in refused
    assert "text: PR.md: the markers are generated; remove them" in refused
    assert 'text: PR.md: "## Testing" holds only the "Runtime:" line; the facts are generated' in refused
    assert 'text: PR.md: "## AI use" is generated from the log and the commits; remove it' in refused


def test_a_summary_of_two_paragraphs_or_none_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path)
    tail = "## Screenshots" + chr(10) * 2 + "Screenshots: none (no visible change)" + chr(10)
    two = (
        "docs: Document the widget"
        + chr(10) * 2
        + "## Summary"
        + chr(10) * 2
        + PARAGRAPH
        + chr(10) * 2
        + "A second one."
        + chr(10) * 2
        + tail
    )
    assert 'text: PR.md: "## Summary" is one paragraph; found 2' in problems(generate(built, two))
    none = "docs: Document the widget\n\n## Summary\n\n## Screenshots\n\nScreenshots: none (no visible change)\n"
    assert 'text: PR.md: "## Summary" has no paragraph' in problems(generate(built, none))


def test_the_fixes_lines_come_from_the_task_file_one_per_issue_and_none_when_it_does_not_finish(tmp_path: Path) -> None:
    built = make(tmp_path)
    path = built.unit.dir / "TASK.md"
    task = path.read_text(encoding="utf-8")
    path.write_text(task.replace("issues: [757]", "issues: [757, 758]"), encoding="utf-8")
    body = generate(built).body
    assert "\nFixes #757\nFixes #758\n" in body
    path.write_text(
        task.replace("issues: [757]", "issues: [757, 758]").replace("kind: feature", "kind: feature\nfinishes: false"),
        encoding="utf-8",
    )
    assert "Fixes" not in generate(built).body


def test_the_checklist_has_one_ticked_item_per_done_criterion(tmp_path: Path) -> None:
    body = generate(make(tmp_path)).body
    assert "Done:\n\n- [x] The widget waits for its ready frame.\n- [x] A test proves it.\n" in body


def created_task(built: Built, done: str | None) -> None:
    """Write the unit's TASK.md as `task create` writes it for issue 757, whose "done" section is `done` (None: the
    issue has none)."""
    conventions = built.conventions
    heading = section_heading(conventions, "done")
    body = "### Summary\n\nPort the widget update.\n\n" + (f"### {heading}\n\n{done}\n" if done is not None else "")
    issue = IssueView(757, "gameplay: Port the widget", "open", None, False, ("task",), (), body)
    claimed = datetime(2026, 10, 22, 11, 0, tzinfo=UTC)
    task = build_task_file(
        CreateRequest(issues=(757,)), "work", built.kind, "alice", claimed, [issue], LeadsResult(), [], conventions
    )
    (built.unit.dir / "TASK.md").write_text(render_task_file(task), encoding="utf-8")


def checklist(body: str) -> str:
    """The text of the generated Checklist section."""
    return body.split("## Checklist\n\n", 1)[1].split("\n## ", 1)[0]


def test_the_checklist_reads_the_quoted_done_section_task_create_writes(tmp_path: Path) -> None:
    built = make(tmp_path)
    created_task(built, "- The widget waits for its ready frame.\n- A test proves it.")
    assert "> - The widget waits" in (built.unit.dir / "TASK.md").read_text(encoding="utf-8")
    generated = generate(built)
    expected = "Done:\n\n- [x] The widget waits for its ready frame.\n- [x] A test proves it."
    assert checklist(generated.body).startswith(expected)
    assert not any("Done section" in p for p in problems(generated))
    created_task(built, "The widget waits for its\nready frame.\n\nA test proves it.")
    assert checklist(generate(built).body).startswith(expected)


def test_a_done_bullet_that_wraps_onto_more_lines_is_one_whole_checklist_item(tmp_path: Path) -> None:
    built = make(tmp_path)
    created_task(
        built,
        "- The widget waits for its ready frame,\n  then plays the full\n  animation.\n"
        "- A test proves it,\nflush with the margin.\n"
        "  - A nested bullet is its own item\n    that wraps too.",
    )
    generated = generate(built)
    expected = (
        "Done:\n\n- [x] The widget waits for its ready frame, then plays the full animation.\n"
        "- [x] A test proves it, flush with the margin.\n"
        "- [x] A nested bullet is its own item that wraps too."
    )
    assert checklist(generated.body).startswith(expected)
    assert not any("Done section" in p for p in problems(generated))


def test_a_blank_line_ends_a_done_bullet_and_prose_after_it_is_not_an_item(tmp_path: Path) -> None:
    built = make(tmp_path)
    created_task(built, "- The widget waits\n  for its ready frame.\n\nSome closing prose.\n- A test proves it.")
    expected = "Done:\n\n- [x] The widget waits for its ready frame.\n- [x] A test proves it."
    assert checklist(generate(built).body).startswith(expected)


def test_a_checked_bullet_that_wraps_keeps_its_whole_text(tmp_path: Path) -> None:
    built = make(tmp_path)
    created_task(built, "- [x] The widget waits\n  for its ready frame.")
    assert checklist(generate(built).body).startswith("Done:\n\n- [x] The widget waits for its ready frame.")


def test_a_title_area_no_player_sees_may_leave_out_or_keep_the_runtime_line(tmp_path: Path) -> None:
    """The pull request template says only gameplay, graphics and audio titles need the line: pr text agrees."""
    built = make(tmp_path, shape="tooling")
    text = (built.unit.dir / "PR.md").read_text(encoding="utf-8").split("\n", 1)[1]
    runtime = "- Runtime: trace diff widget60, 600 updates, 0 outside tolerance"
    assert "Runtime:" not in text
    left_out = generate(built, "tools: Fix the widget\n" + text)
    assert problems(left_out) == [] and "Runtime:" not in left_out.body
    with_line = text.replace("## Screenshots", f"## Testing\n\n{runtime}\n\n## Screenshots")
    kept = generate(built, "tools: Fix the widget\n" + with_line)
    assert problems(kept) == [] and runtime in kept.body
    template = (built.root / ".github" / "pull_request_template.md").read_text(encoding="utf-8")
    assert "Any other title area does not need the line" in template


def test_a_done_section_with_no_criterion_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path)
    created_task(built, None)
    assert "> See issue #757." in (built.unit.dir / "TASK.md").read_text(encoding="utf-8")
    message = "text: TASK.md: the Done section gives no item for the Checklist; write what done means there"
    assert any(p.startswith(message) for p in problems(generate(built)))


def test_the_paragraph_and_the_facts_sit_between_their_markers(tmp_path: Path) -> None:
    built = make(tmp_path)
    generated = generate(built)
    assert f"<!-- coney:summary -->\n{PARAGRAPH}\n<!-- /coney:summary -->" in generated.body
    assert f"<!-- coney:facts -->\n{generated.facts}\n<!-- /coney:facts -->" in generated.body


def test_the_facts_name_every_stage_with_its_stamp_the_change_and_the_chain_head(tmp_path: Path) -> None:
    built = make(tmp_path)
    subject = built.subject()
    facts = pr_text.facts_block(subject)
    assert facts == pr_text.facts_block(built.subject())
    report = subject.report
    assert facts.startswith(
        f"Stages (local log of `work`, chain head `{report.head[:12]}`; change `diff {built.diff[:12]}` "
        f"on `origin/main` `{built.base[:12]}`):"
    )
    for step, name in (("studied", "Studied"), ("compared", "Compared"), ("checked", "Check"), ("reviewed", "Review")):
        stamp = report.latest(step)
        assert stamp is not None and f"`{stamp.hash[:12]}`" in facts and f"- {name}" in facts
    assert "- Failed first: `Widget ready frame` failed before the fix, passes after" in facts
    assert "- Check: profile `full`, preset `ci`, 412 passed, 0 failed, 0 skipped" in facts
    assert "build record `" in facts and "- Review: pass, Claude Opus 5.5, 1 function clean" in facts
    assert "Stand-ins:\n- `src/ai/widget.cpp:12` `the sound plays later`" in facts


def test_two_functions_of_one_address_are_counted_by_their_own_compared_stamps(tmp_path: Path) -> None:
    built = make(tmp_path)
    built.edit(SOURCE_FILE, TWO_FUNCTIONS)

    def short() -> list[str]:
        return pr_text._stage_lines(built.subject(), True)

    assert "- Studied 1 of 1 functions" in short() and "- Compared 1 of 2 functions" in short()
    built.compare(key="Widget::reset()")
    assert "- Compared 2 of 2 functions" in short()


def test_a_check_with_no_test_run_names_its_checks_and_never_reads_as_an_empty_run(tmp_path: Path) -> None:
    built = make(tmp_path, shape="tooling", skip=frozenset({"checked"}))
    built.check("tooling", preset=None, tests=None)
    facts = pr_text.facts_block(built.subject())
    stamp = built.subject().report.latest("checked")
    assert stamp is not None
    names = ("docs-check", "mkdocs", "pre-commit", "ruff", "mypy", "pytest", "actionlint")
    expected = ", ".join(f"`{name}`" for name in names)
    assert f"- Check: profile `tooling`, no build; checks {expected} (stamp `{stamp.hash[:12]}`)" in facts
    assert "0 passed" not in facts and "preset ``" not in facts and "no build or test run" not in facts


def test_a_tooling_check_gives_the_pytest_counts_it_ran(tmp_path: Path) -> None:
    built = make(tmp_path, shape="tooling", skip=frozenset({"checked"}))
    built.check("tooling", preset=None, tests=None, pytest={"passed": 7276, "skipped": 11, "deselected": 1})
    facts = pr_text.facts_block(built.subject())
    stamp = built.subject().report.latest("checked")
    assert stamp is not None
    assert (
        "- Check: profile `tooling`, no build; pytest 7276 passed, 0 failed, 11 skipped; checks `docs-check`, " in facts
    )


def test_a_docs_check_says_no_build_or_test_ran(tmp_path: Path) -> None:
    built = make(tmp_path, shape="tooling", skip=frozenset({"checked"}))
    built.check("docs", preset=None, tests=None)
    facts = pr_text.facts_block(built.subject())
    assert "- Check: profile `docs`, no build or test run; checks `docs-check`, `mkdocs`" in facts


def test_a_stage_that_has_not_run_is_said_so_in_the_facts(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"checked", "reviewed"}))
    facts = pr_text.facts_block(built.subject())
    assert "- Check: not run" in facts and "- Review: not reviewed" in facts


def test_names_from_stamps_and_pairs_are_code_spans_with_backticks_and_mentions_neutralised() -> None:
    assert pr_text.code("a `b` @carol\nnext") == "`a 'b' @​carol next`"
    assert pr_text.code("") == "``"
    built_name = pr_text.code("Widget @team `ready`")
    assert "`" not in built_name[1:-1] and "@team" not in built_name


def test_a_test_name_with_a_mention_is_neutralised_in_the_facts(tmp_path: Path) -> None:
    built = make(tmp_path)
    built.fails_first("Ping @maintainers `now`")
    facts = pr_text.facts_block(built.subject())
    assert "@maintainers" not in facts and "@​maintainers 'now'" in facts


def test_a_keeps_test_is_named_in_the_facts_apart_from_the_tests_that_failed_first(tmp_path: Path) -> None:
    built = make(tmp_path)
    stamp = built.fails_first("Widget same lookup", result="keeps")
    facts = pr_text.facts_block(built.subject())
    assert (
        "- Kept behaviour: `Widget same lookup` passes before the change and after it, not failed first "
        f"(stamp `{stamp.hash[:12]}`)"
    ) in facts
    assert "- Failed first: `Widget ready frame` failed before the fix, passes after" in facts
    assert "Failed first: `Widget same lookup`" not in facts


def test_the_short_facts_count_the_keeps_tests(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    built = make(tmp_path)
    built.fails_first("Widget same lookup", result="keeps")
    built.fails_first("Widget same scan", result="keeps")
    monkeypatch.setattr(pr_text, "FACTS_LIMIT", len(pr_text.facts_block(built.subject())) - 1)
    assert "- Kept behaviour: 2 tests (pass before the change and after it)" in pr_text.facts_block(built.subject())


def test_the_facts_become_counts_over_the_limit(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    built = make(tmp_path)
    full = pr_text.facts_block(built.subject())
    monkeypatch.setattr(pr_text, "FACTS_LIMIT", len(full) - 1)
    short = pr_text.facts_block(built.subject())
    assert len(short) < len(full)
    assert "- Studied 1 of 1 functions" in short and "- Failed first: 1 tests" in short
    assert "- Check: profile" in short and "- Review: pass" in short
    assert "Stand-ins and not-ported pieces: 1" in short


def test_the_facts_are_at_most_the_limit_whatever_the_unit(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    built = make(tmp_path)
    monkeypatch.setattr(pr_text, "FACTS_LIMIT", 120)
    assert len(pr_text.facts_block(built.subject())) <= 120


def test_the_facts_are_cut_at_a_line_so_no_code_span_is_left_open(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    built = make(tmp_path)
    monkeypatch.setattr(pr_text, "FACTS_LIMIT", 200)
    facts = pr_text.facts_block(built.subject())
    assert len(facts) <= 200
    assert all(line.count("`") % 2 == 0 for line in facts.split("\n")), facts


def test_a_check_or_review_of_an_older_change_is_shown_stale_in_the_facts(tmp_path: Path) -> None:
    built = make(tmp_path)
    old = built.diff
    built.edit("src/ai/widget.cpp", built.root.joinpath("src/ai/widget.cpp").read_text(encoding="utf-8") + "// more\n")
    facts = pr_text.facts_block(built.subject())
    assert f"- Check: stale (made for change `{old[:12]}`), profile `full`" in facts
    assert f"- Review: stale (made for change `{old[:12]}`), Claude Opus 5.5" in facts
    assert "- Review: pass" not in facts


# --- images ---------------------------------------------------------------------------------------------------------


def shots(built: Built, **files: bytes) -> None:
    """Create pictures in the unit's shots folder and show them in PR.md."""
    folder = built.unit.dir / "shots"
    folder.mkdir(exist_ok=True)
    for name, data in files.items():
        (folder / name).write_bytes(data)
    lines = "\n\n".join(f"![{name}](./shots/{name})" for name in files)
    text = (built.unit.dir / "PR.md").read_text(encoding="utf-8")
    (built.unit.dir / "PR.md").write_text(
        text.replace("Screenshots: none (no visible change)", lines), encoding="utf-8"
    )


def test_pictures_are_kept_in_the_body_as_relative_references_and_hashed(tmp_path: Path) -> None:
    built = make(tmp_path)
    shots(built, **{"before.png": b"\x89PNG one", "after.png": b"\x89PNG two"})
    generated = generate(built)
    assert problems(generated) == []
    assert "![before.png](./shots/before.png)" in generated.body and "![after.png](./shots/after.png)" in generated.body
    assert generated.text_hashes.images_sha256 != make(tmp_path / "b").text_hashes().images_sha256


@pytest.mark.parametrize(
    ("files", "message"),
    [
        ({"missing.png": b""}, None),
        ({"clip.mp4": b"v"}, "images: shots/clip.mp4 is a video; use clips publish"),
        ({"notes.txt": b"t"}, "images: shots/notes.txt is not a picture (png, jpg, jpeg, gif)"),
        ({"a.png": b"same", "b.png": b"same"}, "images: shots/b.png is identical to shots/a.png"),
    ],
)
def test_a_bad_picture_is_refused(tmp_path: Path, files: dict[str, bytes], message: str | None) -> None:
    built = make(tmp_path)
    shots(built, **files)
    if message is None:
        (built.unit.dir / "shots" / "missing.png").unlink()
        message = "images: shots/missing.png does not exist"
    assert message in problems(generate(built))


@pytest.mark.parametrize(
    "ref", ["C:/Users/someone/scratch/work/shots/a.png", "/home/someone/shots/a.png", "./shots/../a.png", "a.png"]
)
def test_a_picture_that_is_not_a_relative_path_under_the_shots_folder_is_refused(tmp_path: Path, ref: str) -> None:
    built = make(tmp_path)
    shots(built, **{"a.png": b"\x89PNG one"})
    path = built.unit.dir / "PR.md"
    path.write_text(path.read_text(encoding="utf-8").replace("./shots/a.png", ref), encoding="utf-8")
    generated = generate(built)
    assert any(f"the picture {ref} is not a relative path under ./shots/" in p for p in problems(generated))
    assert ref not in generated.body


def test_a_picture_over_the_limit_or_beyond_the_count_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path)
    shots(built, **{"big.png": b"x" * 2048, "other.png": b"y"})
    small = dataclasses.replace(
        built.conventions.pull_requests,
        images=dataclasses.replace(built.conventions.pull_requests.images, max_bytes=1024, max_count=1),
    )
    conventions = dataclasses.replace(built.conventions, pull_requests=small)
    refused = problems(generate(built, conventions=conventions))
    assert "images: 2 pictures; at most 1" in refused and "images: shots/big.png is 0 MB (limit 0)" in refused


def test_a_picture_inside_a_git_checkout_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path)
    folder = tmp_path / "checkout-ish"
    (folder / ".git").mkdir(parents=True)
    (folder / "shots").mkdir()
    (folder / "shots" / "x.png").write_bytes(b"png")
    picture = pr_text.Image("./shots/x.png", "x", folder / "shots" / "x.png")
    text = pr_text.EngineerText(folder / "PR.md", folder, "t", "p", False, None, (picture,), None, None)
    assert pr_text.image_problems(text, built.conventions) == [
        "images: shots/x.png is inside a git checkout; game pictures stay in the scratch folder"
    ]


def test_a_picture_replaced_after_review_makes_the_review_stale(tmp_path: Path) -> None:
    built = make(tmp_path, skip=frozenset({"reviewed"}))
    shots(built, **{"after.png": b"one"})
    built.review()
    (built.unit.dir / "shots" / "after.png").write_bytes(b"two")
    result = built.verify()
    assert "reviewed: stale: the screenshots changed since Review" in [r.text for r in result.refusals]


# --- AI use ---------------------------------------------------------------------------------------------------------


def test_ai_use_names_the_models_of_the_trailers_or_the_log_and_adds_no_trailer(tmp_path: Path) -> None:
    built = make(tmp_path)
    body = generate(built).body
    assert "- **Commits written by:** an AI\n- **Model:** Claude Opus 5.5" in body
    assert "Co-Authored-By" not in body
    body = generate(built, trailers=("Claude Sonnet 5.5",)).body
    assert "- **Model:** Claude Sonnet 5.5\n" in body and "Opus" not in body.split("## AI use")[1]


def test_a_persons_own_pull_request_takes_the_files_ai_use_lines(tmp_path: Path) -> None:
    built = make(tmp_path)
    subject = dataclasses.replace(built.subject(), report=stamps.Report())
    task_text = (built.unit.dir / "TASK.md").read_text(encoding="utf-8")
    task = parse_task_file(task_text, subject.conventions.tasks)
    path = built.unit.dir / "PR.md"
    path.write_text(
        path.read_text(encoding="utf-8") + "\n## AI use\n\n- **Commits written by:** a person\n- **Model:** n/a\n",
        encoding="utf-8",
    )
    engineer = pr_text.read_engineer_file(path, subject.conventions)
    generated = pr_text.generate(subject, task, engineer)
    assert "- **Commits written by:** a person\n- **Model:** n/a" in generated.body
    assert not any("AI use" in t for t in problems(generated))
    assert isinstance(subject, pr_proof.Subject)


def test_the_title_the_paragraph_and_the_pictures_have_stable_hashes(tmp_path: Path) -> None:
    built = make(tmp_path)
    one = built.text_hashes()
    path = built.unit.dir / "PR.md"
    original = path.read_text(encoding="utf-8")
    crlf = original.replace("\n", "  \r\n")
    path.write_bytes(crlf.encode("utf-8"))
    assert built.text_hashes() == one  # line endings and trailing spaces are not content (as task review reads it)
    path.write_text(original.replace("waits for", "waits on"), encoding="utf-8")
    assert built.text_hashes().paragraph_sha256 != one.paragraph_sha256
    path.write_text(original.replace("Fix the", "Fix a"), encoding="utf-8")
    assert built.text_hashes().title_sha256 != one.title_sha256


def test_the_sections_pr_text_writes_are_the_ones_the_conventions_name(tmp_path: Path) -> None:
    """#1094: the review packet tells the reviewer which sections `PR.md` lacks on purpose; the list is read from the
    conventions' section names, and holds exactly the sections the engineer's file does not write."""
    built = make(tmp_path)
    rules = built.conventions.pull_requests
    found = pr_text.generated_sections(built.conventions)
    names = [name for name, _ in found]
    assert names == [name for name in rules.sections if name in names]  # in the template's order
    assert set(names) == {rules.closing.section, rules.checklist.section, rules.runtime.section, rules.ai_use.section}
    engineer = (built.unit.dir / "PR.md").read_text(encoding="utf-8")
    body = generate(built).body
    for name in rules.sections:
        written_by_engineer = f"## {name}\n" in engineer
        assert f"## {name}\n" in body
        # The Summary and Testing are the engineer's too, in part; the other two are not in the file at all.
        if name in (rules.checklist.section, rules.ai_use.section):
            assert not written_by_engineer and name in names
        if name in (rules.screenshots.section, rules.issues_affected.section):
            assert name not in names
    what = dict(found)
    assert rules.closing.keyword in what[rules.closing.section]
    assert rules.runtime.line in what[rules.runtime.section]


def test_the_generated_sections_follow_a_renamed_section(tmp_path: Path) -> None:
    built = make(tmp_path)
    sections = ["Authorship" if name == "AI use" else name for name in built.conventions.pull_requests.sections]
    conventions = parse_conventions(
        changed(("pull_requests.sections", sections), ("pull_requests.ai_use.section", "Authorship"))
    )
    assert [name for name, _ in pr_text.generated_sections(conventions)][-1] == "Authorship"


def test_the_generated_files_are_named_once() -> None:
    assert (pr_text.TEXT_FILE, pr_text.TITLE_FILE, pr_text.BODY_FILE) == ("PR.md", "PR-TITLE.txt", "PR-BODY.md")
