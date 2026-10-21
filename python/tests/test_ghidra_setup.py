# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.ghidra_setup: `coney-tools ghidra setup` on made-up folders, with stand-ins for Ghidra."""

import hashlib
from collections.abc import Mapping, Sequence
from pathlib import Path

import pytest

from coney_tools import ghidra_setup
from coney_tools.config import ConfigError
from coney_tools.disc import Disc
from coney_tools.ghidra_headless import GhidraProject
from coney_tools.progress_research import ListedFunction

EXECUTABLE = b"\x7fELF made-up executable"
LISTING = [ListedFunction(0x00100008, 448, "entry", False)]


class Recorder:
    """Stands in for every program setup runs: records the commands and fakes what each leaves behind."""

    def __init__(self) -> None:
        self.commands: list[list[str]] = []
        self.imported: list[bytes] = []

    def __call__(self, command: Sequence[str], env: Mapping[str, str], log: Path, cwd: Path) -> int:
        self.commands.append(list(command))
        log.write_text("log\n", encoding="utf-8")
        if "-import" in command:
            source = Path(command[command.index("-import") + 1])
            self.imported.append(source.read_bytes())
            project = GhidraProject(Path(command[1]), command[2])
            project.gpr.write_text("", encoding="utf-8")
            idata = project.folder / f"{project.name}.rep" / "idata"
            idata.mkdir(parents=True, exist_ok=True)
            (idata / "~index.dat").write_text(f"VERSION=1\n/\n  00000000:{source.name}:1\nNEXT-ID:1\n", "utf-8")
        elif "-postScript" in command:
            post = command.index("-postScript")
            Path(command[post + 3]).write_text("0x00100008\trenamed\t448\n", encoding="utf-8")
        elif Path(command[0]).stem == "sleigh":
            (cwd / "r5900.sla").write_text("", encoding="utf-8")
        return 0


def make_disc(tmp_path: Path, content: bytes = EXECUTABLE) -> Disc:
    """A made-up disc folder holding only the executable."""
    folder = tmp_path / "disc"
    folder.mkdir()
    (folder / "SLUS_212.15").write_bytes(content)
    return Disc(folder)


def make_ghidra(tmp_path: Path, sla: bool) -> Path:
    """A made-up Ghidra install with the extension; with `sla`, its language already compiled."""
    install = tmp_path / "ghidra"
    (install / "support").mkdir(parents=True)
    for name in ("analyzeHeadless", "analyzeHeadless.bat", "sleigh", "sleigh.bat"):
        (install / "support" / name).write_text("", encoding="utf-8")
    languages = install / "Ghidra" / "Extensions" / "ghidra-emotionengine-reloaded" / "data" / "languages"
    languages.mkdir(parents=True)
    (languages / "r5900.slaspec").write_text("", encoding="utf-8")
    if sla:
        (languages / "r5900.sla").write_text("", encoding="utf-8")
    return install


def make_setup(
    tmp_path: Path, install: Path, recorder: Recorder, fetched: list[Path] | None = None, server: bool = False
) -> ghidra_setup.Setup:
    """A Setup over made-up folders whose programs are `recorder` and whose fetch only records."""

    def fetch(dest: Path, cache: Path) -> None:
        if fetched is not None:
            fetched.append(dest)
        make_ghidra(dest.parent, sla=False).rename(dest)

    return ghidra_setup.Setup(
        install=install,
        jdk=tmp_path / "jdk",
        project=GhidraProject(tmp_path / "projects", "warriors"),
        work=tmp_path / "scratch",
        downloads=tmp_path / "scratch" / "downloads",
        platform="linux",
        runner=recorder,
        fetch=fetch,
        server_up=lambda: server,
    )


def sha1(data: bytes) -> str:
    """The SHA-1 of `data` in hex."""
    return hashlib.sha1(data).hexdigest()


def test_setup_does_every_step_on_a_bare_machine(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    recorder = Recorder()
    fetched: list[Path] = []
    setup = make_setup(tmp_path, tmp_path / "tools" / "ghidra_12.1.4_PUBLIC", recorder, fetched)
    code = ghidra_setup.run_setup(setup, make_disc(tmp_path), LISTING, ghidra_setup.Options(sha1(EXECUTABLE)))
    assert code == 0
    assert fetched == [tmp_path / "tools" / "ghidra_12.1.4_PUBLIC"]
    steps = [Path(c[0]).stem for c in recorder.commands]
    assert steps == ["sleigh", "analyzeHeadless", "analyzeHeadless"]
    imported = recorder.commands[1]
    assert imported[imported.index("-processor") + 1] == "r5900:LE:32:default"
    assert imported[imported.index("-loader") + 1] == "ElfLoader"
    assert recorder.imported == [EXECUTABLE]
    # The copy of the executable is gone once it is imported: only logs stay in the scratch folder.
    assert not list((tmp_path / "scratch").rglob("SLUS_212.15"))
    out = capsys.readouterr().out
    assert f"SHA-1 {sha1(EXECUTABLE)}" in out
    assert "matched 1" in out


def test_setup_skips_what_is_done(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    recorder = Recorder()
    setup = make_setup(tmp_path, make_ghidra(tmp_path, sla=True), recorder)
    disc = make_disc(tmp_path)
    options = ghidra_setup.Options(sha1(EXECUTABLE), names=False)
    assert ghidra_setup.run_setup(setup, disc, LISTING, options) == 0
    recorder.commands.clear()
    assert ghidra_setup.run_setup(setup, disc, LISTING, options) == 0
    assert recorder.commands == []
    assert "exists" in capsys.readouterr().out


def test_dry_run_changes_nothing(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    recorder = Recorder()
    fetched: list[Path] = []
    setup = make_setup(tmp_path, tmp_path / "tools" / "ghidra", recorder, fetched)
    options = ghidra_setup.Options(sha1(EXECUTABLE), dry_run=True)
    assert ghidra_setup.run_setup(setup, make_disc(tmp_path), LISTING, options) == 0
    assert (recorder.commands, fetched) == ([], [])
    out = capsys.readouterr().out
    assert "would fetch" in out and "would import" in out and "would apply" in out


def test_another_release_is_refused_unless_asked(tmp_path: Path) -> None:
    recorder = Recorder()
    setup = make_setup(tmp_path, make_ghidra(tmp_path, sla=True), recorder)
    disc = make_disc(tmp_path, b"another release")
    with pytest.raises(ConfigError, match="not the NTSC-U release"):
        ghidra_setup.run_setup(setup, disc, LISTING, ghidra_setup.Options(sha1(EXECUTABLE)))
    assert recorder.commands == []
    options = ghidra_setup.Options(sha1(EXECUTABLE), any_release=True, names=False)
    assert ghidra_setup.run_setup(setup, disc, LISTING, options) == 0


def test_no_disc_is_needed_once_the_program_is_imported(tmp_path: Path) -> None:
    recorder = Recorder()
    setup = make_setup(tmp_path, make_ghidra(tmp_path, sla=True), recorder)
    options = ghidra_setup.Options(sha1(EXECUTABLE))
    with pytest.raises(ConfigError, match="no disc"):
        ghidra_setup.run_setup(setup, None, LISTING, options)
    assert ghidra_setup.run_setup(setup, make_disc(tmp_path), LISTING, options) == 0
    assert ghidra_setup.run_setup(setup, None, LISTING, options) == 0


def test_a_project_open_in_a_server_is_left_alone(tmp_path: Path) -> None:
    recorder = Recorder()
    setup = make_setup(tmp_path, make_ghidra(tmp_path, sla=True), recorder, server=True)
    setup.project.folder.mkdir(parents=True)
    (setup.project.folder / "warriors.lock").write_text("", encoding="utf-8")
    with pytest.raises(ConfigError, match="ghidra-mcp"):
        ghidra_setup.run_setup(setup, make_disc(tmp_path), LISTING, ghidra_setup.Options(sha1(EXECUTABLE)))
    assert recorder.commands == []


def test_no_jdk_says_what_to_install(tmp_path: Path) -> None:
    setup = make_setup(tmp_path, make_ghidra(tmp_path, sla=True), Recorder())
    setup = ghidra_setup.Setup(**{**setup.__dict__, "jdk": None})
    with pytest.raises(ConfigError, match="JDK 21"):
        ghidra_setup.run_setup(setup, make_disc(tmp_path), LISTING, ghidra_setup.Options(sha1(EXECUTABLE)))


def test_a_failed_step_names_its_log(tmp_path: Path) -> None:
    def failing(command: Sequence[str], env: Mapping[str, str], log: Path, cwd: Path) -> int:
        log.write_text("ERROR something\n", encoding="utf-8")
        return 1

    setup = make_setup(tmp_path, make_ghidra(tmp_path, sla=False), Recorder())
    setup = ghidra_setup.Setup(**{**setup.__dict__, "runner": failing})
    with pytest.raises(ConfigError, match=r"ghidra-sleigh\.log"):
        ghidra_setup.run_setup(setup, make_disc(tmp_path), LISTING, ghidra_setup.Options(sha1(EXECUTABLE)))
