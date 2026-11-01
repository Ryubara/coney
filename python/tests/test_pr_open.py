# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools pr open` (docs/guides/pr.md#open): the whole release against a real local bare remote and a
real worktree, with `gh` and the claims faked. The units are the made-up ones of `pr_support`."""

import json
import shutil
import subprocess
from collections.abc import Sequence
from datetime import UTC, datetime
from pathlib import Path

import pytest
import yaml
from conventions_support import changed
from git_support import git
from pr_open_support import OPENER, PNG, REPO, Open, make_open, picture, show
from pr_support import UNVERIFIED, make

from coney_tools import conventions, identity, pr_open, pr_open_unit, pr_proof, pr_publish, pr_ready, unit_record
from coney_tools.cli import main
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.issue_claim_remote import GhRemote
from coney_tools.task_file import TaskFile, render_task_file


def docs_unit(tmp_path: Path, shape: str = "docs") -> Open:
    """A released-ready unit of the given shape (`docs` by default: fast, no `@orig` function)."""
    return make_open(make(tmp_path, shape))


# --- 21: opening ---------------------------------------------------------------------------------------------------


def test_opens_a_pull_request_with_everything_the_design_lists(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    picture(built, "after.png")
    show(built, "after.png")
    tip, commits = built.tip, git(built.root, "rev-list", "--count", "HEAD")
    assert opened.run() == 0, opened.text()
    # pushed
    assert opened.origin_tip() == tip
    # created with the exact title, body file and one --attach per picture, from the task's folder
    [create] = [call for call in opened.gh.calls if call[0][:2] == ["pr", "create"]]
    title = (built.unit.dir / "PR-TITLE.txt").read_text(encoding="utf-8").strip()
    assert create == (
        ["pr", "create", "--repo", REPO, "--base", "main", "--head", "work", "--title", title,
         "--body-file", "PR-BODY.md", "--attach", "./shots/after.png"],
        built.unit.dir,
    )  # fmt: skip
    body = (built.unit.dir / "PR-BODY.md").read_text(encoding="utf-8")
    assert "![after.png](./shots/after.png)" in body and "<!-- coney:facts -->" in body
    # auto-merge on, with the repository's method
    assert ["pr", "merge", "812", "--repo", REPO, "--squash", "--auto"] in [args for args, _ in opened.gh.calls]
    # /progress pr-opened on the issue
    assert [c.body for c in opened.github.comment_list[757] if c.body == "/progress pr-opened"]
    # what it published
    published = json.loads((built.unit.dir / pr_publish.PUBLISHED_FILE).read_text(encoding="utf-8"))
    assert published["number"] == 812 and published["tip"] == tip and published["engineer"]["id"] == "engineer-1"
    assert len(published["images"]) == 1 and published["progress_posted"] is True
    # one stamp, bound to the change; no commit made
    [stamp] = opened.opened_stamps()
    assert stamp.commit == tip and stamp.covers["tip"] == tip and stamp.covers["diff_sha256"] == built.diff
    assert stamp.covers["base"] == built.base and len(stamp.covers["facts_sha256"]) == 64
    assert stamp.detail["opener"] == OPENER and stamp.detail["issues"] == [757] and stamp.detail["profile"] == "docs"
    assert stamp.detail["rebased_from"] == "" and stamp.detail["fix"] == "" and stamp.detail["kind"] == "docs"
    assert (
        git(built.root, "rev-parse", "HEAD").strip() == tip
        and git(built.root, "rev-list", "--count", "HEAD") == commits
    )
    assert opened.said[-1].startswith(
        "opened #812 https://github.com/acme/widgets/pull/812 (task work, engineer engineer-1/"
    )
    assert opened.said[-1].endswith("0 tests, 0 functions, auto-merge on)")


def test_a_feature_with_an_orig_function_opens_too(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path, "feature")
    assert opened.run() == 0, opened.text()
    assert opened.said[-1].endswith("1 tests, 1 functions, auto-merge on)")
    assert opened.opened_stamps()[0].detail["profile"] == "full"


@pytest.mark.parametrize("shape", ["docs", "tooling"])
def test_a_docs_task_and_a_tooling_task_open_with_their_profile_and_no_build_record(tmp_path: Path, shape: str) -> None:
    opened = docs_unit(tmp_path, shape)
    assert opened.run() == 0, opened.text()
    [stamp] = opened.opened_stamps()
    assert stamp.detail["profile"] == shape
    fails_first = [s for s in opened.log.stamps if s.step == "fails-first"]
    assert [s.result for s in fails_first] == ["none"]
    assert not (opened.built.unit.dir / "build-runs").exists()


# --- 22, 23: every step can be run twice ----------------------------------------------------------------------------


def test_a_rerun_after_a_failed_push_appends_no_second_stamp(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    opened.fail_push = 1
    assert opened.run() == 4
    assert "push: " in opened.text() and opened.gh.writes() == [] and len(opened.opened_stamps()) == 1
    assert opened.run() == 0, opened.text()
    assert len(opened.opened_stamps()) == 1 and opened.origin_tip() == opened.built.tip


def test_a_rerun_after_gh_create_failed_does_not_open_a_second_pull_request(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    opened.gh.create_then_fail = True
    assert opened.run() == 4
    assert "gh: " in opened.text() and "run pr open again" in opened.text()
    assert opened.run() == 0, opened.text()
    assert opened.gh.names().count("pr create") == 1 and list(opened.gh.prs) == [812]
    assert "pr edit" in opened.gh.names() and opened.gh.prs[812]["auto"] is True
    assert len(opened.opened_stamps()) == 1


def test_a_run_that_changes_nothing_since_the_last_one_appends_no_stamp(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    assert opened.run() == 0
    assert opened.run() == 0, opened.text()
    assert len(opened.opened_stamps()) == 1 and opened.said[-1].startswith("updated #812")


# --- 24: the lease ---------------------------------------------------------------------------------------------------


def test_the_push_fails_when_the_remote_branch_moved_and_never_forces(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    assert opened.run() == 0
    old = built.tip
    other = tmp_path / "someone"
    subprocess.run(["git", "clone", "-q", "-b", "work", str(built.origin), str(other)], check=True)
    (other / "extra.md").write_text("theirs\n", encoding="utf-8")
    git(other, "add", "-A")
    git(other, "commit", "-q", "-m", "a maintainer edits the branch")
    git(other, "push", "-q", "origin", "work")
    theirs = git(other, "rev-parse", "HEAD").strip()
    built.edit("docs/guides/widget.md", "# The widget\n\nIt waits longer.\n")
    built.check("docs")
    built.review(functions={})
    opened.said.clear()
    opened.git_calls.clear()
    assert opened.run() == 4
    assert f"push: origin/work moved to {theirs[:12]} (the last pr open left it at {old[:12]})" in opened.text()
    assert opened.origin_tip() == theirs
    pushes = [args for args in opened.git_calls if args[0] == "push"]
    assert pushes and all(args[1] == f"--force-with-lease=work:{old}" for args in pushes)
    assert not any(word in ("--force", "-f", "--force-with-lease") for args in pushes for word in args)


def test_a_branch_the_remote_has_but_no_run_pushed_is_not_overwritten(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    other = built.root.parent.parent / "someone"
    subprocess.run(["git", "clone", "-q", str(built.origin), str(other)], check=True)
    git(other, "checkout", "-q", "-b", "work")
    (other / "theirs.md").write_text("x\n", encoding="utf-8")
    git(other, "add", "-A")
    git(other, "commit", "-q", "-m", "theirs")
    git(other, "push", "-q", "origin", "work")
    assert opened.run() == 4
    assert "no earlier pr open pushed it" in opened.text() and opened.gh.writes() == []


# --- 25, 26: an existing pull request -------------------------------------------------------------------------------


def test_an_existing_open_pull_request_is_edited_and_unchanged_pictures_are_not_uploaded_again(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    picture(built, "after.png")
    show(built, "after.png")
    assert opened.run() == 0, opened.text()
    picture(built, "before.png", PNG + b"\x01")
    show(built, "after.png", "before.png")
    opened.gh.calls.clear()
    assert opened.run() == 0, opened.text()
    assert "pr create" not in opened.gh.names()
    [edit] = [args for args, _ in opened.gh.calls if args[:2] == ["pr", "edit"]]
    assert edit[edit.index("--attach") :] == ["--attach", "./shots/before.png"]
    assert edit[edit.index("--body-file") + 1] == "PR-BODY-upload.md"
    body = opened.gh.prs[812]["body"]
    assert body.count("https://example.test/assets/") == 2 and "./shots/" not in body
    assert len(json.loads((built.unit.dir / pr_publish.PUBLISHED_FILE).read_text(encoding="utf-8"))["images"]) == 2
    assert opened.said[-1].startswith("updated #812")


def test_a_merged_pull_request_of_the_branch_is_refused(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    assert opened.run() == 0
    opened.gh.prs[812]["merged"] = True
    opened.gh.calls.clear()
    assert opened.run() == 1
    assert "pr open: #812 already merged this branch; new work is a new task" in opened.text()
    assert opened.gh.writes() == []


def test_a_closed_pull_request_needs_reopen(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    assert opened.run() == 0
    opened.gh.prs[812]["state"] = "CLOSED"
    opened.gh.calls.clear()
    assert opened.run() == 1
    assert "pr open: #812 was closed; --reopen to continue it" in opened.text() and opened.gh.writes() == []
    assert opened.run(reopen=True) == 0, opened.text()
    assert "pr reopen" in opened.gh.names() and opened.gh.prs[812]["state"] == "OPEN"


# --- 27, 28: the rebase ----------------------------------------------------------------------------------------------


def test_a_clean_rebase_keeps_check_and_review_and_records_where_it_started(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    old, diff = built.tip, built.diff
    opened.advance_main("docs/guides/other.md", "# Other\n")
    assert opened.run() == 0, opened.text()
    new = git(built.root, "rev-parse", "HEAD").strip()
    assert new != old and opened.origin_tip() == new
    [stamp] = opened.opened_stamps()
    assert stamp.covers["tip"] == new and stamp.covers["diff_sha256"] == diff and stamp.detail["rebased_from"] == old
    assert git(built.root, "rev-list", "--count", "origin/main..HEAD").strip() == "1"  # no commit of ours added
    assert f"rebased onto origin/main: {old[:12]} -> {new[:12]}" in opened.text()


def test_a_rebase_that_stops_with_no_conflict_says_what_git_said(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    old = built.tip
    opened.advance_main("docs/guides/other.md", "# Other\n")
    # a machine with no committer identity anywhere: the rebase starts, then cannot write its first commit
    for key in ("core.autocrlf", "core.eol", "core.safecrlf"):  # the line endings stay as the checkout was made
        kept = subprocess.run(["git", "-C", str(built.root), "config", "--get", key], capture_output=True, text=True)
        if kept.returncode == 0:
            git(built.root, "config", key, kept.stdout.strip())
    empty = tmp_path / "empty.gitconfig"
    empty.write_text("", encoding="utf-8")
    monkeypatch.setenv("GIT_CONFIG_GLOBAL", str(empty))
    monkeypatch.setenv("GIT_CONFIG_NOSYSTEM", "1")
    for name in ("GIT_AUTHOR_NAME", "GIT_AUTHOR_EMAIL", "GIT_COMMITTER_NAME", "GIT_COMMITTER_EMAIL", "EMAIL"):
        monkeypatch.delenv(name, raising=False)
    git(built.root, "config", "--unset", "user.name")
    git(built.root, "config", "--unset", "user.email")
    git(built.root, "config", "user.useConfigOnly", "true")
    assert opened.run() == 4, opened.text()
    text = opened.text()
    assert "git rebase origin/main stopped with no conflict: " in text and "aborted, the branch is as it was" in text
    assert "conflicts at" not in text and "user.email" in text
    assert git(built.root, "rev-parse", "HEAD").strip() == old and opened.gh.writes() == []
    assert not (built.root / ".git" / "rebase-merge").exists()
    assert not (built.unit.dir / pr_publish.REBASE_FILE).exists()


def tree_bytes(root: Path) -> dict[Path, bytes]:
    """Every file of the worktree with its bytes, line endings made LF (git may write either on a checkout)."""
    crlf, lf = bytes([13, 10]), bytes([10])
    return {p: p.read_bytes().replace(crlf, lf) for p in root.rglob("*") if p.is_file() and ".git" not in p.parts}


def test_a_rebase_conflict_leaves_the_worktree_as_it_was_writes_rebase_md_and_exits_3(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    opened.advance_main("docs/guides/widget.md", "# Theirs\n")
    before = opened.snapshot()
    files = tree_bytes(built.root)
    assert opened.run() == 3
    # a real run leaves REBASE.md and its lock in the task's folder; nothing else of the unit changes
    after = opened.snapshot()
    assert after.pop("folder").split() == sorted([*before.pop("folder").split(), "REBASE.md", "pr-open.lock"])
    assert after == before
    assert tree_bytes(built.root) == files
    rebase = (built.unit.dir / "REBASE.md").read_text(encoding="utf-8")
    assert "- `docs/guides/widget.md`" in rebase and "The branch is as it was" in rebase
    assert "docs/guides/widget.md" in opened.text() and "REBASE.md" in opened.text()
    assert opened.gh.writes() == [] and opened.opened_stamps() == []
    assert git(built.origin, "for-each-ref", "refs/heads/work").strip() == ""


def test_a_change_the_stamps_no_longer_cover_is_reported_with_every_stale_stage_and_nothing_happens(
    tmp_path: Path,
) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    built.edit("docs/guides/widget.md", "# The widget\n\nIt waits, and more.\n")
    assert opened.run() == 1
    text = opened.text()
    assert "checked: stale" in text and "reviewed: stale" in text and "redo: task check" in text
    assert opened.gh.writes() == [] and opened.opened_stamps() == []
    assert git(built.origin, "for-each-ref", "refs/heads/work").strip() == ""


def test_a_rebase_that_changes_the_branchs_own_diff_is_reported_after_it(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    built.edit("docs/guides/a.md", "# A\n")
    built.edit("docs/guides/b.md", "# B\n", "docs: b")
    built.check("docs")
    built.review(functions={})
    opened.advance_main("docs/guides/b.md", "# B\n")  # main already has one of the branch's files
    assert opened.run() == 1
    text = opened.text()
    assert "rebased onto origin/main" in text and "checked: stale" in text and "reviewed: stale" in text
    assert opened.gh.writes() == [] and opened.opened_stamps() == []
    assert git(built.origin, "for-each-ref", "refs/heads/work").strip() == ""


# --- 29: --dry-run --------------------------------------------------------------------------------------------------


def test_a_dry_run_changes_nothing(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    opened.advance_main("docs/guides/other.md", "# Other\n")
    before = opened.snapshot()
    comments = len(opened.github.comment_list[757])
    assert opened.run(dry_run=True) == 0, opened.text()
    assert opened.snapshot() == before
    assert opened.gh.writes() == [] and len(opened.github.comment_list[757]) == comments
    assert "dry run: rebase onto origin/main: rebased" in opened.text()
    assert "dry run: would open the pull request with 0 picture(s)" in opened.text()
    assert not [p for p in git(opened.built.root, "worktree", "list").splitlines() if "coney-pr-open" in p]


def test_a_dry_run_reports_a_conflict_and_a_refusal_as_a_real_run_would(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    opened.advance_main("docs/guides/widget.md", "# Theirs\n")
    before = opened.snapshot()
    assert opened.run(dry_run=True) == 3
    assert opened.snapshot() == before and opened.gh.writes() == []
    stale = docs_unit(tmp_path / "stale")
    stale.built.edit("docs/guides/widget.md", "# The widget\n\nChanged.\n")
    assert stale.run(dry_run=True) == 1 and "checked: stale" in stale.text()


# --- 30: the caller --------------------------------------------------------------------------------------------------


@pytest.mark.parametrize("kind", ["engineer", "reviewer", "tooling"])
def test_an_agent_that_is_not_the_coordinator_is_refused_before_anything_is_read(tmp_path: Path, kind: str) -> None:
    opened = docs_unit(tmp_path)
    opened.who = identity.Identity("a1", kind, "claude-opus-5-5", verified=True)
    assert opened.run() == 2
    assert "pr open: only the coordinator opens pull requests; ask it" in opened.text()
    assert opened.gh.calls == [] and opened.git_calls == []


@pytest.mark.parametrize(
    "who", [identity.Identity("c", "coordinator", "claude-opus-5-5", verified=True), identity.Identity()]
)
def test_the_coordinator_and_a_person_at_the_machine_may_open(tmp_path: Path, who: identity.Identity) -> None:
    opened = docs_unit(tmp_path)
    opened.who = who
    assert opened.run() == 0, opened.text()


# --- 31: a fix task --------------------------------------------------------------------------------------------------


def start_fix(opened: Open, record: bool = True) -> None:
    """The fix task `work-fix` of the parent `work`: its task file and, unless `record` is off (a fix made by hand, as
    older ones were), its unit record."""
    built = opened.built
    folder = built.unit.dir.parent / "work-fix"
    folder.mkdir()
    task = TaskFile(
        task="work-fix",
        issues=(757,),
        kind="fix",
        claimant=OPENER,
        claimed_at=datetime(2026, 10, 23, 9, 0, 0, tzinfo=UTC),
        sections={"Done": "- The failing check passes."},
        parent="work",
    )
    (folder / "TASK.md").write_text(render_task_file(task), encoding="utf-8")
    if not record:
        return
    unit_record.write_record(
        folder / built.conventions.tasks.start.unit_file,
        unit_record.UnitRecord(
            task="work-fix",
            kind="fix",
            issues=(757,),
            claimant=OPENER,
            branch="work",
            worktree=built.root,
            base_ref="origin/main",
            base_commit=built.base,
            started=datetime(2026, 10, 23, 10, 0, 0, tzinfo=UTC),
            parent="work",
        ),
    )


def test_a_fix_task_is_released_onto_its_parents_pull_request_and_appends_to_the_parents_log(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    assert opened.run() == 0
    start_fix(opened)
    built.edit("docs/guides/widget.md", "# The widget\n\nIt waits for the frame.\n", "docs: fix")
    built.check("docs")
    built.review(functions={})
    opened.gh.calls.clear()
    assert opened.run(task="work-fix") == 0, opened.text()
    assert "pr create" not in opened.gh.names() and "pr edit" in opened.gh.names()
    assert opened.said[-1].startswith("updated #812")
    assert any(line.startswith("  fix touches 1 file since the last release") for line in opened.said)
    first, second = opened.opened_stamps()
    assert first.detail["fix"] == "" and second.detail["fix"] == "work-fix" and second.task == "work"
    assert second.covers["tip"] == built.tip and opened.origin_tip() == built.tip


def test_a_fix_task_with_no_unit_record_is_released_through_its_parent_s_record(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    assert opened.run() == 0
    start_fix(opened, record=False)
    built.edit("docs/guides/widget.md", "# The widget\n\nIt waits for the frame.\n", "docs: fix")
    built.check("docs")
    built.review(functions={})
    opened.gh.calls.clear()
    assert opened.run(task="work-fix") == 0, opened.text()
    assert "pr create" not in opened.gh.names() and "pr edit" in opened.gh.names()
    first, second = opened.opened_stamps()
    assert first.detail["fix"] == "" and second.detail["fix"] == "work-fix" and second.task == "work"


def test_a_fix_task_with_no_record_whose_parent_has_none_either_is_refused(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    assert opened.run() == 0
    start_fix(opened, record=False)
    (opened.built.unit.dir / opened.built.conventions.tasks.start.unit_file).unlink()
    assert opened.run(task="work-fix") == 2
    assert "work-fix: no unit worktree" in opened.text()


def test_a_fix_whose_parent_pull_request_is_merged_or_missing_is_refused(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    assert opened.run() == 0
    start_fix(opened)
    opened.gh.prs[812]["merged"] = True
    assert opened.run(task="work-fix") == 1
    assert "work-fix has no open pull request to fix (#812 is merged)" in opened.text()
    opened.gh.prs.clear()
    assert opened.run(task="work-fix") == 1
    assert "work-fix has no open pull request to fix (the branch has none)" in opened.text()


def test_a_fix_whose_parent_pull_request_is_closed_is_refused_and_not_reopened(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    assert opened.run() == 0
    start_fix(opened)
    opened.gh.prs[812]["state"] = "CLOSED"
    opened.gh.calls.clear()
    assert opened.run(task="work-fix") == 1
    assert "work-fix has no open pull request to fix (#812 is closed)" in opened.text()
    assert opened.gh.writes() == [] and opened.gh.prs[812]["state"] == "CLOSED"


# --- 33: the read-back -----------------------------------------------------------------------------------------------


def test_a_description_that_comes_back_with_a_local_path_is_repaired_once(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    opened.gh.corrupt_body = 1
    assert opened.run() == 0, opened.text()
    assert "the description on GitHub is wrong" in opened.text() and opened.gh.names().count("pr edit") == 1
    assert "C:\\Users" not in opened.gh.prs[812]["body"]


def test_a_description_that_stays_wrong_after_the_repair_exits_4_with_the_pull_requests_address(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    opened.gh.corrupt_body = 99
    assert opened.run() == 4
    assert "read-back: https://github.com/acme/widgets/pull/812 is wrong" in opened.text()
    assert opened.gh.names().count("pr edit") == 1 and opened.gh.names().count("pr create") == 1
    opened.gh.corrupt_body = 0
    assert opened.run() == 0, opened.text()  # rerunning repairs it in place
    assert opened.gh.names().count("pr create") == 1


# --- 34: the repository's name ---------------------------------------------------------------------------------------


def test_the_repository_comes_from_gh_and_is_written_to_no_file_but_in_the_addresses_gh_printed(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    assert opened.run() == 0
    repos = {args[args.index("--repo") + 1] for args, _ in opened.gh.calls if "--repo" in args}
    assert repos == {REPO}
    # the fake prints addresses that carry the name, as gh's do: they may be remembered, nothing else may hold it
    folders = [opened.built.unit.dir, opened.built.root]
    for folder in folders:
        for path in folder.rglob("*"):
            if not path.is_file() or ".git" in path.parts:
                continue
            text = path.read_bytes().decode("utf-8", errors="replace")
            if path.name == pr_publish.PUBLISHED_FILE:
                data = json.loads(text)
                assert REPO in data["url"]
                data["url"] = ""
                text = json.dumps(data)
            assert REPO not in text, path.name
    assert REPO not in "".join(" ".join(args) for args in opened.git_calls)


def test_a_fork_opens_in_its_parent_and_an_explicit_repository_wins(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    opened.gh.parent = "up/widgets"
    assert opened.run() == 0
    assert "repo view" in opened.gh.names()
    assert {args[args.index("--repo") + 1] for args, _ in opened.gh.calls if "--repo" in args} == {"up/widgets"}
    other = docs_unit(tmp_path / "second")
    assert other.run(repo="chosen/place") == 0
    assert "repo view" not in other.gh.names()
    assert {args[args.index("--repo") + 1] for args, _ in other.gh.calls if "--repo" in args} == {"chosen/place"}


# --- auto-merge ------------------------------------------------------------------------------------------------------


def test_no_auto_merge_leaves_it_off(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    assert opened.run(no_auto_merge=True) == 0
    assert "pr merge" not in opened.gh.names() and opened.said[-1].endswith("auto-merge off)")


def test_auto_merge_is_left_off_for_an_account_that_may_not_turn_it_on(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    opened.github.permissions = {OPENER: "read"}
    assert opened.run() == 0
    assert "auto-merge left off" in opened.text() and "pr merge" not in opened.gh.names()


# --- the preconditions -----------------------------------------------------------------------------------------------


def test_a_gh_that_is_too_old_or_not_logged_in_stops_the_run(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    opened.gh.version = "2.97.0"
    assert opened.run() == 2 and "gh 2.99 or newer is needed (--attach); found 2.97.0" in opened.text()
    opened.gh.version, opened.gh.logged_in = "2.102.0", False
    assert opened.run() == 2 and "gh is not ready" in opened.text()
    assert opened.git_calls == []


def test_a_task_that_was_never_started_has_no_unit(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    (opened.built.unit.dir / opened.built.conventions.tasks.start.unit_file).unlink()
    assert opened.run() == 2 and "pr open: work: no unit worktree (run task start)" in opened.text()


def test_uncommitted_changes_stop_the_run_before_the_checks(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    (opened.built.root / "docs" / "guides" / "widget.md").write_text("edited\n", encoding="utf-8")
    assert opened.run() == 2
    assert (
        "the worktree has uncommitted changes: docs/guides/widget.md; the engineer commits or removes them"
        in opened.text()
    )


def test_a_key_the_branch_adds_to_the_conventions_does_not_stop_an_older_tool(tmp_path: Path) -> None:
    """The first release of a branch that adds a rule is read by the tool main already has, which does not know the
    key: it is skipped and said once, and the run goes on past the unit lookup into the proof, which refuses the
    Check and Review made before the file changed (the commit that adds the key), not the key."""
    opened = docs_unit(tmp_path)
    path = opened.built.root / conventions.CONVENTIONS_FILE
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    data["claims"]["a_later_rule"] = 3
    opened.built.edit(conventions.CONVENTIONS_FILE, yaml.safe_dump(data), "add a rule")
    assert opened.run() == 1
    text = opened.text()
    assert "unknown key" not in text
    assert text.count("ignoring claims.a_later_rule") == 1
    assert "uncommitted changes" not in text and "stale" in text, text


def test_a_detached_head_is_not_a_unit_worktree(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    git(opened.built.root, "checkout", "-q", "--detach")
    assert opened.run() == 2 and "detached HEAD" in opened.text()


def test_a_claim_the_opener_does_not_hold_is_refused(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    opened.github.actor = "bob"
    assert opened.run() == 1
    assert f"#757 is claimed by @{OPENER}, not @bob" in opened.text() and opened.gh.writes() == []
    unclaimed = make_open(make(tmp_path / "second"))
    unclaimed.github.comment_list[757].clear()
    assert unclaimed.run() == 1 and "the claim on #757 was released or never made" in unclaimed.text()


def test_a_run_of_the_same_task_already_going_stops_this_one(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    with pr_open_unit.exclusive(opened.built.unit.dir / "pr-open.lock", "work"):
        assert opened.run() == 2
    assert "another pr open of work is running" in opened.text()
    assert opened.run() == 0, opened.text()  # the lock goes with the run that held it


def test_a_unit_that_was_not_reviewed_is_refused_with_the_stage_to_redo(tmp_path: Path) -> None:
    opened = make_open(make(tmp_path, "docs", skip=frozenset({"reviewed"})))
    assert opened.run() == 1
    assert "reviewed: not reviewed" in opened.text() and "redo: task review" in opened.text()
    assert opened.gh.writes() == [] and git(opened.built.origin, "for-each-ref", "refs/heads/work").strip() == ""


def test_a_problem_in_pr_md_is_listed_with_the_text_stage(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    path = opened.built.unit.dir / "PR.md"
    path.write_text(path.read_text(encoding="utf-8") + "\n## Checklist\n\n- [x] done\n", encoding="utf-8")
    assert opened.run() == 1
    assert 'text: PR.md: "## Checklist" is generated; remove it' in opened.text()


# --- 35: exit codes and --json through main --------------------------------------------------------------------------


def run_main(opened: Open, monkeypatch: pytest.MonkeyPatch, *args: str) -> int:
    """`coney-tools pr open ...` from the unit's checkout, with the made-up machine."""
    monkeypatch.setattr(pr_open, "default_env", opened.env)
    monkeypatch.chdir(opened.built.root)
    return main(["pr", "open", *args])


def test_the_exit_codes_through_the_command_line(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    opened = docs_unit(tmp_path / "ok")
    assert run_main(opened, monkeypatch, "work") == 0
    assert run_main(docs_unit(tmp_path / "stale-unit"), monkeypatch, "work", "--dry-run") == 0
    stale = docs_unit(tmp_path / "stale")
    stale.built.edit("docs/guides/widget.md", "# Changed\n")
    assert run_main(stale, monkeypatch, "work") == 1
    nounit = docs_unit(tmp_path / "nounit")
    (nounit.built.unit.dir / nounit.built.conventions.tasks.start.unit_file).unlink()
    assert run_main(nounit, monkeypatch, "work") == 2
    conflict = docs_unit(tmp_path / "conflict")
    conflict.advance_main("docs/guides/widget.md", "# Theirs\n")
    assert run_main(conflict, monkeypatch, "work") == 3
    broken = docs_unit(tmp_path / "broken")
    broken.fail_push = 1
    assert run_main(broken, monkeypatch, "work") == 4


def test_json_prints_one_object_for_a_success_and_for_a_refusal(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    opened = docs_unit(tmp_path / "ok")
    printed = opened.said
    assert run_main(opened, monkeypatch, "work", "--json") == 0
    data = json.loads(printed[-1])
    assert data["ok"] is True and data["exit"] == 0 and data["task"] == "work" and data["refusals"] == []
    assert data["pr"] == {"number": 812, "url": "https://github.com/acme/widgets/pull/812", "updated": False}
    assert data["tip"] == opened.built.tip and data["auto_merge"] is True and isinstance(data["log"], list)
    stale = docs_unit(tmp_path / "stale")
    stale.built.edit("docs/guides/widget.md", "# Changed\n")
    assert run_main(stale, monkeypatch, "work", "--json") == 1
    refused = json.loads(stale.said[-1])
    assert refused["ok"] is False and refused["exit"] == 1 and refused["message"].startswith("refused: ")
    assert {r["stage"] for r in refused["refusals"]} >= {"checked", "reviewed"}
    assert all(set(r) == {"stage", "text", "redo"} for r in refused["refusals"])
    assert capsys.readouterr().out == ""  # everything went through the injected `say`


def test_the_task_defaults_to_the_branch_of_the_checkout(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    opened = docs_unit(tmp_path)
    assert run_main(opened, monkeypatch) == 0, opened.text()


# --- the real wiring and the fix round -------------------------------------------------------------------------------


def test_the_claims_client_of_the_real_machine_survives_a_gh_that_fails(monkeypatch: pytest.MonkeyPatch) -> None:
    def failing(args: Sequence[str], folder: Path | None) -> str:
        message = "gh api failed: HTTP 404: Not Found" if "permission" in " ".join(args) else "gh api failed"
        raise pr_publish.GhError(message)

    monkeypatch.setattr(pr_publish, "real_gh", failing)
    remote = GhRemote("bot", "marker", "reply", pr_open._claims_runner)
    # an account that is not a collaborator: GitHub answers 404, which means no permission, not a crash
    assert remote.permission("somebody") == "none"
    # a comment that cannot be posted is the claims client's own error, which the command warns about
    with pytest.raises(ConfigError):
        remote.post_command(757, "/progress pr-opened")
    with pytest.raises(ConfigError):
        remote.current_user()


def test_a_progress_comment_that_cannot_be_posted_after_the_pull_request_exists_is_a_warning(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)

    def broken(number: int, body: str) -> int:
        raise ConfigError("gh issue comment failed")

    opened.github.post_command = broken  # type: ignore[method-assign]
    assert opened.run() == 0, opened.text()
    assert "/progress pr-opened was not posted" in opened.text()
    published = json.loads((opened.built.unit.dir / pr_publish.PUBLISHED_FILE).read_text(encoding="utf-8"))
    assert published["number"] == 812 and published["progress_posted"] is False


def test_an_error_after_the_stamp_was_written_is_exit_4_not_2(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    opened.fail_ls_remote = 1
    assert opened.run() == 4
    assert len(opened.opened_stamps()) == 1 and "run pr open again" in opened.text()
    assert opened.run() == 0, opened.text()
    assert len(opened.opened_stamps()) == 1


def test_a_rebase_git_cannot_start_is_exit_4_and_not_called_a_conflict(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    opened.advance_main("docs/guides/other.md", "# Other\n")
    opened.fail_rebase = True
    assert opened.run() == 4
    assert "failed before it started" in opened.text() and "mid-rebase" not in opened.text()
    assert not (opened.built.unit.dir / pr_publish.REBASE_FILE).exists() and opened.opened_stamps() == []


def test_the_rebase_is_linear_whatever_the_machines_git_config_says(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    opened.advance_main("docs/guides/other.md", "# Other\n")
    assert opened.run() == 0, opened.text()
    [rebase] = [args for args in opened.git_calls if "rebase" in args and "--abort" not in args]
    assert "--no-rebase-merges" in rebase and "rebase.autoSquash=false" in rebase


def test_the_files_of_a_release_are_kept_in_the_task_folder_whatever_text_file_is_used(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    picture(built, "after.png")
    show(built, "after.png")
    elsewhere = tmp_path / "elsewhere"
    shutil.copytree(built.unit.dir / "shots", elsewhere / "shots")
    shutil.copy(built.unit.dir / "PR.md", elsewhere / "PR.md")
    assert opened.run(text=elsewhere / "PR.md") == 0, opened.text()
    for name in ("PR-TITLE.txt", "PR-BODY.md", pr_publish.PUBLISHED_FILE):
        assert (built.unit.dir / name).is_file() and not (elsewhere / name).exists()
    [create] = [call for call in opened.gh.calls if call[0][:2] == ["pr", "create"]]
    args, cwd = create
    assert cwd == elsewhere and args[args.index("--body-file") + 1] == str(built.unit.dir / "PR-BODY.md")
    assert args[args.index("--attach") + 1] == "./shots/after.png"


def test_a_closed_pull_request_whose_branch_github_deleted_can_be_reopened(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    assert opened.run() == 0
    tip = opened.built.tip
    opened.gh.prs[812]["state"] = "CLOSED"
    git(opened.built.origin, "update-ref", "-d", "refs/heads/work")
    assert opened.run(reopen=True) == 0, opened.text()
    assert opened.origin_tip() == tip and opened.gh.prs[812]["state"] == "OPEN"


def test_a_dry_run_says_what_would_happen_to_the_pull_request_that_exists(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    assert opened.run() == 0
    opened.said.clear()
    assert opened.run(dry_run=True) == 0, opened.text()
    assert "would update #812" in opened.text() and "would turn on auto-merge" in opened.text()
    opened.said.clear()
    assert opened.run(dry_run=True, no_auto_merge=True) == 0
    assert "would leave auto-merge off" in opened.text()
    fresh = docs_unit(tmp_path / "fresh")
    assert fresh.run(dry_run=True) == 0 and "would open the pull request" in fresh.text()


# --- the rules come from the default branch --------------------------------------------------------------------------


def test_the_merge_rules_come_from_the_default_branch_not_from_the_branch_under_release(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    path = opened.built.root / ".github" / "conventions.yml"
    frozen = path.read_text(encoding="utf-8").replace("  auto_merge: true", "  auto_merge: false")
    assert frozen != path.read_text(encoding="utf-8")
    # the branch's own file still says auto-merge is on; the default branch has been frozen since it was cut
    opened.advance_main(".github/conventions.yml", frozen, "freeze auto-merge")
    assert opened.run() == 0, opened.text()
    assert not any(args[:2] == ["pr", "merge"] for args, _ in opened.gh.calls)
    assert "auto-merge" in opened.said[-1] and opened.said[-1].endswith("auto-merge off)")


def test_a_branch_that_loosens_the_stamp_rules_is_refused_by_the_default_branchs(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    # the default branch asks that every stamp says a hook vouched for it; this branch's own file does not, and a
    # Check stamp was written without the hook identity
    opened.advance_main(
        ".github/conventions.yml",
        yaml.safe_dump(changed(("stamps.require_hook_source", True))),
        "ci: require the hook identity",
    )
    git(built.root, "fetch", "-q", "origin", "main")
    built.stamp("checked", covers={"diff_sha256": built.diff}, detail={"profile": "docs", "checks": []}, who=UNVERIFIED)
    assert opened.run() == 1
    assert "without the hook identity (source unverified)" in opened.text(), opened.text()
    assert opened.gh.writes() == [] and "refs/heads/work" not in git(built.origin, "for-each-ref")


def test_the_release_rules_are_read_once_from_the_remotes_default_branch(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    opened = docs_unit(tmp_path)
    seen: list[str] = []
    real = conventions.load_release_rules_at

    def spy(root: Path, ref: str, own: Conventions) -> tuple[Conventions, str]:
        seen.append(ref)
        return real(root, ref, own)

    monkeypatch.setattr(pr_open_unit, "load_release_rules_at", spy)
    assert opened.run() == 0, opened.text()
    assert seen == ["origin/main"]


def set_manual_paths(opened: Open, on: bool) -> None:
    """Make `pull_requests.auto_merge_manual_paths` say `on` on the remote's main."""
    path = opened.built.root / ".github" / "conventions.yml"
    text = path.read_text(encoding="utf-8")
    line = "  auto_merge_manual_paths: "
    assert f"{line}true" in text
    opened.advance_main(".github/conventions.yml", text.replace(f"{line}true", f"{line}{str(on).lower()}"), "set")


def touch_manual_path(opened: Open, name: str, shape: str) -> None:
    """Commit a change to `name`, a path that is the maintainer's to merge, and get the unit ready to release."""
    built = opened.built
    text = (built.root / name).read_text(encoding="utf-8") if (built.root / name).exists() else "x\n"
    built.edit(name, text + "\n# one more line\n", "a change the maintainer merges")
    built.check(shape)
    built.review(functions={})


MANUAL = ["AGENTS.md", ".github/conventions.yml", ".claude/hooks/pr_guard.py"]


@pytest.mark.parametrize("name", MANUAL)
def test_auto_merge_stays_off_for_a_manual_path_when_the_setting_is_off(tmp_path: Path, name: str) -> None:
    shape = "tooling" if name.startswith(".claude") else "docs"
    opened = docs_unit(tmp_path, shape)
    set_manual_paths(opened, False)
    # The branch takes main's new file before its own change, so the stamps cover what is released.
    git(opened.built.root, "fetch", "-q", "origin", "main")
    git(opened.built.root, "rebase", "-q", "origin/main")
    touch_manual_path(opened, name, shape)
    assert opened.run() == 0, opened.text()
    assert not any(args[:2] == ["pr", "merge"] for args, _ in opened.gh.calls)
    assert f"auto-merge left off: the change touches {name}, which the maintainer merges" in opened.text()
    assert opened.said[-1].endswith("auto-merge off)")


@pytest.mark.parametrize("name", MANUAL)
def test_auto_merge_is_on_for_a_manual_path_when_the_setting_is_on(tmp_path: Path, name: str) -> None:
    shape = "tooling" if name.startswith(".claude") else "docs"
    opened = docs_unit(tmp_path, shape)
    touch_manual_path(opened, name, shape)
    assert opened.run() == 0, opened.text()
    assert opened.gh.prs[812]["auto"] is True and "left off" not in opened.text()
    assert opened.said[-1].endswith("auto-merge on)")


def test_the_fix_scope_count_leaves_out_what_main_changed_in_the_rebase(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    assert opened.run() == 0
    start_fix(opened)
    opened.advance_main("docs/guides/other.md", "# Other\n")
    opened.advance_main("docs/guides/more.md", "# More\n")
    built.edit("docs/guides/widget.md", "# The widget\n\nIt waits for the frame.\n", "docs: fix")
    built.check("docs")
    built.review(functions={})
    assert opened.run(task="work-fix") == 0, opened.text()
    assert any(line.startswith("  fix touches 1 file since the last release") for line in opened.said), opened.text()


# --- auto-merge that is on when it should be off ---------------------------------------------------------------------


def test_a_fix_round_that_now_touches_a_maintainer_file_turns_auto_merge_off(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    assert opened.run() == 0
    assert opened.gh.prs[812]["auto"] is True
    set_manual_paths(opened, False)
    start_fix(opened)
    text = (built.root / "AGENTS.md").read_text(encoding="utf-8") if (built.root / "AGENTS.md").exists() else "x\n"
    built.edit("AGENTS.md", text + "\nOne more line.\n", "docs: fix touches AGENTS.md")
    built.check("docs")
    built.review(functions={})
    assert opened.run(task="work-fix") == 0, opened.text()
    assert opened.gh.prs[812]["auto"] is False
    assert ["pr", "merge", "812", "--repo", REPO, "--disable-auto"] in [args for args, _ in opened.gh.calls]
    assert opened.said[-1].endswith("auto-merge off)")


def test_auto_merge_somebody_turned_on_by_hand_is_turned_off_when_the_release_declines_it(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    assert opened.run() == 0
    assert opened.gh.prs[812]["auto"] is True
    assert opened.run(no_auto_merge=True) == 0, opened.text()
    assert opened.gh.prs[812]["auto"] is False
    assert "turning it off" in opened.text()


def test_auto_merge_that_stays_on_after_the_disable_is_exit_4(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    assert opened.run() == 0
    real = opened.gh.__call__

    def stuck(args: Sequence[str], cwd: Path | None) -> str:
        return "" if "--disable-auto" in args else real(args, cwd)

    opened.gh = stuck  # type: ignore[assignment]
    assert opened.run(no_auto_merge=True) == 4
    assert "auto-merge is on" in opened.text()


# --- one default branch, origin's, for every rule --------------------------------------------------------------------


def test_a_branch_that_names_another_default_branch_in_its_own_file_changes_nothing(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    text = (built.root / ".github" / "conventions.yml").read_text(encoding="utf-8")
    assert "default_branch: main" in text
    built.edit(".github/conventions.yml", text.replace("default_branch: main", "default_branch: sneaky"), "ci: rename")
    built.check("docs")
    built.review(functions={})
    seen: list[str] = []
    real = conventions.load_release_rules_at

    def spy(root: Path, ref: str, own: Conventions) -> tuple[Conventions, str]:
        seen.append(ref)
        return real(root, ref, own)

    with pytest.MonkeyPatch.context() as patch:
        patch.setattr(pr_open_unit, "load_release_rules_at", spy)
        assert opened.run() == 0, opened.text()
    assert seen == ["origin/main"]
    assert not any("sneaky" in " ".join(call) for call in opened.git_calls)


def test_a_unit_started_from_another_base_is_refused_before_anything_is_touched(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    record_path = opened.built.unit.dir / opened.built.conventions.tasks.start.unit_file
    record = unit_record.read_record(record_path)
    unit_record.write_record(
        record_path, unit_record.UnitRecord(**{**record.__dict__, "base_ref": "origin/stack-below"})
    )
    before = opened.snapshot()
    assert opened.run() == 2
    assert "started from origin/stack-below, not from origin/main" in opened.text()
    assert opened.snapshot() == before and opened.gh.writes() == []


def test_an_unset_origin_head_stops_the_run(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    git(opened.built.root, "remote", "set-head", "origin", "-d")
    assert opened.run() == 2
    assert "origin/HEAD is not set" in opened.text()


def test_the_rules_come_from_origin_even_when_the_release_goes_to_another_remote(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    git(built.root, "remote", "add", "up", str(built.origin))
    git(built.root, "fetch", "-q", "up")
    record_path = built.unit.dir / built.conventions.tasks.start.unit_file
    record = unit_record.read_record(record_path)
    unit_record.write_record(record_path, unit_record.UnitRecord(**{**record.__dict__, "base_ref": "up/main"}))
    seen: list[str] = []
    real = conventions.load_release_rules_at

    def spy(root: Path, ref: str, own: Conventions) -> tuple[Conventions, str]:
        seen.append(ref)
        return real(root, ref, own)

    with pytest.MonkeyPatch.context() as patch:
        patch.setattr(pr_open_unit, "load_release_rules_at", spy)
        assert opened.run(remote="up") == 0, opened.text()
    assert seen == ["origin/main"]


def test_the_claims_client_is_built_from_the_default_branchs_rules(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    data = yaml.safe_load((built.root / ".github" / "conventions.yml").read_text(encoding="utf-8"))
    base_bot = data["claims"]["bot_login"]
    # only the branch names another bot; origin/main keeps the real one
    data["claims"]["bot_login"] = "branch-bot[bot]"
    built.edit(".github/conventions.yml", yaml.safe_dump(data), "ci: bot")
    built.check("docs")
    built.review(functions={})
    seen: list[str] = []

    def remote_from(rules: Conventions) -> GhRemote:
        seen.append(rules.claims.bot_login)
        return opened.github  # type: ignore[return-value]

    env = opened.env()
    env.remote_from = remote_from
    assert pr_open.run_open(opened.request(), env) == 0, opened.text()
    assert base_bot != "branch-bot[bot]" and seen == [base_bot]


def test_auto_merge_is_off_before_the_push_so_a_failed_edit_cannot_leave_the_new_tip_to_merge(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    assert opened.run() == 0
    assert opened.gh.prs[812]["auto"] is True
    set_manual_paths(opened, False)
    start_fix(opened)
    text = (built.root / "AGENTS.md").read_text(encoding="utf-8") if (built.root / "AGENTS.md").exists() else "x\n"
    built.edit("AGENTS.md", text + "\nOne more line.\n", "docs: fix touches AGENTS.md")
    built.check("docs")
    built.review(functions={})
    fake = opened.gh
    real = fake.__call__

    def edit_fails(args: Sequence[str], cwd: Path | None) -> str:
        if args[:2] == ["pr", "edit"]:
            raise pr_publish.GhError("gh pr edit failed: the upload was refused")
        return real(args, cwd)

    opened.gh = edit_fails  # type: ignore[assignment]
    assert opened.run(task="work-fix") == 4
    assert fake.prs[812]["auto"] is False


def test_auto_merge_that_stays_on_stops_the_run_before_anything_is_pushed(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    built = opened.built
    assert opened.run() == 0
    set_manual_paths(opened, False)
    start_fix(opened)
    text = (built.root / "AGENTS.md").read_text(encoding="utf-8") if (built.root / "AGENTS.md").exists() else "x\n"
    built.edit("AGENTS.md", text + "\nOne more line.\n", "docs: fix touches AGENTS.md")
    built.check("docs")
    built.review(functions={})
    pushed = opened.origin_tip()
    real = opened.gh.__call__

    def stuck(args: Sequence[str], cwd: Path | None) -> str:
        return "" if "--disable-auto" in args else real(args, cwd)

    opened.gh = stuck  # type: ignore[assignment]
    assert opened.run(task="work-fix") == 4
    assert "stays on" in opened.text() and "nothing was pushed" in opened.text()
    assert opened.origin_tip() == pushed


def test_a_reopened_pull_request_has_auto_merge_turned_off_before_the_push(tmp_path: Path) -> None:
    opened = docs_unit(tmp_path)
    assert opened.run() == 0
    fake = opened.gh
    # closed with its auto-merge request kept, then released again with --reopen and --no-auto-merge
    fake.prs[812]["state"] = "CLOSED"
    assert fake.prs[812]["auto"] is True
    real = fake.__call__

    def edit_fails(args: Sequence[str], cwd: Path | None) -> str:
        if args[:2] == ["pr", "edit"]:
            raise pr_publish.GhError("gh pr edit failed: the upload was refused")
        return real(args, cwd)

    opened.gh = edit_fails  # type: ignore[assignment]
    assert opened.run(reopen=True, no_auto_merge=True) == 4
    assert fake.prs[812]["auto"] is False


def test_the_counts_include_each_test_that_failed_first_or_keeps_the_behaviour_and_no_failed_one(
    tmp_path: Path,
) -> None:
    built = make(tmp_path)
    built.fails_first("Widget same lookup", result="keeps")
    built.fails_first("Widget never red", result="fail")
    proof = pr_ready.Proof(built.subject(), [], None, [], [], [])
    tests, functions = pr_open._counts(proof)
    assert tests == 2  # "Widget ready frame" failed first, "Widget same lookup" keeps, the failed run proves nothing
    assert functions == len(pr_proof.changed_addresses(proof.subject.changed)) >= 1
