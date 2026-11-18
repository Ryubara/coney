# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools perf ...` commands: run Coney's `--bench` over several crowd sizes and tabulate the reports.

`perf bench` plays one level for a fixed number of frames once per fighter count, each run spawning that many AI
fighters on the first frame of play (`coney --bench`, docs/guides/performance.md), and prints one Markdown table of
the frame times, the main zones and the counters. `perf table` prints the same table from reports already written.
The runs are lockstep test-mode runs, so two builds measured with the same command did the same work; only the times
depend on the machine, which is why the table says which build and renderer each row came from.

`perf bench --scene` runs named scenes instead (perf_scenes.py): each scene three times, the scenes interleaved, a table
of the medians, and with `--compare FOLDER` the comparison with a saved baseline that fails past the budget on a quiet
machine (perf_compare.py, docs/engine/renderer.md#benchmark).
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from coney_tools import build_cli, build_root, perf_compare
from coney_tools.config import ConfigError, find_repo_root, load_config
from coney_tools.perf_scenes import SCENES, Scene, find_scenes

DEFAULT_FIGHTERS = (0, 10, 25, 50, 100)
DEFAULT_FRAMES = 600
DEFAULT_WARMUP = 60
DEFAULT_LEVEL = "level95"  # the hub: free play from the first frame, no scene holding the crowd back
RUNS = 3  # how many times each scene runs: its figure is the median of them

# The zones a table shows, in order, with their column titles: the frame's parts, then the humans' update and the
# drawing of them (the rest are in the reports).
ZONE_COLUMNS = (
    ("step", "step"),
    ("humans.brains", "brains"),
    ("humans.state", "move"),
    ("humans.actions", "actions"),
    ("render", "render"),
    ("render.skin", "skin"),
    ("render.humans", "draw humans"),
    ("present.swap", "swap"),
)
# The counters a table shows, per frame, with their column titles.
COUNTER_COLUMNS = (
    ("gl.draws", "draws"),
    ("gl.triangles", "triangles"),
    ("gl.upload_bytes", "upload KB"),
    ("alloc.count", "allocs"),
)


@dataclass(frozen=True)
class Report:
    """One `--bench` report: its metadata, frame-time spread, zones (ms) and counters (per frame), by name."""

    meta: dict[str, str]
    frames: int
    frame_ms: dict[str, float]
    zones_ms: dict[str, dict[str, float]]
    counters: dict[str, dict[str, float]]


def parse_report(text: str) -> Report:
    """A report from the JSON `coney --bench` wrote; ConfigError when it is not one."""
    try:
        data: dict[str, Any] = json.loads(text)
        return Report(
            meta={str(key): str(value) for key, value in data["meta"].items()},
            frames=int(data["frames"]),
            frame_ms={str(key): float(value) for key, value in data["frame_ms"].items()},
            zones_ms={str(name): {k: float(v) for k, v in spread.items()} for name, spread in data["zones_ms"].items()},
            counters={str(name): {k: float(v) for k, v in spread.items()} for name, spread in data["counters"].items()},
        )
    except (ValueError, KeyError, TypeError, AttributeError) as error:
        raise ConfigError(f"not a bench report: {error}") from error


def _zone_mean(report: Report, name: str) -> str:
    """A zone's mean in ms with two decimals, or a dash when the run never entered it."""
    spread = report.zones_ms.get(name)
    return f"{spread['mean']:.2f}" if spread else "-"


def _counter_mean(report: Report, name: str) -> str:
    """A counter's mean per frame, whole (bytes as KB), or a dash when the run had none."""
    spread = report.counters.get(name)
    if not spread:
        return "-"
    value = spread["mean"] / 1024 if name.endswith("bytes") else spread["mean"]
    return f"{value:,.0f}"


def table(reports: list[Report]) -> str:
    """A Markdown table of the reports, one row each in the order given, then a line naming the builds and
    renderers they came from."""
    header = ["fighters", "frame ms", "p95", "p99", "fps"]
    header += [title for _, title in ZONE_COLUMNS] + [title for _, title in COUNTER_COLUMNS]
    lines = ["| " + " | ".join(header) + " |", "|" + " --- |" * len(header)]
    for report in reports:
        mean = report.frame_ms.get("mean", 0.0)
        row = [
            report.meta.get("fighters_spawned", "?"),
            f"{mean:.2f}",
            f"{report.frame_ms.get('p95', 0.0):.2f}",
            f"{report.frame_ms.get('p99', 0.0):.2f}",
            f"{1000.0 / mean:.0f}" if mean > 0 else "-",
        ]
        row += [_zone_mean(report, name) for name, _ in ZONE_COLUMNS]
        row += [_counter_mean(report, name) for name, _ in COUNTER_COLUMNS]
        lines.append("| " + " | ".join(row) + " |")
    sources = sorted(
        {
            f"{r.meta.get('build', '?')} build, {r.meta.get('level', '?')}, {r.frames} frames measured"
            f"{' headless' if r.meta.get('headless') == 'yes' else ''}, renderer {r.meta.get('renderer', '?')}"
            f"{', skinning ' + r.meta['skinning'] if 'skinning' in r.meta else ''}"
            for r in reports
        }
    )
    return "\n".join(lines) + "\n\n" + "\n".join(f"Source: {source}" for source in sources) + "\n"


def bench_command(
    coney: Path,
    disc: str,
    level: str,
    fighters: int,
    frames: int,
    warmup: int,
    headless: bool,
    report: Path,
    *,
    cpu_skinning: bool = False,
) -> list[str]:
    """The command line of one bench run: no movies first; windowed runs present without waiting for the display
    (`--vsync off`); `cpu_skinning` skins the humans on the CPU, to compare with the GPU's skinning."""
    command = [str(coney), "--disc", disc, "--play-level", level, "--skip-movies", "--frames", str(frames + warmup)]
    command += ["--bench", str(report), "--bench-fighters", str(fighters), "--bench-warmup", str(warmup)]
    command += ["--headless"] if headless else ["--vsync", "off", "--no-activate"]
    command += ["--cpu-skinning"] if cpu_skinning else []
    return command


def _coney_executable(given: Path | None, preset: str) -> Path:
    """Coney's program: the one given, else the preset's build in the checkout."""
    if given is not None:
        return given
    root = find_repo_root(Path.cwd())
    for name in ("coney.exe", "coney"):
        candidate = build_root.preset_folder(root, preset) / "src" / "platform" / name
        if candidate.is_file():
            return candidate
    raise ConfigError(f"no Coney build at build/{preset}/src/platform/; build the {preset} preset or pass --coney")


def _disc(given: str | None) -> str:
    """The disc for Coney: the one given, else game_dir from coney.local.toml."""
    if given is not None:
        return given
    game = load_config(find_repo_root(Path.cwd())).paths["game_dir"]
    if game is None:
        raise ConfigError("no disc: pass --disc or set game_dir in coney.local.toml")
    images = sorted(game.glob("*.iso")) if game.is_dir() else [game]
    return str(images[0] if len(images) == 1 else game)


def run_bench(args: argparse.Namespace) -> int:
    """Run the bench once per fighter count, keep the reports and logs in --out, print the table; 1 on a failed run."""
    coney = _coney_executable(args.coney, args.preset)
    disc = _disc(args.disc)
    if args.scene:
        return run_scene_bench(args, coney=coney, disc=disc)
    if args.compare:
        raise ConfigError("--compare compares scenes: name them with --scene (or --scene all)")
    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)
    reports = []
    for fighters in args.fighters:
        stem = (
            f"bench-{args.level}-{fighters}{'-headless' if args.headless else ''}{'-cpu' if args.cpu_skinning else ''}"
        )
        report_path = (out / f"{stem}.json").resolve()
        command = bench_command(
            coney,
            disc,
            args.level,
            fighters,
            args.frames,
            args.warmup,
            args.headless,
            report_path,
            cpu_skinning=args.cpu_skinning,
        )
        print(f"perf: {fighters} fighters ...", flush=True)
        with (out / f"{stem}.log").open("w", encoding="utf-8") as log:
            done = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
        if done.returncode != 0 or not report_path.is_file():
            print(f"perf: the run with {fighters} fighters failed (exit {done.returncode}); see {out / stem}.log")
            return 1
        reports.append(parse_report(report_path.read_text(encoding="utf-8")))
    print(table(reports), end="")
    return 0


def scene_bench_command(
    coney: Path,
    disc: str,
    scene: Scene,
    report: Path,
    *,
    cpu_skinning: bool = False,
    display_preset: str | None = None,
    renderer: str | None = None,
) -> list[str]:
    """The command line of one run of a scene: the plain bench run, then the scene's checkpoint, pinned camera, player
    spot, god mode (a crowd must not kill the player), render size, and `--display-preset` / `--renderer` when asked."""
    command = bench_command(
        coney,
        disc,
        scene.level,
        scene.fighters,
        scene.frames,
        scene.warmup,
        scene.headless,
        report,
        cpu_skinning=cpu_skinning,
    )
    command += ["--checkpoint", str(scene.checkpoint), "--camera", scene.camera]
    command += ["--bench-place", scene.place] if scene.place else []
    command += ["--bench-god"] if scene.fighters > 0 else []
    command += ["--render-size", scene.render_size] if scene.render_size else []
    command += ["--display-preset", display_preset] if display_preset else []
    command += ["--renderer", renderer] if renderer else []
    return command


def _runs_of(reports: list[Report], pick: Callable[[Report], float | None]) -> str:
    """A table cell: the median over the runs of what `pick` reads, with the fastest and slowest run in brackets (as
    the renderer design's tables); `none` when a run lacks it."""
    values = [pick(report) for report in reports]
    if not values or any(value is None for value in values):
        return "none"
    found = [value for value in values if value is not None]
    return f"{perf_compare.median(found):.2f} ({min(found):.2f}-{max(found):.2f})"


def _count_of(reports: list[Report], name: str) -> str:
    """A counter's median over the runs, whole with thousands separators, or `none`."""
    values = [report.counters[name]["mean"] if name in report.counters else None for report in reports]
    if not values or any(value is None for value in values):
        return "none"
    return f"{perf_compare.median([value for value in values if value is not None]):,.0f}"


def _zone(name: str) -> Callable[[Report], float | None]:
    """Reads a zone's mean ms from a report; None when the run never entered it."""
    return lambda report: report.zones_ms[name]["mean"] if name in report.zones_ms else None


def _size_of(report: Report) -> str:
    """The frame size a run drew at: the render size when one was named, else the window, `none` when headless."""
    size = report.meta.get("render_size", "none")
    return size if size != "none" else report.meta.get("window", "none")


def scene_table(scenes: dict[str, list[Report]]) -> str:
    """A Markdown table of the scenes' runs, one row each in the order given: the median over the runs of each run's
    mean (fastest and slowest in brackets), in ms a frame; draws and triangles a frame. A time a run lacks (the GPU's
    on a machine without timer queries, anything headless) is `none`."""
    by_name = {scene.name: scene for scene in SCENES}
    header = [
        "Scene",
        "Fighters",
        "Render size",
        "Frame",
        "p95",
        "Step",
        "Render",
        "GPU",
        "Present",
        "Draws",
        "Triangles",
    ]
    lines = ["| " + " | ".join(header) + " |", "|" + " --- |" * len(header)]
    for name, reports in scenes.items():
        known = by_name.get(name)
        row = [
            known.title if known else name,
            str(known.fighters) if known else reports[0].meta.get("fighters_spawned", "?"),
            _size_of(reports[0]),
            _runs_of(reports, lambda r: r.frame_ms.get("mean")),
            _runs_of(reports, lambda r: r.frame_ms.get("p95")),
            _runs_of(reports, _zone("step")),
            _runs_of(reports, _zone("render")),
            _runs_of(reports, _zone("gpu.frame")),
            _runs_of(reports, _zone("present")),
            _count_of(reports, "gl.draws"),
            _count_of(reports, "gl.triangles"),
        ]
        lines.append("| " + " | ".join(row) + " |")
    return "\n".join(lines) + "\n"


SCENE_REPORT = re.compile(r"^scene-(?P<name>.+)-run(?P<number>\d+)\.json$")


def load_scene_reports(folder: Path) -> dict[str, list[Report]]:
    """The scene reports in a folder (`scene-NAME-runN.json`, as `perf bench --scene` writes them), by scene name with
    the runs in order; ConfigError when the folder has none (it is the baseline of `--compare`)."""
    found: dict[str, list[tuple[int, Report]]] = {}
    for path in sorted(folder.glob("scene-*-run*.json")) if folder.is_dir() else []:
        match = SCENE_REPORT.match(path.name)
        if match:
            report = parse_report(path.read_text(encoding="utf-8"))
            found.setdefault(match["name"], []).append((int(match["number"]), report))
    if not found:
        raise ConfigError(
            f"no baseline: {folder} holds no scene reports (scene-NAME-runN.json); record one on a quiet machine with "
            f"`coney-tools perf bench --scene all --out {folder}`"
        )
    return {name: [report for _, report in sorted(runs, key=lambda run: run[0])] for name, runs in found.items()}


def _run_coney(command: list[str], log: Path) -> int:
    """Run one bench command with its output in `log`; its exit code."""
    with log.open("w", encoding="utf-8") as handle:
        return subprocess.run(command, stdout=handle, stderr=subprocess.STDOUT, check=False).returncode


def _busy_holders() -> list[str]:
    """Who holds a build slot now. When the slots cannot be read the machine is not known to be quiet, so that is
    reported as the holder."""
    try:
        return perf_compare.quiet_holders(build_cli.status_json(find_repo_root(Path.cwd())))
    except (ConfigError, OSError, ValueError) as error:
        return [f"unknown (the build slots could not be read: {error})"]


def run_scene_bench(
    args: argparse.Namespace,
    *,
    coney: Path,
    disc: str,
    run: Callable[[list[str], Path], int] = _run_coney,
    holders: Callable[[], list[str]] = _busy_holders,
) -> int:
    """Run each scene `RUNS` times, the scenes interleaved (run 1 of each, then run 2 of each, so a busy minute does
    not fall on one scene), keep the reports and logs in --out, print the table of medians. With --compare also print
    each scene beside the baseline's; exit 1 when a run fails, or a scene is over the budget on a quiet machine (no
    build held a slot before the runs or after them)."""
    scenes: list[Scene] = args.scene
    baseline = load_scene_reports(args.compare) if args.compare else None
    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)
    busy_before = holders() if baseline else []
    reports: dict[str, list[Report]] = {scene.name: [] for scene in scenes}
    for number in range(1, RUNS + 1):
        for scene in scenes:
            stem = f"scene-{scene.name}-run{number}"
            report_path = (out / f"{stem}.json").resolve()
            command = scene_bench_command(
                coney,
                disc,
                scene,
                report_path,
                cpu_skinning=args.cpu_skinning,
                display_preset=args.display_preset,
                renderer=args.renderer,
            )
            print(f"perf: scene {scene.name}, run {number} of {RUNS} ...", flush=True)
            code = run(command, out / f"{stem}.log")
            if code != 0 or not report_path.is_file():
                print(f"perf: scene {scene.name} run {number} failed (exit {code}); see {out / stem}.log")
                return 1
            reports[scene.name].append(parse_report(report_path.read_text(encoding="utf-8")))
    print(scene_table(reports), end="")
    if baseline is None:
        return 0
    busy = busy_before + [holder for holder in holders() if holder not in busy_before]
    rows = perf_compare.compare_scenes(
        {name: [r.frame_ms["mean"] for r in runs] for name, runs in reports.items()},
        {name: [r.frame_ms["mean"] for r in runs] for name, runs in baseline.items()},
    )
    print(perf_compare.format_comparison(rows, {scene.name: scene.title for scene in scenes}, busy), end="")
    return perf_compare.exit_code(rows, busy)


def run_table(paths: list[Path]) -> int:
    """Print the table of reports already written."""
    print(table([parse_report(path.read_text(encoding="utf-8")) for path in paths]), end="")
    return 0


def _fighter_counts(text: str) -> list[int]:
    """`0,10,25` as a list of whole numbers from 0 to 1000."""
    try:
        counts = [int(part) for part in text.split(",") if part.strip()]
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"not a list of numbers: {text}") from error
    if not counts or any(count < 0 or count > 1000 for count in counts):
        raise argparse.ArgumentTypeError("fighter counts are whole numbers from 0 to 1000")
    return counts


def _scene_list(text: str) -> list[Scene]:
    """`hub,street` or `all` as scenes of the table."""
    try:
        return find_scenes(text)
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error


def add_parser(groups: argparse._SubParsersAction[argparse.ArgumentParser]) -> None:
    """Register `coney-tools perf bench` and `perf table`."""
    group = groups.add_parser("perf", help="performance: benchmark runs and their tables (docs/guides/performance.md)")
    commands = group.add_subparsers(dest="command", required=True)
    bench = commands.add_parser(
        "bench",
        help="play a level once per fighter count with `coney --bench` and print a table of the reports",
    )
    bench.add_argument("--coney", type=Path, help="Coney's executable; default: build/PRESET/src/platform/coney")
    bench.add_argument(
        "--preset",
        default="playtest",
        help="the build folder to take Coney from (default playtest: optimised, with the debug tools --bench needs)",
    )
    bench.add_argument("--disc", help="the disc; default: game_dir from coney.local.toml")
    bench.add_argument("--level", default=DEFAULT_LEVEL, help=f"the level to play (default {DEFAULT_LEVEL})")
    bench.add_argument(
        "--fighters",
        type=_fighter_counts,
        default=list(DEFAULT_FIGHTERS),
        help="the fighter counts, comma-separated (default 0,10,25,50,100)",
    )
    bench.add_argument("--frames", type=int, default=DEFAULT_FRAMES, help="frames measured per run (default 600)")
    bench.add_argument("--warmup", type=int, default=DEFAULT_WARMUP, help="frames before measuring (default 60)")
    bench.add_argument("--headless", action="store_true", help="no window: the simulation's cost alone")
    bench.add_argument(
        "--cpu-skinning",
        action="store_true",
        help="skin the humans on the CPU (coney --cpu-skinning), to compare with the GPU's skinning",
    )
    bench.add_argument(
        "--scene",
        type=_scene_list,
        help="run named scenes (comma-separated, or all) three times each instead of the fighter counts: "
        + ", ".join(scene.name for scene in SCENES)
        + "; the scene sets the level, fighters, frames and size",
    )
    bench.add_argument(
        "--compare",
        type=Path,
        metavar="FOLDER",
        help="with --scene: print each scene's median beside the baseline's reports in FOLDER; exit 1 when a scene is "
        "past the budget, if no other build holds a build slot",
    )
    bench.add_argument("--display-preset", help="with --scene: coney --display-preset NAME")
    bench.add_argument("--renderer", help="with --scene: coney --renderer NAME (gl3 or gpu)")
    bench.add_argument("--out", type=Path, required=True, help="the folder for the reports and logs (scratch)")
    table_parser = commands.add_parser("table", help="print the table of bench reports already written")
    table_parser.add_argument("reports", type=Path, nargs="+", help="the JSON reports, in row order")


def run(args: argparse.Namespace) -> int:
    """Dispatch `coney-tools perf ...`."""
    if args.command == "bench":
        return run_bench(args)
    return run_table(args.reports)
