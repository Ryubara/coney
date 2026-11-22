# SPDX-License-Identifier: GPL-3.0-or-later
"""CI's python job and the local gate run the same repository checks (task 1618).

`task check` once skipped `orig check-tags`, `orig check-copy`, `repo audit --files`, `repo check-conventions`, `repo
render --check` and the generators' `--check`s, so a unit passed its gate and failed CI. The checks are one list
(`ci_python_checks.CHECKS`) that `docs-check` runs, and these tests hold that list to `.github/workflows/python.yml`:
a step CI gains and the gate lacks fails here, until it is added to the list or named, with its reason, in
`ci_python_checks.NOT_RUN_LOCALLY`.
"""

from __future__ import annotations

import shlex

import pytest
import yaml
from conventions_support import REPO
from test_task_check_steps import Commands, NoPages, context

from coney_tools import check_cli, ci_python_checks, docs_check, progress_cli, task_check_steps
from coney_tools.conventions import load_conventions

WORKFLOW = REPO / ".github" / "workflows" / "python.yml"
#: What every `run:` of the job that is not a `coney-tools` command starts with.
COMMAND = "uv run --project python "
CONEY_TOOLS = COMMAND + "coney-tools "


def python_job_steps() -> list[str]:
    """The `run:` text of every step of CI's `python` job, in order."""
    jobs = yaml.safe_load(WORKFLOW.read_text(encoding="utf-8"))["jobs"]
    return [str(step["run"]).strip() for step in jobs["python"]["steps"] if "run" in step]


def coney_tools_words(step: str) -> tuple[str, ...] | None:
    """The words after `coney-tools` of a step that runs it, else None."""
    return tuple(shlex.split(step[len(CONEY_TOOLS) :])) if step.startswith(CONEY_TOOLS) else None


@pytest.fixture(autouse=True)
def unbuilt_environment(monkeypatch: pytest.MonkeyPatch) -> None:
    """The commands are CI's words, whatever environment this checkout has (`--no-sync` is for a built one)."""
    monkeypatch.setenv("UV_PROJECT_ENVIRONMENT", ".venv-never-built")


@pytest.fixture
def pages_pass(monkeypatch: pytest.MonkeyPatch) -> None:
    """`docs check` finds nothing and the progress check agrees, so only the repository checks decide `docs-check`."""
    monkeypatch.setattr(docs_check, "context_for", lambda root: object())
    monkeypatch.setattr(docs_check, "check_changed", lambda root, base, ctx: NoPages())
    monkeypatch.setattr(progress_cli, "check_as_ci", lambda root, base: (0, []))


@pytest.mark.reads("docs_only")
def test_every_coney_tools_step_of_the_ci_python_job_is_run_by_the_gate() -> None:
    steps = [words for step in python_job_steps() if (words := coney_tools_words(step)) is not None]
    assert steps, "python.yml names no coney-tools step: the test reads the wrong job"
    ours = {tuple(words) for words in ci_python_checks.CHECKS}
    ours.add(tuple(progress_cli.ci_command().split()))  # run in-process by `docs-check` (progress_cli.check_as_ci)
    ours.update(ci_python_checks.NOT_RUN_LOCALLY)
    ci_run = [words for words in steps if words[:1] == ("ci",)]
    missing = [" ".join(words) for words in steps if words not in ours and words not in ci_run]
    assert not missing, (
        f"CI's python job runs {missing}, which task check does not: add it to ci_python_checks.CHECKS (with CI's own "
        "words) or to NOT_RUN_LOCALLY with the reason it cannot run on a machine"
    )


@pytest.mark.reads("docs_only")
def test_the_gate_runs_no_check_the_ci_python_job_does_not() -> None:
    steps = {words for step in python_job_steps() if (words := coney_tools_words(step)) is not None}
    for words in ci_python_checks.CHECKS:
        assert tuple(words) in steps, f"`{' '.join(words)}` is no step of {WORKFLOW.name}: CI's words changed"
    for words in ci_python_checks.NOT_RUN_LOCALLY:
        assert tuple(words) in steps, f"`{' '.join(words)}` is no step of {WORKFLOW.name}: drop it from NOT_RUN_LOCALLY"
    assert all(reason.strip() for reason in ci_python_checks.NOT_RUN_LOCALLY.values())


@pytest.mark.reads("docs_only")
def test_ci_run_in_the_job_stands_for_checks_the_gate_has() -> None:
    # `ci run --only repo --only pre-commit`: its `repo` step is `repo audit --files` and the gate has pre-commit.
    ci_run = [words for step in python_job_steps() if (words := coney_tools_words(step)) and words[:2] == ("ci", "run")]
    assert len(ci_run) == 1
    only = [ci_run[0][i + 1] for i, word in enumerate(ci_run[0]) if word == "--only"]
    assert sorted(only) == ["pre-commit", "repo"], "CI's `ci run` step runs a step the gate does not stand in for"
    assert ("repo", "audit", "--files") in ci_python_checks.CHECKS
    assert "pre-commit" in load_conventions(REPO).proof.profiles[0].checks


@pytest.mark.reads("docs_only")
def test_the_other_steps_of_the_job_are_the_ones_the_gate_has_its_own_check_for() -> None:
    known = {
        "uv sync --project python",
        *(" ".join(command) for command in check_cli.RUFF_COMMANDS),
        " ".join(check_cli.MYPY_COMMAND),
        "uv run --project python pytest python/tests",
    }
    others = [step for step in python_job_steps() if coney_tools_words(step) is None]
    unknown = [step for step in others if step not in known]
    assert not unknown, f"CI's python job runs {unknown}: add a check to task check (proof.profiles) or name it here"


def test_the_checks_are_ci_words_with_the_base_the_gate_measures_from() -> None:
    commands = [ci_python_checks.command(words, "origin/main") for words in ci_python_checks.CHECKS]
    assert [
        "uv",
        "run",
        "--project",
        "python",
        "coney-tools",
        "orig",
        "check-tags",
        "--base",
        "origin/main",
    ] in commands
    assert ["uv", "run", "--project", "python", "coney-tools", "repo", "audit", "--files"] in commands
    assert not [word for command in commands for word in command if word == progress_cli.CI_BASE]


@pytest.mark.usefixtures("pages_pass")
def test_docs_check_runs_every_repository_check_of_ci() -> None:
    runner = Commands()
    result = task_check_steps.docs_check_step(context(runner))
    ran = [command[5:] for command in runner.commands]
    assert ran == [list(ci_python_checks.command(words, "origin/main")[5:]) for words in ci_python_checks.CHECKS]
    assert result.ok and f"{len(ci_python_checks.CHECKS)} repository checks pass" in result.detail


@pytest.mark.usefixtures("pages_pass")
def test_a_failing_repository_check_fails_docs_check_and_the_rest_still_run() -> None:
    runner = Commands()
    runner.codes["check-copy"] = (1, "orig check-copy: 1 decompiler fingerprint(s)\nsrc/core/x.cpp:3: goto\n")
    result = task_check_steps.docs_check_step(context(runner))
    assert not result.ok and "1 repository check fails" in result.detail
    assert len(runner.commands) == len(ci_python_checks.CHECKS)  # one run shows every problem
    text = "\n".join(result.output)
    assert "orig check-copy --base origin/main: exit 1" in text and "src/core/x.cpp:3: goto" in text
    assert "repo check-conventions" not in text


@pytest.mark.reads("docs_only")
def test_the_guide_names_each_repository_check_the_gate_runs() -> None:
    guide = (REPO / "docs" / "guides" / "coney-tools-repo.md").read_text(encoding="utf-8")
    missing = [" ".join(words) for words in ci_python_checks.CHECKS if f"`{' '.join(words)}`" not in guide]
    assert not missing, f"docs/guides/coney-tools-repo.md#task-check-repository-checks does not name {missing}"
    for words, reason in ci_python_checks.NOT_RUN_LOCALLY.items():
        assert reason in guide, f"the guide must say why `{' '.join(words)}` cannot run locally: {reason}"
