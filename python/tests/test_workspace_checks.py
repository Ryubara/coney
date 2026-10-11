# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.workspace_checks: the prerequisite checks of `coney-tools workspace init`."""

from collections.abc import Mapping, Sequence
from pathlib import Path

from coney_tools.workspace_checks import Check, check_prerequisites, find_jdk, java_major, parse_version


def fake_tools(outputs: Mapping[str, str]) -> tuple[object, object]:
    """A `which` and a `run` that know only the programs in `outputs` (program name -> its version output)."""

    def which(name: str) -> str | None:
        return f"/bin/{name}" if name in outputs else None

    def run(command: Sequence[str]) -> str | None:
        name = Path(command[0]).name
        return outputs.get(name)

    return which, run


def by_name(checks: list[Check]) -> dict[str, Check]:
    """The checks keyed by name."""
    return {check.name: check for check in checks}


def checks_for(platform: str, outputs: Mapping[str, str], env: Mapping[str, str] | None = None) -> dict[str, Check]:
    """Run every check on `platform` against made-up programs."""
    which, run = fake_tools(outputs)
    return by_name(check_prerequisites(platform, which, run, env or {}, None))  # type: ignore[arg-type]


def test_parse_version_takes_the_first_dotted_number() -> None:
    assert parse_version("cmake version 3.28.3\n") == (3, 28, 3)
    assert parse_version("g++ (Ubuntu 13.2.0-23ubuntu4) 13.2.0") == (13, 2, 0)
    assert parse_version("v18.19.1") == (18, 19, 1)
    assert parse_version("17.8.34525.116") == (17, 8, 34525, 116)
    assert parse_version("no digits") is None


def test_java_major_reads_old_and_new_numbering() -> None:
    assert java_major('openjdk version "21.0.4" 2024-07-16') == 21
    assert java_major('java version "1.8.0_381"') == 8
    assert java_major('openjdk version "17" 2021-09-14') == 17
    assert java_major("something else") is None


def test_linux_needs_gcc_13_or_clang_19() -> None:
    checks = checks_for("linux", {"g++": "g++ (GCC) 12.3.0", "git": "git version 2.43.0"})
    assert not checks["C++ compiler"].ok
    assert checks["C++ compiler"].required
    checks = checks_for("linux", {"g++": "g++ (GCC) 12.3.0", "g++-13": "g++-13 (GCC) 13.1.0"})
    assert checks["C++ compiler"].ok
    assert "13.1.0" in checks["C++ compiler"].detail
    checks = checks_for("linux", {"clang++-19": "Ubuntu clang version 19.1.1"})
    assert checks["C++ compiler"].ok


def test_windows_reads_the_visual_studio_version() -> None:
    env = {"ProgramFiles(x86)": "C:/PF86"}
    assert checks_for("windows", {"vswhere.exe": "17.8.34525.116"}, env)["C++ compiler"].ok
    assert not checks_for("windows", {"vswhere.exe": "17.6.0"}, env)["C++ compiler"].ok
    assert not checks_for("windows", {}, env)["C++ compiler"].ok


def test_windows_takes_cmake_and_ninja_from_visual_studio() -> None:
    checks = checks_for("windows", {"vswhere.exe": "17.10.0"}, {"ProgramFiles(x86)": "C:/PF86"})
    assert checks["CMake 3.28+"].ok and "Visual Studio" in checks["CMake 3.28+"].detail
    assert checks["Ninja"].ok


def test_macos_needs_xcode_16() -> None:
    assert checks_for("macos", {"clang++": "Apple clang version 16.0.0 (clang-1600.0.26.3)"})["C++ compiler"].ok
    assert not checks_for("macos", {"clang++": "Apple clang version 15.0.0"})["C++ compiler"].ok


def test_old_cmake_is_named_with_the_version_needed() -> None:
    check = checks_for("linux", {"cmake": "cmake version 3.27.9"})["CMake 3.28+"]
    assert not check.ok
    assert "3.27.9" in check.detail


def test_research_tools_are_optional() -> None:
    checks = checks_for("linux", {"node": "v16.20.0"})
    assert not checks["Node.js 18+"].ok
    assert not checks["Node.js 18+"].required
    assert not checks["JDK 21"].required


def test_jdk_from_path_counts() -> None:
    checks = checks_for("linux", {"java": 'openjdk version "21.0.2" 2024-01-16'})
    assert checks["JDK 21"].ok


def test_find_jdk_prefers_the_configured_home_then_java_home(tmp_path: Path) -> None:
    configured = tmp_path / "jdk21"
    other = tmp_path / "jdk17"

    def run(command: Sequence[str]) -> str | None:
        if Path(command[0]).is_relative_to(configured):
            return 'openjdk version "21.0.1"'
        if Path(command[0]).is_relative_to(other):
            return 'openjdk version "17.0.9"'
        return None

    def which(name: str) -> str | None:
        return None

    assert find_jdk("linux", which, run, {"JAVA_HOME": str(other)}, configured) == configured
    # A JAVA_HOME that is too old is not taken.
    assert find_jdk("linux", which, run, {"JAVA_HOME": str(other)}, None) is None
    assert find_jdk("linux", which, run, {"JAVA_HOME": str(configured)}, None) == configured
