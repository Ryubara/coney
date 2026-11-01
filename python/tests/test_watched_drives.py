# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.watched_drives: the drives that hold a Coney folder, each with its free space and limits
(docs/guides/workspace.md#watched-drives). The disk is a stand-in (watched_support.Machine)."""

from pathlib import Path

import pytest
from conventions_support import REPO
from watched_support import GIB, Machine, make_machine

from coney_tools.config import ConfigError, load_config
from coney_tools.conventions import load_conventions
from coney_tools.watched_drives import Drive, Watch, read, worst

CONVENTIONS = load_conventions(REPO)
FLOOR = CONVENTIONS.workspace.disk_min_free_gb
MARGIN = CONVENTIONS.workspace.disk_margin_gb


def watch(machine: Machine) -> Watch:
    """The machine's watched drives."""
    return read(machine.checkout, CONVENTIONS.workspace, machine.probe())


def names(found: Watch) -> list[str]:
    """The drives' names, in order."""
    return [drive.name for drive in found.drives]


# --- the defaults need no setting -----------------------------------------------------------------------------------


def test_without_a_local_file_only_the_workspace_drive_is_watched(tmp_path: Path) -> None:
    found = watch(make_machine(tmp_path, c=61))
    [drive] = found.drives
    assert (drive.name, drive.free_bytes, drive.holds) == ("C:", 61 * GIB, ("the workspace drive",))
    assert found.problems == []


def test_the_default_limits_are_the_conventions(tmp_path: Path) -> None:
    [drive] = watch(make_machine(tmp_path)).drives
    assert (drive.min_free_gb, drive.warn_free_gb) == (FLOOR, FLOOR + MARGIN)


def test_without_conventions_the_built_in_limits_apply(tmp_path: Path) -> None:
    machine = make_machine(tmp_path)
    [drive] = read(machine.checkout, None, machine.probe()).drives
    assert (drive.min_free_gb, drive.warn_free_gb) == (30, 40)


def test_a_build_root_on_the_workspace_drive_is_counted_once(tmp_path: Path) -> None:
    machine = make_machine(tmp_path, c=61)
    builds = machine.root / "c" / "builds"
    machine.local(f"build_root = '{builds.as_posix()}'\n")
    [drive] = watch(machine).drives
    assert drive.holds == ("the workspace drive", f"the build root ({builds.resolve()})")


def test_a_build_root_on_another_drive_is_a_second_drive(tmp_path: Path) -> None:
    machine = make_machine(tmp_path, c=200, e=3.7)
    builds = machine.root / "e" / "coney_builds"
    machine.local(f"build_root = '{builds.as_posix()}'\n")
    found = watch(machine)
    assert names(found) == ["C:", "E:"]
    assert [drive.free_bytes for drive in found.drives] == [200 * GIB, int(3.7 * GIB)]
    assert found.drives[1].holds == (f"the build root ({builds.resolve()})",)


# --- extra paths ----------------------------------------------------------------------------------------------------


def test_an_extra_path_on_a_third_drive_is_watched(tmp_path: Path) -> None:
    machine = make_machine(tmp_path, f=20)
    extra = machine.root / "f" / "scratch"
    machine.local(f"watch_paths = ['{extra.as_posix()}']\n")
    found = watch(machine)
    assert names(found) == ["C:", "F:"]
    assert found.drives[1].holds == (f"the watched folder ({extra.resolve()})",)
    assert found.drives[1].low


def test_an_extra_path_on_a_drive_already_watched_adds_no_drive(tmp_path: Path) -> None:
    machine = make_machine(tmp_path)
    extra = machine.root / "c" / "emulators"
    machine.local(f"watch_paths = ['{extra.as_posix()}']\n")
    [drive] = watch(machine).drives
    assert drive.holds == ("the workspace drive", f"the watched folder ({extra.resolve()})")


def test_the_workspace_build_root_and_extras_make_three_drives_in_that_order(tmp_path: Path) -> None:
    machine = make_machine(tmp_path)
    builds = (machine.root / "e" / "b").as_posix()
    machine.local(f"build_root = '{builds}'\nwatch_paths = ['{(machine.root / 'f').as_posix()}']\n")
    assert names(watch(machine)) == ["C:", "E:", "F:"]


def test_a_relative_extra_path_is_relative_to_the_local_file(tmp_path: Path) -> None:
    machine = make_machine(tmp_path)
    machine.local("watch_paths = ['../../../../f/tools']\n")
    assert names(watch(machine)) == ["C:", "F:"]


def test_an_extra_path_that_does_not_exist_yet_is_watched_by_its_drive(tmp_path: Path) -> None:
    machine = make_machine(tmp_path)
    machine.local(f"watch_paths = ['{(machine.root / 'f' / 'not' / 'made' / 'yet').as_posix()}']\n")
    assert names(watch(machine)) == ["C:", "F:"]


def test_a_drive_that_cannot_be_read_is_a_problem_and_the_others_still_show(tmp_path: Path) -> None:
    machine = make_machine(tmp_path, c=90, f=-1)
    machine.local(f"watch_paths = ['{(machine.root / 'f').as_posix()}']\n")
    found = watch(machine)
    assert names(found) == ["C:"]
    assert len(found.problems) == 1 and "not ready" in found.problems[0]


def test_the_workspace_drive_that_cannot_be_read_raises(tmp_path: Path) -> None:
    machine = make_machine(tmp_path, c=-1)
    with pytest.raises(OSError, match="not ready"):
        watch(machine)


# --- limits ---------------------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("free", "low", "warn"),
    [
        (FLOOR + MARGIN, False, False),
        (FLOOR + MARGIN - 0.1, False, True),
        (FLOOR - 0.1, True, True),
        (FLOOR, False, True),
    ],
)
def test_a_drive_is_amber_under_the_floor_plus_the_margin_and_red_under_the_floor(
    tmp_path: Path, free: float, low: bool, warn: bool
) -> None:
    [drive] = watch(make_machine(tmp_path, c=free)).drives
    assert (drive.low, drive.warn) == (low, warn)


def test_a_path_can_give_its_drive_its_own_limits(tmp_path: Path) -> None:
    machine = make_machine(tmp_path, c=35, f=12)
    path = (machine.root / "f" / "tools").as_posix()
    machine.local(f"watch_paths = [{{ path = '{path}', min_free_gb = 10, margin_gb = 5 }}]\n")
    c, f = watch(machine).drives
    assert (c.min_free_gb, c.warn_free_gb) == (FLOOR, FLOOR + MARGIN)
    assert (f.min_free_gb, f.warn_free_gb) == (10, 15)
    assert not f.low and f.warn


def test_one_limit_alone_keeps_the_other_default(tmp_path: Path) -> None:
    machine = make_machine(tmp_path)
    machine.local(f"watch_paths = [{{ path = '{(machine.root / 'f').as_posix()}', min_free_gb = 2 }}]\n")
    f = watch(machine).drives[1]
    assert (f.min_free_gb, f.warn_free_gb) == (2, 2 + MARGIN)


def test_limits_on_a_path_of_a_default_drive_set_that_drives_limits(tmp_path: Path) -> None:
    machine = make_machine(tmp_path, e=50)
    builds = machine.root / "e" / "coney_builds"
    machine.local(
        f"build_root = '{builds.as_posix()}'\n"
        f"watch_paths = [{{ path = '{builds.as_posix()}', min_free_gb = 60, margin_gb = 20 }}]\n"
    )
    e = watch(machine).drives[1]
    assert (e.min_free_gb, e.warn_free_gb) == (60, 80) and e.low
    assert e.holds[0] == f"the build root ({builds.resolve()})"


def test_two_paths_giving_one_drive_limits_are_refused(tmp_path: Path) -> None:
    machine = make_machine(tmp_path)
    f = (machine.root / "f").as_posix()
    machine.local(f"watch_paths = [{{ path = '{f}/a', min_free_gb = 2 }}, {{ path = '{f}/b', min_free_gb = 3 }}]\n")
    with pytest.raises(ConfigError, match="two entries"):
        watch(machine)


# --- the worst drive ------------------------------------------------------------------------------------------------


def drive(name: str, free: float, floor: int = 30, margin: int = 10) -> Drive:
    """A drive with `free` GB and these limits."""
    return Drive(name, (), int(free * GIB), floor, margin)


def test_the_worst_drive_has_the_least_room_above_its_own_floor() -> None:
    assert worst([drive("C:", 61), drive("E:", 3.7), drive("F:", 20, floor=5)]).name == "E:"
    assert worst([drive("C:", 40), drive("F:", 16, floor=5)]).name == "C:"
    assert worst([drive("C:", 40), drive("F:", 6, floor=5)]).name == "F:"


def test_a_tie_goes_to_the_first_drive() -> None:
    assert worst([drive("C:", 40), drive("E:", 40)]).name == "C:"


# --- the local file -------------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("text", "message"),
    [
        ("watch_paths = 'x'\n", "watch_paths must be a list"),
        ("watch_paths = [3]\n", "watch_paths"),
        ("watch_paths = [{ min_free_gb = 3 }]\n", "path"),
        ("watch_paths = [{ path = 'x', min_free = 3 }]\n", "min_free"),
        ("watch_paths = [{ path = 'x', min_free_gb = -1 }]\n", "min_free_gb"),
        ("watch_paths = [{ path = 'x', margin_gb = 'a' }]\n", "margin_gb"),
        ("watch_paths = ['']\n", "watch_paths"),
    ],
)
def test_a_bad_watch_path_is_named(tmp_path: Path, text: str, message: str) -> None:
    machine = make_machine(tmp_path)
    machine.local(text)
    with pytest.raises(ConfigError, match=message):
        load_config(machine.checkout)


def test_the_watch_paths_load_in_order_with_their_limits(tmp_path: Path) -> None:
    machine = make_machine(tmp_path)
    machine.local("watch_paths = ['a', { path = 'b', min_free_gb = 4, margin_gb = 0 }]\n")
    first, second = load_config(machine.checkout).watch
    assert first.path == (machine.checkout / "a").resolve() and first.min_free_gb is None
    assert (second.path.name, second.min_free_gb, second.margin_gb) == ("b", 4, 0)


def test_no_watch_paths_is_an_empty_list(tmp_path: Path) -> None:
    machine = make_machine(tmp_path)
    machine.local("")
    assert load_config(machine.checkout).watch == ()
