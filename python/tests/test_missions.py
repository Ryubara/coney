# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the mission status list and its pages, on a synthetic list and masterlist."""

from pathlib import Path

import pytest

from coney_tools import missions, missions_render, natives
from coney_tools.cli import main
from coney_tools.config import ConfigError

TEXT = """
about: Intro text.
lifecycle: Lifecycle text.
missions:
  - level: 1
    group: story
    slot: story mission 1
    label: "Mission 1"
    summary: Short summary.
    summary_source: web
    sources: ["https://example.org/page"]
    status: in-progress
    checkpoints:
      count: 3
      default: not-started
      each:
        1: {status: pending-approval, note: "Plays | to the end."}
        2: {status: in-progress}
    research:
      - {title: A page, path: "research/a.md#x"}
    issues: ["An issue."]
    test: coney_tests "[x]"
  - level: 2
    group: hub
    slot: the hub
    label: "The hub"
    summary: Another.
    summary_source: research
    status: approved
    checkpoints: {count: 1, default: approved}
"""


def _binding(
    name: str, category: str, levels: tuple[int, ...], mission1: bool = False, done: bool = False
) -> natives.Binding:
    """A masterlist entry reduced to the fields the coverage reads."""
    usage = natives.Usage(chunks=1, calls=1, boot=False, mission1=mission1, result_used=False, levels=levels)
    return natives.Binding(
        name=name,
        category=category,
        origin="game",
        registered_by="RegisterBindings",
        main=natives.Variant(wrapper=None, calls=(), args=(), results=()),
        overloads=(),
        description="",
        evidence="inferred",
        depth="thorough" if done else "brief",
        notes="",
        usage=usage,
        coney="implemented" if done else "not implemented",
    )


def _parse(text: str = TEXT, sections: dict[int, int] | None = None) -> missions.MissionList:
    """The synthetic list, which lists only levels 1 and 2 (level 1 has a disc title, the hub none)."""
    return missions.parse(text, sections, expected={1, 2}, titles={1: "First One"})


def test_parse_builds_checkpoints_from_default_and_each() -> None:
    first = _parse().missions[0]
    assert [c.status for c in first.checkpoints] == ["pending-approval", "in-progress", "not-started"]
    assert first.built() == 1 and first.approved() == 0
    assert first.checkpoints[0].note == "Plays | to the end."


@pytest.mark.parametrize(
    ("old", "new", "message"),
    [
        ("status: in-progress", "status: pending-approval", "needs every checkpoint built"),
        ("status: in-progress", "status: nonsense", "`status` must be one of"),
        ("status: in-progress", "status: not-started", "Not Started but a checkpoint has started"),
        ("status: approved", "status: needs-fixes", "must list its known issues"),
        ("count: 3", "count: 4", "checkpoints.count is 4 but the levels list has 3"),
        ("group: hub", "group: other", "`group` must be one of"),
        ("level: 2", "level: 1", "listed twice"),
        ("summary_source: web", "summary_source: guess", "`summary_source` must be one of"),
        ('    sources: ["https://example.org/page"]\n', "", "must list its `sources`"),
    ],
)
def test_parse_rejects(old: str, new: str, message: str) -> None:
    with pytest.raises(ConfigError, match=message):
        _parse(TEXT.replace(old, new, 1), {1: 3, 2: 1})


def test_parse_requires_a_disc_title_except_for_the_hub() -> None:
    with pytest.raises(ConfigError, match="level1: no title in the levels list"):
        missions.parse(TEXT, None, expected={1, 2}, titles={2: "Unused"})


def test_parse_reports_a_missing_level() -> None:
    with pytest.raises(ConfigError, match="level3: missing"):
        missions.parse(TEXT, None, expected={1, 2, 3})


def test_parse_checks_the_sections_count() -> None:
    with pytest.raises(ConfigError, match="levels list has 5 sections"):
        _parse(TEXT, {1: 5})


def test_coverage_counts_new_bindings_in_story_order() -> None:
    first = natives.STORY_LEVELS[0][0]
    second = natives.STORY_LEVELS[1][0]
    masterlist = natives.Masterlist(
        bindings=[
            _binding("A", "character", (first, second), mission1=True, done=True),
            _binding("B", "gang", (first,), done=True),
            _binding("C", "gang", (second,)),
        ]
    )
    cover = missions.coverage(masterlist)
    assert cover[99].total == 1 and cover[99].new_done == 1
    assert (cover[first].total, cover[first].new, cover[first].new_done) == (2, 1, 1)
    assert cover[second].total == 2 and cover[second].new == 1 and cover[second].families == {"gang": (1, 0)}


def test_pages_show_status_checkpoints_and_links() -> None:
    listing = _parse()
    pages = missions_render.render(listing, {1: missions.Coverage(10, 4, 3, 2, {"gang": (4, 2)})})
    assert set(pages) == {"index.md", "level1.md", "level2.md"}
    page = pages["level1.md"]
    assert page.startswith(missions_render.GENERATED)
    assert "| Status | \U0001f6a7 In Progress |" in page
    assert "| 1 | \U0001f3ae Pending Gameplay Approval | Plays \\| to the end. |" in page
    assert "| [Gang](../references/bindings/gang.md)" not in page  # the family title is Gangs
    assert "| [Gangs](../references/bindings/gang.md) | 4 | 2 |" in page
    assert "[A page](../research/a.md#x)" in page
    assert "# Mission 1: First One (level1)" in page
    assert "paraphrased from web sources" in page and "- <https://example.org/page>" in page
    assert "paraphrased" not in pages["level2.md"] and "# The hub (level2)" in pages["level2.md"]
    assert "--play-level level1 --checkpoint 1" in page and "## Known issues" in page
    index = pages["index.md"]
    assert "| [Mission 1: First One](level1.md) | `level1` | \U0001f6a7 In Progress | 1 of 3 | 0 | 2 of 4 |" in index
    assert "| ✅ Approved | 1 |" in index
    assert all(
        max(len(line) for line in text.splitlines() if not line.startswith("|")) <= 120 for text in pages.values()
    )


def test_render_command_writes_then_checks(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    (tmp_path / "research" / "bindings").mkdir(parents=True)
    # The real list names every story level, so use the repository's own file against an empty masterlist.
    repo = Path(__file__).resolve().parents[2]
    (tmp_path / "research" / "missions.yaml").write_bytes((repo / "research" / "missions.yaml").read_bytes())
    monkeypatch.chdir(tmp_path)
    assert main(["missions", "render", "--check"]) == 1
    assert main(["missions", "render"]) == 0
    assert (tmp_path / "docs" / "missions" / "index.md").is_file()
    assert main(["missions", "render", "--check"]) == 0
    (tmp_path / "docs" / "missions" / "level99.md").write_text("old", encoding="utf-8")
    (tmp_path / "docs" / "missions" / "level1234.md").write_text("gone", encoding="utf-8")
    assert main(["missions", "render", "--check"]) == 1
    assert main(["missions", "render"]) == 0
    assert not (tmp_path / "docs" / "missions" / "level1234.md").exists()


def test_the_repository_list_is_valid_and_its_pages_current() -> None:
    repo = Path(__file__).resolve().parents[2]
    listing = missions.load(repo)
    assert len(listing.missions) == 29
    # The same check CI runs: the committed pages equal the rendered ones.
    covers = missions.coverage(natives.load(repo))
    for name, text in missions_render.render(listing, covers).items():
        assert (repo / "docs" / "missions" / name).read_text(encoding="utf-8").replace("\r\n", "\n") == text
