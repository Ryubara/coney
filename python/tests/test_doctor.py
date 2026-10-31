# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.doctor: `coney-tools doctor` on made-up machines, discs and folders."""

import hashlib
from collections.abc import Sequence
from pathlib import Path

import pytest

from coney_tools import doctor
from coney_tools.cli import main
from coney_tools.disc import Disc
from coney_tools.ghidra_headless import GhidraProject
from coney_tools.workspace_checks import Check, Run

SLUS = b"made-up executable"
DIR = b"made-up directory"
WAD = b"made-up archive!"
REFERENCE = {
    "SLUS_212.15": doctor.Reference(len(SLUS), hashlib.sha1(SLUS).hexdigest()),
    "WARRIORS.DIR": doctor.Reference(len(DIR), hashlib.sha1(DIR).hexdigest()),
    "WARRIORS.WAD": doctor.Reference(len(WAD), None),
}


def make_disc(tmp_path: Path, files: dict[str, bytes]) -> Disc:
    """A made-up disc folder holding `files`."""
    folder = tmp_path / "disc"
    folder.mkdir()
    for name, data in files.items():
        (folder / name).write_bytes(data)
    return Disc(folder)


def by_name(checks: Sequence[Check]) -> dict[str, Check]:
    """The checks by name."""
    return {check.name: check for check in checks}


def test_the_ntsc_u_disc_passes(tmp_path: Path) -> None:
    disc = make_disc(tmp_path, {"SLUS_212.15": SLUS, "WARRIORS.DIR": DIR, "WARRIORS.WAD": WAD})
    checks = doctor.disc_checks(disc, REFERENCE)
    assert all(check.ok for check in checks), checks
    assert hashlib.sha1(SLUS).hexdigest() in by_name(checks)["SLUS_212.15"].detail


def test_another_release_is_a_note_not_a_problem(tmp_path: Path) -> None:
    disc = make_disc(tmp_path, {"SLUS_212.15": b"other", "WARRIORS.DIR": DIR, "WARRIORS.WAD": WAD})
    check = by_name(doctor.disc_checks(disc, REFERENCE))["SLUS_212.15"]
    assert not check.ok and not check.required
    assert "not the NTSC-U release" in check.detail


def test_a_disc_missing_the_archive_is_a_problem(tmp_path: Path) -> None:
    disc = make_disc(tmp_path, {"SLUS_212.15": SLUS})
    checks = by_name(doctor.disc_checks(disc, REFERENCE))
    assert not checks["WARRIORS.WAD"].ok and checks["WARRIORS.WAD"].required
    assert "missing" in checks["WARRIORS.WAD"].detail


def test_no_disc_is_a_note(tmp_path: Path) -> None:
    (check,) = doctor.disc_checks(None, REFERENCE)
    assert not check.ok and not check.required
    assert "--disc" in check.hint


def test_the_reference_matches_the_ghidra_setup_hash() -> None:
    from coney_tools.disc import NTSC_U_SHA1

    assert doctor.NTSC_U["SLUS_212.15"].sha1 == NTSC_U_SHA1


def fake_run(outputs: dict[str, str]) -> Run:
    """A stand-in for run_command answering by program name."""

    def run(command: Sequence[str]) -> str | None:
        return outputs.get(Path(command[0]).name)

    return run


def test_old_default_gcc_with_a_newer_one_installed_says_to_set_cc() -> None:
    which = {"g++": "/usr/bin/g++", "g++-13": "/usr/bin/g++-13"}.get
    run = fake_run({"g++": "g++ (Ubuntu 11.4.0) 11.4.0", "g++-13": "g++-13 (Ubuntu 13.1.0) 13.1.0"})
    check = doctor.default_compiler_check("linux", which, run, {})
    assert check is not None and not check.ok
    assert "CC=gcc-13 CXX=g++-13" in check.hint


def test_default_gcc_note_is_quiet_when_cxx_is_set_or_gcc_is_new() -> None:
    which = {"g++": "/usr/bin/g++", "g++-13": "/usr/bin/g++-13"}.get
    old = fake_run({"g++": "g++ 11.4.0", "g++-13": "g++-13 13.1.0"})
    assert doctor.default_compiler_check("linux", which, old, {"CXX": "g++-13"}) is None
    assert doctor.default_compiler_check("linux", which, fake_run({"g++": "g++ 13.2.0"}), {}) is None
    assert doctor.default_compiler_check("windows", which, old, {}) is None


def test_uv_is_required() -> None:
    assert doctor.uv_check({"uv": "/bin/uv"}.get).ok
    missing = doctor.uv_check(lambda name: None)
    assert not missing.ok and missing.required


def test_disk_space_check() -> None:
    gib = 1 << 30
    assert doctor.disk_check(40 * gib).ok
    low = doctor.disk_check(3 * gib)
    assert not low.ok and low.required
    assert "6.5 GB" in low.hint


def make_ghidra(tmp_path: Path, sla: bool) -> Path:
    """A made-up Ghidra install with the extension; with `sla`, its language compiled."""
    install = tmp_path / "ghidra"
    (install / "support").mkdir(parents=True)
    (install / "support" / "analyzeHeadless").write_text("", encoding="utf-8")
    languages = install / "Ghidra" / "Extensions" / "ghidra-emotionengine-reloaded" / "data" / "languages"
    languages.mkdir(parents=True)
    if sla:
        (languages / "r5900.sla").write_text("", encoding="utf-8")
    return install


def test_ghidra_checks_point_at_ghidra_setup(tmp_path: Path) -> None:
    project = GhidraProject(tmp_path / "projects", "warriors")
    checks = by_name(doctor.ghidra_checks(tmp_path / "none", project))
    assert not checks["Ghidra 12.1.4"].ok and not checks["Ghidra 12.1.4"].required
    assert "ghidra setup" in checks["Ghidra 12.1.4"].hint
    checks = by_name(doctor.ghidra_checks(make_ghidra(tmp_path, sla=False), project))
    assert checks["Ghidra 12.1.4"].ok
    assert not checks["EE language (r5900.sla)"].ok
    assert not checks["Ghidra project"].ok


def test_port_check_tells_ghidra_mcp_from_another_program() -> None:
    assert doctor.port_check(8090, listening=lambda port: False, answers=lambda: False).ok
    assert doctor.port_check(8090, listening=lambda port: True, answers=lambda: True).ok
    taken = doctor.port_check(8090, listening=lambda port: True, answers=lambda: False)
    assert not taken.ok and not taken.required
    assert "-Port" in taken.hint


def test_report_prints_each_section_and_counts_problems(capsys: pytest.CaptureFixture[str]) -> None:
    sections = [
        ("Build tools", [Check("Git", True, "/usr/bin/git", "install Git", True)]),
        ("Disc", [Check("Disc", False, "none given", "pass --disc", False)]),
        ("Disk", [Check("Free space", False, "2 GB", "free some space", True)]),
    ]
    assert doctor.report(sections) == 1
    out = capsys.readouterr().out
    assert "Build tools" in out and "  ok       Git: /usr/bin/git" in out
    assert "  note     Disc: none given -> pass --disc" in out
    assert "  problem  Free space: 2 GB -> free some space" in out
    assert out.rstrip().endswith("1 problem, 1 note.")


def test_report_is_zero_without_problems(capsys: pytest.CaptureFixture[str]) -> None:
    assert doctor.report([("Build tools", [Check("Git", True, "git", "", True)])]) == 0
    assert "No problems" in capsys.readouterr().out


def test_doctor_runs_in_a_plain_clone_without_a_config(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    checkout = tmp_path / "coney"
    checkout.mkdir()
    (checkout / "coney.local.example.toml").write_text("", encoding="utf-8")
    disc = make_disc(tmp_path, {"SLUS_212.15": b"other release"})
    monkeypatch.chdir(checkout)
    monkeypatch.setattr(doctor, "check_prerequisites", lambda *args: [Check("Git", True, "git", "", True)])
    monkeypatch.setattr(doctor, "is_listening", lambda port: False)
    code = main(["doctor", "--disc", str(disc.path)])
    out = capsys.readouterr().out
    assert "coney.local.toml: none (defaults are used, in" in out
    assert "coney-workspace" in out  # beside the clone, never inside it
    assert "not the NTSC-U release" in out
    # The archive is missing from the made-up disc: a problem, so the exit code is 1.
    assert "problem  WARRIORS.WAD: missing" in out
    assert code == 1


def test_pcsx2_check_points_a_newcomer_at_the_forks_release(tmp_path: Path) -> None:
    missing = doctor.pcsx2_check("windows", None)
    assert not missing.ok and not missing.required
    assert "--fetch pcsx2-mcp-release" in missing.hint
    build = tmp_path / "PCSX2-MCP-v1.1.0-win64"
    (build / "pcsx2-mcp-server").mkdir(parents=True)
    (build / "pcsx2-qt.exe").write_bytes(b"")
    assert doctor.pcsx2_check("windows", build).ok
    assert "Windows only" in doctor.pcsx2_check("linux", None).hint


def test_bios_check_never_offers_a_download(tmp_path: Path) -> None:
    copy = tmp_path / "pcsx2"
    check = doctor.bios_check(copy)
    assert not check.ok and "your own console" in check.hint
    (copy / "bios").mkdir(parents=True)
    (copy / "bios" / "dump.bin").write_bytes(b"")
    assert doctor.bios_check(copy).ok
    assert "dump.bin" not in doctor.bios_check(copy).detail


def make_visual_studio(tmp_path: Path, toolset: str, atl: bool) -> Path:
    """A made-up Visual Studio install whose default MSVC toolset is `toolset`; with `atl`, its ATL headers."""
    install = tmp_path / "BuildTools"
    build = install / "VC" / "Auxiliary" / "Build"
    build.mkdir(parents=True)
    (build / "Microsoft.VCToolsVersion.default.txt").write_text(f"{toolset}\n", encoding="utf-8")
    include = install / "VC" / "Tools" / "MSVC" / toolset / "atlmfc" / "include"
    include.mkdir(parents=True)
    if atl:
        (include / "atlbase.h").write_text("", encoding="utf-8")
    return install


def test_atl_is_found_under_the_default_msvc_toolset(tmp_path: Path) -> None:
    install = make_visual_studio(tmp_path, "14.44.35207", atl=True)
    check = doctor.atl_check("windows", fake_run({"vswhere.exe": f"{install}\n"}), {})
    assert check is not None and check.ok and check.required
    assert "14.44.35207" in check.detail


def test_missing_atl_is_a_problem_that_names_the_component(tmp_path: Path) -> None:
    install = make_visual_studio(tmp_path, "14.44.35207", atl=False)
    check = doctor.atl_check("windows", fake_run({"vswhere.exe": f"{install}\n"}), {})
    assert check is not None and not check.ok and check.required
    assert "Microsoft.VisualStudio.Component.VC.ATL" in check.hint


def test_atl_is_looked_for_in_the_active_toolset_first(tmp_path: Path) -> None:
    # A developer shell names its toolset in VCToolsInstallDir; that one wins over vswhere's default.
    install = make_visual_studio(tmp_path, "14.44.35207", atl=True)
    active = tmp_path / "other" / "14.38.33130"
    (active / "atlmfc" / "include").mkdir(parents=True)
    run = fake_run({"vswhere.exe": f"{install}\n"})
    check = doctor.atl_check("windows", run, {"VCToolsInstallDir": f"{active}\\"})
    assert check is not None and not check.ok
    assert "14.38.33130" in check.detail


def test_atl_is_only_checked_on_windows_with_visual_studio() -> None:
    assert doctor.atl_check("linux", fake_run({}), {}) is None
    # No Visual Studio: the compiler check already says so.
    assert doctor.atl_check("windows", fake_run({}), {}) is None
