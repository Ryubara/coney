# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools perf bench --scene` and `--compare`: the scene table, the runs, the comparison with a saved
baseline, the quiet check and the exit code (docs/engine/renderer.md#benchmark)."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

import pytest

from coney_tools.cli import _build_parser as build_parser
from coney_tools.config import ConfigError
from coney_tools.perf_cli import RUNS, run_scene_bench, scene_bench_command, scene_table
from coney_tools.perf_compare import (
    MIN_TOLERANCE,
    compare_scenes,
    format_comparison,
    median,
    quiet_holders,
    spread,
)
from coney_tools.perf_scenes import SCENES, find_scenes, scene_names

CONEY = Path("coney")


def _report(
    mean: float, *, gpu: float | None = None, render_size: str = "none", window: str = "960x720", level: str = "level95"
) -> str:
    """A made-up report as `coney --bench` writes it, with the meta this task adds."""
    spread_of = {"mean": mean, "p50": mean, "p95": mean + 1, "p99": mean + 2, "max": 40.0}
    zones = {"step": {"mean": 1.5, "p50": 1, "p95": 2, "p99": 2, "max": 3}}
    if gpu is not None:
        zones["gpu.frame"] = {"mean": gpu, "p50": gpu, "p95": gpu, "p99": gpu, "max": gpu}
    return json.dumps(
        {
            "meta": {"level": level, "render_size": render_size, "window": window, "build": "RelWithDebInfo"},
            "frames": 600,
            "frame_ms": spread_of,
            "zones_ms": zones,
            "counters": {"gl.draws": {"mean": 411.0, "p50": 1, "p95": 1, "p99": 1, "max": 1}},
        }
    )


def _write_runs(folder: Path, scene: str, means: list[float]) -> None:
    """Write one report per run for a scene, named as `perf bench --scene` names them."""
    folder.mkdir(parents=True, exist_ok=True)
    for number, mean in enumerate(means, start=1):
        (folder / f"scene-{scene}-run{number}.json").write_text(_report(mean), encoding="utf-8")


# -- the scene table


def test_the_table_has_the_scenes_the_design_names() -> None:
    names = scene_names()
    for expected in ("hub", "hub-crowd", "street"):
        assert expected in names
    assert len(names) == len(set(names))


def test_every_scene_names_what_it_runs() -> None:
    for scene in SCENES:
        assert scene.level.startswith("level")
        assert scene.fighters >= 0
        assert scene.frames > 0
        assert scene.warmup >= 0
        assert scene.checkpoint >= 1
        # A pinned camera is X,Y,Z and a quaternion (and a field of view): 7 or 8 numbers.
        assert scene.camera is not None
        assert len(scene.camera.split(",")) in (7, 8)
        if scene.render_size is not None:
            width, _, height = scene.render_size.partition("x")
            assert int(width) > 0 and int(height) > 0
            # A render size draws frames, so it needs a window.
            assert not scene.headless


def test_a_scene_list_is_names_or_all() -> None:
    assert [scene.name for scene in find_scenes("hub,street")] == ["hub", "street"]
    assert [scene.name for scene in find_scenes("all")] == list(scene_names())
    # A name repeated runs once; the order given is kept.
    assert [scene.name for scene in find_scenes("street,hub,street")] == ["street", "hub"]
    with pytest.raises(ValueError, match="no-such-scene"):
        find_scenes("hub,no-such-scene")
    with pytest.raises(ValueError):
        find_scenes("")


def test_the_scene_option_parses() -> None:
    args = build_parser().parse_args(["perf", "bench", "--out", "x", "--scene", "hub,hub-crowd"])
    assert [scene.name for scene in args.scene] == ["hub", "hub-crowd"]
    assert build_parser().parse_args(["perf", "bench", "--out", "x"]).scene is None
    every = build_parser().parse_args(["perf", "bench", "--out", "x", "--scene", "all", "--compare", "base"])
    assert len(every.scene) == len(SCENES)
    assert every.compare == Path("base")
    with pytest.raises(SystemExit):
        build_parser().parse_args(["perf", "bench", "--out", "x", "--scene", "nope"])


def test_a_scene_run_is_three_runs_of_each() -> None:
    assert RUNS == 3


def test_a_scene_command_pins_the_view_and_the_size() -> None:
    crowd = next(scene for scene in SCENES if scene.name == "hub-crowd")
    command = scene_bench_command(CONEY, "disc.iso", crowd, Path("out.json"))
    assert command[command.index("--play-level") + 1] == "level95"
    assert command[command.index("--checkpoint") + 1] == str(crowd.checkpoint)
    assert command[command.index("--camera") + 1] == crowd.camera
    assert command[command.index("--bench-fighters") + 1] == "50"
    assert command[command.index("--frames") + 1] == str(crowd.frames + crowd.warmup)
    assert "--bench-god" in command
    assert "--render-size" not in command
    sized = next(scene for scene in SCENES if scene.render_size is not None)
    sized_command = scene_bench_command(CONEY, "disc.iso", sized, Path("out.json"))
    assert sized_command[sized_command.index("--render-size") + 1] == sized.render_size


def test_a_headless_scene_has_no_window_options() -> None:
    headless = next(scene for scene in SCENES if scene.headless)
    command = scene_bench_command(CONEY, "disc.iso", headless, Path("out.json"))
    assert "--headless" in command
    assert "--vsync" not in command
    assert "--render-size" not in command


def test_the_display_preset_and_renderer_are_passed_through() -> None:
    scene = SCENES[0]
    plain = scene_bench_command(CONEY, "disc.iso", scene, Path("out.json"))
    assert "--renderer" not in plain
    assert "--display-preset" not in plain
    passed = scene_bench_command(CONEY, "disc.iso", scene, Path("out.json"), display_preset="enhanced", renderer="gpu")
    assert passed[passed.index("--renderer") + 1] == "gpu"
    assert passed[passed.index("--display-preset") + 1] == "enhanced"


# -- the runs


class FakeConey:
    """A stand-in for Coney: records the commands and writes the report each asks for."""

    def __init__(self, means: dict[str, float], *, fail_on: int | None = None) -> None:
        self.means = means
        self.fail_on = fail_on
        self.commands: list[list[str]] = []

    def __call__(self, command: list[str], log: Path) -> int:
        self.commands.append(command)
        log.write_text("log\n", encoding="utf-8")
        if self.fail_on is not None and len(self.commands) == self.fail_on:
            return 3
        report = Path(command[command.index("--bench") + 1])
        level = command[command.index("--play-level") + 1]
        size = command[command.index("--render-size") + 1] if "--render-size" in command else "none"
        window = "none" if "--headless" in command else (size if size != "none" else "960x720")
        mean = next(self.means[name] for name in self.means if name in str(report.name))
        report.write_text(_report(mean, gpu=0.5, render_size=size, window=window, level=level), encoding="utf-8")
        return 0


def _args(out: Path, scenes: str, compare: Path | None = None) -> argparse.Namespace:
    return argparse.Namespace(
        scene=find_scenes(scenes),
        compare=compare,
        out=out,
        display_preset=None,
        renderer=None,
        cpu_skinning=False,
        command="bench",
    )


def _run(
    tmp_path: Path, fake: FakeConey, scenes: str, compare: Path | None = None, holders: list[str] | None = None
) -> tuple[int, str]:
    """Run the scenes against the fake Coney with a machine whose busy holders are `holders`."""
    import contextlib
    import io

    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = run_scene_bench(
            _args(tmp_path / "out", scenes, compare),
            coney=CONEY,
            disc="disc.iso",
            run=fake,
            holders=lambda: list(holders or []),
        )
    return code, buffer.getvalue()


def test_every_scene_runs_three_times_interleaved(tmp_path: Path) -> None:
    fake = FakeConey({"scene-hub-run": 3.0, "scene-street-run": 2.0})
    code, text = _run(tmp_path, fake, "hub,street")
    assert code == 0
    assert len(fake.commands) == 2 * RUNS
    # Interleaved: run 1 of each scene, then run 2 of each, so a busy minute does not fall on one scene.
    reports = [Path(c[c.index("--bench") + 1]).name for c in fake.commands]
    assert reports == [
        "scene-hub-run1.json",
        "scene-street-run1.json",
        "scene-hub-run2.json",
        "scene-street-run2.json",
        "scene-hub-run3.json",
        "scene-street-run3.json",
    ]
    for name in reports:
        assert (tmp_path / "out" / name).is_file()
        assert (tmp_path / "out" / name.replace(".json", ".log")).is_file()
    # The table: one row per scene, the median of the three runs' means, the render size from the report's meta.
    assert "| Hub |" in text
    assert "3.00 (3.00-3.00)" in text
    assert "| 960x720 |" in text


def test_a_failed_run_stops_with_exit_1(tmp_path: Path) -> None:
    fake = FakeConey({"scene-hub-run": 3.0}, fail_on=2)
    code, text = _run(tmp_path, fake, "hub")
    assert code == 1
    assert "failed" in text
    assert len(fake.commands) == 2


def test_the_table_shows_gpu_time_and_the_window() -> None:
    from coney_tools.perf_cli import parse_report

    reports = {"hub": [parse_report(_report(3.0, gpu=0.5)) for _ in range(3)]}
    reports["hub-headless"] = [parse_report(_report(2.0, window="none")) for _ in range(3)]
    text = scene_table(reports)
    header = text.splitlines()[0]
    assert "GPU" in header
    assert "Render size" in header
    hub_row = next(line for line in text.splitlines() if line.startswith("| Hub |"))
    assert "0.50" in hub_row
    # A run with no GPU time (headless, or no timer queries) shows none, not zero.
    headless_row = next(line for line in text.splitlines() if line.startswith("| Hub, headless |"))
    assert "none" in headless_row


# -- the comparison


def test_median_and_spread() -> None:
    assert median([3.0, 1.0, 2.0]) == 2.0
    assert median([1.0, 2.0, 3.0, 10.0]) == 2.5
    assert spread([2.0, 2.2, 2.1]) == pytest.approx(0.1)
    assert spread([2.0]) == 0.0


def test_a_scene_within_the_budget_passes() -> None:
    rows = compare_scenes({"hub": [3.0, 3.05, 3.02]}, {"hub": [3.0, 3.1, 3.05]})
    assert len(rows) == 1
    assert rows[0].verdict == "ok"
    assert rows[0].change_percent == pytest.approx((3.02 / 3.05 - 1) * 100)


def test_the_budget_is_the_larger_of_five_percent_and_the_baselines_spread() -> None:
    base = {"hub": [3.0, 3.0, 3.0]}
    # 5 % of 3.0 is 3.15: a tight baseline allows exactly that.
    assert compare_scenes({"hub": [3.15] * 3}, base)[0].verdict == "ok"
    assert compare_scenes({"hub": [3.16] * 3}, base)[0].verdict == "over"
    assert MIN_TOLERANCE == 0.05
    # A noisy baseline (4.0 over 2.0: 100 % spread) widens it to twice the median.
    noisy = {"hub": [2.0, 3.0, 4.0]}
    assert compare_scenes({"hub": [5.9] * 3}, noisy)[0].verdict == "ok"
    row = compare_scenes({"hub": [6.1] * 3}, noisy)[0]
    assert row.verdict == "over"
    assert row.limit == pytest.approx(6.0)


def test_a_faster_scene_passes_and_a_scene_without_a_baseline_is_new() -> None:
    rows = compare_scenes({"hub": [2.0] * 3, "street": [1.0] * 3}, {"hub": [3.0] * 3})
    by_name = {row.scene: row for row in rows}
    assert by_name["hub"].verdict == "ok"
    assert by_name["hub"].change_percent < 0
    assert by_name["street"].verdict == "no baseline"
    assert by_name["street"].baseline is None


def test_the_comparison_prints_each_scene_beside_its_baseline() -> None:
    rows = compare_scenes({"hub": [3.3] * 3}, {"hub": [3.0] * 3})
    text = format_comparison(rows, {"hub": "Hub"}, [])
    line = next(line for line in text.splitlines() if line.startswith("| Hub |"))
    assert "3.00" in line
    assert "3.30" in line
    assert "+10.0 %" in line
    assert "over" in line
    assert "quiet" in text


# -- the quiet check


def _status(**claims: Any) -> dict[str, object]:
    """A `build status --json` object: the main lane's slots hold the given claims (None: free)."""
    slots = [{"slot": n, "stale": False, "claim": claim} for n, claim in enumerate(claims.values(), start=1)]
    return {"main": {"slots": slots, "queue": []}, "fast": {"slots": [], "queue": []}}


def test_a_machine_with_every_slot_free_is_quiet() -> None:
    assert quiet_holders(_status(a=None, b=None)) == []


def test_a_held_slot_is_named() -> None:
    claim = {"agent": "1207", "preset": "dev", "step": "build", "worktree": "w", "pid": 1, "started": "x"}
    holders = quiet_holders(_status(a=claim, b=None))
    assert len(holders) == 1
    assert "1207" in holders[0]
    assert "dev" in holders[0]


def test_a_stale_claim_does_not_make_the_machine_busy() -> None:
    claim = {"agent": "gone", "preset": "dev", "step": "build"}
    status = _status(a=claim)
    status["main"]["slots"][0]["stale"] = True  # type: ignore[index]
    assert quiet_holders(status) == []


def test_every_lane_counts() -> None:
    claim = {"agent": "suite", "preset": "dev", "step": "pytest"}
    status = _status(a=None)
    status["check"] = {"slots": [{"slot": 1, "stale": False, "claim": claim}], "queue": []}
    assert len(quiet_holders(status)) == 1


# -- the exit code


def test_over_the_budget_on_a_quiet_machine_exits_1(tmp_path: Path) -> None:
    _write_runs(tmp_path / "base", "hub", [3.0, 3.0, 3.0])
    fake = FakeConey({"scene-hub-run": 4.0})
    code, text = _run(tmp_path, fake, "hub", compare=tmp_path / "base")
    assert code == 1
    assert "over" in text
    assert "quiet" in text


def test_over_the_budget_on_a_busy_machine_reports_and_exits_0(tmp_path: Path) -> None:
    _write_runs(tmp_path / "base", "hub", [3.0, 3.0, 3.0])
    fake = FakeConey({"scene-hub-run": 4.0})
    code, text = _run(tmp_path, fake, "hub", compare=tmp_path / "base", holders=["1207 (dev, build, main lane)"])
    assert code == 0
    assert "over" in text
    assert "1207 (dev, build, main lane)" in text
    assert "not quiet" in text


def test_within_the_budget_exits_0(tmp_path: Path) -> None:
    _write_runs(tmp_path / "base", "hub", [3.0, 3.0, 3.0])
    code, _ = _run(tmp_path, FakeConey({"scene-hub-run": 3.1}), "hub", compare=tmp_path / "base")
    assert code == 0


def test_a_baseline_without_the_scene_is_not_a_failure(tmp_path: Path) -> None:
    _write_runs(tmp_path / "base", "street", [2.0, 2.0, 2.0])
    code, text = _run(tmp_path, FakeConey({"scene-hub-run": 9.0}), "hub", compare=tmp_path / "base")
    assert code == 0
    assert "no baseline" in text


def test_a_baseline_folder_with_no_reports_is_refused(tmp_path: Path) -> None:
    (tmp_path / "empty").mkdir()
    with pytest.raises(ConfigError, match="baseline"):
        _run(tmp_path, FakeConey({"scene-hub-run": 3.0}), "hub", compare=tmp_path / "empty")
    with pytest.raises(ConfigError, match="baseline"):
        _run(tmp_path, FakeConey({"scene-hub-run": 3.0}), "hub", compare=tmp_path / "missing")


def test_a_busy_minute_during_the_runs_also_spoils_the_gate(tmp_path: Path) -> None:
    _write_runs(tmp_path / "base", "hub", [3.0, 3.0, 3.0])
    calls = iter([[], ["1207 (dev, build, main lane)"]])
    import contextlib
    import io

    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        code = run_scene_bench(
            _args(tmp_path / "out", "hub", tmp_path / "base"),
            coney=CONEY,
            disc="disc.iso",
            run=FakeConey({"scene-hub-run": 4.0}),
            holders=lambda: next(calls),
        )
    assert code == 0
    assert "1207" in buffer.getvalue()
