# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools trace ...` commands: run a scenario on Coney, and compare two traces.

`trace coney` plays a scenario's input script on Coney headless (`--play-level`, `--input-script`, `--trace`), so the
same file drives both games. `trace diff` compares the original's trace with Coney's (coney_tools.trace_diff) and
exits with 1 when a column is outside its tolerance, so a parity check can gate a change
(docs/guides/research-workflow.md#comparing-with-coney).
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from coney_tools.config import ConfigError, find_repo_root, load_config
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
        candidate = root / "build" / "dev" / "src" / "platform" / name
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
    images = sorted(game.glob("*.iso")) if game.is_dir() else [game]
    return str(images[0] if len(images) == 1 else game)


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
