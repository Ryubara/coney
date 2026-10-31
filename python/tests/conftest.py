# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared pytest fixtures and hooks: every test starts in the same environment, whatever shell runs the suite, and every
test that reads the checkout's docs or engine files says so (repo_reads.py), which `--reads` selects by."""

from __future__ import annotations

import os
from collections.abc import Generator, Iterator
from pathlib import Path

import pytest
import repo_reads
from ambient_env import CLEARED

from coney_tools import identity


@pytest.fixture(autouse=True)
def clean_environment(monkeypatch: pytest.MonkeyPatch) -> Iterator[None]:
    """Remove the variables the tools read (`ambient_env.CLEARED`), so a test sees the same thing in a task's shell,
    an agent tool, CI and a clean terminal. A test that needs one sets it with `monkeypatch.setenv`, which this
    fixture's `monkeypatch` is shared with, so the value is put back afterwards either way. The cached identity is
    forgotten on both sides: it is read from `CONEY_AGENT_TOKEN` once per process."""
    for name in CLEARED:
        monkeypatch.delenv(name, raising=False)
    identity.reset()
    yield
    identity.reset()


# --- the reads of each test (repo_reads.py) --------------------------------------------------------------------------

_SCOPE = repo_reads.load_scope()
_RECORDER = repo_reads.Recorder(_SCOPE)
repo_reads.install(_RECORDER)
_LOG_FOLDER = os.environ.get("CONEY_TEST_READS_LOG")


def pytest_addoption(parser: pytest.Parser) -> None:
    """`--reads CLASS`: run only the tests marked as reading one of the classes given (task check's scoped run)."""
    parser.addoption(
        "--reads",
        action="append",
        default=[],
        choices=_SCOPE.scoped,
        metavar="CLASS",
        help="run only the tests marked @pytest.mark.reads(CLASS); repeat for several (proof.pytest.scoped)",
    )


def pytest_configure(config: pytest.Config) -> None:
    """Register the `reads` marker with the classes it may name."""
    config.addinivalue_line(
        "markers",
        f"{repo_reads.MARKER}(*classes): the test reads the checkout's files of these classes "
        f"({', '.join(_SCOPE.scoped)}); a scoped task check runs it for a change to them",
    )


def pytest_collection_modifyitems(config: pytest.Config, items: list[pytest.Item]) -> None:
    """With `--reads`, keep only the tests whose marker names one of the classes asked for; refuse a marker that names
    a class the conventions do not scope (a typo would quietly drop the test from every scoped run)."""
    for item in items:
        named = repo_reads.declared(marker.args for marker in item.iter_markers(repo_reads.MARKER))
        unknown = named - set(_SCOPE.scoped)
        if unknown:
            raise pytest.UsageError(
                f"{item.nodeid}: @pytest.mark.{repo_reads.MARKER} names {', '.join(sorted(unknown))}, which is not "
                f"in proof.pytest.scoped ({', '.join(_SCOPE.scoped)})"
            )
    wanted = set(config.getoption("--reads"))
    if not wanted:
        return
    kept: list[pytest.Item] = []
    dropped: list[pytest.Item] = []
    for item in items:
        named = repo_reads.declared(marker.args for marker in item.iter_markers(repo_reads.MARKER))
        (kept if named & wanted else dropped).append(item)
    if dropped:
        config.hook.pytest_deselected(items=dropped)
        items[:] = kept


@pytest.hookimpl(wrapper=True)
def pytest_make_collect_report(
    collector: pytest.Collector,
) -> Generator[None, pytest.CollectReport, pytest.CollectReport]:
    """Watch a test module while it is imported: what it reads then counts for each of its tests."""
    if not isinstance(collector, pytest.Module):
        return (yield)
    _RECORDER.start()
    try:
        return (yield)
    finally:
        _RECORDER.by_module[str(collector.path)] = _RECORDER.stop()


@pytest.hookimpl(wrapper=True)
def pytest_runtest_setup(item: pytest.Item) -> Generator[None, None, None]:
    """Start watching a test's reads, from its setup on."""
    _RECORDER.start(str(item.path))
    return (yield)


@pytest.hookimpl(wrapper=True)
def pytest_runtest_teardown(item: pytest.Item, nextitem: pytest.Item | None) -> Generator[None, None, None]:
    """After a test's teardown, fail it when it read a scoped class its `reads` marker does not name."""
    try:
        result = yield
    finally:
        reads = _RECORDER.stop()
    if _LOG_FOLDER:
        log = Path(_LOG_FOLDER) / f"reads-{os.getpid()}.jsonl"
        with log.open("a", encoding="utf-8") as out:
            out.write(repo_reads.log_line(item.nodeid, reads) + "\n")
    named = repo_reads.declared(marker.args for marker in item.iter_markers(repo_reads.MARKER))
    unnamed = repo_reads.missing(reads, named)
    if unnamed:
        pytest.fail(repo_reads.failure(item.nodeid, unnamed), pytrace=False)
    return result
