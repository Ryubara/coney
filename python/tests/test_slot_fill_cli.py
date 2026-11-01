# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools task slots --fill-check` and `--waive` (task_cli): exit codes, what is printed, the cap on
blocks in a row, and that `task slots` lists the waivers (docs/guides/slot-fill.md).

GitHub and the disk are stand-ins (`FakeSources`, `drives`); the scratch folder is a temporary one.
"""

import argparse
import json
from collections.abc import Callable

import pytest
from slot_fill_support import FakeSources, issue
from test_task_slots import Scene

from coney_tools import slot_fill, slot_fill_blocks, slot_waivers, task_cli, watched_drives
from coney_tools.config import ConfigError
from coney_tools.watched_drives import Drive


@pytest.fixture
def scene(tmp_path_factory: pytest.TempPathFactory) -> Scene:
    return Scene(tmp_path_factory.mktemp("slots"))


@pytest.fixture
def sources() -> FakeSources:
    return FakeSources()


@pytest.fixture
def slots(
    scene: Scene, sources: FakeSources, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> Callable[..., tuple[int, str]]:
    """`coney-tools task slots <words>` on the scene: (exit code, what it printed)."""
    monkeypatch.setattr(task_cli, "scratch_folder", lambda root: scene.scratch)
    monkeypatch.setattr(task_cli, "load_conventions", lambda root: scene.conventions)
    monkeypatch.setattr(task_cli, "find_repo_root", lambda start: scene.scratch)
    monkeypatch.setattr(task_cli, "_workspace_trees", lambda root: [])
    monkeypatch.setattr(task_cli, "_waiver_who", lambda: "coordinator test")
    monkeypatch.setattr(task_cli, "_build_slots", lambda root: scene.build_slots)

    def run(
        *words: str, free_gb: float = 500.0, drives: list[Drive] | None = None, read_disk: bool = False
    ) -> tuple[int, str]:
        group = argparse.ArgumentParser().add_subparsers(dest="group")
        task_cli.add_task_parser(group)
        args = group.choices["task"].parse_args(["slots", *words])
        code = task_cli.run_task(
            args,
            now=scene.now,
            remote=scene.github,
            sources=sources,
            drives=None
            if read_disk
            else drives or [Drive("C:", ("the workspace drive",), int(free_gb * 1024**3), 30, 10)],
        )
        return code, capsys.readouterr().out

    return run


def test_a_failing_check_exits_1_with_the_message(
    scene: Scene, sources: FakeSources, slots: Callable[..., tuple[int, str]]
) -> None:
    scene.unit("a-one")
    sources.ready_issues = [issue(7)]
    code, out = slots("--fill-check")
    assert code == 1 and out.startswith(slot_fill.BLOCKED) and "#7 " in out


def test_a_passing_check_exits_0(scene: Scene, sources: FakeSources, slots: Callable[..., tuple[int, str]]) -> None:
    scene.unit("a-one")
    code, out = slots("--fill-check")
    assert code == 0 and out.startswith("ok: ") and "no issue is ready to start" in out


def test_low_disk_passes_and_says_so(scene: Scene, sources: FakeSources, slots: Callable[..., tuple[int, str]]) -> None:
    sources.ready_issues = [issue(7)]
    assert slots("--fill-check", free_gb=5.0)[0] == 0
    assert slots("--fill-check", free_gb=80.0)[0] == 1


def test_a_low_second_drive_passes_and_says_which(sources: FakeSources, slots: Callable[..., tuple[int, str]]) -> None:
    sources.ready_issues = [issue(7)]
    drives = [
        Drive("C:", ("the workspace drive",), 300 * 1024**3, 30, 10),
        Drive("E:", ("the build root (E:/b)",), int(3.7 * 1024**3), 30, 10),
    ]
    code, out = slots("--fill-check", drives=drives)
    assert code == 0 and "3.7 GB on E: (the build root (E:/b)), under its 30 GB floor" in out


def test_drives_that_cannot_be_read_are_no_reason_and_a_warning(
    sources: FakeSources, slots: Callable[..., tuple[int, str]], monkeypatch: pytest.MonkeyPatch
) -> None:
    def broken(checkout: object, workspace: object) -> object:
        raise ConfigError("coney.local.toml: watch_paths must be a list")

    monkeypatch.setattr(watched_drives, "read", broken)
    sources.ready_issues = [issue(7)]
    code, out = slots("--fill-check", read_disk=True)
    assert code == 1 and "\nwarning: the free disk space could not be read (coney.local.toml: watch_paths" in out


def test_a_drive_that_is_not_plugged_in_is_a_warning_beside_the_others(
    sources: FakeSources, slots: Callable[..., tuple[int, str]], monkeypatch: pytest.MonkeyPatch
) -> None:
    found = watched_drives.Watch([Drive("C:", ("the workspace drive",), 300 * 1024**3, 30, 10)], ["F: is not ready"])
    monkeypatch.setattr(watched_drives, "read", lambda checkout, workspace: found)
    sources.ready_issues = [issue(7)]
    code, out = slots("--fill-check", read_disk=True)
    assert code == 1 and "\nwarning: F: is not ready" in out


def test_a_github_that_cannot_be_read_exits_0_with_a_warning_line(
    sources: FakeSources, slots: Callable[..., tuple[int, str]]
) -> None:
    sources.down = {"all"}
    code, out = slots("--fill-check")
    assert code == 0 and "\nwarning: GitHub could not be read" in out


def test_the_waiver_is_made_listed_and_ends_the_failure(
    scene: Scene, sources: FakeSources, slots: Callable[..., tuple[int, str]]
) -> None:
    scene.unit("a-one")
    sources.ready_issues = [issue(7)]
    code, out = slots(
        "--waive", "2", "--reason", "maintainer-pause", "--hours", "6", "--note", "away until the evening"
    )
    assert code == 0 and out.startswith("waiver: #1 2 place(s), maintainer-pause, until 2026-10-22T18:00:00Z")
    assert "by coordinator test: away until the evening" in out
    assert slots("--fill-check")[0] == 0
    code, out = slots()
    assert "waivers: 1 active" in out and "maintainer-pause" in out
    data = json.loads(slots("--json")[1])
    assert [(w["id"], w["slots"], w["reason"]) for w in data["waivers"]] == [(1, 2, "maintainer-pause")]


def test_the_waiver_ends_by_itself(scene: Scene, sources: FakeSources, slots: Callable[..., tuple[int, str]]) -> None:
    scene.unit("a-one")
    sources.ready_issues = [issue(7)]
    slots("--waive", "2", "--reason", "machine-load", "--hours", "1", "--note", "builds are queued up")
    assert slots("--fill-check")[0] == 0
    scene.clock.advance(hours=1, minutes=1)
    assert slots("--fill-check")[0] == 1
    assert "waivers: none active" in slots()[1] and "(1 ended" in slots()[1]


@pytest.mark.parametrize(
    ("words", "message"),
    [
        (["--waive", "1"], "--waive needs --reason"),
        (["--waive", "1", "--reason", "maintainer-pause"], "--waive needs --hours"),
        (["--waive", "1", "--reason", "maintainer-pause", "--hours", "1"], "--waive needs --note"),
        (["--waive", "1", "--reason", "x", "--hours", "1", "--note", "a note long enough"], "not a waiver code"),
        (["--waive", "1", "--reason", "maintainer-pause", "--hours", "30", "--note", "a note long enough"], "24 hours"),
        (["--waive", "1", "--reason", "machine-load", "--hours", "9", "--note", "a note long enough"], "3 hours"),
        (
            ["--waive", "1", "--reason", "maintainer-decision", "--hours", "9", "--note", "a note long enough"],
            "name the blocked issues",
        ),
        (
            [
                "--waive",
                "1",
                "--reason",
                "maintainer-decision",
                "--hours",
                "9",
                "--note",
                "a note long enough",
                "--issues",
                "x",
            ],
            "expected issue numbers",
        ),
        (["--waive", "1", "--admit"], "separate commands"),
        (["--fill-check", "--waive", "1"], "separate commands"),
    ],
)
def test_a_waiver_the_rules_refuse_is_a_config_error(
    slots: Callable[..., tuple[int, str]], words: list[str], message: str
) -> None:
    with pytest.raises(ConfigError, match=message):
        slots(*words)


def test_a_maintainer_decision_waiver_names_the_issues(scene: Scene, slots: Callable[..., tuple[int, str]]) -> None:
    code, out = slots(
        "--waive", "1", "--reason", "maintainer-decision", "--hours", "24", "--note", "waits on the cutover call",
        "--issues", "#1030, 1031",
    )  # fmt: skip
    assert code == 0 and "blocked: #1030, #1031" in out
    assert [w.issues for w in slot_waivers.read_waivers(scene.scratch)] == [(1030, 1031)]


def test_with_a_session_the_blocks_are_capped_and_a_pass_clears_the_count(
    scene: Scene, sources: FakeSources, slots: Callable[..., tuple[int, str]]
) -> None:
    scene.unit("a-one")
    sources.ready_issues = [issue(7)]
    codes = [slots("--fill-check", "--session", "s1")[0] for _ in range(4)]
    assert codes == [1, 1, 1, 0]
    code, out = slots("--fill-check", "--session", "s1")
    assert code == 0 and "warning: the check still fails but it has blocked 3 turn ends in a row" in out
    sources.ready_issues = []
    assert slots("--fill-check", "--session", "s1")[0] == 0  # a pass clears the count
    sources.ready_issues = [issue(7)]
    assert slots("--fill-check", "--session", "s1")[0] == 1


def test_without_a_session_nothing_is_counted(
    scene: Scene, sources: FakeSources, slots: Callable[..., tuple[int, str]]
) -> None:
    scene.unit("a-one")
    sources.ready_issues = [issue(7)]
    assert [slots("--fill-check")[0] for _ in range(5)] == [1] * 5
    assert not (scene.scratch / slot_fill_blocks.BLOCKS_FILE).exists()


def test_the_blocks_come_back_after_the_reset(
    scene: Scene, sources: FakeSources, slots: Callable[..., tuple[int, str]]
) -> None:
    scene.unit("a-one")
    sources.ready_issues = [issue(7)]
    for _ in range(4):
        slots("--fill-check", "--session", "s1")
    scene.clock.advance(minutes=31)
    assert slots("--fill-check", "--session", "s1")[0] == 1


def test_a_start_ends_the_failure(scene: Scene, sources: FakeSources, slots: Callable[..., tuple[int, str]]) -> None:
    scene.unit("a-one")
    sources.ready_issues = [issue(7), issue(8)]
    assert slots("--fill-check")[0] == 1
    scene.unit("b-two")
    scene.unit("c-three")
    scene.unit("d-fix", kind="fix")
    code, output = slots("--fill-check")
    assert code == 0 and "every place is full" in output
