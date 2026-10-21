# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools perf ...` commands: run Coney's `--bench` over several crowd sizes and tabulate the reports.

`perf bench` plays one level for a fixed number of frames once per fighter count, each run spawning that many AI
fighters on the first frame of play (`coney --bench`, docs/guides/performance.md), and prints one Markdown table of
the frame times, the main zones and the counters. `perf table` prints the same table from reports already written.
The runs are lockstep test-mode runs, so two builds measured with the same command did the same work; only the times
depend on the machine, which is why the table says which build and renderer each row came from.
"""

from __future__ import annotations

import argparse
import json
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from coney_tools.config import ConfigError, find_repo_root, load_config

DEFAULT_FIGHTERS = (0, 10, 25, 50, 100)
DEFAULT_FRAMES = 600
DEFAULT_WARMUP = 60
DEFAULT_LEVEL = "level95"  # the hub: free play from the first frame, no scene holding the crowd back

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
) -> list[str]:
    """The command line of one bench run: no movies first; windowed runs present without waiting for the display
    (`--vsync off`)."""
    command = [str(coney), "--disc", disc, "--play-level", level, "--skip-movies", "--frames", str(frames + warmup)]
    command += ["--bench", str(report), "--bench-fighters", str(fighters), "--bench-warmup", str(warmup)]
    command += ["--headless"] if headless else ["--vsync", "off", "--no-activate"]
    return command


def _coney_executable(given: Path | None, preset: str) -> Path:
    """Coney's program: the one given, else the preset's build in the checkout."""
    if given is not None:
        return given
    root = find_repo_root(Path.cwd())
    for name in ("coney.exe", "coney"):
        candidate = root / "build" / preset / "src" / "platform" / name
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
    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)
    reports = []
    for fighters in args.fighters:
        stem = f"bench-{args.level}-{fighters}{'-headless' if args.headless else ''}"
        report_path = (out / f"{stem}.json").resolve()
        command = bench_command(
            coney, disc, args.level, fighters, args.frames, args.warmup, args.headless, report_path
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


def add_parser(groups: argparse._SubParsersAction[argparse.ArgumentParser]) -> None:
    """Register `coney-tools perf bench` and `perf table`."""
    group = groups.add_parser("perf", help="performance: benchmark runs and their tables (docs/guides/performance.md)")
    commands = group.add_subparsers(dest="command", required=True)
    bench = commands.add_parser(
        "bench",
        help="play a level once per fighter count with `coney --bench` and print a table of the reports",
    )
    bench.add_argument("--coney", type=Path, help="Coney's executable; default: build/PRESET/src/platform/coney")
    bench.add_argument("--preset", default="release", help="the build folder to take Coney from (default release)")
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
    bench.add_argument("--out", type=Path, required=True, help="the folder for the reports and logs (scratch)")
    table_parser = commands.add_parser("table", help="print the table of bench reports already written")
    table_parser.add_argument("reports", type=Path, nargs="+", help="the JSON reports, in row order")


def run(args: argparse.Namespace) -> int:
    """Dispatch `coney-tools perf ...`."""
    if args.command == "bench":
        return run_bench(args)
    return run_table(args.reports)
