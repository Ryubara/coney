# SPDX-License-Identifier: GPL-3.0-or-later
"""`/progress <stage>` after a stage stamp, so a claim whose work goes on is never released for idleness.

The claim's idle clock (docs/guides/issues.md#claiming) restarts on every `/progress`. `task create` posts `assigned`,
`task start` `claimed` and `pr open` `pr-opened`; the stages between them are stamps (`studied`, `fails-first`,
`compared`, `checked`, `reviewed`), written by five commands. Rather than five copies of the same call, the stamp log
tells its observers what it wrote (`stamps.observe`), and the command line registers `observer()` here: after a
passing stamp (or a `fails-first` of `none`) in a log whose folder holds a unit record (`tasks.start.unit_file`), it
posts `/progress <step>` on each issue of the unit as the signed-in account, the way `task start` does.

A new stage is posted at once; the same stage again only after `claims.progress_refresh_hours`, remembered per issue in
`claim-progress.json` beside the record (an issue whose post failed is tried again at the next stamp). A post that
takes longer than `claims.progress_post_seconds`, or fails, is a warning on stderr and never touches the stamp. Under
pytest the real `gh` is never used: a test passes its own `post`.
"""

from __future__ import annotations

import json
import os
import sys
from collections.abc import Callable, Sequence
from datetime import UTC, datetime, timedelta
from pathlib import Path

from coney_tools.audit_github import run_gh
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import Conventions, load_conventions
from coney_tools.fails_first import DECISION
from coney_tools.issue_claim_model import iso_time, parse_time
from coney_tools.stamps import Stamp
from coney_tools.unit_record import read_record

#: The stamp steps that post `/progress`; the others are posted by their own commands.
STAGE_STEPS = ("studied", "fails-first", "compared", "checked", "reviewed")
#: The results that count as the stage being reached (`none`: a fails-first that has no test, for docs and tooling).
REACHED = ("pass", "none")
#: Beside the unit record: the last stage posted and when.
STATE_FILE = "claim-progress.json"

Post = Callable[[int, str], object]


def _gh_post(conventions: Conventions) -> Post:
    """Post through `gh` as the signed-in account; a post that takes longer than `claims.progress_post_seconds` fails
    (a warning), so a slow network cannot hang a stamp command."""
    from coney_tools.issue_claim_remote import GhRemote

    rules = conventions.claims

    def run(arguments: list[str]) -> str:
        return run_gh(arguments, timeout=rules.progress_post_seconds)

    return GhRemote(rules.bot_login, rules.card_marker, rules.reply_marker, run).post_command


def stage_reached(written: Sequence[Stamp]) -> str | None:
    """The last stage among the stamps just written that posts `/progress`, or None. A coordinator's recorded decision
    on a changed test (`orig fails-first --accept-change`) is a `fails-first` stamp of result `none` written before any
    test has run, so it is no stage reached."""
    reached = [
        stamp.step
        for stamp in written
        if stamp.step in STAGE_STEPS and stamp.result in REACHED and stamp.detail.get("decision") != DECISION
    ]
    return reached[-1] if reached else None


def post_progress(
    folder: Path,
    written: Sequence[Stamp],
    conventions: Conventions,
    post: Post,
    now: datetime,
    warn: Callable[[str], None],
) -> list[int]:
    """Post `/progress <stage>` for the stamps just written to the log in `folder`; the issues it posted on.

    Nothing when no stage was reached, when the log's task has no unit record (not started with `task start`), or
    when the same stage was posted less than `claims.progress_refresh_hours` ago.
    """
    step = stage_reached(written)
    if step is None or step not in conventions.claims.stages:
        return []
    unit_dir = folder.parent
    record_path = unit_dir / conventions.tasks.start.unit_file
    if not record_path.is_file():
        return []
    try:
        record = read_record(record_path)
    except ConfigError as error:
        warn(f"warning: /progress {step} not posted: {error}")
        return []
    state_path = unit_dir / STATE_FILE
    state = _read_state(state_path)
    refresh = timedelta(hours=conventions.claims.progress_refresh_hours)
    posted: list[int] = []
    for number in record.issues:
        # Each issue keeps its own mark, so one whose post failed is tried again at the next stamp.
        last = state.get(number)
        if last is not None and last[0] == step and now - last[1] < refresh:
            continue
        try:
            post(number, f"/progress {step}")
        except (ConfigError, OSError) as error:
            warn(f"warning: /progress {step} was not posted on #{number} ({error}); the next stamp tries again")
            continue
        posted.append(number)
        state[number] = (step, now)
    if posted:
        _write_state(state_path, state)
    return posted


def _read_state(path: Path) -> dict[int, tuple[str, datetime]]:
    """The last stage posted on each issue and when, from `claim-progress.json`; empty when it is missing or damaged."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        issues = data["issues"]
        return {int(number): (str(item["step"]), parse_time(str(item["at"]))) for number, item in issues.items()}
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        return {}


def _write_state(path: Path, state: dict[int, tuple[str, datetime]]) -> None:
    """Save the last stage posted on each issue."""
    issues = {str(number): {"step": step, "at": iso_time(at)} for number, (step, at) in sorted(state.items())}
    path.write_text(json.dumps({"issues": issues}, indent=1) + "\n", encoding="utf-8")


def observer(post: Post | None = None) -> Callable[[Path, list[Stamp]], None]:
    """The stamp log's observer the command line registers: reads the conventions of the checkout it runs in."""

    def notice(folder: Path, written: list[Stamp]) -> None:
        # Under pytest nothing may reach GitHub: a test that runs a stage command through `main` posts nothing.
        if stage_reached(written) is None or (post is None and "PYTEST_CURRENT_TEST" in os.environ):
            return
        try:
            conventions = load_conventions(find_repo_root(Path.cwd()))
            poster = post or _gh_post(conventions)
        except ConfigError as error:
            print(f"warning: /progress not posted ({error})", file=sys.stderr)
            return
        post_progress(
            folder, written, conventions, poster, datetime.now(UTC), lambda line: print(line, file=sys.stderr)
        )

    return notice
