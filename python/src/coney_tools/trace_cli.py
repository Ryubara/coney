# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools trace ...` commands: run a scenario on Coney, and compare traces.

`trace coney` plays a scenario's input script on Coney headless (`--play-level`, `--input-script`, `--trace`), so the
same file drives both games. `trace diff` compares the original's trace with Coney's (coney_tools.trace_diff) and
exits with 1 when a column is outside its tolerance, so a parity check can gate a change
(docs/guides/research-traces.md#comparing-with-coney). `trace fight-report` computes the behaviour statistics of
an all-humans trace and compares the original's runs with Coney's (coney_tools.fight_report).
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from coney_tools import build_root, fight_report
from coney_tools.config import ConfigError, disc_for_coney, find_repo_root, load_config
from coney_tools.scenario import Scenario, ScenarioError, load_scenario
from coney_tools.trace_diff import TraceError, compare, load_trace, report, to_start_frame
from coney_tools.wad import refuse_inside_repo


def _scenario(path: Path) -> Scenario:
    """Load a scenario, as a ConfigError when it is bad."""
    try:
        return load_scenario(path, find_repo_root(Path.cwd()))
    except ScenarioError as error:
        raise ConfigError(str(error)) from error


def _parse_tolerances(items: list[str]) -> dict[str, float]:
    """`COLUMN=VALUE` pairs (`*=VALUE` for every column without its own)."""
    tolerances = {}
    for item in items:
        name, _, value = item.partition("=")
        try:
            tolerances[name.strip()] = float(value)
        except ValueError as error:
            raise ConfigError(f"--tolerance {item!r}: expected COLUMN=NUMBER") from error
    return tolerances


def run_diff(
    original_path: Path,
    coney_path: Path,
    scenario_path: Path | None,
    columns: list[str] | None,
    tolerance_items: list[str],
    options: tuple[int | None, int | None, int | None, bool, int],
) -> int:
    """`trace diff`: print the comparison; 0 when every column is within tolerance, 1 otherwise."""
    start, end, shift, start_frame, context = options
    tolerances: dict[str, float] = {}
    if scenario_path is not None:
        scenario = _scenario(scenario_path)
        settings = scenario.diff
        columns = columns or list(settings.columns) or None
        tolerances.update(settings.tolerance)
        start_frame = start_frame or settings.start_frame
        shift = settings.shift if shift is None else shift
        start = scenario.first_input_step() if start is None else start
    tolerances.update(_parse_tolerances(tolerance_items))
    start = 1 if start is None else start
    shift = 0 if shift is None else shift
    try:
        original, coney = load_trace(original_path), load_trace(coney_path)
        if start_frame:
            original, coney = to_start_frame(original, start), to_start_frame(coney, start + shift)
        comparison = compare(original, coney, columns, tolerances, start, end, shift, context)
    except TraceError as error:
        raise ConfigError(str(error)) from error
    print(report(comparison))
    return 0 if comparison.ok else 1


def _coney_executable(given: Path | None) -> Path:
    """Coney's program: the one given, else the dev build in the checkout."""
    if given is not None:
        return given
    root = find_repo_root(Path.cwd())
    for name in ("coney.exe", "coney"):
        candidate = build_root.preset_folder(root, "dev") / "src" / "platform" / name
        if candidate.is_file():
            return candidate
    raise ConfigError("no Coney build at build/dev/src/platform/; build it or pass --coney")


def _disc(given: str | None) -> str:
    """The disc for Coney: the one given, else game_dir from coney.local.toml."""
    if given is not None:
        return given
    game = load_config(find_repo_root(Path.cwd())).paths["game_dir"]
    if game is None:
        raise ConfigError("no disc: pass --disc or set game_dir in coney.local.toml")
    return str(disc_for_coney(game))


def shifted_script(text: str, by: int) -> str:
    """The input script `text` with every frame number raised by `by` (comments and blank lines are kept)."""
    lines = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            lines.append(line)
            continue
        frame, _, rest = stripped.partition(" ")
        lines.append(f"{int(frame) + by} {rest}")
    return "\n".join(lines) + "\n"


def trimmed_trace(text: str, settle: int) -> str:
    """The trace CSV `text` without its first `settle` steps, the rest numbered from 1 again."""
    header, *rows = text.splitlines()
    kept = []
    for row in rows:
        step, _, rest = row.partition(",")
        if int(step) > settle:
            kept.append(f"{int(step) - settle},{rest}")
    return "\n".join([header, *kept]) + "\n"


def coney_command(scenario: Scenario, executable: Path, disc: str, out: Path, script: Path | None = None) -> list[str]:
    """The command line that plays `scenario` on Coney headless and traces it to `out`.

    `script` is the input script to play instead of the scenario's (the settled one, when it has a `settle`), and the
    run lasts the scenario's settle longer than its updates.
    """
    if not scenario.coney_level:
        raise ConfigError(f"{scenario.path}: has no [coney] level")
    return [
        str(executable),
        "--disc",
        disc,
        "--play-level",
        scenario.coney_level,
        *scenario.coney_args,
        "--headless",
        "--frames",
        str(scenario.updates + scenario.coney_settle),
        "--input-script",
        str(script if script is not None else scenario.input_path),
        "--trace",
        str(out),
    ]


def run_coney(scenario_path: Path, out: Path, executable: Path | None, disc: str | None) -> int:
    """`trace coney`: run the scenario on Coney and write its trace; Coney's exit code when it fails."""
    refuse_inside_repo(out)
    scenario = _scenario(scenario_path)
    out.parent.mkdir(parents=True, exist_ok=True)
    settle = scenario.coney_settle
    script = None
    played = out
    if settle:
        # The script starts `settle` updates later, and the trace Coney writes keeps only what follows them.
        script = out.with_name(out.stem + ".input.txt")
        script.write_text(shifted_script(scenario.input_path.read_text(encoding="utf-8"), settle), encoding="utf-8")
        played = out.with_name(out.stem + ".full.csv")
    command = coney_command(scenario, _coney_executable(executable), _disc(disc), played, script)
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        print(result.stdout + result.stderr, file=sys.stderr)
        print(f"coney-tools: Coney exited with {result.returncode}", file=sys.stderr)
        return result.returncode
    if settle:
        out.write_text(trimmed_trace(played.read_text(encoding="utf-8"), settle), encoding="utf-8")
        played.unlink()
        if script is not None:
            script.unlink()
    print(f"{out}: {scenario.updates} steps of {scenario.coney_level}")
    return 0


def _gang_labels(order: list[int] | None, analysis: fight_report.Analysis) -> dict[int, str]:
    """Labels `g0`, `g1`, ... for the gangs of a run: in the order given, else by gang id."""
    gangs = order if order else fight_report.gangs_of(analysis)
    return {gang: f"g{rank}" for rank, gang in enumerate(gangs)}


def _fight_runs(
    paths: list[Path], order: list[int] | None, settings: fight_report.Settings
) -> list[dict[str, fight_report.Metric]]:
    """The metrics of each trace in `paths`, the sides being the gangs of `order` (labelled in that order)."""
    runs = []
    for path in paths:
        analysis = fight_report.analyse(fight_report.load_fight_trace(path), *settings, sides=order)
        runs.append(fight_report.scalars(analysis, _gang_labels(order, analysis)))
    return runs


def _name_goals() -> None:
    """Name the goals by the repository's reference list, when the command runs inside the repository."""
    try:
        goals = find_repo_root(Path.cwd()) / "research" / "references" / "goal-types.yaml"
    except ConfigError:
        return
    if goals.is_file():
        fight_report.load_goal_names(goals)


def run_fight_report(
    traces: list[Path],
    original: list[list[Path]],
    coney: list[list[Path]],
    options: tuple[fight_report.Settings, bool, float],
    gangs: tuple[list[int] | None, list[int] | None],
) -> int:
    """`trace fight-report`: the metrics of each trace, or (with `original` and `coney`) the two sets compared.

    `original` and `coney` hold one group of runs per orientation (which gang is side 1), paired in order: each
    orientation of Coney's is compared against the original's spread over its own orientation
    (docs/research/rumble.md#gang-battle-metrics). `options` is analyse()'s settings (updates a second, the humans to
    leave out, the engage distance, the first step), whether to print each human, and the comparison's margin.
    Returns 1 when a compared metric is outside the original's spread, else 0.
    """
    settings, per_human, margin = options
    try:
        _name_goals()
        if bool(original) != bool(coney):
            raise ConfigError("--original and --coney go together: compare needs both")
        if original:
            if traces:
                raise ConfigError("name either traces to report or --original and --coney to compare, not both")
            if len(original) != len(coney):
                raise ConfigError(
                    f"{len(original)} --original groups and {len(coney)} --coney groups: "
                    "give one of each per orientation"
                )
            flagged = False
            for index, (theirs, ours) in enumerate(zip(original, coney, strict=True), start=1):
                runs_original = _fight_runs(theirs, gangs[0], settings)
                runs_coney = _fight_runs(ours, gangs[1], settings)
                comparisons = fight_report.compare_runs(runs_original, runs_coney, margin)
                if len(original) > 1:
                    print(f"orientation {index}:")
                print(fight_report.report_compare(comparisons, (len(theirs), len(ours))))
                flagged = flagged or any(c.flag for c in comparisons)
            return 1 if flagged else 0
        if not traces:
            raise ConfigError("name a trace to report, or --original and --coney to compare")
        for path in traces:
            analysis = fight_report.analyse(fight_report.load_fight_trace(path), *settings, sides=gangs[0])
            print(fight_report.report_run(analysis, per_human, _gang_labels(gangs[0], analysis) if gangs[0] else None))
    except fight_report.FightTraceError as error:
        raise ConfigError(str(error)) from error
    return 0
