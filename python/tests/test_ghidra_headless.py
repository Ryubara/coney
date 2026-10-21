# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.ghidra_headless: the project index, the Java environment and the log tail."""

from pathlib import Path

from coney_tools import ghidra_headless
from coney_tools.ghidra_headless import GhidraProject

INDEX = """VERSION=1
/
  00000000:SLUS_212.15:1a6edfedc8e290595612395600
/overlays
  00000001:LEVEL01.BIN:1a6edfedc8e290595612395601
NEXT-ID:2
MD5:d41d8cd98f00b204e9800998ecf8427e
"""


def make_project(tmp_path: Path, index: str | None) -> GhidraProject:
    """A made-up Ghidra project folder with `index` as its file index (none when None)."""
    project = GhidraProject(tmp_path, "warriors")
    idata = tmp_path / "warriors.rep" / "idata"
    idata.mkdir(parents=True)
    if index is not None:
        (idata / "~index.dat").write_text(index, encoding="utf-8")
    return project


def test_programs_lists_the_files_by_project_path(tmp_path: Path) -> None:
    project = make_project(tmp_path, INDEX)
    assert ghidra_headless.programs(project) == {"/SLUS_212.15", "/overlays/LEVEL01.BIN"}
    assert ghidra_headless.has_program(project, "SLUS_212.15")
    assert not ghidra_headless.has_program(project, "LEVEL01.BIN")


def test_programs_of_a_missing_project_is_empty(tmp_path: Path) -> None:
    assert ghidra_headless.programs(GhidraProject(tmp_path / "none", "warriors")) == set()
    assert ghidra_headless.programs(make_project(tmp_path, None)) == set()


def test_project_paths(tmp_path: Path) -> None:
    project = GhidraProject(tmp_path, "warriors")
    assert project.gpr == tmp_path / "warriors.gpr"
    assert project.exists is False
    project.gpr.write_text("", encoding="utf-8")
    assert project.exists is True


def test_java_env_puts_the_jdk_first(tmp_path: Path) -> None:
    env = ghidra_headless.java_env(tmp_path / "jdk", {"PATH": "elsewhere", "JAVA_HOME": "old"})
    assert env["JAVA_HOME"] == str(tmp_path / "jdk")
    assert env["PATH"].startswith(str(tmp_path / "jdk" / "bin"))
    assert env["PATH"].endswith("elsewhere")


def test_log_tail_keeps_the_last_lines(tmp_path: Path) -> None:
    log = tmp_path / "x.log"
    log.write_text("".join(f"line {n}\n" for n in range(40)), encoding="utf-8")
    assert ghidra_headless.log_tail(log, 3) == "line 37\nline 38\nline 39"
    assert ghidra_headless.log_tail(tmp_path / "missing.log") == ""
