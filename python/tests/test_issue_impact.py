# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.issue_impact: which open issues a branch may have changed.

A real git repository is made in a temporary folder (invented code, names and addresses only) and the open issues are
made-up `gh issue list` JSON, so nothing reaches the network.
"""

import json
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any

import pytest
from conventions_support import conventions_with, write_checkout
from git_support import git
from pytest import CaptureFixture, MonkeyPatch

from coney_tools import audit_text, cli, issue_cli, issue_impact, issue_impact_open, issue_impact_report
from coney_tools.config import ConfigError

RULES = conventions_with().issues.impact
NOW = datetime(2026, 10, 24, 12, 0, tzinfo=UTC)

BRAIN_H = """#pragma once
namespace coney::ai {
class Brain {
  public:
    /// Thinks.
    /// @orig 0x001490b8 Brain_Think (Brain.cpp)
    int think(int hunger);
};
} // namespace coney::ai
"""

BRAIN_CPP = """#include "ai/brain.h"
namespace coney::ai {
int Brain::think(int hunger) {
    return hunger > 3 ? 1 : 0;
}
} // namespace coney::ai
"""

AI_PAGE = """# AI

## Brain {#brain}

The brain thinks.

### Hunger

Hunger counts up.

## Goals {#goals}

Goals are picked.
"""


def commit_all(repo: Path, text: str) -> None:
    """Commit every change in the checkout."""
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", text)


def put(repo: Path, name: str, text: str) -> None:
    """Write a file of the checkout."""
    (repo / name).parent.mkdir(parents=True, exist_ok=True)
    (repo / name).write_text(text, encoding="utf-8")


@pytest.fixture
def repo(tmp_path: Path) -> Path:
    """A checkout whose branch `impact-demo` changes a tagged function and a page section, adds, renames, deletes."""
    root = tmp_path / "repo"
    root.mkdir()
    git(root, "init", "-q", "-b", "main")
    put(root, "src/ai/brain.h", BRAIN_H)
    put(root, "src/ai/brain.cpp", BRAIN_CPP)
    put(root, "docs/research/ai.md", AI_PAGE)
    put(root, "docs/guides/old-name.md", "# Old\n\nText that stays.\n")
    put(root, "docs/guides/gone.md", "# Gone\n")
    commit_all(root, "base")
    git(root, "checkout", "-q", "-b", "impact-demo")
    put(root, "src/ai/brain.cpp", BRAIN_CPP.replace("hunger > 3 ? 1 : 0", "hunger > kHungerLimit ? 1 : 0"))
    put(root, "docs/research/ai.md", AI_PAGE.replace("Hunger counts up.", "Hunger counts up by 2 each tick."))
    git(root, "mv", "docs/guides/old-name.md", "docs/guides/new-name.md")
    git(root, "rm", "-q", "docs/guides/gone.md")
    put(root, "tools/new_tool.py", "TOOL_LIMIT = 3  # see 0x00322b78\n")
    commit_all(root, "tools: Add a thing\n\nFixes #109\nReplaces #630, #631\n")
    return root


def issue(number: int, body: str, title: str = "some work", labels: tuple[str, ...] = ("task",)) -> dict[str, Any]:
    """An open issue as `gh issue list --json` returns it."""
    return {
        "number": number,
        "title": title,
        "body": body,
        "labels": [{"name": name} for name in labels],
        "url": f"https://example.invalid/issues/{number}",
        "updatedAt": "2026-10-01T00:00:00Z",
    }


def run(
    repo: Path, issues: list[dict[str, Any]], rules: Any = RULES, **kwargs: Any
) -> tuple[list[Any], list[Any], Any]:
    """Gather the branch's facts and match `issues`; the candidates, the excluded and the facts."""
    messages = [git(repo, "log", "--format=%B", "main..impact-demo")]
    touched = issue_impact.gather(repo, "main", "impact-demo", messages, **kwargs)
    touched.branch = "impact-demo"
    candidates, excluded = issue_impact.match_issues(issue_impact_open.parse_issues(issues), touched, rules)
    return candidates, excluded, touched


def by_number(candidates: list[Any]) -> dict[int, Any]:
    """The candidates keyed by issue number."""
    return {candidate.number: candidate for candidate in candidates}


# --- what the branch touches -------------------------------------------------------------------------------------


def test_the_facts_of_a_branch_are_its_paths_addresses_names_and_sections(repo: Path) -> None:
    touched = issue_impact.gather(repo, "main", "impact-demo")
    assert touched.paths["src/ai/brain.cpp"] == "changed"
    assert touched.paths["docs/guides/gone.md"] == "deleted"
    assert touched.paths["tools/new_tool.py"] == "added"
    assert touched.paths["docs/guides/old-name.md"] == "renamed to docs/guides/new-name.md"
    assert touched.paths["docs/guides/new-name.md"] == "renamed from docs/guides/old-name.md"
    # An @orig address is touched when its function's body changed, and one in a changed line is too.
    assert "the @orig of" in touched.addresses["0x001490b8"] and "Brain::think" in touched.addresses["0x001490b8"]
    assert "tools/new_tool.py" in touched.addresses["0x00322b78"]
    assert touched.names["kHungerLimit"] == "src/ai/brain.cpp" and touched.names["TOOL_LIMIT"] == "tools/new_tool.py"
    # The edited line is under `### Hunger`, inside `## Brain` and the page's title: all three are touched, `## Goals`
    # is not.
    assert touched.sections["docs/research/ai.md"] == {"ai", "brain", "hunger"}


def test_the_messages_name_the_issues_it_fixes_and_the_pull_requests_it_replaces(repo: Path) -> None:
    messages = [git(repo, "log", "--format=%B", "main..impact-demo")]
    touched = issue_impact.gather(repo, "main", "impact-demo", messages, "Fixes #7\n", ["old-branch"])
    assert touched.fixes == {109, 7}
    assert touched.replaces == ["#630", "#631", "old-branch"]


def test_a_branch_that_cannot_be_read_is_a_configuration_error(repo: Path) -> None:
    with pytest.raises(ConfigError, match="git merge-base"):
        issue_impact.gather(repo, "no-such-ref", "impact-demo")


def test_a_diff_is_parsed_into_files_with_their_changed_ranges() -> None:
    patch = (
        "diff --git a/a.md b/a.md\nindex 1..2 100644\n--- a/a.md\n+++ b/a.md\n"
        "@@ -3,2 +3,3 @@ ## Heading\n-old line\n+new line\n+another\n"
        "diff --git a/x.txt b/y.txt\nsimilarity index 90%\nrename from x.txt\nrename to y.txt\n"
        "diff --git a/dead.md b/dead.md\ndeleted file mode 100644\n--- a/dead.md\n+++ /dev/null\n@@ -1 +0,0 @@\n-bye\n"
    )
    first, renamed, dead = issue_impact.parse_diff(patch)
    assert first.new == "a.md" and first.ranges == [(3, 5)] and "new line" in first.text
    assert (renamed.old, renamed.new, renamed.status) == ("x.txt", "y.txt", "renamed")
    assert dead.status == "deleted"


def test_the_headings_of_a_page_carry_their_anchors_and_skip_code_fences() -> None:
    page = "# Top\n\n## Brain {#brain}\n\n```sh\n# a comment\n```\n\n### Hunger and food\n"
    assert audit_text.heading_spans(page) == [
        (1, 1, frozenset({"top"})),
        (3, 2, frozenset({"brain"})),
        (9, 3, frozenset({"hunger-and-food"})),
    ]


# --- matching the open issues ----------------------------------------------------------------------------------


def test_each_kind_of_citation_matches_with_its_reason(repo: Path) -> None:
    issues = [
        issue(101, "The thinking is at `0x001490b8` and is wrong."),
        issue(102, "See [the brain](../blob/main/docs/research/ai.md#brain) for the rules."),
        issue(103, "See [goals](../blob/main/docs/research/ai.md#goals) for the goals."),
        issue(104, "The code is in `src/ai/brain.cpp`."),
        issue(105, "Make `Brain::think()` use `kHungerLimit`."),
        issue(106, "Blocked by #630, the old pull request."),
        issue(107, "Work is on the branch impact-demo."),
        issue(108, "Nothing here cites anything this branch touches."),
        issue(110, "Everything under `src/ai/` needs a pass."),
        issue(111, "The page [docs/guides/old-name.md](../blob/main/docs/guides/old-name.md) is the guide."),
    ]
    candidates, _, _ = run(repo, issues)
    found = by_number(candidates)
    assert set(found) == {101, 102, 104, 105, 106, 107, 110, 111}
    assert [(c.number, c.strength) for c in candidates] == [
        (106, "replaced"),
        (107, "branch"),
        (101, "address"),
        (102, "section"),
        (104, "path"),
        (111, "path"),
        (110, "folder"),
        (105, "name"),
    ]
    assert "cites #630, which this change replaces" in found[106].reasons[0].detail
    assert "names this branch (impact-demo)" in found[107].reasons[0].detail
    assert "cites 0x001490b8: the @orig of Brain::think" in found[101].reasons[0].detail
    assert found[102].reasons[0].what == "docs/research/ai.md#brain"
    assert "a section this change edits" in found[102].reasons[0].detail
    assert "which this change modifies" in found[104].reasons[0].detail
    assert "which this change renames" in found[111].reasons[0].detail
    assert "the folder src/ai/, where this change touches" in found[110].reasons[0].detail
    assert [r.what for r in found[105].reasons] == ["Brain::think", "kHungerLimit"]


def test_an_issue_the_change_fixes_is_left_out_and_said_so(repo: Path) -> None:
    candidates, excluded, _ = run(
        repo, [issue(109, "It cites `src/ai/brain.cpp`."), issue(104, "In `src/ai/brain.cpp`.")]
    )
    assert [c.number for c in candidates] == [104]
    assert excluded == [{"number": 109, "why": "this change fixes it (Fixes #N): merging closes it"}]


def test_a_description_and_replaces_option_add_to_the_commit_messages(repo: Path) -> None:
    issues = [issue(7, "Cites `src/ai/brain.cpp`."), issue(8, "Waits on the branch old-launcher.")]
    candidates, excluded, _ = run(repo, issues, description="Fixes #7\n", replaces=["old-launcher"])
    assert [c.number for c in candidates] == [8] and [e["number"] for e in excluded] == [7]


def test_candidates_are_ordered_by_strongest_reason_then_by_how_many_then_by_number(repo: Path) -> None:
    issues = [
        issue(5, "Cites `src/ai/brain.cpp`."),
        issue(3, "Cites `src/ai/brain.cpp` and `kHungerLimit`."),
        issue(4, "Cites `0x001490b8`."),
        issue(2, "Cites `Brain::think`."),
    ]
    candidates, _, _ = run(repo, issues)
    assert [c.number for c in candidates] == [4, 3, 5, 2]


def test_an_address_in_a_changed_line_matches_an_issue_that_cites_it(repo: Path) -> None:
    candidates, _, _ = run(repo, [issue(9, "Look at 0x00322B78 (upper case).")])
    (hit,) = candidates
    assert hit.reasons[0].kind == "address" and "tools/new_tool.py" in hit.reasons[0].detail


def test_a_name_or_path_that_too_many_open_issues_cite_is_skipped(repo: Path) -> None:
    rules = conventions_with(("issues.impact.common_max_issues", 2)).issues.impact
    crowd = [issue(n, "Cites `src/ai/brain.cpp`.") for n in (11, 12, 13)]
    quiet = [issue(14, "Cites `tools/new_tool.py`.")]
    candidates, _, _ = run(repo, [*crowd, *quiet], rules)
    assert [c.number for c in candidates] == [14]
    candidates, _, _ = run(repo, [*crowd[:2], *quiet], rules)
    assert [c.number for c in candidates] == [11, 12, 14]


def test_a_cited_page_with_an_anchor_the_change_did_not_edit_is_not_a_match(repo: Path) -> None:
    candidates, _, _ = run(
        repo, [issue(20, "The rules: docs/research/ai.md#goals"), issue(21, "The page docs/research/ai.md")]
    )
    assert [c.number for c in candidates] == [21]
    assert candidates[0].reasons[0].kind == "path"


def test_a_path_that_only_looks_like_another_is_not_a_match(repo: Path) -> None:
    issues = [issue(30, "See src/ai/brain.cpp.bak and xsrc/ai/brain.cpp and src/ai/brain.cppx.")]
    candidates, _, _ = run(repo, issues)
    assert candidates == []


def test_a_link_with_the_website_prefix_is_read_as_the_path_behind_it(repo: Path) -> None:
    body = "[src/ai/brain.cpp](https://example.invalid/someone/coney/blob/main/src/ai/brain.cpp)"
    candidates, _, _ = run(repo, [issue(31, body)])
    assert [c.number for c in candidates] == [31]


def test_no_issues_and_no_touches_give_no_candidates(repo: Path) -> None:
    assert run(repo, [])[0] == []


def test_the_branch_main_is_never_matched_by_name(repo: Path) -> None:
    touched = issue_impact.gather(repo, "main", "impact-demo")
    touched.branch = "main"
    candidates, _ = issue_impact.match_issues(
        issue_impact_open.parse_issues([issue(40, "Merged into main last week.")]), touched, RULES
    )
    assert candidates == []


# --- the open issues from gh -----------------------------------------------------------------------------------


class FakeGh:
    """A `gh` that answers `issue list` with a fixed list and counts its calls; it can be made to fail."""

    def __init__(self, issues: list[dict[str, Any]], fail: bool = False) -> None:
        self.issues = issues
        self.fail = fail
        self.calls: list[list[str]] = []

    def __call__(self, arguments: list[str]) -> str:
        self.calls.append(arguments)
        if self.fail:
            raise OSError("gh issue list failed: no network")
        return json.dumps(self.issues)


def test_the_issues_are_read_once_and_cached_for_the_configured_time(tmp_path: Path) -> None:
    gh = FakeGh([issue(1, "a")])
    cache = tmp_path / "scratch" / "issue-impact" / "open.json"
    first, _ = issue_impact_open.read_open_issues(cache, RULES, gh, NOW)
    again, _ = issue_impact_open.read_open_issues(cache, RULES, gh, NOW + timedelta(minutes=RULES.cache_minutes - 1))
    assert [i.number for i in first] == [i.number for i in again] == [1] and len(gh.calls) == 1
    assert gh.calls[0][:4] == ["issue", "list", "--state", "open"]
    issue_impact_open.read_open_issues(cache, RULES, gh, NOW + timedelta(minutes=RULES.cache_minutes + 1))
    assert len(gh.calls) == 2
    issue_impact_open.read_open_issues(cache, RULES, gh, NOW + timedelta(minutes=RULES.cache_minutes + 2), refresh=True)
    assert len(gh.calls) == 3


def test_a_failed_read_falls_back_to_an_old_cache_with_a_notice(tmp_path: Path) -> None:
    cache = tmp_path / "open.json"
    issue_impact_open.read_open_issues(cache, RULES, FakeGh([issue(1, "a")]), NOW)
    issues, notices = issue_impact_open.read_open_issues(
        cache, RULES, FakeGh([], fail=True), NOW + timedelta(days=2), refresh=True
    )
    assert [i.number for i in issues] == [1]
    assert len(notices) == 1 and "from the cache of 2026-10-24 12:00 UTC" in notices[0] and "no network" in notices[0]


def test_a_failed_read_with_no_cache_is_a_configuration_error(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="cannot read the open issues"):
        issue_impact_open.read_open_issues(tmp_path / "open.json", RULES, FakeGh([], fail=True), NOW)


def test_a_damaged_cache_is_read_again_and_a_missing_cache_folder_is_made(tmp_path: Path) -> None:
    cache = tmp_path / "deep" / "open.json"
    cache.parent.mkdir()
    cache.write_text("not json", encoding="utf-8")
    gh = FakeGh([issue(2, "b")])
    issues, _ = issue_impact_open.read_open_issues(cache, RULES, gh, NOW)
    assert [i.number for i in issues] == [2] and json.loads(cache.read_text(encoding="utf-8"))["issues"][0][
        "number"
    ] == 2


def test_a_malformed_issue_list_is_a_configuration_error() -> None:
    with pytest.raises(ConfigError, match="not a JSON list"):
        issue_impact_open.parse_issues({"number": 1})
    with pytest.raises(ConfigError, match="malformed"):
        issue_impact_open.parse_issues([{"title": "no number"}])


# --- the reports -----------------------------------------------------------------------------------------------


def test_the_json_report_has_a_stable_shape(repo: Path) -> None:
    candidates, excluded, touched = run(repo, [issue(101, "At `0x001490b8`.", "ai: Fix the brain", ("task", "A-ai"))])
    data = issue_impact_report.report_json(touched, candidates, excluded, ["a notice"])
    assert list(data) == ["version", "base", "head", "branch", "touched", "issues", "excluded", "notices"]
    assert data["version"] == 1 and data["base"] == "main" and data["branch"] == "impact-demo"
    (item,) = data["issues"]
    assert list(item) == ["number", "title", "url", "labels", "strength", "reasons"]
    assert item["number"] == 101 and item["labels"] == ["task", "A-ai"] and item["strength"] == "address"
    assert list(item["reasons"][0]) == ["kind", "what", "detail"]
    assert set(data["touched"]) == {"paths", "addresses", "names", "sections"}
    json.dumps(data)


def test_the_text_report_is_one_line_per_issue_with_the_reasons_cut_to_the_limit(repo: Path) -> None:
    body = "At `0x001490b8`, in `src/ai/brain.cpp`, `Brain::think`, `kHungerLimit` and `TOOL_LIMIT`."
    candidates, excluded, _ = run(repo, [issue(101, body, "ai: Fix the brain"), issue(109, "x")])
    lines = issue_impact_report.report_lines(candidates, excluded, ["a note"], max_reasons=2)
    assert lines[0].startswith("#101  ai: Fix the brain  --  cites 0x001490b8")
    assert lines[0].count(";") == 2 and "and 3 more" in lines[0]
    assert lines[1:] == [
        "1 open issue(s) may be affected",
        "left out: #109 (this change fixes it (Fixes #N): merging closes it)",
        "note: a note",
    ]


# --- the command ------------------------------------------------------------------------------------------------


@pytest.fixture
def at_checkout(repo: Path, tmp_path: Path, monkeypatch: MonkeyPatch) -> Path:
    """The command runs inside the demo checkout, which gets the conventions file (untracked, so no diff)."""
    write_checkout(repo)
    monkeypatch.chdir(repo)
    monkeypatch.setattr(issue_cli, "scratch_folder", lambda root: tmp_path / "scratch")
    return repo


def issues_file(tmp_path: Path, issues: list[dict[str, Any]]) -> str:
    """A `gh issue list --json` file; its path."""
    path = tmp_path / "open.json"
    path.write_text(json.dumps(issues), encoding="utf-8")
    return str(path)


def test_the_command_prints_a_line_per_issue_with_why_it_matched(
    at_checkout: Path, tmp_path: Path, capsys: CaptureFixture[str]
) -> None:
    listed = issues_file(tmp_path, [issue(104, "In `src/ai/brain.cpp`.", "ai: Fix the brain"), issue(109, "x")])
    assert cli.main(["issue", "impact", "--base", "main", "--issues", listed]) == 0
    out = capsys.readouterr().out.splitlines()
    assert out[0] == "#104  ai: Fix the brain  --  cites src/ai/brain.cpp, which this change modifies"
    assert out[1] == "1 open issue(s) may be affected"
    assert "left out: #109" in out[2]


def test_the_json_output_is_the_report(at_checkout: Path, tmp_path: Path, capsys: CaptureFixture[str]) -> None:
    listed = issues_file(tmp_path, [issue(101, "At `0x001490b8`.")])
    assert cli.main(["issue", "impact", "--base", "main", "--issues", listed, "--json"]) == 0
    data = json.loads(capsys.readouterr().out)
    assert data["version"] == 1 and data["branch"] == "impact-demo" and data["head"] == "HEAD"
    assert [item["number"] for item in data["issues"]] == [101]


def test_a_description_and_replaces_options_shape_the_list(
    at_checkout: Path, tmp_path: Path, capsys: CaptureFixture[str]
) -> None:
    text = tmp_path / "PR.md"
    text.write_text("## Summary\n\nFixes #7\n", encoding="utf-8")
    listed = issues_file(
        tmp_path, [issue(7, "In `src/ai/brain.cpp`."), issue(8, "Waits on #44."), issue(9, "On old-x.")]
    )
    args = ["issue", "impact", "--base", "main", "--issues", listed, "--description", str(text)]
    assert cli.main([*args, "--replaces", "44", "--replaces", "old-x"]) == 0
    out = capsys.readouterr().out
    assert "#8  some work  --  cites #44, which this change replaces" in out
    assert "#9  some work  --  names the branch old-x, which this change replaces" in out
    assert "left out: #7" in out


def test_the_default_base_is_the_default_branch_on_origin() -> None:
    args = cli._build_parser().parse_args(["issue", "impact"])
    assert args.base is None and args.head == "HEAD" and args.replaces == [] and not args.json


def test_the_command_reads_github_through_the_cache(at_checkout: Path, tmp_path: Path) -> None:
    gh = FakeGh([issue(104, "In `src/ai/brain.cpp`.")])
    args = cli._build_parser().parse_args(["issue", "impact", "--base", "main", "--json"])
    assert issue_cli.run_impact(args, gh, lambda: NOW) == 0
    assert issue_cli.run_impact(args, gh, lambda: NOW) == 0
    assert len(gh.calls) == 1 and (tmp_path / "scratch" / "issue-impact" / "open-issues.json").is_file()
    refresh = cli._build_parser().parse_args(["issue", "impact", "--base", "main", "--refresh"])
    assert issue_cli.run_impact(refresh, gh, lambda: NOW) == 0
    assert len(gh.calls) == 2


@pytest.mark.parametrize(
    ("extra", "message"),
    [
        (["--base", "no-such-ref"], "git merge-base"),
        (["--base", "main", "--issues", "nowhere.json"], "--issues nowhere.json: cannot be read"),
        (["--base", "main", "--description", "nowhere.md"], "--description nowhere.md: cannot be read"),
    ],
)
def test_unreadable_inputs_are_a_usage_error(
    at_checkout: Path, capsys: CaptureFixture[str], extra: list[str], message: str
) -> None:
    assert cli.main(["issue", "impact", *extra]) == 2
    assert message in capsys.readouterr().err


def test_an_issue_citing_a_section_the_change_removed_or_renamed_still_matches(repo: Path) -> None:
    page = (repo / "docs/research/ai.md").read_text(encoding="utf-8")
    renamed = page.replace("## Goals {#goals}", "## Aims {#aims}").replace(
        "### Hunger\n\nHunger counts", "Hunger counts"
    )
    put(repo, "docs/research/ai.md", renamed)
    commit_all(repo, "docs: Rename a heading")
    issues = [
        issue(30, "Goals are in docs/research/ai.md#goals"),
        issue(31, "Hunger is in docs/research/ai.md#hunger"),
        issue(32, "The new name is docs/research/ai.md#aims"),
        issue(33, "Untouched: docs/research/ai.md#nothing-like-it"),
    ]
    candidates, _, touched = run(repo, issues)
    assert {"goals", "aims", "hunger"} <= touched.sections["docs/research/ai.md"]
    found = by_number(candidates)
    assert set(found) == {30, 31, 32}
    assert found[30].reasons[0].kind == "section" and found[31].reasons[0].kind == "section"


def test_names_and_addresses_only_in_a_changed_test_file_are_not_touched(repo: Path) -> None:
    put(repo, "tests/test_thing.py", "kOnlyInTest = 0x00999999\n")
    commit_all(repo, "tests: Add a test")
    touched = issue_impact.gather(repo, "main", "impact-demo")
    assert touched.paths["tests/test_thing.py"] == "added"
    assert "kOnlyInTest" not in touched.names and "0x00999999" not in touched.addresses
    candidates, _, _ = run(
        repo, [issue(40, "Mentions `kOnlyInTest` at 0x00999999."), issue(41, "The file tests/test_thing.py is odd.")]
    )
    assert [c.number for c in candidates] == [41]


def test_a_reply_that_is_not_a_list_is_refused_and_never_cached(tmp_path: Path) -> None:
    cache = tmp_path / "open.json"
    bad = FakeGh([])
    bad.issues = {"message": "rate limited"}  # type: ignore[assignment]
    with pytest.raises(ConfigError, match="not a JSON list"):
        issue_impact_open.read_open_issues(cache, RULES, bad, NOW)
    assert not cache.exists()
    good = FakeGh([issue(1, "a")])
    issues, _ = issue_impact_open.read_open_issues(cache, RULES, good, NOW)
    assert [i.number for i in issues] == [1] and len(good.calls) == 1


@pytest.mark.parametrize(
    "stored",
    [
        {"read": "2026-10-24T12:00:00", "issues": [{"number": 1}]},  # no time zone
        {"read": "yesterday", "issues": []},
        {"read": "2026-10-24T12:00:00+00:00", "issues": {"not": "a list"}},
        {"read": "2026-10-24T12:00:00+00:00", "issues": [{"title": "no number"}]},
        ["not", "an", "object"],
    ],
)
def test_bad_cache_data_is_a_cache_miss_not_a_crash(tmp_path: Path, stored: Any) -> None:
    cache = tmp_path / "open.json"
    cache.write_text(json.dumps(stored), encoding="utf-8")
    gh = FakeGh([issue(2, "b")])
    issues, notices = issue_impact_open.read_open_issues(cache, RULES, gh, NOW)
    assert [i.number for i in issues] == [2] and notices == [] and len(gh.calls) == 1
    # With gh down as well, the bad cache is no fallback either.
    cache.write_text(json.dumps(stored), encoding="utf-8")
    with pytest.raises(ConfigError, match="cannot read the open issues"):
        issue_impact_open.read_open_issues(cache, RULES, FakeGh([], fail=True), NOW)


def test_the_address_of_a_function_that_only_moved_is_still_touched(repo: Path) -> None:
    """A moved function keeps its compare, but an issue that cites its address is still about code that moved."""
    git(repo, "checkout", "-q", "main")
    git(repo, "checkout", "-q", "-b", "moved-demo")
    for name in ("src/ai/brain.h", "src/ai/brain.cpp"):
        text = (repo / name).read_text(encoding="utf-8")
        put(repo, name, text.replace("class Brain", "class Mind").replace("Brain::", "Mind::"))
    commit_all(repo, "core: Rework the brain's name")
    touched = issue_impact.gather(repo, "main", "moved-demo")
    assert "Mind::think" in touched.addresses["0x001490b8"] and "moved" in touched.addresses["0x001490b8"]
