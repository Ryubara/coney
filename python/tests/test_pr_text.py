# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for step 5: the pull request text generated from the engineer's file, the task and the stamps, then checked."""

import dataclasses
from pathlib import Path

import pytest
from conventions_support import changed
from pr_support import PARAGRAPH, Built, make

from coney_tools import pr_proof, pr_text, stamps
from coney_tools.config import ConfigError
from coney_tools.conventions import parse_conventions
from coney_tools.pr_text import Generated
from coney_tools.task_file import parse_task_file


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


def test_a_section_the_conventions_add_is_generated_as_a_todo_the_checks_refuse(tmp_path: Path) -> None:
    built = make(tmp_path)
    sections = [*built.conventions.pull_requests.sections, "Issues affected"]
    conventions = parse_conventions(changed(("pull_requests.sections", sections)))
    generated = generate(built, conventions=conventions)
    assert "## Issues affected\n\nTODO:" in generated.body
    assert any(t.startswith("body: '## Issues affected' is not filled in") for t in problems(generated))


def with_issues_affected(built: Built) -> object:
    """The conventions with the template's extra section, as the Issues affected rule adds it."""
    sections = [*built.conventions.pull_requests.sections, "Issues affected"]
    return parse_conventions(changed(("pull_requests.sections", sections)))


def test_the_engineer_writes_the_issues_affected_section_and_it_is_kept_as_written(tmp_path: Path) -> None:
    built = make(tmp_path)
    text = (built.unit.dir / "PR.md").read_text(encoding="utf-8")
    section = "## Issues affected\n\n- #812: its step 2 is done\n"
    written = text.replace("## Testing", section + "\n## Testing")
    generated = generate(built, written, conventions=with_issues_affected(built))
    assert section in generated.body
    assert problems(generated) == []


def test_without_the_section_the_candidates_of_issue_impact_are_a_todo_the_engineer_confirms(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    built = make(tmp_path)
    seen: list[tuple[Path, str, str]] = []

    def fake_impact(root: Path, base: str, tip: str) -> dict[str, object]:
        seen.append((root, base, tip))
        return {
            "issues": [
                {"number": 757, "title": "Widget `ready` @team", "reasons": [{"detail": "modifies src/ai/widget.cpp"}]}
            ]
        }

    monkeypatch.setattr(pr_text, "IMPACT_MODULE", "json")  # any module that exists stands for coney_tools.issue_impact
    monkeypatch.setattr(pr_text, "_run_impact", fake_impact)
    generated = generate(built, conventions=with_issues_affected(built))
    section = generated.body.split("## Issues affected" + chr(10) * 2)[1].split(chr(10) * 2 + "## ")[0]
    assert section.startswith("TODO: write this section in PR.md")
    assert "- `#757` `Widget 'ready' @\u200bteam` (candidate: `modifies src/ai/widget.cpp`)" in section
    assert any(t.startswith("body: '## Issues affected' is not filled in") for t in problems(generated))
    assert seen == [(built.root, built.base, built.tip)]


def test_a_failing_issue_impact_is_said_in_the_todo_not_raised(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    built = make(tmp_path)

    def broken(root: Path, base: str, tip: str) -> dict[str, object]:
        raise ConfigError("cannot read the open issues with gh (offline)")

    monkeypatch.setattr(pr_text, "IMPACT_MODULE", "json")
    monkeypatch.setattr(pr_text, "_run_impact", broken)
    generated = generate(built, conventions=with_issues_affected(built))
    assert "`coney-tools issue impact` could not run (cannot read the open issues with gh (offline))." in generated.body
    assert not generated.ok


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
        f"on main `{built.base[:12]}`):"
    )
    for step, name in (("studied", "Studied"), ("compared", "Compared"), ("checked", "Check"), ("reviewed", "Review")):
        stamp = report.latest(step)
        assert stamp is not None and f"`{stamp.hash[:12]}`" in facts and f"- {name}" in facts
    assert "- Failed first: `Widget ready frame` failed before the fix, passes after" in facts
    assert "- Check: profile `full`, preset `ci`, 412 passed, 0 failed, 0 skipped" in facts
    assert "build record `" in facts and "- Review: pass, Claude Opus 5.5, 1 function clean" in facts
    assert "Stand-ins:\n- `src/ai/widget.cpp:12` `the sound plays later`" in facts


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
    text = pr_text.EngineerText(
        folder, "t", "p", False, None, (pr_text.Image("./shots/x.png", "x", folder / "shots" / "x.png"),), None, None
    )
    assert pr_text.image_problems(text, built.conventions) == [
        "images: shots/x.png is inside a git checkout; game pictures stay in the scratch folder"
    ]


def test_a_picture_that_is_not_under_shots_is_refused(tmp_path: Path) -> None:
    built = make(tmp_path)
    (built.unit.dir / "elsewhere.png").write_bytes(b"png")
    text = (
        (built.unit.dir / "PR.md")
        .read_text(encoding="utf-8")
        .replace("Screenshots: none (no visible change)", "![x](./elsewhere.png)")
    )
    assert "images: elsewhere.png is not under shots/ next to PR.md" in problems(generate(built, text))


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
    path.write_text(
        path.read_text(encoding="utf-8").replace(PARAGRAPH, PARAGRAPH.replace(" the ", "  the\n")), encoding="utf-8"
    )
    assert built.text_hashes() == one  # a wrapped line or a doubled space is the same paragraph
    path.write_text(path.read_text(encoding="utf-8").replace("Fix the", "Fix a"), encoding="utf-8")
    assert built.text_hashes().title_sha256 != one.title_sha256
