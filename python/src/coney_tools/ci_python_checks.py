# SPDX-License-Identifier: GPL-3.0-or-later
"""The repository checks of CI's python job, which `task check` runs too (docs/guides/coney-tools-repo.md#task-check).

CI's `python` job (`.github/workflows/python.yml`) runs a list of `coney-tools` commands over the checkout: the
conventions and their generated blocks, the `@orig` tag and copy rules, and every generator's `--check`. `task check`
once ran none of them, so a unit passed its gate and failed CI (task 1618). `CHECKS` is that list in CI's own words and
the `docs-check` check runs each of them (`task_check_steps.docs_check_step`), with the base the gate measures from in
place of CI's `HEAD^1`. `test_ci_python_checks.py` holds the list to the workflow, so a step CI gains and the gate lacks
fails a test until it is added here, or named in `NOT_RUN_LOCALLY` with the reason it cannot run on a machine.
`progress check` is the one command of the job that is not here: `progress_cli.check_as_ci` runs it in-process.
"""

from __future__ import annotations

from collections.abc import Sequence

from coney_tools.progress_cli import CI_BASE

#: The words after `coney-tools` of each step, as CI spells them; `CI_BASE` (HEAD^1, the branch a pull request's merge
#: commit joins) is replaced by the gate's own base in `command`.
CHECKS: tuple[tuple[str, ...], ...] = (
    ("repo", "check-conventions"),
    ("repo", "render", "--check"),
    ("repo", "audit", "--files"),
    ("orig", "check-tags", "--base", CI_BASE),
    ("orig", "check-copy", "--base", CI_BASE),
    ("bindings", "render", "--check"),
    ("bindings", "sync-coney", "--check"),
    ("bindings", "render-cpp", "--check"),
    ("refs", "render", "--check"),
    ("missions", "render", "--check"),
    ("research", "render", "--check"),
    ("pcsx2", "recipes", "render", "--check"),
    ("console", "render-cpp", "--check"),
    ("console", "render", "--check"),
)
#: A step of CI's python job that cannot run on a machine, by its words, with the reason (the guide repeats it). None
#: now: every check above reads only the checkout.
NOT_RUN_LOCALLY: dict[tuple[str, ...], str] = {}


def command(words: Sequence[str], base: str) -> list[str]:
    """The command that runs the check `words`: CI's `uv run --project python coney-tools ...`, its `HEAD^1` replaced
    by `base`, the branch the change is measured from."""
    return ["uv", "run", "--project", "python", "coney-tools", *(base if word == CI_BASE else word for word in words)]
