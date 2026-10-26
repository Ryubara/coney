# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools perf`: reading `coney --bench` reports, the table and the bench command line."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from coney_tools.cli import _build_parser as build_parser
from coney_tools.config import ConfigError
from coney_tools.perf_cli import bench_command, parse_report, table


def _report(fighters: int, frame_mean: float, *, headless: bool = False) -> str:
    """A made-up report as `coney --bench` writes it."""
    spread = {"mean": frame_mean, "p50": frame_mean, "p95": frame_mean + 1, "p99": frame_mean + 2, "max": 40.0}
    return json.dumps(
        {
            "meta": {
                "level": "level99",
                "build": "RelWithDebInfo",
                "headless": "yes" if headless else "no",
                "renderer": "Test GPU, OpenGL 4.6",
                "fighters_spawned": str(fighters),
            },
            "frames": 600,
            "frame_ms": spread,
            "zones_ms": {"step": {"mean": 1.5, "p50": 1, "p95": 2, "p99": 2, "max": 3}},
            "counters": {
                "gl.draws": {"mean": 1234.4, "p50": 1, "p95": 1, "p99": 1, "max": 1},
                "gl.upload_bytes": {"mean": 2048.0, "p50": 1, "p95": 1, "p99": 1, "max": 1},
            },
        }
    )


def test_a_report_is_read_back() -> None:
    report = parse_report(_report(10, 8.0))
    assert report.meta["fighters_spawned"] == "10"
    assert report.frames == 600
    assert report.frame_ms["p99"] == 10.0
    assert report.zones_ms["step"]["mean"] == 1.5
    assert report.counters["gl.draws"]["mean"] == 1234.4


def test_a_report_that_is_not_one_is_refused() -> None:
    with pytest.raises(ConfigError):
        parse_report("{}")
    with pytest.raises(ConfigError):
        parse_report("not json")


def test_the_table_has_a_row_per_report_and_names_its_source() -> None:
    text = table([parse_report(_report(0, 4.0)), parse_report(_report(100, 20.0))])
    lines = text.splitlines()
    assert lines[0].startswith("| fighters | frame ms | p95 | p99 | fps | step |")
    assert lines[2].startswith("| 0 | 4.00 | 5.00 | 6.00 | 250 | 1.50 |")
    assert lines[3].startswith("| 100 | 20.00 | 21.00 | 22.00 | 50 | 1.50 |")
    # A zone the run never entered is a dash; bytes are shown as KB; counts are whole.
    assert "| 1,234 |" in lines[3]
    assert "| 2 |" in lines[3]
    assert "Source: RelWithDebInfo build, level99, 600 frames measured, renderer Test GPU, OpenGL 4.6" in text


def test_the_bench_command_plays_the_level_with_the_fighters() -> None:
    windowed = bench_command(Path("coney"), "disc.iso", "level99", 50, 600, 60, False, Path("out.json"))
    assert windowed == [
        "coney",
        "--disc",
        "disc.iso",
        "--play-level",
        "level99",
        "--skip-movies",
        "--frames",
        "660",
        "--bench",
        "out.json",
        "--bench-fighters",
        "50",
        "--bench-warmup",
        "60",
        "--vsync",
        "off",
        "--no-activate",
    ]
    headless = bench_command(Path("coney"), "disc.iso", "level99", 0, 100, 10, True, Path("out.json"))
    assert headless[-1] == "--headless"
    assert "--vsync" not in headless
    assert "--cpu-skinning" not in windowed
    cpu = bench_command(Path("coney"), "disc.iso", "level95", 10, 600, 60, False, Path("out.json"), cpu_skinning=True)
    assert cpu[-1] == "--cpu-skinning"


def test_the_perf_commands_parse() -> None:
    args = build_parser().parse_args(["perf", "bench", "--out", "x", "--fighters", "0,100", "--headless"])
    assert args.fighters == [0, 100]
    assert args.headless
    # The release preset has no debug tools, so no --bench: the optimised build with them is playtest's.
    assert args.preset == "playtest"
    assert not args.cpu_skinning
    assert build_parser().parse_args(["perf", "bench", "--out", "x", "--cpu-skinning"]).cpu_skinning
    with pytest.raises(SystemExit):
        build_parser().parse_args(["perf", "bench", "--out", "x", "--fighters", "1001"])
    table_args = build_parser().parse_args(["perf", "table", "a.json", "b.json"])
    assert table_args.reports == [Path("a.json"), Path("b.json")]
