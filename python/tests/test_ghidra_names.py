# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.ghidra_names: `coney-tools ghidra import-names` with a stand-in for analyzeHeadless."""

from collections.abc import Mapping, Sequence
from pathlib import Path

import pytest

from coney_tools import ghidra_cli, ghidra_headless, ghidra_names
from coney_tools.cli import main
from coney_tools.config import ConfigError
from coney_tools.ghidra_headless import GhidraProject
from coney_tools.progress_research import ListedFunction

LISTING = [
    ListedFunction(0x00100008, 448, "entry", False),
    ListedFunction(0x001001C8, 8, "", False),
    ListedFunction(0x00100200, 64, "Ped_Update", True),
    ListedFunction(0x00100300, 32, "FUN_00100300", False),
    ListedFunction(0x00100400, 16, "Cam_Reset", False),
    ListedFunction(0x00100500, 24, "Hud_Draw", False),
]


def test_names_input_lists_only_the_named_functions() -> None:
    text = ghidra_names.names_input(LISTING)
    rows = [line.split("\t") for line in text.splitlines() if not line.startswith("#")]
    assert rows == [
        ["0x00100008", "448", "entry"],
        ["0x00100200", "64", "Ped_Update"],
        ["0x00100400", "16", "Cam_Reset"],
        ["0x00100500", "24", "Hud_Draw"],
    ]


def test_parse_report_reads_outcome_and_size() -> None:
    report = ghidra_names.parse_report("0x00100008\tsame\t448\n0x00100400\tsize\t20\n\n")
    assert report == {0x00100008: ("same", 448), 0x00100400: ("size", 20)}


def test_parse_report_refuses_an_unknown_outcome() -> None:
    with pytest.raises(ConfigError, match="unknown outcome"):
        ghidra_names.parse_report("0x00100008\tweird\t448\n")


def test_summary_groups_outcomes_into_matched_created_skipped() -> None:
    report = {
        0x00100008: ("same", 448),
        0x00100200: ("renamed", 60),
        0x00100400: ("created", 16),
        0x00100500: ("inside", -1),
    }
    summary = ghidra_names.summarize(LISTING, report)
    assert (summary.names, summary.matched, summary.created, summary.skipped) == (4, 2, 1, 1)
    # Ped_Update was renamed although Ghidra's body is 60 bytes, not the listed 64: counted, not refused.
    assert summary.size_differs == 1
    assert summary.counts["inside"] == 1


def test_summary_refuses_a_report_that_misses_a_name() -> None:
    with pytest.raises(ConfigError, match="report has 1 of 4"):
        ghidra_names.summarize(LISTING, {0x00100008: ("same", 448)})


def test_import_command_runs_the_script_on_the_existing_program(tmp_path: Path) -> None:
    project = GhidraProject(tmp_path / "ghidra", "warriors")
    command = ghidra_names.import_command(
        Path("analyzeHeadless"), project, "SLUS_212.15", tmp_path / "in.tsv", tmp_path / "out.tsv", "apply", False
    )
    assert command[:5] == ["analyzeHeadless", str(tmp_path / "ghidra"), "warriors", "-process", "SLUS_212.15"]
    assert "-noanalysis" in command and "-readOnly" not in command
    post = command.index("-postScript")
    assert command[post + 1 :] == ["ImportNames.java", str(tmp_path / "in.tsv"), str(tmp_path / "out.tsv"), "apply"]
    assert command[command.index("-scriptPath") + 1] == str(ghidra_names.SCRIPT_DIR)


def test_import_command_dry_run_is_read_only_before_the_script_arguments(tmp_path: Path) -> None:
    project = GhidraProject(tmp_path, "warriors")
    command = ghidra_names.import_command(
        Path("a"), project, "SLUS_212.15", tmp_path / "i", tmp_path / "o", "apply", True
    )
    assert command.index("-readOnly") < command.index("-postScript")


def test_the_script_ships_with_the_package() -> None:
    script = ghidra_names.SCRIPT_DIR / ghidra_names.SCRIPT
    text = script.read_text(encoding="utf-8")
    assert "public class ImportNames extends GhidraScript" in text
    # Every outcome the script writes is one the reader knows.
    for outcome in ghidra_names.OUTCOMES:
        assert f'"{outcome}' in text


class FakeHeadless:
    """Stands in for analyzeHeadless: answers every name of the input file with one outcome."""

    def __init__(self, outcome: str = "renamed", code: int = 0, write: bool = True) -> None:
        self.outcome = outcome
        self.code = code
        self.write = write
        self.commands: list[list[str]] = []
        self.envs: list[Mapping[str, str]] = []

    def __call__(self, command: Sequence[str], env: Mapping[str, str], log: Path, cwd: Path) -> int:
        self.commands.append(list(command))
        self.envs.append(env)
        log.write_text("headless log\n", encoding="utf-8")
        post = command.index("-postScript")
        names, report = Path(command[post + 2]), Path(command[post + 3])
        if self.write:
            rows = [line.split("\t") for line in names.read_text(encoding="utf-8").splitlines() if line[:1] != "#"]
            report.write_text("".join(f"{r[0]}\t{self.outcome}\t{r[1]}\n" for r in rows), encoding="utf-8")
        return self.code


def make_install(tmp_path: Path) -> Path:
    """A made-up Ghidra install with both launchers."""
    support = tmp_path / "ghidra_install" / "support"
    support.mkdir(parents=True)
    for name in ("analyzeHeadless", "analyzeHeadless.bat"):
        (support / name).write_text("", encoding="utf-8")
    return tmp_path / "ghidra_install"


def test_import_names_runs_headless_and_counts(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    fake = FakeHeadless("renamed")
    project = GhidraProject(tmp_path / "proj", "warriors")
    summary = ghidra_names.import_names(
        ghidra_names.ImportJob(project, "SLUS_212.15", make_install(tmp_path), tmp_path / "jdk", tmp_path / "work"),
        LISTING,
        fake,
    )
    assert (summary.matched, summary.created, summary.skipped) == (4, 0, 0)
    assert fake.envs[0]["JAVA_HOME"] == str(tmp_path / "jdk")
    # The input and report files are temporary: only the log stays in the work folder.
    assert sorted(p.name for p in (tmp_path / "work").iterdir()) == ["ghidra-import-names.log"]


def test_import_names_without_a_report_names_the_log(tmp_path: Path) -> None:
    fake = FakeHeadless(code=1, write=False)
    job = ghidra_names.ImportJob(
        GhidraProject(tmp_path / "p", "w"), "SLUS_212.15", make_install(tmp_path), tmp_path / "jdk", tmp_path / "w"
    )
    with pytest.raises(ConfigError, match=r"ghidra-import-names\.log"):
        ghidra_names.import_names(job, LISTING, fake)


def test_format_summary_prints_counts_only() -> None:
    report = {f.address: ("created", f.size) for f in LISTING if f.named}
    lines = ghidra_names.format_summary(ghidra_names.summarize(LISTING, report), dry_run=True)
    assert lines[0].startswith("import-names (dry run, nothing saved): 4 names")
    assert any("created 4" in line for line in lines)
    assert not any("Ped_Update" in line for line in lines)


def test_analyze_headless_picks_the_launcher_for_the_platform(tmp_path: Path) -> None:
    install = make_install(tmp_path)
    assert ghidra_headless.analyze_headless(install, "windows").name == "analyzeHeadless.bat"
    assert ghidra_headless.analyze_headless(install, "linux").name == "analyzeHeadless"
    with pytest.raises(ConfigError, match="analyzeHeadless"):
        ghidra_headless.analyze_headless(tmp_path / "nothing", "linux")


def test_cli_import_names_needs_an_imported_program(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    checkout = tmp_path / "repos" / "coney"
    (checkout / "docs" / "progress").mkdir(parents=True)
    (checkout / "coney.local.example.toml").write_text("", encoding="utf-8")
    (checkout / "docs" / "progress" / "ghidra-functions.tsv").write_text(
        "0x00100008\t448\tentry\t0\n", encoding="utf-8"
    )
    monkeypatch.chdir(checkout)
    monkeypatch.setattr(ghidra_cli, "find_jdk", lambda *args: tmp_path / "jdk")
    code = main(["ghidra", "import-names", "--project-dir", str(tmp_path / "proj"), "--dry-run"])
    assert code == 2
    assert "run `coney-tools ghidra setup` first" in capsys.readouterr().err
