# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `orig fails-first --detach` and `--result` (fails_first_detach.py): both builds and test runs happen in a
process of their own with no time budget, the stamps are written when it ends, and the outcome is read later (issue
1384). No process is started: `build_cli.spawn_detached` and the build are stand-ins, so what the tests check is what
the detached child is asked to do, what it records and who its stamps name."""

import json
import os
import subprocess
from contextlib import nullcontext
from pathlib import Path
from typing import Any

import pytest
from conventions_support import write_checkout
from git_support import git
from test_fails_first import ONE, build_output, write_log

from coney_tools import build_claims, build_cli, build_run, fails_first, identity, process_lifetime, stamps
from coney_tools.cli import _build_parser, main
from coney_tools.conventions import load_conventions
from coney_tools.fails_first import BuildOutcome
from coney_tools.unit import Unit

NAME = "Widget ready frame"
TEST_ARGS = ["orig", "fails-first", "--test", NAME, "--agent", "alice"]


class Checkout:
    """A checkout on `main` with a base commit, a test commit and a fix commit, and a fake build that fails the test on
    the test commit and passes it on the fix commit."""

    def __init__(self, tmp_path: Path) -> None:
        self.root = tmp_path / "repos" / "coney"
        (self.root / "src").mkdir(parents=True)
        (self.root / "tests").mkdir()
        write_checkout(self.root)
        git(self.root, "init", "-q", "-b", "main")
        (self.root / "src" / "widget.cpp").write_text("int ready() { return 0; }\n", encoding="utf-8")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-q", "-m", "base")
        (self.root / "tests" / "widget_test.cpp").write_text(f'TEST_CASE("{NAME}") {{}}\n', encoding="utf-8")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-q", "-m", "test")
        self.test = git(self.root, "rev-parse", "HEAD").strip()
        (self.root / "src" / "widget.cpp").write_text("int ready() { return 7; }\n", encoding="utf-8")
        git(self.root, "add", "-A")
        git(self.root, "commit", "-q", "-m", "fix")
        self.fix = git(self.root, "rev-parse", "HEAD").strip()
        self.logs = tmp_path / "logs"
        #: What each run of the fake build was asked: (the commit checked out, the names).
        self.runs: list[tuple[str, tuple[str, ...]]] = []
        #: Whether each build runner the command asked for was the unattended one (no time budget).
        self.unattended: list[bool] = []
        self.red_fails = True

    @property
    def unit(self) -> Unit:
        """The unit of the task `main`."""
        return Unit.of(self.root, "main")

    @property
    def result_file(self) -> Path:
        """Where a detached run records its outcome."""
        return self.unit.dir / "fails-first-result.json"

    def runner(self, root: Path, preset: str, names: tuple[str, ...]) -> BuildOutcome:
        """The fake build: red on the test commit (unless told otherwise), green on any other."""
        head = git(root, "rev-parse", "HEAD").strip()
        self.runs.append((head, names))
        failed = ONE if head == self.test and self.red_fails else ()
        text = build_output(write_log(self.logs, ONE, failed), ONE, failed)
        return fails_first.parse_build_output(text, 1 if failed else 0)

    def arguments(self, *more: str) -> list[str]:
        """The command line of a fails-first run for the one test."""
        return [*TEST_ARGS, "--test-commit", self.test, "--fix-commit", self.fix, *more]

    def log(self) -> list[stamps.Stamp]:
        """The stamps written so far."""
        return stamps.verify(self.unit.stamps, load_conventions(self.root).stamps).stamps


@pytest.fixture
def checkout(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Checkout:
    """A checkout the cwd is set to, with the fake build in place of `coney-tools build` and no job objects."""
    made = Checkout(tmp_path)
    monkeypatch.chdir(made.root)

    def build_runner(agent: str, unattended: bool = False) -> fails_first.Runner:
        made.unattended.append(unattended)
        return made.runner

    monkeypatch.setattr(fails_first, "build_runner", build_runner)
    monkeypatch.setattr(build_claims, "unit_activity", lambda *args: nullcontext())
    monkeypatch.setattr(process_lifetime, "bind_children", lambda: None)
    monkeypatch.setattr(process_lifetime, "stop_on_signals", lambda: None)
    identity.reset()
    return made


@pytest.fixture
def spawned(monkeypatch: pytest.MonkeyPatch) -> list[tuple[list[str], Path]]:
    """The commands `spawn_detached` was asked to start, with their working folders (none is started)."""
    commands: list[tuple[list[str], Path]] = []

    def spawn(command: list[str], cwd: Path, output: Path) -> int:
        commands.append((command, cwd))
        return 4242

    monkeypatch.setattr(build_cli, "spawn_detached", spawn)
    return commands


def record(checkout: Checkout) -> dict[str, Any]:
    data: dict[str, Any] = json.loads(checkout.result_file.read_text(encoding="utf-8"))
    return data


def branch(checkout: Checkout) -> str:
    """The branch the worktree has checked out ("HEAD" when it is detached)."""
    return git(checkout.root, "rev-parse", "--abbrev-ref", "HEAD").strip()


# --- the parent: --detach checks, then starts the child and returns -------------------------------------------------


def test_detach_starts_a_child_with_the_same_run_and_returns_at_once(
    checkout: Checkout, spawned: list[tuple[list[str], Path]], capsys: pytest.CaptureFixture[str]
) -> None:
    assert main(checkout.arguments("--preset", "release", "--detach")) == 0
    [(command, cwd)] = spawned
    assert command[1:4] == ["-m", "coney_tools.cli", "orig"] and command[4] == "fails-first"
    assert command[command.index("--test") + 1] == NAME
    assert command[command.index("--test-commit") + 1] == checkout.test
    assert command[command.index("--fix-commit") + 1] == checkout.fix
    assert command[command.index("--preset") + 1] == "release"
    assert command[command.index("--agent") + 1] == "alice"
    # The child is told the task: the worktree is detached at a commit for most of its life, and the branch names it.
    assert command[command.index("--task") + 1] == "main"
    assert command[-2] == "--result-file" and command[-1] == str(checkout.result_file)
    assert "--detach" not in command and "--keeps" not in command
    assert cwd == checkout.root
    assert record(checkout)["state"] == "running"
    assert checkout.runs == [] and checkout.log() == []  # nothing ran or was stamped yet
    assert "detached: pid 4242" in capsys.readouterr().out


def test_detach_carries_keeps_to_the_child(checkout: Checkout, spawned: list[tuple[list[str], Path]]) -> None:
    assert main(checkout.arguments("--keeps", "--detach")) == 0
    assert "--keeps" in spawned[0][0]


def test_detach_refuses_what_a_run_would_refuse_before_it_starts_anything(
    checkout: Checkout, spawned: list[tuple[list[str], Path]], capsys: pytest.CaptureFixture[str]
) -> None:
    (checkout.root / "src" / "widget.cpp").write_text("changed\n", encoding="utf-8")
    assert main(checkout.arguments("--detach")) == 2
    assert "uncommitted changes" in capsys.readouterr().err
    assert spawned == [] and not checkout.result_file.exists()


def test_detach_needs_an_agent_id_for_the_build_slots(
    checkout: Checkout, spawned: list[tuple[list[str], Path]], capsys: pytest.CaptureFixture[str]
) -> None:
    arguments = ["orig", "fails-first", "--test", NAME, "--test-commit", checkout.test, "--detach"]
    assert main(arguments) == 2
    assert "--agent" in capsys.readouterr().out and spawned == []


def test_a_second_detach_is_refused_while_the_first_runs(
    checkout: Checkout, spawned: list[tuple[list[str], Path]], capsys: pytest.CaptureFixture[str]
) -> None:
    assert main(checkout.arguments("--detach")) == 0
    capsys.readouterr()
    # The first run is under way: the worktree is detached at a commit and its marker is there, which a plain
    # preflight would call "an earlier run left it detached"; the refusal names the run that is still going.
    assert main(checkout.arguments("--detach")) == 2
    assert "is still running" in capsys.readouterr().err and len(spawned) == 1


@pytest.mark.parametrize(
    "more",
    [
        ["--none", "docs only"],
        ["--accept-change", "why"],
        ["--result"],
    ],
)
def test_detach_goes_with_a_real_run_only(
    checkout: Checkout, more: list[str], capsys: pytest.CaptureFixture[str]
) -> None:
    assert main([*checkout.arguments("--detach"), *more]) == 2
    assert "--detach" in capsys.readouterr().out


def test_result_goes_alone(checkout: Checkout, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["orig", "fails-first", "--result", "--test", NAME]) == 2
    assert "--result" in capsys.readouterr().out


def test_the_parser_takes_detach_and_result_and_a_hidden_result_file() -> None:
    parsed = _build_parser().parse_args(["orig", "fails-first", "--detach"])
    assert parsed.detach and not parsed.result and parsed.result_file is None
    assert _build_parser().parse_args(["orig", "fails-first", "--result"]).result


# --- the child: the run itself, its outcome recorded however it ends ------------------------------------------------


def test_the_child_runs_both_commits_stamps_and_restores_the_worktree(checkout: Checkout) -> None:
    assert main(checkout.arguments("--result-file", str(checkout.result_file))) == 0
    assert [head for head, _ in checkout.runs] == [checkout.test, checkout.fix]
    [stamp] = checkout.log()
    assert (stamp.step, stamp.subject, stamp.result) == ("fails-first", NAME, "pass")
    assert branch(checkout) == "main"
    assert record(checkout)["state"] == "done" and record(checkout)["exit_code"] == 0


def test_the_childs_build_has_no_time_budget_and_a_plain_run_keeps_its_own(checkout: Checkout) -> None:
    assert main(checkout.arguments("--result-file", str(checkout.result_file))) == 0
    assert main(checkout.arguments()) == 0
    assert checkout.unattended == [True, False]


def test_a_test_that_does_not_fail_first_is_recorded_as_exit_1_with_its_fail_stamp(checkout: Checkout) -> None:
    checkout.red_fails = False
    assert main(checkout.arguments("--result-file", str(checkout.result_file))) == 1
    assert record(checkout)["exit_code"] == 1
    assert [stamp.result for stamp in checkout.log()] == ["fail"]


def test_a_refused_child_records_exit_2_and_stamps_nothing(checkout: Checkout) -> None:
    def broken(root: Path, preset: str, names: tuple[str, ...]) -> BuildOutcome:
        return fails_first.parse_build_output("build: FAILED\nresult: FAILED at build\n", 1)

    checkout.runner = broken  # type: ignore[method-assign]
    assert main(checkout.arguments("--result-file", str(checkout.result_file))) == 2
    assert record(checkout)["exit_code"] == 2 and checkout.log() == []
    assert branch(checkout) == "main"


# --- reading the outcome --------------------------------------------------------------------------------------------


def test_result_says_none_was_recorded(checkout: Checkout, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["orig", "fails-first", "--result"]) == 1
    assert "no detached orig fails-first recorded" in capsys.readouterr().out


def test_result_shows_the_output_and_the_exit_code_of_the_run(
    checkout: Checkout, capsys: pytest.CaptureFixture[str]
) -> None:
    assert main(checkout.arguments("--result-file", str(checkout.result_file))) == 0
    (checkout.unit.dir / "fails-first-detached.out").write_text("orig fails-first: x: pass\n", encoding="utf-8")
    capsys.readouterr()
    assert main(["orig", "fails-first", "--result"]) == 0
    out = capsys.readouterr().out
    assert "orig fails-first: x: pass" in out and out.rstrip().endswith("exit 0")


def test_result_of_a_failed_run_exits_with_its_code(checkout: Checkout) -> None:
    checkout.red_fails = False
    assert main(checkout.arguments("--result-file", str(checkout.result_file))) == 1
    assert main(["orig", "fails-first", "--result"]) == 1


def test_result_of_a_run_that_is_going_exits_75(
    checkout: Checkout, spawned: list[tuple[list[str], Path]], capsys: pytest.CaptureFixture[str]
) -> None:
    assert main(checkout.arguments("--detach")) == 0
    capsys.readouterr()
    # The child has started: its pid is alive and is the one the record says.
    build_cli._write_record(
        checkout.result_file,
        {
            "state": "running",
            "agent": "alice",
            "pid": os.getpid(),
            "process_started": process_lifetime.process_started(os.getpid()),
            "started": 1.0,
        },
    )
    assert main(["orig", "fails-first", "--result"]) == 75
    assert "still running" in capsys.readouterr().out


def test_result_of_a_killed_run_says_it_ended_without_one(
    checkout: Checkout, capsys: pytest.CaptureFixture[str]
) -> None:
    gone = {"state": "running", "agent": "alice", "pid": 2**22 + 7, "process_started": 1.0, "started": 1.0}
    build_cli._write_record(checkout.result_file, gone)
    assert main(["orig", "fails-first", "--result"]) == 1
    assert "ended without a result" in capsys.readouterr().out


# --- the identity: the stamps of a detached run name the agent that started it --------------------------------------


def test_the_detached_run_stamps_as_the_verified_agent_that_started_it(
    checkout: Checkout, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    # The hook removes the caller's identity file when `--detach` returns, before the child reads it (issue 1370: a
    # detached `task check` stamped `unverified`, agent `unknown`). So the parent hands its identity on in a file of
    # its own, and the child, started with only that token, stamps as the same agent.
    folder = tmp_path / "identity"
    monkeypatch.setattr(identity, "default_folder", lambda: folder)
    caller = identity.Identity(id="agent-7", type="tooling", model="claude-sonnet-5-5", verified=True, session="s1")
    envs: list[dict[str, str]] = []
    start = subprocess.Popen

    class Started:
        pid = 4242

    def popen(command: list[str], **kwargs: Any) -> Any:
        # Only the child is faked; the git calls of the command go on as they are.
        if "coney_tools.cli" not in command:
            return start(command, **kwargs)
        envs.append(kwargs["env"])
        return Started()

    with monkeypatch.context() as caller_side:
        caller_side.setattr(identity, "current", lambda: caller)
        caller_side.setattr(subprocess, "Popen", popen)
        assert main(checkout.arguments("--detach")) == 0
    # The child's environment holds a token of its own, which names the caller (its own token's file is long gone).
    assert identity.load(envs[0], folder=folder) == caller
    # Now the child process: the same checkout, started with only that environment.
    monkeypatch.setenv(identity.TOKEN_VARIABLE, envs[0][identity.TOKEN_VARIABLE])
    identity.reset()
    assert main(checkout.arguments("--result-file", str(checkout.result_file))) == 0
    [stamp] = checkout.log()
    assert stamp.who["source"] == "hook"
    assert stamp.who["agent"] == caller.agent()


def test_an_unverified_caller_hands_on_nothing_so_the_stamps_stay_unverified(
    checkout: Checkout, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    envs: list[dict[str, str]] = []
    start = subprocess.Popen

    class Started:
        pid = 1

    def popen(command: list[str], **kwargs: Any) -> Any:
        if "coney_tools.cli" not in command:
            return start(command, **kwargs)
        envs.append(kwargs["env"])
        return Started()

    monkeypatch.setattr(identity, "default_folder", lambda: tmp_path / "identity")
    monkeypatch.setattr(subprocess, "Popen", popen)
    assert main(checkout.arguments("--detach")) == 0
    assert identity.TOKEN_VARIABLE not in envs[0]


# --- the build and the queued advice --------------------------------------------------------------------------------


def test_the_unattended_build_runner_asks_for_no_budget_and_the_plain_one_does_not(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    commands: list[list[str]] = []

    def stream(command: list[str], cwd: Path, on_line: Any) -> tuple[int, str]:
        commands.append(list(command))
        return 0, "result: ok\n"

    monkeypatch.setattr(fails_first, "stream_lines", stream)
    fails_first.build_runner("alice", unattended=True)(tmp_path, "dev", ("a",))
    fails_first.build_runner("alice")(tmp_path, "dev", ("a",))
    assert "--unattended" in commands[0] and "--unattended" not in commands[1]
    assert "--detach" not in commands[0]  # the child is detached already: a nested --detach would hand the run on again


def test_a_build_that_cannot_fit_the_budget_says_to_use_detach(checkout: Checkout) -> None:
    refused = (
        "time budget: 0 min of the call so far; build would take about 46 min (median of 5 runs) and the budget is "
        "45 min\nresult: QUEUED at build (exit 75); do other work and run the command again\n"
    )

    def queued(root: Path, preset: str, names: tuple[str, ...]) -> BuildOutcome:
        return fails_first.parse_build_output(refused, 75)

    with pytest.raises(fails_first.FailsFirstError, match=r"--detach") as error:
        fails_first.run(
            checkout.unit,
            checkout.root,
            tests=(NAME,),
            test_commit=checkout.test,
            fix_commit=checkout.fix,
            preset="dev",
            runner=queued,
            rules=load_conventions(checkout.root).stamps,
            who={"kind": "tool", "tool": "test"},
        )
    assert "orig fails-first" in str(error.value) and branch(checkout) == "main"


def test_a_detached_run_does_not_advise_detaching_again(checkout: Checkout) -> None:
    def queued(root: Path, preset: str, names: tuple[str, ...]) -> BuildOutcome:
        return fails_first.parse_build_output("result: QUEUED at build (exit 75)\n", 75)

    with pytest.raises(fails_first.FailsFirstError) as error:
        fails_first.run(
            checkout.unit,
            checkout.root,
            tests=(NAME,),
            test_commit=checkout.test,
            fix_commit=checkout.fix,
            preset="dev",
            runner=queued,
            rules=load_conventions(checkout.root).stamps,
            who={"kind": "tool", "tool": "test"},
            detached=True,
        )
    assert "--detach" not in str(error.value)


def test_build_takes_unattended_as_the_limits_of_a_detached_run() -> None:
    # A call someone waits on may wait 40 minutes and take 45; the child of `orig fails-first --detach` is waited on by
    # no one, so it has what a `build --detach` child has: 240 minutes of waiting and no budget.
    parsed = _build_parser().parse_args(["build", "--agent", "a", "--unattended"])
    assert build_cli.limits(parsed) == (build_cli.DETACHED_MAX_WAIT, 0.0)
    plain = _build_parser().parse_args(["build", "--agent", "a"])
    assert build_cli.limits(plain) == (build_cli.MAX_WAIT_CAP, build_cli.BUDGET_CAP)
    assert build_run.EXIT_QUEUED == 75
